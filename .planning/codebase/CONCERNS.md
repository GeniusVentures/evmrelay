# Codebase Concerns

**Analysis Date:** 2026-05-25

---

## Security Considerations

### Single-Provider RPC Trust (No Quorum)

- **Risk:** `RpcReceiptSource` accepts a single `JsonRpcTransport` and trusts one RPC response path for `eth_getBlockByNumber`, `eth_getLogs`, and `eth_getTransactionReceipt`. A compromised or malicious RPC provider can inject forged receipts, logs, or block data that would pass current validation.
- **Files:** `include/eth/rpc_receipt_source.hpp`, `src/eth/rpc_receipt_source.cpp`, `include/eth/json_rpc.hpp`, `src/eth/json_rpc.cpp`
- **Current mitigation:** None — no multi-provider quorum exists. The `RpcEndpointPool` in `include/eth/rpc_manager.hpp` does health-tracking but does not enforce quorum comparison across providers.
- **Recommendations:** Implement the `RpcQuorumPolicy`, `RpcQuorumClient`, and `ProviderVote<T>` types specified in `AgentDocs/EVMRELAY_SECURITY_HARDENING_PLAN.md` Phase 1. At minimum, require responses from multiple independent providers (different trust domains) before producing verification evidence for bridge events.

### No SecurityDecision Object for Bridge Verification

- **Risk:** Event callbacks from `EthWatchService` and receipt responses from `RpcReceiptSource` pass raw data without binding chain id, block hash, tx hash, log index, quorum metadata, or degraded state into a verifiable decision. The parent watcher/bridge integration at `src/watcher/impl/evm_messaging_watcher.*` currently uses a single WebSocket endpoint and string concatenation for `eth_subscribe` — not verifiable evidence.
- **Files:** `include/eth/eth_watch_service.hpp`, `src/eth/eth_watch_service.cpp`, `include/eth/rpc_receipt_source.hpp`, parent project: `src/watcher/impl/evm_messaging_watcher.*`
- **Recommendations:** Add `SecurityDecision` as the structured evidence object per Phase 2 of the hardening plan. Bridge mint paths must consume verified decisions only, not raw P2P observations or single-RPC responses.

### CI/CD Token Exposure

- **Risk:** The release CI workflow (`cmake-multi-platform.yml`) sets `GH_TOKEN: ${{ secrets.GNUS_TOKEN_1 }}` at job scope (exposed to all steps), uses self-hosted runners with `IPC_LOCK` capability and broad destructive cleanup, pins container images by `:latest` tag not digest, and pins third-party actions by tag rather than commit SHA.
- **Files:** `.github/workflows/cmake-multi-platform.yml` (lines 66, 73, 123, 132), `.github/workflows/sanitizers.yml`
- **Status (2026-05-25): PARTIALLY FIXED**
  - ✅ `GH_TOKEN` moved from job scope to step-level (`env:` only on "Download thirdparty release" and "Download zkLLVM release" steps)
  - ✅ Explicit `permissions: contents: read, packages: read` added at top-level and job-level
  - ✅ `permissions:` blocks added to all workflows (`sanitizers.yml`, `benchmarks.yml`, `fuzz.yml`, `valgrind.yml`)
  - ✅ `persist-credentials: false` set on all `actions/checkout` steps
  - ✅ `actions/checkout` and `actions/upload-artifact` already pinned by commit SHA
  - ⚠️ Container images still `:latest` — TODO to pin by digest (`@sha256:...`) when digest is known
  - ⚠️ `IPC_LOCK` capability still required for Android builds
  - ⚠️ Self-hosted runner cleanup (`sudo rm -rf`) remains operationally necessary
  - See `AgentDocs/EVMRELAY_SECURITY_HARDENING_PLAN.md` Phase 7 for the full hardening list.

### No Durable Relay State Machine

- **Risk:** `EventDeduper` is in-memory only. A crash after observation but before verification could drop bridge events. No replay protection across restarts. No state machine prevents duplicate validator votes or exit submissions after recovery.
- **Files:** Event deduplication is in-memory (no persistent store found for bridge event lifecycle)
- **Current mitigation:** None. Restart loses all in-progress bridge event state.
- **Recommendations:** Implement a persistent `RelayStateMachine` with explicit states (Discovered→QuorumPending→QuorumVerified→Voted→MintConsensusReached→...→Finalized) per Phase 4 of the hardening plan. State identity must bind source chain, tx hash, log index, block hash, and message nonce.

### No Production Config Validation

