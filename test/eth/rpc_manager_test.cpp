// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <eth/rpc_manager.hpp>

#include <cstdlib>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace {

struct ScopedEnvVar
{
    explicit ScopedEnvVar(const char* name)
        : name(name)
    {
        const char* value = std::getenv(name);
        if (value != nullptr)
        {
            had_value = true;
            previous = value;
        }
    }

    ~ScopedEnvVar()
    {
        if (had_value)
        {
            ::setenv(name, previous.c_str(), 1);
        }
        else
        {
            ::unsetenv(name);
        }
    }

    void set(const std::string& value)
    {
        ::setenv(name, value.c_str(), 1);
    }

    const char* name;
    bool        had_value = false;
    std::string previous;
};

eth::rpc::RpcEndpointConfig make_endpoint(
    std::string chain_name,
    uint64_t    chain_id,
    std::string url_template,
    uint32_t    priority,
    uint32_t    weight)
{
    eth::rpc::RpcEndpointConfig config;
    config.chain_name = std::move(chain_name);
    config.chain_id = chain_id;
    config.url_template = std::move(url_template);
    config.priority = priority;
    config.weight = weight;
    config.rate_limit_per_second = 10;
    return config;
}

} // namespace

TEST(RpcManagerTest, RendersUrlWithoutKeyPlaceholder)
{
    const auto endpoint = make_endpoint("base-mainnet", 8453, "https://base.example/rpc", 1, 1);
    const auto rendered = eth::rpc::render_rpc_endpoint_url(endpoint);

    ASSERT_TRUE(rendered);
    EXPECT_EQ(rendered.value(), "https://base.example/rpc");
}

TEST(RpcManagerTest, RendersUrlUsingApiKeyLiteral)
{
    auto endpoint = make_endpoint("ethereum-mainnet", 1, "https://eth.example/v1/{key}", 1, 1);
    endpoint.api_key_literal = "literal-key";

    const auto rendered = eth::rpc::render_rpc_endpoint_url(endpoint);

    ASSERT_TRUE(rendered);
    EXPECT_EQ(rendered.value(), "https://eth.example/v1/literal-key");
}

TEST(RpcManagerTest, RendersUrlUsingApiKeyEnvVar)
{
    ScopedEnvVar env("RPC_MANAGER_TEST_KEY");
    env.set("env-key");

    auto endpoint = make_endpoint("ethereum-mainnet", 1, "https://eth.example/v1/{key}", 1, 1);
    endpoint.api_key_env_var = "RPC_MANAGER_TEST_KEY";

    const auto rendered = eth::rpc::render_rpc_endpoint_url(endpoint);

    ASSERT_TRUE(rendered);
    EXPECT_EQ(rendered.value(), "https://eth.example/v1/env-key");
}

TEST(RpcManagerTest, FailsWhenTemplateRequiresMissingKey)
{
    auto endpoint = make_endpoint("ethereum-mainnet", 1, "https://eth.example/v1/{key}", 1, 1);

    const auto rendered = eth::rpc::render_rpc_endpoint_url(endpoint);

    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, eth::rpc::RpcEndpointErrorCode::kMissingApiKey);
}

TEST(RpcManagerTest, FailsWhenConfiguredEnvVarIsMissing)
{
    auto endpoint = make_endpoint("ethereum-mainnet", 1, "https://eth.example/v1/{key}", 1, 1);
    endpoint.api_key_env_var = "RPC_MANAGER_MISSING_KEY";

    const auto rendered = eth::rpc::render_rpc_endpoint_url(
        endpoint,
        [](std::string_view)
        {
            return std::nullopt;
        });

    ASSERT_FALSE(rendered);
    EXPECT_EQ(rendered.error().code, eth::rpc::RpcEndpointErrorCode::kMissingApiKey);
}

TEST(RpcManagerTest, GroupsAndSortsEndpointsDeterministically)
{
    eth::rpc::RpcManagerConfig config;
    config.max_endpoints_per_chain = 2;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://a.example", 10, 1));
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://b.example", 1, 3));
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://c.example", 1, 10));
    config.endpoints.push_back(make_endpoint("base-mainnet", 8453, "https://base.example", 2, 1));

    const auto groups = eth::rpc::group_rpc_endpoints(config);

    ASSERT_EQ(groups.size(), 2U);
    EXPECT_EQ(groups[0].chain_name, "base-mainnet");
    EXPECT_EQ(groups[1].chain_name, "ethereum-mainnet");
    ASSERT_EQ(groups[1].endpoints.size(), 2U);
    EXPECT_EQ(groups[1].endpoints[0].url, "https://c.example");
    EXPECT_EQ(groups[1].endpoints[1].url, "https://b.example");
}

TEST(RpcManagerTest, PoolSkipsFailedAndDisabledEndpoints)
{
    std::vector<eth::rpc::RpcEndpoint> endpoints;

    eth::rpc::RpcEndpoint first;
    first.chain_name = "ethereum-mainnet";
    first.chain_id = 1;
    first.url = "https://a.example";
    first.priority = 1;
    first.weight = 10;
    endpoints.push_back(first);

    eth::rpc::RpcEndpoint second = first;
    second.url = "https://b.example";
    second.priority = 2;
    endpoints.push_back(second);

    eth::rpc::RpcEndpointPool pool(std::move(endpoints));
    auto chosen = pool.next_endpoint();
    ASSERT_TRUE(chosen);
    EXPECT_EQ(chosen->get().url, "https://a.example");

    pool.mark_temporary_failure("https://a.example");
    chosen = pool.next_endpoint();
    ASSERT_TRUE(chosen);
    EXPECT_EQ(chosen->get().url, "https://b.example");

    pool.disable("https://b.example");
    EXPECT_FALSE(pool.next_endpoint().has_value());
}

TEST(RpcManagerTest, ManagerBuildsPoolsPerChain)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://a.example", 1, 1));
    config.endpoints.push_back(make_endpoint("base-mainnet", 8453, "https://base.example", 1, 1));

    eth::rpc::RpcManager manager(std::move(config));

    const auto base_pool = manager.pool("base-mainnet", 8453);
    ASSERT_TRUE(base_pool);
    ASSERT_TRUE(base_pool->get().next_endpoint());
    EXPECT_EQ(base_pool->get().next_endpoint()->get().url, "https://base.example");

    EXPECT_FALSE(manager.pool("missing-chain", 1).has_value());
}
