# EVM Relay Security Hardening Plan

Source prompt: LayerZero/KelpDAO-style incident where a compromised developer session, poisoned RPC/process state, and external provider DoS caused a bridge path to trust forged chain data.

Scope: `evmrelay` event watching, receipt acquisition, validator vote evidence, peer/cache configuration, CI, and operator controls. `evmrelay` observes EVM chain state; SuperGenius validators independently verify the event through multiple RPC paths; bridge mint consensus occurs through reputation-weighted validator voting; and outbound UTXO-to-EVM exits are contained by the EVM exit contract review/throttle window.

## Current Architecture Snapshot

- `EthWatchService` owns P2P chain watching, peer discovery, RLPx/ETH sessions, event filtering, and decoded event callbacks.
- `IEthReceiptSource` is the transport-neutral receipt interface. It currently exposes receipts and batches without quorum metadata.
- `RpcReceiptSource` reads block heads, logs, and receipts through one `JsonRpcTransport`.
- Bridge event evidence binds source chain, destination chain, block number/hash, tx hash, log index, bridge contract, event topic/data, sender, nonce/amount, recipient, observed time, finality depth, and RPC quorum metadata.
- SuperGenius bridge mint authorization should require high-threshold validator consensus. The genesis node may have high initial reputation weight, but it must not be able to satisfy bridge mint consensus by itself.
- The UTXO-to-EVM exit path has the strongest containment control: review windows, outbound transfer throttles, pause, and burn/recovery controls on the EVM exit contract.
- The release workflow uses self-hosted runners, broad token exposure in job env, unpinned container tags, and unpinned actions.

## Primary Security Goal

No single RPC provider, RLPx peer, cached peer file, process, developer credential, validator key, CI job, admin key, validator, or high-reputation genesis node can cause a production bridge mint or EVM exit by itself.

The relay and validators must fail closed when independent observations are unavailable, divergent, stale, or insufficiently bound to the bridge event. The system should be designed so forged EVM state requires either source-contract compromise, validator quorum compromise, shared verifier/config/build failure, voting-weight/governance compromise, or severe finality failure.

## Bridge-Specific Consensus Model

The bridge path should be stricter than normal SuperGenius reputation consensus because one false positive can create credit or release external liquidity, while a false negative usually delays a user.

Recommended bridge mint rule:

```text
Mint allowed only if:
  each voting validator independently verifies the source event
  each validator uses local multi-RPC quorum
  effective bridge voting weight >= bridge threshold
  distinct validator count >= minimum count
  trust-domain/operator diversity >= minimum diversity
  genesis node cannot satisfy threshold alone
  source event is domain-bound and replay-protected
```

Initial bridge thresholds should be near-total but not literal 100%, so one offline validator cannot halt the bridge:

```text
Initial / low validator count:
  >= 90% effective bridge voting weight
  AND >= 3 independent validators
  AND >= 2 or 3 infrastructure/trust domains
  AND genesis node cannot satisfy threshold alone

High-value or anomalous bridge mints:
  >= 95% effective bridge voting weight
  AND stricter monitoring / delayed outbound liquidity
```

Use an effective bridge-weight cap even if normal reputation gives one node much higher weight:

```text
max_effective_bridge_weight_per_validator = 33% to 40%
```

This keeps the genesis node important at bootstrap without making it a bridge single point of failure.

## Outbound Rollout Policy

SuperGenius-to-EVM bridging should be rolled out in stages while the exit path hardens.

```text
Stage 0:
  EVM exits target testnets only.
  Mainnet exits are manually handled outside the automatic bridge.

Stage 1:
  Manual review for all SuperGenius-to-EVM exits.

Stage 2:
  Automatic exits for <= $100.
  Manual review for > $100.

Stage 3:
  Automatic exits for <= $250.
  Manual review for > $250.

Stage 4:
  Automatic exits with cool-off / review window.
  Higher-value or anomalous exits stay manual or require elevated review.
```

Each stage should require explicit governance/config activation, a rollback plan, metrics, alerts, and a cap on aggregate 24-hour outbound value per token and chain.

## UTXO And EVM Exit Containment

The system bridges from EVM into SuperGenius UTXO, then later bridges from SuperGenius UTXO out to EVM. The EVM exit contract has the review/throttle window, so the most important containment point is the UTXO-to-EVM exit.

Policy:

- EVM-to-UTXO minting still requires high bridge consensus because it creates internal value.
- Bridge-originated UTXO value should retain provenance until EVM exit.
- UTXO-to-EVM withdrawals should include enough source/provenance references for the EVM exit contract and operators to review risk.
- EVM exit review windows, outbound transfer throttles, pause, and burn/recovery controls provide the final containment layer.