- **Risk:** No startup validation that production config includes required quorum policy, confirmation depth, provider trust domains, bridge thresholds, or validator policies. A misconfigured production instance could silently operate with unsafe defaults (e.g., `confirmation_depth = 0`, single provider, genesis-alone bridge threshold).
- **Files:** No config audit tool exists.
- **Recommendations:** Implement `--dry-run-config-audit` per Phase 5 of the hardening plan, rejecting unsafe production configs at startup.

---

## Tech Debt

### Incomplete RPC Manager Bridge Verification Pipeline

- **Issue:** The `RpcManager`, `RpcEndpointPool`, and `RpcHttpTransport` runtime foundation exists, but the verifier layer (quorum, receipt verification, bridge evidence) is not yet built. Tasks 13-23 of `AgentDocs/EVMRELAY_COMPLETION_PLAN.md` remain incomplete.
- **Files:** `include/eth/rpc_manager.hpp`, `src/eth/rpc_manager.cpp`, `include/eth/rpc_http_transport.hpp`, `src/eth/rpc_http_transport.cpp`
- **Impact:** Cannot bridge from P2P observation to verified bridge evidence. Production bridge mint consensus cannot rely on evmrelay observations without the verifier layer.
- **Fix approach:** Follow `AgentDocs/RPC_MANAGER_HANDOFF.md` and `EVMRELAY_COMPLETION_PLAN.md` tasks 13-23: ChainList ingestion, endpoint health/backoff, `RpcReceiptVerifier`, quorum comparison, receipt/log/block verification.

### Example-Local `--direct-enode` Path

- **Issue:** The `--direct-enode` connection mode lives entirely in `examples/eth_watch/eth_watch.cpp` with no production API behind it. Per `AgentDocs/CHECKPOINT.md`, it "remains example-local" and the decision on whether to make it a production direct-session API was deferred.
- **Files:** `examples/eth_watch/eth_watch.cpp` (lines ~990-1010, 1190-1277)
- **Impact:** Direct enode connections require running the example binary. No programmatic API for testing or integration.
- **Fix approach:** Either make `--direct-enode` example-local permanently (document it as such) or add a `DirectSession` production API under `include/eth` / `src/eth` per previous checkpoint suggestions.

### Third-Party `intx.hpp` Vendored In-Tree (2127 lines)

- **Issue:** `include/rlp/intx.hpp` is a complete vendored copy of the intx extended-precision integer library by Pawel Bylica. At 2127 lines, it is the single largest header in the project, containing its own platform detection macros, built-in 128-bit integer support, and uint template implementations.
- **Files:** `include/rlp/intx.hpp`
- **Impact:** Hard to update when upstream changes; duplicates a third-party library; increases header parse time across all translation units that include it; potential for local divergence from upstream.
- **Fix approach:** If intx is available as a proper CMake package (it is widely packaged), replace the vendored copy with a `find_package(intx)` dependency. Otherwise, at minimum add a comment documenting the upstream commit SHA and version this vendored copy corresponds to.

### Large Monolithic Message Encoding/Decoding File

- **Issue:** `src/eth/messages.cpp` at 2137 lines is the largest implementation file. It contains encoding and decoding for ~20 ETH protocol messages (Status, NewBlockHashes, GetBlockHeaders, BlockHeaders, GetReceipts, Receipts, GetPooledTransactions, PooledTransactions, GetBlockBodies, BlockBodies, NewBlock, BlockRangeUpdate, UpgradeStatus, NewPooledTransactionHashes), plus transaction and block header list decoders.
- **Files:** `src/eth/messages.cpp` (2137 lines), `include/eth/messages.hpp`
- **Impact:** Hard to navigate; encoding and decoding responsibilities are interleaved; adding a new message type requires touching this large file.
- **Fix approach:** Split into per-message or per-message-group files (`eth/status_message.cpp`, `eth/block_messages.cpp`, `eth/transaction_messages.cpp`, `eth/receipt_messages.cpp`) while keeping the shared namespace and helper functions in a common header.

---

## Known Bugs

### Live Peer Disconnect Before HELLO (Base/Gnosis/BNB)

