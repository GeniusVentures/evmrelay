// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <base/parse_utility.hpp>
#include <eth/json_rpc.hpp>
#include <boost/json/serialize.hpp>

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

TEST(JsonRpcTest, BuildsGetBlockByNumberRequest)
{
    const auto request = eth::rpc::make_get_block_by_number_request(
        eth::rpc::RpcBlockTag::kFinalized,
        7);
    const auto serialized = boost::json::serialize(request);

    EXPECT_NE(serialized.find("\"method\":\"eth_getBlockByNumber\""), std::string::npos);
    EXPECT_NE(serialized.find("\"finalized\""), std::string::npos);
    EXPECT_NE(serialized.find("false"), std::string::npos);
    EXPECT_NE(serialized.find("\"id\":7"), std::string::npos);
}

TEST(JsonRpcTest, BuildsGetLogsRequestWithAddressAndTopics)
{
    eth::EventFilter filter;
    filter.addresses.push_back(make_filled<eth::Address>(0x10));
    filter.topics.push_back(make_filled<eth::Hash256>(0x20));
    filter.topics.push_back(std::nullopt);

    const auto request = eth::rpc::make_get_logs_request(filter, 10, 20, 9);
    const auto serialized = boost::json::serialize(request);

    EXPECT_NE(serialized.find("\"method\":\"eth_getLogs\""), std::string::npos);
    EXPECT_NE(serialized.find("\"fromBlock\":\"0xa\""), std::string::npos);
    EXPECT_NE(serialized.find("\"toBlock\":\"0x14\""), std::string::npos);
    EXPECT_NE(serialized.find("\"topics\""), std::string::npos);
    EXPECT_NE(serialized.find("null"), std::string::npos);
}

TEST(JsonRpcTest, BuildsGetTransactionReceiptRequest)
{
    const auto tx_hash = make_filled<eth::Hash256>(0xA0);
    const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash, 11);
    const auto serialized = boost::json::serialize(request);

    EXPECT_NE(serialized.find("\"method\":\"eth_getTransactionReceipt\""), std::string::npos);
    EXPECT_NE(serialized.find("\"id\":11"), std::string::npos);
    EXPECT_NE(serialized.find(rlp::base::parse::hex_array_string(tx_hash)), std::string::npos);
}

TEST(JsonRpcTest, ParsesBlockNumberResponse)
{
    const auto parsed = eth::rpc::parse_block_number_response(
        R"({"jsonrpc":"2.0","id":1,"result":{"number":"0x2a"}})");

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, 42U);
}

TEST(JsonRpcTest, ParsesGetLogsResponse)
{
    const std::string json =
        R"({"jsonrpc":"2.0","id":1,"result":[{)"
        R"("address":"0x101112131415161718191a1b1c1d1e1f20212223",)"
        R"("topics":["0x202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"],)"
        R"("data":"0x010203",)"
        R"("blockNumber":"0x64",)"
        R"("blockHash":"0x303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f",)"
        R"("transactionHash":"0x404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f",)"
        R"("logIndex":"0x5")"
        R"(}]})";

    const auto parsed = eth::rpc::parse_get_logs_response(json);

    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->size(), 1U);
    EXPECT_EQ((*parsed)[0].block_number, 100U);
    EXPECT_EQ((*parsed)[0].log_index, 5U);
    EXPECT_EQ((*parsed)[0].log.data, (eth::codec::ByteBuffer{0x01, 0x02, 0x03}));
    ASSERT_EQ((*parsed)[0].log.topics.size(), 1U);
}

TEST(JsonRpcTest, ParsesTransactionReceiptResponseWithExplicitLogIndexes)
{
    const std::string json =
        R"({"jsonrpc":"2.0","id":1,"result":{)"
        R"("status":"0x1",)"
        R"("blockNumber":"0x64",)"
        R"("blockHash":"0x303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f",)"
        R"("transactionHash":"0x404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f",)"
        R"("logs":[{)"
        R"("address":"0x101112131415161718191a1b1c1d1e1f20212223",)"
        R"("topics":["0x202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f"],)"
        R"("data":"0x010203",)"
        R"("blockNumber":"0x64",)"
        R"("blockHash":"0x303132333435363738393a3b3c3d3e3f404142434445464748494a4b4c4d4e4f",)"
        R"("transactionHash":"0x404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f",)"
        R"("logIndex":"0x5")"
        R"(}]}})";

    const auto parsed = eth::rpc::parse_transaction_receipt_response(json);

    ASSERT_TRUE(parsed.has_value());
    ASSERT_TRUE(parsed->receipt.status.has_value());
    EXPECT_TRUE(*parsed->receipt.status);
    EXPECT_EQ(parsed->block_number, 100U);
    ASSERT_EQ(parsed->receipt.logs.size(), 1U);
    ASSERT_EQ(parsed->log_indices.size(), 1U);
    EXPECT_EQ(parsed->log_indices[0], 5U);
}
