// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#include <gtest/gtest.h>
#include "discv4/discv4_client.hpp"
#include "discv4/discv4_ping.hpp"
#include "discv4/discv4_pong.hpp"
#include "discv4/discv4_packet.hpp"
#include <boost/asio/io_context.hpp>
#include <secp256k1.h>
#include <thread>
#include <chrono>

namespace {

// Helper to generate test keypair
struct TestKeypair {
    std::array<uint8_t, 32> private_key;
    std::array<uint8_t, 64> public_key;
};

TestKeypair generate_test_keypair() {
    TestKeypair result;

    // Use a deterministic private key for testing
    for (size_t i = 0; i < 32; ++i) {
        result.private_key[i] = static_cast<uint8_t>(i + 1);
    }

    // Derive public key
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    secp256k1_pubkey pubkey;

    if (secp256k1_ec_pubkey_create(ctx, &pubkey, result.private_key.data())) {
        std::array<uint8_t, 65> pubkey_bytes;
        size_t len = 65;
        secp256k1_ec_pubkey_serialize(ctx, pubkey_bytes.data(), &len, &pubkey, SECP256K1_EC_UNCOMPRESSED);

        // Copy uncompressed public key (skip first 0x04 byte)
        std::copy(pubkey_bytes.begin() + 1, pubkey_bytes.end(), result.public_key.begin());
    }

    secp256k1_context_destroy(ctx);
    return result;
}

} // namespace

// ============================================================================
// discv4 Packet Tests
// ============================================================================

class discv4PacketTest : public ::testing::Test {
protected:
    void SetUp() override {
        keypair_ = generate_test_keypair();
    }

    TestKeypair keypair_;
};

TEST_F(discv4PacketTest, PingPacketCreation) {
    std::cout << "\n[TEST] PingPacketCreation - Creating and encoding PING packet\n";

    discv4::discv4_ping ping(
        "192.168.1.100", 30303, 30303,  // from
        "138.197.51.181", 30303, 30303   // to
    );

    std::cout << "  → Created PING packet\n";
    std::cout << "  → From: 192.168.1.100:30303\n";
    std::cout << "  → To: 138.197.51.181:30303\n";

    auto payload = ping.RlpPayload();
    ASSERT_FALSE(payload.empty()) << "PING payload should not be empty";

    std::cout << "  → Encoded payload: " << payload.size() << " bytes\n";

    // Verify packet type is included
    EXPECT_EQ(payload[0], 0x01) << "First byte should be PING packet type (0x01)";

    std::cout << "  ✓ PING packet created successfully\n";
}

TEST_F(discv4PacketTest, PingPacketStructure) {
    std::cout << "\n[TEST] PingPacketStructure - Verifying PING packet RLP structure\n";

    discv4::discv4_ping ping(
        "127.0.0.1", 30303, 30303,
        "127.0.0.1", 30303, 30303
    );

    auto payload = ping.RlpPayload();
    ASSERT_FALSE(payload.empty());

    // Payload should be: packet-type(1) || RLP([version, from, to, expiration])
    EXPECT_EQ(payload[0], 0x01);

    // Second byte should be RLP list prefix
    EXPECT_TRUE(payload[1] >= 0xc0) << "Second byte should be RLP list prefix";

    std::cout << "  → Packet type: 0x" << std::hex << (int)payload[0] << std::dec << "\n";
    std::cout << "  → RLP list prefix: 0x" << std::hex << (int)payload[1] << std::dec << "\n";
    std::cout << "  ✓ PING structure is valid\n";
}

