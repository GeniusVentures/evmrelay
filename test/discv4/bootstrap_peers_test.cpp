// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

/// @file bootstrap_peers_test.cpp
/// @brief Unit tests for bootstrap peer JSON loading helpers.

#include <gtest/gtest.h>

#include <discv4/bootstrap_peers.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace
{

std::string make_enode(
    const std::string& host,
    const uint16_t     port,
    const char         fill_char)
{
    return std::string("enode://")
        + std::string(128U, fill_char)
        + "@"
        + host
        + ":"
        + std::to_string(port);
}

std::filesystem::path write_file(
    const std::filesystem::path& path,
    const std::string&           contents)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << contents;
    return path;
}

class BootstrapPeersTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        temp_dir_ = std::filesystem::temp_directory_path() / "bootstrap_peers_test";
        std::filesystem::create_directories(temp_dir_);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path temp_dir_;
};

} // namespace

TEST_F(BootstrapPeersTest, LoadBootstrapPeersFromJsonTextParsesMatchingChain)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '1') + "\"},"
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '2') + "\"}"
        + "],"
        + "\"ethereum-sepolia\":["
        + "{\"enode\":\"" + make_enode("192.168.0.1", 30305U, '3') + "\"}"
        + "]}"
        ;

    const auto peers = discv4::load_bootstrap_peers_from_json_text(
        "ethereum-mainnet",
        json_text);

    ASSERT_EQ(peers.size(), 2U);
    EXPECT_EQ(peers[0].peer.ip, "127.0.0.1");
    EXPECT_EQ(peers[0].peer.tcp_port, 30303U);
    EXPECT_EQ(peers[1].peer.ip, "10.0.0.2");
    EXPECT_EQ(peers[1].peer.tcp_port, 30304U);
}

TEST_F(BootstrapPeersTest, LoadBootstrapPeersFromJsonTextIgnoresOtherChainsAndInvalidEntries)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":["
        + "{\"enode\":\"not-an-enode\"},"
        + "{\"foo\":\"bar\"}"
        + "],"
        + "\"ethereum-sepolia\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '4') + "\"}"
        + "]}"
        ;

    const auto peers = discv4::load_bootstrap_peers_from_json_text(
        "ethereum-mainnet",
        json_text);

    EXPECT_TRUE(peers.empty());
}

TEST_F(BootstrapPeersTest, LoadBootstrapPeersFromJsonReadsPlainJsonFile)
{
    const auto json_path = write_file(
        temp_dir_ / "chain_enodes.json",
        std::string("{")
            + "\"ethereum-mainnet\":["
            + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '5') + "\"}"
            + "]}"
    );

    const auto peers = discv4::load_bootstrap_peers_from_json(
        "ethereum-mainnet",
        json_path);

    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].peer.ip, "127.0.0.1");
    EXPECT_EQ(peers[0].peer.tcp_port, 30303U);
}

TEST_F(BootstrapPeersTest, FindBootstrapPeersJsonPathPrefersJsonThenGzip)
{
    const std::filesystem::path bin_dir = temp_dir_ / "bin";
    std::filesystem::create_directories(bin_dir);

    const std::string argv0 = (bin_dir / "test_binary").string();
    write_file(bin_dir / "chain_enodes.json.gz", "gzip-placeholder");

    const auto gzip_path = discv4::find_bootstrap_peers_json_path(argv0, "");
    ASSERT_TRUE(gzip_path.has_value());
    EXPECT_EQ(gzip_path->filename(), "chain_enodes.json.gz");

    write_file(bin_dir / "chain_enodes.json", "{}");
    const auto json_path = discv4::find_bootstrap_peers_json_path(argv0, "");
    ASSERT_TRUE(json_path.has_value());
    EXPECT_EQ(json_path->filename(), "chain_enodes.json");
}

static constexpr const char* kBootstrapPeersUrl = "https://enodes.gnus.ai/chain_enodes.json.gz";
static constexpr const char* kMainnetChainKey = "ethereum-mainnet";

TEST_F(BootstrapPeersTest, DownloadBootstrapJsonFromLiveUrlLoadsMainnetPeers)
{
    const auto json_text = discv4::download_bootstrap_json(kBootstrapPeersUrl);
    ASSERT_TRUE(json_text.has_value()) << "Failed to download bootstrap peer JSON from live URL";
    EXPECT_FALSE(json_text->empty());

    const auto peers = discv4::load_bootstrap_peers_from_json_text(
        kMainnetChainKey,
        *json_text);

    EXPECT_FALSE(peers.empty()) << "Expected at least one mainnet bootstrap peer from live URL";
    const auto valid_peer = std::find_if(
        peers.begin(),
        peers.end(),
        [](const discv4::ValidatedPeer& peer)
        {
            return !peer.peer.ip.empty() && peer.peer.tcp_port != 0;
        });
    EXPECT_NE(valid_peer, peers.end()) << "Expected at least one bootstrap peer with IP and TCP port";
}
