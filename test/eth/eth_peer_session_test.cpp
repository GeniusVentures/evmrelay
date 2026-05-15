// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <algorithm>
#include <gtest/gtest.h>
#include <eth/messages.hpp>
#include <eth/eth_handshake.hpp>
#include <eth/eth_handshake_guard.hpp>
#include <eth/eth_peer_session.hpp>
#include <rlpx/rlpx_session.hpp>

namespace {

constexpr uint64_t kSepoliaNetworkID = 11155111;

eth::Hash256 make_sepolia_genesis()
{
    eth::Hash256 genesis{};
    const uint8_t raw[32] = {
        0x25, 0xa5, 0xcc, 0x10, 0x6e, 0xea, 0x71, 0x38,
        0xac, 0xab, 0x33, 0x23, 0x1d, 0x71, 0x60, 0xd6,
        0x9c, 0xb7, 0x77, 0xee, 0x0c, 0x2c, 0x55, 0x3f,
        0xcd, 0xdf, 0x51, 0x38, 0x99, 0x3e, 0x6d, 0xd9,
    };
    std::copy(raw, raw + 32, genesis.begin());
    return genesis;
}

eth::ForkId make_sepolia_fork_id()
{
    return eth::ForkId{{0xed, 0x88, 0xb5, 0xfd}, 0};
}

class MockEthSessionChannel final : public eth::IEthSessionChannel
{
public:
    [[nodiscard]] uint8_t negotiated_eth_version() const noexcept override
    {
        return negotiatedEthVersion;
    }

    [[nodiscard]] uint8_t negotiated_eth_offset() const noexcept override
    {
        return negotiatedEthOffset;
    }

    [[nodiscard]] const rlpx::PeerInfo& peer_info() const noexcept override
    {
        return peerInfo;
    }

    [[nodiscard]] rlpx::VoidResult post_message(rlpx::framing::Message message) noexcept override
    {
        postedMessages.push_back(std::move(message));
        return rlp::outcome::success();
    }

    [[nodiscard]] rlpx::Result<rlpx::framing::Message> receive_message(
        boost::asio::yield_context /*yield*/) noexcept override
    {
        if (receivedMessages.empty())
        {
            return rlpx::SessionError::kNotConnected;
        }

        auto message = std::move(receivedMessages.front());
        receivedMessages.erase(receivedMessages.begin());
        return message;
    }

    [[nodiscard]] rlpx::Result<rlpx::framing::Message> receive_message_with_timeout(
        std::chrono::steady_clock::duration /*timeout*/,
        boost::asio::yield_context          yield) noexcept override
    {
        return receive_message(yield);
    }

    void set_eth_message_handler(rlpx::EthMessageHandler handler) noexcept override
    {
        ethMessageHandler = std::move(handler);
    }

    uint8_t negotiatedEthVersion = eth::kEthProtocolVersion69;
    uint8_t negotiatedEthOffset = 0x10;
    rlpx::PeerInfo peerInfo{};
    std::vector<rlpx::framing::Message> postedMessages{};
    std::vector<rlpx::framing::Message> receivedMessages{};
    rlpx::EthMessageHandler ethMessageHandler{};
};

rlpx::ByteBuffer encode_status_payload(const eth::StatusMessage& status)
{
    const auto encoded = eth::protocol::encode_status(status);
    EXPECT_TRUE(encoded.has_value());
    if (!encoded)
    {
        return {};
    }
    return encoded.value();
}

eth::StatusMessage69 make_matching_status69()
{
    eth::StatusMessage69 status;
    status.protocol_version = eth::kEthProtocolVersion69;
    status.network_id = kSepoliaNetworkID;
    status.genesis_hash = make_sepolia_genesis();
    status.fork_id = make_sepolia_fork_id();
    status.earliest_block = 0;
    status.latest_block = 100;
    status.latest_block_hash = make_sepolia_genesis();
    return status;
}

rlpx::ByteBuffer encode_new_block_hashes_payload()
{
    eth::NewBlockHashesMessage hashes;
    hashes.entries.push_back({make_sepolia_genesis(), 1});

    const auto encoded = eth::protocol::encode_new_block_hashes(hashes);
    EXPECT_TRUE(encoded.has_value());
    if (!encoded)
    {
        return {};
    }
    return encoded.value();
}

} // namespace

