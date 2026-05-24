# EVM Relay Completion Plan

## Current Handoff Snapshot - 2026-05-20

Branch at handoff: `develop`

HEAD at handoff before the current checkpoint commit: `523e99c Document ENR tree discovery operations`

Current tracked working tree has live Sepolia diagnostics, queue behavior, tests, and documentation updates:

- `AgentDocs/CHECKPOINT.md`
- `AgentDocs/EVMRELAY_COMPLETION_PLAN.md`
- `examples/README.md`
- `examples/eth_watch/eth_watch.cpp`
- `include/eth/eth_peer_queue.hpp`
- `include/eth/eth_watch_service.hpp`
- `include/rlpx/rlpx_session.hpp`
- `src/eth/eth_peer_queue.cpp`
- `src/eth/eth_peer_session.cpp`
- `src/eth/eth_watch_service.cpp`
- `src/rlpx/rlpx_session.cpp`
- related focused tests under `test/`

Do not touch these unrelated local untracked artifacts unless explicitly requested:

- `AgentDocs/Refactor_chat.txt`
- `CRDT.Datastore.TEST.unit_2/`
- `CRDT.Datastore.TEST/`
- `examples/all.json`
- `examples/logs/`
- `examples/test_discovery.sh`
- `go-ethereum/`
- `rlp_enodes/`

Non-negotiable handoff constraints:

- Do not modify `rlp_enodes` except when explicitly requested; it is otherwise reference-only for this phase.
- Do not change gzip or JSON loading behavior. The hosted filename may be `chain_enodes.json.gz`, but the client receives JSON, so the `.gz` suffix should not drive loader refactors.
- ~~Do not refactor `examples/eth_watch` yet. It remains a functional test / CLI wrapper until production `EthWatchService` orchestration exists.~~ Production `EthWatchService` orchestration now owns cache/discovery-backed modes; direct manual modes remain example-local.
- Do not add bridge consensus/finality logic here. Relay responsibility is chain/message filtering, decoding, and callback dispatch. Bridge finality and UTXO messaging are handled outside this path through RPC verification.

