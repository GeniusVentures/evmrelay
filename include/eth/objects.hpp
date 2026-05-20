// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_OBJECTS_HPP
#define EVMRELAY_INCLUDE_ETH_OBJECTS_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <rlp/intx.hpp>
#include <rlp/rlp_decoder.hpp>
#include <rlp/rlp_encoder.hpp>
#include <rlp/rlp_ethereum.hpp>

namespace eth::codec {

using Hash256 = rlp::Hash256;
using Address = rlp::Address;
using Bloom = rlp::Bloom;
using ByteBuffer = std::vector<uint8_t>;

/// @brief EVM log entry emitted by a CALL or CREATE instruction.
struct LogEntry
{
    Address address{};
    std::vector<Hash256> topics;
    ByteBuffer data;
};

/// @brief Ethereum transaction types per EIP-2718.
enum class TransactionType : uint8_t
{
    kLegacy   = 0x00, ///< Pre-EIP-2718 (no type prefix)
    kAccessList = 0x01, ///< EIP-2930
    kDynamicFee = 0x02  ///< EIP-1559
};

/// @brief An EIP-2930 access list entry: contract address + storage slots.
struct AccessListEntry
{
    Address address{};
    std::vector<Hash256> storage_keys;
};

/// @brief Full Ethereum transaction covering legacy, EIP-2930 and EIP-1559 formats.
struct Transaction
{
    TransactionType type = TransactionType::kLegacy;

    // Common fields (all types)
    uint64_t nonce = 0;
    uint64_t gas_limit = 0;
    std::optional<Address> to;   ///< Empty for contract creation
    intx::uint256 value{};
    ByteBuffer data;

    // Legacy / EIP-2930 gas price
    std::optional<intx::uint256> gas_price;

    // EIP-1559 fee fields
    std::optional<intx::uint256> max_priority_fee_per_gas;
    std::optional<intx::uint256> max_fee_per_gas;

    // EIP-2930 / EIP-1559 access list
    std::vector<AccessListEntry> access_list;

    // Chain-id (present in EIP-2930 and EIP-1559; replicated by v in legacy)
    std::optional<uint64_t> chain_id;

    // ECDSA signature
    intx::uint256 v{};
    intx::uint256 r{};
    intx::uint256 s{};
};

struct Receipt {
    std::optional<Hash256> state_root;
    std::optional<bool> status;
    intx::uint256 cumulative_gas_used{};
    Bloom bloom{};
    std::vector<LogEntry> logs;
};

struct BlockHeader {
    Hash256 parent_hash{};
    Hash256 ommers_hash{};
    Address beneficiary{};
    Hash256 state_root{};
    Hash256 transactions_root{};
    Hash256 receipts_root{};
    Bloom logs_bloom{};
    intx::uint256 difficulty{};
    uint64_t number = 0;
    uint64_t gas_limit = 0;
    uint64_t gas_used = 0;
    uint64_t timestamp = 0;
    ByteBuffer extra_data;
    Hash256 mix_hash{};
    std::array<uint8_t, 8> nonce{};
    std::optional<intx::uint256> base_fee_per_gas;
};

using EncodeResult = rlp::EncodingResult<ByteBuffer>;

template <typename T>
using DecodeResult = rlp::Result<T>;

[[nodiscard]] EncodeResult encode_log_entry(const LogEntry& entry) noexcept;
[[nodiscard]] DecodeResult<LogEntry> decode_log_entry(rlp::ByteView rlp_data) noexcept;

[[nodiscard]] EncodeResult encode_access_list_entry(const AccessListEntry& entry) noexcept;
[[nodiscard]] DecodeResult<AccessListEntry> decode_access_list_entry(rlp::ByteView rlp_data) noexcept;

/// @brief Encode a transaction.  Typed transactions (EIP-2930, EIP-1559)
///        are prefixed with their type byte before the RLP payload, per EIP-2718.
[[nodiscard]] EncodeResult encode_transaction(const Transaction& tx) noexcept;
[[nodiscard]] DecodeResult<Transaction> decode_transaction(rlp::ByteView raw_data) noexcept;

[[nodiscard]] EncodeResult encode_receipt(const Receipt& receipt) noexcept;
[[nodiscard]] DecodeResult<Receipt> decode_receipt(rlp::ByteView rlp_data) noexcept;

[[nodiscard]] EncodeResult encode_block_header(const BlockHeader& header) noexcept;
[[nodiscard]] DecodeResult<BlockHeader> decode_block_header(rlp::ByteView rlp_data) noexcept;

} // namespace eth::codec


#endif // EVMRELAY_INCLUDE_ETH_OBJECTS_HPP
