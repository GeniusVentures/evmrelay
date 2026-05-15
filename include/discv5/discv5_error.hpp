// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/outcome/result.hpp>
#include <boost/outcome/try.hpp>

namespace discv5
{

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

// ---------------------------------------------------------------------------
// Error enumeration
// ---------------------------------------------------------------------------

/// @brief Enumeration of all error conditions that can be returned by the
///        discv5 module.
///
/// Follows the same idiom as discv4::discv4Error: a plain enum class used as
/// the error type in outcome::result<T, discv5Error>.
enum class discv5Error
{
    kEnrMissingPrefix,            ///< Input string does not start with "enr:"
    kEnrBase64DecodeFailed,       ///< Base64url decode of the ENR body failed
    kEnrRlpDecodeFailed,          ///< RLP decode of the ENR record failed
    kEnrTooShort,                 ///< RLP list has too few items (need ≥ 2)
    kEnrTooLarge,                 ///< Serialised ENR exceeds kEnrMaxBytes (300) bytes
    kEnrSignatureInvalid,         ///< Signature verification failed
    kEnrSignatureWrongSize,       ///< Signature field is not 64 bytes
    kEnrMissingSecp256k1Key,      ///< Required "secp256k1" field is absent
    kEnrInvalidSecp256k1Key,      ///< secp256k1 field cannot be parsed as pubkey
    kEnrMissingAddress,           ///< Neither "ip" nor "ip6" field is present
    kEnrInvalidIp,                ///< "ip" field is not exactly 4 bytes
    kEnrInvalidIp6,               ///< "ip6" field is not exactly 16 bytes
    kEnrInvalidUdpPort,           ///< "udp" field value is zero or out of range
    kEnrInvalidEthEntry,          ///< "eth" entry could not be decoded as [hash, next]
    kEnrIdentityUnknown,          ///< "id" field does not name a supported scheme
    kEnodeUriMalformed,           ///< enode:// URI could not be parsed
    kEnodeHexPubkeyInvalid,       ///< Hex-encoded pubkey in enode URI has wrong length/chars
    kContextCreationFailed,       ///< Failed to create secp256k1 context
    kCrawlerAlreadyRunning,       ///< start() called while crawler is active
    kCrawlerNotRunning,           ///< stop() called on an idle crawler
    kNetworkSendFailed,           ///< UDP send operation failed
    kNetworkReceiveFailed,        ///< UDP receive operation failed
};

// ---------------------------------------------------------------------------
// Result alias templates
// ---------------------------------------------------------------------------

/// @brief Result type for discv5 operations that return a value.
///
/// @tparam T  Success value type.
template <typename T>
using Result = outcome::result<T, discv5Error, outcome::policy::all_narrow>;

/// @brief Result type for discv5 operations that return nothing on success.
using VoidResult = outcome::result<void, discv5Error, outcome::policy::all_narrow>;

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

/// @brief Convert a @p discv5Error code to a human-readable C string.
///
/// @param error  The error code to describe.
/// @return       A static null-terminated string.  Never returns nullptr.
const char* to_string(discv5Error error) noexcept;

} // namespace discv5
