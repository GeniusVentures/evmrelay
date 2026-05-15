// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include <eth/messages.hpp>
#include <eth/eth_types.hpp>
#include <rlp/rlp_encoder.hpp>
#include <rlp/rlp_decoder.hpp>
#include <rlp/intx.hpp>
#include <rlpx/rlpx_types.hpp>
#include <rlpx/rlpx_error.hpp>
#include <array>
#include <string>
#include <optional>

namespace {

// Helper to create filled arrays for testing
template <typename Array>
Array make_filled(uint8_t seed) {
    Array value{};
    for (size_t i = 0; i < value.size(); ++i) {
        value[i] = static_cast<uint8_t>(seed + i);
    }
    return value;
}

// Helper to parse hex string to array
template <size_t N>
std::optional<std::array<uint8_t, N>> hex_to_array(const std::string& hex) {
    if (hex.size() != N * 2) {
        return std::nullopt;
    }
    std::array<uint8_t, N> result{};
    for (size_t i = 0; i < N; ++i) {
        const auto high = hex[i * 2];
        const auto low = hex[i * 2 + 1];

        uint8_t high_val = 0, low_val = 0;

        if (high >= '0' && high <= '9') high_val = high - '0';
        else if (high >= 'a' && high <= 'f') high_val = 10 + (high - 'a');
        else if (high >= 'A' && high <= 'F') high_val = 10 + (high - 'A');
        else return std::nullopt;

        if (low >= '0' && low <= '9') low_val = low - '0';
        else if (low >= 'a' && low <= 'f') low_val = 10 + (low - 'a');
        else if (low >= 'A' && low <= 'F') low_val = 10 + (low - 'A');
        else return std::nullopt;

        result[i] = (high_val << 4) | low_val;
    }
    return result;
}

// Helper to convert array to hex string
template <size_t N>
std::string array_to_hex(const std::array<uint8_t, N>& arr) {
    std::string result;
    result.reserve(N * 2);
    for (uint8_t byte : arr) {
        const char* hex_chars = "0123456789abcdef";
        result += hex_chars[byte >> 4];
        result += hex_chars[byte & 0x0f];
    }
    return result;
}

} // namespace

// ============================================================================
// ETH Protocol Message Tests
// ============================================================================

class EthProtocolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize any common test data
    }
};

// Test ETH Status message encoding/decoding
TEST_F(EthProtocolTest, StatusMessageRoundtrip) {
    std::cout << "\n[TEST] StatusMessageRoundtrip - Testing ETH Status message encode/decode\n";

    eth::StatusMessage69 original_msg{};
    original_msg.protocol_version = 69;
    original_msg.network_id = 1;
    original_msg.genesis_hash = make_filled<eth::Hash256>(0xbb);
    original_msg.fork_id.fork_hash = make_filled<std::array<uint8_t, 4>>(0xcc);
    original_msg.fork_id.next_fork = 20000000;
    original_msg.earliest_block = 0;
    original_msg.latest_block = 1000;
    original_msg.latest_block_hash = make_filled<eth::Hash256>(0xaa);
    eth::StatusMessage original{original_msg};

    std::cout << "  → Encoding Status message (protocol=" << (int)original_msg.protocol_version
              << ", network=" << original_msg.network_id << ")\n";

    // Encode
    auto encoded = eth::protocol::encode_status(original);
    ASSERT_TRUE(encoded.has_value()) << "Failed to encode Status message";

    std::cout << "  → Encoded to " << encoded.value().size() << " bytes\n";
    std::cout << "  → Decoding Status message\n";

    // Decode
    auto decoded = eth::protocol::decode_status(
        rlp::ByteView(encoded.value().data(), encoded.value().size())
    );
    ASSERT_TRUE(decoded.has_value()) << "Failed to decode Status message";

    // Verify all fields match
    const auto& result = decoded.value();
    const auto common = eth::get_common_fields(result);
    const auto& result69 = std::get<eth::StatusMessage69>(result);
    EXPECT_EQ(common.protocol_version, original_msg.protocol_version);
    EXPECT_EQ(common.network_id, original_msg.network_id);
    EXPECT_EQ(common.genesis_hash, original_msg.genesis_hash);
    EXPECT_EQ(common.fork_id.fork_hash, original_msg.fork_id.fork_hash);
    EXPECT_EQ(common.fork_id.next_fork, original_msg.fork_id.next_fork);
    EXPECT_EQ(result69.earliest_block, original_msg.earliest_block);
    EXPECT_EQ(result69.latest_block, original_msg.latest_block);
    EXPECT_EQ(result69.latest_block_hash, original_msg.latest_block_hash);

    std::cout << "  ✓ All fields match after roundtrip\n";
}

