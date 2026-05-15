// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/objects.hpp>
#include <rlp/rlp_decoder.hpp>

namespace {

template <typename Array>
Array make_filled(uint8_t seed) {
    Array value{};
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

} // namespace

TEST(EthObjectsTest, LogEntryRoundtrip) {
    eth::codec::LogEntry original;
    original.address = make_filled<eth::codec::Address>(0x10);
    original.topics.push_back(make_filled<eth::codec::Hash256>(0x01));
    original.topics.push_back(make_filled<eth::codec::Hash256>(0x11));
    original.data = {0xde, 0xad, 0xbe, 0xef};

    auto encoded = eth::codec::encode_log_entry(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::codec::decode_log_entry(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    EXPECT_EQ(result.address, original.address);
    EXPECT_EQ(result.topics, original.topics);
    EXPECT_EQ(result.data, original.data);
}

TEST(EthObjectsTest, ReceiptRoundtripStatus) {
    eth::codec::Receipt original;
    original.status = true;
    original.cumulative_gas_used = intx::uint256(21000);
    original.bloom = make_filled<eth::codec::Bloom>(0x20);

    eth::codec::LogEntry log;
    log.address = make_filled<eth::codec::Address>(0x30);
    log.topics.push_back(make_filled<eth::codec::Hash256>(0x40));
    log.data = {0x01, 0x02};
    original.logs.push_back(log);

    auto encoded = eth::codec::encode_receipt(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::codec::decode_receipt(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.status.has_value());
    EXPECT_TRUE(result.status.value());
    EXPECT_FALSE(result.state_root.has_value());
    EXPECT_EQ(result.cumulative_gas_used, original.cumulative_gas_used);
    EXPECT_EQ(result.bloom, original.bloom);
    ASSERT_EQ(result.logs.size(), 1u);
    EXPECT_EQ(result.logs[0].address, log.address);
    EXPECT_EQ(result.logs[0].topics, log.topics);
    EXPECT_EQ(result.logs[0].data, log.data);
}

TEST(EthObjectsTest, ReceiptRoundtripStateRoot) {
    eth::codec::Receipt original;
    original.state_root = make_filled<eth::codec::Hash256>(0x55);
    original.cumulative_gas_used = intx::uint256(42000);
    original.bloom = make_filled<eth::codec::Bloom>(0x66);

    auto encoded = eth::codec::encode_receipt(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::codec::decode_receipt(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.state_root.has_value());
    EXPECT_EQ(result.state_root.value(), original.state_root.value());
    EXPECT_FALSE(result.status.has_value());
    EXPECT_EQ(result.cumulative_gas_used, original.cumulative_gas_used);
    EXPECT_EQ(result.bloom, original.bloom);
}

TEST(EthObjectsTest, BlockHeaderRoundtrip) {
    eth::codec::BlockHeader original;
    original.parent_hash = make_filled<eth::codec::Hash256>(0x01);
    original.ommers_hash = make_filled<eth::codec::Hash256>(0x02);
    original.beneficiary = make_filled<eth::codec::Address>(0x03);
    original.state_root = make_filled<eth::codec::Hash256>(0x04);
    original.transactions_root = make_filled<eth::codec::Hash256>(0x05);
    original.receipts_root = make_filled<eth::codec::Hash256>(0x06);
    original.logs_bloom = make_filled<eth::codec::Bloom>(0x07);
    original.difficulty = intx::uint256(12345);
    original.number = 900;
    original.gas_limit = 30000000;
    original.gas_used = 21000;
    original.timestamp = 1700000000;
    original.extra_data = {0x12, 0x34, 0x56};
    original.mix_hash = make_filled<eth::codec::Hash256>(0x08);
    original.nonce = make_filled<std::array<uint8_t, 8>>(0x09);
    original.base_fee_per_gas = intx::uint256(100);

    auto encoded = eth::codec::encode_block_header(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::codec::decode_block_header(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    EXPECT_EQ(result.parent_hash, original.parent_hash);
    EXPECT_EQ(result.ommers_hash, original.ommers_hash);
    EXPECT_EQ(result.beneficiary, original.beneficiary);
    EXPECT_EQ(result.state_root, original.state_root);
    EXPECT_EQ(result.transactions_root, original.transactions_root);
    EXPECT_EQ(result.receipts_root, original.receipts_root);
    EXPECT_EQ(result.logs_bloom, original.logs_bloom);
    EXPECT_EQ(result.difficulty, original.difficulty);
    EXPECT_EQ(result.number, original.number);
    EXPECT_EQ(result.gas_limit, original.gas_limit);
    EXPECT_EQ(result.gas_used, original.gas_used);
    EXPECT_EQ(result.timestamp, original.timestamp);
    EXPECT_EQ(result.extra_data, original.extra_data);
    EXPECT_EQ(result.mix_hash, original.mix_hash);
    EXPECT_EQ(result.nonce, original.nonce);
    ASSERT_TRUE(result.base_fee_per_gas.has_value());
    EXPECT_EQ(result.base_fee_per_gas.value(), original.base_fee_per_gas.value());
}