TEST(EthPeerSessionTest, BuildLocalStatusUsesEth69LayoutForEth69)
{
    const auto status = eth::BuildLocalStatusMessage(
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis(),
        make_sepolia_fork_id());

    ASSERT_TRUE(std::holds_alternative<eth::StatusMessage69>(status));

    const auto& status69 = std::get<eth::StatusMessage69>(status);
    EXPECT_EQ(status69.protocol_version, eth::kEthProtocolVersion69);
    EXPECT_EQ(status69.network_id, kSepoliaNetworkID);
    EXPECT_EQ(status69.genesis_hash, make_sepolia_genesis());
    EXPECT_EQ(status69.fork_id.fork_hash, make_sepolia_fork_id().fork_hash);
}

TEST(EthPeerSessionTest, BuildLocalStatusUsesEth68LayoutForEth68)
{
    const auto status = eth::BuildLocalStatusMessage(
        eth::kEthProtocolVersion68,
        kSepoliaNetworkID,
        make_sepolia_genesis(),
        make_sepolia_fork_id());

    ASSERT_TRUE(std::holds_alternative<eth::StatusMessage68>(status));

    const auto& status68 = std::get<eth::StatusMessage68>(status);
    EXPECT_EQ(status68.protocol_version, eth::kEthProtocolVersion68);
    EXPECT_EQ(status68.network_id, kSepoliaNetworkID);
    EXPECT_EQ(status68.genesis_hash, make_sepolia_genesis());
    EXPECT_EQ(status68.blockhash, make_sepolia_genesis());
}

TEST(EthPeerSessionTest, ValidateRemoteStatusAcceptsMatchingStatus)
{
    eth::StatusMessage69 remote_status;
    remote_status.protocol_version = eth::kEthProtocolVersion69;
    remote_status.network_id = kSepoliaNetworkID;
    remote_status.genesis_hash = make_sepolia_genesis();
    remote_status.fork_id = make_sepolia_fork_id();
    remote_status.earliest_block = 0;
    remote_status.latest_block = 100;
    remote_status.latest_block_hash = make_sepolia_genesis();

    const auto result = eth::ValidateRemoteStatusMessage(
        remote_status,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    EXPECT_TRUE(result.has_value());
}

TEST(EthPeerSessionTest, ValidateRemoteStatusRejectsProtocolVersionMismatch)
{
    eth::StatusMessage68 remote_status;
    remote_status.protocol_version = eth::kEthProtocolVersion68;
    remote_status.network_id = kSepoliaNetworkID;
    remote_status.genesis_hash = make_sepolia_genesis();
    remote_status.fork_id = make_sepolia_fork_id();
    remote_status.td = 0;
    remote_status.blockhash = make_sepolia_genesis();

    const auto result = eth::ValidateRemoteStatusMessage(
        remote_status,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kProtocolVersionMismatch);
}

TEST(EthPeerSessionTest, ValidateRemoteStatusRejectsNetworkMismatch)
{
    eth::StatusMessage69 remote_status;
    remote_status.protocol_version = eth::kEthProtocolVersion69;
    remote_status.network_id = 999;
    remote_status.genesis_hash = make_sepolia_genesis();
    remote_status.fork_id = make_sepolia_fork_id();
    remote_status.earliest_block = 0;
    remote_status.latest_block = 100;
    remote_status.latest_block_hash = make_sepolia_genesis();

    const auto result = eth::ValidateRemoteStatusMessage(
        remote_status,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kNetworkIDMismatch);
}

TEST(EthPeerSessionTest, ValidateRemoteStatusRejectsGenesisMismatch)
{
    auto wrong_genesis = make_sepolia_genesis();
    wrong_genesis[0] ^= 0xFF;

    eth::StatusMessage69 remote_status;
    remote_status.protocol_version = eth::kEthProtocolVersion69;
    remote_status.network_id = kSepoliaNetworkID;
    remote_status.genesis_hash = wrong_genesis;
    remote_status.fork_id = make_sepolia_fork_id();
    remote_status.earliest_block = 0;
    remote_status.latest_block = 100;
    remote_status.latest_block_hash = wrong_genesis;

    const auto result = eth::ValidateRemoteStatusMessage(
        remote_status,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kGenesisMismatch);
}

TEST(EthPeerSessionTest, ValidateRemoteStatusRejectsInvalidBlockRange)
{
    eth::StatusMessage69 remote_status;
    remote_status.protocol_version = eth::kEthProtocolVersion69;
    remote_status.network_id = kSepoliaNetworkID;
    remote_status.genesis_hash = make_sepolia_genesis();
    remote_status.fork_id = make_sepolia_fork_id();
    remote_status.earliest_block = 101;
    remote_status.latest_block = 100;
    remote_status.latest_block_hash = make_sepolia_genesis();

    const auto result = eth::ValidateRemoteStatusMessage(
        remote_status,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kInvalidBlockRange);
}

TEST(EthPeerSessionTest, PerformEthStatusHandshakeQueuesLocalStatusAndAcceptsRemoteStatus)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    rlpx::framing::Message remoteStatusMessage{};
    remoteStatusMessage.id = static_cast<uint8_t>(channel->negotiatedEthOffset + eth::protocol::kStatusMessageId);
    remoteStatusMessage.payload = encode_status_payload(make_matching_status69());
    channel->receivedMessages.push_back(std::move(remoteStatusMessage));

    boost::asio::io_context io;
    rlp::outcome::result<eth::EthStatusHandshakeResult, eth::StatusValidationError, rlp::outcome::policy::all_narrow> result =
        rlp::outcome::failure(eth::StatusValidationError::kProtocolVersionMismatch);

    boost::asio::spawn(io, [&channel, &result](boost::asio::yield_context yield)
    {
        result = eth::PerformEthStatusHandshake(
            eth::EthStatusHandshakeStart{
                channel,
                kSepoliaNetworkID,
                make_sepolia_genesis(),
                make_sepolia_fork_id(),
                eth::EthStatusAcceptedHandler{},
                rlpx::EthMessageHandler{}
            },
            yield);
    });
    io.run();

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(channel->postedMessages.size(), 1U);
    EXPECT_EQ(channel->postedMessages.front().id,
              static_cast<uint8_t>(channel->negotiatedEthOffset + eth::protocol::kStatusMessageId));
    EXPECT_EQ(eth::ExtractLatestBlockNumber(result.value().remote_status), 100U);
}

