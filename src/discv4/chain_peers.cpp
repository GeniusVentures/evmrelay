// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <discv4/chain_peers.hpp>
#include <discv4/discv4_packet.hpp>
#include <discv5/discv5_enr.hpp>
#include <base/parse_utility.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/json.hpp>
#include <boost/json/serialize.hpp>
#include <zlib.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/sha.h>
#include <openssl/ssl.h>
#include <secp256k1_recovery.h>

namespace
{

namespace http = boost::beast::http;

inline constexpr std::string_view kSchemeHttps = "https://";
inline constexpr std::string_view kSchemeHttp = "http://";
inline constexpr char kPathSeparator = '/';
inline constexpr char kPortSeparator = ':';
inline constexpr char kQuerySeparator = '?';
inline constexpr char kEnodeSeparator = '@';
inline constexpr size_t kGzipMagicSize = 2;
inline constexpr unsigned char kGzipMagicByte0 = 0x1f;
inline constexpr unsigned char kGzipMagicByte1 = 0x8b;
inline constexpr int kGzipWindowBits = 16 + MAX_WBITS;
inline constexpr size_t kInflateChunkSize = 16384;
inline constexpr int kHttpVersion11 = 11;
inline constexpr int kHttpStatusOk = 200;
inline constexpr int kHttpStatusMultipleChoices = 300;
inline constexpr std::string_view kDefaultTarget = "/";
inline constexpr std::string_view kDefaultHttpsPort = "443";
inline constexpr std::string_view kDefaultHttpPort = "80";
inline constexpr std::string_view kEnodePrefix = "enode://";
inline constexpr std::string_view kChainPeerCacheJsonFilename = "chain_enodes.json";
inline constexpr std::string_view kChainPeerCacheJsonGzipFilename = "chain_enodes.json.gz";
inline constexpr std::string_view kChainPeerNodesFieldName = "nodes";
inline constexpr std::string_view kChainPeerBootnodesFieldName = "bootnodes";
inline constexpr std::string_view kSignatureFieldName = "signature";
inline constexpr std::string_view kSignerAddressFieldName = "signerAddress";
inline constexpr std::string_view kTrustedChainPeerCacheSignerAddress = "0x7c91841f3594cb02dba5aae5909ceaaf2211d454";
inline constexpr size_t kRecoverableSignatureSize = 65;
inline constexpr size_t kUncompressedPublicKeySize = 65;
inline constexpr unsigned char kUncompressedPublicKeyPrefix = 0x04;
inline constexpr size_t kEthereumAddressSize = 20;
inline constexpr size_t kSha256Size = 32;

struct HttpUrlParts
{
    bool        is_https = false;
    std::string host;
    std::string port;
    std::string target;
};

std::optional<std::string> decode_chain_peer_cache_body(std::string body)
{
    if (body.size() < kGzipMagicSize
        || static_cast<unsigned char>(body.at(0)) != kGzipMagicByte0
        || static_cast<unsigned char>(body.at(1)) != kGzipMagicByte1)
    {
        return body;
    }

    z_stream stream_state{};
    stream_state.next_in = reinterpret_cast<Bytef*>(body.data());
    stream_state.avail_in = static_cast<uInt>(body.size());

    if (inflateInit2(&stream_state, kGzipWindowBits) != Z_OK)
    {
        return std::nullopt;
    }

    std::string decoded;
    std::array<char, kInflateChunkSize> chunk{};
    int rc = Z_OK;
    while (rc != Z_STREAM_END)
    {
        stream_state.next_out = reinterpret_cast<Bytef*>(chunk.data());
        stream_state.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&stream_state, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END)
        {
            inflateEnd(&stream_state);
            return std::nullopt;
        }

        decoded.append(chunk.data(), chunk.size() - stream_state.avail_out);
    }

    inflateEnd(&stream_state);
    return decoded;
}

