// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/chain_tracker.hpp>
#include <eth/eth_watch_service.hpp>
#include <eth/messages.hpp>

namespace {

template <typename Array>
Array make_filled(uint8_t seed)
{
    Array value{};
    for (size_t i = 0; i < value.size(); ++i)
    {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

} // namespace

// ============================================================================
// ChainTracker — mark_seen / is_seen
// ============================================================================

TEST(ChainTrackerTest, NewBlockIsMarkedSeen)
{
    eth::ChainTracker tracker;
    const auto hash = make_filled<eth::codec::Hash256>(0x01);

    EXPECT_FALSE(tracker.is_seen(hash));
    EXPECT_TRUE(tracker.mark_seen(hash, 100));
    EXPECT_TRUE(tracker.is_seen(hash));
}

TEST(ChainTrackerTest, DuplicateBlockReturnsFalse)
{
    eth::ChainTracker tracker;
    const auto hash = make_filled<eth::codec::Hash256>(0x01);

    EXPECT_TRUE(tracker.mark_seen(hash, 100));
    EXPECT_FALSE(tracker.mark_seen(hash, 100));  // duplicate
    EXPECT_EQ(tracker.seen_count(), 1u);
}

TEST(ChainTrackerTest, TipUpdatesOnHigherBlock)
{
    eth::ChainTracker tracker;

    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x01), 50);
    EXPECT_EQ(tracker.tip(), 50u);

    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x02), 100);
    EXPECT_EQ(tracker.tip(), 100u);

    // Lower block does not lower the tip
    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x03), 75);
    EXPECT_EQ(tracker.tip(), 100u);
}

TEST(ChainTrackerTest, TipHashFollowsHighestBlock)
{
    eth::ChainTracker tracker;
    const auto hash_low  = make_filled<eth::codec::Hash256>(0x01);
    const auto hash_high = make_filled<eth::codec::Hash256>(0x02);

    tracker.mark_seen(hash_low,  50);
    ASSERT_TRUE(tracker.tip_hash().has_value());
    EXPECT_EQ(tracker.tip_hash().value(), hash_low);

    tracker.mark_seen(hash_high, 100);
    ASSERT_TRUE(tracker.tip_hash().has_value());
    EXPECT_EQ(tracker.tip_hash().value(), hash_high);
}

TEST(ChainTrackerTest, TipHashInitiallyEmpty)
{
    eth::ChainTracker tracker;
    EXPECT_FALSE(tracker.tip_hash().has_value());
    EXPECT_EQ(tracker.tip(), 0u);
}

TEST(ChainTrackerTest, SeenCountIncreasesWithNewBlocks)
{
    eth::ChainTracker tracker;
    EXPECT_EQ(tracker.seen_count(), 0u);

    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x01), 1);
    EXPECT_EQ(tracker.seen_count(), 1u);

    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x02), 2);
    EXPECT_EQ(tracker.seen_count(), 2u);

    // Duplicate does not increase count
    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x01), 1);
    EXPECT_EQ(tracker.seen_count(), 2u);
}

// ============================================================================
// ChainTracker — window eviction
// ============================================================================

TEST(ChainTrackerTest, WindowEvictsOldestEntry)
{
    // Window of 3
    eth::ChainTracker tracker(3);

    const auto h1 = make_filled<eth::codec::Hash256>(0x01);
    const auto h2 = make_filled<eth::codec::Hash256>(0x02);
    const auto h3 = make_filled<eth::codec::Hash256>(0x03);
    const auto h4 = make_filled<eth::codec::Hash256>(0x04);

    tracker.mark_seen(h1, 1);
    tracker.mark_seen(h2, 2);
    tracker.mark_seen(h3, 3);
    EXPECT_EQ(tracker.seen_count(), 3u);

    // Adding h4 evicts h1
    tracker.mark_seen(h4, 4);
    EXPECT_EQ(tracker.seen_count(), 3u);
    EXPECT_FALSE(tracker.is_seen(h1));
    EXPECT_TRUE(tracker.is_seen(h2));
    EXPECT_TRUE(tracker.is_seen(h3));
    EXPECT_TRUE(tracker.is_seen(h4));
}

TEST(ChainTrackerTest, EvictedBlockCanBeSeenAgain)
{
    eth::ChainTracker tracker(2);

    const auto h1 = make_filled<eth::codec::Hash256>(0x01);
    const auto h2 = make_filled<eth::codec::Hash256>(0x02);
    const auto h3 = make_filled<eth::codec::Hash256>(0x03);

    tracker.mark_seen(h1, 1);
    tracker.mark_seen(h2, 2);
    tracker.mark_seen(h3, 3);  // evicts h1

    // h1 is no longer seen — mark_seen returns true again
    EXPECT_FALSE(tracker.is_seen(h1));
    EXPECT_TRUE(tracker.mark_seen(h1, 1));
}

// ============================================================================
// ChainTracker — reset
// ============================================================================

TEST(ChainTrackerTest, ResetClearsAllState)
{
    eth::ChainTracker tracker;
    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x01), 100);
    tracker.mark_seen(make_filled<eth::codec::Hash256>(0x02), 200);

    tracker.reset();

    EXPECT_EQ(tracker.tip(), 0u);
    EXPECT_FALSE(tracker.tip_hash().has_value());
    EXPECT_EQ(tracker.seen_count(), 0u);
    EXPECT_FALSE(tracker.is_seen(make_filled<eth::codec::Hash256>(0x01)));
}

// ============================================================================
// EthWatchService — deduplication via ChainTracker
// ============================================================================

TEST(ChainTrackerTest, EthWatchServiceDeduplicatesGetReceipts)
{
    eth::EthWatchService svc;

    int send_count = 0;
    svc.set_send_callback([&send_count](uint8_t, std::vector<uint8_t>)
    {
        ++send_count;
    });

    const auto hash = make_filled<eth::codec::Hash256>(0x01);

    // Announce the same block hash twice via NewBlockHashes
    eth::NewBlockHashesMessage nbh;
    nbh.entries.push_back({hash, 100});

    auto encoded = eth::protocol::encode_new_block_hashes(nbh);
    ASSERT_TRUE(encoded.has_value());

    const rlp::ByteView payload(encoded.value().data(), encoded.value().size());

    svc.process_message(eth::protocol::kNewBlockHashesMessageId, payload);
    svc.process_message(eth::protocol::kNewBlockHashesMessageId, payload);

    // GetReceipts should only be sent once despite two announcements
    EXPECT_EQ(send_count, 1);
}

TEST(ChainTrackerTest, EthWatchServiceTipTracksHighestBlock)
{
    eth::EthWatchService svc;
    svc.set_send_callback([](uint8_t, std::vector<uint8_t>) {});

    EXPECT_EQ(svc.tip(), 0u);

    eth::NewBlockHashesMessage nbh;
    nbh.entries.push_back({make_filled<eth::codec::Hash256>(0x01), 50});
    nbh.entries.push_back({make_filled<eth::codec::Hash256>(0x02), 200});
    nbh.entries.push_back({make_filled<eth::codec::Hash256>(0x03), 150});

    auto encoded = eth::protocol::encode_new_block_hashes(nbh);
    ASSERT_TRUE(encoded.has_value());

    svc.process_message(
        eth::protocol::kNewBlockHashesMessageId,
        rlp::ByteView(encoded.value().data(), encoded.value().size()));

    EXPECT_EQ(svc.tip(), 200u);
    ASSERT_TRUE(svc.tip_hash().has_value());
    EXPECT_EQ(svc.tip_hash().value(), make_filled<eth::codec::Hash256>(0x02));
}

