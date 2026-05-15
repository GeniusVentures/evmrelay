// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/event_filter.hpp>

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

eth::codec::LogEntry make_log(
    const eth::codec::Address& address,
    std::initializer_list<eth::codec::Hash256> topics,
    eth::codec::ByteBuffer data = {})
{
    eth::codec::LogEntry log;
    log.address = address;
    log.topics  = topics;
    log.data    = std::move(data);
    return log;
}

} // namespace

// ============================================================================
// EventFilter::matches – address filtering
// ============================================================================

TEST(EventFilterTest, EmptyFilterMatchesAll)
{
    eth::EventFilter filter;

    auto log = make_log(make_filled<eth::codec::Address>(0x01), {});
    EXPECT_TRUE(filter.matches(log));
}

TEST(EventFilterTest, AddressFilterAcceptsMatchingAddress)
{
    eth::EventFilter filter;
    const auto target = make_filled<eth::codec::Address>(0xAA);
    filter.addresses.push_back(target);

    auto log = make_log(target, {});
    EXPECT_TRUE(filter.matches(log));
}

TEST(EventFilterTest, AddressFilterRejectsNonMatchingAddress)
{
    eth::EventFilter filter;
    filter.addresses.push_back(make_filled<eth::codec::Address>(0xAA));

    auto log = make_log(make_filled<eth::codec::Address>(0xBB), {});
    EXPECT_FALSE(filter.matches(log));
}

TEST(EventFilterTest, MultipleAddressesAcceptsAny)
{
    eth::EventFilter filter;
    const auto addr1 = make_filled<eth::codec::Address>(0x11);
    const auto addr2 = make_filled<eth::codec::Address>(0x22);
    filter.addresses.push_back(addr1);
    filter.addresses.push_back(addr2);

    EXPECT_TRUE(filter.matches(make_log(addr1, {})));
    EXPECT_TRUE(filter.matches(make_log(addr2, {})));
    EXPECT_FALSE(filter.matches(make_log(make_filled<eth::codec::Address>(0x33), {})));
}

// ============================================================================
// EventFilter::matches – topic filtering
// ============================================================================

TEST(EventFilterTest, TopicWildcardMatchesAnyTopic)
{
    eth::EventFilter filter;
    filter.topics.push_back(std::nullopt); // wildcard at position 0

    auto log = make_log(
        make_filled<eth::codec::Address>(0x01),
        {make_filled<eth::codec::Hash256>(0xAA)});
    EXPECT_TRUE(filter.matches(log));
}

TEST(EventFilterTest, TopicExactMatchAccepts)
{
    const auto sig = make_filled<eth::codec::Hash256>(0xDD);

    eth::EventFilter filter;
    filter.topics.push_back(sig);

    auto log = make_log(
        make_filled<eth::codec::Address>(0x01),
        {sig});
    EXPECT_TRUE(filter.matches(log));
}

TEST(EventFilterTest, TopicExactMatchRejectsWrongTopic)
{
    eth::EventFilter filter;
    filter.topics.push_back(make_filled<eth::codec::Hash256>(0xDD));

    auto log = make_log(
        make_filled<eth::codec::Address>(0x01),
        {make_filled<eth::codec::Hash256>(0xEE)});
    EXPECT_FALSE(filter.matches(log));
}

TEST(EventFilterTest, TopicRequiredButLogHasNoTopics)
{
    eth::EventFilter filter;
    filter.topics.push_back(make_filled<eth::codec::Hash256>(0xDD));

    auto log = make_log(make_filled<eth::codec::Address>(0x01), {});
    EXPECT_FALSE(filter.matches(log));
}

TEST(EventFilterTest, MultiTopicPartialWildcard)
{
    const auto sig   = make_filled<eth::codec::Hash256>(0x01); // Transfer(...)
    const auto from  = make_filled<eth::codec::Hash256>(0x10);
    const auto other = make_filled<eth::codec::Hash256>(0x99);

    // Match topic[0]=sig, topic[1]=wildcard, topic[2]=from
    eth::EventFilter filter;
    filter.topics.push_back(sig);
    filter.topics.push_back(std::nullopt);
    filter.topics.push_back(from);

    auto matching_log = make_log(
        make_filled<eth::codec::Address>(0x01),
        {sig, other, from});
    EXPECT_TRUE(filter.matches(matching_log));

    auto wrong_from = make_log(
        make_filled<eth::codec::Address>(0x01),
        {sig, other, other});
    EXPECT_FALSE(filter.matches(wrong_from));
}

// ============================================================================
// EventFilter::matches – block range
// ============================================================================

TEST(EventFilterTest, BlockRangeFromOnly)
{
    eth::EventFilter filter;
    filter.from_block = 100;

    auto log = make_log(make_filled<eth::codec::Address>(0x01), {});
    EXPECT_FALSE(filter.matches(log, 99));
    EXPECT_TRUE(filter.matches(log, 100));
    EXPECT_TRUE(filter.matches(log, 200));
}

TEST(EventFilterTest, BlockRangeToOnly)
{
    eth::EventFilter filter;
    filter.to_block = 200;

    auto log = make_log(make_filled<eth::codec::Address>(0x01), {});
    EXPECT_TRUE(filter.matches(log, 100));
    EXPECT_TRUE(filter.matches(log, 200));
    EXPECT_FALSE(filter.matches(log, 201));
}

TEST(EventFilterTest, BlockRangeBothEnds)
{
    eth::EventFilter filter;
    filter.from_block = 100;
    filter.to_block   = 200;

    auto log = make_log(make_filled<eth::codec::Address>(0x01), {});
    EXPECT_FALSE(filter.matches(log, 99));
    EXPECT_TRUE(filter.matches(log, 150));
    EXPECT_FALSE(filter.matches(log, 201));
}

