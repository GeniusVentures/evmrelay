// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/event_filter.hpp>
#include <algorithm>

namespace eth {

// ---------------------------------------------------------------------------
// EventFilter::matches
// ---------------------------------------------------------------------------

bool EventFilter::matches(const codec::LogEntry& log, uint64_t block) const noexcept
{
    // Block range check
    if (from_block.has_value() && block < from_block.value())
    {
        return false;
    }
    if (to_block.has_value() && block > to_block.value())
    {
        return false;
    }

    // Address check: if filter specifies addresses, the log's emitter must be in the list
    if (!addresses.empty())
    {
        const auto it = std::find(addresses.begin(), addresses.end(), log.address);
        if (it == addresses.end())
        {
            return false;
        }
    }

    // Topic check: per-position matching
    for (size_t i = 0; i < topics.size(); ++i)
    {
        if (!topics[i].has_value())
        {
            // Wildcard – any value (including absent) is fine
            continue;
        }

        if (i >= log.topics.size())
        {
            // Log doesn't have a topic at this position but filter requires one
            return false;
        }

        if (log.topics[i] != topics[i].value())
        {
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// EventWatcher
// ---------------------------------------------------------------------------

WatchId EventWatcher::watch(EventFilter filter, EventCallback callback) noexcept
{
    const WatchId id = next_id_++;
    subscriptions_.push_back({id, std::move(filter), std::move(callback)});
    return id;
}

void EventWatcher::unwatch(WatchId id) noexcept
{
    subscriptions_.erase(
        std::remove_if(
            subscriptions_.begin(),
            subscriptions_.end(),
            [id](const Subscription& s) { return s.id == id; }),
        subscriptions_.end());
}

size_t EventWatcher::process_block_logs(
    const std::vector<codec::LogEntry>& logs,
    uint64_t                            block_number,
    const codec::Hash256&               block_hash) noexcept
{
    size_t matched_logs = 0;
    uint32_t log_index = 0;
    for (const auto& log : logs)
    {
        bool matched = false;
        for (const auto& sub : subscriptions_)
        {
            if (sub.filter.matches(log, block_number))
            {
                matched = true;
                MatchedEvent event{
                    log,
                    block_number,
                    block_hash,
                    codec::Hash256{},   // tx_hash unknown at block-log level
                    log_index,
                    std::nullopt
                };
                sub.callback(event);
            }
        }
        if (matched)
        {
            ++matched_logs;
        }
        ++log_index;
    }
    return matched_logs;
}

size_t EventWatcher::process_receipt(
    const codec::Receipt& receipt,
    const codec::Hash256& tx_hash,
    uint64_t              block_number,
    const codec::Hash256& block_hash,
    uint32_t              first_log_index) noexcept
{
    size_t matched_logs = 0;
    uint32_t log_index = first_log_index;
    uint32_t receipt_log_index = 0;
    for (const auto& log : receipt.logs)
    {
        bool matched = false;
        for (const auto& sub : subscriptions_)
        {
            if (sub.filter.matches(log, block_number))
            {
                matched = true;
                MatchedEvent event{
                    log,
                    block_number,
                    block_hash,
                    tx_hash,
                    log_index,
                    receipt_log_index
                };
                sub.callback(event);
            }
        }
        if (matched)
        {
            ++matched_logs;
        }
        ++log_index;
        ++receipt_log_index;
    }
    return matched_logs;
}

} // namespace eth