TEST(EthPeerSessionTest, PerformEthStatusHandshakeRejectsPreHandshakeNonStatusMessage)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    rlpx::framing::Message nonStatusMessage{};
    nonStatusMessage.id = static_cast<uint8_t>(channel->negotiatedEthOffset + eth::protocol::kNewBlockHashesMessageId);
    nonStatusMessage.payload = encode_new_block_hashes_payload();
    channel->receivedMessages.push_back(std::move(nonStatusMessage));

    boost::asio::io_context io;
    rlp::outcome::result<eth::EthStatusHandshakeResult, eth::StatusValidationError, rlp::outcome::policy::all_narrow> result =
        rlp::outcome::failure(eth::StatusValidationError::kProtocolVersionMismatch);

    boost::asio::spawn(io, [&channel, &result](boost::asio::yield_context yield)
    {
        result = eth::PerformEthStatusHandshake(
            eth::EthStatusHandshakeStart{
                channel,
                kSepoliaNetworkID,
                make_sepolia_genesis(),
                make_sepolia_fork_id(),
                eth::EthStatusAcceptedHandler{},
                rlpx::EthMessageHandler{}
            },
            yield);
    });
    io.run();

    ASSERT_FALSE(result.has_value());
}

TEST(EthPeerSessionTest, StartEthStatusHandshakeInstallsPostHandshakeForwarder)
{
    auto channel = std::make_shared<MockEthSessionChannel>();
    std::vector<uint8_t> forwardedPayload{};

    const bool started = eth::StartEthStatusHandshake(
        eth::EthStatusHandshakeStart{
            channel,
            kSepoliaNetworkID,
            make_sepolia_genesis(),
            make_sepolia_fork_id(),
            eth::EthStatusAcceptedHandler{},
            [&forwardedPayload](uint8_t ethMessageId, const rlpx::ByteBuffer& payload)
            {
                EXPECT_EQ(ethMessageId, eth::protocol::kNewBlockHashesMessageId);
                forwardedPayload = payload;
            }
        });

    ASSERT_TRUE(started);
    ASSERT_TRUE(static_cast<bool>(channel->ethMessageHandler));

    const auto newBlockHashesPayload = encode_new_block_hashes_payload();
    channel->ethMessageHandler(eth::protocol::kNewBlockHashesMessageId, newBlockHashesPayload);

    EXPECT_EQ(forwardedPayload, newBlockHashesPayload);
}