Build/test commands verified for the current local changes:

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
ctest -R 'discv5_enr_test|discv5_enr_tree_test|eth_enr_tree_peer_cache_live_test|eth_watch_service_test|eth_watch_example_test|eth_watch_cli_test|eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
git diff --check
```

Verified result:

```text
100% tests passed, 0 tests failed out of 10
```

Current implementation state:

- `EthPeerQueue` is the shared queue for cached peers, discovery-produced peers, and eligible reconnect feedback.
- `DialScheduler` has a feedback hook for dial/session exits.
- `Peer Disconnect kTooManyPeers` now receives a long cool-off and is not requeued during normal live-test runtimes.
- ~~Connected peers that later fail with `kTcpError` or `kTimeout` are requeued.~~
- ~~Peers that never connected and failed with `kTcpError` / `kTimeout` are not requeued.~~
- ~~Requeue attempts are capped by `max_disconnect_requeues` so flaky peers stop cycling.~~
- ~~Bounded queue behavior, duplicate discovery suppression, and deterministic capacity drops are tested.~~
- ~~`EthWatchService` now exposes `initialize(config, callback)` and `run(io)`.~~
- ~~The production service runtime creates the shared watcher pool, per-chain `DialScheduler`, per-chain `EthPeerQueue`, cached-node preload, discovery-only bootnode storage, discv4 fallback for empty cached `nodes`, RLPx connect, ETH Status handshake, `EthWatchRunner` setup, watch registration, and decoded notification dispatch.~~
- ~~Focused service tests cover cached-node scheduler/queue creation, Gnosis-style empty `nodes` plus valid `bootnodes` fallback startup, production-path scheduler feedback requeue, and invalid config rejection.~~
- ~~`examples/eth_watch` cache-based `--chain` and `--all-chains` modes now build `EthWatchServiceConfig`, register decoded notification output callbacks, and call `EthWatchService::initialize(...)` / `run(io)`.~~
- Direct host/port/pubkey and `--direct-enode` manual modes remain on the example-local direct helper path.
- ~~`examples/eth_watch/eth_watch_example_test.cpp` is the compiled C++ replacement for the old shell smoke harnesses.~~
- ~~The tracked `examples/test_eth_watch.sh` and `examples/test_eth_watch_smoke.sh` shell test harnesses have been removed.~~
- ~~ENR-tree / EIP-1459 discovery is implemented for configured Ethereum and Polygon chains.~~
- ~~`examples/chains.json` was replaced with `examples/chains_config.json`, using canonical keys such as `ethereum-mainnet`, `ethereum-sepolia`, `polygon-mainnet`, and `polygon-amoy`.~~
- ~~`examples/chain_config.hpp` loads fork hashes from generated `chain_enodes.json(.gz)` only, and loads ENR-tree roots from `chains_config.json`.~~
- ~~`discv5::EnrTreeResolver` resolves `enrtree://` DNS discovery roots into ENR bootnodes and traverses production trees breadth-first.~~
- ~~The ENR parser handles production ENRs with list-valued fields such as `eth`, `snap`, and `wit`.~~
- ~~`EthWatchService` resolves configured/default ENR trees, keeps resolved ENRs discovery-only, starts discv5 discovery from them, and feeds discovered peers into the shared `EthPeerQueue`.~~
- ~~If ENR-tree resolution produces no usable ENRs, `EthWatchService` falls back to discv4 bootnodes when valid bootnodes exist.~~
- ~~`test/eth/eth_enr_tree_peer_cache_live_test.cpp` is an opt-in live functional test that starts with an empty `EthPeerQueue`, runs ENR-tree/discv5 discovery for five seconds, and reports accepted peers.~~
- ~~Live validation on 2026-05-19:~~
  - `polygon-mainnet`: 493 peers accepted into an empty queue over 5 seconds.
  - `ethereum-mainnet`: 862 peers accepted into an empty queue over 5 seconds.
- ~~Project headers under the touched project paths now use include guards instead of `#pragma once`.~~
- ~~`eth_watch` now reports final phase counters for transport failures, RLPx auth, local HELLO, peer HELLO, ETH Status sent/accepted/rejected, disconnect phase, TooManyPeers phase, ETH message counts, requeues, and backoff drops.~~
- ~~`--cache-peer-start-offset <N>` rotates the cached peer list before the existing band-spread behavior.~~
- ~~Live Sepolia validation confirmed `fork_hash=268956b6` and `fork_next=0` are accepted by real peers.~~
- ~~ENR-only `discover-first` validation confirmed `cached_peers=0`, `discovered_peers=11`, `remote_status_accepted=3`, and `transport_connect_failures=0` in a 180-second run.~~

Immediate next step:

Run the opt-in live all-chains functional test:

```bash
cd evmrelay
cmake --build build/OSX/Debug --target eth_watch_all_chains_live_test
env EVMRELAY_RUN_LIVE_ALL_CHAINS_TEST=1 \
    EVMRELAY_LIVE_ALL_CHAINS_JSON=/path/to/chain_enodes.json \
    ./build/OSX/Debug/test_bin/eth_watch_all_chains_live_test
```

The C++ functional test starts every configured chain concurrently through
`EthWatchService` in `kDiscoverFirst` mode, asserts cached peers are not used,
requires total discovered peers to meet or exceed the startup cached-peer count,
verifies each chain discovers peers, then requires at least 10 accepted ETH
Status handshakes and at least 5 ETH messages before the timed stop.

Suggested tests for the next session:

- ~~Example cache-first mode delegates scheduler/queue creation to `EthWatchService`.~~
- ~~Example empty `nodes` plus valid `bootnodes` mode delegates discv4 fallback to `EthWatchService`.~~
- Direct-enode mode remains available for local/manual smoke testing.
- CLI output callbacks receive decoded notifications with chain metadata from the service.
- ~~Discovery callback enqueues discovered peers into the same `EthPeerQueue` in an end-to-end service test.~~

