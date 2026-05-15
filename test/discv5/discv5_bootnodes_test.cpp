// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// Bootnode source and chain registry tests.

#include <gtest/gtest.h>
#include "discv5/discv5_bootnodes.hpp"

namespace discv5
{
namespace
{

class BootnodeSourceTest : public ::testing::Test {};

// ---------------------------------------------------------------------------
// StaticEnrBootnodeSource
// ---------------------------------------------------------------------------

/// @test fetch() returns all URIs passed at construction.
TEST_F(BootnodeSourceTest, StaticEnrSourceReturnsAllUris)
{
    const std::vector<std::string> uris = { "enr:-abc", "enr:-def" };
    StaticEnrBootnodeSource source(uris, "test-enr");

    const auto fetched = source.fetch();
    ASSERT_EQ(fetched.size(), uris.size());
    EXPECT_EQ(fetched[0], "enr:-abc");
    EXPECT_EQ(fetched[1], "enr:-def");
}

/// @test name() returns the label passed at construction.
TEST_F(BootnodeSourceTest, StaticEnrSourceName)
{
    StaticEnrBootnodeSource source({}, "my-label");
    EXPECT_EQ(source.name(), "my-label");
}

// ---------------------------------------------------------------------------
// StaticEnodeBootnodeSource
// ---------------------------------------------------------------------------

/// @test fetch() returns all enode URIs.
TEST_F(BootnodeSourceTest, StaticEnodeSourceReturnsAllUris)
{
    const std::vector<std::string> uris = { "enode://aabb@1.2.3.4:30303" };
    StaticEnodeBootnodeSource source(uris, "test-enode");

    const auto fetched = source.fetch();
    ASSERT_EQ(fetched.size(), 1U);
    EXPECT_EQ(fetched[0], "enode://aabb@1.2.3.4:30303");
}

// ---------------------------------------------------------------------------
// ChainBootnodeRegistry — for_chain(ChainId)
// ---------------------------------------------------------------------------

/// @test for_chain returns non-null source for every known ChainId.
TEST_F(BootnodeSourceTest, ForChainEnumAllKnownChains)
{
    const ChainId known_chains[] =
    {
        ChainId::kEthereumMainnet,
        ChainId::kEthereumSepolia,
        ChainId::kEthereumHolesky,
        ChainId::kPolygonMainnet,
        ChainId::kPolygonAmoy,
        ChainId::kBscMainnet,
        ChainId::kBscTestnet,
        ChainId::kBaseMainnet,
        ChainId::kBaseSepolia,
    };

    for (const ChainId id : known_chains)
    {
        auto source = ChainBootnodeRegistry::for_chain(id);
        ASSERT_NE(source, nullptr)
            << "Expected non-null source for chain "
            << ChainBootnodeRegistry::chain_name(id);
        EXPECT_FALSE(source->name().empty())
            << "Source name must not be empty for chain "
            << ChainBootnodeRegistry::chain_name(id);
    }
}

/// @test Ethereum mainnet source returns at least one ENR URI.
TEST_F(BootnodeSourceTest, EthereumMainnetHasEnrBootnodes)
{
    auto source = ChainBootnodeRegistry::for_chain(ChainId::kEthereumMainnet);
    ASSERT_NE(source, nullptr);

    const auto uris = source->fetch();
    ASSERT_FALSE(uris.empty()) << "Ethereum mainnet must have at least one bootnode";

    // Every URI must start with "enr:".
    for (const auto& uri : uris)
    {
        EXPECT_EQ(uri.rfind("enr:", 0U), 0U)
            << "Ethereum mainnet bootnodes should be ENR URIs; got: " << uri;
    }
}

/// @test Sepolia source returns only enode:// URIs.
TEST_F(BootnodeSourceTest, SepoliaHasEnodeBootnodes)
{
    auto source = ChainBootnodeRegistry::for_chain(ChainId::kEthereumSepolia);
    ASSERT_NE(source, nullptr);

    const auto uris = source->fetch();
    ASSERT_FALSE(uris.empty()) << "Sepolia must have at least one bootnode";

    for (const auto& uri : uris)
    {
        EXPECT_EQ(uri.rfind("enode://", 0U), 0U)
            << "Sepolia bootnodes should be enode:// URIs; got: " << uri;
    }
}

/// @test BSC mainnet source has non-empty list.
TEST_F(BootnodeSourceTest, BscMainnetHasBootnodes)
{
    auto source = ChainBootnodeRegistry::for_chain(ChainId::kBscMainnet);
    ASSERT_NE(source, nullptr);
    EXPECT_FALSE(source->fetch().empty()) << "BSC mainnet must have at least one bootnode";
}

// ---------------------------------------------------------------------------
// ChainBootnodeRegistry — for_chain(uint64_t)
// ---------------------------------------------------------------------------

/// @test for_chain(uint64_t) resolves Ethereum mainnet by its chain ID.
TEST_F(BootnodeSourceTest, ForChainIntegerEthereumMainnet)
{
    static constexpr uint64_t kEthMainnetId = 1U;
    auto source = ChainBootnodeRegistry::for_chain(kEthMainnetId);
    ASSERT_NE(source, nullptr)
        << "Expected non-null source for chain id 1 (Ethereum mainnet)";
}

/// @test for_chain(uint64_t) resolves Polygon mainnet.
TEST_F(BootnodeSourceTest, ForChainIntegerPolygonMainnet)
{
    static constexpr uint64_t kPolygonId = 137U;
    auto source = ChainBootnodeRegistry::for_chain(kPolygonId);
    ASSERT_NE(source, nullptr)
        << "Expected non-null source for chain id 137 (Polygon mainnet)";
}

/// @test for_chain(uint64_t) returns nullptr for an unknown chain ID.
TEST_F(BootnodeSourceTest, ForChainIntegerUnknown)
{
    static constexpr uint64_t kUnknownId = 99999U;
    auto source = ChainBootnodeRegistry::for_chain(kUnknownId);
    EXPECT_EQ(source, nullptr)
        << "Expected nullptr for unknown chain id " << kUnknownId;
}

// ---------------------------------------------------------------------------
// ChainBootnodeRegistry — chain_name
// ---------------------------------------------------------------------------

/// @test chain_name returns a non-empty string for every known chain.
TEST_F(BootnodeSourceTest, ChainNameNonEmptyForAllKnownChains)
{
    const ChainId chains[] =
    {
        ChainId::kEthereumMainnet, ChainId::kEthereumSepolia,
        ChainId::kPolygonMainnet,  ChainId::kBscMainnet,
    };

    for (const ChainId id : chains)
    {
        const char* name = ChainBootnodeRegistry::chain_name(id);
        ASSERT_NE(name, nullptr);
        EXPECT_GT(std::strlen(name), 0U)
            << "chain_name should be non-empty";
    }
}

} // anonymous namespace
} // namespace discv5
