// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv5/discv5_crawler.hpp"
#include "discv5/discv5_enr.hpp"

#include <algorithm>
#include <sstream>
#include <iomanip>

namespace discv5
{

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

discv5_crawler::discv5_crawler(const discv5Config& config) noexcept
    : config_(config)
{
}

// ---------------------------------------------------------------------------
// add_bootstrap
// ---------------------------------------------------------------------------

void discv5_crawler::add_bootstrap(const EnrRecord& record) noexcept
{
    auto peer_result = EnrParser::to_validated_peer(record);
    if (!peer_result)
    {
        ++stat_invalid_enr_;
        return;
    }
    process_found_peers({ peer_result.value() });
}

// ---------------------------------------------------------------------------
// set_peer_discovered_callback / set_error_callback
// ---------------------------------------------------------------------------

void discv5_crawler::set_peer_discovered_callback(PeerDiscoveredCallback callback) noexcept
{
    peer_callback_ = std::move(callback);
}

void discv5_crawler::set_error_callback(ErrorCallback callback) noexcept
{
    error_callback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

VoidResult discv5_crawler::start() noexcept
{
    if (running_.exchange(true))
    {
        return discv5Error::kCrawlerAlreadyRunning;
    }

    // Seed the queue from the configured bootstrap ENR/enode URIs.
    for (const auto& uri : config_.bootstrap_enrs)
    {
        if (uri.rfind("enr:", 0U) == 0U)
        {
            enqueue_enr_uri(uri);
        }
        else if (uri.rfind("enode://", 0U) == 0U)
        {
            enqueue_enode_uri(uri);
        }
    }

    return outcome::success();
}

// ---------------------------------------------------------------------------
// stop
// ---------------------------------------------------------------------------

VoidResult discv5_crawler::stop() noexcept
{
    if (!running_.exchange(false))
    {
        return discv5Error::kCrawlerNotRunning;
    }
    return outcome::success();
}

// ---------------------------------------------------------------------------
// process_found_peers
// ---------------------------------------------------------------------------

void discv5_crawler::process_found_peers(const std::vector<ValidatedPeer>& peers) noexcept
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    for (const auto& peer : peers)
    {
        const std::string key = node_key(peer.node_id);

        // Dedup: skip if already discovered or already queued.
        if (discovered_ids_.count(key) != 0U)
        {
            ++stat_duplicates_;
            continue;
        }

        // Check if already in the queued list (linear scan — queue is bounded).
        const bool already_queued = std::any_of(
            queued_peers_.begin(), queued_peers_.end(),
            [&key](const ValidatedPeer& qp)
            {
                return node_key(qp.node_id) == key;
            });

        if (already_queued)
        {
            ++stat_duplicates_;
            continue;
        }

        queued_peers_.push_back(peer);
    }
}

// ---------------------------------------------------------------------------
// ingest_discovered_peers
// ---------------------------------------------------------------------------

void discv5_crawler::ingest_discovered_peers(const std::vector<ValidatedPeer>& peers) noexcept
{
    for (const auto& peer : peers)
    {
        bool already_discovered = false;
        bool already_queued = false;

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            const std::string key = node_key(peer.node_id);
            already_discovered = (discovered_ids_.count(key) != 0U);

            already_queued = std::any_of(
                queued_peers_.begin(), queued_peers_.end(),
                [&key](const ValidatedPeer& qp)
                {
                    return node_key(qp.node_id) == key;
                });

            if (!already_discovered && !already_queued)
            {
                queued_peers_.push_back(peer);
            }
        }

        if (already_discovered)
        {
            ++stat_duplicates_;
            continue;
        }

        emit_peer(peer);
    }
}

// ---------------------------------------------------------------------------
// stats
// ---------------------------------------------------------------------------

CrawlerStats discv5_crawler::stats() const noexcept
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    CrawlerStats s{};
    s.queued      = queued_peers_.size();
    s.measured    = measured_ids_.size();
    s.failed      = failed_ids_.size();
    s.discovered  = stat_discovered_.load();
    s.invalid_enr = stat_invalid_enr_.load();
    s.wrong_chain = stat_wrong_chain_.load();
    s.no_eth_entry = stat_no_eth_entry_.load();
    s.duplicates  = stat_duplicates_.load();
    return s;
}

// ---------------------------------------------------------------------------
// is_running
// ---------------------------------------------------------------------------

bool discv5_crawler::is_running() const noexcept
{
    return running_.load();
}

// ---------------------------------------------------------------------------
// mark_measured / mark_failed
// ---------------------------------------------------------------------------

void discv5_crawler::mark_measured(const NodeId& node_id) noexcept
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    measured_ids_.insert(node_key(node_id));
}

void discv5_crawler::mark_failed(const NodeId& node_id) noexcept
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    failed_ids_.insert(node_key(node_id));
}

// ---------------------------------------------------------------------------
// dequeue_next
// ---------------------------------------------------------------------------

std::optional<ValidatedPeer> discv5_crawler::dequeue_next() noexcept
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (queued_peers_.empty())
    {
        return std::nullopt;
    }

    ValidatedPeer peer = queued_peers_.front();
    queued_peers_.erase(queued_peers_.begin());
    return peer;
}

// ---------------------------------------------------------------------------
// is_discovered
// ---------------------------------------------------------------------------

bool discv5_crawler::is_discovered(const NodeId& node_id) const noexcept
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return discovered_ids_.count(node_key(node_id)) != 0U;
}

