// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_RPC_MANAGER_CONFIG_HPP
#define EVMRELAY_INCLUDE_ETH_RPC_MANAGER_CONFIG_HPP

#include <base/json_utility.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace eth::rpc {

/// @brief One configured RPC endpoint for a chain.
struct RpcEndpointConfig
{
    std::string chain_name;
    uint64_t    chain_id = 0;
    std::string url_template;
    std::optional<std::string> api_key_env_var;
    std::optional<std::string> api_key_literal;
    uint32_t    priority = 0;
    uint32_t    weight = 0;
    uint32_t    rate_limit_per_second = 0;
    bool        is_paid = false;
    bool        is_public = true;
    bool        verified = false;
};

/// @brief Top-level RPC manager configuration.
struct RpcManagerConfig
{
    size_t                        max_endpoints_per_chain = 0;
    std::vector<RpcEndpointConfig> endpoints;
};

/// @brief Load RPC manager configuration from JSON text.
/// @param json_text JSON document contents.
/// @return Parsed configuration, or JSON field error.
[[nodiscard]] rlp::base::json::JsonResult<RpcManagerConfig> load_rpc_manager_config_result_from_json_text(
    const std::string& json_text);

/// @brief Load RPC manager configuration from a JSON file.
/// @param json_path JSON file path.
/// @return Parsed configuration, or JSON field error.
[[nodiscard]] rlp::base::json::JsonResult<RpcManagerConfig> load_rpc_manager_config_result_from_json(
    const std::filesystem::path& json_path);

/// @brief Load RPC manager configuration from JSON text.
/// @param json_text JSON document contents.
/// @return Parsed configuration, or `std::nullopt` when missing or invalid.
[[nodiscard]] std::optional<RpcManagerConfig> load_rpc_manager_config_from_json_text(
    const std::string& json_text);

/// @brief Load RPC manager configuration from a JSON file.
/// @param json_path JSON file path.
/// @return Parsed configuration, or `std::nullopt` when missing or invalid.
[[nodiscard]] std::optional<RpcManagerConfig> load_rpc_manager_config_from_json(
    const std::filesystem::path& json_path);

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_RPC_MANAGER_CONFIG_HPP