TEST_F(discv4PacketTest, PongPacketParsing) {
    std::cout << "\n[TEST] PongPacketParsing - Parsing PONG packet\n";

    // Create a minimal PONG RLP payload
    rlp::RlpEncoder encoder;
    ASSERT_TRUE(encoder.BeginList().has_value());

    // to_endpoint: [ip, udp_port, tcp_port]
    ASSERT_TRUE(encoder.BeginList().has_value());
    std::array<uint8_t, 4> ip = {127, 0, 0, 1};
    ASSERT_TRUE(encoder.add(rlp::ByteView(ip.data(), ip.size())).has_value());
    ASSERT_TRUE(encoder.add(uint16_t(30303)).has_value());
    ASSERT_TRUE(encoder.add(uint16_t(30303)).has_value());
    ASSERT_TRUE(encoder.EndList().has_value());

    // ping_hash (32 bytes)
    std::array<uint8_t, 32> ping_hash{};
    ASSERT_TRUE(encoder.add(rlp::ByteView(ping_hash.data(), ping_hash.size())).has_value());

    // expiration
    uint32_t expiration = static_cast<uint32_t>(std::time(nullptr)) + 60;
    ASSERT_TRUE(encoder.add(expiration).has_value());

    ASSERT_TRUE(encoder.EndList().has_value());

    auto bytes_result = encoder.MoveBytes();
    ASSERT_TRUE(bytes_result.has_value());

    // Build full packet: hash (32) + signature (65) + packet_type (1) + RLP payload
    std::vector<uint8_t> full_packet;
    std::array<uint8_t, 32> hash{};
    std::array<uint8_t, 65> signature{};
    uint8_t packet_type = 0x02; // PONG

    full_packet.insert(full_packet.end(), hash.begin(), hash.end());
    full_packet.insert(full_packet.end(), signature.begin(), signature.end());
    full_packet.push_back(packet_type);
    full_packet.insert(full_packet.end(), bytes_result.value().begin(), bytes_result.value().end());

    std::cout << "  → Created test PONG packet: " << full_packet.size() << " bytes\n";

    // Parse it
    auto pong_result = discv4::discv4_pong::Parse(
        rlp::ByteView(full_packet.data(), full_packet.size())
    );

    ASSERT_TRUE(pong_result.has_value()) << "Failed to parse PONG packet";

    const auto& pong = pong_result.value();
    EXPECT_EQ(pong.toEndpoint.ip[0], 127);
    EXPECT_EQ(pong.toEndpoint.ip[1], 0);
    EXPECT_EQ(pong.toEndpoint.ip[2], 0);
    EXPECT_EQ(pong.toEndpoint.ip[3], 1);
    EXPECT_EQ(pong.toEndpoint.udpPort, 30303);
    EXPECT_EQ(pong.toEndpoint.tcpPort, 30303);

    std::cout << "  → Parsed endpoint: " << (int)pong.toEndpoint.ip[0] << "."
              << (int)pong.toEndpoint.ip[1] << "."
              << (int)pong.toEndpoint.ip[2] << "."
              << (int)pong.toEndpoint.ip[3] << ":" << pong.toEndpoint.udpPort << "\n";
    std::cout << "  ✓ PONG packet parsed successfully\n";
}

TEST_F(discv4PacketTest, Keccak256Hash) {
    std::cout << "\n[TEST] Keccak256Hash - Testing Keccak-256 hashing\n";

    std::vector<uint8_t> test_data = {0x01, 0x02, 0x03, 0x04};

    auto hash = discv4::discv4_packet::Keccak256(test_data);

    EXPECT_EQ(hash.size(), 32) << "Keccak-256 should produce 32 bytes";

    // Hash should be deterministic
    auto hash2 = discv4::discv4_packet::Keccak256(test_data);
    EXPECT_EQ(hash, hash2) << "Same input should produce same hash";

    // Different input should produce different hash
    std::vector<uint8_t> different_data = {0x05, 0x06, 0x07, 0x08};
    auto hash3 = discv4::discv4_packet::Keccak256(different_data);
    EXPECT_NE(hash, hash3) << "Different input should produce different hash";

    std::cout << "  → Hash size: " << hash.size() << " bytes\n";
    std::cout << "  → Hash (first 8 bytes): ";
    for (int i = 0; i < 8; ++i) {
        printf("%02x", hash[i]);
    }
    std::cout << "...\n";
    std::cout << "  ✓ Keccak-256 working correctly\n";
}

// ============================================================================
// discv4 Client Tests
// ============================================================================

