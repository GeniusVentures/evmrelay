// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/secp256k1_utility.hpp>
#include <base/parse_utility.hpp>
#include <secp256k1.h>
#include <array>
#include <cstring>

namespace {

eth::Secp256k1PrivateKey valid_private_key()
{
    eth::Secp256k1PrivateKey key{};
    key[31] = 1;
    return key;
}

eth::Hash256 message_hash(uint8_t seed)
{
    eth::Hash256 hash{};
    for (size_t i = 0; i < hash.size(); ++i)
    {
        hash[i] = static_cast<uint8_t>(seed + i);
    }
    return hash;
}

} // namespace

TEST(Secp256k1UtilityTest, SignsAndRecoversAddress)
{
    const auto key = valid_private_key();
    const auto expected_address = eth::secp256k1_address_from_private_key(key);
    const auto signature = eth::secp256k1_sign_recoverable(message_hash(0x10), key);

    ASSERT_TRUE(expected_address.has_value());
    ASSERT_TRUE(signature.has_value());
    EXPECT_EQ(signature->size(), 65U);

    const auto recovered = eth::secp256k1_recover_address(message_hash(0x10), *signature);
    ASSERT_TRUE(recovered.has_value());
    EXPECT_EQ(*recovered, *expected_address);
}

TEST(Secp256k1UtilityTest, RejectsInvalidPrivateKey)
{
    eth::Secp256k1PrivateKey key{};

    EXPECT_FALSE(eth::secp256k1_address_from_private_key(key).has_value());
    EXPECT_FALSE(eth::secp256k1_sign_recoverable(message_hash(0x10), key).has_value());
}

TEST(Secp256k1UtilityTest, RejectsShortSignature)
{
    eth::codec::ByteBuffer signature(64, 0);

    EXPECT_FALSE(eth::secp256k1_recover_address(message_hash(0x10), signature).has_value());
}

TEST(Secp256k1UtilityTest, RejectsBadRecoveryId)
{
    const auto signature = eth::secp256k1_sign_recoverable(message_hash(0x10), valid_private_key());
    ASSERT_TRUE(signature.has_value());

    auto bad_signature = *signature;
    ASSERT_EQ(bad_signature.size(), 65U);
    bad_signature.back() = 4;

    EXPECT_FALSE(eth::secp256k1_recover_address(message_hash(0x10), bad_signature).has_value());
}

TEST(Secp256k1UtilityTest, RejectsCorruptSignatureBytes)
{
    const auto signature = eth::secp256k1_sign_recoverable(message_hash(0x10), valid_private_key());
    ASSERT_TRUE(signature.has_value());

    auto bad_signature = *signature;
    bad_signature.front() ^= 0x01;

    const auto recovered = eth::secp256k1_recover_address(message_hash(0x10), bad_signature);
    EXPECT_TRUE(!recovered.has_value()
        || *recovered != *eth::secp256k1_address_from_private_key(valid_private_key()));
}

TEST(Secp256k1UtilityTest, DifferentMessageRecoversDifferentAddress)
{
    const auto expected_address = eth::secp256k1_address_from_private_key(valid_private_key());
    const auto signature = eth::secp256k1_sign_recoverable(message_hash(0x10), valid_private_key());
    ASSERT_TRUE(expected_address.has_value());
    ASSERT_TRUE(signature.has_value());

    const auto recovered = eth::secp256k1_recover_address(message_hash(0x20), *signature);
    EXPECT_TRUE(!recovered.has_value() || *recovered != *expected_address);
}

// ---------------------------------------------------------------------------
// Bridge V2: X-only decompression tests (DecompressXOnlyPubkey)
// ---------------------------------------------------------------------------