std::optional<HttpUrlParts> parse_http_url(std::string_view url)
{
    HttpUrlParts out{};
    std::string_view rest;
    if (url.rfind(kSchemeHttps, 0) == 0)
    {
        out.is_https = true;
        rest = url.substr(kSchemeHttps.size());
    }
    else if (url.rfind(kSchemeHttp, 0) == 0)
    {
        out.is_https = false;
        rest = url.substr(kSchemeHttp.size());
    }
    else
    {
        return std::nullopt;
    }

    const auto slash = rest.find(kPathSeparator);
    const auto host_port = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    out.target = (slash == std::string_view::npos) ? std::string(kDefaultTarget) : std::string(rest.substr(slash));
    if (host_port.empty())
    {
        return std::nullopt;
    }

    const auto colon = host_port.rfind(kPortSeparator);
    if (colon != std::string_view::npos)
    {
        out.host = std::string(host_port.substr(0, colon));
        out.port = std::string(host_port.substr(colon + 1));
    }
    else
    {
        out.host = std::string(host_port);
        out.port = out.is_https ? std::string(kDefaultHttpsPort) : std::string(kDefaultHttpPort);
    }

    if (out.host.empty() || out.port.empty())
    {
        return std::nullopt;
    }
    return out;
}

std::optional<eth::Hash256> parse_hash256(std::string_view value)
{
    eth::Hash256 hash{};
    if (!rlp::base::parse::hex_array(value, hash))
    {
        return std::nullopt;
    }
    return hash;
}

std::optional<uint64_t> parse_fork_next_value(const boost::json::value& value)
{
    if (value.is_int64() && value.as_int64() >= 0)
    {
        return static_cast<uint64_t>(value.as_int64());
    }

    if (!value.is_string())
    {
        return std::nullopt;
    }

    const auto& string_value = value.as_string();
    return rlp::base::parse::uint64_hex(std::string_view(string_value.data(), string_value.size()));
}

bool parse_signature_bytes(
    std::string_view                                signature,
    std::array<uint8_t, kRecoverableSignatureSize>& out)
{
    return rlp::base::parse::hex_array(signature, out);
}

std::optional<std::string> parse_signature_string(const boost::json::object& root)
{
    const auto* signature = root.if_contains(std::string(kSignatureFieldName));
    if (signature == nullptr || !signature->is_string())
    {
        return std::nullopt;
    }

    const auto& signature_string = signature->as_string();
    return std::string(signature_string.data(), signature_string.size());
}

std::optional<std::string> parse_signer_address_string(const boost::json::object& root)
{
    const auto* signer_address = root.if_contains(std::string(kSignerAddressFieldName));
    if (signer_address == nullptr || !signer_address->is_string())
    {
        return std::nullopt;
    }

    const auto& signer_address_string = signer_address->as_string();
    return std::string(signer_address_string.data(), signer_address_string.size());
}

boost::json::object make_unsigned_chain_peer_cache_object(const boost::json::object& root)
{
    boost::json::object unsigned_root;
    for (const auto& entry : root)
    {
        if (entry.key() == kSignatureFieldName || entry.key() == kSignerAddressFieldName)
        {
            continue;
        }
        unsigned_root[entry.key()] = entry.value();
    }
    return unsigned_root;
}

std::array<uint8_t, kSha256Size> sha256_bytes(const std::string& json_text)
{
    std::array<uint8_t, kSha256Size> hash{};
    SHA256(
        reinterpret_cast<const unsigned char*>(json_text.data()),
        json_text.size(),
        hash.data());
    return hash;
}

