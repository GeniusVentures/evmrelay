// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_http_transport.hpp>

#include <boost/json/serialize.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/thread.hpp>
#include <openssl/ssl.h>
#include <base/rlp-logger.hpp>

#include <algorithm>
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

// ── HTTP status classification thresholds (no magic numbers in bodies) ──────
constexpr int kHttpSuccessStart      = 200;  ///< First 2xx success status.
constexpr int kHttpSuccessEnd        = 300;  ///< Exclusive upper bound for 2xx.
constexpr int kHttpClientErrorStart  = 400;  ///< First 4xx client error (permanent).
constexpr int kHttpServerErrorStart  = 500;  ///< First 5xx server error (transient).
constexpr int kHttpUnknownStatusMax  = 600;  ///< Exclusive upper bound for valid status codes.

/**
 * @brief Classifies an HTTP response outcome for retry decisions (D-23).
 *
 * @param[in] status_code  HTTP response status code (res.result_int()).
 *
 * @return kSuccess for 2xx/3xx, kPermanentFailure for 4xx (auth/bad-request — never
 *         succeeds on retry), kTransientFailure for 5xx and 0/unknown.
 */
enum class CallOutcome
{
    kSuccess,          ///< 2xx/3xx response — return body.
    kTransientFailure, ///< Connection ec, timeout, or 5xx — retry with backoff.
    kPermanentFailure  ///< 4xx (auth/bad-request) — bail immediately, no retry.
};

[[nodiscard]] CallOutcome classify_http_status( int status_code )
{
    if ( status_code >= kHttpSuccessStart && status_code < kHttpSuccessEnd )
    {
        return CallOutcome::kSuccess;
    }
    if ( status_code >= kHttpClientErrorStart && status_code < kHttpServerErrorStart )
    {
        return CallOutcome::kPermanentFailure;
    }
    if ( status_code >= kHttpServerErrorStart && status_code < kHttpUnknownStatusMax )
    {
        return CallOutcome::kTransientFailure;
    }
    // 3xx redirects, 1xx informational, or 0/unknown — treat as transient
    // (redirects are not expected for RPC endpoints; unknown deserves a retry).
    return CallOutcome::kTransientFailure;
}

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

[[nodiscard]] std::pair<std::optional<std::string>, CallOutcome> read_body_from_response(
    const http::response<http::string_body>& res)
{
    const auto status = res.result_int();
    if ( status >= kHttpSuccessStart && status < kHttpSuccessEnd )
    {
        return { res.body(), CallOutcome::kSuccess };
    }
    // Non-2xx: classify via HTTP status so callers can decide retry vs bail (D-23).
    return { std::nullopt, classify_http_status( status ) };
}

[[nodiscard]] std::pair<std::optional<std::string>, CallOutcome> read_https_response(
    http::verb                    verb,
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
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

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
        return { std::nullopt, CallOutcome::kTransientFailure };
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
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    stream.handshake(ssl::stream_base::client, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    http::request<http::string_body> req{verb, target, kHttpVersion};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (verb != http::verb::get && verb != http::verb::head)
    {
        req.set(http::field::content_type, "application/json");
        req.body() = body;
    }
    req.prepare_payload();

    http::write(stream, req, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    http::response<http::string_body> res;
    http::read(stream, buffer, res, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    auto [response_body, outcome] = read_body_from_response(res);
    if (!response_body.has_value())
    {
        stream.shutdown(ec);
        return { std::nullopt, outcome };
    }

    stream.shutdown(ec);
    return { response_body, CallOutcome::kSuccess };
}

/**
 * @brief Performs a single HTTP (non-TLS) RPC attempt with classified outcome (D-23).
 *
 * @param[in] verb    HTTP verb (POST for RPC calls).
 * @param[in] host    Resolved host name.
 * @param[in] port    Resolved port string.
 * @param[in] target  Request target (path + query).
 * @param[in] body    Serialized JSON-RPC request body.
 * @param[in] options Transport options (timeout, TLS verify — verify_peer ignored for plain HTTP).
 * @return Pair of {response body or nullopt, CallOutcome} so the caller can decide retry.
 */
[[nodiscard]] std::pair<std::optional<std::string>, CallOutcome> attempt_http(
    http::verb                     verb,
    const std::string&             host,
    const std::string&             port,
    const std::string&             target,
    const std::string&             body,
    const RpcHttpTransportOptions& options)
{
    asio::io_context    io;
    beast::flat_buffer  buffer;
    boost::system::error_code ec;

    tcp::resolver resolver(io);
    const auto results = resolver.resolve(host, port, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    http::request<http::string_body> req{ verb, target, kHttpVersion };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();

    beast::tcp_stream stream(io);
    stream.expires_after(options.timeout);
    stream.connect(results, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    http::write(stream, req, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    http::response<http::string_body> res;
    http::read(stream, buffer, res, ec);
    if (ec)
    {
        return { std::nullopt, CallOutcome::kTransientFailure };
    }

    auto [response_body, outcome] = read_body_from_response(res);
    stream.socket().shutdown(tcp::socket::shutdown_both, ec);
    if (!response_body.has_value())
    {
        return { std::nullopt, outcome };
    }
    return { response_body, CallOutcome::kSuccess };
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

    // Retry loop with exponential backoff on transient failures (D-23 Approach A).
    // Permanent failures (4xx) bail immediately; transient failures (connection ec,
    // timeout ec, 5xx) retry up to options_.retry_count times.
    auto logger = rlp::base::createLogger( "rpc_http_transport" );
    for ( unsigned int attempt = 0u; attempt <= options_.retry_count; ++attempt )
    {
        std::pair<std::optional<std::string>, CallOutcome> result;
        if ( parsed->is_https )
        {
            boost::system::error_code ec;
            result = read_https_response(
                http::verb::post,
                parsed->host,
                parsed->port,
                parsed->target,
                body,
                options_,
                ec );
        }
        else
        {
            result = attempt_http(
                http::verb::post,
                parsed->host,
                parsed->port,
                parsed->target,
                body,
                options_ );
        }

        if ( result.first.has_value() )
        {
            return result.first;
        }

        if ( result.second == CallOutcome::kPermanentFailure )
        {
            // 4xx (auth/bad-request) — retrying cannot help, bail immediately.
            return std::nullopt;
        }

        if ( attempt < options_.retry_count )
        {
            // Transient failure: backoff = min(base * 2^attempt, cap) (D-23).
            const auto raw_backoff = options_.retry_backoff.count() * ( 1ull << attempt );
            const auto capped_backoff = std::min(
                raw_backoff,
                static_cast<unsigned long long>( RpcHttpTransportOptions::kMaxRetryBackoffMs ) );
            logger->debug( "RpcHttpTransport::call transient failure on attempt {}/{} — "
                           "retrying in {} ms",
                           attempt + 1u,
                           options_.retry_count,
                           capped_backoff );
            boost::this_thread::sleep_for( boost::chrono::milliseconds( capped_backoff ) );
        }
    }

    return std::nullopt;
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

    boost::system::error_code ec;
    const auto [response_body, outcome] = read_https_response(
        http::verb::get,
        parsed->host,
        parsed->port,
        parsed->target,
        {},
        options,
        ec );
    (void)outcome; // HttpsGet is single-attempt (Option A, D-23) — caller handles failure.
    return response_body;
}

} // namespace eth::rpc