// ---------------------------------------------------------------------------
// Private: enqueue_enr_uri
// ---------------------------------------------------------------------------

void discv5_crawler::enqueue_enr_uri(const std::string& uri) noexcept
{
    auto record_result = EnrParser::parse(uri);
    if (!record_result)
    {
        ++stat_invalid_enr_;
        if (error_callback_)
        {
            error_callback_("Invalid ENR URI: " + uri.substr(0U, 60U));
        }
        return;
    }

    auto peer_result = EnrParser::to_validated_peer(record_result.value());
    if (!peer_result)
    {
        ++stat_invalid_enr_;
        return;
    }

    process_found_peers({ peer_result.value() });
}

// ---------------------------------------------------------------------------
// Private: enqueue_enode_uri
// ---------------------------------------------------------------------------

void discv5_crawler::enqueue_enode_uri(const std::string& uri) noexcept
{
    // Expected format: enode://<128-hex-pubkey>@<ip>:<port>
    static constexpr std::string_view kEnodePrefix = "enode://";
    static constexpr size_t           kPubkeyHexLen = 128U;

    if (uri.size() < kEnodePrefix.size() + kPubkeyHexLen + 2U)  // +2 for '@' and ':'
    {
        ++stat_invalid_enr_;
        return;
    }

    const std::string_view body(uri.data() + kEnodePrefix.size(),
                                uri.size() - kEnodePrefix.size());

    // Locate '@' separator between pubkey and host:port.
    const size_t at_pos = body.find('@');
    if (at_pos == std::string_view::npos || at_pos != kPubkeyHexLen)
    {
        ++stat_invalid_enr_;
        return;
    }

    // Decode 128-hex pubkey → 64 bytes.
    NodeId node_id{};
    const std::string_view hex_key = body.substr(0U, kPubkeyHexLen);

    static constexpr size_t kHexCharsPerByte = 2U;
    for (size_t i = 0U; i < kNodeIdBytes; ++i)
    {
        const size_t hex_offset = i * kHexCharsPerByte;
        const auto hi_char = hex_key[hex_offset];
        const auto lo_char = hex_key[hex_offset + 1U];

        auto hex_to_nibble = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
            if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(10U + (c - 'a')); }
            if (c >= 'A' && c <= 'F') { return static_cast<uint8_t>(10U + (c - 'A')); }
            return 0xFFU;
        };

        const uint8_t hi = hex_to_nibble(hi_char);
        const uint8_t lo = hex_to_nibble(lo_char);

        if (hi == 0xFFU || lo == 0xFFU)
        {
            ++stat_invalid_enr_;
            return;
        }

        // M012 — kHexNibbleBits could be named, but the 4/8 bit shifts here
        // are derived from hex encoding semantics (4 bits per nibble).
        static constexpr uint8_t kNibbleBits = 4U;
        node_id[i] = static_cast<uint8_t>((hi << kNibbleBits) | lo);
    }

    // Parse host:port from the part after '@'.
    const std::string_view host_port = body.substr(at_pos + 1U);
    const size_t colon_pos = host_port.rfind(':');
    if (colon_pos == std::string_view::npos)
    {
        ++stat_invalid_enr_;
        return;
    }

    const std::string host(host_port.substr(0U, colon_pos));
    const std::string port_str(host_port.substr(colon_pos + 1U));

    uint16_t port = 0U;
    for (const char c : port_str)
    {
        if (c < '0' || c > '9')
        {
            ++stat_invalid_enr_;
            return;
        }
        // Safe: port strings are always short; overflow cannot occur before the check.
        port = static_cast<uint16_t>(port * 10U + static_cast<uint16_t>(c - '0'));
    }

    if (port == 0U)
    {
        ++stat_invalid_enr_;
        return;
    }

    ValidatedPeer peer;
    peer.node_id  = node_id;
    peer.ip       = host;
    peer.udp_port = port;
    peer.tcp_port = port;
    peer.last_seen = std::chrono::steady_clock::now();

    process_found_peers({ peer });
}

// ---------------------------------------------------------------------------
// Private: emit_peer
// ---------------------------------------------------------------------------

void discv5_crawler::emit_peer(const ValidatedPeer& peer) noexcept
{
    // Apply fork-id filter when configured.
    if (config_.required_fork_id.has_value())
    {
        if (!peer.eth_fork_id.has_value())
        {
            ++stat_no_eth_entry_;
            return;
        }
        const ForkId& required = config_.required_fork_id.value();
        const ForkId& actual   = peer.eth_fork_id.value();
        if (required.hash != actual.hash || required.next != actual.next)
        {
            ++stat_wrong_chain_;
            return;
        }
    }

    // Dedup before emission (also checked in process_found_peers, but re-check here
    // for thread safety if emit_peer is ever called from outside process_found_peers).
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const std::string key = node_key(peer.node_id);
        if (!discovered_ids_.insert(key).second)
        {
            ++stat_duplicates_;
            return;
        }
    }

    ++stat_discovered_;

    if (peer_callback_)
    {
        peer_callback_(peer);
    }
}

// ---------------------------------------------------------------------------
// Private: node_key
// ---------------------------------------------------------------------------

std::string discv5_crawler::node_key(const NodeId& id) noexcept
{
    // Use the raw bytes as a string key — no hex conversion needed for map keys.
    return std::string(reinterpret_cast<const char*>(id.data()), id.size());
}

} // namespace discv5
