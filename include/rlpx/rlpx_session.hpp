// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "rlpx_types.hpp"
#include "rlpx_error.hpp"
#include "framing/message_stream.hpp"
#include "protocol/messages.hpp"
#include <boost/asio/spawn.hpp>
#include <atomic>
#include <memory>
#include <functional>

namespace rlpx {

// Message handler callback types
using MessageHandler = std::function<void(const protocol::Message&)>;
using HelloHandler = std::function<void(const protocol::HelloMessage&)>;
using DisconnectHandler = std::function<void(const protocol::DisconnectMessage&)>;
using PingHandler = std::function<void(const protocol::PingMessage&)>;
using PongHandler = std::function<void(const protocol::PongMessage&)>;
using EthMessageHandler = std::function<void(uint8_t, const ByteBuffer&)>;

// Session creation parameters for outbound connections
struct SessionConnectParams {
    std::string_view remote_host;
    uint16_t remote_port;
    gsl::span<const uint8_t, kPublicKeySize> local_public_key;
    gsl::span<const uint8_t, kPrivateKeySize> local_private_key;
    PublicKey peer_public_key;
    std::string_view client_id;
    uint16_t listen_port;
};

// Session creation parameters for inbound connections
struct SessionAcceptParams {
    gsl::span<const uint8_t, kPublicKeySize> local_public_key;
    gsl::span<const uint8_t, kPrivateKeySize> local_private_key;
    std::string_view client_id;
    uint16_t listen_port;
    // Socket passed separately via executor context
};

// Peer information (grouped values stored as member)
struct PeerInfo {
    PublicKey public_key;
    std::string client_id;
    uint16_t listen_port;
    std::string remote_address;
    uint16_t remote_port;
};

/// RLPx session managing encrypted P2P communication.
class RlpxSession : public std::enable_shared_from_this<RlpxSession> {
public:
    /// @brief Factory for outbound connections.
    /// @param params Session connection parameters.
    /// @param yield  Boost.Asio stackful coroutine context.
    /// @return Constructed session on success, SessionError on failure.
    [[nodiscard]] static Result<std::shared_ptr<RlpxSession>>
    connect(const SessionConnectParams& params, boost::asio::yield_context yield) noexcept;

    /// @brief Factory for inbound connections.
    /// @param params Session accept parameters.
    /// @param yield  Boost.Asio stackful coroutine context.
    /// @return Constructed session on success, SessionError on failure.
    [[nodiscard]] static Result<std::shared_ptr<RlpxSession>>
    accept(const SessionAcceptParams& params, boost::asio::yield_context yield) noexcept;

    ~RlpxSession();

    // Non-copyable, moveable
    RlpxSession(const RlpxSession&) = delete;
    RlpxSession& operator=(const RlpxSession&) = delete;
    RlpxSession(RlpxSession&&) noexcept;
    RlpxSession& operator=(RlpxSession&&) noexcept;

    /// Send message (takes ownership via move).
    [[nodiscard]] VoidResult
    post_message(framing::Message message) noexcept;

    /// @brief Receive message with timeout during protocol startup handshakes.
    /// @param timeout Maximum time to wait for one inbound message.
    /// @param yield Boost.Asio stackful coroutine context.
    /// @return Next message on success, SessionError on timeout or transport failure.
    [[nodiscard]] Result<framing::Message>
    receive_message_with_timeout(
        std::chrono::steady_clock::duration timeout,
        boost::asio::yield_context          yield) noexcept;

    /// @brief Receive message (stackful coroutine pull model).
    /// @param yield Boost.Asio stackful coroutine context.
    /// @return Next message on success, SessionError on failure.
    [[nodiscard]] Result<framing::Message>
    receive_message(boost::asio::yield_context yield) noexcept;

    /// @brief Graceful disconnect (sync, callable from callbacks).
    /// @param reason Disconnect reason code.
    [[nodiscard]] VoidResult
    disconnect(DisconnectReason reason) noexcept;

    /// @brief Graceful disconnect (coroutine overload).
    /// @param reason Disconnect reason code.
    /// @param yield  Boost.Asio stackful coroutine context.
    [[nodiscard]] VoidResult
    disconnect(DisconnectReason reason, boost::asio::yield_context yield) noexcept;

    // Message handler registration
    void set_hello_handler(HelloHandler handler) noexcept {
        hello_handler_ = std::move(handler);
    }

    void set_disconnect_handler(DisconnectHandler handler) noexcept {
        disconnect_handler_ = std::move(handler);
    }

