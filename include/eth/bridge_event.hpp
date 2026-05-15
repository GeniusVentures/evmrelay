// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_BRIDGE_EVENT_HPP
#define EVMRELAY_INCLUDE_ETH_BRIDGE_EVENT_HPP

#include <eth/eth_receipt_source.hpp>
#include <set>

namespace eth {

/// @brief Transport-neutral bridge event claim signed by watcher nodes.
struct BridgeEventClaim
{
    uint64_t src_chain_id = 0;
    uint64_t dest_chain_id = 0;

    uint64_t block_number = 0;
    Hash256  block_hash{};
    Hash256  tx_hash{};
    uint32_t log_index = 0;

    Address                bridge_contract{};
    Hash256                event_topic0{};
    std::vector<Hash256>   topics;
    codec::ByteBuffer      data;

    Address       sender{};
    intx::uint256 token_id_or_nonce{};
    intx::uint256 amount{};
    Address       recipient{};

    uint64_t observed_at = 0;
    uint64_t finality_depth = 0;
};

/// @brief Watcher signature over a normalized bridge event claim.
struct BridgeEventObservation
{
    BridgeEventClaim  claim;
    Address           observer{};
    codec::ByteBuffer signature;
};

/// @brief Dedupe/consumption key for a bridge event.
struct BridgeEventKey
{
    uint64_t src_chain_id = 0;
    Hash256  tx_hash{};
    uint32_t log_index = 0;
};

[[nodiscard]] bool operator==(const BridgeEventKey& lhs, const BridgeEventKey& rhs) noexcept;
[[nodiscard]] bool operator<(const BridgeEventKey& lhs, const BridgeEventKey& rhs) noexcept;
[[nodiscard]] BridgeEventKey bridge_event_key(const BridgeEventClaim& claim) noexcept;

/// @brief In-memory event deduper keyed by source chain, transaction hash, and log index.
class EventDeduper
{
public:
    [[nodiscard]] bool contains(const BridgeEventKey& key) const;
    [[nodiscard]] bool mark_seen(const BridgeEventKey& key);
    [[nodiscard]] size_t size() const noexcept;
    void clear() noexcept;

private:
    std::set<BridgeEventKey> seen_;
};

enum class ReceiptLogVerificationError
{
    kNone,
    kMissingReceiptStatus,
    kReceiptFailed,
    kBlockHashMismatch,
    kTxHashMismatch,
    kLogIndexOutOfRange,
    kContractMismatch,
    kTopic0Mismatch,
    kTopicsMismatch,
    kDataMismatch,
};

struct ReceiptLogVerificationResult
{
    ReceiptLogVerificationError error = ReceiptLogVerificationError::kNone;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return error == ReceiptLogVerificationError::kNone;
    }
};

[[nodiscard]] ReceiptLogVerificationResult verify_receipt_log(
    const ReceiptResult&     receipt,
    const BridgeEventClaim&  claim) noexcept;

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_BRIDGE_EVENT_HPP