namespace {

constexpr size_t kXOnlyKeyBytes = 32;
constexpr size_t kCompressedKeyLen = 33;
constexpr size_t kUncompressedKeyLen = 65;

/// @brief Build a known secp256k1 public key (uncompressed + compressed) from a
///        private key using libsecp256k1 directly.  Establishes ground truth for
///        the decompression tests without hand-computed curve math.
struct KnownKeyMaterial
{
    std::array<uint8_t, kUncompressedKeyLen> uncompressed{}; ///< [04][X_big][Y_big]
    std::array<uint8_t, kCompressedKeyLen>   compressed{};   ///< [02/03][X_big]
    bool                                     y_odd = false;
};

/// @brief Copy a canonical big-endian coordinate into the ABI bytes32 value.
std::array<uint8_t, kXOnlyKeyBytes> to_contract_order(const uint8_t* big_endian)
{
    std::array<uint8_t, kXOnlyKeyBytes> out{};
    for (size_t i = 0; i < kXOnlyKeyBytes; ++i)
    {
        out[i] = big_endian[i];
    }
    return out;
}

/// @brief Produce a deterministic key material from @p seed_byte (placed at the
///        LSB of a 32-byte private key).  Returns nullopt if libsecp256k1 fails.
std::optional<KnownKeyMaterial> build_known_material(uint8_t seed_byte)
{
    eth::Secp256k1PrivateKey private_key{};
    private_key[31] = seed_byte;

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (ctx == nullptr)
    {
        return std::nullopt;
    }

    secp256k1_pubkey pubkey{};
    if (!secp256k1_ec_pubkey_create(ctx, &pubkey, private_key.data()))
    {
        secp256k1_context_destroy(ctx);
        return std::nullopt;
    }

    KnownKeyMaterial material{};
    size_t uncompressed_len = material.uncompressed.size();
    secp256k1_ec_pubkey_serialize(
        ctx, material.uncompressed.data(), &uncompressed_len,
        &pubkey, SECP256K1_EC_UNCOMPRESSED);

    size_t compressed_len = material.compressed.size();
    secp256k1_ec_pubkey_serialize(
        ctx, material.compressed.data(), &compressed_len,
        &pubkey, SECP256K1_EC_COMPRESSED);

    secp256k1_context_destroy(ctx);

    // Compressed prefix 0x03 => odd Y, 0x02 => even Y.
    material.y_odd = (material.compressed[0] == 0x03);
    return material;
}

/// @brief Build the expected 128-char destination for a known key material:
///        hex_bytes(big-endian X) + hex_bytes(big-endian Y), each with the "0x" prefix
///        stripped (matching GetAddress()'s plain-hex output).
std::string expected_destination(const KnownKeyMaterial& material)
{
    const auto contract_x = to_contract_order(material.uncompressed.data() + 1);
    const auto contract_y = to_contract_order(material.uncompressed.data() + 1 + kXOnlyKeyBytes);
    const std::string x_hex = rlp::base::parse::hex_bytes(contract_x.data(), kXOnlyKeyBytes);
    const std::string y_hex = rlp::base::parse::hex_bytes(contract_y.data(), kXOnlyKeyBytes);
    return x_hex.substr(rlp::base::parse::kHexCharsPerByte)
         + y_hex.substr(rlp::base::parse::kHexCharsPerByte);
}

} // namespace

TEST(Secp256k1UtilityTest, DecompressEvenYKnownVector)
{
    // Find a private key whose public key has an EVEN Y (0x02 prefix).
    std::optional<KnownKeyMaterial> even;
    for (uint8_t seed = 1; seed != 0 && !even.has_value(); ++seed)
    {
        auto material = build_known_material(seed);
        if (material && !material->y_odd)
        {
            even = material;
        }
    }
    ASSERT_TRUE(even.has_value()) << "Could not derive an even-Y key for the test";

    const auto contract_x = to_contract_order(even->uncompressed.data() + 1);
    const auto result = eth::DecompressXOnlyPubkey(contract_x, /*destination_y_odd=*/false);
    ASSERT_TRUE(result.has_value()) << "Decompression of a valid even-Y X returned nullopt";
    EXPECT_EQ(result->size(), 128U) << "Destination must be 128 hex chars";
    EXPECT_EQ(*result, expected_destination(*even))
        << "Decompressed destination must match the known uncompressed X+Y";
}

