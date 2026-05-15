// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/eth_watch_cli.hpp>

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

