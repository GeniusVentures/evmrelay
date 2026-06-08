# Phase 01: ChainList Provider & intx Documentation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-05-25
**Phase:** 01-chainlist-provider-intx-documentation
**Areas discussed:** Chain filtering policy, Download location, Endpoint format, Error handling

---

## Chain Filtering Policy

**User's choice:** Use `examples/chains_config.json` to determine which chains to include for filtering — only produce endpoints for chains configured there.

**Notes:** This keeps the chain configuration authority in one place and prevents ingesting endpoints for unsupported chains.

---

## Download Location

**User's choice:** URL configurable via `chains_config.json`; local cache packaged with distribution.

**Notes:** Not hardcoded in C++. The local cache serves as a fallback when `chainid.network` is unreachable.

---

## Endpoint Format

**User's choice:** Direct `std::vector<RpcEndpointConfig>` output — no intermediate `ChainListEntry` struct.

**Notes:** Matches the existing `rpc_manager_config.cpp` pattern where `build_endpoint()` constructs `RpcEndpointConfig` directly from parsed JSON.

---

## Error Handling

**User's choice:** Fallback to local cache when `chainid.network` is unreachable. If both fail, return empty vector — not a fatal error.

**Notes:** `RpcManager` can operate with partially available endpoints. Startup should not fail because an external metadata source is down.

---

## OpenCode's Discretion

- Download mechanism choice (Boost.Beast HTTP or library utility)
- Exact JSON schema fields to parse
- File naming and location within `include/eth/` / `src/eth/`
