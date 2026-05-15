// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef discv4_ERROR_HPP
#define discv4_ERROR_HPP

#include <boost/outcome/result.hpp>
#include <boost/outcome/try.hpp>

namespace discv4 {

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

/**
 * @brief Discovery v4 protocol error codes
 */
enum class discv4Error {
    kNetworkSendFailed,           // Failed to send UDP packet
    kNetworkReceiveFailed,        // Failed to receive UDP packet
    kPacketTooSmall,              // Packet size below minimum (98 bytes)
    kInvalidPacketType,           // Unknown or invalid packet type
    kHashMismatch,                // Packet hash verification failed
    kSignatureParseFailed,        // Failed to parse ECDSA signature
    kSignatureRecoveryFailed,     // Failed to recover public key from signature
    kSigningFailed,               // Failed to sign packet with private key
    kContextCreationFailed,       // Failed to create secp256k1 context
    kInvalidPublicKey,            // Invalid or malformed public key
    kRlpPayloadEmpty,             // RLP payload generation returned empty
    kPongTimeout,                 // PONG response timeout
    kPongParseFailed,             // Failed to parse PONG packet
};

/**
 * @brief Result type for Discovery v4 operations
 *
 * @tparam T The success value type
 */
template<typename T>
using Result = outcome::result<T, discv4Error, outcome::policy::all_narrow>;

/**
 * @brief Result type for Discovery v4 operations without return value
 */
using VoidResult = outcome::result<void, discv4Error, outcome::policy::all_narrow>;

/**
 * @brief Convert error code to human-readable string
 *
 * @param error The error code
 * @return Error description string
 */
const char* to_string(discv4Error error) noexcept;

} // namespace discv4

#endif // discv4_ERROR_HPP