- **Symptoms:** Live runs against Base mainnet, BNB Smart Chain, and Gnosis Chain show peers completing RLPx auth but disconnecting before sending peer HELLO. For example, Base discover-first 45s run showed 23 auth successes, all 23 disconnected before HELLO. Gnosis had 17 HELLO accepts but 16/17 remote ETH Status rejections.
- **Files:** `src/rlpx/rlpx_session.cpp`, `src/eth/eth_peer_session.cpp`, `src/eth/eth_watch_service.cpp`
- **Trigger:** Connecting to certain live chain peers via discv4/discv5 discovery. The peers accept the RLPx auth handshake but either disconnect immediately or reject ETH Status.
- **Workaround:** For Sepolia (most tested chain), the issue is less severe — ENR-only discover-first achieved 3 accepted remote Status out of 7 auth successes (43%). BNB never accepts ETH Status (0 accepted out of 41-48 attempts).
- **Notes:** This may be related to remote peer admission policies (too many peers, protocol version mismatch, fork ID expectations) rather than a code bug. The `kTooManyPeers` backoff behavior was already tuned. Further diagnosis requires per-session liveness tracking which was noted as "not yet implemented" in `AgentDocs/CHECKPOINT.md`.

### Live URL Test Fragility in `discv4_chain_peers_test`

- **Symptoms:** The test passes 23/24 locally; the only failure is the live URL download test case that fetches `chain_enodes.json.gz` from the configured URL. In network-restricted environments (sandboxed CI, no DNS), this test fails.
- **Files:** `test/discv4/chain_peers_test.cpp`
- **Trigger:** Running CTest without a pre-downloaded `chain_enodes.json.gz` in `build/OSX/Debug/` and without network access.
- **Workaround:** Pre-download the chain peer cache to `build/<Platform>/<BuildType>/chain_enodes.json.gz` or set `EVMRELAY_CHAIN_ENODES_JSON` environment variable before running tests. The test already supports `EVMRELAY_CHAIN_ENODES_JSON` override and falls back to local files before live URL.

---

## Performance Bottlenecks

### No RPC Request Batching

- **Problem:** `RpcReceiptSource` issues individual JSON-RPC requests for `eth_getBlockByNumber`, `eth_getLogs`, and `eth_getTransactionReceipt` sequentially. Each round-trip adds latency, especially when verifying multiple events across multiple chains.
- **Files:** `src/eth/rpc_receipt_source.cpp`, `src/eth/json_rpc.cpp`
- **Cause:** The transport layer (`RpcHttpTransport`) handles single request/response cycles. No batching or pipelining.
- **Improvement path:** Add JSON-RPC batch support to `RpcHttpTransport` and `JsonRpcTransport` so receipt verification can issue multiple `eth_getTransactionReceipt` calls for a block's events in one HTTP round-trip.

### In-Memory Deduplication Only

- **Problem:** `EventDeduper` is entirely in-memory. Every restart re-processes the full block range since the last known block, re-decoding and re-filtering events that were already processed. No Bloom-filter pre-check on restart.
- **Files:** Dedup logic is in-memory (no file-based seen-block store was found)
- **Improvement path:** Persist seen block numbers/tx hashes to a small on-disk store. On restart, skip blocks below the last persisted watermark. Add a Bloom filter pre-check for event topics before full ABI decoding.

### Large Vendored Header Impacts Compile Time

- **Problem:** `include/rlp/intx.hpp` at 2127 lines with heavy template metaprogramming is included by RLP encoder/decoder headers, which are foundational includes. Every translation unit that touches RLP pays the parse cost.
- **Files:** `include/rlp/intx.hpp`
- **Improvement path:** Use forward declarations or a `intx_fwd.hpp` minimal header. Move the heavy template implementations to a `.ipp` file included only where needed.

---

## Fragile Areas

### `src/eth/eth_watch_service.cpp` (1152 lines)

- **Files:** `src/eth/eth_watch_service.cpp`, `include/eth/eth_watch_service.hpp`
- **Why fragile:** Orchestrates discovery strategy selection (ENR-tree vs cache-ENR discv5 vs discv4), peer queue creation, dial scheduler lifecycle, watcher pool management, RLPx session setup, ETH Status handshake, watch registration, and callback dispatch — all in one class. Adding a new discovery source or session policy requires touching this central orchestrator.
- **Safe modification:** Add new discovery strategies through the existing `ChainDiscoveryDefault` enum and `allows_*_discovery()` helper functions. Add new session policies through the existing Strategy pattern seams (test seams allow replacing live dial/connect). Avoid adding more conditional branches to `initialize()` or `run_chain_service()`.
- **Test coverage:** `test/eth/eth_watch_service_test.cpp` (1279 lines) covers config validation, scheduler creation, discovery fallback, scheduler feedback, queue handoff, and peer selection modes. Good coverage of existing paths.

### `src/eth/messages.cpp` (2137 lines)

