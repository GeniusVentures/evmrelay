// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <rlpx/protocol/messages.hpp>
#include <vector>

using namespace rlpx;
using namespace rlpx::protocol;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::vector<uint8_t> from_hex(std::string_view hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        auto byte = static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16));
        out.push_back(byte);
    }
    return out;
}

// ---------------------------------------------------------------------------
// go-ethereum wire-format vector tests
//
// These bytes were computed from go-ethereum's RLP encoding of protoHandshake:
//   type protoHandshake struct {
//       Version    uint64
//       Name       string
//       Caps       []Cap         // each Cap is [name string, version uint64]
//       ListenPort uint64
//       ID         []byte        // 64-byte uncompressed pubkey WITHOUT 0x04 prefix
//   }
//
// Vector: Version=5, Name="test-client", Caps=[["eth",68]], Port=30303, ID=64×0x00
// Computed with Python rlp_encode, verified against go-ethereum rlp package.
// ---------------------------------------------------------------------------

// The exact wire bytes for the vector above (91 bytes).
// f8 59              — outer list, 89-byte payload
//   05               — version = 5
//   8b 746573742d636c69656e74  — "test-client"
//   c6 c5 83 657468 44  — caps list: [["eth", 68]]
//   82 765f           — port = 30303
//   b8 40 00…00       — ID: 64 zero bytes as RLP byte string
static constexpr std::string_view kHelloWireHex =
    "f859"
    "05"
    "8b746573742d636c69656e74"
    "c6c58365746844"
    "82765f"
    "b840"
    "0000000000000000000000000000000000000000000000000000000000000000"
    "0000000000000000000000000000000000000000000000000000000000000000";

TEST(ProtocolMessagesGoEthVectors, HelloEncodeMatchesWireFormat) {
    HelloMessage msg;
    msg.protocol_version = 5;
    msg.client_id = "test-client";
    msg.capabilities = { {"eth", 68} };
    msg.listen_port = 30303;
    msg.node_id.fill(0x00);

    auto result = msg.encode();
    ASSERT_TRUE(result.has_value()) << "encode() failed";

    auto expected = from_hex(kHelloWireHex);
    EXPECT_EQ(result.value(), expected)
        << "Encoded bytes do not match go-ethereum wire format.\n"
        << "  got " << result.value().size() << " bytes, expected " << expected.size();
}

TEST(ProtocolMessagesGoEthVectors, HelloDecodeFromWireBytes) {
    auto wire = from_hex(kHelloWireHex);
    auto result = HelloMessage::decode(ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode() failed on go-ethereum wire bytes";

    EXPECT_EQ(result.value().protocol_version, 5u);
    EXPECT_EQ(result.value().client_id, "test-client");
    ASSERT_EQ(result.value().capabilities.size(), 1u);
    EXPECT_EQ(result.value().capabilities[0].name, "eth");
    EXPECT_EQ(result.value().capabilities[0].version, 68u);
    EXPECT_EQ(result.value().listen_port, 30303u);
    PublicKey zero_id;
    zero_id.fill(0x00);
    EXPECT_EQ(result.value().node_id, zero_id);
}

TEST(ProtocolMessagesGoEthVectors, HelloRoundTripPreservesWireFormat) {
    // Decode from wire → re-encode → must produce identical bytes
    auto wire = from_hex(kHelloWireHex);
    auto decoded = HelloMessage::decode(ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(decoded.has_value());

    auto reencoded = decoded.value().encode();
    ASSERT_TRUE(reencoded.has_value());
    EXPECT_EQ(reencoded.value(), wire);
}

// ---------------------------------------------------------------------------
// Original round-trip tests (preserved)
// ---------------------------------------------------------------------------

TEST(ProtocolMessagesTest, HelloEncodeBasic) {
    HelloMessage msg;
    msg.protocol_version = 5;
    msg.client_id = "TestClient/v1.0";
    msg.listen_port = 30303;
    msg.node_id.fill(0x42);
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().empty());
}

TEST(ProtocolMessagesTest, HelloEncodeWithCapabilities) {
    HelloMessage msg;
    msg.protocol_version = 5;
    msg.client_id = "TestClient/v1.0";
    msg.capabilities = {
        {"eth", 66},
        {"snap", 1}
    };
    msg.listen_port = 30303;
    msg.node_id.fill(0x42);
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().empty());
}

TEST(ProtocolMessagesTest, HelloRoundtrip) {
    HelloMessage original;
    original.protocol_version = 5;
    original.client_id = "TestClient/v1.0";
    original.capabilities = {
        {"eth", 66},
        {"snap", 1},
        {"wit", 0}
    };
    original.listen_port = 30303;
    original.node_id.fill(0x42);
    
    auto encoded = original.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    
    EXPECT_EQ(decoded.value().protocol_version, original.protocol_version);
    EXPECT_EQ(decoded.value().client_id, original.client_id);
    EXPECT_EQ(decoded.value().listen_port, original.listen_port);
    EXPECT_EQ(decoded.value().node_id, original.node_id);
    EXPECT_EQ(decoded.value().capabilities.size(), original.capabilities.size());
}

TEST(ProtocolMessagesTest, HelloRoundtripPreservesEthCapabilities66Through69) {
    HelloMessage original;
    original.protocol_version = 5;
    original.client_id = "TestClient/v1.0";
    original.capabilities = {
        {"eth", 66},
        {"eth", 67},
        {"eth", 68},
        {"eth", 69}
    };
    original.listen_port = 30303;
    original.node_id.fill(0x24);

    auto encoded = original.encode();
    ASSERT_TRUE(encoded.has_value());

    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded.value().capabilities.size(), original.capabilities.size());

    for (size_t i = 0; i < original.capabilities.size(); ++i) {
        EXPECT_EQ(decoded.value().capabilities[i].name, original.capabilities[i].name);
        EXPECT_EQ(decoded.value().capabilities[i].version, original.capabilities[i].version);
    }
}

