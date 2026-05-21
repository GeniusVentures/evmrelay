// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_peer_queue.hpp>

#include <base/rlp-logger.hpp>

#include <algorithm>
#include <string_view>

namespace eth
{

EthPeerQueue::EthPeerQueue(
    std::shared_ptr<discv4::DialScheduler> scheduler,
    EthPeerQueueConfig                     config) noexcept
    : scheduler_(std::move(scheduler))
    , config_(config)
{
    if (scheduler_)
    {
        scheduler_->feedback_fn =
            [this](const discv4::ValidatedPeer& peer, rlpx::DisconnectReason reason, bool was_connected)
            {
                (void)report_peer_disconnected(EthPeerDisconnectFeedback{peer, reason, was_connected});
            };
    }
}

EthPeerQueue::~EthPeerQueue()
{
    if (scheduler_)
    {
        scheduler_->feedback_fn = nullptr;
    }
}

void EthPeerQueue::preload_cached_peers(const std::vector<discv4::ValidatedPeer>& peers) noexcept
{
    const auto rotated_peers = rotate_cached_peers(peers);
    const auto spread_peers = spread_cached_peers_for_dial_slots(rotated_peers);
    for (const auto& peer : spread_peers)
    {
        if (enqueue_candidate(peer, false))
        {
            ++cached_peer_count_;
        }
    }
}

void EthPeerQueue::set_discovery_bootnodes(std::vector<discv4::ValidatedPeer> bootnodes) noexcept
{
    discovery_bootnodes_ = std::move(bootnodes);
}

bool EthPeerQueue::enqueue_discovered_peer(const discv4::DiscoveredPeer& peer) noexcept
{
    discv4::ValidatedPeer candidate{};
    candidate.peer = peer;
    std::copy(peer.node_id.begin(), peer.node_id.end(), candidate.pubkey.begin());
    if (!enqueue_candidate(std::move(candidate), false))
    {
        return false;
    }
    ++discovered_peer_count_;
    return true;
}

bool EthPeerQueue::enqueue_validated_discovery_peer(const discovery::ValidatedPeer& peer) noexcept
{
    discv4::ValidatedPeer candidate{};
    candidate.peer.node_id = peer.node_id;
    candidate.peer.ip = peer.ip;
    candidate.peer.udp_port = peer.udp_port;
    candidate.peer.tcp_port = peer.tcp_port;
    candidate.peer.last_seen = peer.last_seen;
    if (peer.eth_fork_id.has_value())
    {
        discv4::ForkId fork_id{};
        fork_id.hash = peer.eth_fork_id->hash;
        fork_id.next = peer.eth_fork_id->next;
        candidate.peer.eth_fork_id = fork_id;
    }
    std::copy(peer.node_id.begin(), peer.node_id.end(), candidate.pubkey.begin());
    if (!enqueue_candidate(std::move(candidate), false))
    {
        return false;
    }
    ++discovered_peer_count_;
    return true;
}

bool EthPeerQueue::report_peer_disconnected(const EthPeerDisconnectFeedback& feedback) noexcept
{
    const auto key = node_key(feedback.peer.peer.node_id);
    ++disconnect_feedback_count_;
    if (feedback.was_connected)
    {
        ++disconnected_after_connected_count_;
    }
    else
    {
        ++disconnected_before_connected_count_;
    }

    switch (feedback.reason)
    {
    case rlpx::DisconnectReason::kTooManyPeers:
        if (feedback.was_connected)
        {
            ++too_many_peers_after_connected_count_;
        }
        else
        {
            ++too_many_peers_before_connected_count_;
        }
        break;
    case rlpx::DisconnectReason::kTcpError:
        ++tcp_failure_count_;
        break;
    case rlpx::DisconnectReason::kTimeout:
        ++timeout_count_;
        break;
    case rlpx::DisconnectReason::kSubprotocolError:
        ++subprotocol_error_count_;
        break;
    default:
        break;
    }

    if (feedback.reason == rlpx::DisconnectReason::kTooManyPeers)
    {
        static auto log = rlp::base::createLogger("eth_peer_queue");
        backoff_until_[key] = std::chrono::steady_clock::now() + config_.too_many_peers_backoff;
        ++too_many_peers_backoff_count_;
        log->debug("Backing off peer {}:{} after TooManyPeers disconnect",
                   feedback.peer.peer.ip,
                   feedback.peer.peer.tcp_port);
        return false;
    }

    if (!is_requeueable_disconnect(feedback))
    {
        return false;
    }

    auto& disconnect_count = disconnect_counts_[key];
    if (disconnect_count >= config_.max_disconnect_requeues)
    {
        ++flaky_peer_drop_count_;
        return false;
    }

    ++disconnect_count;
    if (!enqueue_candidate(feedback.peer, true))
    {
        return false;
    }

    ++requeued_peer_count_;
    return true;
}

const std::vector<discv4::ValidatedPeer>& EthPeerQueue::discovery_bootnodes() const noexcept
{
    return discovery_bootnodes_;
}

bool EthPeerQueue::needs_discovery() const noexcept
{
    return cached_peer_count_ == 0 && !discovery_bootnodes_.empty();
}

size_t EthPeerQueue::cached_peer_count() const noexcept
{
    return cached_peer_count_;
}

size_t EthPeerQueue::discovered_peer_count() const noexcept
{
    return discovered_peer_count_;
}

size_t EthPeerQueue::requeued_peer_count() const noexcept
{
    return requeued_peer_count_;
}

size_t EthPeerQueue::duplicate_peer_drop_count() const noexcept
{
    return duplicate_peer_drop_count_;
}

size_t EthPeerQueue::capacity_drop_count() const noexcept
{
    return capacity_drop_count_;
}

size_t EthPeerQueue::flaky_peer_drop_count() const noexcept
{
    return flaky_peer_drop_count_;
}

size_t EthPeerQueue::too_many_peers_backoff_count() const noexcept
{
    return too_many_peers_backoff_count_;
}

size_t EthPeerQueue::backoff_drop_count() const noexcept
{
    return backoff_drop_count_;
}

EthPeerQueueStatsSnapshot EthPeerQueue::stats() const noexcept
{
    EthPeerQueueStatsSnapshot snapshot{};
    snapshot.cached_peer_count = cached_peer_count_;
    snapshot.discovered_peer_count = discovered_peer_count_;
    snapshot.requeued_peer_count = requeued_peer_count_;
    snapshot.duplicate_peer_drop_count = duplicate_peer_drop_count_;
    snapshot.capacity_drop_count = capacity_drop_count_;
    snapshot.flaky_peer_drop_count = flaky_peer_drop_count_;
    snapshot.too_many_peers_backoff_count = too_many_peers_backoff_count_;
    snapshot.backoff_drop_count = backoff_drop_count_;
    snapshot.disconnect_feedback_count = disconnect_feedback_count_;
    snapshot.disconnected_before_connected_count = disconnected_before_connected_count_;
    snapshot.disconnected_after_connected_count = disconnected_after_connected_count_;
    snapshot.too_many_peers_before_connected_count = too_many_peers_before_connected_count_;
    snapshot.too_many_peers_after_connected_count = too_many_peers_after_connected_count_;
    snapshot.tcp_failure_count = tcp_failure_count_;
    snapshot.timeout_count = timeout_count_;
    snapshot.subprotocol_error_count = subprotocol_error_count_;
    return snapshot;
}

std::shared_ptr<discv4::DialScheduler> EthPeerQueue::scheduler() const noexcept
{
    return scheduler_;
}

bool EthPeerQueue::enqueue_candidate(discv4::ValidatedPeer peer, bool allow_known_peer) noexcept
{
    if (!scheduler_)
    {
        return false;
    }

    const auto key = node_key(peer.peer.node_id);
    const auto now = std::chrono::steady_clock::now();
    if (const auto backoff = backoff_until_.find(key);
        backoff != backoff_until_.end())
    {
        if (now < backoff->second)
        {
            ++backoff_drop_count_;
            return false;
        }
        backoff_until_.erase(backoff);
    }

    const auto insert_result = seen_node_ids_.insert(key);
    if (!insert_result.second && !allow_known_peer)
    {
        ++duplicate_peer_drop_count_;
        return false;
    }

    const bool can_start_immediately =
        scheduler_->active < scheduler_->pool->max_per_chain &&
        scheduler_->pool->active_total.load() < scheduler_->pool->max_total;
    if (!can_start_immediately && scheduler_->queue.size() >= config_.max_pending_peers)
    {
        if (insert_result.second)
        {
            seen_node_ids_.erase(key);
        }
        ++capacity_drop_count_;
        return false;
    }

    scheduler_->enqueue(std::move(peer));
    return true;
}

std::vector<discv4::ValidatedPeer> EthPeerQueue::rotate_cached_peers(
    const std::vector<discv4::ValidatedPeer>& peers) const
{
    if (peers.size() <= 1U || config_.cache_peer_start_offset == 0U)
    {
        return peers;
    }

    const size_t offset = config_.cache_peer_start_offset % peers.size();
    if (offset == 0U)
    {
        return peers;
    }

    std::vector<discv4::ValidatedPeer> rotated;
    rotated.reserve(peers.size());
    rotated.insert(rotated.end(), peers.begin() + static_cast<std::ptrdiff_t>(offset), peers.end());
    rotated.insert(rotated.end(), peers.begin(), peers.begin() + static_cast<std::ptrdiff_t>(offset));
    return rotated;
}

std::vector<discv4::ValidatedPeer> EthPeerQueue::spread_cached_peers_for_dial_slots(
    const std::vector<discv4::ValidatedPeer>& peers) const
{
    if (!scheduler_ || !scheduler_->pool || peers.size() <= 1U)
    {
        return peers;
    }

    const auto configured_slots = scheduler_->pool->max_per_chain;
    if (configured_slots <= 1)
    {
        return peers;
    }

    const auto band_count = std::min(
        peers.size(),
        static_cast<size_t>(configured_slots));
    if (band_count <= 1U)
    {
        return peers;
    }

    std::vector<discv4::ValidatedPeer> spread;
    spread.reserve(peers.size());

    for (size_t offset = 0U; spread.size() < peers.size(); ++offset)
    {
        for (size_t band = 0U; band < band_count; ++band)
        {
            const size_t begin = (peers.size() * band) / band_count;
            const size_t end = (peers.size() * (band + 1U)) / band_count;
            const size_t index = begin + offset;
            if (index < end)
            {
                spread.push_back(peers[index]);
            }
        }
    }

    return spread;
}

std::string EthPeerQueue::node_key(const discv4::NodeId& node_id)
{
    static constexpr std::string_view kHexDigits = "0123456789abcdef";
    std::string key;
    key.reserve(node_id.size() * 2U);
    for (const auto byte : node_id)
    {
        key.push_back(kHexDigits[(byte >> 4U) & 0x0fU]);
        key.push_back(kHexDigits[byte & 0x0fU]);
    }
    return key;
}

bool EthPeerQueue::is_requeueable_disconnect(const EthPeerDisconnectFeedback& feedback) noexcept
{
    switch (feedback.reason)
    {
    case rlpx::DisconnectReason::kTcpError:
    case rlpx::DisconnectReason::kTimeout:
        return feedback.was_connected;
    default:
        return false;
    }
}

std::shared_ptr<EthPeerQueue> make_eth_peer_queue(
    std::shared_ptr<discv4::DialScheduler> scheduler,
    const discv4::ChainPeerConfig&         chain_config,
    EthPeerQueueConfig                     config,
    bool                                   preload_cached_peers)
{
    auto queue = std::make_shared<EthPeerQueue>(std::move(scheduler), config);
    queue->set_discovery_bootnodes(chain_config.bootnodes);
    if (preload_cached_peers)
    {
        queue->preload_cached_peers(chain_config.nodes);
    }
    return queue;
}

} // namespace eth
