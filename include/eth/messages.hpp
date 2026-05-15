// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "eth_types.hpp"
#include <rlp/rlp_encoder.hpp>
#include <rlp/rlp_decoder.hpp>
#include <chrono>
#include <vector>

namespace eth::protocol {

inline constexpr uint8_t kStatusMessageId = 0x00;

/// @brief Maximum time to wait for a peer's ETH Status after sending ours.
/// Matches go-ethereum's handshakeTimeout (eth/protocols/eth/handshake.go).
/// If the peer does not reply within this window it is dropped as malicious
/// or mismatched (e.g. a Polygon bor node on the Ethereum P2P network).
inline constexpr std::chrono::seconds kStatusHandshakeTimeout{5};
inline constexpr uint8_t kNewBlockHashesMessageId = 0x01;
inline constexpr uint8_t kTransactionsMessageId = 0x02;
inline constexpr uint8_t kGetBlockHeadersMessageId = 0x03;
inline constexpr uint8_t kBlockHeadersMessageId = 0x04;
inline constexpr uint8_t kGetBlockBodiesMessageId = 0x05;
inline constexpr uint8_t kBlockBodiesMessageId = 0x06;
inline constexpr uint8_t kNewBlockMessageId = 0x07;
inline constexpr uint8_t kNewPooledTransactionHashesMessageId = 0x08;
inline constexpr uint8_t kGetPooledTransactionsMessageId = 0x09;
inline constexpr uint8_t kPooledTransactionsMessageId = 0x0a;
inline constexpr uint8_t kGetReceiptsMessageId = 0x0f;
inline constexpr uint8_t kReceiptsMessageId = 0x10;

using ByteBuffer = std::vector<uint8_t>;
using EncodeResult = rlp::EncodingResult<ByteBuffer>;

template <typename T>
using DecodeResult = rlp::Result<T>;

/// @brief Result type for Status validation (void on success, error on mismatch).
///        Uses boost::outcome with StatusValidationError — mirrors go-ethereum's
///        readStatus() return values from eth/protocols/eth/handshake.go.
using ValidationResult = rlp::outcome::result<void, eth::StatusValidationError,
                                               rlp::outcome::policy::all_narrow>;

/// @brief Validate a decoded StatusMessage against our expected chain parameters.
///
/// Mirrors go-ethereum's readStatus() checks (handshake.go):
///   - NetworkID must match @p expected_network_id
///   - Genesis must match @p expected_genesis
///   - For ETH/69: EarliestBlock must be <= LatestBlock (when LatestBlock != 0)
///
/// @param msg                  The decoded peer Status message (variant).
/// @param expected_network_id  Our chain's network ID.
/// @param expected_genesis     Our chain's genesis block hash.
/// @return Success, or the first validation error encountered.
[[nodiscard]] ValidationResult validate_status(
    const eth::StatusMessage& msg,
    uint64_t                  expected_network_id,
    const eth::Hash256&       expected_genesis) noexcept;

// STATUS
[[nodiscard]] EncodeResult encode_status(const StatusMessage& msg) noexcept;
[[nodiscard]] DecodeResult<StatusMessage> decode_status(rlp::ByteView rlp_data) noexcept;

// NEW_BLOCK_HASHES
[[nodiscard]] EncodeResult encode_new_block_hashes(const NewBlockHashesMessage& msg) noexcept;
[[nodiscard]] DecodeResult<NewBlockHashesMessage> decode_new_block_hashes(rlp::ByteView rlp_data) noexcept;

// NEW_POOLED_TRANSACTION_HASHES
[[nodiscard]] EncodeResult encode_new_pooled_tx_hashes(const NewPooledTransactionHashesMessage& msg) noexcept;
[[nodiscard]] DecodeResult<NewPooledTransactionHashesMessage> decode_new_pooled_tx_hashes(rlp::ByteView rlp_data) noexcept;

// GET_BLOCK_HEADERS
[[nodiscard]] EncodeResult encode_get_block_headers(const GetBlockHeadersMessage& msg) noexcept;
[[nodiscard]] DecodeResult<GetBlockHeadersMessage> decode_get_block_headers(rlp::ByteView rlp_data) noexcept;

// BLOCK_HEADERS
[[nodiscard]] EncodeResult encode_block_headers(const BlockHeadersMessage& msg) noexcept;
[[nodiscard]] DecodeResult<BlockHeadersMessage> decode_block_headers(rlp::ByteView rlp_data) noexcept;

// GET_BLOCK_BODIES
[[nodiscard]] EncodeResult encode_get_block_bodies(const GetBlockBodiesMessage& msg) noexcept;
[[nodiscard]] DecodeResult<GetBlockBodiesMessage> decode_get_block_bodies(rlp::ByteView rlp_data) noexcept;

// BLOCK_BODIES
[[nodiscard]] EncodeResult encode_block_bodies(const BlockBodiesMessage& msg) noexcept;
[[nodiscard]] DecodeResult<BlockBodiesMessage> decode_block_bodies(rlp::ByteView rlp_data) noexcept;

// NEW_BLOCK
[[nodiscard]] EncodeResult encode_new_block(const NewBlockMessage& msg) noexcept;
[[nodiscard]] DecodeResult<NewBlockMessage> decode_new_block(rlp::ByteView rlp_data) noexcept;

// GET_RECEIPTS
[[nodiscard]] EncodeResult encode_get_receipts(const GetReceiptsMessage& msg) noexcept;
[[nodiscard]] DecodeResult<GetReceiptsMessage> decode_get_receipts(rlp::ByteView rlp_data) noexcept;

// RECEIPTS
[[nodiscard]] EncodeResult encode_receipts(const ReceiptsMessage& msg) noexcept;
[[nodiscard]] DecodeResult<ReceiptsMessage> decode_receipts(rlp::ByteView rlp_data) noexcept;

// GET_POOLED_TRANSACTIONS
[[nodiscard]] EncodeResult encode_get_pooled_transactions(const GetPooledTransactionsMessage& msg) noexcept;
[[nodiscard]] DecodeResult<GetPooledTransactionsMessage> decode_get_pooled_transactions(rlp::ByteView rlp_data) noexcept;

// POOLED_TRANSACTIONS
[[nodiscard]] EncodeResult encode_pooled_transactions(const PooledTransactionsMessage& msg) noexcept;
[[nodiscard]] DecodeResult<PooledTransactionsMessage> decode_pooled_transactions(rlp::ByteView rlp_data) noexcept;

} // namespace eth::protocol
