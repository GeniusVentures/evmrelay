// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/messages.hpp>
#include <algorithm>

namespace {

template <typename Array>
Array make_filled(uint8_t seed) {
    Array value{};
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

static std::vector<uint8_t> from_hex(std::string_view hex) {
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// go-ethereum ETH/69 wire-format vector tests
//
// go-ethereum StatusPacket (eth/69):
//   type StatusPacket69 struct {
//       ProtocolVersion uint32
//       NetworkID       uint64
//       Genesis         common.Hash   // 32 bytes
//       ForkID          forkid.ID     // [Hash[4], Next uint64] as list
//       EarliestBlock   uint64
//       LatestBlock     uint64
//       LatestBlockHash common.Hash   // 32 bytes
//   }
//
// Vector: ProtocolVersion=69, NetworkID=11155111 (Sepolia),
//         Genesis=32×0x00, ForkID={Hash=[0xf0,0xfb,0x06,0xe5], Next=0},
//         EarliestBlock=0, LatestBlock=1000, LatestBlockHash=32×0x00
//
// Computed with Python rlp_encode; matches go-ethereum RLP struct encoding.
// ---------------------------------------------------------------------------
static constexpr std::string_view kStatusEth69WireHex =
    "f852"
    "45"                // ProtocolVersion = 69
    "83aa36a7"          // NetworkID = 11155111
    "a0" "0000000000000000000000000000000000000000000000000000000000000000"  // Genesis
    "c6" "84f0fb06e5" "80"  // ForkID: [hash=f0fb06e5, next=0]
    "80"                // EarliestBlock = 0
    "8203e8"            // LatestBlock = 1000
    "a0" "0000000000000000000000000000000000000000000000000000000000000000"; // LatestBlockHash

TEST(EthMessagesGoEthVectors, StatusEth69DecodeFromWireBytes) {
    auto wire = from_hex(kStatusEth69WireHex);
    auto result = eth::protocol::decode_status(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_status() failed on go-ethereum ETH/69 wire bytes";

    const auto* msg69 = std::get_if<eth::StatusMessage69>(&result.value());
    ASSERT_NE(msg69, nullptr) << "Expected StatusMessage69 variant";
    EXPECT_EQ(msg69->protocol_version, 69u);
    EXPECT_EQ(msg69->network_id, 11155111u);
    eth::Hash256 zero_hash{};
    EXPECT_EQ(msg69->genesis_hash, zero_hash);
    EXPECT_EQ(msg69->fork_id.fork_hash[0], 0xf0u);
    EXPECT_EQ(msg69->fork_id.fork_hash[1], 0xfbu);
    EXPECT_EQ(msg69->fork_id.fork_hash[2], 0x06u);
    EXPECT_EQ(msg69->fork_id.fork_hash[3], 0xe5u);
    EXPECT_EQ(msg69->fork_id.next_fork, 0u);
    EXPECT_EQ(msg69->earliest_block, 0u);
    EXPECT_EQ(msg69->latest_block, 1000u);
    EXPECT_EQ(msg69->latest_block_hash, zero_hash);
}

TEST(EthMessagesGoEthVectors, StatusEth69EncodeMatchesWireFormat) {
    eth::StatusMessage69 msg69;
    msg69.protocol_version = 69;
    msg69.network_id = 11155111;
    msg69.genesis_hash.fill(0x00);
    msg69.fork_id.fork_hash = {0xf0, 0xfb, 0x06, 0xe5};
    msg69.fork_id.next_fork = 0;
    msg69.earliest_block = 0;
    msg69.latest_block = 1000;
    msg69.latest_block_hash.fill(0x00);

    eth::StatusMessage status = msg69;
    auto result = eth::protocol::encode_status(status);
    ASSERT_TRUE(result.has_value()) << "encode_status() failed";

    auto expected = from_hex(kStatusEth69WireHex);
    EXPECT_EQ(result.value(), expected)
        << "Encoded ETH/69 Status does not match go-ethereum wire format.\n"
        << "  got " << result.value().size() << " bytes, expected " << expected.size();
}

TEST(EthMessagesGoEthVectors, StatusEth69RoundTripPreservesWireFormat) {
    auto wire = from_hex(kStatusEth69WireHex);
    auto decoded = eth::protocol::decode_status(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(decoded.has_value());

    auto reencoded = eth::protocol::encode_status(decoded.value());
    ASSERT_TRUE(reencoded.has_value());
    EXPECT_EQ(reencoded.value(), wire);
}


TEST(EthMessagesTest, StatusRoundtrip) {
    eth::StatusMessage69 original;
    original.protocol_version = 69;
    original.network_id = 11155111;
    original.genesis_hash = make_filled<eth::Hash256>(0x20);
    original.fork_id.fork_hash = make_filled<std::array<uint8_t, 4>>(0x01);
    original.fork_id.next_fork = 987654;
    original.earliest_block = 0;
    original.latest_block = 5000000;
    original.latest_block_hash = make_filled<eth::Hash256>(0x10);

    eth::StatusMessage wrapped = original;
    auto encoded = eth::protocol::encode_status(wrapped);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_status(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto* result = std::get_if<eth::StatusMessage69>(&decoded.value());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->protocol_version, original.protocol_version);
    EXPECT_EQ(result->network_id, original.network_id);
    EXPECT_EQ(result->genesis_hash, original.genesis_hash);
    EXPECT_EQ(result->fork_id.fork_hash, original.fork_id.fork_hash);
    EXPECT_EQ(result->fork_id.next_fork, original.fork_id.next_fork);
    EXPECT_EQ(result->earliest_block, original.earliest_block);
    EXPECT_EQ(result->latest_block, original.latest_block);
    EXPECT_EQ(result->latest_block_hash, original.latest_block_hash);
}

// ---------------------------------------------------------------------------
// ETH/68 tests — wire: [version, networkid, td, blockhash, genesis, forkid]
// ---------------------------------------------------------------------------

TEST(EthMessagesEth68, StatusEth68DecodeFromWireBytes) {
    eth::Hash256 blockhash{};
    blockhash.fill(0xAA);
    eth::Hash256 genesis{};
    std::array<uint8_t, 4> fork_hash = {0xed, 0x88, 0xb5, 0xfd};

    rlp::RlpEncoder enc;
    (void)enc.BeginList();
    (void)enc.add(uint8_t{68});
    (void)enc.add(uint64_t{11155111});
    (void)enc.add(intx::uint256{0});
    (void)enc.add(rlp::ByteView(blockhash.data(), 32));
    (void)enc.add(rlp::ByteView(genesis.data(), 32));
    (void)enc.BeginList();
    (void)enc.add(rlp::ByteView(fork_hash.data(), 4));
    (void)enc.add(uint64_t{0});
    (void)enc.EndList();
    (void)enc.EndList();

    auto bytes_result = enc.GetBytes();
    ASSERT_TRUE(bytes_result.has_value());
    auto wire = std::vector<uint8_t>(bytes_result.value()->begin(), bytes_result.value()->end());

    auto result = eth::protocol::decode_status(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_status() failed on ETH/68 wire bytes";

    const auto* msg68 = std::get_if<eth::StatusMessage68>(&result.value());
    ASSERT_NE(msg68, nullptr) << "Expected StatusMessage68 variant";
    EXPECT_EQ(msg68->protocol_version, 68u);
    EXPECT_EQ(msg68->network_id, 11155111u);
    EXPECT_EQ(msg68->td, intx::uint256{0});
    EXPECT_EQ(msg68->blockhash, blockhash);
    eth::Hash256 zero_hash{};
    EXPECT_EQ(msg68->genesis_hash, zero_hash);
    EXPECT_EQ(msg68->fork_id.fork_hash[0], 0xedu);
    EXPECT_EQ(msg68->fork_id.fork_hash[1], 0x88u);
    EXPECT_EQ(msg68->fork_id.fork_hash[2], 0xb5u);
    EXPECT_EQ(msg68->fork_id.fork_hash[3], 0xfdu);
    EXPECT_EQ(msg68->fork_id.next_fork, 0u);
}

TEST(EthMessagesEth68, StatusEth68RoundTrip) {
    eth::StatusMessage68 original;
    original.protocol_version = 68;
    original.network_id = 11155111;
    original.td = intx::uint256{0x1234abcd};
    original.blockhash = make_filled<eth::Hash256>(0xBB);
    original.genesis_hash = make_filled<eth::Hash256>(0x00);
    original.fork_id.fork_hash = {0xed, 0x88, 0xb5, 0xfd};
    original.fork_id.next_fork = 0;

    eth::StatusMessage wrapped = original;
    auto encoded = eth::protocol::encode_status(wrapped);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_status(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto* result = std::get_if<eth::StatusMessage68>(&decoded.value());
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->protocol_version, original.protocol_version);
    EXPECT_EQ(result->network_id, original.network_id);
    EXPECT_EQ(result->td, original.td);
    EXPECT_EQ(result->blockhash, original.blockhash);
    EXPECT_EQ(result->genesis_hash, original.genesis_hash);
    EXPECT_EQ(result->fork_id.fork_hash, original.fork_id.fork_hash);
    EXPECT_EQ(result->fork_id.next_fork, original.fork_id.next_fork);
}

TEST(EthMessagesEth68, StatusEth66Through68RoundTripPreservesProtocolVersion) {
    const std::array<uint8_t, 3> kSupportedVersions{ 66U, 67U, 68U };

    for (const uint8_t version : kSupportedVersions) {
        eth::StatusMessage68 original;
        original.protocol_version = version;
        original.network_id = 11155111;
        original.td = intx::uint256{0x42};
        original.blockhash = make_filled<eth::Hash256>(static_cast<uint8_t>(0xA0U + version));
        original.genesis_hash = make_filled<eth::Hash256>(static_cast<uint8_t>(0x10U + version));
        original.fork_id.fork_hash = {0xed, 0x88, 0xb5, 0xfd};
        original.fork_id.next_fork = 0U;

        eth::StatusMessage wrapped = original;
        auto encoded = eth::protocol::encode_status(wrapped);
        ASSERT_TRUE(encoded.has_value()) << "encode_status failed for ETH/" << static_cast<int>(version);

        auto decoded = eth::protocol::decode_status(rlp::ByteView(encoded.value().data(), encoded.value().size()));
        ASSERT_TRUE(decoded.has_value()) << "decode_status failed for ETH/" << static_cast<int>(version);

        const auto* result = std::get_if<eth::StatusMessage68>(&decoded.value());
        ASSERT_NE(result, nullptr) << "Expected StatusMessage68 variant for ETH/" << static_cast<int>(version);
        EXPECT_EQ(result->protocol_version, version);
        EXPECT_EQ(result->network_id, original.network_id);
        EXPECT_EQ(result->td, original.td);
        EXPECT_EQ(result->blockhash, original.blockhash);
        EXPECT_EQ(result->genesis_hash, original.genesis_hash);
        EXPECT_EQ(result->fork_id.fork_hash, original.fork_id.fork_hash);
        EXPECT_EQ(result->fork_id.next_fork, original.fork_id.next_fork);
    }
}

TEST(EthMessagesEth68, StatusEth68ValidateCommonFields) {
    eth::StatusMessage68 msg68;
    msg68.protocol_version = 68;
    msg68.network_id = 11155111;
    msg68.genesis_hash = make_filled<eth::Hash256>(0xAB);
    msg68.fork_id.fork_hash = {0x01, 0x02, 0x03, 0x04};
    msg68.fork_id.next_fork = 42;

    eth::StatusMessage status = msg68;
    const auto common = eth::get_common_fields(status);
    EXPECT_EQ(common.protocol_version, 68u);
    EXPECT_EQ(common.network_id, 11155111u);
    EXPECT_EQ(common.genesis_hash, msg68.genesis_hash);
    EXPECT_EQ(common.fork_id.fork_hash, msg68.fork_id.fork_hash);
    EXPECT_EQ(common.fork_id.next_fork, 42u);
}

TEST(EthMessagesEth68, StatusEth69ValidateCommonFields) {
    eth::StatusMessage69 msg69;
    msg69.protocol_version = 69;
    msg69.network_id = 1;
    msg69.genesis_hash = make_filled<eth::Hash256>(0xCD);
    msg69.fork_id.fork_hash = {0x05, 0x06, 0x07, 0x08};
    msg69.fork_id.next_fork = 99;

    eth::StatusMessage status = msg69;
    const auto common = eth::get_common_fields(status);
    EXPECT_EQ(common.protocol_version, 69u);
    EXPECT_EQ(common.network_id, 1u);
    EXPECT_EQ(common.genesis_hash, msg69.genesis_hash);
    EXPECT_EQ(common.fork_id.fork_hash, msg69.fork_id.fork_hash);
    EXPECT_EQ(common.fork_id.next_fork, 99u);
}

TEST(EthMessagesTest, NewBlockHashesRoundtrip) {
    eth::NewBlockHashesMessage original;
    original.entries.push_back({make_filled<eth::Hash256>(0x01), 123});
    original.entries.push_back({make_filled<eth::Hash256>(0x02), 456});

    auto encoded = eth::protocol::encode_new_block_hashes(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_new_block_hashes(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_EQ(result.entries.size(), original.entries.size());
    EXPECT_EQ(result.entries[0].hash, original.entries[0].hash);
    EXPECT_EQ(result.entries[0].number, original.entries[0].number);
    EXPECT_EQ(result.entries[1].hash, original.entries[1].hash);
    EXPECT_EQ(result.entries[1].number, original.entries[1].number);
}

TEST(EthMessagesTest, NewPooledTransactionHashesRoundtrip) {
    eth::NewPooledTransactionHashesMessage original;
    original.hashes.push_back(make_filled<eth::Hash256>(0xA0));
    original.hashes.push_back(make_filled<eth::Hash256>(0xB0));

    auto encoded = eth::protocol::encode_new_pooled_tx_hashes(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_new_pooled_tx_hashes(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_EQ(result.hashes.size(), original.hashes.size());
    EXPECT_EQ(result.hashes[0], original.hashes[0]);
    EXPECT_EQ(result.hashes[1], original.hashes[1]);
}

TEST(EthMessagesTest, GetBlockHeadersRoundtripByHash) {
    eth::GetBlockHeadersMessage original;
    original.start_hash = make_filled<eth::Hash256>(0x11);
    original.max_headers = 128;
    original.skip = 2;
    original.reverse = true;

    auto encoded = eth::protocol::encode_get_block_headers(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_get_block_headers(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.start_hash.has_value());
    EXPECT_EQ(result.start_hash.value(), original.start_hash.value());
    EXPECT_FALSE(result.start_number.has_value());
    EXPECT_EQ(result.max_headers, original.max_headers);
    EXPECT_EQ(result.skip, original.skip);
    EXPECT_EQ(result.reverse, original.reverse);
}

TEST(EthMessagesTest, GetBlockHeadersRoundtripByNumber) {
    eth::GetBlockHeadersMessage original;
    original.start_number = 900;
    original.max_headers = 64;
    original.skip = 0;
    original.reverse = false;

    auto encoded = eth::protocol::encode_get_block_headers(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_get_block_headers(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.start_number.has_value());
    EXPECT_EQ(result.start_number.value(), original.start_number.value());
    EXPECT_FALSE(result.start_hash.has_value());
    EXPECT_EQ(result.max_headers, original.max_headers);
    EXPECT_EQ(result.skip, original.skip);
    EXPECT_EQ(result.reverse, original.reverse);
}

TEST(EthMessagesTest, GetBlockHeadersRoundtripEth66Envelope) {
    eth::GetBlockHeadersMessage original;
    original.request_id = 42;
    original.start_hash = make_filled<eth::Hash256>(0x55);
    original.max_headers = 16;
    original.skip = 1;
    original.reverse = false;

    auto encoded = eth::protocol::encode_get_block_headers(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_get_block_headers(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.request_id.has_value());
    EXPECT_EQ(result.request_id.value(), original.request_id.value());
    ASSERT_TRUE(result.start_hash.has_value());
    EXPECT_EQ(result.start_hash.value(), original.start_hash.value());
    EXPECT_EQ(result.max_headers, original.max_headers);
    EXPECT_EQ(result.skip, original.skip);
    EXPECT_EQ(result.reverse, original.reverse);
}

TEST(EthMessagesTest, BlockHeadersRoundtripEth66Envelope) {
    eth::BlockHeadersMessage original;
    original.request_id = 88;

    eth::codec::BlockHeader header;
    header.parent_hash = make_filled<eth::Hash256>(0x01);
    header.ommers_hash = make_filled<eth::Hash256>(0x02);
    header.beneficiary = make_filled<eth::Address>(0x03);
    header.state_root = make_filled<eth::Hash256>(0x04);
    header.transactions_root = make_filled<eth::Hash256>(0x05);
    header.receipts_root = make_filled<eth::Hash256>(0x06);
    header.logs_bloom = make_filled<eth::Bloom>(0x07);
    header.difficulty = intx::uint256(123);
    header.number = 900;
    header.gas_limit = 30000000;
    header.gas_used = 21000;
    header.timestamp = 1700000000;
    header.extra_data = {0xab, 0xcd};
    header.mix_hash = make_filled<eth::Hash256>(0x08);
    header.nonce = make_filled<std::array<uint8_t, 8>>(0x09);
    header.base_fee_per_gas = intx::uint256(100);
    original.headers.push_back(header);

    auto encoded = eth::protocol::encode_block_headers(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_block_headers(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.request_id.has_value());
    EXPECT_EQ(result.request_id.value(), original.request_id.value());
    ASSERT_EQ(result.headers.size(), 1u);
    EXPECT_EQ(result.headers[0].parent_hash, header.parent_hash);
    EXPECT_EQ(result.headers[0].number, header.number);
}

TEST(EthMessagesTest, GetReceiptsRoundtripEth66Envelope) {
    eth::GetReceiptsMessage original;
    original.request_id = 1001;
    original.block_hashes.push_back(make_filled<eth::Hash256>(0x21));
    original.block_hashes.push_back(make_filled<eth::Hash256>(0x31));

    auto encoded = eth::protocol::encode_get_receipts(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_get_receipts(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.request_id.has_value());
    EXPECT_EQ(result.request_id.value(), original.request_id.value());
    EXPECT_EQ(result.block_hashes, original.block_hashes);
}

TEST(EthMessagesTest, ReceiptsRoundtripEth66Envelope) {
    eth::ReceiptsMessage original;
    original.request_id = 1002;

    eth::codec::Receipt receipt;
    receipt.status = true;
    receipt.cumulative_gas_used = intx::uint256(21000);
    receipt.bloom = make_filled<eth::Bloom>(0x40);

    eth::codec::LogEntry log;
    log.address = make_filled<eth::Address>(0x50);
    log.topics.push_back(make_filled<eth::Hash256>(0x60));
    log.data = {0xaa, 0xbb};
    receipt.logs.push_back(log);

    original.receipts.push_back({receipt});

    auto encoded = eth::protocol::encode_receipts(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_receipts(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.request_id.has_value());
    EXPECT_EQ(result.request_id.value(), original.request_id.value());
    ASSERT_EQ(result.receipts.size(), 1u);
    ASSERT_EQ(result.receipts[0].size(), 1u);
    ASSERT_TRUE(result.receipts[0][0].status.has_value());
    EXPECT_TRUE(result.receipts[0][0].status.value());
    EXPECT_EQ(result.receipts[0][0].logs.size(), 1u);
    EXPECT_EQ(result.receipts[0][0].logs[0].address, log.address);
}

TEST(EthMessagesTest, GetPooledTransactionsRoundtripEth66Envelope) {
    eth::GetPooledTransactionsMessage original;
    original.request_id = 2001;
    original.transaction_hashes.push_back(make_filled<eth::Hash256>(0x71));
    original.transaction_hashes.push_back(make_filled<eth::Hash256>(0x81));

    auto encoded = eth::protocol::encode_get_pooled_transactions(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_get_pooled_transactions(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.request_id.has_value());
    EXPECT_EQ(result.request_id.value(), original.request_id.value());
    EXPECT_EQ(result.transaction_hashes, original.transaction_hashes);
}

TEST(EthMessagesTest, PooledTransactionsRoundtripEth66Envelope) {
    rlp::RlpEncoder tx1_encoder;
    ASSERT_TRUE(tx1_encoder.BeginList().has_value());
    ASSERT_TRUE(tx1_encoder.add(static_cast<uint8_t>(0x01)).has_value());
    ASSERT_TRUE(tx1_encoder.add(static_cast<uint8_t>(0x02)).has_value());
    ASSERT_TRUE(tx1_encoder.EndList().has_value());
    auto tx1_bytes = tx1_encoder.GetBytes();
    ASSERT_TRUE(tx1_bytes.has_value());

    rlp::RlpEncoder tx2_encoder;
    ASSERT_TRUE(tx2_encoder.BeginList().has_value());
    ASSERT_TRUE(tx2_encoder.add(static_cast<uint8_t>(0x03)).has_value());
    ASSERT_TRUE(tx2_encoder.EndList().has_value());
    auto tx2_bytes = tx2_encoder.GetBytes();
    ASSERT_TRUE(tx2_bytes.has_value());

    eth::PooledTransactionsMessage original;
    original.request_id = 2002;
    original.encoded_transactions.emplace_back(tx1_bytes.value()->begin(), tx1_bytes.value()->end());
    original.encoded_transactions.emplace_back(tx2_bytes.value()->begin(), tx2_bytes.value()->end());

    auto encoded = eth::protocol::encode_pooled_transactions(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_pooled_transactions(rlp::ByteView(encoded.value().data(), encoded.value().size()));
    ASSERT_TRUE(decoded.has_value());

    const auto& result = decoded.value();
    ASSERT_TRUE(result.request_id.has_value());
    EXPECT_EQ(result.request_id.value(), original.request_id.value());
    EXPECT_EQ(result.encoded_transactions, original.encoded_transactions);
}

// ---------------------------------------------------------------------------
// validate_status() tests — mirrors go-ethereum's testHandshake() cases from
// eth/protocols/eth/handshake_test.go exactly.
//
// go-ethereum readStatus() checks (in order):
//   1. NetworkID must match
//   2. Genesis must match
//   3. ForkID filter (not yet implemented — requires chain config)
//   4. For ETH/69: EarliestBlock <= LatestBlock (when LatestBlock != 0)
// ---------------------------------------------------------------------------

namespace {

/// @brief Build a valid Sepolia ETH/69 StatusMessage for use in validation tests.
eth::StatusMessage make_valid_status()
{
    eth::StatusMessage69 msg69;
    msg69.protocol_version  = 69;
    msg69.network_id        = 11155111;  // Sepolia
    msg69.genesis_hash.fill(0xAB);
    msg69.fork_id.fork_hash = {0xf0, 0xfb, 0x06, 0xe5};
    msg69.fork_id.next_fork = 0;
    msg69.earliest_block    = 0;
    msg69.latest_block      = 1'000'000;
    msg69.latest_block_hash.fill(0xCD);
    return eth::StatusMessage{msg69};
}

} // anonymous namespace

/// @brief A Status that matches all expected parameters must pass validation.
TEST(StatusValidationTest, ValidStatus_Passes)
{
    auto msg = make_valid_status();
    const auto common = eth::get_common_fields(msg);
    auto result = eth::protocol::validate_status(msg, 11155111, common.genesis_hash);
    EXPECT_TRUE(result.has_value()) << "Valid status must pass validation";
}

/// @brief NetworkID mismatch → kNetworkIDMismatch.
///        go-ethereum: errNetworkIDMismatch
TEST(StatusValidationTest, WrongNetworkID_Fails)
{
    auto msg = make_valid_status();
    std::get<eth::StatusMessage69>(msg).network_id = 1;  // mainnet, not Sepolia
    eth::Hash256 genesis;
    genesis.fill(0xAB);
    auto result = eth::protocol::validate_status(msg, 11155111, genesis);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kNetworkIDMismatch);
}

/// @brief Genesis mismatch → kGenesisMismatch.
///        go-ethereum: errGenesisMismatch
TEST(StatusValidationTest, WrongGenesis_Fails)
{
    auto msg = make_valid_status();
    eth::Hash256 our_genesis;
    our_genesis.fill(0x11);  // different from msg.genesis_hash (0xAB)
    auto result = eth::protocol::validate_status(msg, 11155111, our_genesis);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kGenesisMismatch);
}

/// @brief EarliestBlock > LatestBlock → kInvalidBlockRange.
///        go-ethereum: errInvalidBlockRange (via BlockRangeUpdatePacket.Validate())
TEST(StatusValidationTest, InvalidBlockRange_EarliestGreaterThanLatest_Fails)
{
    auto msg = make_valid_status();
    std::get<eth::StatusMessage69>(msg).earliest_block = 500'000;
    std::get<eth::StatusMessage69>(msg).latest_block   = 100'000;  // earlier than earliest
    const auto common = eth::get_common_fields(msg);
    auto result = eth::protocol::validate_status(msg, 11155111, common.genesis_hash);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), eth::StatusValidationError::kInvalidBlockRange);
}

/// @brief EarliestBlock == LatestBlock is valid (single-block range).
TEST(StatusValidationTest, EarliestEqualsLatest_Passes)
{
    auto msg = make_valid_status();
    std::get<eth::StatusMessage69>(msg).earliest_block = 500'000;
    std::get<eth::StatusMessage69>(msg).latest_block   = 500'000;
    const auto common = eth::get_common_fields(msg);
    auto result = eth::protocol::validate_status(msg, 11155111, common.genesis_hash);
    EXPECT_TRUE(result.has_value());
}

/// @brief LatestBlock == 0 skips range check (peer hasn't synced yet).
///        go-ethereum: BlockRangeUpdatePacket.Validate() allows EarliestBlock=0 LatestBlock=0
///        but rejects LatestBlockHash == common.Hash{}.  We only check numeric range here.
TEST(StatusValidationTest, LatestBlockZero_SkipsRangeCheck_Passes)
{
    auto msg = make_valid_status();
    std::get<eth::StatusMessage69>(msg).earliest_block = 0;
    std::get<eth::StatusMessage69>(msg).latest_block   = 0;
    const auto common = eth::get_common_fields(msg);
    auto result = eth::protocol::validate_status(msg, 11155111, common.genesis_hash);
    EXPECT_TRUE(result.has_value());
}

/// @brief NetworkID check fires before genesis check (order matches go-ethereum).
TEST(StatusValidationTest, NetworkIDCheckedBeforeGenesis)
{
    auto msg = make_valid_status();
    std::get<eth::StatusMessage69>(msg).network_id = 999;           // wrong network
    std::get<eth::StatusMessage69>(msg).genesis_hash.fill(0x00);    // also wrong genesis
    eth::Hash256 our_genesis;
    our_genesis.fill(0xAB);
    auto result = eth::protocol::validate_status(msg, 11155111, our_genesis);
    ASSERT_FALSE(result.has_value());
    // network_id is checked first — must not report genesis mismatch
    EXPECT_EQ(result.error(), eth::StatusValidationError::kNetworkIDMismatch);
}

// ---------------------------------------------------------------------------
// go-ethereum wire-format vector tests — ported from
// eth/protocols/eth/protocol_test.go::TestMessages and
// TestGetBlockHeadersDataEncodeDecode.
//
// These verify our encoders/decoders produce EXACTLY the same bytes as
// go-ethereum's RLP struct encoding — not just internal roundtrips.
//
// request_id = 1111 = 0x0457 in all envelope-wrapped messages.
// hashes[0] = common.HexToHash("deadc0de") → 28 zero bytes + 0xdeadc0de
// hashes[1] = common.HexToHash("feedbeef") → 28 zero bytes + 0xfeedbeef
// ---------------------------------------------------------------------------

namespace {

/// @brief Build a 32-byte Hash256 from a 4-byte big-endian suffix (rest zeros).
/// Matches go-ethereum common.HexToHash("deadc0de") etc.
eth::Hash256 hash_from_suffix(uint32_t suffix)
{
    eth::Hash256 h{};
    h[28] = static_cast<uint8_t>(suffix >> 24);
    h[29] = static_cast<uint8_t>(suffix >> 16);
    h[30] = static_cast<uint8_t>(suffix >>  8);
    h[31] = static_cast<uint8_t>(suffix >>  0);
    return h;
}

} // anonymous namespace

TEST(EthMessagesGoEthVectors, GetBlockHeadersByHash_MatchesWireFormat)
{
    eth::GetBlockHeadersMessage msg;
    msg.request_id   = 1111;
    msg.start_hash   = hash_from_suffix(0xdeadc0de);
    msg.max_headers  = 5;
    msg.skip         = 5;
    msg.reverse      = false;

    auto encoded = eth::protocol::encode_get_block_headers(msg);
    ASSERT_TRUE(encoded.has_value());

    auto expected = from_hex(
        "e8820457e4"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "050580");
    EXPECT_EQ(encoded.value(), expected)
        << "GetBlockHeaders-by-hash encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, GetBlockHeadersByNumber_MatchesWireFormat)
{
    eth::GetBlockHeadersMessage msg;
    msg.request_id   = 1111;
    msg.start_number = 9999;
    msg.max_headers  = 5;
    msg.skip         = 5;
    msg.reverse      = false;

    auto encoded = eth::protocol::encode_get_block_headers(msg);
    ASSERT_TRUE(encoded.has_value());

    auto expected = from_hex("ca820457c682270f050580");
    EXPECT_EQ(encoded.value(), expected)
        << "GetBlockHeaders-by-number encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, GetBlockBodies_MatchesWireFormat)
{
    eth::GetBlockBodiesMessage msg;
    msg.request_id   = 1111;
    msg.block_hashes = { hash_from_suffix(0xdeadc0de), hash_from_suffix(0xfeedbeef) };

    auto encoded = eth::protocol::encode_get_block_bodies(msg);
    ASSERT_TRUE(encoded.has_value());

    auto expected = from_hex(
        "f847820457f842"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "a000000000000000000000000000000000000000000000000000000000feedbeef");
    EXPECT_EQ(encoded.value(), expected)
        << "GetBlockBodies encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, GetReceipts_MatchesWireFormat)
{
    eth::GetReceiptsMessage msg;
    msg.request_id   = 1111;
    msg.block_hashes = { hash_from_suffix(0xdeadc0de), hash_from_suffix(0xfeedbeef) };

    auto encoded = eth::protocol::encode_get_receipts(msg);
    ASSERT_TRUE(encoded.has_value());

    auto expected = from_hex(
        "f847820457f842"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "a000000000000000000000000000000000000000000000000000000000feedbeef");
    EXPECT_EQ(encoded.value(), expected)
        << "GetReceipts encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, GetPooledTransactions_MatchesWireFormat)
{
    eth::GetPooledTransactionsMessage msg;
    msg.request_id         = 1111;
    msg.transaction_hashes = { hash_from_suffix(0xdeadc0de), hash_from_suffix(0xfeedbeef) };

    auto encoded = eth::protocol::encode_get_pooled_transactions(msg);
    ASSERT_TRUE(encoded.has_value());

    auto expected = from_hex(
        "f847820457f842"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "a000000000000000000000000000000000000000000000000000000000feedbeef");
    EXPECT_EQ(encoded.value(), expected)
        << "GetPooledTransactions encoding does not match go-ethereum wire format";
}

// go-ethereum encodes GetBlockHeadersPacket{1111, nil} (nil inner request pointer)
// as [1111, []] = c4820457c0.  Our flat struct always has the origin fields so
// the zero-value case encodes as [1111, [0,0,false]] = c7820457c3808080.
// The nil-pointer concept doesn't exist in our struct model; document actual output.
TEST(EthMessagesGoEthVectors, EmptyGetBlockHeaders_ZeroValue_EncodesWithZeroFields)
{
    eth::GetBlockHeadersMessage msg;
    msg.request_id  = 1111;
    msg.max_headers = 0;

    auto encoded = eth::protocol::encode_get_block_headers(msg);
    ASSERT_TRUE(encoded.has_value());
    // Our zero-value struct → [request_id, [0, 0, false]]
    EXPECT_EQ(encoded.value(), from_hex("c7820457c3808080"))
        << "Zero-value GetBlockHeaders should encode inner list with zero fields";
}

TEST(EthMessagesGoEthVectors, EmptyGetBlockBodies_MatchesWireFormat)
{
    eth::GetBlockBodiesMessage msg;
    msg.request_id = 1111;

    auto encoded = eth::protocol::encode_get_block_bodies(msg);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(encoded.value(), from_hex("c4820457c0"))
        << "Empty GetBlockBodies encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, EmptyGetReceipts_MatchesWireFormat)
{
    eth::GetReceiptsMessage msg;
    msg.request_id = 1111;

    auto encoded = eth::protocol::encode_get_receipts(msg);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(encoded.value(), from_hex("c4820457c0"))
        << "Empty GetReceipts encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, EmptyGetPooledTransactions_MatchesWireFormat)
{
    eth::GetPooledTransactionsMessage msg;
    msg.request_id = 1111;

    auto encoded = eth::protocol::encode_get_pooled_transactions(msg);
    ASSERT_TRUE(encoded.has_value());
    EXPECT_EQ(encoded.value(), from_hex("c4820457c0"))
        << "Empty GetPooledTransactions encoding does not match go-ethereum wire format";
}

TEST(EthMessagesGoEthVectors, GetBlockHeadersByHash_DecodeFromWireBytes)
{
    auto wire = from_hex(
        "e8820457e4"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "050580");

    auto result = eth::protocol::decode_get_block_headers(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_get_block_headers() failed on go-ethereum wire bytes";

    ASSERT_TRUE(result.value().request_id.has_value());
    EXPECT_EQ(result.value().request_id.value(), 1111u);
    ASSERT_TRUE(result.value().start_hash.has_value());
    EXPECT_FALSE(result.value().start_number.has_value());
    EXPECT_EQ(result.value().start_hash.value(), hash_from_suffix(0xdeadc0de));
    EXPECT_EQ(result.value().max_headers, 5u);
    EXPECT_EQ(result.value().skip, 5u);
    EXPECT_EQ(result.value().reverse, false);
}

TEST(EthMessagesGoEthVectors, GetBlockHeadersByNumber_DecodeFromWireBytes)
{
    auto wire = from_hex("ca820457c682270f050580");

    auto result = eth::protocol::decode_get_block_headers(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_get_block_headers() failed on go-ethereum wire bytes";

    ASSERT_TRUE(result.value().request_id.has_value());
    EXPECT_EQ(result.value().request_id.value(), 1111u);
    ASSERT_TRUE(result.value().start_number.has_value());
    EXPECT_FALSE(result.value().start_hash.has_value());
    EXPECT_EQ(result.value().start_number.value(), 9999u);
    EXPECT_EQ(result.value().max_headers, 5u);
    EXPECT_EQ(result.value().skip, 5u);
    EXPECT_EQ(result.value().reverse, false);
}

TEST(EthMessagesGoEthVectors, GetBlockBodies_DecodeFromWireBytes)
{
    auto wire = from_hex(
        "f847820457f842"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "a000000000000000000000000000000000000000000000000000000000feedbeef");

    auto result = eth::protocol::decode_get_block_bodies(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_get_block_bodies() failed on go-ethereum wire bytes";

    ASSERT_TRUE(result.value().request_id.has_value());
    EXPECT_EQ(result.value().request_id.value(), 1111u);
    ASSERT_EQ(result.value().block_hashes.size(), 2u);
    EXPECT_EQ(result.value().block_hashes[0], hash_from_suffix(0xdeadc0de));
    EXPECT_EQ(result.value().block_hashes[1], hash_from_suffix(0xfeedbeef));
}

TEST(EthMessagesGoEthVectors, GetReceipts_DecodeFromWireBytes)
{
    auto wire = from_hex(
        "f847820457f842"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "a000000000000000000000000000000000000000000000000000000000feedbeef");

    auto result = eth::protocol::decode_get_receipts(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_get_receipts() failed on go-ethereum wire bytes";

    ASSERT_TRUE(result.value().request_id.has_value());
    EXPECT_EQ(result.value().request_id.value(), 1111u);
    ASSERT_EQ(result.value().block_hashes.size(), 2u);
    EXPECT_EQ(result.value().block_hashes[0], hash_from_suffix(0xdeadc0de));
    EXPECT_EQ(result.value().block_hashes[1], hash_from_suffix(0xfeedbeef));
}

TEST(EthMessagesGoEthVectors, GetPooledTransactions_DecodeFromWireBytes)
{
    auto wire = from_hex(
        "f847820457f842"
        "a000000000000000000000000000000000000000000000000000000000deadc0de"
        "a000000000000000000000000000000000000000000000000000000000feedbeef");

    auto result = eth::protocol::decode_get_pooled_transactions(rlp::ByteView(wire.data(), wire.size()));
    ASSERT_TRUE(result.has_value()) << "decode_get_pooled_transactions() failed on go-ethereum wire bytes";

    ASSERT_TRUE(result.value().request_id.has_value());
    EXPECT_EQ(result.value().request_id.value(), 1111u);
    ASSERT_EQ(result.value().transaction_hashes.size(), 2u);
    EXPECT_EQ(result.value().transaction_hashes[0], hash_from_suffix(0xdeadc0de));
    EXPECT_EQ(result.value().transaction_hashes[1], hash_from_suffix(0xfeedbeef));
}

/// @brief Both start_hash and start_number set → encode must fail.
/// go: TestGetBlockHeadersDataEncodeDecode {fail: true, packet: {Hash: hash, Number: 314}}
TEST(EthMessagesGoEthVectors, GetBlockHeaders_BothHashAndNumber_EncodeFails)
{
    eth::GetBlockHeadersMessage msg;
    msg.start_hash   = hash_from_suffix(0xdeadc0de);
    msg.start_number = 314;
    msg.max_headers  = 5;

    auto encoded = eth::protocol::encode_get_block_headers(msg);
    EXPECT_FALSE(encoded.has_value())
        << "encode_get_block_headers() must fail when both start_hash and start_number are set";
}
