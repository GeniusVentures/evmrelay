// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <vector>

namespace discv4 {

/// @brief ENRRequest packet — discv4 wire type 0x05 (EIP-868).
///
/// Mirrors go-ethereum v4wire.ENRRequest:
/// @code
///   ENRRequest struct {
///       Expiration uint64
///       Rest []rlp.RawValue `rlp:"tail"`
///   }
/// @endcode
///
/// Wire layout (after signing):
///   hash(32) || sig(65) || packet-type(1) || RLP([expiration])
struct discv4_enr_request
{
    uint64_t expiration = 0U;  ///< Unix timestamp after which the packet expires.

    /// @brief Encode as packet-type byte || RLP([expiration]), ready for signing.
    ///
    /// Returns an empty vector on encoding failure (mirrors discv4_ping::RlpPayload()).
    ///
    /// @return Encoded bytes.
    [[nodiscard]] std::vector<uint8_t> RlpPayload() const noexcept;
};

} // namespace discv4

