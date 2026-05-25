// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_RPC_MANAGER_HPP
#define EVMRELAY_INCLUDE_ETH_RPC_MANAGER_HPP

#include <eth/finality_policy.hpp>
#include <eth/rpc_manager_config.hpp>
#include <boost/outcome/result.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eth::rpc {

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

enum class RpcEndpointState
{
    kAvailable,
    kTemporarilyFailed,
    kDisabled
};

enum class RpcEndpointErrorCode
{
    kMissingApiKey,
    kInvalidTemplate
};

struct RpcEndpointError
{
    RpcEndpointErrorCode code = RpcEndpointErrorCode::kInvalidTemplate;
    std::string          detail;
};

template <typename T>
using RpcResult = outcome::result<T, RpcEndpointError, outcome::policy::all_narrow>;

struct RpcEndpoint
{
    std::string chain_name;
    uint64_t    chain_id = 0;
    std::string url;
    uint32_t    priority = 0;
    uint32_t    weight = 0;
    uint32_t    rate_limit_per_second = 0;
    bool        is_paid = false;
    bool        is_public = true;
    bool        verified = false;
    RpcEndpointState state = RpcEndpointState::kAvailable;
};

struct RpcEndpointGroup
{
    std::string            chain_name;
    uint64_t               chain_id = 0;
    std::vector<RpcEndpoint> endpoints;
};

using RpcEnvLookup = std::function<std::optional<std::string>(std::string_view)>;

[[nodiscard]] const char* to_string(RpcEndpointErrorCode code) noexcept;
[[nodiscard]] const char* to_string(RpcEndpointState state) noexcept;

/// @brief Resolve a URL template into a concrete endpoint URL.
/// @details The only supported placeholder is `{key}`.
[[nodiscard]] RpcResult<std::string> render_rpc_endpoint_url(
    const RpcEndpointConfig& config,
    RpcEnvLookup             env_lookup = {});

/// @brief Materialize a runtime endpoint from configuration.
[[nodiscard]] RpcResult<RpcEndpoint> build_rpc_endpoint(
    const RpcEndpointConfig& config,
    RpcEnvLookup             env_lookup = {});

/// @brief Group endpoints by chain name and chain id with deterministic ordering.
[[nodiscard]] std::vector<RpcEndpointGroup> group_rpc_endpoints(
    const RpcManagerConfig& config,
    RpcEnvLookup            env_lookup = {});

class RpcEndpointPool
{
public:
    RpcEndpointPool() = default;
    explicit RpcEndpointPool(std::vector<RpcEndpoint> endpoints);

    [[nodiscard]] const std::vector<RpcEndpoint>& endpoints() const noexcept
    {
        return endpoints_;
    }

    [[nodiscard]] std::vector<RpcEndpoint>& endpoints() noexcept
    {
        return endpoints_;
    }

    [[nodiscard]] std::optional<std::reference_wrapper<RpcEndpoint>> next_endpoint();
    [[nodiscard]] std::optional<std::reference_wrapper<const RpcEndpoint>> next_endpoint() const;

    void mark_temporary_failure(std::string_view url);
    void disable(std::string_view url);
    void reset_temporary_failures();

private:
    [[nodiscard]] static bool is_usable(const RpcEndpoint& endpoint) noexcept;

    std::vector<RpcEndpoint> endpoints_;
};

class RpcManager
{
public:
    explicit RpcManager(
        RpcManagerConfig config,
        RpcEnvLookup     env_lookup = {});

    [[nodiscard]] const std::vector<RpcEndpointGroup>& endpoint_groups() const noexcept
    {
        return groups_;
    }

    [[nodiscard]] std::optional<std::reference_wrapper<RpcEndpointPool>> pool(
        std::string_view chain_name,
        uint64_t         chain_id);
    [[nodiscard]] std::optional<std::reference_wrapper<const RpcEndpointPool>> pool(
        std::string_view chain_name,
        uint64_t         chain_id) const;

private:
    struct PoolEntry
    {
        RpcEndpointGroup group;
        RpcEndpointPool  pool;
    };

    [[nodiscard]] static std::string make_chain_key(std::string_view chain_name, uint64_t chain_id);

    std::vector<RpcEndpointGroup> groups_;
    std::vector<PoolEntry>        pools_;
};

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_RPC_MANAGER_HPP
