// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/auth/ecies_cipher.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <base/rlp-logger.hpp>
#include <secp256k1.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <cstring>

namespace rlpx::auth {

namespace {

static rlp::base::Logger& ecies_log() {
    static auto log = rlp::base::createLogger("rlpx.ecies");
    return log;
}

/// go-ethereum ecies deriveKeys: concatKDF then hash Km.
/// concatKDF(SHA256, z, nil, 32) = SHA256(0x00000001 || z) → 32 bytes
/// Ke = result[0:16], Km_raw = result[16:32], Km = SHA256(Km_raw)
struct EciesKeys {
    std::array<uint8_t, 16> enc_key{};  ///< AES-128 encryption key
    std::array<uint8_t, 32> mac_key{};  ///< HMAC key = SHA256(Km_raw)
};

EciesKeys derive_keys(const SharedSecret& shared_secret) noexcept {
    // Step 1: concatKDF — SHA256(0x00000001 || z) → 32 bytes
    uint8_t kdf_input[4 + kSharedSecretSize];
    kdf_input[0] = 0x00;
    kdf_input[1] = 0x00;
    kdf_input[2] = 0x00;
    kdf_input[3] = 0x01;
    std::memcpy(kdf_input + 4, shared_secret.data(), kSharedSecretSize);

    uint8_t kdf_out[32];
    EVP_Digest(kdf_input, sizeof(kdf_input), kdf_out, nullptr, EVP_sha256(), nullptr);

    EciesKeys keys;
    // Ke = first 16 bytes
    std::memcpy(keys.enc_key.data(), kdf_out, 16);
    // Km = SHA256(kdf_out[16:32])  — go-ethereum deriveKeys hashes Km before use
    EVP_Digest(kdf_out + 16, 16, keys.mac_key.data(), nullptr, EVP_sha256(), nullptr);
    return keys;
}

/// AES-128-CTR encrypt/decrypt (symmetric).
ByteBuffer aes128_ctr(ByteView data, const std::array<uint8_t, 16>& key, ByteView iv) noexcept {
    ByteBuffer out(data.size());
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) { return {}; }
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data());
    int len = 0;
    EVP_EncryptUpdate(ctx, out.data(), &len, data.data(), static_cast<int>(data.size()));
    EVP_CIPHER_CTX_free(ctx);
    out.resize(static_cast<size_t>(len));
    return out;
}

/// HMAC-SHA256(key, data) — key and data are arbitrary length.
std::array<uint8_t, 32> hmac_sha256(ByteView key, ByteView data) noexcept {
    std::array<uint8_t, 32> mac{};
    unsigned int mac_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         data.data(), data.size(), mac.data(), &mac_len);
    return mac;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

AuthResult<ByteBuffer> EciesCipher::encrypt(const EciesEncryptParams& params) noexcept {
    // Generate ephemeral keypair
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) { return AuthError::kEciesEncryptFailed; }

    PrivateKey ephemeral_priv{};
    for (;;) {
        RAND_bytes(ephemeral_priv.data(), static_cast<int>(ephemeral_priv.size()));
        if (secp256k1_ec_seckey_verify(ctx, ephemeral_priv.data())) { break; }
    }

    secp256k1_pubkey ephemeral_secp_pub;
    if (!secp256k1_ec_pubkey_create(ctx, &ephemeral_secp_pub, ephemeral_priv.data())) {
        ecies_log()->debug("encrypt: pubkey_create failed");
        secp256k1_context_destroy(ctx);
        return AuthError::kInvalidPublicKey;
    }

    // Serialize ephemeral public key (65 bytes uncompressed)
    std::array<uint8_t, kUncompressedPubKeySize> ephemeral_pub_bytes{};
    size_t pub_len = kUncompressedPubKeySize;
    secp256k1_ec_pubkey_serialize(ctx, ephemeral_pub_bytes.data(), &pub_len,
                                  &ephemeral_secp_pub, SECP256K1_EC_UNCOMPRESSED);
    secp256k1_context_destroy(ctx);

    // ECDH: shared secret = x-coordinate of (ephemeral_priv * recipient_pub)
    auto shared_result = compute_shared_secret(params.recipient_public_key, ephemeral_priv);
    if (!shared_result) {
        ecies_log()->debug("encrypt: compute_shared_secret failed (code {})",
                           static_cast<int>(shared_result.error()));
        return shared_result.error();
    }

    // Derive AES-128 + MAC-16 keys
    const auto keys = derive_keys(shared_result.value());

    // Generate random 16-byte IV
    std::array<uint8_t, kAesBlockSize> iv{};
    RAND_bytes(iv.data(), static_cast<int>(iv.size()));

    // AES-128-CTR encrypt
    const ByteBuffer ciphertext = aes128_ctr(params.plaintext, keys.enc_key, iv);

    // MAC: HMAC-SHA256(mac_key, iv || ciphertext || shared_mac_data)
    ByteBuffer mac_input;
    mac_input.reserve(iv.size() + ciphertext.size() + params.shared_mac_data.size());
    mac_input.insert(mac_input.end(), iv.begin(), iv.end());
    mac_input.insert(mac_input.end(), ciphertext.begin(), ciphertext.end());
    mac_input.insert(mac_input.end(), params.shared_mac_data.begin(), params.shared_mac_data.end());
    const auto mac = hmac_sha256(ByteView(keys.mac_key.data(), keys.mac_key.size()), mac_input);

    // Wire format: ephemeral_pub(65) || iv(16) || ciphertext(N) || mac(32)
    ByteBuffer result;
    result.reserve(ephemeral_pub_bytes.size() + iv.size() + ciphertext.size() + mac.size());
    result.insert(result.end(), ephemeral_pub_bytes.begin(), ephemeral_pub_bytes.end());
    result.insert(result.end(), iv.begin(), iv.end());
    result.insert(result.end(), ciphertext.begin(), ciphertext.end());
    result.insert(result.end(), mac.begin(), mac.end());
    return result;
}

