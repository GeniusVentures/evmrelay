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

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <discv4/chain_peers.hpp>

/// @brief Load the latest fork hash from generated chain_enodes.json/.gz.
///
/// `chains_config.json` is discovery-root config only. Fork IDs are generated
/// into chain_enodes.json(.gz), so examples should read them from that cache.
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

/// @brief Return the configured EIP-1459 ENR-tree root URL for @p chain.
[[nodiscard]] inline std::optional<std::string>
load_enr_tree_url( const std::string& chain, const std::string& argv0 ) noexcept
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

        const boost::json::value* entry = obj->if_contains( chain );
        if ( !entry || !entry->is_object() )
        {
            continue;
        }

        const auto* enr_tree = entry->as_object().if_contains( "enrTree" );
        const boost::json::string* value = enr_tree == nullptr ? nullptr : enr_tree->if_string();
        if ( !value || value->empty() )
        {
            continue;
        }

        return std::string( value->data(), value->size() );
    }

    return std::nullopt;
}

#endif // EVMRELAY_EXAMPLES_CHAIN_CONFIG_HPP
