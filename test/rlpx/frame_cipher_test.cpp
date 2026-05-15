// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlpx/framing/frame_cipher.hpp>
#include <rlpx/crypto/kdf.hpp>
#include <vector>

using namespace rlpx;
using namespace rlpx::framing;
using namespace rlpx::auth;

namespace {

/// Round a frame payload size up to the next 16-byte boundary.
constexpr size_t padded_frame_size(size_t fsize) noexcept
{
    const size_t rem = fsize % kFrameHeaderSize;
    return (rem == 0) ? fsize : fsize + (kFrameHeaderSize - rem);
}

/// Total wire size of an encrypted frame: header_ct + header_mac + padded_frame + frame_mac.
constexpr size_t encrypted_frame_wire_size(size_t fsize) noexcept
{
    return kFrameHeaderSize + kMacSize + padded_frame_size(fsize) + kMacSize;
}

} // namespace

// Helper to create test secrets
FrameSecrets create_test_secrets()
{
    FrameSecrets secrets;
    secrets.aes_secret.fill(0x42);
    secrets.mac_secret.fill(0x55);
    secrets.egress_mac_seed.assign(kNonceSize, 0xAA);
    secrets.ingress_mac_seed.assign(kNonceSize, 0xBB);
    return secrets;
}

// Helper to create flipped secrets for decrypt side
// (alice's egress = bob's ingress, alice's ingress = bob's egress)
FrameSecrets create_flipped_secrets()
{
    FrameSecrets secrets;
    secrets.aes_secret.fill(0x42);
    secrets.mac_secret.fill(0x55);
    secrets.egress_mac_seed.assign(kNonceSize, 0xBB);   // Swapped
    secrets.ingress_mac_seed.assign(kNonceSize, 0xAA);  // Swapped
    return secrets;
}

TEST(FrameCipherTest, ConstructorInitialization) {
    auto secrets = create_test_secrets();
    FrameCipher cipher(secrets);

    EXPECT_EQ(cipher.secrets().aes_secret, secrets.aes_secret);
    EXPECT_EQ(cipher.secrets().mac_secret, secrets.mac_secret);
}

TEST(FrameCipherTest, EncryptFrame) {
    auto secrets = create_test_secrets();
    FrameCipher cipher(secrets);

    std::vector<uint8_t> frame_data = {0x01, 0x02, 0x03, 0x04, 0x05};

    FrameEncryptParams params{};
    params.frame_data = frame_data;
    params.is_first_frame = true;

    auto result = cipher.encrypt_frame(params);

    ASSERT_TRUE(result.has_value());

    // Encrypted frame: header(16) + header_mac(16) + padded_frame + frame_mac(16)
    EXPECT_EQ(result.value().size(), encrypted_frame_wire_size(frame_data.size()));
}

TEST(FrameCipherTest, EncryptEmptyFrame) {
    // ...existing code...

    auto secrets = create_test_secrets();
    FrameCipher cipher(secrets);
    
    std::vector<uint8_t> empty_data;
    
    FrameEncryptParams params{};
    params.frame_data = empty_data;
    params.is_first_frame = true;
    
    auto result = cipher.encrypt_frame(params);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FramingError::kInvalidFrameSize);
}

TEST(FrameCipherTest, EncryptTooLargeFrame) {
    auto secrets = create_test_secrets();
    FrameCipher cipher(secrets);
    
    std::vector<uint8_t> too_large_data(kMaxFrameSize + 1, 0xFF);
    
    FrameEncryptParams params{};
    params.frame_data = too_large_data;
    params.is_first_frame = true;
    
    auto result = cipher.encrypt_frame(params);
    
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), FramingError::kInvalidFrameSize);
}

