// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv5/discv5_error.hpp"

namespace discv5
{

const char* to_string(discv5Error error) noexcept
{
    switch (error)
    {
        case discv5Error::kEnrMissingPrefix:
            return "ENR URI does not start with 'enr:'";
        case discv5Error::kEnrBase64DecodeFailed:
            return "Base64url decode of ENR body failed";
        case discv5Error::kEnrRlpDecodeFailed:
            return "RLP decode of ENR record failed";
        case discv5Error::kEnrTooShort:
            return "ENR RLP list has too few items (need signature + seq + at least one kv pair)";
        case discv5Error::kEnrTooLarge:
            return "Serialised ENR exceeds maximum allowed size";
        case discv5Error::kEnrSignatureInvalid:
            return "ENR secp256k1-v4 signature verification failed";
        case discv5Error::kEnrSignatureWrongSize:
            return "ENR signature field is not 64 bytes";
        case discv5Error::kEnrMissingSecp256k1Key:
            return "ENR record is missing required 'secp256k1' field";
        case discv5Error::kEnrInvalidSecp256k1Key:
            return "ENR 'secp256k1' field is not a valid compressed public key";
        case discv5Error::kEnrMissingAddress:
            return "ENR record has no dialable address ('ip' or 'ip6')";
        case discv5Error::kEnrInvalidIp:
            return "ENR 'ip' field is not exactly 4 bytes";
        case discv5Error::kEnrInvalidIp6:
            return "ENR 'ip6' field is not exactly 16 bytes";
        case discv5Error::kEnrInvalidUdpPort:
            return "ENR 'udp' port value is zero or out of range";
        case discv5Error::kEnrInvalidEthEntry:
            return "ENR 'eth' entry could not be decoded as [fork_hash, fork_next]";
        case discv5Error::kEnrIdentityUnknown:
            return "ENR 'id' field names an unsupported identity scheme";
        case discv5Error::kEnodeUriMalformed:
            return "enode:// URI could not be parsed";
        case discv5Error::kEnodeHexPubkeyInvalid:
            return "Hex-encoded public key in enode URI is invalid";
        case discv5Error::kContextCreationFailed:
            return "Failed to create secp256k1 context";
        case discv5Error::kCrawlerAlreadyRunning:
            return "Crawler start() called while already running";
        case discv5Error::kCrawlerNotRunning:
            return "Crawler stop() called while not running";
        case discv5Error::kNetworkSendFailed:
            return "UDP send operation failed";
        case discv5Error::kNetworkReceiveFailed:
            return "UDP receive operation failed";
        default:
            return "Unknown discv5 error";
    }
}

} // namespace discv5
