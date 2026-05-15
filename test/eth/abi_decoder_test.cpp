// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/abi_decoder.hpp>
#include <array>
#include <cstdint>

namespace {

// Helper: fill an array from a seed byte
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

// Helper: build a 32-byte ABI word with a uint256 value in big-endian
eth::codec::Hash256 make_uint256_word(uint64_t value)
{
    eth::codec::Hash256 word{};
    // The uint64 value occupies the rightmost 8 bytes (bytes [24..31]) in big-endian
    for (int i = 0; i < 8; ++i)
    {
        word[31 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return word;
}

// Helper: build a 32-byte ABI word with an address in the rightmost 20 bytes
eth::codec::Hash256 make_address_word(const eth::codec::Address& addr)
{
    eth::codec::Hash256 word{};
    std::copy(addr.begin(), addr.end(), word.begin() + 12);
    return word;
}

// Helper: append a big-endian uint256 (from uint64) as 32 bytes to a buffer
void append_uint256(eth::codec::ByteBuffer& buf, uint64_t value)
{
    for (int i = 0; i < 24; ++i) { buf.push_back(0); }
    for (int i = 7; i >= 0; --i)
    {
        buf.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

// Helper: append a 32-byte ABI word to a buffer
void append_word(eth::codec::ByteBuffer& buf, const eth::codec::Hash256& word)
{
    buf.insert(buf.end(), word.begin(), word.end());
}

} // namespace

// ============================================================================
// keccak256 / event_signature_hash
// ============================================================================

TEST(AbiDecoderTest, Keccak256EmptyString)
{
    // keccak256("") = c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470
    auto hash = eth::abi::keccak256(std::string{});

    EXPECT_EQ(hash[0],  0xc5);
    EXPECT_EQ(hash[1],  0xd2);
    EXPECT_EQ(hash[31], 0x70);
}

TEST(AbiDecoderTest, EventSignatureHashTransfer)
{
    // keccak256("Transfer(address,address,uint256)")
    // = 0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef
    auto hash = eth::abi::event_signature_hash("Transfer(address,address,uint256)");

    EXPECT_EQ(hash[0],  0xdd);
    EXPECT_EQ(hash[1],  0xf2);
    EXPECT_EQ(hash[2],  0x52);
    EXPECT_EQ(hash[3],  0xad);
    EXPECT_EQ(hash[31], 0xef);
}

TEST(AbiDecoderTest, EventSignatureHashApproval)
{
    // keccak256("Approval(address,address,uint256)")
    // = 0x8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925
    auto hash = eth::abi::event_signature_hash("Approval(address,address,uint256)");

    EXPECT_EQ(hash[0],  0x8c);
    EXPECT_EQ(hash[1],  0x5b);
    EXPECT_EQ(hash[31], 0x25);
}

TEST(AbiDecoderTest, EventSignatureHashTransfer721)
{
    // keccak256("Transfer(address,address,uint256)") — same as ERC-20
    // ERC-721 Transfer has same signature, same hash
    auto erc20  = eth::abi::event_signature_hash("Transfer(address,address,uint256)");
    auto erc721 = eth::abi::event_signature_hash("Transfer(address,address,uint256)");
    EXPECT_EQ(erc20, erc721);
}

// ============================================================================
// decode_abi_word — static typed decoding
// ============================================================================

TEST(AbiDecoderTest, DecodeWordAddress)
{
    const auto addr = make_filled<eth::codec::Address>(0xAA);
    const auto word = make_address_word(addr);

    auto result = eth::abi::decode_abi_word(word, eth::abi::AbiParamKind::kAddress);
    ASSERT_TRUE(result.has_value());

    const auto* decoded_addr = std::get_if<eth::codec::Address>(&result.value());
    ASSERT_NE(decoded_addr, nullptr);
    EXPECT_EQ(*decoded_addr, addr);
}

TEST(AbiDecoderTest, DecodeWordUint256)
{
    const auto word = make_uint256_word(1000000);

    auto result = eth::abi::decode_abi_word(word, eth::abi::AbiParamKind::kUint);
    ASSERT_TRUE(result.has_value());

    const auto* val = std::get_if<intx::uint256>(&result.value());
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, intx::uint256(1000000));
}

TEST(AbiDecoderTest, DecodeWordBytes32)
{
    const auto hash = make_filled<eth::codec::Hash256>(0xBB);

    auto result = eth::abi::decode_abi_word(hash, eth::abi::AbiParamKind::kBytes32);
    ASSERT_TRUE(result.has_value());

    const auto* val = std::get_if<eth::codec::Hash256>(&result.value());
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, hash);
}

TEST(AbiDecoderTest, DecodeWordBoolTrue)
{
    eth::codec::Hash256 word{};
    word[31] = 0x01;

    auto result = eth::abi::decode_abi_word(word, eth::abi::AbiParamKind::kBool);
    ASSERT_TRUE(result.has_value());

    const auto* val = std::get_if<bool>(&result.value());
    ASSERT_NE(val, nullptr);
    EXPECT_TRUE(*val);
}

TEST(AbiDecoderTest, DecodeWordBoolFalse)
{
    eth::codec::Hash256 word{};  // all zeros

    auto result = eth::abi::decode_abi_word(word, eth::abi::AbiParamKind::kBool);
    ASSERT_TRUE(result.has_value());

    const auto* val = std::get_if<bool>(&result.value());
    ASSERT_NE(val, nullptr);
    EXPECT_FALSE(*val);
}

// ============================================================================
// decode_log_data — non-indexed parameter decoding from data field
// ============================================================================

TEST(AbiDecoderTest, DecodeLogDataSingleUint256)
{
    // data = ABI-encoded uint256(500000000) — one 32-byte word
    eth::codec::ByteBuffer data;
    append_uint256(data, 500000000ULL);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kUint, false, "amount"}
    };

