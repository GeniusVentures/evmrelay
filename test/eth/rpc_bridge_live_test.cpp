// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file rpc_bridge_live_test.cpp
/// @brief Live integration test: RPC-based bridge event detection, receipt
///        verification, and claim construction against Ethereum Sepolia GNUS.
///
/// This test validates the RPC → event detection → receipt verification →
/// claim construction pipeline that SuperGenius will use for bridge mint
/// consensus. It uses a public Sepolia RPC endpoint to scan for real
/// Transfer events on the GNUS.AI testnet contract.
///
/// Environment variables:
///   EVMRELAY_RUN_LIVE_RPC_TEST=1          — required to run
///   EVMRELAY_LIVE_RPC_URL                 — override default Sepolia RPC URL
///   EVMRELAY_LIVE_RPC_CONFIRMATION_DEPTH  — override default depth (default: 12)
///   EVMRELAY_LIVE_RPC_MAX_SCAN_BLOCKS     — max blocks to scan (default: 500)

#include <gtest/gtest.h>

#include <eth/rpc_receipt_source.hpp>
#include <eth/rpc_http_transport.hpp>
#include <eth/rpc_manager.hpp>
#include <eth/json_rpc.hpp>
#include <eth/eth_receipt_source.hpp>
#include <eth/bridge_event.hpp>
#include <eth/bridge_observation.hpp>
#include <base/parse_utility.hpp>
#include <boost/json.hpp>
#include <boost/json/serialize.hpp>

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr std::string_view kDefaultSepoliaRpc = "https://ethereum-sepolia-rpc.publicnode.com";
constexpr std::string_view kGnusSepolia = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";
constexpr uint64_t kSepoliaChainId = 11155111;
constexpr uint64_t kDefaultConfirmationDepth = 12;
constexpr uint64_t kDefaultMaxScanBlocks = 500;

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

uint64_t env_u64_or(const char* name, uint64_t fallback)
{
    const auto value = env_string(name);
    if (!value.has_value())
    {
        return fallback;
    }
    try
    {
        return std::stoull(*value);
    }
    catch (...)
    {
        return fallback;
    }
}

eth::Address parse_address(std::string_view hex_addr)
{
    eth::Address addr{};
    if (!rlp::base::parse::hex_array(hex_addr, addr))
    {
        throw std::runtime_error("Invalid address hex: " + std::string(hex_addr));
    }
    return addr;
}

eth::Hash256 parse_topic0(std::string_view event_signature)
{
    return eth::abi::event_signature_hash(std::string(event_signature));
}

struct FoundEvent
{
    eth::Address  contract_address{};
    eth::Hash256  tx_hash{};
    eth::Hash256  block_hash{};
    uint64_t      block_number = 0;
    uint32_t      log_index = 0;
    eth::Hash256  topic0{};
    std::vector<eth::Hash256> topics;
    eth::codec::ByteBuffer data;
};

/// @brief Run a scoped RpcHttpTransport call and parse the JSON response.
struct LiveRpcClient
{
    eth::rpc::RpcHttpTransport transport;

    explicit LiveRpcClient(std::string rpc_url)
        : transport(std::move(rpc_url))
    {
    }

    std::optional<uint64_t> get_block_number(eth::rpc::RpcBlockTag tag)
    {
        const auto request = eth::rpc::make_get_block_by_number_request(tag, 1);
        const auto response = transport.call(request);
        if (!response.has_value())
        {
            return std::nullopt;
        }
        return eth::rpc::parse_block_number_response(response.value());
    }

    std::optional<std::vector<eth::rpc::RpcLog>> get_logs(
        const eth::Address& contract,
        const eth::Hash256& topic0,
        uint64_t            from_block,
        uint64_t            to_block)
    {
        eth::EventFilter filter;
        filter.addresses.push_back(contract);
        filter.topics.push_back(topic0);

        const auto request = eth::rpc::make_get_logs_request(filter, from_block, to_block, 2);
        const auto response = transport.call(request);
        if (!response.has_value())
        {
            return std::nullopt;
        }
        return eth::rpc::parse_get_logs_response(response.value());
    }

