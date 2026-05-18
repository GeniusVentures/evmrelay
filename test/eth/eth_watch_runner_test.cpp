// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/eth_watch_dialer.hpp>
#include <eth/eth_watch_runner.hpp>
#include <eth/messages.hpp>
#include <eth/eth_types.hpp>
#include <boost/asio/spawn.hpp>
#include <chrono>

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

discv4::ValidatedPeer make_validated_peer(
    const uint8_t seed,
    const uint16_t port)
{
    discv4::ValidatedPeer peer{};
    for (size_t i = 0; i < peer.peer.node_id.size(); ++i)
    {
        peer.peer.node_id[i] = static_cast<uint8_t>(seed + i);
        peer.pubkey[i] = peer.peer.node_id[i];
    }
    peer.peer.ip = "127.0.0.1";
    peer.peer.udp_port = port;
    peer.peer.tcp_port = port;
    peer.peer.last_seen = std::chrono::steady_clock::now();
    return peer;
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

class MockEthSessionChannel final : public eth::IEthSessionChannel
{
public:
    uint8_t negotiatedVersion = eth::kEthProtocolVersion69;
    uint8_t negotiatedOffset = 0x10;
    rlpx::PeerInfo peerInfo{};
    rlpx::EthMessageHandler ethHandler{};
    std::vector<rlpx::framing::Message> sentMessages{};
    bool failPostMessage = false;

    [[nodiscard]] uint8_t negotiated_eth_version() const noexcept override
    {
        return negotiatedVersion;
    }

    [[nodiscard]] uint8_t negotiated_eth_offset() const noexcept override
    {
        return negotiatedOffset;
    }

    [[nodiscard]] const rlpx::PeerInfo& peer_info() const noexcept override
    {
        return peerInfo;
    }

    [[nodiscard]] rlpx::VoidResult post_message(rlpx::framing::Message message) noexcept override
    {
        if (failPostMessage)
        {
            return rlpx::SessionError::kConnectionFailed;
        }
        sentMessages.push_back(std::move(message));
        return rlp::outcome::success();
    }

    [[nodiscard]] rlpx::Result<rlpx::framing::Message> receive_message(
        boost::asio::yield_context /*yield*/) noexcept override
    {
        return rlpx::SessionError::kNotConnected;
    }

    [[nodiscard]] rlpx::Result<rlpx::framing::Message> receive_message_with_timeout(
        std::chrono::steady_clock::duration /*timeout*/,
        boost::asio::yield_context          /*yield*/) noexcept override
    {
        return rlpx::SessionError::kNotConnected;
    }

    void set_eth_message_handler(rlpx::EthMessageHandler handler) noexcept override
    {
        ethHandler = std::move(handler);
    }
};

TEST(EthWatchRunnerTest, NotificationContextCarriesChainMetadata)
{
    eth::WatchEventNotification notification;
    notification.context.chain_name = "ethereum-mainnet";
    notification.context.network_id = 1;
    notification.context.peer_client_id = "test-peer";
    notification.context.peer_address = "127.0.0.1:30303";

    EXPECT_EQ(notification.context.chain_name, "ethereum-mainnet");
    EXPECT_EQ(notification.context.network_id, 1U);
    EXPECT_EQ(notification.context.peer_client_id, "test-peer");
    EXPECT_EQ(notification.context.peer_address, "127.0.0.1:30303");
}

TEST(EthWatchRunnerTest, NotificationCarriesDecodedValues)
{
    eth::WatchEventNotification notification;
    notification.event_signature = "Transfer(address,address,uint256)";
    notification.values.push_back(bool{true});

    EXPECT_EQ(notification.event_signature, "Transfer(address,address,uint256)");
    ASSERT_EQ(notification.values.size(), 1U);
    ASSERT_NE(std::get_if<bool>(&notification.values[0]), nullptr);
    EXPECT_TRUE(*std::get_if<bool>(&notification.values[0]));
}

TEST(EthWatchRunnerTest, NotificationContextDefaultsAreEmpty)
{
    const eth::WatchEventNotification notification{};

    EXPECT_TRUE(notification.context.chain_name.empty());
    EXPECT_EQ(notification.context.network_id, 0U);
    EXPECT_TRUE(notification.context.peer_client_id.empty());
    EXPECT_TRUE(notification.context.peer_address.empty());
    EXPECT_TRUE(notification.event_signature.empty());
    EXPECT_TRUE(notification.values.empty());
}

TEST(EthWatchRunnerTest, ConnectionConfigDefaultsToThreePerChain)
{
    const eth::EthWatchConnectionConfig config{};

    EXPECT_EQ(config.max_total_connections, 24);
    EXPECT_EQ(config.max_connections_per_chain, 3);
}

TEST(EthWatchRunnerTest, ConnectionConfigStoresOverrides)
{
    eth::EthWatchConnectionConfig config{};
    config.max_total_connections = 12;
    config.max_connections_per_chain = 2;

    EXPECT_EQ(config.max_total_connections, 12);
    EXPECT_EQ(config.max_connections_per_chain, 2);
}

TEST(EthWatchRunnerTest, MakeEthWatcherPoolUsesConnectionConfig)
{
    eth::EthWatchConnectionConfig config{};
    config.max_total_connections = 7;
    config.max_connections_per_chain = 2;

    const auto pool = eth::make_eth_watcher_pool(config);

    ASSERT_TRUE(static_cast<bool>(pool));
    EXPECT_EQ(pool->max_total, 7);
    EXPECT_EQ(pool->max_per_chain, 2);
}

TEST(EthWatchRunnerTest, StartEthWatchChainPeerDialingEnqueuesPeersBehindActiveLimit)
{
    boost::asio::io_context io;
    eth::EthWatchConnectionConfig config{};
    config.max_total_connections = 1;
    config.max_connections_per_chain = 1;
    const auto pool = eth::make_eth_watcher_pool(config);

    const std::vector<discv4::ValidatedPeer> peers{
        make_validated_peer(0x10, 30303U),
        make_validated_peer(0x20, 30304U)
    };

    const auto scheduler = eth::start_eth_watch_chain_peer_dialing(
        io,
        pool,
        [](discv4::ValidatedPeer,
           std::function<void()>,
           std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
           boost::asio::yield_context)
        {
        },
        peers);

    ASSERT_TRUE(static_cast<bool>(scheduler));
    EXPECT_EQ(scheduler->active, 1);
    EXPECT_EQ(pool->active_total.load(), 1);
    ASSERT_EQ(scheduler->queue.size(), 1U);
    EXPECT_EQ(scheduler->queue.front().peer.tcp_port, 30304U);
    scheduler->stop();
}

TEST(EthWatchRunnerTest, SendLocalStatusPostsStatusAtNegotiatedOffset)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    channel->peerInfo.client_id = "mock-peer";

    eth::ForkId forkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
    eth::EthWatchRunner runner(channel, "ethereum-mainnet", 1, make_filled<eth::Hash256>(0x01), forkId);

    ASSERT_TRUE(runner.send_local_status());
    ASSERT_EQ(channel->sentMessages.size(), 1U);
    EXPECT_EQ(channel->sentMessages[0].id, 0x10);
}