    auto result = eth::abi::decode_log_data(data, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1u);

    const auto* val = std::get_if<intx::uint256>(&result.value()[0]);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, intx::uint256(500000000ULL));
}

TEST(AbiDecoderTest, DecodeLogDataAddressAndUint256)
{
    // data = ABI-encoded (address, uint256)
    const auto addr = make_filled<eth::codec::Address>(0xCC);

    eth::codec::ByteBuffer data;
    append_word(data, make_address_word(addr));
    append_uint256(data, 42000000ULL);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, false, "recipient"},
        {eth::abi::AbiParamKind::kUint,    false, "amount"},
    };

    auto result = eth::abi::decode_log_data(data, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 2u);

    const auto* decoded_addr = std::get_if<eth::codec::Address>(&result.value()[0]);
    ASSERT_NE(decoded_addr, nullptr);
    EXPECT_EQ(*decoded_addr, addr);

    const auto* decoded_amount = std::get_if<intx::uint256>(&result.value()[1]);
    ASSERT_NE(decoded_amount, nullptr);
    EXPECT_EQ(*decoded_amount, intx::uint256(42000000ULL));
}

TEST(AbiDecoderTest, DecodeLogDataDynamicBytes)
{
    // ABI-encode bytes:  head = offset (32), tail = length (3) + "abc" + padding
    // Encoding:
    //   [0x00..0x20]  = offset = 32
    //   [0x03]        = length = 3
    //   [0x61 0x62 0x63 0x00..] = "abc" padded to 32 bytes
    eth::codec::ByteBuffer data;
    append_uint256(data, 32);                          // head: offset to tail
    append_uint256(data, 3);                           // tail: length
    data.push_back(0x61); data.push_back(0x62); data.push_back(0x63);  // "abc"
    while (data.size() % 32 != 0) { data.push_back(0x00); }           // pad to 32

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kBytes, false, "payload"}
    };

    auto result = eth::abi::decode_log_data(data, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1u);

    const auto* val = std::get_if<eth::codec::ByteBuffer>(&result.value()[0]);
    ASSERT_NE(val, nullptr);
    ASSERT_EQ(val->size(), 3u);
    EXPECT_EQ((*val)[0], 0x61);
    EXPECT_EQ((*val)[1], 0x62);
    EXPECT_EQ((*val)[2], 0x63);
}

TEST(AbiDecoderTest, DecodeLogDataDynamicString)
{
    // ABI-encode string "hello"
    eth::codec::ByteBuffer data;
    append_uint256(data, 32);   // offset to tail
    append_uint256(data, 5);    // length = 5
    data.push_back('h'); data.push_back('e'); data.push_back('l');
    data.push_back('l'); data.push_back('o');
    while (data.size() % 32 != 0) { data.push_back(0x00); }

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kString, false, "name"}
    };

    auto result = eth::abi::decode_log_data(data, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 1u);

    const auto* val = std::get_if<std::string>(&result.value()[0]);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "hello");
}

TEST(AbiDecoderTest, DecodeLogDataEmptyParams)
{
    eth::codec::ByteBuffer data;
    auto result = eth::abi::decode_log_data(data, {});
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().empty());
}

