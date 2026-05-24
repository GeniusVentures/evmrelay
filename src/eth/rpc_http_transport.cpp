// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_http_transport.hpp>

#include <boost/json/serialize.hpp>
#include <boost/asio/ssl.hpp>
#include <openssl/ssl.h>

#include <cctype>
#include <utility>

namespace eth::rpc {

namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

constexpr auto kHttpVersion = 11;

[[nodiscard]] std::optional<std::pair<std::string, std::string>> split_host_port(
    std::string_view authority,
    bool             is_https)
{
    const auto colon = authority.rfind(':');
    if (colon == std::string_view::npos)
    {
        return std::pair<std::string, std::string>{
            std::string(authority),
            is_https ? "443" : "80"};
    }

    const auto host = authority.substr(0, colon);
    const auto port = authority.substr(colon + 1);
    if (host.empty() || port.empty())
    {
        return std::nullopt;
    }
    return std::pair<std::string, std::string>{std::string(host), std::string(port)};
}

[[nodiscard]] std::optional<std::string> read_body_from_response(const http::response<http::string_body>& res)
{
    if (res.result_int() < 200 || res.result_int() >= 300)
    {
        return std::nullopt;
    }
    return res.body();
}

[[nodiscard]] std::optional<std::string> read_https_response(
    const std::string&            host,
    const std::string&            port,
    const std::string&            target,
    const std::string&            body,
    const RpcHttpTransportOptions& options,
    boost::system::error_code&    ec)
{
    asio::io_context io;
    beast::flat_buffer buffer;
    tcp::resolver resolver(io);
    const auto results = resolver.resolve(host, port, ec);
    if (ec)
    {
        return std::nullopt;
    }

    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.set_default_verify_paths(ec);
    if (ec)
    {
        return std::nullopt;
    }

    ssl::stream<beast::tcp_stream> stream(io, ssl_ctx);
    if (!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str()))
    {
        return std::nullopt;
    }

    stream.set_verify_mode(options.verify_peer ? ssl::verify_peer : ssl::verify_none);
    if (options.verify_peer)
    {
        stream.set_verify_callback(ssl::rfc2818_verification(host));
    }

    beast::get_lowest_layer(stream).expires_after(options.timeout);
    beast::get_lowest_layer(stream).connect(results, ec);
    if (ec)
    {
        return std::nullopt;
    }

    stream.handshake(ssl::stream_base::client, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::request<http::string_body> req{http::verb::post, target, kHttpVersion};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    http::write(stream, req, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::response<http::string_body> res;
    http::read(stream, buffer, res, ec);
    if (ec)
    {
        return std::nullopt;
    }

    const auto response_body = read_body_from_response(res);
    if (!response_body.has_value())
    {
        return std::nullopt;
    }

    stream.shutdown(ec);
    return response_body;
}

} // namespace

RpcHttpTransport::RpcHttpTransport(
    std::string            endpoint_url,
    RpcHttpTransportOptions options)
    : endpoint_url_(std::move(endpoint_url))
    , options_(options)
{
}

std::optional<RpcHttpTransport::ParsedUrl> RpcHttpTransport::parse_url(std::string_view endpoint_url)
{
    const auto scheme_end = endpoint_url.find("://");
    if (scheme_end == std::string_view::npos)
    {
        return std::nullopt;
    }

    ParsedUrl parsed;
    parsed.scheme = std::string(endpoint_url.substr(0, scheme_end));
    parsed.is_https = parsed.scheme == "https";
    if (!parsed.is_https && parsed.scheme != "http")
    {
        return std::nullopt;
    }

    const auto authority_begin = scheme_end + 3;
    const auto path_begin = endpoint_url.find('/', authority_begin);
    const auto authority = endpoint_url.substr(
        authority_begin,
        path_begin == std::string_view::npos ? std::string_view::npos : path_begin - authority_begin);
    if (authority.empty())
    {
        return std::nullopt;
    }

    const auto host_port = split_host_port(authority, parsed.is_https);
    if (!host_port.has_value())
    {
        return std::nullopt;
    }
    parsed.host = std::move(host_port->first);
    parsed.port = std::move(host_port->second);
    parsed.target = path_begin == std::string_view::npos ? "/" : std::string(endpoint_url.substr(path_begin));
    if (parsed.target.empty())
    {
        parsed.target = "/";
    }
    return parsed;
}

std::optional<std::string> RpcHttpTransport::call(const boost::json::object& request)
{
    const auto parsed = parse_url(endpoint_url_);
    if (!parsed.has_value())
    {
        return std::nullopt;
    }

    const auto body = boost::json::serialize(request);

    if (parsed->is_https)
    {
        boost::system::error_code ec;
        return read_https_response(
            parsed->host,
            parsed->port,
            parsed->target,
            body,
            options_,
            ec);
    }

    asio::io_context io;
    beast::flat_buffer buffer;
    boost::system::error_code ec;

    tcp::resolver resolver(io);
    const auto results = resolver.resolve(parsed->host, parsed->port, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::request<http::string_body> req{http::verb::post, parsed->target, kHttpVersion};
    req.set(http::field::host, parsed->host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    beast::tcp_stream stream(io);
    stream.expires_after(options_.timeout);
    stream.connect(results, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::write(stream, req, ec);
    if (ec)
    {
        return std::nullopt;
    }

    http::response<http::string_body> res;
    http::read(stream, buffer, res, ec);
    if (ec)
    {
        return std::nullopt;
    }

    const auto response_body = read_body_from_response(res);
    if (!response_body.has_value())
    {
        return std::nullopt;
    }

    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    return response_body;
}

} // namespace eth::rpc
