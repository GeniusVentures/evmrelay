// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <iomanip>
#include <boost/asio/spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/beast/ssl.hpp>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <eth/messages.hpp>
#include <eth/eth_peer_session.hpp>
#include <eth/eth_watch_runner.hpp>
#include <eth/eth_watch_service.hpp>
#include <eth/eth_watch_cli.hpp>
#include <discv4/bootstrap_peers.hpp>
#include <discv4/bootnodes.hpp>
#include <discv4/bootnodes_test.hpp>
#include <discv4/dial_scheduler.hpp>
#include <discv4/discv4_client.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <rlpx/rlpx_error.hpp>
#include <rlpx/rlpx_session.hpp>
#include <base/rlp-logger.hpp>
#include <eth/eth_handshake_guard.hpp>
#include <eth/eth_handshake.hpp>

namespace {

inline constexpr const char* kDefaultBootstrapPeersUrl = "https://enodes.gnus.ai/chain_enodes.json.gz";
inline constexpr auto kWatchStatsInterval = std::chrono::seconds(4);

namespace http = boost::beast::http;

enum class DiscoveryMode {
    kDiscv4,
    kDiscv5,
};

struct Config {
    std::string host;
    uint16_t port = 0;
    std::string peer_pubkey_hex;
    std::string canonical_chain_name;
    std::vector<eth::cli::WatchSpec> watch_specs;
    bool prefer_direct_enode = false;
    bool fork_id_overridden = false;
    // ETH Status fields — must match the target chain
    uint64_t network_id = 1;
    eth::Hash256 genesis_hash{};
    eth::ForkId  fork_id{};   ///< EIP-2124 fork identifier; set per chain
    // Discovery — set when --chain is used; empty when explicit host/port/pubkey given
    std::vector<std::string> bootnode_enodes;
    DiscoveryMode discovery_mode = DiscoveryMode::kDiscv4;
};

std::optional<uint8_t> hex_to_nibble(char c) {
    if (c >= '0' && c <= '9') {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return static_cast<uint8_t>(10 + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F') {
        return static_cast<uint8_t>(10 + (c - 'A'));
    }
    return std::nullopt;
}

template <size_t N>
bool parse_hex_array(std::string_view hex, std::array<uint8_t, N>& out) {
    if (hex.size() != N * 2) {
        return false;
    }
    for (size_t i = 0; i < N; ++i) {
        const size_t index = i * 2;
        auto hi = hex_to_nibble(hex.at(index));
        auto lo = hex_to_nibble(hex.at(index + 1));
        if (!hi || !lo) {
            return false;
        }
        out.at(i) = static_cast<uint8_t>(((*hi) << 4) | *lo);
    }
    return true;
}

std::optional<uint16_t> parse_uint16(std::string_view value) {
    uint16_t out = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size()) {
        return std::nullopt;
    }
    return out;
}

std::optional<uint8_t> parse_uint8(std::string_view value) {
    unsigned int out = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size() || out > 0xFFU) {
        return std::nullopt;
    }
    return static_cast<uint8_t>(out);
}

std::optional<uint64_t> parse_uint64(std::string_view value)
{
    uint64_t out = 0;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return out;
}

std::optional<eth::Hash256> parse_hash256(std::string_view value)
{
    constexpr std::string_view kHexPrefix = "0x";
    if (value.substr(0, kHexPrefix.size()) == kHexPrefix)
    {
        value.remove_prefix(kHexPrefix.size());
    }

    eth::Hash256 hash{};
    if (!parse_hex_array(value, hash))
    {
        return std::nullopt;
    }
    return hash;
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
    auto port_value = parse_uint16(port_view);
    if (!port_value) {
        return std::nullopt;
    }

    Config cfg;
    cfg.host = std::string(host_view);
    cfg.port = *port_value;
    cfg.peer_pubkey_hex = std::string(pubkey_hex);
    return cfg;
}

// ---------------------------------------------------------------------------
// Chain registry — all per-chain constants in one place.
// Adding a new chain requires only one new entry in the map inside
// load_chain_config below.
// ---------------------------------------------------------------------------

