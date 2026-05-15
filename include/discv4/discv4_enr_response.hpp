// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <rlp/common.hpp>
#include <rlp/rlp_decoder.hpp>
#include <discv4/discv4_constants.hpp>

namespace discv4 {

/// @brief Fork identifier per EIP-2124.
///
/// Mirrors go-ethereum forkid.ID:
/// @code
///   type ID struct {
///       Hash [4]byte   // CRC32 checksum of genesis + applied fork blocks
///       Next uint64    // Next upcoming fork block/timestamp; 0 = none
///   }
/// @endcode
///
/// Wire encoding inside an ENR `eth` entry:
///   RLP(enrEntry) = RLP([ RLP([hash4, next]) ])   (outer list = enrEntry struct, inner = ForkId struct)
struct ForkId
{
    std::array<uint8_t, 4U> hash{};  ///< CRC32 checksum of genesis + applied fork blocks.
    uint64_t                next{};  ///< Block/timestamp of the next upcoming fork; 0 = none.

    bool operator==( const ForkId& other ) const noexcept
    {
        return hash == other.hash && next == other.next;
    }
};

/// @brief Parsed ENRResponse — discv4 wire type 0x06 (EIP-868).
///
/// Mirrors go-ethereum v4wire.ENRResponse:
/// @code
///   ENRResponse struct {
///       ReplyTok []byte   // Hash of the ENRRequest packet.
///       Record   enr.Record
///       Rest []rlp.RawValue `rlp:"tail"`
///   }
/// @endcode
///
/// Wire layout (incoming):
///   hash(32) || sig(65) || type(1) || RLP([reply_tok(32), record_rlp])
struct discv4_enr_response
{
    /// Hash of the originating ENRRequest packet (the ReplyTok field).
    std::array<uint8_t, kWireHashSize> request_hash{};

    /// Raw RLP bytes of the remote ENR record (EIP-778), including the RLP list header.
    /// Format: RLP([signature, seq, key0, val0, key1, val1, ...])
    std::vector<uint8_t> record_rlp{};

    /// @brief Parse a full discv4 wire packet into a discv4_enr_response.
    ///
    /// @param raw  Full wire bytes: hash(32) || sig(65) || type(1) || RLP_payload.
    /// @return Parsed response on success, decoding error on failure.
    [[nodiscard]] static rlp::Result<discv4_enr_response> Parse( rlp::ByteView raw ) noexcept;

    /// @brief Extract the ForkId from the `eth` ENR entry in record_rlp.
    ///
    /// ENR record (EIP-778): RLP([signature, seq, key0, val0, key1, val1, ...])
    /// `eth` value wire encoding: RLP(enrEntry) = RLP([ [hash4, next_uint64] ])
    ///
    /// @return ForkId if the `eth` entry is present and well-formed, error otherwise.
    [[nodiscard]] rlp::Result<ForkId> ParseEthForkId() const noexcept;
};

} // namespace discv4

