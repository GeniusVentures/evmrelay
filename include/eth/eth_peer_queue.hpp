// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_PEER_QUEUE_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_PEER_QUEUE_HPP

#include <discv4/chain_peers.hpp>
#include <discv4/dial_scheduler.hpp>
#include <rlpx/rlpx_types.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eth
{

/// @brief Bounded peer producer queue policy.
struct EthPeerQueueConfig
{
    /// @brief Maximum peers waiting in the scheduler queue.
    ///
    /// Active dial attempts do not count against this limit. When the pending
    /// queue is full, newly produced peers are dropped deterministically.
    size_t max_pending_peers = 1024;

    /// @brief Maximum requeues after disconnect feedback before a peer is considered flaky.
    size_t max_disconnect_requeues = 3;
};

/// @brief Feedback from a completed or disconnected peer session.
struct EthPeerDisconnectFeedback
{
    discv4::ValidatedPeer  peer{};
    rlpx::DisconnectReason reason = rlpx::DisconnectReason::kRequested;
    bool                   was_connected = false;
};

/// @brief Producer/consumer boundary for eth-watch peer candidates.
///
/// Cached chain peers and live discovery both feed this queue. Discovery seeds
/// are retained separately and are never consumed as direct RLPx/ETH peers.
class EthPeerQueue
{
public:
    explicit EthPeerQueue(
        std::shared_ptr<discv4::DialScheduler> scheduler,
        EthPeerQueueConfig                     config = {}) noexcept;
    ~EthPeerQueue();

    /// @brief Enqueue pre-cached RLPx/ETH peer candidates from chain_enodes.json.nodes.
    void preload_cached_peers(const std::vector<discv4::ValidatedPeer>& peers) noexcept;

    /// @brief Store discovery-only seed nodes from chain_enodes.json.bootnodes.
    void set_discovery_bootnodes(std::vector<discv4::ValidatedPeer> bootnodes) noexcept;

    /// @brief Enqueue a live peer produced by discovery.
    [[nodiscard]] bool enqueue_discovered_peer(const discv4::DiscoveredPeer& peer) noexcept;

    /// @brief Requeue eligible disconnected peers without letting flaky nodes cycle forever.
    [[nodiscard]] bool report_peer_disconnected(const EthPeerDisconnectFeedback& feedback) noexcept;

    [[nodiscard]] const std::vector<discv4::ValidatedPeer>& discovery_bootnodes() const noexcept;
    [[nodiscard]] bool needs_discovery() const noexcept;
    [[nodiscard]] size_t cached_peer_count() const noexcept;
    [[nodiscard]] size_t discovered_peer_count() const noexcept;
    [[nodiscard]] size_t requeued_peer_count() const noexcept;
    [[nodiscard]] size_t duplicate_peer_drop_count() const noexcept;
    [[nodiscard]] size_t capacity_drop_count() const noexcept;
    [[nodiscard]] size_t flaky_peer_drop_count() const noexcept;
    [[nodiscard]] std::shared_ptr<discv4::DialScheduler> scheduler() const noexcept;

private:
    [[nodiscard]] bool enqueue_candidate(discv4::ValidatedPeer peer, bool allow_known_peer) noexcept;
    [[nodiscard]] static std::string node_key(const discv4::NodeId& node_id);
    [[nodiscard]] static bool is_requeueable_disconnect(const EthPeerDisconnectFeedback& feedback) noexcept;

    std::shared_ptr<discv4::DialScheduler> scheduler_;
    EthPeerQueueConfig                      config_{};
    std::vector<discv4::ValidatedPeer>     discovery_bootnodes_;
    std::unordered_set<std::string>         seen_node_ids_;
    std::unordered_map<std::string, size_t> disconnect_counts_;
    size_t                                 cached_peer_count_ = 0;
    size_t                                 discovered_peer_count_ = 0;
    size_t                                 requeued_peer_count_ = 0;
    size_t                                 duplicate_peer_drop_count_ = 0;
    size_t                                 capacity_drop_count_ = 0;
    size_t                                 flaky_peer_drop_count_ = 0;
};

/// @brief Create an eth-watch peer queue and preload the chain cache split.
[[nodiscard]] std::shared_ptr<EthPeerQueue> make_eth_peer_queue(
    std::shared_ptr<discv4::DialScheduler> scheduler,
    const discv4::ChainPeerConfig&         chain_config,
    EthPeerQueueConfig                     config = {},
    bool                                   preload_cached_peers = true);

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_ETH_PEER_QUEUE_HPP
