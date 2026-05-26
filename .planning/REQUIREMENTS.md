# Requirements: evmrelay

**Defined:** 2026-05-25
**Core Value:** Produce normalized Ethereum bridge event observations and provide public RPC endpoint infrastructure that SuperGenius can use to independently verify bridge events before mint consensus.

## v1 Requirements

### ChainList Provider

- [ ] **CHNL-01**: Parse `chains.json` from `chainid.network` using schema-driven JSON, extracting chain name, chainId, networkId, shortName, status, nativeCurrency symbol, and RPC URL list for each entry
- [ ] **CHNL-02**: Filter RPC URLs — keep only `http://` and `https://`, drop URLs containing API-key placeholders (`${INFURA_API_KEY}`, `${ALCHEMY_API_KEY}`, etc.), drop malformed URLs, deduplicate by `chainId + URL`
- [ ] **CHNL-03**: Separate `wss://` endpoints into a distinct list, excluded from `RpcManager` config by default
- [ ] **CHNL-04**: Apply chain status filters — exclude deprecated chains by default, optionally include/exclude testnets and incubating chains
- [ ] **CHNL-05**: Downloader with fallback — primary source `chainid.network/chains.json`, fallback to `ethereum-lists/_data/chains/*.json` per-chain files with error-isolated parsing

### RPC Infrastructure

- [ ] **RPC-01**: Convert normalized ChainList entries into `RpcEndpointConfig` candidates consumable by `RpcManager`
- [ ] **RPC-02**: Optional `eth_chainId` endpoint probing — POST `eth_chainId` to each HTTP(S) endpoint, compare returned chainId against configured value, mark mismatched endpoints, handle timeouts and errors without blocking startup
- [ ] **RPC-03**: `RpcEndpointPool` health/backoff — add retry suppression metadata, backoff duration tracking, and temporary failure recovery to the existing `RpcEndpointState` enum
- [ ] **RPC-04**: `RpcReceiptSourceFactory` — adapter that creates chain-scoped `RpcReceiptSource` instances from `RpcManager` endpoint pools without pushing transport policy into `EthWatchService`

### Config & Audit

- [ ] **AUDIT-01**: Production config audit tool — validate that loaded config includes required RPC endpoints per configured chain, non-zero confirmation depth, and valid provider fields; reject unsafe configurations at startup with clear diagnostics

### Code Health

- [ ] **CODE-01**: Document `intx.hpp` upstream commit SHA in file header (vendored from `chfast/intx`)
- [ ] **CODE-02**: Resolve `--direct-enode` path — either add production API class under `include/eth/` or permanently document as example-local
- [ ] **CODE-03**: Split `src/eth/messages.cpp` (2137 lines) into per-message-group files while preserving shared helpers
- [ ] **CODE-04**: End-to-end bridge event pipeline tests — verify observation → event matching → ABI decode → callback delivery without network I/O

### Tests

- [ ] **TEST-01**: RLP decoder fuzz tests — coverage-guided fuzz testing for malformed RLP inputs from untrusted network peers
- [ ] **TEST-02**: Config audit rejection tests — verify unsafe configs (missing endpoints, zero confirmation depth, missing provider fields) are rejected

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
| CHNL-01 | Phase 1 | Pending |
| CHNL-02 | Phase 1 | Pending |
| CHNL-03 | Phase 1 | Pending |
| CHNL-04 | Phase 1 | Pending |
| CHNL-05 | Phase 1 | Pending |
| RPC-01 | Phase 2 | Pending |
| RPC-02 | Phase 2 | Pending |
| RPC-03 | Phase 2 | Pending |
| RPC-04 | Phase 2 | Pending |
| AUDIT-01 | Phase 3 | Pending |
| CODE-01 | Phase 1 | Pending |
| CODE-02 | Phase 4 | Pending |
| CODE-03 | Phase 4 | Pending |
| TEST-01 | Phase 3 | Pending |
| TEST-02 | Phase 3 | Pending |

**Coverage:**
- v1 requirements: 15 total
- Mapped to phases: 15
- Unmapped: 0 ✓

---
*Requirements defined: 2026-05-25*
*Last updated: 2026-05-25 after initial definition*
