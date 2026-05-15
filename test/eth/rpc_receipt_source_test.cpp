// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <base/parse_utility.hpp>
#include <eth/eth_receipt_source.hpp>
#include <eth/rpc_receipt_source.hpp>
#include <boost/json.hpp>
#include <boost/json/serialize.hpp>
#include <functional>

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

class FakeJsonRpcTransport final : public eth::rpc::JsonRpcTransport
{
public:
    std::optional<std::string> call(const boost::json::object& request) override
    {
        requests.push_back(request);
        if (!handler)
        {
            return std::nullopt;
        }
        return handler(request);
    }

    std::vector<boost::json::object> requests;
    std::function<std::optional<std::string>(const boost::json::object&)> handler;
};

std::string method_of(const boost::json::object& request)
{
    return std::string(request.at("method").as_string());
}

std::string block_response(uint64_t block_number)
{
    boost::json::object block;
    block["number"] = rlp::base::parse::uint64_hex_quantity(block_number);

    boost::json::object response;
    response["jsonrpc"] = "2.0";
    response["id"] = 1;
    response["result"] = std::move(block);
    return boost::json::serialize(response);
}

std::string logs_response(
    const eth::Address& address,
    const eth::Hash256& topic0,
    const eth::Hash256& block_hash,
    const eth::Hash256& tx_hash,
    uint64_t            block_number,
    uint32_t            log_index)
{
    boost::json::array topics;
    topics.push_back(boost::json::value(rlp::base::parse::hex_array_string(topic0)));

    boost::json::object log;
    log["address"] = rlp::base::parse::hex_array_string(address);
    log["topics"] = std::move(topics);
    log["data"] = "0x";
    log["blockNumber"] = rlp::base::parse::uint64_hex_quantity(block_number);
    log["blockHash"] = rlp::base::parse::hex_array_string(block_hash);
    log["transactionHash"] = rlp::base::parse::hex_array_string(tx_hash);
    log["logIndex"] = rlp::base::parse::uint64_hex_quantity(log_index);

    boost::json::array logs;
    logs.push_back(std::move(log));

    boost::json::object response;
    response["jsonrpc"] = "2.0";
    response["id"] = 1;
    response["result"] = std::move(logs);
    return boost::json::serialize(response);
}

std::string empty_logs_response()
{
    boost::json::object response;
    response["jsonrpc"] = "2.0";
    response["id"] = 1;
    response["result"] = boost::json::array{};
    return boost::json::serialize(response);
}

std::string receipt_response(
    const eth::Address& address,
    const eth::Hash256& topic0,
    const eth::Hash256& block_hash,
    const eth::Hash256& tx_hash,
    uint64_t            block_number,
    uint32_t            log_index)
{
    boost::json::array topics;
    topics.push_back(boost::json::value(rlp::base::parse::hex_array_string(topic0)));

    boost::json::object log;
    log["address"] = rlp::base::parse::hex_array_string(address);
    log["topics"] = std::move(topics);
    log["data"] = "0x";
    log["blockNumber"] = rlp::base::parse::uint64_hex_quantity(block_number);
    log["blockHash"] = rlp::base::parse::hex_array_string(block_hash);
    log["transactionHash"] = rlp::base::parse::hex_array_string(tx_hash);
    log["logIndex"] = rlp::base::parse::uint64_hex_quantity(log_index);

    boost::json::array logs;
    logs.push_back(std::move(log));

    boost::json::object receipt;
    receipt["status"] = "0x1";
    receipt["blockNumber"] = rlp::base::parse::uint64_hex_quantity(block_number);
    receipt["blockHash"] = rlp::base::parse::hex_array_string(block_hash);
    receipt["transactionHash"] = rlp::base::parse::hex_array_string(tx_hash);
    receipt["logs"] = std::move(logs);

    boost::json::object response;
    response["jsonrpc"] = "2.0";
    response["id"] = 1;
    response["result"] = std::move(receipt);
    return boost::json::serialize(response);
}

} // namespace