AuthResult<ByteBuffer> EciesCipher::decrypt(const EciesDecryptParams& params) noexcept {
    // Wire: ephemeral_pub(65) || iv(16) || ciphertext(N) || mac(32)
    constexpr size_t kMinSize = kEciesOverheadSize;
    const size_t ciphertext_size = static_cast<size_t>(params.ciphertext.size());
    if (ciphertext_size < kMinSize) {
        return AuthError::kEciesDecryptFailed;
    }

    const ByteView ephemeral_pub_bytes = params.ciphertext.subspan(0, kUncompressedPubKeySize);
    const ByteView iv                  = params.ciphertext.subspan(kUncompressedPubKeySize, kAesBlockSize);
    const size_t   ct_offset           = kUncompressedPubKeySize + kAesBlockSize;
    const size_t   ct_len              = ciphertext_size - kMinSize;
    const ByteView ciphertext          = params.ciphertext.subspan(ct_offset, ct_len);
    const ByteView mac                 = params.ciphertext.subspan(ct_offset + ct_len, kEciesMacSize);

    // Parse ephemeral public key (skip 0x04 prefix)
    PublicKey ephemeral_pub{};
    std::memcpy(ephemeral_pub.data(),
                ephemeral_pub_bytes.data() + kUncompressedPubKeyPrefixSize,
                kPublicKeySize);

    // ECDH
    auto shared_result = compute_shared_secret(ephemeral_pub, params.recipient_private_key);
    if (!shared_result) { return shared_result.error(); }

    const auto keys = derive_keys(shared_result.value());

    // Verify MAC
    ByteBuffer mac_input;
    mac_input.reserve(iv.size() + ciphertext.size() + params.shared_mac_data.size());
    mac_input.insert(mac_input.end(), iv.begin(), iv.end());
    mac_input.insert(mac_input.end(), ciphertext.begin(), ciphertext.end());
    mac_input.insert(mac_input.end(), params.shared_mac_data.begin(), params.shared_mac_data.end());
    const auto expected_mac = hmac_sha256(ByteView(keys.mac_key.data(), keys.mac_key.size()), mac_input);

    if (std::memcmp(mac.data(), expected_mac.data(), kEciesMacSize) != 0) {
        ecies_log()->debug("decrypt: MAC mismatch");
        return AuthError::kEciesDecryptFailed;
    }

    return aes128_ctr(ciphertext, keys.enc_key, iv);
}

size_t EciesCipher::estimate_encrypted_size(size_t plaintext_size) noexcept {
    return kUncompressedPubKeySize + kAesBlockSize + plaintext_size + kEciesMacSize;
}

AuthResult<SharedSecret> EciesCipher::compute_shared_secret(
    gsl::span<const uint8_t, kPublicKeySize> public_key,
    gsl::span<const uint8_t, kPrivateKeySize> private_key
) noexcept {
    auto result = rlpx::crypto::Ecdh::compute_shared_secret(public_key, private_key);
    if (!result) { return AuthError::kSharedSecretFailed; }
    return result.value();
}

AesKey EciesCipher::derive_aes_key(ByteView /*shared_secret*/) noexcept {
    return AesKey{};  // Not used — derive_keys() handles both keys together
}

MacKey EciesCipher::derive_mac_key(ByteView /*shared_secret*/) noexcept {
    return MacKey{};  // Not used — derive_keys() handles both keys together
}

} // namespace rlpx::auth
