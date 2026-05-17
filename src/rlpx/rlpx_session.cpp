// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/rlpx_session.hpp>
#include <rlpx/auth/auth_handshake.hpp>
#include <rlpx/framing/frame_cipher.hpp>
#include <rlpx/protocol/messages.hpp>
#include <rlpx/socket/socket_transport.hpp>
#include <eth/eth_types.hpp>
#include <base/rlp-logger.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/read.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <queue>
#include <mutex>
#include <chrono>

namespace rlpx {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

namespace {

inline constexpr uint8_t kBaseProtocolLength = 16U;
inline constexpr uint8_t kUnknownProtocolLength = 1U;
inline constexpr uint8_t kSnapProtocolLength = 8U;

Result<framing::Message> receive_stream_message_with_timeout(
    framing::MessageStream&      stream,
    asio::any_io_executor       executor,
    asio::yield_context         yield) noexcept
{
    std::shared_ptr<boost::system::error_code> timer_error = std::make_shared<boost::system::error_code>();
    auto timer = std::make_shared<asio::steady_timer>(executor, kProtocolHandshakeTimeout);
    timer->async_wait([&stream, timer_error](const boost::system::error_code& ec)
    {
        *timer_error = ec;
        if (!ec)
        {
            stream.close();
        }
    });

    auto message_result = stream.receive_message(yield);
    timer->cancel();

    if (!message_result)
    {
        if (*timer_error != asio::error::operation_aborted)
        {
            return SessionError::kHandshakeFailed;
        }
        return message_result.error();
    }

    return message_result;
}

uint8_t negotiate_eth_version(const std::vector<protocol::Capability>& capabilities) noexcept
{
    uint8_t negotiated_version = 0U;

    for (const auto& capability : capabilities)
    {
        if (capability.name != "eth")
        {
            continue;
        }

        if ((capability.version >= eth::kEthProtocolVersion66 &&
             capability.version <= eth::kEthProtocolVersion69) &&
            capability.version > negotiated_version)
        {
            negotiated_version = capability.version;
        }
    }

    return negotiated_version;
}

uint8_t capability_wire_length(const protocol::Capability& capability) noexcept
{
    if (capability.name == "eth")
    {
        if (capability.version == eth::kEthProtocolVersion69)
        {
            return 18U;
        }
        if (capability.version >= eth::kEthProtocolVersion66 &&
            capability.version <= eth::kEthProtocolVersion68)
        {
            return 17U;
        }
        return 0U;
    }

    if (capability.name == "snap")
    {
        return kSnapProtocolLength;
    }

    return kUnknownProtocolLength;
}

bool capability_less(const protocol::Capability& lhs, const protocol::Capability& rhs) noexcept
{
    if (lhs.name != rhs.name)
    {
        return lhs.name < rhs.name;
    }
    return lhs.version < rhs.version;
}

uint8_t negotiate_eth_offset(const std::vector<protocol::Capability>& capabilities) noexcept
{
    std::vector<protocol::Capability> sorted_capabilities = capabilities;
    std::sort(sorted_capabilities.begin(), sorted_capabilities.end(), capability_less);

    uint8_t offset = kBaseProtocolLength;
    std::optional<protocol::Capability> matched_eth;
    uint8_t matched_eth_offset = 0U;

    for (const auto& capability : sorted_capabilities)
    {
        const uint8_t length = capability_wire_length(capability);
        if (length == 0U)
        {
            continue;
        }

        if (capability.name == "eth")
        {
            if (!matched_eth.has_value() || capability.version > matched_eth->version)
            {
                if (matched_eth.has_value())
                {
                    offset = static_cast<uint8_t>(offset - capability_wire_length(*matched_eth));
                }
                matched_eth = capability;
                matched_eth_offset = offset;
                offset = static_cast<uint8_t>(offset + length);
                continue;
            }
            continue;
        }

        offset = static_cast<uint8_t>(offset + length);
    }

    return matched_eth.has_value() ? matched_eth_offset : 0U;
}

} // namespace

// Message channel for lock-free communication
class RlpxSession::MessageChannel {
public:
    MessageChannel() = default;

    void push(framing::Message msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(msg));
    }

