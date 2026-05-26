// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_manager.hpp>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <utility>

namespace eth::rpc {

namespace {

[[nodiscard]] std::optional<std::string> default_env_lookup(std::string_view name)
{
    const std::string key{name};
    const char* value = std::getenv(key.c_str());
    if (value == nullptr)
    {
        return std::nullopt;
    }
    return std::string(value);
}

[[nodiscard]] bool contains_key_placeholder(std::string_view template_text)
{
    return template_text.find("{key}") != std::string_view::npos;
}

[[nodiscard]] std::string replace_all_key_placeholders(
    std::string template_text,
    std::string_view key)
{
    const std::string placeholder{"{key}"};
    std::string::size_type pos = 0;
    while ((pos = template_text.find(placeholder, pos)) != std::string::npos)
    {
        template_text.replace(pos, placeholder.size(), key);
        pos += key.size();
    }
    return template_text;
}

[[nodiscard]] bool endpoint_less(const RpcEndpoint& lhs, const RpcEndpoint& rhs) noexcept
{
    if (lhs.priority != rhs.priority)
    {
        return lhs.priority < rhs.priority;
    }
    if (lhs.weight != rhs.weight)
    {
        return lhs.weight > rhs.weight;
    }
    if (lhs.chain_name != rhs.chain_name)
    {
        return lhs.chain_name < rhs.chain_name;
    }
    if (lhs.chain_id != rhs.chain_id)
    {
        return lhs.chain_id < rhs.chain_id;
    }
    return lhs.url < rhs.url;
}

[[nodiscard]] std::string chain_key(std::string_view chain_name, uint64_t chain_id)
{
    return std::string(chain_name) + "#" + std::to_string(chain_id);
}

} // namespace

const char* to_string(RpcEndpointErrorCode code) noexcept
{
    switch (code)
    {
        case RpcEndpointErrorCode::kMissingApiKey: return "missing_api_key";
        case RpcEndpointErrorCode::kInvalidTemplate: return "invalid_template";
    }
    return "invalid_template";
}

const char* to_string(RpcEndpointState state) noexcept
{
    switch (state)
    {
        case RpcEndpointState::kAvailable: return "available";
        case RpcEndpointState::kTemporarilyFailed: return "temporarily_failed";
        case RpcEndpointState::kDisabled: return "disabled";
    }
    return "available";
}

RpcResult<std::string> render_rpc_endpoint_url(
    const RpcEndpointConfig& config,
    RpcEnvLookup             env_lookup)
{
    std::optional<std::string> key;
    if (config.api_key_literal.has_value())
    {
        key = config.api_key_literal;
    }
    else if (config.api_key_env_var.has_value())
    {
        if (env_lookup)
        {
            key = env_lookup(*config.api_key_env_var);
        }
        else
        {
            key = default_env_lookup(*config.api_key_env_var);
        }
    }

    const bool requires_key = contains_key_placeholder(config.url_template);
    if (!requires_key)
    {
        return config.url_template;
    }

    if (!key.has_value())
    {
        return outcome::failure(RpcEndpointError{
            RpcEndpointErrorCode::kMissingApiKey,
            config.api_key_literal.has_value()
                ? "apiKeyLiteral"
                : (config.api_key_env_var.has_value() ? *config.api_key_env_var : "urlTemplate")});
    }

    return replace_all_key_placeholders(config.url_template, *key);
}

RpcResult<RpcEndpoint> build_rpc_endpoint(
    const RpcEndpointConfig& config,
    RpcEnvLookup             env_lookup)
{
    const auto url = render_rpc_endpoint_url(config, std::move(env_lookup));
    if (!url)
    {
        return outcome::failure(url.error());
    }

    RpcEndpoint endpoint;
    endpoint.chain_name = config.chain_name;
    endpoint.chain_id = config.chain_id;
    endpoint.url = url.value();
    endpoint.priority = config.priority;
    endpoint.weight = config.weight;
    endpoint.rate_limit_per_second = config.rate_limit_per_second;
    endpoint.is_paid = config.is_paid;
    endpoint.is_public = config.is_public;
    endpoint.verified = config.verified;
    return endpoint;
}

std::vector<RpcEndpointGroup> group_rpc_endpoints(
    const RpcManagerConfig& config,
    RpcEnvLookup            env_lookup)
{
    std::map<std::string, RpcEndpointGroup> groups;

    for (const auto& endpoint_config : config.endpoints)
    {
        const auto endpoint = build_rpc_endpoint(endpoint_config, env_lookup);
        if (!endpoint)
        {
            continue;
        }

        const auto key = chain_key(endpoint_config.chain_name, endpoint_config.chain_id);
        auto& group = groups[key];
        group.chain_name = endpoint_config.chain_name;
        group.chain_id = endpoint_config.chain_id;
        group.endpoints.push_back(endpoint.value());
    }

    std::vector<RpcEndpointGroup> ordered_groups;
    ordered_groups.reserve(groups.size());
    for (auto& [_, group] : groups)
    {
        std::sort(group.endpoints.begin(), group.endpoints.end(), endpoint_less);
        if (config.max_endpoints_per_chain != 0 && group.endpoints.size() > config.max_endpoints_per_chain)
        {
            group.endpoints.resize(config.max_endpoints_per_chain);
        }
        ordered_groups.push_back(std::move(group));
    }

    std::sort(
        ordered_groups.begin(),
        ordered_groups.end(),
        [](const RpcEndpointGroup& lhs, const RpcEndpointGroup& rhs)
        {
            if (lhs.chain_name != rhs.chain_name)
            {
                return lhs.chain_name < rhs.chain_name;
            }
            if (lhs.chain_id != rhs.chain_id)
            {
                return lhs.chain_id < rhs.chain_id;
            }
            return lhs.endpoints.size() < rhs.endpoints.size();
        });

    return ordered_groups;
}

RpcEndpointPool::RpcEndpointPool(std::vector<RpcEndpoint> endpoints)
    : endpoints_(std::move(endpoints))
{
    std::sort(endpoints_.begin(), endpoints_.end(), endpoint_less);
}

std::optional<std::reference_wrapper<RpcEndpoint>> RpcEndpointPool::next_endpoint()
{
    for (auto& endpoint : endpoints_)
    {
        if (is_usable(endpoint))
        {
            return endpoint;
        }
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const RpcEndpoint>> RpcEndpointPool::next_endpoint() const
{
    for (const auto& endpoint : endpoints_)
    {
        if (is_usable(endpoint))
        {
            return endpoint;
        }
    }
    return std::nullopt;
}

void RpcEndpointPool::mark_temporary_failure(std::string_view url)
{
    const auto now = std::chrono::steady_clock::now();

    for (auto& endpoint : endpoints_)
    {
        if (endpoint.url == url)
        {
            if ( endpoint.failure_count > 0
                 && ( now - endpoint.last_failure_time ) > kEscalationWindow )
            {
                endpoint.failure_count = 0;
            }

            endpoint.state = RpcEndpointState::kTemporarilyFailed;
            endpoint.last_failure_time = now;
            ++endpoint.failure_count;

            const auto backoff_seconds = std::chrono::seconds(
                std::min<uint64_t>(
                    (1ULL << (endpoint.failure_count - 1)),
                    kMaxBackoff.count() ) );
            endpoint.backoff_until = now + backoff_seconds;

            if ( endpoint.failure_count >= kEscalationThreshold )
            {
                endpoint.state = RpcEndpointState::kDisabled;
            }
        }
    }
}

void RpcEndpointPool::disable(std::string_view url)
{
    for (auto& endpoint : endpoints_)
    {
        if (endpoint.url == url)
        {
            endpoint.state = RpcEndpointState::kDisabled;
        }
    }
}

void RpcEndpointPool::reset_temporary_failures()
{
    for (auto& endpoint : endpoints_)
    {
        if (endpoint.state == RpcEndpointState::kTemporarilyFailed)
        {
            endpoint.state = RpcEndpointState::kAvailable;
            endpoint.failure_count = 0;
            endpoint.backoff_until = {};
        }
    }
}

bool RpcEndpointPool::is_usable(const RpcEndpoint& endpoint) noexcept
{
    if ( endpoint.state == RpcEndpointState::kAvailable )
    {
        return true;
    }
    if ( endpoint.state == RpcEndpointState::kTemporarilyFailed
         && std::chrono::steady_clock::now() >= endpoint.backoff_until )
    {
        return true;
    }
    return false;
}

RpcManager::RpcManager(RpcManagerConfig config, RpcEnvLookup env_lookup)
    : groups_(group_rpc_endpoints(config, std::move(env_lookup)))
{
    pools_.reserve(groups_.size());
    for (auto& group : groups_)
    {
        pools_.push_back(PoolEntry{group, RpcEndpointPool{group.endpoints}});
    }
}

std::optional<std::reference_wrapper<RpcEndpointPool>> RpcManager::pool(
    std::string_view chain_name,
    uint64_t         chain_id)
{
    const auto key = chain_key(chain_name, chain_id);
    for (auto& entry : pools_)
    {
        if (chain_key(entry.group.chain_name, entry.group.chain_id) == key)
        {
            return entry.pool;
        }
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const RpcEndpointPool>> RpcManager::pool(
    std::string_view chain_name,
    uint64_t         chain_id) const
{
    const auto key = chain_key(chain_name, chain_id);
    for (const auto& entry : pools_)
    {
        if (chain_key(entry.group.chain_name, entry.group.chain_id) == key)
        {
            return entry.pool;
        }
    }
    return std::nullopt;
}

std::string RpcManager::make_chain_key(std::string_view chain_name, uint64_t chain_id)
{
    return chain_key(chain_name, chain_id);
}

std::optional<RpcReceiptSourceHandle> make_receipt_source(
    RpcManager&    manager,
    std::string    chain_name,
    uint64_t       chain_id,
    FinalityPolicy finality_policy)
{
    auto pool = manager.pool(chain_name, chain_id);
    if (!pool.has_value())
    {
        return std::nullopt;
    }

    auto endpoint = pool->get().next_endpoint();
    if (!endpoint.has_value())
    {
        return std::nullopt;
    }

    auto transport = std::make_unique<RpcHttpTransport>(endpoint->get().url);
    auto source = std::make_unique<RpcReceiptSource>(
        *transport,
        finality_policy);

    RpcReceiptSourceHandle handle;
    handle.transport = std::move(transport);
    handle.source = std::move(source);
    return handle;
}

} // namespace eth::rpc
