// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_WATCH_DIALER_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_WATCH_DIALER_HPP

#include <boost/asio/io_context.hpp>
#include <discv4/dial_scheduler.hpp>
#include <eth/eth_watch_service.hpp>

#include <memory>
#include <vector>

namespace eth
{

/// @brief Create the shared watcher connection pool used by chain peer dialers.
/// @param config Pool limits.
/// @return Shared watcher pool.
[[nodiscard]] std::shared_ptr<discv4::WatcherPool> make_eth_watcher_pool(
    const EthWatchConnectionConfig& config);

/// @brief Create a per-chain dial scheduler and enqueue the provided peer candidates.
/// @param io Boost.Asio context used by the scheduler.
/// @param pool Shared watcher pool.
/// @param dial_fn Callback used for each dial attempt.
/// @param peers Validated RLPx/ETH peer candidates from `chain_enodes.json.nodes`.
/// @return Scheduler that owns the dial queue and active sessions.
[[nodiscard]] std::shared_ptr<discv4::DialScheduler> start_eth_watch_chain_peer_dialing(
    boost::asio::io_context&                  io,
    std::shared_ptr<discv4::WatcherPool>      pool,
    discv4::DialFn                            dial_fn,
    const std::vector<discv4::ValidatedPeer>& peers);

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_ETH_WATCH_DIALER_HPP
