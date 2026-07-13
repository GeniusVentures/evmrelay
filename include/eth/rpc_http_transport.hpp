// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_RPC_HTTP_TRANSPORT_HPP
#define EVMRELAY_INCLUDE_ETH_RPC_HTTP_TRANSPORT_HPP

#include <eth/rpc_receipt_source.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace eth::rpc {

struct RpcHttpTransportOptions
{
    /// @brief Default retry attempt count for transient failures (D-23).
    static constexpr unsigned int kDefaultRetryCount     = 3u;
    /// @brief Default base backoff delay in ms; doubled each attempt (D-23).
    static constexpr unsigned int kDefaultRetryBackoffMs = 1000u;
    /// @brief Cap (ms) for the doubled backoff so high retry_count cannot stall (D-23).
    static constexpr unsigned int kMaxRetryBackoffMs     = 8000u;

    std::chrono::seconds timeout{30};
    bool                 verify_peer = true;
    /// @brief Max retry attempts on transient failure (0 = no retry, D-23).
    unsigned int              retry_count = kDefaultRetryCount;
    /// @brief Base backoff delay; doubled each attempt (D-23).
    std::chrono::milliseconds retry_backoff{kDefaultRetryBackoffMs};
};

class RpcHttpTransport final : public JsonRpcTransport
{
public:
    explicit RpcHttpTransport(
        std::string              endpoint_url,
        RpcHttpTransportOptions   options = {});

    [[nodiscard]] std::optional<std::string> call(const boost::json::object& request) override;

    /// @brief Perform a one-shot HTTPS GET and return the response body.
    /// @param url     Fully-qualified https:// URL (host[:port]/target).
    /// @param options Transport options (timeout, TLS verify).
    /// @return Response body on HTTP 2xx, or std::nullopt on any failure.
    /// @note   Single-attempt; instance call() retries on transient failure per D-23.
    ///         Callers needing retry should construct an RpcHttpTransport and call().
    [[nodiscard]] static std::optional<std::string> HttpsGet(
        const std::string&             url,
        const RpcHttpTransportOptions& options = {});

    [[nodiscard]] std::string_view endpoint_url() const noexcept
    {
        return endpoint_url_;
    }

private:
    struct ParsedUrl
    {
        std::string scheme;
        std::string host;
        std::string port;
        std::string target;
        bool        is_https = false;
    };

    [[nodiscard]] static std::optional<ParsedUrl> parse_url(std::string_view endpoint_url);

    std::string             endpoint_url_;
    RpcHttpTransportOptions  options_;
};

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_RPC_HTTP_TRANSPORT_HPP
