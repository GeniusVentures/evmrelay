// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <eth/objects.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace eth {

/// @brief Specifies which logs to accept: by emitting address(es) and/or topic(s).
///
/// Topic matching follows the eth_getLogs semantics:
///  - An empty topics vector matches any log.
///  - topics[i] == std::nullopt matches any value at position i.
///  - topics[i] == some_hash matches only that exact value at position i.
///
/// Address matching:
///  - An empty addresses vector matches logs from any contract.
///  - Otherwise only logs whose emitting address appears in the list are accepted.
struct EventFilter
{
    /// Contracts to watch; empty means "any contract".
    std::vector<codec::Address> addresses;

    /// Per-position topic constraints.  Each slot is optional<Hash256>:
    ///   std::nullopt  → wildcard (accept any value at this position)
    ///   has_value()   → must equal this exact topic hash
    std::vector<std::optional<codec::Hash256>> topics;

    /// Optional inclusive block range.
    std::optional<uint64_t> from_block;
    std::optional<uint64_t> to_block;

    /// @brief Test whether a log entry matches this filter.
    /// @param log    The log entry to test.
    /// @param block  The block number the log was included in (0 if unknown).
    /// @return true if the log satisfies all filter constraints.
    [[nodiscard]] bool matches(const codec::LogEntry& log, uint64_t block = 0) const noexcept;
};

/// @brief A matched event: the original log decorated with block context.
struct MatchedEvent
{
    codec::LogEntry log;          ///< The raw log entry
    uint64_t        block_number; ///< Block the log appeared in
    codec::Hash256  block_hash;   ///< Hash of that block (if known)
    codec::Hash256  tx_hash;      ///< Transaction hash (if known)
    uint32_t        log_index;    ///< Position within the block's log set
};

/// @brief Registration handle returned by EventWatcher::watch().
///        Passed back to EventWatcher::unwatch() to remove a subscription.
using WatchId = uint32_t;

/// @brief Callback invoked for every log that matches a registered filter.
using EventCallback = std::function<void(const MatchedEvent&)>;

/// @brief Registers filters and dispatches matching logs to callbacks.
///
/// Thread-safety: not thread-safe. All calls must be made from the same thread
/// (or externally synchronised).
class EventWatcher
{
public:
    EventWatcher() = default;
    ~EventWatcher() = default;

    // Non-copyable
    EventWatcher(const EventWatcher&) = delete;
    EventWatcher& operator=(const EventWatcher&) = delete;

    // Moveable
    EventWatcher(EventWatcher&&) = default;
    EventWatcher& operator=(EventWatcher&&) = default;

    /// @brief Register a new filter+callback pair.
    /// @param filter   The EventFilter describing which logs to accept.
    /// @param callback Function called for every matching log.
    /// @return A WatchId that can later be passed to unwatch().
    WatchId watch(EventFilter filter, EventCallback callback) noexcept;

    /// @brief Remove a previously registered subscription.
    /// @param id  The WatchId returned by watch().
    void unwatch(WatchId id) noexcept;

    /// @brief Process a slice of logs from a single block and trigger callbacks.
    /// @param logs          Logs to process.
    /// @param block_number  Block number the logs belong to.
    /// @param block_hash    Hash of that block.
    /// @return Number of logs that matched at least one subscription.
    size_t process_block_logs(
        const std::vector<codec::LogEntry>& logs,
        uint64_t                            block_number,
        const codec::Hash256&               block_hash) noexcept;

    /// @brief Process a single receipt and trigger callbacks for matching logs.
    /// @param receipt       The transaction receipt containing logs.
    /// @param tx_hash       Hash of the transaction that generated the receipt.
    /// @param block_number  Block number the receipt belongs to.
    /// @param block_hash    Hash of that block.
    /// @return Number of logs that matched at least one subscription.
    size_t process_receipt(
        const codec::Receipt& receipt,
        const codec::Hash256& tx_hash,
        uint64_t              block_number,
        const codec::Hash256& block_hash) noexcept;

    /// @brief Return the number of active subscriptions.
    [[nodiscard]] size_t subscription_count() const noexcept
    {
        return subscriptions_.size();
    }

private:
    struct Subscription
    {
        WatchId       id;
        EventFilter   filter;
        EventCallback callback;
    };

    std::vector<Subscription> subscriptions_;
    WatchId                   next_id_ = 1;
};

} // namespace eth

