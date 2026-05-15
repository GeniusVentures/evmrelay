// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_FINALITY_POLICY_HPP
#define EVMRELAY_INCLUDE_ETH_FINALITY_POLICY_HPP

#include <cstdint>
#include <optional>

namespace eth {

enum class FinalityHeadKind
{
    kUnavailable,
    kFinalized,
    kSafe,
    kConfirmedLatest,
};

struct FinalityPolicy
{
    bool     prefer_finalized = true;
    bool     prefer_safe = true;
    uint64_t confirmation_depth = 0;
};

struct ChainHeadSnapshot
{
    std::optional<uint64_t> finalized_number;
    std::optional<uint64_t> safe_number;
    std::optional<uint64_t> latest_number;
};

struct FinalityDecision
{
    FinalityHeadKind kind = FinalityHeadKind::kUnavailable;
    uint64_t         block_number = 0;
    uint64_t         confirmation_depth = 0;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return kind != FinalityHeadKind::kUnavailable;
    }
};

[[nodiscard]] FinalityPolicy finality_policy_for_chain_id(uint64_t chain_id) noexcept;
[[nodiscard]] FinalityDecision choose_finality_head(
    const FinalityPolicy&   policy,
    const ChainHeadSnapshot& heads) noexcept;
[[nodiscard]] bool is_final_under_policy(
    uint64_t                block_number,
    const FinalityPolicy&   policy,
    const ChainHeadSnapshot& heads) noexcept;

} // namespace eth

#endif // EVMRELAY_INCLUDE_ETH_FINALITY_POLICY_HPP