If bridge-originated UTXO value can be transferred internally, provenance must not be lost. Splits, merges, transfers, or conversions should preserve a proportional bridge-origin tag, message id, or risk marker where practical. The goal is to prevent bad EVM-to-UTXO credit from becoming indistinguishable from ordinary value before exit review.

## Phase 0: Inventory And Cut Lines

1. Classify every event callback and bridge evidence consumer as one of:
   - observational only;
   - creates an off-chain alert or metric;
   - creates validator vote evidence;
   - contributes to SuperGenius bridge mint consensus;
   - submits or triggers UTXO-to-EVM exit action.
2. Identify where reputation-weighted bridge consensus is enforced and where genesis-node voting weight is capped for bridge operations.
3. Identify how bridge-originated UTXO provenance is preserved through transfers, splits, merges, and exits.
4. Define production versus development/test profile flags. Production should refuse startup unless quorum, confirmation, peer, validator-threshold, provenance, exit-review, and admin policies are explicit.

Deliverables:

- Code owners for RPC observation, bridge vote evidence, validator consensus, UTXO provenance, EVM exit controls, config validation, CI, and deployment hardening.
- Threat-model note in `SECURITY.md` or `AgentDocs`.
- A list of all production bridge channels and whether this repository observes, generates validator vote evidence, contributes to mint consensus, or submits exit transactions.

## Phase 1: RPC Quorum And Fail-Closed Receipt Evidence

Current gap: `RpcReceiptSource` has a single `JsonRpcTransport` and trusts one RPC response path for `eth_getBlockByNumber`, `eth_getLogs`, and `eth_getTransactionReceipt`.

Implement:

- `RpcProviderId` with name, URL redaction label, provider family, trust domain, and internal/external classification.
- `RpcQuorumPolicy` with `min_providers`, `min_trust_domains`, `threshold`, required block tag policy, max skew, and `fail_closed` behavior.
- `RpcQuorumClient` that queries providers independently and returns `QuorumResult<T>`.
- `ProviderVote<T>` with provider id, latency, success/error, normalized response hash, block number, block hash, chain id when applicable, and response-specific fields.
- Explicit degraded states: `Healthy`, `Divergent`, `QuorumUnavailable`, `ProviderDoS`, `Paused`.

Apply quorum to:

- finality head selection;
- `eth_getLogs` ranges;
- `eth_getTransactionReceipt`;
- re-check by exact block hash before a validator votes;
- destination confirmation checks if this repo later submits UTXO-to-EVM exit transactions.

Fail closed on:

- insufficient provider count;
- only one trust domain available;
- provider disagreement on block hash, receipt status, tx hash, log index, emitter, event topics/data, chain id, or finality head;
- external outage that would otherwise leave internal-only data;
- internal outage that would otherwise leave one external provider.

Tests:

- providers disagree on block hash;
- providers disagree on receipt status;
- providers disagree on event logs;
- external providers unavailable while internal provider returns a relayable event;
- internal providers unavailable while one external provider returns a relayable event;
- only one provider available in production mode;
- provider returns wrong chain id;
- provider omits block hash or log index.

## Phase 2: SecurityDecision Object

Current gap: the receipt interfaces return receipts/batches, but not a decision object proving how a validator concluded an event is safe to vote on.

Add `SecurityDecision` as the object passed from observation to validator voting:

- source chain id;
- destination chain id;
- source bridge contract;
- destination bridge contract or configured receiver;
- tx hash;
- log index;
- block number;
- block hash;
- required confirmation depth;
- finality head kind and block number;
- payload hash;
- message nonce;
- event topic0 and normalized log hash;
- quorum policy id;
- provider vote summary;
- degraded state;
- decision timestamp;
- relay channel/profile id.

Rules:

- Build validator vote evidence only from a `SecurityDecision`.
- Persist the decision before voting.
- Re-check the source receipt by exact block hash immediately before voting.
- Include enough metadata in logs to reconstruct the decision without logging secrets.

Tests:

- decision cannot be constructed without block hash, tx hash, log index, source chain, destination chain, and quorum metadata;
- decision hash changes when any domain field changes;
- stale finality depth is rejected;
- divergent decision cannot enter validator voting state.

## Phase 3: ValidatorVotePolicy And Consensus Thresholds

Current gap: production validator voting needs an explicit policy gate. A validator should vote only after local multi-RPC quorum and local bridge policy checks.

Implement:

