// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/abi_decoder.hpp>
#include <eth/bridge_observation.hpp>

namespace {

template <typename Array>
Array make_filled(uint8_t seed)
{
    Array value{};
    for (size_t i = 0; i < value.size(); ++i)
    {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

eth::Secp256k1PrivateKey valid_private_key()
{
    eth::Secp256k1PrivateKey key{};
    key[31] = 1;
    return key;
}

eth::BridgeEventClaim make_claim()
{
    eth::BridgeEventClaim claim;
    claim.src_chain_id = 1;
    claim.dest_chain_id = 56;
    claim.block_number = 123456;
    claim.block_hash = make_filled<eth::Hash256>(0x10);
    claim.tx_hash = make_filled<eth::Hash256>(0x30);
    claim.log_index = 5;
    claim.bridge_contract = make_filled<eth::Address>(0x50);
    claim.event_topic0 = eth::abi::event_signature_hash("BridgeSourceBurned(address,uint256,uint256,address)");
    claim.topics.push_back(claim.event_topic0);
    claim.topics.push_back(make_filled<eth::Hash256>(0x70));
    claim.data = {0x01, 0x02, 0x03};
    claim.sender = make_filled<eth::Address>(0x80);
    claim.token_id_or_nonce = intx::uint256{42};
    claim.amount = intx::uint256{1000};
    claim.recipient = make_filled<eth::Address>(0xA0);
    claim.observed_at = 987654;
    claim.finality_depth = 64;
    return claim;
}

} // namespace

TEST(BridgeObservationTest, DomainSeparatorDependsOnBridgeDomainFields)
{
    const auto contract = make_filled<eth::Address>(0x10);

    const auto base = eth::bridge_event_domain_separator(1, 56, contract);
    EXPECT_EQ(base, eth::bridge_event_domain_separator(1, 56, contract));
    EXPECT_NE(base, eth::bridge_event_domain_separator(2, 56, contract));
    EXPECT_NE(base, eth::bridge_event_domain_separator(1, 57, contract));
    EXPECT_NE(base, eth::bridge_event_domain_separator(1, 56, make_filled<eth::Address>(0x20)));
}

TEST(BridgeObservationTest, ClaimHashDependsOnClaimFields)
{
    auto claim = make_claim();
    auto changed = claim;
    changed.amount = intx::uint256{1001};

    EXPECT_EQ(eth::bridge_event_claim_hash(claim), eth::bridge_event_claim_hash(claim));
    EXPECT_NE(eth::bridge_event_claim_hash(claim), eth::bridge_event_claim_hash(changed));
}

TEST(BridgeObservationTest, SignsAndVerifiesObservation)
{
    const auto claim = make_claim();
    const auto key = valid_private_key();
    const auto observer = eth::observer_address_from_private_key(key);

    const auto observation = eth::sign_bridge_event_claim(claim, key);

    ASSERT_TRUE(observer.has_value());
    ASSERT_TRUE(observation.has_value());
    EXPECT_EQ(observation->observer, *observer);
    EXPECT_EQ(observation->signature.size(), 65U);
    EXPECT_TRUE(eth::verify_bridge_event_observation(*observation));
}

TEST(BridgeObservationTest, RejectsInvalidPrivateKey)
{
    const auto claim = make_claim();
    eth::Secp256k1PrivateKey zero_key{};

    EXPECT_FALSE(eth::observer_address_from_private_key(zero_key).has_value());
    EXPECT_FALSE(eth::sign_bridge_event_claim(claim, zero_key).has_value());
}

TEST(BridgeObservationTest, RejectsTamperedClaim)
{
    const auto observation = eth::sign_bridge_event_claim(make_claim(), valid_private_key());
    ASSERT_TRUE(observation.has_value());

    auto tampered = *observation;
    tampered.claim.amount = intx::uint256{1001};

    EXPECT_FALSE(eth::verify_bridge_event_observation(tampered));
}

TEST(BridgeObservationTest, RejectsWrongObserver)
{
    const auto observation = eth::sign_bridge_event_claim(make_claim(), valid_private_key());
    ASSERT_TRUE(observation.has_value());

    auto tampered = *observation;
    tampered.observer = make_filled<eth::Address>(0xEE);

    EXPECT_FALSE(eth::verify_bridge_event_observation(tampered));
}

TEST(BridgeObservationTest, RejectsBadSignatureSize)
{
    const auto observation = eth::sign_bridge_event_claim(make_claim(), valid_private_key());
    ASSERT_TRUE(observation.has_value());

    auto tampered = *observation;
    tampered.signature.pop_back();

    EXPECT_FALSE(eth::verify_bridge_event_observation(tampered));
}

TEST(BridgeObservationTest, RejectsCorruptSignatureBytes)
{
    const auto observation = eth::sign_bridge_event_claim(make_claim(), valid_private_key());
    ASSERT_TRUE(observation.has_value());

    auto tampered = *observation;
    ASSERT_FALSE(tampered.signature.empty());
    tampered.signature.front() ^= 0x01;

    EXPECT_FALSE(eth::verify_bridge_event_observation(tampered));
}

TEST(BridgeObservationTest, RejectsBadRecoveryId)
{
    const auto observation = eth::sign_bridge_event_claim(make_claim(), valid_private_key());
    ASSERT_TRUE(observation.has_value());

    auto tampered = *observation;
    ASSERT_EQ(tampered.signature.size(), 65U);
    tampered.signature.back() = 4;

    EXPECT_FALSE(eth::verify_bridge_event_observation(tampered));
}