std::optional<std::string> recover_signer_address(
    const std::array<uint8_t, kSha256Size>&               message_hash,
    const std::array<uint8_t, kRecoverableSignatureSize>& signature_bytes)
{
    secp256k1_context* context = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (context == nullptr)
    {
        return std::nullopt;
    }

    const int recovery_id = static_cast<int>(signature_bytes.back());
    secp256k1_ecdsa_recoverable_signature signature{};
    if (!secp256k1_ecdsa_recoverable_signature_parse_compact(context, &signature, signature_bytes.data(), recovery_id))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_pubkey public_key{};
    if (!secp256k1_ecdsa_recover(context, &public_key, &signature, message_hash.data()))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    std::array<uint8_t, kUncompressedPublicKeySize> public_key_bytes{};
    size_t public_key_length = public_key_bytes.size();
    if (!secp256k1_ec_pubkey_serialize(
            context,
            public_key_bytes.data(),
            &public_key_length,
            &public_key,
            SECP256K1_EC_UNCOMPRESSED))
    {
        secp256k1_context_destroy(context);
        return std::nullopt;
    }

    secp256k1_context_destroy(context);

    if (public_key_length != public_key_bytes.size() || public_key_bytes.front() != kUncompressedPublicKeyPrefix)
    {
        return std::nullopt;
    }

    const std::vector<uint8_t> public_key_payload(public_key_bytes.begin() + 1, public_key_bytes.end());
    const auto address_hash = discv4::discv4_packet::Keccak256(public_key_payload);

    static constexpr std::string_view kHexDigits = "0123456789abcdef";
    std::string address = "0x";
    address.reserve(2 + (kEthereumAddressSize * rlp::base::parse::kHexCharsPerByte));
    for (auto it = address_hash.end() - static_cast<std::ptrdiff_t>(kEthereumAddressSize); it != address_hash.end(); ++it)
    {
        const uint8_t byte = *it;
        address.push_back(kHexDigits[(byte >> 4) & 0x0f]);
        address.push_back(kHexDigits[byte & 0x0f]);
    }

    return address;
}

std::optional<discv4::ChainPeerCacheSignatureVerificationResult> verify_chain_peer_cache_json_signature_impl(
    const boost::json::value& parsed,
    const std::string&        expected_signer_address)
{
    const auto* root = parsed.if_object();
    if (root == nullptr)
    {
        return std::nullopt;
    }

    discv4::ChainPeerCacheSignatureVerificationResult result{};
    const auto signature = parse_signature_string(*root);
    const auto signer_address = parse_signer_address_string(*root);
    if (!signature.has_value() || !signer_address.has_value())
    {
        return result;
    }

    result.has_signature = true;
    result.signer_address = *signer_address;

    std::array<uint8_t, kRecoverableSignatureSize> signature_bytes{};
    if (!parse_signature_bytes(*signature, signature_bytes))
    {
        return result;
    }

    const boost::json::object unsigned_root = make_unsigned_chain_peer_cache_object(*root);
    const std::string unsigned_json = boost::json::serialize(unsigned_root);
    const auto message_hash = sha256_bytes(unsigned_json);
    const auto recovered_signer_address = recover_signer_address(message_hash, signature_bytes);
    if (!recovered_signer_address.has_value())
    {
        return result;
    }

    const std::string expected = rlp::base::parse::ascii_lower(expected_signer_address);
    const std::string recovered = rlp::base::parse::ascii_lower(*recovered_signer_address);
    const std::string declared = rlp::base::parse::ascii_lower(*signer_address);
    result.signature_valid = !expected.empty() && recovered == expected && declared == expected;
    return result;
}

const boost::json::object* find_chain_peer_cache_chain_object(
    const boost::json::value& parsed,
    const std::string&        chain_name)
{
    const auto* root = parsed.if_object();
    if (root == nullptr)
    {
        return nullptr;
    }

    const auto* chain_entry = root->if_contains(chain_name);
    if (chain_entry == nullptr)
    {
        return nullptr;
    }

    return chain_entry->if_object();
}

const boost::json::array* find_chain_peer_cache_chain_peer_array(
    const boost::json::value& parsed,
    const std::string&        chain_name,
    std::string_view          field_name)
{
    const auto* chain_object = find_chain_peer_cache_chain_object(parsed, chain_name);
    if (chain_object == nullptr)
    {
        return nullptr;
    }

    const auto* nodes = chain_object->if_contains(std::string(field_name));
    if (nodes == nullptr)
    {
        return nullptr;
    }

    return nodes->if_array();
}

