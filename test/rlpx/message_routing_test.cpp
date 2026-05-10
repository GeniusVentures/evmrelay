// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <rlpx/protocol/messages.hpp>
#include <rlpx/rlpx_session.hpp>

using namespace rlpx;
using namespace rlpx::protocol;

class MessageRoutingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup if needed
    }

    void TearDown() override {
        // Common cleanup if needed
    }
};

// Test message type identification
TEST_F(MessageRoutingTest, MessageTypeIdentification) {
    Message hello_msg;
    hello_msg.id = kHelloMessageId;
    EXPECT_TRUE(hello_msg.is_hello());
    EXPECT_FALSE(hello_msg.is_disconnect());
    EXPECT_FALSE(hello_msg.is_ping());
    EXPECT_FALSE(hello_msg.is_pong());

    Message disconnect_msg;
    disconnect_msg.id = kDisconnectMessageId;
    EXPECT_FALSE(disconnect_msg.is_hello());
    EXPECT_TRUE(disconnect_msg.is_disconnect());
    EXPECT_FALSE(disconnect_msg.is_ping());
    EXPECT_FALSE(disconnect_msg.is_pong());

    Message ping_msg;
    ping_msg.id = kPingMessageId;
    EXPECT_FALSE(ping_msg.is_hello());
    EXPECT_FALSE(ping_msg.is_disconnect());
    EXPECT_TRUE(ping_msg.is_ping());
    EXPECT_FALSE(ping_msg.is_pong());

    Message pong_msg;
    pong_msg.id = kPongMessageId;
    EXPECT_FALSE(pong_msg.is_hello());
    EXPECT_FALSE(pong_msg.is_disconnect());
    EXPECT_FALSE(pong_msg.is_ping());
    EXPECT_TRUE(pong_msg.is_pong());
}

// Test message creation with encoded payload
TEST_F(MessageRoutingTest, MessageCreationWithPayload) {
    // Create a Ping message
    PingMessage ping;
    auto ping_encoded = ping.encode();
    ASSERT_TRUE(ping_encoded.has_value());

    // Wrap in generic Message
    Message msg;
    msg.id = kPingMessageId;
    msg.payload = std::move(ping_encoded.value());

    EXPECT_EQ(msg.id, kPingMessageId);
    EXPECT_TRUE(msg.is_ping());
    EXPECT_FALSE(msg.payload.empty());
}

// Test hello message wrapping
TEST_F(MessageRoutingTest, HelloMessageWrapping) {
    HelloMessage hello;
    hello.protocol_version = kProtocolVersion;
    hello.client_id = "test-client";
    hello.listen_port = 30303;
    for (size_t i = 0; i < hello.node_id.size(); ++i) {
        hello.node_id[i] = static_cast<uint8_t>(i);
    }

    auto encoded = hello.encode();
    ASSERT_TRUE(encoded.has_value());

    Message msg;
    msg.id = kHelloMessageId;
    msg.payload = std::move(encoded.value());

    EXPECT_TRUE(msg.is_hello());
    EXPECT_FALSE(msg.payload.empty());

    // Verify can decode back
    auto decoded = HelloMessage::decode(msg.payload);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().client_id, "test-client");
    EXPECT_EQ(decoded.value().listen_port, 30303);
}

// Test disconnect message wrapping
TEST_F(MessageRoutingTest, DisconnectMessageWrapping) {
    DisconnectMessage disconnect;
    disconnect.reason = DisconnectReason::kClientQuitting;

    auto encoded = disconnect.encode();
    ASSERT_TRUE(encoded.has_value());

    Message msg;
    msg.id = kDisconnectMessageId;
    msg.payload = std::move(encoded.value());

    EXPECT_TRUE(msg.is_disconnect());
    EXPECT_FALSE(msg.payload.empty());

    // Verify can decode back
    auto decoded = DisconnectMessage::decode(msg.payload);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().reason, DisconnectReason::kClientQuitting);
}

// Test message ID constants
TEST_F(MessageRoutingTest, MessageIdConstants) {
    EXPECT_EQ(kHelloMessageId, 0x00);
    EXPECT_EQ(kDisconnectMessageId, 0x01);
    EXPECT_EQ(kPingMessageId, 0x02);
    EXPECT_EQ(kPongMessageId, 0x03);

    // Ensure they're unique
    EXPECT_NE(kHelloMessageId, kDisconnectMessageId);
    EXPECT_NE(kHelloMessageId, kPingMessageId);
    EXPECT_NE(kDisconnectMessageId, kPingMessageId);
}

// Test unknown message type
TEST_F(MessageRoutingTest, UnknownMessageType) {
    Message msg;
    msg.id = 0xFF;  // Unknown message ID

    EXPECT_FALSE(msg.is_hello());
    EXPECT_FALSE(msg.is_disconnect());
    EXPECT_FALSE(msg.is_ping());
    EXPECT_FALSE(msg.is_pong());
}

// Test multiple messages with different IDs
TEST_F(MessageRoutingTest, MultipleMessageTypes) {
    std::vector<Message> messages;

    // Add Hello
    {
        HelloMessage hello;
        hello.protocol_version = kProtocolVersion;
        hello.client_id = "test";
        hello.listen_port = 30303;
        for (size_t i = 0; i < hello.node_id.size(); ++i) {
            hello.node_id[i] = 0;
        }
        auto encoded = hello.encode();
        ASSERT_TRUE(encoded.has_value());

        Message msg;
        msg.id = kHelloMessageId;
        msg.payload = std::move(encoded.value());
        messages.push_back(std::move(msg));
    }

    // Add Ping
    {
        PingMessage ping;
        auto encoded = ping.encode();
        ASSERT_TRUE(encoded.has_value());

        Message msg;
        msg.id = kPingMessageId;
        msg.payload = std::move(encoded.value());
        messages.push_back(std::move(msg));
    }

    // Add Disconnect
    {
        DisconnectMessage disconnect;
        disconnect.reason = DisconnectReason::kRequested;
        auto encoded = disconnect.encode();
        ASSERT_TRUE(encoded.has_value());

        Message msg;
        msg.id = kDisconnectMessageId;
        msg.payload = std::move(encoded.value());
        messages.push_back(std::move(msg));
    }

    ASSERT_EQ(messages.size(), 3);
    EXPECT_TRUE(messages[0].is_hello());
    EXPECT_TRUE(messages[1].is_ping());
    EXPECT_TRUE(messages[2].is_disconnect());
}

TEST_F(MessageRoutingTest, NegotiatedEthWireIdNormalizesToLocalEthId) {
    const auto local_eth_id = rlpx::RlpxSession::normalize_eth_message_id_for_test(0x11, 0x10);
    ASSERT_TRUE(local_eth_id.has_value());
    EXPECT_EQ(*local_eth_id, 0x01);
}

TEST_F(MessageRoutingTest, WireIdBelowNegotiatedEthOffsetDoesNotNormalize) {
    const auto local_eth_id = rlpx::RlpxSession::normalize_eth_message_id_for_test(0x03, 0x10);
    EXPECT_FALSE(local_eth_id.has_value());
}

TEST_F(MessageRoutingTest, ZeroNegotiatedEthOffsetDoesNotNormalize) {
    const auto local_eth_id = rlpx::RlpxSession::normalize_eth_message_id_for_test(0x10, 0x00);
    EXPECT_FALSE(local_eth_id.has_value());
}