## Context

The current `rlp_enodes` output schema requires each chain entry to contain both:

- `nodes`: scored, currently usable RLPx/ETH peer candidates for chain sessions.
- `bootnodes`: discovery seed nodes used only for discv4/discv5 peer discovery.

These fields must not be treated as aliases. `nodes` are the preferred input for `eth_watch` message/event subscriptions. `bootnodes` are discovery infrastructure and should not be assumed to speak ETH status or provide block/event data.

The downloaded `chain_enodes.json.gz` file is a pre-cache for both connection and discovery startup:

- use `nodes` as the first RLPx/ETH dial candidate set when a chain has usable pre-cached peers;
- use `bootnodes` as the first discovery seed set when discovery is needed;
- when a chain has no usable pre-cached `nodes` set, such as Gnosis Chain in the current workflow, start discv4 discovery from `bootnodes` and promote discovered peers into the dial queue after validation.

EIP-1459 / ENR tree support is implemented for chains that publish DNS discovery trees. Based on current chain support assumptions, configured Ethereum-family chains and Polygon-family chains default to EIP-1459 ENR-tree discovery, while other chains default to discv4 discovery unless their chain config explicitly provides a usable `enrtree://` source. ENR-tree support extends the discovery seed acquisition path without replacing the `chain_enodes.json.gz` pre-cache behavior.

The schema file in this checkout is `evmrelay/rlp_enodes/chain_enodes_schema.json`.

## Target Architecture

Keep the implementation modular and keep source roles separate:

- `rlp_enodes`: generates and validates chain metadata, scored peers, bootnodes, fork IDs, upcoming fork metadata, and signatures.
- `discv4::chain_peers`: loads signed chain peer cache data for RLPx/ETH connection candidates from `nodes`.
- `discv4::bootstrap_peers`: loads discovery seed data from `bootnodes`.
- Chain pre-cache: `chain_enodes.json.gz` is loaded before live discovery and supplies both pre-cached peer candidates (`nodes`) and discovery seeds (`bootnodes`).
- Discovery fallback: chains without usable pre-cached `nodes` use discv4 discovery from `bootnodes` and feed discovered peers into the dial queue.
- ENR tree / EIP-1459 support: resolves `enrtree://` sources into ENR bootnodes and feeds them only into discovery seed paths. It is the default discovery source for configured Ethereum and Polygon chains that publish DNS discovery trees; all other chains remain discv4-first by default.
- Discovery-to-dialer handoff: discovery producers emit validated peer candidates into a bounded peer queue; dialers consume from that queue independently under per-chain/global connection limits.
- `eth::EthWatchService` / relay runner: production owner for chain metadata intake, discovery-to-dialer queue orchestration, RLPx/ETH session setup, event watching, and decoded message/event callbacks.
- `examples/eth_watch/eth_watch.cpp`: thin CLI/example wrapper only; it should parse arguments, build configs, call production eth-watch APIs, and avoid owning scheduler/discovery orchestration.
- Discovery clients: optionally expand peer sets from `bootnodes`, but promote discovered candidates into peer candidates only after validation.

## Non-Negotiable Semantics

- `nodes` means RLPx/ETH peer candidates.
- `bootnodes` means discovery seeds.
- `chain_enodes.json.gz` is a pre-cache, not a hard requirement that every chain has pre-cached `nodes`.
- Missing or empty `nodes` is not fatal when `bootnodes` can seed discovery. Gnosis Chain is the expected example: use discv4 discovery and enqueue validated discovered peers.
- `enrtree://` entries and EIP-1459 DNS-discovery records are later-stage discovery sources, not direct ETH session peers.
- Discovery defaults are chain-specific: Ethereum and Polygon chains prefer EIP-1459 when configured; non-Ethereum/Polygon chains prefer discv4 unless explicit ENR-tree support is present and validated.
- Direct ETH session code must not fall back to `bootnodes` when `nodes` is empty unless the mode is explicitly "discover first".
- Discovery and RLPx/ETH connections must remain decoupled. Discovery should keep running and enqueue peers as they are found; the dialer should pull/drain from that queue as connection slots become available.
- Discovery callbacks must not perform blocking RLPx work directly. They may validate, deduplicate, filter, and enqueue.
- Example binaries must not own core relay orchestration. If orchestration logic is needed by the relay, it belongs under `include/eth` and `src/eth`, with `EthWatchService` as the primary production integration point unless a smaller helper class is warranted.
- Schema validation must fail or warn clearly when a chain entry lacks required `nodes` or `bootnodes`.
- Docs, tests, and function names should use "chain peers" for `nodes` and "bootnodes/bootstrap peers" for `bootnodes`.