/// @brief Decode a 64-char hex literal into a Hash256.
static eth::Hash256 hash_from_hex(const char* hex)
{
    eth::Hash256 out{};
    for (size_t i = 0; i < 32; ++i)
    {
        const auto hi = hex_to_nibble(hex[(i * 2)]).value_or(0);
        const auto lo = hex_to_nibble(hex[(i * 2) + 1]).value_or(0);
        out.at(i) = static_cast<uint8_t>((hi << 4) | lo);
    }
    return out;
}

struct ChainEntry
{
    const char*                     canonical_name;
    const std::vector<std::string>* bootnodes;
    uint64_t                        network_id;
    const char*                     genesis_hex;
    eth::ForkId                     fork_id{};  ///< EIP-2124; computed from genesis + past forks
};

/// @brief Look up chain config by name
std::optional<Config> load_chain_config(std::string_view chain_name)
{
    // Fork-ids are pre-computed via EIP-2124 for each chain as of early 2025.
    // Sepolia: MergeNetsplit@1735371, Shanghai@1677557088, Cancun@1706655072, Prague@1741159776
    static const eth::ForkId kSepoliaForkId{ { 0xed, 0x88, 0xb5, 0xfd }, 0 };

    static const std::unordered_map<std::string, ChainEntry> kChains = {
        { "mainnet",      ChainEntry{ "ethereum-mainnet", &ETHEREUM_MAINNET_BOOTNODES, 1,        "d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3" } },
        { "ethereum-mainnet", ChainEntry{ "ethereum-mainnet", &ETHEREUM_MAINNET_BOOTNODES, 1,        "d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3" } },
        { "sepolia",      ChainEntry{ "ethereum-sepolia", &ETHEREUM_SEPOLIA_BOOTNODES, 11155111, "25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9", kSepoliaForkId } },
        { "ethereum-sepolia", ChainEntry{ "ethereum-sepolia", &ETHEREUM_SEPOLIA_BOOTNODES, 11155111, "25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9", kSepoliaForkId } },
        { "holesky",      ChainEntry{ "ethereum-holesky", &ETHEREUM_HOLESKY_BOOTNODES, 17000,    "b5f7f912443c940f21fd611f12828d75b534364ed9e95ca4e307729a4661bde4" } },
        { "ethereum-holesky", ChainEntry{ "ethereum-holesky", &ETHEREUM_HOLESKY_BOOTNODES, 17000,    "b5f7f912443c940f21fd611f12828d75b534364ed9e95ca4e307729a4661bde4" } },
        { "polygon",      ChainEntry{ "polygon-mainnet", &POLYGON_MAINNET_BOOTNODES,  137,      "a9c28ce2141b56c474f1dc504bee9b01eb1bd7d1a507580d5519d4437a97de1b" } },
        { "polygon-mainnet", ChainEntry{ "polygon-mainnet", &POLYGON_MAINNET_BOOTNODES,  137,      "a9c28ce2141b56c474f1dc504bee9b01eb1bd7d1a507580d5519d4437a97de1b" } },
        { "polygon-amoy", ChainEntry{ "polygon-amoy", &POLYGON_AMOY_BOOTNODES,     80002,    "0000000000000000000000000000000000000000000000000000000000000000" } },
        { "bsc",          ChainEntry{ "bnb-smart-chain", &BSC_MAINNET_BOOTNODES,      56,       "0d21840abff46b96c84b2ac9e10e4f5cdaeb5693cb665db62a2f3b02d2d57b5b" } },
        { "bnb-smart-chain", ChainEntry{ "bnb-smart-chain", &BSC_MAINNET_BOOTNODES,      56,       "0d21840abff46b96c84b2ac9e10e4f5cdaeb5693cb665db62a2f3b02d2d57b5b" } },
        { "bsc-testnet",  ChainEntry{ "bnb-smart-chain-testnet", &BSC_TESTNET_STATICNODES,    97,       "6d3c66c5357ec91d5c43af47e234a939b22557cbb552dc45bebbceeed90fbe10" } },
        { "bnb-smart-chain-testnet", ChainEntry{ "bnb-smart-chain-testnet", &BSC_TESTNET_STATICNODES,    97,       "6d3c66c5357ec91d5c43af47e234a939b22557cbb552dc45bebbceeed90fbe10" } },
        { "base",         ChainEntry{ "base-mainnet", &BASE_MAINNET_BOOTNODES,     8453,     "f712aa9241cc24369b143cf6dce85f0902a9731e70d66818a3a5845b296c73dd" } },
        { "base-mainnet", ChainEntry{ "base-mainnet", &BASE_MAINNET_BOOTNODES,     8453,     "f712aa9241cc24369b143cf6dce85f0902a9731e70d66818a3a5845b296c73dd" } },
        { "base-sepolia", ChainEntry{ "base-sepolia", &BASE_SEPOLIA_BOOTNODES,     84532,    "0dcc9e089e30b90ddfc55be9a37dd15bc551aeee999d2e2b51414c54eaf934e4" } },
    };

    const auto it = kChains.find(std::string(chain_name));
    if (it == kChains.end())
    {
        return std::nullopt;
    }

    const auto& entry = it->second;
    if (entry.bootnodes->empty())
    {
        static auto log = rlp::base::createLogger("eth_watch");
        SPDLOG_LOGGER_ERROR(log, "No bootnodes configured for chain: {}", chain_name);
        return std::nullopt;
    }

    Config cfg;
    cfg.canonical_chain_name = entry.canonical_name;
    cfg.network_id   = entry.network_id;
    cfg.genesis_hash = hash_from_hex(entry.genesis_hex);
    cfg.fork_id      = entry.fork_id;
    // Store all bootnodes for discv4 — host/port/pubkey filled in after discovery
    for (const auto& bn : *entry.bootnodes)
    {
        cfg.bootnode_enodes.push_back(bn);
    }
    return cfg;
}


