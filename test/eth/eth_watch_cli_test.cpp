// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/eth_watch_cli.hpp>
#include <discv4/chain_peers.hpp>

namespace {

std::string make_enode(const std::string& ip, uint16_t port, char fill)
{
    return std::string("enode://")
        + std::string(128, fill)
        + "@"
        + ip
        + ":"
        + std::to_string(port);
}

} // namespace

// ============================================================================
// parse_address
// ============================================================================

TEST(EthWatchCliTest, ParseAddressBareHex)
{
    // 40 bare hex chars — no 0x prefix
    const std::string hex = "A0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48";
    auto result = eth::cli::parse_address(hex);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0),  0xA0);
    EXPECT_EQ(result->at(1),  0xb8);
    EXPECT_EQ(result->at(19), 0x48);
}

TEST(EthWatchCliTest, ParseAddressWithPrefix)
{
    const std::string hex = "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48";
    auto result = eth::cli::parse_address(hex);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0),  0xA0);
    EXPECT_EQ(result->at(19), 0x48);
}

TEST(EthWatchCliTest, ParseAddressUppercasePrefix)
{
    const std::string hex = "0XA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48";
    auto result = eth::cli::parse_address(hex);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->at(0), 0xA0);
}

TEST(EthWatchCliTest, ParseAddressZeroAddress)
{
    const std::string hex = "0000000000000000000000000000000000000000";
    auto result = eth::cli::parse_address(hex);
    ASSERT_TRUE(result.has_value());
    for (const auto b : *result)
    {
        EXPECT_EQ(b, 0x00);
    }
}

TEST(EthWatchCliTest, ParseAddressTooShort)
{
    auto result = eth::cli::parse_address("A0b86991");
    EXPECT_FALSE(result.has_value());
}

TEST(EthWatchCliTest, ParseAddressTooLong)
{
    // 42 chars after stripping 0x — one byte too long
    auto result = eth::cli::parse_address("0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB4800");
    EXPECT_FALSE(result.has_value());
}

TEST(EthWatchCliTest, ParseAddressInvalidChar)
{
    // 'G' is not a valid hex character
    auto result = eth::cli::parse_address("G0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48");
    EXPECT_FALSE(result.has_value());
}

