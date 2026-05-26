# EVM Relay Security Hardening Plan

Source document: `evmrelay/AgentDocs/EVMRELAY_SECURITY_HARDENING_PLAN.md`

Audit scope:

- `evmrelay` on `develop`: event watching, receipt acquisition, bridge event claims, RLPx/RPC receipt paths.
- parent SuperGenius workspace: bridge consensus adapter, validator quorum, mint transaction routing, UTXO state, CI.

Do not implement a generic bridge service. Use the files listed below as the source of truth.

## Current Architecture Snapshot

Status: partially implemented.

Real files and classes:

- `evmrelay/include/eth/eth_watch_service.hpp`, `evmrelay/src/eth/eth_watch_service.cpp`: `eth::EthWatchService`, `eth::WatchEventNotification`, RLPx ETH receipt watching.
- `evmrelay/include/eth/eth_receipt_source.hpp`, `evmrelay/src/eth/eth_receipt_source.cpp`: `eth::IEthReceiptSource`, `eth::ReceiptResult`, `eth::ReceiptBatch`, `eth::EthReceiptSourceBridge`.
- `evmrelay/include/eth/rpc_receipt_source.hpp`, `evmrelay/src/eth/rpc_receipt_source.cpp`: `eth::rpc::RpcReceiptSource` using one `JsonRpcTransport`.
- `evmrelay/include/eth/bridge_event.hpp`, `evmrelay/src/eth/bridge_event.cpp`: `eth::BridgeEventClaim`, `eth::BridgeEventObservation`, `eth::EventDeduper`, `verify_receipt_log`.
- `evmrelay/include/eth/bridge_observation.hpp`, `evmrelay/src/eth/bridge_observation.cpp`: canonical bridge event payload, claim hash, watcher signature verification.
- `src/account/BridgeConsensusAdapter.hpp`, `src/account/BridgeConsensusAdapter.cpp`: bridge-owned consensus subject helpers for `gnus.bridge_event.v1`.
- `src/blockchain/Consensus.hpp`, `src/blockchain/Consensus.cpp`: generic weighted consensus using `ValidatorRegistry`.
- `src/blockchain/ValidatorRegistry.hpp`, `src/blockchain/ValidatorRegistry.cpp`: role/reputation weights and generic quorum threshold.
- `src/account/TransactionManager.hpp`, `src/account/TransactionManager.cpp`: registers bridge subject/certificate handlers and routes finalized bridge claims to `MintFunds`.
- `src/account/InputValidators.hpp`, `src/account/InputValidators.cpp`: `PublicChainInputValidator` currently accepts placeholder public-chain verification.
- `src/account/MintTransactionV2.hpp`, `src/account/MintTransactionV2.cpp`: UTXO-aware bridge/public-chain mint transaction.
- `src/account/UTXOManager.hpp`, `src/account/UTXOManager.cpp`: local UTXO storage, consumption, checkpoints.

Current tests:

- `evmrelay/test/eth/bridge_event_test.cpp`
- `evmrelay/test/eth/bridge_observation_test.cpp`
- `evmrelay/test/eth/eth_receipt_source_test.cpp`
- `evmrelay/test/eth/rpc_receipt_source_test.cpp`
- `test/src/blockchain/bridge_consensus_adapter_test.cpp`
- `test/src/blockchain/consensus_certificate_test.cpp`
- `test/src/account/utxo_manager_test.cpp`

Current behavior:

- evmrelay can build, sign, decode, and verify canonical bridge event claims.
- evmrelay can verify a claim against a single receipt with exact block hash, tx hash, log index, contract, topic, topics, and data matching.
- `RpcReceiptSource` still trusts one `JsonRpcTransport`.
- parent consensus can carry an opaque bridge payload under `gnus.bridge_event.v1`.
- `TransactionManager` approves bridge consensus subjects when they decode and map to a mint request.
- On finalized bridge certificate, `TransactionManager::OnBridgeEventConsensusCertificate` calls `MintFunds`.
- `MintFunds` creates `MintTransactionV2` with a public-chain input reference and mints to the claim recipient.
- `PublicChainInputValidator::VerifyPublicChainSmartContract` is a placeholder that returns `true`.
- Generic consensus quorum still uses `ValidatorRegistry` weights; bridge-specific quorum, effective bridge-weight caps, minimum validator count, and trust-domain diversity are missing.

Security risk:

