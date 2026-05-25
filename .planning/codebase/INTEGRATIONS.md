# External Integrations

**Analysis Date:** 2026-05-25

## APIs & External Services

### Ethereum P2P Network (DevP2P)

**Overview:** The primary integration method is direct peer-to-peer connections to Ethereum-compatible blockchain nodes via the DevP2P protocol stack. No centralized RPC dependency is required for core operation.

**Protocol Layers:**
- **Node Discovery (Discv4):** UDP-based peer discovery. Implemented in `include/discv4/` and `src/discv4/`. Uses `Snappy` compression, secp256k1 signing, and SHA3 hashing for packet authentication.
  - Bootstrap node lists from `chain_enodes.json(.gz)` cache files
  - ENR (EIP-778) record requests for peer metadata
- **Node Discovery (Discv5):** UDP-based discovery with topic-based advertisement. Implemented in `include/discv5/` and `src/discv5/`.
  - ENR record resolution and fork filter validation
  - ENR-tree DNS discovery (EIP-1459) via DNS TXT records
- **RLPx Transport:** TCP-based encrypted transport layer. Implemented in `include/rlpx/` and `src/rlpx/`.
  - ECDH key exchange via secp256k1 (`include/rlpx/crypto/ecdh.hpp`)
  - ECIES encryption with AES-256-CTR + HMAC-SHA256 (`include/rlpx/auth/ecies_cipher.hpp`)
  - Message framing with AES/HMAC frame ciphers (`include/rlpx/framing/frame_cipher.hpp`)
  - Boost.Asio coroutine-based session management (`include/rlpx/rlpx_session.hpp`)
- **ETH Subprotocol (eth/68, eth/69):** Ethereum wire protocol. Implemented in `include/eth/`.
  - Handshake: ETH Status message exchange with fork ID validation (`include/eth/eth_handshake.hpp`)
  - Message types: NewBlockHashes, Transactions, GetBlockHeaders, BlockHeaders, GetBlockBodies, BlockBodies, NewBlock, NewPooledTransactionHashes, GetPooledTransactions, PooledTransactions, GetReceipts, Receipts (`examples/chains_config.json`)
  - Chain-specific schema sets: `ethereum-default`, `polygon-bor`, `bsc68`, `base-op-geth`, `gnosis-geth-like`

### ENR-Tree DNS Discovery (EIP-1459)

- **Ethereum Mainnet:** `enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.mainnet.ethdisco.net`
- **Ethereum Sepolia:** `enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.sepolia.ethdisco.net`
- **Ethereum Holesky:** `enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.holesky.ethdisco.net`
- **Ethereum Hoodi:** `enrtree://AKA3AM6LPBYEUDMVNU3BSVQJ5AD45Y7YPOHJLEF6W26QOE4VTUDPE@all.hoodi.ethdisco.net`
- **Polygon Mainnet:** `enrtree://AKUEZKN7PSKVNR65FZDHECMKOJQSGPARGTPPBI7WS2VUL4EGR6XPC@pos.polygon-peers.io`
- **Polygon Amoy:** `enrtree://AKUEZKN7PSKVNR65FZDHECMKOJQSGPARGTPPBI7WS2VUL4EGR6XPC@amoy.polygon-peers.io`

Implementation: `include/discv5/enr_tree.hpp` — DNS TXT record lookup, ENR URI parsing and resolution via `EnrTreeResolver`.

### GNUS.AI Chain Peer Cache

- **Service:** `https://enodes.gnus.ai/chain_enodes.json.gz`
- **Purpose:** Pre-crawled, ranked, fork-filtered peer lists for all supported EVM chains
- **Format:** Gzip-compressed JSON; keys: `ethereum-mainnet`, `ethereum-sepolia`, `ethereum-holesky`, `polygon-mainnet`, `polygon-amoy`, `bnb-smart-chain`, `bnb-smart-chain-testnet`, `base-mainnet`, `base-sepolia`, `gnosis-chain`
- **Client:** `src/discv4/chain_peers.cpp` — `refresh_chain_peer_cache_json()` downloads and caches the file locally next to the executable
- **Reference:** `examples/README.md` line 195: `https://enodes.gnus.ai/chain_enodes.json.gz`

### Ethereum Discv4 DNS Lists

