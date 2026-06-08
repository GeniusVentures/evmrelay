# Requirements: evmrelay

**Defined:** 2026-05-25
**Core Value:** Produce normalized Ethereum bridge event observations and provide public RPC endpoint infrastructure that SuperGenius can use to independently verify bridge events before mint consensus.

## v1 Requirements

### ChainList Provider

- [x] **CHNL-01**: Parse `chains.json` from `chainid.network` using schema-driven JSON, extracting chain name, chainId, networkId, shortName, status, nativeCurrency symbol, and RPC URL list for each entry
- [x] **CHNL-02**: Filter RPC URLs — keep only `http://` and `https://`, drop URLs containing API-key placeholders (`${INFURA_API_KEY}`, `${ALCHEMY_API_KEY}`, etc.), drop malformed URLs, deduplicate by `chainId + URL`
- [x] **CHNL-03**: Separate `wss://` endpoints into a distinct list, excluded from `RpcManager` config by default
- [x] **CHNL-04**: Apply chain status filters — exclude deprecated chains by default, optionally include/exclude testnets and incubating chains
- [x] **CHNL-05**: Downloader with fallback — primary source `chainid.network/chains.json`, fallback to `ethereum-lists/_data/chains/*.json` per-chain files with error-isolated parsing

### RPC Infrastructure

- [x] **RPC-01**: Convert normalized ChainList entries into `RpcEndpointConfig` candidates consumable by `RpcManager`
- [x] **RPC-02**: Optional `eth_chainId` endpoint probing — POST `eth_chainId` to each HTTP(S) endpoint, compare returned chainId against configured value, mark mismatched endpoints, handle timeouts and errors without blocking startup
- [x] **RPC-03**: `RpcEndpointPool` health/backoff — add retry suppression metadata, backoff duration tracking, and temporary failure recovery to the existing `RpcEndpointState` enum
- [x] **RPC-04**: `RpcReceiptSourceFactory` — adapter that creates chain-scoped `RpcReceiptSource` instances from `RpcManager` endpoint pools without pushing transport policy into `EthWatchService`

### Config & Audit

- [x] **AUDIT-01**: Production config audit tool — validate that loaded config includes required RPC endpoints per configured chain, non-zero confirmation depth, and valid provider fields; reject unsafe configurations at startup with clear diagnostics

### Code Health

- [x] **CODE-01**: Document `intx.hpp` upstream commit SHA in file header (vendored from `chfast/intx`)
- [x] **CODE-02**: Resolve `--direct-enode` path — either add production API class under `include/eth/` or permanently document as example-local
- [x] **CODE-03**: Split `src/eth/messages.cpp` (2137 lines) into per-message-group files while preserving shared helpers
- [x] **CODE-04**: End-to-end bridge event pipeline tests — verify observation → event matching → ABI decode → callback delivery without network I/O

### Tests

- [x] **TEST-01**: RLP decoder fuzz tests — coverage-guided fuzz testing for malformed RLP inputs from untrusted network peers
- [x] **TEST-02**: Config audit rejection tests — verify unsafe configs (missing endpoints, zero confirmation depth, missing provider fields) are rejected

## v2 Requirements

None deferred — all scope is committed for evmrelay finalization handoff.

## Out of Scope

| Feature | Reason |
|---------|--------|
| RPC quorum client (`RpcQuorumClient`, `ProviderVote<T>`) | SuperGenius builds multi-provider comparison on evmrelay's endpoint infrastructure |
| `SecurityDecision` structured evidence object | SuperGenius wraps evmrelay observations with quorum metadata |
| Multi-provider receipt verification | SuperGenius orchestrates across evmrelay's endpoint pool |
| Bridge mint consensus / validator voting | SuperGenius `src/watcher/` concern |
| Parent watcher integration (`evm_messaging_watcher.*` replacement) | SuperGenius adapter layer over evmrelay |
| `messages.cpp` full refactor into per-message files | TECH DEBT — split deferred due to risk; only document the size concern |
| `eth_watch_service.cpp` refactor | TECH DEBT — high risk of breaking existing test coverage; document fragility |
| `intx.hpp` dependency replacement | TECH DEBT — vendored copy works; full `find_package(intx)` migration deferred |

## Traceability

See [ROADMAP.md](ROADMAP.md) for per-phase goal statements and success criteria.

| Requirement | Phase | Status |
|-------------|-------|--------|
| CHNL-01 | Phase 1 | Done |
| CHNL-02 | Phase 1 | Done |
| CHNL-03 | Phase 1 | Done |
| CHNL-04 | Phase 1 | Done |
| CHNL-05 | Phase 1 | Done |
| RPC-01 | Phase 2 | Done |
| RPC-02 | Phase 2 | Done |
| RPC-03 | Phase 2 | Done |
| RPC-04 | Phase 2 | Done |
| AUDIT-01 | Phase 2 | Done |
| CODE-01 | Phase 1 | Done |
| CODE-02 | Phase 4 | Done |
| CODE-03 | Phase 4 | Done |
| CODE-04 | Phase 4 | Done |
| TEST-01 | Phase 3 | Done |
| TEST-02 | Phase 3 | Done |

**Coverage:**
- v1 requirements: 15 total
- Mapped to phases: 15
- Completed: 15 ✓
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-25*
*Last updated: 2026-05-26 — all 15 requirements completed*
