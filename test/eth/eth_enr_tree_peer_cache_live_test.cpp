// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <discv4/dial_scheduler.hpp>
#include <discv5/discv5_client.hpp>
#include <discv5/enr_tree.hpp>
#include <eth/eth_peer_queue.hpp>
#include <gtest/gtest.h>
#include <rlpx/crypto/ecdh.hpp>

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{

constexpr const char* kRunLiveEnv = "EVMRELAY_RUN_LIVE_ENR_TREE_TEST";
constexpr const char* kChainEnv = "EVMRELAY_LIVE_ENR_TREE_CHAIN";
constexpr const char* kRunSecondsEnv = "EVMRELAY_LIVE_ENR_TREE_SECONDS";
constexpr const char* kExpectedPeersEnv = "EVMRELAY_LIVE_ENR_TREE_MIN_PEERS";
constexpr uint32_t kDefaultRunSeconds = 5U;
constexpr size_t kDefaultMinPeers = 1U;

struct LiveChain
{
    std::string canonical_name;
    uint64_t network_id = 0U;
    std::vector<std::string> enr_trees;
};

bool env_enabled(const char* name)
{
    const char* value = std::getenv(name);
    return value != nullptr && std::string(value) == "1";
}

uint32_t env_u32_or(const char* name, uint32_t fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return fallback;
    }

    try
    {
        const unsigned long parsed = std::stoul(value);
        return parsed == 0UL ? fallback : static_cast<uint32_t>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

size_t env_size_or(const char* name, size_t fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        return fallback;
    }

    try
    {
        const unsigned long parsed = std::stoul(value);
        return parsed == 0UL ? fallback : static_cast<size_t>(parsed);
    }
    catch (...)
    {
        return fallback;
    }
}

LiveChain selected_live_chain()
{
    const char* requested = std::getenv(kChainEnv);
    const std::string chain = requested == nullptr ? "ethereum-mainnet" : requested;

    if (chain == "polygon-mainnet")
    {
        return {
            "polygon-mainnet",
            137U,
            {
                "enrtree://AKUEZKN7PSKVNR65FZDHECMKOJQSGPARGTPPBI7WS2VUL4EGR6XPC@pos.polygon-peers.io"
            }
        };
    }

    if (chain == "polygon-amoy")
    {
        return {
            "polygon-amoy",
            80002U,
            {
                "enrtree://AKUEZKN7PSKVNR65FZDHECMKOJQSGPARGTPPBI7WS2VUL4EGR6XPC@amoy.polygon-peers.io"
            }
        };
    }

    if (chain == "ethereum-sepolia")
    {
        return {
            "ethereum-sepolia",
            11155111U,
            {
                "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net"
            }
        };
    }

    if (chain == "ethereum-holesky")
    {
        return {
            "ethereum-holesky",
            17000U,
            {
                "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.holesky.ethdisco.net"
            }
        };
    }

    if (chain == "ethereum-hoodi")
    {
        return {
            "ethereum-hoodi",
            560048U,
            {
                "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.hoodi.ethdisco.net"
            }
        };
    }

    return {
        "ethereum-mainnet",
        1U,
        {
            "enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.mainnet.ethdisco.net"
        }
    };
}

discv4::DialFn no_op_dial_fn()
{
    return [](
        discv4::ValidatedPeer,
        std::function<void()> done,
        std::function<void(std::shared_ptr<rlpx::RlpxSession>)>,
        boost::asio::yield_context)
    {
        done();
    };
}

} // namespace

TEST(EthEnrTreePeerCacheLiveTest, EmptyCacheFillsFromEnrTreeDiscovery)
{
    if (!env_enabled(kRunLiveEnv))
    {
        GTEST_SKIP()
            << "set " << kRunLiveEnv << "=1 to run live DNS/discv5 discovery";
    }

    const LiveChain chain = selected_live_chain();
    const uint32_t run_seconds = env_u32_or(kRunSecondsEnv, kDefaultRunSeconds);
    const size_t min_peers = env_size_or(kExpectedPeersEnv, kDefaultMinPeers);

    std::vector<std::string> bootstrap_enrs =
        discv5::EnrTreeResolver{}.resolve(chain.enr_trees);

    ASSERT_FALSE(bootstrap_enrs.empty())
        << "ENR-tree resolution returned no usable bootnodes for "
        << chain.canonical_name;

    boost::asio::io_context io;
    auto pool = std::make_shared<discv4::WatcherPool>(1, 1);
    auto scheduler = std::make_shared<discv4::DialScheduler>(
        io,
        pool,
        no_op_dial_fn());

    eth::EthPeerQueueConfig queue_config{};
    queue_config.max_pending_peers = 4096U;
    auto peer_queue = std::make_shared<eth::EthPeerQueue>(scheduler, queue_config);

    EXPECT_EQ(peer_queue->cached_peer_count(), 0U);
    EXPECT_EQ(peer_queue->discovered_peer_count(), 0U);

    discv5::discv5Config config{};
    config.bind_ip = "0.0.0.0";
    config.bind_port = 0U;
    config.bootstrap_enrs = std::move(bootstrap_enrs);
    config.query_interval_sec = 1U;
    config.max_concurrent_queries = 16U;

    const auto keypair = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    ASSERT_TRUE(keypair.has_value()) << "failed to generate local discv5 keypair";
    config.private_key = keypair.value().private_key;
    config.public_key = keypair.value().public_key;

    discv5::discv5_client client(io, config);
    client.set_peer_discovered_callback(
        [peer_queue](const discovery::ValidatedPeer& peer)
        {
            (void)peer_queue->enqueue_validated_discovery_peer(peer);
        });

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value())
        << "failed to start live discv5 client; UDP bind may be unavailable";

    io.run_for(std::chrono::seconds(run_seconds));

    client.stop();
    scheduler->stop();

    const size_t discovered = peer_queue->discovered_peer_count();
    std::cout
        << "[ LIVE    ] " << chain.canonical_name
        << " ENR-tree discovery accepted " << discovered
        << " peers into an empty EthPeerQueue over "
        << run_seconds << " seconds\n";
    std::cout
        << "[ LIVE    ] discv5 stats: packets=" << client.received_packet_count()
        << " nodes=" << client.nodes_packet_count()
        << " findnode_failures=" << client.send_findnode_failure_count()
        << " queue_capacity_drops=" << peer_queue->capacity_drop_count()
        << " duplicate_drops=" << peer_queue->duplicate_peer_drop_count()
        << "\n";

    EXPECT_GE(discovered, min_peers)
        << "ENR-tree discovery did not fill the empty peer queue";
}
