// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// examples/discovery/test_enr_survey.cpp
//
// Diagnostic live test: run discv4 discovery with **no pre-dial filter**, collect
// every DiscoveredPeer produced by the ENR-enrichment path, and at the end print
// a frequency table of the eth fork-hashes actually seen in live ENR responses.
//
// This intentionally does zero dialing / RLPx — its only purpose is to determine:
//   1. Whether request_enr() is successfully completing for live Sepolia peers.
//   2. Which fork-hash bytes actually appear in live ENR `eth` entries.
//   3. Whether the Sepolia fork-hash used by make_fork_id_filter() is correct.
//
// Usage:
//   ./test_enr_survey [--log-level debug] [--timeout 60]

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>

#include <discv4/bootnodes_test.hpp>
#include <discv4/discv4_client.hpp>
#include <rlpx/crypto/ecdh.hpp>
#include <base/rlp-logger.hpp>

#include <spdlog/spdlog.h>

#include "../chain_config.hpp"

// ── Helpers ───────────────────────────────────────────────────────────────────

/// @brief Format a 4-byte array as "aa bb cc dd".
static std::string format_hash4( const std::array<uint8_t, 4U>& h ) noexcept
{
    std::ostringstream oss;
    oss << std::hex << std::setfill( '0' );
    for ( size_t i = 0; i < h.size(); ++i )
    {
        if ( i != 0 ) { oss << ' '; }
        oss << std::setw( 2 ) << static_cast<unsigned>( h[i] );
    }
    return oss.str();
}

// ── main ──────────────────────────────────────────────────────────────────────