TEST(AbiDecoderTest, DecodeLogDataTooShort)
{
    // Only 16 bytes but one uint256 param needs 32
    eth::codec::ByteBuffer data(16, 0x00);
    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kUint, false, "x"}
    };

    auto result = eth::abi::decode_log_data(data, params);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// decode_log — full ERC-20 Transfer event
// ============================================================================

TEST(AbiDecoderTest, DecodeLogErc20Transfer)
{
    // Transfer(address indexed from, address indexed to, uint256 value)
    // topics[0] = keccak256("Transfer(address,address,uint256)")
    // topics[1] = from (address, indexed)
    // topics[2] = to   (address, indexed)
    // data      = value (uint256, non-indexed)

    const auto from_addr = make_filled<eth::codec::Address>(0x11);
    const auto to_addr   = make_filled<eth::codec::Address>(0x22);
    const uint64_t amount = 1000000000000000000ULL; // 1 ETH in wei

    eth::codec::LogEntry log;
    log.address = make_filled<eth::codec::Address>(0x99); // token contract

    // topic[0] = signature hash
    log.topics.push_back(eth::abi::event_signature_hash("Transfer(address,address,uint256)"));
    // topic[1] = from (address, ABI-encoded as 32-byte word)
    log.topics.push_back(make_address_word(from_addr));
    // topic[2] = to
    log.topics.push_back(make_address_word(to_addr));

    // data = uint256 amount
    append_uint256(log.data, amount);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "from"},
        {eth::abi::AbiParamKind::kAddress, true,  "to"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    auto result = eth::abi::decode_log(log, "Transfer(address,address,uint256)", params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 3u);

    // from
    const auto* decoded_from = std::get_if<eth::codec::Address>(&result.value()[0]);
    ASSERT_NE(decoded_from, nullptr);
    EXPECT_EQ(*decoded_from, from_addr);

    // to
    const auto* decoded_to = std::get_if<eth::codec::Address>(&result.value()[1]);
    ASSERT_NE(decoded_to, nullptr);
    EXPECT_EQ(*decoded_to, to_addr);

    // value
    const auto* decoded_val = std::get_if<intx::uint256>(&result.value()[2]);
    ASSERT_NE(decoded_val, nullptr);
    EXPECT_EQ(*decoded_val, intx::uint256(amount));
}

TEST(AbiDecoderTest, DecodeLogWrongSignatureRejected)
{
    eth::codec::LogEntry log;
    // Put the wrong signature hash in topic[0]
    log.topics.push_back(make_filled<eth::codec::Hash256>(0xFF));

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true, "from"},
    };

    auto result = eth::abi::decode_log(log, "Transfer(address,address,uint256)", params);
    EXPECT_FALSE(result.has_value());
}

TEST(AbiDecoderTest, DecodeLogNoTopicsRejected)
{
    eth::codec::LogEntry log;
    // no topics at all

    auto result = eth::abi::decode_log(log, "Transfer(address,address,uint256)", {});
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// decode_log — ERC-20 Approval event
// ============================================================================

TEST(AbiDecoderTest, DecodeLogErc20Approval)
{
    // Approval(address indexed owner, address indexed spender, uint256 value)
    const auto owner   = make_filled<eth::codec::Address>(0x33);
    const auto spender = make_filled<eth::codec::Address>(0x44);
    const uint64_t allowance = 0xFFFFFFFFULL;

    eth::codec::LogEntry log;
    log.address = make_filled<eth::codec::Address>(0x99);
    log.topics.push_back(eth::abi::event_signature_hash("Approval(address,address,uint256)"));
    log.topics.push_back(make_address_word(owner));
    log.topics.push_back(make_address_word(spender));
    append_uint256(log.data, allowance);

    std::vector<eth::abi::AbiParam> params = {
        {eth::abi::AbiParamKind::kAddress, true,  "owner"},
        {eth::abi::AbiParamKind::kAddress, true,  "spender"},
        {eth::abi::AbiParamKind::kUint,    false, "value"},
    };

    auto result = eth::abi::decode_log(log, "Approval(address,address,uint256)", params);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().size(), 3u);

    const auto* decoded_owner = std::get_if<eth::codec::Address>(&result.value()[0]);
    ASSERT_NE(decoded_owner, nullptr);
    EXPECT_EQ(*decoded_owner, owner);

    const auto* decoded_spender = std::get_if<eth::codec::Address>(&result.value()[1]);
    ASSERT_NE(decoded_spender, nullptr);
    EXPECT_EQ(*decoded_spender, spender);
}

