// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

/// @file chain_peers_test.cpp
/// @brief Unit tests for chain peer JSON loading helpers.

#include <gtest/gtest.h>

#include <discv4/bootstrap_peers.hpp>
#include <discv4/chain_peers.hpp>
#include <eth/messages.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
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

class ChainPeersTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        temp_dir_ = std::filesystem::temp_directory_path() / "chain_peers_test";
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

TEST_F(ChainPeersTest, LoadChainPeersFromJsonTextParsesMatchingChain)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '1') + "\"},"
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '2') + "\"}"
        + "]},"
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("192.168.0.1", 30305U, '3') + "\"}"
        + "]}}"
        ;

    const auto peers = discv4::load_chain_peers_from_json_text(
        "ethereum-mainnet",
        json_text);

    ASSERT_EQ(peers.size(), 2U);
    EXPECT_EQ(peers[0].peer.ip, "127.0.0.1");
    EXPECT_EQ(peers[0].peer.tcp_port, 30303U);
    EXPECT_EQ(peers[1].peer.ip, "10.0.0.2");
    EXPECT_EQ(peers[1].peer.tcp_port, 30304U);
}

TEST_F(ChainPeersTest, LoadChainPeersFromJsonTextIgnoresOtherChainsAndInvalidEntries)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"nodes\":["
        + "{\"enode\":\"not-an-enode\"},"
        + "{\"foo\":\"bar\"}"
        + "]},"
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '4') + "\"}"
        + "]}}"
        ;

    const auto peers = discv4::load_chain_peers_from_json_text(
        "ethereum-mainnet",
        json_text);

    EXPECT_TRUE(peers.empty());
}

TEST_F(ChainPeersTest, LoadChainPeersFromJsonReadsPlainJsonFile)
{
    const auto json_path = write_file(
        temp_dir_ / "chain_enodes.json",
        std::string("{")
            + "\"ethereum-mainnet\":{"
            + "\"networkId\":1,"
            + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
            + "\"nodes\":["
            + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '5') + "\"}"
            + "]}}"
    );

    const auto peers = discv4::load_chain_peers_from_json(
        "ethereum-mainnet",
        json_path);

    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].peer.ip, "127.0.0.1");
    EXPECT_EQ(peers[0].peer.tcp_port, 30303U);
}

TEST_F(ChainPeersTest, ChainPeersUseNodesArrayAndIgnoreBootnodes)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '1') + "\"}"
        + "],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '2') + "\"}"
        + "]}}";

    const auto peers = discv4::load_chain_peers_from_json_text(
        "ethereum-mainnet",
        json_text);

    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].peer.ip, "127.0.0.1");
    EXPECT_EQ(peers[0].peer.tcp_port, 30303U);
}

TEST_F(ChainPeersTest, BootstrapPeersUseBootnodesArrayAndIgnoreNodes)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"forkId\":\"07c9462e\","
        + "\"forkNext\":\"0\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '3') + "\"}"
        + "],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '4') + "\"}"
        + "]}}";

    const auto peers = discv4::load_bootstrap_peers_from_json_text(
        "ethereum-mainnet",
        json_text);

    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].peer.ip, "10.0.0.2");
    EXPECT_EQ(peers[0].peer.tcp_port, 30304U);
}

TEST_F(ChainPeersTest, BootstrapPeersFromJsonFileUseBootnodesArrayAndIgnoreNodes)
{
    const auto json_path = write_file(
        temp_dir_ / "chain_enodes.json",
        std::string("{")
            + "\"ethereum-mainnet\":{"
            + "\"networkId\":1,"
            + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
            + "\"forkId\":\"07c9462e\","
            + "\"forkNext\":\"0\","
            + "\"nodes\":["
            + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '3') + "\"}"
            + "],"
            + "\"bootnodes\":["
            + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '4') + "\"}"
            + "]}}");

    const auto peers = discv4::load_bootstrap_peers_from_json(
        "ethereum-mainnet",
        json_path);

    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].peer.ip, "10.0.0.2");
    EXPECT_EQ(peers[0].peer.tcp_port, 30304U);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigRequiresBootnodesArray)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '5') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-mainnet",
        json_text);

    EXPECT_FALSE(config.has_value());
}

TEST_F(ChainPeersTest, LoadChainPeerConfigRequiresNodesArray)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '6') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-mainnet",
        json_text);

    EXPECT_FALSE(config.has_value());
}

