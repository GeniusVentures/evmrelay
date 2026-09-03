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

// Beast's expires_after() only governs async operations, so the former sync
// connect/handshake/write/read chain could block a caller forever on a stalled
// peer (io_context threads then never joined at shutdown). One deadline timer
// bounds the whole exchange, including DNS resolution.
template <typename Stream, typename Handshake>
[[nodiscard]] std::optional<std::string> exchange(
    asio::io_context&                io,
    Stream&                          stream,
    const std::string&               host,
    const std::string&               port,
    http::request<http::string_body> req,
    std::chrono::seconds             timeout,
    Handshake                        handshake)
{
    tcp::resolver                     resolver(io);
    asio::steady_timer                deadline(io);
    beast::flat_buffer                buffer;
    http::response<http::string_body> res;
    std::optional<std::string>        result;

    deadline.expires_after(timeout);
    deadline.async_wait([&](boost::system::error_code ec)
    {
        if (!ec)
        {
            resolver.cancel();
            beast::get_lowest_layer(stream).close();
        }
    });
    auto done = [&]() { deadline.cancel(); };

    resolver.async_resolve(host, port, [&](boost::system::error_code ec, tcp::resolver::results_type results)
    {
        if (ec) { return done(); }
        beast::get_lowest_layer(stream).async_connect(results, [&](boost::system::error_code ec, auto)
        {
            if (ec) { return done(); }
            handshake([&](boost::system::error_code ec)
            {
                if (ec) { return done(); }
                http::async_write(stream, req, [&](boost::system::error_code ec, std::size_t)
                {
                    if (ec) { return done(); }
                    http::async_read(stream, buffer, res, [&](boost::system::error_code ec, std::size_t)
                    {
                        if (!ec)
                        {
                            result = read_body_from_response(res);
                        }
                        done();
                    });
                });
            });
        });
    });

    io.run();
    return result;
}

[[nodiscard]] http::request<http::string_body> make_request(
    http::verb         verb,
    const std::string& host,
    const std::string& target,
    const std::string& body)
{
    http::request<http::string_body> req{verb, target, kHttpVersion};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (verb != http::verb::get && verb != http::verb::head)
    {
        req.set(http::field::content_type, "application/json");
        req.body() = body;
    }
    req.prepare_payload();
    return req;
}

[[nodiscard]] std::optional<std::string> read_https_response(
    http::verb                    verb,
    const std::string&            host,
    const std::string&            port,
    const std::string&            target,
    const std::string&            body,
    const RpcHttpTransportOptions& options)
{
    asio::io_context io;
    boost::system::error_code ec;

    ssl::context ssl_ctx(ssl::context::tls_client);

    ssl_ctx.set_default_verify_paths(ec);

    {
        const char* env_cert_file = std::getenv("SSL_CERT_FILE");
        if (env_cert_file != nullptr && env_cert_file[0] != '\0')
        {
            boost::system::error_code load_ec;
            ssl_ctx.load_verify_file(env_cert_file, load_ec);
        }
    }

    {
        static const char* kFallbackCaPaths[] = {
            "/etc/ssl/cert.pem",
            "/opt/homebrew/etc/openssl@3/cert.pem",
            "/opt/homebrew/etc/ca-certificates/cert.pem",
            "/usr/local/etc/openssl@3/cert.pem",
            "/usr/local/etc/openssl/cert.pem",
            "/etc/ssl/certs/ca-certificates.crt",
        };
        for (const auto* ca_path : kFallbackCaPaths)
        {
            boost::system::error_code load_ec;
            ssl_ctx.load_verify_file(ca_path, load_ec);
        }
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

    return exchange(io, stream, host, port, make_request(verb, host, target, body), options.timeout,
                    [&](auto&& on_done) { stream.async_handshake(ssl::stream_base::client, on_done); });
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
        return read_https_response(
            http::verb::post,
            parsed->host,
            parsed->port,
            parsed->target,
            body,
            options_);
    }

    asio::io_context  io;
    beast::tcp_stream stream(io);
    return exchange(io, stream, parsed->host, parsed->port,
                    make_request(http::verb::post, parsed->host, parsed->target, body), options_.timeout,
                    [](auto&& on_done) { on_done(boost::system::error_code{}); });
}

std::optional<std::string> RpcHttpTransport::HttpsGet(
    const std::string&            url,
    const RpcHttpTransportOptions& options)
{
    const auto parsed = parse_url(url);
    if ( !parsed.has_value() || !parsed->is_https )
    {
        return std::nullopt;
    }

    return read_https_response(
        http::verb::get,
        parsed->host,
        parsed->port,
        parsed->target,
        {},
        options );
}

} // namespace eth::rpc
