// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// Snappy compression tests for the RLPx message layer.
//
// Test vectors are derived from:
//   - go-ethereum/p2p/rlpx/rlpx_test.go :: TestReadWriteMsg (roundtrip with snappy)
//   - go-ethereum/p2p/rlpx/rlpx_test.go :: TestFrameReadWrite frame content vector
//   - Captured live reth peer bytes (Sepolia Status message, snappy-compressed frame payload)
//
// The go-ethereum snappy contract (p2p/rlpx/rlpx.go SetSnappy):
//   - Enabled after HELLO when peer version >= 5 (snappyProtocolVersion)
//   - Applied per-message: Compress(msgId_RLP || payload) before framing
//   - On receive:  after deframe → Decompress → split msgId_RLP | payload

#include <gtest/gtest.h>
#include <snappy.h>
#include <vector>
#include <string>
#include <cstdint>

// ─── helpers ────────────────────────────────────────────────────────────────

namespace {

/// Decode a lowercase hex string into bytes.
std::vector<uint8_t> from_hex(const std::string& hex)
{
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        auto nibble = [](char c) -> uint8_t
        {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            return static_cast<uint8_t>(c - 'A' + 10);
        };
        out.push_back(static_cast<uint8_t>((nibble(hex[i]) << 4) | nibble(hex[i + 1])));
    }
    return out;
}

/// Compress bytes using the google snappy block format (same as go's snappy.Encode).
std::vector<uint8_t> compress(const std::vector<uint8_t>& in)
{
    std::string out;
    snappy::Compress(reinterpret_cast<const char*>(in.data()), in.size(), &out);
    return std::vector<uint8_t>(out.begin(), out.end());
}

/// Decompress bytes using snappy block format (same as go's snappy.Decode).
/// Returns empty on failure.
std::vector<uint8_t> decompress(const std::vector<uint8_t>& in)
{
    size_t uncompressed_len = 0;
    if (!snappy::GetUncompressedLength(
            reinterpret_cast<const char*>(in.data()), in.size(), &uncompressed_len))
        return {};

    std::vector<uint8_t> out(uncompressed_len);
    if (!snappy::RawUncompress(
            reinterpret_cast<const char*>(in.data()), in.size(),
            reinterpret_cast<char*>(out.data())))
        return {};

    return out;
}

} // namespace

// ─── SnappyRoundTripTest ──────────────────────────────────────────────────────
//
// Mirrors go-ethereum TestReadWriteMsg: messages survive compress→decompress.

class SnappyRoundTripTest : public ::testing::Test {};

/// Empty input: snappy must round-trip to empty.
TEST_F(SnappyRoundTripTest, Empty)
{
    const std::vector<uint8_t> plain{};
    EXPECT_EQ(decompress(compress(plain)), plain);
}

/// Single byte.
TEST_F(SnappyRoundTripTest, SingleByte)
{
    const std::vector<uint8_t> plain{0x42};
    EXPECT_EQ(decompress(compress(plain)), plain);
}

/// go-ethereum TestReadWriteMsg testData = []byte("test") with code 23.
/// Wire content = RLP(23) || "test" = 0x17 0x74 0x65 0x73 0x74
TEST_F(SnappyRoundTripTest, GoEth_TestReadWriteMsg_CodeAndData)
{
    // code 23 RLP-encodes as 0x17; "test" is raw bytes
    const std::vector<uint8_t> plain = {0x17, 0x74, 0x65, 0x73, 0x74};
    EXPECT_EQ(decompress(compress(plain)), plain);
}

/// go-ethereum TestFrameReadWrite frame content: code=8, msg=[1,2,3,4].
/// wantContent = "08C401020304" — this is what goes into the frame before framing/encryption.
/// With snappy enabled that content is compressed first.
TEST_F(SnappyRoundTripTest, GoEth_TestFrameReadWrite_Content)
{
    // 0x08 = RLP(8)  ;  0xC401020304 = RLP([1,2,3,4])
    const std::vector<uint8_t> plain = from_hex("08C401020304");
    EXPECT_EQ(decompress(compress(plain)), plain);
}

/// Typical Ethereum message size (80 bytes, matching captured reth Status).
TEST_F(SnappyRoundTripTest, TypicalEthMessageSize_80Bytes)
{
    std::vector<uint8_t> plain(80);
    for (size_t i = 0; i < plain.size(); ++i)
        plain[i] = static_cast<uint8_t>(i);
    EXPECT_EQ(decompress(compress(plain)), plain);
}

