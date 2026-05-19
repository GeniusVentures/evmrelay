// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <array>
#include <atomic>
#include <functional>
#include <boost/asio/spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/redirect_error.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <csignal>

#include <eth/messages.hpp>
#include <eth/eth_peer_session.hpp>
#include <eth/eth_watch_runner.hpp>
#include <eth/eth_watch_service.hpp>
#include <eth/eth_watch_cli.hpp>
#include <discv4/chain_peers.hpp>
#include <discv4/dial_scheduler.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/rlpx_error.hpp>
#include <rlpx/rlpx_session.hpp>
#include <base/parse_utility.hpp>
#include <base/rlp-logger.hpp>
#include <eth/eth_handshake_guard.hpp>
#include <eth/eth_handshake.hpp>

namespace {

inline constexpr const char* kDefaultChainPeersUrl = "https://enodes.gnus.ai/chain_enodes.json.gz";
inline constexpr auto kWatchStatsInterval = std::chrono::seconds(4);
inline constexpr uint64_t kDefaultDetailedEventLimit = 2;

const std::array<std::string_view, 4> kDefaultMainnetChains{
    "ethereum-mainnet",
    "polygon-mainnet",
    "bnb-smart-chain",
    "base-mainnet",
};

struct Config {
    std::string host;
    uint16_t port = 0;
    std::string peer_pubkey_hex;
    std::string canonical_chain_name;
    std::vector<eth::cli::WatchSpec> watch_specs;
    bool prefer_direct_enode = false;
    bool fork_id_overridden = false;
    bool use_chain_peer_cache = false;
    // ETH Status fields — must match the target chain
    uint64_t network_id = 1;
    eth::Hash256 genesis_hash{};
    eth::ForkId  fork_id{};   ///< EIP-2124 fork identifier; set per chain
    std::vector<discv4::ValidatedPeer> chain_peers;
    discv4::ChainPeerConfig chain_peer_config{};
};

struct WatchOutputState
{
    uint64_t total_events = 0;
    uint64_t detailed_event_limit = kDefaultDetailedEventLimit;
    std::unordered_map<std::string, uint64_t> events_by_chain;
};

std::optional<eth::Hash256> parse_hash256(std::string_view value)
{
    eth::Hash256 hash{};
    if (!rlp::base::parse::hex_array(value, hash))
    {
        return std::nullopt;
    }
    return hash;
}

std::optional<eth::EthWatchDiscoveryMode> parse_peer_selection_mode(std::string_view value) noexcept
{
    if (value == "cache-only")
    {
        return eth::EthWatchDiscoveryMode::kCacheOnly;
    }
    if (value == "discover-if-needed")
    {
        return eth::EthWatchDiscoveryMode::kDiscoverIfNeeded;
    }
    if (value == "discover-first")
    {
        return eth::EthWatchDiscoveryMode::kDiscoverFirst;
    }
    if (value == "hybrid")
    {
        return eth::EthWatchDiscoveryMode::kHybrid;
    }
    return std::nullopt;
}

std::optional<Config> parse_enode(std::string_view enode) {
    constexpr std::string_view kPrefix = "enode://";
    if (enode.size() < kPrefix.size() || enode.substr(0, kPrefix.size()) != kPrefix) {
        return std::nullopt;
    }

    const auto without_prefix = enode.substr(kPrefix.size());
    const auto at_pos = without_prefix.find('@');
    if (at_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto pubkey_hex = without_prefix.substr(0, at_pos);
    if (pubkey_hex.size() != rlpx::kPublicKeySize * 2) {
        return std::nullopt;
    }

    const auto address_part = without_prefix.substr(at_pos + 1);
    const auto query_pos = address_part.find('?');
    const auto host_port = address_part.substr(0, query_pos);
    const auto colon_pos = host_port.rfind(':');
    if (colon_pos == std::string_view::npos) {
        return std::nullopt;
    }

    const auto host_view = host_port.substr(0, colon_pos);
    const auto port_view = host_port.substr(colon_pos + 1);
    auto port_value = rlp::base::parse::uint16_decimal(port_view);
    if (!port_value) {
        return std::nullopt;
    }

    Config cfg;
    cfg.host = std::string(host_view);
    cfg.port = *port_value;
    cfg.peer_pubkey_hex = std::string(pubkey_hex);
    return cfg;
}

void apply_chain_peer_config(
    Config&                              config,
    const discv4::ChainPeerConfig& chain_peer_config) noexcept
{
    config.canonical_chain_name = chain_peer_config.canonical_name;
    config.network_id = chain_peer_config.network_id;
    config.genesis_hash = chain_peer_config.genesis_hash;
    config.chain_peers = chain_peer_config.nodes;
    config.chain_peer_config = chain_peer_config;
    config.use_chain_peer_cache = !chain_peer_config.nodes.empty() || !chain_peer_config.bootnodes.empty();
    if (!config.fork_id_overridden && chain_peer_config.fork_id.has_value())
    {
        config.fork_id = *chain_peer_config.fork_id;
    }
}

void log_watch_notification(
    const eth::WatchEventNotification& notification,
    const std::shared_ptr<WatchOutputState>& output_state)
{
    static auto ev_log = rlp::base::createLogger("eth_watch");
    auto bytes_to_hex = [](const auto& arr)
    {
        std::string s;
        s.reserve(arr.size() * 2);
        for (const auto b : arr)
        {
            const char hex[] = "0123456789abcdef";
            s += hex[(static_cast<uint8_t>(b) >> 4) & 0xf];
            s += hex[ static_cast<uint8_t>(b)       & 0xf];
        }
        return s;
    };

    ++output_state->total_events;
    const auto chain_count = ++output_state->events_by_chain[notification.context.chain_name];

    std::string header = "event_count=" + std::to_string(output_state->total_events) +
                         " chain_count=" + std::to_string(chain_count) +
                         " " + notification.event_signature + " at block " +
                         std::to_string(notification.event.block_number) +
                         " chain=" + notification.context.chain_name;
    if (notification.event.tx_hash != eth::codec::Hash256{})
    {
        header += "  tx: 0x" + bytes_to_hex(notification.event.tx_hash);
    }
    SPDLOG_LOGGER_INFO(ev_log, "{}", header);

    if (output_state->total_events > output_state->detailed_event_limit)
    {
        return;
    }

    for (size_t i = 0; i < notification.values.size(); ++i)
    {
        const std::string label = std::to_string(i);
        std::string value;
        if (const auto* addr = std::get_if<eth::codec::Address>(&notification.values[i]))
        {
            value = "0x" + bytes_to_hex(*addr);
        }
        else if (const auto* u256 = std::get_if<intx::uint256>(&notification.values[i]))
        {
            value = intx::to_string(*u256);
        }
        else if (const auto* b32 = std::get_if<eth::codec::Hash256>(&notification.values[i]))
        {
            value = "0x" + bytes_to_hex(*b32);
        }
        else if (const auto* bval = std::get_if<bool>(&notification.values[i]))
        {
            value = (*bval ? "true" : "false");
        }
        SPDLOG_LOGGER_INFO(ev_log, "  [{}] {}", label, value);
    }
}

std::optional<discv4::ChainPeerConfig> load_chain_peer_config(
    const std::string&                   chain_name,
    const std::string&                   argv0,
    const std::string&                   chain_peers_json_path,
    const std::string&                   chain_peers_url,
    bool                                 chain_peers_url_enabled)
{
    std::optional<discv4::ChainPeerCacheRefreshResult> refresh_result;
    if (chain_peers_json_path.empty() && chain_peers_url_enabled)
    {
        refresh_result = discv4::refresh_chain_peer_cache_json(
            discv4::chain_peer_cache_json_path(argv0),
            chain_peers_url);
    }

    const auto chain_peers_json_file = discv4::find_chain_peer_cache_json_path(argv0, chain_peers_json_path);
    if (chain_peers_json_file.has_value())
    {
        return discv4::load_chain_peer_config_from_json(chain_name, *chain_peers_json_file);
    }
    if (refresh_result.has_value() && refresh_result->cache_available)
    {
        return discv4::load_chain_peer_config_from_json(chain_name, refresh_result->cache_path);
    }
    return std::nullopt;
}

void print_usage(const char* exe)
{
    std::cout << "Usage:\n"
              << "  " << exe << " <host> <port> <peer_pubkey_hex>\n"
              << "  " << exe << " --chain <chain_name>\n"
              << "  " << exe << " --all-chains\n"
              << "  " << exe << " --chain <chain_name> --chain-peers-json <path>\n"
              << "  " << exe << " --chain <chain_name> --chain-peers-url <url>\n"
              << "  " << exe << " --chain <chain_name> --direct-enode <enode://...>\n"
              << "\nDirect mode overrides:\n"
              << "  --network-id <uint64>            Override ETH Status network id\n"
              << "  --genesis-hash <0x64hex>         Override ETH Status genesis hash\n"
              << "  --fork-id-hash <0x8hex>          Override ETH Status fork id hash\n"
              << "  --fork-id-next <uint64>          Override ETH Status fork id next fork\n"
              << "\nOptional watch flags (repeatable, must follow connection args):\n"
              << "  --watch-contract <0x20byteHex>   Contract address to filter (omit for any)\n"
              << "  --watch-event    <signature>      Event signature, e.g. Transfer(address,address,uint256)\n"
              << "  Each --watch-event pairs with the preceding --watch-contract (or any contract if none).\n"
              << "  --display-events <count>          Print full decoded details for only the first count matches (default 2).\n"
              << "  --max-peers-per-chain <count>     Active dial/watch slots per chain (default 3).\n"
              << "  --max-peers-total <count>         Active dial/watch slots across all chains (default 24).\n"
              << "  --peer-selection <mode>           cache-only, discover-if-needed, discover-first, or hybrid.\n"
              << "\nExamples:\n"
              << "  " << exe << " --chain ethereum-sepolia --watch-event Transfer(address,address,uint256)\n"
              << "  " << exe << " --all-chains --watch-event Transfer(address,address,uint256)\n"
              << "  " << exe << " --chain ethereum-sepolia --direct-enode enode://<pubkey>@<host>:<port> --watch-event Transfer(address,address,uint256)\n"
              << "  " << exe << " 127.0.0.1 30303 <pubkey> --network-id 1337 --genesis-hash 0xfa742c20043b1d8a13ea6421d85e9678429f9f50c2e25b2814c61f7444504fec --log-level debug\n"
              << "  " << exe << " --chain ethereum-mainnet --watch-contract 0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48 --watch-event Transfer(address,address,uint256)\n"
              << "\nAvailable chains:\n"
              << "  ethereum-mainnet, ethereum-sepolia, ethereum-holesky\n"
              << "  polygon-mainnet, polygon-amoy\n"
              << "  bnb-smart-chain, bnb-smart-chain-testnet\n"
              << "  base-mainnet, base-sepolia\n";
}

/// @brief Attempts an RLPx connection to a peer and runs the ETH watch loop.
///        Calls @p on_done on every exit path so the DialScheduler can recycle
///        the dial slot.  Calls @p on_connected once the session is established
///        so the scheduler can track it for async stop().
void run_watch(std::string host,
               uint16_t port,
               rlpx::PublicKey peer_pubkey,
               std::string chain_name,
               uint64_t network_id,
               eth::Hash256 genesis_hash,
               eth::ForkId fork_id,
               const std::vector<eth::cli::WatchSpec>& watch_specs,
               std::shared_ptr<WatchOutputState> output_state,
               const std::function<void()>& on_done,
               const std::function<void(std::shared_ptr<rlpx::RlpxSession>)>& on_connected,
               boost::asio::yield_context yield)
{
    static auto log = rlp::base::createLogger("eth_watch");

    SPDLOG_LOGGER_DEBUG(log, "run_watch: begin chain={} host={} port={} network_id={}",
                        chain_name, host, port, network_id);

    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if (!keypair_result)
    {
        SPDLOG_LOGGER_ERROR(log, "run_watch: failed to generate local keypair");
        on_done();
        return;
    }

    const auto& keypair = keypair_result.value();

    const rlpx::SessionConnectParams params{
        host,
        port,
        keypair.public_key,
        keypair.private_key,
        peer_pubkey,
        "rlp-eth-watch",
        0
    };

    SPDLOG_LOGGER_DEBUG(log, "run_watch: connecting to {}:{}", host, port);
    auto session_result = rlpx::RlpxSession::connect(params, yield);
    if (!session_result)
    {
        const auto err = session_result.error();
        SPDLOG_LOGGER_DEBUG(log, "run_watch: failed to connect to {}:{} (error {}: {})",
                            host, port, static_cast<int>(err), rlpx::to_string(err));
        on_done();
        return;
    }

    SPDLOG_LOGGER_DEBUG(log, "run_watch: connect returned success");

    auto session = std::move(session_result.value());
    auto watch_runner = std::make_shared<eth::EthWatchRunner>(
        session,
        chain_name,
        network_id,
        genesis_hash,
        fork_id);
    auto executor = yield.get_executor();
    auto status_received = std::make_shared<std::atomic<bool>>(false);
    auto status_timeout = std::make_shared<boost::asio::steady_timer>(executor);
    auto stats_timer = std::make_shared<boost::asio::steady_timer>(executor);
    status_timeout->expires_after(eth::protocol::kStatusHandshakeTimeout);

    SPDLOG_LOGGER_DEBUG(log, "run_watch: watch runner created");
    SPDLOG_LOGGER_DEBUG(log, "run_watch: HELLO from peer: {}", session->peer_info().client_id);
    const uint8_t negotiated_eth_version = session->negotiated_eth_version();
    const uint8_t negotiated_eth_offset = session->negotiated_eth_offset();
    SPDLOG_LOGGER_DEBUG(log, "run_watch: negotiated eth version={} offset=0x{:02x}",
                        static_cast<int>(negotiated_eth_version),
                        negotiated_eth_offset);

    SPDLOG_LOGGER_DEBUG(log, "run_watch: sending local ETH Status");
    const auto handshake_result = eth::PerformEthStatusHandshake(
        eth::EthStatusHandshakeStart{
            std::make_shared<eth::RlpxEthSessionChannel>(session),
            network_id,
            genesis_hash,
            fork_id,
            eth::EthStatusAcceptedHandler{},
            rlpx::EthMessageHandler{}
        },
        yield);
    if (!handshake_result)
    {
        using E = eth::StatusValidationError;
        switch (handshake_result.error())
        {
        case E::kProtocolVersionMismatch:
            SPDLOG_LOGGER_ERROR(log, "run_watch: ETH Status handshake failed: protocol version mismatch");
            break;
        case E::kNetworkIDMismatch:
            SPDLOG_LOGGER_ERROR(log, "run_watch: ETH Status handshake failed: network id mismatch");
            break;
        case E::kGenesisMismatch:
            SPDLOG_LOGGER_ERROR(log, "run_watch: ETH Status handshake failed: genesis mismatch");
            break;
        case E::kInvalidBlockRange:
            SPDLOG_LOGGER_ERROR(log, "run_watch: ETH Status handshake failed: invalid block range");
            break;
        }
        (void)session->disconnect(rlpx::DisconnectReason::kSubprotocolError);
        on_done();
        return;
    }

    const auto common = eth::get_common_fields(handshake_result.value().remote_status);
    const uint64_t latest_block = eth::ExtractLatestBlockNumber(handshake_result.value().remote_status);
    status_received->store(true);
    status_timeout->cancel();
    on_connected(session);
    SPDLOG_LOGGER_INFO(log, "ETH Status: network_id={} protocol={} latest_block={}",
                       common.network_id,
                       static_cast<int>(common.protocol_version),
                       latest_block);
    SPDLOG_LOGGER_INFO(log, "Connected. Watching for events...");
    SPDLOG_LOGGER_DEBUG(log, "run_watch: local ETH Status queued");

    watch_runner->set_event_callback([output_state](const eth::WatchEventNotification& notification)
    {
        log_watch_notification(notification, output_state);
    });

    if (watch_specs.empty())
    {
        SPDLOG_LOGGER_INFO(log, "No event filter configured — watching all ETH messages.");
    }
    else
    {
        for (const auto& spec : watch_specs)
        {
            eth::codec::Address contract{};
            if (!spec.contract_hex.empty())
            {
                auto addr = eth::cli::parse_address(spec.contract_hex);
                if (!addr)
                {
                    SPDLOG_LOGGER_ERROR(log, "Invalid contract address: {}", spec.contract_hex);
                    on_done();
                    return;
                }
                contract = *addr;
            }

            const auto abi_params = eth::cli::infer_params(spec.event_signature);
            (void)watch_runner->watch_event(contract, spec.event_signature, abi_params);

            if (!spec.contract_hex.empty())
            {
                SPDLOG_LOGGER_INFO(log, "Watching: {} on contract {}", spec.event_signature, spec.contract_hex);
            }
            else
            {
                SPDLOG_LOGGER_INFO(log, "Watching: {}", spec.event_signature);
            }
        }
    }

    SPDLOG_LOGGER_DEBUG(log, "run_watch: installing session bridge");
    if (!StartEthStatusHandshake(
            eth::EthStatusHandshakeStart{
                std::make_shared<eth::RlpxEthSessionChannel>(session),
                network_id,
                genesis_hash,
                fork_id,
                eth::EthStatusAcceptedHandler{},
                [watch_runner](uint8_t eth_msg_id, const rlpx::ByteBuffer& payload)
                {
                    const rlp::ByteView payload_view(payload.data(), payload.size());
                    watch_runner->service().process_message(eth_msg_id, payload_view);
                }
            }))
    {
        SPDLOG_LOGGER_ERROR(log, "run_watch: failed to install ETH message handler");
        (void)session->disconnect(rlpx::DisconnectReason::kSubprotocolError);
        on_done();
        return;
    }
    watch_runner->install_session_bridge();

    SPDLOG_LOGGER_DEBUG(log, "run_watch: status handshake timeout armed for {} ms",
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            eth::protocol::kStatusHandshakeTimeout).count());

    session->set_disconnect_handler([status_timeout, stats_timer](const rlpx::protocol::DisconnectMessage& msg)
    {
        static auto disc_log = rlp::base::createLogger("eth_watch");
        SPDLOG_LOGGER_DEBUG(disc_log, "run_watch: Disconnected reason={}", static_cast<int>(msg.reason));
        status_timeout->cancel();
        stats_timer->cancel();
    });

    session->set_ping_handler([session](const rlpx::protocol::PingMessage&)
    {
        const rlpx::protocol::PongMessage pong;
        auto encoded = pong.encode();
        if (!encoded)
        {
            return;
        }
        rlpx::framing::Message pong_msg{};
        pong_msg.id = rlpx::kPongMessageId;
        pong_msg.payload = std::move(encoded.value());
        (void)session->post_message(std::move(pong_msg));
    });

    session->set_generic_handler([session,
                                  watch_runner,
                                  status_received,
                                  status_timeout](const rlpx::protocol::Message& msg)
    {
        static auto gh_log = rlp::base::createLogger("eth_watch");
        const uint8_t negotiated_eth_offset = session->negotiated_eth_offset();
        const auto eth_id = eth::NormalizeEthWireMessageId(msg.id, negotiated_eth_offset);
        if (!eth_id.has_value())
        {
            return;
        }

        if (!status_received->load())
        {
            SPDLOG_LOGGER_WARN(gh_log, "generic_handler: non-Status ETH message (id=0x{:02x}) received before handshake",
                               *eth_id);
            status_timeout->cancel();
            (void)session->disconnect(rlpx::DisconnectReason::kSubprotocolError);
            return;
        }

        const rlp::ByteView payload(msg.payload.data(), msg.payload.size());
        if (*eth_id == eth::protocol::kNewBlockHashesMessageId)
        {
            auto decoded = eth::protocol::decode_new_block_hashes(payload);
            if (decoded)
            {
                SPDLOG_LOGGER_DEBUG(gh_log, "generic_handler: NewBlockHashes count={}", decoded.value().entries.size());
            }
            else
            {
                SPDLOG_LOGGER_WARN(gh_log, "generic_handler: NewBlockHashes decode failed");
            }
        }

        SPDLOG_LOGGER_DEBUG(gh_log, "generic_handler: ETH msg id=0x{:02x} payload_size={}", *eth_id, msg.payload.size());
    });

    {
        boost::system::error_code hs_ec;
        SPDLOG_LOGGER_DEBUG(log, "run_watch: waiting for remote ETH Status or timeout");
        status_timeout->async_wait(boost::asio::redirect_error(yield, hs_ec));
        SPDLOG_LOGGER_DEBUG(log, "run_watch: status wait completed ec='{}' status_received={}",
                            hs_ec.message(),
                            status_received->load());

        if (!status_received->load())
        {
            if (hs_ec != boost::asio::error::operation_aborted)
            {
                SPDLOG_LOGGER_WARN(log, "run_watch: ETH Status handshake timeout ({}:{}) — peer is likely on a different chain",
                                   host, port);
                (void)session->disconnect(rlpx::DisconnectReason::kTimeout);
            }
            on_done();
            return;
        }
    }

    for (;;)
    {
        stats_timer->expires_after(kWatchStatsInterval);

        boost::system::error_code stats_ec;
        stats_timer->async_wait(boost::asio::redirect_error(yield, stats_ec));
        if (stats_ec == boost::asio::error::operation_aborted)
        {
            break;
        }
        if (stats_ec)
        {
            SPDLOG_LOGGER_DEBUG(log, "run_watch: stats timer stopped: {}", stats_ec.message());
            break;
        }

        const auto stats = watch_runner->service().stats();
        SPDLOG_LOGGER_INFO(log,
                           "Watch stats [{}]: eth_messages={} new_block_hashes={} new_blocks={} receipts_messages={} "
                           "decode_failures={} receipts_requested={} receipts_processed={} logs_seen={} "
                           "matched_logs={} discarded_logs={} subscriptions={}",
                           chain_name,
                           stats.eth_messages_seen,
                           stats.new_block_hashes_messages,
                           stats.new_block_messages,
                           stats.receipts_messages,
                           stats.decode_failures,
                           stats.receipts_requested,
                           stats.receipts_processed,
                           stats.logs_seen,
                           stats.matched_logs,
                           stats.discarded_logs,
                           watch_runner->service().subscription_count());
    }

    on_done();
}

std::optional<eth::ForkId> parse_fork_id_hash(std::string_view value)
{
    eth::ForkId forkId{};
    if (!rlp::base::parse::hex_array(value, forkId.fork_hash))
    {
        return std::nullopt;
    }
    return forkId;
}

std::optional<uint64_t> parse_fork_id_next(std::string_view value)
{
    return rlp::base::parse::uint64_decimal(value);
}

} // namespace

