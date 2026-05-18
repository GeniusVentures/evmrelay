// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// examples/discovery/test_discovery.cpp
//
// Functional test for discv4/chain peer cache seeding against live Ethereum peers.
// Uses DialScheduler to seed and observe discovery/chain peer cache inputs for each
// configured chain.
//
// Checks (GTest-style output):
//   1. At least one chain peer is loaded from the current cache / refresh path
//   2. Dial / connect statistics are reported for diagnostics only
//
// Exit code 0 = chain peer cache loading checks pass, 1 = any chain peer cache loading check failed.
//
// Usage:
//   ./test_discovery [--log-level debug] [--timeout 30] [--connections 1]

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>

#include <discv4/bootnodes.hpp>
#include <discv4/bootnodes_test.hpp>
#include <discv4/bootstrap_peers.hpp>
#include <discv4/chain_peers.hpp>
#include <discv4/dial_scheduler.hpp>
#include <discv4/discv4_client.hpp>
#include <eth/eth_types.hpp>
#include <eth/messages.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/framing/message_stream.hpp>
#include <rlpx/protocol/messages.hpp>
#include <rlpx/rlpx_error.hpp>
#include <rlpx/rlpx_session.hpp>
#include <base/rlp-logger.hpp>

#include <spdlog/spdlog.h>

#include "../chain_config.hpp"

// ── Ethereum mainnet chain constants ─────────────────────────────────────────

static constexpr uint64_t kMainnetNetworkId = 1;
static constexpr uint8_t  kEthOffset        = 0x10;
static constexpr const char* kForkHashChainKey = "mainnet";
static constexpr const char* kChainPeerCacheChainKey = "ethereum-mainnet";
static constexpr const char* kChainPeersUrlDefault = "https://enodes.gnus.ai/chain_enodes.json.gz";

struct ChainTarget
{
    const char* chain_key;
    const char* chain_peer_cache_key;
    uint64_t network_id = 0;
    const char* genesis_hex;
    const std::vector<std::string>* bootnodes = nullptr;
    std::optional<std::array<uint8_t, 4U>> fork_hash_fallback;
};

static std::optional<uint8_t> hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<uint8_t>(10 + c - 'a');
    }
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<uint8_t>(10 + c - 'A');
    }
    return std::nullopt;
}

