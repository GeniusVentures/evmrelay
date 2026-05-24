// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_watch_service.hpp>
#include <eth/eth_handshake.hpp>
#include <eth/eth_watch_dialer.hpp>
#include <eth/eth_watch_runner.hpp>
#include <eth/messages.hpp>
#include <base/rlp-logger.hpp>
#include <discv5/discv5_enr.hpp>
#include <discv5/enr_tree.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/rlpx_error.hpp>
#include <rlpx/rlpx_session.hpp>

#include <algorithm>
#include <memory>
#include <optional>

namespace eth {

namespace
{

bool allows_enr_tree_discovery(const discv4::ChainPeerConfig& chain) noexcept
{
    return chain.discovery_default == discv4::ChainDiscoveryDefault::kAuto
        || chain.discovery_default == discv4::ChainDiscoveryDefault::kEnrTree;
}

bool allows_cache_enr_discv5_discovery(const discv4::ChainPeerConfig& chain) noexcept
{
    return chain.discovery_default == discv4::ChainDiscoveryDefault::kAuto
        || chain.discovery_default == discv4::ChainDiscoveryDefault::kCacheEnrDiscv5;
}

bool allows_discv4_discovery(const discv4::ChainPeerConfig& chain) noexcept
{
    return chain.discovery_default == discv4::ChainDiscoveryDefault::kAuto
        || chain.discovery_default == discv4::ChainDiscoveryDefault::kDiscv4
        || chain.discovery_default == discv4::ChainDiscoveryDefault::kEnrTree;
}

bool chain_config_matches_discovery_mode(
    const discv4::ChainPeerConfig& chain,
    EthWatchDiscoveryMode          mode,
    bool                           enable_discv4_fallback,
    bool                           enable_enr_tree_discovery) noexcept
{
    const bool has_cached_nodes = !chain.nodes.empty();
    const bool has_bootnodes = !chain.bootnodes.empty();
    const bool has_enr_tree = enable_enr_tree_discovery
        && allows_enr_tree_discovery(chain)
        && !chain.enr_trees.empty();
    const bool has_discv5_bootnodes = enable_enr_tree_discovery
        && allows_cache_enr_discv5_discovery(chain)
        && !chain.discv5_bootnodes.empty();
    const bool has_discovery_source =
        has_enr_tree
        || has_discv5_bootnodes
        || (enable_discv4_fallback && allows_discv4_discovery(chain) && has_bootnodes);

    switch (mode)
    {
    case EthWatchDiscoveryMode::kCacheOnly:
        return has_cached_nodes;
    case EthWatchDiscoveryMode::kDiscoverFirst:
        return has_discovery_source;
    case EthWatchDiscoveryMode::kDiscoverIfNeeded:
    case EthWatchDiscoveryMode::kHybrid:
        return has_cached_nodes || has_discovery_source;
    }

    return false;
}

bool should_preload_cached_peers(EthWatchDiscoveryMode mode) noexcept
{
    return mode != EthWatchDiscoveryMode::kDiscoverFirst;
}

bool should_start_discv4_discovery(
    const EthPeerQueue&            queue,
    const discv4::ChainPeerConfig& chain,
    EthWatchDiscoveryMode          mode,
    bool                           enable_discv4_fallback) noexcept
{
    if (!enable_discv4_fallback ||
        !allows_discv4_discovery(chain) ||
        queue.discovery_bootnodes().empty())
    {
        return false;
    }

    switch (mode)
    {
    case EthWatchDiscoveryMode::kCacheOnly:
        return false;
    case EthWatchDiscoveryMode::kDiscoverIfNeeded:
        return queue.needs_discovery();
    case EthWatchDiscoveryMode::kDiscoverFirst:
    case EthWatchDiscoveryMode::kHybrid:
        return true;
    }

    return false;
}

std::vector<std::string> configured_enr_tree_urls(
    const discv4::ChainPeerConfig& chain) noexcept
{
    return chain.enr_trees;
}

bool should_start_enr_tree_discovery(
    const EthPeerQueue&            queue,
    const discv4::ChainPeerConfig& chain,
    EthWatchDiscoveryMode          mode,
    bool                           enable_enr_tree_discovery) noexcept
{
    if (!enable_enr_tree_discovery ||
        !allows_enr_tree_discovery(chain) ||
        configured_enr_tree_urls(chain).empty())
    {
        return false;
    }

    switch (mode)
    {
    case EthWatchDiscoveryMode::kCacheOnly:
        return false;
    case EthWatchDiscoveryMode::kDiscoverIfNeeded:
        return queue.cached_peer_count() == 0U;
    case EthWatchDiscoveryMode::kDiscoverFirst:
    case EthWatchDiscoveryMode::kHybrid:
        return true;
    }

    return false;
}

bool should_start_cache_enr_discovery(
    const EthPeerQueue&            queue,
    const discv4::ChainPeerConfig& chain,
    EthWatchDiscoveryMode          mode,
    bool                           enable_enr_tree_discovery) noexcept
{
    if (!enable_enr_tree_discovery ||
        !allows_cache_enr_discv5_discovery(chain) ||
        chain.discv5_bootnodes.empty())
    {
        return false;
    }

    switch (mode)
    {
    case EthWatchDiscoveryMode::kCacheOnly:
        return false;
    case EthWatchDiscoveryMode::kDiscoverIfNeeded:
        return queue.cached_peer_count() == 0U;
    case EthWatchDiscoveryMode::kDiscoverFirst:
    case EthWatchDiscoveryMode::kHybrid:
        return true;
    }

    return false;
}

discv4::FilterFn make_optional_fork_id_filter(
    const std::array<uint8_t, 4U>& expected_hash) noexcept
{
    return [expected_hash](const discv4::DiscoveredPeer& peer) noexcept
    {
        if (!peer.eth_fork_id.has_value())
        {
            return true;
        }
        return peer.eth_fork_id->hash == expected_hash;
    };
}

bool direct_enr_tree_peer_allowed(
    const discv4::ChainPeerConfig& chain,
    const discovery::ValidatedPeer& peer) noexcept
{
    if (!chain.fork_id.has_value()
        || chain.discovery_fork_filter == discv4::DiscoveryForkFilter::kDisabled)
    {
        return true;
    }
    if (!peer.eth_fork_id.has_value())
    {
        return false;
    }
    return peer.eth_fork_id->hash == chain.fork_id->fork_hash
        && peer.eth_fork_id->next == chain.fork_id->next_fork;
}

size_t enqueue_resolved_enr_tree_peers(
    const discv4::ChainPeerConfig& chain,
    const std::shared_ptr<EthPeerQueue>& queue,
    const std::vector<std::string>& enrs) noexcept
{
    if (!queue)
    {
        return 0U;
    }

    size_t enqueued = 0U;
    for (const auto& enr : enrs)
    {
        if (enr.rfind("enr:", 0U) != 0U)
        {
            continue;
        }
        const auto record = discv5::EnrParser::parse(enr);
        if (!record)
        {
            continue;
        }
        const auto peer = discv5::EnrParser::to_validated_peer(record.value());
        if (!peer || !direct_enr_tree_peer_allowed(chain, peer.value()))
        {
            continue;
        }
        if (queue->enqueue_validated_discovery_peer(peer.value()))
        {
            ++enqueued;
        }
    }
    return enqueued;
}

} // namespace

EthWatchService::~EthWatchService()
{
    stop();
}

// ---------------------------------------------------------------------------
// make_eth_watcher_pool
// ---------------------------------------------------------------------------

std::shared_ptr<discv4::WatcherPool> make_eth_watcher_pool(
    const EthWatchConnectionConfig& config)
{
    return std::make_shared<discv4::WatcherPool>(
        discv4::WatcherPoolConfig{
            config.max_total_connections,
            config.max_connections_per_chain
        });
}

// ---------------------------------------------------------------------------
// start_eth_watch_chain_peer_dialing
// ---------------------------------------------------------------------------

std::shared_ptr<discv4::DialScheduler> start_eth_watch_chain_peer_dialing(
    boost::asio::io_context&                  io,
    std::shared_ptr<discv4::WatcherPool>      pool,
    discv4::DialFn                            dial_fn,
    const std::vector<discv4::ValidatedPeer>& peers)
{
    auto scheduler = std::make_shared<discv4::DialScheduler>(
        io,
        std::move(pool),
        std::move(dial_fn));

    for (const auto& peer : peers)
    {
        scheduler->enqueue(peer);
    }

    return scheduler;
}

// ---------------------------------------------------------------------------
// initialize / run / stop
// ---------------------------------------------------------------------------

bool EthWatchService::initialize(
    EthWatchServiceConfig          config,
    WatchEventNotificationCallback callback) noexcept
{
    stop();
    orchestration_initialized_ = false;
    orchestration_config_ = {};
    orchestration_callback_ = {};

    if (config.chains.empty())
    {
        return false;
    }

    for (const auto& chain : config.chains)
    {
        if (chain.canonical_name.empty())
        {
            return false;
        }
        if (!chain_config_matches_discovery_mode(
                chain,
                config.discovery_mode,
                config.enable_discv4_fallback,
                config.enable_enr_tree_discovery))
        {
            return false;
        }
    }

    orchestration_config_ = std::move(config);
    orchestration_callback_ = std::move(callback);
    orchestration_initialized_ = true;
    return true;
}

void EthWatchService::run(boost::asio::io_context& io) noexcept
{
    if (!orchestration_initialized_ || orchestration_running_)
    {
        return;
    }

    runtime_chains_.clear();
    active_runners_.clear();
    runtime_stats_by_chain_.clear();
    auto pool = make_eth_watcher_pool(orchestration_config_.connection);

    for (const auto& chain_config : orchestration_config_.chains)
    {
        RuntimeChain runtime{};
        runtime.config = chain_config;
        runtime.stats = std::make_shared<EthWatchRuntimeStatsSnapshot>();
        runtime_stats_by_chain_[runtime.config.canonical_name] = runtime.stats;

        if (orchestration_config_.attach_peer_dialer)
        {
            auto dial_fn = orchestration_config_.dial_fn_factory
                ? orchestration_config_.dial_fn_factory(chain_config)
                : make_default_dial_fn(chain_config);

            runtime.scheduler = std::make_shared<discv4::DialScheduler>(
                io,
                pool,
                std::move(dial_fn));

            if (chain_config.fork_id.has_value()
                && chain_config.discovery_fork_filter == discv4::DiscoveryForkFilter::kRequire)
            {
                runtime.scheduler->filter_fn = make_optional_fork_id_filter(
                    chain_config.fork_id->fork_hash);
            }
        }

        runtime.peer_queue = make_eth_peer_queue(
            runtime.scheduler,
            runtime.config,
            orchestration_config_.peer_queue,
            should_preload_cached_peers(orchestration_config_.discovery_mode));

        if (runtime.peer_queue
            && should_start_enr_tree_discovery(
                *runtime.peer_queue,
                runtime.config,
                orchestration_config_.discovery_mode,
                orchestration_config_.enable_enr_tree_discovery))
        {
            runtime.discv5_enr_tree_started = start_enr_tree_discovery(io, runtime);
        }

        if (runtime.peer_queue
            && !runtime.discv5_enr_tree_started
            && should_start_cache_enr_discovery(
                *runtime.peer_queue,
                runtime.config,
                orchestration_config_.discovery_mode,
                orchestration_config_.enable_enr_tree_discovery))
        {
            runtime.discv5_cache_enr_started =
                start_discv5_discovery(io, runtime, runtime.config.discv5_bootnodes);
        }

        if (runtime.peer_queue
            && !runtime.discv5_enr_tree_started
            && !runtime.discv5_cache_enr_started
            && should_start_discv4_discovery(
                *runtime.peer_queue,
                runtime.config,
                orchestration_config_.discovery_mode,
                orchestration_config_.enable_discv4_fallback))
        {
            start_discv4_fallback(io, runtime);
        }

        runtime_chains_.push_back(std::move(runtime));
    }

    orchestration_running_ = true;
}

void EthWatchService::stop() noexcept
{
    for (auto& runtime : runtime_chains_)
    {
        if (runtime.scheduler)
        {
            runtime.scheduler->stop();
        }
        if (runtime.discovery_client)
        {
            runtime.discovery_client->stop();
        }
        if (runtime.discv5_client)
        {
            runtime.discv5_client->stop();
        }
    }
    active_runners_.clear();
    runtime_chains_.clear();
    runtime_stats_by_chain_.clear();
    orchestration_running_ = false;
}

bool EthWatchService::initialized() const noexcept
{
    return orchestration_initialized_;
}

size_t EthWatchService::runtime_chain_count() const noexcept
{
    return runtime_chains_.size();
}

size_t EthWatchService::scheduler_count() const noexcept
{
    return std::count_if(runtime_chains_.begin(), runtime_chains_.end(),
        [](const RuntimeChain& runtime) { return runtime.scheduler != nullptr; });
}

size_t EthWatchService::peer_queue_count() const noexcept
{
    return std::count_if(runtime_chains_.begin(), runtime_chains_.end(),
        [](const RuntimeChain& runtime) { return runtime.peer_queue != nullptr; });
}

size_t EthWatchService::discovery_client_count() const noexcept
{
    return std::count_if(runtime_chains_.begin(), runtime_chains_.end(),
        [](const RuntimeChain& runtime) { return runtime.discovery_client != nullptr; });
}

size_t EthWatchService::discv4_fallback_count() const noexcept
{
    return std::count_if(runtime_chains_.begin(), runtime_chains_.end(),
        [](const RuntimeChain& runtime) { return runtime.discv4_fallback_started; });
}

size_t EthWatchService::active_runner_count() const noexcept
{
    return active_runners_.size();
}

WatchStatsSnapshot EthWatchService::aggregate_runtime_stats() const noexcept
{
    WatchStatsSnapshot aggregate{};
    for (const auto& runner : active_runners_)
    {
        if (!runner)
        {
            continue;
        }

        const auto stats = runner->service().stats();
        aggregate.eth_messages_seen += stats.eth_messages_seen;
        aggregate.new_block_hashes_messages += stats.new_block_hashes_messages;
        aggregate.new_block_messages += stats.new_block_messages;
        aggregate.receipts_messages += stats.receipts_messages;
        aggregate.decode_failures += stats.decode_failures;
        aggregate.receipts_requested += stats.receipts_requested;
        aggregate.receipts_processed += stats.receipts_processed;
        aggregate.logs_seen += stats.logs_seen;
        aggregate.matched_logs += stats.matched_logs;
        aggregate.discarded_logs += stats.discarded_logs;
    }
    return aggregate;
}

EthWatchRuntimeStatsSnapshot EthWatchService::aggregate_connection_stats() const noexcept
{
    EthWatchRuntimeStatsSnapshot aggregate{};
    for (const auto& runtime : runtime_chains_)
    {
        if (runtime.stats)
        {
            aggregate.tcp_connect_failures += runtime.stats->tcp_connect_failures;
            aggregate.tcp_connected += runtime.stats->tcp_connected;
            aggregate.auth_success += runtime.stats->auth_success;
            aggregate.local_hello_sent += runtime.stats->local_hello_sent;
            aggregate.peer_disconnect_before_hello += runtime.stats->peer_disconnect_before_hello;
            aggregate.peer_hello_accepted += runtime.stats->peer_hello_accepted;
            aggregate.eth_status_sent += runtime.stats->eth_status_sent;
            aggregate.remote_status_accepted += runtime.stats->remote_status_accepted;
            aggregate.remote_status_rejected += runtime.stats->remote_status_rejected;
            for (size_t i = 0; i < aggregate.remote_status_rejected_disconnect_reasons.size(); ++i)
            {
                aggregate.remote_status_rejected_disconnect_reasons[i] +=
                    runtime.stats->remote_status_rejected_disconnect_reasons[i];
            }
            for (size_t i = 0; i < aggregate.remote_status_rejected_validation_errors.size(); ++i)
            {
                aggregate.remote_status_rejected_validation_errors[i] +=
                    runtime.stats->remote_status_rejected_validation_errors[i];
            }
        }

        if (runtime.peer_queue)
        {
            const auto queue_stats = runtime.peer_queue->stats();
            aggregate.peer_queue.cached_peer_count += queue_stats.cached_peer_count;
            aggregate.peer_queue.discovered_peer_count += queue_stats.discovered_peer_count;
            aggregate.peer_queue.requeued_peer_count += queue_stats.requeued_peer_count;
            aggregate.peer_queue.duplicate_peer_drop_count += queue_stats.duplicate_peer_drop_count;
            aggregate.peer_queue.capacity_drop_count += queue_stats.capacity_drop_count;
            aggregate.peer_queue.flaky_peer_drop_count += queue_stats.flaky_peer_drop_count;
            aggregate.peer_queue.too_many_peers_backoff_count += queue_stats.too_many_peers_backoff_count;
            aggregate.peer_queue.backoff_drop_count += queue_stats.backoff_drop_count;
            aggregate.peer_queue.disconnect_feedback_count += queue_stats.disconnect_feedback_count;
            aggregate.peer_queue.disconnected_before_connected_count += queue_stats.disconnected_before_connected_count;
            aggregate.peer_queue.disconnected_after_connected_count += queue_stats.disconnected_after_connected_count;
            aggregate.peer_queue.too_many_peers_before_connected_count += queue_stats.too_many_peers_before_connected_count;
            aggregate.peer_queue.too_many_peers_after_connected_count += queue_stats.too_many_peers_after_connected_count;
            aggregate.peer_queue.tcp_failure_count += queue_stats.tcp_failure_count;
            aggregate.peer_queue.timeout_count += queue_stats.timeout_count;
            aggregate.peer_queue.subprotocol_error_count += queue_stats.subprotocol_error_count;
        }
    }
    return aggregate;
}

std::shared_ptr<EthPeerQueue> EthWatchService::peer_queue(
    const std::string& chain_name) const noexcept
{
    auto it = std::find_if(runtime_chains_.begin(), runtime_chains_.end(),
        [&chain_name](const RuntimeChain& runtime)
        {
            return runtime.config.canonical_name == chain_name;
        });
    if (it == runtime_chains_.end())
    {
        return nullptr;
    }
    return it->peer_queue;
}

discv4::DialFn EthWatchService::make_default_dial_fn(
    const discv4::ChainPeerConfig& chain_config) noexcept
{
    const auto stats_it = runtime_stats_by_chain_.find(chain_config.canonical_name);
    const auto runtime_stats = stats_it != runtime_stats_by_chain_.end()
        ? stats_it->second
        : std::shared_ptr<EthWatchRuntimeStatsSnapshot>{};

    return [this, chain_config, runtime_stats](
        discv4::ValidatedPeer                                      vp,
        std::function<void(rlpx::DisconnectReason)>                on_done,
        std::function<void(std::shared_ptr<rlpx::RlpxSession>)>    on_connected,
        boost::asio::yield_context                                yield)
    {
        static auto log = rlp::base::createLogger("eth_watch");
        auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
        if (!keypair_result)
        {
            log->warn("Failed to generate local RLPx keypair for chain '{}'",
                      chain_config.canonical_name);
            on_done(rlpx::DisconnectReason::kProtocolError);
            return;
        }

        const auto& keypair = keypair_result.value();
        const rlpx::SessionConnectParams params{
            vp.peer.ip,
            vp.peer.tcp_port,
            keypair.public_key,
            keypair.private_key,
            vp.pubkey,
            "rlp-eth-watch",
            0,
            [runtime_stats](rlpx::ConnectProgressPhase phase, rlpx::DisconnectReason)
            {
                if (!runtime_stats)
                {
                    return;
                }

                switch (phase)
                {
                case rlpx::ConnectProgressPhase::kTcpConnected:
                    ++runtime_stats->tcp_connected;
                    break;
                case rlpx::ConnectProgressPhase::kAuthSucceeded:
                    ++runtime_stats->auth_success;
                    break;
                case rlpx::ConnectProgressPhase::kLocalHelloSent:
                    ++runtime_stats->local_hello_sent;
                    break;
                case rlpx::ConnectProgressPhase::kPeerDisconnectBeforeHello:
                    ++runtime_stats->peer_disconnect_before_hello;
                    break;
                case rlpx::ConnectProgressPhase::kPeerHelloAccepted:
                    ++runtime_stats->peer_hello_accepted;
                    break;
                }
            }
        };

        log->debug("Dialing RLPx peer {}:{} for chain '{}'",
                   vp.peer.ip,
                   vp.peer.tcp_port,
                   chain_config.canonical_name);

        rlpx::DisconnectReason disconnect_reason = rlpx::DisconnectReason::kTcpError;
        auto session_result = rlpx::RlpxSession::connect(params, yield, &disconnect_reason);
        if (!session_result)
        {
            if (runtime_stats && disconnect_reason == rlpx::DisconnectReason::kTcpError)
            {
                ++runtime_stats->tcp_connect_failures;
            }
            log->debug("RLPx dial failed for {}:{} on chain '{}' ({}: {})",
                       vp.peer.ip,
                       vp.peer.tcp_port,
                       chain_config.canonical_name,
                       static_cast<int>(session_result.error()),
                       rlpx::to_string(session_result.error()));
            on_done(disconnect_reason);
            return;
        }

        auto session = std::move(session_result.value());
        auto fork_id = chain_config.fork_id.value_or(ForkId{});
        auto runner = std::make_shared<EthWatchRunner>(
            session,
            chain_config.canonical_name,
            chain_config.network_id,
            chain_config.genesis_hash,
            fork_id,
            chain_config.eth_message_schemas);

        std::optional<rlpx::DisconnectReason> remote_status_disconnect_reason;
        if (runtime_stats)
        {
            ++runtime_stats->eth_status_sent;
        }
        const auto handshake_result = PerformEthStatusHandshake(
            EthStatusHandshakeStart{
                std::make_shared<RlpxEthSessionChannel>(session),
                chain_config.network_id,
                chain_config.genesis_hash,
                fork_id,
                chain_config.eth_message_schemas,
                EthStatusAcceptedHandler{},
                [&remote_status_disconnect_reason](rlpx::DisconnectReason reason)
                {
                    remote_status_disconnect_reason = reason;
                },
                rlpx::EthMessageHandler{}
            },
            yield);
        if (!handshake_result)
        {
            if (runtime_stats)
            {
                ++runtime_stats->remote_status_rejected;
                if (remote_status_disconnect_reason.has_value())
                {
                    ++runtime_stats->remote_status_rejected_disconnect_reasons[
                        static_cast<uint8_t>(*remote_status_disconnect_reason)];
                }
                else
                {
                    const auto error_index = static_cast<size_t>(handshake_result.error());
                    if (error_index < runtime_stats->remote_status_rejected_validation_errors.size())
                    {
                        ++runtime_stats->remote_status_rejected_validation_errors[error_index];
                    }
                }
            }
            log->debug("ETH Status handshake failed for {}:{} on chain '{}' (error {})",
                       vp.peer.ip,
                       vp.peer.tcp_port,
                       chain_config.canonical_name,
                       static_cast<int>(handshake_result.error()));
            (void)session->disconnect(rlpx::DisconnectReason::kSubprotocolError);
            on_done(rlpx::DisconnectReason::kSubprotocolError);
            return;
        }

        log->info("Connected RLPx/ETH peer {}:{} for chain '{}'",
                  vp.peer.ip,
                  vp.peer.tcp_port,
                  chain_config.canonical_name);
        if (runtime_stats)
        {
            ++runtime_stats->remote_status_accepted;
        }

        on_connected(session);
        runner->set_event_callback(orchestration_callback_);
        for (const auto& watch : orchestration_config_.watches)
        {
            (void)runner->watch_event(
                watch.contract_address,
                watch.event_signature,
                watch.params,
                watch.from_block,
                watch.to_block);
        }
        runner->install_session_bridge();
        active_runners_.push_back(std::move(runner));
    };
}

bool EthWatchService::start_enr_tree_discovery(
    boost::asio::io_context& io,
    RuntimeChain&            runtime) noexcept
{
    const auto urls = configured_enr_tree_urls(runtime.config);
    if (urls.empty())
    {
        return false;
    }

    std::vector<std::string> bootstrap_enrs;
    if (orchestration_config_.enr_tree_resolver)
    {
        bootstrap_enrs = orchestration_config_.enr_tree_resolver(runtime.config, urls);
    }
    else
    {
        bootstrap_enrs = discv5::EnrTreeResolver{}.resolve(urls);
    }

    if (bootstrap_enrs.empty())
    {
        return false;
    }

    const auto direct_enqueued = enqueue_resolved_enr_tree_peers(
        runtime.config,
        runtime.peer_queue,
        bootstrap_enrs);
    if (direct_enqueued > 0U)
    {
        static auto log = rlp::base::createLogger("eth_watch");
        log->info("Enqueued {} direct ENR-tree peer(s) for chain '{}'",
                  direct_enqueued,
                  runtime.config.canonical_name);
    }

    return start_discv5_discovery(io, runtime, bootstrap_enrs);
}

bool EthWatchService::start_discv5_discovery(
    boost::asio::io_context&        io,
    RuntimeChain&                   runtime,
    const std::vector<std::string>& bootstrap_enrs) noexcept
{
    if (bootstrap_enrs.empty())
    {
        return false;
    }

    if (orchestration_config_.discv5_enr_tree_starter)
    {
        return orchestration_config_.discv5_enr_tree_starter(
            io,
            runtime.config,
            runtime.peer_queue,
            bootstrap_enrs);
    }

    discv5::discv5Config discovery_config = orchestration_config_.discv5_discovery;
    discovery_config.bootstrap_enrs = bootstrap_enrs;
    if (runtime.config.fork_id.has_value()
        && runtime.config.discovery_fork_filter == discv4::DiscoveryForkFilter::kRequire)
    {
        discv5::ForkId fork_id{};
        fork_id.hash = runtime.config.fork_id->fork_hash;
        fork_id.next = runtime.config.fork_id->next_fork;
        discovery_config.required_fork_id = fork_id;
    }

    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (keypair_result)
    {
        discovery_config.private_key = keypair_result.value().private_key;
        discovery_config.public_key = keypair_result.value().public_key;
    }

    try
    {
        runtime.discv5_client = orchestration_config_.discv5_client_factory
            ? orchestration_config_.discv5_client_factory(io, discovery_config)
            : std::make_shared<discv5::discv5_client>(io, discovery_config);
    }
    catch (...)
    {
        runtime.discv5_client.reset();
        return false;
    }

    if (!runtime.discv5_client)
    {
        return false;
    }

    auto queue = runtime.peer_queue;
    runtime.discv5_client->set_peer_discovered_callback(
        [queue](const discovery::ValidatedPeer& peer)
        {
            if (queue)
            {
                (void)queue->enqueue_validated_discovery_peer(peer);
            }
        });

    const auto start_result = runtime.discv5_client->start();
    if (!start_result)
    {
        runtime.discv5_client.reset();
        return false;
    }

    return true;
}

void EthWatchService::start_discv4_fallback(
    boost::asio::io_context& io,
    RuntimeChain&            runtime) noexcept
{
    if (orchestration_config_.discv4_fallback_starter)
    {
        runtime.discv4_fallback_started =
            orchestration_config_.discv4_fallback_starter(
                io,
                runtime.config,
                runtime.peer_queue);
        return;
    }

    discv4::discv4Config discovery_config = orchestration_config_.discovery;
    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (keypair_result)
    {
        discovery_config.private_key = keypair_result.value().private_key;
        discovery_config.public_key = keypair_result.value().public_key;
    }

    try
    {
        runtime.discovery_client = orchestration_config_.discovery_client_factory
            ? orchestration_config_.discovery_client_factory(io, discovery_config)
            : std::make_shared<discv4::discv4_client>(io, discovery_config);
    }
    catch (...)
    {
        runtime.discovery_client.reset();
        return;
    }

    if (!runtime.discovery_client)
    {
        return;
    }

    auto queue = runtime.peer_queue;
    runtime.discovery_client->set_peer_discovered_callback(
        [queue](const discv4::DiscoveredPeer& peer)
        {
            if (queue)
            {
                (void)queue->enqueue_discovered_peer(peer);
            }
        });

    const auto start_result = runtime.discovery_client->start();
    if (!start_result)
    {
        runtime.discovery_client.reset();
        return;
    }
    runtime.discv4_fallback_started = true;

    for (const auto& bootnode : runtime.peer_queue->discovery_bootnodes())
    {
        const std::string host = bootnode.peer.ip;
        const uint16_t port = bootnode.peer.udp_port;
        const discv4::NodeId node_id = bootnode.peer.node_id;
        auto client = runtime.discovery_client;
        boost::asio::spawn(io,
            [client, host, port, node_id](boost::asio::yield_context yield)
            {
                (void)client->find_node(host, port, node_id, yield);
            });
    }
}

// ---------------------------------------------------------------------------
// set_send_callback
// ---------------------------------------------------------------------------

void EthWatchService::set_send_callback(SendCallback cb) noexcept
{
    send_cb_ = std::move(cb);
}

void EthWatchService::set_eth_message_schemas(std::vector<EthMessageSchema> schemas) noexcept
{
    eth_message_schemas_ = std::move(schemas);
}

// ---------------------------------------------------------------------------
// watch_event
// ---------------------------------------------------------------------------

EventWatchId EthWatchService::watch_event(
    const codec::Address&             contract_address,
    const std::string&                event_signature,
    const std::vector<abi::AbiParam>& params,
    DecodedEventCallback              callback,
    std::optional<uint64_t>           from_block,
    std::optional<uint64_t>           to_block) noexcept
{
    const EventWatchId id = next_id_++;

    EventFilter filter;

    const codec::Address zero_addr{};
    if (contract_address != zero_addr)
    {
        filter.addresses.push_back(contract_address);
    }

    filter.topics.push_back(abi::event_signature_hash(event_signature));
    filter.from_block = from_block;
    filter.to_block   = to_block;

    subscriptions_.push_back({id, event_signature, params, std::move(callback)});

    watcher_.watch(filter, [this, id](const MatchedEvent& ev)
    {
        auto it = std::find_if(subscriptions_.begin(), subscriptions_.end(),
            [id](const Subscription& s) { return s.id == id; });
        if (it == subscriptions_.end())
        {
            return;
        }
        auto decoded = abi::decode_log(ev.log, it->event_signature, it->params);
        if (!decoded)
        {
            return;
        }
        it->callback(ev, decoded.value());
    });

    return id;
}

// ---------------------------------------------------------------------------
// unwatch
// ---------------------------------------------------------------------------

void EthWatchService::unwatch(EventWatchId id) noexcept
{
    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [id](const Subscription& s) { return s.id == id; }),
        subscriptions_.end());

    watcher_.unwatch(id);
}