## Tasks

### 1. Schema and Generator Alignment

- Confirm whether the canonical schema filename should be `chain_enodes_schema.json` or `chain_enodes.schema.json`; rename or update references so only one name is used.
- Update `rlp_enodes/README.md` examples so every chain output includes both `nodes` and `bootnodes`.
- Add generator tests that assert combined and per-chain output contain separate `nodes` and `bootnodes` arrays.
- Add generator tests that allow chains to have empty or missing usable pre-cached `nodes` as long as they have valid `bootnodes` for discovery fallback.
- Generator support/tests for `bootnodes_enrtree` sources should keep EIP-1459 trees resolving into the `bootnodes` array without polluting `nodes`.
- Record source metadata for bootnodes where practical so operators can trace whether a bootnode came from static config, Go/YAML source, or later EIP-1459 DNS discovery.
- Add negative tests for missing `nodes`, missing `bootnodes`, malformed `enr`, malformed `pubkey`, invalid `score`, invalid `ip`, and invalid `port`.
- Keep signature generation over the final combined document with `signature` and `signerAddress` excluded from the signed payload.

### 2. C++ Loader Separation

- ~~Extend `ChainPeerConfig` or add a sibling structure so chain peer candidates and bootnode candidates are represented separately.~~
- ~~Keep `load_chain_peers_from_json_text(...)` parsing only `nodes`.~~
- ~~Update `bootstrap_peers` so `load_bootstrap_peers_from_json_text(...)` and `load_bootstrap_peers_from_json(...)` parse only `bootnodes`, not `nodes`.~~
- ~~Add parsing/validation tests for ENR entries originating from EIP-1459 trees, including invalid ENR records and trees that resolve to zero usable bootnodes.~~
- Add tests proving:
  - ~~chain peer loading ignores `bootnodes`;~~
  - ~~bootstrap loading ignores `nodes`;~~
  - ~~chain metadata still loads when both arrays exist;~~
  - ~~missing required arrays are handled deterministically.~~
  - ~~chain metadata can load with empty `nodes` and valid `bootnodes` for discovery fallback.~~

### 3. Relay Peer Selection Flow

- In `eth_watch` and relay startup code, load `chain_enodes.json.gz` as a pre-cache before live discovery.
- ~~For each chain, enqueue pre-cached `nodes` into the dial queue when present.~~
- ~~For chains with no usable pre-cached `nodes`, start discv4 discovery from pre-cached `bootnodes`, validate discovered peers, and enqueue them into the same dial queue.~~
- ~~Allow discovery to continue in parallel even when pre-cached `nodes` exist, but keep the behavior explicit so operators can choose cache-only, discover-first, or hybrid mode.~~
- ~~Add an explicit ENR-tree discovery source path for EIP-1459 after the discv4 fallback path is complete:~~
  - ~~resolve `enrtree://` / DNS discovery records into ENR bootnodes;~~
  - ~~seed discv5 discovery from the resolved bootnodes;~~
  - ~~validate discovered peers before promoting them into RLPx/ETH candidates.~~