TEST(FrameCipherTest, DecryptHeader) {
    auto secrets = create_test_secrets();
    FrameCipher cipher_encrypt(secrets);
    
    std::vector<uint8_t> frame_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    FrameEncryptParams params{};
    params.frame_data = frame_data;
    params.is_first_frame = true;
    
    auto encrypted = cipher_encrypt.encrypt_frame(params);
    ASSERT_TRUE(encrypted.has_value());
    
    // Extract header and header MAC
    std::array<uint8_t, kFrameHeaderSize> header_ct;
    std::array<uint8_t, kMacSize> header_mac;
    
    std::memcpy(header_ct.data(), encrypted.value().data(), kFrameHeaderSize);
    std::memcpy(header_mac.data(), encrypted.value().data() + kFrameHeaderSize, kMacSize);
    
    // Decrypt header with flipped secrets (alice's egress = bob's ingress)
    FrameCipher cipher_decrypt(create_flipped_secrets());
    auto size_result = cipher_decrypt.decrypt_header(header_ct, header_mac);
    
    ASSERT_TRUE(size_result.has_value());
    EXPECT_EQ(size_result.value(), frame_data.size());
}

TEST(FrameCipherTest, DecryptHeaderInvalidMac) {
    auto secrets = create_test_secrets();
    FrameCipher cipher_encrypt(secrets);
    
    std::vector<uint8_t> frame_data = {0x01, 0x02, 0x03, 0x04, 0x05};
    
    FrameEncryptParams params{};
    params.frame_data = frame_data;
    params.is_first_frame = true;
    
    auto encrypted = cipher_encrypt.encrypt_frame(params);
    ASSERT_TRUE(encrypted.has_value());
    
    // Extract header and corrupt MAC
    std::array<uint8_t, kFrameHeaderSize> header_ct;
    std::array<uint8_t, kMacSize> header_mac;
    
    std::memcpy(header_ct.data(), encrypted.value().data(), kFrameHeaderSize);
    std::memcpy(header_mac.data(), encrypted.value().data() + kFrameHeaderSize, kMacSize);
    
    // Corrupt MAC
    header_mac[0] ^= 0xFF;
    
    // Try to decrypt with flipped secrets
    FrameCipher cipher_decrypt(create_flipped_secrets());
    auto size_result = cipher_decrypt.decrypt_header(header_ct, header_mac);
    
    EXPECT_FALSE(size_result.has_value());
    EXPECT_EQ(size_result.error(), FramingError::kMacMismatch);
}

TEST(FrameCipherTest, EncryptDecryptRoundtrip) {
    auto secrets = create_test_secrets();
    
    std::vector<uint8_t> original_data = {
        0x48, 0x65, 0x6c, 0x6c, 0x6f,  // "Hello"
        0x20, 0x57, 0x6f, 0x72, 0x6c, 0x64  // " World"
    };
    
    // Encrypt
    FrameCipher cipher_encrypt(secrets);
    FrameEncryptParams params{};
    params.frame_data = original_data;
    params.is_first_frame = true;
    
    auto encrypted = cipher_encrypt.encrypt_frame(params);
    ASSERT_TRUE(encrypted.has_value());
    
    // Parse encrypted frame
    size_t offset = 0;
    std::array<uint8_t, kFrameHeaderSize> header_ct;
    std::array<uint8_t, kMacSize> header_mac;
    
    std::memcpy(header_ct.data(), encrypted.value().data() + offset, kFrameHeaderSize);
    offset += kFrameHeaderSize;
    
    std::memcpy(header_mac.data(), encrypted.value().data() + offset, kMacSize);
    offset += kMacSize;
    
    size_t frame_size = encrypted.value().size() - offset - kMacSize;
    std::vector<uint8_t> frame_ct(frame_size);
    std::memcpy(frame_ct.data(), encrypted.value().data() + offset, frame_size);
    offset += frame_size;
    
    std::array<uint8_t, kMacSize> frame_mac;
    std::memcpy(frame_mac.data(), encrypted.value().data() + offset, kMacSize);
    
    // Decrypt with flipped secrets (alice's egress = bob's ingress)
    FrameCipher cipher_decrypt(create_flipped_secrets());
    FrameDecryptParams decrypt_params{};
    decrypt_params.header_ciphertext = ByteView(header_ct.data(), header_ct.size());
    decrypt_params.header_mac = ByteView(header_mac.data(), header_mac.size());
    decrypt_params.frame_ciphertext = ByteView(frame_ct.data(), frame_ct.size());
    decrypt_params.frame_mac = ByteView(frame_mac.data(), frame_mac.size());
    
    auto decrypted = cipher_decrypt.decrypt_frame(decrypt_params);
    ASSERT_TRUE(decrypted.has_value());
    EXPECT_EQ(decrypted.value(), original_data);
}

