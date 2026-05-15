// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include "../rlpx_types.hpp"
#include "../rlpx_error.hpp"

namespace rlpx::crypto {

// ECDH (Elliptic Curve Diffie-Hellman) operations using secp256k1
class Ecdh {
public:
    Ecdh() = delete;

    // Compute shared secret from peer's public key and our private key
    // Uses secp256k1 curve multiplication
    [[nodiscard]] static CryptoResult<SharedSecret>
    compute_shared_secret(
        gsl::span<const uint8_t, kPublicKeySize> public_key,
        gsl::span<const uint8_t, kPrivateKeySize> private_key
    ) noexcept;

    // Generate ephemeral key pair for ECDH
    struct KeyPair {
        PublicKey public_key;
        PrivateKey private_key;
    };

    [[nodiscard]] static CryptoResult<KeyPair>
    generate_ephemeral_keypair() noexcept;

    // Verify public key is valid point on curve
    [[nodiscard]] static bool
    verify_public_key(gsl::span<const uint8_t, kPublicKeySize> public_key) noexcept;
};

} // namespace rlpx::crypto
