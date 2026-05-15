// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include "discv4/discv4_pong.hpp"
#include "discv4/discv4_enr_request.hpp"
#include "discv4/discv4_enr_response.hpp"
#include "discv4/discv4_constants.hpp"
#include "discv4/discv4_error.hpp"
#include <rlp/result.hpp>
#include <rlpx/rlpx_error.hpp>
#include <base/rlp-logger.hpp>
#include <array>
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace discv4 {

using rlp::base::Logger;
using rlp::base::createLogger;

namespace asio = boost::asio;
using udp = asio::ip::udp;

// Node identifier (64 bytes - uncompressed secp256k1 public key)
using NodeId = std::array<uint8_t, 64>;

// Discovered peer information
struct DiscoveredPeer {
    NodeId node_id;
    std::string ip;
    uint16_t udp_port;
    uint16_t tcp_port;
    std::chrono::steady_clock::time_point last_seen;
    std::optional<ForkId> eth_fork_id{};  ///< ENR-derived ForkId; empty if eth entry absent or ENR failed.
};

// Discovery client configuration
struct discv4Config {
    /// @brief Local UDP bind address.
    /// @note Discovery v4 is currently IPv4-only in this implementation.
    ///       Packet handlers and wire endpoint parsing still assume 4-byte IPv4 addresses.
    std::string bind_ip = "0.0.0.0";
    uint16_t bind_port = 30303;
    uint16_t tcp_port = 30303;
    std::array<uint8_t, 32> private_key{};  // secp256k1 private key
    NodeId public_key{};                     // secp256k1 public key (uncompressed, 64 bytes)
    std::chrono::milliseconds ping_timeout{kDefaultPingTimeout};
    std::chrono::seconds peer_expiry{kDefaultPeerExpiry};   // 5 minutes
};

// Callback types
using PeerDiscoveredCallback = std::function<void(const DiscoveredPeer&)>;
using ErrorCallback = std::function<void(const std::string&)>;

/**
 * @brief Discovery v4 protocol client
 *
 * Implements the Ethereum Discovery v4 protocol for peer discovery.
 * Uses UDP for communication with bootstrap nodes and discovered peers.
 *
 * Protocol flow:
 * 1. Send PING to bootstrap nodes
 * 2. Receive PONG responses
 * 3. Send FIND_NODE to discover more peers
 * 4. Receive NEIGHBOURS responses with peer lists
 * 5. Maintain K-bucket routing table
 */
class discv4_client {
public:
    explicit discv4_client(asio::io_context& io_context, const discv4Config& config);
    ~discv4_client();

    // Start discovery process
    rlpx::VoidResult start();

    // Stop discovery
    void stop();

    /// @brief Send PING to a specific node.
    /// @param ip      Target node IP address.
    /// @param port    Target node UDP port.
    /// @param node_id Target node identifier.
    /// @param yield   Boost.Asio stackful coroutine context.
    discv4::Result<discv4_pong> ping(
        const std::string& ip,
        uint16_t port,
        const NodeId& node_id,
        boost::asio::yield_context yield
    );

    /// @brief Send FIND_NODE to discover peers near a target.
    /// @param ip        Target node IP address.
    /// @param port      Target node UDP port.
    /// @param target_id Target node identifier to search near.
    /// @param yield     Boost.Asio stackful coroutine context.
    rlpx::VoidResult find_node(
        const std::string& ip,
        uint16_t port,
        const NodeId& target_id,
        boost::asio::yield_context yield
    );

    /// @brief Send ENRRequest to a bonded peer and return the raw ENR record bytes.
    ///
    /// The peer must already be bonded (ping/pong complete) before calling this.
    /// Mirrors go-ethereum UDPv4::RequestENR().
    ///
    /// @param ip    Target node IP address.
    /// @param port  Target node UDP port.
    /// @param yield Boost.Asio stackful coroutine context.
    /// @return Parsed ENRResponse on success, error on timeout or parse failure.
    discv4::Result<discv4_enr_response> request_enr(
        const std::string& ip,
        uint16_t port,
        boost::asio::yield_context yield
    );

    // Get list of discovered peers
    std::vector<DiscoveredPeer> get_peers() const;

    // Set callback for when new peers are discovered
    void set_peer_discovered_callback(PeerDiscoveredCallback callback);

    // Set error callback
    void set_error_callback(ErrorCallback callback);

    // Get local node ID
    const NodeId& local_node_id() const { return config_.public_key; }

    /// @brief Return the local UDP port the socket is bound to.
    ///        Useful in tests where bind_port=0 (OS-assigned ephemeral port).
    uint16_t bound_port() const noexcept
    {
        return socket_.local_endpoint().port();
    }

private:
    // Receive loop
    void receive_loop(boost::asio::yield_context yield);

    // Handle incoming packet
    void handle_packet(const uint8_t* data, size_t length, const udp::endpoint& sender);

    // Handle specific packet types
    void handle_ping(const uint8_t* data, size_t length, const udp::endpoint& sender);
    void handle_pong(const uint8_t* data, size_t length, const udp::endpoint& sender);
    void handle_find_node(const uint8_t* data, size_t length, const udp::endpoint& sender);
    void handle_neighbours(const uint8_t* data, size_t length, const udp::endpoint& sender);
    void handle_enr_request(const uint8_t* data, size_t length, const udp::endpoint& sender);
    void handle_enr_response(const uint8_t* data, size_t length, const udp::endpoint& sender);

    // Send packet
    discv4::Result<void> send_packet(
        const std::vector<uint8_t>& packet,
        const udp::endpoint& destination,
        boost::asio::yield_context yield
    );

    // Sign packet with ECDSA
    discv4::Result<std::vector<uint8_t>> sign_packet(const std::vector<uint8_t>& payload);

    // Verify packet signature
    discv4::Result<NodeId> verify_packet(const uint8_t* data, size_t length);

    // Compute node ID from public key (keccak256 hash)
    static NodeId compute_node_id(const std::array<uint8_t, 64>& public_key);

    asio::io_context& io_context_;
    discv4Config      config_;
    udp::socket       socket_;
    Logger logger_ = createLogger("discv4");

    // Peer table
    mutable std::mutex peers_mutex_;
    std::unordered_map<std::string, DiscoveredPeer> peers_;  // key: node_id_hex

    // Callbacks
    PeerDiscoveredCallback peer_callback_;
    ErrorCallback error_callback_;

    // Running state
    std::atomic<bool> running_{false};

    // Pending reply entries — one per outstanding PING or FIND_NODE.
    // Keyed by reply_key(). All access on the single io_context thread — no mutex needed.
    struct PendingReply
    {
        std::shared_ptr<boost::asio::steady_timer> timer;
        std::shared_ptr<discv4_pong>               pong;         ///< filled by handle_pong; null for non-ping entries
        std::shared_ptr<discv4_enr_response>       enr_response; ///< filled by handle_enr_response; null for non-ENR entries
        std::array<uint8_t, kWireHashSize>         expected_hash{}; ///< outbound PING/ENRRequest wire hash; used for PONG/ReplyTok verification
    };
    std::unordered_map<std::string, PendingReply> pending_replies_;

    /// Endpoints that completed PING→PONG bond. key: "ip:port"
    std::unordered_set<std::string> bonded_set_;
    /// Endpoints already queued for recursive PING+FIND_NODE (prevents duplicate work). key: "ip:port"
    std::unordered_set<std::string> discovered_set_;

    /// @brief Build the pending-reply map key.
    static std::string reply_key(const std::string& ip, uint16_t port, uint8_t ptype) noexcept;

    /// @brief Ensure a PING→PONG bond exists before sending FIND_NODE.
    ///        Calls ping() if the endpoint is not yet in bonded_set_.
    void ensure_bond(const std::string& ip, uint16_t port,
                     boost::asio::yield_context yield) noexcept;
};

} // namespace discv4