- **Service:** `https://github.com/ethereum/discv4-dns-lists` (all.json crawl data)
- **Purpose:** Source data for the `rlp_enodes/` Go crawler tool that generates filtered chain peer lists
- **Client:** `rlp_enodes/app.go` — downloads all.json, filters by chain ID and fork ID, ranks by score/recency

### JSON-RPC API (Optional Fallback)

**Purpose:** Alternative receipt source when P2P connection is unavailable. Used for:
- Backfill of missed blocks (`include/eth/rpc_receipt_source.hpp`)
- Finality head tracking (`include/eth/finality_policy.hpp`)
- Transaction receipt lookup (`include/eth/json_rpc.hpp` methods: `eth_getBlockByNumber`, `eth_getLogs`, `eth_getTransactionReceipt`)

**Transport:** HTTP/HTTPS via Boost.Beast (`include/eth/rpc_http_transport.hpp`)

**Supported chains for public RPC (examples/send_test_transactions.sh):**
| Chain | Default RPC URL |
|-------|----------------|
| Ethereum | `https://rpc.ankr.com/eth` |
| Polygon | `https://polygon-rpc.com` |
| BSC | `https://bsc-dataseed.binance.org` |
| Base | `https://mainnet.base.org` |
| Sepolia | `https://ethereum-sepolia-rpc.publicnode.com` |
| Polygon Amoy | `https://rpc-amoy.polygon.technology` |
| BSC Testnet | `https://data-seed-prebsc-1-s1.binance.org:8545` |
| Base Sepolia | `https://sepolia.base.org` |

**RPC Configuration:** JSON-based endpoint config with API key support via environment variable templates (`include/eth/rpc_manager_config.hpp`):
- `url_template` with `{key}` placeholder substitution
- `api_key_env_var` for environment variable lookup
- Priority, weight, and rate limiting per endpoint
- Endpoint states: `kAvailable`, `kTemporarilyFailed`, `kDisabled`

## Data Storage

**Databases:**
- Not detected — no SQL/NoSQL database integration

**File Storage:**
- Local filesystem only
- Chain peer caches: `chain_enodes.json`, `chain_enodes.json.gz` (stored next to executable or at user-specified path)
- Chain configuration: `chains_config.json` (maintained in repo at `examples/chains_config.json`)
- RPC manager config: JSON file via `load_rpc_manager_config_from_json()` (`include/eth/rpc_manager_config.hpp`)

**Caching:**
- GitHub Actions artifact caching for fuzz corpus (`actions/cache` in `fuzz.yml`)
- GitHub Actions artifact caching for benchmark baselines (`actions/cache` in `benchmarks.yml`)

## Authentication & Identity

**Auth Provider:**
- Custom — No third-party identity provider
- RLPx auth: ECDH-secp256k1 handshake with ephemeral keys (`include/rlpx/auth/auth_handshake.hpp`)
- ENR records signed with node identity keys
- RPC API keys: Environment variable injection via `{key}` template substitution (`include/eth/rpc_manager.hpp`)
- Test transactions: Private key via `.env` file or environment variable (`PRIVATE_KEY`) for Foundry `cast`

**Key Management:**
- Test wallet generation: `cast wallet new` (Foundry tooling)
- `.env` files git-ignored, never committed

## Monitoring & Observability

**Error Tracking:**
- Not detected — no external error tracking service (Sentry, Bugsnag, etc.)

**Logs:**
- spdlog v1.4.2 with `SPDLOG_FMT_EXTERNAL` (uses fmt library)
- Logging levels: `trace`, `debug`, `info`, `warn`, `error`
- Log output: stdout/stderr via `base/rlp-logger.hpp`
- Example debug logs written to `examples/logs/eth_watch_<timestamp>.log`
- RLPx session: `sgLogger` macro for session-scoped logging
- CI: `-DSGNS_PRINT_LOGS=ON` flag for verbose Linux builds

## CI/CD & Deployment

**Hosting:**
- Static library (`libevmrelay.a`) embedded into SuperGenius applications
- No standalone deployment