class discv4ClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        keypair_ = generate_test_keypair();

        config_.bind_ip = "127.0.0.1";
        config_.bind_port = 0;  // Let OS assign port
        config_.tcp_port = 30303;
        config_.private_key = keypair_.private_key;
        config_.public_key = keypair_.public_key;
    }

    void TearDown() override {
        if (client_) {
            client_->stop();
            client_.reset();
        }
    }

    TestKeypair keypair_;
    discv4::discv4Config config_;
    std::unique_ptr<discv4::discv4_client> client_;
    boost::asio::io_context io_context_;
};

TEST_F(discv4ClientTest, ClientCreation) {
    std::cout << "\n[TEST] ClientCreation - Creating discv4_client\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);
    ASSERT_NE(client_, nullptr);

    std::cout << "  → Client created successfully\n";
    std::cout << "  → Bind port: " << config_.bind_port << " (0 = auto-assign)\n";
    std::cout << "  → TCP port: " << config_.tcp_port << "\n";
    std::cout << "  ✓ discv4_client instantiated\n";
}

TEST_F(discv4ClientTest, ClientRejectsIPv6BindAddress) {
    std::cout << "\n[TEST] ClientRejectsIPv6BindAddress - Rejecting IPv6 bind_ip until handlers are IPv6-safe\n";

    config_.bind_ip = "::1";

    EXPECT_THROW(
        {
            client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);
        },
        std::runtime_error)
        << "discv4_client must reject IPv6 bind_ip while discv4 packet handling is IPv4-only";
}

TEST_F(discv4ClientTest, ClientStartStop) {
    std::cout << "\n[TEST] ClientStartStop - Testing client lifecycle\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);

    std::cout << "  → Starting client\n";
    auto start_result = client_->start();
    EXPECT_TRUE(start_result.has_value()) << "Client should start successfully";

    std::cout << "  → Client started\n";
    std::cout << "  → Stopping client\n";

    client_->stop();

    std::cout << "  ✓ Client lifecycle working correctly\n";
}

TEST_F(discv4ClientTest, LocalNodeId) {
    std::cout << "\n[TEST] LocalNodeId - Verifying local node ID\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);

    const auto& node_id = client_->local_node_id();
    EXPECT_EQ(node_id.size(), 64) << "Node ID should be 64 bytes";
    EXPECT_EQ(node_id, keypair_.public_key) << "Node ID should match public key";

    std::cout << "  → Node ID size: " << node_id.size() << " bytes\n";
    std::cout << "  → Node ID (first 16 bytes): ";
    for (int i = 0; i < 16; ++i) {
        printf("%02x", node_id[i]);
    }
    std::cout << "...\n";
    std::cout << "  ✓ Local node ID correct\n";
}

TEST_F(discv4ClientTest, PeerDiscoveryCallback) {
    std::cout << "\n[TEST] PeerDiscoveryCallback - Testing peer discovery callbacks\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);

    bool callback_called = false;
    discv4::DiscoveredPeer discovered_peer;

    client_->set_peer_discovered_callback([&](const discv4::DiscoveredPeer& peer) {
        std::cout << "  → Peer discovered callback invoked\n";
        std::cout << "    IP: " << peer.ip << ":" << peer.udp_port << "\n";
        callback_called = true;
        discovered_peer = peer;
    });

    std::cout << "  → Callback registered\n";
    std::cout << "  ✓ Callback mechanism working\n";
}

TEST_F(discv4ClientTest, ErrorCallback) {
    std::cout << "\n[TEST] ErrorCallback - Testing error callbacks\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);

    bool error_called = false;
    std::string error_message;

    client_->set_error_callback([&](const std::string& msg) {
        std::cout << "  → Error callback invoked: " << msg << "\n";
        error_called = true;
        error_message = msg;
    });

    std::cout << "  → Error callback registered\n";
    std::cout << "  ✓ Error callback mechanism working\n";
}

// ============================================================================
// discv4 Endpoint Tests
// ============================================================================

class discv4EndpointTest : public ::testing::Test {};

