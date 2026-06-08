# Phase 02: RPC Infrastructure - Discussion Log

> **Audit trail only.**

**Date:** 2026-05-25
**Phase:** 02-rpc-infrastructure
**Areas discussed:** eth_chainId probing, Health/backoff strategy, RpcReceiptSourceFactory, Config audit placement

---

## eth_chainId Probing

**User's choice:** Synchronous at startup — POST `eth_chainId` to each HTTP(S) endpoint, validate returned chainId, mark mismatched/failed endpoints before making them available for receipt fetching.

---

## Health/Backoff Strategy

**User's choice:** Exponential backoff — 1s, 2s, 4s, capped at 60s. Three failures within 5-minute window escalate to `kDisabled`. Cooldown metadata on `RpcEndpoint`.

---

## RpcReceiptSourceFactory

**User's choice:** Free function `make_receipt_source(RpcManager&, chain_name, chain_id) -> std::optional<RpcReceiptSource>` — matches existing codegen patterns, no factory class needed.

---

## Config Audit Hook

**User's choice:** Land AUDIT-01 in Phase 2. Audit runs after ChainList ingest + probing, has full picture of endpoint availability.

---

## OpenCode's Discretion

- Metadata fields on RpcEndpoint vs companion struct
- Backoff timer mechanism
- Exact namespace/signature for make_receipt_source
