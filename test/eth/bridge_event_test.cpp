// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

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
