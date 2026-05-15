// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <discv5/discv5_error.hpp>
#include <discv5/discv5_types.hpp>

namespace discv5
{

// ---------------------------------------------------------------------------
// IBootnodeSource — abstract source of bootstrap seed records
// ---------------------------------------------------------------------------

/// @brief Abstract interface for providers of bootstrap seed records.
///
/// A concrete implementation may serve static ENR strings, static enode URIs,
/// or query a remote registry.  The crawler calls @p fetch() once at startup
/// to obtain the initial seed set.
///
/// Implementations must be const-correct and noexcept on @p fetch().
class IBootnodeSource
{
public:
    virtual ~IBootnodeSource() = default;

    /// @brief Return all bootstrap ENR URIs available from this source.
    ///
    /// ENR URIs are preferred; the crawler will attempt to parse each one via
    /// EnrParser.  Enode URIs are also accepted and will be converted to a
    /// minimal ValidatedPeer without an EnrRecord.
    ///
    /// @return  Vector of URI strings.  May be empty if no seeds are available.
    [[nodiscard]] virtual std::vector<std::string> fetch() const noexcept = 0;

    /// @brief Human-readable name of this source, for logging.
    [[nodiscard]] virtual std::string name() const noexcept = 0;
};

// ---------------------------------------------------------------------------
// StaticEnrBootnodeSource
// ---------------------------------------------------------------------------

/// @brief Bootstrap source backed by a fixed list of "enr:…" URI strings.
///
/// Suitable for Ethereum mainnet, Sepolia, and any chain that publishes ENR
/// boot nodes (e.g. in go-ethereum's params/bootnodes.go).
class StaticEnrBootnodeSource : public IBootnodeSource
{
public:
    /// @brief Construct with a pre-built list of ENR URI strings.
    ///
    /// @param enr_uris  Vector of "enr:…" strings.
    /// @param name      Human-readable label for logging.
    explicit StaticEnrBootnodeSource(
        std::vector<std::string> enr_uris,
        std::string              name = "static-enr") noexcept;

    [[nodiscard]] std::vector<std::string> fetch() const noexcept override;
    [[nodiscard]] std::string              name()  const noexcept override;

private:
    std::vector<std::string> enr_uris_;
    std::string              name_;
};

// ---------------------------------------------------------------------------
// StaticEnodeBootnodeSource
// ---------------------------------------------------------------------------

/// @brief Bootstrap source backed by a fixed list of "enode://…" URI strings.
///
/// Used for chains that only publish enode boot nodes (e.g. BSC, Polygon).
/// The crawler will create a minimal ValidatedPeer for each entry.
class StaticEnodeBootnodeSource : public IBootnodeSource
{
public:
    /// @brief Construct with a pre-built list of enode URI strings.
    ///
    /// @param enode_uris  Vector of "enode://…" strings.
    /// @param name        Human-readable label for logging.
    explicit StaticEnodeBootnodeSource(
        std::vector<std::string> enode_uris,
        std::string              name = "static-enode") noexcept;

    [[nodiscard]] std::vector<std::string> fetch() const noexcept override;
    [[nodiscard]] std::string              name()  const noexcept override;

private:
    std::vector<std::string> enode_uris_;
    std::string              name_;
};

// ---------------------------------------------------------------------------
// ChainId
// ---------------------------------------------------------------------------

/// @brief EVM chain identifiers supported by ChainBootnodeRegistry.
///
/// Add new entries here when additional chains need to be supported.
enum class ChainId : uint64_t
{
    kEthereumMainnet   = 1,
    kEthereumSepolia   = 11155111,
    kEthereumHolesky   = 17000,
    kPolygonMainnet    = 137,
    kPolygonAmoy       = 80002,
    kBscMainnet        = 56,
    kBscTestnet        = 97,
    kBaseMainnet       = 8453,
    kBaseSepolia       = 84532,
};

// ---------------------------------------------------------------------------
// ChainBootnodeRegistry
// ---------------------------------------------------------------------------

/// @brief Per-chain bootstrap seed registry.
///
/// Returns a concrete @p IBootnodeSource for a given @p ChainId.  Data is
/// sourced from officially maintained chain documentation and client repos,
/// not from stale guesses; see comments on each chain's factory function.
///
/// @note
/// The returned source should be treated as a starting point.  Always verify
/// against the latest official sources before depending on these entries in
/// production:
///  - Ethereum:  https://github.com/ethereum/go-ethereum/blob/master/params/bootnodes.go
///  - Polygon:   https://docs.polygon.technology/pos/reference/seed-and-bootnodes
///  - BSC:       https://docs.bnbchain.org/bnb-smart-chain/developers/node_operators/boot_node
///  - Base:      https://github.com/base-org/op-geth / https://github.com/base-org/op-node
class ChainBootnodeRegistry
{
public:
    ChainBootnodeRegistry() = default;

    /// @brief Return an @p IBootnodeSource for the requested chain.
    ///
    /// @param chain_id  Numeric EVM chain identifier.
    /// @return          Owning pointer to the source, or nullptr when the chain
    ///                  is not registered (caller should check).
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> for_chain(ChainId chain_id) noexcept;

    /// @brief Convenience overload that accepts a raw integer chain id.
    ///
    /// @param chain_id_int  EVM chain id integer.
    /// @return              Owning pointer, or nullptr when unrecognised.
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> for_chain(uint64_t chain_id_int) noexcept;

    /// @brief Return the human-readable name for a chain identifier.
    ///
    /// @param chain_id  Chain enum value.
    /// @return          Name string (e.g. "ethereum-mainnet").
    [[nodiscard]] static const char* chain_name(ChainId chain_id) noexcept;

private:
    // Per-chain factory helpers.
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_ethereum_mainnet()  noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_ethereum_sepolia()  noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_ethereum_holesky()  noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_polygon_mainnet()   noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_polygon_amoy()      noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_bsc_mainnet()       noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_bsc_testnet()       noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_base_mainnet()      noexcept;
    [[nodiscard]] static std::unique_ptr<IBootnodeSource> make_base_sepolia()      noexcept;
};

} // namespace discv5
