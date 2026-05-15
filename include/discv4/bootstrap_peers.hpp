// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <discv4/chain_peers.hpp>

namespace discv4
{

using BootstrapCacheRefreshResult = ChainPeerCacheRefreshResult;
using BootstrapChainConfig = ChainPeerConfig;
using BootstrapSignatureVerificationResult = ChainPeerCacheSignatureVerificationResult;

std::filesystem::path bootstrap_cache_json_path(const std::string& argv0);
std::optional<std::filesystem::path> find_bootstrap_peers_json_path(
    const std::string& argv0,
    const std::string& override_path);
std::optional<std::string> download_bootstrap_json(const std::string& url);
bool write_bootstrap_cache_json_if_changed(
    const std::filesystem::path& json_path,
    const std::string&           json_text);
std::optional<BootstrapCacheRefreshResult> refresh_bootstrap_cache_json(
    const std::filesystem::path& json_path,
    const std::string&           url);
std::vector<ValidatedPeer> load_bootstrap_peers_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);
std::vector<ValidatedPeer> load_bootstrap_peers_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path);
std::optional<std::array<uint8_t, 4>> load_bootstrap_fork_id_hash_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);
std::optional<std::array<uint8_t, 4>> load_bootstrap_fork_id_hash_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path);
std::optional<BootstrapChainConfig> load_bootstrap_chain_config_from_json_text(
    const std::string& chain_name,
    const std::string& json_text);
std::optional<BootstrapChainConfig> load_bootstrap_chain_config_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path);
BootstrapSignatureVerificationResult verify_bootstrap_json_signature(
    const std::string& json_text,
    const std::string& expected_signer_address);

} // namespace discv4
