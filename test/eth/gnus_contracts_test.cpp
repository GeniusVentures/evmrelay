// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file gnus_contracts_test.cpp
/// @brief Tests that EthWatchService correctly filters Transfer events for the
///        real GNUS.AI smart contract addresses on each supported network.
///
/// These tests exercise the full pipeline:
///   parse_address → watch_event → process_receipts → DecodedEventCallback
///
/// The contract addresses are the live production and testnet addresses from
/// https://docs.gnus.ai/gnus.ai-gitbook/technical-information/gnus.ai-smart-contracts

#include <gtest/gtest.h>
#include <eth/abi_decoder.hpp>
#include <eth/eth_watch_cli.hpp>
#include <eth/eth_watch_service.hpp>
#include <eth/messages.hpp>
#include <array>
#include <optional>
#include <string_view>

namespace {

// ---------------------------------------------------------------------------
// GNUS.AI contract addresses (production + testnet)
// ---------------------------------------------------------------------------

/// Mainnet addresses (Ethereum, BSC, Base share the same address)
constexpr std::string_view kGnusEthereum  = "0x614577036F0a024DBC1C88BA616b394DD65d105a";
constexpr std::string_view kGnusBsc       = "0x614577036F0a024DBC1C88BA616b394DD65d105a";
constexpr std::string_view kGnusBase      = "0x614577036F0a024DBC1C88BA616b394DD65d105a";
constexpr std::string_view kGnusPolygon   = "0x127E47abA094a9a87D084a3a93732909Ff031419";

/// Testnet addresses
constexpr std::string_view kGnusSepolia      = "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70";
constexpr std::string_view kGnusPolygonAmoy  = "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB";
constexpr std::string_view kGnusBscTestnet   = "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB";
constexpr std::string_view kGnusBaseTestnet  = "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Parse a hex address string (with or without 0x prefix) into a 20-byte array.
/// Asserts on failure so tests abort clearly if the constant is malformed.
eth::codec::Address parse_contract(std::string_view hex_addr)
{
    auto result = eth::cli::parse_address(hex_addr);
    if (!result.has_value())
    {
        ADD_FAILURE() << "Failed to parse contract address: " << hex_addr;
        return {};
    }
    return result.value();
}

/// Build a 32-byte ABI word with an address right-aligned (left-padded with zeros).
eth::codec::Hash256 make_address_word(const eth::codec::Address& addr)
{
    eth::codec::Hash256 word{};
    std::copy(addr.begin(), addr.end(), word.begin() + 12);
    return word;
}

/// Append a uint64 value as a big-endian 32-byte ABI word to a byte buffer.
void append_uint256(eth::codec::ByteBuffer& buf, uint64_t value)
{
    for (int i = 0; i < 24; ++i) { buf.push_back(0); }
    for (int i = 7; i >= 0; --i)
    {
        buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

/// Build a minimal ERC-20 Transfer log for a given contract address.
eth::codec::LogEntry make_transfer_log(
    const eth::codec::Address& contract,
    const eth::codec::Address& from,
    const eth::codec::Address& to,
    uint64_t                   amount)
{
    eth::codec::LogEntry log;
    log.address = contract;
    log.topics.push_back(eth::abi::event_signature_hash("Transfer(address,address,uint256)"));
    log.topics.push_back(make_address_word(from));
    log.topics.push_back(make_address_word(to));
    append_uint256(log.data, amount);
    return log;
}

/// Standard Transfer ABI param list.
std::vector<eth::abi::AbiParam> transfer_params()
{
    return {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };
}

/// Shared test body: register a watch for contract_addr, inject a synthetic
/// Transfer receipt, and assert the callback fires with the correct values.
void run_transfer_watch_test(std::string_view contract_hex,
                             uint64_t         expected_amount,
                             uint64_t         block_number)
{
    const eth::codec::Address contract = parse_contract(contract_hex);

    const eth::codec::Address sender   = {
        0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    };
    const eth::codec::Address receiver = {
        0xCA, 0xFE, 0xBA, 0xBE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
    };

    struct Capture {
        int      call_count       = 0;
        uint64_t block_number     = 0;
        eth::codec::Address from_addr;
        eth::codec::Address to_addr;
        intx::uint256       value;
    } capture;

    eth::EthWatchService svc;
    svc.watch_event(
        contract,
        "Transfer(address,address,uint256)",
        transfer_params(),
        [&capture](const eth::MatchedEvent& ev, const std::vector<eth::abi::AbiValue>& vals)
        {
            ++capture.call_count;
            capture.block_number = ev.block_number;

            if (vals.size() >= 1)
            {
                if (const auto* a = std::get_if<eth::codec::Address>(&vals[0]))
                {
                    capture.from_addr = *a;
                }
            }
            if (vals.size() >= 2)
            {
                if (const auto* a = std::get_if<eth::codec::Address>(&vals[1]))
                {
                    capture.to_addr = *a;
                }
            }
            if (vals.size() >= 3)
            {
                if (const auto* u = std::get_if<intx::uint256>(&vals[2]))
                {
                    capture.value = *u;
                }
            }
        });

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(contract, sender, receiver, expected_amount));

    const eth::Hash256 block_hash{};
    svc.process_receipts({receipt}, {{}}, block_number, block_hash);

    EXPECT_EQ(capture.call_count,   1);
    EXPECT_EQ(capture.block_number, block_number);
    EXPECT_EQ(capture.from_addr,    sender);
    EXPECT_EQ(capture.to_addr,      receiver);
    EXPECT_EQ(capture.value,        intx::uint256(expected_amount));
}

/// Assert that a Transfer log for a DIFFERENT contract does NOT trigger the callback.
void run_wrong_contract_test(std::string_view watched_hex,
                             std::string_view other_hex)
{
    const eth::codec::Address watched = parse_contract(watched_hex);
    const eth::codec::Address other   = parse_contract(other_hex);
    const eth::codec::Address from    = {0x11};
    const eth::codec::Address to      = {0x22};

    int call_count = 0;
    eth::EthWatchService svc;
    svc.watch_event(watched, "Transfer(address,address,uint256)", transfer_params(),
        [&call_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&)
        {
            ++call_count;
        });

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(other, from, to, 1ULL));

    svc.process_receipts({receipt}, {{}}, 1, {});
    EXPECT_EQ(call_count, 0);
}

} // namespace