**CI Pipeline:**
- **Platform:** GitHub Actions with self-hosted runners (`self-hosted`, `X64`, `Linux`; `gv-OSX-Large`; `sg-arm-linux`)
- **Workflows:**
  - `cmake-multi-platform.yml` — Multi-platform release builds (Linux x86_64/aarch64, Windows, OSX, Android arm64-v8a/armeabi-v7a, iOS)
    - Container: `ghcr.io/geniusventures/debian-bullseye:latest` for Linux builds
    - Depends on `GeniusVentures/thirdparty` and `GeniusVentures/zkLLVM` GitHub releases
    - CTest runs on non-mobile targets
  - `fuzz.yml` — LibFuzzer-based fuzz testing for RLP decoder/encoder (short: 180s manual, long: 7200s scheduled — currently disabled)
  - `sanitizers.yml` — Memory safety testing: ASan + UBSan, TSan (both gcc and clang; Windows ASan disabled)
  - `valgrind.yml` — Valgrind Memcheck on Linux (Release build, leak detection)
  - `benchmarks.yml` — Google Benchmark performance tests (Linux + Windows, manual trigger)

**Third-party Dependency Pipeline:**
- Pre-built thirdparty: Downloaded from `GeniusVentures/thirdparty` GitHub releases per platform/build-type
- Pre-built zkLLVM: Downloaded from `GeniusVentures/zkLLVM` GitHub releases

## Environment Configuration

**Required env vars (runtime — examples/eth_watch):**
- `PRIVATE_KEY` — For test transaction generation with `cast`
- `TEST_ADDRESS` — Recipient address for test transactions
- `TO_ADDRESS` — Override recipient for send script

**Secrets location:**
- GitHub Actions secrets: `GNUS_TOKEN_1` (for package/container registry auth)
- Local credentials: `.env` files (git-ignored, never committed)

## Webhooks & Callbacks

**Incoming:**
- Not detected — no inbound webhook or callback endpoints

**Outgoing:**
- DNS TXT queries for EIP-1459 ENR-tree resolution (`include/discv5/enr_tree.hpp` — `system_txt_lookup`)
- HTTP/HTTPS outbound for JSON-RPC calls (Boost.Beast client in `include/eth/rpc_http_transport.hpp`)
- HTTP/HTTPS outbound for chain peer cache refresh (`https://enodes.gnus.ai/chain_enodes.json.gz`)

## Supported Blockchain Networks

| Network | Chain ID | Discovery Method | ETH Schema Set |
|---------|----------|-----------------|----------------|
| Ethereum Mainnet | 1 | ENR-tree (discv5) | ethereum-default |
| Ethereum Sepolia | 11155111 | ENR-tree (discv5) | ethereum-default |
| Ethereum Holesky | 17000 | ENR-tree (discv5) | ethereum-default |
| Ethereum Hoodi | — | ENR-tree (discv5) | ethereum-default |
| Polygon Mainnet | 137 | ENR-tree (discv5), fork filter disabled | polygon-bor |
| Polygon Amoy | 80002 | ENR-tree (discv5), fork filter disabled | polygon-bor |
| BNB Smart Chain | 56 | Discv4 | bsc68 |
| BSC Testnet | 97 | Discv4 | bsc68 |
| Base Mainnet | 8453 | cache-enr-discv5 | base-op-geth |
| Base Sepolia | 84532 | cache-enr-discv5 | base-op-geth |
| Gnosis Chain | 100 | Discv4 | gnosis-geth-like |
| SuperGenius Mainnet | 369 | — | — |
| SuperGenius Testnet | 963 | — | — |
| SuperGenius Devnet | 144 | — | — |

## GNUS Token Contract Addresses

| Chain | Address | Source |
|-------|---------|--------|
| Ethereum Mainnet | `0x614577036F0a024DBC1C88BA616b394DD65d105a` | `examples/README.md` |
| Polygon Mainnet | `0x127E47abA094a9a87D084a3a93732909Ff031419` | `examples/README.md` |
| BSC Mainnet | `0x614577036F0a024DBC1C88BA616b394DD65d105a` | `examples/README.md` |
| Base Mainnet | `0x614577036F0a024DBC1C88BA616b394DD65d105a` | `examples/README.md` |
| Sepolia Testnet | `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70` | `examples/README.md` |
| Polygon Amoy | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` | `examples/README.md` |
| BSC Testnet | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` | `examples/README.md` |
| Base Sepolia | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` | `examples/README.md` |

---

*Integration audit: 2026-05-25*
