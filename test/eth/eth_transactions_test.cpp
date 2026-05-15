// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/objects.hpp>
#include <eth/messages.hpp>
#include <fstream>

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

} // namespace

// ============================================================================
// Legacy transaction (type 0x00)
// ============================================================================

TEST(TransactionTest, LegacyRoundtrip)
{
    eth::codec::Transaction tx;
    tx.type      = eth::codec::TransactionType::kLegacy;
    tx.nonce     = 42;
    tx.gas_price = intx::uint256(20000000000ULL);
    tx.gas_limit = 21000;
    tx.to        = make_filled<eth::codec::Address>(0xAA);
    tx.value     = intx::uint256(1000000000000000000ULL);
    tx.data      = {0xde, 0xad, 0xbe, 0xef};
    tx.v         = intx::uint256(27);
    tx.r         = intx::uint256(0xAAAA);
    tx.s         = intx::uint256(0xBBBB);

    auto encoded = eth::codec::encode_transaction(tx);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_FALSE(encoded.value().empty());
    // Legacy starts with an RLP list prefix (>= 0xC0)
    EXPECT_GE(encoded.value()[0], 0xC0u);

    auto decoded = eth::codec::decode_transaction(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& r = decoded.value();
    EXPECT_EQ(r.type, tx.type);
    EXPECT_EQ(r.nonce, tx.nonce);
    ASSERT_TRUE(r.gas_price.has_value());
    EXPECT_EQ(r.gas_price.value(), tx.gas_price.value());
    EXPECT_EQ(r.gas_limit, tx.gas_limit);
    ASSERT_TRUE(r.to.has_value());
    EXPECT_EQ(r.to.value(), tx.to.value());
    EXPECT_EQ(r.value, tx.value);
    EXPECT_EQ(r.data, tx.data);
    EXPECT_EQ(r.v, tx.v);
    EXPECT_EQ(r.r, tx.r);
    EXPECT_EQ(r.s, tx.s);
}

TEST(TransactionTest, LegacyContractCreation)
{
    eth::codec::Transaction tx;
    tx.type      = eth::codec::TransactionType::kLegacy;
    tx.nonce     = 1;
    tx.gas_price = intx::uint256(1000000000ULL);
    tx.gas_limit = 300000;
    // tx.to is empty → contract creation
    tx.value     = intx::uint256(0);
    tx.data      = {0x60, 0x80, 0x60, 0x40}; // minimal init code
    tx.v         = intx::uint256(1);
    tx.r         = intx::uint256(2);
    tx.s         = intx::uint256(3);

    auto encoded = eth::codec::encode_transaction(tx);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::codec::decode_transaction(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_FALSE(decoded.value().to.has_value());
    EXPECT_EQ(decoded.value().data, tx.data);
}

// ============================================================================
// EIP-2930 access list transaction (type 0x01)
// ============================================================================

TEST(TransactionTest, AccessListRoundtrip)
{
    eth::codec::Transaction tx;
    tx.type      = eth::codec::TransactionType::kAccessList;
    tx.chain_id  = 11155111; // Sepolia
    tx.nonce     = 7;
    tx.gas_price = intx::uint256(30000000000ULL);
    tx.gas_limit = 50000;
    tx.to        = make_filled<eth::codec::Address>(0x11);
    tx.value     = intx::uint256(0);
    tx.data      = {};

    eth::codec::AccessListEntry entry;
    entry.address      = make_filled<eth::codec::Address>(0x55);
    entry.storage_keys = {make_filled<eth::codec::Hash256>(0x01), make_filled<eth::codec::Hash256>(0x02)};
    tx.access_list.push_back(entry);

    tx.v = intx::uint256(0);
    tx.r = intx::uint256(0xCCCC);
    tx.s = intx::uint256(0xDDDD);

    auto encoded = eth::codec::encode_transaction(tx);
    ASSERT_TRUE(encoded.has_value());
    // EIP-2718 typed: first byte is the type prefix
    EXPECT_EQ(encoded.value()[0], 0x01u);

    auto decoded = eth::codec::decode_transaction(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& r = decoded.value();
    EXPECT_EQ(r.type, eth::codec::TransactionType::kAccessList);
    ASSERT_TRUE(r.chain_id.has_value());
    EXPECT_EQ(r.chain_id.value(), tx.chain_id.value());
    EXPECT_EQ(r.nonce, tx.nonce);
    ASSERT_EQ(r.access_list.size(), 1u);
    EXPECT_EQ(r.access_list[0].address, entry.address);
    ASSERT_EQ(r.access_list[0].storage_keys.size(), 2u);
    EXPECT_EQ(r.access_list[0].storage_keys[0], entry.storage_keys[0]);
    EXPECT_EQ(r.v, tx.v);
}

// ============================================================================
// EIP-1559 dynamic fee transaction (type 0x02)
// ============================================================================

TEST(TransactionTest, DynamicFeeRoundtrip)
{
    eth::codec::Transaction tx;
    tx.type                    = eth::codec::TransactionType::kDynamicFee;
    tx.chain_id                = 1; // Mainnet
    tx.nonce                   = 100;
    tx.max_priority_fee_per_gas = intx::uint256(2000000000ULL);  // 2 gwei tip
    tx.max_fee_per_gas          = intx::uint256(50000000000ULL); // 50 gwei cap
    tx.gas_limit                = 21000;
    tx.to                       = make_filled<eth::codec::Address>(0x99);
    tx.value                    = intx::uint256(500000000000000000ULL); // 0.5 ETH
    tx.data                     = {};
    tx.v                        = intx::uint256(1);
    tx.r                        = intx::uint256(0x1234);
    tx.s                        = intx::uint256(0x5678);

    auto encoded = eth::codec::encode_transaction(tx);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(encoded.value()[0], 0x02u);

    auto decoded = eth::codec::decode_transaction(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& r = decoded.value();
    EXPECT_EQ(r.type, eth::codec::TransactionType::kDynamicFee);
    ASSERT_TRUE(r.chain_id.has_value());
    EXPECT_EQ(r.chain_id.value(), tx.chain_id.value());
    EXPECT_EQ(r.nonce, tx.nonce);
    ASSERT_TRUE(r.max_priority_fee_per_gas.has_value());
    EXPECT_EQ(r.max_priority_fee_per_gas.value(), tx.max_priority_fee_per_gas.value());
    ASSERT_TRUE(r.max_fee_per_gas.has_value());
    EXPECT_EQ(r.max_fee_per_gas.value(), tx.max_fee_per_gas.value());
    EXPECT_EQ(r.gas_limit, tx.gas_limit);
    ASSERT_TRUE(r.to.has_value());
    EXPECT_EQ(r.to.value(), tx.to.value());
    EXPECT_EQ(r.value, tx.value);
    EXPECT_EQ(r.v, tx.v);
    EXPECT_EQ(r.r, tx.r);
    EXPECT_EQ(r.s, tx.s);
}

TEST(TransactionTest, DynamicFeeEmptyAccessList)
{
    eth::codec::Transaction tx;
    tx.type                    = eth::codec::TransactionType::kDynamicFee;
    tx.chain_id                = 1;
    tx.nonce                   = 0;
    tx.max_priority_fee_per_gas = intx::uint256(0);
    tx.max_fee_per_gas          = intx::uint256(10000000000ULL);
    tx.gas_limit                = 21000;
    tx.to                       = make_filled<eth::codec::Address>(0x77);
    tx.value                    = intx::uint256(0);
    tx.v                        = intx::uint256(0);
    tx.r                        = intx::uint256(1);
    tx.s                        = intx::uint256(2);

    auto encoded = eth::codec::encode_transaction(tx);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::codec::decode_transaction(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded.value().access_list.empty());
}

// ============================================================================
// GetBlockBodies / BlockBodies round-trip (eth/66 envelope)
// ============================================================================

TEST(BlockBodiesTest, GetBlockBodiesRoundtripEth66)
{
    eth::GetBlockBodiesMessage original;
    original.request_id = 555;
    original.block_hashes.push_back(make_filled<eth::Hash256>(0x10));
    original.block_hashes.push_back(make_filled<eth::Hash256>(0x20));

    auto encoded = eth::protocol::encode_get_block_bodies(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_get_block_bodies(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& r = decoded.value();
    ASSERT_TRUE(r.request_id.has_value());
    EXPECT_EQ(r.request_id.value(), original.request_id.value());
    ASSERT_EQ(r.block_hashes.size(), 2u);
    EXPECT_EQ(r.block_hashes[0], original.block_hashes[0]);
    EXPECT_EQ(r.block_hashes[1], original.block_hashes[1]);
}

TEST(BlockBodiesTest, BlockBodiesRoundtripEth66)
{
    eth::BlockBodiesMessage original;
    original.request_id = 556;

    eth::BlockBody body;
    {
        eth::codec::Transaction tx;
        tx.type      = eth::codec::TransactionType::kLegacy;
        tx.nonce     = 5;
        tx.gas_price = intx::uint256(1000000000ULL);
        tx.gas_limit = 21000;
        tx.to        = make_filled<eth::codec::Address>(0xCC);
        tx.value     = intx::uint256(100);
        tx.v         = intx::uint256(27);
        tx.r         = intx::uint256(1);
        tx.s         = intx::uint256(2);
        body.transactions.push_back(tx);
    }
    original.bodies.push_back(body);

    auto encoded = eth::protocol::encode_block_bodies(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_block_bodies(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& r = decoded.value();
    ASSERT_TRUE(r.request_id.has_value());
    EXPECT_EQ(r.request_id.value(), original.request_id.value());
    ASSERT_EQ(r.bodies.size(), 1u);
    ASSERT_EQ(r.bodies[0].transactions.size(), 1u);
    EXPECT_EQ(r.bodies[0].transactions[0].nonce, 5u);
}

// ============================================================================
// NewBlock round-trip
// ============================================================================

TEST(NewBlockTest, NewBlockRoundtrip)
{
    eth::NewBlockMessage original;
    original.header.parent_hash = make_filled<eth::Hash256>(0x01);
    original.header.number      = 19000000;
    original.header.gas_limit   = 30000000;
    original.header.gas_used    = 15000000;
    original.header.timestamp   = 1700000000;
    original.header.mix_hash    = make_filled<eth::Hash256>(0x0A);
    original.header.ommers_hash = make_filled<eth::Hash256>(0x0B);
    original.header.beneficiary = make_filled<eth::Address>(0x0C);
    original.header.state_root  = make_filled<eth::Hash256>(0x0D);
    original.header.transactions_root = make_filled<eth::Hash256>(0x0E);
    original.header.receipts_root     = make_filled<eth::Hash256>(0x0F);
    original.header.difficulty        = intx::uint256(0);
    original.header.base_fee_per_gas  = intx::uint256(7);

    {
        eth::codec::Transaction tx;
        tx.type                    = eth::codec::TransactionType::kDynamicFee;
        tx.chain_id                = 1;
        tx.nonce                   = 0;
        tx.max_priority_fee_per_gas = intx::uint256(1000000000ULL);
        tx.max_fee_per_gas          = intx::uint256(20000000000ULL);
        tx.gas_limit                = 21000;
        tx.to                       = make_filled<eth::codec::Address>(0xEE);
        tx.value                    = intx::uint256(1);
        tx.v                        = intx::uint256(0);
        tx.r                        = intx::uint256(10);
        tx.s                        = intx::uint256(11);
        original.transactions.push_back(tx);
    }

    original.total_difficulty = intx::uint256(0xFFFFFFFFFFFFFFFFULL);

    auto encoded = eth::protocol::encode_new_block(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_new_block(
        rlp::ByteView(encoded.value().data(), encoded.value().size()));
    if (!decoded.has_value())
    {
        std::ofstream f("/tmp/newblock_error.txt");
        f << "decode_new_block error code: " << static_cast<int>(decoded.error()) << "\n";
        f.close();
    }
    ASSERT_TRUE(decoded.has_value());

    const auto& r = decoded.value();
    EXPECT_EQ(r.header.number, original.header.number);
    EXPECT_EQ(r.header.gas_limit, original.header.gas_limit);
    EXPECT_EQ(r.total_difficulty, original.total_difficulty);
    ASSERT_EQ(r.transactions.size(), 1u);
    EXPECT_EQ(r.transactions[0].nonce, original.transactions[0].nonce);
    EXPECT_EQ(r.transactions[0].type, eth::codec::TransactionType::kDynamicFee);
}