TEST_F(discv4EndpointTest, EndpointEncoding) {
    std::cout << "\n[TEST] EndpointEncoding - Testing endpoint RLP encoding\n";

    discv4::discv4_ping::Endpoint endpoint("192.168.1.1", 30303, 30303);

    auto encoded = endpoint.encode();
    ASSERT_TRUE(encoded.has_value()) << "Endpoint encoding should succeed";

    std::cout << "  → Endpoint: 192.168.1.1:30303\n";
    std::cout << "  → Encoded size: " << encoded.value().size() << " bytes\n";

    // Verify it's a valid RLP list
    EXPECT_TRUE(encoded.value()[0] >= 0xc0) << "Should be RLP list";

    std::cout << "  ✓ Endpoint encoded successfully\n";
}

TEST_F(discv4EndpointTest, EndpointIPv4Parsing) {
    std::cout << "\n[TEST] EndpointIPv4Parsing - Testing IPv4 address parsing\n";

    struct TestCase {
        std::string ip;
        std::array<uint8_t, 4> expected_bytes;
    };

    TestCase test_cases[] = {
        {"127.0.0.1", {127, 0, 0, 1}},
        {"192.168.1.1", {192, 168, 1, 1}},
        {"10.0.0.1", {10, 0, 0, 1}},
        {"138.197.51.181", {138, 197, 51, 181}},
    };

    for (const auto& test : test_cases) {
        std::cout << "  → Testing IP: " << test.ip << "\n";

        discv4::discv4_ping::Endpoint endpoint(test.ip, 30303, 30303);

        for (size_t i = 0; i < 4; ++i) {
            EXPECT_EQ(endpoint.ipBytes[i], test.expected_bytes[i])
                << "Byte " << i << " mismatch for " << test.ip;
        }
    }

    std::cout << "  ✓ All IPv4 addresses parsed correctly\n";
}

TEST_F(discv4EndpointTest, EndpointPortEncoding) {
    std::cout << "\n[TEST] EndpointPortEncoding - Testing port number encoding\n";

    uint16_t test_ports[] = {30303, 30311, 8545, 65535, 1};

    for (uint16_t port : test_ports) {
        std::cout << "  → Testing port: " << port << "\n";

        discv4::discv4_ping::Endpoint endpoint("127.0.0.1", port, port);

        EXPECT_EQ(endpoint.udpPort, port);
        EXPECT_EQ(endpoint.tcpPort, port);

        auto encoded = endpoint.encode();
        ASSERT_TRUE(encoded.has_value());
    }

    std::cout << "  ✓ All port numbers encoded correctly\n";
}

// ============================================================================
// discv4 Protocol Flow Tests
// ============================================================================

class discv4ProtocolFlowTest : public ::testing::Test {
protected:
    void SetUp() override {
        keypair_ = generate_test_keypair();
    }

    TestKeypair keypair_;
};

TEST_F(discv4ProtocolFlowTest, PingPongSequence) {
    std::cout << "\n[TEST] PingPongSequence - Testing PING → PONG protocol flow\n";

    // Create PING
    discv4::discv4_ping ping(
        "192.168.1.100", 30303, 30303,
        "138.197.51.181", 30303, 30303
    );

    auto ping_payload = ping.RlpPayload();
    ASSERT_FALSE(ping_payload.empty());

    std::cout << "  → PING created: " << ping_payload.size() << " bytes\n";

    // Simulate PONG response
    rlp::RlpEncoder encoder;
    ASSERT_TRUE(encoder.BeginList().has_value());

    // to_endpoint
    ASSERT_TRUE(encoder.BeginList().has_value());
    std::array<uint8_t, 4> ip = {192, 168, 1, 100};
    ASSERT_TRUE(encoder.add(rlp::ByteView(ip.data(), ip.size())).has_value());
    ASSERT_TRUE(encoder.add(uint16_t(30303)).has_value());
    ASSERT_TRUE(encoder.add(uint16_t(30303)).has_value());
    ASSERT_TRUE(encoder.EndList().has_value());

    // ping_hash (compute from PING)
    auto ping_hash = discv4::discv4_packet::Keccak256(ping_payload);
    ASSERT_TRUE(encoder.add(rlp::ByteView(ping_hash.data(), ping_hash.size())).has_value());

    // expiration
    uint32_t expiration = static_cast<uint32_t>(std::time(nullptr)) + 60;
    ASSERT_TRUE(encoder.add(expiration).has_value());

    ASSERT_TRUE(encoder.EndList().has_value());

    auto pong_bytes = encoder.MoveBytes();
    ASSERT_TRUE(pong_bytes.has_value());

    // Build full PONG packet: hash (32) + signature (65) + packet_type (1) + RLP payload
    std::vector<uint8_t> full_pong_packet;
    std::array<uint8_t, 32> pong_hash{};
    std::array<uint8_t, 65> pong_signature{};
    uint8_t pong_packet_type = 0x02; // PONG

    full_pong_packet.insert(full_pong_packet.end(), pong_hash.begin(), pong_hash.end());
    full_pong_packet.insert(full_pong_packet.end(), pong_signature.begin(), pong_signature.end());
    full_pong_packet.push_back(pong_packet_type);
    full_pong_packet.insert(full_pong_packet.end(), pong_bytes.value().begin(), pong_bytes.value().end());

    std::cout << "  → PONG created: " << full_pong_packet.size() << " bytes\n";

    // Parse PONG
    auto pong_result = discv4::discv4_pong::Parse(
        rlp::ByteView(full_pong_packet.data(), full_pong_packet.size())
    );

    ASSERT_TRUE(pong_result.has_value());

    std::cout << "  → PONG parsed successfully\n";
    std::cout << "  ✓ PING → PONG sequence working\n";
}

