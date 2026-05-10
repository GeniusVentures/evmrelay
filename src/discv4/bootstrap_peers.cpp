// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <discv4/bootstrap_peers.hpp>
#include <discv5/discv5_enr.hpp>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <boost/json.hpp>
#include <zlib.h>

#include <array>
#include <chrono>
#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <openssl/err.h>
#include <openssl/ssl.h>

namespace
{

namespace http = boost::beast::http;

inline constexpr char kSchemeHttps[] = "https://";
inline constexpr char kSchemeHttp[] = "http://";
inline constexpr char kPathSeparator = '/';
inline constexpr char kPortSeparator = ':';
inline constexpr char kQuerySeparator = '?';
inline constexpr char kEnodeSeparator = '@';
inline constexpr uint8_t kHexBase = 16;
inline constexpr uint8_t kHexAlphaOffset = 10;
inline constexpr size_t kHexCharsPerByte = 2;
inline constexpr size_t kGzipMagicSize = 2;
inline constexpr unsigned char kGzipMagicByte0 = 0x1f;
inline constexpr unsigned char kGzipMagicByte1 = 0x8b;
inline constexpr int kGzipWindowBits = 16 + MAX_WBITS;
inline constexpr size_t kInflateChunkSize = 16384;
inline constexpr int kHttpVersion11 = 11;
inline constexpr int kHttpStatusOk = 200;
inline constexpr int kHttpStatusMultipleChoices = 300;
inline constexpr char kDefaultTarget[] = "/";
inline constexpr char kDefaultHttpsPort[] = "443";
inline constexpr char kDefaultHttpPort[] = "80";
inline constexpr char kEnodePrefix[] = "enode://";
inline constexpr char kBootstrapJsonFilename[] = "chain_enodes.json";
inline constexpr char kBootstrapJsonGzipFilename[] = "chain_enodes.json.gz";

struct HttpUrlParts
{
    bool        is_https = false;
    std::string host;
    std::string port;
    std::string target;
};

std::optional<uint8_t> hex_to_nibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return static_cast<uint8_t>(c - '0');
    }
    if (c >= 'a' && c <= 'f')
    {
        return static_cast<uint8_t>(kHexAlphaOffset + (c - 'a'));
    }
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<uint8_t>(kHexAlphaOffset + (c - 'A'));
    }
    return std::nullopt;
}

template <size_t N>
bool parse_hex_array(std::string_view hex, std::array<uint8_t, N>& out)
{
    if (hex.size() != N * kHexCharsPerByte)
    {
        return false;
    }

    for (size_t i = 0; i < N; ++i)
    {
        const size_t index = i * kHexCharsPerByte;
        const auto hi = hex_to_nibble(hex.at(index));
        const auto lo = hex_to_nibble(hex.at(index + 1));
        if (!hi || !lo)
        {
            return false;
        }
        out.at(i) = static_cast<uint8_t>(((*hi) << 4) | *lo);
    }
    return true;
}

std::optional<uint16_t> parse_uint16(std::string_view value)
{
    uint16_t out = 0;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), out);
    if (ec != std::errc{} || ptr != value.data() + value.size())
    {
        return std::nullopt;
    }
    return out;
}

std::optional<std::string> decode_bootstrap_body(std::string body)
{
    if (body.size() < kGzipMagicSize ||
        static_cast<unsigned char>(body[0]) != kGzipMagicByte0 ||
        static_cast<unsigned char>(body[1]) != kGzipMagicByte1)
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
        rest = url.substr(std::char_traits<char>::length(kSchemeHttps));
    }
    else if (url.rfind(kSchemeHttp, 0) == 0)
    {
        out.is_https = false;
        rest = url.substr(std::char_traits<char>::length(kSchemeHttp));
    }
    else
    {
        return std::nullopt;
    }

    const auto slash = rest.find(kPathSeparator);
    const auto host_port = (slash == std::string_view::npos) ? rest : rest.substr(0, slash);
    out.target = (slash == std::string_view::npos) ? kDefaultTarget : std::string(rest.substr(slash));
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
        out.port = out.is_https ? kDefaultHttpsPort : kDefaultHttpPort;
    }

    if (out.host.empty() || out.port.empty())
    {
        return std::nullopt;
    }
    return out;
}

