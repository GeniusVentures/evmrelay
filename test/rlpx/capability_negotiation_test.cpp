// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <gtest/gtest.h>
#include <rlpx/protocol/messages.hpp>
#include <rlpx/rlpx_session.hpp>

namespace {

using Capability = rlpx::protocol::Capability;

TEST(RlpxCapabilityNegotiationTest, NegotiatesHighestSupportedEthVersion)
{
    const std::vector<Capability> capabilities = {
        {"eth", 66},
        {"eth", 68},
        {"eth", 69},
    };

    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_version_for_test(capabilities), 69U);
}

TEST(RlpxCapabilityNegotiationTest, ComputesEthOffsetFromCapabilityOrdering)
{
    const std::vector<Capability> capabilities = {
        {"abc", 1},
        {"eth", 69},
    };

    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_offset_for_test(capabilities), 17U);
}

TEST(RlpxCapabilityNegotiationTest, EthOffsetDefaultsToBaseWhenEthIsFirstMatchedCapability)
{
    const std::vector<Capability> capabilities = {
        {"eth", 69},
        {"snap", 1},
    };

    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_offset_for_test(capabilities), 16U);
}

TEST(RlpxCapabilityNegotiationTest, NoEthCapabilityYieldsZeroVersionAndOffset)
{
    const std::vector<Capability> capabilities = {
        {"snap", 1},
        {"abc", 1},
    };

    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_version_for_test(capabilities), 0U);
    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_offset_for_test(capabilities), 0U);
}

TEST(RlpxCapabilityNegotiationTest, UnsupportedEthVersionYieldsZeroVersionAndZeroOffset)
{
    const std::vector<Capability> capabilities = {
        {"abc", 1},
        {"eth", 65},
    };

    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_version_for_test(capabilities), 0U);
    EXPECT_EQ(rlpx::RlpxSession::negotiate_eth_offset_for_test(capabilities), 0U);
}

} // namespace