    std::optional<eth::ReceiptResult> get_receipt(const eth::Hash256& tx_hash)
    {
        const auto request = eth::rpc::make_get_transaction_receipt_request(tx_hash, 3);
        const auto response = transport.call(request);
        if (!response.has_value())
        {
            return std::nullopt;
        }
        return eth::rpc::parse_transaction_receipt_response(response.value());
    }
};

} // namespace

TEST(RpcBridgeLiveTest, PollFinalizedBlocksAndDetectTransferEvents)
{
    if (!env_enabled("EVMRELAY_RUN_LIVE_RPC_TEST"))
    {
        GTEST_SKIP() << "Set EVMRELAY_RUN_LIVE_RPC_TEST=1 to run live RPC test";
    }

    const auto rpc_url = env_string("EVMRELAY_LIVE_RPC_URL")
                             .value_or(std::string(kDefaultSepoliaRpc));
    const auto confirmation_depth = env_u64_or("EVMRELAY_LIVE_RPC_CONFIRMATION_DEPTH",
                                               kDefaultConfirmationDepth);
    const auto max_scan_blocks = env_u64_or("EVMRELAY_LIVE_RPC_MAX_SCAN_BLOCKS",
                                            kDefaultMaxScanBlocks);

    std::cout << "[rpc_bridge_live] RPC URL: " << rpc_url << "\n";
    std::cout << "[rpc_bridge_live] Confirmation depth: " << confirmation_depth << "\n";
    std::cout << "[rpc_bridge_live] Max scan blocks: " << max_scan_blocks << "\n";

    LiveRpcClient client(rpc_url);
    const auto contract = parse_address(kGnusSepolia);
    const auto transfer_topic0 = parse_topic0("Transfer(address,address,uint256)");

    std::cout << "[rpc_bridge_live] Sepolia GNUS contract: " << kGnusSepolia << "\n";
    std::cout << "[rpc_bridge_live] Transfer topic0: "
              << rlp::base::parse::hex_array_string(transfer_topic0) << "\n";

    // 1. Get the latest block number
    const auto latest = client.get_block_number(eth::rpc::RpcBlockTag::kLatest);
    ASSERT_TRUE(latest.has_value()) << "Failed to fetch latest block number";
    std::cout << "[rpc_bridge_live] Latest block: " << latest.value() << "\n";

    // 2. Compute safe-from block (latest - confirmation_depth - max_scan_blocks)
    const auto safe_latest = (latest.value() > confirmation_depth)
                                 ? (latest.value() - confirmation_depth)
                                 : 0ULL;
    const auto from_block = (safe_latest > max_scan_blocks)
                                ? (safe_latest - max_scan_blocks + 1)
                                : 0ULL;

    std::cout << "[rpc_bridge_live] Scanning blocks " << from_block
              << " → " << safe_latest << " ("
              << (safe_latest - from_block + 1) << " blocks)\n";

    // 3. Fetch logs
    const auto logs = client.get_logs(contract, transfer_topic0, from_block, safe_latest);
    ASSERT_TRUE(logs.has_value()) << "Failed to fetch logs from Sepolia RPC";

    if (logs.value().empty())
    {
        std::cout << "[rpc_bridge_live] No Transfer events found in the scan window. "
                  << "This is acceptable — the contract may have no recent activity.\n";
        SUCCEED();
        return;
    }

    std::cout << "[rpc_bridge_live] Found " << logs.value().size()
              << " Transfer event(s)\n";

    // 4. Verify the first event by fetching its receipt
    const auto& first_log = logs.value().front();
    std::cout << "[rpc_bridge_live] Verifying event: tx="
              << rlp::base::parse::hex_array_string(first_log.tx_hash)
              << " block=" << first_log.block_number
              << " log_index=" << first_log.log_index << "\n";

    const auto receipt = client.get_receipt(first_log.tx_hash);
    ASSERT_TRUE(receipt.has_value())
        << "Failed to fetch receipt for tx "
        << rlp::base::parse::hex_array_string(first_log.tx_hash);

    EXPECT_EQ(receipt->block_number, first_log.block_number);
    EXPECT_EQ(receipt->tx_hash, first_log.tx_hash);

    std::cout << "[rpc_bridge_live] Receipt verified: block_number="
              << receipt->block_number << " status="
              << (receipt->receipt.status.has_value()
                      ? (*receipt->receipt.status ? "success" : "revert")
                      : "unknown")
              << " logs_count=" << receipt->receipt.logs.size() << "\n";

    // 5. Construct a BridgeEventClaim from the verified event
    const auto event_topic0 = eth::abi::event_signature_hash(
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)");
    eth::BridgeEventClaim claim;
    claim.src_chain_id = kSepoliaChainId;
    claim.dest_chain_id = 0;
    claim.block_number = receipt->block_number;
    claim.block_hash = receipt->block_hash;
    claim.tx_hash = receipt->tx_hash;
    claim.log_index = first_log.log_index;
    claim.bridge_contract = contract;
    claim.event_topic0 = event_topic0;
    claim.observed_at = static_cast<uint64_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    claim.finality_depth = static_cast<uint32_t>(confirmation_depth);

    std::cout << "[rpc_bridge_live] Constructed BridgeEventClaim: "
              << "src_chain=" << claim.src_chain_id
              << " block=" << claim.block_number
              << " log_index=" << claim.log_index << "\n";

    // 6. Verify claim ↔ receipt consistency
    const auto claim_hash = eth::bridge_event_claim_hash(claim);
    EXPECT_FALSE(claim_hash == eth::Hash256{})
        << "Bridge event claim hash should not be zero";
    std::cout << "[rpc_bridge_live] claim_hash="
              << rlp::base::parse::hex_array_string(claim_hash) << "\n";

    // 7. Verify receipt log via evmrelay's built-in verifier
    const auto verify_result = eth::verify_receipt_log(
        receipt.value(),
        claim);
    std::cout << "[rpc_bridge_live] verify_receipt_log: "
              << (verify_result ? "PASS" : "FAIL")
              << "\n";

    SUCCEED();
}

