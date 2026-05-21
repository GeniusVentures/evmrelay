// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// examples/discovery/test_discv5_connect.cpp
//
// Live functional harness:
//   discv5 discovery (Sepolia) -> RLPx ETH Status validation.
//
// Pass criterion:
//   connected (right chain) >= --connections (default: 3)
//
// Usage:
//   ./test_discv5_connect [--log-level debug] [--timeout 180] [--connections 3] [--dials 16]

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>

#include <discv4/bootnodes_test.hpp>
#include <discv4/dial_scheduler.hpp>
#include <discv5/discv5_bootnodes.hpp>
#include <discv5/discv5_client.hpp>
#include <discv5/discv5_enr.hpp>
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

static constexpr uint64_t kSepoliaNetworkId = 11155111;
static constexpr uint8_t  kEthOffset        = 0x10;
static constexpr uint16_t kSepoliaRlpxPort  = 30303;

static std::string pubkey_to_hex(const rlpx::PublicKey& pubkey)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(pubkey.size() * 2U);
    for (const uint8_t byte : pubkey)
    {
        out.push_back(kHex[(byte >> 4U) & 0x0FU]);
        out.push_back(kHex[byte & 0x0FU]);
    }
    return out;
}

static eth::Hash256 sepolia_genesis()
{
    eth::Hash256 h{};
    const char* hex = "25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9";
    for (size_t i = 0; i < 32; ++i)
    {
        auto nibble = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
            if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(10 + c - 'a'); }
            return 0U;
        };
        h[i] = static_cast<uint8_t>((nibble(hex[i * 2U]) << 4U) | nibble(hex[i * 2U + 1U]));
    }
    return h;
}

// Sepolia post-BPO2 fallback hash — used only when chain_enodes.json(.gz) is not found.
static const std::array<uint8_t, 4U> kSepoliaForkHashFallback{ 0x26, 0x89, 0x56, 0xb6 };

struct DialStats
{
    std::atomic<int> dialed{0};
    std::atomic<int> connect_failed{0};
    std::atomic<int> wrong_chain{0};
    std::atomic<int> status_timeout{0};
    std::atomic<int> too_many_peers{0};
    std::atomic<int> connected{0};
    std::atomic<int> connected_seeded{0};
    std::atomic<int> connected_discv5{0};
    std::atomic<int> filtered_bad_peers{0};
};

struct QualityFilterState
{
    std::unordered_map<std::string, int> fail_counts{};
    std::unordered_map<std::string, int> ip_fail_counts{};
    std::unordered_map<uint16_t, int> port_fail_counts{};
    std::unordered_map<std::string, int> subnet_enqueue_counts{};
    std::unordered_set<std::string> blocked_pubkeys{};
    std::unordered_set<std::string> blocked_ips{};
    std::unordered_set<uint16_t> blocked_ports{};
    int block_threshold{2};
    int ip_block_threshold{3};
    int port_block_threshold{4};
    int subnet_enqueue_limit{2};
};

static bool is_publicly_routable_ip(const std::string& ip)
{
    boost::system::error_code ec;
    const auto addr = boost::asio::ip::make_address(ip, ec);
    if (ec)
    {
        return false;
    }

    if (addr.is_v4())
    {
        const auto b = addr.to_v4().to_bytes();
        if (b[0] == 0U || b[0] == 10U || b[0] == 127U)
        {
            return false;
        }
        if (b[0] == 169U && b[1] == 254U)
        {
            return false;
        }
        if (b[0] == 172U && b[1] >= 16U && b[1] <= 31U)
        {
            return false;
        }
        if (b[0] == 192U && b[1] == 168U)
        {
            return false;
        }
        if (b[0] >= 224U)
        {
            return false;
        }
        return true;
    }

    const auto v6 = addr.to_v6();
    return !v6.is_loopback() && !v6.is_link_local() && !v6.is_multicast() && !v6.is_unspecified();
}

