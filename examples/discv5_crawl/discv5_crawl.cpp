// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// discv5_crawl — live discv5 peer discovery example.
//
// Usage:
//   discv5_crawl [options]
//
// Options:
//   --chain <name>         Canonical chain key from chain_enodes.json(.gz).
//   --bootnode-enr <uri>   Add an extra bootstrap ENR ("enr:…") or
//                          enode ("enode://…") URI.  May be repeated.
//   --port <udp-port>      Local UDP bind port.  Default: 9000.
//   --timeout <secs>       Stop after this many seconds.  Default: 60.
//   --log-level <level>    spdlog level (trace/debug/info/warn/error).
//                          Default: info.
//
// The binary starts a discv5_client, seeds it from data files plus any explicit
// --bootnode-enr flags, and runs until the timeout expires.  It reports the
// final CrawlerStats to stdout.
//
// This is an opt-in live test — it requires network access and is NOT
// wired into the CTest suite.

#include <boost/asio/spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/redirect_error.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../chain_config.hpp"
#include <discv5/enr_tree.hpp>
#include <discv5/discv5_client.hpp>
#include <discv5/discv5_constants.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <base/rlp-logger.hpp>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

inline constexpr const char* kDefaultChainPeersUrl = "https://enodes.gnus.ai/chain_enodes.json.gz";

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------

struct CliArgs
{
    std::string           chain            = "ethereum-mainnet";
    std::string           chain_peers_json_path;
    std::string           chain_peers_url  = kDefaultChainPeersUrl;
    bool                  chain_peers_url_enabled = true;
    std::vector<std::string> extra_enrs{};
    uint16_t              bind_port        = discv5::kDefaultUdpPort;
    uint32_t              timeout_sec      = 60U;
    std::string           log_level        = "info";
};

void print_usage(const char* argv0)
{
    std::cerr
        << "Usage: " << argv0 << " [options]\n"
        << "\nOptions:\n"
        << "  --chain <name>         Canonical chain key (default: ethereum-mainnet)\n"
        << "  --chain-peers-json <path> Load chain metadata from a local cache\n"
        << "  --chain-peers-url <url>   Override remote chain metadata cache URL\n"
        << "  --no-chain-peers-url      Disable remote metadata cache refresh\n"
        << "  --bootnode-enr <uri>   Add extra ENR or enode URI (may repeat)\n"
        << "  --port <udp-port>      Local UDP bind port (default: " << discv5::kDefaultUdpPort << ")\n"
        << "  --timeout <secs>       Stop after N seconds (default: 60)\n"
        << "  --log-level <level>    trace|debug|info|warn|error (default: info)\n"
        << "  --help                 Show this message\n";
}

std::optional<CliArgs> parse_args(int argc, char** argv)
{
    CliArgs args;

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view flag(argv[i]);

        if (flag == "--help" || flag == "-h")
        {
            print_usage(argv[0]);
            return std::nullopt;
        }

        auto require_next = [&](std::string_view name) -> const char*
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Error: " << name << " requires an argument\n";
                print_usage(argv[0]);
                return nullptr;
            }
            return argv[++i];
        };

        if (flag == "--chain")
        {
            const char* val = require_next("--chain");
            if (!val) { return std::nullopt; }
            args.chain = val;
        }
        else if (flag == "--chain-peers-json" || flag == "--bootstrap-peers-json")
        {
            const char* val = require_next(flag);
            if (!val) { return std::nullopt; }
            args.chain_peers_json_path = val;
        }
        else if (flag == "--chain-peers-url" || flag == "--bootstrap-peers-url")
        {
            const char* val = require_next(flag);
            if (!val) { return std::nullopt; }
            args.chain_peers_url = val;
            args.chain_peers_url_enabled = true;
        }
        else if (flag == "--no-chain-peers-url" || flag == "--no-bootstrap-peers-url")
        {
            args.chain_peers_url_enabled = false;
        }
        else if (flag == "--bootnode-enr")
        {
            const char* val = require_next("--bootnode-enr");
            if (!val) { return std::nullopt; }
            args.extra_enrs.emplace_back(val);
        }
        else if (flag == "--port")
        {
            const char* val = require_next("--port");
            if (!val) { return std::nullopt; }
            args.bind_port = static_cast<uint16_t>(std::stoul(val));
        }
        else if (flag == "--timeout")
        {
            const char* val = require_next("--timeout");
            if (!val) { return std::nullopt; }
            args.timeout_sec = static_cast<uint32_t>(std::stoul(val));
        }
        else if (flag == "--log-level")
        {
            const char* val = require_next("--log-level");
            if (!val) { return std::nullopt; }
            args.log_level = val;
        }
        else
        {
            std::cerr << "Error: unknown option '" << flag << "'\n";
            print_usage(argv[0]);
            return std::nullopt;
        }
    }

    return args;
}