// Test Status message for different networks
TEST_F(EthProtocolTest, StatusMessageMultipleNetworks) {
    std::cout << "\n[TEST] StatusMessageMultipleNetworks - Testing Status for all supported chains\n";

    struct NetworkTest {
        uint64_t network_id;
        std::string name;
    };

    const NetworkTest networks[] = {
        {1, "Mainnet"},
        {11155111, "Sepolia"},
        {17000, "Holesky"},
        {137, "Polygon"},
        {56, "BSC"},
    };

    for (const auto& network : networks) {
        std::cout << "  → Testing " << network.name << " (network_id=" << network.network_id << ")\n";

        eth::StatusMessage69 msg69{};
        msg69.protocol_version = 69;
        msg69.network_id = network.network_id;
        msg69.genesis_hash = {};
        msg69.fork_id = {};
        msg69.earliest_block = 0;
        msg69.latest_block = 0;
        msg69.latest_block_hash = {};
        eth::StatusMessage msg{msg69};

        auto encoded = eth::protocol::encode_status(msg);
        ASSERT_TRUE(encoded.has_value())
            << "Failed to encode Status for " << network.name;

        auto decoded = eth::protocol::decode_status(
            rlp::ByteView(encoded.value().data(), encoded.value().size())
        );
        ASSERT_TRUE(decoded.has_value())
            << "Failed to decode Status for " << network.name;

        EXPECT_EQ(eth::get_common_fields(decoded.value()).network_id, network.network_id)
            << "Network ID mismatch for " << network.name;
    }

    std::cout << "  ✓ All " << sizeof(networks)/sizeof(networks[0]) << " networks tested successfully\n";
}

// Test NewBlockHashes message
TEST_F(EthProtocolTest, NewBlockHashesRoundtrip) {
    std::cout << "\n[TEST] NewBlockHashesRoundtrip - Testing NewBlockHashes message\n";

    eth::NewBlockHashesMessage original;
    original.entries.push_back({
        make_filled<eth::Hash256>(0x11),
        1000
    });
    original.entries.push_back({
        make_filled<eth::Hash256>(0x22),
        1001
    });
    original.entries.push_back({
        make_filled<eth::Hash256>(0x33),
        1002
    });

    std::cout << "  → Encoding " << original.entries.size() << " block hash entries\n";

    // Encode
    auto encoded = eth::protocol::encode_new_block_hashes(original);
    ASSERT_TRUE(encoded.has_value()) << "Failed to encode NewBlockHashes";

    std::cout << "  → Encoded to " << encoded.value().size() << " bytes\n";
    std::cout << "  → Decoding NewBlockHashes\n";

    // Decode
    auto decoded = eth::protocol::decode_new_block_hashes(
        rlp::ByteView(encoded.value().data(), encoded.value().size())
    );
    ASSERT_TRUE(decoded.has_value()) << "Failed to decode NewBlockHashes";

    // Verify entries
    const auto& result = decoded.value();
    EXPECT_EQ(result.entries.size(), original.entries.size());
    for (size_t i = 0; i < original.entries.size(); ++i) {
        EXPECT_EQ(result.entries[i].hash, original.entries[i].hash);
        EXPECT_EQ(result.entries[i].number, original.entries[i].number);
    }

    std::cout << "  ✓ All " << result.entries.size() << " entries verified\n";
}

// Test empty NewBlockHashes message
TEST_F(EthProtocolTest, NewBlockHashesEmpty) {
    std::cout << "\n[TEST] NewBlockHashesEmpty - Testing empty NewBlockHashes\n";

    eth::NewBlockHashesMessage original;
    // Empty entries

    auto encoded = eth::protocol::encode_new_block_hashes(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_new_block_hashes(
        rlp::ByteView(encoded.value().data(), encoded.value().size())
    );
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().entries.size(), 0);

    std::cout << "  ✓ Empty message handled correctly\n";
}

// Test large block numbers in NewBlockHashes
TEST_F(EthProtocolTest, NewBlockHashesLargeNumbers) {
    std::cout << "\n[TEST] NewBlockHashesLargeNumbers - Testing large block numbers\n";

    eth::NewBlockHashesMessage original;
    original.entries.push_back({
        make_filled<eth::Hash256>(0x99),
        18000000  // Large block number
    });

    std::cout << "  → Testing block number: 18000000\n";

    auto encoded = eth::protocol::encode_new_block_hashes(original);
    ASSERT_TRUE(encoded.has_value());

    auto decoded = eth::protocol::decode_new_block_hashes(
        rlp::ByteView(encoded.value().data(), encoded.value().size())
    );
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded.value().entries[0].number, 18000000);

    std::cout << "  ✓ Large block number handled correctly\n";
}

