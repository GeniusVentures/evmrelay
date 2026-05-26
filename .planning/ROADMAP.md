# Roadmap: evmrelay v1

**Generated:** 2026-05-25
**Granularity:** Standard (per config.json)
**Parallelization:** Sequential phases
**Total Phases:** 4
**Total Requirements:** 15

---

## Phase 1: ChainList Provider & intx Documentation

**Goal:** Ingest public chain metadata from `chainid.network` and `ethereum-lists`, producing normalized,
filtered, and deduplicated RPC endpoint lists that `RpcManager` can consume. Document the vendored
`intx.hpp` upstream provenance.

**Mapped Requirements:** CHNL-01, CHNL-02, CHNL-03, CHNL-04, CHNL-05, CODE-01

### Success Criteria

1. `chains.json` is parsed via schema-driven JSON (`JsonSchemaObject`), extracting all required fields
   (chain name, chainId, networkId, shortName, status, nativeCurrency symbol, RPC URL list) for each entry.
2. RPC URLs are filtered to `http://`/`https://` only, API-key placeholders are rejected, malformed URLs
   are dropped, and duplicates are eliminated by `chainId + URL` key.
3. `wss://` endpoints are collected into a separate list and excluded from default `RpcEndpointConfig`
   output.
4. Chain status filters exclude deprecated chains by default; testnet and incubating chain inclusion is
   configurable.
5. Downloader falls back from `chainid.network/chains.json` to per-chain `ethereum-lists/_data/chains/*.json`
   files with error-isolated parsing (one broken file does not fail the entire ingest).
6. The vendored `intx.hpp` file header documents the upstream `chfast/intx` commit SHA and upgrade path.

---

## Phase 2: RPC Infrastructure

**Goal:** Bridge the ChainList provider output into the existing `RpcManager`/`RpcEndpointPool`
infrastructure with optional endpoint probing and health/backoff metadata, then expose chain-scoped
receipt sources via a factory adapter.

**Mapped Requirements:** RPC-01, RPC-02, RPC-03, RPC-04

### Success Criteria

1. Normalized ChainList entries are converted into `RpcEndpointConfig` structs that `RpcManager` can
   consume directly.
2. Optional `eth_chainId` probing POSTs to each HTTP(S) endpoint, validates the returned chainId,
   and marks mismatched endpoints without blocking startup.
3. `RpcEndpointPool` gains retry suppression, backoff duration tracking, and temporary failure
   recovery metadata around the existing `RpcEndpointState` enum.
4. `RpcReceiptSourceFactory` creates chain-scoped `RpcReceiptSource` instances from `RpcManager`
   endpoint pools without leaking transport policy into `EthWatchService`.

---

## Phase 3: Config Audit & Tests

**Goal:** Ship a production safety gate that rejects unsafe configurations at startup, and fill the
two most critical test gaps: RLP fuzz coverage and config-audit rejection tests.

**Mapped Requirements:** AUDIT-01, TEST-01, TEST-02

### Success Criteria

1. `--dry-run-config-audit` validates required RPC endpoints per configured chain, non-zero
   confirmation depth, and valid provider fields, rejecting unsafe configs with clear diagnostics.
2. RLP decoder fuzz tests provide coverage-guided coverage against malformed inputs from untrusted
   network peers.
3. Config audit rejection tests verify that missing-endpoint, zero-confirmation-depth, and
   missing-provider-field configs are each rejected.

---

## Phase 4: Code Health

**Goal:** Resolve the three remaining code-health concerns: the `--direct-enode` production path,
the oversized `messages.cpp`, and end-to-end pipeline tests.

**Mapped Requirements:** CODE-02, CODE-03, CODE-04

### Success Criteria

1. The `--direct-enode` API is either promoted to a production class under `include/eth/` or
   permanently documented as example-local only.
2. `src/eth/messages.cpp` (2137 lines) is split into per-message-group files while preserving
   shared helper functions and backward compatibility.
3. End-to-end bridge event pipeline tests exercise observation → event matching → ABI decode →
   callback delivery without any network I/O.

---

## Out of Scope (v1)

These remain SuperGenius or deferred concerns (see REQUIREMENTS.md Out of Scope section):

| Concern | Disposition |
|---------|-------------|
| `RpcQuorumClient` / `ProviderVote<T>` | SuperGenius scope |
| `SecurityDecision` structured evidence | SuperGenius scope |
| Multi-provider receipt verification | SuperGenius scope |
| Bridge mint consensus / validator voting | SuperGenius scope |
| `eth_watch_service.cpp` refactor | TECH DEBT — deferred due to risk |
| `messages.cpp` full refactor | Covered by CODE-03 (split only) |
| `intx.hpp` dependency replacement | TECH DEBT — vendored copy works |

---

*Roadmap generated: 2026-05-25*
