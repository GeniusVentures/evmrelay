// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"

namespace rlpx::crypto {

// AES-256-CTR mode encryption/decryption
class Aes {
public:
    Aes() = delete;

    // Encrypt data using AES-256-CTR
    // Parameters:
    //   key: 256-bit AES key
    //   iv: 128-bit initialization vector (counter)
    //   plaintext: Data to encrypt
    [[nodiscard]] static CryptoResult<ByteBuffer>
    encrypt_ctr(
        gsl::span<const uint8_t, kAesKeySize> key,
        gsl::span<const uint8_t, kAesBlockSize> iv,
        ByteView plaintext
    ) noexcept;

    // Decrypt data using AES-256-CTR
    [[nodiscard]] static CryptoResult<ByteBuffer>
    decrypt_ctr(
        gsl::span<const uint8_t, kAesKeySize> key,
        gsl::span<const uint8_t, kAesBlockSize> iv,
        ByteView ciphertext
    ) noexcept;

    // In-place encryption (more efficient, reuses buffer)
    [[nodiscard]] static CryptoResult<void>
    encrypt_ctr_inplace(
        gsl::span<const uint8_t, kAesKeySize> key,
        gsl::span<const uint8_t, kAesBlockSize> iv,
        MutableByteView data
    ) noexcept;

    // In-place decryption
    [[nodiscard]] static CryptoResult<void>
    decrypt_ctr_inplace(
        gsl::span<const uint8_t, kAesKeySize> key,
        gsl::span<const uint8_t, kAesBlockSize> iv,
        MutableByteView data
    ) noexcept;
};

} // namespace rlpx::crypto