enum class StopReason
{
    kIoStopped,
    kTimeout,
    kSignal,
};

enum class RunStatus
{
    kCallbackEmissionsSeen,
    kSendFailuresOnly,
    kPartialLiveTraffic,
    kMeasuredWithoutReceive,
    kErrorsOnly,
    kNoLiveResponse,
};

const char* to_string(StopReason reason) noexcept
{
    switch (reason)
    {
        case StopReason::kTimeout:
            return "timeout";
        case StopReason::kSignal:
            return "signal";
        case StopReason::kIoStopped:
        default:
            return "io_stopped";
    }
}

const char* to_string(RunStatus status) noexcept
{
    switch (status)
    {
        case RunStatus::kCallbackEmissionsSeen:
            return "callback_emissions_seen";
        case RunStatus::kSendFailuresOnly:
            return "send_failures_only";
        case RunStatus::kPartialLiveTraffic:
            return "partial_live_traffic";
        case RunStatus::kMeasuredWithoutReceive:
            return "measured_without_receive";
        case RunStatus::kErrorsOnly:
            return "errors_only";
        case RunStatus::kNoLiveResponse:
        default:
            return "no_live_response";
    }
}

RunStatus classify_run(
    size_t callback_discoveries,
    size_t callback_errors,
    size_t received_packets,
    size_t send_failures,
    const discv5::CrawlerStats& stats) noexcept
{
    if (callback_discoveries > 0U || stats.discovered > 0U)
    {
        return RunStatus::kCallbackEmissionsSeen;
    }

    if (send_failures > 0U)
    {
        return RunStatus::kSendFailuresOnly;
    }

    if (received_packets > 0U)
    {
        return RunStatus::kPartialLiveTraffic;
    }

    if (stats.measured > 0U)
    {
        return RunStatus::kMeasuredWithoutReceive;
    }

    if (callback_errors > 0U)
    {
        return RunStatus::kErrorsOnly;
    }

    return RunStatus::kNoLiveResponse;
}

const char* interpret_run(
    size_t callback_discoveries,
    size_t callback_errors,
    size_t received_packets,
    size_t whoareyou_packets,
    size_t send_failures,
    const discv5::CrawlerStats& stats) noexcept
{
    if (callback_discoveries > 0U || stats.discovered > 0U)
    {
        return "peers were emitted by the crawler callback path";
    }

    if (send_failures > 0U)
    {
        return "outbound FINDNODE send failures occurred before any discovery callback fired";
    }

    if (whoareyou_packets > 0U)
    {
        return "remote WHOAREYOU challenges were parsed, but outbound handshake and NODES decode are not implemented yet";
    }

    if (received_packets > 0U)
    {
        return "live packets arrived, but receive-side WHOAREYOU/HANDSHAKE/NODES decode is still missing";
    }

    if (stats.measured > 0U)
    {
        return "crawler marked peers as measured, but no inbound packets were classified or emitted";
    }

    if (callback_errors > 0U)
    {
        return "the run reported crawler errors and produced no discoveries";
    }

    return "no observable discv5 traffic reached the current harness during the bounded run";
}

