// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <discv4/chain_peers.hpp>
#include <eth/eth_watch_cli.hpp>
#include <eth/eth_watch_service.hpp>

#include <boost/asio/io_context.hpp>

#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

struct TestRunner
{
    int run = 0;
    int failed = 0;

    void start(const std::string& name)
    {
        ++run;
        current = name;
        std::cout << "[ RUN      ] " << current << "\n";
    }

    void pass()
    {
        std::cout << "[       OK ] " << current << "\n";
    }

    void fail(const std::string& message)
    {
        ++failed;
        std::cout << "[  FAILED  ] " << current << ": " << message << "\n";
    }

    int finish() const
    {
        std::cout << "[==========] " << run << " test(s) ran\n";
        if (failed == 0)
        {
            std::cout << "[  PASSED  ] " << run << " test(s)\n";
            return 0;
        }
        std::cout << "[  FAILED  ] " << failed << " test(s)\n";
        return 1;
    }

    std::string current;
};

std::string make_enode(const std::string& ip, uint16_t port, char fill)
{
    return std::string("enode://")
        + std::string(128, fill)
        + "@"
        + ip
        + ":"
        + std::to_string(port);
}

std::string make_chain_json(
    const std::string& chain_name,
    uint64_t           network_id,
    const std::string& genesis_hex,
    const std::string& fork_id,
    const std::string& nodes_json,
    const std::string& bootnodes_json)
{
    return std::string("{\"") + chain_name + "\":{"
        + "\"networkId\":" + std::to_string(network_id) + ","
        + "\"genesisHex\":\"" + genesis_hex + "\","
        + "\"forkId\":\"" + fork_id + "\","
        + "\"forkNext\":\"0\","
        + "\"nodes\":" + nodes_json + ","
        + "\"bootnodes\":" + bootnodes_json
        + "}}";
}

std::string peer_json(const std::string& ip, uint16_t port, char fill)
{
    return std::string("{\"enode\":\"") + make_enode(ip, port, fill) + "\"}";
}

std::optional<discv4::ChainPeerConfig> load_config(
    const std::string& chain_name,
    const std::string& nodes_json,
    const std::string& bootnodes_json,
    uint64_t           network_id = 11155111,
    const std::string& genesis_hex = "25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9",
    const std::string& fork_id = "268956b6")
{
    return discv4::load_chain_peer_config_from_json_text(
        chain_name,
        make_chain_json(chain_name, network_id, genesis_hex, fork_id, nodes_json, bootnodes_json));
}

discv4::DialFn no_op_dial_fn()
{
    return [](
        discv4::ValidatedPeer,
        std::function<void(rlpx::DisconnectReason)> done,
        std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
        boost::asio::yield_context)
    {
        done(rlpx::DisconnectReason::kTcpError);
    };
}

bool expect(bool condition, const std::string& message, TestRunner& runner)
{
    if (!condition)
    {
        runner.fail(message);
        return false;
    }
    return true;
}

void test_service_starts_from_cached_chain_metadata(TestRunner& runner)
{
    runner.start("EthWatchExample.CachedChainMetadata");

    auto chain_config = load_config(
        "ethereum-sepolia",
        "[" + peer_json("10.0.0.1", 30303, 'a') + "]",
        "[" + peer_json("10.0.0.2", 30304, 'b') + "]");
    if (!expect(chain_config.has_value(), "failed to load chain metadata", runner)) { return; }

    auto watches = eth::cli::build_service_watch_specs({
        {"0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70", "Transfer(address,address,uint256)"},
        {"0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70", "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"},
    });
    if (!expect(watches.has_value(), "failed to build watch specs", runner)) { return; }

    eth::EthWatchConnectionConfig connection{};
    connection.max_connections_per_chain = 1;
    connection.max_total_connections = 1;

    auto service_config = eth::cli::build_service_config(
        connection,
        std::move(*watches),
        {*chain_config});
    service_config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };

    boost::asio::io_context io;
    eth::EthWatchService service;
    if (!expect(service.initialize(std::move(service_config), [](const eth::WatchEventNotification&) {}),
                "service rejected cache-backed config", runner)) { return; }
    service.run(io);

    auto queue = service.peer_queue("ethereum-sepolia");
    if (!expect(queue != nullptr, "peer queue not created", runner)) { return; }
    if (!expect(queue->cached_peer_count() == 1U, "cached peer was not enqueued", runner)) { return; }
    if (!expect(!queue->needs_discovery(), "cached-peer chain should not require fallback discovery", runner)) { return; }
    if (!expect(service.discv4_fallback_count() == 0U, "unexpected fallback discovery startup", runner)) { return; }

    runner.pass();
}

