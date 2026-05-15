// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace discovery
{

/// @brief Node identifier — 64-byte uncompressed secp256k1 public key (without the 0x04 prefix).
using NodeId = std::array<uint8_t, 64>;

/// @brief Ethereum EIP-2124 fork identifier used for chain-correctness filtering.
///
/// Mirrors the go-ethereum `forkid.ID` type:
///   - @p hash   — CRC-32 of the canonical chain's genesis hash XOR'd with all
///                 activated fork block numbers up to the current block.
///   - @p next   — the next scheduled fork block (0 if none is known).
struct ForkId
{
    std::array<uint8_t, 4> hash{};  ///< 4-byte CRC-32 fork hash
    uint64_t               next{};  ///< next fork block number (0 = none)
};

/// @brief Minimal peer descriptor produced by both discv4 and discv5 crawlers.
///
/// Contains only the information that the downstream dial scheduler needs to
/// attempt an RLPx TCP connection and to apply an optional per-chain filter.
///
/// Intentionally kept narrow — it is the stable handoff contract between the
/// discovery layer and the connection layer.  Neither the scheduler nor the
/// caller should need to know which discovery protocol produced this record.
struct ValidatedPeer
{
    NodeId                  node_id{};           ///< 64-byte secp256k1 public key
    std::string             ip{};                ///< IPv4 dotted-decimal or IPv6 string
    uint16_t                udp_port{};          ///< UDP port used by the discovery protocol
    uint16_t                tcp_port{};          ///< TCP port for RLPx (devp2p)
    std::chrono::steady_clock::time_point
                            last_seen{};         ///< Wallclock of last discovery contact
    std::optional<ForkId>   eth_fork_id{};       ///< Present when parsed from ENR "eth" entry
};

} // namespace discovery