    std::optional<framing::Message> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return std::nullopt;
        }
        auto msg = std::move(queue_.front());
        queue_.pop();
        return msg;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::queue<framing::Message> queue_;
};

// Private constructor
RlpxSession::RlpxSession(
    std::unique_ptr<framing::MessageStream> stream,
    PeerInfo peer_info,
    bool is_initiator
) noexcept
    : stream_(std::move(stream))
    , peer_info_(std::move(peer_info))
    , is_initiator_(is_initiator)
    , send_channel_(std::make_unique<MessageChannel>())
    , recv_channel_(std::make_unique<MessageChannel>())
{
}

RlpxSession::~RlpxSession() {
    // Ensure we're in a terminal state
    auto current_state = state_.load(std::memory_order_acquire);
    if (current_state != SessionState::kClosed && current_state != SessionState::kError) {
        // Force transition to closed state
        state_.store(SessionState::kClosed, std::memory_order_release);
    }
    
    // Channels and stream will be cleaned up automatically via unique_ptr
}

// Move operations - need special handling for atomic
RlpxSession::RlpxSession(RlpxSession&& other) noexcept
    : stream_(std::move(other.stream_))
    , peer_info_(std::move(other.peer_info_))
    , negotiated_eth_version_(other.negotiated_eth_version_)
    , negotiated_eth_offset_(other.negotiated_eth_offset_)
    , is_initiator_(other.is_initiator_)
    , send_channel_(std::move(other.send_channel_))
    , recv_channel_(std::move(other.recv_channel_))
    , hello_handler_(std::move(other.hello_handler_))
    , disconnect_handler_(std::move(other.disconnect_handler_))
    , ping_handler_(std::move(other.ping_handler_))
    , pong_handler_(std::move(other.pong_handler_))
    , generic_handler_(std::move(other.generic_handler_)) {
    state_.store(other.state_.load(std::memory_order_acquire), std::memory_order_release);
}

RlpxSession& RlpxSession::operator=(RlpxSession&& other) noexcept {
    if (this != &other) {
        stream_ = std::move(other.stream_);
        peer_info_ = std::move(other.peer_info_);
        negotiated_eth_version_ = other.negotiated_eth_version_;
        negotiated_eth_offset_ = other.negotiated_eth_offset_;
        is_initiator_ = other.is_initiator_;
        send_channel_ = std::move(other.send_channel_);
        recv_channel_ = std::move(other.recv_channel_);
        hello_handler_ = std::move(other.hello_handler_);
        disconnect_handler_ = std::move(other.disconnect_handler_);
        ping_handler_ = std::move(other.ping_handler_);
        pong_handler_ = std::move(other.pong_handler_);
        generic_handler_ = std::move(other.generic_handler_);
        state_.store(other.state_.load(std::memory_order_acquire), std::memory_order_release);
    }
    return *this;
}

uint8_t RlpxSession::negotiate_eth_version_for_test(
    const std::vector<protocol::Capability>& capabilities) noexcept
{
    return negotiate_eth_version(capabilities);
}

uint8_t RlpxSession::negotiate_eth_offset_for_test(
    const std::vector<protocol::Capability>& capabilities) noexcept
{
    return negotiate_eth_offset(capabilities);
}

std::optional<uint8_t> RlpxSession::normalize_eth_message_id_for_test(
    uint8_t wire_message_id,
    uint8_t negotiated_eth_offset) noexcept
{
    if (wire_message_id < negotiated_eth_offset)
    {
        return std::nullopt;
    }

    return static_cast<uint8_t>(wire_message_id - negotiated_eth_offset);
}

