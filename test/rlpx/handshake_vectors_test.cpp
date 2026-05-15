// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// Validates derive_frame_secrets() against go-ethereum's known test vectors from
// go-ethereum/p2p/rlpx/rlpx_test.go :: TestHandshakeForwardCompatibility
//
// Keys used in go-ethereum test:
//   keyA  = 49a7b37aa6f6645917e7b807e9d1c00d4fa71f18343b0d4122a4d2df64dd6fee
//   keyB  = b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291
//   ephB  = e238eb8e04fee6511ab04c6dd3c89ce097b11f25d584863ac2b6d5b35b1847e4
//   nonceA = 7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6
//   nonceB = 559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd
//
// Expected (Auth₂, Ack₂, responder side):
//   wantAES  = 80e8632c05fed6fc2a13b0f8d31a3cf645366239170ea067065aba8e28bac487
//   wantMAC  = 2ea74ec5dae199227dff1af715362700e989d889d7a493cb0639691efb8e5f98
//   wantFooIngressHash = 0c7ec6340062cc46f5e9f1e3cf86f8c8c403c5a0964f5df0ebd34a75ddc86db5

#include <gtest/gtest.h>
#include <rlpx/auth/auth_handshake.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/rlpx_types.hpp>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>
#include <nil/crypto3/hash/accumulators/hash.hpp>
#include <secp256k1.h>
#include <array>
#include <cstring>

using namespace rlpx;
using namespace rlpx::auth;

namespace {

/// Decode a fixed-length hex string into a byte array.
template<size_t N>
std::array<uint8_t, N> unhex_arr(const char* hex_str) noexcept
{
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i)
    {
        const char hi = hex_str[i * 2];
        const char lo = hex_str[i * 2 + 1];
        auto nibble = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
            if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(c - 'a' + 10); }
            return static_cast<uint8_t>(c - 'A' + 10);
        };
        out[i] = static_cast<uint8_t>((nibble(hi) << 4U) | nibble(lo));
    }
    return out;
}

/// Decode a variable-length hex string into a ByteBuffer.
ByteBuffer unhex_buf(const char* hex_str) noexcept
{
    const size_t n = std::strlen(hex_str) / 2;
    ByteBuffer out(n);
    for (size_t i = 0; i < n; ++i)
    {
        const char hi = hex_str[i * 2];
        const char lo = hex_str[i * 2 + 1];
        auto nibble = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
            if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(c - 'a' + 10); }
            return static_cast<uint8_t>(c - 'A' + 10);
        };
        out[i] = static_cast<uint8_t>((nibble(hi) << 4U) | nibble(lo));
    }
    return out;
}

/// Derive the uncompressed 64-byte public key from a private key via secp256k1.
PublicKey pubkey_from_privkey(const PrivateKey& priv) noexcept
{
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    secp256k1_pubkey secp_pub;
    (void)secp256k1_ec_pubkey_create(ctx, &secp_pub, priv.data());
    std::array<uint8_t, kUncompressedPubKeySize> serialized{};
    size_t len = kUncompressedPubKeySize;
    secp256k1_ec_pubkey_serialize(ctx, serialized.data(), &len,
                                  &secp_pub, SECP256K1_EC_UNCOMPRESSED);
    secp256k1_context_destroy(ctx);
    PublicKey pub{};
    // Skip 0x04 prefix byte
    std::memcpy(pub.data(), serialized.data() + kUncompressedPubKeyPrefixSize, kPublicKeySize);
    return pub;
}

/// keccak256 of data.
std::array<uint8_t, kNonceSize> keccak256_of(const uint8_t* data, size_t len) noexcept
{
    using Hasher = nil::crypto3::hashes::keccak_1600<256>;
    nil::crypto3::accumulator_set<Hasher> acc;
    nil::crypto3::hash<Hasher>(data, data + len, acc);
    auto digest = nil::crypto3::accumulators::extract::hash<Hasher>(acc);
    std::array<uint8_t, kNonceSize> result{};
    std::copy(digest.begin(), digest.end(), result.begin());
    return result;
}

} // anonymous namespace

// ── go-ethereum test constants ────────────────────────────────────────────────

