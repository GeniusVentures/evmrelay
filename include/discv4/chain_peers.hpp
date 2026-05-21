// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_DISCV4_CHAIN_PEERS_HPP
#define EVMRELAY_INCLUDE_DISCV4_CHAIN_PEERS_HPP

#include <discv4/dial_scheduler.hpp>
#include <eth/eth_types.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace discv4
{

enum class ChainDiscoveryDefault
{
    kAuto,
    kDiscv4,
    kCacheEnrDiscv5,
    kEnrTree
};

/**
 * @brief Result of refreshing the local chain peer cache from a remote URL.
 */
struct ChainPeerCacheRefreshResult
{
    std::filesystem::path cache_path;
    bool                  cache_available = false;
    bool                  cache_updated = false;
};

/**
 * @brief Parsed chain configuration shared across peer discovery and ETH session setup.
 */
struct ChainPeerConfig
{
    std::string                canonical_name;
    uint64_t                   network_id = 0;
    eth::Hash256               genesis_hash{};
    std::vector<ValidatedPeer> nodes;
    std::vector<ValidatedPeer> bootnodes;
    std::vector<std::string>   discv5_bootnodes;
    std::vector<std::string>   enr_trees;
    ChainDiscoveryDefault      discovery_default = ChainDiscoveryDefault::kAuto;
    std::optional<eth::ForkId> fork_id;
    std::string                signature;
    std::string                signer_address;
};

/**
 * @brief Verification result for a signed chain peer cache JSON document.
 */
struct ChainPeerCacheSignatureVerificationResult
{
    bool        has_signature = false;
    bool        signature_valid = false;
    std::string signer_address;
};

/**
 * @brief Return the default local cache path for `chain_enodes.json` next to the executable.
 * @param argv0 Program path used to locate the executable directory.
 * @return Cache file path.
 */
std::filesystem::path chain_peer_cache_json_path(const std::string& argv0);

/**
 * @brief Locate a local chain peer cache JSON file.
 * @param argv0 Program path used to locate the executable directory.
 * @param override_path Optional explicit file path.
 * @return Existing JSON or gzip-compressed JSON file path, or `std::nullopt` if none exists.
 */
std::optional<std::filesystem::path> find_chain_peer_cache_json_path(
    const std::string& argv0,
    const std::string& override_path);

/**
 * @brief Download the chain peer cache JSON payload from a remote URL.
 *        Supports either raw JSON or gzip-compressed JSON bodies.
 * @param url Source URL.
 * @return Decoded JSON text, or `std::nullopt` on failure.
 */
std::optional<std::string> download_chain_peer_cache_json(const std::string& url);

/**
 * @brief Write the chain peer JSON cache only when the contents changed.
 * @param json_path Destination cache path.
 * @param json_text JSON text to store.
 * @return `true` when the cache contents changed and were rewritten.
 */
bool write_chain_peer_cache_json_if_changed(
    const std::filesystem::path& json_path,
    const std::string&           json_text);

/**
 * @brief Refresh a local chain peer cache from a remote URL.
 * @param json_path Destination cache path.
 * @param url Source URL.
 * @return Refresh metadata when download succeeded, or `std::nullopt` on failure.
 */
std::optional<ChainPeerCacheRefreshResult> refresh_chain_peer_cache_json(
    const std::filesystem::path& json_path,
    const std::string&           url);

/**
 * @brief Parse an `enode://` peer URI into a validated peer entry.
 * @param enode Peer URI.
 * @return Parsed validated peer, or `std::nullopt` when the URI is malformed.
 */
std::optional<ValidatedPeer> make_validated_peer_from_enode(const std::string& enode);

/**
 * @brief Parse an `enr:` peer URI into a validated peer entry.
 * @param enr_uri Peer ENR URI.
 * @return Parsed validated peer, or `std::nullopt` when the URI is malformed.
 */
std::optional<ValidatedPeer> make_validated_peer_from_enr(const std::string& enr_uri);

/**
 * @brief Load cached peers for a specific chain from JSON text.
 * @param chain_name Canonical top-level JSON key.
 * @param json_text JSON document contents.
 * @return Parsed validated peers.
 */
std::vector<ValidatedPeer> load_chain_peers_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);

/**
 * @brief Load cached peers for a specific chain from a JSON or gzip-compressed JSON file.
 * @param chain_name Canonical top-level JSON key.
 * @param json_path JSON file path.
 * @return Parsed validated peers.
 */
std::vector<ValidatedPeer> load_chain_peers_from_json(
    const std::string&            chain_name,
    const std::filesystem::path&  json_path);

/**
 * @brief Load the first available peer fork hash for a specific chain from JSON text.
 * @param chain_name Canonical top-level JSON key.
 * @param json_text JSON document contents.
 * @return 4-byte fork hash when present, or `std::nullopt`.
 */
std::optional<std::array<uint8_t, 4>> load_chain_fork_id_hash_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);

/**
 * @brief Load the first available peer fork hash for a specific chain from a JSON or gzip-compressed JSON file.
 * @param chain_name Canonical top-level JSON key.
 * @param json_path JSON file path.
 * @return 4-byte fork hash when present, or `std::nullopt`.
 */
std::optional<std::array<uint8_t, 4>> load_chain_fork_id_hash_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path);

/**
 * @brief Load the parsed chain configuration from JSON text.
 * @param chain_name Canonical top-level JSON key.
 * @param json_text JSON document contents.
 * @return Parsed chain config, or `std::nullopt` when missing or invalid.
 */
std::optional<ChainPeerConfig> load_chain_peer_config_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);

/**
 * @brief Load the parsed chain configuration from a JSON or gzip-compressed JSON file.
 * @param chain_name Canonical top-level JSON key.
 * @param json_path JSON file path.
 * @return Parsed chain config, or `std::nullopt` when missing or invalid.
 */
std::optional<ChainPeerConfig> load_chain_peer_config_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path);

/**
 * @brief Verify the top-level `chain_enodes.json` signature.
 * @param json_text JSON document contents.
 * @param expected_signer_address Expected Ethereum signer address.
 * @return Verification result.
 */
ChainPeerCacheSignatureVerificationResult verify_chain_peer_cache_json_signature(
    const std::string& json_text,
    const std::string& expected_signer_address);

} // namespace discv4

#endif // EVMRELAY_INCLUDE_DISCV4_CHAIN_PEERS_HPP
