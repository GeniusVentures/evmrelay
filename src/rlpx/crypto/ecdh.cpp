// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <rlpx/crypto/ecdh.hpp>
#include <base/rlp-logger.hpp>
#include <secp256k1.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <cstring>

namespace rlpx::crypto {

namespace {
    // Global secp256k1 context (thread-safe, read-only after creation)
    secp256k1_context* get_context() {
        static secp256k1_context* ctx = secp256k1_context_create(
            SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY
        );
        return ctx;
    }

    rlp::base::Logger& ecdh_log() {
        static auto log = rlp::base::createLogger("rlpx.ecdh");
        return log;
    }
}

CryptoResult<SharedSecret> Ecdh::compute_shared_secret(
    gsl::span<const uint8_t, kPublicKeySize> public_key,
    gsl::span<const uint8_t, kPrivateKeySize> private_key
) noexcept {
    auto* ctx = get_context();
    if ( !ctx ) {
        return CryptoError::kSecp256k1Error;
    }

    // Verify private key is valid
    if ( !secp256k1_ec_seckey_verify(ctx, private_key.data()) ) {
        ecdh_log()->debug("compute_shared_secret: secp256k1_ec_seckey_verify failed");
        return CryptoError::kInvalidPrivateKey;
    }

    // Parse public key (uncompressed format: 0x04 prefix + 64 bytes = kUncompressedPubKeySize)
    secp256k1_pubkey parsed_pubkey;
    
    std::array<uint8_t, kUncompressedPubKeySize> pubkey_with_prefix;
    pubkey_with_prefix[0] = kUncompressedPubKeyPrefix;
    std::memcpy(pubkey_with_prefix.data() + kUncompressedPubKeyPrefixSize, public_key.data(), kPublicKeySize);

    if ( !secp256k1_ec_pubkey_parse(ctx, &parsed_pubkey, pubkey_with_prefix.data(), kUncompressedPubKeySize) ) {
        ecdh_log()->debug("compute_shared_secret: secp256k1_ec_pubkey_parse failed");
        return CryptoError::kInvalidPublicKey;
    }

    // Compute ECDH shared point
    SharedSecret shared_secret;
    
    // Compute pubkey * privkey using secp256k1_ec_pubkey_tweak_mul equivalent
    secp256k1_pubkey result_pubkey = parsed_pubkey;
    if ( !secp256k1_ec_pubkey_tweak_mul(ctx, &result_pubkey, private_key.data()) ) {
        ecdh_log()->debug("compute_shared_secret: secp256k1_ec_pubkey_tweak_mul failed");
        return CryptoError::kEcdhFailed;
    }
    
    // Serialize the result (uncompressed format: 0x04 || x || y = kUncompressedPubKeySize bytes)
    unsigned char serialized[kUncompressedPubKeySize];
    size_t output_len = kUncompressedPubKeySize;
    if ( !secp256k1_ec_pubkey_serialize(ctx, serialized, &output_len, &result_pubkey, SECP256K1_EC_UNCOMPRESSED) ) {
        ecdh_log()->debug("compute_shared_secret: secp256k1_ec_pubkey_serialize failed");
        return CryptoError::kEcdhFailed;
    }
    
    // Extract x-coordinate (bytes after 0x04 prefix, skipping kUncompressedPubKeyPrefixSize byte)
    std::memcpy(shared_secret.data(), serialized + kUncompressedPubKeyPrefixSize, kSharedSecretSize);

    return shared_secret;
}

CryptoResult<Ecdh::KeyPair> Ecdh::generate_ephemeral_keypair() noexcept {
    auto* ctx = get_context();
    if ( !ctx ) {
        return CryptoError::kSecp256k1Error;
    }

    KeyPair keypair;

    // Generate random private key
    while ( true ) {
        if ( RAND_bytes(keypair.private_key.data(), kPrivateKeySize) != 1 ) {
            return CryptoError::kOpenSslError;
        }

        // Verify it's a valid private key
        if ( secp256k1_ec_seckey_verify(ctx, keypair.private_key.data()) ) {
            break;
        }
        // If invalid, try again (very rare)
    }

    // Compute corresponding public key
    secp256k1_pubkey pubkey;
    if ( !secp256k1_ec_pubkey_create(ctx, &pubkey, keypair.private_key.data()) ) {
        return CryptoError::kSecp256k1Error;
    }

    // Serialize public key in uncompressed format (kUncompressedPubKeySize bytes)
    std::array<uint8_t, kUncompressedPubKeySize> serialized;
    size_t output_len = kUncompressedPubKeySize;
    if ( !secp256k1_ec_pubkey_serialize(
            ctx,
            serialized.data(),
            &output_len,
            &pubkey,
            SECP256K1_EC_UNCOMPRESSED
        ) ) {
        return CryptoError::kSecp256k1Error;
    }

    // Copy public key (skip 0x04 prefix byte)
    std::memcpy(keypair.public_key.data(), serialized.data() + kUncompressedPubKeyPrefixSize, kPublicKeySize);

    return keypair;
}

bool Ecdh::verify_public_key(gsl::span<const uint8_t, kPublicKeySize> public_key) noexcept {
    auto* ctx = get_context();
    if ( !ctx ) {
        return false;
    }

    // Add 0x04 prefix for uncompressed format
    std::array<uint8_t, kUncompressedPubKeySize> pubkey_with_prefix;
    pubkey_with_prefix[0] = kUncompressedPubKeyPrefix;
    std::memcpy(pubkey_with_prefix.data() + kUncompressedPubKeyPrefixSize, public_key.data(), kPublicKeySize);

    secp256k1_pubkey parsed_pubkey;
    return secp256k1_ec_pubkey_parse(ctx, &parsed_pubkey, pubkey_with_prefix.data(), kUncompressedPubKeySize) == 1;
}

} // namespace rlpx::crypto
