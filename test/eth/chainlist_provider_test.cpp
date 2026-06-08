// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/chainlist_provider.hpp>
#include <eth/rpc_manager_config.hpp>

namespace {

using namespace eth::rpc;

TEST(ChainlistProviderTest, ParsesValidAggregatedJson)
{
    const char* kJson = R"([
        {"name":"Ethereum Mainnet","chainId":1,"rpc":["https://mainnet.infura.io/v3/key","https://cloudflare-eth.com"]},
        {"name":"Polygon Mainnet","chainId":137,"rpc":["https://polygon-rpc.com"]}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    const auto& endpoints = result.value();
    EXPECT_EQ( endpoints.size(), 3U );

    bool has_mainnet = false;
    bool has_polygon = false;
    for ( const auto& ep : endpoints )
    {
        if ( ep.chain_id == 1 )
        {
            has_mainnet = true;
            EXPECT_EQ( ep.chain_name, "Ethereum Mainnet" );
        }
        if ( ep.chain_id == 137 )
        {
            has_polygon = true;
            EXPECT_EQ( ep.chain_name, "Polygon Mainnet" );
        }
    }
    EXPECT_TRUE( has_mainnet );
    EXPECT_TRUE( has_polygon );
}

TEST(ChainlistProviderTest, FiltersDeprecatedChains)
{
    const char* kJson = R"([
        {"name":"Old Chain","chainId":99,"rpc":["https://old.example.com"],"status":"deprecated"}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    EXPECT_TRUE( result.value().empty() );
}

TEST(ChainlistProviderTest, DefaultsMissingStatusToActive)
{
    const char* kJson = R"([
        {"name":"No Status Chain","chainId":42,"rpc":["https://nostatus.example.com"]}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value().size(), 1U );
}

TEST(ChainlistProviderTest, FiltersApiKeyPlaceholders)
{
    const char* kJson = R"([
        {"name":"Test Chain","chainId":1,"rpc":[
            "https://rpc.example.com",
            "https://mainnet.infura.io/v3/${INFURA_API_KEY}",
            "https://eth-mainnet.g.alchemy.com/v2/${ALCHEMY_API_KEY}",
            "https://rpc.ankr.com/eth/${ANKR_API_KEY}",
            "https://eth-mainnet.gateway.pokt.network/v1/lb/${POKT_API_KEY}",
            "https://eth-mainnet.blastapi.io/${BLASTAPI_API_KEY}"
        ]}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    const auto& endpoints = result.value();
    EXPECT_EQ( endpoints.size(), 1U );
    EXPECT_EQ( endpoints[0].url_template, "https://rpc.example.com" );
}

TEST(ChainlistProviderTest, FiltersWebsocketUrls)
{
    const char* kJson = R"([
        {"name":"Test","chainId":1,"rpc":[
            "https://rpc.example.com",
            "wss://ws.example.com",
            "ws://ws2.example.com"
        ]}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    const auto& endpoints = result.value();
    EXPECT_EQ( endpoints.size(), 1U );
    EXPECT_EQ( endpoints[0].url_template, "https://rpc.example.com" );
}

TEST(ChainlistProviderTest, FiltersMalformedUrls)
{
    const char* kJson = R"([
        {"name":"Test","chainId":1,"rpc":[
            "https://valid.example.com",
            "",
            "not-a-url",
            "ftp://invalid.example.com"
        ]}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    const auto& endpoints = result.value();
    EXPECT_EQ( endpoints.size(), 1U );
    EXPECT_EQ( endpoints[0].url_template, "https://valid.example.com" );
}

TEST(ChainlistProviderTest, DeduplicatesRepeatedEndpoints)
{
    const char* kJson = R"([
        {"name":"Test","chainId":1,"rpc":[
            "https://rpc.example.com",
            "https://rpc.example.com",
            "https://rpc2.example.com"
        ]}
    ])";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    EXPECT_EQ( result.value().size(), 2U );
}

TEST(ChainlistProviderTest, ExcludesChainsNotInConfig)
{
    const char* kJson = R"([
        {"name":"Chain A","chainId":1,"rpc":["https://a.example.com"]},
        {"name":"Chain B","chainId":2,"rpc":["https://b.example.com"]},
        {"name":"Chain C","chainId":3,"rpc":["https://c.example.com"]}
    ])";

    auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );

    const std::vector<uint64_t> kConfigured = {1, 3};
    const auto filtered = filter_to_configured_chains(
        std::move( result.value() ), kConfigured );

    EXPECT_EQ( filtered.size(), 2U );
    bool has_chain1 = false;
    bool has_chain3 = false;
    for ( const auto& ep : filtered )
    {
        if ( ep.chain_id == 1 )
        {
            has_chain1 = true;
        }
        if ( ep.chain_id == 3 )
        {
            has_chain3 = true;
        }
    }
    EXPECT_TRUE( has_chain1 );
    EXPECT_TRUE( has_chain3 );
}

TEST(ChainlistProviderTest, RejectsInvalidJson)
{
    const auto result = load_chainlist_from_json_text( "not valid json" );
    EXPECT_FALSE( result.has_value() );
}

TEST(ChainlistProviderTest, HandlesEmptyArray)
{
    const char* kJson = "[]";

    const auto result = load_chainlist_from_json_text( kJson );
    ASSERT_TRUE( result.has_value() );
    EXPECT_TRUE( result.value().empty() );
}

} // namespace
