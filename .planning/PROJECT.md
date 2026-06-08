# evmrelay

## What This Is

evmrelay is a C++17 static library that provides Ethereum P2P protocol infrastructure — peer discovery (discv4/discv5), RLPx encrypted transport, ETH subprotocol event watching, public RPC endpoint provisioning, and JSON-RPC receipt fetching. It is a submodule of the SuperGenius blockchain project, consumed by `src/watcher/` and `src/account/` for bridge event observation and RPC-backed verification.

## Core Value

Produce normalized Ethereum bridge event observations and provide public RPC endpoint infrastructure that SuperGenius can use to independently verify bridge events before mint consensus.

## Requirements

### Validated

- ✓ Watcher Service — P2P peer discovery (discv4/discv5), RLPx transport, ETH event watching, ABI decode, bridge event normalization (tasks 1-6)
- ✓ RPC Connection Maker — RpcManager, RpcEndpointPool, RpcHttpTransport, receipt fetching via JSON-RPC (tasks 2-4)
- ✓ Bridge event types — BridgeEventClaim, BridgeEventObservation, EventDeduper, verify_receipt_log()
- ✓ Discovery/dialer decoupling — EthPeerQueue, DialScheduler, EthWatchService production orchestration
- ✓ EIP-1459 ENR-tree discovery for Ethereum and Polygon chains
- ✓ Multi-chain support — 11 chains verified in live all-chains functional test

### Active

- [ ] **CHNL-01**: ChainList / Ethereum-Lists RPC metadata ingestion — parse `chains.json`, filter/deduplicate RPC URLs, produce normalized endpoint lists
- [ ] **CHNL-02**: ChainList downloader with fallback — `chainid.network/chains.json` primary, `ethereum-lists/_data/chains/*.json` fallback
- [ ] **RPC-01**: Convert ChainList RPC endpoints into `RpcEndpointConfig` candidates consumable by `RpcManager`
- [ ] **RPC-02**: Optional `eth_chainId` endpoint probing — validate chain-id match, mark mismatched endpoints
- [ ] **RPC-03**: `RpcEndpointPool` health/backoff — retry suppression, backoff metadata, temporary failure tracking
- [ ] **RPC-04**: `RpcReceiptSourceFactory` — adapter creating chain-scoped receipt sources from `RpcManager`
- [ ] **AUDIT-01**: Production config audit tool — `--dry-run-config-audit` rejecting unsafe configs at startup
- [ ] **CONCERN-03**: `--direct-enode` production API (or documented as example-local permanently)
- [ ] **CONCERN-05**: Split `src/eth/messages.cpp` (2137 lines) into per-message-group files
- [ ] **CONCERN-06**: Refactor `src/eth/eth_watch_service.cpp` (1152 lines) to reduce orchestration complexity
- [ ] **TEST-01**: No fuzz testing for RLP decoder — add coverage-guided fuzz tests
- [ ] **TEST-02**: End-to-end bridge verification consumer boundary tests — done (3 tests, `bridge_event_e2e_test.cpp`)

### Out of Scope

- RPC quorum client (`RpcQuorumClient`, `ProviderVote<T>`) — SuperGenius builds on evmrelay's endpoints
- `SecurityDecision` structured evidence object — SuperGenius wraps evmrelay's observations
- Multi-provider quorum comparison — SuperGenius orchestrates across evmrelay's endpoint infrastructure
- Bridge mint consensus, validator voting, UTXO management — SuperGenius `src/watcher/` and `src/account/`
- Parent watcher integration (`evm_messaging_watcher.*` replacement) — SuperGenius adapter layer

## Context

**Current state:** ~85% complete per `EVMRELAY_COMPLETION_PLAN.md`. Tasks 1-12 (discovery, dialer, ENR-tree, production service API, documentation) are done. Tasks 13-17 (ChainList, endpoint health, receipt source factory) remain.

**Brownfield:** Mature codebase (`src/`, `include/`, `test/`) with comprehensive codebase docs in `.planning/codebase/` (ARCHITECTURE, CONCERNS, CONVENTIONS, INTEGRATIONS, STACK, STRUCTURE, TESTING).

**Key planning artifacts:**
- `AgentDocs/EVMRELAY_COMPLETION_PLAN.md` — Full task breakdown with scope split (evmrelay vs SuperGenius)
- `AgentDocs/RPC_MANAGER_HANDOFF.md` — RPC layer implementation order and current state
- `AgentDocs/HANDOFF.md` — evmrelay-only remaining work (ChainList, config audit, tests)
- `.planning/codebase/CONCERNS.md` — Security, tech debt, bugs, test gaps, missing features

**Consumer:** SuperGenius `src/watcher/` (bridge orchestrator) and `src/account/` (mint verification) consume evmrelay as a library. The handoff boundary is: evmrelay produces `WatchEventNotification` / `BridgeEventClaim` / `BridgeEventObservation` types; SuperGenius builds RPC quorum, `SecurityDecision`, and bridge consensus on top.

## Constraints

- **Language:** C++17 only — no C++20 features (no designated initializers, no native coroutines)
- **Async:** Boost.Asio stackful coroutines (`spawn`/`yield_context`) for all async I/O
- **Errors:** `boost::outcome::result<T, E>` — no exceptions in hot paths
- **JSON:** `boost::json` with schema-driven parsing via `JsonSchemaObject`/`JsonSchemaArray`
- **Data architecture:** No hardcoded chain names, RPC URLs, API keys, or operational facts in C++ source. All chain-specific data loaded from external JSON at runtime.
- **Build:** Ninja, CMake 3.15+, macOS CI
- **Testing:** Google Test + CTest, wait-condition templates (no `sleep_for`), ≥80% coverage target
- **Style:** Allman braces, 4-space indent, 120 char line limit, `#ifndef` guards, `kCamelCase` constants
- **Threading:** Single-threaded `io_context` — all public API calls from the same thread
- **Dependencies:** Boost 1.85.0, OpenSSL 3.3, libsecp256k1, fmt, spdlog, Snappy, ZLIB

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| RPC quorum client is SuperGenius scope | evmrelay provides endpoint infrastructure; quorum comparison is consumer logic | ✓ Good |
| `SecurityDecision` is SuperGenius scope | Wraps evmrelay observations with quorum metadata | ✓ Good |
| Free functions preferred over classes for config/data loading | Follows existing `rpc_manager_config.hpp` pattern; simpler, more testable | ✓ Good |
| Schema-driven JSON parsing via `JsonSchemaObject` | Consistent validation, error messages, no manual `if_contains` branches | ✓ Good |
| `intx.hpp` vendored from chfast/intx (upstream commit tracked) | Reduces external dependency; commit SHA documented for upgrade path | — Pending |
| `--direct-enode` currently example-local | Deferred decision on production API | ⚠️ Revisit |

---
*Last updated: 2026-05-25 after initialization*