std::vector<discv4::ValidatedPeer> parse_bootstrap_peers_from_json_value(
    const boost::json::value& parsed,
    const std::string&        chain_name)
{
    std::vector<discv4::ValidatedPeer> peers;

    const auto* root = parsed.if_object();
    if (root == nullptr)
    {
        return peers;
    }

    const auto* chain_entry = root->if_contains(chain_name);
    if (chain_entry == nullptr)
    {
        return peers;
    }

    const auto* arr = chain_entry->if_array();
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

        if (const auto* enode = obj->if_contains("enode"); enode != nullptr && enode->is_string())
        {
            const auto& enode_str = enode->as_string();
            if (auto vp = discv4::make_validated_peer_from_enode(std::string(enode_str.data(), enode_str.size())))
            {
                peers.push_back(std::move(*vp));
            }
            continue;
        }

        if (const auto* enr = obj->if_contains("enr"); enr != nullptr && enr->is_string())
        {
            const auto& enr_str = enr->as_string();
            if (auto vp = discv4::make_validated_peer_from_enr(std::string(enr_str.data(), enr_str.size())))
            {
                peers.push_back(std::move(*vp));
            }
        }
    }

    return peers;
}

[[nodiscard]] bool is_success_status(const int status_code) noexcept
{
    return status_code >= kHttpStatusOk && status_code < kHttpStatusMultipleChoices;
}

std::optional<std::filesystem::path> find_existing_bootstrap_json_path(
    const std::filesystem::path& directory)
{
    const std::filesystem::path json_file = directory / kBootstrapJsonFilename;
    if (std::filesystem::is_regular_file(json_file))
    {
        return json_file;
    }

    const std::filesystem::path gzip_file = directory / kBootstrapJsonGzipFilename;
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
    if (pubkey_hex.size() != rlpx::kPublicKeySize * kHexCharsPerByte)
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

    const auto port_value = parse_uint16(host_port.substr(colon_pos + 1));
    if (!port_value)
    {
        return std::nullopt;
    }

    NodeId node_id{};
    if (!parse_hex_array(pubkey_hex, node_id))
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


std::filesystem::path bootstrap_cache_json_path(const std::string& argv0)
{
    return std::filesystem::path(argv0).parent_path() / kBootstrapJsonFilename;
}

std::optional<std::filesystem::path> find_bootstrap_peers_json_path(
    const std::string& argv0,
    const std::string& override_path)
{
    if (!override_path.empty())
    {
        const std::filesystem::path override_file(override_path);
        if (std::filesystem::is_regular_file(override_file))
        {
            return override_file;
        }
        return std::nullopt;
    }

    const std::filesystem::path bin_dir = std::filesystem::path(argv0).parent_path();
    if (const auto local_file = find_existing_bootstrap_json_path(bin_dir); local_file.has_value())
    {
        return local_file;
    }

    if (bin_dir.filename() == "MacOS" && bin_dir.parent_path().filename() == "Contents")
    {
        const auto resources_dir = bin_dir.parent_path() / "Resources";
        if (const auto resources_file = find_existing_bootstrap_json_path(resources_dir); resources_file.has_value())
        {
            return resources_file;
        }
    }

    return std::nullopt;
}

std::optional<std::string> download_bootstrap_json(const std::string& url)
{
    const auto url_parts = parse_http_url(url);
    if (!url_parts)
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
        if (ec || !is_success_status(res.result_int()))
        {
            return std::nullopt;
        }

        stream.shutdown(ec);
        return decode_bootstrap_body(std::move(res.body()));
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
    if (ec || !is_success_status(res.result_int()))
    {
        return std::nullopt;
    }

    stream.socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
    return decode_bootstrap_body(std::move(res.body()));
}

bool write_bootstrap_cache_json_if_changed(
    const std::filesystem::path& json_path,
    const std::string&           json_text)
{
    std::ifstream existing_file(json_path, std::ios::binary);
    if (existing_file.is_open())
    {
        const std::string existing_json((std::istreambuf_iterator<char>(existing_file)),
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

std::optional<BootstrapCacheRefreshResult> refresh_bootstrap_cache_json(
    const std::filesystem::path& json_path,
    const std::string&           url)
{
    const auto json_text = download_bootstrap_json(url);
    if (!json_text)
    {
        return std::nullopt;
    }

    BootstrapCacheRefreshResult result{};
    result.cache_path = json_path;
    result.cache_updated = write_bootstrap_cache_json_if_changed(json_path, *json_text);
    result.cache_available = result.cache_updated || std::filesystem::is_regular_file(json_path);
    return result;
}

std::vector<ValidatedPeer> load_bootstrap_peers_from_json_text(
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

    return parse_bootstrap_peers_from_json_value(parsed, chain_name);
}

std::vector<ValidatedPeer> load_bootstrap_peers_from_json(
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

    const std::string json_text((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    const auto decoded_json_text = decode_bootstrap_body(json_text);
    if (!decoded_json_text)
    {
        return peers;
    }

    return load_bootstrap_peers_from_json_text(chain_name, *decoded_json_text);
}

} // namespace discv4

