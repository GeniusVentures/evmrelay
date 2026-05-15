// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file discv5_client_test.cpp
/// @brief Unit tests for discv5_client lifecycle and receive-loop behaviour.

#include <discv5/discv5_client.hpp>
#include <discv5/discv5_constants.hpp>
#include <gtest/gtest.h>

#include <nil/crypto3/hash/algorithm/hash.hpp>
#include <nil/crypto3/hash/keccak.hpp>
#include <openssl/evp.h>

#include <array>
#include <algorithm>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>
#include <chrono>

namespace
{

/// @brief Build a minimal discv5Config using an ephemeral UDP port.
static discv5::discv5Config make_config()
{
    discv5::discv5Config config;
    config.bind_ip = "127.0.0.1";
    config.bind_port = 0U;
    config.query_interval_sec = 1U;
    return config;
}

/// @brief Build a config with one invalid enode bootstrap entry.
static discv5::discv5Config make_invalid_bootnode_config()
{
    discv5::discv5Config config = make_config();
    config.bootstrap_enrs.push_back(
        "enode://"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "1111111111111111111111111111111111111111111111111111111111111111"
        "@not-an-ip:30303");
    return config;
}

/// @brief Derive the 32-byte discv5 node address used for header masking.
static std::array<uint8_t, discv5::kKeccak256Bytes> derive_node_address(const discv5::NodeId& public_key)
{
    const auto hash_val =
        nil::crypto3::hash<nil::crypto3::hashes::keccak_1600<256>>(
            public_key.cbegin(), public_key.cend());
    return static_cast<std::array<uint8_t, discv5::kKeccak256Bytes>>(hash_val);
}

/// @brief Apply discv5 AES-128-CTR masking to the bytes after the IV.
static void apply_masking(
    const std::array<uint8_t, discv5::kKeccak256Bytes>& destination_node_addr,
    std::vector<uint8_t>& packet)
{
    std::array<uint8_t, discv5::kMaskingIvBytes> key{};
    std::copy_n(destination_node_addr.begin(), key.size(), key.begin());

    std::array<uint8_t, discv5::kMaskingIvBytes> iv{};
    std::copy_n(packet.begin(), iv.size(), iv.begin());

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    ASSERT_NE(ctx, nullptr) << "EVP_CIPHER_CTX_new() failed";

    ASSERT_EQ(EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, key.data(), iv.data()), 1)
        << "EVP_EncryptInit_ex() failed";

    int out_len = 0;
    ASSERT_EQ(
        EVP_EncryptUpdate(
            ctx,
            packet.data() + discv5::kMaskingIvBytes,
            &out_len,
            packet.data() + discv5::kMaskingIvBytes,
            static_cast<int>(packet.size() - discv5::kMaskingIvBytes)),
        1) << "EVP_EncryptUpdate() failed";

    EXPECT_EQ(out_len, static_cast<int>(packet.size() - discv5::kMaskingIvBytes));
    EVP_CIPHER_CTX_free(ctx);
}

/// @brief Append a big-endian uint16 to a byte vector.
static void append_u16_be(std::vector<uint8_t>& out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<uint8_t>(value & 0xFFU));
}

/// @brief Append a big-endian uint64 to a byte vector.
static void append_u64_be(std::vector<uint8_t>& out, uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFFU));
    }
}

/// @brief Build a synthetic masked WHOAREYOU packet for the local client identity.
static std::vector<uint8_t> make_whoareyou_packet(
    const discv5::NodeId& local_public_key,
    uint16_t auth_size = static_cast<uint16_t>(discv5::kWhoareyouAuthDataBytes))
{
    std::vector<uint8_t> packet;
    packet.reserve(discv5::kStaticPacketBytes + auth_size);

    for (size_t i = 0U; i < discv5::kMaskingIvBytes; ++i)
    {
        packet.push_back(static_cast<uint8_t>(0xA0U + i));
    }

    packet.insert(packet.end(), {'d', 'i', 's', 'c', 'v', '5'});
    append_u16_be(packet, discv5::kProtocolVersion);
    packet.push_back(discv5::kFlagWhoareyou);

    for (size_t i = 0U; i < discv5::kGcmNonceBytes; ++i)
    {
        packet.push_back(static_cast<uint8_t>(0x10U + i));
    }

    append_u16_be(packet, auth_size);

    for (size_t i = 0U; i < auth_size; ++i)
    {
        packet.push_back(0U);
    }

    if (auth_size >= discv5::kWhoareyouAuthDataBytes)
    {
        const size_t auth_offset = discv5::kStaticPacketBytes;
        for (size_t i = 0U; i < discv5::kWhoareyouIdNonceBytes; ++i)
        {
            packet[auth_offset + i] = static_cast<uint8_t>(0x20U + i);
        }

        std::vector<uint8_t> record_seq_bytes;
        append_u64_be(record_seq_bytes, 7U);
        std::copy(
            record_seq_bytes.begin(),
            record_seq_bytes.end(),
            packet.begin() + auth_offset + discv5::kWhoareyouIdNonceBytes);
    }

    const auto node_address = derive_node_address(local_public_key);
    apply_masking(node_address, packet);
    return packet;
}

