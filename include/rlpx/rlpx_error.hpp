// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <boost/outcome/result.hpp>
#include <boost/outcome/try.hpp>
#include "rlpx_types.hpp"

namespace rlpx {

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

enum class SessionError {
    kNetworkFailure,
    kAuthenticationFailed,
    kHandshakeFailed,
    kInvalidState,
    kPeerDisconnected,
    kTimeout,
    kCompressionError,
    kEncryptionError,
    kDecryptionError,
    kInvalidMessage,
    kInvalidFrameSize,
    kBufferOverflow,
    kConnectionFailed,
    kNotConnected,
    kDisconnected
};

enum class AuthError {
    kEciesEncryptFailed,
    kEciesDecryptFailed,
    kInvalidAuthMessage,
    kInvalidAckMessage,
    kSharedSecretFailed,
    kSignatureInvalid,
    kKeyDerivationFailed,
    kInvalidPublicKey,
    kInvalidNonce
};

enum class FramingError {
    kEncryptionFailed,
    kDecryptionFailed,
    kMacMismatch,
    kInvalidFrameSize,
    kBufferTooSmall,
    kInvalidHeader,
    kCompressionFailed,
    kDecompressionFailed
};

enum class CryptoError {
    kAesEncryptFailed,
    kAesDecryptFailed,
    kHmacFailed,
    kKdfFailed,
    kEcdhFailed,
    kInvalidKeySize,
    kInvalidIvSize,
    kInvalidPublicKey,
    kInvalidPrivateKey,
    kOpenSslError,
    kSecp256k1Error
};

template<typename T>
using Result = outcome::result<T, SessionError, outcome::policy::all_narrow>;

template<typename T>
using AuthResult = outcome::result<T, AuthError, outcome::policy::all_narrow>;

template<typename T>
using FramingResult = outcome::result<T, FramingError, outcome::policy::all_narrow>;

template<typename T>
using CryptoResult = outcome::result<T, CryptoError, outcome::policy::all_narrow>;

using VoidResult = outcome::result<void, SessionError, outcome::policy::all_narrow>;
using AuthVoidResult = outcome::result<void, AuthError, outcome::policy::all_narrow>;
using FramingVoidResult = outcome::result<void, FramingError, outcome::policy::all_narrow>;
using CryptoVoidResult = outcome::result<void, CryptoError, outcome::policy::all_narrow>;

// Error message conversion
const char* to_string(SessionError error) noexcept;
const char* to_string(AuthError error) noexcept;
const char* to_string(FramingError error) noexcept;
const char* to_string(CryptoError error) noexcept;

} // namespace rlpx