static eth::Hash256 hash256_from_hex(const char* hex)
{
    eth::Hash256 h{};
    for (size_t i = 0; i < 32; ++i)
    {
        const auto hi = hex_to_nibble(hex[i * 2]).value_or(0);
        const auto lo = hex_to_nibble(hex[i * 2 + 1]).value_or(0);
        h[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return h;
}

static std::vector<ChainTarget> all_chain_targets()
{
    return {
        { "mainnet", "ethereum-mainnet", 1, "d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3", &ETHEREUM_MAINNET_BOOTNODES, std::array<uint8_t, 4U>{ 0x07, 0xc9, 0x46, 0x2e } },
        { "sepolia", "ethereum-sepolia", 11155111, "25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9", &ETHEREUM_SEPOLIA_BOOTNODES, std::array<uint8_t, 4U>{ 0x26, 0x89, 0x56, 0xb6 } },
        { "holesky", "ethereum-holesky", 17000, "b5f7f912443c940f21fd611f12828d75b534364ed9e95ca4e307729a4661bde4", &ETHEREUM_HOLESKY_BOOTNODES, std::array<uint8_t, 4U>{ 0x9b, 0xc6, 0xcb, 0x31 } }
    };
}

static eth::Hash256 mainnet_genesis()
{
    return hash256_from_hex("d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3");
}

// Ethereum mainnet fallback hash — used only when chains.json is not found.
// Update chains.json instead of this constant when the fork advances.
static const std::array<uint8_t, 4U> kMainnetForkHashFallback{ 0x07, 0xc9, 0x46, 0x2e };

// ── Dial-attempt statistics ───────────────────────────────────────────────────

struct DialStats
{
    std::atomic<int> dialed{0};                     ///< total dial attempts started
    std::atomic<int> connect_failed{0};             ///< TCP / auth / pre-HELLO Disconnect
    std::atomic<int> wrong_chain{0};                ///< Status received but wrong network_id
    std::atomic<int> status_timeout{0};             ///< no Status within timeout (not TooManyPeers)
    std::atomic<int> too_many_peers{0};             ///< TooManyPeers before chain confirmed
    std::atomic<int> too_many_peers_right_chain{0}; ///< TooManyPeers after chain confirmed
    std::atomic<int> connected{0};                  ///< right chain, Status validated
    std::atomic<int> eth_messages{0};               ///< post-handshake ETH messages observed
};


// Does not set up EthWatchService — just validates the chain and returns.

static void dial_connect_only(
    discv4::ValidatedPeer                                           vp,
    std::function<void()>                                          on_done,
    std::function<void(std::shared_ptr<rlpx::RlpxSession>)>       on_connected,
    std::function<void()>                                          on_eth_message,
    boost::asio::yield_context                                     yield,
    std::shared_ptr<DialStats>                                     stats,
    eth::ForkId                                                    fork_id,
    eth::Hash256                                                   genesis,
    uint64_t                                                       network_id)
{
    static auto log = rlp::base::createLogger("test_discovery");
    ++stats->dialed;

    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (!keypair_result)
    {
        ++stats->connect_failed;
        on_done();
        return;
    }
    const auto& keypair = keypair_result.value();

    const rlpx::SessionConnectParams params{
        vp.peer.ip,
        vp.peer.tcp_port,
        keypair.public_key,
        keypair.private_key,
        vp.pubkey,
        "rlp-test-discovery",
        0
    };

    auto session_result = rlpx::RlpxSession::connect(params, yield);
    if (!session_result)
    {
        ++stats->connect_failed;
        on_done();
        return;
    }
    auto session = std::move(session_result.value());

    // Send ETH Status (69)
    {
        eth::StatusMessage69 status69{
            69,
            network_id,
            genesis,
            fork_id,
            0,
            0,
            genesis,
        };
        eth::StatusMessage status = status69;
        auto encoded = eth::protocol::encode_status(status);
        if (encoded)
        {
            (void)session->post_message(rlpx::framing::Message{
                static_cast<uint8_t>(kEthOffset + eth::protocol::kStatusMessageId),
                std::move(encoded.value())
            });
        }
    }

    auto executor           = yield.get_executor();
    auto status_received    = std::make_shared<std::atomic<bool>>(false);
    auto status_timeout     = std::make_shared<boost::asio::steady_timer>(executor);
    auto lifetime           = std::make_shared<boost::asio::steady_timer>(executor);
    auto disconnect_reason  = std::make_shared<std::atomic<int>>(
                                  static_cast<int>(rlpx::DisconnectReason::kRequested));
    status_timeout->expires_after(eth::protocol::kStatusHandshakeTimeout);
    lifetime->expires_after(std::chrono::seconds(10)); // stay connected briefly after handshake

    session->set_disconnect_handler(
        [lifetime, status_timeout, disconnect_reason]
        (const rlpx::protocol::DisconnectMessage& msg)
        {
            disconnect_reason->store(static_cast<int>(msg.reason));
            lifetime->cancel();
            status_timeout->cancel();
        });

    session->set_ping_handler([session](const rlpx::protocol::PingMessage&) {
        const rlpx::protocol::PongMessage pong;
        auto encoded = pong.encode();
        if (!encoded) { return; }
        (void)session->post_message(rlpx::framing::Message{
            rlpx::kPongMessageId,
            std::move(encoded.value())
        });
    });

    session->set_generic_handler([session, status_received, status_timeout,
                                   on_connected, on_eth_message, genesis, stats, network_id](const rlpx::protocol::Message& msg)
    {
        static auto gh_log = rlp::base::createLogger("test_discovery");
        if (msg.id < kEthOffset) { return; }
        const auto eth_id = static_cast<uint8_t>(msg.id - kEthOffset);

        if (eth_id != eth::protocol::kStatusMessageId)
        {
            if (status_received->load())
            {
                ++stats->eth_messages;
                on_eth_message();
            }
            return;
        }

        const rlp::ByteView payload(msg.payload.data(), msg.payload.size());
        auto decoded = eth::protocol::decode_status(payload);
        if (!decoded)
        {
            status_timeout->cancel();
            (void)session->disconnect(rlpx::DisconnectReason::kSubprotocolError);
            return;
        }
        auto valid = eth::protocol::validate_status(decoded.value(), network_id, genesis);
        if (!valid)
        {
            SPDLOG_LOGGER_DEBUG(gh_log, "ETH Status validation failed: {}",
                                static_cast<int>(valid.error()));
            ++stats->wrong_chain;
            status_timeout->cancel();
            (void)session->disconnect(rlpx::DisconnectReason::kSubprotocolError);
            return;
        }
        ++stats->connected;
        status_received->store(true);
        status_timeout->cancel();
        on_connected(session);
    });

    boost::system::error_code hs_ec;
    status_timeout->async_wait(boost::asio::redirect_error(yield, hs_ec));

    if (!status_received->load())
    {
        if (hs_ec)  // timer was cancelled — peer disconnected us before Status
        {
            const auto reason = static_cast<rlpx::DisconnectReason>(disconnect_reason->load());
            if (reason == rlpx::DisconnectReason::kTooManyPeers)
            {
                ++stats->too_many_peers;
            }
            else
            {
                ++stats->connect_failed;
            }
        }
        else  // timer fired naturally — no Status received within timeout
        {
            ++stats->status_timeout;
        }
        (void)session->disconnect(rlpx::DisconnectReason::kTimeout);
        on_done();
        return;
    }

    // Stay briefly connected so on_connected can be counted
    boost::system::error_code lt_ec;
    lifetime->async_wait(boost::asio::redirect_error(yield, lt_ec));
    on_done();
}

// ── runtime types and helper functions ────────────────────────────────────────

struct ChainRuntime
{
    ChainTarget                                target{};
    eth::Hash256                               genesis{};
    eth::ForkId                                fork_id{};
    std::shared_ptr<DialStats>                 stats;
    std::shared_ptr<discv4::WatcherPool>       pool;
    std::shared_ptr<discv4::DialScheduler>     scheduler;
    std::shared_ptr<discv4::discv4_client>     dv4;
    std::shared_ptr<std::atomic<int>>          peers_count;
    std::shared_ptr<std::atomic<int>>          chain_peers_loaded;
    std::vector<discv4::ValidatedPeer>         bootstrap_peers;
    std::shared_ptr<std::function<void()>>     maybe_finish;
};

static std::optional<eth::ForkId> load_chain_fork_id(
    const ChainTarget&  target,
    const std::string&  argv0)
{
    const auto loaded_hash = load_fork_hash(target.chain_key, argv0);
    if (!loaded_hash && !target.fork_hash_fallback.has_value())
    {
        return std::nullopt;
    }

    eth::ForkId fork_id{};
    fork_id.fork_hash = loaded_hash.value_or(target.fork_hash_fallback.value());
    fork_id.next_fork = 0;
    return fork_id;
}

static void enqueue_chain_peers(
    const ChainTarget&                                target,
    const std::optional<std::filesystem::path>&       chain_peers_json_file,
    const std::optional<discv4::ChainPeerCacheRefreshResult>& refresh_result,
    const std::shared_ptr<discv4::DialScheduler>&     scheduler,
    const std::shared_ptr<std::atomic<int>>&          chain_peers_loaded)
{
    auto enqueue_loaded_peers =
        [&scheduler, &chain_peers_loaded]
        (std::vector<discv4::ValidatedPeer> chain_peers)
        {
            for (auto& peer : chain_peers)
            {
                if (peer.peer.tcp_port == 0)
                {
                    continue;
                }
                if (!rlpx::crypto::Ecdh::verify_public_key(peer.pubkey))
                {
                    continue;
                }
                ++(*chain_peers_loaded);
                scheduler->enqueue(std::move(peer));
            }
        };

    if (chain_peers_json_file.has_value())
    {
        enqueue_loaded_peers(discv4::load_chain_peers_from_json(
            target.chain_peer_cache_key,
            *chain_peers_json_file));

        std::cout << "[  INFO    ] [" << target.chain_key << "] loaded "
                  << chain_peers_loaded->load() << " chain peer(s) for '"
                  << target.chain_peer_cache_key << "' from "
                  << chain_peers_json_file->string() << "\n";
        return;
    }

    if (refresh_result.has_value())
    {
        enqueue_loaded_peers(discv4::load_chain_peers_from_json(
            target.chain_peer_cache_key,
            refresh_result->cache_path));

        std::cout << "[  INFO    ] [" << target.chain_key << "] loaded "
                  << chain_peers_loaded->load() << " chain peer(s) for '"
                  << target.chain_peer_cache_key << "' from "
                  << refresh_result->cache_path.string() << "\n";
    }
}

static void seed_bootnodes(
    boost::asio::io_context&                         io,
    const std::shared_ptr<discv4::discv4_client>&   dv4,
    const ChainTarget&                               target,
    const std::vector<discv4::ValidatedPeer>&        bootstrap_peers)
{
    if (!bootstrap_peers.empty())
    {
        for (const auto& bootnode_peer : bootstrap_peers)
        {
            const std::string host_copy = bootnode_peer.peer.ip;
            const uint16_t port_copy = bootnode_peer.peer.udp_port;
            const discv4::NodeId bn_id = bootnode_peer.peer.node_id;
            boost::asio::spawn(io,
                [dv4, host_copy, port_copy, bn_id](boost::asio::yield_context yc)
                {
                    (void)dv4->find_node(host_copy, port_copy, bn_id, yc);
                });
        }
        return;
    }

    if (target.bootnodes == nullptr)
    {
        return;
    }

    for (const auto& enode : *target.bootnodes)
    {
        const auto bootnode_peer = discv4::make_validated_peer_from_enode(enode);
        if (!bootnode_peer)
        {
            continue;
        }

        const std::string host_copy = bootnode_peer->peer.ip;
        const uint16_t port_copy = bootnode_peer->peer.udp_port;
        const discv4::NodeId bn_id = bootnode_peer->peer.node_id;
        boost::asio::spawn(io,
            [dv4, host_copy, port_copy, bn_id](boost::asio::yield_context yc)
            {
                (void)dv4->find_node(host_copy, port_copy, bn_id, yc);
            });
    }
}

static std::vector<discv4::ValidatedPeer> load_bootstrap_peers(
    const ChainTarget&                                target,
    const std::optional<std::filesystem::path>&       chain_peers_json_file,
    const std::optional<discv4::ChainPeerCacheRefreshResult>& refresh_result)
{
    if (chain_peers_json_file.has_value())
    {
        return discv4::load_bootstrap_peers_from_json(
            target.chain_peer_cache_key,
            *chain_peers_json_file);
    }

    if (refresh_result.has_value())
    {
        return discv4::load_bootstrap_peers_from_json(
            target.chain_peer_cache_key,
            refresh_result->cache_path);
    }

    return {};
}

static std::optional<ChainRuntime> create_chain_runtime(
    boost::asio::io_context&                             io,
    const ChainTarget&                                   target,
    int                                                  max_dials,
    int                                                  min_connections,
    const std::optional<std::filesystem::path>&          chain_peers_json_file,
    const std::optional<discv4::ChainPeerCacheRefreshResult>& refresh_result,
    const rlpx::crypto::Ecdh::KeyPair&                   keypair,
    boost::asio::steady_timer&                           deadline,
    const std::string&                                   argv0)
{
    const auto fork_id = load_chain_fork_id(target, argv0);
    if (!fork_id)
    {
        std::cout << "[  WARN    ] [" << target.chain_key << "] missing fork hash configuration\n";
        return std::nullopt;
    }

    ChainRuntime runtime{};
    runtime.target = target;
    runtime.genesis = hash256_from_hex(target.genesis_hex);
    runtime.fork_id = *fork_id;
    runtime.stats = std::make_shared<DialStats>();
    runtime.pool = std::make_shared<discv4::WatcherPool>(50, max_dials * 2);
    runtime.peers_count = std::make_shared<std::atomic<int>>(0);
    runtime.chain_peers_loaded = std::make_shared<std::atomic<int>>(0);
    runtime.maybe_finish = std::make_shared<std::function<void()>>();
    runtime.bootstrap_peers = load_bootstrap_peers(target, chain_peers_json_file, refresh_result);

    discv4::discv4Config dv4_cfg;
    dv4_cfg.bind_port = 0;
    std::copy(keypair.private_key.begin(), keypair.private_key.end(), dv4_cfg.private_key.begin());
    std::copy(keypair.public_key.begin(), keypair.public_key.end(), dv4_cfg.public_key.begin());
    runtime.dv4 = std::make_shared<discv4::discv4_client>(io, dv4_cfg);

    auto sched_ref = std::make_shared<discv4::DialScheduler*>(nullptr);
    runtime.scheduler = std::make_shared<discv4::DialScheduler>(io, runtime.pool,
        [stats = runtime.stats, fork_id_value = runtime.fork_id, genesis = runtime.genesis,
         network_id = runtime.target.network_id, maybe_finish = runtime.maybe_finish]
        (discv4::ValidatedPeer                                      vp,
         std::function<void()>                                      on_done,
         std::function<void(std::shared_ptr<rlpx::RlpxSession>)>   on_connected,
         boost::asio::yield_context                                 yc) mutable
        {
            dial_connect_only(vp, std::move(on_done),
                [on_connected, maybe_finish]
                (std::shared_ptr<rlpx::RlpxSession> s) mutable
                {
                    on_connected(s);
                    (*maybe_finish)();
                },
                [maybe_finish]()
                {
                    (*maybe_finish)();
                },
                yc, stats, fork_id_value, genesis, network_id);
        });
    *sched_ref = runtime.scheduler.get();

    *runtime.maybe_finish = [&deadline]()
    {
        deadline.cancel();
    };

    // Do not pre-filter discovery/chain peer candidates by ENR fork id.
    // Chain validation is performed after connect via ETH Status.
    runtime.scheduler->filter_fn = {};

    runtime.dv4->set_peer_discovered_callback(
        [scheduler = runtime.scheduler, peers_count = runtime.peers_count](const discv4::DiscoveredPeer& peer)
        {
            discv4::ValidatedPeer vp;
            vp.peer = peer;
            std::copy(peer.node_id.begin(), peer.node_id.end(), vp.pubkey.begin());
            if (!rlpx::crypto::Ecdh::verify_public_key(vp.pubkey))
            {
                return;
            }
            ++(*peers_count);
            scheduler->enqueue(std::move(vp));
        });

    runtime.dv4->set_error_callback([](const std::string&) {});

    enqueue_chain_peers(
        runtime.target,
        chain_peers_json_file,
        refresh_result,
        runtime.scheduler,
        runtime.chain_peers_loaded);

    return runtime;
}

// ── Test framework ────────────────────────────────────────────────────────────

namespace {

struct TestSuite
{
    int run = 0, passed = 0, failed = 0;
    std::string current;

    void start(const std::string& name)
    {
        current = name;
        ++run;
        std::cout << "[ RUN      ] " << name << "\n";
    }
    void pass(const std::string& detail = "")
    {
        ++passed;
        std::cout << "[       OK ] " << current << "\n";
        if (!detail.empty()) std::cout << "            " << detail << "\n";
    }
    void fail(const std::string& detail = "")
    {
        ++failed;
        std::cout << "[  FAILED  ] " << current << "\n";
        if (!detail.empty()) std::cout << "            " << detail << "\n";
    }
    void header(int n)
    {
        std::cout << "\n[==========] DiscoveryTest (" << n << " checks)\n\n";
    }
    void footer()
    {
        std::cout << "\n[==========] " << run << " check(s)\n";
        std::cout << "[  PASSED  ] " << passed << "\n";
        if (failed) std::cout << "[  FAILED  ] " << failed << "\n";
        std::cout << "\n";
    }
};

} // namespace

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char** argv)
{
    int timeout_secs    = 30;
    int min_connections = 1;
    int max_dials       = 16;  // target dialed peers (go-ethereum: MaxPeers/dialRatio = 50/3 ≈ 16)
                               // active concurrent attempts = min(target*2, 50) per go-ethereum's freeDialSlots()
    std::string chain_peers_json_path;
    std::string chain_peers_url = kChainPeersUrlDefault;
    bool chain_peers_url_enabled = true;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--log-level" && i + 1 < argc)
        {
            std::string_view lvl(argv[++i]);
            if (lvl == "debug")    spdlog::set_level(spdlog::level::debug);
            else if (lvl == "info") spdlog::set_level(spdlog::level::info);
            else if (lvl == "warn") spdlog::set_level(spdlog::level::warn);
            else if (lvl == "off")  spdlog::set_level(spdlog::level::off);
        }
        else if (arg == "--timeout" && i + 1 < argc)   { timeout_secs    = std::atoi(argv[++i]); }
        else if (arg == "--connections" && i + 1 < argc){ min_connections = std::atoi(argv[++i]); }
        else if (arg == "--dials" && i + 1 < argc)      { max_dials       = std::atoi(argv[++i]); }
        else if ((arg == "--chain-peers-json" || arg == "--bootstrap-peers-json") && i + 1 < argc)
        {
            chain_peers_json_path = argv[++i];
        }
        else if ((arg == "--chain-peers-url" || arg == "--bootstrap-peers-url") && i + 1 < argc)
        {
            chain_peers_url = argv[++i];
            chain_peers_url_enabled = true;
        }
        else if (arg == "--no-chain-peers-url" || arg == "--no-bootstrap-peers-url")
        {
            chain_peers_url_enabled = false;
        }
    }

    // ── Fork hash — loaded from chains.json, fallback to compiled-in value ──────
    const auto loaded_hash = load_fork_hash(kForkHashChainKey, argv[0]);
    if ( !loaded_hash )
    {
        std::cout << "[  WARN    ] chains.json not found or missing 'mainnet' key — "
                     "using compiled-in fallback hash.\n";
    }
    const eth::ForkId mainnet_fork_id{
        loaded_hash.value_or(kMainnetForkHashFallback),
        0
    };

    TestSuite suite;
    const auto chain_targets = all_chain_targets();
    suite.header(static_cast<int>(chain_targets.size()));

    boost::asio::io_context io;

    // Shared result counters (written only from the single io_context thread)
    auto chain_runtimes = std::vector<ChainRuntime>{};

    // ── discv4 setup ─────────────────────────────────────────────────────────
    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (!keypair_result)
    {
        std::cout << "Failed to generate keypair\n";
        return 1;
    }
    const auto& keypair = keypair_result.value();

    // ── Overall test timeout ─────────────────────────────────────────────────
    boost::asio::steady_timer deadline(io, std::chrono::seconds(timeout_secs));

    std::optional<discv4::ChainPeerCacheRefreshResult> refresh_result;
    if (chain_peers_json_path.empty() && chain_peers_url_enabled)
    {
        refresh_result = discv4::refresh_chain_peer_cache_json(
            discv4::chain_peer_cache_json_path(argv[0]),
            chain_peers_url);
        if (refresh_result.has_value())
        {
            std::cout << "[  INFO    ] chain peer cache refresh: "
                      << (refresh_result->cache_updated ? "updated" :
                          (refresh_result->cache_available ? "unchanged" : "unavailable"))
                      << " " << refresh_result->cache_path.string() << "\n";
        }
    }

    const auto chain_peers_json_file =
        discv4::find_chain_peer_cache_json_path(argv[0], chain_peers_json_path);
    if (!chain_peers_json_path.empty() && !chain_peers_json_file.has_value())
    {
        std::cout << "Chain peer cache file not found: " << chain_peers_json_path << "\n";
        return 1;
    }
    if (chain_peers_json_file.has_value())
    {
        std::cout << "[  INFO    ] chain peer cache path: "
                  << chain_peers_json_file->string() << "\n";
    }

    for (const auto& target : chain_targets)
    {
        auto runtime = create_chain_runtime(
            io,
            target,
            max_dials,
            min_connections,
            chain_peers_json_file,
            refresh_result,
            keypair,
            deadline,
            argv[0]);
        if (runtime.has_value())
        {
            chain_runtimes.push_back(std::move(*runtime));
        }
    }

    if (chain_runtimes.empty())
    {
        std::cout << "No chain runtimes configured\n";
        return 1;
    }

    deadline.async_wait([&](boost::system::error_code) {
        for (auto& runtime : chain_runtimes)
        {
            runtime.scheduler->stop();
            runtime.dv4->stop();
        }
        io.stop();
    });

    // ── Signal handler ───────────────────────────────────────────────────────
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int) {
        deadline.cancel();
        for (auto& runtime : chain_runtimes)
        {
            runtime.scheduler->stop();
            runtime.dv4->stop();
        }
        io.stop();
    });

    for (auto& runtime : chain_runtimes)
    {
        const auto start_result = runtime.dv4->start();
        if (!start_result)
        {
            std::cout << "Failed to start discv4 for chain " << runtime.target.chain_key << "\n";
            return 1;
        }

        seed_bootnodes(io, runtime.dv4, runtime.target, runtime.bootstrap_peers);
    }

    io.run();

    for (auto& runtime : chain_runtimes)
    {
        std::cout << "\n[  STATS   ] [" << runtime.target.chain_key << "] Dial breakdown:\n"
                  << "              chain peers loaded:       " << runtime.chain_peers_loaded->load() << "\n"
                  << "              discovered peers:             " << runtime.peers_count->load() << "\n"
                  << "              dialed:                       " << runtime.stats->dialed.load() << "\n"
                  << "              connect failed:               " << runtime.stats->connect_failed.load() << "\n"
                  << "              wrong chain:                  " << runtime.stats->wrong_chain.load() << "\n"
                  << "              too many peers:               " << runtime.stats->too_many_peers.load() << "\n"
                  << "              too many peers (right chain): " << runtime.stats->too_many_peers_right_chain.load() << "\n"
                  << "              status timeout:               " << runtime.stats->status_timeout.load() << "\n"
                  << "              connected (right chain):      " << runtime.stats->connected.load() << "\n"
                  << "              eth messages observed:        " << runtime.stats->eth_messages.load() << "\n";

        const int connections = runtime.scheduler->total_validated;

        suite.start(std::string("DiscoveryTest.") + runtime.target.chain_key + ".ChainPeersLoaded");
        if (runtime.chain_peers_loaded->load() > 0)
        {
            suite.pass(std::to_string(runtime.chain_peers_loaded->load()) + " chain peer(s) loaded");
        }
        else
        {
            suite.fail("No chain peers loaded from chain_enodes cache/refresh path");
        }

        std::cout << "[  INFO    ] [" << runtime.target.chain_key << "] active ETH Status connection(s): "
                  << connections << "\n";
    }

    // std::exit bypasses stack-variable destructors (including io_context), which avoids
    // boost::coroutines::detail::forced_unwind being thrown during io cleanup when
    // active coroutines are present at shutdown (TCP connect, etc.).
    std::cout.flush();
    std::exit(suite.failed > 0 ? 1 : 0);
}