// Auth₂ ciphertext (from eip8HandshakeAuthTests[0].input, whitespace stripped)
static const char* kAuth2Hex =
    "01b304ab7578555167be8154d5cc456f567d5ba302662433674222360f08d5f1534499d3678b513b"
    "0fca474f3a514b18e75683032eb63fccb16c156dc6eb2c0b1593f0d84ac74f6e475f1b8d56116b84"
    "9634a8c458705bf83a626ea0384d4d7341aae591fae42ce6bd5c850bfe0b999a694a49bbbaf3ef6c"
    "da61110601d3b4c02ab6c30437257a6e0117792631a4b47c1d52fc0f8f89caadeb7d02770bf999cc"
    "147d2df3b62e1ffb2c9d8c125a3984865356266bca11ce7d3a688663a51d82defaa8aad69da39ab6"
    "d5470e81ec5f2a7a47fb865ff7cca21516f9299a07b1bc63ba56c7a1a892112841ca44b6e0034dee"
    "70c9adabc15d76a54f443593fafdc3b27af8059703f88928e199cb122362a4b35f62386da7caad09"
    "c001edaeb5f8a06d2b26fb6cb93c52a9fca51853b68193916982358fe1e5369e249875bb8d0d0ec3"
    "6f917bc5e1eafd5896d46bd61ff23f1a863a8a8dcd54c7b109b771c8e61ec9c8908c733c0263440e"
    "2aa067241aaa433f0bb053c7b31a838504b148f570c0ad62837129e547678c5190341e4f1693956c"
    "3bf7678318e2d5b5340c9e488eefea198576344afbdf66db5f51204a6961a63ce072c8926c";

// Ack₂ ciphertext (from eip8HandshakeRespTests[0].input, whitespace stripped)
static const char* kAck2Hex =
    "01ea0451958701280a56482929d3b0757da8f7fbe5286784beead59d95089c217c9b917788989470"
    "b0e330cc6e4fb383c0340ed85fab836ec9fb8a49672712aeabbdfd1e837c1ff4cace34311cd7f4de"
    "05d59279e3524ab26ef753a0095637ac88f2b499b9914b5f64e143eae548a1066e14cd2f4bd7f814"
    "c4652f11b254f8a2d0191e2f5546fae6055694aed14d906df79ad3b407d94692694e259191cde171"
    "ad542fc588fa2b7333313d82a9f887332f1dfc36cea03f831cb9a23fea05b33deb999e85489e645f"
    "6aab1872475d488d7bd6c7c120caf28dbfc5d6833888155ed69d34dbdc39c1f299be1057810f34fb"
    "e754d021bfca14dc989753d61c413d261934e1a9c67ee060a25eefb54e81a4d14baff922180c395d"
    "3f998d70f46f6b58306f969627ae364497e73fc27f6d17ae45a413d322cb8814276be6ddd13b885b"
    "201b943213656cde498fa0e9ddc8e0b8f8a53824fbd82254f3e2c17e8eaea009c38b4aa0a3f306e8"
    "797db43c25d68e86f262e564086f59a2fc60511c42abfb3057c247a8a8fe4fb3ccbadde17514b7ac"
    "8000cdb6a912778426260c47f38919a91f25f4b5ffb455d6aaaf150f7e5529c100ce62d6d92826a7"
    "1778d809bdf60232ae21ce8a437eca8223f45ac37f6487452ce626f549b3b5fdee26afd2072e4bc7"
    "5833c2464c805246155289f4";

// ── Test ──────────────────────────────────────────────────────────────────────

