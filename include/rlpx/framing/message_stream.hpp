// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"
#include "frame_cipher.hpp"
#include "../socket/socket_transport.hpp"
#include <boost/asio/spawn.hpp>
#include <memory>

namespace rlpx::framing {

// Protocol message structure
struct Message {
    uint8_t id;
    ByteBuffer payload;
};

// Message send parameters
struct MessageSendParams {
    uint8_t message_id;
    ByteView payload;
    bool compress;
};

/// Message stream handles framing, encryption, and compression.
class MessageStream {
public:
    /// Takes ownership of cipher and socket transport.
    MessageStream(
        std::unique_ptr<FrameCipher> cipher,
        socket::SocketTransport transport
    ) noexcept;

    /// @brief Send message (encodes, compresses if enabled, frames, encrypts).
    /// @param params Message send parameters.
    /// @param yield  Boost.Asio stackful coroutine context.
    /// @return Success or SessionError on failure.
    [[nodiscard]] VoidResult
    send_message(const MessageSendParams& params, boost::asio::yield_context yield) noexcept;

    /// @brief Receive message (decrypts, deframes, decompresses, decodes).
    /// @param yield Boost.Asio stackful coroutine context.
    /// @return Decoded Message on success, SessionError on failure.
    [[nodiscard]] Result<Message>
    receive_message(boost::asio::yield_context yield) noexcept;

    /// Enable compression after hello exchange.
    void enable_compression() noexcept { compression_enabled_ = true; }

    /// Close the underlying socket, unblocking any pending receive_message call.
    void close() noexcept;

    /// Query state.
    [[nodiscard]] bool is_compression_enabled() const noexcept { 
        return compression_enabled_; 
    }

    /// Access cipher secrets (grouped values).
    [[nodiscard]] const auth::FrameSecrets& cipher_secrets() const noexcept {
        return cipher_->secrets();
    }

private:
    [[nodiscard]] FramingResult<void>
    send_frame(ByteView frame_data, boost::asio::yield_context yield) noexcept;

    [[nodiscard]] FramingResult<ByteBuffer>
    receive_frame(boost::asio::yield_context yield) noexcept;

    std::unique_ptr<FrameCipher> cipher_;
    socket::SocketTransport transport_;
    bool compression_enabled_{false};

    // Reusable buffers to minimize allocations
    ByteBuffer send_buffer_;
    ByteBuffer recv_buffer_;
};

} // namespace rlpx::framing
