// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <eth/finality_policy.hpp>

namespace eth {

namespace {

constexpr uint64_t kEthereumMainnetChainId = 1;
constexpr uint64_t kEthereumSepoliaChainId = 11155111;
constexpr uint64_t kEthereumHoleskyChainId = 17000;
constexpr uint64_t kPolygonMainnetChainId = 137;
constexpr uint64_t kPolygonAmoyChainId = 80002;
constexpr uint64_t kBscMainnetChainId = 56;
constexpr uint64_t kBscTestnetChainId = 97;
constexpr uint64_t kBaseMainnetChainId = 8453;
constexpr uint64_t kBaseSepoliaChainId = 84532;

[[nodiscard]] std::optional<uint64_t> confirmed_head(
    std::optional<uint64_t> latest,
    uint64_t                confirmation_depth) noexcept
{
    if (!latest.has_value() || confirmation_depth == 0 || *latest < confirmation_depth)
    {
        return std::nullopt;
    }
    return *latest - confirmation_depth;
}

} // namespace

FinalityPolicy finality_policy_for_chain_id(uint64_t chain_id) noexcept
{
    switch (chain_id)
    {
        case kEthereumMainnetChainId:
        case kEthereumSepoliaChainId:
        case kEthereumHoleskyChainId:
            return FinalityPolicy{
                true,
                true,
                64,
            };

        case kBaseMainnetChainId:
        case kBaseSepoliaChainId:
            return FinalityPolicy{
                true,
                true,
                900,
            };

        case kPolygonMainnetChainId:
        case kPolygonAmoyChainId:
            return FinalityPolicy{
                false,
                false,
                256,
            };

        case kBscMainnetChainId:
        case kBscTestnetChainId:
            return FinalityPolicy{
                false,
                false,
                30,
            };

        default:
            return FinalityPolicy{
                true,
                true,
                64,
            };
    }
}

FinalityDecision choose_finality_head(
    const FinalityPolicy&    policy,
    const ChainHeadSnapshot& heads) noexcept
{
    if (policy.prefer_finalized && heads.finalized_number.has_value())
    {
        return FinalityDecision{
            FinalityHeadKind::kFinalized,
            *heads.finalized_number,
            0,
        };
    }

    if (policy.prefer_safe && heads.safe_number.has_value())
    {
        return FinalityDecision{
            FinalityHeadKind::kSafe,
            *heads.safe_number,
            0,
        };
    }

    const auto confirmed = confirmed_head(heads.latest_number, policy.confirmation_depth);
    if (!confirmed.has_value())
    {
        return {};
    }

    return FinalityDecision{
        FinalityHeadKind::kConfirmedLatest,
        *confirmed,
        policy.confirmation_depth,
    };
}

bool is_final_under_policy(
    uint64_t                 block_number,
    const FinalityPolicy&    policy,
    const ChainHeadSnapshot& heads) noexcept
{
    const auto decision = choose_finality_head(policy, heads);
    return decision && block_number <= decision.block_number;
}

} // namespace eth
