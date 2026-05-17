// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/eth_handshake_guard.hpp>
#include <eth/messages.hpp>

namespace {

constexpr uint64_t kSepoliaNetworkID = 11155111;
constexpr uint8_t kNegotiatedEthOffset = 0x10;

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

rlpx::protocol::Message make_status_message()
{
    eth::StatusMessage69 status;
    status.protocol_version = eth::kEthProtocolVersion69;
    status.network_id = kSepoliaNetworkID;
    status.genesis_hash = make_sepolia_genesis();
    status.fork_id = make_sepolia_fork_id();
    status.earliest_block = 0;
    status.latest_block = 100;
    status.latest_block_hash = make_sepolia_genesis();

    auto encoded = eth::protocol::encode_status(status);
    EXPECT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kStatusMessageId);
    message.payload = std::move(encoded.value());
    return message;
}

rlpx::protocol::Message make_new_block_hashes_message()
{
    eth::NewBlockHashesMessage hashes;
    hashes.entries.push_back({make_sepolia_genesis(), 1});

    auto encoded = eth::protocol::encode_new_block_hashes(hashes);
    EXPECT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kNewBlockHashesMessageId);
    message.payload = std::move(encoded.value());
    return message;
}

rlpx::protocol::Message make_invalid_status_message()
{
    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kStatusMessageId);
    message.payload = {0x01, 0x02, 0x03};
    return message;
}

rlpx::protocol::Message make_status_message_with_network_id(uint64_t network_id)
{
    eth::StatusMessage69 status;
    status.protocol_version = eth::kEthProtocolVersion69;
    status.network_id = network_id;
    status.genesis_hash = make_sepolia_genesis();
    status.fork_id = make_sepolia_fork_id();
    status.earliest_block = 0;
    status.latest_block = 100;
    status.latest_block_hash = make_sepolia_genesis();

    auto encoded = eth::protocol::encode_status(status);
    EXPECT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kStatusMessageId);
    message.payload = std::move(encoded.value());
    return message;
}

rlpx::protocol::Message make_status_message_with_genesis(const eth::Hash256& genesis_hash)
{
    eth::StatusMessage69 status;
    status.protocol_version = eth::kEthProtocolVersion69;
    status.network_id = kSepoliaNetworkID;
    status.genesis_hash = genesis_hash;
    status.fork_id = make_sepolia_fork_id();
    status.earliest_block = 0;
    status.latest_block = 100;
    status.latest_block_hash = genesis_hash;

    auto encoded = eth::protocol::encode_status(status);
    EXPECT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kStatusMessageId);
    message.payload = std::move(encoded.value());
    return message;
}

rlpx::protocol::Message make_status_message_with_block_range(uint64_t earliest_block, uint64_t latest_block)
{
    eth::StatusMessage69 status;
    status.protocol_version = eth::kEthProtocolVersion69;
    status.network_id = kSepoliaNetworkID;
    status.genesis_hash = make_sepolia_genesis();
    status.fork_id = make_sepolia_fork_id();
    status.earliest_block = earliest_block;
    status.latest_block = latest_block;
    status.latest_block_hash = make_sepolia_genesis();

    auto encoded = eth::protocol::encode_status(status);
    EXPECT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kStatusMessageId);
    message.payload = std::move(encoded.value());
    return message;
}

TEST(EthHandshakeGuardTest, NormalizeEthWireMessageIdRejectsIdsBelowOffset)
{
    const auto normalized = eth::NormalizeEthWireMessageId(0x0f, kNegotiatedEthOffset);

    EXPECT_FALSE(normalized.has_value());
}

TEST(EthHandshakeGuardTest, NormalizeEthWireMessageIdReturnsEthLocalId)
{
    const auto normalized = eth::NormalizeEthWireMessageId(0x11, kNegotiatedEthOffset);

    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(normalized.value(), 1U);
}

TEST(EthHandshakeGuardTest, NormalizeEthWireMessageIdAcceptsBaseProtocolMessages)
{
    const auto normalized = eth::NormalizeEthWireMessageId(
        eth::protocol::kStatusMessageId,
        0U);

    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(normalized.value(), eth::protocol::kStatusMessageId);
}

TEST(EthHandshakeGuardTest, HandleHandshakeAcceptsValidStatusFirst)
{
    bool statusReceived = false;

    const auto disposition = eth::HandleEthHandshakeMessage(
        make_status_message(),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis(),
        statusReceived);

    EXPECT_EQ(disposition, eth::HandshakeMessageDisposition::kAcceptedStatus);
    EXPECT_TRUE(statusReceived);
}

