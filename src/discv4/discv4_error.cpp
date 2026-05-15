// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv4/discv4_error.hpp"

namespace discv4 {

const char* to_string(discv4Error error) noexcept
{
    switch (error)
    {
        case discv4Error::kNetworkSendFailed:
            return "Failed to send UDP packet";
        case discv4Error::kNetworkReceiveFailed:
            return "Failed to receive UDP packet";
        case discv4Error::kPacketTooSmall:
            return "Packet size below minimum (98 bytes)";
        case discv4Error::kInvalidPacketType:
            return "Unknown or invalid packet type";
        case discv4Error::kHashMismatch:
            return "Packet hash verification failed";
        case discv4Error::kSignatureParseFailed:
            return "Failed to parse ECDSA signature";
        case discv4Error::kSignatureRecoveryFailed:
            return "Failed to recover public key from signature";
        case discv4Error::kSigningFailed:
            return "Failed to sign packet with private key";
        case discv4Error::kContextCreationFailed:
            return "Failed to create secp256k1 context";
        case discv4Error::kInvalidPublicKey:
            return "Invalid or malformed public key";
        case discv4Error::kRlpPayloadEmpty:
            return "RLP payload generation returned empty";
        case discv4Error::kPongTimeout:
            return "PONG response timeout";
        case discv4Error::kPongParseFailed:
            return "Failed to parse PONG packet";
        default:
            return "Unknown error";
    }
}

} // namespace discv4

