// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv4/discv4_client.hpp"
#include "discv4/discv4_constants.hpp"
#include "discv4/discv4_error.hpp"
#include "discv4/discv4_ping.hpp"
#include "discv4/discv4_pong.hpp"
#include "discv4/discv4_enr_request.hpp"
#include "discv4/discv4_enr_response.hpp"
#include "base/rlp-logger.hpp"
#include <boost/asio/spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <secp256k1.h>
#include <secp256k1_recovery.h>
#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>

namespace discv4 {

// All wire-protocol constants live in discv4_constants.hpp.
// The following are implementation-only aliases for local brevity.
static constexpr size_t  kHashSize           = kWireHashSize;
static constexpr size_t  kSigSize            = kWireSigSize;
static constexpr size_t  kPacketTypeSize     = kWirePacketTypeSize;
static constexpr size_t  kPacketHeaderSize   = kWireHeaderSize;
static constexpr size_t  kPacketTypeOffset   = kWirePacketTypeOffset;
static constexpr size_t  kUncompressedPubKey = kUncompressedPubKeySize;

discv4_client::discv4_client(asio::io_context& io_context, const discv4Config& config)
    : io_context_(io_context)
    , config_(config)
    , socket_(io_context) {
    boost::system::error_code ec;
    const auto bind_address = asio::ip::make_address(config_.bind_ip, ec);
    if (ec) {
        throw std::runtime_error("Invalid bind_ip: " + config_.bind_ip + " (" + ec.message() + ")");
    }

    if (!bind_address.is_v4()) {
        throw std::runtime_error(
            "discv4 bind_ip must be IPv4 until discv4 handlers are IPv6-safe: " + config_.bind_ip);
    }

    socket_.open(udp::v4(), ec);
    if (ec) {
        throw std::runtime_error("Failed to open UDP socket: " + ec.message());
    }

    socket_.bind(udp::endpoint(bind_address, config_.bind_port), ec);
    if (ec) {
        throw std::runtime_error("Failed to bind UDP socket to " + config_.bind_ip + ":" +
                                 std::to_string(config_.bind_port) + " (" + ec.message() + ")");
    }
}

discv4_client::~discv4_client() {
    stop();
}

rlpx::VoidResult discv4_client::start() {
    if (running_.exchange(true)) {
        return rlp::outcome::success();  // Already running
    }

    // Start receive loop
    asio::spawn(io_context_, [this](asio::yield_context yield) {
        receive_loop(yield);
    });

    return rlp::outcome::success();
}

void discv4_client::stop()
{
    running_ = false;
    for (auto& [key, reply] : pending_replies_)
    {
        reply.timer->cancel();
    }
    pending_replies_.clear();
    boost::system::error_code ec;
    socket_.close(ec);
}

void discv4_client::receive_loop(asio::yield_context yield) {
    std::array<uint8_t, kUdpBufferSize> buffer{};

    while (running_) {
        udp::endpoint sender_endpoint;
        boost::system::error_code ec;

        size_t bytes_received = socket_.async_receive_from(
            asio::buffer(buffer),
            sender_endpoint,
            asio::redirect_error(yield, ec)
        );

        if (ec) {
            if (ec == asio::error::operation_aborted) {
                break;  // Socket closed
            }
            if (error_callback_) {
                error_callback_("Receive error: " + ec.message());
            }
            continue;
        }

        if (bytes_received > 0) {
            handle_packet(buffer.data(), bytes_received, sender_endpoint);
        }
    }
}

void discv4_client::handle_packet(const uint8_t* data, size_t length, const udp::endpoint& sender) {
    if (length < kPacketHeaderSize) {
        return;
    }

    const uint8_t packet_type = data[kPacketTypeOffset];

    switch (packet_type) {
        case kPacketTypePing:
            handle_ping(data, length, sender);
            break;
        case kPacketTypePong:
            handle_pong(data, length, sender);
            break;
        case kPacketTypeFindNode:
            handle_find_node(data, length, sender);
            break;
        case kPacketTypeNeighbours:
            handle_neighbours(data, length, sender);
            break;
        case kPacketTypeEnrRequest:
            handle_enr_request(data, length, sender);
            break;
        case kPacketTypeEnrResponse:
            handle_enr_response(data, length, sender);
            break;
        default:
            if (error_callback_) {
                error_callback_("Unknown packet type: " + std::to_string(packet_type));
            }
            break;
    }
}

void discv4_client::handle_ping(const uint8_t* data, size_t length, const udp::endpoint& sender) {
    if (length < kPacketHeaderSize) {
        return;
    }

    // The ping_hash field in PONG = the first kHashSize bytes of the incoming PING wire packet
    std::array<uint8_t, kHashSize> ping_hash{};
    std::copy(data, data + kHashSize, ping_hash.begin());
    logger_->debug("PING from {}:{} — sending PONG", sender.address().to_string(), sender.port());

    rlp::RlpEncoder encoder;
    if (!encoder.BeginList()) { return; }

    if (!encoder.BeginList()) { return; }
    const auto addr     = sender.address().to_v4().to_bytes();
    const rlp::ByteView ip_bv(addr.data(), addr.size());
    if (!encoder.add(ip_bv))                      { return; }
    if (!encoder.add(sender.port()))               { return; }
    if (!encoder.add(config_.tcp_port))            { return; }
    if (!encoder.EndList())                        { return; }

    if (!encoder.add(rlp::ByteView(ping_hash.data(), ping_hash.size()))) { return; }

    const uint32_t expiration = static_cast<uint32_t>(std::time(nullptr)) + kPacketExpirySeconds;
    if (!encoder.add(expiration)) { return; }

    if (!encoder.EndList()) { return; }

    auto bytes_result = encoder.MoveBytes();
    if (!bytes_result) { return; }

    std::vector<uint8_t> payload;
    payload.reserve(kPacketTypeSize + bytes_result.value().size());
    payload.push_back(kPacketTypePong);
    payload.insert(payload.end(), bytes_result.value().begin(), bytes_result.value().end());

    auto signed_packet = sign_packet(payload);
    if (!signed_packet) { return; }

    const std::string ip   = sender.address().to_string();
    const uint16_t    port = sender.port();

    // Mark sender as bonded — they reached us so the endpoint is proven reachable.
    bonded_set_.insert(ip + ":" + std::to_string(port));

    asio::spawn(io_context_,
        [this, ip, port, pkt = std::move(signed_packet.value())](asio::yield_context yield)
        {
            const udp::endpoint dest(asio::ip::make_address(ip), port);
            auto send_result = send_packet(pkt, dest, yield);
            if (!send_result) { return; }
            logger_->debug("Bond complete with {}:{} — sending FIND_NODE", ip, port);
            auto find_result = find_node(ip, port, config_.public_key, yield);
            (void)find_result;
        });
}

void discv4_client::handle_pong(const uint8_t* data, size_t length, const udp::endpoint& sender) {
    if (length < kPacketHeaderSize) {
        return;
    }

    const rlp::ByteView full_packet(data, length);
    auto pong_result = discv4_pong::Parse(full_packet);
    if (!pong_result) {
        return; // silently drop malformed PONG
    }

    logger_->debug("PONG from {}:{}", sender.address().to_string(), sender.port());

    // Only accept PONG if we have an outstanding PING to this endpoint.
    // Mirrors go-ethereum verifyPong → handleReply check.
    const std::string key = reply_key(sender.address().to_string(), sender.port(), kPacketTypePong);
    auto it = pending_replies_.find(key);
    if (it == pending_replies_.end())
    {
        return; // unsolicited — drop
    }

    if (pong_result.value().pingHash != it->second.expected_hash)
    {
        return; // wrong ping token — drop stale/spoofed PONG
    }

    *it->second.pong = std::move(pong_result.value());
    it->second.timer->cancel(); // wake the waiting ping() coroutine
}

void discv4_client::handle_find_node(const uint8_t* data, size_t length, const udp::endpoint& sender) {
    // TODO: Implement FIND_NODE handling — respond with NEIGHBOURS
    (void)data;
    (void)length;
    (void)sender;
}

void discv4_client::handle_neighbours(const uint8_t* data, size_t length, const udp::endpoint& sender) {
    if (length < kPacketHeaderSize) {
        return;
    }

    // Only accept NEIGHBOURS if we have an outstanding FIND_NODE to this endpoint.
    // Mirrors go-ethereum verifyNeighbors → handleReply check.
    const std::string key = reply_key(sender.address().to_string(), sender.port(), kPacketTypeNeighbours);
    auto it = pending_replies_.find(key);
    if (it == pending_replies_.end())
    {
        return; // unsolicited — drop
    }

    // Cancel the waiting find_node() coroutine — we got the reply.
    it->second.timer->cancel();

    const rlp::ByteView raw(data + kPacketHeaderSize, length - kPacketHeaderSize);
    rlp::RlpDecoder decoder(raw);

    auto outer_len_result = decoder.ReadListHeaderBytes();
    if (!outer_len_result) { return; }

    auto nodes_len_result = decoder.ReadListHeaderBytes();
    if (!nodes_len_result) { return; }

    const rlp::ByteView after_nodes_start = decoder.Remaining();
    const size_t        nodes_byte_len    = nodes_len_result.value();

    while (!decoder.IsFinished())
    {
        const size_t consumed = after_nodes_start.size() - decoder.Remaining().size();
        if (consumed >= nodes_byte_len) { break; }

        auto node_len_result = decoder.ReadListHeaderBytes();
        if (!node_len_result) { break; }
        const rlp::ByteView node_start = decoder.Remaining();
        const size_t        node_len   = node_len_result.value();

        rlp::Bytes ip_bytes;
        if (!decoder.read(ip_bytes) || ip_bytes.size() != kIPv4Size) { break; }

        uint16_t udp_port = 0;
        if (!decoder.read(udp_port)) { break; }

        uint16_t tcp_port = 0;
        if (!decoder.read(tcp_port)) { break; }

        rlp::Bytes pubkey_bytes;
        if (!decoder.read(pubkey_bytes) || pubkey_bytes.size() != kNodeIdSize) { break; }

        // Skip any remaining fields in this node entry for forward compatibility.
        const size_t node_consumed = node_start.size() - decoder.Remaining().size();
        if (node_consumed < node_len)
        {
            const size_t        remaining_in_node = node_len - node_consumed;
            const rlp::ByteView skip_view         = decoder.Remaining().substr(0, remaining_in_node);
            rlp::RlpDecoder     skip_decoder(skip_view);
            while (!skip_decoder.IsFinished()) { if (!skip_decoder.SkipItem()) { break; } }
            const size_t        actually_skipped = skip_view.size() - skip_decoder.Remaining().size();
            decoder = rlp::RlpDecoder(decoder.Remaining().substr(actually_skipped));
        }

        if (tcp_port == 0) { continue; }

        DiscoveredPeer peer;
        std::copy(pubkey_bytes.begin(), pubkey_bytes.end(), peer.node_id.begin());
        peer.ip = std::to_string(ip_bytes[0]) + "." +
                  std::to_string(ip_bytes[1]) + "." +
                  std::to_string(ip_bytes[2]) + "." +
                  std::to_string(ip_bytes[3]);
        peer.udp_port  = udp_port;
        peer.tcp_port  = tcp_port;
        peer.last_seen = std::chrono::steady_clock::now();

        {
            const std::lock_guard<std::mutex> lock(peers_mutex_);
            std::string node_key;
            node_key.reserve(kNodeIdHexSize);
            for (const uint8_t byte : peer.node_id)
            {
                const char* hex_chars = "0123456789abcdef";
                node_key += hex_chars[byte >> 4];
                node_key += hex_chars[byte & 0x0fu];
            }
            peers_[node_key] = peer;
        }

        // Recursive kademlia: bond -> ENR enrichment -> peer_callback_ -> find_node.
        // peer_callback_ is deferred into the coroutine so eth_fork_id is populated
        // before the caller decides whether to enqueue the peer for dialing.
        const std::string ep_key = peer.ip + ":" + std::to_string(peer.udp_port);
        if (running_ && discovered_set_.count(ep_key) == 0)
        {
            discovered_set_.insert(ep_key);
            const std::string disc_ip   = peer.ip;
            const uint16_t    disc_port = peer.udp_port;
            const NodeId      disc_id   = peer.node_id;
            asio::spawn(io_context_,
                [this, disc_ip, disc_port, disc_id, enriched_peer = peer](asio::yield_context yield) mutable
                {
                    ensure_bond(disc_ip, disc_port, yield);

                    // Request ENR and populate eth_fork_id when available.
                    auto enr_result = request_enr(disc_ip, disc_port, yield);
                    if (enr_result)
                    {
                        auto fork_result = enr_result.value().ParseEthForkId();
                        if (fork_result)
                        {
                            enriched_peer.eth_fork_id = fork_result.value();
                        }
                    }

                    if (peer_callback_)
                    {
                        logger_->debug("Neighbour peer: {}:{}", enriched_peer.ip, enriched_peer.tcp_port);
                        peer_callback_(enriched_peer);
                    }

                    auto fn = find_node(disc_ip, disc_port, disc_id, yield);
                    (void)fn;
                });
        }
    }
}


void discv4_client::handle_enr_request(const uint8_t* /*data*/, size_t /*length*/, const udp::endpoint& sender)
{
    // We do not yet maintain a local ENR record, so we cannot send a valid ENRResponse.
    // Silently drop inbound ENRRequests — mirrors the behaviour of a node that has no
    // ENR to advertise.  This will be revisited when local ENR support is added.
    logger_->debug("ENRRequest from {}:{} — no local ENR, dropping",
                   sender.address().to_string(), sender.port());
}

void discv4_client::handle_enr_response(const uint8_t* data, size_t length, const udp::endpoint& sender)
{
    if ( length < kPacketHeaderSize )
    {
        return;
    }

    const rlp::ByteView raw( data, length );
    auto parse_result = discv4_enr_response::Parse( raw );
    if ( !parse_result )
    {
        return; // silently drop malformed ENRResponse
    }

    const std::string key = reply_key( sender.address().to_string(), sender.port(), kPacketTypeEnrResponse );
    auto it = pending_replies_.find( key );
    if ( it == pending_replies_.end() )
    {
        return; // unsolicited — drop
    }

    // Verify ReplyTok matches the hash of the ENRRequest we sent.
    if ( parse_result.value().request_hash != it->second.expected_hash )
    {
        return; // wrong token — drop
    }

    *it->second.enr_response = std::move( parse_result.value() );
    it->second.timer->cancel(); // wake the waiting request_enr() coroutine
}

discv4::Result<discv4_enr_response> discv4_client::request_enr(
    const std::string& ip,
    uint16_t           port,
    asio::yield_context yield )
{
    if ( !running_ ) { return discv4Error::kNetworkSendFailed; }

    discv4_enr_request req;
    req.expiration = static_cast<uint64_t>( std::time( nullptr ) ) + kPacketExpirySeconds;

    const auto payload = req.RlpPayload();
    if ( payload.empty() ) { return discv4Error::kRlpPayloadEmpty; }

    auto signed_packet = sign_packet( payload );
    if ( !signed_packet ) { return discv4Error::kSigningFailed; }

    // The outer hash occupies the first kWireHashSize bytes of the signed wire packet.
    // This is what the remote will echo back as ReplyTok in its ENRResponse.
    std::array<uint8_t, kWireHashSize> sent_hash{};
    std::copy( signed_packet.value().begin(),
               signed_packet.value().begin() + kWireHashSize,
               sent_hash.begin() );

    // Register pending reply before sending — mirrors go-ethereum's RequestENR flow.
    const std::string key      = reply_key( ip, port, kPacketTypeEnrResponse );
    auto              timer    = std::make_shared<asio::steady_timer>( io_context_ );
    auto              enr_slot = std::make_shared<discv4_enr_response>();

    PendingReply entry{};
    entry.timer         = timer;
    entry.enr_response  = enr_slot;
    entry.expected_hash = sent_hash;
    pending_replies_[key] = std::move( entry );

    timer->expires_after( config_.ping_timeout );

    const udp::endpoint destination( asio::ip::make_address( ip ), port );
    auto send_result = send_packet( signed_packet.value(), destination, yield );
    if ( !send_result )
    {
        pending_replies_.erase( key );
        return discv4Error::kNetworkSendFailed;
    }

    boost::system::error_code ec;
    timer->async_wait( asio::redirect_error( yield, ec ) );
    pending_replies_.erase( key );

    if ( !enr_slot->record_rlp.empty() )
    {
        // The reply slot is authoritative: a fast ENRResponse can arrive before
        // async_wait() is armed, in which case timer->cancel() has no pending wait
        // to abort. Treat a populated slot as success on all platforms.
        return *enr_slot;
    }
    return discv4Error::kPongTimeout;
}

std::string discv4_client::reply_key(const std::string& ip, uint16_t port, uint8_t ptype) noexcept{
    return ip + ":" + std::to_string(port) + ":" + std::to_string(ptype);
}

void discv4_client::ensure_bond(const std::string& ip, uint16_t port,
                                 boost::asio::yield_context yield) noexcept
{
    const std::string ep_key = ip + ":" + std::to_string(port);
    if (bonded_set_.count(ep_key) != 0) { return; }

    NodeId dummy_id{};
    auto result = ping(ip, port, dummy_id, yield);
    if (result) { bonded_set_.insert(ep_key); }
}

discv4::Result<discv4_pong> discv4_client::ping(
    const std::string& ip,
    uint16_t port,
    const NodeId& /*node_id*/,
    asio::yield_context yield
) {
    if (!running_) { return discv4Error::kNetworkSendFailed; }
    discv4_ping ping_packet(
        config_.bind_ip, config_.bind_port, config_.tcp_port,
        ip, port, port
    );

    auto payload = ping_packet.RlpPayload();
    if (payload.empty()) { return discv4Error::kRlpPayloadEmpty; }

    auto signed_packet = sign_packet(payload);
    if (!signed_packet) { return discv4Error::kSigningFailed; }

    std::array<uint8_t, kWireHashSize> sent_hash{};
    std::copy(signed_packet.value().begin(),
              signed_packet.value().begin() + kWireHashSize,
              sent_hash.begin());

    // Register pending reply matcher before sending — mirrors go-ethereum's sendPing replyMatcher.
    const std::string key  = reply_key(ip, port, kPacketTypePong);
    auto timer      = std::make_shared<asio::steady_timer>(io_context_);
    auto pong_slot  = std::make_shared<discv4_pong>();
    pending_replies_[key] = PendingReply{ timer, pong_slot, nullptr, sent_hash };
    timer->expires_after(config_.ping_timeout);

    udp::endpoint destination(asio::ip::make_address(ip), port);
    auto send_result = send_packet(signed_packet.value(), destination, yield);
    if (!send_result)
    {
        pending_replies_.erase(key);
        return discv4Error::kNetworkSendFailed;
    }

    boost::system::error_code ec;
    timer->async_wait(asio::redirect_error(yield, ec));
    pending_replies_.erase(key);

    if (pong_slot->expiration != 0)
    {
        // The reply slot is authoritative: a fast PONG can arrive before
        // async_wait() is armed, in which case timer->cancel() has no pending wait
        // to abort. Treat a populated slot as success on all platforms.
        bonded_set_.insert(ip + ":" + std::to_string(port));
        return *pong_slot;
    }
    return discv4Error::kPongTimeout;
}

rlpx::VoidResult discv4_client::find_node(
    const std::string& ip,
    uint16_t port,
    const NodeId& target_id,
    asio::yield_context yield
) {
    if (!running_) { return rlp::outcome::success(); }
    // Ensure bond before querying — mirrors go-ethereum's ensureBond call in findnode().
    ensure_bond(ip, port, yield);

    rlp::RlpEncoder encoder;
    if (!encoder.BeginList()) { return rlp::outcome::success(); }
    if (!encoder.add(rlp::ByteView(target_id.data(), target_id.size()))) { return rlp::outcome::success(); }
    const uint32_t expiration = static_cast<uint32_t>(std::time(nullptr)) + kPacketExpirySeconds;
    if (!encoder.add(expiration)) { return rlp::outcome::success(); }
    if (!encoder.EndList()) { return rlp::outcome::success(); }

    auto bytes_result = encoder.MoveBytes();
    if (!bytes_result) { return rlp::outcome::success(); }

    std::vector<uint8_t> payload;
    payload.reserve(kWirePacketTypeSize + bytes_result.value().size());
    payload.push_back(kPacketTypeFindNode);
    payload.insert(payload.end(), bytes_result.value().begin(), bytes_result.value().end());

    auto signed_packet = sign_packet(payload);
    if (!signed_packet) { return rlp::outcome::success(); }

    // Register pending reply matcher for NEIGHBOURS before sending — mirrors go-ethereum's pending() call.
    const std::string key   = reply_key(ip, port, kPacketTypeNeighbours);
    auto timer = std::make_shared<asio::steady_timer>(io_context_);
    pending_replies_[key] = PendingReply{ timer, nullptr, nullptr, {} };
    timer->expires_after(config_.ping_timeout); // reuse ping_timeout as findnode reply timeout

    const udp::endpoint destination(asio::ip::make_address(ip), port);
    auto send_result = send_packet(signed_packet.value(), destination, yield);
    if (!send_result)
    {
        pending_replies_.erase(key);
        return rlp::outcome::success();
    }

    boost::system::error_code ec;
    timer->async_wait(asio::redirect_error(yield, ec));
    pending_replies_.erase(key);

    return rlp::outcome::success();
}

discv4::Result<void> discv4_client::send_packet(
    const std::vector<uint8_t>& packet,
    const udp::endpoint& destination,
    asio::yield_context yield
) {
    boost::system::error_code ec;

    socket_.async_send_to(
        asio::buffer(packet),
        destination,
        asio::redirect_error(yield, ec)
    );

    if (ec) {
        if (error_callback_) {
            error_callback_("Send error: " + ec.message());
        }
        return discv4Error::kNetworkSendFailed;
    }

    return rlp::outcome::success();
}

discv4::Result<std::vector<uint8_t>> discv4_client::sign_packet(const std::vector<uint8_t>& payload) {
    // payload = packet-type(1) || RLP(packet-data)
    //
    // Correct discv4 packet construction (EIP-8 / go-ethereum discv4):
    //   sig  = sign( keccak256( packet-type || packet-data ) )   [65 bytes, recoverable]
    //   hash = keccak256( sig || packet-type || packet-data )    [32 bytes]
    //   wire = hash || sig || packet-type || packet-data

    // Step 1: sign keccak256(payload)
    const auto msg_hash = discv4_packet::Keccak256(payload);

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if (!ctx) {
        return discv4Error::kContextCreationFailed;
    }

    secp256k1_ecdsa_recoverable_signature sig;
    if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, msg_hash.data(),
                                          config_.private_key.data(), nullptr, nullptr)) {
        secp256k1_context_destroy(ctx);
        return discv4Error::kSigningFailed;
    }

    std::array<uint8_t, kSigSize> sig_bytes{};
    int recid = 0;
    secp256k1_ecdsa_recoverable_signature_serialize_compact(
        ctx, sig_bytes.data(), &recid, &sig);
    sig_bytes[kSigSize - 1] = static_cast<uint8_t>(recid);

    secp256k1_context_destroy(ctx);

    // Step 2: outer hash = keccak256( sig || payload )
    std::vector<uint8_t> to_outer_hash;
    to_outer_hash.reserve(kSigSize + payload.size());
    to_outer_hash.insert(to_outer_hash.end(), sig_bytes.begin(), sig_bytes.end());
    to_outer_hash.insert(to_outer_hash.end(), payload.begin(), payload.end());
    const auto outer_hash = discv4_packet::Keccak256(to_outer_hash);

    // Step 3: assemble wire packet
    std::vector<uint8_t> packet;
    packet.reserve(kHashSize + kSigSize + payload.size());
    packet.insert(packet.end(), outer_hash.begin(), outer_hash.end());
    packet.insert(packet.end(), sig_bytes.begin(), sig_bytes.end());
    packet.insert(packet.end(), payload.begin(), payload.end());

    return packet;
}