TEST(EthHandshakeGuardTest, HandleHandshakeRejectsNonStatusBeforeHandshake)
{
    bool statusReceived = false;

    const auto disposition = eth::HandleEthHandshakeMessage(
        make_new_block_hashes_message(),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis(),
        statusReceived);

    EXPECT_EQ(disposition, eth::HandshakeMessageDisposition::kRejected);
    EXPECT_FALSE(statusReceived);
}

TEST(EthHandshakeGuardTest, HandleHandshakeIgnoresNonStatusAfterHandshake)
{
    bool statusReceived = true;

    const auto disposition = eth::HandleEthHandshakeMessage(
        make_new_block_hashes_message(),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis(),
        statusReceived);

    EXPECT_EQ(disposition, eth::HandshakeMessageDisposition::kIgnored);
    EXPECT_TRUE(statusReceived);
}

TEST(EthHandshakeGuardTest, DecodeValidatedStatusMessageRejectsInvalidPayload)
{
    const auto decoded = eth::DecodeValidatedStatusMessage(
        make_invalid_status_message(),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), eth::StatusValidationError::kProtocolVersionMismatch);
}

TEST(EthHandshakeGuardTest, DecodeValidatedStatusMessageRejectsNetworkMismatch)
{
    const auto decoded = eth::DecodeValidatedStatusMessage(
        make_status_message_with_network_id(999),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), eth::StatusValidationError::kNetworkIDMismatch);
}

TEST(EthHandshakeGuardTest, DecodeValidatedStatusMessageRejectsGenesisMismatch)
{
    auto wrong_genesis = make_sepolia_genesis();
    wrong_genesis[0] ^= 0xFF;

    const auto decoded = eth::DecodeValidatedStatusMessage(
        make_status_message_with_genesis(wrong_genesis),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), eth::StatusValidationError::kGenesisMismatch);
}

TEST(EthHandshakeGuardTest, DecodeValidatedStatusMessageRejectsInvalidBlockRange)
{
    const auto decoded = eth::DecodeValidatedStatusMessage(
        make_status_message_with_block_range(101, 100),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    EXPECT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error(), eth::StatusValidationError::kInvalidBlockRange);
}

TEST(EthHandshakeGuardTest, ExtractLatestBlockNumberReturnsEth69LatestBlock)
{
    const auto decoded = eth::DecodeValidatedStatusMessage(
        make_status_message(),
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion69,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(eth::ExtractLatestBlockNumber(decoded.value()), 100U);
}

TEST(EthHandshakeGuardTest, DecodeValidatedStatusMessageAcceptsBaseProtocolStatusAtZeroOffset)
{
    eth::StatusMessage68 status;
    status.protocol_version = eth::kEthProtocolVersion68;
    status.network_id = kSepoliaNetworkID;
    status.genesis_hash = make_sepolia_genesis();
    status.fork_id = make_sepolia_fork_id();
    status.td = 0;
    status.blockhash = make_sepolia_genesis();

    auto encoded = eth::protocol::encode_status(status);
    ASSERT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = eth::protocol::kStatusMessageId;
    message.payload = std::move(encoded.value());

    const auto decoded = eth::DecodeValidatedStatusMessage(
        message,
        0U,
        eth::kEthProtocolVersion68,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(eth::get_common_fields(decoded.value()).protocol_version, eth::kEthProtocolVersion68);
}

TEST(EthHandshakeGuardTest, DecodeValidatedStatusMessageAcceptsEth68AtNegotiatedOffset)
{
    eth::StatusMessage68 status;
    status.protocol_version = eth::kEthProtocolVersion68;
    status.network_id = kSepoliaNetworkID;
    status.genesis_hash = make_sepolia_genesis();
    status.fork_id = make_sepolia_fork_id();
    status.td = 0;
    status.blockhash = make_sepolia_genesis();

    auto encoded = eth::protocol::encode_status(status);
    ASSERT_TRUE(encoded.has_value());

    rlpx::protocol::Message message{};
    message.id = static_cast<uint8_t>(kNegotiatedEthOffset + eth::protocol::kStatusMessageId);
    message.payload = std::move(encoded.value());

    const auto decoded = eth::DecodeValidatedStatusMessage(
        message,
        kNegotiatedEthOffset,
        eth::kEthProtocolVersion68,
        kSepoliaNetworkID,
        make_sepolia_genesis());

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(eth::get_common_fields(decoded.value()).protocol_version, eth::kEthProtocolVersion68);
}

} // namespace

