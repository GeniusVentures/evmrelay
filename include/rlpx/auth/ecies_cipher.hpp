// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_RLPX_AUTH_ECIES_CIPHER_HPP
#define EVMRELAY_INCLUDE_RLPX_AUTH_ECIES_CIPHER_HPP

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"

namespace rlpx::auth {

// ECIES encryption parameters
struct EciesEncryptParams {
    ByteView plaintext;
    gsl::span<const uint8_t, kPublicKeySize> recipient_public_key;
    ByteView shared_mac_data;
};

// ECIES decryption parameters
struct EciesDecryptParams {
    ByteView ciphertext;
    gsl::span<const uint8_t, kPrivateKeySize> recipient_private_key;
    ByteView shared_mac_data;
};

// ECIES cipher implementation using OpenSSL
class EciesCipher {
public:
    // No shared state - all static
    EciesCipher() = delete;

    // Encrypt plaintext for recipient's public key
    [[nodiscard]] static AuthResult<ByteBuffer>
    encrypt(const EciesEncryptParams& params) noexcept;

    // Decrypt ciphertext with recipient's private key
    [[nodiscard]] static AuthResult<ByteBuffer>
    decrypt(const EciesDecryptParams& params) noexcept;

    // Estimate encrypted size for buffer allocation
    [[nodiscard]] static size_t
    estimate_encrypted_size(size_t plaintext_size) noexcept;

private:
    // ECDH shared secret computation
    [[nodiscard]] static AuthResult<SharedSecret>
    compute_shared_secret(
        gsl::span<const uint8_t, kPublicKeySize> public_key,
        gsl::span<const uint8_t, kPrivateKeySize> private_key
    ) noexcept;

    // NIST SP 800-56 Concatenation KDF
    [[nodiscard]] static AesKey derive_aes_key(ByteView shared_secret) noexcept;
    [[nodiscard]] static MacKey derive_mac_key(ByteView shared_secret) noexcept;
};

} // namespace rlpx::auth

#endif // EVMRELAY_INCLUDE_RLPX_AUTH_ECIES_CIPHER_HPP