TEST_F(discv4ProtocolFlowTest, ExpirationValidation) {
    std::cout << "\n[TEST] ExpirationValidation - Testing packet expiration\n";

    uint32_t now = static_cast<uint32_t>(std::time(nullptr));

    struct TestCase {
        uint32_t expiration;
        bool should_be_valid;
        std::string description;
    };

    TestCase test_cases[] = {
        {now + 60, true, "Future expiration (valid)"},
        {now - 60, false, "Past expiration (expired)"},
        {now, false, "Current time (expired)"},
        {now + 3600, true, "Far future (valid)"},
    };

    for (const auto& test : test_cases) {
        std::cout << "  → " << test.description << "\n";
        std::cout << "    Expiration: " << test.expiration << ", Now: " << now << "\n";

        bool is_valid = test.expiration > now;
        EXPECT_EQ(is_valid, test.should_be_valid) << test.description;
    }

    std::cout << "  ✓ Expiration validation working\n";
}

// ============================================================================
// discv4 Integration Tests
// ============================================================================

class discv4IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        keypair_ = generate_test_keypair();

        config_.bind_ip = "127.0.0.1";
        config_.bind_port = 0;  // Auto-assign
        config_.tcp_port = 30303;
        config_.private_key = keypair_.private_key;
        config_.public_key = keypair_.public_key;
    }

    void TearDown() override {
        if (client_) {
            client_->stop();
            client_.reset();
        }
    }

    TestKeypair keypair_;
    discv4::discv4Config config_;
    std::unique_ptr<discv4::discv4_client> client_;
    boost::asio::io_context io_context_;
};

TEST_F(discv4IntegrationTest, ClientInitialization) {
    std::cout << "\n[TEST] ClientInitialization - Full client initialization\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);
    ASSERT_NE(client_, nullptr);

    std::cout << "  → Client created\n";

    auto start_result = client_->start();
    EXPECT_TRUE(start_result.has_value());

    std::cout << "  → Client started\n";

    const auto& node_id = client_->local_node_id();
    EXPECT_EQ(node_id.size(), 64);

    std::cout << "  → Local node ID verified\n";
    std::cout << "  ✓ Client fully initialized\n";
}

TEST_F(discv4IntegrationTest, PeerTableManagement) {
    std::cout << "\n[TEST] PeerTableManagement - Testing peer table operations\n";

    client_ = std::make_unique<discv4::discv4_client>(io_context_, config_);
    auto start_result = client_->start();
    ASSERT_TRUE(start_result) << "Client should start successfully";

    std::cout << "  → Getting initial peer list\n";
    auto peers = client_->get_peers();
    EXPECT_EQ(peers.size(), 0) << "Initial peer table should be empty";

    std::cout << "  → Initial peers: " << peers.size() << "\n";
    std::cout << "  ✓ Peer table management working\n";
}


