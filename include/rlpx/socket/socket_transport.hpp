// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"
#include <boost/asio/spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <memory>

namespace rlpx::socket {

/// Transport layer abstraction over TCP socket.
/// Provides async read/write operations with proper error handling.
class SocketTransport {
public:
    using tcp = boost::asio::ip::tcp;
    using Strand = boost::asio::strand<boost::asio::any_io_executor>;

    /// Create transport from connected socket.
    explicit SocketTransport(tcp::socket socket) noexcept;

    // Non-copyable, moveable
    SocketTransport(const SocketTransport&) = delete;
    SocketTransport& operator=(const SocketTransport&) = delete;
    SocketTransport(SocketTransport&&) noexcept = default;
    SocketTransport& operator=(SocketTransport&&) noexcept = default;

    /// @brief Async read exact number of bytes.
    /// @param num_bytes Number of bytes to read.
    /// @param yield     Boost.Asio stackful coroutine context.
    /// @return Filled ByteBuffer on success, SessionError on failure.
    [[nodiscard]] Result<ByteBuffer>
    read_exact(size_t num_bytes, boost::asio::yield_context yield) noexcept;

    /// @brief Async write all bytes.
    /// @param data  Data to send.
    /// @param yield Boost.Asio stackful coroutine context.
    /// @return Success or SessionError on failure.
    [[nodiscard]] VoidResult
    write_all(ByteView data, boost::asio::yield_context yield) noexcept;

    /// Close socket gracefully.
    [[nodiscard]] VoidResult close() noexcept;

    /// Query connection state.
    [[nodiscard]] bool is_open() const noexcept;

    // Get remote endpoint info
    [[nodiscard]] std::string remote_address() const noexcept;
    [[nodiscard]] uint16_t remote_port() const noexcept;

    // Get local endpoint info
    [[nodiscard]] std::string local_address() const noexcept;
    [[nodiscard]] uint16_t local_port() const noexcept;

private:
    tcp::socket socket_;
    Strand strand_; ///< Ensures thread-safe sequential operations.
};

/// @brief Connect to remote endpoint with timeout.
/// @param executor Asio executor to use for the connection.
/// @param host     Remote hostname or IP.
/// @param port     Remote TCP port.
/// @param timeout  Connection timeout duration.
/// @param yield    Boost.Asio stackful coroutine context.
/// @return Connected SocketTransport on success, SessionError on failure.
[[nodiscard]] Result<SocketTransport>
connect_with_timeout(
    boost::asio::any_io_executor executor,
    std::string_view host,
    uint16_t port,
    std::chrono::milliseconds timeout,
    boost::asio::yield_context yield
) noexcept;

} // namespace rlpx::socket
