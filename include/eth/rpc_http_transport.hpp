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
    std::chrono::seconds timeout{30};
    bool                 verify_peer = true;
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