TEST(RpcReceiptSourceTest, GetReceiptUsesJsonRpcTransport)
{
    const auto address = make_filled<eth::Address>(0x10);
    const auto topic0 = make_filled<eth::Hash256>(0x20);
    const auto block_hash = make_filled<eth::Hash256>(0x30);
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [&](const boost::json::object& request)
    {
        EXPECT_EQ(method_of(request), "eth_getTransactionReceipt");
        return receipt_response(address, topic0, block_hash, tx_hash, 100, 5);
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12});

    const auto receipt = source.get_receipt(tx_hash);

    ASSERT_TRUE(receipt.has_value());
    EXPECT_EQ(receipt->tx_hash, tx_hash);
    EXPECT_EQ(receipt->block_number, 100U);
    EXPECT_EQ(receipt->block_hash, block_hash);
    ASSERT_EQ(receipt->log_indices.size(), 1U);
    EXPECT_EQ(receipt->log_indices[0], 5U);
}

TEST(RpcReceiptSourceTest, GetReceiptReturnsNulloptWhenTransportFails)
{
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [](const boost::json::object&)
    {
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12});

    EXPECT_FALSE(source.get_receipt(tx_hash).has_value());
}

TEST(RpcReceiptSourceTest, GetReceiptReturnsNulloptForMalformedJson)
{
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [](const boost::json::object&)
    {
        return std::optional<std::string>("{not json");
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12});

    EXPECT_FALSE(source.get_receipt(tx_hash).has_value());
}

TEST(RpcReceiptSourceTest, BackfillFetchesLogsThenEmitsReceipts)
{
    const auto address = make_filled<eth::Address>(0x10);
    const auto topic0 = eth::abi::event_signature_hash("Ping()");
    const auto block_hash = make_filled<eth::Hash256>(0x30);
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [&](const boost::json::object& request) -> std::optional<std::string>
    {
        const auto method = method_of(request);
        if (method == "eth_getLogs")
        {
            return logs_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        if (method == "eth_getTransactionReceipt")
        {
            return receipt_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12});
    source.add_filter(eth::make_event_filter(address, "Ping()"));

    std::optional<eth::ReceiptBatch> emitted;
    source.set_receipt_batch_handler(
        [&emitted](const eth::ReceiptBatch& batch)
        {
            emitted = batch;
        });

    ASSERT_TRUE(source.backfill(100, 100));

    ASSERT_TRUE(emitted.has_value());
    EXPECT_EQ(emitted->block_number, 100U);
    EXPECT_EQ(emitted->block_hash, block_hash);
    ASSERT_EQ(emitted->receipts.size(), 1U);
    ASSERT_EQ(emitted->tx_hashes.size(), 1U);
    EXPECT_EQ(emitted->tx_hashes[0], tx_hash);
    ASSERT_EQ(emitted->log_indices.size(), 1U);
    ASSERT_EQ(emitted->log_indices[0].size(), 1U);
    EXPECT_EQ(emitted->log_indices[0][0], 5U);
    EXPECT_EQ(source.last_processed_block(), 100U);
}

TEST(RpcReceiptSourceTest, BackfillWithNoLogsAdvancesWithoutEmitting)
{
    FakeJsonRpcTransport transport;
    transport.handler = [](const boost::json::object& request) -> std::optional<std::string>
    {
        if (method_of(request) == "eth_getLogs")
        {
            return empty_logs_response();
        }
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12},
        99);
    source.add_filter(eth::make_event_filter(make_filled<eth::Address>(0x10), "Ping()"));

    size_t emitted_count = 0;
    source.set_receipt_batch_handler(
        [&emitted_count](const eth::ReceiptBatch&)
        {
            ++emitted_count;
        });

    EXPECT_TRUE(source.backfill(100, 100));
    EXPECT_EQ(source.last_processed_block(), 100U);
    EXPECT_EQ(emitted_count, 0U);
}

TEST(RpcReceiptSourceTest, BackfillFailsOnMalformedLogsAndDoesNotAdvance)
{
    FakeJsonRpcTransport transport;
    transport.handler = [](const boost::json::object& request) -> std::optional<std::string>
    {
        if (method_of(request) == "eth_getLogs")
        {
            return std::optional<std::string>("{not json");
        }
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12},
        99);
    source.add_filter(eth::make_event_filter(make_filled<eth::Address>(0x10), "Ping()"));

    size_t emitted_count = 0;
    source.set_receipt_batch_handler(
        [&emitted_count](const eth::ReceiptBatch&)
        {
            ++emitted_count;
        });

    EXPECT_FALSE(source.backfill(100, 100));
    EXPECT_EQ(source.last_processed_block(), 99U);
    EXPECT_EQ(emitted_count, 0U);
}

