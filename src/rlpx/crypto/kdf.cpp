// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/crypto/kdf.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <cstring>

namespace rlpx::crypto {

// NIST SP 800-56C Concatenation KDF using SHA-256
CryptoResult<ByteBuffer> Kdf::derive(
    ByteView shared_secret,
    size_t key_data_len,
    ByteView shared_info
) noexcept {
    if ( shared_secret.empty() || key_data_len == 0 ) {
        return CryptoError::kKdfFailed;
    }

    ByteBuffer output;
    output.reserve(key_data_len);

    // Counter starts at 1 (big-endian 32-bit = 4 bytes)
    static constexpr size_t   kCounterSize    = sizeof(uint32_t);
    static constexpr uint32_t kMaxIterations  = 1000U; ///< Prevents infinite loop on pathological inputs
    uint32_t counter = 1;
    size_t hash_len = SHA256_DIGEST_LENGTH;
    
    while ( output.size() < key_data_len ) {
        // Create hash input: counter || shared_secret || shared_info
        ByteBuffer hash_input;
        hash_input.reserve(kCounterSize + shared_secret.size() + shared_info.size());

        // Add counter (big-endian)
        hash_input.push_back(static_cast<uint8_t>((counter >> 24U) & 0xFFU));
        hash_input.push_back(static_cast<uint8_t>((counter >> 16U) & 0xFFU));
        hash_input.push_back(static_cast<uint8_t>((counter >>  8U) & 0xFFU));
        hash_input.push_back(static_cast<uint8_t>( counter         & 0xFFU));

        // Add shared secret
        hash_input.insert(hash_input.end(), shared_secret.begin(), shared_secret.end());
        
        // Add shared info if provided
        if ( !shared_info.empty() ) {
            hash_input.insert(hash_input.end(), shared_info.begin(), shared_info.end());
        }
        
        // Compute SHA-256
        std::array<uint8_t, SHA256_DIGEST_LENGTH> digest;
        if ( !SHA256(hash_input.data(), hash_input.size(), digest.data()) ) {
            return CryptoError::kKdfFailed;
        }
        
        // Append to output (truncate if needed)
        size_t bytes_to_copy = std::min(hash_len, key_data_len - output.size());
        output.insert(output.end(), digest.begin(), digest.begin() + bytes_to_copy);
        
        counter++;
        
        // Prevent infinite loop on pathological inputs
        if ( counter > kMaxIterations ) {
            return CryptoError::kKdfFailed;
        }
    }
    
    return output;
}

CryptoResult<AesKey> Kdf::derive_aes_key(ByteView shared_secret, ByteView info) noexcept {
    auto key_data_result = derive(shared_secret, kAesKeySize, info);
    if (!key_data_result) {
        return key_data_result.error();
    }
    auto key_data = std::move(key_data_result.value());
    
    if ( key_data.size() != kAesKeySize ) {
        return CryptoError::kInvalidKeySize;
    }
    
    AesKey key;
    std::memcpy(key.data(), key_data.data(), kAesKeySize);
    return key;
}

CryptoResult<MacKey> Kdf::derive_mac_key(ByteView shared_secret, ByteView info) noexcept {
    auto key_data_result = derive(shared_secret, kMacKeySize, info);
    if (!key_data_result) {
        return key_data_result.error();
    }
    auto key_data = std::move(key_data_result.value());
    
    if ( key_data.size() != kMacKeySize ) {
        return CryptoError::kInvalidKeySize;
    }
    
    MacKey key;
    std::memcpy(key.data(), key_data.data(), kMacKeySize);
    return key;
}

CryptoResult<Kdf::DerivedKeys> Kdf::derive_keys(ByteView shared_secret, ByteView info) noexcept {
    const size_t total_len = kAesKeySize + kMacKeySize;
    auto key_data_result = derive(shared_secret, total_len, info);
    if (!key_data_result) {
        return key_data_result.error();
    }
    auto key_data = std::move(key_data_result.value());
    
    if ( key_data.size() != total_len ) {
        return CryptoError::kKdfFailed;
    }
    
    DerivedKeys keys;
    std::memcpy(keys.aes_key.data(), key_data.data(), kAesKeySize);
    std::memcpy(keys.mac_key.data(), key_data.data() + kAesKeySize, kMacKeySize);
    
    return keys;
}

} // namespace rlpx::crypto
