// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/bridge_event.hpp>

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

eth::BridgeEventClaim make_claim()
{
    eth::BridgeEventClaim claim;
    claim.src_chain_id = 1;
    claim.dest_chain_id = 4242;
    claim.block_number = 1000;
    claim.block_hash = make_filled<eth::Hash256>(0x10);
    claim.tx_hash = make_filled<eth::Hash256>(0x20);
    claim.log_index = 0;
    claim.bridge_contract = make_filled<eth::Address>(0x30);
    claim.event_topic0 = make_filled<eth::Hash256>(0x40);
    claim.topics.push_back(claim.event_topic0);
    claim.topics.push_back(make_filled<eth::Hash256>(0x50));
    claim.data = {0x01, 0x02, 0x03};
    claim.sender = make_filled<eth::Address>(0x60);
    claim.token_id_or_nonce = intx::uint256(7);
    claim.amount = intx::uint256(42);
    claim.recipient = make_filled<eth::Address>(0x70);
    claim.observed_at = 12345;
    claim.finality_depth = 64;
    return claim;
}

eth::ReceiptResult make_matching_receipt(const eth::BridgeEventClaim& claim)
{
    eth::codec::LogEntry log;
    log.address = claim.bridge_contract;
    log.topics = claim.topics;
    log.data = claim.data;

    eth::ReceiptResult receipt;
    receipt.receipt.status = true;
    receipt.receipt.logs.push_back(log);
    receipt.tx_hash = claim.tx_hash;
    receipt.block_number = claim.block_number;
    receipt.block_hash = claim.block_hash;
    return receipt;
}

} // namespace

TEST(BridgeEventTest, BridgeEventKeyUsesSourceChainTransactionAndLogIndex)
{
    auto claim = make_claim();
    const auto key = eth::bridge_event_key(claim);

    EXPECT_EQ(key.src_chain_id, claim.src_chain_id);
    EXPECT_EQ(key.tx_hash, claim.tx_hash);
    EXPECT_EQ(key.log_index, claim.log_index);
}

TEST(BridgeEventTest, EventDeduperRejectsDuplicateKey)
{
    auto claim = make_claim();
    eth::EventDeduper deduper;

    EXPECT_TRUE(deduper.mark_seen(eth::bridge_event_key(claim)));
    EXPECT_FALSE(deduper.mark_seen(eth::bridge_event_key(claim)));
    EXPECT_TRUE(deduper.contains(eth::bridge_event_key(claim)));
    EXPECT_EQ(deduper.size(), 1U);

    claim.log_index = 1;
    EXPECT_TRUE(deduper.mark_seen(eth::bridge_event_key(claim)));
    EXPECT_EQ(deduper.size(), 2U);
}

TEST(BridgeEventTest, VerifyReceiptLogAcceptsExactSuccessfulLog)
{
    const auto claim = make_claim();
    const auto receipt = make_matching_receipt(claim);

    const auto result = eth::verify_receipt_log(receipt, claim);
    EXPECT_TRUE(result);
    EXPECT_EQ(result.error, eth::ReceiptLogVerificationError::kNone);
}

TEST(BridgeEventTest, VerifyReceiptLogRejectsFailedReceipt)
{
    const auto claim = make_claim();
    auto receipt = make_matching_receipt(claim);
    receipt.receipt.status = false;

    const auto result = eth::verify_receipt_log(receipt, claim);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, eth::ReceiptLogVerificationError::kReceiptFailed);
}

TEST(BridgeEventTest, VerifyReceiptLogRejectsWrongLogFields)
{
    const auto claim = make_claim();
    auto receipt = make_matching_receipt(claim);
    receipt.receipt.logs[0].address = make_filled<eth::Address>(0x90);

    const auto result = eth::verify_receipt_log(receipt, claim);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, eth::ReceiptLogVerificationError::kContractMismatch);
}

TEST(BridgeEventTest, VerifyReceiptLogRejectsOutOfRangeLogIndex)
{
    auto claim = make_claim();
    const auto receipt = make_matching_receipt(claim);
    claim.log_index = 1;

    const auto result = eth::verify_receipt_log(receipt, claim);
    EXPECT_FALSE(result);
    EXPECT_EQ(result.error, eth::ReceiptLogVerificationError::kLogIndexOutOfRange);
}

TEST(BridgeEventTest, VerifyReceiptLogUsesExplicitRpcLogIndexes)
{
    auto claim = make_claim();
    claim.log_index = 12;

    auto receipt = make_matching_receipt(claim);
    receipt.log_indices = {12};

    const auto result = eth::verify_receipt_log(receipt, claim);
    EXPECT_TRUE(result);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdIsDeterministic)
{
    const auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);
    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_EQ(id1, id2);
    EXPECT_FALSE(id1 == eth::Hash256{});
}

TEST(BridgeEventTest, ComputeBridgeMessageIdMatchesFreeFunction)
{
    const auto claim = make_claim();
    const auto from_claim = eth::bridge_message_id(claim);
    const auto from_fn = eth::compute_bridge_message_id(
        claim.src_chain_id, claim.bridge_contract, claim.tx_hash, claim.log_index);
    EXPECT_EQ(from_claim, from_fn);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdChangesWithDifferentChainId)
{
    auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);
    claim.src_chain_id = 56;
    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_NE(id1, id2);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdChangesWithDifferentContract)
{
    auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);
    claim.bridge_contract = make_filled<eth::Address>(0xAA);
    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_NE(id1, id2);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdChangesWithDifferentTxHash)
{
    auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);
    claim.tx_hash = make_filled<eth::Hash256>(0xBB);
    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_NE(id1, id2);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdChangesWithDifferentLogIndex)
{
    auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);
    claim.log_index = 5;
    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_NE(id1, id2);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdIgnoresNonCanonicalFields)
{
    auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);

    claim.amount = intx::uint256(99999);
    claim.block_number = 55555;
    claim.observed_at = 77777;
    claim.finality_depth = 32;

    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_EQ(id1, id2);
}

TEST(BridgeEventTest, ComputeBridgeMessageIdProducesValidHashWithZeroFields)
{
    eth::BridgeEventClaim zero_claim;
    const auto id = eth::bridge_message_id(zero_claim);
    EXPECT_FALSE(id == eth::Hash256{});
}

TEST(BridgeEventTest, ComputeBridgeMessageIdNearCollisionDifferentDestChain)
{
    auto claim = make_claim();
    const auto id1 = eth::bridge_message_id(claim);
    claim.dest_chain_id = 999;
    const auto id2 = eth::bridge_message_id(claim);
    EXPECT_EQ(id1, id2);
}