TEST_F(ChainPeersTest, LoadChainPeerConfigAllowsEmptyNodesWithBootnodes)
{
    const std::string json_text = std::string("{")
        + "\"gnosis-chain\":{"
        + "\"networkId\":100,"
        + "\"genesisHex\":\"4f1dd23188aab3a0b3768e6a2b5f6cbf3fcb259af45d37b228a8a0ae61161f80\","
        + "\"forkId\":\"06000064\","
        + "\"forkNext\":\"0\","
        + "\"nodes\":[],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '7') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "gnosis-chain",
        json_text);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->canonical_name, "gnosis-chain");
    EXPECT_EQ(config->network_id, 100U);
    EXPECT_TRUE(config->nodes.empty());
    ASSERT_EQ(config->bootnodes.size(), 1U);
    EXPECT_EQ(config->bootnodes[0].peer.ip, "10.0.0.2");
    EXPECT_EQ(config->bootnodes[0].peer.tcp_port, 30304U);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigAppliesTopLevelForkIdToPeers)
{
    const std::string node = make_enode("10.0.0.1", 30303U, 'a');
    const std::string bootnode = make_enode("10.0.0.2", 30304U, 'b');
    const std::string json_text = std::string("{")
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"" + std::string(64U, '1') + "\","
        + "\"forkId\":\"deadbeef\","
        + "\"forkNext\":\"1234\","
        + "\"nodes\":[{\"enode\":\"" + node + "\"}],"
        + "\"bootnodes\":[{\"enode\":\"" + bootnode + "\"}]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_TRUE(config->fork_id.has_value());
    ASSERT_EQ(config->nodes.size(), 1U);
    ASSERT_EQ(config->bootnodes.size(), 1U);
    ASSERT_TRUE(config->nodes[0].peer.eth_fork_id.has_value());
    ASSERT_TRUE(config->bootnodes[0].peer.eth_fork_id.has_value());
    EXPECT_EQ(config->nodes[0].peer.eth_fork_id->hash, config->fork_id->fork_hash);
    EXPECT_EQ(config->nodes[0].peer.eth_fork_id->next, config->fork_id->next_fork);
    EXPECT_EQ(config->bootnodes[0].peer.eth_fork_id->hash, config->fork_id->fork_hash);
    EXPECT_EQ(config->bootnodes[0].peer.eth_fork_id->next, config->fork_id->next_fork);
}

TEST_F(ChainPeersTest, FindChainPeersJsonPathPrefersJsonThenGzip)
{
    const std::filesystem::path bin_dir = temp_dir_ / "bin";
    std::filesystem::create_directories(bin_dir);

    const std::string argv0 = (bin_dir / "test_binary").string();
    write_file(bin_dir / "chain_enodes.json.gz", "gzip-placeholder");

    const auto gzip_path = discv4::find_chain_peer_cache_json_path(argv0, "");
    ASSERT_TRUE(gzip_path.has_value());
    EXPECT_EQ(gzip_path->filename(), "chain_enodes.json.gz");

    write_file(bin_dir / "chain_enodes.json", "{}");
    const auto json_path = discv4::find_chain_peer_cache_json_path(argv0, "");
    ASSERT_TRUE(json_path.has_value());
    EXPECT_EQ(json_path->filename(), "chain_enodes.json");
}

TEST_F(ChainPeersTest, FindChainPeersJsonPathChecksCurrentWorkingDirectory)
{
    const std::filesystem::path bin_dir = temp_dir_ / "bin";
    const std::filesystem::path cwd_dir = temp_dir_ / "cwd";
    std::filesystem::create_directories(bin_dir);
    std::filesystem::create_directories(cwd_dir);

    const std::filesystem::path old_cwd = std::filesystem::current_path();
    std::filesystem::current_path(cwd_dir);

    const std::string argv0 = (bin_dir / "examples" / "eth_watch" / "eth_watch").string();
    write_file(cwd_dir / "chain_enodes.json.gz", "{}");

    const auto path = discv4::find_chain_peer_cache_json_path(argv0, "");

    std::filesystem::current_path(old_cwd);

    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(std::filesystem::weakly_canonical(*path),
              std::filesystem::weakly_canonical(cwd_dir / "chain_enodes.json.gz"));
}

