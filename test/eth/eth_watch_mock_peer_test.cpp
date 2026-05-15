// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/eth_watch_runner.hpp>
#include <eth/messages.hpp>
#include <eth/eth_types.hpp>

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

TEST(EthWatchMockPeerTest, SimulatedReceiptsFlowUpdatesWatchServiceAndEmitsNotification)
{
    const auto token = make_filled<eth::codec::Address>(0xAA);
    const auto from = make_filled<eth::codec::Address>(0x11);
    const auto to = make_filled<eth::codec::Address>(0x22);
    const auto block_hash = make_filled<eth::Hash256>(0xBB);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    eth::EthWatchService watch_service;

    std::vector<uint8_t> get_receipts_payload;
    watch_service.set_send_callback([&get_receipts_payload](uint8_t, std::vector<uint8_t> payload)
    {
        get_receipts_payload = std::move(payload);
    });

    int callback_count = 0;
    watch_service.watch_event(token,
                              "Transfer(address,address,uint256)",
                              params,
                              [&callback_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&)
                              {
                                  ++callback_count;
                              });

    eth::NewBlockHashesMessage hashes;
    hashes.entries.push_back({block_hash, 999});
    auto hashes_encoded = eth::protocol::encode_new_block_hashes(hashes);
    ASSERT_TRUE(hashes_encoded.has_value());

    watch_service.process_message(
        eth::protocol::kNewBlockHashesMessageId,
        rlp::ByteView(hashes_encoded.value().data(), hashes_encoded.value().size()));

    ASSERT_FALSE(get_receipts_payload.empty());

    auto get_receipts = eth::protocol::decode_get_receipts(
        rlp::ByteView(get_receipts_payload.data(), get_receipts_payload.size()));
    ASSERT_TRUE(get_receipts.has_value());
    ASSERT_TRUE(get_receipts.value().request_id.has_value());

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token, from, to, 42ULL));

    eth::ReceiptsMessage receipts;
    receipts.request_id = get_receipts.value().request_id.value();
    receipts.receipts.push_back({receipt});

    auto receipts_encoded = eth::protocol::encode_receipts(receipts);
    ASSERT_TRUE(receipts_encoded.has_value());

    watch_service.process_message(
        eth::protocol::kReceiptsMessageId,
        rlp::ByteView(receipts_encoded.value().data(), receipts_encoded.value().size()));

    EXPECT_EQ(callback_count, 1);
    const auto stats = watch_service.stats();
    EXPECT_EQ(stats.eth_messages_seen, 2U);
    EXPECT_EQ(stats.receipts_requested, 1U);
    EXPECT_EQ(stats.receipts_messages, 1U);
    EXPECT_EQ(stats.matched_logs, 1U);
}

} // namespace