- A single RPC/provider path can still produce the source evidence that validators later package into a bridge claim.
- A validly encoded bridge claim can pass parent subject handling without local multi-RPC quorum evidence.
- Bridge mints use generic validator reputation weights, so a high-weight genesis/full node can be too influential for bridge mint finality.
- Public-chain mint validation has an explicit placeholder.

Proposed change:

- Keep evmrelay focused on observation and canonical source-event evidence.
- Add quorum evidence and a source-event decision record in evmrelay before bridge claims are proposed.
- Add bridge-specific consensus policy in parent SuperGenius before bridge certificates can mint.
- Replace `PublicChainInputValidator` placeholder behavior with validation against finalized bridge certificate/source-decision metadata.
- Keep bridge parsing outside core consensus through `BridgeConsensusAdapter`.

Risk level: critical.

Estimated complexity: large.

## Primary Security Goal

Status: partially implemented.

Current behavior:

- Domain-bound bridge payloads exist in evmrelay and are carried as opaque consensus subjects in parent code.
- No code currently enforces the full goal that one RPC provider, one validator, one genesis node, one cached peer file, or one CI credential cannot cause a production bridge mint.

Security risk:

- The system can reject malformed payloads, but it cannot yet prove independent source-chain observation or bridge-specific consensus diversity.

Proposed change:

- Define a production bridge channel as valid only when it has:
  - evmrelay multi-RPC quorum evidence;
  - a source-event decision record bound to the source event;
  - a parent bridge consensus policy that caps per-validator effective bridge weight;
  - minimum distinct validator count;
  - minimum operator/trust-domain diversity;
  - durable replay protection for source chain id + tx hash + log index + block hash.

Tests to add:

- evmrelay: quorum decision cannot be created from one provider in production profile.
- parent: bridge certificate with generic quorum but missing bridge-specific policy is rejected for mint routing.
- parent: genesis-only bridge certificate cannot route to `MintFunds`.

Implementation order: after Phase 1 quorum and before enabling production bridge mint routing.

Risk level: critical.

Estimated complexity: medium.

## Bridge-Specific Consensus Model

Status: missing.

Real files and classes:

- `src/blockchain/Consensus.hpp/.cpp`: existing generic `ConsensusManager::TallyVotes` and `CreateCertificate`.
- `src/blockchain/ValidatorRegistry.hpp/.cpp`: generic validator weights and `IsQuorum`.
- `src/account/BridgeConsensusAdapter.hpp/.cpp`: bridge payload adapter, correct place to keep bridge-specific subject helpers.
- `src/account/TransactionManager.cpp`: current bridge subject and bridge certificate handlers.

Current behavior:

- `ConsensusManager::TallyVotes` sums registry weights and calls `ValidatorRegistry::IsQuorum`.
- `ValidatorRegistry::WeightConfig` gives `GENESIS` validators high weight by default.
- Bridge claims use the same consensus certificate path as other subjects.
- No bridge-only quorum policy exists.

Security risk:

- Normal reputation-weighted consensus is not strict enough for bridge mints.
- A genesis or high-reputation validator can contribute too much effective bridge weight.
- Consensus certificates do not expose operator/trust-domain diversity.

Proposed change:

- Add a bridge-owned policy layer in parent code, not inside evmrelay:
  - a bridge-specific policy/tally implementation in `src/account` or `src/blockchain`, using local naming chosen during implementation;
  - configured fields for `threshold_numerator`, `threshold_denominator`, `min_distinct_validators`, `min_trust_domains`, `max_effective_weight_per_validator`, and `genesis_cannot_satisfy_alone`;
  - effective bridge weight computed from a `ConsensusCertificate` and `ValidatorRegistry::Registry`.
  - Keep generic `ConsensusManager` reusable; do not fork core consensus unless tests show the bridge policy cannot be enforced at certificate handling time.
- Enforce this policy in `TransactionManager::OnBridgeEventConsensusCertificate` before `MintFunds`.
- Add operator/trust-domain metadata to the bridge policy config. Do not invent a network service; keep this as local config/data consumed by the bridge certificate handler.

Tests to add:

- `test/src/blockchain/bridge_consensus_adapter_test.cpp` or a new parent bridge policy test:
  - generic quorum reached but bridge threshold not reached rejects mint routing;
  - one high-weight genesis validator cannot satisfy bridge policy;
  - effective bridge weight is capped per validator;
  - minimum distinct validators is enforced;
  - minimum trust domains is enforced;
  - malformed or missing policy rejects in production mode.