- `ValidatorVotePolicy` with configured source peers, destination receivers, required confirmation depths, allowed event topics, nonce policy, replay store, quorum policy, and bridge consensus policy.
- `ValidatorVoteRequest` containing `SecurityDecision`, normalized bridge event evidence, validator identity, effective bridge weight, and local vote metadata.
- `ValidatorVotePolicy::validate(request)` returning structured accept/reject reasons.
- A checked vote entrypoint as the only production path for producing validator vote evidence or submitting a validator vote transaction.
- Structured logs for every rejected vote attempt.

Production validator voting must reject:

- missing quorum evidence;
- degraded state other than `Healthy`;
- unknown source peer or destination receiver;
- wrong chain id;
- unknown event topic;
- stale finality;
- nonce gap or replay;
- unsafe 1-of-1 validator/verifier threshold;
- bridge consensus that genesis can satisfy alone;
- bridge consensus without minimum distinct validator and trust-domain counts;
- policy/profile mismatch.

Consensus target:

- Normal SuperGenius reputation consensus can remain reputation-weighted.
- Bridge mint consensus should use stricter bridge-specific thresholds.
- Cap effective bridge voting weight per validator so the genesis node or any high-reputation node cannot mint by itself.
- High-value or anomalous bridge mints require around 95% effective bridge voting weight or manual/operator review before outbound liquidity is made available.

Tests:

- vote request without quorum evidence;
- vote request with a single provider vote;
- vote request from unknown bridge contract;
- vote request with stale confirmation depth;
- replayed tx hash/log index;
- nonce gap;
- production config with 1-of-1 validator threshold;
- production config where genesis alone can satisfy bridge mint threshold;
- bridge threshold reduction without governance protection.

## Phase 4: Durable Relay State Machine

Current gap: `EventDeduper` is in-memory. That is not sufficient for crash recovery or replay protection around irreversible bridge actions.

Implement a durable `RelayStateMachine`:

```text
Discovered
QuorumPending
QuorumVerified
VoteRejected
Voted
MintConsensusReached
ExitSubmitted
ExitReviewPending
ExitConfirmed
Finalized
Rejected
Paused
```

State identity:

- source chain id;
- destination chain id;
- source bridge contract;
- tx hash;
- log index;
- block hash;
- message nonce or payload hash;
- bridge-origin provenance id when the value enters SuperGenius UTXO.

Rules:

- Every state transition is explicit and persisted.
- Replays are idempotent and cannot re-enter voting after terminal states.
- Reorgs move affected messages to `Rejected` or `Paused`.
- Do not mark EVM exit complete until destination confirmation is quorum verified and the exit review window has passed or been explicitly approved.
- Bridge-originated UTXO provenance follows splits, merges, transfers, and exits.

Tests:

- crash/restart after `QuorumVerified` does not duplicate validator voting;
- crash/restart after `Voted` does not submit duplicate votes;
- reorg removes previously observed event;
- block hash changes for same tx/log id;
- destination confirmation mismatch;
- bridge-originated UTXO split/merge preserves provenance;
- disputed provenance pauses or rejects EVM exit.

## Phase 5: Production Configuration Validation

Add strict production validation:

- explicit chain id, canonical name, source bridge contract, destination receiver, event topic, confirmation depth, quorum policy, provider trust domains, validator set, effective bridge-weight caps, bridge threshold, admin policy, provenance policy, exit review policy, rollout stage, per-stage value thresholds, and replay store;
- no mutable defaults in production;
- no production startup with `confirmation_depth = 0`;
- no startup with one provider, one trust domain, or one validator for high-value channels;
- no startup where genesis or any single validator can satisfy bridge mint threshold alone;
- no silent fallback from quorum to single provider;
- startup security summary emitted as structured logs.

Suggested command:

```bash
evmrelay --dry-run-config-audit --profile production --config <path>
```

Tests:

- missing quorum policy fails;
- missing peer mapping fails;
- threshold reduction fails;
- confirmation-depth reduction fails;
- 1-of-1 validator set fails;
- genesis-alone threshold fails;
- missing UTXO provenance policy fails for bridge channels;
- missing EVM exit review policy fails for UTXO-to-EVM channels;
- outbound mainnet exit enabled before approved rollout stage fails;
- automatic exit threshold above the active rollout limit fails;
- development profile still allows local single-provider tests.

## Phase 6: Logging, Metrics, And Alerts

For each relay decision, log:

- source/destination chain id;
- source/destination contract;
- tx hash;
- log index;
- block number/hash;
- confirmation depth;
- provider vote summary;
- quorum result;
- validator id / observer address;
- effective bridge voting weight;
- message hash;
- bridge-origin provenance id where applicable;
- state transition;
- rejection reason if rejected.

