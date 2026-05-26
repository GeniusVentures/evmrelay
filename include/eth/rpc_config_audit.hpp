// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_RPC_CONFIG_AUDIT_HPP
#define EVMRELAY_INCLUDE_ETH_RPC_CONFIG_AUDIT_HPP

#include <eth/rpc_manager_config.hpp>
#include <eth/finality_policy.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eth::rpc {

enum class AuditSeverity
{
    kInfo,
    kWarning,
    kError
};

struct AuditFinding
{
    AuditSeverity severity = AuditSeverity::kInfo;
    std::string   chain_name;
    uint64_t      chain_id = 0;
    std::string   message;
};

struct AuditSummary
{
    bool                    passed = true;
    std::vector<AuditFinding> findings;
};

[[nodiscard]] AuditSummary audit_rpc_config(
    const RpcManagerConfig&   config,
    const FinalityPolicy&     finality_policy);

[[nodiscard]] inline std::string to_string(AuditSeverity severity)
{
    switch (severity)
    {
        case AuditSeverity::kInfo: return "INFO";
        case AuditSeverity::kWarning: return "WARN";
        case AuditSeverity::kError: return "ERROR";
    }
    return "INFO";
}

} // namespace eth::rpc

#endif // EVMRELAY_INCLUDE_ETH_RPC_CONFIG_AUDIT_HPP
