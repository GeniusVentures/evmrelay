// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <eth/eth_receipt_source.hpp>

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

eth::codec::Hash256 make_address_word(const eth::codec::Address& address)
{
    eth::codec::Hash256 word{};
    std::copy(address.begin(), address.end(), word.begin() + 12);
    return word;
}

void append_uint256(eth::codec::ByteBuffer& buffer, uint64_t value)
{
    for (int i = 0; i < 24; ++i)
    {
        buffer.push_back(0);
    }
    for (int i = 7; i >= 0; --i)
    {
        buffer.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

eth::codec::LogEntry make_transfer_log(
    const eth::codec::Address& token,
    const eth::codec::Address& from,
    const eth::codec::Address& to,
    uint64_t amount)
{
    eth::codec::LogEntry log;
    log.address = token;
    log.topics.push_back(eth::abi::event_signature_hash("Transfer(address,address,uint256)"));
    log.topics.push_back(make_address_word(from));
    log.topics.push_back(make_address_word(to));
    append_uint256(log.data, amount);
    return log;
}

class MockReceiptSource final : public eth::IEthReceiptSource
{
public:
    struct RegisteredFilter
    {
        eth::WatchId id = 0;
        eth::EventFilter filter;
    };

    eth::WatchId add_filter(eth::EventFilter filter) override
    {
        const eth::WatchId id = next_id_++;
        filters.push_back({id, std::move(filter)});
        return id;
    }

    void remove_filter(eth::WatchId id) override
    {
        filters.erase(
            std::remove_if(filters.begin(), filters.end(),
                [id](const RegisteredFilter& entry)
                {
                    return entry.id == id;
                }),
            filters.end());
    }

    void set_receipt_batch_handler(eth::ReceiptBatchHandler handler) override
    {
        handler_ = std::move(handler);
    }

    std::optional<eth::ReceiptResult> get_receipt(const eth::Hash256& tx_hash) override
    {
        if (!receipt_result.has_value() || receipt_result->tx_hash != tx_hash)
        {
            return std::nullopt;
        }
        return receipt_result;
    }

    void emit(const eth::ReceiptBatch& batch)
    {
        if (handler_)
        {
            handler_(batch);
        }
    }

    std::vector<RegisteredFilter> filters;
    std::optional<eth::ReceiptResult> receipt_result;

private:
    eth::WatchId next_id_ = 1;
    eth::ReceiptBatchHandler handler_;
};

} // namespace

TEST(EthReceiptSourceBridgeTest, RegistersSourceFilterAndFeedsReceiptBatchToWatchService)
{
    const auto token = make_filled<eth::codec::Address>(0xAA);
    const auto from = make_filled<eth::codec::Address>(0x11);
    const auto to = make_filled<eth::codec::Address>(0x22);
    const auto tx_hash = make_filled<eth::Hash256>(0x33);
    const auto block_hash = make_filled<eth::Hash256>(0x44);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    eth::EthWatchService service;
    MockReceiptSource source;
    eth::EthReceiptSourceBridge bridge(service, source);

    std::optional<eth::MatchedEvent> matched;
    std::vector<eth::abi::AbiValue> decoded_values;
    const auto watch_id = bridge.watch_event(
        token,
        "Transfer(address,address,uint256)",
        params,
        [&matched, &decoded_values](
            const eth::MatchedEvent& event,
            const std::vector<eth::abi::AbiValue>& values)
        {
            matched = event;
            decoded_values = values;
        },
        100,
        200);

    EXPECT_NE(watch_id, 0U);
    ASSERT_EQ(source.filters.size(), 1U);
    EXPECT_EQ(source.filters[0].filter.addresses, std::vector<eth::codec::Address>{token});
    ASSERT_EQ(source.filters[0].filter.topics.size(), 1U);
    ASSERT_TRUE(source.filters[0].filter.topics[0].has_value());
    EXPECT_EQ(*source.filters[0].filter.topics[0],
              eth::abi::event_signature_hash("Transfer(address,address,uint256)"));
    EXPECT_EQ(source.filters[0].filter.from_block, std::optional<uint64_t>(100));
    EXPECT_EQ(source.filters[0].filter.to_block, std::optional<uint64_t>(200));

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token, from, to, 42));

    eth::ReceiptBatch batch;
    batch.receipts.push_back(receipt);
    batch.tx_hashes.push_back(tx_hash);
    batch.block_number = 150;
    batch.block_hash = block_hash;
    source.emit(batch);

    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(matched->block_number, 150U);
    EXPECT_EQ(matched->block_hash, block_hash);
    EXPECT_EQ(matched->tx_hash, tx_hash);
    ASSERT_EQ(decoded_values.size(), 3U);
}

TEST(EthReceiptSourceBridgeTest, UnwatchRemovesServiceSubscriptionAndSourceFilter)
{
    const auto token = make_filled<eth::codec::Address>(0xAA);
    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    eth::EthWatchService service;
    MockReceiptSource source;
    eth::EthReceiptSourceBridge bridge(service, source);

    const auto watch_id = bridge.watch_event(
        token,
        "Transfer(address,address,uint256)",
        params,
        [](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) {});

    EXPECT_EQ(service.subscription_count(), 1U);
    ASSERT_EQ(source.filters.size(), 1U);

    bridge.unwatch(watch_id);

    EXPECT_EQ(service.subscription_count(), 0U);
    EXPECT_TRUE(source.filters.empty());
}

TEST(EthReceiptSourceTest, MockSourceSupportsReceiptLookupByTransactionHash)
{
    const auto tx_hash = make_filled<eth::Hash256>(0x90);
    MockReceiptSource source;

    eth::ReceiptResult result;
    result.tx_hash = tx_hash;
    result.block_number = 123;
    source.receipt_result = result;

    const auto found = source.get_receipt(tx_hash);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->block_number, 123U);

    const auto missing = source.get_receipt(make_filled<eth::Hash256>(0xA0));
    EXPECT_FALSE(missing.has_value());
}