std::optional<discv4::ValidatedPeer> make_validated_peer_from_generated_node_fields(
    const boost::json::object& node);

std::vector<discv4::ValidatedPeer> parse_chain_peers_from_json_value(
    const boost::json::value& parsed,
    const std::string&        chain_name,
    std::string_view          field_name)
{
    std::vector<discv4::ValidatedPeer> peers;

    const auto* arr = find_chain_peer_cache_chain_peer_array(parsed, chain_name, field_name);
    if (arr == nullptr)
    {
        return peers;
    }

    for (const auto& item : *arr)
    {
        const auto* obj = item.if_object();
        if (obj == nullptr)
        {
            continue;
        }

        if (const auto* enr = obj->if_contains("enr"); enr != nullptr && enr->is_string())
        {
            const auto& enr_str = enr->as_string();
            if (auto vp = discv4::make_validated_peer_from_enr(std::string(enr_str.data(), enr_str.size())))
            {
                peers.push_back(std::move(*vp));
                continue;
            }
        }

        if (auto vp = make_validated_peer_from_generated_node_fields(*obj))
        {
            peers.push_back(std::move(*vp));
            continue;
        }

        if (const auto* enode = obj->if_contains("enode"); enode != nullptr && enode->is_string())
        {
            const auto& enode_str = enode->as_string();
            if (auto vp = discv4::make_validated_peer_from_enode(std::string(enode_str.data(), enode_str.size())))
            {
                peers.push_back(std::move(*vp));
            }
        }
    }

    return peers;
}

std::optional<std::array<uint8_t, 4>> parse_chain_fork_id_hash_from_json_value(
    const boost::json::value& parsed,
    const std::string&        chain_name)
{
    const auto* arr = find_chain_peer_cache_chain_peer_array(parsed, chain_name, kChainPeerNodesFieldName);
    if (arr == nullptr)
    {
        return std::nullopt;
    }

    for (const auto& item : *arr)
    {
        const auto* obj = item.if_object();
        if (obj == nullptr)
        {
            continue;
        }

        const auto* fork_id = obj->if_contains("forkId");
        if (fork_id == nullptr || !fork_id->is_string())
        {
            continue;
        }

        std::array<uint8_t, 4> fork_hash{};
        const auto& fork_id_str = fork_id->as_string();
        if (rlp::base::parse::hex_array(std::string_view(fork_id_str.data(), fork_id_str.size()), fork_hash))
        {
            return fork_hash;
        }
    }

    return std::nullopt;
}

std::optional<discv4::ValidatedPeer> make_validated_peer_from_generated_node_fields(
    const boost::json::object& node)
{
    const auto* pubkey = node.if_contains("pubkey");
    const auto* ip = node.if_contains("ip");
    const auto* port = node.if_contains("port");
    if (pubkey == nullptr || ip == nullptr || port == nullptr)
    {
        return std::nullopt;
    }
    if (!pubkey->is_string() || !ip->is_string() || !port->is_int64())
    {
        return std::nullopt;
    }

    const auto port_signed = port->as_int64();
    if (port_signed <= 0 || port_signed > UINT16_MAX)
    {
        return std::nullopt;
    }

    const auto& pubkey_string = pubkey->as_string();
    discv4::NodeId node_id{};
    if (!rlp::base::parse::hex_array(std::string_view(pubkey_string.data(), pubkey_string.size()), node_id))
    {
        return std::nullopt;
    }

    const auto& ip_string = ip->as_string();
    discv4::ValidatedPeer peer{};
    peer.peer.node_id = node_id;
    peer.peer.ip = std::string(ip_string.data(), ip_string.size());
    peer.peer.tcp_port = static_cast<uint16_t>(port_signed);
    peer.peer.udp_port = static_cast<uint16_t>(port_signed);
    peer.peer.last_seen = std::chrono::steady_clock::now();
    std::copy(node_id.begin(), node_id.end(), peer.pubkey.begin());
    return peer;
}