- ~~Add chain discovery strategy selection:~~
  - ~~Ethereum mainnet/testnets and Polygon mainnet/testnets default to EIP-1459 ENR-tree discovery once sources are configured;~~
  - ~~all other chains default to discv4 from `bootnodes`;~~
  - ~~invalid or missing ENR-tree data falls back to discv4 when valid `bootnodes` exist.~~
- ~~Preserve the producer/consumer split that is partially present today:~~
  - ~~`discv4_client::set_peer_discovered_callback(...)` and `discv5_client::set_peer_discovered_callback(...)` are discovery producer hooks;~~
  - ~~`DialScheduler::enqueue(...)` is the dialer handoff point;~~
  - ~~`DialScheduler` owns active dial counts, retry suppression, queue draining, and session slot recycling.~~
- ~~Wire `eth_watch` discover-first mode so discv4 callbacks enqueue peers into `DialScheduler` while discovery continues in parallel.~~
- ~~Add a small adapter layer if needed to convert discv4/discv5 peer records into one dialer peer type; keep protocol-specific discovery details out of the connection code.~~
- ~~Add backpressure policy for the peer queue: bounded size, duplicate suppression, recent-dial suppression, and deterministic drop behavior when discovery outpaces dialing.~~
- ~~Move reusable connection-pool, dial-queue, and discovery callback wiring out of `examples/eth_watch/eth_watch.cpp` into `evmrelay/src/eth/eth_watch_service.cpp` or a focused `src/eth` helper owned by `EthWatchService`.~~
- ~~Keep `examples/eth_watch/eth_watch.cpp` as a thin consumer of the production service API for cache-based modes: argument parsing, config construction, watch registration, and output formatting only. Direct-enode/manual mode remains as a local test helper for now.~~
- ~~Do not silently mix bootnodes into direct ETH session candidates.~~
- ~~Preserve fork ID, network ID, and genesis hash from chain metadata through direct peer and discovered peer paths.~~
- ~~Add Gnosis Chain coverage for the no-pre-cached-nodes path using real loaded chain metadata; fallback startup and service-level discovery-to-dial-queue handoff are covered.~~

### 4. Discovery/Dialer Decoupling Checks

Existing code already has several useful pieces:

- `discv4::DialScheduler` keeps a `queue`, active counters, dial history, and drains queued peers as slots are released.
- `discv4_client` emits discovered peers through `set_peer_discovered_callback(...)`.
- `discv5_client` delegates peer lifecycle to `discv5_crawler` and emits discovered peers through the same callback shape.
- Discovery examples already enqueue discovered peers into `DialScheduler`.

Remaining work:

- ~~Move the example-only callback-to-scheduler wiring into reusable `EthWatchService` production orchestration under `include/eth` and `src/eth`.~~
- ~~Remove scheduler ownership and discovery lifecycle ownership from `examples/eth_watch/eth_watch.cpp` once the production API exists.~~
- ~~Ensure cached `nodes` and discv4 discovery feed the same dial queue abstraction.~~
- ~~Ensure discv5 discovery and EIP-1459-derived seeds feed the same dial queue abstraction.~~
- ~~Ensure the pre-cache path and discovery fallback path feed the same queue abstraction: `nodes` enqueue immediately, `bootnodes` only seed discovery, discovered peers enqueue after validation.~~
- ~~Wire dial/session disconnect feedback into the peer queue so eligible peers are requeued and flaky peers stop cycling.~~
- ~~Keep discovery loops alive while the dial queue drains; do not structure discovery as a blocking pre-scan before dialing.~~
- ~~Add tests that simulate discovery producing peers while dial slots are saturated and verify queued peers are dialed as slots release.~~
- ~~Add tests that discovery can continue producing peers after successful and failed RLPx sessions without coupling lifecycle ownership.~~

### 5. Completion Criteria for EVM Relay