Add metrics and alerts for:

- RPC provider divergence;
- quorum unavailable;
- internal-only or external-only degraded operation;
- unsafe failover;
- new validator/admin address;
- threshold reduction;
- confirmation-depth reduction;
- unknown contract emitting a watched topic;
- repeated rejected vote attempts;
- genesis or one operator approaching bridge threshold alone;
- high-value mint request;
- UTXO-to-EVM exit entering review;
- outbound rollout stage change;
- manual review threshold change;
- exit review override, pause, burn, or throttle change;
- log export gaps;
- state machine stuck in `QuorumPending`, `Voted`, `MintConsensusReached`, `ExitSubmitted`, or `ExitReviewPending`.

Do not log:

- private keys;
- bearer tokens;
- session tokens;
- raw provider URLs with embedded credentials.

## Phase 7: CI/CD And Supply Chain

Current gaps observed in `.github/workflows`:

- release workflow uses `GH_TOKEN: secrets.GNUS_TOKEN_1` at job scope;
- third-party actions are version-pinned by tag, not commit SHA;
- container image uses `:latest`;
- container grants `IPC_LOCK`;
- workflow changes are ignored by the release workflow path filter;
- self-hosted runner cleanup performs broad destructive operations.

Hardening tasks:

- Add explicit workflow `permissions:` with least privilege.
- Move privileged tokens to the steps that need them.
- Replace long-lived `GNUS_TOKEN_1` with OIDC or scoped release/download tokens where feasible.
- Pin third-party actions by commit SHA.
- Pin container images by digest.
- Require review for `.github/workflows/**`.
- Ensure PR workflows cannot access production secrets.
- Add provenance/SBOM generation for release artifacts.
- Sign release archives and verify signatures in deployment.
- Separate build, staging, and production credentials.
- Add CI check for suspicious lifecycle scripts and unexpected workflow permission broadening.

## Phase 8: Runtime And Developer Controls

Runtime:

- run non-root;
- read-only root filesystem where practical;
- drop Linux capabilities;
- block privileged containers;
- restrict `ptrace` and debug tooling;
- monitor unexpected shared libraries, memory maps, shell execution, core dumps, and config drift;
- restart from clean images after suspected tampering.

Developer workstation and repository handling:

- add `SECURITY.md` guidance for cloning untrusted repos only in disposable containers/VMs;
- avoid running unknown install scripts on privileged workstations;
- require hardware-backed MFA for GitHub/cloud;
- avoid long-lived local cloud credentials;
- document token rotation and developer compromise response.

## Suggested Implementation Order

1. Add `RpcQuorumPolicy`, `QuorumResult`, and tests around normalized receipt/log/block comparisons.
2. Add `RpcQuorumClient` and wire it behind `IEthReceiptSource` without removing existing test seams.
3. Add `SecurityDecision` and require it for validator vote construction.
4. Add `ValidatorVotePolicy` and enforce bridge-specific thresholds, effective weight caps, minimum validator counts, and trust-domain diversity.
5. Add durable relay/bridge state, replay tests, and UTXO provenance tracking requirements.
6. Add EVM exit review/throttle rollout policy validation and alerts.
7. Add production config audit and startup validation.
8. Add structured security logs and alert hooks.
9. Harden CI workflows and add `SECURITY.md`.
10. Add deployment/runtime hardening manifests once production deployment target is known.

## Definition Of Done

- A single RPC provider cannot produce a production validator vote.
- RPC/provider DoS cannot collapse verification into single-provider trust.
- Validators reject vote requests without quorum evidence and policy approval.
- Chain id, block hash, emitter contract, tx hash, log index, peer, nonce, payload hash, and quorum policy are bound into the validator decision.
- Bridge mint consensus requires high effective voting weight, minimum distinct validators, and trust-domain diversity.
- Genesis or any one high-reputation validator cannot satisfy bridge mint consensus alone.
- Bridge-originated UTXO value preserves provenance through internal movement until EVM exit.
- UTXO-to-EVM exits are subject to review windows, throttles, pause, and recovery controls.
- Reorgs move affected messages to a safe state.
- Replays are durably rejected.
- Unsafe production configs fail startup validation.
- Threshold reductions require protected governance or are rejected.
- Logs reconstruct every relay decision.
- Alerts cover divergence, unsafe failover, validator/admin changes, config reductions, high-value bridge activity, exit review events, and runtime tampering indicators.
- Tests cover normal relay flow and adversarial failure modes.
