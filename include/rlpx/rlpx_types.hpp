// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <vector>
#include <optional>
#include <gsl/span>
#include <rlp/common.hpp>  // For rlp::Bytes and rlp::ByteView types

namespace rlpx {

// Constant sizes — expressed as sizeof() on wire structs where possible
inline constexpr size_t kPublicKeySize    = 64;
inline constexpr size_t kPrivateKeySize   = 32;
inline constexpr size_t kNonceSize        = 32;
inline constexpr size_t kSharedSecretSize = 32;
inline constexpr size_t kAesKeySize       = 32;
inline constexpr size_t kMacKeySize       = 32;

/// Uncompressed secp256k1 public key: 0x04 prefix byte + 64 bytes = 65 bytes total.
inline constexpr size_t  kUncompressedPubKeyPrefixSize = 1;  ///< The single 0x04 prefix byte
inline constexpr size_t  kUncompressedPubKeySize       = kPublicKeySize + kUncompressedPubKeyPrefixSize;
inline constexpr uint8_t kUncompressedPubKeyPrefix     = 0x04U;

/// HMAC-SHA256 full digest output size.
inline constexpr size_t kHmacSha256Size = 32;

/// RLPx wire structs — sizes are derived via sizeof() rather than magic numbers.

/// 16-byte AES block (AES-128 / AES-256 share the same block size).
struct AesBlock    { uint8_t bytes[16]; };

/// 16-byte frame-level MAC digest (first 16 bytes of the running Keccak state).
struct MacDigestWire { uint8_t bytes[16]; };

/// 16-byte RLPx frame header: 3-byte length || 13-byte header-data (RLP list).
struct FrameHeaderWire { uint8_t bytes[16]; };

inline constexpr size_t kAesBlockSize    = sizeof(AesBlock);
inline constexpr size_t kMacSize         = sizeof(MacDigestWire);
inline constexpr size_t kFrameHeaderSize = sizeof(FrameHeaderWire);

/// ECIES / EIP-8 wire constants.
inline constexpr size_t kEciesMacSize          = kHmacSha256Size;
inline constexpr size_t kEciesOverheadSize     = kUncompressedPubKeySize + kAesBlockSize + kEciesMacSize;
inline constexpr size_t kEip8LengthPrefixSize  = sizeof(uint16_t);
inline constexpr size_t kEip8AuthPaddingSize   = 100;
inline constexpr size_t kMaxEip8HandshakePacketSize = 2048U;

/// Number of bytes used to encode the frame length inside the frame header.
inline constexpr size_t kFrameLengthSize = 3;
inline constexpr size_t kFrameHeaderDataOffset   = kFrameLengthSize;
inline constexpr size_t kFrameHeaderWithMacSize  = kFrameHeaderSize + kMacSize;
inline constexpr size_t kFramePaddingAlignment   = kAesBlockSize;
inline constexpr size_t kFrameLengthMsbOffset    = 0;
inline constexpr size_t kFrameLengthMiddleOffset = 1;
inline constexpr size_t kFrameLengthLsbOffset    = 2;
inline constexpr size_t kFrameLengthMsbShift     = 16U;
inline constexpr size_t kFrameLengthMiddleShift  = 8U;
inline constexpr size_t kFrameLengthLsbShift     = 0U;
inline constexpr std::array<uint8_t, kFrameLengthSize> kFrameHeaderStaticRlpBytes = { 0xC2U, 0x80U, 0x80U };

/// RLPx auth message wire constants
/// Recoverable ECDSA compact signature: 64 bytes data + 1 byte recovery id.
inline constexpr size_t kEcdsaCompactSigSize = 64;
inline constexpr size_t kEcdsaRecoveryIdSize = 1;
inline constexpr size_t kEcdsaSigSize        = kEcdsaCompactSigSize + kEcdsaRecoveryIdSize;

/// Version byte appended to auth/ack messages (RLPx v4 EIP-8).
inline constexpr size_t  kAuthVersionSize    = 1;
inline constexpr uint8_t kAuthVersion        = 4U;

/// Maximum RLPx frame payload: 16 MiB
inline constexpr size_t kMaxFrameSizeMiB = 16;
inline constexpr size_t kMaxFrameSize    = kMaxFrameSizeMiB * 1024 * 1024;

// Type aliases for better semantics
using PublicKey    = std::array<uint8_t, kPublicKeySize>;
using PrivateKey   = std::array<uint8_t, kPrivateKeySize>;
using Nonce        = std::array<uint8_t, kNonceSize>;
using SharedSecret = std::array<uint8_t, kSharedSecretSize>;
using AesKey       = std::array<uint8_t, kAesKeySize>;
using MacKey       = std::array<uint8_t, kMacKeySize>;
using MacDigest    = std::array<uint8_t, kMacSize>;
using FrameHeader  = std::array<uint8_t, kFrameHeaderSize>;

// Hide implementation details behind type aliases (Law of Demeter)
using ByteBuffer = std::vector<uint8_t>;
using ByteView = gsl::span<const uint8_t>;
using MutableByteView = gsl::span<uint8_t>;

// Session state enum
enum class SessionState : uint8_t {
    kUninitialized = 0,
    kConnecting,
    kAuthenticating,
    kHandshaking,
    kActive,
    kDisconnecting,
    kClosed,
    kError
};

// Disconnect reasons (from RLPx spec)
enum class DisconnectReason : uint8_t {
    kRequested = 0x00,
    kTcpError = 0x01,
    kProtocolError = 0x02,
    kUselessPeer = 0x03,
    kTooManyPeers = 0x04,
    kAlreadyConnected = 0x05,
    kIncompatibleVersion = 0x06,
    kInvalidIdentity = 0x07,
    kClientQuitting = 0x08,
    kUnexpectedIdentity = 0x09,
    kSelfConnection = 0x0A,
    kTimeout = 0x0B,
    kSubprotocolError = 0x10
};

// Protocol message IDs
inline constexpr uint8_t kHelloMessageId = 0x00;
inline constexpr uint8_t kDisconnectMessageId = 0x01;
inline constexpr uint8_t kPingMessageId = 0x02;
inline constexpr uint8_t kPongMessageId = 0x03;

// Protocol version
inline constexpr uint8_t kProtocolVersion = 5;

// Timing constants
inline constexpr auto kTcpConnectionTimeout  = std::chrono::seconds(10);
inline constexpr auto kProtocolHandshakeTimeout = std::chrono::seconds(5);
inline constexpr auto kSendLoopPollInterval  = std::chrono::milliseconds(10);

// Conversion functions for interop with rlp library
namespace detail {    
    // Convert rlpx::ByteView (gsl::span) to rlp::ByteView (std::basic_string_view)
    inline rlp::ByteView to_rlp_view(rlpx::ByteView view) noexcept {
        return rlp::ByteView(view.data(), view.size());
    }
    
    // Convert rlp::ByteView to rlpx::ByteView
    inline rlpx::ByteView from_rlp_view(rlp::ByteView view) noexcept {
        return rlpx::ByteView(view.data(), view.size());
    }
    
    // Convert rlpx::ByteBuffer to rlp::Bytes
    inline rlp::Bytes to_rlp_bytes(const rlpx::ByteBuffer& buffer) {
        return rlp::Bytes(buffer.begin(), buffer.end());
    }
    
    // Convert rlp::Bytes to rlpx::ByteBuffer
    inline rlpx::ByteBuffer from_rlp_bytes(const rlp::Bytes& bytes) {
        return rlpx::ByteBuffer(bytes.begin(), bytes.end());
    }
}

} // namespace rlpx