Implementation order: Phase 3, before certificate-to-mint routing can be considered production-ready.

Risk level: critical.

Estimated complexity: large.

## Outbound Rollout Policy

Status: missing in this repo.

Real files and classes:

- No EVM exit transaction submitter or EVM exit contract integration was found in this workspace.
- Parent code currently covers incoming bridge mint routing through `TransactionManager::OnBridgeEventConsensusCertificate`.
- `MintTransactionV2` and `UTXOManager` create internal UTXO value after an EVM-origin event.

Current behavior:

- There is no SuperGenius-to-EVM exit path implementation in the audited code.
- There is no rollout-stage config for mainnet/testnet exits, review windows, or outbound value caps.

Security risk:

- The original plan assumed an exit submitter and contract controls that are not present here.
- If an exit path is later added without rollout gates, forged internal value could leave to EVM too quickly.

Proposed change:

- Keep outbound exit hardening as a parent SuperGenius task only when exit code exists.
- Add a production config requirement now that fails closed when an exit channel is configured without:
  - rollout stage;
  - manual review threshold;
  - 24-hour outbound cap;
  - pause flag;
  - destination chain allowlist.
- Do not add fake exit submitter classes.

Tests to add:

- Config audit rejects `utxo_to_evm` production channel unless rollout stage and review policy are explicit.
- Config audit rejects mainnet automatic exit stage unless explicitly enabled.

Implementation order: after bridge mint consensus policy, before any automatic outbound exit work.

Risk level: high.

Estimated complexity: medium once config audit exists; unknown for exit contract integration because it is not present.

## UTXO And EVM Exit Containment

Status: partially implemented for UTXO accounting; missing for bridge provenance.

Real files and classes:

- `src/account/MintTransactionV2.hpp/.cpp`
- `src/account/TransferTransaction.hpp/.cpp`
- `src/account/UTXOStructs.hpp/.cpp`
- `src/account/UTXOManager.hpp/.cpp`
- `src/account/proto/SGTransaction.proto`

Current behavior:

- `MintTransactionV2` carries `chain_id`, `token_id`, `amount`, and `UTXOTxParams`.
- `MintFunds` uses the bridge claim hash as `transaction_hash` and creates a public-chain input reference.
- UTXO records store owner, state, epochs, and optional spent-by tx id.
- UTXO outputs do not carry bridge provenance, source chain id, source tx hash, log index, block hash, or bridge risk marker.
- Transfers can consume bridge-minted UTXOs without preserving a bridge-origin tag.

Security risk:

- Once bridge-originated value is minted into UTXOs, provenance can be lost during internal movement.
- A later exit review path would not be able to distinguish bridge-originated value from ordinary value.

Proposed change:

- Extend UTXO metadata in `src/account/proto/SGTransaction.proto` with bridge provenance fields using additive protobuf fields.
- Add a parent-owned provenance type in `src/account/UTXOStructs.hpp` if needed by existing style.
- On `TransactionManager::OnBridgeEventConsensusCertificate`, bind minted outputs to:
  - source chain id;
  - destination chain id;
  - bridge contract;
  - tx hash;
  - log index;
  - block hash;
  - claim hash/security decision id.
- Update transfer/mint processing so bridge provenance is preserved proportionally through splits and merges.
- Do not change unrelated UTXO selection or storage behavior except where provenance propagation requires it.

Tests to add before production logic:

- `test/src/account/utxo_manager_test.cpp`: UTXO record round-trips bridge provenance.
- transfer tests: split preserves provenance on all outputs.
- transfer tests: merge of bridge and non-bridge value preserves proportional bridge-origin amount or rejects until policy is defined.
- bridge certificate test: minted output includes source event provenance.

Implementation order: after bridge certificate policy and before enabling outbound exits.

Risk level: high.

Estimated complexity: large.

## Phase 0: Inventory And Cut Lines

Status: partially implemented.

Current behavior:

- Bridge evidence producers are in evmrelay.
- Bridge consensus subject creation/decoding is in parent `BridgeConsensusAdapter`.
- Bridge certificate-to-mint routing is in `TransactionManager`.
- No production/development bridge profile boundary exists.

Security risk:

- The code has no explicit cut line between observation, validator vote evidence, bridge consensus, mint execution, UTXO provenance, and future exit handling.

Concrete implementation tasks:

1. Document the bridge path in `AgentDocs/BRIDGE_MINT_PLAN.md` or a new `AgentDocs/BRIDGE_SECURITY_MODEL.md` using only these real paths:
   - evmrelay observation: `eth::BridgeEventClaim`
   - parent consensus subject: `CreateBridgeEventConsensusSubject`
   - parent subject validation: `TransactionManager::HandleBridgeEventConsensusSubject`
   - parent certificate routing: `TransactionManager::OnBridgeEventConsensusCertificate`
   - mint execution: `TransactionManager::MintFunds`
   - public-chain input validation: `PublicChainInputValidator`
2. Add production/development bridge profile config in the parent repo and evmrelay config entry points that actually exist.
3. Add a startup/config audit path before production bridge channels are allowed.
4. Identify code owners for:
   - evmrelay RPC/receipt quorum;
   - parent bridge vote policy;
   - parent mint routing;
   - UTXO provenance;
   - CI workflow hardening.

Tests to add:

- Config audit rejects production bridge profile with missing quorum policy.
- Config audit rejects production bridge profile with missing bridge consensus policy.

Risk level: high.

Estimated complexity: small.

## Phase 1: RPC Quorum And Fail-Closed Receipt Evidence

Status: missing.

Real files and tests:

- Implement in `evmrelay/include/eth` and `evmrelay/src/eth`.
- Existing seam: `eth::rpc::JsonRpcTransport` and `eth::rpc::RpcReceiptSource`.
- Existing tests: `evmrelay/test/eth/rpc_receipt_source_test.cpp`, `evmrelay/test/eth/bridge_event_test.cpp`, `evmrelay/test/eth/json_rpc_test.cpp`.

Current behavior:

- `RpcReceiptSource` calls one transport for `eth_getBlockByNumber`, `eth_getLogs`, and `eth_getTransactionReceipt`.
- `ReceiptResult` has receipt, tx hash, block number/hash, and log indexes, but no provider votes or quorum metadata.

Security risk:

- A single compromised RPC provider can supply a relayable event.
- Provider DoS can collapse verification to the remaining provider unless fail-closed behavior is explicit.

Concrete implementation tasks:

1. Add provider identity, quorum policy, provider vote, and quorum result data structures under `evmrelay/include/eth`; these are new and are not present today.
2. Add a small quorum layer that uses the existing `JsonRpcTransport` seam; do not replace `RpcReceiptSource` with unrelated RPC infrastructure.
3. Normalize and compare:
   - block head by number/tag;
   - logs by tx hash/log index/block hash/address/topics/data;
   - transaction receipt by status, tx hash, block hash, log index, address, topics, data.
4. Extend or wrap `IEthReceiptSource` so production bridge evidence receives quorum metadata without breaking current tests.
5. Keep single-provider `RpcReceiptSource` available for development tests only.
6. Fail closed on provider disagreement, missing block hash/log index, wrong chain id, one trust domain, or insufficient provider count.

Tests to add before production logic:

- providers disagree on block hash.
- providers disagree on receipt status.
- providers disagree on event logs.
- only internal provider remains available.
- only one external provider remains available.
- one provider returns wrong chain id.
- provider omits block hash or log index.
- development profile can still use existing single-provider tests.

Risk level: critical.

Estimated complexity: large.

## Phase 2: Source-Event Decision Record

Status: missing.

Real files and tests:

- Add to evmrelay near `eth::BridgeEventClaim`; choose filenames during implementation to match evmrelay style.
- Use in parent bridge subject creation through `src/account/BridgeConsensusAdapter.hpp/.cpp` only after serialization format is defined.
- Existing tests to extend: `evmrelay/test/eth/bridge_observation_test.cpp`, `test/src/blockchain/bridge_consensus_adapter_test.cpp`.

Current behavior:

- `BridgeEventClaim` binds source/destination chain, block number/hash, tx hash, log index, bridge contract, topics/data, sender, token/nonce, amount, recipient, observed time, and finality depth.
- It does not bind quorum policy id, provider vote summary, degraded state, or re-check metadata.

Security risk:

- Parent validators can approve a bridge claim without proof of how source-chain safety was decided.
- Logs cannot reconstruct whether the claim came from healthy independent observations.

Concrete implementation tasks:

1. Add a decision record with:
   - all current `BridgeEventClaim` fields;
   - quorum policy id;
   - provider vote summary;
   - degraded state;
   - finality head kind/block number;
   - decision timestamp;
   - relay channel/profile id;
   - normalized payload/log hash.