TEST(EthWatchRunnerTest, InstallSessionBridgeNormalizesAndProcessesEthMessages)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    channel->peerInfo.client_id = "mock-peer";

    eth::ForkId forkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
    eth::EthWatchRunner runner(channel, "ethereum-mainnet", 1, make_filled<eth::Hash256>(0x01), forkId);
    runner.install_session_bridge();

    ASSERT_TRUE(static_cast<bool>(channel->ethHandler));

    eth::NewBlockHashesMessage hashes;
    hashes.entries.push_back({make_filled<eth::Hash256>(0x22), 999});
    auto encoded = eth::protocol::encode_new_block_hashes(hashes);
    ASSERT_TRUE(encoded.has_value());

    channel->ethHandler(eth::protocol::kNewBlockHashesMessageId, encoded.value());

    const auto stats = runner.service().stats();
    EXPECT_EQ(stats.eth_messages_seen, 1U);
    EXPECT_EQ(stats.new_block_hashes_messages, 1U);
    ASSERT_EQ(channel->sentMessages.size(), 1U);
    EXPECT_EQ(channel->sentMessages[0].id, 0x1f);
}

TEST(EthWatchRunnerTest, SendLocalStatusFailsWithoutNegotiatedVersion)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    channel->negotiatedVersion = 0;

    eth::ForkId forkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
    eth::EthWatchRunner runner(channel, "ethereum-mainnet", 1, make_filled<eth::Hash256>(0x01), forkId);

    EXPECT_FALSE(runner.send_local_status());
    EXPECT_TRUE(channel->sentMessages.empty());
}

