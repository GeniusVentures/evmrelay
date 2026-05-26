// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_RPC_PROBE_HPP
#define EVMRELAY_INCLUDE_ETH_RPC_PROBE_HPP

#include <eth/rpc_manager.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eth::rpc {

enum class ProbeStatus
{
    kSuccess,
    kChainIdMismatch,
    kTransportError,
    kParseError
};

struct ProbeResult
{
    std::string url;
    uint64_t    expected_chain_id = 0;
    ProbeStatus status = ProbeStatus::kTransportError;
    std::string detail;
};

[[nodiscard]] const char* to_string(ProbeStatus status) noexcept;

[[nodiscard]] ProbeResult probe_endpoint_chain_id(
    std::string_view url,
    uint64_t         expected_chain_id,
    std::chrono::seconds timeout = std::chrono::seconds(10));

[[nodiscard]] std::vector<ProbeResult> probe_endpoint_pool(
    RpcEndpointPool& pool,
    uint64_t         expected_chain_id,
    std::chrono::seconds timeout = std::chrono::seconds(10));

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_RPC_PROBE_HPP