TEST(ProtocolMessagesTest, HelloEmptyClientId) {
    HelloMessage msg;
    msg.protocol_version = 5;
    msg.client_id = "";
    msg.listen_port = 30303;
    msg.node_id.fill(0x42);
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
}

TEST(ProtocolMessagesTest, DisconnectEncodeRequested) {
    DisconnectMessage msg;
    msg.reason = DisconnectReason::kRequested;
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().empty());
}

TEST(ProtocolMessagesTest, DisconnectEncodeClientQuitting) {
    DisconnectMessage msg;
    msg.reason = DisconnectReason::kClientQuitting;
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().empty());
}

TEST(ProtocolMessagesTest, DisconnectRoundtrip) {
    DisconnectMessage original;
    original.reason = DisconnectReason::kTooManyPeers;
    
    auto encoded = original.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = DisconnectMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    
    EXPECT_EQ(decoded.value().reason, original.reason);
}

TEST(ProtocolMessagesTest, DisconnectAllReasons) {
    std::vector<DisconnectReason> reasons = {
        DisconnectReason::kRequested,
        DisconnectReason::kTcpError,
        DisconnectReason::kProtocolError,
        DisconnectReason::kUselessPeer,
        DisconnectReason::kTooManyPeers,
        DisconnectReason::kAlreadyConnected,
        DisconnectReason::kIncompatibleVersion,
        DisconnectReason::kInvalidIdentity,
        DisconnectReason::kClientQuitting,
        DisconnectReason::kUnexpectedIdentity,
        DisconnectReason::kSelfConnection,
        DisconnectReason::kTimeout,
        DisconnectReason::kSubprotocolError
    };
    
    for ( auto reason : reasons ) {
        DisconnectMessage msg;
        msg.reason = reason;
        
        auto encoded = msg.encode();
        ASSERT_TRUE(encoded.has_value()) << "Failed to encode reason: " << static_cast<int>(reason);
        
        auto decoded = DisconnectMessage::decode(encoded.value());
        ASSERT_TRUE(decoded.has_value()) << "Failed to decode reason: " << static_cast<int>(reason);
        
        EXPECT_EQ(decoded.value().reason, reason);
    }
}

TEST(ProtocolMessagesTest, PingEncode) {
    PingMessage msg;
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().empty());
}

TEST(ProtocolMessagesTest, PingRoundtrip) {
    PingMessage original;
    
    auto encoded = original.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = PingMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
}

TEST(ProtocolMessagesTest, PongEncode) {
    PongMessage msg;
    
    auto result = msg.encode();
    
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result.value().empty());
}

TEST(ProtocolMessagesTest, PongRoundtrip) {
    PongMessage original;
    
    auto encoded = original.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = PongMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
}

TEST(ProtocolMessagesTest, MessageWrapper) {
    Message msg;
    msg.id = kHelloMessageId;
    msg.payload = {0x01, 0x02, 0x03};
    
    EXPECT_TRUE(msg.is_hello());
    EXPECT_FALSE(msg.is_disconnect());
    EXPECT_FALSE(msg.is_ping());
    EXPECT_FALSE(msg.is_pong());
}

TEST(ProtocolMessagesTest, MessageWrapperDisconnect) {
    Message msg;
    msg.id = kDisconnectMessageId;
    msg.payload = {0x01, 0x02, 0x03};
    
    EXPECT_FALSE(msg.is_hello());
    EXPECT_TRUE(msg.is_disconnect());
    EXPECT_FALSE(msg.is_ping());
    EXPECT_FALSE(msg.is_pong());
}

TEST(ProtocolMessagesTest, MessageWrapperPing) {
    Message msg;
    msg.id = kPingMessageId;
    
    EXPECT_FALSE(msg.is_hello());
    EXPECT_FALSE(msg.is_disconnect());
    EXPECT_TRUE(msg.is_ping());
    EXPECT_FALSE(msg.is_pong());
}

TEST(ProtocolMessagesTest, MessageWrapperPong) {
    Message msg;
    msg.id = kPongMessageId;
    
    EXPECT_FALSE(msg.is_hello());
    EXPECT_FALSE(msg.is_disconnect());
    EXPECT_FALSE(msg.is_ping());
    EXPECT_TRUE(msg.is_pong());
}

TEST(ProtocolMessagesTest, HelloLargeClientId) {
    HelloMessage msg;
    msg.protocol_version = 5;
    msg.client_id = std::string(1000, 'X');  // Very long client ID
    msg.listen_port = 30303;
    msg.node_id.fill(0x42);
    
    auto encoded = msg.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().client_id.size(), 1000);
}

TEST(ProtocolMessagesTest, HelloManyCapabilities) {
    HelloMessage msg;
    msg.protocol_version = 5;
    msg.client_id = "TestClient";
    msg.listen_port = 30303;
    msg.node_id.fill(0x42);
    
    // Add many capabilities
    for ( int i = 0; i < 10; ++i ) {
        msg.capabilities.push_back({"proto" + std::to_string(i), static_cast<uint8_t>(i)});
    }
    
    auto encoded = msg.encode();
    ASSERT_TRUE(encoded.has_value());
    
    auto decoded = HelloMessage::decode(encoded.value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().capabilities.size(), 10);
}