TEST(EthWatchCliTest, ParseAddressEmptyString)
{
    auto result = eth::cli::parse_address("");
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// infer_params
// ============================================================================

TEST(EthWatchCliTest, InferParamsTransfer)
{
    const auto params = eth::cli::infer_params("Transfer(address,address,uint256)");
    ASSERT_EQ(params.size(), 3u);

    EXPECT_EQ(params[0].kind,    eth::abi::AbiParamKind::kAddress);
    EXPECT_TRUE(params[0].indexed);
    EXPECT_EQ(params[0].name,    "from");

    EXPECT_EQ(params[1].kind,    eth::abi::AbiParamKind::kAddress);
    EXPECT_TRUE(params[1].indexed);
    EXPECT_EQ(params[1].name,    "to");

    EXPECT_EQ(params[2].kind,    eth::abi::AbiParamKind::kUint);
    EXPECT_FALSE(params[2].indexed);
    EXPECT_EQ(params[2].name,    "value");
}

TEST(EthWatchCliTest, InferParamsApproval)
{
    const auto params = eth::cli::infer_params("Approval(address,address,uint256)");
    ASSERT_EQ(params.size(), 3u);

    EXPECT_EQ(params[0].kind,    eth::abi::AbiParamKind::kAddress);
    EXPECT_TRUE(params[0].indexed);
    EXPECT_EQ(params[0].name,    "owner");

    EXPECT_EQ(params[1].kind,    eth::abi::AbiParamKind::kAddress);
    EXPECT_TRUE(params[1].indexed);
    EXPECT_EQ(params[1].name,    "spender");

    EXPECT_EQ(params[2].kind,    eth::abi::AbiParamKind::kUint);
    EXPECT_FALSE(params[2].indexed);
    EXPECT_EQ(params[2].name,    "value");
}

TEST(EthWatchCliTest, InferParamsUnknownSignatureReturnsEmpty)
{
    const auto params = eth::cli::infer_params("Swap(address,uint256,uint256,uint256,uint256,address)");
    EXPECT_TRUE(params.empty());
}

TEST(EthWatchCliTest, InferParamsEmptyStringReturnsEmpty)
{
    const auto params = eth::cli::infer_params("");
    EXPECT_TRUE(params.empty());
}

// ============================================================================
// WatchSpec construction
// ============================================================================

TEST(EthWatchCliTest, WatchSpecDefaultConstruction)
{
    eth::cli::WatchSpec spec;
    EXPECT_TRUE(spec.contract_hex.empty());
    EXPECT_TRUE(spec.event_signature.empty());
}

TEST(EthWatchCliTest, WatchSpecAggregateInit)
{
    eth::cli::WatchSpec spec{"0xdeadbeef", "Transfer(address,address,uint256)"};
    EXPECT_EQ(spec.contract_hex,    "0xdeadbeef");
    EXPECT_EQ(spec.event_signature, "Transfer(address,address,uint256)");
}

TEST(EthWatchCliTest, BuildServiceWatchSpecsRejectsInvalidContract)
{
    const std::vector<eth::cli::WatchSpec> watch_specs{
        {"not-an-address", "Transfer(address,address,uint256)"},
    };

    const auto service_watches = eth::cli::build_service_watch_specs(watch_specs);
    EXPECT_FALSE(service_watches.has_value());
}

TEST(EthWatchCliTest, BuildServiceConfigPreservesLoadedGnosisDiscoveryFallbackMetadata)
{
    const std::string json_text = std::string("{")
        + "\"gnosis-chain\":{"
        + "\"networkId\":100,"
        + "\"genesisHex\":\"4f1dd23188aab3a0b3768e6a2b5f6cbf3fcb259af45d37b228a8a0ae61161f80\","
        + "\"forkId\":\"06000064\","
        + "\"forkNext\":\"0\","
        + "\"nodes\":[],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '7') + "\"}"
        + "]}}";

    auto loaded_config = discv4::load_chain_peer_config_from_json_text(
        "gnosis-chain",
        json_text);
    ASSERT_TRUE(loaded_config.has_value());

    const auto service_watches = eth::cli::build_service_watch_specs({
        {"", "Transfer(address,address,uint256)"},
    });
    ASSERT_TRUE(service_watches.has_value());

    eth::EthWatchConnectionConfig connection{};
    connection.max_total_connections = 4;
    connection.max_connections_per_chain = 1;

    auto service_config = eth::cli::build_service_config(
        connection,
        *service_watches,
        {*loaded_config});

    ASSERT_EQ(service_config.chains.size(), 1U);
    EXPECT_EQ(service_config.chains.front().canonical_name, "gnosis-chain");
    EXPECT_EQ(service_config.chains.front().network_id, 100U);
    EXPECT_TRUE(service_config.chains.front().nodes.empty());
    ASSERT_EQ(service_config.chains.front().bootnodes.size(), 1U);
    EXPECT_EQ(service_config.chains.front().bootnodes.front().peer.ip, "10.0.0.2");
    EXPECT_EQ(service_config.connection.max_connections_per_chain, 1);
    EXPECT_EQ(service_config.discovery_mode, eth::EthWatchDiscoveryMode::kDiscoverIfNeeded);
    ASSERT_EQ(service_config.watches.size(), 1U);
    EXPECT_EQ(service_config.watches.front().event_signature, "Transfer(address,address,uint256)");
}

TEST(EthWatchCliTest, BuildServiceConfigPreservesExplicitDiscoveryMode)
{
    eth::EthWatchConnectionConfig connection{};
    auto service_config = eth::cli::build_service_config(
        connection,
        {},
        {},
        eth::EthWatchDiscoveryMode::kHybrid);

    EXPECT_EQ(service_config.discovery_mode, eth::EthWatchDiscoveryMode::kHybrid);
}

// ============================================================================
// EventRegistry: Bridge V2 (BridgeOutInitiated) + retained v1 (BridgeSourceBurned)
// Plan 05.2-04 Task 1 — verify v2 registration has kBytes32 param 5, kBool param 6.
// ============================================================================

TEST(EventRegistryTest, RegistryContainsBridgeOutInitiated)
{
    const auto* params = eth::cli::event_registry().lookup(
        "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->size(), 7u); // 7 params: sender + 5 values + bool
}

TEST(EventRegistryTest, BridgeOutInitiatedParam5IsBytes32)
{
    const auto params = eth::cli::event_registry().params_for(
        "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)");
    ASSERT_GE(params.size(), 7u);
    EXPECT_EQ(params[5].kind, eth::abi::AbiParamKind::kBytes32);
    EXPECT_NE(params[5].kind, eth::abi::AbiParamKind::kBytes);
    EXPECT_FALSE(params[5].indexed); // sgnsDestination is not indexed
}

TEST(EventRegistryTest, BridgeOutInitiatedParam6IsBool)
{
    const auto params = eth::cli::event_registry().params_for(
        "BridgeOutInitiated(address,uint256,uint256,uint256,uint256,bytes32,bool)");
    ASSERT_GE(params.size(), 7u);
    EXPECT_EQ(params[6].kind, eth::abi::AbiParamKind::kBool);
    EXPECT_FALSE(params[6].indexed); // destinationYOdd is not indexed
    EXPECT_EQ(params[6].name, "destinationYOdd");
}

TEST(EventRegistryTest, BridgeSourceBurnedStillRegistered)
{
    // D-05: the old v1 event is retained for backward-compatible catch-up scanning.
    const auto* params = eth::cli::event_registry().lookup(
        "BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)");
    ASSERT_NE(params, nullptr);
    EXPECT_EQ(params->size(), 6u);
    EXPECT_EQ((*params)[5].kind, eth::abi::AbiParamKind::kBytes);
}
