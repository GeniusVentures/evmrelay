// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#ifndef EVMRELAY_INCLUDE_ETH_ETH_WATCH_CLI_HPP
#define EVMRELAY_INCLUDE_ETH_ETH_WATCH_CLI_HPP

#include <base/parse_utility.hpp>
#include <eth/abi_decoder.hpp>
#include <eth/eth_watch_service.hpp>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eth::cli {

/// @brief One watch subscription specified on the command line.
struct WatchSpec
{
    std::string contract_hex;    ///< 40 hex chars (20 bytes), empty = any contract
    std::string event_signature; ///< e.g. "Transfer(address,address,uint256)"
};

/// @brief Parse a 0x-prefixed or bare 40-hex-char Ethereum address.
/// @return Parsed address or nullopt if the format is invalid.
[[nodiscard]] inline std::optional<codec::Address> parse_address(std::string_view hex) noexcept
{
    codec::Address addr{};
    if (!rlp::base::parse::hex_array(hex, addr)) { return std::nullopt; }
    return addr;
}

// ---------------------------------------------------------------------------
// Known-event registry
// ---------------------------------------------------------------------------

/// @brief Registry mapping event signatures to their ABI parameter descriptors.
///
/// Pre-populated with common GNUS/ERC-20/bridge events.  External code can
/// call `event_registry().register_event(sig, params)` to add project-specific
/// events before starting the watcher.
///
/// Example — adding a custom event:
/// @code
///   eth::cli::event_registry().register_event(
///       "MyEvent(address,uint256)",
///       { {eth::abi::AbiParamKind::kAddress, true,  "owner"},
///         {eth::abi::AbiParamKind::kUint,    false, "value"} });
/// @endcode
class EventRegistry
{
public:
    /// @brief Register (or overwrite) the ABI parameter list for @p sig.
    void register_event(std::string sig, std::vector<abi::AbiParam> params)
    {
        map_[std::move(sig)] = std::move(params);
    }

    /// @brief Look up ABI parameters for @p sig.
    /// @return Pointer to the parameter list, or nullptr if unknown.
    [[nodiscard]] const std::vector<abi::AbiParam>* lookup(const std::string& sig) const noexcept
    {
        auto it = map_.find(sig);
        return (it != map_.end()) ? &it->second : nullptr;
    }

    /// @brief Convenience: return params for @p sig, or empty vector if unknown.
    [[nodiscard]] std::vector<abi::AbiParam> params_for(const std::string& sig) const
    {
        if (const auto* p = lookup(sig)) { return *p; }
        return {};
    }

private:
    std::unordered_map<std::string, std::vector<abi::AbiParam>> map_;
};