void test_gnosis_empty_nodes_uses_discovery_fallback(TestRunner& runner)
{
    runner.start("EthWatchExample.GnosisDiscoveryFallback");

    auto chain_config = load_config(
        "gnosis-chain",
        "[]",
        "[" + peer_json("10.0.0.3", 30305, 'c') + "]",
        100,
        "4f1dd23188aab3a0b3768e6a2b5f6cbf3fcb259af45d37b228a8a0ae61161f80",
        "06000064");
    if (!expect(chain_config.has_value(), "failed to load Gnosis metadata", runner)) { return; }
    if (!expect(chain_config->nodes.empty(), "Gnosis fixture should have empty nodes", runner)) { return; }
    if (!expect(chain_config->bootnodes.size() == 1U, "Gnosis fixture should have a bootnode", runner)) { return; }

    eth::EthWatchServiceConfig service_config{};
    service_config.chains.push_back(*chain_config);
    service_config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };
    service_config.discv4_fallback_starter = [](
        boost::asio::io_context&,
        const discv4::ChainPeerConfig& chain,
        std::shared_ptr<eth::EthPeerQueue> queue)
    {
        return chain.canonical_name == "gnosis-chain"
            && chain.nodes.empty()
            && !chain.bootnodes.empty()
            && queue
            && queue->needs_discovery();
    };

    boost::asio::io_context io;
    eth::EthWatchService service;
    if (!expect(service.initialize(std::move(service_config), [](const eth::WatchEventNotification&) {}),
                "service rejected discovery-fallback config", runner)) { return; }
    service.run(io);

    auto queue = service.peer_queue("gnosis-chain");
    if (!expect(queue != nullptr, "Gnosis peer queue not created", runner)) { return; }
    if (!expect(queue->cached_peer_count() == 0U, "Gnosis should not have cached peers", runner)) { return; }
    if (!expect(queue->discovery_bootnodes().size() == 1U, "Gnosis bootnode not preserved", runner)) { return; }
    if (!expect(queue->needs_discovery(), "Gnosis queue should need discovery", runner)) { return; }
    if (!expect(service.discv4_fallback_count() == 1U, "fallback discovery did not start", runner)) { return; }

    runner.pass();
}

void test_all_chain_service_config(TestRunner& runner)
{
    runner.start("EthWatchExample.AllChainsConfig");

    const std::vector<std::string> chains{
        "ethereum-mainnet",
        "polygon-mainnet",
        "bnb-smart-chain",
        "base-mainnet",
    };
    const std::vector<char> node_fills{'1', '2', '3', '4'};
    const std::vector<char> bootnode_fills{'5', '6', '7', '8'};

    std::vector<discv4::ChainPeerConfig> chain_configs;
    for (size_t i = 0; i < chains.size(); ++i)
    {
        const auto& chain = chains[i];
        auto config = load_config(
            chain,
            "[" + peer_json("10.0.1." + std::to_string(i + 1), 30303, node_fills[i]) + "]",
            "[" + peer_json("10.0.2." + std::to_string(i + 1), 30304, bootnode_fills[i]) + "]",
            1);
        if (!expect(config.has_value(), "failed to load metadata for " + chain, runner)) { return; }
        chain_configs.push_back(*config);
    }

    auto watches = eth::cli::build_service_watch_specs({
        {"", "Transfer(address,address,uint256)"},
    });
    if (!expect(watches.has_value(), "failed to build all-chain watch specs", runner)) { return; }

    auto service_config = eth::cli::build_service_config(
        eth::EthWatchConnectionConfig{},
        std::move(*watches),
        std::move(chain_configs));
    service_config.dial_fn_factory = [](const discv4::ChainPeerConfig&) { return no_op_dial_fn(); };

    boost::asio::io_context io;
    eth::EthWatchService service;
    if (!expect(service.initialize(std::move(service_config), [](const eth::WatchEventNotification&) {}),
                "service rejected all-chain config", runner)) { return; }
    service.run(io);

    if (!expect(service.runtime_chain_count() == chains.size(), "wrong runtime chain count", runner)) { return; }
    if (!expect(service.peer_queue_count() == chains.size(), "wrong peer queue count", runner)) { return; }
    if (!expect(service.scheduler_count() == chains.size(), "wrong scheduler count", runner)) { return; }

    runner.pass();
}

} // namespace

int main()
{
    TestRunner runner;
    std::cout << "[==========] eth_watch C++ example tests\n";
    test_service_starts_from_cached_chain_metadata(runner);
    test_gnosis_empty_nodes_uses_discovery_fallback(runner);
    test_all_chain_service_config(runner);
    return runner.finish();
}
