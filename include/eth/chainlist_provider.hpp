// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_CHAINLIST_PROVIDER_HPP
#define EVMRELAY_INCLUDE_ETH_CHAINLIST_PROVIDER_HPP

#include <eth/rpc_manager_config.hpp>
#include <base/json_utility.hpp>

#include <cstdint>
#include <string_view>
#include <vector>

namespace eth::rpc {

/// @brief Parse chainid.network chains.json into normalized RPC endpoint configs.
///
/// Filters deprecated chains, wss:// URLs, and API-key placeholder URLs.
/// Deduplicates by chainId + URL.
///
/// @param json_text  Raw chains.json array text.
/// @return Vector of RpcEndpointConfig, or JSON parse error.
[[nodiscard]] rlp::base::json::JsonResult<std::vector<RpcEndpointConfig>> load_chainlist_from_json_text(
    std::string_view json_text);

/// @brief Filter endpoint configs to only those matching configured chain IDs.
///
/// @param endpoints          Endpoints to filter (consumed by move).
/// @param configured_chain_ids  Set of chain IDs that are configured/provisioned.
/// @return Filtered endpoints (only configured chains remain).
[[nodiscard]] std::vector<RpcEndpointConfig> filter_to_configured_chains(
    std::vector<RpcEndpointConfig>        endpoints,
    const std::vector<uint64_t>&          configured_chain_ids);

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_CHAINLIST_PROVIDER_HPP
