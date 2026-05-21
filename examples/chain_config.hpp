// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
#ifndef EVMRELAY_EXAMPLES_CHAIN_CONFIG_HPP
#define EVMRELAY_EXAMPLES_CHAIN_CONFIG_HPP

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <discv4/chain_peers.hpp>

namespace evmrelay::examples
{

[[nodiscard]] inline std::optional<discv4::ChainDiscoveryDefault>
parse_chain_discovery_default( const std::string_view value ) noexcept
{
    if ( value == "auto" )
    {
        return discv4::ChainDiscoveryDefault::kAuto;
    }
    if ( value == "discv4" )
    {
        return discv4::ChainDiscoveryDefault::kDiscv4;
    }
    if ( value == "cache-enr-discv5" )
    {
        return discv4::ChainDiscoveryDefault::kCacheEnrDiscv5;
    }
    if ( value == "enr-tree" )
    {
        return discv4::ChainDiscoveryDefault::kEnrTree;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<discv4::DiscoveryForkFilter>
parse_discovery_fork_filter( const std::string_view value ) noexcept
{
    if ( value == "require" )
    {
        return discv4::DiscoveryForkFilter::kRequire;
    }
    if ( value == "disabled" )
    {
        return discv4::DiscoveryForkFilter::kDisabled;
    }
    return std::nullopt;
}

[[nodiscard]] inline std::optional<boost::json::object>
load_chain_config_root( const std::string& argv0 ) noexcept
{
    const std::filesystem::path bin_dir =
        std::filesystem::path( argv0 ).parent_path();

    const std::filesystem::path candidates[] = {
        bin_dir / "chains_config.json",
        std::filesystem::path( "chains_config.json" )
    };

    for ( const auto& candidate : candidates )
    {
        std::ifstream file( candidate );
        if ( !file.is_open() )
        {
            continue;
        }

        boost::system::error_code ec;
        const boost::json::value jval = boost::json::parse( file, ec );
        if ( ec )
        {
            continue;
        }

        const boost::json::object* obj = jval.if_object();
        if ( !obj )
        {
            continue;
        }

        return *obj;
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<boost::json::object>
load_chain_config_entry( const std::string& chain, const std::string& argv0 ) noexcept
{
    const auto root = load_chain_config_root( argv0 );
    if ( !root.has_value() )
    {
        return std::nullopt;
    }

    const boost::json::value* entry = root->if_contains( chain );
    if ( !entry || !entry->is_object() )
    {
        return std::nullopt;
    }

    return entry->as_object();
}

[[nodiscard]] inline std::vector<std::string>
load_default_all_chains( const std::string& argv0 ) noexcept
{
    std::vector<std::string> chains;
    const auto root = load_chain_config_root( argv0 );
    if ( !root.has_value() )
    {
        return chains;
    }

    const auto* value = root->if_contains( "_defaultAllChains" );
    const auto* array = value == nullptr ? nullptr : value->if_array();
    if ( array == nullptr )
    {
        return chains;
    }

    for ( const auto& item : *array )
    {
        const auto* chain = item.if_string();
        if ( chain != nullptr && !chain->empty() )
        {
            chains.emplace_back( chain->data(), chain->size() );
        }
    }

    return chains;
}

/// @brief Load the latest fork hash from generated chain_enodes.json/.gz.
///
/// Fork IDs are generated into chain_enodes.json(.gz), so examples should read
/// them from that cache.
///
/// @param chain  Canonical chain key, e.g. "ethereum-sepolia".
/// @param argv0  Value of argv[0] used to locate files next to the binary.
/// @return Parsed 4-byte fork hash, or nullopt if no cache/key/forkId is found.
[[nodiscard]] inline std::optional<std::array<uint8_t, 4U>>
load_fork_hash( const std::string& chain, const std::string& argv0 ) noexcept
{
    const std::filesystem::path bin_dir = std::filesystem::path( argv0 ).parent_path();
    const std::filesystem::path candidates[] = {
        bin_dir / "chain_enodes.json",
        bin_dir / "chain_enodes.json.gz",
        std::filesystem::path( "chain_enodes.json" ),
        std::filesystem::path( "chain_enodes.json.gz" )
    };

    for ( const auto& candidate : candidates )
    {
        if ( !std::filesystem::is_regular_file( candidate ) )
        {
            continue;
        }

        const auto config = discv4::load_chain_peer_config_from_json( chain, candidate );
        if ( config.has_value() && config->fork_id.has_value() )
        {
            return config->fork_id->fork_hash;
        }
    }

    return std::nullopt;
}

[[nodiscard]] inline std::optional<discv4::ChainPeerConfig>
load_chain_peer_config(
    const std::string& chain_name,
    const std::string& argv0,
    const std::string& chain_peers_json_path,
    const std::string& chain_peers_url,
    bool               chain_peers_url_enabled ) noexcept
{
    std::optional<discv4::ChainPeerCacheRefreshResult> refresh_result;
    if ( chain_peers_json_path.empty() && chain_peers_url_enabled )
    {
        refresh_result = discv4::refresh_chain_peer_cache_json(
            discv4::chain_peer_cache_json_path( argv0 ),
            chain_peers_url );
    }

    const auto chain_peers_json_file =
        discv4::find_chain_peer_cache_json_path( argv0, chain_peers_json_path );
    if ( chain_peers_json_file.has_value() )
    {
        return discv4::load_chain_peer_config_from_json( chain_name, *chain_peers_json_file );
    }
    if ( refresh_result.has_value() && refresh_result->cache_available )
    {
        return discv4::load_chain_peer_config_from_json( chain_name, refresh_result->cache_path );
    }
    return std::nullopt;
}

/// @brief Return the configured EIP-1459 ENR-tree root URL for @p chain.
[[nodiscard]] inline std::optional<std::string>
load_enr_tree_url( const std::string& chain, const std::string& argv0 ) noexcept
{
    const auto entry = load_chain_config_entry( chain, argv0 );
    if ( !entry.has_value() )
    {
        return std::nullopt;
    }

    const auto* enr_tree = entry->if_contains( "enrTree" );
    const boost::json::string* value = enr_tree == nullptr ? nullptr : enr_tree->if_string();
    if ( !value || value->empty() )
    {
        return std::nullopt;
    }

    return std::string( value->data(), value->size() );
}

/// @brief Apply data-driven discovery defaults from chains_config.json.
inline void apply_chain_discovery_config(
    discv4::ChainPeerConfig& chain_peer_config,
    const std::string&       argv0 ) noexcept
{
    const auto entry = load_chain_config_entry( chain_peer_config.canonical_name, argv0 );
    if ( !entry.has_value() )
    {
        return;
    }

    const auto* discovery_default = entry->if_contains( "discoveryDefault" );
    if ( discovery_default != nullptr && discovery_default->is_string() )
    {
        const auto& value = discovery_default->as_string();
        if ( const auto parsed_default = parse_chain_discovery_default(
                 std::string_view( value.data(), value.size() ) );
             parsed_default.has_value() )
        {
            chain_peer_config.discovery_default = *parsed_default;
        }
    }

    if ( const auto* enr_tree = entry->if_contains( "enrTree" );
         enr_tree != nullptr && enr_tree->is_string() )
    {
        const auto& value = enr_tree->as_string();
        if ( !value.empty() )
        {
            chain_peer_config.enr_trees = {std::string( value.data(), value.size() )};
        }
    }

    if ( const auto* enr_trees = entry->if_contains( "enrTrees" );
         enr_trees != nullptr && enr_trees->is_array() )
    {
        std::vector<std::string> trees;
        for ( const auto& item : enr_trees->as_array() )
        {
            if ( !item.is_string() )
            {
                continue;
            }
            const auto& value = item.as_string();
            if ( !value.empty() )
            {
                trees.emplace_back( value.data(), value.size() );
            }
        }
        if ( !trees.empty() )
        {
            chain_peer_config.enr_trees = std::move( trees );
        }
    }

    const auto* discovery_fork_filter = entry->if_contains( "discoveryForkFilter" );
    if ( discovery_fork_filter != nullptr && discovery_fork_filter->is_string() )
    {
        const auto& value = discovery_fork_filter->as_string();
        if ( const auto parsed_filter = parse_discovery_fork_filter(
                 std::string_view( value.data(), value.size() ) );
             parsed_filter.has_value() )
        {
            chain_peer_config.discovery_fork_filter = *parsed_filter;
        }
    }
}

} // namespace evmrelay::examples

using evmrelay::examples::apply_chain_discovery_config;
using evmrelay::examples::load_chain_peer_config;
using evmrelay::examples::load_default_all_chains;
using evmrelay::examples::load_enr_tree_url;
using evmrelay::examples::load_fork_hash;

#endif // EVMRELAY_EXAMPLES_CHAIN_CONFIG_HPP