std::optional<discv4::ChainPeerConfig> parse_chain_peer_config_from_json_value(
    const boost::json::value& parsed,
    const std::string&        chain_name)
{
    const auto* chain_object = find_chain_peer_cache_chain_object(parsed, chain_name);
    if (chain_object == nullptr)
    {
        return std::nullopt;
    }

    const auto* network_id_value = chain_object->if_contains("networkId");
    const auto* genesis_hex_value = chain_object->if_contains("genesisHex");
    const auto* nodes = find_chain_peer_cache_chain_peer_array(parsed, chain_name, kChainPeerNodesFieldName);
    const auto* bootnodes = find_chain_peer_cache_chain_peer_array(parsed, chain_name, kChainPeerBootnodesFieldName);
    if (network_id_value == nullptr || genesis_hex_value == nullptr || nodes == nullptr || bootnodes == nullptr)
    {
        return std::nullopt;
    }
    if (!network_id_value->is_int64() || !genesis_hex_value->is_string())
    {
        return std::nullopt;
    }

    const auto network_id_signed = network_id_value->as_int64();
    if (network_id_signed < 0)
    {
        return std::nullopt;
    }

    const auto& genesis_hex_string = genesis_hex_value->as_string();
    const auto genesis_hash = parse_hash256(std::string_view(genesis_hex_string.data(), genesis_hex_string.size()));
    if (!genesis_hash.has_value())
    {
        return std::nullopt;
    }

    discv4::ChainPeerConfig config{};
    config.canonical_name = chain_name;
    config.network_id = static_cast<uint64_t>(network_id_signed);
    config.genesis_hash = *genesis_hash;
    config.nodes = parse_chain_peers_from_json_value(parsed, chain_name, kChainPeerNodesFieldName);
    config.bootnodes = parse_chain_peers_from_json_value(parsed, chain_name, kChainPeerBootnodesFieldName);

    if (const auto* fork_id_value = chain_object->if_contains("forkId");
        fork_id_value != nullptr && fork_id_value->is_string())
    {
        std::array<uint8_t, 4> fork_hash{};
        const auto& fork_id_string = fork_id_value->as_string();
        if (rlp::base::parse::hex_array(std::string_view(fork_id_string.data(), fork_id_string.size()), fork_hash))
        {
            eth::ForkId fork_id{};
            fork_id.fork_hash = fork_hash;
            if (const auto* fork_next_value = chain_object->if_contains("forkNext");
                fork_next_value != nullptr)
            {
                if (const auto parsed_fork_next = parse_fork_next_value(*fork_next_value);
                    parsed_fork_next.has_value())
                {
                    fork_id.next_fork = *parsed_fork_next;
                }
            }
            config.fork_id = fork_id;
        }
    }
    else if (const auto fork_hash = parse_chain_fork_id_hash_from_json_value(parsed, chain_name);
             fork_hash.has_value())
    {
        eth::ForkId fork_id{};
        fork_id.fork_hash = *fork_hash;
        config.fork_id = fork_id;
    }

    if (const auto verification = verify_chain_peer_cache_json_signature_impl(parsed, std::string(kTrustedChainPeerCacheSignerAddress));
        verification.has_value())
    {
        if (verification->has_signature && !verification->signature_valid)
        {
            return std::nullopt;
        }
        config.signature = verification->has_signature ? "present" : "";
        config.signer_address = verification->signer_address;
    }

    return config;
}

[[nodiscard]] bool is_success_status(const int status_code) noexcept
{
    return status_code >= kHttpStatusOk && status_code < kHttpStatusMultipleChoices;
}