// ============================================================================
// EventWatcher
// ============================================================================

TEST(EventWatcherTest, WatchAndUnwatch)
{
    eth::EventWatcher watcher;
    EXPECT_EQ(watcher.subscription_count(), 0u);

    eth::EventFilter filter;
    int              call_count = 0;

    auto id = watcher.watch(filter, [&](const eth::MatchedEvent&) { ++call_count; });
    EXPECT_EQ(watcher.subscription_count(), 1u);

    watcher.unwatch(id);
    EXPECT_EQ(watcher.subscription_count(), 0u);

    // After unwatch, callback should NOT be called
    eth::codec::Hash256 block_hash{};
    auto log = make_log(make_filled<eth::codec::Address>(0x01), {});
    watcher.process_block_logs({log}, 1, block_hash);
    EXPECT_EQ(call_count, 0);
}

TEST(EventWatcherTest, ProcessBlockLogsDispatchesMatchingLogs)
{
    const auto watched_addr  = make_filled<eth::codec::Address>(0xAA);
    const auto ignored_addr  = make_filled<eth::codec::Address>(0xBB);
    const auto block_hash    = make_filled<eth::codec::Hash256>(0xFF);

    eth::EventFilter filter;
    filter.addresses.push_back(watched_addr);

    std::vector<eth::MatchedEvent> received;
    eth::EventWatcher watcher;
    watcher.watch(filter, [&](const eth::MatchedEvent& ev) { received.push_back(ev); });

    std::vector<eth::codec::LogEntry> logs = {
        make_log(watched_addr,  {make_filled<eth::codec::Hash256>(0x01)}),
        make_log(ignored_addr,  {make_filled<eth::codec::Hash256>(0x02)}),
        make_log(watched_addr,  {make_filled<eth::codec::Hash256>(0x03)}),
    };

    watcher.process_block_logs(logs, 500, block_hash);

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].block_number, 500u);
    EXPECT_EQ(received[0].block_hash,   block_hash);
    EXPECT_EQ(received[0].log.address,  watched_addr);
    EXPECT_EQ(received[0].log_index,    0u);
    EXPECT_EQ(received[1].log_index,    2u);
}

TEST(EventWatcherTest, ProcessReceiptDispatchesWithTxHash)
{
    const auto tx_hash   = make_filled<eth::codec::Hash256>(0xCC);
    const auto blk_hash  = make_filled<eth::codec::Hash256>(0xDD);

    eth::codec::Receipt receipt;
    receipt.status             = true;
    receipt.cumulative_gas_used = intx::uint256(21000);
    receipt.bloom              = {};
    receipt.logs.push_back(
        make_log(make_filled<eth::codec::Address>(0x01), {make_filled<eth::codec::Hash256>(0x01)}));
    receipt.logs.push_back(
        make_log(make_filled<eth::codec::Address>(0x02), {make_filled<eth::codec::Hash256>(0x02)}));

    // Filter watching address 0x01 only
    eth::EventFilter filter;
    filter.addresses.push_back(make_filled<eth::codec::Address>(0x01));

    std::vector<eth::MatchedEvent> received;
    eth::EventWatcher watcher;
    watcher.watch(filter, [&](const eth::MatchedEvent& ev) { received.push_back(ev); });

    watcher.process_receipt(receipt, tx_hash, 9000, blk_hash);

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].tx_hash,      tx_hash);
    EXPECT_EQ(received[0].block_number, 9000u);
    EXPECT_EQ(received[0].log_index,    0u);
}

TEST(EventWatcherTest, MultipleSubscribersReceiveIndependently)
{
    eth::EventWatcher watcher;

    const auto addr_a = make_filled<eth::codec::Address>(0xAA);
    const auto addr_b = make_filled<eth::codec::Address>(0xBB);
    const auto bh     = make_filled<eth::codec::Hash256>(0x00);

    eth::EventFilter fa;  fa.addresses.push_back(addr_a);
    eth::EventFilter fb;  fb.addresses.push_back(addr_b);

    int cnt_a = 0, cnt_b = 0;
    watcher.watch(fa, [&](const eth::MatchedEvent&) { ++cnt_a; });
    watcher.watch(fb, [&](const eth::MatchedEvent&) { ++cnt_b; });

    std::vector<eth::codec::LogEntry> logs = {
        make_log(addr_a, {}),
        make_log(addr_b, {}),
        make_log(addr_a, {}),
    };
    watcher.process_block_logs(logs, 1, bh);

    EXPECT_EQ(cnt_a, 2);
    EXPECT_EQ(cnt_b, 1);
}

TEST(EventWatcherTest, TopicSignatureMatching)
{
    // Simulate watching only Transfer events (topic[0] = Transfer sig hash)
    const auto transfer_sig = make_filled<eth::codec::Hash256>(0xEE);
    const auto approval_sig = make_filled<eth::codec::Hash256>(0xFF);
    const auto bh           = make_filled<eth::codec::Hash256>(0x00);

    eth::EventFilter filter;
    filter.topics.push_back(transfer_sig);

    int matches = 0;
    eth::EventWatcher watcher;
    watcher.watch(filter, [&](const eth::MatchedEvent&) { ++matches; });

    std::vector<eth::codec::LogEntry> logs = {
        make_log(make_filled<eth::codec::Address>(0x01), {transfer_sig}),
        make_log(make_filled<eth::codec::Address>(0x01), {approval_sig}),
        make_log(make_filled<eth::codec::Address>(0x01), {transfer_sig}),
    };
    watcher.process_block_logs(logs, 42, bh);

    EXPECT_EQ(matches, 2);
}