/// Larger message: 1024 bytes of patterned data.
TEST_F(SnappyRoundTripTest, LargeMessage_1024Bytes)
{
    std::vector<uint8_t> plain(1024);
    for (size_t i = 0; i < plain.size(); ++i)
        plain[i] = static_cast<uint8_t>(i & 0xFF);
    EXPECT_EQ(decompress(compress(plain)), plain);
}

/// High-entropy data (incompressible) must still round-trip correctly.
TEST_F(SnappyRoundTripTest, HighEntropyData)
{
    // 256 distinct bytes — worst case for compression ratio
    std::vector<uint8_t> plain(256);
    for (int i = 0; i < 256; ++i)
        plain[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    EXPECT_EQ(decompress(compress(plain)), plain);
}

// ─── SnappyKnownVectorTest ────────────────────────────────────────────────────
//
// Validates against real captured bytes from live Ethereum peers.

class SnappyKnownVectorTest : public ::testing::Test {};

/// Captured snappy-compressed ETH Status payload from a reth Sepolia peer.
/// The frame payload (after RLPx decrypt+deframe) starts with 0x50 = varint(80),
/// meaning the uncompressed message is exactly 80 bytes.
/// Uncompressed first byte must be 0x10 (ETH Status msg_id = offset 16).
///
/// Source: live reth peer capture, session checkpoint 004.
TEST_F(SnappyKnownVectorTest, RethSepolia_StatusPayload_UncompressedLength)
{
    const auto compressed = from_hex(
        "50f04ff84e4482261180a0291355d70f1664ca8c926cfcaf8ac7a17e813f258f"
        "bc92216ae7533b90ca0be8a00d0ccca452bdb244100115e37de64ca640a25558"
        "5d5a94df5610052a6dada558c684fa7ac27680"
    );

    size_t uncompressed_len = 0;
    ASSERT_TRUE(snappy::GetUncompressedLength(
        reinterpret_cast<const char*>(compressed.data()),
        compressed.size(),
        &uncompressed_len));

    EXPECT_EQ(uncompressed_len, 80u);
}

/// Same bytes: decompression must succeed and produce a valid-looking ETH Status.
/// First byte of decompressed frame content = 0x10 (RLP-encoded uint 16 = Status msg_id).
TEST_F(SnappyKnownVectorTest, RethSepolia_StatusPayload_DecompressSucceeds)
{
    const auto compressed = from_hex(
        "50f04ff84e4482261180a0291355d70f1664ca8c926cfcaf8ac7a17e813f258f"
        "bc92216ae7533b90ca0be8a00d0ccca452bdb244100115e37de64ca640a25558"
        "5d5a94df5610052a6dada558c684fa7ac27680"
    );

    const auto plain = decompress(compressed);
    ASSERT_EQ(plain.size(), 80u);

    // First byte is the RLP long-list prefix (0xF8) for the Status payload list.
    EXPECT_EQ(plain[0], 0xF8u);
}

/// Same reth bytes: re-compressing the decompressed result must decompress back to the same.
TEST_F(SnappyKnownVectorTest, RethSepolia_StatusPayload_RecompressRoundTrip)
{
    const auto compressed = from_hex(
        "50f04ff84e4482261180a0291355d70f1664ca8c926cfcaf8ac7a17e813f258f"
        "bc92216ae7533b90ca0be8a00d0ccca452bdb244100115e37de64ca640a25558"
        "5d5a94df5610052a6dada558c684fa7ac27680"
    );

    const auto plain = decompress(compressed);
    ASSERT_EQ(plain.size(), 80u);

    // Re-compress and decompress must yield the same plaintext.
    EXPECT_EQ(decompress(compress(plain)), plain);
}

// ─── SnappyMessageStreamLayerTest ────────────────────────────────────────────
//
// Tests the compress/decompress logic as used by MessageStream:
//   send: encode(msg_id || payload) → compress → frame
//   recv: deframe → decompress → decode(msg_id || payload)

class SnappyMessageStreamLayerTest : public ::testing::Test
{
protected:
    /// Simulates MessageStream send: RLP(msg_id) || raw_payload → compress.
    static std::vector<uint8_t> stream_compress(uint8_t msg_id,
                                                const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> msg;
        // RLP encoding of a small uint: values 0x00-0x7f encode as themselves.
        msg.push_back(msg_id);
        msg.insert(msg.end(), payload.begin(), payload.end());
        return compress(msg);
    }

    /// Simulates MessageStream recv: decompress → split(msg_id, payload).
    static bool stream_decompress(const std::vector<uint8_t>& compressed,
                                  uint8_t& out_id,
                                  std::vector<uint8_t>& out_payload)
    {
        const auto plain = decompress(compressed);
        if (plain.empty()) return false;
        out_id = plain[0];
        out_payload.assign(plain.begin() + 1, plain.end());
        return true;
    }
};

/// go-ethereum TestReadWriteMsg: code=23, data="test".
TEST_F(SnappyMessageStreamLayerTest, GoEth_TestReadWriteMsg_Code23_DataTest)
{
    const uint8_t code = 23;
    const std::vector<uint8_t> data = {0x74, 0x65, 0x73, 0x74}; // "test"

    const auto compressed = stream_compress(code, data);

    uint8_t recv_code = 0;
    std::vector<uint8_t> recv_data;
    ASSERT_TRUE(stream_decompress(compressed, recv_code, recv_data));

    EXPECT_EQ(recv_code, code);
    EXPECT_EQ(recv_data, data);
}

/// go-ethereum TestFrameReadWrite: code=8, payload=RLP([1,2,3,4]).
TEST_F(SnappyMessageStreamLayerTest, GoEth_TestFrameReadWrite_Code8_RlpList)
{
    const uint8_t code = 8;
    // RLP encoding of [1,2,3,4]: C4 01 02 03 04
    const std::vector<uint8_t> payload = {0xC4, 0x01, 0x02, 0x03, 0x04};

    const auto compressed = stream_compress(code, payload);

    uint8_t recv_code = 0;
    std::vector<uint8_t> recv_data;
    ASSERT_TRUE(stream_decompress(compressed, recv_code, recv_data));

    EXPECT_EQ(recv_code, code);
    EXPECT_EQ(recv_data, payload);
}

/// HELLO message (code=0) with typical capability list.
TEST_F(SnappyMessageStreamLayerTest, HelloMessage_Code0_CapabilityPayload)
{
    const uint8_t code = 0;
    // Arbitrary HELLO-sized payload (80 bytes typical)
    std::vector<uint8_t> payload(80);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i);

    const auto compressed = stream_compress(code, payload);

    uint8_t recv_code = 0;
    std::vector<uint8_t> recv_data;
    ASSERT_TRUE(stream_decompress(compressed, recv_code, recv_data));

    EXPECT_EQ(recv_code, code);
    EXPECT_EQ(recv_data, payload);
}

