// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_probe.hpp>

#include <eth/json_rpc.hpp>
#include <eth/rpc_http_transport.hpp>
#include <base/rlp-logger.hpp>

namespace eth::rpc {

const char* to_string(ProbeStatus status) noexcept
{
    switch (status)
    {
        case ProbeStatus::kSuccess: return "success";
        case ProbeStatus::kChainIdMismatch: return "chain_id_mismatch";
        case ProbeStatus::kTransportError: return "transport_error";
        case ProbeStatus::kParseError: return "parse_error";
    }
    return "transport_error";
}

ProbeResult probe_endpoint_chain_id(
    std::string_view url,
    uint64_t         expected_chain_id,
    std::chrono::seconds timeout)
{
    ProbeResult result;
    result.url = url;
    result.expected_chain_id = expected_chain_id;

    RpcHttpTransportOptions opts;
    opts.timeout = timeout;
    RpcHttpTransport transport(std::string(url), opts);

    const auto request = make_eth_chain_id_request(1);
    const auto response = transport.call(request);
    if (!response.has_value())
    {
        result.status = ProbeStatus::kTransportError;
        result.detail = "HTTP transport call failed";
        return result;
    }

    const auto chain_id = parse_chain_id_response(response.value());
    if (!chain_id.has_value())
    {
        result.status = ProbeStatus::kParseError;
        result.detail = "Failed to parse chain_id response";
        return result;
    }

    if (chain_id.value() != expected_chain_id)
    {
        result.status = ProbeStatus::kChainIdMismatch;
        result.detail = "Expected chain_id " + std::to_string(expected_chain_id)
                        + ", got " + std::to_string(chain_id.value());
        return result;
    }

    result.status = ProbeStatus::kSuccess;
    return result;
}

std::vector<ProbeResult> probe_endpoint_pool(
    RpcEndpointPool&   pool,
    uint64_t           expected_chain_id,
    std::chrono::seconds timeout)
{
    std::vector<ProbeResult> results;
    results.reserve(pool.endpoints().size());

    auto logger = rlp::base::createLogger("rpc_probe");

    for (auto& endpoint : pool.endpoints())
    {
        auto result = probe_endpoint_chain_id(
            endpoint.url,
            expected_chain_id,
            timeout);

        switch (result.status)
        {
            case ProbeStatus::kSuccess:
                endpoint.verified = true;
                logger->info("Probe succeeded: {} (chain_id={})",
                             endpoint.url, expected_chain_id);
                break;
            case ProbeStatus::kChainIdMismatch:
                pool.mark_temporary_failure(endpoint.url);
                logger->warn("Probe mismatch: {} — {}", endpoint.url, result.detail);
                break;
            default:
                pool.mark_temporary_failure(endpoint.url);
                logger->warn("Probe failed: {} — {}", endpoint.url, result.detail);
                break;
        }
        results.push_back(std::move(result));
    }

    return results;
}

} // namespace eth::rpc