// ============================================================================
// parse_address — verify all GNUS contract addresses parse correctly
// ============================================================================

TEST(GnusContractsTest, ParseEthereumMainnetAddress)
{
    auto addr = eth::cli::parse_address(kGnusEthereum);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(addr->at(0),  0x61);
    EXPECT_EQ(addr->at(19), 0x5a);
}

TEST(GnusContractsTest, ParseSepoliaTestnetAddress)
{
    auto addr = eth::cli::parse_address(kGnusSepolia);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(addr->at(0),  0x9a);
    EXPECT_EQ(addr->at(19), 0x70);
}

TEST(GnusContractsTest, ParsePolygonMainnetAddress)
{
    auto addr = eth::cli::parse_address(kGnusPolygon);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(addr->at(0),  0x12);
    EXPECT_EQ(addr->at(19), 0x19);
}

TEST(GnusContractsTest, ParsePolygonAmoyAddress)
{
    auto addr = eth::cli::parse_address(kGnusPolygonAmoy);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(addr->at(0),  0xeC);
    EXPECT_EQ(addr->at(19), 0xeB);
}

TEST(GnusContractsTest, EthereumBscBaseShareSameAddress)
{
    const auto eth_addr  = eth::cli::parse_address(kGnusEthereum);
    const auto bsc_addr  = eth::cli::parse_address(kGnusBsc);
    const auto base_addr = eth::cli::parse_address(kGnusBase);

    ASSERT_TRUE(eth_addr.has_value());
    ASSERT_TRUE(bsc_addr.has_value());
    ASSERT_TRUE(base_addr.has_value());
    EXPECT_EQ(*eth_addr, *bsc_addr);
    EXPECT_EQ(*eth_addr, *base_addr);
}

TEST(GnusContractsTest, TestnetAmoyBscBaseShareSameAddress)
{
    const auto amoy_addr = eth::cli::parse_address(kGnusPolygonAmoy);
    const auto bsc_addr  = eth::cli::parse_address(kGnusBscTestnet);
    const auto base_addr = eth::cli::parse_address(kGnusBaseTestnet);

    ASSERT_TRUE(amoy_addr.has_value());
    ASSERT_TRUE(bsc_addr.has_value());
    ASSERT_TRUE(base_addr.has_value());
    EXPECT_EQ(*amoy_addr, *bsc_addr);
    EXPECT_EQ(*amoy_addr, *base_addr);
}

// ============================================================================
// Transfer event watching — mainnet contracts
// ============================================================================

