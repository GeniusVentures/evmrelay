// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/eth_watch_service.hpp>

#include "../../examples/chain_config.hpp"

#include <boost/asio/io_context.hpp>

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

constexpr const char* kRunLiveEnv = "EVMRELAY_RUN_LIVE_ALL_CHAINS_TEST";
constexpr const char* kChainsEnv = "EVMRELAY_LIVE_ALL_CHAINS";
constexpr const char* kChainPeersJsonEnv = "EVMRELAY_LIVE_ALL_CHAINS_JSON";
constexpr const char* kChainConfigArgv0Env = "EVMRELAY_LIVE_ALL_CHAINS_CONFIG_ARGV0";
constexpr const char* kRunSecondsEnv = "EVMRELAY_LIVE_ALL_CHAINS_SECONDS";
constexpr const char* kMinDiscoveredPerChainEnv = "EVMRELAY_LIVE_ALL_CHAINS_MIN_DISCOVERED_PER_CHAIN";
constexpr const char* kMinDiscoveredTotalEnv = "EVMRELAY_LIVE_ALL_CHAINS_MIN_DISCOVERED_TOTAL";
constexpr const char* kMinStatusAcceptedEnv = "EVMRELAY_LIVE_ALL_CHAINS_MIN_STATUS_ACCEPTED";
constexpr const char* kMinEthMessagesEnv = "EVMRELAY_LIVE_ALL_CHAINS_MIN_ETH_MESSAGES";
constexpr const char* kMaxPeersPerChainEnv = "EVMRELAY_LIVE_ALL_CHAINS_MAX_PEERS_PER_CHAIN";
constexpr const char* kMaxPeersTotalEnv = "EVMRELAY_LIVE_ALL_CHAINS_MAX_PEERS_TOTAL";
constexpr const char* kMaxPendingPeersEnv = "EVMRELAY_LIVE_ALL_CHAINS_MAX_PENDING_PEERS";

constexpr uint32_t kDefaultRunSeconds = 180U;
constexpr size_t kDefaultMinDiscoveredPerChain = 1U;
constexpr size_t kDefaultMinStatusAccepted = 10U;
constexpr size_t kDefaultMinEthMessages = 5U;
constexpr int kDefaultMaxPeersPerChain = 3;
constexpr int kDefaultMaxPeersTotal = 33;
constexpr size_t kDefaultMaxPendingPeers = 300U;

bool env_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

std::optional<std::string> env_string(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty())
    {
        return std::nullopt;
    }
    return std::string(value);
}

