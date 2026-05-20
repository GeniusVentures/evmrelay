// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_RLPX_AUTH_AUTH_KEYS_HPP
#define EVMRELAY_INCLUDE_RLPX_AUTH_AUTH_KEYS_HPP

#include "../rlpx_types.hpp"

namespace rlpx::auth {

// Authentication key material generated during handshake
struct AuthKeyMaterial {
    PublicKey peer_public_key;
    PublicKey peer_ephemeral_public_key;
    PublicKey local_ephemeral_public_key;
    PrivateKey local_ephemeral_private_key;
    Nonce initiator_nonce;
    Nonce recipient_nonce;
    ByteBuffer initiator_auth_message;
    ByteBuffer recipient_ack_message;
};

// Derived frame encryption secrets
struct FrameSecrets {
    AesKey     aes_secret;
    MacKey     mac_secret;
    ByteBuffer egress_mac_seed;   ///< Raw bytes written to egress MAC Keccak accumulator
    ByteBuffer ingress_mac_seed;  ///< Raw bytes written to ingress MAC Keccak accumulator
};

} // namespace rlpx::auth

#endif // EVMRELAY_INCLUDE_RLPX_AUTH_AUTH_KEYS_HPP
