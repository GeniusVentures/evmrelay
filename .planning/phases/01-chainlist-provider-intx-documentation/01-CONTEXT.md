# Phase 01: ChainList Provider & intx Documentation - Context

**Gathered:** 2026-05-25
**Status:** Ready for planning

## Phase Boundary

Ingest public chain metadata from `chainid.network/chains.json` (with `ethereum-lists/_data/chains/*.json` fallback), filter to only chains configured in `chains_config.json`, deduplicate/validate RPC URLs, produce a `std::vector<RpcEndpointConfig>` consumable by `RpcManager`. Document the vendored `intx.hpp` upstream provenance.

## Implementation Decisions

### Chain Filtering & Data Source
- **D-01:** Only produce endpoints for chains listed in `examples/chains_config.json` — this is the authority for which chains evmrelay supports. Unconfigured chains from ChainList are skipped.
- **D-02:** The `chainid.network/chains.json` URL is configurable via `chains_config.json` (new field or existing config mechanism), not hardcoded in C++.
- **D-03:** A local cache of `chains.json` is packaged with the distribution for offline/fallback use. When `chainid.network` is unreachable, the loader falls back to the local cache rather than failing startup.

### Endpoint Format
- **D-04:** The parser produces `std::vector<RpcEndpointConfig>` directly — no intermediate `ChainListEntry` struct. Conversion to `RpcEndpointConfig` happens inline during parsing (matching the `rpc_manager_config.cpp` pattern where `build_endpoint()` constructs `RpcEndpointConfig` from parsed JSON).
- **D-05:** Each parsed chain entry maps to one `RpcEndpointConfig` per RPC URL. Default values for `priority`, `weight`, `rate_limit_per_second` are `0`; `is_public = true`; `verified = false`.

### Filtering Rules
- **D-06:** Keep only `http://` and `https://` URLs. Drop `wss://` URLs entirely (no separate websocket list needed).
- **D-07:** Drop URLs containing API-key placeholders: `${INFURA_API_KEY}`, `${ALCHEMY_API_KEY}`, `${ANKR_API_KEY}`, `${POKT_API_KEY}`, `${BLASTAPI_API_KEY}`.
- **D-08:** Exclude chains with `status = "deprecated"`. Include testnets only if the chain is present in `chains_config.json`. Incubating chains are included by default (config controls filtering).

### Error Handling
- **D-09:** `chainid.network` unreachable → fallback to local cache. If both fail, return empty vector (not a fatal error — `RpcManager` will operate with whatever endpoints are available).

### intx Documentation
- **D-10:** Add a comment at the top of `include/rlp/intx.hpp` documenting the upstream `chfast/intx` repo URL and vendored commit SHA (already done — `evmrelay/include/rlp/intx.hpp:4`).

### OpenCode's Discretion
- Download mechanism (Boost.Beast HTTP vs. other) — use whatever fits the existing RPC transport patterns.
- Exact JSON schema fields — derive from `chainid.network/chains.json` format; skip fields not needed for `RpcEndpointConfig` production.
- File naming and location within `include/eth/` / `src/eth/`.

## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Project Foundation
- `.planning/PROJECT.md` — Core value, constraints, key decisions
- `.planning/REQUIREMENTS.md` — v1 requirements (CHNL-01 through CHNL-05, CODE-01)
- `.planning/ROADMAP.md` — Phase goal, success criteria, requirement mapping

### Codebase Patterns
- `include/base/json_utility.hpp` — Schema-driven JSON parsing (`JsonSchemaObject`, `JsonSchemaArray`, `JsonParsedObject`)
- `include/eth/rpc_manager_config.hpp` — `RpcEndpointConfig` / `RpcManagerConfig` structs, config loading free functions
- `src/eth/rpc_manager_config.cpp` — Schema-based config loading pattern to follow
- `include/eth/rpc_manager.hpp` — `RpcEndpoint`, `RpcEndpointPool`, `RpcManager` — consumers of the output

### Chain Configuration
- `examples/chains_config.json` — Authority for which chains evmrelay supports (filter source)
- `chainid.network/chains.json` — External data source for chain metadata and RPC URLs (documented format)

### Planning Artifacts
- `AgentDocs/EVMRELAY_COMPLETION_PLAN.md` §7 — ChainList requirements and test specifications
- `AgentDocs/RPC_MANAGER_HANDOFF.md` — Implementation order

## Existing Code Insights

### Reusable Assets
- `rlp::base::json::JsonSchemaObject` / `parse_schema_object()` — The schema-driven JSON parser. All new JSON parsing must use this, not manual `boost::json` access.
- `eth::rpc::RpcEndpointConfig` — Existing config struct; output target. Fields already defined: `chain_name`, `chain_id`, `url_template`, `api_key_env_var`, `api_key_literal`, `priority`, `weight`, `rate_limit_per_second`, `is_paid`, `is_public`, `verified`.
- `eth::rpc::load_rpc_manager_config_result_from_json_text()` — Pattern for config loading: declare schema → parse → build domain objects → return `JsonResult<T>`.

### Established Patterns
- Free functions in `eth::rpc` namespace for config/data loading (no unnecessary classes)
- `JsonResult<T>` return type for all parsing functions
- `[[nodiscard]]` on all result-returning functions
- `BOOST_OUTCOME_TRY` for early-return on parse errors
- Schema declared as `static const` local in anonymous namespace

### Integration Points
- Output feeds into `RpcManager` constructor or config loading — produces `std::vector<RpcEndpointConfig>`
- No integration with `EthWatchService` required at this phase
- New files under `include/eth/` and `src/eth/`, tests under `test/eth/`

## Specific Ideas

No specific requirements beyond the decisions above — open to standard approaches matching existing codebase patterns.

## Deferred Ideas

None — discussion stayed within phase scope.

---

*Phase: 01-ChainList Provider & intx Documentation*
*Context gathered: 2026-05-25*