const char* consistency_note(
    size_t callback_discoveries,
    size_t received_packets,
    size_t send_failures,
    const discv5::CrawlerStats& stats) noexcept
{
    if (callback_discoveries != stats.discovered)
    {
        return "callback discovery count differs from crawler discovered count";
    }

    if (stats.measured > 0U && received_packets == 0U)
    {
        return "measured peers were recorded without any receive-loop packet classification";
    }

    if (stats.failed > 0U && send_failures == 0U)
    {
        return "some peers failed without a local FINDNODE send-failure being recorded";
    }

    if (received_packets > 0U && stats.discovered == 0U)
    {
        return "packets were received, but no peers reached the discovered callback path";
    }

    return "counters are internally consistent for the current partial discv5 harness";
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    const auto args_opt = parse_args(argc, argv);
    if (!args_opt.has_value())
    {
        return EXIT_FAILURE;
    }
    const CliArgs& args = args_opt.value();

    // -----------------------------------------------------------------------
    // Configure logging.
    // -----------------------------------------------------------------------
    spdlog::set_level(spdlog::level::from_str(args.log_level));
    auto logger = rlp::base::createLogger("discv5_crawl");

    // -----------------------------------------------------------------------
    // Build bootnode seed list.
    // -----------------------------------------------------------------------
    discv5::discv5Config cfg;
    cfg.bind_port          = args.bind_port;
    cfg.query_interval_sec = 10U;   // Probe every 10 s for the live demo

    const auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (!keypair_result)
    {
        logger->error("Failed to generate local secp256k1 keypair for discv5_crawl");
        return EXIT_FAILURE;
    }

    std::copy(
        keypair_result.value().private_key.begin(),
        keypair_result.value().private_key.end(),
        cfg.private_key.begin());
    std::copy(
        keypair_result.value().public_key.begin(),
        keypair_result.value().public_key.end(),
        cfg.public_key.begin());

    size_t chain_seed_count = 0U;
    size_t extra_seed_count = args.extra_enrs.size();

    auto chain_config = load_chain_peer_config(
        args.chain,
        argv[0],
        args.chain_peers_json_path,
        args.chain_peers_url,
        args.chain_peers_url_enabled);
    if (chain_config.has_value())
    {
        apply_chain_discovery_config(*chain_config, argv[0]);
        cfg.bootstrap_enrs.insert(
            cfg.bootstrap_enrs.end(),
            chain_config->discv5_bootnodes.begin(),
            chain_config->discv5_bootnodes.end());
        chain_seed_count = chain_config->discv5_bootnodes.size();

        if (!chain_config->enr_trees.empty())
        {
            const auto tree_seeds = discv5::EnrTreeResolver{}.resolve(chain_config->enr_trees);
            cfg.bootstrap_enrs.insert(cfg.bootstrap_enrs.end(), tree_seeds.begin(), tree_seeds.end());
            chain_seed_count += tree_seeds.size();
            logger->info("Chain: {}  cache ENR seeds: {}  ENR-tree seeds: {}",
                         args.chain,
                         chain_config->discv5_bootnodes.size(),
                         tree_seeds.size());
        }
        else
        {
            logger->info("Chain: {}  cache ENR seeds: {}",
                         args.chain,
                         chain_config->discv5_bootnodes.size());
        }
    }
    else
    {
        logger->warn("No chain metadata found for {}; using only explicit bootnodes", args.chain);
    }

    // Append any manually specified bootstrap URIs.
    for (const auto& uri : args.extra_enrs)
    {
        cfg.bootstrap_enrs.push_back(uri);
        logger->info("Extra bootnode: {}", uri.substr(0U, 60U));
    }

    if (cfg.bootstrap_enrs.empty())
    {
        logger->error("No bootstrap nodes available. Use --bootnode-enr or provide chain metadata.");
        return EXIT_FAILURE;
    }

    // -----------------------------------------------------------------------
    // Build and start the client.
    // -----------------------------------------------------------------------
    boost::asio::io_context io;

    discv5::discv5_client client(io, cfg);

    // Track peers as they are discovered.
    std::atomic<size_t> total_discovered{0U};
    std::atomic<size_t> total_errors{0U};

    client.set_peer_discovered_callback(
        [&logger, &total_discovered](const discovery::ValidatedPeer& peer)
        {
            ++total_discovered;
            logger->debug("Discovered peer {}  {}:{}  eth_fork={}",
                         total_discovered.load(),
                         peer.ip,
                         peer.tcp_port,
                         peer.eth_fork_id.has_value() ? "yes" : "no");
        });

    client.set_error_callback(
        [&logger, &total_errors](const std::string& msg)
        {
            ++total_errors;
            logger->warn("Crawler error: {}", msg);
        });

    {
        const auto start_result = client.start();
        if (!start_result.has_value())
        {
            logger->error("Failed to start discv5 client: {}",
                          discv5::to_string(start_result.error()));
            return EXIT_FAILURE;
        }
    }

    const uint16_t actual_bound_port = client.bound_port();
    StopReason stop_reason = StopReason::kIoStopped;

    logger->info("discv5_crawl started on UDP port {}.  Running for {} s …",
                 actual_bound_port, args.timeout_sec);

    // -----------------------------------------------------------------------
    // Run the io_context with a timeout and Ctrl-C handler.
    // -----------------------------------------------------------------------
    boost::asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait(
        [&client, &io, &stop_reason](const boost::system::error_code& /*ec*/, int /*sig*/)
        {
            stop_reason = StopReason::kSignal;
            client.stop();
            io.stop();
        });

    boost::asio::spawn(io,
        [&io, &client, &stop_reason, timeout_sec = args.timeout_sec](boost::asio::yield_context yield)
        {
            boost::asio::steady_timer timer(io);
            timer.expires_after(std::chrono::seconds(timeout_sec));
            boost::system::error_code ec;
            timer.async_wait(boost::asio::redirect_error(yield, ec));

            if (ec == boost::asio::error::operation_aborted)
            {
                return;
            }

            stop_reason = StopReason::kTimeout;
            client.stop();
            io.stop();
        });

    io.run();

    // -----------------------------------------------------------------------
    // Print final stats.
    // -----------------------------------------------------------------------
    const discv5::CrawlerStats stats = client.stats();
    const size_t received_packets = client.received_packet_count();
    const size_t whoareyou_packets = client.whoareyou_packet_count();
    const size_t handshake_packets = client.handshake_packet_count();
    const size_t outbound_handshake_attempts = client.outbound_handshake_attempt_count();
    const size_t outbound_handshake_failures = client.outbound_handshake_failure_count();
    const size_t inbound_hs_reject_auth = client.inbound_handshake_reject_auth_count();
    const size_t inbound_hs_reject_challenge = client.inbound_handshake_reject_challenge_count();
    const size_t inbound_hs_reject_record = client.inbound_handshake_reject_record_count();
    const size_t inbound_hs_reject_crypto = client.inbound_handshake_reject_crypto_count();
    const size_t inbound_hs_reject_decrypt = client.inbound_handshake_reject_decrypt_count();
    const size_t inbound_hs_seen = client.inbound_handshake_seen_count();
    const size_t inbound_msg_seen = client.inbound_message_seen_count();
    const size_t inbound_msg_decrypt_fail = client.inbound_message_decrypt_fail_count();
    const size_t nodes_packets = client.nodes_packet_count();
    const size_t dropped_undersized_packets = client.dropped_undersized_packet_count();
    const size_t send_failures = client.send_findnode_failure_count();
    const RunStatus run_status = classify_run(
        total_discovered.load(),
        total_errors.load(),
        received_packets,
        send_failures,
        stats);
    const char* interpretation = interpret_run(
        total_discovered.load(),
        total_errors.load(),
        received_packets,
        whoareyou_packets,
        send_failures,
        stats);
    const char* counter_note = consistency_note(
        total_discovered.load(),
        received_packets,
        send_failures,
        stats);
    const bool show_trace_diagnostics = (spdlog::get_level() <= spdlog::level::trace);

    std::cout << "\n=== discv5_crawl results ===\n"
              << "  chain                  : " << args.chain                                  << "\n"
              << "  udp port               : " << actual_bound_port                       << "\n"
              << "  stop reason            : " << to_string(stop_reason)                 << "\n"
              << "  run status             : " << to_string(run_status)                  << "\n"
              << "  chain seeds            : " << chain_seed_count                        << "\n"
              << "  extra seeds            : " << extra_seed_count                        << "\n"
              << "  bootstrap seeds        : " << cfg.bootstrap_enrs.size()               << "\n"
              << "  callback discoveries   : " << total_discovered.load()                << "\n"
              << "  callback errors        : " << total_errors.load()                    << "\n"
              << "  packets received       : " << received_packets                       << "\n"
              << "  whoareyou packets      : " << whoareyou_packets                      << "\n"
              << "  handshake packets      : " << handshake_packets                      << "\n"
              << "  nodes packets          : " << nodes_packets                          << "\n"
              << "  undersized dropped     : " << dropped_undersized_packets             << "\n"
              << "  findnode send failures : " << send_failures                          << "\n"
              << "  discovered  : " << stats.discovered   << "\n"
              << "  queued      : " << stats.queued        << "\n"
              << "  measured    : " << stats.measured      << "\n"
              << "  failed      : " << stats.failed        << "\n"
              << "  duplicates  : " << stats.duplicates    << "\n"
              << "  wrong_chain : " << stats.wrong_chain   << "\n"
              << "  no_eth_entry: " << stats.no_eth_entry  << "\n"
              << "  invalid_enr : " << stats.invalid_enr   << "\n"
              << "  interpretation: " << interpretation                        << "\n"
              << "  counter consistency: " << counter_note                     << "\n"
              << "  note        : use --log-level trace to include detailed handshake/message diagnostics\n";

    if (show_trace_diagnostics)
    {
        std::cout << "  handshake attempts     : " << outbound_handshake_attempts            << "\n"
                  << "  handshake failures     : " << outbound_handshake_failures            << "\n"
                  << "  hs reject auth         : " << inbound_hs_reject_auth                 << "\n"
                  << "  hs reject challenge    : " << inbound_hs_reject_challenge            << "\n"
                  << "  hs reject record       : " << inbound_hs_reject_record               << "\n"
                  << "  hs reject crypto       : " << inbound_hs_reject_crypto               << "\n"
                  << "  hs reject decrypt      : " << inbound_hs_reject_decrypt              << "\n"
                  << "  hs inbound seen        : " << inbound_hs_seen                        << "\n"
                  << "  msg inbound seen       : " << inbound_msg_seen                       << "\n"
                  << "  msg decrypt fail       : " << inbound_msg_decrypt_fail               << "\n";
    }

    std::cout
              << "===========================\n";

    return EXIT_SUCCESS;
}
