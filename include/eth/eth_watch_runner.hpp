// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_WATCH_RUNNER_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_WATCH_RUNNER_HPP

#include <eth/eth_peer_session.hpp>
#include <eth/eth_session_channel.hpp>
#include <eth/eth_watch_service.hpp>
#include <rlpx/rlpx_session.hpp>
#include <memory>
#include <string>
#include <vector>

namespace eth {

/// @brief Per-session ETH watch runner layered above RLPx and EthWatchService.
class EthWatchRunner
{
public:
    /// @brief Construct a runner for one RLPx session.
    EthWatchRunner(
        std::shared_ptr<IEthSessionChannel> channel,
        std::string                        chain_name,
        uint64_t                           network_id,
        Hash256                            genesis_hash,
        ForkId                             fork_id,
        std::vector<EthMessageSchema>      eth_message_schemas = {}) noexcept;

    EthWatchRunner(
        std::shared_ptr<rlpx::RlpxSession> session,
        std::string                        chain_name,
        uint64_t                           network_id,
        Hash256                            genesis_hash,
        ForkId                             fork_id,
        std::vector<EthMessageSchema>      eth_message_schemas = {}) noexcept;

    /// @brief Set the top-level callback for enriched filtered events.
    void set_event_callback(WatchEventNotificationCallback callback) noexcept;

    /// @brief Access the underlying watch service for subscription setup.
    [[nodiscard]] EthWatchService& service() noexcept;

    /// @brief Send local ETH Status for the negotiated protocol version.
    [[nodiscard]] bool send_local_status() noexcept;

    /// @brief Install the session ETH message bridge into the underlying RLPx session.
    void install_session_bridge() noexcept;

    /// @brief Register a watch and emit enriched notifications through the runner callback.
    EventWatchId watch_event(
        const codec::Address&             contract_address,
        const std::string&                event_signature,
        const std::vector<abi::AbiParam>& params,
        std::optional<uint64_t>           from_block = std::nullopt,
        std::optional<uint64_t>           to_block   = std::nullopt) noexcept;

    /// @brief Access the enriched event context.
    [[nodiscard]] const WatchEventContext& context() const noexcept;

private:
    void notify_event(
        const std::string&                 event_signature,
        const MatchedEvent&                event,
        const std::vector<abi::AbiValue>&  values) noexcept;

    std::shared_ptr<rlpx::RlpxSession> session_;
    std::shared_ptr<IEthSessionChannel> channel_;
    std::string                        chain_name_;
    uint64_t                           network_id_ = 0;
    Hash256                            genesis_hash_{};
    ForkId                             fork_id_{};
    std::vector<EthMessageSchema>      eth_message_schemas_;
    EthWatchService                    watch_service_{};
    WatchEventNotificationCallback     event_callback_{};
};

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_ETH_WATCH_RUNNER_HPP
