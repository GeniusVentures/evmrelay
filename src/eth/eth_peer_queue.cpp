// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_peer_queue.hpp>

#include <algorithm>

namespace eth
{

EthPeerQueue::EthPeerQueue(std::shared_ptr<discv4::DialScheduler> scheduler) noexcept
    : scheduler_(std::move(scheduler))
{
}

void EthPeerQueue::preload_cached_peers(const std::vector<discv4::ValidatedPeer>& peers) noexcept
{
    if (!scheduler_)
    {
        return;
    }

    for (const auto& peer : peers)
    {
        ++cached_peer_count_;
        scheduler_->enqueue(peer);
    }
}

void EthPeerQueue::set_discovery_bootnodes(std::vector<discv4::ValidatedPeer> bootnodes) noexcept
{
    discovery_bootnodes_ = std::move(bootnodes);
}

void EthPeerQueue::enqueue_discovered_peer(const discv4::DiscoveredPeer& peer) noexcept
{
    if (!scheduler_)
    {
        return;
    }

    discv4::ValidatedPeer candidate{};
    candidate.peer = peer;
    std::copy(peer.node_id.begin(), peer.node_id.end(), candidate.pubkey.begin());
    ++discovered_peer_count_;
    scheduler_->enqueue(std::move(candidate));
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

std::shared_ptr<discv4::DialScheduler> EthPeerQueue::scheduler() const noexcept
{
    return scheduler_;
}

std::shared_ptr<EthPeerQueue> make_eth_peer_queue(
    std::shared_ptr<discv4::DialScheduler> scheduler,
    const discv4::ChainPeerConfig&         chain_config)
{
    auto queue = std::make_shared<EthPeerQueue>(std::move(scheduler));
    queue->set_discovery_bootnodes(chain_config.bootnodes);
    queue->preload_cached_peers(chain_config.nodes);
    return queue;
}

} // namespace eth