static std::optional<std::string> subnet_key_v4_24(const std::string& ip)
{
    boost::system::error_code ec;
    const auto addr = boost::asio::ip::make_address(ip, ec);
    if (ec || !addr.is_v4())
    {
        return std::nullopt;
    }

    const auto b = addr.to_v4().to_bytes();
    return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]);
}

static bool is_candidate_blocked(
    const discv4::ValidatedPeer& vp,
    const std::shared_ptr<QualityFilterState>& quality)
{
    const std::string pubkey = pubkey_to_hex(vp.pubkey);
    if (quality->blocked_pubkeys.find(pubkey) != quality->blocked_pubkeys.end())
    {
        return true;
    }

    if (quality->blocked_ips.find(vp.peer.ip) != quality->blocked_ips.end())
    {
        return true;
    }

    if (quality->blocked_ports.find(vp.peer.tcp_port) != quality->blocked_ports.end())
    {
        return true;
    }

    return false;
}

static void dial_connect_only(
    discv4::ValidatedPeer vp,
    std::function<void(rlpx::DisconnectReason)> on_done,
    std::function<void(std::shared_ptr<rlpx::RlpxSession>)> on_connected,
    boost::asio::yield_context yield,
    std::shared_ptr<DialStats> stats,
    std::shared_ptr<QualityFilterState> quality,
    eth::ForkId fork_id)
{
    ++stats->dialed;

    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (!keypair_result)
    {
        ++stats->connect_failed;
        on_done(rlpx::DisconnectReason::kTcpError);
        return;
    }
    const auto& keypair = keypair_result.value();

    const rlpx::SessionConnectParams params{
        vp.peer.ip,
        vp.peer.tcp_port,
        keypair.public_key,
        keypair.private_key,
        vp.pubkey,
        "rlp-test-discv5-connect",
        0
    };

    auto session_result = rlpx::RlpxSession::connect(params, yield);
    if (!session_result)
    {
        const std::string key = pubkey_to_hex(vp.pubkey);
        const int fails = ++quality->fail_counts[key];
        if (fails >= quality->block_threshold)
        {
            quality->blocked_pubkeys.insert(key);
        }

        const int ip_fails = ++quality->ip_fail_counts[vp.peer.ip];
        if (ip_fails >= quality->ip_block_threshold)
        {
            quality->blocked_ips.insert(vp.peer.ip);
        }

        const int port_fails = ++quality->port_fail_counts[vp.peer.tcp_port];
        if (port_fails >= quality->port_block_threshold)
        {
            quality->blocked_ports.insert(vp.peer.tcp_port);
        }

        ++stats->connect_failed;
        on_done(rlpx::DisconnectReason::kTcpError);
        return;
    }
    auto session = std::move(session_result.value());

    {
        const eth::Hash256 genesis = sepolia_genesis();
        eth::StatusMessage69 status69{
            69,
            kSepoliaNetworkId,
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

    auto executor = yield.get_executor();
    auto status_received = std::make_shared<std::atomic<bool>>(false);
    auto status_timeout = std::make_shared<boost::asio::steady_timer>(executor);
    auto lifetime = std::make_shared<boost::asio::steady_timer>(executor);
    auto disconnect_reason = std::make_shared<std::atomic<int>>(
        static_cast<int>(rlpx::DisconnectReason::kRequested));
    status_timeout->expires_after(eth::protocol::kStatusHandshakeTimeout);
    lifetime->expires_after(std::chrono::seconds(10));

    session->set_disconnect_handler(
        [lifetime, status_timeout, disconnect_reason](const rlpx::protocol::DisconnectMessage& msg)
        {
            disconnect_reason->store(static_cast<int>(msg.reason));
            lifetime->cancel();
            status_timeout->cancel();
        });

    session->set_ping_handler([session](const rlpx::protocol::PingMessage&)
    {
        const rlpx::protocol::PongMessage pong;
        auto encoded = pong.encode();
        if (!encoded)
        {
            return;
        }

        (void)session->post_message(rlpx::framing::Message{
            rlpx::kPongMessageId,
            std::move(encoded.value())
        });
    });

    const eth::Hash256 genesis = sepolia_genesis();
    session->set_generic_handler([session, status_received, status_timeout,
                                  on_connected, genesis, stats](const rlpx::protocol::Message& msg)
    {
        if (msg.id < kEthOffset)
        {
            return;
        }

        const auto eth_id = static_cast<uint8_t>(msg.id - kEthOffset);
        if (eth_id != eth::protocol::kStatusMessageId)
        {
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

        auto valid = eth::protocol::validate_status(decoded.value(), kSepoliaNetworkId, genesis);
        if (!valid)
        {
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
        if (hs_ec)
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
        else
        {
            ++stats->status_timeout;
        }

        (void)session->disconnect(rlpx::DisconnectReason::kTimeout);
        on_done(rlpx::DisconnectReason::kTcpError);
        return;
    }

    boost::system::error_code lt_ec;
    lifetime->async_wait(boost::asio::redirect_error(yield, lt_ec));
    on_done(rlpx::DisconnectReason::kTcpError);
}

int main(int argc, char** argv)
{
    int timeout_secs = 180;
    int min_connections = 3;
    int max_dials = 16;
    bool enable_seeded = false;
    bool require_fork = true;
    bool enqueue_bootstrap_candidates = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--log-level" && i + 1 < argc)
        {
            std::string_view lvl(argv[++i]);
            if (lvl == "trace") { spdlog::set_level(spdlog::level::trace); }
            else if (lvl == "debug") { spdlog::set_level(spdlog::level::debug); }
            else if (lvl == "info") { spdlog::set_level(spdlog::level::info); }
            else if (lvl == "warn") { spdlog::set_level(spdlog::level::warn); }
            else if (lvl == "off") { spdlog::set_level(spdlog::level::off); }
        }
        else if (arg == "--timeout" && i + 1 < argc)
        {
            timeout_secs = std::atoi(argv[++i]);
        }
        else if (arg == "--connections" && i + 1 < argc)
        {
            min_connections = std::atoi(argv[++i]);
        }
        else if (arg == "--dials" && i + 1 < argc)
        {
            max_dials = std::atoi(argv[++i]);
        }
        else if (arg == "--seeded" && i + 1 < argc)
        {
            const std::string_view mode(argv[++i]);
            if (mode == "on")
            {
                enable_seeded = true;
            }
            else if (mode == "off")
            {
                enable_seeded = false;
            }
            else
            {
                std::cout << "Invalid --seeded value (use on|off)\n";
                return 1;
            }
        }
        else if (arg == "--require-fork" && i + 1 < argc)
        {
            const std::string_view mode(argv[++i]);
            if (mode == "on")
            {
                require_fork = true;
            }
            else if (mode == "off")
            {
                require_fork = false;
            }
            else
            {
                std::cout << "Invalid --require-fork value (use on|off)\n";
                return 1;
            }
        }
        else if (arg == "--enqueue-bootstrap-candidates" && i + 1 < argc)
        {
            const std::string_view mode(argv[++i]);
            if (mode == "on")
            {
                enqueue_bootstrap_candidates = true;
            }
            else if (mode == "off")
            {
                enqueue_bootstrap_candidates = false;
            }
            else
            {
                std::cout << "Invalid --enqueue-bootstrap-candidates value (use on|off)\n";
                return 1;
            }
        }
    }

    const auto loaded_hash = load_fork_hash("ethereum-sepolia", argv[0]);
    if (!loaded_hash)
    {
        std::cout << "[  WARN    ] chain_enodes.json(.gz) not found or missing 'ethereum-sepolia' forkId — using compiled-in fallback hash.\n";
    }

    const std::array<uint8_t, 4U> sepolia_hash = loaded_hash.value_or(kSepoliaForkHashFallback);
    const eth::ForkId sepolia_fork_id{ sepolia_hash, 0U };
    const discovery::ForkId sepolia_discovery_fork_id{ sepolia_hash, 0U };

    boost::asio::io_context io;

    std::atomic<int> discovered_peers{0};
    std::atomic<int> discovered_candidates{0};
    auto stats = std::make_shared<DialStats>();

    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (!keypair_result)
    {
        std::cout << "Failed to generate keypair\n";
        return 1;
    }

    discv5::discv5Config dv5_cfg;
    dv5_cfg.bind_port = 0;
    dv5_cfg.query_interval_sec = 10U;
    if (require_fork)
    {
        dv5_cfg.required_fork_id = sepolia_discovery_fork_id;
    }
    std::copy(
        keypair_result.value().private_key.begin(),
        keypair_result.value().private_key.end(),
        dv5_cfg.private_key.begin());
    std::copy(
        keypair_result.value().public_key.begin(),
        keypair_result.value().public_key.end(),
        dv5_cfg.public_key.begin());

    auto source = discv5::ChainBootnodeRegistry::for_chain(discv5::ChainId::kEthereumSepolia);
    if (!source)
    {
        std::cout << "Failed to load Sepolia discv5 bootnode source\n";
        return 1;
    }

    dv5_cfg.bootstrap_enrs = source->fetch();
    if (dv5_cfg.bootstrap_enrs.empty())
    {
        std::cout << "No discv5 Sepolia bootnodes configured\n";
        return 1;
    }

    auto dv5 = std::make_shared<discv5::discv5_client>(io, dv5_cfg);

    const int kMaxActiveDials = 50;
    auto pool = std::make_shared<discv4::WatcherPool>(kMaxActiveDials, max_dials * 2);
    auto sched_ref = std::make_shared<discv4::DialScheduler*>(nullptr);
    auto seeded_pubkeys = std::make_shared<std::unordered_set<std::string>>();
    auto quality = std::make_shared<QualityFilterState>();

    boost::asio::steady_timer deadline(io, std::chrono::seconds(timeout_secs));

    auto scheduler = std::make_shared<discv4::DialScheduler>(
        io,
        pool,
        [&io, &deadline, min_connections, sched_ref, stats, seeded_pubkeys, quality, sepolia_fork_id]
        (discv4::ValidatedPeer vp,
         std::function<void(rlpx::DisconnectReason)> on_done,
         std::function<void(std::shared_ptr<rlpx::RlpxSession>)> on_connected,
         boost::asio::yield_context yc) mutable
        {
            dial_connect_only(
                vp,
                std::move(on_done),
                [on_connected, &io, &deadline, min_connections, sched_ref, stats, seeded_pubkeys]
                (std::shared_ptr<rlpx::RlpxSession> s) mutable
                {
                    const std::string remote_pubkey_hex = pubkey_to_hex(s->peer_info().public_key);
                    if (seeded_pubkeys->find(remote_pubkey_hex) != seeded_pubkeys->end())
                    {
                        ++stats->connected_seeded;
                    }
                    else
                    {
                        ++stats->connected_discv5;
                    }
                    on_connected(s);
                    if (*sched_ref && (*sched_ref)->total_validated >= min_connections)
                    {
                        deadline.cancel();
                        io.stop();
                    }
                },
                yc,
                stats,
                quality,
                sepolia_fork_id);
        });
    *sched_ref = scheduler.get();

    auto parse_enode = [](const std::string& enode)
        -> std::optional<std::tuple<std::string, uint16_t, std::string>>
    {
        const std::string prefix = "enode://";
        if (enode.substr(0, prefix.size()) != prefix)
        {
            return std::nullopt;
        }
        const auto at = enode.find('@', prefix.size());
        if (at == std::string::npos)
        {
            return std::nullopt;
        }
        const auto colon = enode.rfind(':');
        if (colon == std::string::npos || colon < at)
        {
            return std::nullopt;
        }

        std::string pubkey = enode.substr(prefix.size(), at - prefix.size());
        std::string host = enode.substr(at + 1, colon - at - 1);
        uint16_t port = static_cast<uint16_t>(std::stoi(enode.substr(colon + 1)));
        return std::make_tuple(host, port, pubkey);
    };

    auto hex_to_nibble = [](char c) -> std::optional<uint8_t>
    {
        if (c >= '0' && c <= '9') { return static_cast<uint8_t>(c - '0'); }
        if (c >= 'a' && c <= 'f') { return static_cast<uint8_t>(10 + c - 'a'); }
        if (c >= 'A' && c <= 'F') { return static_cast<uint8_t>(10 + c - 'A'); }
        return std::nullopt;
    };

    if (enable_seeded)
    {
        for (const auto& enode : ETHEREUM_SEPOLIA_BOOTNODES)
        {
            auto parsed = parse_enode(enode);
            if (!parsed)
            {
                continue;
            }

            const auto& [host, port, pubkey_hex] = *parsed;
            if (port != kSepoliaRlpxPort || pubkey_hex.size() != 128U)
            {
                continue;
            }

            discv4::ValidatedPeer vp;
            vp.peer.ip = host;
            vp.peer.udp_port = port;
            vp.peer.tcp_port = port;
            vp.peer.last_seen = std::chrono::steady_clock::now();

            bool ok = true;
            for (size_t i = 0; i < vp.pubkey.size() && ok; ++i)
            {
                auto hi = hex_to_nibble(pubkey_hex[i * 2U]);
                auto lo = hex_to_nibble(pubkey_hex[i * 2U + 1U]);
                if (!hi || !lo)
                {
                    ok = false;
                    break;
                }
                vp.pubkey[i] = static_cast<uint8_t>((*hi << 4U) | *lo);
                vp.peer.node_id[i] = vp.pubkey[i];
            }

            if (!ok || !rlpx::crypto::Ecdh::verify_public_key(vp.pubkey))
            {
                continue;
            }

            if (is_candidate_blocked(vp, quality))
            {
                ++stats->filtered_bad_peers;
                continue;
            }

            seeded_pubkeys->insert(pubkey_to_hex(vp.pubkey));
            scheduler->enqueue(std::move(vp));
        }
    }

    dv5->set_peer_discovered_callback(
        [scheduler, stats, quality, &discovered_peers, &discovered_candidates, sepolia_hash, require_fork]
        (const discovery::ValidatedPeer& peer)
        {
            ++discovered_candidates;

            if (peer.tcp_port == 0)
            {
                return;
            }

            if (require_fork && (!peer.eth_fork_id.has_value() || peer.eth_fork_id.value().hash != sepolia_hash))
            {
                return;
            }

            discv4::ValidatedPeer vp;
            vp.peer.node_id = peer.node_id;
            vp.peer.ip = peer.ip;
            vp.peer.udp_port = peer.udp_port;
            vp.peer.tcp_port = peer.tcp_port;
            vp.peer.last_seen = peer.last_seen;
            if (peer.eth_fork_id.has_value())
            {
                vp.peer.eth_fork_id = discv4::ForkId{
                    peer.eth_fork_id.value().hash,
                    peer.eth_fork_id.value().next
                };
            }

            vp.pubkey = peer.node_id;
            if (!rlpx::crypto::Ecdh::verify_public_key(vp.pubkey))
            {
                return;
            }

            if (!is_publicly_routable_ip(vp.peer.ip))
            {
                ++stats->filtered_bad_peers;
                return;
            }

            if (is_candidate_blocked(vp, quality))
            {
                ++stats->filtered_bad_peers;
                return;
            }

            const auto subnet_key = subnet_key_v4_24(vp.peer.ip);
            if (subnet_key.has_value())
            {
                const int queued = quality->subnet_enqueue_counts[subnet_key.value()];
                if (queued >= quality->subnet_enqueue_limit)
                {
                    ++stats->filtered_bad_peers;
                    return;
                }
                ++quality->subnet_enqueue_counts[subnet_key.value()];
            }

            ++discovered_peers;
            scheduler->enqueue(std::move(vp));
        });

    dv5->set_error_callback([](const std::string& msg)
    {
        std::cout << "discv5 error: " << msg << "\n";
    });

    if (enqueue_bootstrap_candidates)
    {
        for (const auto& enr_uri : dv5_cfg.bootstrap_enrs)
        {
            auto record_result = discv5::EnrParser::parse(enr_uri);
            if (!record_result)
            {
                continue;
            }

            auto peer_result = discv5::EnrParser::to_validated_peer(record_result.value());
            if (!peer_result)
            {
                continue;
            }

            if (peer_result.value().tcp_port == 0)
            {
                continue;
            }

            discv4::ValidatedPeer vp;
            vp.peer.node_id = peer_result.value().node_id;
            vp.peer.ip = peer_result.value().ip;
            vp.peer.udp_port = peer_result.value().udp_port;
            vp.peer.tcp_port = peer_result.value().tcp_port;
            vp.peer.last_seen = peer_result.value().last_seen;
            if (peer_result.value().eth_fork_id.has_value())
            {
                vp.peer.eth_fork_id = discv4::ForkId{
                    peer_result.value().eth_fork_id.value().hash,
                    peer_result.value().eth_fork_id.value().next
                };
            }

            vp.pubkey = peer_result.value().node_id;
            if (!rlpx::crypto::Ecdh::verify_public_key(vp.pubkey))
            {
                continue;
            }

            if (!is_publicly_routable_ip(vp.peer.ip))
            {
                ++stats->filtered_bad_peers;
                continue;
            }

            if (is_candidate_blocked(vp, quality))
            {
                ++stats->filtered_bad_peers;
                continue;
            }

            const auto subnet_key = subnet_key_v4_24(vp.peer.ip);
            if (subnet_key.has_value())
            {
                const int queued = quality->subnet_enqueue_counts[subnet_key.value()];
                if (queued >= quality->subnet_enqueue_limit)
                {
                    ++stats->filtered_bad_peers;
                    continue;
                }
                ++quality->subnet_enqueue_counts[subnet_key.value()];
            }

            scheduler->enqueue(std::move(vp));
        }
    }

    deadline.async_wait([&](boost::system::error_code)
    {
        scheduler->stop();
        dv5->stop();
        io.stop();
    });

    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code, int)
    {
        deadline.cancel();
        scheduler->stop();
        dv5->stop();
        io.stop();
    });

    const auto start_result = dv5->start();
    if (!start_result)
    {
        std::cout << "Failed to start discv5 client\n";
        return 1;
    }

    std::cout << "Running discv5 Sepolia discovery + connect harness (seeded="
              << (enable_seeded ? "on" : "off")
              << ", require-fork=" << (require_fork ? "on" : "off")
              << ", enqueue-bootstrap-candidates=" << (enqueue_bootstrap_candidates ? "on" : "off")
              << ")...\n";
    io.run();

    std::cout << "\n[  STATS   ] Dial breakdown:\n"
              << "              dialed:                  " << stats->dialed.load() << "\n"
              << "              connect failed:          " << stats->connect_failed.load() << "\n"
              << "              wrong chain:             " << stats->wrong_chain.load() << "\n"
              << "              too many peers:          " << stats->too_many_peers.load() << "\n"
              << "              status timeout:          " << stats->status_timeout.load() << "\n"
              << "              connected (right chain): " << stats->connected.load() << "\n"
              << "              connected (seeded):      " << stats->connected_seeded.load() << "\n"
              << "              connected (discv5):      " << stats->connected_discv5.load() << "\n"
              << "              filtered bad peers:      " << stats->filtered_bad_peers.load() << "\n"
              << "              candidates seen:         " << discovered_candidates.load() << "\n"
              << "              discovered peers:        " << discovered_peers.load() << "\n";

    const int connections = scheduler->total_validated;
    if (connections >= min_connections)
    {
        std::cout << "\n[       OK ] Discv5ConnectHarness.ActiveSepoliaConnections\n"
                  << "            " << connections << " active Sepolia ETH Status connection(s) confirmed\n\n";
        std::cout.flush();
        std::exit(0);
    }

    std::cout << "\n[  FAILED  ] Discv5ConnectHarness.ActiveSepoliaConnections\n"
              << "            Only " << connections << "/" << min_connections
              << " Sepolia connection(s) — run with --log-level debug for details\n\n";
    std::cout.flush();
    std::exit(1);
}