// ---------------------------------------------------------------------------
// request_receipts
// ---------------------------------------------------------------------------

void EthWatchService::request_receipts(const Hash256& block_hash,
                                       uint64_t       block_number) noexcept
{
    if (!send_cb_)
    {
        return;
    }

    // Deduplicate — skip if we have already requested receipts for this block
    if (!chain_tracker_.mark_seen(block_hash, block_number))
    {
        return;
    }

    const uint64_t req_id = next_req_id_++;

    GetReceiptsMessage req;
    req.request_id = req_id;
    req.block_hashes.push_back(block_hash);

    auto encoded = protocol::encode_get_receipts(req);
    if (!encoded)
    {
        return;
    }

    pending_requests_[req_id] = {block_hash, block_number};
    ++stats_.receipts_requested;
    send_cb_(protocol::kGetReceiptsMessageId, std::move(encoded.value()));
}

// ---------------------------------------------------------------------------
// process_message
// ---------------------------------------------------------------------------

void EthWatchService::process_message(uint8_t eth_msg_id, rlp::ByteView payload) noexcept
{
    ++stats_.eth_messages_seen;

    if (eth_msg_id == protocol::kNewBlockHashesMessageId)
    {
        ++stats_.new_block_hashes_messages;
        auto decoded = protocol::decode_new_block_hashes(payload);
        if (!decoded)
        {
            ++stats_.decode_failures;
            return;
        }
        for (const auto& entry : decoded.value().entries)
        {
            request_receipts(entry.hash, entry.number);
        }
        return;
    }

    if (eth_msg_id == protocol::kNewBlockMessageId)
    {
        ++stats_.new_block_messages;
        auto decoded = eth_message_schemas_.empty()
            ? protocol::decode_new_block(payload)
            : protocol::decode_new_block(payload, eth_message_schemas_);
        if (!decoded)
        {
            ++stats_.decode_failures;
            return;
        }
        // NewBlock does not include a block hash on the wire — use zeroed sentinel.
        // Still trigger request_receipts so callers with a send_cb get receipts.
        const Hash256 block_hash{};
        process_new_block(decoded.value(), block_hash);
        return;
    }

    if (eth_msg_id == protocol::kReceiptsMessageId)
    {
        ++stats_.receipts_messages;
        auto decoded = eth_message_schemas_.empty()
            ? protocol::decode_receipts(payload)
            : protocol::decode_receipts(payload, eth_message_schemas_);
        if (!decoded)
        {
            ++stats_.decode_failures;
            return;
        }

        const auto& msg = decoded.value();
        size_t block_idx = 0;
        for (const auto& block_receipts : msg.receipts)
        {
            uint64_t block_number = 0;
            Hash256  block_hash{};

            // Correlate to a pending request if request_id is present
            if (msg.request_id.has_value())
            {
                // Each block in the response corresponds to one hash in the request.
                // We issued one hash per request, so request_id maps 1:1.
                if (block_idx == 0)
                {
                    auto it = pending_requests_.find(msg.request_id.value());
                    if (it != pending_requests_.end())
                    {
                        block_hash   = it->second.block_hash;
                        block_number = it->second.block_number;
                        pending_requests_.erase(it);
                    }
                }
            }

            std::vector<Hash256> tx_hashes(block_receipts.size());
            process_receipts(block_receipts, tx_hashes, block_number, block_hash);
            ++block_idx;
        }
        return;
    }
}