- ~~Event watching works from cached `nodes` for configured chains.~~
- ~~Direct `--direct-enode` remains available for local and manual testing.~~
- ~~`chain_enodes.json.gz` is used as the startup pre-cache for both peer dialing (`nodes`) and discovery seeding (`bootnodes`).~~
- ~~Chains without pre-cached `nodes`, including Gnosis Chain, can still start from `bootnodes`, run discv4 discovery, and enqueue validated discovered peers.~~
- ~~Optional discover-first/hybrid mode uses `bootnodes` only for discovery.~~
- ~~Optional ENR-tree mode supports EIP-1459 DNS discovery, keeps resolved ENRs in the discovery-only path, and becomes the preferred default for configured Ethereum and Polygon chains.~~
- ~~Discovery and connection dialing are decoupled: discovery producers enqueue peers continuously, and dialers consume queued peers according to connection limits.~~
- ~~Core discovery/dial/session orchestration lives in `src/eth` production code, not in `examples/eth_watch/eth_watch.cpp`.~~
- ~~The relay can watch configured chain/message filters, decode matching logs/messages, and invoke callbacks. Bridge-message finality verification and UTXO-system messaging are handled outside this relay path through RPC-based verification.~~
- ~~Unit tests cover loader semantics, peer selection, fork metadata, signature validation, event filtering, ABI decoding, receipt sources, and runner behavior.~~
- ~~Smoke tests document exact known-good commands for local direct-enode and cached peer cache flows.~~

### 6. Documentation Cleanup

- Update `BOOTNODES_CONFIGURATION.md`, `WHY_NO_MESSAGES.md`, and quick test docs to consistently state:
  - bootnodes are discovery-only;
  - `chain_enodes.json.nodes` is the RLPx/ETH peer candidate list;
  - `chain_enodes.json.bootnodes` is the discovery seed list.
- Remove or rewrite sections that imply cached peers and bootnodes are interchangeable.
- Update command examples to use `--chain-peers-json` for peer cache input and a separate explicit option for discovery seed input if/when that option exists.
- Document `chain_enodes.json.gz` as the pre-cache for startup peer candidates and discovery seeds.
- Document the Gnosis/no-pre-cached-nodes path: load chain metadata and bootnodes, run discv4 discovery, then enqueue discovered peers.
- Document EIP-1459 / ENR tree usage separately, including example `enrtree://` source configuration, the expected discover-first flow, and the default strategy split: Ethereum/Polygon prefer EIP-1459, other chains prefer discv4.
- Document the discovery-to-dialer pipeline: discovery seeds produce discovered peer candidates, peer candidates enter a bounded dial queue, and RLPx/ETH sessions consume from that queue.
- Document `examples/eth_watch/eth_watch.cpp` as an example CLI over the production `EthWatchService` APIs, not as the implementation home for relay behavior.

## Suggested Implementation Order

1. Lock schema filename and documentation examples.
2. ~~Add failing C++ tests for `nodes` vs `bootnodes` separation.~~
3. ~~Update `bootstrap_peers` to parse `bootnodes`.~~
4. ~~Treat `chain_enodes.json.gz` as the startup pre-cache: load `nodes` for immediate dialing and `bootnodes` for discovery seeding.~~
5. ~~Add or formalize the discovery-to-dialer queue adapter and backpressure policy under `include/eth` / `src/eth`.~~
6. ~~Implement discv4 fallback for chains without usable pre-cached `nodes`, with Gnosis Chain as the first target.~~
7. ~~Move reusable scheduler/discovery orchestration out of `examples/eth_watch/eth_watch.cpp` and into `EthWatchService` production code.~~
8. ~~Add chain config support for both arrays without changing direct peer behavior.~~
9. ~~Update `eth_watch` peer selection so direct mode, cache-first mode, bootnode discovery fallback, and hybrid mode are explicit through production service APIs. Cache-first and bootnode fallback now use production service APIs; direct mode remains an example-local manual helper.~~
10. ~~Add EIP-1459 / ENR tree source support and tests for discovery seed generation for chains that support it, with Ethereum/Polygon defaulting to ENR-tree discovery and other chains defaulting to discv4.~~
11. ~~Run focused discv4, discv5, eth, and eth_watch tests.~~
12. ~~Refresh docs and smoke-test commands.~~
