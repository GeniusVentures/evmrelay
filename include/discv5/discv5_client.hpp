// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>

#include <base/rlp-logger.hpp>
#include <discv5/discv5_crawler.hpp>
#include <discv5/discv5_error.hpp>
#include <discv5/discv5_types.hpp>

namespace discv5
{

using rlp::base::Logger;
using rlp::base::createLogger;

namespace asio = boost::asio;
using udp = asio::ip::udp;

// ---------------------------------------------------------------------------
// discv5_client
// ---------------------------------------------------------------------------

/// @brief Discovery v5 protocol client.
///
/// Owns the UDP socket, drives the receive loop, and delegates peer lifecycle
/// management to an internal @p discv5_crawler.
///
/// The public interface is deliberately narrow and mirrors the discv4_client
/// shape so that callers can adopt either protocol with minimal friction:
///
/// @code{.cpp}
///   discv5::discv5Config cfg;
///   cfg.bootstrap_enrs = ChainBootnodeRegistry::for_chain(ChainId::kEthereumSepolia)->fetch();
///   auto client = std::make_unique<discv5::discv5_client>(io, cfg);
///   client->set_peer_discovered_callback([](const discovery::ValidatedPeer& p){ … });
///   client->start();
///   io.run();
/// @endcode
///
/// Thread safety
/// -------------
/// All public methods must be called from the thread that drives the supplied
/// @p io_context.  The peer-discovered callback is invoked on that same thread.
class discv5_client
{
public:
    /// @brief Construct the client.
    ///
    /// Does NOT bind the socket or start the receive loop.  Call start().
    ///
    /// @param io_context  Boost.Asio io_context.  Must outlive this object.
    /// @param config      Client configuration.  A copy is taken.
    explicit discv5_client(asio::io_context& io_context, const discv5Config& config);

    ~discv5_client();

    // Non-copyable, non-movable.
    discv5_client(const discv5_client&)            = delete;
    discv5_client& operator=(const discv5_client&) = delete;
    discv5_client(discv5_client&&)                 = delete;
    discv5_client& operator=(discv5_client&&)      = delete;

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    /// @brief Add a single bootstrap ENR URI.
    ///
    /// May be called before or after start().
    ///
    /// @param enr_uri  "enr:…" or "enode://…" string.
    void add_bootnode(const std::string& enr_uri) noexcept;

    /// @brief Register the callback invoked for each newly discovered peer.
    ///
    /// @param callback  Called with a ValidatedPeer on every new discovery.
    void set_peer_discovered_callback(PeerDiscoveredCallback callback) noexcept;

    /// @brief Register the error callback.
    ///
    /// @param callback  Called with a diagnostic string on non-fatal errors.
    void set_error_callback(ErrorCallback callback) noexcept;

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    /// @brief Bind the UDP socket and start the receive + crawler loops.
    ///
    /// @return  success, or a discv5Error on socket/bind failure.
    VoidResult start() noexcept;

    /// @brief Close the UDP socket and signal the crawler to stop.
    void stop() noexcept;

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------

    /// @brief Return a snapshot of current crawler activity counters.
    [[nodiscard]] CrawlerStats stats() const noexcept;

    /// @brief Return the local node identifier (64-byte public key).
    [[nodiscard]] const NodeId& local_node_id() const noexcept;

    /// @brief Returns true when the client has been started and not yet stopped.
    [[nodiscard]] bool is_running() const noexcept;

    /// @brief Return the local UDP port the socket is currently bound to.
    ///        Useful in tests when bind_port is 0 (ephemeral OS-assigned port).
    [[nodiscard]] uint16_t bound_port() const noexcept;

    /// @brief Return the number of non-undersized UDP packets accepted by the receive loop.
    [[nodiscard]] size_t received_packet_count() const noexcept;

    /// @brief Return the number of undersized UDP packets dropped by the receive loop.
    [[nodiscard]] size_t dropped_undersized_packet_count() const noexcept;

    /// @brief Return the number of FINDNODE send attempts that failed.
    [[nodiscard]] size_t send_findnode_failure_count() const noexcept;

    /// @brief Return the number of valid WHOAREYOU packets parsed by the receive path.
    [[nodiscard]] size_t whoareyou_packet_count() const noexcept;

    /// @brief Return the number of successfully decrypted handshake packets.
    [[nodiscard]] size_t handshake_packet_count() const noexcept;

    /// @brief Return the number of outbound handshake send attempts.
    [[nodiscard]] size_t outbound_handshake_attempt_count() const noexcept;

    /// @brief Return the number of outbound handshake send attempts that failed.
    [[nodiscard]] size_t outbound_handshake_failure_count() const noexcept;

    /// @brief Return the number of inbound handshake packets rejected during auth parsing.
    [[nodiscard]] size_t inbound_handshake_reject_auth_count() const noexcept;

    /// @brief Return the number of inbound handshake packets rejected due to missing/mismatched challenge state.
    [[nodiscard]] size_t inbound_handshake_reject_challenge_count() const noexcept;

    /// @brief Return the number of inbound handshake packets rejected during ENR/identity validation.
    [[nodiscard]] size_t inbound_handshake_reject_record_count() const noexcept;

    /// @brief Return the number of inbound handshake packets rejected during shared-secret/key derivation.
    [[nodiscard]] size_t inbound_handshake_reject_crypto_count() const noexcept;

