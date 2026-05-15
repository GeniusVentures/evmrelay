// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/bridge_event.hpp>
#include <algorithm>

namespace eth {

bool operator==(const BridgeEventKey& lhs, const BridgeEventKey& rhs) noexcept
{
    return lhs.src_chain_id == rhs.src_chain_id
        && lhs.tx_hash == rhs.tx_hash
        && lhs.log_index == rhs.log_index;
}

bool operator<(const BridgeEventKey& lhs, const BridgeEventKey& rhs) noexcept
{
    if (lhs.src_chain_id != rhs.src_chain_id)
    {
        return lhs.src_chain_id < rhs.src_chain_id;
    }
    if (lhs.tx_hash != rhs.tx_hash)
    {
        return std::lexicographical_compare(
            lhs.tx_hash.begin(), lhs.tx_hash.end(),
            rhs.tx_hash.begin(), rhs.tx_hash.end());
    }
    return lhs.log_index < rhs.log_index;
}

BridgeEventKey bridge_event_key(const BridgeEventClaim& claim) noexcept
{
    return BridgeEventKey{
        claim.src_chain_id,
        claim.tx_hash,
        claim.log_index,
    };
}

bool EventDeduper::contains(const BridgeEventKey& key) const
{
    return seen_.find(key) != seen_.end();
}

bool EventDeduper::mark_seen(const BridgeEventKey& key)
{
    return seen_.insert(key).second;
}

size_t EventDeduper::size() const noexcept
{
    return seen_.size();
}

void EventDeduper::clear() noexcept
{
    seen_.clear();
}

ReceiptLogVerificationResult verify_receipt_log(
    const ReceiptResult&    receipt,
    const BridgeEventClaim& claim) noexcept
{
    if (!receipt.receipt.status.has_value())
    {
        return {ReceiptLogVerificationError::kMissingReceiptStatus};
    }
    if (!*receipt.receipt.status)
    {
        return {ReceiptLogVerificationError::kReceiptFailed};
    }
    if (receipt.block_hash != claim.block_hash)
    {
        return {ReceiptLogVerificationError::kBlockHashMismatch};
    }
    if (receipt.tx_hash != claim.tx_hash)
    {
        return {ReceiptLogVerificationError::kTxHashMismatch};
    }
    size_t receipt_log_index = claim.log_index;
    if (!receipt.log_indices.empty())
    {
        const auto it = std::find(
            receipt.log_indices.begin(),
            receipt.log_indices.end(),
            claim.log_index);
        if (it == receipt.log_indices.end())
        {
            return {ReceiptLogVerificationError::kLogIndexOutOfRange};
        }
        receipt_log_index = static_cast<size_t>(std::distance(receipt.log_indices.begin(), it));
    }

    if (receipt_log_index >= receipt.receipt.logs.size())
    {
        return {ReceiptLogVerificationError::kLogIndexOutOfRange};
    }

    const auto& log = receipt.receipt.logs[receipt_log_index];
    if (log.address != claim.bridge_contract)
    {
        return {ReceiptLogVerificationError::kContractMismatch};
    }
    if (log.topics.empty() || log.topics.front() != claim.event_topic0)
    {
        return {ReceiptLogVerificationError::kTopic0Mismatch};
    }
    if (!claim.topics.empty() && log.topics != claim.topics)
    {
        return {ReceiptLogVerificationError::kTopicsMismatch};
    }
    if (log.data != claim.data)
    {
        return {ReceiptLogVerificationError::kDataMismatch};
    }

    return {};
}

} // namespace eth
