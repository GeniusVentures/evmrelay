// Copyright 2025 GeniusVentures
// SPDX-License-Identifier: Apache-2.0

#include <discv4/bootstrap_peers.hpp>

namespace discv4
{

std::filesystem::path bootstrap_cache_json_path(const std::string& argv0)
{
    return chain_peer_cache_json_path(argv0);
}

std::optional<std::filesystem::path> find_bootstrap_peers_json_path(
    const std::string& argv0,
    const std::string& override_path)
{
    return find_chain_peer_cache_json_path(argv0, override_path);
}

std::optional<std::string> download_bootstrap_json(const std::string& url)
{
    return download_chain_peer_cache_json(url);
}

bool write_bootstrap_cache_json_if_changed(
    const std::filesystem::path& json_path,
    const std::string&           json_text)
{
    return write_chain_peer_cache_json_if_changed(json_path, json_text);
}

std::optional<BootstrapCacheRefreshResult> refresh_bootstrap_cache_json(
    const std::filesystem::path& json_path,
    const std::string&           url)
{
    return refresh_chain_peer_cache_json(json_path, url);
}

std::vector<ValidatedPeer> load_bootstrap_peers_from_json_text(
    const std::string& chain_name,
    const std::string& json_text)
{
    return load_chain_peers_from_json_text(chain_name, json_text);
}

std::vector<ValidatedPeer> load_bootstrap_peers_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path)
{
    return load_chain_peers_from_json(chain_name, json_path);
}

std::optional<std::array<uint8_t, 4>> load_bootstrap_fork_id_hash_from_json_text(
    const std::string& chain_name,
    const std::string& json_text)
{
    return load_chain_fork_id_hash_from_json_text(chain_name, json_text);
}

std::optional<std::array<uint8_t, 4>> load_bootstrap_fork_id_hash_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path)
{
    return load_chain_fork_id_hash_from_json(chain_name, json_path);
}

std::optional<BootstrapChainConfig> load_bootstrap_chain_config_from_json_text(
    const std::string& chain_name,
    const std::string& json_text)
{
    return load_chain_peer_config_from_json_text(chain_name, json_text);
}

std::optional<BootstrapChainConfig> load_bootstrap_chain_config_from_json(
    const std::string&           chain_name,
    const std::filesystem::path& json_path)
{
    return load_chain_peer_config_from_json(chain_name, json_path);
}

BootstrapSignatureVerificationResult verify_bootstrap_json_signature(
    const std::string& json_text,
    const std::string& expected_signer_address)
{
    return verify_chain_peer_cache_json_signature(json_text, expected_signer_address);
}

} // namespace discv4