    /// @brief Return the number of inbound handshake packets rejected due to message decrypt failure.
    [[nodiscard]] size_t inbound_handshake_reject_decrypt_count() const noexcept;

    /// @brief Return the number of inbound handshake packets observed before validation.
    [[nodiscard]] size_t inbound_handshake_seen_count() const noexcept;

    /// @brief Return the number of inbound MESSAGE packets observed before validation/decrypt.
    [[nodiscard]] size_t inbound_message_seen_count() const noexcept;

    /// @brief Return the number of inbound MESSAGE packets that failed decrypt with a matching session.
    [[nodiscard]] size_t inbound_message_decrypt_fail_count() const noexcept;

    /// @brief Return the number of successfully decoded NODES packets.
    [[nodiscard]] size_t nodes_packet_count() const noexcept;

private:
    // -----------------------------------------------------------------------
    // Internal coroutines
    // -----------------------------------------------------------------------

    /// @brief Async receive loop — reads UDP packets and dispatches them.
    ///
    /// @param yield  Boost.ASIO stackful coroutine context.
    void receive_loop(asio::yield_context yield);

    /// @brief Async crawler loop — drains the queued peer set and issues
    ///        FINDNODE requests at the configured interval.
    ///
    /// @param yield  Boost.ASIO stackful coroutine context.
    void crawler_loop(asio::yield_context yield);

    /// @brief Handle a raw incoming UDP packet.
    ///
    /// @param data    Pointer to packet bytes.
    /// @param length  Byte count.
    /// @param sender  Remote endpoint.
    void handle_packet(
        const uint8_t*     data,
        size_t             length,
        const udp::endpoint& sender) noexcept;

    /// @brief Send a FINDNODE request to @p target.
    ///
    /// @param peer    Peer to query.
    /// @param yield   Boost.ASIO stackful coroutine context.
    VoidResult send_findnode(const ValidatedPeer& peer, asio::yield_context yield);

    /// @brief Send a raw UDP packet to a peer endpoint.
    VoidResult send_packet(
        const std::vector<uint8_t>& packet,
        const ValidatedPeer& peer,
        asio::yield_context yield);

    /// @brief Send a WHOAREYOU challenge in response to a packet from @p sender.
    VoidResult send_whoareyou(
        const udp::endpoint& sender,
        const std::array<uint8_t, kKeccak256Bytes>& remote_node_addr,
        const std::array<uint8_t, kGcmNonceBytes>& request_nonce,
        asio::yield_context yield);

    /// @brief Process a decrypted FINDNODE request and send a NODES response.
    VoidResult handle_findnode_request(
        const std::vector<uint8_t>& req_id,
        const udp::endpoint& sender,
        asio::yield_context yield);

    /// @brief Build the local ENR record used in handshake/NODES responses.
    Result<std::vector<uint8_t>> build_local_enr() noexcept;

    // -----------------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------------

    struct SessionState
    {
        std::array<uint8_t, 16U> write_key{};
        std::array<uint8_t, 16U> read_key{};
        std::array<uint8_t, kKeccak256Bytes> remote_node_addr{};
        NodeId remote_node_id{};
        std::vector<uint8_t> last_req_id{};
    };

    struct PendingRequest
    {
        ValidatedPeer peer{};
        std::vector<uint8_t> req_id{};
        std::array<uint8_t, kGcmNonceBytes> request_nonce{};
        std::vector<uint8_t> challenge_data{};
        std::array<uint8_t, kWhoareyouIdNonceBytes> id_nonce{};
        uint64_t record_seq{};
        bool have_challenge{false};
    };

    struct ChallengeState
    {
        std::array<uint8_t, kKeccak256Bytes> remote_node_addr{};
        std::vector<uint8_t> challenge_data{};
        std::array<uint8_t, kGcmNonceBytes> request_nonce{};
        std::array<uint8_t, kWhoareyouIdNonceBytes> id_nonce{};
        uint64_t record_seq{};
    };

    asio::io_context& io_context_;
    discv5Config      config_;
    udp::socket       socket_;
    discv5_crawler    crawler_;
    Logger            logger_ = createLogger("discv5");

    std::unordered_map<std::string, SessionState> sessions_;
    std::unordered_map<std::string, PendingRequest> pending_requests_;
    std::unordered_map<std::string, ChallengeState> sent_challenges_;

    std::atomic<bool>   running_{false};
    std::atomic<size_t> received_packets_{0U};
    std::atomic<size_t> dropped_undersized_packets_{0U};
    std::atomic<size_t> send_findnode_failures_{0U};
    std::atomic<size_t> whoareyou_packets_{0U};
    std::atomic<size_t> handshake_packets_{0U};
    std::atomic<size_t> outbound_handshake_attempts_{0U};
    std::atomic<size_t> outbound_handshake_failures_{0U};
    std::atomic<size_t> inbound_handshake_reject_auth_{0U};
    std::atomic<size_t> inbound_handshake_reject_challenge_{0U};
    std::atomic<size_t> inbound_handshake_reject_record_{0U};
    std::atomic<size_t> inbound_handshake_reject_crypto_{0U};
    std::atomic<size_t> inbound_handshake_reject_decrypt_{0U};
    std::atomic<size_t> inbound_handshake_seen_{0U};
    std::atomic<size_t> inbound_message_seen_{0U};
    std::atomic<size_t> inbound_message_decrypt_fail_{0U};
    std::atomic<size_t> nodes_packets_{0U};
};

} // namespace discv5