- **Files:** `src/eth/messages.cpp`, `include/eth/messages.hpp`
- **Why fragile:** Every ETH protocol message encode/decode pair lives in one file. Adding a new message type (e.g., for a new subprotocol) or fixing a decoding edge case risks breaking unrelated message handling. The file has ~20 message types each with encode and decode functions interleaved.
- **Safe modification:** Add new message encode/decode pairs at the end of the file following the existing pattern. Test with the corresponding message test in `test/eth/eth_messages_test.cpp` (1207 lines). Avoid restructuring existing message handling unless splitting the file into per-message-group files.
- **Test coverage:** `test/eth/eth_messages_test.cpp` has 1207 lines but coverage distribution per message type is not separately measured.

### `include/rlp/intx.hpp` (2127 lines, Vendored Third-Party)

- **Files:** `include/rlp/intx.hpp`
- **Why fragile:** Full vendored copy of an external library. Any update to the upstream intx library requires manual diff-and-merge. Build-time macros (`INTX_HAS_BUILTIN_INT128`, platform detection) may diverge from the upstream version. No tracking of which upstream commit this copy corresponds to.
- **Safe modification:** Do not modify. If a new integer type or operation is needed, prefer using the existing intx API rather than extending the vendored copy. Add a comment at the top of the file documenting the upstream commit/tag.

---

## Scaling Limits

### Single-Threaded IO Context

- **Resource/System:** Boost.Asio `io_context` runs on one thread per `EthWatchService` instance. All peer discovery, RLPx sessions, ETH message processing, and RPC HTTP calls share this thread.
- **Current capacity:** One IO context handles 11 chains concurrently (verified in live all-chains test: 2904 discovered peers, 17 ETH Status accepts, 938 ETH messages in one run).
- **Limit:** Single-threaded event loop will bottleneck on CPU-bound RLP decoding or crypto operations for many concurrent sessions. RPC HTTP calls are blocking within the event loop.
- **Scaling path:** Move RPC HTTP calls to a separate IO context or thread pool. Consider `io_context` pool (`boost::asio::thread_pool`) for CPU-intensive RLP decoding of large block bodies.

### No Persistent Peer Score Persistence

- **Resource/System:** `EthPeerQueue` and `DialScheduler` track peer backoff in-memory only. Each restart loses all peer quality metrics.
- **Current capacity:** In-memory only.
- **Limit:** Restart re-discovers from scratch. No carry-forward of known-good or known-bad peers.
- **Scaling path:** Persist peer scores and backoff timestamps to the chain peer cache format so `chain_enodes.json` reflects live peer quality across restarts.

---

## Dependencies at Risk

### Boost.Coroutine (C++20 Compatibility Note)

- **Risk:** The project uses Boost stackful coroutines (`Boost::coroutine`, `Boost::context`) for async operations as required by C++17 compatibility (see `AgentDocs/AGENT_MISTAKES.md` M019). The CHECKPOINT.md notes that calling `EthWatchService::stop()` during a live GTest process surfaced `boost::coroutines::detail::forced_unwind` from active Boost coroutine stacks.
- **Files:** Async network code throughout `src/rlpx/`, `src/discv4/`, `src/discv5/`, `src/eth/`
- **Impact:** The forced_unwind on stop is scoped to the opt-in live functional test and does not affect production teardown. However, if the project ever migrates to C++20 native coroutines, all async code must be rewritten (per M019 rules).
- **Migration plan:** Documented in M019 — when C++17 requirement is dropped, replace `boost::asio::yield_context` patterns with C++20 `co_await`/`co_return`. No migration timeline set.

### Vendored `intx` Library

- **Risk:** `include/rlp/intx.hpp` is a complete vendored copy of the intx library by Pawel Bylica (Apache 2.0). No upstream tracking, no package manager integration.
- **Files:** `include/rlp/intx.hpp`
- **Impact:** Bug fixes or performance improvements from upstream intx are not automatically pulled in. Security fixes to the integer library would need manual backporting.
- **Migration plan:** Replace with `find_package(intx)` or a proper CMake `FetchContent` integration. At minimum, document the vendored version's upstream commit SHA.

### `go-ethereum/` Submodule for Local Testing

- **Risk:** The `go-ethereum/` directory exists locally (untracked, in `.gitignore`) for local Geth testing. The README documents using local Geth for Sepolia direct-connection testing. If `go-ethereum/` becomes a tracked submodule with specific version requirements, it adds a large dependency.
- **Files:** `go-ethereum/` (untracked), referenced in `README.md` and `AgentDocs/CHECKPOINT.md`
- **Impact:** Currently untracked/local-only. If it ever becomes a tracked dependency, it would add hundreds of MB to the repository.
- **Recommendations:** Keep `go-ethereum/` as a local-only testing artifact. Do not track it. Document the expected Geth version for reproducible testing.

