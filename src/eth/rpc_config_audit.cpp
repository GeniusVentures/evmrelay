// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/rpc_config_audit.hpp>

#include <base/rlp-logger.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace eth::rpc {

namespace {

AuditFinding make_finding(AuditSeverity severity,
                          std::string   chain_name,
                          uint64_t      chain_id,
                          std::string   message)
{
    return {severity, std::move(chain_name), chain_id, std::move(message)};
}

} // namespace

AuditSummary audit_rpc_config(
    const RpcManagerConfig& config,
    const FinalityPolicy&   finality_policy)
{
    AuditSummary summary;
    summary.passed = true;

    if (config.endpoints.empty())
    {
        summary.findings.push_back(
            make_finding(AuditSeverity::kError, "", 0,
                         "No RPC endpoints configured — at least one endpoint per chain is required"));
        summary.passed = false;
        return summary;
    }

    if (finality_policy.confirmation_depth == 0
        && !finality_policy.prefer_finalized
        && !finality_policy.prefer_safe)
    {
        summary.findings.push_back(
            make_finding(AuditSeverity::kError, "", 0,
                         "Confirmation depth is zero and neither 'prefer_finalized' nor 'prefer_safe' is set — "
                         "no finality guarantee"));
        summary.passed = false;
    }

    std::map<uint64_t, size_t> endpoint_count_by_chain;
    std::set<std::string> seen_endpoints;

    for (const auto& ep : config.endpoints)
    {
        if (ep.chain_name.empty())
        {
            summary.findings.push_back(
                make_finding(AuditSeverity::kError, "", ep.chain_id,
                             "Endpoint has empty chain_name"));
            summary.passed = false;
        }

        if (ep.chain_id == 0)
        {
            summary.findings.push_back(
                make_finding(AuditSeverity::kError, ep.chain_name, 0,
                             "Endpoint has zero chain_id"));
            summary.passed = false;
        }

        if (ep.url_template.empty())
        {
            summary.findings.push_back(
                make_finding(AuditSeverity::kError, ep.chain_name, ep.chain_id,
                             "Endpoint has empty url_template"));
            summary.passed = false;
        }

        const auto dedup_key = std::to_string(ep.chain_id) + "|" + ep.url_template;
        if (seen_endpoints.find(dedup_key) != seen_endpoints.end())
        {
            summary.findings.push_back(
                make_finding(AuditSeverity::kWarning, ep.chain_name, ep.chain_id,
                             "Duplicate endpoint: " + ep.url_template));
        }
        seen_endpoints.insert(dedup_key);

        ++endpoint_count_by_chain[ep.chain_id];
    }

    for (const auto& [chain_id, count] : endpoint_count_by_chain)
    {
        if (count == 0)
        {
            std::string chain_label = chain_id != 0
                ? "chain_id=" + std::to_string(chain_id)
                : std::string("(unknown chain)");
            summary.findings.push_back(
                make_finding(AuditSeverity::kError, "", chain_id,
                             "No usable endpoints for " + chain_label));
            summary.passed = false;
        }
    }

    return summary;
}

} // namespace eth::rpc