static constexpr const char* kChainPeersUrl = "https://enodes.gnus.ai/chain_enodes.json.gz";
static constexpr const char* kMainnetChainKey = "ethereum-mainnet";
static constexpr const char* kChainPeersCacheEnv = "EVMRELAY_CHAIN_ENODES_JSON";
static constexpr const char* kChainPeersJsonFile = "chain_enodes.json";
static constexpr const char* kChainPeersGzipFile = "chain_enodes.json.gz";

std::optional<std::filesystem::path> find_pre_downloaded_chain_peer_cache()
{
    const char* env_path = std::getenv(kChainPeersCacheEnv);
    if (env_path != nullptr && env_path[0] != '\0')
    {
        const std::filesystem::path path(env_path);
        if (std::filesystem::exists(path))
        {
            return path;
        }
    }

    const std::filesystem::path cwd = std::filesystem::current_path();
    const std::filesystem::path gzip_path = cwd / kChainPeersGzipFile;
    if (std::filesystem::exists(gzip_path))
    {
        return gzip_path;
    }

    const std::filesystem::path json_path = cwd / kChainPeersJsonFile;
    if (std::filesystem::exists(json_path))
    {
        return json_path;
    }

    return std::nullopt;
}

TEST_F(ChainPeersTest, DownloadChainPeerCacheJsonFromLiveUrlLoadsMainnetPeers)
{
    std::vector<discv4::ValidatedPeer> peers;
    if (const auto cache_path = find_pre_downloaded_chain_peer_cache(); cache_path.has_value())
    {
        peers = discv4::load_chain_peers_from_json(kMainnetChainKey, *cache_path);
    }
    else
    {
        const auto json_text = discv4::download_chain_peer_cache_json(kChainPeersUrl);
        ASSERT_TRUE(json_text.has_value()) << "Failed to download chain peer JSON from live URL";
        EXPECT_FALSE(json_text->empty());

        peers = discv4::load_chain_peers_from_json_text(
            kMainnetChainKey,
            *json_text);
    }

    EXPECT_FALSE(peers.empty()) << "Expected at least one mainnet chain peer from cache or live URL";
    const auto valid_peer = std::find_if(
        peers.begin(),
        peers.end(),
        [](const discv4::ValidatedPeer& peer)
        {
            return !peer.peer.ip.empty() && peer.peer.tcp_port != 0;
        });
    EXPECT_NE(valid_peer, peers.end()) << "Expected at least one chain peer with IP and TCP port";
}