std::optional<std::filesystem::path> find_existing_chain_peer_cache_json_path(
    const std::filesystem::path& directory)
{
    std::filesystem::path json_file = directory / std::string(kChainPeerCacheJsonFilename);
    if (std::filesystem::is_regular_file(json_file))
    {
        return json_file;
    }

    std::filesystem::path gzip_file = directory / std::string(kChainPeerCacheJsonGzipFilename);
    if (std::filesystem::is_regular_file(gzip_file))
    {
        return gzip_file;
    }

    return std::nullopt;
}

} // namespace

namespace discv4
{

std::optional<ValidatedPeer> make_validated_peer_from_enode(const std::string& enode)
{
    constexpr std::string_view kPrefix = kEnodePrefix;
    if (enode.size() < kPrefix.size() || std::string_view(enode).substr(0, kPrefix.size()) != kPrefix)
    {
        return std::nullopt;
    }

    const auto without_prefix = std::string_view(enode).substr(kPrefix.size());
    const auto at_pos = without_prefix.find(kEnodeSeparator);
    if (at_pos == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto pubkey_hex = without_prefix.substr(0, at_pos);
    if (pubkey_hex.size() != rlpx::kPublicKeySize * rlp::base::parse::kHexCharsPerByte)
    {
        return std::nullopt;
    }

    const auto address_part = without_prefix.substr(at_pos + 1);
    const auto query_pos = address_part.find(kQuerySeparator);
    const auto host_port = address_part.substr(0, query_pos);
    const auto colon_pos = host_port.rfind(kPortSeparator);
    if (colon_pos == std::string_view::npos)
    {
        return std::nullopt;
    }

    const auto port_value = rlp::base::parse::uint16_decimal(host_port.substr(colon_pos + 1));
    if (!port_value.has_value())
    {
        return std::nullopt;
    }

    NodeId node_id{};
    if (!rlp::base::parse::hex_array(pubkey_hex, node_id))
    {
        return std::nullopt;
    }

    ValidatedPeer vp{};
    vp.peer.node_id = node_id;
    vp.peer.ip = std::string(host_port.substr(0, colon_pos));
    vp.peer.udp_port = *port_value;
    vp.peer.tcp_port = *port_value;
    vp.peer.last_seen = std::chrono::steady_clock::now();
    std::copy(node_id.begin(), node_id.end(), vp.pubkey.begin());
    return vp;
}

std::optional<ValidatedPeer> make_validated_peer_from_enr(const std::string& enr_uri)
{
    const auto record_result = discv5::EnrParser::parse(enr_uri);
    if (!record_result)
    {
        return std::nullopt;
    }

    const auto peer_result = discv5::EnrParser::to_validated_peer(record_result.value());
    if (!peer_result)
    {
        return std::nullopt;
    }

    const auto& peer = peer_result.value();
    ValidatedPeer vp{};
    vp.peer.node_id = peer.node_id;
    vp.peer.ip = peer.ip;
    vp.peer.udp_port = peer.udp_port;
    vp.peer.tcp_port = peer.tcp_port;
    vp.peer.last_seen = peer.last_seen;
    if (peer.eth_fork_id.has_value())
    {
        ForkId fork_id{};
        fork_id.hash = peer.eth_fork_id->hash;
        fork_id.next = peer.eth_fork_id->next;
        vp.peer.eth_fork_id = fork_id;
    }
    std::copy(peer.node_id.begin(), peer.node_id.end(), vp.pubkey.begin());
    return vp;
}

std::filesystem::path chain_peer_cache_json_path(const std::string& argv0)
{
    return std::filesystem::path(argv0).parent_path() / std::string(kChainPeerCacheJsonFilename);
}

std::optional<std::filesystem::path> find_chain_peer_cache_json_path(
    const std::string& argv0,
    const std::string& override_path)
{
    if (!override_path.empty())
    {
        std::filesystem::path override_file(override_path);
        if (std::filesystem::is_regular_file(override_file))
        {
            return override_file;
        }
        return std::nullopt;
    }

    const std::filesystem::path bin_dir = std::filesystem::path(argv0).parent_path();
    if (const auto local_file = find_existing_chain_peer_cache_json_path(bin_dir); local_file.has_value())
    {
        return local_file;
    }

    if (bin_dir.filename() == "MacOS" && bin_dir.parent_path().filename() == "Contents")
    {
        const auto resources_dir = bin_dir.parent_path() / "Resources";
        if (const auto resources_file = find_existing_chain_peer_cache_json_path(resources_dir); resources_file.has_value())
        {
            return resources_file;
        }
    }

    return std::nullopt;
}

std::optional<std::string> download_chain_peer_cache_json(const std::string& url)
{
    const auto url_parts = parse_http_url(url);
    if (!url_parts.has_value())
    {
        return std::nullopt;
    }

    boost::asio::io_context ioc;
    boost::asio::ip::tcp::resolver resolver(ioc);
    boost::beast::flat_buffer buffer;

    boost::system::error_code ec;
    const auto results = resolver.resolve(url_parts->host, url_parts->port, ec);
    if (ec)
    {
        return std::nullopt;
    }

    if (url_parts->is_https)
    {
        boost::asio::ssl::context ssl_ctx(boost::asio::ssl::context::tls_client);
        ssl_ctx.set_verify_mode(boost::asio::ssl::verify_none);
        boost::beast::ssl_stream<boost::beast::tcp_stream> stream(ioc, ssl_ctx);
        if (!SSL_set_tlsext_host_name(stream.native_handle(), url_parts->host.c_str()))
        {
            return std::nullopt;
        }
        boost::beast::get_lowest_layer(stream).connect(results, ec);
        if (ec)
        {
            return std::nullopt;
        }

        stream.handshake(boost::asio::ssl::stream_base::client, ec);
        if (ec)
        {
            return std::nullopt;
        }

        http::request<http::empty_body> req{http::verb::get, url_parts->target, kHttpVersion11};
        req.set(http::field::host, url_parts->host);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(stream, req, ec);
        if (ec)
        {
            return std::nullopt;
        }

        http::response<http::string_body> res;
        http::read(stream, buffer, res, ec);
        if (ec || !is_success_status(static_cast<int>(res.result_int())))
        {
            return std::nullopt;
        }

        stream.shutdown(ec);
        return decode_chain_peer_cache_body(std::move(res.body()));
    }

    boost::beast::tcp_stream stream(ioc);
    stream.connect(results, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::request<http::empty_body> req{http::verb::get, url_parts->target, kHttpVersion11};
    req.set(http::field::host, url_parts->host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    http::write(stream, req, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::response<http::string_body> res;
    http::read(stream, buffer, res, ec);
    if (ec || !is_success_status(static_cast<int>(res.result_int())))
    {
        return std::nullopt;
    }

    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    return decode_chain_peer_cache_body(std::move(res.body()));
}

bool write_chain_peer_cache_json_if_changed(
    const std::filesystem::path& json_path,
    const std::string&           json_text)
{
    std::ifstream existing_file(json_path, std::ios::binary);
    if (existing_file.is_open())
    {
        const std::string existing_json(
            (std::istreambuf_iterator<char>(existing_file)),
            std::istreambuf_iterator<char>());
        if (existing_json == json_text)
        {
            return false;
        }
    }

    std::ofstream output(json_path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        return false;
    }

    output.write(json_text.data(), static_cast<std::streamsize>(json_text.size()));
    return output.good();
}

std::optional<ChainPeerCacheRefreshResult> refresh_chain_peer_cache_json(
    const std::filesystem::path& json_path,
    const std::string&           url)
{
    const auto json_text = download_chain_peer_cache_json(url);
    if (!json_text.has_value())
    {
        return std::nullopt;
    }

    const auto verification = verify_chain_peer_cache_json_signature(*json_text, std::string(kTrustedChainPeerCacheSignerAddress));
    if (!verification.has_signature || !verification.signature_valid)
    {
        return std::nullopt;
    }

    ChainPeerCacheRefreshResult result{};
    result.cache_path = json_path;
    result.cache_updated = write_chain_peer_cache_json_if_changed(json_path, *json_text);
    result.cache_available = result.cache_updated || std::filesystem::is_regular_file(json_path);
    return result;
}

std::vector<ValidatedPeer> load_chain_peers_from_json_text(
    const std::string& chain_name,
    const std::string& json_text)
{
    std::vector<ValidatedPeer> peers;
    if (chain_name.empty())
    {
        return peers;
    }

    boost::system::error_code ec;
    const boost::json::value parsed = boost::json::parse(json_text, ec);
    if (ec)
    {
        return peers;
    }

    return parse_chain_peers_from_json_value(parsed, chain_name, kChainPeerNodesFieldName);
}

std::vector<ValidatedPeer> load_chain_peers_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path)
{
    std::vector<ValidatedPeer> peers;
    if (chain_name.empty())
    {
        return peers;
    }

    std::ifstream file(json_path, std::ios::binary);
    if (!file.is_open())
    {
        return peers;
    }

    const std::string json_text(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    const auto decoded_json_text = decode_chain_peer_cache_body(json_text);
    if (!decoded_json_text.has_value())
    {
        return peers;
    }

    return load_chain_peers_from_json_text(chain_name, *decoded_json_text);
}

std::optional<std::array<uint8_t, 4>> load_chain_fork_id_hash_from_json_text(
    const std::string& chain_name,
    const std::string& json_text)
{
    boost::system::error_code ec;
    const boost::json::value parsed = boost::json::parse(json_text, ec);
    if (ec)
    {
        return std::nullopt;
    }
    return parse_chain_fork_id_hash_from_json_value(parsed, chain_name);
}

std::optional<std::array<uint8_t, 4>> load_chain_fork_id_hash_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path)
{
    std::ifstream input(json_path, std::ios::binary);
    if (!input)
    {
        return std::nullopt;
    }

    const std::string raw((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const auto decoded = decode_chain_peer_cache_body(raw);
    if (!decoded.has_value())
    {
        return std::nullopt;
    }

    return load_chain_fork_id_hash_from_json_text(chain_name, *decoded);
}

std::optional<ChainPeerConfig> load_chain_peer_config_from_json_text(
    const std::string& chain_name,
    const std::string& json_text)
{
    if (chain_name.empty())
    {
        return std::nullopt;
    }

    boost::system::error_code ec;
    const boost::json::value parsed = boost::json::parse(json_text, ec);
    if (ec)
    {
        return std::nullopt;
    }

    return parse_chain_peer_config_from_json_value(parsed, chain_name);
}

std::optional<ChainPeerConfig> load_chain_peer_config_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path)
{
    if (chain_name.empty())
    {
        return std::nullopt;
    }

    std::ifstream file(json_path, std::ios::binary);
    if (!file.is_open())
    {
        return std::nullopt;
    }

    const std::string json_text(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    const auto decoded_json_text = decode_chain_peer_cache_body(json_text);
    if (!decoded_json_text.has_value())
    {
        return std::nullopt;
    }

    return load_chain_peer_config_from_json_text(chain_name, *decoded_json_text);
}

ChainPeerCacheSignatureVerificationResult verify_chain_peer_cache_json_signature(
    const std::string& json_text,
    const std::string& expected_signer_address)
{
    boost::system::error_code ec;
    const boost::json::value parsed = boost::json::parse(json_text, ec);
    if (ec)
    {
        return {};
    }

    const auto result = verify_chain_peer_cache_json_signature_impl(parsed, expected_signer_address);
    if (!result.has_value())
    {
        return {};
    }

    return *result;
}

} // namespace discv4
