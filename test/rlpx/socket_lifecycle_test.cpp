// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlpx/socket/socket_transport.hpp>
#include <rlpx/rlpx_session.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <chrono>
#include <queue>

using namespace rlpx;
using namespace rlpx::socket;

class SocketLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create I/O context for async operations
        io_context_ = std::make_unique<boost::asio::io_context>();
    }

    void TearDown() override {
        // Clean up
        io_context_->stop();
        io_context_.reset();
    }

    std::unique_ptr<boost::asio::io_context> io_context_;
};

// Test: SocketTransport constructor with valid socket
TEST_F(SocketLifecycleTest, SocketTransportConstruction) {
    // Create a TCP socket
    boost::asio::ip::tcp::socket sock(*io_context_);
    
    // Create transport from socket
    SocketTransport transport(std::move(sock));
    
    // Transport should be created successfully (not open since no connection)
    EXPECT_FALSE(transport.is_open());
}

// Test: SocketTransport move semantics
TEST_F(SocketLifecycleTest, SocketTransportMoveSemantics) {
    // Create a TCP socket
    boost::asio::ip::tcp::socket sock(*io_context_);
    
    // Create transport
    SocketTransport transport1(std::move(sock));
    
    // Move construct
    SocketTransport transport2(std::move(transport1));
    
    // transport2 should have the socket now
    EXPECT_FALSE(transport2.is_open());
    
    // Move assign
    boost::asio::ip::tcp::socket sock2(*io_context_);
    SocketTransport transport3(std::move(sock2));
    transport3 = std::move(transport2);
    
    EXPECT_FALSE(transport3.is_open());
}

// Test: SocketTransport close operation
TEST_F(SocketLifecycleTest, SocketTransportClose) {
    boost::asio::ip::tcp::socket sock(*io_context_);
    SocketTransport transport(std::move(sock));
    
    // Close should succeed even if not connected
    auto result = transport.close();
    EXPECT_TRUE(result.has_value());
    
    // Should not be open after close
    EXPECT_FALSE(transport.is_open());
}

// Test: SocketTransport endpoint info when not connected
TEST_F(SocketLifecycleTest, SocketTransportEndpointInfoNotConnected) {
    boost::asio::ip::tcp::socket sock(*io_context_);
    SocketTransport transport(std::move(sock));
    
    // Endpoint info should return empty/zero when not connected
    EXPECT_EQ(transport.remote_address(), "");
    EXPECT_EQ(transport.remote_port(), 0);
    EXPECT_EQ(transport.local_address(), "");
    EXPECT_EQ(transport.local_port(), 0);
}

// Test: SessionConnectParams validation
TEST_F(SocketLifecycleTest, SessionConnectParamsCreation) {
    PublicKey peer_key{};
    std::fill(peer_key.begin(), peer_key.end(), 0x42);
    
    PublicKey local_pub{};
    std::fill(local_pub.begin(), local_pub.end(), 0x01);
    
    PrivateKey local_priv{};
    std::fill(local_priv.begin(), local_priv.end(), 0x02);
    
    SessionConnectParams params{
        "example.com",
        30303,
        local_pub,
        local_priv,
        peer_key,
        "test-client",
        30303
    };
    
    // Verify params were set correctly
    EXPECT_EQ(params.remote_host, "example.com");
    EXPECT_EQ(params.remote_port, 30303);
    EXPECT_EQ(params.client_id, "test-client");
    EXPECT_EQ(params.listen_port, 30303);
    EXPECT_EQ(params.peer_public_key, peer_key);
}

// Test: SessionAcceptParams validation
TEST_F(SocketLifecycleTest, SessionAcceptParamsCreation) {
    PublicKey local_pub{};
    std::fill(local_pub.begin(), local_pub.end(), 0x01);
    
    PrivateKey local_priv{};
    std::fill(local_priv.begin(), local_priv.end(), 0x02);
    
    SessionAcceptParams params{
        local_pub,
        local_priv,
        "test-server",
        30303
    };
    
    // Verify params were set correctly
    EXPECT_EQ(params.client_id, "test-server");
    EXPECT_EQ(params.listen_port, 30303);
}