// Factory for outbound connections
Result<std::shared_ptr<RlpxSession>>
RlpxSession::connect(const SessionConnectParams& params, asio::yield_context yield) noexcept {
    static auto log = rlp::base::createLogger("rlpx.session");

    // Step 1: Establish TCP connection with timeout
    auto executor = yield.get_executor();

    auto transport_result = socket::connect_with_timeout(
        executor,
        params.remote_host,
        params.remote_port,
        kTcpConnectionTimeout,
        yield
    );
    if (!transport_result) {
        return transport_result.error();
    }

    // Step 2: Run the real RLPx auth handshake (auth → ack)
    auth::HandshakeConfig hs_config;
    std::copy(params.local_public_key.begin(),  params.local_public_key.end(),  hs_config.local_public_key.begin());
    std::copy(params.local_private_key.begin(), params.local_private_key.end(), hs_config.local_private_key.begin());
    hs_config.client_id    = std::string(params.client_id);
    hs_config.listen_port  = params.listen_port;
    hs_config.peer_public_key = params.peer_public_key;

    auth::AuthHandshake handshake(hs_config, std::move(transport_result.value()));
    auto hs_result = handshake.execute(yield);
    if (!hs_result) {
        return hs_result.error();
    }
    SPDLOG_LOGGER_INFO(log, "connect: auth handshake completed");
    auto& hs = hs_result.value();

    // Step 3: Build MessageStream with derived frame secrets
    if (!hs.transport) {
        return SessionError::kAuthenticationFailed;
    }
    auto cipher = std::make_unique<framing::FrameCipher>(hs.frame_secrets);
    auto stream = std::make_unique<framing::MessageStream>(
        std::move(cipher),
        std::move(hs.transport.value())
    );

    // Step 4: Build session with peer info
    PeerInfo peer_info{};
    peer_info.public_key = params.peer_public_key;
    peer_info.client_id = std::string(params.client_id);
    peer_info.listen_port = params.listen_port;
    peer_info.remote_address = "";
    peer_info.remote_port = params.remote_port;

    auto session = std::shared_ptr<RlpxSession>(new RlpxSession(
        std::move(stream),
        std::move(peer_info),
        true  // is_initiator
    ));

    SPDLOG_LOGGER_INFO(log, "connect: session created, sending local HELLO");

    // Step 5: Send our HELLO (initiator sends first)
    protocol::HelloMessage hello;
    hello.protocol_version = kProtocolVersion;
    hello.client_id        = std::string(params.client_id);
    // Advertise ETH/66-69 — peers negotiate the highest common version.
    hello.capabilities     = {
        protocol::Capability{ "eth", eth::kEthProtocolVersion66 },
        protocol::Capability{ "eth", eth::kEthProtocolVersion67 },
        protocol::Capability{ "eth", eth::kEthProtocolVersion68 },
        protocol::Capability{ "eth", eth::kEthProtocolVersion69 }
    };
    hello.listen_port      = params.listen_port;
    std::copy(params.local_public_key.begin(),
              params.local_public_key.end(),
              hello.node_id.begin());

    auto hello_encoded = hello.encode();
    if (!hello_encoded) {
        return SessionError::kHandshakeFailed;
    }

    framing::MessageSendParams hello_send{};
    hello_send.message_id = kHelloMessageId;
    hello_send.payload = std::move(hello_encoded.value());
    hello_send.compress = false;
    auto send_result = session->stream_->send_message(hello_send, yield);
    if (!send_result) {
        return send_result.error();
    }
    SPDLOG_LOGGER_INFO(log, "connect: local HELLO sent");

    // Step 6: Receive peer HELLO
    SPDLOG_LOGGER_INFO(log, "connect: waiting for peer HELLO");
    auto recv_result = receive_stream_message_with_timeout(*session->stream_, executor, yield);
    if (!recv_result) {
        SPDLOG_LOGGER_INFO(log, "connect: peer HELLO receive failed with error {}",
                           static_cast<int>(recv_result.error()));
        return recv_result.error();
    }
    auto& peer_msg = recv_result.value();
    SPDLOG_LOGGER_INFO(log, "connect: first peer message id=0x{:02x} payload_size={}",
                       peer_msg.id,
                       peer_msg.payload.size());
    {
        static auto log = rlp::base::createLogger("rlpx.session");
        SPDLOG_LOGGER_DEBUG(log, "connect: first message from peer, id=0x{:02x} payload_size={}", peer_msg.id, peer_msg.payload.size());
    }
    if (peer_msg.id == kHelloMessageId) {
        auto peer_hello = protocol::HelloMessage::decode(
            ByteView(peer_msg.payload.data(), peer_msg.payload.size()));
        if (peer_hello) {
            session->peer_info_.client_id     = peer_hello.value().client_id;
            session->peer_info_.listen_port   = peer_hello.value().listen_port;
            session->negotiated_eth_version_  = negotiate_eth_version(peer_hello.value().capabilities);
            session->negotiated_eth_offset_   = negotiate_eth_offset(peer_hello.value().capabilities);
            SPDLOG_LOGGER_INFO(log, "connect: peer HELLO decoded client='{}' p2p_version={} caps={}",
                               peer_hello.value().client_id,
                               static_cast<int>(peer_hello.value().protocol_version),
                               peer_hello.value().capabilities.size());
            static auto log = rlp::base::createLogger("rlpx.session");
            SPDLOG_LOGGER_DEBUG(log, "connect: peer HELLO ok, client='{}' port={} caps={}",
                peer_hello.value().client_id,
                peer_hello.value().listen_port,
                peer_hello.value().capabilities.size());
            SPDLOG_LOGGER_DEBUG(log, "connect: negotiated eth version={}",
                static_cast<int>(session->negotiated_eth_version_));
            SPDLOG_LOGGER_DEBUG(log, "connect: negotiated eth offset=0x{:02x}",
                session->negotiated_eth_offset_);

            // RLPx spec: enable Snappy compression if both sides advertise p2p version >= 5.
            if (peer_hello.value().protocol_version >= kProtocolVersion) {
                session->stream_->enable_compression();
                SPDLOG_LOGGER_INFO(log, "connect: Snappy enabled for peer p2p version {}",
                                   static_cast<int>(peer_hello.value().protocol_version));
                SPDLOG_LOGGER_DEBUG(log, "connect: Snappy compression enabled (peer p2p v{})",
                    peer_hello.value().protocol_version);
            }

            if (session->hello_handler_) {
                session->hello_handler_(peer_hello.value());
            }
        } else {
            static auto log = rlp::base::createLogger("rlpx.session");
            SPDLOG_LOGGER_WARN(log, "connect: peer HELLO decode failed, payload_size={}", peer_msg.payload.size());
            return SessionError::kHandshakeFailed;
        }
    } else if (peer_msg.id == kDisconnectMessageId) {
        static auto log = rlp::base::createLogger("rlpx.session");
        auto disc = protocol::DisconnectMessage::decode(peer_msg.payload);
        SPDLOG_LOGGER_INFO(log, "connect: peer sent Disconnect before HELLO");
        SPDLOG_LOGGER_DEBUG(log, "connect: peer sent Disconnect before HELLO, reason={}",
            disc ? static_cast<int>(disc.value().reason) : -1);
        return SessionError::kHandshakeFailed;
    } else {
        static auto log = rlp::base::createLogger("rlpx.session");
        SPDLOG_LOGGER_INFO(log, "connect: unexpected first peer message id=0x{:02x}", peer_msg.id);
        SPDLOG_LOGGER_WARN(log, "connect: expected HELLO (0x00) but got id=0x{:02x}", peer_msg.id);
        return SessionError::kHandshakeFailed;
    }

    // Step 7: Activate session and start I/O loops
    session->state_.store(SessionState::kActive, std::memory_order_release);
    SPDLOG_LOGGER_INFO(log, "connect: session activated, starting I/O loops");

    asio::spawn(
        executor,
        [s = session](asio::yield_context yc) {
            auto result = s->run_send_loop(yc);
            (void)result;
        }
    );

    asio::spawn(
        executor,
        [s = session](asio::yield_context yc) {
            auto result = s->run_receive_loop(yc);
            (void)result;
        }
    );

    return std::move(session);
}

