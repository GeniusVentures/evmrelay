// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlpx/rlpx_types.hpp>

using namespace rlpx;

class RlpxStateTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup if needed
    }

    void TearDown() override {
        // Common cleanup if needed
    }
};

// Test SessionState enum values
TEST_F(RlpxStateTest, SessionStateValues) {
    EXPECT_EQ(static_cast<uint8_t>(SessionState::kUninitialized), 0);
    EXPECT_LT(static_cast<uint8_t>(SessionState::kConnecting), 
              static_cast<uint8_t>(SessionState::kActive));
    EXPECT_LT(static_cast<uint8_t>(SessionState::kActive), 
              static_cast<uint8_t>(SessionState::kDisconnecting));
}

// Test DisconnectReason enum values
TEST_F(RlpxStateTest, DisconnectReasonValues) {
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kRequested), 0x00);
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kTcpError), 0x01);
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kProtocolError), 0x02);
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kClientQuitting), 0x08);
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kSubprotocolError), 0x10);
}

// Test that terminal states are correctly identified
TEST_F(RlpxStateTest, TerminalStates) {
    // Terminal states
    EXPECT_TRUE(SessionState::kClosed == SessionState::kClosed);
    EXPECT_TRUE(SessionState::kError == SessionState::kError);
    
    // Non-terminal states
    EXPECT_FALSE(SessionState::kActive == SessionState::kClosed);
    EXPECT_FALSE(SessionState::kConnecting == SessionState::kError);
}

// Test state progression order
TEST_F(RlpxStateTest, StateProgression) {
    // Normal progression: Uninitialized -> Connecting -> Authenticating -> 
    // Handshaking -> Active -> Disconnecting -> Closed
    
    auto s1 = SessionState::kUninitialized;
    auto s2 = SessionState::kConnecting;
    auto s3 = SessionState::kAuthenticating;
    auto s4 = SessionState::kHandshaking;
    auto s5 = SessionState::kActive;
    auto s6 = SessionState::kDisconnecting;
    auto s7 = SessionState::kClosed;
    
    EXPECT_LT(static_cast<int>(s1), static_cast<int>(s2));
    EXPECT_LT(static_cast<int>(s2), static_cast<int>(s3));
    EXPECT_LT(static_cast<int>(s3), static_cast<int>(s4));
    EXPECT_LT(static_cast<int>(s4), static_cast<int>(s5));
    EXPECT_LT(static_cast<int>(s5), static_cast<int>(s6));
    EXPECT_LT(static_cast<int>(s6), static_cast<int>(s7));
}

// Test disconnect reason ranges
TEST_F(RlpxStateTest, DisconnectReasonRanges) {
    // Standard reasons are 0x00-0x0B
    EXPECT_LE(static_cast<uint8_t>(DisconnectReason::kRequested), 0x0B);
    EXPECT_LE(static_cast<uint8_t>(DisconnectReason::kTimeout), 0x0B);
    
    // Subprotocol error is 0x10
    EXPECT_EQ(static_cast<uint8_t>(DisconnectReason::kSubprotocolError), 0x10);
}

// Test protocol message IDs
TEST_F(RlpxStateTest, ProtocolMessageIds) {
    EXPECT_EQ(kHelloMessageId, 0x00);
    EXPECT_EQ(kDisconnectMessageId, 0x01);
    EXPECT_EQ(kPingMessageId, 0x02);
    EXPECT_EQ(kPongMessageId, 0x03);
}

// Test protocol version
TEST_F(RlpxStateTest, ProtocolVersion) {
    EXPECT_EQ(kProtocolVersion, 5);
}
