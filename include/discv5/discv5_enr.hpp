// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_DISCV5_DISCV5_ENR_HPP
#define EVMRELAY_INCLUDE_DISCV5_DISCV5_ENR_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <discv5/discv5_error.hpp>
#include <discv5/discv5_types.hpp>

namespace discv5
{

// ---------------------------------------------------------------------------
// EnrParser
// ---------------------------------------------------------------------------

/// @brief Parses and validates Ethereum Node Records (EIP-778).
///
/// Responsibilities:
///  - Decode the base64url body of an "enr:…" URI.
///  - RLP-decode the record into signature + key–value content.
///  - Verify the secp256k1-v4 ECDSA signature.
///  - Extract all standard fields into an @p EnrRecord.
///  - Reject incomplete records that cannot yield a dialable @p ValidatedPeer.
///
/// The class is stateless and all public methods are const/noexcept-safe.
class EnrParser
{
public:
    // -----------------------------------------------------------------------
    // Primary entry point
    // -----------------------------------------------------------------------

    /// @brief Parse and validate an ENR URI of the form "enr:<base64url>".
    ///
    /// On success returns a fully populated EnrRecord whose @p node_id and at
    /// least one dialable endpoint (ip/udp_port or ip6/udp6_port) are set.
    ///
    /// @param enr_uri  Null-terminated ENR URI string.
    /// @return         Populated EnrRecord on success, discv5Error on failure.
    static Result<EnrRecord> parse(const std::string& enr_uri) noexcept;

    // -----------------------------------------------------------------------
    // Step-level helpers (also used by tests)
    // -----------------------------------------------------------------------

    /// @brief Decode the base64url body of an ENR URI into raw bytes.
    ///
    /// Strips the leading "enr:" prefix (case-sensitive) and decodes the
    /// remainder as unpadded RFC-4648 §5 base64url.
    ///
    /// @param enr_uri  Full URI string including the "enr:" prefix.
    /// @return         Raw RLP bytes on success, error on failure.
    static Result<std::vector<uint8_t>> decode_uri(const std::string& enr_uri) noexcept;

    /// @brief Base64url-decode a raw body string (without the "enr:" prefix).
    ///
    /// Accepts both padded and unpadded base64url input.
    ///
    /// @param body  Base64url-encoded string.
    /// @return      Decoded bytes on success, error on failure.
    static Result<std::vector<uint8_t>> base64url_decode(const std::string& body) noexcept;

    /// @brief RLP-decode raw bytes into an EnrRecord (no signature verification).
    ///
    /// Populates all key–value fields but leaves @p node_id and the signature
    /// validity flag untouched — the caller should call @p verify_signature
    /// separately.
    ///
    /// @param raw  Raw RLP bytes of the full ENR record.
    /// @return     Partially-populated EnrRecord, or error on decode failure.
    static Result<EnrRecord> decode_rlp(const std::vector<uint8_t>& raw) noexcept;

    /// @brief Verify the secp256k1-v4 signature embedded in @p record.
    ///
    /// Uses the compressed public key from the "secp256k1" field to verify the
    /// signature over keccak256(RLP([seq, k1, v1, …])).  On success sets
    /// @p record.node_id to the recovered 64-byte uncompressed key.
    ///
    /// @param record  EnrRecord whose @p raw_rlp and @p compressed_pubkey are
    ///                already populated (as returned by decode_rlp).
    /// @return        outcome::success() on valid signature, error otherwise.
    static VoidResult verify_signature(EnrRecord& record) noexcept;

    /// @brief Convert a fully-parsed EnrRecord into a ValidatedPeer.
    ///
    /// Fails if neither an IPv4 nor an IPv6 dialable endpoint is present.
    ///
    /// @param record  Populated EnrRecord (signature already verified).
    /// @return        ValidatedPeer on success, kEnrMissingAddress on failure.
    static Result<ValidatedPeer> to_validated_peer(const EnrRecord& record) noexcept;

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// @brief Decode a single 4-byte "ip" field into a dotted-decimal string.
    static Result<std::string> decode_ipv4(const std::vector<uint8_t>& bytes) noexcept;

    /// @brief Decode a 16-byte "ip6" field into a compressed IPv6 string.
    static Result<std::string> decode_ipv6(const std::vector<uint8_t>& bytes) noexcept;

    /// @brief Decode a big-endian uint16 from up to 2 bytes.
    static Result<uint16_t> decode_port(const std::vector<uint8_t>& bytes) noexcept;

    /// @brief Decode the "eth" key–value entry into a ForkId.
    static Result<ForkId> decode_eth_entry(const std::vector<uint8_t>& bytes) noexcept;

    /// @brief Derive the 64-byte uncompressed node_id from a 33-byte compressed key.
    static Result<NodeId> decompress_pubkey(
        const std::array<uint8_t, kCompressedKeyBytes>& compressed) noexcept;
};

} // namespace discv5

#endif // EVMRELAY_INCLUDE_DISCV5_DISCV5_ENR_HPP