---

## Test Coverage Gaps

### No Tests for RPC Quorum / Multi-Provider Verification

- **What's not tested:** No test verifies that two independent RPC providers returning conflicting receipt data produces a fail-closed result. No test verifies quorum across trust domains.
- **Files:** `test/eth/rpc_receipt_source_test.cpp`, `test/eth/rpc_manager_test.cpp`
- **Risk:** Multi-provider verification cannot be validated as fail-closed until tests exist.
- **Priority:** High — must be added before production bridge verification depends on RPC manager.

### No End-to-End Bridge Verification Tests

- **What's not tested:** The full path from `EthWatchService` observing a candidate bridge event → RPC verification → SecurityDecision → parent watcher callback is not tested end-to-end.
- **Files:** No end-to-end bridge verification test exists.
- **Risk:** Individual components may work in isolation but the integration may fail (incorrect callback signatures, missing metadata, type mismatches).
- **Priority:** High — needed before production bridge path is activated.

### Live Test Fragility

- **What's not tested:** Live chain tests (`eth_watch_all_chains_live_test`, `eth_enr_tree_peer_cache_live_test`, live URL download in `discv4_chain_peers_test`) require network access and are opt-in via environment variables. They cannot run in restricted CI environments.
- **Files:** `test/eth/eth_watch_all_chains_live_test.cpp`, `test/eth/eth_enr_tree_peer_cache_live_test.cpp`, `test/discv4/chain_peers_test.cpp`
- **Risk:** Live network behavior regressions are only caught during manual testing.
- **Priority:** Medium — acceptable for now since live tests are opt-in. Consider a dedicated CI job with network access for periodic live regression testing.

### No Fuzz Testing for RLP Decoder

- **What's not tested:** The RLP decoder has no coverage-guided fuzz testing for malformed inputs. A `test/fuzz/` directory exists (225 lines total) but appears minimal.
- **Files:** `test/fuzz/` (225 lines total, minimal)
- **Risk:** Malformed RLP data from untrusted network peers could trigger undefined behavior or crashes in the decoder.
- **Priority:** Medium — P2P network peers are untrusted input sources. Fuzz the RLP decoder with libFuzzer or AFL.

---

## Missing Critical Features

### ChainList / Ethereum-Lists RPC Metadata Ingestion

- **Problem:** No automated ingestion of public RPC endpoint metadata from `chainid.network/chains.json` or `ethereum-lists/chains`. RPC endpoints must be manually configured.
- **Blocks:** Task 13-15 of `AgentDocs/EVMRELAY_COMPLETION_PLAN.md`. Without this, production deployments need manual RPC endpoint curation.
- **Files:** None implemented yet. Should live under `include/eth/` / `src/eth/` as `ChainlistProvider` per `RPC_MANAGER_HANDOFF.md`.

### RPC Receipt Verifier (Bridge Evidence)

- **Problem:** No component verifies an observed bridge event/log against independent RPC endpoints with quorum comparison, receipt field validation, block hash re-check, and finality depth checking.
- **Blocks:** Bridge mint consensus reliability. Without this, P2P observations cannot be independently verified before entering the validator vote path.
- **Files:** None implemented yet. Should live under `include/eth/` / `src/eth/` per `EVMRELAY_COMPLETION_PLAN.md` task 18 and `RPC_MANAGER_HANDOFF.md`.

### Production Config Audit Tool

- **Problem:** No `--dry-run-config-audit` that validates production config completeness (quorum policy, confirmation depth, provider trust domains, bridge thresholds, validator policy) and rejects unsafe configurations at startup.
- **Blocks:** Safe production deployment. Operators can accidentally run with unsafe defaults.
- **Files:** None implemented yet. Per `EVMRELAY_SECURITY_HARDENING_PLAN.md` Phase 5.

### Per-Session Liveness Diagnostics

- **Problem:** The live eth_watch output shows aggregate counters (auth_success, peer_hello_accepted, eth_messages) but no per-active-session liveness summary (last message age, idle detection, receive-loop state). This was explicitly noted as "not yet implemented" in `AgentDocs/CHECKPOINT.md`.
- **Blocks:** Debugging which sessions go idle and why peers disconnect.
- **Files:** `src/eth/eth_watch_service.cpp`, `src/eth/eth_peer_session.cpp`
- **Fix approach:** Per CHECKPOINT.md: "Add active-session liveness diagnostics: endpoint, negotiated ETH version, accepted fork id, total messages per session, last message id, last message age, and receive-loop closed/active state."

---

*Concerns audit: 2026-05-25*