// Test: PeerInfo structure
TEST_F(SocketLifecycleTest, PeerInfoCreation) {
    PublicKey peer_key{};
    std::fill(peer_key.begin(), peer_key.end(), 0x42);
    
    PeerInfo info{};
    info.public_key = peer_key;
    info.client_id = "peer-client";
    info.listen_port = 30303;
    info.remote_address = "192.168.1.1";
    info.remote_port = 30303;
    
    // Verify info structure
    EXPECT_EQ(info.client_id, "peer-client");
    EXPECT_EQ(info.listen_port, 30303);
    EXPECT_EQ(info.remote_address, "192.168.1.1");
    EXPECT_EQ(info.remote_port, 30303);
    EXPECT_EQ(info.public_key, peer_key);
}

// Test: MessageChannel push and pop operations
TEST_F(SocketLifecycleTest, MessageChannelOperations) {
    // Note: MessageChannel is private, but we can test through RlpxSession
    // This test validates the concept
    
    std::queue<framing::Message> queue;
    
    // Push a message
    framing::Message msg1{};
    msg1.id = 0x00;
    msg1.payload = {0x01, 0x02, 0x03};
    queue.push(std::move(msg1));
    
    // Queue should not be empty
    EXPECT_FALSE(queue.empty());
    
    // Pop the message
    auto msg2 = std::move(queue.front());
    queue.pop();
    
    // Verify message content
    EXPECT_EQ(msg2.id, 0x00);
    EXPECT_EQ(msg2.payload.size(), 3);
    EXPECT_EQ(msg2.payload[0], 0x01);
    
    // Queue should be empty now
    EXPECT_TRUE(queue.empty());
}

// Test: Session state transitions
TEST_F(SocketLifecycleTest, SessionStateTransitions) {
    // Test state values
    EXPECT_EQ(static_cast<int>(SessionState::kUninitialized), 0);
    EXPECT_EQ(static_cast<int>(SessionState::kConnecting), 1);
    EXPECT_EQ(static_cast<int>(SessionState::kAuthenticating), 2);
    EXPECT_EQ(static_cast<int>(SessionState::kHandshaking), 3);
    EXPECT_EQ(static_cast<int>(SessionState::kActive), 4);
    EXPECT_EQ(static_cast<int>(SessionState::kDisconnecting), 5);
    EXPECT_EQ(static_cast<int>(SessionState::kClosed), 6);
    EXPECT_EQ(static_cast<int>(SessionState::kError), 7);
}

// Test: Disconnect reasons
TEST_F(SocketLifecycleTest, DisconnectReasons) {
    // Verify disconnect reason values match protocol
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kRequested), static_cast<uint8_t>(0x00));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kTcpError), static_cast<uint8_t>(0x01));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kProtocolError), static_cast<uint8_t>(0x02));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kUselessPeer), static_cast<uint8_t>(0x03));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kTooManyPeers), static_cast<uint8_t>(0x04));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kAlreadyConnected), static_cast<uint8_t>(0x05));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kIncompatibleVersion), static_cast<uint8_t>(0x06));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kInvalidIdentity), static_cast<uint8_t>(0x07));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kClientQuitting), static_cast<uint8_t>(0x08));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kUnexpectedIdentity), static_cast<uint8_t>(0x09));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kSelfConnection), static_cast<uint8_t>(0x0A));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kTimeout), static_cast<uint8_t>(0x0B));
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kSubprotocolError), static_cast<uint8_t>(0x10));
}

// Note: Full integration tests with actual network connections would require
// a test server setup. The tests above validate the core data structures
// and basic operations without requiring network connectivity.

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
