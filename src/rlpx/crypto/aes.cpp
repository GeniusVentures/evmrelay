// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/crypto/aes.hpp>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <cstring>

namespace rlpx::crypto {

CryptoResult<ByteBuffer> Aes::encrypt_ctr(
    gsl::span<const uint8_t, kAesKeySize> key,
    gsl::span<const uint8_t, kAesBlockSize> iv,
    ByteView plaintext
) noexcept {
    ByteBuffer ciphertext(plaintext.size());
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if ( !ctx ) {
        return CryptoError::kAesEncryptFailed;
    }

    // Initialize encryption with AES-256-CTR
    if ( EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesEncryptFailed;
    }

    int len = 0;
    int ciphertext_len = 0;

    // Encrypt plaintext
    if ( EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), 
                          static_cast<int>(plaintext.size())) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesEncryptFailed;
    }
    ciphertext_len = len;

    // Finalize encryption (CTR mode doesn't add padding)
    if ( EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesEncryptFailed;
    }
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    
    ciphertext.resize(ciphertext_len);
    return ciphertext;
}

CryptoResult<ByteBuffer> Aes::decrypt_ctr(
    gsl::span<const uint8_t, kAesKeySize> key,
    gsl::span<const uint8_t, kAesBlockSize> iv,
    ByteView ciphertext
) noexcept {
    ByteBuffer plaintext(ciphertext.size());
    
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if ( !ctx ) {
        return CryptoError::kAesDecryptFailed;
    }

    // Initialize decryption with AES-256-CTR
    if ( EVP_DecryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesDecryptFailed;
    }

    int len = 0;
    int plaintext_len = 0;

    // Decrypt ciphertext
    if ( EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(),
                          static_cast<int>(ciphertext.size())) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesDecryptFailed;
    }
    plaintext_len = len;

    // Finalize decryption
    if ( EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesDecryptFailed;
    }
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    
    plaintext.resize(plaintext_len);
    return plaintext;
}

CryptoResult<void> Aes::encrypt_ctr_inplace(
    gsl::span<const uint8_t, kAesKeySize> key,
    gsl::span<const uint8_t, kAesBlockSize> iv,
    MutableByteView data
) noexcept {
    if ( data.empty() ) {
        return outcome::success();
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if ( !ctx ) {
        return CryptoError::kAesEncryptFailed;
    }

    // Initialize encryption
    if ( EVP_EncryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesEncryptFailed;
    }

    int len = 0;
    
    // Encrypt in place
    if ( EVP_EncryptUpdate(ctx, data.data(), &len, data.data(),
                          static_cast<int>(data.size())) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesEncryptFailed;
    }

    // Finalize
    if ( EVP_EncryptFinal_ex(ctx, data.data() + len, &len) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesEncryptFailed;
    }

    EVP_CIPHER_CTX_free(ctx);
    return outcome::success();
}

CryptoResult<void> Aes::decrypt_ctr_inplace(
    gsl::span<const uint8_t, kAesKeySize> key,
    gsl::span<const uint8_t, kAesBlockSize> iv,
    MutableByteView data
) noexcept {
    if ( data.empty() ) {
        return outcome::success();
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if ( !ctx ) {
        return CryptoError::kAesDecryptFailed;
    }

    // Initialize decryption
    if ( EVP_DecryptInit_ex(ctx, EVP_aes_256_ctr(), nullptr, key.data(), iv.data()) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesDecryptFailed;
    }

    int len = 0;
    
    // Decrypt in place
    if ( EVP_DecryptUpdate(ctx, data.data(), &len, data.data(),
                          static_cast<int>(data.size())) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesDecryptFailed;
    }

    // Finalize
    if ( EVP_DecryptFinal_ex(ctx, data.data() + len, &len) != 1 ) {
        EVP_CIPHER_CTX_free(ctx);
        return CryptoError::kAesDecryptFailed;
    }

    EVP_CIPHER_CTX_free(ctx);
    return outcome::success();
}

} // namespace rlpx::crypto
