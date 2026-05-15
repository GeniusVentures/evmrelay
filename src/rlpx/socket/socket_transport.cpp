// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/socket/socket_transport.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

namespace rlpx::socket {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

/* ============================================================================
 * ASYNC I/O ARCHITECTURE DESIGN
 * ============================================================================
 * 
 * Thread Safety Model:
 * - All socket operations run on a strand to ensure sequential execution
 * - No internal locking needed - strand provides implicit synchronization
 * - Multiple coroutines can safely call read/write - strand serializes them
 * 
 * Coroutine Integration:
 * - All async operations return Result<T> directly when called via yield_context
 * - Use boost::asio::yield_context as completion token (C++17 stackful coroutines)
 * - Error handling via Result<T> instead of exceptions
 * - Proper RAII cleanup on coroutine destruction
 * 
 * Read Operation Flow:
 * 1. read_exact(n, yield) -> allocate buffer of size n
 * 2. async_read(socket, buffer, n bytes) with yield_context
 * 3. On success -> return filled ByteBuffer
 * 4. On error -> convert boost::system::error_code to SessionError
 * 
 * Write Operation Flow:
 * 1. write_all(data, yield) -> create buffer view
 * 2. async_write(socket, buffer) with yield_context
 * 3. On success -> return success
 * 4. On error -> convert error_code to SessionError
 * 
 * Connection Establishment:
 * 1. Resolve hostname to endpoint
 * 2. Create socket with executor
 * 3. Race async_connect() vs timeout timer
 * 4. On connect success -> cancel timer, return SocketTransport
 * 5. On timeout -> cancel connect, return timeout error
 * 
 * Error Mapping:
 * - boost::asio::error::eof -> SessionError::kConnectionFailed
 * - boost::asio::error::connection_reset -> SessionError::kConnectionFailed
 * - boost::asio::error::operation_aborted -> SessionError::kConnectionFailed
 * - Other errors -> SessionError::kInvalidMessage (temporary)
 * 
 * ============================================================================
 */

// Constructor
SocketTransport::SocketTransport(tcp::socket socket) noexcept
    : socket_(std::move(socket))
    , strand_(socket_.get_executor())
{
}

// Async read exact number of bytes
Result<ByteBuffer>
SocketTransport::read_exact(size_t num_bytes, asio::yield_context yield) noexcept {
    // Allocate buffer for incoming data
    ByteBuffer buffer(num_bytes);
    
    boost::system::error_code ec;
    size_t bytes_read = asio::async_read(
        socket_,
        asio::buffer(buffer.data(), num_bytes),
        asio::redirect_error(yield, ec)
    );
    
    if (ec) {
        if (ec == asio::error::eof ||
            ec == asio::error::connection_reset ||
            ec == asio::error::operation_aborted) {
            return SessionError::kConnectionFailed;
        }
        return SessionError::kInvalidMessage;
    }
    
    if (bytes_read != num_bytes) {
        return SessionError::kInvalidMessage;
    }
    
    return buffer;
}

// Async write all bytes
VoidResult
SocketTransport::write_all(ByteView data, asio::yield_context yield) noexcept {
    boost::system::error_code ec;
    asio::async_write(
        socket_,
        asio::buffer(data.data(), data.size()),
        asio::redirect_error(yield, ec)
    );
    
    if (ec) {
        if (ec == asio::error::eof || 
            ec == asio::error::connection_reset ||
            ec == asio::error::broken_pipe) {
            return SessionError::kConnectionFailed;
        }
        return SessionError::kInvalidMessage;
    }
    
    return outcome::success();
}

// Close socket gracefully
VoidResult SocketTransport::close() noexcept {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        
        // Shutdown both send and receive
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        // Ignore shutdown errors - connection may already be closed
        
        // Close the socket
        socket_.close(ec);
        if (ec) {
            return SessionError::kConnectionFailed;
        }
    }
    return outcome::success();
}

// Query connection state
bool SocketTransport::is_open() const noexcept {
    return socket_.is_open();
}

// Get remote endpoint info
std::string SocketTransport::remote_address() const noexcept {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        auto endpoint = socket_.remote_endpoint(ec);
        if (!ec) {
            return endpoint.address().to_string();
        }
    }
    return "";
}

uint16_t SocketTransport::remote_port() const noexcept {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        auto endpoint = socket_.remote_endpoint(ec);
        if (!ec) {
            return endpoint.port();
        }
    }
    return 0;
}

// Get local endpoint info
std::string SocketTransport::local_address() const noexcept {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        auto endpoint = socket_.local_endpoint(ec);
        if (!ec) {
            return endpoint.address().to_string();
        }
    }
    return "";
}

uint16_t SocketTransport::local_port() const noexcept {
    if (socket_.is_open()) {
        boost::system::error_code ec;
        auto endpoint = socket_.local_endpoint(ec);
        if (!ec) {
            return endpoint.port();
        }
    }
    return 0;
}

// Connect to remote endpoint with timeout
Result<SocketTransport>
connect_with_timeout(
    asio::any_io_executor executor,
    std::string_view host,
    uint16_t port,
    std::chrono::milliseconds timeout,
    asio::yield_context yield
) noexcept {
    // Create socket
    tcp::socket socket(executor);

    // Arm the timeout: cancel the socket if the timer fires before connect completes.
    asio::steady_timer timer(executor, timeout);
    timer.async_wait([&socket](const boost::system::error_code& ec)
    {
        if (!ec)
        {
            boost::system::error_code ignore;
            socket.cancel(ignore);
        }
    });

    // Resolve hostname to endpoints
    tcp::resolver resolver(executor);
    boost::system::error_code resolve_ec;
    auto endpoints = resolver.async_resolve(
        host,
        std::to_string(port),
        asio::redirect_error(yield, resolve_ec)
    );

    if (resolve_ec)
    {
        timer.cancel();
        return SessionError::kConnectionFailed;
    }

    // Connect to one of the resolved endpoints
    boost::system::error_code connect_ec;
    asio::async_connect(
        socket,
        endpoints,
        asio::redirect_error(yield, connect_ec)
    );

    timer.cancel();

    if (connect_ec)
    {
        return SessionError::kConnectionFailed;
    }

    // Connection successful
    return SocketTransport(std::move(socket));
}

} // namespace rlpx::socket
