// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_RLPX_PROTOCOL_MESSAGES_HPP
#define EVMRELAY_INCLUDE_RLPX_PROTOCOL_MESSAGES_HPP

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"
#include <string>
#include <vector>

namespace rlpx::protocol {

// Capability structure for Hello message
struct Capability {
    std::string name;    // e.g., "eth", "snap"
    uint8_t version;     // e.g., 66, 67
};

// Hello message (message ID 0x00)
struct HelloMessage {
    uint8_t protocol_version = kProtocolVersion;  // RLPx protocol version (5)
    std::string client_id;                         // e.g., "MyClient/v1.0.0"
    std::vector<Capability> capabilities;          // Supported subprotocols
    uint16_t listen_port;                          // TCP port
    PublicKey node_id;                             // 64-byte node ID

    // Encode to RLP
    [[nodiscard]] Result<ByteBuffer> encode() const noexcept;

    // Decode from RLP
    [[nodiscard]] static Result<HelloMessage> decode(ByteView rlp_data) noexcept;
};

// Disconnect message (message ID 0x01)
struct DisconnectMessage {
    DisconnectReason reason;

    // Encode to RLP
    [[nodiscard]] Result<ByteBuffer> encode() const noexcept;

    // Decode from RLP
    [[nodiscard]] static Result<DisconnectMessage> decode(ByteView rlp_data) noexcept;
};

// Ping message (message ID 0x02)
struct PingMessage {
    // Ping has no payload in RLPx
    
    // Encode to RLP (empty list)
    [[nodiscard]] Result<ByteBuffer> encode() const noexcept;

    // Decode from RLP
    [[nodiscard]] static Result<PingMessage> decode(ByteView rlp_data) noexcept;
};

// Pong message (message ID 0x03)
struct PongMessage {
    // Pong has no payload in RLPx
    
    // Encode to RLP (empty list)
    [[nodiscard]] Result<ByteBuffer> encode() const noexcept;

    // Decode from RLP
    [[nodiscard]] static Result<PongMessage> decode(ByteView rlp_data) noexcept;
};

// Generic protocol message wrapper
struct Message {
    uint8_t id;          // Message ID
    ByteBuffer payload;  // RLP-encoded payload

    [[nodiscard]] bool is_hello() const noexcept { return id == kHelloMessageId; }
    [[nodiscard]] bool is_disconnect() const noexcept { return id == kDisconnectMessageId; }
    [[nodiscard]] bool is_ping() const noexcept { return id == kPingMessageId; }
    [[nodiscard]] bool is_pong() const noexcept { return id == kPongMessageId; }
};

} // namespace rlpx::protocol

#endif // EVMRELAY_INCLUDE_RLPX_PROTOCOL_MESSAGES_HPP
