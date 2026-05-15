// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"

namespace rlpx::crypto {

// HMAC-SHA256 operations
class Hmac {
public:
    Hmac() = delete;

    // Compute HMAC-SHA256 digest
    [[nodiscard]] static CryptoResult<ByteBuffer>
    compute(ByteView key, ByteView data) noexcept;

    // Compute HMAC-SHA256 and return fixed-size MAC (truncated to kMacSize)
    [[nodiscard]] static CryptoResult<MacDigest>
    compute_mac(ByteView key, ByteView data) noexcept;

    // Verify HMAC (constant-time comparison)
    [[nodiscard]] static bool
    verify(ByteView key, ByteView data, ByteView expected_mac) noexcept;
};

} // namespace rlpx::crypto
