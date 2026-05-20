// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_CHAIN_TRACKER_HPP
#define EVMRELAY_INCLUDE_ETH_CHAIN_TRACKER_HPP

#include <eth/objects.hpp>
#include <cstdint>
#include <optional>
#include <set>

namespace eth {

/// @brief Tracks the chain tip and deduplicates block processing requests.
///
/// Responsibilities:
///  - Record which blocks have already had their receipts requested so
///    duplicate GetReceipts messages are never emitted for the same block.
///  - Track the highest known block number (the "tip").
///  - Detect when a block arrives that is lower than the current tip
///    (potential reorg or redundant announcement).
///
/// This class is intentionally minimal — it does not store headers or
/// receipts; it only tracks identity (hash) and height (number).
///
/// Thread-safety: not thread-safe. All calls must be externally synchronized.
class ChainTracker
{
public:
    /// @brief Maximum number of block hashes to remember for deduplication.
    ///        Older entries beyond this window are evicted (FIFO order by
    ///        insertion, not by block number).
    static constexpr size_t kDefaultWindowSize = 1024;

    /// @brief Construct with an optional custom deduplication window size.
    explicit ChainTracker(size_t window_size = kDefaultWindowSize) noexcept;

    ~ChainTracker() = default;

    ChainTracker(const ChainTracker&) = delete;
    ChainTracker& operator=(const ChainTracker&) = delete;
    ChainTracker(ChainTracker&&) = default;
    ChainTracker& operator=(ChainTracker&&) = default;

    /// @brief Attempt to mark a block as "receipts requested".
    ///
    /// If this block hash has already been seen, returns false and the caller
    /// should NOT emit another GetReceipts request.
    /// If it is new, records it and returns true — the caller should proceed.
    ///
    /// Also updates the tip if block_number > current tip.
    ///
    /// @param block_hash    Hash of the block being processed.
    /// @param block_number  Height of the block.
    /// @return true if this is the first time this block has been seen.
    bool mark_seen(const codec::Hash256& block_hash,
                   uint64_t              block_number) noexcept;

    /// @brief Return true if this block hash has already been seen.
    [[nodiscard]] bool is_seen(const codec::Hash256& block_hash) const noexcept;

    /// @brief Return the highest block number seen so far (0 if none).
    [[nodiscard]] uint64_t tip() const noexcept;

    /// @brief Return the hash of the highest block seen, if any.
    [[nodiscard]] std::optional<codec::Hash256> tip_hash() const noexcept;

    /// @brief Return the number of blocks currently in the deduplication window.
    [[nodiscard]] size_t seen_count() const noexcept;

    /// @brief Reset all state (tip, seen set, eviction queue).
    void reset() noexcept;

private:
    size_t                          window_size_;
    uint64_t                        tip_number_ = 0;
    std::optional<codec::Hash256>   tip_hash_;

    /// Ordered set for O(log n) lookup.
    std::set<codec::Hash256>        seen_set_;

    /// FIFO queue for eviction — stores hashes in insertion order.
    std::vector<codec::Hash256>     eviction_queue_;
    size_t                          eviction_head_ = 0;  ///< Index of oldest entry
};

} // namespace eth


#endif // EVMRELAY_INCLUDE_ETH_CHAIN_TRACKER_HPP