/// @brief Send a UDP datagram to the provided localhost port.
static void send_udp_packet(uint16_t port, size_t size)
{
    boost::asio::io_context io;
    boost::asio::ip::udp::socket socket(io);
    boost::system::error_code ec;
    socket.open(boost::asio::ip::udp::v4(), ec);
    ASSERT_FALSE(ec) << "open() failed";

    const boost::asio::ip::udp::endpoint endpoint(
        boost::asio::ip::address_v4::loopback(),
        port);

    std::vector<uint8_t> buffer(size, 0x42U);
    const std::size_t sent = socket.send_to(boost::asio::buffer(buffer), endpoint, 0, ec);

    EXPECT_FALSE(ec) << "send_to() failed";
    EXPECT_EQ(sent, buffer.size()) << "send_to() wrote a short datagram";
}

/// @brief Send a UDP datagram with explicit payload bytes to the provided localhost port.
static void send_udp_packet_bytes(uint16_t port, const std::vector<uint8_t>& buffer)
{
    boost::asio::io_context io;
    boost::asio::ip::udp::socket socket(io);
    boost::system::error_code ec;
    socket.open(boost::asio::ip::udp::v4(), ec);
    ASSERT_FALSE(ec) << "open() failed";

    const boost::asio::ip::udp::endpoint endpoint(
        boost::asio::ip::address_v4::loopback(),
        port);

    const std::size_t sent = socket.send_to(boost::asio::buffer(buffer), endpoint, 0, ec);

    EXPECT_FALSE(ec) << "send_to() failed";
    EXPECT_EQ(sent, buffer.size()) << "send_to() wrote a short datagram";
}

} // namespace

/// @brief start() flips the running state and binds an ephemeral UDP port.
TEST(Discv5ClientTest, StartAndStopUpdateRunningState)
{
    boost::asio::io_context io;
    discv5::discv5_client client(io, make_config());

    EXPECT_FALSE(client.is_running());
    EXPECT_NE(client.bound_port(), 0U);

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value()) << "start() must succeed";

    EXPECT_TRUE(client.is_running());
    EXPECT_NE(client.bound_port(), 0U);

    client.stop();
    EXPECT_FALSE(client.is_running());

    client.stop();
    EXPECT_FALSE(client.is_running());
}

/// @brief A valid-sized UDP datagram is consumed by the receive loop.
TEST(Discv5ClientTest, ReceiveLoopCountsValidPacket)
{
    boost::asio::io_context io;
    discv5::discv5_client client(io, make_config());

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value()) << "start() must succeed";

    send_udp_packet(client.bound_port(), discv5::kMinPacketBytes);

    io.run_for(std::chrono::milliseconds(100));

    EXPECT_EQ(client.received_packet_count(), 1U);
    EXPECT_EQ(client.dropped_undersized_packet_count(), 0U);
    EXPECT_EQ(client.whoareyou_packet_count(), 0U);
    EXPECT_EQ(client.handshake_packet_count(), 0U);
    EXPECT_EQ(client.nodes_packet_count(), 0U);

    client.stop();
}

/// @brief An undersized UDP datagram is dropped before the packet handler path.
TEST(Discv5ClientTest, ReceiveLoopDropsUndersizedPacket)
{
    boost::asio::io_context io;
    discv5::discv5_client client(io, make_config());

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value()) << "start() must succeed";

    send_udp_packet(client.bound_port(), discv5::kMinPacketBytes - 1U);

    io.run_for(std::chrono::milliseconds(100));

    EXPECT_EQ(client.received_packet_count(), 0U);
    EXPECT_EQ(client.dropped_undersized_packet_count(), 1U);

    client.stop();
}

/// @brief An invalid bootstrap enode address increments the FINDNODE send-failure counter.
TEST(Discv5ClientTest, InvalidBootstrapAddressCountsSendFailure)
{
    boost::asio::io_context io;
    discv5::discv5_client client(io, make_invalid_bootnode_config());

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value()) << "start() must succeed";

    io.run_for(std::chrono::milliseconds(100));

    EXPECT_EQ(client.send_findnode_failure_count(), 1U);

    client.stop();
}

/// @brief An unsolicited masked WHOAREYOU packet is ignored by the pending-request gate.
TEST(Discv5ClientTest, ReceiveLoopCountsWhoareyouPacket)
{
    boost::asio::io_context io;
    const discv5::discv5Config config = make_config();
    discv5::discv5_client client(io, config);

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value()) << "start() must succeed";

    const std::vector<uint8_t> packet = make_whoareyou_packet(config.public_key);
    send_udp_packet_bytes(client.bound_port(), packet);

    io.run_for(std::chrono::milliseconds(100));

    EXPECT_EQ(client.received_packet_count(), 1U);
    EXPECT_EQ(client.whoareyou_packet_count(), 0U);

    client.stop();
}

/// @brief A WHOAREYOU packet with wrong auth size is not classified as WHOAREYOU.
TEST(Discv5ClientTest, ReceiveLoopRejectsWhoareyouWrongAuthSize)
{
    boost::asio::io_context io;
    const discv5::discv5Config config = make_config();
    discv5::discv5_client client(io, config);

    const auto start_result = client.start();
    ASSERT_TRUE(start_result.has_value()) << "start() must succeed";

    const std::vector<uint8_t> packet = make_whoareyou_packet(
        config.public_key,
        static_cast<uint16_t>(discv5::kWhoareyouAuthDataBytes + 1U));
    send_udp_packet_bytes(client.bound_port(), packet);

    io.run_for(std::chrono::milliseconds(100));

    EXPECT_EQ(client.received_packet_count(), 1U);
    EXPECT_EQ(client.whoareyou_packet_count(), 0U);

    client.stop();
}