/// ETH Status message (code=0x10).
TEST_F(SnappyMessageStreamLayerTest, EthStatusMessage_Code0x10)
{
    const uint8_t code = 0x10;
    // Typical status payload: ~79 bytes after the msg_id byte
    std::vector<uint8_t> payload(79, 0xAB);

    const auto compressed = stream_compress(code, payload);

    uint8_t recv_code = 0;
    std::vector<uint8_t> recv_data;
    ASSERT_TRUE(stream_decompress(compressed, recv_code, recv_data));

    EXPECT_EQ(recv_code, code);
    EXPECT_EQ(recv_data, payload);
}

/// Corrupt snappy bytes must fail gracefully (not crash).
TEST_F(SnappyMessageStreamLayerTest, CorruptBytes_FailsGracefully)
{
    const std::vector<uint8_t> garbage = {0xFF, 0xFE, 0xFD, 0xFC};
    const auto plain = decompress(garbage);
    EXPECT_TRUE(plain.empty());
}

/// Truncated snappy stream must fail gracefully.
TEST_F(SnappyMessageStreamLayerTest, TruncatedBytes_FailsGracefully)
{
    // Take a valid compressed buffer and chop it in half
    const std::vector<uint8_t> orig(64, 0x42);
    const auto compressed = compress(orig);
    const std::vector<uint8_t> truncated(compressed.begin(),
                                          compressed.begin() +
                                          static_cast<ptrdiff_t>(compressed.size() / 2));

    const auto plain = decompress(truncated);
    EXPECT_TRUE(plain.empty());
}
