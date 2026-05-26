// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>

#include <eth/rpc_config_audit.hpp>
#include <eth/rpc_manager_config.hpp>
#include <eth/finality_policy.hpp>

namespace {

eth::rpc::RpcEndpointConfig make_endpoint(
    std::string chain_name,
    uint64_t    chain_id,
    std::string url_template)
{
    eth::rpc::RpcEndpointConfig config;
    config.chain_name = std::move(chain_name);
    config.chain_id = chain_id;
    config.url_template = std::move(url_template);
    return config;
}

} // namespace

TEST(RpcConfigAuditTest, PassesValidConfig)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://eth.example"));

    eth::FinalityPolicy policy;
    policy.prefer_finalized = true;
    policy.confirmation_depth = 12;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_TRUE(summary.passed);
    EXPECT_TRUE(summary.findings.empty());
}

TEST(RpcConfigAuditTest, RejectsEmptyEndpointList)
{
    eth::rpc::RpcManagerConfig config;

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 12;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_FALSE(summary.passed);
    EXPECT_GE(summary.findings.size(), 1U);
    EXPECT_EQ(summary.findings[0].severity, eth::rpc::AuditSeverity::kError);
}

TEST(RpcConfigAuditTest, WarnsOnZeroConfirmationDepthWithoutFinality)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://eth.example"));

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 0;
    policy.prefer_finalized = false;
    policy.prefer_safe = false;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_FALSE(summary.passed);
}

TEST(RpcConfigAuditTest, PassesZeroConfirmationDepthWithFinalityFlag)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://eth.example"));

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 0;
    policy.prefer_finalized = true;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_TRUE(summary.passed);
}

TEST(RpcConfigAuditTest, RejectsEmptyChainName)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("", 1, "https://eth.example"));

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 12;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_FALSE(summary.passed);
}

TEST(RpcConfigAuditTest, RejectsZeroChainId)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 0, "https://eth.example"));

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 12;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_FALSE(summary.passed);
}

TEST(RpcConfigAuditTest, RejectsEmptyUrlTemplate)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, ""));

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 12;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_FALSE(summary.passed);
}

TEST(RpcConfigAuditTest, WarnsOnDuplicateEndpoints)
{
    eth::rpc::RpcManagerConfig config;
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://eth.example"));
    config.endpoints.push_back(make_endpoint("ethereum-mainnet", 1, "https://eth.example"));

    eth::FinalityPolicy policy;
    policy.confirmation_depth = 12;

    const auto summary = eth::rpc::audit_rpc_config(config, policy);

    EXPECT_TRUE(summary.passed);
    bool has_warning = false;
    for (const auto& f : summary.findings)
    {
        if (f.severity == eth::rpc::AuditSeverity::kWarning)
        {
            has_warning = true;
        }
    }
    EXPECT_TRUE(has_warning);
}
