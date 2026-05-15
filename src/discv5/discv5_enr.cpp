// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include "discv5/discv5_enr.hpp"
#include "discv5/discv5_constants.hpp"

#include <rlp/rlp_decoder.hpp>
#include <rlp/rlp_encoder.hpp>
#include <rlp/common.hpp>

#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>

#include <secp256k1.h>

#include <algorithm>
#include <array>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/address_v6.hpp>
#include <cstring>
#include <sstream>

namespace discv5
{

// ---------------------------------------------------------------------------
// Base64url decode lookup table
// ---------------------------------------------------------------------------

/// @brief Static decode table for the base64url alphabet (RFC-4648 §5).
///        Index = ASCII code, value = 6-bit group (or kBase64Invalid).
///        Built from the named constants in discv5_constants.hpp so that no
///        bare literals appear in the initialiser.
static const std::array<uint8_t, 256> kBase64UrlTable = []()
{
    std::array<uint8_t, 256> t{};
    t.fill(kBase64Invalid);

    // A–Z map to indices 0–25.
    for (uint8_t i = 0U; i < kBase64UpperCount; ++i)
    {
        t[static_cast<uint8_t>('A') + i] = i;
    }

    // a–z map to indices 26–51 (kBase64LowerStart).
    for (uint8_t i = 0U; i < kBase64LowerCount; ++i)
    {
        t[static_cast<uint8_t>('a') + i] = static_cast<uint8_t>(kBase64LowerStart + i);
    }

    // 0–9 map to indices 52–61 (kBase64DigitStart).
    for (uint8_t i = 0U; i < kBase64DigitCount; ++i)
    {
        t[static_cast<uint8_t>('0') + i] = static_cast<uint8_t>(kBase64DigitStart + i);
    }

    t[static_cast<uint8_t>('-')] = kBase64DashIndex;
    t[static_cast<uint8_t>('_')] = kBase64UnderIndex;
    return t;
}();

// ---------------------------------------------------------------------------
// Public: parse
// ---------------------------------------------------------------------------

Result<EnrRecord> EnrParser::parse(const std::string& enr_uri) noexcept
{
    BOOST_OUTCOME_TRY(auto raw, decode_uri(enr_uri));

    if (raw.size() > kEnrMaxBytes)
    {
        return discv5Error::kEnrTooLarge;
    }

    BOOST_OUTCOME_TRY(auto record, decode_rlp(raw));

    BOOST_OUTCOME_TRY(verify_signature(record));

    return record;
}

// ---------------------------------------------------------------------------
// Public: decode_uri
// ---------------------------------------------------------------------------

Result<std::vector<uint8_t>> EnrParser::decode_uri(const std::string& enr_uri) noexcept
{
    if (enr_uri.size() < kEnrPrefixLen ||
        enr_uri.compare(0, kEnrPrefixLen, kEnrPrefix) != 0)
    {
        return discv5Error::kEnrMissingPrefix;
    }

    const std::string body = enr_uri.substr(kEnrPrefixLen);
    return base64url_decode(body);
}

// ---------------------------------------------------------------------------
// Public: base64url_decode
// ---------------------------------------------------------------------------

Result<std::vector<uint8_t>> EnrParser::base64url_decode(const std::string& body) noexcept
{
    // Strip any trailing '=' padding that some implementations add.
    const size_t effective_len = [&]()
    {
        size_t n = body.size();
        while (n > 0U && body[n - 1U] == '=')
        {
            --n;
        }
        return n;
    }();

    // Output size = floor(effective_len * 6 / 8)
    const size_t out_size = (effective_len * 6U) / 8U;
    std::vector<uint8_t> out;
    out.reserve(out_size);

    uint32_t accumulator = 0U;
    size_t   bits        = 0U;

    for (size_t i = 0U; i < effective_len; ++i)
    {
        const uint8_t ch  = static_cast<uint8_t>(body[i]);
        const uint8_t val = kBase64UrlTable[ch];

        if (val == kBase64Invalid)
        {
            return discv5Error::kEnrBase64DecodeFailed;
        }

        accumulator = (accumulator << kBase64BitsPerChar) | val;
        bits += kBase64BitsPerChar;

        if (bits >= kBase64BitsPerByte)
        {
            bits -= kBase64BitsPerByte;
            out.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFFU));
        }
    }

    return out;
}

// ---------------------------------------------------------------------------
// Public: decode_rlp
// ---------------------------------------------------------------------------

