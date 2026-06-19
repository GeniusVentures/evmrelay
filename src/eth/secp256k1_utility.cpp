// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/secp256k1_utility.hpp>
#include <eth/abi_decoder.hpp>
#include <base/parse_utility.hpp>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <algorithm>

namespace eth {

namespace {

constexpr size_t kCompactSignatureBytes = 64;
constexpr size_t kRecoverableSignatureBytes = 65;
constexpr size_t kUncompressedPublicKeyBytes = 65;
constexpr uint8_t kUncompressedPublicKeyPrefix = 0x04;

// ── X-only decompression constants (Bridge V2) ──────────────────────────────
constexpr size_t kXOnlyKeyBytes = 32;
constexpr size_t kCompressedKeyLen = 33;
constexpr uint8_t kEvenYParityPrefix = 0x02;
constexpr uint8_t kOddYParityPrefix = 0x03;

Address address_from_uncompressed_public_key(
    const std::array<uint8_t, kUncompressedPublicKeyBytes>& public_key)
{
    codec::ByteBuffer payload(public_key.begin() + 1, public_key.end());
    const auto hash = abi::keccak256(payload.data(), payload.size());

    Address address{};
    std::copy(hash.end() - address.size(), hash.end(), address.begin());
    return address;
}

std::optional<Address> address_from_public_key(secp256k1_context* context, const secp256k1_pubkey& public_key)
{
    std::array<uint8_t, kUncompressedPublicKeyBytes> public_key_bytes{};
    size_t public_key_length = public_key_bytes.size();
    if (!secp256k1_ec_pubkey_serialize(
            context,
            public_key_bytes.data(),
            &public_key_length,
            &public_key,
            SECP256K1_EC_UNCOMPRESSED))
    {
        return std::nullopt;
    }

    if (public_key_length != public_key_bytes.size()
        || public_key_bytes.front() != kUncompressedPublicKeyPrefix)
    {
        return std::nullopt;
    }

    return address_from_uncompressed_public_key(public_key_bytes);
}

} // namespace

std::optional<Address> secp256k1_address_from_private_key(
    const Secp256k1PrivateKey& private_key) noexcept
{
    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    if (!secp256k1_ec_seckey_verify(context, private_key.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_pubkey public_key{};
    if (!secp256k1_ec_pubkey_create(context, &public_key, private_key.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    const auto address = address_from_public_key(context, public_key);
    secp256k1_context_destroy(context);
    return address;
}

std::optional<Address> secp256k1_recover_address(
    const Hash256&           message_hash,
    const codec::ByteBuffer& recoverable_signature) noexcept
{
    if (recoverable_signature.size() != kRecoverableSignatureBytes)
    {
        return std::nullopt;
    }

    const int recovery_id = static_cast<int>(recoverable_signature[kCompactSignatureBytes]);
    if (recovery_id < 0 || recovery_id > 3)
    {
        return std::nullopt;
    }

    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    secp256k1_ecdsa_recoverable_signature signature{};
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(
            context,
            &signature,
            recoverable_signature.data(),
            recovery_id))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_pubkey public_key{};
    if (!secp256k1_ecdsa_recover(context, &public_key, &signature, message_hash.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    const auto address = address_from_public_key(context, public_key);
    secp256k1_context_destroy(context);
    return address;
}

std::optional<codec::ByteBuffer> secp256k1_sign_recoverable(
    const Hash256&             message_hash,
    const Secp256k1PrivateKey& private_key) noexcept
{
    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    if (!secp256k1_ec_seckey_verify(context, private_key.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_ecdsa_recoverable_signature signature{};
    if (!secp256k1_ecdsa_sign_recoverable(
            context,
            &signature,
            message_hash.data(),
            private_key.data(),
            nullptr,
            nullptr))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    codec::ByteBuffer signature_bytes(kRecoverableSignatureBytes);
    int recovery_id = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(
        context,
        signature_bytes.data(),
        &recovery_id,
        &signature);
    secp256k1_context_destroy(context);

    signature_bytes[kCompactSignatureBytes] = static_cast<uint8_t>(recovery_id);
    return signature_bytes;
}

std::optional<std::string> DecompressXOnlyPubkey(
    const std::array<uint8_t, 32>& contract_x_bytes,
    bool                           destination_y_odd) noexcept
{
    // Guard: reject the point-at-infinity (all-zero X) -- invalid as a public key.
    const bool all_zero = std::all_of(
        contract_x_bytes.begin(), contract_x_bytes.end(),
        [](const uint8_t b) { return b == 0; });
    if (all_zero)
    {
        return std::nullopt;
    }

    // Select the compressed-key prefix from the explicit parity flag carried by
    // the event (D-09). false = even Y (0x02), true = odd Y (0x03).
    const uint8_t prefix = destination_y_odd ? kOddYParityPrefix : kEvenYParityPrefix;

    // libsecp256k1 expects the X coordinate in big-endian (MSB first).  Contract
    // bytes are stored in reverse order (LSB first in hex), so reverse them.
    std::array<uint8_t, kXOnlyKeyBytes> x_bigendian{};
    for (size_t i = 0; i < kXOnlyKeyBytes; ++i)
    {
        x_bigendian[i] = contract_x_bytes[kXOnlyKeyBytes - 1 - i];
    }

    // Build the 33-byte compressed key: [prefix][X_bigendian].
    std::array<uint8_t, kCompressedKeyLen> compressed{};
    compressed[0] = prefix;
    std::copy(x_bigendian.begin(), x_bigendian.end(), compressed.begin() + 1);

    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    secp256k1_pubkey public_key{};
    if (!secp256k1_ec_pubkey_parse(
            context,
            &public_key,
            compressed.data(),
            compressed.size()))
    {
        // X + parity combination is not on the curve -- malformed event data.
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    std::array<uint8_t, kUncompressedPublicKeyBytes> uncompressed{};
    size_t uncompressed_len = uncompressed.size();
    secp256k1_ec_pubkey_serialize(
        context,
        uncompressed.data(),
        &uncompressed_len,
        &public_key,
        SECP256K1_EC_UNCOMPRESSED);
    secp256k1_context_destroy(context);

    // uncompressed = [0x04][X_bigendian(32)][Y_bigendian(32)] (65 bytes, indices 0..64).
    // Y_bigendian occupies indices 33..64.  Reverse to contract byte order so that
    // hex_bytes() reproduces the same ordering used by GetAddress():
    //   contract_y[i] = Y_bigendian[31 - i] = uncompressed[33 + (31 - i)] = uncompressed[64 - i].
    std::array<uint8_t, kXOnlyKeyBytes> contract_y{};
    for (size_t i = 0; i < kXOnlyKeyBytes; ++i)
    {
        contract_y[i] = uncompressed[kUncompressedPublicKeyBytes - 1 - i];
    }

    // Destination = hex_bytes(contract_X, 32) + hex_bytes(contract_Y, 32) = 128 chars.
    const std::string destination = rlp::base::parse::hex_bytes(contract_x_bytes.data(), kXOnlyKeyBytes)
                                  + rlp::base::parse::hex_bytes(contract_y.data(), kXOnlyKeyBytes);
    return destination;
}

} // namespace eth