2. Add construction validation: reject missing chain ids, block hash, tx hash, log index, source contract, topic0, finality depth, and quorum metadata.
3. Add deterministic decision hash.
4. Require bridge claim creation/signing for production to come from a healthy source-event decision record.
5. Persist the decision before parent vote proposal creation. Use the existing CRDT/storage style only after choosing the storage owner.

Tests to add:

- cannot construct without required domain fields.
- decision hash changes when any domain field changes.
- stale finality depth is rejected.
- degraded decision cannot create production bridge vote evidence.
- bridge consensus adapter rejects payloads missing decision/quorum metadata once the payload format is upgraded.

Risk level: critical.

Estimated complexity: medium.

## Phase 3: Bridge Certificate Policy And Consensus Thresholds

Status: missing.

Real files and tests:

- Parent implementation: `src/account/BridgeConsensusAdapter.hpp/.cpp`, `src/account/TransactionManager.cpp`, `src/blockchain/Consensus.hpp/.cpp`, `src/blockchain/ValidatorRegistry.hpp/.cpp`.
- Tests: `test/src/blockchain/bridge_consensus_adapter_test.cpp`, `test/src/blockchain/consensus_certificate_test.cpp`.

Current behavior:

- `TransactionManager::HandleBridgeEventConsensusSubject` checks only nonzero chain ids and mappability to `BridgeEventMintRequest`.
- `TransactionManager::OnBridgeEventConsensusCertificate` mints after certificate handling and `MintFunds`.
- `PublicChainInputValidator::VerifyPublicChainSmartContract` returns `true`.

Security risk:

- A bridge subject can be approved without local multi-RPC quorum evidence or bridge policy.
- Generic quorum can authorize bridge mints with unsafe effective validator concentration.

Concrete implementation tasks:

1. Add parent bridge-specific policy and tally code using the existing `ConsensusCertificate` and `ValidatorRegistry` data.
2. Validate `BridgeEventClaim` or the source-event decision record against configured bridge channels:
   - source chain id;
   - destination chain id;
   - source bridge contract;
   - allowed topic;
   - required finality depth;
   - relay profile id/quorum policy id when Phase 2 exists.
3. Add durable replay check keyed by source chain id, tx hash, log index, and block hash.
4. Add bridge certificate policy validation before `MintFunds`.
5. Replace `PublicChainInputValidator::VerifyPublicChainSmartContract` placeholder with validation that the transaction references an accepted bridge certificate/source-event decision.

Tests to add:

- vote/subject without quorum evidence rejects.
- unknown bridge contract rejects.
- wrong chain id rejects.
- stale finality rejects.
- replayed tx hash/log index rejects.
- bridge certificate with unsafe threshold rejects.
- bridge certificate where genesis alone satisfies generic quorum rejects.
- `PublicChainInputValidator` rejects when no accepted bridge certificate backs the public-chain mint.

Risk level: critical.

Estimated complexity: large.

## Phase 4: Durable Relay State Machine

Status: missing.

Real files and tests:

- evmrelay currently has only `eth::EventDeduper` in `evmrelay/include/eth/bridge_event.hpp`.
- Parent replay/transaction state lives in `src/account/TransactionManager.cpp`, `src/account/UTXOManager.cpp`, and consensus certificate storage in `src/blockchain/Consensus.cpp`.

Current behavior:

- evmrelay dedupes bridge events in memory by source chain id, tx hash, and log index.
- Parent transaction replay protection is account nonce/certificate oriented, not source-event oriented.

Security risk:

- Crash/restart can lose bridge event dedupe state.
- A reorg or changed block hash for the same tx/log id is not represented as a durable rejected/paused state.
- Certificate-to-mint routing can be retried without a bridge-specific idempotency record.

Concrete implementation tasks:

1. Add a durable bridge event state store using the existing persistence style selected by the owning component.
2. State identity:
   - source chain id;
   - destination chain id;
   - bridge contract;
   - tx hash;
   - log index;
   - block hash;
   - claim/decision hash.
3. States:
   - `Discovered`
   - `QuorumVerified`
   - `VoteRejected`
   - `Voted`
   - `MintConsensusReached`
   - `MintSubmitted`
   - `Finalized`
   - `Rejected`
   - `Paused`