Result<EnrRecord> EnrParser::decode_rlp(const std::vector<uint8_t>& raw) noexcept
{
    EnrRecord record;
    record.raw_rlp = raw;

    const rlp::ByteView view(raw.data(), raw.size());
    rlp::RlpDecoder     decoder(view);

    // Outer structure must be a list.
    {
        auto is_list_result = decoder.IsList();
        if (!is_list_result || !is_list_result.value())
        {
            return discv5Error::kEnrRlpDecodeFailed;
        }
    }

    // Read list header; returns the payload length in bytes.
    auto list_len_result = decoder.ReadListHeaderBytes();
    if (!list_len_result)
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }
    const size_t list_payload_len = list_len_result.value();

    // Snapshot the view immediately after the outer list header.
    const rlp::ByteView after_list_header = decoder.Remaining();

    // ----- Element 0: signature (64 bytes, compact secp256k1) ---------------

    {
        rlp::Bytes sig_bytes;
        if (!decoder.read(sig_bytes))
        {
            return discv5Error::kEnrRlpDecodeFailed;
        }
        if (sig_bytes.size() != kEnrSigBytes)
        {
            return discv5Error::kEnrSignatureWrongSize;
        }
        // Store sig bytes for verify_signature.
        record.extra_fields["__sig__"] =
            std::vector<uint8_t>(sig_bytes.begin(), sig_bytes.end());
    }

    // ----- Compute content bytes for signature verification -----------------
    // content = everything in the outer list AFTER the signature field,
    // re-wrapped as a new RLP list: RLP([seq, k1, v1, ...])
    {
        const rlp::ByteView after_sig = decoder.Remaining();
        const size_t        sig_consumed =
            after_list_header.size() - after_sig.size();
        const size_t content_elements_len = list_payload_len - sig_consumed;

        rlp::RlpEncoder content_enc;
        if (!content_enc.BeginList() ||
            !content_enc.AddRaw(rlp::ByteView(after_sig.data(), content_elements_len)) ||
            !content_enc.EndList())
        {
            return discv5Error::kEnrRlpDecodeFailed;
        }
        auto content_bytes_result = content_enc.MoveBytes();
        if (!content_bytes_result)
        {
            return discv5Error::kEnrRlpDecodeFailed;
        }
        const rlp::Bytes& cb = content_bytes_result.value();
        record.extra_fields["__content__"] =
            std::vector<uint8_t>(cb.begin(), cb.end());
    }

    // ----- Element 1: sequence number (uint64) ------------------------------

    if (!decoder.read(record.seq))
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }

    // ----- Elements 2..N: key–value pairs -----------------------------------

    while (!decoder.IsFinished())
    {
        // Key: encoded as RLP string (bytes → interpret as ASCII).
        rlp::Bytes key_bytes;
        if (!decoder.read(key_bytes))
        {
            break;
        }
        const std::string key(key_bytes.begin(), key_bytes.end());

        // Value: encoded as RLP string or embedded list.
        rlp::Bytes val_bytes;
        if (!decoder.read(val_bytes))
        {
            break;
        }
        const std::vector<uint8_t> val(val_bytes.begin(), val_bytes.end());

        if (key == "id")
        {
            record.identity_scheme = std::string(val.begin(), val.end());
        }
        else if (key == "secp256k1")
        {
            if (val.size() == kCompressedKeyBytes)
            {
                std::copy(val.begin(), val.end(), record.compressed_pubkey.begin());
            }
        }
        else if (key == "ip")
        {
            auto ip_result = decode_ipv4(val);
            if (ip_result)
            {
                record.ip = ip_result.value();
            }
        }
        else if (key == "ip6")
        {
            auto ip6_result = decode_ipv6(val);
            if (ip6_result)
            {
                record.ip6 = ip6_result.value();
            }
        }
        else if (key == "tcp")
        {
            auto port_result = decode_port(val);
            if (port_result)
            {
                record.tcp_port = port_result.value();
            }
        }
        else if (key == "udp")
        {
            auto port_result = decode_port(val);
            if (port_result)
            {
                record.udp_port = port_result.value();
            }
        }
        else if (key == "tcp6")
        {
            auto port_result = decode_port(val);
            if (port_result)
            {
                record.tcp6_port = port_result.value();
            }
        }
        else if (key == "udp6")
        {
            auto port_result = decode_port(val);
            if (port_result)
            {
                record.udp6_port = port_result.value();
            }
        }
        else if (key == "eth")
        {
            auto eth_result = decode_eth_entry(val);
            if (eth_result)
            {
                record.eth_fork_id = eth_result.value();
            }
        }
        else
        {
            record.extra_fields[key] = val;
        }
    }

    // Require at least the "secp256k1" key to be present.
    const bool has_pubkey =
        (record.compressed_pubkey != std::array<uint8_t, kCompressedKeyBytes>{});

    if (!has_pubkey)
    {
        return discv5Error::kEnrMissingSecp256k1Key;
    }

    return record;
}

