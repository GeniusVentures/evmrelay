// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_DISCV5_DISCV5_TYPES_HPP
#define EVMRELAY_INCLUDE_DISCV5_DISCV5_TYPES_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <discv5/discv5_constants.hpp>
#include <discovery/discovered_peer.hpp>

namespace discv5
{

// Import shared aliases so callers don't have to qualify separately.
using discovery::NodeId;
using discovery::ForkId;
using discovery::ValidatedPeer;

// ---------------------------------------------------------------------------
// EnrRecord
// ---------------------------------------------------------------------------

/// @brief Parsed Ethereum Node Record as defined by EIP-778.
///
/// All fields are optional except @p seq, @p raw_rlp, and @p node_id (which
/// are populated whenever a record is successfully parsed by EnrParser).
///
/// Key–value pairs that are not natively understood are stored verbatim in
/// @p extra_fields so that the record can be re-serialised without loss.
struct EnrRecord
{
    /// @brief Sequence number.  Higher value → more recent record.
    uint64_t seq{};

    /// @brief Raw RLP bytes of the complete record (signature + content).
    ///        Stored so that the signature can be re-verified later.
    std::vector<uint8_t> raw_rlp{};

    /// @brief 64-byte uncompressed node public key (sans 0x04 prefix).
    ///        Derived from the "secp256k1" (compressed) field.
    NodeId node_id{};

    /// @brief 33-byte compressed secp256k1 public key as stored in the record.
    std::array<uint8_t, kCompressedKeyBytes> compressed_pubkey{};

    /// @brief IPv4 address string (empty if absent).
    std::string ip{};

    /// @brief IPv6 address string (empty if absent).
    std::string ip6{};

    /// @brief UDP discovery port (0 if absent).
    uint16_t udp_port{};

    /// @brief TCP RLPx port (0 if absent).
    uint16_t tcp_port{};

    /// @brief UDP discovery port for IPv6 endpoint (0 if absent).
    uint16_t udp6_port{};

    /// @brief TCP port for IPv6 endpoint (0 if absent).
    uint16_t tcp6_port{};

    /// @brief ENR identity scheme name, e.g. "v4" for secp256k1-v4.
    std::string identity_scheme{};

    /// @brief Optional Ethereum fork identifier parsed from the "eth" entry.
    std::optional<ForkId> eth_fork_id{};

    /// @brief Unknown key–value pairs preserved verbatim (key → raw bytes).
    std::unordered_map<std::string, std::vector<uint8_t>> extra_fields{};
};

// ---------------------------------------------------------------------------
// Discv5Peer
// ---------------------------------------------------------------------------

/// @brief A peer discovered by the discv5 crawler.
///
/// Owns the source @p enr, derives the @p peer handoff record from it, and
/// tracks the last-contact timestamp for eviction bookkeeping.
struct Discv5Peer
{
    EnrRecord                             enr{};          ///< Full parsed ENR
    ValidatedPeer                         peer{};         ///< Handoff record for DialScheduler
    std::chrono::steady_clock::time_point last_seen{};    ///< Time of most recent contact
};

// ---------------------------------------------------------------------------
// discv5Config
// ---------------------------------------------------------------------------

/// @brief Configuration for the discv5 client and crawler.
///
/// All numeric fields default to the values defined in discv5_constants.hpp.
struct discv5Config
{
    /// Bind address for the local UDP socket.
    std::string  bind_ip             = "0.0.0.0";

    /// UDP port to bind.  0 → OS-assigned ephemeral port.
    uint16_t     bind_port           = kDefaultUdpPort;

    /// TCP port advertised to peers (for RLPx dial-back).
    uint16_t     tcp_port            = kDefaultTcpPort;

    /// secp256k1 private key (32 bytes).  Must be set before start().
    std::array<uint8_t, kPrivateKeyBytes> private_key{};

    /// secp256k1 public key (64 bytes, uncompressed, no 0x04 prefix).
    NodeId public_key{};

    /// Bootstrap ENR URI strings ("enr:…").  At least one is required.
    std::vector<std::string> bootstrap_enrs{};

    /// Maximum number of concurrent FINDNODE queries.
    size_t   max_concurrent_queries  = kDefaultMaxConcurrent;

    /// Seconds between full crawler sweeps.
    uint32_t query_interval_sec      = kDefaultQueryIntervalSec;

    /// Seconds before a discovered peer is considered stale and evicted.
    uint32_t peer_expiry_sec         = kDefaultPeerExpirySec;

    /// When set, only peers whose ENR "eth" entry matches this fork are
    /// forwarded to the peer-discovered callback.
    std::optional<ForkId> required_fork_id{};
};

// ---------------------------------------------------------------------------
// Callback types
// ---------------------------------------------------------------------------

/// @brief Invoked when a new valid peer has been discovered and passed all
///        configured filters (chain filter, address validation, dedup).
using PeerDiscoveredCallback = std::function<void(const ValidatedPeer&)>;

/// @brief Invoked when a non-fatal error occurs inside the crawler (e.g. a
///        malformed ENR from a remote node).
using ErrorCallback = std::function<void(const std::string&)>;

} // namespace discv5

#endif // EVMRELAY_INCLUDE_DISCV5_DISCV5_TYPES_HPP