/// @brief Verifies derive_frame_secrets() against go-ethereum test vectors.
///
/// Uses Auth₂ / Ack₂ ciphertexts and known private keys from
/// TestHandshakeForwardCompatibility in go-ethereum/p2p/rlpx/rlpx_test.go.
/// Runs from the **responder** (keyB) perspective.
TEST(HandshakeVectorsTest, DeriveFrameSecretsMatchesGoEthereum)
{
    // Private keys (from go-ethereum test) — only ephB and ephA are needed for ECDH
    const auto privEphB = unhex_arr<kPrivateKeySize>(
        "e238eb8e04fee6511ab04c6dd3c89ce097b11f25d584863ac2b6d5b35b1847e4");
    const auto privA = unhex_arr<kPrivateKeySize>(
        "49a7b37aa6f6645917e7b807e9d1c00d4fa71f18343b0d4122a4d2df64dd6fee");

    const auto nonceA = unhex_arr<kNonceSize>(
        "7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6");
    const auto nonceB = unhex_arr<kNonceSize>(
        "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd");

    // Derive public keys from private keys
    const PublicKey pubA    = pubkey_from_privkey(privA);
    const PublicKey pubEphB = pubkey_from_privkey(privEphB);

    // go-ethereum responder's handleAuthMsg extracts the initiator's ephemeral pubkey
    // from the signature. For our test we use the known ephA pubkey directly.
    const PublicKey pubEphA = pubkey_from_privkey(
        unhex_arr<kPrivateKeySize>(
            "869d6ecf5211f1cc60418a13b9d870b22959d0c16f02bec714c960dd2298a32d"));

    // Wire bytes — as stored in go-ethereum h.rbuf / authPacket / authRespPacket
    const ByteBuffer auth_wire = unhex_buf(kAuth2Hex);
    const ByteBuffer ack_wire  = unhex_buf(kAck2Hex);

    // Build AuthKeyMaterial for responder (keyB) perspective:
    //   - initiator = keyA, responder = keyB
    //   - initiator_nonce = nonceA, recipient_nonce = nonceB
    //   - peer_ephemeral_public_key = ephA (initiator's ephemeral)
    //   - local_ephemeral_private_key = ephB (responder's ephemeral)
    AuthKeyMaterial keys;
    keys.peer_public_key              = pubA;
    keys.peer_ephemeral_public_key    = pubEphA;
    keys.local_ephemeral_public_key   = pubEphB;
    keys.local_ephemeral_private_key  = privEphB;
    keys.initiator_nonce              = nonceA;
    keys.recipient_nonce              = nonceB;
    keys.initiator_auth_message       = auth_wire;
    keys.recipient_ack_message        = ack_wire;

    // Derive secrets from the responder's perspective (is_initiator = false)
    const FrameSecrets secrets = derive_frame_secrets(keys, false);

    // ── Verify AES secret ────────────────────────────────────────────────────
    const auto want_aes = unhex_arr<kAesKeySize>(
        "80e8632c05fed6fc2a13b0f8d31a3cf645366239170ea067065aba8e28bac487");
    EXPECT_EQ(secrets.aes_secret, want_aes)
        << "AES secret mismatch";

    // ── Verify MAC secret ────────────────────────────────────────────────────
    const auto want_mac = unhex_arr<kMacKeySize>(
        "2ea74ec5dae199227dff1af715362700e989d889d7a493cb0639691efb8e5f98");
    EXPECT_EQ(secrets.mac_secret, want_mac)
        << "MAC secret mismatch";

    // ── Verify ingress MAC seed produces correct hash after "foo" ────────────
    // go-ethereum: io.WriteString(derived.IngressMAC, "foo") then Sum(nil)
    // Our seed already contains xor(mac,nonce) || auth_wire.
    // Writing "foo" appends 3 more bytes to the Keccak state.
    // Since sum() = keccak256(all_written), the expected hash is:
    //   keccak256(ingress_mac_seed || "foo")
    ByteBuffer ingress_plus_foo = secrets.ingress_mac_seed;
    ingress_plus_foo.push_back('f');
    ingress_plus_foo.push_back('o');
    ingress_plus_foo.push_back('o');

    const auto foo_ingress_hash = keccak256_of(
        ingress_plus_foo.data(), ingress_plus_foo.size());

    const auto want_foo_ingress = unhex_arr<kNonceSize>(
        "0c7ec6340062cc46f5e9f1e3cf86f8c8c403c5a0964f5df0ebd34a75ddc86db5");
    EXPECT_EQ(foo_ingress_hash, want_foo_ingress)
        << "IngressMAC(\"foo\") hash mismatch — seed bytes are wrong";
}