// ---------------------------------------------------------------------------
// Public: verify_signature
// ---------------------------------------------------------------------------

VoidResult EnrParser::verify_signature(EnrRecord& record) noexcept
{
    // Retrieve stored signature and content bytes.
    const auto sig_it     = record.extra_fields.find("__sig__");
    const auto content_it = record.extra_fields.find("__content__");

    if (sig_it == record.extra_fields.end() || content_it == record.extra_fields.end())
    {
        return discv5Error::kEnrRlpDecodeFailed;
    }

    const std::vector<uint8_t>& sig_bytes     = sig_it->second;
    const std::vector<uint8_t>& content_bytes = content_it->second;

    // hash = keccak256(content)
    const auto hash_val =
        nil::crypto3::hash<nil::crypto3::hashes::keccak_1600<256>>(
            content_bytes.cbegin(), content_bytes.cend());
    const std::array<uint8_t, kKeccak256Bytes> hash_array = hash_val;

    secp256k1_context* ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    // Parse the compressed public key from "secp256k1" field.
    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(
            ctx, &pubkey,
            record.compressed_pubkey.data(),
            kCompressedKeyBytes))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    // Parse the compact (64-byte) ECDSA signature.
    secp256k1_ecdsa_signature sig;
    if (!secp256k1_ecdsa_signature_parse_compact(
            ctx, &sig, sig_bytes.data()))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrSignatureInvalid;
    }

    // Verify.
    if (!secp256k1_ecdsa_verify(ctx, &sig, hash_array.data(), &pubkey))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrSignatureInvalid;
    }

    secp256k1_context_destroy(ctx);

    // Derive the 64-byte uncompressed node_id.
    auto node_id_result = decompress_pubkey(record.compressed_pubkey);
    if (!node_id_result)
    {
        return node_id_result.error();
    }
    record.node_id = node_id_result.value();

    // Clean up internal bookkeeping fields.
    record.extra_fields.erase("__sig__");
    record.extra_fields.erase("__content__");

    return rlp::outcome::success();
}

// ---------------------------------------------------------------------------
// Public: to_validated_peer
// ---------------------------------------------------------------------------