TEST_F(ChainPeersTest, LoadChainPeerConfigFromJsonTextParsesSharedMetadataAndNodes)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"forkId\":\"ed88b5fd\","
        + "\"forkNext\":1735371,"
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '6') + "\"}"
        + "],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '7') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->canonical_name, "ethereum-sepolia");
    EXPECT_EQ(config->network_id, 11155111U);
    ASSERT_EQ(config->nodes.size(), 1U);
    EXPECT_EQ(config->nodes[0].peer.ip, "127.0.0.1");
    ASSERT_EQ(config->bootnodes.size(), 1U);
    EXPECT_EQ(config->bootnodes[0].peer.ip, "10.0.0.2");
    ASSERT_TRUE(config->fork_id.has_value());
    EXPECT_EQ(config->fork_id->fork_hash[0], 0xed);
    EXPECT_EQ(config->fork_id->fork_hash[1], 0x88);
    EXPECT_EQ(config->fork_id->fork_hash[2], 0xb5);
    EXPECT_EQ(config->fork_id->fork_hash[3], 0xfd);
    EXPECT_EQ(config->fork_id->next_fork, 1735371U);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigParsesReadmeSchemaHexForkNext)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"forkId\":\"07c9462e\","
        + "\"forkNext\":\"695db057\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '6') + "\"}"
        + "],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '7') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-mainnet",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_TRUE(config->fork_id.has_value());
    EXPECT_EQ(config->fork_id->fork_hash[0], 0x07);
    EXPECT_EQ(config->fork_id->fork_hash[1], 0xc9);
    EXPECT_EQ(config->fork_id->fork_hash[2], 0x46);
    EXPECT_EQ(config->fork_id->fork_hash[3], 0x2e);
    EXPECT_EQ(config->fork_id->next_fork, 0x695db057ULL);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigParsesEthMessageSchemas)
{
    const std::string json_text = std::string("{")
        + "\"polygon-amoy\":{"
        + "\"networkId\":80002,"
        + "\"genesisHex\":\"7202b2b53c5a0836e773e319d18922cc756dd67432f9a1f65352b61f4406c697\","
        + "\"ethMessageSchemas\":[{"
        + "\"name\":\"Status\","
        + "\"id\":0,"
        + "\"protocolVersion\":69,"
        + "\"fields\":["
        + "{\"name\":\"protocol_version\",\"type\":\"uint8\",\"offset\":0},"
        + "{\"name\":\"network_id\",\"type\":\"uint64\",\"offset\":1},"
        + "{\"name\":\"bor_extra\",\"type\":\"uint32\",\"offset\":2},"
        + "{\"name\":\"genesis_hash\",\"type\":\"hash32\",\"size\":32,\"offset\":3},"
        + "{\"name\":\"fork_id\",\"type\":\"forkid\",\"offset\":4},"
        + "{\"name\":\"earliest_block\",\"type\":\"uint64\",\"offset\":5},"
        + "{\"name\":\"latest_block\",\"type\":\"uint64\",\"offset\":6},"
        + "{\"name\":\"latest_block_hash\",\"type\":\"hash32\",\"size\":32,\"offset\":7}"
        + "]},"
        + "{\"name\":\"NewBlockHashes\",\"id\":1,\"fields\":[{\"name\":\"entries\",\"type\":\"block_hash_entries\",\"offset\":0}]},"
        + "{\"name\":\"Transactions\",\"id\":2,\"fields\":[{\"name\":\"transactions\",\"type\":\"transactions\",\"offset\":0}]},"
        + "{\"name\":\"GetBlockHeaders\",\"id\":3,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"query\",\"type\":\"get_block_headers_query\",\"offset\":1}]},"
        + "{\"name\":\"BlockHeaders\",\"id\":4,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"headers\",\"type\":\"block_headers\",\"offset\":1}]},"
        + "{\"name\":\"GetBlockBodies\",\"id\":5,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"block_hashes\",\"type\":\"hash_list\",\"offset\":1}]},"
        + "{\"name\":\"BlockBodies\",\"id\":6,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"bodies\",\"type\":\"block_bodies\",\"offset\":1}]},"
        + "{\"name\":\"NewBlock\",\"id\":7,\"fields\":[{\"name\":\"block\",\"type\":\"block\",\"offset\":0},{\"name\":\"total_difficulty\",\"type\":\"uint256\",\"offset\":1}]},"
        + "{\"name\":\"NewPooledTransactionHashes\",\"id\":8,\"fields\":[{\"name\":\"types\",\"type\":\"bytes\",\"offset\":0},{\"name\":\"sizes\",\"type\":\"uint32_list\",\"offset\":1},{\"name\":\"hashes\",\"type\":\"hash_list\",\"offset\":2}]},"
        + "{\"name\":\"GetPooledTransactions\",\"id\":9,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"transaction_hashes\",\"type\":\"hash_list\",\"offset\":1}]},"
        + "{\"name\":\"PooledTransactions\",\"id\":10,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"transactions\",\"type\":\"pooled_transactions\",\"offset\":1}]},"
        + "{\"name\":\"GetReceipts\",\"id\":15,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"block_hashes\",\"type\":\"hash_list\",\"offset\":1}]},"
        + "{\"name\":\"Receipts\",\"id\":16,\"fields\":[{\"name\":\"request_id\",\"type\":\"uint64\",\"offset\":0},{\"name\":\"receipts\",\"type\":\"receipts\",\"offset\":1}]}"
        + "],"
        + "\"nodes\":[],"
        + "\"bootnodes\":[]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "polygon-amoy",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->eth_message_schemas.size(), 13U);
    const auto& schema = config->eth_message_schemas.front();
    EXPECT_EQ(schema.name, "Status");
    EXPECT_EQ(schema.message_id, eth::protocol::kStatusMessageId);
    ASSERT_TRUE(schema.protocol_version.has_value());
    EXPECT_EQ(*schema.protocol_version, eth::kEthProtocolVersion69);
    ASSERT_EQ(schema.fields.size(), 8U);
    EXPECT_EQ(schema.fields[2].name, "bor_extra");
    EXPECT_EQ(schema.fields[2].type, eth::EthMessageFieldType::kUint32);
    ASSERT_TRUE(schema.fields[7].size.has_value());
    EXPECT_EQ(*schema.fields[7].size, 32U);

    const auto has_schema_id = [&config](uint8_t message_id)
    {
        return std::any_of(
            config->eth_message_schemas.begin(),
            config->eth_message_schemas.end(),
            [message_id](const eth::EthMessageSchema& item)
            {
                return item.message_id == message_id;
            });
    };

    EXPECT_TRUE(has_schema_id(eth::protocol::kNewBlockHashesMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kTransactionsMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kGetBlockHeadersMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kBlockHeadersMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kGetBlockBodiesMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kBlockBodiesMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kNewBlockMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kNewPooledTransactionHashesMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kGetPooledTransactionsMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kPooledTransactionsMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kGetReceiptsMessageId));
    EXPECT_TRUE(has_schema_id(eth::protocol::kReceiptsMessageId));

    const auto receipts_schema = std::find_if(
        config->eth_message_schemas.begin(),
        config->eth_message_schemas.end(),
        [](const eth::EthMessageSchema& item)
        {
            return item.message_id == eth::protocol::kReceiptsMessageId;
        });
    ASSERT_NE(receipts_schema, config->eth_message_schemas.end());
    ASSERT_EQ(receipts_schema->fields.size(), 2U);
    EXPECT_EQ(receipts_schema->fields[1].type, eth::EthMessageFieldType::kReceipts);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigResolvesEthMessageSchemaSet)
{
    const std::string json_text =
        "{"
        "\"_ethMessageSchemaSets\":{"
        "\"ethereum-default\":[{"
        "\"name\":\"Status\","
        "\"id\":0,"
        "\"protocolVersion\":69,"
        "\"fields\":["
        "{\"name\":\"protocol_version\",\"type\":\"uint8\",\"offset\":0},"
        "{\"name\":\"network_id\",\"type\":\"uint64\",\"offset\":1},"
        "{\"name\":\"genesis_hash\",\"type\":\"hash32\",\"size\":32,\"offset\":2},"
        "{\"name\":\"fork_id\",\"type\":\"forkid\",\"offset\":3},"
        "{\"name\":\"earliest_block\",\"type\":\"uint64\",\"offset\":4},"
        "{\"name\":\"latest_block\",\"type\":\"uint64\",\"offset\":5},"
        "{\"name\":\"latest_block_hash\",\"type\":\"hash32\",\"size\":32,\"offset\":6}"
        "]}"
        "]},"
        "\"ethereum-sepolia\":{"
        "\"networkId\":11155111,"
        "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        "\"ethMessageSchemaSet\":\"ethereum-default\","
        "\"nodes\":[],"
        "\"bootnodes\":[]"
        "}"
        "}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->eth_message_schemas.size(), 1U);
    EXPECT_EQ(config->eth_message_schemas.front().name, "Status");
    ASSERT_EQ(config->eth_message_schemas.front().fields.size(), 7U);
    EXPECT_EQ(config->eth_message_schemas.front().fields[0].type, eth::EthMessageFieldType::kUint8);
    EXPECT_EQ(config->eth_message_schemas.front().fields[1].type, eth::EthMessageFieldType::kUint64);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigResolvesInheritedEthMessageSchemaSet)
{
    const std::string json_text =
        "{"
        "\"_ethMessageSchemaSets\":{"
        "\"ethereum-default\":["
        "{\"name\":\"Status\",\"id\":0,\"protocolVersion\":69,\"fields\":[{\"name\":\"protocol_version\",\"type\":\"uint8\",\"offset\":0}]},"
        "{\"name\":\"Status\",\"id\":0,\"protocolVersion\":68,\"fields\":[{\"name\":\"protocol_version\",\"type\":\"uint8\",\"offset\":0}]},"
        "{\"name\":\"BlockRangeUpdate\",\"id\":17,\"protocolVersion\":69,\"fields\":[{\"name\":\"latest_block\",\"type\":\"uint64\",\"offset\":0}]}"
        "],"
        "\"bsc68\":{"
        "\"extends\":\"ethereum-default\","
        "\"remove\":[{\"id\":0,\"protocolVersion\":69},{\"id\":17}],"
        "\"append\":[{\"name\":\"UpgradeStatus\",\"id\":11,\"protocolVersion\":68,\"fields\":[{\"name\":\"disable_peer_tx_broadcast\",\"type\":\"bool\",\"offset\":0}]}]"
        "}"
        "},"
        "\"bnb-smart-chain\":{"
        "\"networkId\":56,"
        "\"genesisHex\":\"d04d8fb63c73eccf2c6b53f18518ef166f68f535c9f040a972a1b4a86c32a2f5\","
        "\"ethMessageSchemaSet\":\"bsc68\","
        "\"nodes\":[],"
        "\"bootnodes\":[]"
        "}"
        "}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "bnb-smart-chain",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->eth_message_schemas.size(), 2U);
    EXPECT_EQ(config->eth_message_schemas[0].message_id, eth::protocol::kStatusMessageId);
    ASSERT_TRUE(config->eth_message_schemas[0].protocol_version.has_value());
    EXPECT_EQ(*config->eth_message_schemas[0].protocol_version, eth::kEthProtocolVersion68);
    EXPECT_EQ(config->eth_message_schemas[1].message_id, eth::protocol::kUpgradeStatusMessageId);
}