void print_usage(const char* exe) {
    std::cout << "Usage:\n"
              << "  " << exe << " <host> <port> <peer_pubkey_hex>\n"
              << "  " << exe << " --chain <chain_name>\n"
              << "  " << exe << " --chain <chain_name> --discovery-mode <discv4|discv5>\n"
              << "  " << exe << " --chain <chain_name> --bootstrap-peers-json <path>\n"
              << "  " << exe << " --chain <chain_name> --bootstrap-peers-url <url>\n"
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
              << "\nExamples:\n"
              << "  " << exe << " --chain sepolia --watch-event Transfer(address,address,uint256)\n"
              << "  " << exe << " --chain sepolia --direct-enode enode://<pubkey>@<host>:<port> --watch-event Transfer(address,address,uint256)\n"
              << "  " << exe << " 127.0.0.1 30303 <pubkey> --network-id 1337 --genesis-hash 0xfa742c20043b1d8a13ea6421d85e9678429f9f50c2e25b2814c61f7444504fec --log-level debug\n"
              << "  " << exe << " --chain mainnet --watch-contract 0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48 --watch-event Transfer(address,address,uint256)\n"
              << "\nAvailable chains:\n"
              << "  Ethereum: mainnet, sepolia, holesky, ethereum-mainnet, ethereum-sepolia, ethereum-holesky\n"
              << "  Polygon:  polygon, polygon-amoy, polygon-mainnet\n"
              << "  BSC:      bsc, bsc-testnet, bnb-smart-chain, bnb-smart-chain-testnet\n"
              << "  Base:     base, base-sepolia, base-mainnet\n";
}