int main( int argc, char** argv )
{
    int timeout_secs = 60;

    for ( int i = 1; i < argc; ++i )
    {
        std::string_view arg( argv[i] );
        if ( arg == "--log-level" && i + 1 < argc )
        {
            std::string_view lvl( argv[++i] );
            if ( lvl == "debug" )      { spdlog::set_level( spdlog::level::debug ); }
            else if ( lvl == "info" )  { spdlog::set_level( spdlog::level::info );  }
            else if ( lvl == "warn" )  { spdlog::set_level( spdlog::level::warn );  }
            else if ( lvl == "off" )   { spdlog::set_level( spdlog::level::off );   }
        }
        else if ( arg == "--timeout" && i + 1 < argc )
        {
            timeout_secs = std::atoi( argv[++i] );
        }
    }

    boost::asio::io_context io;

    // ── discv4 setup (identical to test_discovery) ────────────────────────────
    auto keypair_result = rlpx::crypto::Ecdh::generate_ephemeral_keypair();
    if ( !keypair_result )
    {
        std::cout << "Failed to generate keypair\n";
        return 1;
    }
    const auto& keypair = keypair_result.value();

    discv4::discv4Config dv4_cfg;
    dv4_cfg.bind_port = 0;
    std::copy( keypair.private_key.begin(), keypair.private_key.end(), dv4_cfg.private_key.begin() );
    std::copy( keypair.public_key.begin(),  keypair.public_key.end(),  dv4_cfg.public_key.begin() );

    auto dv4 = std::make_shared<discv4::discv4_client>( io, dv4_cfg );

    // ── Counters (written only from the single io_context thread) ─────────────
    std::atomic<int>  peers_total{ 0 };
    std::atomic<int>  peers_with_fork_id{ 0 };
    std::atomic<int>  peers_without_fork_id{ 0 };

    // fork_hash → count  (only safe to read after io.run() returns)
    std::map<std::array<uint8_t, 4U>, int> fork_hash_counts;

    // ── Peer callback: record without filtering ────────────────────────────────
    dv4->set_peer_discovered_callback(
        [&peers_total, &peers_with_fork_id, &peers_without_fork_id, &fork_hash_counts]
        ( const discv4::DiscoveredPeer& peer )
        {
            ++peers_total;
            if ( peer.eth_fork_id.has_value() )
            {
                ++peers_with_fork_id;
                fork_hash_counts[peer.eth_fork_id.value().hash]++;
            }
            else
            {
                ++peers_without_fork_id;
            }
        } );

    dv4->set_error_callback( []( const std::string& ) {} );

    // ── Timeout ───────────────────────────────────────────────────────────────
    boost::asio::steady_timer deadline( io, std::chrono::seconds( timeout_secs ) );
    deadline.async_wait( [&]( boost::system::error_code )
    {
        dv4->stop();
        io.stop();
    } );

    // ── Signal handler ────────────────────────────────────────────────────────
    boost::asio::signal_set signals( io, SIGINT, SIGTERM );
    signals.async_wait( [&]( boost::system::error_code, int )
    {
        deadline.cancel();
        dv4->stop();
        io.stop();
    } );

    // ── Seed discovery with Sepolia bootnodes (identical to test_discovery) ───
    auto parse_enode = []( const std::string& enode )
        -> std::optional<std::tuple<std::string, uint16_t, std::string>>
    {
        const std::string prefix = "enode://";
        if ( enode.substr( 0, prefix.size() ) != prefix ) { return std::nullopt; }
        const auto at = enode.find( '@', prefix.size() );
        if ( at == std::string::npos ) { return std::nullopt; }
        const auto colon = enode.rfind( ':' );
        if ( colon == std::string::npos || colon < at ) { return std::nullopt; }
        std::string pubkey = enode.substr( prefix.size(), at - prefix.size() );
        std::string host   = enode.substr( at + 1, colon - at - 1 );
        uint16_t    port   = static_cast<uint16_t>( std::stoi( enode.substr( colon + 1 ) ) );
        return std::make_tuple( host, port, pubkey );
    };

    auto hex_to_nibble = []( char c ) -> std::optional<uint8_t>
    {
        if ( c >= '0' && c <= '9' ) { return static_cast<uint8_t>( c - '0' ); }
        if ( c >= 'a' && c <= 'f' ) { return static_cast<uint8_t>( 10 + c - 'a' ); }
        if ( c >= 'A' && c <= 'F' ) { return static_cast<uint8_t>( 10 + c - 'A' ); }
        return std::nullopt;
    };

    const auto start_result = dv4->start();
    if ( !start_result )
    {
        std::cout << "Failed to start discv4\n";
        return 1;
    }

    for ( const auto& enode : ETHEREUM_SEPOLIA_BOOTNODES )
    {
        auto parsed = parse_enode( enode );
        if ( !parsed ) { continue; }
        const auto& [host, port, pubkey_hex] = *parsed;
        if ( pubkey_hex.size() != 128 ) { continue; }
        discv4::NodeId bn_id{};
        bool ok = true;
        for ( size_t i = 0; i < 64 && ok; ++i )
        {
            auto hi = hex_to_nibble( pubkey_hex[i * 2] );
            auto lo = hex_to_nibble( pubkey_hex[i * 2 + 1] );
            if ( !hi || !lo ) { ok = false; break; }
            bn_id[i] = static_cast<uint8_t>( ( *hi << 4 ) | *lo );
        }
        if ( !ok ) { continue; }
        std::string host_copy = host;
        uint16_t    port_copy = port;
        boost::asio::spawn( io,
            [dv4, host_copy, port_copy, bn_id]( boost::asio::yield_context yc )
            {
                (void)dv4->find_node( host_copy, port_copy, bn_id, yc );
            } );
    }

    std::cout << "\n[  ENR SURVEY  ] Running for " << timeout_secs << "s ...\n\n";
    io.run();

    // ── Report ────────────────────────────────────────────────────────────────

    // Load expected Sepolia hash from chain_enodes.json(.gz); fall back to compiled-in value.
    static const std::array<uint8_t, 4U> kSepoliaHashFallback{ 0x26, 0x89, 0x56, 0xb6 };
    const std::array<uint8_t, 4U> kSepoliaHash =
        load_fork_hash( "ethereum-sepolia", argv[0] ).value_or( kSepoliaHashFallback );

    const int total   = peers_total.load();
    const int with_id = peers_with_fork_id.load();
    const int without = peers_without_fork_id.load();

    std::cout << "==========  ENR Survey Results  ==========\n\n";
    std::cout << "  Peers discovered (total):  " << total   << "\n";
    std::cout << "  Peers WITH  eth_fork_id:   " << with_id << "\n";
    std::cout << "  Peers WITHOUT eth_fork_id: " << without << "\n\n";

    if ( with_id == 0 )
    {
        std::cout << "  *** No eth_fork_id was populated for ANY peer. ***\n";
        std::cout << "  This means request_enr() is failing or returning no eth entry\n";
        std::cout << "  for all live Sepolia peers. Check --log-level debug output.\n\n";
    }
    else
    {
        std::cout << "  Fork hash breakdown (" << fork_hash_counts.size() << " distinct hash(es)):\n\n";
        for ( const auto& [hash, count] : fork_hash_counts )
        {
            const bool is_sepolia = ( hash == kSepoliaHash );
            std::cout << "    " << format_hash4( hash )
                      << "  :  " << std::setw( 6 ) << count << " peer(s)";
            if ( is_sepolia )
            {
                std::cout << "  <-- Sepolia expected hash MATCH";
            }
            std::cout << "\n";
        }
        std::cout << "\n  Expected Sepolia hash: " << format_hash4( kSepoliaHash ) << "\n";

        bool found_sepolia = ( fork_hash_counts.count( kSepoliaHash ) > 0 );
        if ( found_sepolia )
        {
            std::cout << "  Result: Sepolia hash IS present in live ENR data.\n";
            std::cout << "          The filter logic should work — investigate filter hookup.\n";
        }
        else
        {
            std::cout << "  Result: Sepolia hash NOT found in live ENR data.\n";
            std::cout << "          Either the expected hash is wrong, or these peers are\n";
            std::cout << "          not on the Prague fork. Check the hashes above.\n";
        }
    }

    std::cout << "\n==========================================\n\n";
    std::cout.flush();
    std::exit( 0 );
}
