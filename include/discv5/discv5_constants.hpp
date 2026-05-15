// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace discv5
{

// ===========================================================================
// Part 1 — Fundamental domain sizes (every value has a name; no bare literals
//          anywhere in the module that refers to these quantities).
// ===========================================================================

// ---------------------------------------------------------------------------
// Networking defaults
// ---------------------------------------------------------------------------

/// @brief Default UDP port for discv5 (IANA-assigned).
static constexpr uint16_t kDefaultUdpPort        = 9000U;

/// @brief Default TCP/RLPx port advertised to discovered peers.
static constexpr uint16_t kDefaultTcpPort        = 30303U;

// ---------------------------------------------------------------------------
// secp256k1 key sizes (domain constants — all other sizes are derived below)
// ---------------------------------------------------------------------------

/// @brief Bytes in an uncompressed secp256k1 public key WITHOUT the 0x04 prefix.
///        This is the 64-byte "node id" used by discv4 and passed to DialHistory.
static constexpr size_t   kNodeIdBytes           = 64U;

/// @brief Bytes in a compressed secp256k1 public key (03/02 prefix + 32 bytes).
static constexpr size_t   kCompressedKeyBytes    = 33U;

/// @brief Bytes in a secp256k1 private key.
static constexpr size_t   kPrivateKeyBytes       = 32U;

/// @brief Bytes in the 0x04 uncompressed-point prefix.
static constexpr size_t   kUncompressedPrefixLen = 1U;
static constexpr uint8_t  kUncompressedPubKeyPrefix = 0x04U;
static constexpr size_t   kUncompressedPubKeyDataOffset = kUncompressedPrefixLen;

// ---------------------------------------------------------------------------
// Cryptographic hash sizes
// ---------------------------------------------------------------------------

/// @brief Keccak-256 (legacy) digest size in bytes.
static constexpr size_t   kKeccak256Bytes        = 32U;

// ---------------------------------------------------------------------------
// IPv4 / IPv6 wire sizes
// ---------------------------------------------------------------------------

/// @brief IPv4 address wire size in bytes.
static constexpr size_t   kIPv4Bytes             = 4U;

/// @brief IPv6 address wire size in bytes.
static constexpr size_t   kIPv6Bytes             = 16U;

// ---------------------------------------------------------------------------
// IPv4 octet offsets and big-endian shift amounts
// ---------------------------------------------------------------------------

/// @brief Byte offset of the most-significant octet in a 4-byte IPv4 field.
static constexpr size_t   kIPv4OctetMsb         = 0U;
/// @brief Byte offset of the second octet.
static constexpr size_t   kIPv4Octet1           = 1U;
/// @brief Byte offset of the third octet.
static constexpr size_t   kIPv4Octet2           = 2U;
/// @brief Byte offset of the least-significant octet.
static constexpr size_t   kIPv4OctetLsb         = 3U;

/// @brief Left-shift amount to place the MSB octet in a uint32 (big-endian).
static constexpr uint32_t kIPv4MsbShift         = 24U;
/// @brief Left-shift amount for the second octet.
static constexpr uint32_t kIPv4Octet1Shift      = 16U;
/// @brief Left-shift amount for the third octet.
static constexpr uint32_t kIPv4Octet2Shift      =  8U;
/// @brief Left-shift amount for the least-significant octet (no shift).
static constexpr uint32_t kIPv4LsbShift         =  0U;

// ---------------------------------------------------------------------------
// Port field sizes
// ---------------------------------------------------------------------------

/// @brief Maximum bytes an RLP-encoded UDP/TCP port occupies (big-endian uint16).
static constexpr size_t   kMaxPortBytes          = 2U;

// ---------------------------------------------------------------------------
// ENR "eth" fork-id field sizes
// ---------------------------------------------------------------------------

/// @brief Byte count of the fork-hash field inside an ENR "eth" entry.
static constexpr size_t   kForkHashBytes         = 4U;

// ---------------------------------------------------------------------------
// discv5 wire-packet field sizes (from EIP-8020 / go-ethereum v5wire)
// ---------------------------------------------------------------------------

/// @brief Byte length of the "discv5" protocol-ID magic string.
static constexpr size_t   kProtocolIdBytes       = 6U;

/// @brief The discv5 packet protocol-ID string.
static constexpr char     kProtocolId[]          = "discv5";

/// @brief Byte length of the AES-GCM nonce used for message encryption.
static constexpr size_t   kGcmNonceBytes         = 12U;

/// @brief Byte length of the masking IV that precedes the static header.
static constexpr size_t   kMaskingIvBytes        = 16U;

/// @brief Byte length of the WHOAREYOU id-nonce field.
static constexpr size_t   kWhoareyouIdNonceBytes = 16U;

/// @brief Minimum valid discv5 packet size (go-ethereum minPacketSize).
static constexpr size_t   kMinPacketBytes        = 63U;

/// @brief Maximum valid discv5 packet size in bytes.
static constexpr size_t   kMaxPacketBytes        = 1280U;

/// @brief AES-128 session key size in bytes.
static constexpr size_t   kAes128KeyBytes        = 16U;

/// @brief AES-GCM authentication tag size in bytes.
static constexpr size_t   kGcmTagBytes           = 16U;

/// @brief Random encrypted payload size used for pre-handshake message placeholders.
static constexpr size_t   kRandomMessageCiphertextBytes = 20U;

/// @brief FINDNODE distance that requests the full routing table (log2 space upper bound + 1).
static constexpr uint32_t kFindNodeDistanceAll   = 256U;

/// @brief Single-byte HKDF expansion block counter used for the first output block.
static constexpr uint8_t  kHkdfFirstBlockCounter = 0x01U;

/// @brief Byte size of the leading message-type prefix in decrypted discv5 payloads.
static constexpr size_t   kMessageTypePrefixBytes = 1U;

/// @brief Number of ENR records returned by the local single-record NODES reply helper.
static constexpr uint8_t  kNodesResponseCountSingle = 1U;

// ---------------------------------------------------------------------------
// ENR record sizes
// ---------------------------------------------------------------------------

/// @brief Maximum total size of a serialised ENR record (EIP-778 SizeLimit).
static constexpr size_t   kEnrMaxBytes           = 300U;

/// @brief Compact secp256k1 ECDSA signature size stored in an ENR (no recid).
static constexpr size_t   kEnrSigBytes           = 64U;

/// @brief Byte length of the "enr:" URI prefix.
static constexpr size_t   kEnrPrefixLen          = 4U;

/// @brief The "enr:" URI prefix string.
static constexpr char     kEnrPrefix[]           = "enr:";

/// @brief Initial ENR sequence number for locally generated records.
static constexpr uint8_t  kInitialEnrSeq         = 1U;

/// @brief ENR identity scheme string for secp256k1-v4.
static constexpr char     kIdentitySchemeV4[]    = "v4";
static constexpr size_t   kIdentitySchemeV4Bytes = 2U;

/// @brief Common ENR field key strings and lengths.
static constexpr char     kEnrKeyId[]            = "id";
static constexpr size_t   kEnrKeyIdBytes         = 2U;
static constexpr char     kEnrKeyIp[]            = "ip";
static constexpr size_t   kEnrKeyIpBytes         = 2U;
static constexpr char     kEnrKeySecp256k1[]     = "secp256k1";
static constexpr size_t   kEnrKeySecp256k1Bytes  = 9U;
static constexpr char     kEnrKeyTcp[]           = "tcp";
static constexpr size_t   kEnrKeyTcpBytes        = 3U;
static constexpr char     kEnrKeyUdp[]           = "udp";
static constexpr size_t   kEnrKeyUdpBytes        = 3U;

// ---------------------------------------------------------------------------
// Base64url alphabet sizes (RFC-4648 §5) — used by the ENR URI decoder
// ---------------------------------------------------------------------------

/// @brief Number of upper-case letters (A–Z) in the base64url alphabet.
static constexpr uint8_t  kBase64UpperCount      = 26U;

/// @brief Number of lower-case letters (a–z) in the base64url alphabet.
static constexpr uint8_t  kBase64LowerCount      = 26U;

/// @brief Number of decimal digits (0–9) in the base64url alphabet.
static constexpr uint8_t  kBase64DigitCount      = 10U;

/// @brief Start index of the lower-case letter block (after A–Z).
static constexpr uint8_t  kBase64LowerStart      = kBase64UpperCount;

/// @brief Start index of the digit block (after A–Z and a–z).
static constexpr uint8_t  kBase64DigitStart      =
    kBase64UpperCount + kBase64LowerCount;

/// @brief Table index for the '-' character in base64url.
static constexpr uint8_t  kBase64DashIndex       = 62U;

/// @brief Table index for the '_' character in base64url.
static constexpr uint8_t  kBase64UnderIndex      = 63U;

/// @brief Sentinel value meaning "not a valid base64url character".
static constexpr uint8_t  kBase64Invalid         = 0xFFU;

/// @brief Number of bits encoded by one base64 character.
static constexpr size_t   kBase64BitsPerChar     = 6U;

/// @brief Number of bits in one output byte.
static constexpr size_t   kBase64BitsPerByte     = 8U;

// ---------------------------------------------------------------------------
// Crawler tuning defaults
// ---------------------------------------------------------------------------

/// @brief Default maximum concurrent outbound FINDNODE queries.
static constexpr size_t   kDefaultMaxConcurrent  = 8U;

/// @brief Default interval between crawler sweep rounds (seconds).
static constexpr uint32_t kDefaultQueryIntervalSec  = 30U;

/// @brief Seconds before an unseen peer is evicted from the peer table.
static constexpr uint32_t kDefaultPeerExpirySec     = 600U;

/// @brief Maximum bootstrap ENR entries accepted per chain source.
static constexpr size_t   kMaxBootnodeEnrs           = 64U;

// ===========================================================================
// Part 2 — Wire-layout POD structs.
//
// Every on-wire field size must be derived from sizeof(SomeWireStruct) so
// that no magic number appears in protocol code.  The static_asserts below
// verify that the compiler's layout matches the domain constants.
// ===========================================================================

// ---------------------------------------------------------------------------
// Cryptographic wire types
// ---------------------------------------------------------------------------

/// @brief Wire layout of a 4-byte IPv4 address as stored in an ENR "ip" field.
struct IPv4Wire
{
    uint8_t msb;    ///< Most-significant octet
    uint8_t b1;     ///< Second octet
    uint8_t b2;     ///< Third octet
    uint8_t lsb;    ///< Least-significant octet
};
static_assert(sizeof(IPv4Wire) == kIPv4Bytes, "IPv4Wire layout must be 4 bytes");

/// @brief Wire layout of a 16-byte IPv6 address as stored in an ENR "ip6" field.
struct IPv6Wire
{
    uint8_t bytes[kIPv6Bytes];  ///< 16 raw octets (network byte order)
};
static_assert(sizeof(IPv6Wire) == kIPv6Bytes, "IPv6Wire layout must be 16 bytes");

/// @brief Wire layout of a Keccak-256 hash.
struct Keccak256Wire
{
    uint8_t bytes[kKeccak256Bytes];  ///< 32-byte hash digest
};
static_assert(sizeof(Keccak256Wire) == kKeccak256Bytes,
              "Keccak256Wire layout must be 32 bytes");

/// @brief Wire layout of a compressed secp256k1 public key.
struct CompressedPubKeyWire
{
    uint8_t bytes[kCompressedKeyBytes];  ///< 33-byte compressed point (02/03 prefix + X)
};
static_assert(sizeof(CompressedPubKeyWire) == kCompressedKeyBytes,
              "CompressedPubKeyWire layout must be 33 bytes");

/// @brief Wire layout of an ENR compact secp256k1 signature (no recovery id).
struct EnrSigWire
{
    uint8_t bytes[kEnrSigBytes];  ///< 64-byte compact signature (R || S)
};
static_assert(sizeof(EnrSigWire) == kEnrSigBytes,
              "EnrSigWire layout must be 64 bytes");

/// @brief Wire layout of an uncompressed secp256k1 public key (with 0x04 prefix).
struct UncompressedPubKeyWire
{
    uint8_t prefix;                       ///< Always 0x04
    uint8_t xy[kNodeIdBytes];             ///< 64-byte X || Y coordinate pair
};
static_assert(sizeof(UncompressedPubKeyWire) == kUncompressedPrefixLen + kNodeIdBytes,
              "UncompressedPubKeyWire size mismatch");

/// @brief Total byte count of an uncompressed public key including the 0x04 prefix.
static constexpr size_t kUncompressedKeyBytes =
    sizeof(UncompressedPubKeyWire);  // = kUncompressedPrefixLen + kNodeIdBytes = 65

// ---------------------------------------------------------------------------
// discv5 packet wire types (EIP-8020 / go-ethereum v5wire)
// ---------------------------------------------------------------------------

/// @brief Masking IV that precedes the static packet header.
struct MaskingIvWire
{
    uint8_t bytes[kMaskingIvBytes];  ///< 16-byte AES-128 IV
};
static_assert(sizeof(MaskingIvWire) == kMaskingIvBytes,
              "MaskingIvWire layout must be 16 bytes");

/// @brief AES-GCM nonce embedded in the discv5 static header.
struct GcmNonceWire
{
    uint8_t bytes[kGcmNonceBytes];  ///< 12-byte GCM nonce
};
static_assert(sizeof(GcmNonceWire) == kGcmNonceBytes,
              "GcmNonceWire layout must be 12 bytes");

/// @brief discv5 static packet header, as defined by the EIP-8020 spec.
///
/// Mirrors go-ethereum's @c v5wire.StaticHeader struct.
/// Sits immediately after the @p MaskingIvWire in every packet.
///
/// All multi-byte integers are big-endian on the wire.
#pragma pack(push, 1)
struct StaticHeaderWire
{
    uint8_t       protocol_id[kProtocolIdBytes];  ///< "discv5" magic (0x64 69 73 63 76 35)
    uint16_t      version;                         ///< Protocol version (current = 1)
    uint8_t       flag;                            ///< Packet type flag (0=msg, 1=WHOAREYOU, 2=handshake)
    GcmNonceWire  nonce;                           ///< AES-GCM nonce
    uint16_t      auth_size;                       ///< Byte count of the auth-data section
};
#pragma pack(pop)

/// @brief Byte count of the static header (derived from wire struct, NOT a magic literal).
static constexpr size_t kStaticHeaderBytes = sizeof(StaticHeaderWire);
static constexpr size_t kStaticHeaderVersionOffset = offsetof(StaticHeaderWire, version);
static constexpr size_t kStaticHeaderFlagOffset = offsetof(StaticHeaderWire, flag);
static constexpr size_t kStaticHeaderNonceOffset = offsetof(StaticHeaderWire, nonce);
static constexpr size_t kStaticHeaderAuthSizeOffset = offsetof(StaticHeaderWire, auth_size);

/// @brief WHOAREYOU auth-data layout: id-nonce + highest ENR sequence.
#pragma pack(push, 1)
struct WhoareyouAuthDataWire
{
    uint8_t  id_nonce[kWhoareyouIdNonceBytes];  ///< 16-byte identity challenge nonce
    uint64_t record_seq;                        ///< Highest known ENR sequence (big-endian)
};
#pragma pack(pop)

/// @brief Byte count of WHOAREYOU auth data.
static constexpr size_t kWhoareyouAuthDataBytes = sizeof(WhoareyouAuthDataWire);

/// @brief HANDSHAKE auth-data layout prefixes.
static constexpr size_t kHandshakeAuthSizeFieldBytes = sizeof(uint8_t);
static constexpr size_t kHandshakeAuthSizeFieldCount = 2U;
static constexpr size_t kHandshakeAuthSigSizeOffset = kKeccak256Bytes;
static constexpr size_t kHandshakeAuthPubkeySizeOffset = kHandshakeAuthSigSizeOffset + kHandshakeAuthSizeFieldBytes;
static constexpr size_t kHandshakeAuthFixedBytes =
    kKeccak256Bytes + (kHandshakeAuthSizeFieldCount * kHandshakeAuthSizeFieldBytes);

/// @brief Total fixed bytes at the front of every discv5 packet:
///        masking IV + static header.
static constexpr size_t kStaticPacketBytes =
    sizeof(MaskingIvWire) + sizeof(StaticHeaderWire);

// ---------------------------------------------------------------------------
// discv5 packet-type flag values (stored in StaticHeaderWire::flag)
// ---------------------------------------------------------------------------

/// @brief Flag value for an ordinary encrypted message packet.
static constexpr uint8_t  kFlagMessage     = 0U;

/// @brief Flag value for a WHOAREYOU session-challenge packet.
static constexpr uint8_t  kFlagWhoareyou   = 1U;

/// @brief Flag value for a HANDSHAKE message.
static constexpr uint8_t  kFlagHandshake   = 2U;

// ---------------------------------------------------------------------------
// discv5 application message type bytes (inside decrypted message payload)
// ---------------------------------------------------------------------------

/// @brief PING message type byte.
static constexpr uint8_t  kMsgPing        = 0x01U;

/// @brief PONG message type byte.
static constexpr uint8_t  kMsgPong        = 0x02U;

/// @brief FINDNODE message type byte.
static constexpr uint8_t  kMsgFindNode    = 0x03U;

/// @brief NODES message type byte.
static constexpr uint8_t  kMsgNodes       = 0x04U;

/// @brief TALKREQ message type byte.
static constexpr uint8_t  kMsgTalkReq     = 0x05U;

/// @brief TALKRESP message type byte.
static constexpr uint8_t  kMsgTalkResp    = 0x06U;

/// @brief discv5 protocol version (current).
static constexpr uint16_t kProtocolVersion = 1U;

} // namespace discv5