/// @brief Attempts an RLPx connection to a peer and runs the ETH watch loop.
///        Calls @p on_done on every exit path so the DialScheduler can recycle
///        the dial slot.  Calls @p on_connected once the session is established
///        so the scheduler can track it for async stop().
void run_watch(std::string host,
               uint16_t port,
               rlpx::PublicKey peer_pubkey,
               uint64_t network_id,
               eth::Hash256 genesis_hash,
               eth::ForkId fork_id,
               std::vector<eth::cli::WatchSpec> watch_specs,
               std::function<void()> on_done,
               std::function<void(std::shared_ptr<rlpx::RlpxSession>)> on_connected,
               boost::asio::yield_context yield)
{
    static auto log = rlp::base::createLogger("eth_watch");

    SPDLOG_LOGGER_DEBUG(log, "run_watch: begin host={} port={} network_id={}", host, port, network_id);

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
        std::to_string(network_id),
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
    SPDLOG_LOGGER_INFO(log, "ETH Status: network_id={} protocol={} latest_block={}",
                       common.network_id,
                       static_cast<int>(common.protocol_version),
                       latest_block);
    SPDLOG_LOGGER_INFO(log, "Connected. Watching for events...");
    SPDLOG_LOGGER_DEBUG(log, "run_watch: local ETH Status queued");

    watch_runner->set_event_callback([](const eth::WatchEventNotification& notification)
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

        std::string header = notification.event_signature + " at block " +
                             std::to_string(notification.event.block_number) +
                             " chain=" + notification.context.chain_name;
        if (notification.event.tx_hash != eth::codec::Hash256{})
        {
            header += "  tx: 0x" + bytes_to_hex(notification.event.tx_hash);
        }
        SPDLOG_LOGGER_INFO(ev_log, "{}", header);

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
        if (eth_id.has_value())
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
                           "Watch stats: eth_messages={} new_block_hashes={} new_blocks={} receipts_messages={} "
                           "decode_failures={} receipts_requested={} receipts_processed={} logs_seen={} "
                           "matched_logs={} discarded_logs={} subscriptions={}",
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
    constexpr std::string_view kHexPrefix = "0x";
    if (value.substr(0, kHexPrefix.size()) == kHexPrefix)
    {
        value.remove_prefix(kHexPrefix.size());
    }

    eth::ForkId forkId{};
    if (value.size() != forkId.fork_hash.size() * 2)
    {
        return std::nullopt;
    }
    if (!parse_hex_array(value, forkId.fork_hash))
    {
        return std::nullopt;
    }
    return forkId;
}

std::optional<uint64_t> parse_fork_id_next(std::string_view value)
{
    return parse_uint64(value);
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
        int next_arg = 1;
        std::string chain_name;
        std::string bootstrap_peers_json_path;
        std::string bootstrap_peers_url = kDefaultBootstrapPeersUrl;
        bool bootstrap_peers_url_enabled = true;

        if (std::string_view(argv[next_arg]) == "--chain") {
            if (argc < 3) {
                print_usage(argv[0]);
                return 1;
            }
            const std::string selected_chain_name = argv[next_arg + 1];
            config = load_chain_config(selected_chain_name);
            if (!config) {
                std::cout << "Unknown or unconfigured chain: " << selected_chain_name << "\n"
                          << "Available: mainnet, sepolia, holesky, ethereum-mainnet, ethereum-sepolia, ethereum-holesky, "
                          << "polygon, polygon-mainnet, polygon-amoy, bsc, bsc-testnet, bnb-smart-chain, "
                          << "bnb-smart-chain-testnet, base, base-mainnet, base-sepolia\n";
                return 1;
            }
            chain_name = config->canonical_chain_name;
            next_arg += 2;
        } else if (argc >= 4) {
            const auto port_value = parse_uint16(argv[next_arg + 1]);
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
            } else if (arg == "--discovery-mode") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--discovery-mode requires a value (discv4|discv5).\n";
                    return 1;
                }
                const std::string_view mode(argv[next_arg + 1]);
                if (mode == "discv4") {
                    config->discovery_mode = DiscoveryMode::kDiscv4;
                } else if (mode == "discv5") {
                    config->discovery_mode = DiscoveryMode::kDiscv5;
                } else {
                    std::cout << "Unknown discovery mode: " << mode << "\n";
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
                config->bootnode_enodes.clear();
                next_arg += 2;
            } else if (arg == "--network-id") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--network-id requires an integer argument.\n";
                    return 1;
                }
                const auto network_id = parse_uint64(argv[next_arg + 1]);
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
                const auto fork_next = parse_uint64(argv[next_arg + 1]);
                if (!fork_next) {
                    std::cout << "Invalid fork id next value.\n";
                    return 1;
                }
                config->fork_id.next_fork = *fork_next;
                config->fork_id_overridden = true;
                next_arg += 2;
            } else if (arg == "--bootstrap-peers-json") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--bootstrap-peers-json requires a file path.\n";
                    return 1;
                }
                bootstrap_peers_json_path = argv[next_arg + 1];
                next_arg += 2;
            } else if (arg == "--bootstrap-peers-url") {
                if (next_arg + 1 >= argc) {
                    std::cout << "--bootstrap-peers-url requires a URL.\n";
                    return 1;
                }
                bootstrap_peers_url = argv[next_arg + 1];
                bootstrap_peers_url_enabled = true;
                next_arg += 2;
            } else if (arg == "--no-bootstrap-peers-url") {
                bootstrap_peers_url_enabled = false;
                next_arg += 1;
            } else {
                std::cout << "Unknown argument: " << arg << "\n";
                print_usage(argv[0]);
                return 1;
            }
        }

        if (config->prefer_direct_enode && !config->fork_id_overridden)
        {
            config->fork_id = eth::ForkId{};
        }

        boost::asio::io_context io;

        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        signals.async_wait([&](const boost::system::error_code&, int) {
            io.stop();
        });

        // dv4 declared outside the if-block so it lives past io.run().
        // If --chain mode is not used, this stays null (no-op).
        std::shared_ptr<discv4::discv4_client> dv4;

        if (!config->bootnode_enodes.empty() && !config->prefer_direct_enode)
        {
            if (config->discovery_mode == DiscoveryMode::kDiscv5)
            {
                std::cout << "--discovery-mode discv5 is not wired in eth_watch yet; use discv4 for now.\n";
                return 1;
            }

            // --chain mode: use discv4 to find a real full node, then connect via RLPx
            auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
            if (!keypair_result)
            {
                std::cout << "Failed to generate keypair for discv4.\n";
                return 1;
            }
            const auto& keypair = keypair_result.value();

            discv4::discv4Config dv4_cfg;
            dv4_cfg.bind_port = 0; // OS-assigned ephemeral port
            std::copy(keypair.private_key.begin(), keypair.private_key.end(),
                      dv4_cfg.private_key.begin());
            std::copy(keypair.public_key.begin(), keypair.public_key.end(),
                      dv4_cfg.public_key.begin());

            dv4 = std::make_shared<discv4::discv4_client>(io, dv4_cfg);

            // Capture config values needed in the callback
            const std::string bootstrap_chain_name = config->canonical_chain_name;
            const uint64_t   network_id   = config->network_id;
            const auto       genesis_hash = config->genesis_hash;
            const auto       fork_id      = config->fork_id;
            const auto       watch_specs  = config->watch_specs;

            // Two-level resource caps — desktop defaults (10 per chain, 200 total).
            // Embedding apps pass platform-appropriate values:
            //   mobile: WatcherPool(12, 3)   desktop: WatcherPool(200, 10)
            auto pool = std::make_shared<discv4::WatcherPool>(200, 10);
            auto scheduler = std::make_shared<discv4::DialScheduler>(
                io, pool,
                [network_id, genesis_hash, fork_id, watch_specs]
                (discv4::ValidatedPeer                                            vp,
                 std::function<void()>                                            on_done,
                 std::function<void(std::shared_ptr<rlpx::RlpxSession>)>         on_connected,
                 boost::asio::yield_context                                       yc)
                {
                    run_watch(vp.peer.ip, vp.peer.tcp_port, vp.pubkey,
                              network_id, genesis_hash, fork_id, watch_specs,
                              std::move(on_done), std::move(on_connected), yc);
                });

            dv4->set_peer_discovered_callback(
                [scheduler](const discv4::DiscoveredPeer& peer)
                {
                    discv4::ValidatedPeer vp;
                    vp.peer = peer;
                    std::copy(peer.node_id.begin(), peer.node_id.end(), vp.pubkey.begin());
                    if (!rlpx::crypto::Ecdh::verify_public_key(vp.pubkey))
                    {
                        return;
                    }
                    scheduler->enqueue(std::move(vp));
                });

            dv4->set_error_callback([](const std::string& err) {
                std::cout << "discv4 error: " << err << "\n";
            });

            static auto log = rlp::base::createLogger("eth_watch");
            std::optional<discv4::BootstrapCacheRefreshResult> refresh_result;
            if (bootstrap_peers_json_path.empty() && bootstrap_peers_url_enabled)
            {
                refresh_result = discv4::refresh_bootstrap_cache_json(
                    discv4::bootstrap_cache_json_path(argv[0]),
                    bootstrap_peers_url);
                if (refresh_result.has_value())
                {
                    SPDLOG_LOGGER_INFO(log, "Remote bootstrap refresh for chain '{}' from {} => {} ({})",
                                       bootstrap_chain_name,
                                       bootstrap_peers_url,
                                       refresh_result->cache_updated ? "updated" :
                                           (refresh_result->cache_available ? "unchanged" : "unavailable"),
                                       refresh_result->cache_path.string());
                }
            }

            const auto bootstrap_peers_json_file = discv4::find_bootstrap_peers_json_path(argv[0], bootstrap_peers_json_path);
            if (bootstrap_peers_json_file.has_value())
            {
                const auto bootstrap_peers = discv4::load_bootstrap_peers_from_json(bootstrap_chain_name, *bootstrap_peers_json_file);
                SPDLOG_LOGGER_INFO(log, "Loaded {} local bootstrap peers for chain '{}' from {}",
                                   bootstrap_peers.size(), bootstrap_chain_name, bootstrap_peers_json_file->string());
                for (auto peer : bootstrap_peers)
                {
                    scheduler->enqueue(std::move(peer));
                }
            }
            else if (refresh_result.has_value())
            {
                const auto bootstrap_peers_remote = discv4::load_bootstrap_peers_from_json(
                    bootstrap_chain_name,
                    refresh_result->cache_path);
                SPDLOG_LOGGER_INFO(log, "Loaded {} remote bootstrap peers for chain '{}' from {}",
                                   bootstrap_peers_remote.size(), bootstrap_chain_name, bootstrap_peers_url);
                for (auto peer : bootstrap_peers_remote)
                {
                    scheduler->enqueue(std::move(peer));
                }
            }

            // Ping all bootnodes to seed discovery — wrap in void coroutine
            // because ping() returns Result<pong> which has a deleted default ctor
            for (const auto& enode : config->bootnode_enodes)
            {
                const auto bn = parse_enode(enode);
                if (!bn)
                {
                    continue;
                }
                discv4::NodeId bn_id{};
                if (!parse_hex_array(bn->peer_pubkey_hex, bn_id))
                {
                    continue;
                }
                boost::asio::spawn(io,
                    [dv4, host = bn->host, port = bn->port, bn_id](boost::asio::yield_context yc)
                    {
                        // find_node internally calls ensure_bond (ping→pong) then sends FIND_NODE
                        auto result = dv4->find_node(host, port, bn_id, yc);
                        (void)result;
                    });
            }

            const auto start_result = dv4->start();
            if (!start_result)
            {
                std::cout << "Failed to start discv4.\n";
                return 1;
            }
            std::cout << "Running discv4 peer discovery...\n";
        }
        else
        {
            // Explicit host/port/pubkey mode — connect directly
            rlpx::PublicKey peer_pubkey{};
            if (!parse_hex_array(config->peer_pubkey_hex, peer_pubkey))
            {
                std::cout << "Invalid peer public key hex (expected 128 hex chars).\n";
                return 1;
            }

            boost::asio::spawn(io,
                [host = config->host, port = config->port, peer_pubkey,
                 network_id = config->network_id,
                 genesis_hash = config->genesis_hash,
                 fork_id = config->fork_id,
                 watch_specs = std::move(config->watch_specs)](boost::asio::yield_context yc)
                {
                    run_watch(host, port, peer_pubkey,
                              network_id, genesis_hash, fork_id,
                              watch_specs,
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