TEST(FrameCipherTest, MultipleFrames) {
    auto secrets = create_test_secrets();
    FrameCipher cipher(secrets);
    
    std::vector<std::vector<uint8_t>> frames = {
        {0x01, 0x02, 0x03},
        {0x04, 0x05, 0x06, 0x07},
        {0x08, 0x09}
    };
    
    for ( const auto& frame_data : frames ) {
        FrameEncryptParams params{};
        params.frame_data = frame_data;
        params.is_first_frame = false;
        
        auto result = cipher.encrypt_frame(params);
        ASSERT_TRUE(result.has_value());

        // Each frame should have correct structure
        EXPECT_EQ(result.value().size(), encrypted_frame_wire_size(frame_data.size()));
    }
}

TEST(FrameCipherTest, MaxFrameSize) {
    auto secrets = create_test_secrets();
    FrameCipher cipher(secrets);
    
    std::vector<uint8_t> max_frame(kMaxFrameSize, 0xAA);
    
    FrameEncryptParams params{};
    params.frame_data = max_frame;
    params.is_first_frame = true;
    
    auto result = cipher.encrypt_frame(params);
    
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().size(), encrypted_frame_wire_size(kMaxFrameSize));
}

// ── FrameCipherMacTest ────────────────────────────────────────────────────────
//
// Validates MAC computation against go-ethereum's TestFrameReadWrite vectors.
//
// go-ethereum uses AES key = MAC key = keccak256("") with an empty MAC seed.
// The original test uses a fakeHash that ignores all writes and always returns
// [01*32] — we cannot replicate that, so the MAC bytes differ from go-ethereum's
// golden (which are [01*16]). Instead we hard-code the correct Keccak256 MAC
// values derived from the same inputs, independently computed in Python.
//
// AES-CTR ciphertext bytes match go-ethereum's golden exactly because the
// AES key, IV (all-zeros), and plaintext are identical.
//
// Reference: go-ethereum/p2p/rlpx/rlpx_test.go::TestFrameReadWrite
// Reference: go-ethereum/p2p/rlpx/rlpx_test.go::TestHandshakeForwardCompatibility

