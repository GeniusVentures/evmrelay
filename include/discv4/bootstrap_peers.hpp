// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <discv4/dial_scheduler.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace discv4
{

/**
 * @brief Result of refreshing the local bootstrap peer cache from a remote URL.
 */
struct BootstrapCacheRefreshResult
{
    std::filesystem::path cache_path;
    bool                  cache_available = false;
    bool                  cache_updated = false;
};

/**
 * @brief Return the default local cache path for `chain_enodes.json` next to the executable.
 * @param argv0 Program path used to locate the executable directory.
 * @return Cache file path.
 */
std::filesystem::path bootstrap_cache_json_path(const std::string& argv0);

/**
 * @brief Locate a local bootstrap peer JSON file.
 * @param argv0 Program path used to locate the executable directory.
 * @param override_path Optional explicit file path.
 * @return Existing JSON or gzip-compressed JSON file path, or `std::nullopt` if none exists.
 */
std::optional<std::filesystem::path> find_bootstrap_peers_json_path(
    const std::string& argv0,
    const std::string& override_path);

/**
 * @brief Download the bootstrap peer JSON payload from a remote URL.
 *        Supports either raw JSON or gzip-compressed JSON bodies.
 * @param url Source URL.
 * @return Decoded JSON text, or `std::nullopt` on failure.
 */
std::optional<std::string> download_bootstrap_json(const std::string& url);

/**
 * @brief Write the bootstrap peer JSON cache only when the contents changed.
 * @param json_path Destination cache path.
 * @param json_text JSON text to store.
 * @return `true` when the cache contents changed and were rewritten.
 */
bool write_bootstrap_cache_json_if_changed(
    const std::filesystem::path& json_path,
    const std::string&           json_text);

/**
 * @brief Refresh a local bootstrap peer cache from a remote URL.
 * @param json_path Destination cache path.
 * @param url Source URL.
 * @return Refresh metadata when download succeeded, or `std::nullopt` on failure.
 */
std::optional<BootstrapCacheRefreshResult> refresh_bootstrap_cache_json(
    const std::filesystem::path& json_path,
    const std::string&           url);

/**
 * @brief Parse an `enode://` bootstrap peer URI into a validated peer entry.
 * @param enode Bootstrap peer URI.
 * @return Parsed validated peer, or `std::nullopt` when the URI is malformed.
 */
std::optional<ValidatedPeer> make_validated_peer_from_enode(const std::string& enode);

/**
 * @brief Parse an `enr:` bootstrap peer URI into a validated peer entry.
 * @param enr_uri Bootstrap ENR URI.
 * @return Parsed validated peer, or `std::nullopt` when the URI is malformed.
 */
std::optional<ValidatedPeer> make_validated_peer_from_enr(const std::string& enr_uri);

/**
 * @brief Load bootstrap peers for a specific chain from JSON text.
 * @param chain_name Canonical top-level JSON key.
 * @param json_text JSON document contents.
 * @return Parsed validated peers.
 */
std::vector<ValidatedPeer> load_bootstrap_peers_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);

/**
 * @brief Load bootstrap peers for a specific chain from a JSON or gzip-compressed JSON file.
 * @param chain_name Canonical top-level JSON key.
 * @param json_path JSON file path.
 * @return Parsed validated peers.
 */
std::vector<ValidatedPeer> load_bootstrap_peers_from_json(
    const std::string&            chain_name,
    const std::filesystem::path&  json_path);

} // namespace discv4