// ---------------------------------------------------------------------------
// process_receipts
// ---------------------------------------------------------------------------

void EthWatchService::process_receipts(
    const std::vector<codec::Receipt>& receipts,
    const std::vector<Hash256>&        tx_hashes,
    uint64_t                           block_number,
    const Hash256&                     block_hash,
    const std::vector<std::vector<uint32_t>>& log_indices) noexcept
{
    const size_t count = std::min(receipts.size(), tx_hashes.size());
    stats_.receipts_processed += static_cast<uint64_t>(count);
    uint32_t next_log_index = 0;
    for (size_t i = 0; i < count; ++i)
    {
        const size_t log_count = receipts[i].logs.size();
        uint32_t first_log_index = next_log_index;
        if (i < log_indices.size() && !log_indices[i].empty())
        {
            first_log_index = log_indices[i].front();
        }
        const size_t matched = watcher_.process_receipt(
            receipts[i],
            tx_hashes[i],
            block_number,
            block_hash,
            first_log_index);
        stats_.logs_seen += static_cast<uint64_t>(log_count);
        stats_.matched_logs += static_cast<uint64_t>(matched);
        stats_.discarded_logs += static_cast<uint64_t>(log_count - matched);
        next_log_index += static_cast<uint32_t>(log_count);
    }
}

// ---------------------------------------------------------------------------
// process_new_block
// ---------------------------------------------------------------------------

void EthWatchService::process_new_block(const NewBlockMessage& msg,
                                        const Hash256&         block_hash) noexcept
{
    // Request receipts for each transaction in the block so logs can be watched.
    // The block number comes from the embedded header.
    request_receipts(block_hash, msg.header.number);
}

} // namespace eth