TEST_F(ChainPeersTest, LoadChainPeersPrefersEnrWhenGeneratedNodeContainsEnrAndEnode)
{
    const std::string json_text = std::string("{")
        + "\"base-mainnet\":{"
        + "\"networkId\":8453,"
        + "\"genesisHex\":\"f712aa9241cc24369b143cf6dce85f0902a9731e70d66818a3a5845b296c73dd\","
        + "\"forkId\":\"07c9462e\","
        + "\"forkNext\":\"0\","
        + "\"nodes\":[{"
        + "\"enr\":\"enr:-KO4QHJrtDxt4o49eYS5xk-xD8BT6vz8COvE2EbmqThyUWIVMnN2AfSzOVNiWxw6WrqNZo7Irl5T5beXJFgsiiWtI_2GAZ39TVawg2V0aMfGhAfJRi6AgmlkgnY0gmlwhKz__fSJc2VjcDI1NmsxoQLB8g0y6hCJLW6I5eLIKl3GS_IWOyR12ATJLAFAe-QP1oRzbmFwwIN0Y3CCKv6DdWRwgir-\","
        + "\"enode\":\"" + make_enode("127.0.0.1", 30303U, '1') + "\","
        + "\"pubkey\":\"c1f20d32ea10892d6e88e5e2c82a5dc64bf2163b2475d804c92c01407be40fd621d48bb5a789e5ccb496ab90fe6344d59c7964594b41dc94dfd813c12676114c\","
        + "\"score\":0,"
        + "\"lastResponse\":\"0001-01-01T00:00:00Z\","
        + "\"ip\":\"172.255.253.244\","
        + "\"port\":11006"
        + "}],"
        + "\"bootnodes\":[]"
        + "}}";

    const auto peers = discv4::load_chain_peers_from_json_text(
        "base-mainnet",
        json_text);

    ASSERT_EQ(peers.size(), 1U);
    EXPECT_EQ(peers[0].peer.ip, "172.255.253.244");
    EXPECT_EQ(peers[0].peer.tcp_port, 11006U);
    EXPECT_EQ(peers[0].peer.udp_port, 11006U);

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "base-mainnet",
        json_text);
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->discv5_bootnodes.size(), 1U);
    EXPECT_EQ(
        config->discv5_bootnodes.front(),
        "enr:-KO4QHJrtDxt4o49eYS5xk-xD8BT6vz8COvE2EbmqThyUWIVMnN2AfSzOVNiWxw6WrqNZo7Irl5T5beXJFgsiiWtI_2GAZ39TVawg2V0aMfGhAfJRi6AgmlkgnY0gmlwhKz__fSJc2VjcDI1NmsxoQLB8g0y6hCJLW6I5eLIKl3GS_IWOyR12ATJLAFAe-QP1oRzbmFwwIN0Y3CCKv6DdWRwgir-");
}