/// @brief Process-wide singleton registry, pre-populated with well-known events.
///
/// The registry is initialised on first access.  All registrations persist
/// for the lifetime of the process, so call `register_event()` once at
/// startup before creating any watchers.
inline EventRegistry& event_registry()
{
    static EventRegistry reg = []()
    {
        EventRegistry r;

        // ── ERC-20 ────────────────────────────────────────────────────────────
        r.register_event("Transfer(address,address,uint256)", {
            {abi::AbiParamKind::kAddress, true,  "from"},
            {abi::AbiParamKind::kAddress, true,  "to"},
            {abi::AbiParamKind::kUint,    false, "value"},
        });
        r.register_event("Approval(address,address,uint256)", {
            {abi::AbiParamKind::kAddress, true,  "owner"},
            {abi::AbiParamKind::kAddress, true,  "spender"},
            {abi::AbiParamKind::kUint,    false, "value"},
        });

        // ── ERC-721 ───────────────────────────────────────────────────────────
        // Note: Transfer(address,address,uint256) is the same signature as ERC-20.
        // The third param is indexed `tokenId` in ERC-721 vs non-indexed `value`
        // in ERC-20.  Register it separately under a distinct key so callers can
        // opt in explicitly; the ERC-20 entry above is the default.
        r.register_event("Transfer(address,address,uint256 indexed)", {
            {abi::AbiParamKind::kAddress, true,  "from"},
            {abi::AbiParamKind::kAddress, true,  "to"},
            {abi::AbiParamKind::kUint,    true,  "tokenId"},
        });
        r.register_event("ApprovalForAll(address,address,bool)", {
            {abi::AbiParamKind::kAddress, true,  "owner"},
            {abi::AbiParamKind::kAddress, true,  "operator"},
            {abi::AbiParamKind::kBool,    false, "approved"},
        });

        // ── ERC-1155 ──────────────────────────────────────────────────────────
        r.register_event("TransferSingle(address,address,address,uint256,uint256)", {
            {abi::AbiParamKind::kAddress, true,  "operator"},
            {abi::AbiParamKind::kAddress, true,  "from"},
            {abi::AbiParamKind::kAddress, true,  "to"},
            {abi::AbiParamKind::kUint,    false, "id"},
            {abi::AbiParamKind::kUint,    false, "value"},
        });
        r.register_event("TransferBatch(address,address,address,uint256[],uint256[])", {
            {abi::AbiParamKind::kAddress, true,  "operator"},
            {abi::AbiParamKind::kAddress, true,  "from"},
            {abi::AbiParamKind::kAddress, true,  "to"},
            // uint256[] arrays decoded as raw bytes32 until dynamic array support is added
            {abi::AbiParamKind::kBytes32, false, "ids"},
            {abi::AbiParamKind::kBytes32, false, "values"},
        });

        // ── GNUS Bridge (GNUSBridge.sol) ──────────────────────────────────────
        // event BridgeSourceBurned(address indexed sender, uint256 id, uint256 amount,
        //                          uint256 srcChainID, uint256 destChainID, bytes sgnsDestination)
        r.register_event("BridgeSourceBurned(address,uint256,uint256,uint256,uint256,bytes)", {
            {abi::AbiParamKind::kAddress, true,  "sender"},
            {abi::AbiParamKind::kUint,    false, "id"},
            {abi::AbiParamKind::kUint,    false, "amount"},
            {abi::AbiParamKind::kUint,    false, "srcChainID"},
            {abi::AbiParamKind::kUint,    false, "destChainID"},
            {abi::AbiParamKind::kBytes,   false, "sgnsDestination"},
        });

        return r;
    }();
    return reg;
}

/// @brief Convenience wrapper: look up @p sig in the global registry.
///        Replaces the old `infer_params()` free function.
[[nodiscard]] inline std::vector<abi::AbiParam> infer_params(const std::string& sig)
{
    return event_registry().params_for(sig);
}

/// @brief Convert CLI watch flags into service-level watch specs.
[[nodiscard]] inline std::optional<std::vector<EthWatchEventSpec>> build_service_watch_specs(
    const std::vector<WatchSpec>& watch_specs)
{
    std::vector<EthWatchEventSpec> service_watches;
    service_watches.reserve(watch_specs.size());

    for (const auto& spec : watch_specs)
    {
        EthWatchEventSpec watch{};
        if (!spec.contract_hex.empty())
        {
            auto addr = parse_address(spec.contract_hex);
            if (!addr)
            {
                return std::nullopt;
            }
            watch.contract_address = *addr;
        }

        watch.event_signature = spec.event_signature;
        watch.params = infer_params(spec.event_signature);
        service_watches.push_back(std::move(watch));
    }

    return service_watches;
}

/// @brief Build the production service config used by cache-backed CLI modes.
[[nodiscard]] inline EthWatchServiceConfig build_service_config(
    EthWatchConnectionConfig                    connection,
    std::vector<EthWatchEventSpec>              watches,
    std::vector<discv4::ChainPeerConfig>        chains,
    EthWatchDiscoveryMode                       discovery_mode = EthWatchDiscoveryMode::kDiscoverIfNeeded)
{
    EthWatchServiceConfig service_config{};
    service_config.connection = connection;
    service_config.watches = std::move(watches);
    service_config.chains = std::move(chains);
    service_config.discovery_mode = discovery_mode;
    return service_config;
}

} // namespace eth::cli

#endif // EVMRELAY_INCLUDE_ETH_ETH_WATCH_CLI_HPP