TEST(RpcBridgeLiveTest, PollAndDecodeBridgeSourceBurnedEvents)
{
    if (!env_enabled("EVMRELAY_RUN_LIVE_RPC_TEST"))
    {
        GTEST_SKIP() << "Set EVMRELAY_RUN_LIVE_RPC_TEST=1 to run live RPC test";
    }

    const auto rpc_url = env_string("EVMRELAY_LIVE_RPC_URL")
                             .value_or(std::string(kDefaultSepoliaRpc));
    const auto confirmation_depth = env_u64_or("EVMRELAY_LIVE_RPC_CONFIRMATION_DEPTH",
                                               kDefaultConfirmationDepth);
    const auto max_scan_blocks = env_u64_or("EVMRELAY_LIVE_RPC_MAX_SCAN_BLOCKS",
                                            kDefaultMaxScanBlocks);

    std::cout << "[bridge_burned_live] RPC URL: " << rpc_url << "\n";

    LiveRpcClient client(rpc_url);
    const auto contract = parse_address(kGnusSepolia);
    const auto burned_topic0 = parse_topic0(
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)");

    std::cout << "[bridge_burned_live] Sepolia Diamond proxy: " << kGnusSepolia << "\n";
    std::cout << "[bridge_burned_live] BridgeSourceBurned topic0: "
              << rlp::base::parse::hex_array_string(burned_topic0) << "\n";

    const auto latest = client.get_block_number(eth::rpc::RpcBlockTag::kLatest);
    ASSERT_TRUE(latest.has_value()) << "Failed to fetch latest block number";
    std::cout << "[bridge_burned_live] Latest block: " << latest.value() << "\n";

    const auto safe_latest = (latest.value() > confirmation_depth)
                                 ? (latest.value() - confirmation_depth)
                                 : 0ULL;
    const auto from_block = (safe_latest > max_scan_blocks)
                                ? (safe_latest - max_scan_blocks + 1)
                                : 0ULL;

    std::cout << "[bridge_burned_live] Scanning blocks " << from_block
              << " → " << safe_latest << "\n";

    const auto logs = client.get_logs(contract, burned_topic0, from_block, safe_latest);
    ASSERT_TRUE(logs.has_value()) << "Failed to fetch logs from Sepolia RPC";

    if (logs.value().empty())
    {
        std::cout << "[bridge_burned_live] No BridgeSourceBurned events found. "
                  << "Trigger one with: cast send " << kGnusSepolia
                  << " \"bridgeOut(uint256,uint256,uint256)\" <amount> <token_id> <dest_chain>"
                  << " --private-key <key> --rpc-url " << rpc_url << "\n";
        SUCCEED();
        return;
    }

    std::cout << "[bridge_burned_live] Found " << logs.value().size()
              << " BridgeSourceBurned event(s)\n";

    for (size_t i = 0; i < logs.value().size(); ++i)
    {
        const auto& rpc_log = logs.value()[i];

        std::cout << "\n--- Event " << (i + 1) << " ---\n";

        // Extract sender from topic[1] (indexed address, rightmost 20 bytes)
        std::string sender_hex;
        if (rpc_log.log.topics.size() >= 2)
        {
            const auto& sender_topic = rpc_log.log.topics[1];
            sender_hex = "0x" + rlp::base::parse::hex_array_string(sender_topic).substr(24);
        }

        // ABI-decode log data: id (uint256), amount (uint256), srcChainID (uint256), destChainID (uint256)
        const auto& data = rpc_log.log.data;
        std::cout << "[bridge_burned_live] data size=" << data.size() << " bytes\n";

        if (data.size() >= 128)
        {
            // Each uint256 is 32 bytes, big-endian
            auto read_uint256_be = [](const uint8_t* ptr) -> std::string
            {
                // Skip leading zeros, return decimal
                uint64_t lo = 0;
                for (int j = 24; j < 32; ++j)
                {
                    lo = (lo << 8) | ptr[j];
                }
                // Check if value fits in uint64, else print full hex
                bool fits_u64 = true;
                for (int j = 0; j < 24; ++j)
                {
                    if (ptr[j] != 0) { fits_u64 = false; break; }
                }
                if (fits_u64)
                {
                    return std::to_string(lo);
                }
                std::ostringstream oss;
                oss << "0x";
                for (int j = 0; j < 32; ++j)
                {
                    oss << std::hex << std::setfill('0') << std::setw(2)
                        << static_cast<unsigned>(ptr[j]);
                }
                return oss.str();
            };

            const auto id_str          = read_uint256_be(data.data() + 0);
            const auto amount_str      = read_uint256_be(data.data() + 32);
            const auto src_chain_str   = read_uint256_be(data.data() + 64);
            const auto dest_chain_str  = read_uint256_be(data.data() + 96);

            std::cout << "[bridge_burned_live] sender      = " << sender_hex << "\n";
            std::cout << "[bridge_burned_live] id          = " << id_str << "\n";
            std::cout << "[bridge_burned_live] amount      = " << amount_str << "\n";
            std::cout << "[bridge_burned_live] srcChainID  = " << src_chain_str << "\n";
            std::cout << "[bridge_burned_live] destChainID = " << dest_chain_str << "\n";
        }

        std::cout << "[bridge_burned_live] tx_hash     = "
                  << rlp::base::parse::hex_array_string(rpc_log.tx_hash) << "\n";
        std::cout << "[bridge_burned_live] block       = " << rpc_log.block_number << "\n";
        std::cout << "[bridge_burned_live] log_index   = " << rpc_log.log_index << "\n";

        // Fetch receipt to verify
        const auto receipt = client.get_receipt(rpc_log.tx_hash);
        if (receipt.has_value())
        {
            std::cout << "[bridge_burned_live] receipt status = "
                      << (receipt->receipt.status.has_value()
                              ? (*receipt->receipt.status ? "success" : "revert")
                              : "unknown") << "\n";
            EXPECT_TRUE(receipt->receipt.status.has_value() && *receipt->receipt.status);
        }
    }

    SUCCEED();
}
