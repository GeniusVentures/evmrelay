// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_peer_queue.hpp>

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
    for (const auto& peer : peers)
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
    if (!is_requeueable_disconnect(feedback))
    {
        return false;
    }

    const auto key = node_key(feedback.peer.peer.node_id);
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
    case rlpx::DisconnectReason::kTooManyPeers:
        return true;
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