    void set_ping_handler(PingHandler handler) noexcept {
        ping_handler_ = std::move(handler);
    }

    void set_pong_handler(PongHandler handler) noexcept {
        pong_handler_ = std::move(handler);
    }

    void set_generic_handler(MessageHandler handler) noexcept {
        generic_handler_ = std::move(handler);
    }

    void set_eth_message_handler(EthMessageHandler handler) noexcept {
        eth_message_handler_ = std::move(handler);
    }

    // State queries
    [[nodiscard]] SessionState state() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    [[nodiscard]] bool is_active() const noexcept {
        return state() == SessionState::kActive;
    }

    // Return const reference to grouped peer info
    [[nodiscard]] const PeerInfo& peer_info() const noexcept {
        return peer_info_;
    }

    /// @brief Return the negotiated ETH subprotocol version from HELLO capability matching.
    /// @return 66, 67, 68, or 69 when a common ETH capability was negotiated; 0 otherwise.
    [[nodiscard]] uint8_t negotiated_eth_version() const noexcept
    {
        return negotiated_eth_version_;
    }

    /// @brief Return the negotiated ETH subprotocol message-id base offset.
    /// @return Wire-level ETH base offset when a common ETH capability was negotiated; 0 otherwise.
    [[nodiscard]] uint8_t negotiated_eth_offset() const noexcept
    {
        return negotiated_eth_offset_;
    }

    /// @brief Test helper that selects the highest supported ETH version from capabilities.
    /// @param capabilities Peer HELLO capabilities.
    /// @return Highest supported ETH version, or 0 if none.
    [[nodiscard]] static uint8_t negotiate_eth_version_for_test(
        const std::vector<protocol::Capability>& capabilities) noexcept;

    /// @brief Test helper that computes the negotiated ETH wire offset from capabilities.
    /// @param capabilities Peer HELLO capabilities.
    /// @return ETH wire offset, or 0 if no ETH capability was negotiated.
    [[nodiscard]] static uint8_t negotiate_eth_offset_for_test(
        const std::vector<protocol::Capability>& capabilities) noexcept;

    /// @brief Test helper that normalizes a wire-level ETH message id using the negotiated offset.
    /// @param wire_message_id Wire-level RLPx message id.
    /// @param negotiated_eth_offset Negotiated ETH subprotocol offset.
    /// @return ETH-local message id when the wire id belongs to ETH; std::nullopt otherwise.
    [[nodiscard]] static std::optional<uint8_t> normalize_eth_message_id_for_test(
        uint8_t wire_message_id,
        uint8_t negotiated_eth_offset) noexcept;

    // Access to cipher secrets if needed (grouped values)
    [[nodiscard]] const auth::FrameSecrets& cipher_secrets() const noexcept;

private:
    // Private constructor - use factories
    RlpxSession(
        std::unique_ptr<framing::MessageStream> stream,
        PeerInfo peer_info,
        bool is_initiator
    ) noexcept;

    // Internal coroutine loops
    [[nodiscard]] VoidResult run_send_loop(boost::asio::yield_context yield) noexcept;
    [[nodiscard]] VoidResult run_receive_loop(boost::asio::yield_context yield) noexcept;

    // Message routing
    void route_message(const protocol::Message& msg) noexcept;

    // State transition helpers
    [[nodiscard]] bool try_transition_state(SessionState from, SessionState to) noexcept;
    [[nodiscard]] bool is_terminal_state(SessionState state) const noexcept;
    void force_error_state() noexcept;

    // State management
    std::atomic<SessionState> state_{SessionState::kUninitialized};
    
    // Message stream (owns cipher and socket abstraction)
    std::unique_ptr<framing::MessageStream> stream_;
    
    // Peer metadata - stored as member for const reference access
    PeerInfo peer_info_;
    uint8_t negotiated_eth_version_{0U};
    uint8_t negotiated_eth_offset_{0U};
    bool is_initiator_;

    // Message channels (lock-free, hidden Boost types)
    class MessageChannel;
    std::unique_ptr<MessageChannel> send_channel_;
    std::unique_ptr<MessageChannel> recv_channel_;

    // Message handlers
    HelloHandler hello_handler_;
    DisconnectHandler disconnect_handler_;
    PingHandler ping_handler_;
    PongHandler pong_handler_;
    MessageHandler generic_handler_;
    EthMessageHandler eth_message_handler_;
};

} // namespace rlpx