TEST(Secp256k1UtilityTest, DecompressOddYKnownVector)
{
    // Find a private key whose public key has an ODD Y (0x03 prefix).
    std::optional<KnownKeyMaterial> odd;
    for (uint8_t seed = 1; seed != 0 && !odd.has_value(); ++seed)
    {
        auto material = build_known_material(seed);
        if (material && material->y_odd)
        {
            odd = material;
        }
    }
    ASSERT_TRUE(odd.has_value()) << "Could not derive an odd-Y key for the test";

    const auto contract_x = to_contract_order(odd->uncompressed.data() + 1);
    const auto result = eth::DecompressXOnlyPubkey(contract_x, /*destination_y_odd=*/true);
    ASSERT_TRUE(result.has_value()) << "Decompression of a valid odd-Y X returned nullopt";
    EXPECT_EQ(result->size(), 128U) << "Destination must be 128 hex chars";
    EXPECT_EQ(*result, expected_destination(*odd))
        << "Decompressed destination must match the known uncompressed X+Y";

    // Odd-Y decompression must differ from even-Y decompression for the SAME X.
    const auto even_result = eth::DecompressXOnlyPubkey(contract_x, /*destination_y_odd=*/false);
    ASSERT_TRUE(even_result.has_value());
    EXPECT_NE(*result, *even_result)
        << "Even-Y and odd-Y decompression of the same X must produce different Y";
}

TEST(Secp256k1UtilityTest, DecompressInvalidX)
{
    const std::array<uint8_t, kXOnlyKeyBytes> all_zero{};
    EXPECT_FALSE(eth::DecompressXOnlyPubkey(all_zero, /*destination_y_odd=*/false).has_value());
    EXPECT_FALSE(eth::DecompressXOnlyPubkey(all_zero, /*destination_y_odd=*/true).has_value());
}

TEST(Secp256k1UtilityTest, DecompressInvalidXNotOnCurve)
{
    std::array<uint8_t, kXOnlyKeyBytes> not_on_curve{};
    not_on_curve.fill(0xFF);
    EXPECT_FALSE(eth::DecompressXOnlyPubkey(not_on_curve, /*destination_y_odd=*/false).has_value());
    EXPECT_FALSE(eth::DecompressXOnlyPubkey(not_on_curve, /*destination_y_odd=*/true).has_value());
}

TEST(Secp256k1UtilityTest, DecompressRoundTrip)
{
    // Create a key pair, derive the public key, determine its Y parity, extract X,
    // and verify DecompressXOnlyPubkey reconstructs the expected destination.
    // Uses private key with seed 0x07 -- exercises both byte ordering and parity.
    auto material = build_known_material(0x07);
    ASSERT_TRUE(material.has_value()) << "Failed to build known key material";

    const auto contract_x = to_contract_order(material->uncompressed.data() + 1);
    const auto result = eth::DecompressXOnlyPubkey(contract_x, material->y_odd);
    ASSERT_TRUE(result.has_value()) << "Round-trip decompression returned nullopt";
    EXPECT_EQ(result->size(), 128U);
    EXPECT_EQ(*result, expected_destination(*material))
        << "Round-trip must reproduce the known uncompressed X+Y destination";

    // The first 64 hex chars must equal hex_bytes(contract_X) without the "0x" prefix.
    const std::string x_hex = rlp::base::parse::hex_bytes(contract_x.data(), kXOnlyKeyBytes);
    EXPECT_EQ(result->substr(0, x_hex.size() - rlp::base::parse::kHexCharsPerByte),
              x_hex.substr(rlp::base::parse::kHexCharsPerByte))
        << "Destination X half must equal the input contract X";
}