uint32_t env_u32_or(const char* name, uint32_t fallback)
{
    const auto value = env_string(name);
    if (!value.has_value())
    {
        return fallback;
    }

    try
    {
        const unsigned long parsed = std::stoul(*value);
        return parsed == 0UL ? fallback : static_cast<uint32_t>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

size_t env_size_or(const char* name, size_t fallback)
{
    const auto value = env_string(name);
    if (!value.has_value())
    {
        return fallback;
    }

    try
    {
        const unsigned long parsed = std::stoul(*value);
        return parsed == 0UL ? fallback : static_cast<size_t>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

int env_int_or(const char* name, int fallback)
{
    const auto value = env_string(name);
    if (!value.has_value())
    {
        return fallback;
    }

    try
    {
        const int parsed = std::stoi(*value);
        return parsed <= 0 ? fallback : parsed;
    }
    catch (...)
    {
        return fallback;
    }
}

std::vector<std::string> parse_chain_names(const std::string& value)
{
    std::vector<std::string> names;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        if (!item.empty())
        {
            names.push_back(item);
        }
    }
    return names;
}

std::vector<std::string> default_live_chains()
{
    return {
        "ethereum-mainnet",
        "ethereum-sepolia",
        "ethereum-holesky",
        "ethereum-hoodi",
        "polygon-mainnet",
        "polygon-amoy",
        "bnb-smart-chain",
        "bnb-smart-chain-testnet",
        "base-mainnet",
        "base-sepolia",
        "gnosis-chain",
    };
}

std::vector<std::string> selected_live_chains()
{
    const auto configured = env_string(kChainsEnv);
    if (!configured.has_value())
    {
        return default_live_chains();
    }

    auto names = parse_chain_names(*configured);
    return names.empty() ? default_live_chains() : names;
}

std::optional<discv4::ChainPeerConfig> load_live_chain_config(
    const std::string& chain_name,
    const std::string& chain_peers_json_path,
    const std::string& chain_config_argv0)
{
    auto chain_config = load_chain_peer_config(
        chain_name,
        chain_config_argv0,
        chain_peers_json_path,
        "https://enodes.gnus.ai/chain_enodes.json.gz",
        chain_peers_json_path.empty());
    if (!chain_config.has_value())
    {
        return std::nullopt;
    }

    apply_chain_discovery_config(*chain_config, chain_config_argv0);
    return chain_config;
}

} // namespace

TEST(EthWatchAllChainsLiveTest, DiscoverFirstFindsPeersAndReceivesEthMessages)
{
    if (!env_enabled(kRunLiveEnv))
    {
        GTEST_SKIP()
            << "set " << kRunLiveEnv << "=1 to run live all-chain discovery and RLPx testing";
    }

    const auto chain_names = selected_live_chains();
    const auto chain_peers_json_path = env_string(kChainPeersJsonEnv).value_or("");
    const auto chain_config_argv0 = env_string(kChainConfigArgv0Env).value_or(
        (std::filesystem::current_path() / "examples" / "eth_watch" / "eth_watch").string());
    spdlog::set_level(spdlog::level::warn);

    std::vector<discv4::ChainPeerConfig> chains;
    chains.reserve(chain_names.size());
    size_t cached_peer_startup_count = 0U;
    for (const auto& chain_name : chain_names)
    {
        auto chain_config = load_live_chain_config(
            chain_name,
            chain_peers_json_path,
            chain_config_argv0);
        ASSERT_TRUE(chain_config.has_value())
            << "missing chain metadata for " << chain_name
            << "; set " << kChainPeersJsonEnv << " to a chain_enodes.json path "
            << "or allow the test to refresh the cache";
        cached_peer_startup_count += chain_config->nodes.size();
        chains.push_back(std::move(*chain_config));
    }

    eth::EthWatchServiceConfig config{};
    config.connection.max_connections_per_chain =
        env_int_or(kMaxPeersPerChainEnv, kDefaultMaxPeersPerChain);
    config.connection.max_total_connections =
        env_int_or(kMaxPeersTotalEnv, kDefaultMaxPeersTotal);
    config.peer_queue.max_pending_peers =
        env_size_or(kMaxPendingPeersEnv, kDefaultMaxPendingPeers);
    config.chains = chains;
    config.discovery_mode = eth::EthWatchDiscoveryMode::kDiscoverFirst;
    config.discovery.bind_port = 0U;
    config.discv5_discovery.bind_port = 0U;

    // This opt-in live test is process-scoped. Stopping active Boost coroutine
    // sessions during GTest teardown can surface forced_unwind outside the
    // coroutine stack, so the live service is intentionally left for process exit.
    auto* service = new eth::EthWatchService();
    ASSERT_TRUE(service->initialize(std::move(config), [](const eth::WatchEventNotification&) {}));

    auto* io = new boost::asio::io_context();
    service->run(*io);
    io->run_for(std::chrono::seconds(env_u32_or(kRunSecondsEnv, kDefaultRunSeconds)));

    const auto connection = service->aggregate_connection_stats();
    const auto traffic = service->aggregate_runtime_stats();
    std::unordered_map<std::string, eth::EthPeerQueueStatsSnapshot> chain_stats;
    for (const auto& chain_name : chain_names)
    {
        const auto queue = service->peer_queue(chain_name);
        ASSERT_TRUE(queue) << "missing runtime queue for chain " << chain_name;
        chain_stats.emplace(chain_name, queue->stats());
    }
    const size_t min_discovered_total =
        env_size_or(kMinDiscoveredTotalEnv, cached_peer_startup_count);
    const size_t min_discovered_per_chain =
        env_size_or(kMinDiscoveredPerChainEnv, kDefaultMinDiscoveredPerChain);
    const size_t min_status_accepted =
        env_size_or(kMinStatusAcceptedEnv, kDefaultMinStatusAccepted);
    const size_t min_eth_messages =
        env_size_or(kMinEthMessagesEnv, kDefaultMinEthMessages);

    std::cout
        << "[ LIVE    ] all-chain discover-first summary:"
        << " startup_cached_peers=" << cached_peer_startup_count
        << " final_cached_peers=" << connection.peer_queue.cached_peer_count
        << " discovered_peers=" << connection.peer_queue.discovered_peer_count
        << " remote_status_accepted=" << connection.remote_status_accepted
        << " eth_messages=" << traffic.eth_messages_seen
        << "\n";

    EXPECT_EQ(connection.peer_queue.cached_peer_count, 0U)
        << "discover-first functional test must not enqueue cached nodes as dial candidates";
    EXPECT_GE(connection.peer_queue.discovered_peer_count, min_discovered_total)
        << "discovery did not replace the configured cached peer count";
    EXPECT_GE(connection.remote_status_accepted, min_status_accepted)
        << "not enough peers completed the ETH Status handshake";
    EXPECT_GE(traffic.eth_messages_seen, min_eth_messages)
        << "not enough valid ETH messages arrived before disconnect";

    for (const auto& chain_name : chain_names)
    {
        const auto stats = chain_stats.at(chain_name);
        std::cout
            << "[ LIVE    ] chain=" << chain_name
            << " cached_peers=" << stats.cached_peer_count
            << " discovered_peers=" << stats.discovered_peer_count
            << " disconnect_feedback=" << stats.disconnect_feedback_count
            << "\n";

        EXPECT_EQ(stats.cached_peer_count, 0U)
            << "discover-first functional test must not use cached peers for " << chain_name;
        EXPECT_GE(stats.discovered_peer_count, min_discovered_per_chain)
            << "chain did not discover enough peers: " << chain_name;
    }
}