TEST(GnusContractsTest, WatchTransferOnEthereumMainnet)
{
    run_transfer_watch_test(kGnusEthereum, 1000000000000000000ULL, 21000000);
}

TEST(GnusContractsTest, WatchTransferOnPolygonMainnet)
{
    run_transfer_watch_test(kGnusPolygon, 500000000000000000ULL, 55000000);
}

TEST(GnusContractsTest, WatchTransferOnBscMainnet)
{
    run_transfer_watch_test(kGnusBsc, 250000000000000000ULL, 38000000);
}

TEST(GnusContractsTest, WatchTransferOnBaseMainnet)
{
    run_transfer_watch_test(kGnusBase, 100000000000000000ULL, 12000000);
}

// ============================================================================
// Transfer event watching — testnet contracts
// ============================================================================

TEST(GnusContractsTest, WatchTransferOnSepolia)
{
    run_transfer_watch_test(kGnusSepolia, 42000000000000000ULL, 7500000);
}

TEST(GnusContractsTest, WatchTransferOnPolygonAmoy)
{
    run_transfer_watch_test(kGnusPolygonAmoy, 10000000000000000ULL, 9000000);
}

TEST(GnusContractsTest, WatchTransferOnBscTestnet)
{
    run_transfer_watch_test(kGnusBscTestnet, 99000000000000000ULL, 45000000);
}

TEST(GnusContractsTest, WatchTransferOnBaseTestnet)
{
    run_transfer_watch_test(kGnusBaseTestnet, 1000000000000000ULL, 5000000);
}

// ============================================================================
// Contract address filtering — wrong contract must not fire
// ============================================================================

TEST(GnusContractsTest, SepoliaWatchDoesNotFireForEthereumContract)
{
    run_wrong_contract_test(kGnusSepolia, kGnusEthereum);
}

TEST(GnusContractsTest, PolygonWatchDoesNotFireForAmoyContract)
{
    run_wrong_contract_test(kGnusPolygon, kGnusPolygonAmoy);
}

TEST(GnusContractsTest, EthereumWatchDoesNotFireForPolygonContract)
{
    run_wrong_contract_test(kGnusEthereum, kGnusPolygon);
}

// ============================================================================
// Multi-network watch — all contracts registered, only matching one fires
// ============================================================================

TEST(GnusContractsTest, MultiNetworkWatchOnlyMatchingContractFires)
{
    const auto sepolia_contract = parse_contract(kGnusSepolia);
    const auto amoy_contract    = parse_contract(kGnusPolygonAmoy);
    const auto eth_contract     = parse_contract(kGnusEthereum);

    const eth::codec::Address from = {0x11};
    const eth::codec::Address to   = {0x22};

    int sepolia_count = 0;
    int amoy_count    = 0;
    int eth_count     = 0;

    eth::EthWatchService svc;

    svc.watch_event(sepolia_contract, "Transfer(address,address,uint256)", transfer_params(),
        [&sepolia_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++sepolia_count; });
    svc.watch_event(amoy_contract, "Transfer(address,address,uint256)", transfer_params(),
        [&amoy_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++amoy_count; });
    svc.watch_event(eth_contract, "Transfer(address,address,uint256)", transfer_params(),
        [&eth_count](const eth::MatchedEvent&, const std::vector<eth::abi::AbiValue>&) { ++eth_count; });

    // Fire only a Sepolia Transfer
    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.logs.push_back(make_transfer_log(sepolia_contract, from, to, 100ULL));

    svc.process_receipts({receipt}, {{}}, 7500001, {});

    EXPECT_EQ(sepolia_count, 1);
    EXPECT_EQ(amoy_count,    0);
    EXPECT_EQ(eth_count,     0);
}

// ============================================================================
// CLI parse_address round-trip — all GNUS addresses survive encode→parse
// ============================================================================

TEST(GnusContractsTest, AllGnusAddressesParseWithoutPrefix)
{
    // Verify bare hex (no 0x) also works — CLI supports both
    const std::string_view addresses[] = {
        "614577036F0a024DBC1C88BA616b394DD65d105a",  // Ethereum mainnet
        "127E47abA094a9a87D084a3a93732909Ff031419",  // Polygon mainnet
        "9af8050220D8C355CA3c6dC00a78B474cd3e3c70",  // Sepolia
        "eC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB",  // Amoy / BSC testnet / Base testnet
    };

    for (const auto& hex : addresses)
    {
        auto result = eth::cli::parse_address(hex);
        EXPECT_TRUE(result.has_value()) << "Failed to parse: " << hex;
    }
}

