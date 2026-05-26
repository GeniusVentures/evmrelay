# Phase 02: RPC Infrastructure - Context

**Gathered:** 2026-05-25
**Status:** Ready for planning

## Phase Boundary

Bridge the Phase 1 ChainList output into `RpcManager`/`RpcEndpointPool` with optional `eth_chainId` endpoint probing, exponential health/backoff metadata, and a free-function `RpcReceiptSourceFactory`. Include the production config audit tool (`AUDIT-01`) in this phase since probing and health data feed the audit.

## Implementation Decisions

### eth_chainId Probing
- **D-01:** Probe endpoints synchronously at startup — POST `eth_chainId` to each HTTP(S) endpoint before it becomes available for receipt fetching.
- **D-02:** Validate returned chainId against the configured `chain_id`. Mismatched endpoints are marked unavailable with diagnostic reason. Timeout and connection errors mark the endpoint as temporarily failed.
- **D-03:** Probing is optional (config-controlled). Without probing, all endpoints start as available.

### Health/Backoff Strategy
- **D-04:** Exponential backoff on `RpcEndpointState::kTemporarilyFailed` — first failure: 1s cooldown, second: 2s, third: 4s, capped at 60s. After cooldown expires, endpoint returns to `kAvailable`.
- **D-05:** Three consecutive temporary failures within a configurable window (default 5 minutes) escalate to `kDisabled`. Disabled endpoints require explicit reset or config reload.
- **D-06:** Add `last_failure_time`, `failure_count`, and `backoff_until` metadata fields to `RpcEndpoint` or a companion tracking structure.

### RpcReceiptSourceFactory
- **D-07:** Free function `make_receipt_source(RpcManager&, chain_name, chain_id) -> std::optional<RpcReceiptSource>` that selects the next available endpoint from the pool, creates an `RpcHttpTransport`, and returns a configured `RpcReceiptSource`.
- **D-08:** Returns `std::nullopt` when no available endpoint exists for the chain (fail-closed).

### Config Audit (AUDIT-01 lands here)
- **D-09:** `--dry-run-config-audit` validates at startup: at least one RPC endpoint per configured chain with non-zero confirmation depth, endpoint health after probing, and valid provider fields. Rejects unsafe configs with per-chain diagnostic output.
- **D-10:** Audit runs after ChainList ingest + probing — has full endpoint availability picture.

### OpenCode's Discretion
- Exact metadata fields added to `RpcEndpoint` or companion struct
- Whether to use a separate `EndpointHealthTracker` or extend `RpcEndpoint` directly
- Backoff timer implementation (Boost.Asio timer or timestamp comparison)
- Function signature and exact namespace for `make_receipt_source`

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Foundation
- `.planning/PROJECT.md` — Core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — v1 requirements (RPC-01 through RPC-04, AUDIT-01)
- `.planning/ROADMAP.md` — Phase goal, success criteria

### Codebase Patterns
- `include/eth/rpc_manager.hpp` — `RpcEndpoint`, `RpcEndpointPool`, `RpcManager` — classes to extend
- `include/eth/rpc_receipt_source.hpp` — `RpcReceiptSource`, `JsonRpcTransport` — output of factory
- `include/eth/rpc_http_transport.hpp` — `RpcHttpTransport` — HTTP transport for probing + receipt fetching
- `include/eth/json_rpc.hpp` — `make_json_rpc_request`, `parse_*_response` — reuse for `eth_chainId` probing
- `include/eth/chainlist_provider.hpp` — Phase 1 output (input to this phase)
- `src/eth/rpc_manager.cpp` — Current `RpcEndpointPool` implementation (endpoint selection, health states)

### Planning Artifacts
- `AgentDocs/RPC_MANAGER_HANDOFF.md` — Implementation order, current state
- `AgentDocs/EVMRELAY_COMPLETION_PLAN.md` §8 — RPC Manager finalization spec

## Existing Code Insights

### Reusable Assets
- `RpcEndpointPool::next_endpoint()` — Existing round-robin endpoint selection (extend with health check)
- `RpcEndpointState` enum — `kAvailable`, `kTemporarilyFailed`, `kDisabled` already defined
- `RpcHttpTransport` — Already supports HTTP/HTTPS with Boost.Beast. Reuse for `eth_chainId` probing.
- `json_rpc::make_json_rpc_request("eth_chainId", ...)` — Build `eth_chainId` JSON-RPC request
- `json_rpc::parse_*_response` — Parse JSON-RPC responses (add `eth_chainId` response parser if needed)

### Integration Points
- `RpcEndpointPool` gains `mark_temporary_failure()`, `disable()` already exist — extend with backoff
- New `make_receipt_source()` function bridges `RpcManager` → `RpcReceiptSource`
- Audit tool reads config + probing results → validates completeness

## Specific Ideas

No specific requirements beyond the decisions above.

## Deferred Ideas

None — discussion stayed within phase scope.

---
*Phase: 02-RPC Infrastructure*
*Context gathered: 2026-05-25*