4. Keep outbound exit states out until exit code exists.
5. Make `TransactionManager::OnBridgeEventConsensusCertificate` idempotent for a finalized bridge event.

Tests to add:

- restart after quorum verified does not duplicate vote proposal.
- restart after certificate does not duplicate mint.
- same tx/log with changed block hash is rejected or paused.
- finalized event cannot re-enter mint routing.

Risk level: high.

Estimated complexity: large.

## Phase 5: Production Configuration Validation

Status: missing.

Real files and tests:

- evmrelay config entry points: `evmrelay/include/eth/eth_watch_cli.hpp`, `evmrelay/include/eth/eth_watch_service.hpp`.
- parent likely needs a new bridge config component near `src/account` or existing application config once selected.
- Workflow/config tests should be added alongside the implementation owner.

Current behavior:

- evmrelay `EthWatchServiceConfig` configures watches, chains, discovery, and connection counts.
- No production bridge profile validates provider quorum, trust domains, validator thresholds, provenance, or exit policy.

Security risk:

- Production can start with dev/test-style defaults.
- Missing quorum or threshold fields can silently degrade bridge safety.

Concrete implementation tasks:

1. Add explicit production bridge profile validation.
2. Required fields:
   - chain id and canonical name;
   - source bridge contract;
   - destination chain id/receiver;
   - event topic;
   - confirmation/finality depth;
   - quorum policy;
   - provider trust domains;
   - bridge consensus policy;
   - validator trust-domain mapping;
   - replay store;
   - provenance policy.
3. Reject production startup when:
   - confirmation depth is zero;
   - one provider/trust domain is configured;
   - bridge policy permits 1-of-1 or genesis-only quorum;
   - quorum fallback to single provider is enabled;
   - bridge provenance policy is missing;
   - outbound exit config exists without rollout/review policy.
4. Emit a structured startup security summary.

Tests to add:

- missing quorum policy fails.
- missing bridge contract mapping fails.
- threshold reduction fails.
- confirmation-depth reduction fails.
- 1-of-1 validator policy fails.
- genesis-alone policy fails.
- development profile still allows current single-provider tests.

Risk level: critical.

Estimated complexity: medium.

## Phase 6: Logging, Metrics, And Alerts

Status: partially implemented for generic logs; missing bridge security telemetry.

Real files:

- evmrelay: `EthWatchService` stats in `evmrelay/include/eth/eth_watch_service.hpp`.
- parent: logging in `TransactionManager.cpp`, `Consensus.cpp`, `ValidatorRegistry.cpp`.

Current behavior:

- evmrelay exposes counters for ETH messages, receipts, logs, matched logs, discarded logs, and decode failures.
- parent logs bridge certificate routing but does not log quorum/provider/bridge policy details.

Security risk:

- Operators cannot reconstruct why a bridge event was accepted.
- Unsafe config or threshold changes have no bridge-specific alert surface.

Concrete implementation tasks:

1. Add structured logs for:
   - source/destination chain id;
   - bridge contract;
   - tx hash;
   - log index;
   - block number/hash;
   - finality depth;
   - quorum policy id;
   - provider vote summary;
   - decision hash;
   - bridge effective vote tally;
   - mint transaction hash;
   - rejection reason.
2. Add metrics counters for:
   - provider divergence;
   - quorum unavailable;
   - degraded decision rejected;
   - bridge policy reject reason;
   - replay reject;
   - genesis/high-weight validator cap hit;
   - bridge mint routed.
3. Do not log private keys, provider URLs with credentials, bearer tokens, or session tokens.

Tests to add:

- unit tests for redaction helpers if provider identifiers include URLs.
- bridge policy rejection logs include reason codes without secrets.

Risk level: medium.

Estimated complexity: medium.

## Phase 7: CI/CD And Supply Chain

Status: partially implemented with known gaps.

Real files:

- `.github/workflows/cmake.yml`
- `.github/workflows/build-release-tags.yml`

Current behavior:

- `GH_TOKEN: ${{ secrets.GNUS_TOKEN_1 }}` is set at job scope.
- containers use `ghcr.io/geniusventures/debian-bullseye:latest`.
- `cmake.yml` grants `--cap-add=IPC_LOCK`.
- third-party actions are tag-pinned, e.g. `actions/checkout@v6`.
- self-hosted cleanup uses broad `rm -rf` patterns.
- workflow path filters ignore `.github/workflows/**` in at least `cmake.yml`.

Security risk:

- Compromised job step gets broad token access.
- Mutable action/container tags can change build behavior.
- Self-hosted runner state can affect builds or leak secrets.

Concrete implementation tasks:

1. Add explicit least-privilege `permissions:` to both workflows.
2. Move privileged tokens from job scope to the specific steps that need them.
3. Replace long-lived `GNUS_TOKEN_1` for release/download flows where OIDC or scoped tokens are feasible.
4. Pin actions by commit SHA.
5. Pin containers by digest.
6. Remove `IPC_LOCK` unless a tested build step requires it.
7. Require review for `.github/workflows/**`; do not path-ignore workflow changes.
8. Replace broad cleanup with scoped workspace cleanup that cannot escape expected directories.
9. Add SBOM/provenance/signature generation for release artifacts.

Tests/checks to add:

- workflow lint/check that fails on `:latest`, job-scope `GH_TOKEN`, unpinned third-party actions, broad workflow permissions, and workflow path-ignore.

Risk level: high.

Estimated complexity: medium.

## Phase 8: Runtime And Developer Controls

Status: missing in repo documentation/config.

Real files:

- No deployment manifests were found in the audited paths.
- Add documentation under `AgentDocs` or `SECURITY.md`.

Current behavior:

- No repo-level runtime hardening profile for bridge relay operators was found.
- No developer compromise response doc was found in the audited paths.

Security risk:

- A compromised developer session or runner can poison config/build/runtime state without a standard response.

Concrete implementation tasks:

1. Add `SECURITY.md` or `AgentDocs/SECURITY_HARDENING.md` with:
   - disposable VM/container guidance for untrusted repos;
   - token rotation steps;
   - hardware-backed MFA requirement;
   - local cloud credential handling;
   - bridge relay compromise response.
2. When deployment manifests exist, harden them with:
   - non-root runtime;
   - read-only root filesystem where practical;
   - dropped Linux capabilities;
   - no privileged containers;
   - restricted debug tooling;
   - clean-image restart after suspected tampering.

Tests/checks to add:

- Deployment manifest checks only after manifests exist.
- Documentation presence check can be added to CI if desired.

Risk level: medium.

Estimated complexity: small for docs; unknown for deployment manifests because they are not present.

## Implementation Order

1. Add tests for evmrelay RPC quorum failure cases in `evmrelay/test/eth/rpc_receipt_source_test.cpp`.
2. Implement evmrelay `RpcQuorumPolicy`, provider votes, quorum result, and fail-closed comparisons.
3. Add evmrelay source-event decision record tests and implementation.
4. Upgrade bridge claim/proposal flow to require a healthy source-event decision record in production.
5. Add parent bridge policy tests for threshold, caps, distinct validators, trust domains, and genesis-alone rejection.
6. Implement parent bridge policy and enforce it in `TransactionManager::OnBridgeEventConsensusCertificate` before `MintFunds`.
7. Replace `PublicChainInputValidator::VerifyPublicChainSmartContract` placeholder with certificate/security-decision-backed validation.
8. Add durable bridge event replay/state storage.
9. Add UTXO bridge provenance tests and additive protobuf fields.
10. Implement provenance propagation through bridge mint and transfer.
11. Add production config audit for evmrelay and parent bridge profile.
12. Add bridge logs/metrics/rejection reason codes.
13. Harden GitHub workflows.
14. Add runtime/developer security documentation.

## Definition Of Done

- One RPC provider cannot produce a production bridge vote or mint.
- RPC/provider DoS cannot degrade production bridge verification into single-provider trust.
- Bridge claims bind source chain id, destination chain id, bridge contract, tx hash, log index, block hash, event topic/data, finality, quorum policy, and provider vote summary.
- Parent bridge certificate routing rejects missing or degraded quorum evidence.
- Bridge mint consensus uses bridge-specific effective weight, minimum distinct validators, and trust-domain diversity.
- Genesis or any one high-reputation validator cannot satisfy bridge mint consensus alone.
- `PublicChainInputValidator` no longer returns unconditional success for public-chain bridge mints.
- Source event replay is durably rejected.
- Reorg/block-hash changes move bridge events to rejected or paused state.
- Bridge-originated UTXO value preserves provenance through internal movement.
- Unsafe production bridge configs fail startup.
- Logs reconstruct every accepted and rejected bridge decision without leaking secrets.
- CI workflows avoid mutable action/container tags, broad job-scope secrets, and unsafe self-hosted cleanup.