// Factory for inbound connections
Result<std::shared_ptr<RlpxSession>>
RlpxSession::accept(const SessionAcceptParams& params, asio::yield_context /*yield*/) noexcept {
    (void)params;
    // TODO: Phase 3.5 - Implement inbound connection acceptance
    return SessionError::kConnectionFailed;
}

// Send message
VoidResult RlpxSession::post_message(framing::Message message) noexcept {
    auto current_state = state();
    
    // Only allow sending in active state
    if (current_state != SessionState::kActive) {
        if (current_state == SessionState::kClosed || current_state == SessionState::kError) {
            return SessionError::kConnectionFailed;
        }
        return SessionError::kNotConnected;
    }
    
    send_channel_->push(std::move(message));
    return outcome::success();
}

// Receive message
Result<framing::Message>
RlpxSession::receive_message_with_timeout(
    std::chrono::steady_clock::duration timeout,
    asio::yield_context                 yield) noexcept
{
    auto current_state = state();

    if (current_state != SessionState::kActive)
    {
        if (current_state == SessionState::kClosed || current_state == SessionState::kError)
        {
            return SessionError::kConnectionFailed;
        }
        return SessionError::kNotConnected;
    }

    asio::steady_timer timer(yield.get_executor());
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    for (;;)
    {
        auto msg = recv_channel_->try_pop();
        if (msg)
        {
            return std::move(*msg);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
        {
            return SessionError::kHandshakeFailed;
        }

        const auto remaining = deadline - now;
        const auto poll_interval = std::min<std::chrono::steady_clock::duration>(
            remaining,
            std::chrono::milliseconds(10));
        timer.expires_after(poll_interval);

        boost::system::error_code ec;
        timer.async_wait(asio::redirect_error(yield, ec));
        if (ec == asio::error::operation_aborted)
        {
            continue;
        }
        if (ec)
        {
            return SessionError::kConnectionFailed;
        }
    }
}

Result<framing::Message>
RlpxSession::receive_message(asio::yield_context yield) noexcept {
    auto current_state = state();
    
    if (current_state != SessionState::kActive) {
        if (current_state == SessionState::kClosed || current_state == SessionState::kError) {
            return SessionError::kConnectionFailed;
        }
        return SessionError::kNotConnected;
    }
    
    // Check if there's a message in the receive channel
    auto msg = recv_channel_->try_pop();
    if (!msg) {
        (void)yield;
        return SessionError::kNotConnected; // Would be timeout in real impl
    }
    
    return std::move(*msg);
}

// Graceful disconnect (sync)
VoidResult
RlpxSession::disconnect(DisconnectReason reason) noexcept {
    (void)reason;
    auto current_state = state_.load(std::memory_order_acquire);

    if (current_state == SessionState::kDisconnecting ||
        current_state == SessionState::kClosed ||
        current_state == SessionState::kError) {
        return outcome::success();
    }

    SessionState expected = current_state;
    while (!state_.compare_exchange_weak(
        expected,
        SessionState::kDisconnecting,
        std::memory_order_release,
        std::memory_order_acquire)) {
        if (expected == SessionState::kDisconnecting ||
            expected == SessionState::kClosed ||
            expected == SessionState::kError) {
            return outcome::success();
        }
    }

    state_.store(SessionState::kClosed, std::memory_order_release);
    if (stream_)
    {
        stream_->close();
    }
    return outcome::success();
}

// Graceful disconnect (coroutine overload)
VoidResult
RlpxSession::disconnect(DisconnectReason reason, asio::yield_context /*yield*/) noexcept {
    return disconnect(reason);
}

// Access to cipher secrets
const auth::FrameSecrets& RlpxSession::cipher_secrets() const noexcept {
    return stream_->cipher_secrets();
}

// Internal send loop
VoidResult RlpxSession::run_send_loop(asio::yield_context yield) noexcept {
    // Continuously send messages while session is active
    while (state() == SessionState::kActive) {
        // Check if there are pending messages to send
        auto msg = send_channel_->try_pop();
        
        if (!msg) {
            // No messages pending — yield and check again
            // TODO: Replace polling with proper async condition variable
            boost::system::error_code ec;
            asio::steady_timer(
                yield.get_executor(),
                kSendLoopPollInterval
            ).async_wait(asio::redirect_error(yield, ec));
            
            if (ec) {
                force_error_state();
                return SessionError::kConnectionFailed;
            }
            continue;
        }
        
        // Compress if stream compression is enabled (set after HELLO negotiation)
        framing::MessageSendParams send_params{};
        send_params.message_id = msg->id;
        send_params.payload = msg->payload;
        send_params.compress = stream_->is_compression_enabled();
        
        auto send_result = stream_->send_message(send_params, yield);
        
        if (!send_result) {
            // Network error - transition to error state
            force_error_state();
            return send_result.error();
        }
    }
    
    return outcome::success();
}

// Internal receive loop
VoidResult RlpxSession::run_receive_loop(asio::yield_context yield) noexcept {
    static auto log = rlp::base::createLogger("rlpx.session");
    // Continuously receive messages while session is active
    while (state() == SessionState::kActive) {
        // Receive message from network stream
        auto msg_result = stream_->receive_message(yield);
        
        if (!msg_result) {
            // Network error or connection closed
            SPDLOG_LOGGER_DEBUG(log, "receive_loop: stream error, closing session");
            force_error_state();
            return msg_result.error();
        }
        
        auto& msg = msg_result.value();
        SPDLOG_LOGGER_DEBUG(log, "receive_loop: msg id=0x{:02x} payload_size={}", msg.id, msg.payload.size());
        
        // Convert framing::Message to protocol::Message for routing
        protocol::Message proto_msg{};
        proto_msg.id = msg.id;
        proto_msg.payload = std::move(msg.payload);
        
        // Route message to appropriate handler (if registered)
        route_message(proto_msg);
        
        // Also push to receive channel for pull-based consumption
        framing::Message frame_msg{};
        frame_msg.id = proto_msg.id;
        frame_msg.payload = std::move(proto_msg.payload);
        recv_channel_->push(std::move(frame_msg));
    }
    
    return outcome::success();
}

// Message routing
void RlpxSession::route_message(const protocol::Message& msg) noexcept {
    static auto log = rlp::base::createLogger("rlpx.session");
    // Route based on message ID
    switch (msg.id) {
        case kHelloMessageId:
            if (hello_handler_) {
                auto hello_result = protocol::HelloMessage::decode(msg.payload);
                if (hello_result.has_value()) {
                    hello_handler_(hello_result.value());
                }
            }
            break;

        case kDisconnectMessageId: {
            auto disconnect_result = protocol::DisconnectMessage::decode(msg.payload);
            if (disconnect_result.has_value()) {
                SPDLOG_LOGGER_DEBUG(log, "route: peer sent Disconnect reason={}", static_cast<int>(disconnect_result.value().reason));
                if (disconnect_handler_) {
                    disconnect_handler_(disconnect_result.value());
                }
            } else {
                SPDLOG_LOGGER_DEBUG(log, "route: peer sent Disconnect (decode failed)");
            }
            break;
        }

        case kPingMessageId:
            SPDLOG_LOGGER_DEBUG(log, "route: Ping received");
            if (ping_handler_) {
                auto ping_result = protocol::PingMessage::decode(msg.payload);
                if (ping_result.has_value()) {
                    ping_handler_(ping_result.value());
                }
            }
            break;

        case kPongMessageId:
            SPDLOG_LOGGER_DEBUG(log, "route: Pong received");
            if (pong_handler_) {
                auto pong_result = protocol::PongMessage::decode(msg.payload);
                if (pong_result.has_value()) {
                    pong_handler_(pong_result.value());
                }
            }
            break;

        default:
            if (eth_message_handler_) {
                const uint8_t negotiated_eth_offset = negotiated_eth_offset_;
                const auto eth_message_id = normalize_eth_message_id_for_test(msg.id, negotiated_eth_offset);
                if (eth_message_id.has_value()) {
                    eth_message_handler_(*eth_message_id, msg.payload);
                    return;
                }
            }
            // Unknown message type - call generic handler if set
            if (generic_handler_) {
                generic_handler_(msg);
            }
            break;
    }
}

// State transition helpers
bool RlpxSession::try_transition_state(SessionState from, SessionState to) noexcept {
    SessionState expected = from;
    return state_.compare_exchange_strong(
        expected,
        to,
        std::memory_order_release,
        std::memory_order_acquire
    );
}

bool RlpxSession::is_terminal_state(SessionState state) const noexcept {
    return state == SessionState::kClosed || state == SessionState::kError;
}

void RlpxSession::force_error_state() noexcept {
    state_.store(SessionState::kError, std::memory_order_release);
}

} // namespace rlpx
