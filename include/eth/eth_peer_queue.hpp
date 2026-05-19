// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_PEER_QUEUE_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_PEER_QUEUE_HPP

#include <discv4/chain_peers.hpp>
#include <discv4/dial_scheduler.hpp>

#include <memory>
#include <vector>

namespace eth
{

/// @brief Producer/consumer boundary for eth-watch peer candidates.
///
/// Cached chain peers and live discovery both feed this queue. Discovery seeds
/// are retained separately and are never consumed as direct RLPx/ETH peers.
class EthPeerQueue
{
public:
    explicit EthPeerQueue(std::shared_ptr<discv4::DialScheduler> scheduler) noexcept;

    /// @brief Enqueue pre-cached RLPx/ETH peer candidates from chain_enodes.json.nodes.
    void preload_cached_peers(const std::vector<discv4::ValidatedPeer>& peers) noexcept;

    /// @brief Store discovery-only seed nodes from chain_enodes.json.bootnodes.
    void set_discovery_bootnodes(std::vector<discv4::ValidatedPeer> bootnodes) noexcept;

    /// @brief Enqueue a live peer produced by discovery.
    void enqueue_discovered_peer(const discv4::DiscoveredPeer& peer) noexcept;

    [[nodiscard]] const std::vector<discv4::ValidatedPeer>& discovery_bootnodes() const noexcept;
    [[nodiscard]] bool needs_discovery() const noexcept;
    [[nodiscard]] size_t cached_peer_count() const noexcept;
    [[nodiscard]] size_t discovered_peer_count() const noexcept;
    [[nodiscard]] std::shared_ptr<discv4::DialScheduler> scheduler() const noexcept;

private:
    std::shared_ptr<discv4::DialScheduler> scheduler_;
    std::vector<discv4::ValidatedPeer>     discovery_bootnodes_;
    size_t                                 cached_peer_count_ = 0;
    size_t                                 discovered_peer_count_ = 0;
};

/// @brief Create an eth-watch peer queue and preload the chain cache split.
[[nodiscard]] std::shared_ptr<EthPeerQueue> make_eth_peer_queue(
    std::shared_ptr<discv4::DialScheduler> scheduler,
    const discv4::ChainPeerConfig&         chain_config);

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_ETH_PEER_QUEUE_HPP