TEST(EthWatchRunnerTest, SendLocalStatusFailsWithoutNegotiatedOffset)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    channel->negotiatedOffset = 0;

    eth::ForkId forkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
    eth::EthWatchRunner runner(channel, "ethereum-mainnet", 1, make_filled<eth::Hash256>(0x01), forkId);

    EXPECT_FALSE(runner.send_local_status());
    EXPECT_TRUE(channel->sentMessages.empty());
}

TEST(EthWatchRunnerTest, SendLocalStatusFailsWhenChannelSendFails)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    channel->failPostMessage = true;

    eth::ForkId forkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
    eth::EthWatchRunner runner(channel, "ethereum-mainnet", 1, make_filled<eth::Hash256>(0x01), forkId);

    EXPECT_FALSE(runner.send_local_status());
    EXPECT_TRUE(channel->sentMessages.empty());
}

TEST(EthWatchRunnerTest, WatchEventEmitsEnrichedCallbackWithChainAndPeerMetadata)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    channel->peerInfo.client_id = "mock-peer";
    channel->peerInfo.remote_address = "127.0.0.1:30303";

    const auto token = make_filled<eth::codec::Address>(0xAA);
    const auto from = make_filled<eth::codec::Address>(0x11);
    const auto to = make_filled<eth::codec::Address>(0x22);
    const auto blockHash = make_filled<eth::Hash256>(0xBB);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    eth::ForkId forkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
    eth::EthWatchRunner runner(channel, "ethereum-mainnet", 1, make_filled<eth::Hash256>(0x01), forkId);

    std::optional<eth::WatchEventNotification> notification;
    runner.set_event_callback([&notification](const eth::WatchEventNotification& value)
    {
        notification = value;
    });

    (void)runner.watch_event(token, "Transfer(address,address,uint256)", params);
    runner.install_session_bridge();

    ASSERT_TRUE(static_cast<bool>(channel->ethHandler));

    eth::NewBlockHashesMessage hashes;
    hashes.entries.push_back({blockHash, 999});
    auto hashesEncoded = eth::protocol::encode_new_block_hashes(hashes);
    ASSERT_TRUE(hashesEncoded.has_value());
    channel->ethHandler(eth::protocol::kNewBlockHashesMessageId, hashesEncoded.value());

    ASSERT_EQ(channel->sentMessages.size(), 1U);

    auto getReceipts = eth::protocol::decode_get_receipts(
        rlp::ByteView(channel->sentMessages[0].payload.data(), channel->sentMessages[0].payload.size()));
    ASSERT_TRUE(getReceipts.has_value());
    ASSERT_TRUE(getReceipts.value().request_id.has_value());

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(token, from, to, 42ULL));

    eth::ReceiptsMessage receipts;
    receipts.request_id = getReceipts.value().request_id.value();
    receipts.receipts.push_back({receipt});

    auto receiptsEncoded = eth::protocol::encode_receipts(receipts);
    ASSERT_TRUE(receiptsEncoded.has_value());
    channel->ethHandler(eth::protocol::kReceiptsMessageId, receiptsEncoded.value());

    ASSERT_TRUE(notification.has_value());
    EXPECT_EQ(notification->context.chain_name, "ethereum-mainnet");
    EXPECT_EQ(notification->context.network_id, 1U);
    EXPECT_EQ(notification->context.peer_client_id, "mock-peer");
    EXPECT_EQ(notification->context.peer_address, "127.0.0.1:30303");
    EXPECT_EQ(notification->event_signature, "Transfer(address,address,uint256)");
}

} // namespace
