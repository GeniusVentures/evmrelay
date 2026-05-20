// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_DISCV4_DISCV4_CONSTANTS_HPP
#define EVMRELAY_INCLUDE_DISCV4_DISCV4_CONSTANTS_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace discv4 {

/// discv4 wire-packet layout constants
/// Layout: hash(32) || signature(65) || packet-type(1) || RLP(payload)
static constexpr size_t  kWireHashSize           = 32;   ///< Outer Keccak-256 hash
static constexpr size_t  kWireSigSize            = 65;   ///< Recoverable ECDSA signature
static constexpr size_t  kWirePacketTypeSize     = 1;    ///< Single packet-type byte
static constexpr size_t  kWireRecoveryIdSize     = 1;    ///< Recoverable ECDSA recovery id
static constexpr size_t  kWireCompactSigSize     = kWireSigSize - kWireRecoveryIdSize;
static constexpr size_t  kWireHeaderSize         = kWireHashSize + kWireSigSize + kWirePacketTypeSize;
static constexpr size_t  kWirePacketTypeOffset   = kWireHashSize + kWireSigSize;

/// Node identity
static constexpr size_t  kNodeIdSize             = 64;   ///< Uncompressed secp256k1 pubkey without 0x04 prefix
static constexpr size_t  kNodeIdHexSize          = kNodeIdSize * 2;
static constexpr size_t  kUncompressedPubKeySize = kNodeIdSize + kWirePacketTypeSize; ///< 0x04 prefix + 64 bytes

/// Network
static constexpr size_t  kIPv4Size               = 4;
static constexpr size_t  kUdpBufferSize          = 2048;

/// Packet type identifiers
static constexpr uint8_t kPacketTypePing         = 0x01U;
static constexpr uint8_t kPacketTypePong         = 0x02U;
static constexpr uint8_t kPacketTypeFindNode     = 0x03U;
static constexpr uint8_t kPacketTypeNeighbours   = 0x04U;
static constexpr uint8_t kPacketTypeEnrRequest   = 0x05U;
static constexpr uint8_t kPacketTypeEnrResponse  = 0x06U;

/// Protocol version advertised in PING packets
static constexpr uint8_t kProtocolVersion        = 0x04U;

/// Default packet expiry window (seconds)
static constexpr uint32_t kPacketExpirySeconds   = 60U;

/// Default networking timers.
inline constexpr auto kDefaultPingTimeout        = std::chrono::milliseconds(5000);
inline constexpr auto kDefaultPeerExpiry         = std::chrono::seconds(300);
inline constexpr auto kDefaultDialHistoryExpiry  = std::chrono::seconds(35);

/// ENR sequence number field size in PONG (optional, 6-byte big-endian uint48)
static constexpr size_t kEnrSeqSize              = 6;

} // namespace discv4


#endif // EVMRELAY_INCLUDE_DISCV4_DISCV4_CONSTANTS_HPP
