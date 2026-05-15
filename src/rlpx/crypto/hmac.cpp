// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/crypto/hmac.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <cstring>

namespace rlpx::crypto {

CryptoResult<ByteBuffer> Hmac::compute(ByteView key, ByteView data) noexcept {
    if ( key.empty() ) {
        return CryptoError::kInvalidKeySize;
    }

    ByteBuffer result(EVP_MAX_MD_SIZE);
    unsigned int result_len = 0;

    const unsigned char* hmac_result = HMAC(
        EVP_sha256(),
        key.data(),
        static_cast<int>(key.size()),
        data.data(),
        data.size(),
        result.data(),
        &result_len
    );

    if ( !hmac_result || result_len == 0 ) {
        return CryptoError::kHmacFailed;
    }

    result.resize(result_len);
    return result;
}

CryptoResult<MacDigest> Hmac::compute_mac(ByteView key, ByteView data) noexcept {
    auto full_hmac_result = compute(key, data);
    if ( !full_hmac_result ) {
        return full_hmac_result.error();
    }
    const auto& full_hmac = full_hmac_result.value();
    
    if ( full_hmac.size() < kMacSize ) {
        return CryptoError::kHmacFailed;
    }
    
    MacDigest mac;
    std::memcpy(mac.data(), full_hmac.data(), kMacSize);
    return mac;
}

bool Hmac::verify(ByteView key, ByteView data, ByteView expected_mac) noexcept {
    auto computed_result = compute(key, data);
    if ( !computed_result ) {
        return false;
    }
    
    const auto& computed_mac = computed_result.value();
    const size_t expected_mac_size = static_cast<size_t>(expected_mac.size());

    // Truncate computed MAC to match expected_mac size (e.g., 16 bytes for MacDigest)
    // This matches the behavior of compute_mac which truncates to kMacSize
    if ( expected_mac_size > computed_mac.size() ) {
        return false;
    }
    
    // Constant-time comparison to prevent timing attacks
    int result = CRYPTO_memcmp(
        expected_mac.data(),
        computed_mac.data(),
        expected_mac_size
    );
    
    return result == 0;
}

} // namespace rlpx::crypto
