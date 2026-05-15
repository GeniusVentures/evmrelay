// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlpx/protocol/messages.hpp>
#include <rlpx/rlpx_session.hpp>

using namespace rlpx;
using namespace rlpx::protocol;

class RlpxSessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup if needed
    }

    void TearDown() override {
        // Common cleanup if needed
    }
};

// Test hello message encoding/decoding
TEST_F(RlpxSessionTest, HelloMessageRoundtrip) {
    HelloMessage original;
    original.protocol_version = kProtocolVersion;
    original.client_id = "test-client";
    original.listen_port = 30303;
    for (size_t i = 0; i < original.node_id.size(); ++i) {
        original.node_id[i] = static_cast<uint8_t>(i);
    }
    
    // Add capabilities
    Capability cap1;
    cap1.name = "eth";
    cap1.version = 66;
    original.capabilities.push_back(cap1);
    
    auto encoded = original.encode();
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(encoded.value().empty());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().protocol_version, original.protocol_version);
    EXPECT_EQ(decoded.value().client_id, original.client_id);
    EXPECT_EQ(decoded.value().listen_port, original.listen_port);
    EXPECT_EQ(decoded.value().capabilities.size(), 1);
}

// Test ping message creation
TEST_F(RlpxSessionTest, PingMessageEncoding) {
    PingMessage ping;
    
    auto encoded = ping.encode();
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(encoded.value().empty());
    
    // Verify roundtrip
    auto decoded = PingMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
}

// Test pong message creation
TEST_F(RlpxSessionTest, PongMessageEncoding) {
    PongMessage pong;
    
    auto encoded = pong.encode();
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(encoded.value().empty());
    
    // Verify roundtrip
    auto decoded = PongMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
}

// Test disconnect message with various reasons
TEST_F(RlpxSessionTest, DisconnectMessageReasons) {
    std::vector<DisconnectReason> reasons = {
        DisconnectReason::kRequested,
        DisconnectReason::kTcpError,
        DisconnectReason::kProtocolError,
        DisconnectReason::kUselessPeer,
        DisconnectReason::kTooManyPeers
    };
    
    for (auto reason : reasons) {
        DisconnectMessage msg;
        msg.reason = reason;
        
        auto encoded = msg.encode();
        ASSERT_TRUE(encoded.has_value()) << "Failed to encode reason: " 
                                          << static_cast<int>(reason);
        
        auto decoded = DisconnectMessage::decode(encoded.value());
        ASSERT_TRUE(decoded.has_value()) << "Failed to decode reason: " 
                                          << static_cast<int>(reason);
        EXPECT_EQ(decoded.value().reason, reason);
    }
}

// Test hello message with multiple capabilities
TEST_F(RlpxSessionTest, HelloMessageWithCapabilities) {
    HelloMessage hello;
    hello.protocol_version = kProtocolVersion;
    hello.client_id = "test-client";
    hello.listen_port = 30303;
    
    // Add some capabilities
    Capability cap1;
    cap1.name = "eth";
    cap1.version = 66;
    hello.capabilities.push_back(cap1);
    
    Capability cap2;
    cap2.name = "snap";
    cap2.version = 1;
    hello.capabilities.push_back(cap2);
    
    for (size_t i = 0; i < hello.node_id.size(); ++i) {
        hello.node_id[i] = static_cast<uint8_t>(i);
    }
    
    // Encode and decode
    auto encoded = hello.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    
    // Verify capabilities
    EXPECT_EQ(decoded.value().capabilities.size(), 2);
    if (decoded.value().capabilities.size() >= 2) {
        EXPECT_EQ(decoded.value().capabilities[0].name, "eth");
        EXPECT_EQ(decoded.value().capabilities[0].version, 66);
        EXPECT_EQ(decoded.value().capabilities[1].name, "snap");
        EXPECT_EQ(decoded.value().capabilities[1].version, 1);
    }
}

// Test empty client ID
TEST_F(RlpxSessionTest, HelloMessageEmptyClientId) {
    HelloMessage hello;
    hello.protocol_version = kProtocolVersion;
    hello.client_id = "";
    hello.listen_port = 0;
    
    for (size_t i = 0; i < hello.node_id.size(); ++i) {
        hello.node_id[i] = 0;
    }
    
    auto encoded = hello.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().client_id, "");
}

// Test large client ID
TEST_F(RlpxSessionTest, HelloMessageLargeClientId) {
    HelloMessage hello;
    hello.protocol_version = kProtocolVersion;
    hello.client_id = std::string(256, 'A');
    hello.listen_port = 30303;
    
    for (size_t i = 0; i < hello.node_id.size(); ++i) {
        hello.node_id[i] = static_cast<uint8_t>(i);
    }
    
    auto encoded = hello.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().client_id.size(), 256);
}

TEST_F(RlpxSessionTest, NormalizeEthMessageIdRejectsIdsBelowOffset) {
    const auto normalized = RlpxSession::normalize_eth_message_id_for_test(0x0f, 0x10);

    EXPECT_FALSE(normalized.has_value());
}

TEST_F(RlpxSessionTest, NormalizeEthMessageIdReturnsRelativeIdAtOrAboveOffset) {
    const auto normalized = RlpxSession::normalize_eth_message_id_for_test(0x12, 0x10);

    ASSERT_TRUE(normalized.has_value());
    EXPECT_EQ(*normalized, 0x02);
}

TEST_F(RlpxSessionTest, NegotiateEthVersionReturnsHighestSupportedVersion) {
    std::vector<Capability> capabilities = {
        Capability{"snap", 1},
        Capability{"eth", 66},
        Capability{"eth", 69},
        Capability{"eth", 67}
    };

    EXPECT_EQ(RlpxSession::negotiate_eth_version_for_test(capabilities), 69U);
}

TEST_F(RlpxSessionTest, NegotiateEthOffsetAccountsForSortedCapabilities) {
    std::vector<Capability> capabilities = {
        Capability{"snap", 1},
        Capability{"eth", 66},
        Capability{"eth", 69}
    };

    EXPECT_EQ(RlpxSession::negotiate_eth_offset_for_test(capabilities), 16U);
}
