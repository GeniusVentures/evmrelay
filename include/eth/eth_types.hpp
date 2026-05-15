// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#include <rlp/intx.hpp>
#include <rlp/rlp_ethereum.hpp>
#include "objects.hpp"

namespace eth {

using Hash256 = rlp::Hash256;
using Address = rlp::Address;
using Bloom = rlp::Bloom;

inline constexpr uint8_t kEthProtocolVersion66 = 66U;
inline constexpr uint8_t kEthProtocolVersion67 = 67U;
inline constexpr uint8_t kEthProtocolVersion68 = 68U;
inline constexpr uint8_t kEthProtocolVersion69 = 69U;

struct ForkId {
    std::array<uint8_t, 4> fork_hash{};
    uint64_t next_fork = 0;
};

/// @brief ETH/68 Status message.
/// Wire: [version, networkid, td, blockhash, genesis, forkid]
struct StatusMessage68
{
    uint8_t       protocol_version = kEthProtocolVersion68;
    uint64_t      network_id = 0;
    intx::uint256 td{};
    Hash256       blockhash{};
    Hash256       genesis_hash{};
    ForkId        fork_id{};
};

/// @brief ETH/69 Status message.
/// Wire: [version, networkid, genesis, forkid, earliestBlock, latestBlock, latestBlockHash]
struct StatusMessage69
{
    uint8_t  protocol_version = kEthProtocolVersion69;
    uint64_t network_id = 0;
    Hash256  genesis_hash{};
    ForkId   fork_id{};
    uint64_t earliest_block = 0;
    uint64_t latest_block = 0;
    Hash256  latest_block_hash{};
};

/// @brief Fields common to both ETH/68 and ETH/69 Status messages.
struct CommonStatusFields
{
    uint8_t  protocol_version = 0;
    uint64_t network_id = 0;
    Hash256  genesis_hash{};
    ForkId   fork_id{};
};

/// @brief Dual-version Status message (ETH/68 or ETH/69).
using StatusMessage = std::variant<StatusMessage68, StatusMessage69>;

/// @brief Extract fields common to both ETH/68 and ETH/69 Status messages.
[[nodiscard]] CommonStatusFields get_common_fields(const StatusMessage& msg) noexcept;

/// @brief Errors returned by validate_status(), mirroring go-ethereum's
///        readStatus error values from eth/protocols/eth/handshake.go.
enum class StatusValidationError {
    kProtocolVersionMismatch,  ///< status.ProtocolVersion != negotiated version
    kNetworkIDMismatch,        ///< status.NetworkID != expected network ID
    kGenesisMismatch,          ///< status.Genesis != our genesis hash
    kInvalidBlockRange,        ///< status.EarliestBlock > status.LatestBlock
};

struct NewBlockHashEntry {
    Hash256 hash{};
    uint64_t number = 0;
};

struct NewBlockHashesMessage {
    std::vector<NewBlockHashEntry> entries;
};

struct NewPooledTransactionHashesMessage {
    std::vector<Hash256> hashes;
};

struct GetBlockHeadersMessage {
    std::optional<uint64_t> request_id;
    std::optional<Hash256> start_hash;
    std::optional<uint64_t> start_number;
    uint64_t max_headers = 0;
    uint64_t skip = 0;
    bool reverse = false;
};

struct BlockHeadersMessage {
    std::optional<uint64_t> request_id;
    std::vector<codec::BlockHeader> headers;
};

struct GetReceiptsMessage {
    std::optional<uint64_t> request_id;
    std::vector<Hash256> block_hashes;
};

struct ReceiptsMessage {
    std::optional<uint64_t> request_id;
    std::vector<std::vector<codec::Receipt>> receipts;
};

struct GetPooledTransactionsMessage {
    std::optional<uint64_t> request_id;
    std::vector<Hash256> transaction_hashes;
};

struct PooledTransactionsMessage {
    std::optional<uint64_t> request_id;
    std::vector<std::vector<uint8_t>> encoded_transactions;
};

/// @brief Request for block bodies by hash (message id 0x05).
struct GetBlockBodiesMessage {
    std::optional<uint64_t> request_id;
    std::vector<Hash256> block_hashes;
};

/// @brief A single block body: transactions + ommers (uncle headers).
struct BlockBody {
    std::vector<codec::Transaction> transactions;
    std::vector<codec::BlockHeader> ommers;
};

/// @brief Response to GetBlockBodies (message id 0x06).
struct BlockBodiesMessage {
    std::optional<uint64_t> request_id;
    std::vector<BlockBody> bodies;
};

/// @brief Full new block announcement (message id 0x07).
struct NewBlockMessage {
    codec::BlockHeader header;
    std::vector<codec::Transaction> transactions;
    std::vector<codec::BlockHeader> ommers;
    intx::uint256 total_difficulty{};
};

} // namespace eth