Result<ValidatedPeer> EnrParser::to_validated_peer(const EnrRecord& record) noexcept
{
    if (record.ip.empty() && record.ip6.empty())
    {
        return discv5Error::kEnrMissingAddress;
    }

    ValidatedPeer peer;
    peer.node_id    = record.node_id;
    peer.eth_fork_id = record.eth_fork_id;
    peer.last_seen  = std::chrono::steady_clock::now();

    // Prefer IPv4 when available.
    if (!record.ip.empty())
    {
        peer.ip       = record.ip;
        peer.udp_port = (record.udp_port != 0U) ? record.udp_port : kDefaultUdpPort;
        peer.tcp_port = (record.tcp_port != 0U) ? record.tcp_port : kDefaultTcpPort;
    }
    else
    {
        peer.ip       = record.ip6;
        peer.udp_port = (record.udp6_port != 0U) ? record.udp6_port : kDefaultUdpPort;
        peer.tcp_port = (record.tcp6_port != 0U) ? record.tcp6_port : kDefaultTcpPort;
    }

    return peer;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

Result<std::string> EnrParser::decode_ipv4(const std::vector<uint8_t>& bytes) noexcept
{
    if (bytes.size() != kIPv4Bytes)
    {
        return discv5Error::kEnrInvalidIp;
    }

    // Use IPv4Wire struct to name each field — no magic byte-array indexes.
    IPv4Wire ip{};
    std::memcpy(&ip, bytes.data(), sizeof(IPv4Wire));

    const boost::asio::ip::address_v4::bytes_type address_bytes{
        ip.msb,
        ip.b1,
        ip.b2,
        ip.lsb
    };

    return boost::asio::ip::address_v4(address_bytes).to_string();
}

Result<std::string> EnrParser::decode_ipv6(const std::vector<uint8_t>& bytes) noexcept
{
    if (bytes.size() != kIPv6Bytes)
    {
        return discv5Error::kEnrInvalidIp6;
    }

    // Use IPv6Wire struct and convert it via Boost.Asio.
    IPv6Wire ip6{};
    std::memcpy(ip6.bytes, bytes.data(), sizeof(IPv6Wire));

    boost::asio::ip::address_v6::bytes_type address_bytes{};
    std::copy(std::begin(ip6.bytes), std::end(ip6.bytes), address_bytes.begin());

    return boost::asio::ip::address_v6(address_bytes).to_string();
}

Result<uint16_t> EnrParser::decode_port(const std::vector<uint8_t>& bytes) noexcept
{
    if (bytes.empty() || bytes.size() > kMaxPortBytes)
    {
        return discv5Error::kEnrInvalidUdpPort;
    }

    uint16_t port = 0U;
    for (const uint8_t b : bytes)
    {
        port = static_cast<uint16_t>((port << 8U) | b);
    }

    if (port == 0U)
    {
        return discv5Error::kEnrInvalidUdpPort;
    }

    return port;
}

Result<ForkId> EnrParser::decode_eth_entry(const std::vector<uint8_t>& bytes) noexcept
{
    // "eth" value is RLP([[fork_hash(4 bytes), fork_next(uint64)]]).
    // The outer value bytes are the RLP-encoded list payload.
    if (bytes.empty())
    {
        return discv5Error::kEnrInvalidEthEntry;
    }

    const rlp::ByteView view(bytes.data(), bytes.size());
    rlp::RlpDecoder     outer_dec(view);

    // Outer list (the fork-id list).
    {
        auto is_list = outer_dec.IsList();
        if (!is_list || !is_list.value())
        {
            return discv5Error::kEnrInvalidEthEntry;
        }
    }
    if (!outer_dec.ReadListHeaderBytes())
    {
        return discv5Error::kEnrInvalidEthEntry;
    }

    // Inner fork-id record: [hash(4 bytes), next(uint64)].
    {
        auto is_list = outer_dec.IsList();
        if (!is_list || !is_list.value())
        {
            return discv5Error::kEnrInvalidEthEntry;
        }
    }
    if (!outer_dec.ReadListHeaderBytes())
    {
        return discv5Error::kEnrInvalidEthEntry;
    }

    // fork_hash — exactly kForkHashBytes bytes.
    rlp::Bytes hash_bytes;
    if (!outer_dec.read(hash_bytes) || hash_bytes.size() != kForkHashBytes)
    {
        return discv5Error::kEnrInvalidEthEntry;
    }

    ForkId fork_id;
    std::copy(hash_bytes.begin(), hash_bytes.end(), fork_id.hash.begin());

    // fork_next — uint64.
    if (!outer_dec.read(fork_id.next))
    {
        return discv5Error::kEnrInvalidEthEntry;
    }

    return fork_id;
}

Result<NodeId> EnrParser::decompress_pubkey(
    const std::array<uint8_t, kCompressedKeyBytes>& compressed) noexcept
{
    secp256k1_context* ctx =
        secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (ctx == nullptr)
    {
        return discv5Error::kContextCreationFailed;
    }

    secp256k1_pubkey pubkey;
    if (!secp256k1_ec_pubkey_parse(ctx, &pubkey, compressed.data(), kCompressedKeyBytes))
    {
        secp256k1_context_destroy(ctx);
        return discv5Error::kEnrInvalidSecp256k1Key;
    }

    // Serialise as uncompressed into a named wire struct — no bare 65 literal.
    UncompressedPubKeyWire raw_key{};
    size_t len = sizeof(UncompressedPubKeyWire);
    secp256k1_ec_pubkey_serialize(
        ctx, reinterpret_cast<uint8_t*>(&raw_key), &len,
        &pubkey, SECP256K1_EC_UNCOMPRESSED);

    secp256k1_context_destroy(ctx);

    // Copy the 64-byte X||Y payload (skip the 0x04 prefix stored in raw_key.prefix).
    NodeId node_id{};
    std::copy(raw_key.xy, raw_key.xy + kNodeIdBytes, node_id.begin());

    return node_id;
}

} // namespace discv5