discv4::Result<NodeId> discv4_client::verify_packet(const uint8_t* data, size_t length) {
    if (length < kPacketHeaderSize) {
        return discv4Error::kPacketTooSmall;
    }

    // Wire layout: hash(kHashSize) || sig(kSigSize) || type(1) || packet-data
    const uint8_t* hash_data      = data;
    const uint8_t* sig_data       = data + kHashSize;
    const uint8_t* payload_data   = data + kHashSize + kSigSize;
    const size_t   payload_length = length - kHashSize - kSigSize;

    // Verify outer hash = keccak256( sig || payload )
    std::vector<uint8_t> to_outer_hash;
    to_outer_hash.reserve(kSigSize + payload_length);
    to_outer_hash.insert(to_outer_hash.end(), sig_data,     sig_data + kSigSize);
    to_outer_hash.insert(to_outer_hash.end(), payload_data, payload_data + payload_length);
    const auto computed_hash = discv4_packet::Keccak256(to_outer_hash);

    if (std::memcmp(hash_data, computed_hash.data(), kHashSize) != 0) {
        return discv4Error::kHashMismatch;
    }

    // Recover public key: sig was made over keccak256( payload )
    const std::vector<uint8_t> payload_vec(payload_data, payload_data + payload_length);
    const auto msg_hash = discv4_packet::Keccak256(payload_vec);

    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (!ctx) {
        return discv4Error::kContextCreationFailed;
    }

    secp256k1_ecdsa_recoverable_signature sig;
    const int recid = static_cast<int>(sig_data[kSigSize - 1]);
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(ctx, &sig, sig_data, recid)) {
        secp256k1_context_destroy(ctx);
        return discv4Error::kSignatureParseFailed;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ecdsa_recover(ctx, &pubkey, &sig, msg_hash.data())) {
        secp256k1_context_destroy(ctx);
        return discv4Error::kSignatureRecoveryFailed;
    }

    std::array<uint8_t, kUncompressedPubKey> pubkey_bytes{};
    size_t pubkey_len = kUncompressedPubKey;
    secp256k1_ec_pubkey_serialize(ctx, pubkey_bytes.data(), &pubkey_len,
                                  &pubkey, SECP256K1_EC_UNCOMPRESSED);
    secp256k1_context_destroy(ctx);

    // Skip the 0x04 uncompressed-point prefix byte
    NodeId node_id;
    std::copy(pubkey_bytes.begin() + kPacketTypeSize, pubkey_bytes.end(), node_id.begin());
    return node_id;
}

NodeId discv4_client::compute_node_id(const std::array<uint8_t, 64>& public_key) {
    // Node ID is keccak256(public_key)
    // But for discv4, node ID IS the public key itself (64 bytes)
    return public_key;
}

std::vector<DiscoveredPeer> discv4_client::get_peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);

    std::vector<DiscoveredPeer> result;
    result.reserve(peers_.size());

    for (const auto& [key, peer] : peers_) {
        result.push_back(peer);
    }

    return result;
}

void discv4_client::set_peer_discovered_callback(PeerDiscoveredCallback callback) {
    peer_callback_ = std::move(callback);
}

void discv4_client::set_error_callback(ErrorCallback callback) {
    error_callback_ = std::move(callback);
}

} // namespace discv4