TEST_F(ChainPeersTest, LoadChainPeerConfigFallsBackToNodeForkIdWhenTopLevelForkIdMissing)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '7') + "\",\"forkId\":\"ed88b5fd\"}"
        + "],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '8') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_TRUE(config->fork_id.has_value());
    EXPECT_EQ(config->fork_id->fork_hash[0], 0xed);
    EXPECT_EQ(config->fork_id->fork_hash[1], 0x88);
    EXPECT_EQ(config->fork_id->fork_hash[2], 0xb5);
    EXPECT_EQ(config->fork_id->fork_hash[3], 0xfd);
    EXPECT_EQ(config->fork_id->next_fork, 0U);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigUsesTopLevelForkFieldsWhenPresent)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"forkId\":\"ed88b5fd\","
        + "\"forkNext\":1735371,"
        + "\"nodes\":["
        + "{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '8') + "\",\"forkId\":\"aaaaaaaa\",\"forkNext\":999}"
        + "],"
        + "\"bootnodes\":["
        + "{\"enode\":\"" + make_enode("10.0.0.2", 30304U, '9') + "\"}"
        + "]}}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    ASSERT_TRUE(config.has_value());
    ASSERT_TRUE(config->fork_id.has_value());
    EXPECT_EQ(config->fork_id->fork_hash[0], 0xed);
    EXPECT_EQ(config->fork_id->fork_hash[1], 0x88);
    EXPECT_EQ(config->fork_id->fork_hash[2], 0xb5);
    EXPECT_EQ(config->fork_id->fork_hash[3], 0xfd);
    EXPECT_EQ(config->fork_id->next_fork, 1735371U);
}

