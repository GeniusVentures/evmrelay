// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <eth/objects.hpp>
#include <rlp/result.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace eth::abi {

// ---------------------------------------------------------------------------
// ABI value types
// ---------------------------------------------------------------------------

/// @brief A decoded ABI value.  Only the types needed for common ERC-20/ERC-721
///        events are included: address, uint256, bytes32, bool, bytes, string.
using AbiValue = std::variant<
    codec::Address,       ///< address   (20 bytes)
    intx::uint256,        ///< uint256 / uint<N> widened to 256 bits
    codec::Hash256,       ///< bytes32
    bool,                 ///< bool
    codec::ByteBuffer,    ///< bytes  (dynamic)
    std::string           ///< string (dynamic)
>;

// ---------------------------------------------------------------------------
// ABI parameter descriptor
// ---------------------------------------------------------------------------

/// @brief Identifies one parameter in an event/function signature.
enum class AbiParamKind : uint8_t
{
    kAddress,  ///< address
    kUint,     ///< uint<N> — decoded as uint256
    kInt,      ///< int<N>  — decoded as uint256 (sign handling left to caller)
    kBytes32,  ///< bytesN where N == 32; also covers bytes1-bytes32 zero-padded
    kBool,     ///< bool
    kBytes,    ///< bytes  (dynamic)
    kString,   ///< string (dynamic)
};

struct AbiParam
{
    AbiParamKind kind = AbiParamKind::kUint;
    bool         indexed = false;   ///< true → value appears in topics
    std::string  name;              ///< optional parameter name
};

// ---------------------------------------------------------------------------
// Event signature utilities
// ---------------------------------------------------------------------------

/// @brief Compute Keccak-256 of an arbitrary byte sequence.
/// @param data  Input bytes.
/// @return 32-byte hash.
[[nodiscard]] codec::Hash256 keccak256(const uint8_t* data, size_t len) noexcept;

/// @brief Compute Keccak-256 of a string (e.g. an event signature).
[[nodiscard]] codec::Hash256 keccak256(const std::string& text) noexcept;

/// @brief Compute the event topic[0] hash from a human-readable signature.
/// @param signature  e.g. "Transfer(address,address,uint256)"
/// @return 32-byte Keccak-256 of the canonical signature string.
[[nodiscard]] codec::Hash256 event_signature_hash(const std::string& signature) noexcept;

// ---------------------------------------------------------------------------
// ABI decoding
// ---------------------------------------------------------------------------

/// @brief Decode a single 32-byte ABI word from a topic or head slot.
///
/// - kAddress  → reads rightmost 20 bytes
/// - kUint     → reads as big-endian uint256
/// - kInt      → reads as big-endian uint256 (raw bits, no sign extension)
/// - kBytes32  → returns the 32 bytes directly
/// - kBool     → reads last byte, non-zero = true
///
/// @param word  Exactly 32 bytes (ABI head slot or topic).
/// @param kind  Expected parameter type.
/// @return Decoded value or error.
[[nodiscard]] rlp::Result<AbiValue> decode_abi_word(
    const codec::Hash256& word,
    AbiParamKind          kind) noexcept;

/// @brief Decode an indexed event parameter from a topic hash.
///
/// For value types (address, uint, bool, bytes32) the topic IS the
/// 32-byte ABI-encoded value.  Dynamic types (bytes, string) are hashed
/// and cannot be recovered — an empty ByteBuffer / string is returned.
///
/// @param topic  The 32-byte topic value.
/// @param param  Parameter descriptor (kind, name).
[[nodiscard]] rlp::Result<AbiValue> decode_indexed_param(
    const codec::Hash256& topic,
    const AbiParam&       param) noexcept;

/// @brief Decode the ABI-encoded `data` field of a log entry.
///
/// Decodes the non-indexed parameters from the log's data field following
/// the standard ABI head/tail encoding (EIP-838 / Solidity ABI spec).
///
/// @param data    Raw bytes from LogEntry::data.
/// @param params  Ordered list of non-indexed parameters to decode.
/// @return Vector of decoded values in the same order as `params`, or error.
[[nodiscard]] rlp::Result<std::vector<AbiValue>> decode_log_data(
    const codec::ByteBuffer&    data,
    const std::vector<AbiParam>& params) noexcept;

/// @brief Fully decode a log entry given its event descriptor.
///
/// Combines topic decoding (indexed params) and data decoding (non-indexed).
///
/// @param log          The raw log entry.
/// @param signature    Human-readable event signature, e.g.
///                     "Transfer(address,address,uint256)".
/// @param params       Full ordered parameter list (indexed + non-indexed).
///                     topic[0] is implicitly the signature hash and is not
///                     listed here.
/// @return Decoded values in declaration order (indexed first, then data),
///         or error.
[[nodiscard]] rlp::Result<std::vector<AbiValue>> decode_log(
    const codec::LogEntry&       log,
    const std::string&           signature,
    const std::vector<AbiParam>& params) noexcept;

} // namespace eth::abi

