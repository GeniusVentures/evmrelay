// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/finality_policy.hpp>

TEST(FinalityPolicyTest, PrefersFinalizedHeadWhenAvailable)
{
    const eth::FinalityPolicy policy{
        true,
        true,
        64,
    };
    const eth::ChainHeadSnapshot heads{
        100,
        120,
        200,
    };

    const auto decision = eth::choose_finality_head(policy, heads);

    ASSERT_TRUE(decision);
    EXPECT_EQ(decision.kind, eth::FinalityHeadKind::kFinalized);
    EXPECT_EQ(decision.block_number, 100U);
    EXPECT_EQ(decision.confirmation_depth, 0U);
}

TEST(FinalityPolicyTest, FallsBackToSafeHeadWhenFinalizedUnavailable)
{
    const eth::FinalityPolicy policy{
        true,
        true,
        64,
    };
    const eth::ChainHeadSnapshot heads{
        std::nullopt,
        120,
        200,
    };

    const auto decision = eth::choose_finality_head(policy, heads);

    ASSERT_TRUE(decision);
    EXPECT_EQ(decision.kind, eth::FinalityHeadKind::kSafe);
    EXPECT_EQ(decision.block_number, 120U);
}

TEST(FinalityPolicyTest, UsesConfirmedLatestWhenTagsUnavailable)
{
    const eth::FinalityPolicy policy{
        true,
        true,
        64,
    };
    const eth::ChainHeadSnapshot heads{
        std::nullopt,
        std::nullopt,
        200,
    };

    const auto decision = eth::choose_finality_head(policy, heads);

    ASSERT_TRUE(decision);
    EXPECT_EQ(decision.kind, eth::FinalityHeadKind::kConfirmedLatest);
    EXPECT_EQ(decision.block_number, 136U);
    EXPECT_EQ(decision.confirmation_depth, 64U);
}

TEST(FinalityPolicyTest, DoesNotTreatLatestAsFinalWithoutConfirmationDepth)
{
    const eth::FinalityPolicy policy{
        false,
        false,
        0,
    };
    const eth::ChainHeadSnapshot heads{
        std::nullopt,
        std::nullopt,
        200,
    };

    const auto decision = eth::choose_finality_head(policy, heads);
    EXPECT_FALSE(decision);
    EXPECT_EQ(decision.kind, eth::FinalityHeadKind::kUnavailable);
}

TEST(FinalityPolicyTest, ReportsWhetherBlockIsFinalUnderPolicy)
{
    const eth::FinalityPolicy policy{
        true,
        true,
        30,
    };
    const eth::ChainHeadSnapshot heads{
        std::nullopt,
        std::nullopt,
        1000,
    };

    EXPECT_TRUE(eth::is_final_under_policy(970, policy, heads));
    EXPECT_FALSE(eth::is_final_under_policy(971, policy, heads));
}

TEST(FinalityPolicyTest, ChainDefaultsMatchExpectedFamilies)
{
    const auto ethereum = eth::finality_policy_for_chain_id(1);
    EXPECT_TRUE(ethereum.prefer_finalized);
    EXPECT_TRUE(ethereum.prefer_safe);
    EXPECT_EQ(ethereum.confirmation_depth, 64U);

    const auto polygon = eth::finality_policy_for_chain_id(137);
    EXPECT_FALSE(polygon.prefer_finalized);
    EXPECT_FALSE(polygon.prefer_safe);
    EXPECT_EQ(polygon.confirmation_depth, 256U);

    const auto bsc = eth::finality_policy_for_chain_id(56);
    EXPECT_FALSE(bsc.prefer_finalized);
    EXPECT_FALSE(bsc.prefer_safe);
    EXPECT_EQ(bsc.confirmation_depth, 30U);
}