TEST_F(ChainPeersTest, VerifyChainPeerCacheJsonSignatureRejectsUnsignedDocument)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-mainnet\":{"
        + "\"networkId\":1,"
        + "\"genesisHex\":\"d4e56740f876aef8c010b86a40d5f56745a118d0906a34e69aec8c0db1cb8fa3\","
        + "\"nodes\":[{\"enode\":\"" + make_enode("127.0.0.1", 30303U, '9') + "\"}]}"
        + "}";

    const auto verification = discv4::verify_chain_peer_cache_json_signature(
        json_text,
        "0x7c91841f3594cb02dba5aae5909ceaaf2211d454");

    EXPECT_FALSE(verification.has_signature);
    EXPECT_FALSE(verification.signature_valid);
}

TEST_F(ChainPeersTest, LoadChainPeerConfigFromJsonTextRejectsInvalidSignedDocument)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"nodes\":[{\"enode\":\"" + make_enode("127.0.0.1", 30303U, 'a') + "\"}],"
        + "\"bootnodes\":[{\"enode\":\"" + make_enode("10.0.0.2", 30304U, 'b') + "\"}]},"
        + "\"signature\":\"0xbe70d727841ca92d21297e8b062f8f0abab0e00f236f7e12acaccd749d38c9f0e266fc54c86fd7abddf5185597efed8b40473dd0acceb7374c23d13d87e02cb9\","
        + "\"signerAddress\":\"0x7c91841f3594cb02dba5aae5909ceaaf2211d454\""
        + "}";

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    EXPECT_FALSE(config.has_value());
}

TEST_F(ChainPeersTest, LoadChainPeerConfigAcceptsValidTopLevelSignedDocument)
{
    const std::string json_text = std::string("{")
        + "\"ethereum-sepolia\":{"
        + "\"networkId\":11155111,"
        + "\"genesisHex\":\"25a5cc106eea7138acab33231d7160d69cb777ee0c2c553fcddf5138993e6dd9\","
        + "\"nodes\":[{\"enode\":\"" + make_enode("127.0.0.1", 30303U, 'b') + "\"}],"
        + "\"bootnodes\":[{\"enode\":\"" + make_enode("10.0.0.2", 30304U, 'c') + "\"}]},"
        + "\"signature\":\"0xbe70d727841ca92d21297e8b062f8f0abab0e00f236f7e12acaccd749d38c9f0e266fc54c86fd7abddf5185597efed8b40473dd0acceb7374c23d13d87e02cb901\","
        + "\"signerAddress\":\"0x7c91841f3594cb02dba5aae5909ceaaf2211d454\""
        + "}";

    const auto verification = discv4::verify_chain_peer_cache_json_signature(
        json_text,
        "0x7c91841f3594cb02dba5aae5909ceaaf2211d454");

    EXPECT_TRUE(verification.has_signature);
    EXPECT_FALSE(verification.signer_address.empty());

    const auto config = discv4::load_chain_peer_config_from_json_text(
        "ethereum-sepolia",
        json_text);

    if (verification.signature_valid)
    {
        ASSERT_TRUE(config.has_value());
    }
    else
    {
        EXPECT_FALSE(config.has_value());
    }
}
