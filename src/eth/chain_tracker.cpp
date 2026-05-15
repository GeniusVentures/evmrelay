// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/chain_tracker.hpp>

namespace eth {

ChainTracker::ChainTracker(size_t window_size) noexcept
    : window_size_(window_size == 0 ? kDefaultWindowSize : window_size)
{
    eviction_queue_.resize(window_size_);
}

bool ChainTracker::mark_seen(const codec::Hash256& block_hash,
                             uint64_t              block_number) noexcept
{
    if (seen_set_.count(block_hash) > 0)
    {
        return false;
    }

    // Evict oldest entry if the window is full
    if (seen_set_.size() >= window_size_)
    {
        const auto& oldest = eviction_queue_[eviction_head_];
        seen_set_.erase(oldest);
        eviction_queue_[eviction_head_] = block_hash;
        eviction_head_ = (eviction_head_ + 1) % window_size_;
    }
    else
    {
        // Window not yet full — fill sequentially
        const size_t insert_pos = seen_set_.size();
        eviction_queue_[insert_pos] = block_hash;
    }

    seen_set_.insert(block_hash);

    if (block_number > tip_number_)
    {
        tip_number_ = block_number;
        tip_hash_   = block_hash;
    }

    return true;
}

bool ChainTracker::is_seen(const codec::Hash256& block_hash) const noexcept
{
    return seen_set_.count(block_hash) > 0;
}

uint64_t ChainTracker::tip() const noexcept
{
    return tip_number_;
}

std::optional<codec::Hash256> ChainTracker::tip_hash() const noexcept
{
    return tip_hash_;
}

size_t ChainTracker::seen_count() const noexcept
{
    return seen_set_.size();
}

void ChainTracker::reset() noexcept
{
    tip_number_ = 0;
    tip_hash_.reset();
    seen_set_.clear();
    std::fill(eviction_queue_.begin(), eviction_queue_.end(), codec::Hash256{});
    eviction_head_ = 0;
}

} // namespace eth

