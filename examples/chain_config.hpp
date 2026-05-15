// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

/// @brief Search for chains.json next to the binary, then in CWD.
///        Parse it and return the 4-byte fork hash for @p chain.
///
/// chains.json format (simple name → 8 hex-char string):
/// @code
///   { "sepolia": "268956b6", "mainnet": "07c9462e" }
/// @endcode
///
/// @param chain  Chain name key, e.g. "sepolia".
/// @param argv0  Value of argv[0] used to locate the binary directory.
/// @return Parsed 4-byte fork hash, or nullopt if file/key not found.
[[nodiscard]] inline std::optional<std::array<uint8_t, 4U>>
load_fork_hash( const std::string& chain, const std::string& argv0 ) noexcept
{
    const std::filesystem::path bin_dir =
        std::filesystem::path( argv0 ).parent_path();

    const std::filesystem::path candidates[] = {
        bin_dir / "chains.json",
        std::filesystem::path( "chains.json" )
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
        if ( !entry )
        {
            continue;
        }

        const boost::json::string* hex = entry->if_string();
        if ( !hex || hex->size() != 8U )
        {
            continue;
        }

        auto nibble = []( char c ) -> std::optional<uint8_t>
        {
            if ( c >= '0' && c <= '9' ) { return static_cast<uint8_t>( c - '0' ); }
            if ( c >= 'a' && c <= 'f' ) { return static_cast<uint8_t>( 10 + c - 'a' ); }
            if ( c >= 'A' && c <= 'F' ) { return static_cast<uint8_t>( 10 + c - 'A' ); }
            return std::nullopt;
        };

        std::array<uint8_t, 4U> hash{};
        bool ok = true;
        for ( size_t i = 0; i < 4U && ok; ++i )
        {
            const auto hi = nibble( ( *hex )[i * 2U] );
            const auto lo = nibble( ( *hex )[i * 2U + 1U] );
            if ( !hi || !lo )
            {
                ok = false;
                break;
            }
            hash[i] = static_cast<uint8_t>( ( *hi << 4U ) | *lo );
        }

        if ( ok )
        {
            return hash;
        }
    }

    return std::nullopt;
}