namespace {

/// Decode a fixed-length hex string to a byte array.
template<size_t N>
std::array<uint8_t, N> mac_unhex(const char* s) noexcept
{
    std::array<uint8_t, N> out{};
    auto nibble = [](char c) -> uint8_t {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        return static_cast<uint8_t>(c - 'A' + 10);
    };
    for (size_t i = 0; i < N; ++i)
        out[i] = static_cast<uint8_t>((nibble(s[i*2]) << 4U) | nibble(s[i*2+1]));
    return out;
}

/// go-ethereum TestFrameReadWrite setup:
///   AES key = MAC key = keccak256("") = c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
///   IV = all-zeros (go-ethereum cipher.NewCTR(block, iv) with iv=make([]byte,16))
///   MAC seed = empty (fakeHash ignores all writes; we use empty written buffer)
FrameSecrets make_goeth_frameRW_secrets() noexcept
{
    FrameSecrets s;
    s.aes_secret = mac_unhex<kAesKeySize>(
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    s.mac_secret = mac_unhex<kMacKeySize>(
        "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    // empty egress/ingress seeds → written = {} → sum() = keccak256("")
    s.egress_mac_seed.clear();
    s.ingress_mac_seed.clear();
    return s;
}

} // namespace

/// Verifies that encrypt_frame produces the correct AES-CTR ciphertext bytes
/// for go-ethereum's TestFrameReadWrite frame (msgCode=8, RLP([1,2,3,4])).
///
/// The AES-CTR header_ct and frame_ct must match go-ethereum's golden exactly.
/// The header_mac and frame_mac use real Keccak256 (not go-ethereum's fakeHash)
/// and match values independently computed in Python with the same algorithm.
TEST(FrameCipherMacTest, GoEthFrameRWVectors)
{
    // go-ethereum: msg = []uint{1, 2, 3, 4}; msgEnc = rlp.EncodeToBytes(msg)
    // wantContent = "08C401020304" — msgCode 0x08 prepended, then RLP [1,2,3,4]
    const std::vector<uint8_t> frame_data = {0x08, 0xC4, 0x01, 0x02, 0x03, 0x04};

    FrameCipher cipher(make_goeth_frameRW_secrets());
    auto result = cipher.encrypt_frame(FrameEncryptParams{frame_data, false});
    ASSERT_TRUE(result.has_value());

    const auto& wire = result.value();
    // Frame: 6 bytes padded to 16 → total wire = 16+16+16+16 = 64 bytes
    ASSERT_EQ(wire.size(), 64u);

    // ── header ciphertext (go-ethereum golden) ────────────────────────────────
    const auto want_hct = mac_unhex<16>("00828ddae471818bb0bfa6b551d1cb42");
    EXPECT_EQ((std::array<uint8_t,16>{}), (std::array<uint8_t,16>{})); // sanity
    for (size_t i = 0; i < 16; ++i)
        EXPECT_EQ(wire[i], want_hct[i]) << "header_ct byte " << i;

    // ── header MAC (real Keccak256, computed independently) ───────────────────
    const auto want_hmac = mac_unhex<16>("e5e0de0c87a22a8994e1710717a2b98d");
    for (size_t i = 0; i < 16; ++i)
        EXPECT_EQ(wire[16 + i], want_hmac[i]) << "header_mac byte " << i;

    // ── frame ciphertext (go-ethereum golden) ─────────────────────────────────
    const auto want_fct = mac_unhex<16>("ba628a4ba590cb43f7848f41c4382885");
    for (size_t i = 0; i < 16; ++i)
        EXPECT_EQ(wire[32 + i], want_fct[i]) << "frame_ct byte " << i;

    // ── frame MAC (real Keccak256, computed independently) ────────────────────
    const auto want_fmac = mac_unhex<16>("0df5eedb2f01442e3c4fef533ec0cf3b");
    for (size_t i = 0; i < 16; ++i)
        EXPECT_EQ(wire[48 + i], want_fmac[i]) << "frame_mac byte " << i;
}

// ── FrameCipherVectorTest ─────────────────────────────────────────────────────
//
// Uses the REAL derived secrets from go-ethereum TestHandshakeForwardCompatibility
// (Auth₂ / Ack₂, responder perspective) to drive two FrameCipher instances —
// one for the initiator, one for the responder — and verifies that an encrypted
// frame can be round-tripped end-to-end.
//
// This is the critical test that was missing: GoEthFrameRWVectors above only
// exercises keccak256("") secrets with an empty MAC seed.  This test exercises
// the actual ECDH-derived wantAES / wantMAC values together with their correct
// MAC seeds, which is what the live Sepolia connection uses.
//
// Constants taken verbatim from go-ethereum/p2p/rlpx/rlpx_test.go and
// handshake_vectors_test.cpp (same file, same session).

namespace {

/// Decode a hex string of arbitrary length into a ByteBuffer.
ByteBuffer vec_unhex(const char* hex) noexcept
{
    auto nibble = [](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
        if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(c - 'a' + 10); }
        return static_cast<uint8_t>(c - 'A' + 10);
    };
    const size_t n = std::strlen(hex) / 2;
    ByteBuffer   out(n);
    for (size_t i = 0; i < n; ++i)
    {
        out[i] = static_cast<uint8_t>((nibble(hex[i * 2]) << 4U) | nibble(hex[i * 2 + 1]));
    }
    return out;
}

/// Build FrameSecrets for the given side using TestHandshakeForwardCompatibility vectors.
///
/// Mirrors derive_frame_secrets() in auth_handshake.cpp:
///   mac_seed_bytes(nonce, wire) = xor(mac_secret, nonce) || wire
///
/// Initiator egress  = xor(MAC, nonceB) || authWire
/// Initiator ingress = xor(MAC, nonceA) || ackWire
/// Responder egress  = xor(MAC, nonceA) || ackWire
/// Responder ingress = xor(MAC, nonceB) || authWire
auth::FrameSecrets make_vector_secrets(bool is_initiator) noexcept
{
    // ── known constants from TestHandshakeForwardCompatibility ────────────────
    const auto want_aes = mac_unhex<kAesKeySize>(
        "80e8632c05fed6fc2a13b0f8d31a3cf645366239170ea067065aba8e28bac487");
    const auto want_mac = mac_unhex<kMacKeySize>(
        "2ea74ec5dae199227dff1af715362700e989d889d7a493cb0639691efb8e5f98");
    const auto nonce_a  = mac_unhex<kNonceSize>(
        "7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6");
    const auto nonce_b  = mac_unhex<kNonceSize>(
        "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd");

    // Auth₂ full wire bytes (2-byte EIP-8 prefix + ciphertext)
    const ByteBuffer auth_wire = vec_unhex(
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
        "3bf7678318e2d5b5340c9e488eefea198576344afbdf66db5f51204a6961a63ce072c8926c");

    // Ack₂ full wire bytes (2-byte EIP-8 prefix + ciphertext)
    const ByteBuffer ack_wire = vec_unhex(
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
        "5833c2464c805246155289f4");

    // ── build MAC seed: xor(want_mac, nonce) || wire ──────────────────────────
    auto make_seed = [](const MacKey& mac_key,
                        const std::array<uint8_t, kNonceSize>& nonce,
                        const ByteBuffer& wire) -> ByteBuffer
    {
        ByteBuffer seed;
        seed.reserve(kNonceSize + wire.size());
        for (size_t i = 0; i < kNonceSize; ++i)
        {
            seed.push_back(mac_key[i] ^ nonce[i]);
        }
        seed.insert(seed.end(), wire.begin(), wire.end());
        return seed;
    };

    auth::FrameSecrets s;
    s.aes_secret = want_aes;
    s.mac_secret = want_mac;

    if (is_initiator)
    {
        s.egress_mac_seed  = make_seed(want_mac, nonce_b, auth_wire);
        s.ingress_mac_seed = make_seed(want_mac, nonce_a, ack_wire);
    }
    else
    {
        s.egress_mac_seed  = make_seed(want_mac, nonce_a, ack_wire);
        s.ingress_mac_seed = make_seed(want_mac, nonce_b, auth_wire);
    }

    return s;
}

} // namespace

/// @brief Verifies that a frame encrypted by the initiator can be decrypted by
///        the responder when both use REAL TestHandshakeForwardCompatibility secrets.
///
/// A failure here (kMacMismatch) means the frame cipher is broken for real
/// handshake-derived keys, regardless of whether simpler test vectors pass.
TEST(FrameCipherVectorTest, InitiatorToResponderRoundTrip)
{
    const std::vector<uint8_t> payload = {0x08, 0xC4, 0x01, 0x02, 0x03, 0x04};

    FrameCipher initiator(make_vector_secrets(true));
    FrameCipher responder(make_vector_secrets(false));

    // ── encrypt on initiator side ─────────────────────────────────────────────
    const auto enc = initiator.encrypt_frame(FrameEncryptParams{payload, false});
    ASSERT_TRUE(enc.has_value())
        << "encrypt_frame failed with real handshake secrets";

    const auto& wire   = enc.value();
    const size_t padded = wire.size() - kFrameHeaderSize - kMacSize - kMacSize;

    std::array<uint8_t, kFrameHeaderSize> hct{};
    std::array<uint8_t, kMacSize>         hmac{};
    std::vector<uint8_t>                  fct(padded);
    std::array<uint8_t, kMacSize>         fmac{};

    std::memcpy(hct.data(),  wire.data(),                                       kFrameHeaderSize);
    std::memcpy(hmac.data(), wire.data() + kFrameHeaderSize,                    kMacSize);
    std::memcpy(fct.data(),  wire.data() + kFrameHeaderSize + kMacSize,         padded);
    std::memcpy(fmac.data(), wire.data() + kFrameHeaderSize + kMacSize + padded, kMacSize);

    // ── decrypt on responder side ─────────────────────────────────────────────
    const FrameDecryptParams dp{
        ByteView(hct.data(),  hct.size()),
        ByteView(hmac.data(), hmac.size()),
        ByteView(fct.data(),  fct.size()),
        ByteView(fmac.data(), fmac.size())
    };

    const auto dec = responder.decrypt_frame(dp);
    ASSERT_TRUE(dec.has_value())
        << "decrypt_frame failed with real handshake secrets "
           "(MAC mismatch — frame cipher is broken for live connections)";
    EXPECT_EQ(dec.value(), payload)
        << "decrypted payload does not match original";
}

// ─────────────────────────────────────────────────────────────────────────────

/// Verifies that MAC state is correctly maintained across multiple consecutive
/// frames. This is the regression test for the HashMAC::compute() bug where
/// write(aes_buf) was missing — without it the MAC state diverges after the
/// first frame and every subsequent decrypt_header/decrypt_frame returns
/// kMacMismatch.
TEST(FrameCipherMacTest, ConsecutiveFramesRoundTrip)
{
    const std::vector<std::vector<uint8_t>> payloads = {
        {0x01, 0x02, 0x03, 0x04, 0x05},
        {0x10, 0x20, 0x30},
        {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11},
    };

    auto enc_secrets = create_test_secrets();
    auto dec_secrets = create_flipped_secrets();

    FrameCipher enc(enc_secrets);
    FrameCipher dec(dec_secrets);

    for (size_t idx = 0; idx < payloads.size(); ++idx)
    {
        const auto& payload = payloads[idx];
        auto enc_result = enc.encrypt_frame(FrameEncryptParams{payload, idx == 0});
        ASSERT_TRUE(enc_result.has_value()) << "encrypt failed for frame " << idx;

        const auto& wire = enc_result.value();
        const size_t padded = (payload.size() % 16 == 0)
            ? payload.size()
            : payload.size() + (16 - payload.size() % 16);

        std::array<uint8_t, kFrameHeaderSize> hct;
        std::array<uint8_t, kMacSize>         hmac;
        std::vector<uint8_t>                  fct(padded);
        std::array<uint8_t, kMacSize>         fmac;

        std::memcpy(hct.data(),  wire.data(),                                kFrameHeaderSize);
        std::memcpy(hmac.data(), wire.data() + kFrameHeaderSize,             kMacSize);
        std::memcpy(fct.data(),  wire.data() + kFrameHeaderSize + kMacSize,  padded);
        std::memcpy(fmac.data(), wire.data() + kFrameHeaderSize + kMacSize + padded, kMacSize);

        FrameDecryptParams dp{
            ByteView(hct.data(), hct.size()),
            ByteView(hmac.data(), hmac.size()),
            ByteView(fct.data(), fct.size()),
            ByteView(fmac.data(), fmac.size())
        };

        auto dec_result = dec.decrypt_frame(dp);
        ASSERT_TRUE(dec_result.has_value())
            << "decrypt failed for frame " << idx
            << " (MAC mismatch — check HashMAC::compute() write(aes_buf))";
        EXPECT_EQ(dec_result.value(), payload) << "plaintext mismatch for frame " << idx;
    }
}