TEST(RpcReceiptSourceTest, BackfillFailsWhenReceiptFetchFailsAndDoesNotAdvance)
{
    const auto address = make_filled<eth::Address>(0x10);
    const auto topic0 = eth::abi::event_signature_hash("Ping()");
    const auto block_hash = make_filled<eth::Hash256>(0x30);
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [&](const boost::json::object& request) -> std::optional<std::string>
    {
        const auto method = method_of(request);
        if (method == "eth_getLogs")
        {
            return logs_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        if (method == "eth_getTransactionReceipt")
        {
            return std::nullopt;
        }
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12},
        99);
    source.add_filter(eth::make_event_filter(address, "Ping()"));

    size_t emitted_count = 0;
    source.set_receipt_batch_handler(
        [&emitted_count](const eth::ReceiptBatch&)
        {
            ++emitted_count;
        });

    EXPECT_FALSE(source.backfill(100, 100));
    EXPECT_EQ(source.last_processed_block(), 99U);
    EXPECT_EQ(emitted_count, 0U);
}

TEST(RpcReceiptSourceTest, BackfillEmitsOneReceiptWhenFiltersFindSameTransaction)
{
    const auto address = make_filled<eth::Address>(0x10);
    const auto topic0 = eth::abi::event_signature_hash("Ping()");
    const auto block_hash = make_filled<eth::Hash256>(0x30);
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [&](const boost::json::object& request) -> std::optional<std::string>
    {
        const auto method = method_of(request);
        if (method == "eth_getLogs")
        {
            return logs_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        if (method == "eth_getTransactionReceipt")
        {
            return receipt_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 12});
    source.add_filter(eth::make_event_filter(address, "Ping()"));
    source.add_filter(eth::make_event_filter(address, "Ping()"));

    size_t emitted_count = 0;
    source.set_receipt_batch_handler(
        [&emitted_count](const eth::ReceiptBatch&)
        {
            ++emitted_count;
        });

    ASSERT_TRUE(source.backfill(100, 100));

    EXPECT_EQ(emitted_count, 1U);
}

TEST(RpcReceiptSourceTest, PollOnceUsesFinalityPolicyAndPreservesRpcLogIndex)
{
    const auto address = make_filled<eth::Address>(0x10);
    const auto topic0 = eth::abi::event_signature_hash("Ping()");
    const auto block_hash = make_filled<eth::Hash256>(0x30);
    const auto tx_hash = make_filled<eth::Hash256>(0x40);

    FakeJsonRpcTransport transport;
    transport.handler = [&](const boost::json::object& request) -> std::optional<std::string>
    {
        const auto method = method_of(request);
        if (method == "eth_getBlockByNumber")
        {
            return block_response(102);
        }
        if (method == "eth_getLogs")
        {
            const auto& params = request.at("params").as_array();
            const auto& filter = params[0].as_object();
            EXPECT_EQ(filter.at("fromBlock").as_string(), "0x64");
            EXPECT_EQ(filter.at("toBlock").as_string(), "0x64");
            return logs_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        if (method == "eth_getTransactionReceipt")
        {
            return receipt_response(address, topic0, block_hash, tx_hash, 100, 5);
        }
        return std::nullopt;
    };

    eth::EthWatchService service;
    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 2},
        99);
    eth::EthReceiptSourceBridge bridge(service, source);

    std::optional<eth::MatchedEvent> matched;
    const auto watch_id = bridge.watch_event(
        address,
        "Ping()",
        {},
        [&matched](const eth::MatchedEvent& event, const std::vector<eth::abi::AbiValue>&)
        {
            matched = event;
        });
    ASSERT_NE(watch_id, 0U);

    ASSERT_TRUE(source.poll_once());

    ASSERT_TRUE(matched.has_value());
    EXPECT_EQ(matched->block_number, 100U);
    EXPECT_EQ(matched->block_hash, block_hash);
    EXPECT_EQ(matched->tx_hash, tx_hash);
    EXPECT_EQ(matched->log_index, 5U);
    EXPECT_EQ(source.last_processed_block(), 100U);
}

TEST(RpcReceiptSourceTest, PollOnceFailsWhenNoFinalityHeadIsAvailable)
{
    FakeJsonRpcTransport transport;
    transport.handler = [](const boost::json::object&)
    {
        return std::nullopt;
    };

    eth::rpc::RpcReceiptSource source(
        transport,
        eth::FinalityPolicy{false, false, 2},
        99);

    EXPECT_FALSE(source.poll_once());
    EXPECT_EQ(source.last_processed_block(), 99U);
}