// ── WatcherPool and DialScheduler are defined in include/discv4/dial_scheduler.hpp ──

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage(argv[0]);
            return 1;
        }

        // Parse --log-level first so it takes effect before any loggers are created
        for (int i = 1; i < argc - 1; ++i)
        {
            if (std::string_view(argv[i]) == "--log-level")
            {
                const std::string_view level_str(argv[i + 1]);
                spdlog::level::level_enum lvl = spdlog::level::info;
                if      (level_str == "trace")    { lvl = spdlog::level::trace; }
                else if (level_str == "debug")    { lvl = spdlog::level::debug; }
                else if (level_str == "info")     { lvl = spdlog::level::info; }
                else if (level_str == "warn")     { lvl = spdlog::level::warn; }
                else if (level_str == "error")    { lvl = spdlog::level::err; }
                else if (level_str == "critical") { lvl = spdlog::level::critical; }
                else if (level_str == "off")      { lvl = spdlog::level::off; }
                spdlog::set_level(lvl);
                spdlog::apply_all([lvl](std::shared_ptr<spdlog::logger> l) { l->set_level(lvl); });
                break;
            }
        }

        std::optional<Config> config;
        std::vector<Config> multi_chain_configs;
        int next_arg = 1;
        std::string chain_name;
        std::string chain_peers_json_path;
        std::string chain_peers_url = kDefaultChainPeersUrl;
        bool chain_peers_url_enabled = true;
        auto output_state = std::make_shared<WatchOutputState>();
        eth::EthWatchConnectionConfig watch_connection_config{};
        eth::EthWatchDiscoveryMode discovery_mode = eth::EthWatchDiscoveryMode::kDiscoverIfNeeded;

        for (int i = 1; i < argc; ++i)
        {
            const std::string_view arg(argv[i]);
            if ((arg == "--chain-peers-json" || arg == "--bootstrap-peers-json") && i + 1 < argc)
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

        if (std::string_view(argv[next_arg]) == "--chain") {
            if (argc < 3) {
                print_usage(argv[0]);
                return 1;
            }
            const std::string selected_chain_name = argv[next_arg + 1];
            next_arg += 2;

            const auto chain_peer_config = load_chain_peer_config(
                selected_chain_name,
                argv[0],
                chain_peers_json_path,
                chain_peers_url,
                chain_peers_url_enabled);
            if (!chain_peer_config.has_value())
            {
                std::cout << "Unknown or unconfigured chain: " << selected_chain_name << "\n"
                          << "Expected chain metadata in chain peer cache/file.\n";
                return 1;
            }

            Config cfg{};
            apply_chain_peer_config(cfg, *chain_peer_config);
            config = std::move(cfg);
            chain_name = config->canonical_chain_name;
        } else if (std::string_view(argv[next_arg]) == "--all-chains") {
            ++next_arg;
            for (const auto selected_chain_name : kDefaultMainnetChains)
            {
                const auto chain_peer_config = load_chain_peer_config(
                    std::string(selected_chain_name),
                    argv[0],
                    chain_peers_json_path,
                    chain_peers_url,
                    chain_peers_url_enabled);
                if (!chain_peer_config.has_value())
                {
                    std::cout << "Unknown or unconfigured chain: " << selected_chain_name << "\n"
                              << "Expected chain metadata in chain peer cache/file.\n";
                    return 1;
                }

                Config cfg{};
                apply_chain_peer_config(cfg, *chain_peer_config);
                multi_chain_configs.push_back(std::move(cfg));
            }
            if (multi_chain_configs.empty())
            {
                std::cout << "--all-chains did not load any chain configs.\n";
                return 1;
            }
            config = multi_chain_configs.front();
            chain_name = "all-chains";
        } else if (argc >= 4) {
            const auto port_value = rlp::base::parse::uint16_decimal(argv[next_arg + 1]);
            if (!port_value) {
                std::cout << "Invalid port value.\n";
                return 1;
            }

            Config cfg;
            cfg.host = argv[next_arg];
            cfg.port = *port_value;
            cfg.peer_pubkey_hex = argv[next_arg + 2];
            next_arg += 3;
            config = cfg;
        } else {
            print_usage(argv[0]);
            return 1;
        }

        // Parse optional --watch-contract / --watch-event flags
        std::string pending_contract;
        while (next_arg < argc) {
            const std::string_view arg(argv[next_arg]);

            if (arg == "--watch-contract") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--watch-contract requires an address argument.\n";
                    return 1;
                }
                pending_contract = argv[next_arg + 1];
                next_arg += 2;
            } else if (arg == "--watch-event") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--watch-event requires a signature argument.\n";
                    return 1;
                }
                eth::cli::WatchSpec spec;
                spec.contract_hex    = pending_contract;
                spec.event_signature = argv[next_arg + 1];
                config->watch_specs.push_back(std::move(spec));
                pending_contract.clear();
                next_arg += 2;
            } else if (arg == "--log-level") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--log-level requires a level argument (trace, debug, info, warn, error, critical, off).\n";
                    return 1;
                }
                const std::string_view level_str(argv[next_arg + 1]);
                if (level_str == "trace")         { spdlog::set_level(spdlog::level::trace); }
                else if (level_str == "debug")    { spdlog::set_level(spdlog::level::debug); }
                else if (level_str == "info")     { spdlog::set_level(spdlog::level::info); }
                else if (level_str == "warn")     { spdlog::set_level(spdlog::level::warn); }
                else if (level_str == "error")    { spdlog::set_level(spdlog::level::err); }
                else if (level_str == "critical") { spdlog::set_level(spdlog::level::critical); }
                else if (level_str == "off")      { spdlog::set_level(spdlog::level::off); }
                else
                {
                    std::cout << "Unknown log level: " << level_str << "\n";
                    return 1;
                }
                next_arg += 2;
            } else if (arg == "--direct-enode") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--direct-enode requires an enode argument.\n";
                    return 1;
                }
                auto direct_config = parse_enode(argv[next_arg + 1]);
                if (!direct_config) {
                    std::cout << "Invalid enode supplied to --direct-enode.\n";
                    return 1;
                }
                config->host = direct_config->host;
                config->port = direct_config->port;
                config->peer_pubkey_hex = direct_config->peer_pubkey_hex;
                config->prefer_direct_enode = true;
                next_arg += 2;
            } else if (arg == "--display-events") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--display-events requires an integer argument.\n";
                    return 1;
                }
                const auto display_events = rlp::base::parse::uint64_decimal(argv[next_arg + 1]);
                if (!display_events) {
                    std::cout << "Invalid --display-events value.\n";
                    return 1;
                }
                output_state->detailed_event_limit = *display_events;
                next_arg += 2;
            } else if (arg == "--max-peers-per-chain") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--max-peers-per-chain requires an integer argument.\n";
                    return 1;
                }
                const auto max_peers_per_chain = rlp::base::parse::uint64_decimal(argv[next_arg + 1]);
                if (!max_peers_per_chain ||
                    *max_peers_per_chain == 0 ||
                    *max_peers_per_chain > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                    std::cout << "Invalid --max-peers-per-chain value.\n";
                    return 1;
                }
                watch_connection_config.max_connections_per_chain = static_cast<int>(*max_peers_per_chain);
                next_arg += 2;
            } else if (arg == "--max-peers-total") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--max-peers-total requires an integer argument.\n";
                    return 1;
                }
                const auto max_peers_total = rlp::base::parse::uint64_decimal(argv[next_arg + 1]);
                if (!max_peers_total ||
                    *max_peers_total == 0 ||
                    *max_peers_total > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
                    std::cout << "Invalid --max-peers-total value.\n";
                    return 1;
                }
                watch_connection_config.max_total_connections = static_cast<int>(*max_peers_total);
                next_arg += 2;
            } else if (arg == "--peer-selection") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--peer-selection requires a mode argument.\n";
                    return 1;
                }
                const auto parsed_mode = parse_peer_selection_mode(argv[next_arg + 1]);
                if (!parsed_mode) {
                    std::cout << "Invalid --peer-selection mode. Expected cache-only, discover-if-needed, discover-first, or hybrid.\n";
                    return 1;
                }
                discovery_mode = *parsed_mode;
                next_arg += 2;
            } else if (arg == "--network-id") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--network-id requires an integer argument.\n";
                    return 1;
                }
                const auto network_id = rlp::base::parse::uint64_decimal(argv[next_arg + 1]);
                if (!network_id) {
                    std::cout << "Invalid network id.\n";
                    return 1;
                }
                config->network_id = *network_id;
                next_arg += 2;
            } else if (arg == "--genesis-hash") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--genesis-hash requires a 32-byte hex value.\n";
                    return 1;
                }
                const auto genesis_hash = parse_hash256(argv[next_arg + 1]);
                if (!genesis_hash) {
                    std::cout << "Invalid genesis hash. Expected 32-byte hex value.\n";
                    return 1;
                }
                config->genesis_hash = *genesis_hash;
                next_arg += 2;
            } else if (arg == "--fork-id-hash") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--fork-id-hash requires a 4-byte hex value.\n";
                    return 1;
                }
                const auto fork_id = parse_fork_id_hash(argv[next_arg + 1]);
                if (!fork_id) {
                    std::cout << "Invalid fork id hash. Expected 4-byte hex value.\n";
                    return 1;
                }
                config->fork_id.fork_hash = fork_id->fork_hash;
                config->fork_id_overridden = true;
                next_arg += 2;
            } else if (arg == "--fork-id-next") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--fork-id-next requires an integer argument.\n";
                    return 1;
                }
                const auto fork_next = rlp::base::parse::uint64_decimal(argv[next_arg + 1]);
                if (!fork_next) {
                    std::cout << "Invalid fork id next value.\n";
                    return 1;
                }
                config->fork_id.next_fork = *fork_next;
                config->fork_id_overridden = true;
                next_arg += 2;
            } else if (arg == "--chain-peers-json" || arg == "--bootstrap-peers-json") {
                if (next_arg + 1 >= argc) {
                    std::cout << arg << " requires a file path.\n";
                    return 1;
                }
                chain_peers_json_path = argv[next_arg + 1];
                next_arg += 2;
            } else if (arg == "--chain-peers-url" || arg == "--bootstrap-peers-url") {
                if (next_arg + 1 >= argc) {
                    std::cout << arg << " requires a URL.\n";
                    return 1;
                }
                chain_peers_url = argv[next_arg + 1];
                chain_peers_url_enabled = true;
                next_arg += 2;
            } else if (arg == "--no-chain-peers-url" || arg == "--no-bootstrap-peers-url") {
                chain_peers_url_enabled = false;
                next_arg += 1;
            } else {
                std::cout << "Unknown argument: " << arg << "\n";
                print_usage(argv[0]);
                return 1;
            }
        }

        if (config->prefer_direct_enode &&
            !config->fork_id_overridden &&
            config->canonical_chain_name.empty())
        {
            config->fork_id = eth::ForkId{};
        }

        if (!multi_chain_configs.empty())
        {
            if (config->prefer_direct_enode)
            {
                std::cout << "--direct-enode cannot be combined with --all-chains.\n";
                return 1;
            }
            for (auto& chain_config : multi_chain_configs)
            {
                chain_config.watch_specs = config->watch_specs;
            }
        }

        boost::asio::io_context io;

        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            io.stop();
        });

        eth::EthWatchService service;

        if (!multi_chain_configs.empty())
        {
            static auto log = rlp::base::createLogger("eth_watch");
            auto service_watches = eth::cli::build_service_watch_specs(config->watch_specs);
            if (!service_watches)
            {
                std::cout << "Invalid watch contract address.\n";
                return 1;
            }

            std::vector<discv4::ChainPeerConfig> service_chains;
            service_chains.reserve(multi_chain_configs.size());

            for (const auto& chain_config : multi_chain_configs)
            {
                service_chains.push_back(chain_config.chain_peer_config);
                SPDLOG_LOGGER_INFO(log,
                                   "Starting eth watch service for chain '{}' with {} cached peer(s) and {} bootnode(s)",
                                   chain_config.canonical_chain_name,
                                   chain_config.chain_peer_config.nodes.size(),
                                   chain_config.chain_peer_config.bootnodes.size());
            }

            auto service_config = eth::cli::build_service_config(
                watch_connection_config,
                std::move(*service_watches),
                std::move(service_chains),
                discovery_mode);

            if (!service.initialize(
                    std::move(service_config),
                    [output_state](const eth::WatchEventNotification& notification)
                    {
                        log_watch_notification(notification, output_state);
                    }))
            {
                std::cout << "Invalid eth watch service configuration.\n";
                return 1;
            }
            service.run(io);
            std::cout << "Starting eth watch service for Ethereum, Polygon, BNB Smart Chain, and Base...\n";
        }
        else if (config->use_chain_peer_cache && !config->prefer_direct_enode)
        {
            static auto log = rlp::base::createLogger("eth_watch");
            auto service_watches = eth::cli::build_service_watch_specs(config->watch_specs);
            if (!service_watches)
            {
                std::cout << "Invalid watch contract address.\n";
                return 1;
            }

            auto service_config = eth::cli::build_service_config(
                watch_connection_config,
                std::move(*service_watches),
                {config->chain_peer_config},
                discovery_mode);

            SPDLOG_LOGGER_INFO(log,
                               "Starting eth watch service for chain '{}' with {} cached peer(s) and {} bootnode(s)",
                               config->canonical_chain_name,
                               config->chain_peer_config.nodes.size(),
                               config->chain_peer_config.bootnodes.size());

            if (!service.initialize(
                    std::move(service_config),
                    [output_state](const eth::WatchEventNotification& notification)
                    {
                        log_watch_notification(notification, output_state);
                    }))
            {
                std::cout << "Invalid eth watch service configuration.\n";
                return 1;
            }
            service.run(io);
            std::cout << "Starting eth watch service...\n";
        }
        else
        {
            // Explicit host/port/pubkey mode — connect directly
            rlpx::PublicKey peer_pubkey{};
            if (!rlp::base::parse::hex_array(config->peer_pubkey_hex, peer_pubkey))
            {
                std::cout << "Invalid peer public key hex (expected 128 hex chars).\n";
                return 1;
            }

            boost::asio::spawn(io,
                [host = config->host, port = config->port, peer_pubkey,
                 chain_name = config->canonical_chain_name.empty()
                    ? std::to_string(config->network_id)
                    : config->canonical_chain_name,
                 network_id = config->network_id,
                 genesis_hash = config->genesis_hash,
                 fork_id = config->fork_id,
                 watch_specs = std::move(config->watch_specs),
                 output_state](boost::asio::yield_context yc)
                {
                    run_watch(host, port, peer_pubkey,
                              chain_name,
                              network_id, genesis_hash, fork_id,
                              watch_specs,
                              output_state,
                              []() {},
                              [](std::shared_ptr<rlpx::RlpxSession>) {},
                              yc);
                });
        }

        io.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cout << "Unhandled exception: " << ex.what() << "\n";
        return 1;
    } catch (...) {
        std::cout << "Unhandled exception.\n";
        return 1;
    }
}
