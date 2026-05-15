// Copyright 2026 Genius Ventures, Inc.
// SPDX-License-Identifier: MIT
//
// Bootnode source implementations for the discv5 crawler.
//
// Data provenance:
//   Ethereum mainnet/Sepolia/Holesky — go-ethereum params/bootnodes.go
//     https://github.com/ethereum/go-ethereum/blob/master/params/bootnodes.go
//   Polygon mainnet/Amoy — docs.polygon.technology/pos/reference/seed-and-bootnodes
//   BSC mainnet/testnet  — github.com/bnb-chain/bsc/blob/master/params/config.go
//   Base mainnet/Sepolia — github.com/base-org/op-geth, github.com/base-org/op-node
//
// !!! Always verify against the latest official sources before production use !!!

#include "discv5/discv5_bootnodes.hpp"

#include <unordered_map>
#include <utility>

namespace discv5
{

// ===========================================================================
// StaticEnrBootnodeSource
// ===========================================================================

StaticEnrBootnodeSource::StaticEnrBootnodeSource(
    std::vector<std::string> enr_uris,
    std::string              name) noexcept
    : enr_uris_(std::move(enr_uris))
    , name_(std::move(name))
{
}

std::vector<std::string> StaticEnrBootnodeSource::fetch() const noexcept
{
    return enr_uris_;
}

std::string StaticEnrBootnodeSource::name() const noexcept
{
    return name_;
}

// ===========================================================================
// StaticEnodeBootnodeSource
// ===========================================================================

StaticEnodeBootnodeSource::StaticEnodeBootnodeSource(
    std::vector<std::string> enode_uris,
    std::string              name) noexcept
    : enode_uris_(std::move(enode_uris))
    , name_(std::move(name))
{
}

std::vector<std::string> StaticEnodeBootnodeSource::fetch() const noexcept
{
    return enode_uris_;
}

std::string StaticEnodeBootnodeSource::name() const noexcept
{
    return name_;
}

// ===========================================================================
// ChainBootnodeRegistry — per-chain factory functions
// ===========================================================================

// ---------------------------------------------------------------------------
// Ethereum Mainnet (chain id 1)
// ENR source: go-ethereum params/bootnodes.go V5Bootnodes
// Verified from: https://github.com/ethereum/go-ethereum/blob/master/params/bootnodes.go
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_ethereum_mainnet() noexcept
{
    static const std::vector<std::string> kEnrUris =
    {
        // Teku team
        "enr:-KG4QMOEswP62yzDjSwWS4YEjtTZ5PO6r65CPqYBkgTTkrpaedQ8uEUo1uMALtJIvb2w_WWEVmg5yt1UAuK1ftxUU7QDhGV0aDKQu6TalgMAAAD__________4JpZIJ2NIJpcIQEnfA2iXNlY3AyNTZrMaEDfol8oLr6XJ7FsdAYE7lpJhKMls4G_v6qQOGKJUWGb_uDdGNwgiMog3VkcIIjKA",
        "enr:-KG4QF4B5WrlFcRhUU6dZETwY5ZzAXnA0vGC__L1Kdw602nDZwXSTs5RFXFIFUnbQJmhNGVU6OIX7KVrCSTODsz1tK4DhGV0aDKQu6TalgMAAAD__________4JpZIJ2NIJpcIQExNYEiXNlY3AyNTZrMaECQmM9vp7KhaXhI-nqL_R0ovULLCFSFTa9CPPSdb1zPX6DdGNwgiMog3VkcIIjKA",
        // Prysm team
        "enr:-Ku4QImhMc1z8yCiNJ1TyUxdcfNucje3BGwEHzodEZUan8PherEo4sF7pPHPSIB1NNuSg5fZy7qFsjmUKs2ea1Whi0EBh2F0dG5ldHOIAAAAAAAAAACEZXRoMpD1pf1CAAAAAP__________gmlkgnY0gmlwhBLf22SJc2VjcDI1NmsxoQOVphkDqal4QzPMksc5wnpuC3gvSC8AfbFOnZY_On34wIN1ZHCCIyg",
        "enr:-Ku4QP2xDnEtUXIjzJ_DhlCRN9SN99RYQPJL92TMlSv7U5C1YnYLjwOQHgZIUXw6c-BvRg2Yc2QsZxxoS_pPRVe0yK8Bh2F0dG5ldHOIAAAAAAAAAACEZXRoMpD1pf1CAAAAAP__________gmlkgnY0gmlwhBLf22SJc2VjcDI1NmsxoQMeFF5GrS7UZpAH2Ly84aLK-TyvH-dRo0JM1i8yygH50YN1ZHCCJxA",
        // EF bootnodes
        "enr:-Ku4QHqVeJ8PPICcWk1vSn_XcSkjOkNiTg6Fmii5j6vUQgvzMc9L1goFnLKgXqBJspJjIsB91LTOleFmyWWrFVATGngBh2F0dG5ldHOIAAAAAAAAAACEZXRoMpC1MD8qAAAAAP__________gmlkgnY0gmlwhAMRHkWJc2VjcDI1NmsxoQKLVXFOhp2uX6jeT0DvvDpPcU8FWMjQdR4wMuORMhpX24N1ZHCCIyg",
        "enr:-Ku4QG-2_Md3sZIAUebGYT6g0SMskIml77l6yR-M_JXc-UdNHCmHQeOiMLbylPejyJsdAPsTHJyjJB2sYGDLe0dn8uYBh2F0dG5ldHOIAAAAAAAAAACEZXRoMpC1MD8qAAAAAP__________gmlkgnY0gmlwhBLY-NyJc2VjcDI1NmsxoQORcM6e19T1T9gi7jxEZjk_sjVLGFscUNqAY9obgZaxbIN1ZHCCIyg",
    };
    return std::make_unique<StaticEnrBootnodeSource>(kEnrUris, "ethereum-mainnet-enr");
}

// ---------------------------------------------------------------------------
// Ethereum Sepolia Testnet (chain id 11155111)
// Source: go-ethereum params/bootnodes.go SepoliaBootnodes
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_ethereum_sepolia() noexcept
{
    static const std::vector<std::string> kEnodeUris =
    {
        "enode://4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b@138.197.51.181:30303",
        "enode://143e11fb766781d22d92a2e33f8f104cddae4411a122295ed1fdb6638de96a6ce65f5b7c964ba3763bba27961738fef7d3ecc739268f3e5e771fb4c87b6234ba@146.190.1.103:30303",
        "enode://8b61dc2d06c3f96fddcbebb0efb29d60d3598650275dc469c22229d3e5620369b0d3dedafd929835fe7f489618f19f456fe7c0df572bf2d914a9f4e006f783a9@170.64.250.88:30303",
        "enode://10d62eff032205fcef19497f35ca8477bea0eadfff6d769a147e895d8b2b8f8ae6341630c645c30f5df6e67547c03494ced3d9c5764e8622a26587b083b028e8@139.59.49.206:30303",
        "enode://9e9492e2e8836114cc75f5b929784f4f46c324ad01daf87d956f98b3b6c5fcba95524d6e5cf9861dc96a2c8a171ea7105bb554a197455058de185fa870970c7c@138.68.123.152:30303",
    };
    return std::make_unique<StaticEnodeBootnodeSource>(kEnodeUris, "ethereum-sepolia-enode");
}

// ---------------------------------------------------------------------------
// Ethereum Holesky Testnet (chain id 17000)
// Source: go-ethereum params/bootnodes.go HoleskyBootnodes
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_ethereum_holesky() noexcept
{
    static const std::vector<std::string> kEnodeUris =
    {
        "enode://ac906289e4b7f12df423d654c5a962b6ebe5b3a74cc9e06292a85221f9a64a6f1cfdd6b714ed6dacef51578f92b34c60ee91e9ede9c7f8fadc4d347326d95e2b@146.190.13.128:30303",
        "enode://a3435a0155a3e837c02f5e7f5662a2f1fbc25b48e4dc232016e1c51b544cb5b4510ef633ea3278c0e970fa8ad8141e2d4d0f9f95456c537ff05fdf9b31c15072@178.128.136.233:30303",
    };
    return std::make_unique<StaticEnodeBootnodeSource>(kEnodeUris, "ethereum-holesky-enode");
}

// ---------------------------------------------------------------------------
// Polygon PoS Mainnet (chain id 137)
// Source: https://docs.polygon.technology/pos/reference/seed-and-bootnodes
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_polygon_mainnet() noexcept
{
    static const std::vector<std::string> kEnodeUris =
    {
        "enode://48e6326841ce106f6b4e229a1be7e98a1d12be57e328b08cb461f6744ae4e78f5ec2340996ce9b40928a1a90137aadea13e25ca34774b52a3600d13a52c5c7bb@34.185.209.56:30303",
        "enode://b8f1cc9c5d4403703fbf377116469667d2b1823c0daf16b7250aa576bacf399e42c3930ccfcb02c5df6879565a2b8931335565f0e8d3f8e72385ecf4a4bf160a@3.36.224.80:30303",
        "enode://8729e0c825f3d9cad382555f3e46dcff21af323e89025a0e6312df541f4a9e73abfa562d64906f5e59c51fe6f0501b3e61b07979606c56329c020ed739910759@54.194.245.5:30303",
    };
    return std::make_unique<StaticEnodeBootnodeSource>(kEnodeUris, "polygon-mainnet-enode");
}

// ---------------------------------------------------------------------------
// Polygon Amoy Testnet (chain id 80002)
// Source: https://docs.polygon.technology/pos/reference/seed-and-bootnodes
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_polygon_amoy() noexcept
{
    static const std::vector<std::string> kEnodeUris =
    {
        "enode://d40ab6b340be9f78179bd1ec7aa4df346d43dc1462d85fb44c5d43f595991d2ec215d7c778a7588906cb4edf175b3df231cecce090986a739678cd3c620bf580@34.89.255.109:30303",
        "enode://13abba15caa024325f2209d3566fa77cd864281dda4f73bca4296277bfd919ac68cef4dbb508028e0310a24f6f9e23c761fa41ac735cdc87efdee76d5ff985a7@34.185.137.160:30303",
        "enode://fc5bd3856a4ce6389eef1d6bc637ce7617e6ba8013f7d722d9878cf13f1c5a5a95a9e26ccb0b38bcc330343941ce117ab50db9f61e72ba450dd528a1184d8e6a@34.89.119.250:30303",
    };
    return std::make_unique<StaticEnodeBootnodeSource>(kEnodeUris, "polygon-amoy-enode");
}

// ---------------------------------------------------------------------------
// BNB Smart Chain Mainnet (chain id 56)
// Source: https://github.com/bnb-chain/bsc/blob/master/params/config.go
// Note: BSC uses port 30311 instead of standard 30303.
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_bsc_mainnet() noexcept
{
    static const std::vector<std::string> kEnodeUris =
    {
        "enode://433c8bfdf53a3e2268ccb1b829e47f629793291cbddf0c76ae626da802f90532251fc558e2e0d10d6725e759088439bf1cd4714716b03a259a35d4b2e4acfa7f@52.69.102.73:30311",
        "enode://bac6a548c7884270d53c3694c93ea43fa87ac1c7219f9f25c9d57f6a2fec9d75441bc4bad1e81d78c049a1c4daf3b1404e2bbb5cd9bf60c0f3a723bbaea110bc@3.255.117.110:30311",
        "enode://94e56c84a5a32e2ef744af500d0ddd769c317d3c3dd42d50f5ea95f5f3718a5f81bc5ce32a7a3ea127bc0f10d3f88f4526a67f5b06c1d85f9cdfc6eb46b2b375@3.255.231.219:30311",
    };
    return std::make_unique<StaticEnodeBootnodeSource>(kEnodeUris, "bsc-mainnet-enode");
}

// ---------------------------------------------------------------------------
// BNB Smart Chain Testnet (chain id 97)
// Source: https://github.com/bnb-chain/bsc/blob/master/params/config.go
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_bsc_testnet() noexcept
{
    static const std::vector<std::string> kEnodeUris =
    {
        "enode://1cc4534b14cfe351ab740a1418ab944a234ca2f702915eadb7e558a02010cb7c5a8c295a3b56bcefa7701c07752acd5539cb13df2aab8ae2d98934d712611443@52.71.43.172:30311",
        "enode://28b1d16562dac280dacaaf45d54516b85bc6c994252a9825c5cc4e080d3e53446d05f63ba495ea7d44d6c316b54cd92b245c5c328c37da24605c4a93a0d099c4@34.246.65.14:30311",
        "enode://5a7b996048d1b0a07683a949662c87c09b55247ce774aeee10bb886892e586e3c604564393292e38ef43c023ee9981e1f8b335766ec4f0f256e57f8640b079d5@35.73.137.11:30311",
    };
    return std::make_unique<StaticEnodeBootnodeSource>(kEnodeUris, "bsc-testnet-enode");
}

// ---------------------------------------------------------------------------
// Base Mainnet (chain id 8453, OP Stack)
// Source: https://github.com/base-org/op-geth
//         https://github.com/base-org/op-node
// Note: Base/OP Stack nodes primarily auto-discover via L1 RPC.
//       Check the op-geth and op-node repositories for the latest list.
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_base_mainnet() noexcept
{
    // Base mainnet uses OP Stack discovery; the canonical seed list lives in
    // the op-node repository.  Returning an empty source so the caller can
    // inject seeds via add_bootnode() at runtime.
    return std::make_unique<StaticEnodeBootnodeSource>(
        std::vector<std::string>{}, "base-mainnet-enode");
}

// ---------------------------------------------------------------------------
// Base Sepolia Testnet (chain id 84532, OP Stack)
// Source: https://docs.base.org/base-chain/node-operators/run-a-base-node
// ---------------------------------------------------------------------------
std::unique_ptr<IBootnodeSource> ChainBootnodeRegistry::make_base_sepolia() noexcept
{
    // Same comment as mainnet — check op-geth/op-node for the latest seeds.
    return std::make_unique<StaticEnodeBootnodeSource>(
        std::vector<std::string>{}, "base-sepolia-enode");
}

// ===========================================================================
// ChainBootnodeRegistry::for_chain (enum overload)
// Uses a switch on the typed enum — not a string if/else chain (M011).
// ===========================================================================

std::unique_ptr<IBootnodeSource>
ChainBootnodeRegistry::for_chain(ChainId chain_id) noexcept
{
    switch (chain_id)
    {
        case ChainId::kEthereumMainnet:   return make_ethereum_mainnet();
        case ChainId::kEthereumSepolia:   return make_ethereum_sepolia();
        case ChainId::kEthereumHolesky:   return make_ethereum_holesky();
        case ChainId::kPolygonMainnet:    return make_polygon_mainnet();
        case ChainId::kPolygonAmoy:       return make_polygon_amoy();
        case ChainId::kBscMainnet:        return make_bsc_mainnet();
        case ChainId::kBscTestnet:        return make_bsc_testnet();
        case ChainId::kBaseMainnet:       return make_base_mainnet();
        case ChainId::kBaseSepolia:       return make_base_sepolia();
        default:                          return nullptr;
    }
}

// ===========================================================================
// ChainBootnodeRegistry::for_chain (integer overload)
// Uses an unordered_map<uint64_t, ChainId> — no if/else string chain (M011).
// ===========================================================================

std::unique_ptr<IBootnodeSource>
ChainBootnodeRegistry::for_chain(uint64_t chain_id_int) noexcept
{
    static const std::unordered_map<uint64_t, ChainId> kChainMap =
    {
        { static_cast<uint64_t>(ChainId::kEthereumMainnet),  ChainId::kEthereumMainnet  },
        { static_cast<uint64_t>(ChainId::kEthereumSepolia),  ChainId::kEthereumSepolia  },
        { static_cast<uint64_t>(ChainId::kEthereumHolesky),  ChainId::kEthereumHolesky  },
        { static_cast<uint64_t>(ChainId::kPolygonMainnet),   ChainId::kPolygonMainnet   },
        { static_cast<uint64_t>(ChainId::kPolygonAmoy),      ChainId::kPolygonAmoy      },
        { static_cast<uint64_t>(ChainId::kBscMainnet),       ChainId::kBscMainnet       },
        { static_cast<uint64_t>(ChainId::kBscTestnet),       ChainId::kBscTestnet       },
        { static_cast<uint64_t>(ChainId::kBaseMainnet),      ChainId::kBaseMainnet      },
        { static_cast<uint64_t>(ChainId::kBaseSepolia),      ChainId::kBaseSepolia      },
    };

    const auto it = kChainMap.find(chain_id_int);
    if (it == kChainMap.end())
    {
        return nullptr;
    }
    return for_chain(it->second);
}

// ===========================================================================
// ChainBootnodeRegistry::chain_name
// ===========================================================================

const char* ChainBootnodeRegistry::chain_name(ChainId chain_id) noexcept
{
    switch (chain_id)
    {
        case ChainId::kEthereumMainnet:  return "ethereum-mainnet";
        case ChainId::kEthereumSepolia:  return "ethereum-sepolia";
        case ChainId::kEthereumHolesky:  return "ethereum-holesky";
        case ChainId::kPolygonMainnet:   return "polygon-mainnet";
        case ChainId::kPolygonAmoy:      return "polygon-amoy";
        case ChainId::kBscMainnet:       return "bsc-mainnet";
        case ChainId::kBscTestnet:       return "bsc-testnet";
        case ChainId::kBaseMainnet:      return "base-mainnet";
        case ChainId::kBaseSepolia:      return "base-sepolia";
        default:                         return "unknown";
    }
}

} // namespace discv5
