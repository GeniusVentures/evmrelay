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

## Final Integration Target - 2026-05-24

The relay peer/discovery path is close to complete, but it is not yet in its final SuperGenius integration state. The remaining production work is to make `evmrelay` the EVM observation and RPC verification substrate used by the parent project, replacing the current `src/watcher/impl/evm_messaging_watcher.*` WebSocket-only implementation with a thin adapter over:

- `eth::EthWatchService` for P2P observation and decoded bridge/log callbacks;
- `eth::rpc::RpcManager` / receipt sources for JSON-RPC receipt, log, block, and chain-id verification;
- security decision and validator vote policy objects described in `EVMRELAY_SECURITY_HARDENING_PLAN.md`;
- chain/RPC endpoint metadata loaded through `rpc_manager`, including ChainList / ethereum-lists public endpoint ingestion.

`evmrelay` should not directly mint, burn, or satisfy bridge consensus. Its production responsibility is to produce normalized observations and RPC-backed verification evidence that the SuperGenius validator/consensus layer can use before minting UTXO value or authorizing EVM exits.

The final parent-project bridge flow should be:

```text
EthWatchService observes candidate bridge/burn log
  -> event filter and ABI decoder normalize event evidence
  -> RpcManager verifies receipt/log/block/finality through independent RPC endpoints
  -> SecurityDecision records quorum, chain id, block hash, log index, payload hash, nonce, and finality
  -> src/watcher/impl adapter forwards only verified decisions to parent watcher/validator code
  -> SuperGenius bridge consensus applies validator count, trust-domain, weight-cap, replay, and provenance policy
  -> mint or exit path proceeds only after consensus policy passes
```

The current `src/watcher/impl/evm_messaging_watcher.*` shape should be treated as a compatibility wrapper, not the production EVM verifier. It currently accepts one configured WebSocket endpoint, builds `eth_subscribe` JSON by string concatenation, and forwards raw subscription messages. The final implementation needs to stop treating a single websocket notification as bridge evidence.

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
- `rlp::base::json`: shared schema-driven JSON object and array parser. New JSON object loading should declare `JsonSchemaObject` / `JsonSchemaArray` metadata and consume typed `JsonParsedObject` values instead of feature-local `if_contains` parsing branches.
- `eth::rpc_manager_config`: configuration-only RPC endpoint loader. It validates endpoint config through the base JSON schema parser and should remain separate from runtime RPC orchestration.
- `eth::rpc::RpcManager`: runtime owner for chain-scoped RPC endpoint pools, endpoint health, request dispatch, and receipt source construction. It should be consumed by verifier/adapters, not embedded in `EthWatchService` startup orchestration.
- `eth::ChainlistProvider` or equivalent metadata loader: downloads and normalizes ChainList / ethereum-lists chain metadata into `RpcEndpointConfig` candidates that `rpc_manager` can load, validate, and health-check.
- `src/watcher/impl/evm_messaging_watcher.*`: parent-project adapter. It should translate existing `watcher::MessagingWatcher` configuration/callbacks into `evmrelay` service configuration and forward verified bridge decisions or normalized verified messages, not raw single-provider websocket payloads.
- SuperGenius validator/consensus code: only owner allowed to decide bridge mint consensus, apply bridge voting thresholds, cap effective genesis/high-reputation weight, preserve UTXO bridge provenance, and authorize mint/exit actions.

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
- JSON object parsing should stay schema-driven. Feature modules may do domain conversion after schema validation, but field presence/type/default handling belongs in `base/json_utility`.
- Docs, tests, and function names should use "chain peers" for `nodes` and "bootnodes/bootstrap peers" for `bootnodes`.
- Public RPC endpoint discovery is configuration input, not a trust decision. ChainList / ethereum-lists endpoints must be filtered, normalized, deduplicated, optionally probed with `eth_chainId`, and then loaded into `rpc_manager`; a public endpoint cannot by itself satisfy bridge verification.
- A websocket subscription notification, a P2P observation, or one JSON-RPC receipt response is never sufficient mint evidence. Parent watcher callbacks used for bridge minting must receive a verified decision or an explicitly degraded/unverified diagnostic object.
- `EthWatchService` may observe candidate bridge/burn events, but finality, replay, bridge/burn value validation, and mint consensus must be enforced through RPC verification plus SuperGenius validator consensus.

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

- ~~Update `BOOTNODES_CONFIGURATION.md`, `WHY_NO_MESSAGES.md`, and quick test docs to consistently state:~~
  - ~~bootnodes are discovery-only;~~
  - ~~`chain_enodes.json.nodes` is the RLPx/ETH peer candidate list;~~
  - ~~`chain_enodes.json.bootnodes` is the discovery seed list.~~
- ~~Remove or rewrite sections that imply cached peers and bootnodes are interchangeable.~~
- ~~Update command examples to use `--chain-peers-json` for peer cache input and a separate explicit option for discovery seed input if/when that option exists.~~
- ~~Document `chain_enodes.json.gz` as the pre-cache for startup peer candidates and discovery seeds.~~
- ~~Document the Gnosis/no-pre-cached-nodes path: load chain metadata and bootnodes, run discv4 discovery, then enqueue discovered peers.~~
- ~~Document EIP-1459 / ENR tree usage separately, including example `enrtree://` source configuration, the expected discover-first flow, and the default strategy split: Ethereum/Polygon prefer EIP-1459, other chains prefer discv4.~~
- ~~Document the discovery-to-dialer pipeline: discovery seeds produce discovered peer candidates, peer candidates enter a bounded dial queue, and RLPx/ETH sessions consume from that queue.~~
- ~~Document `examples/eth_watch/eth_watch.cpp` as an example CLI over the production `EthWatchService` APIs, not as the implementation home for relay behavior.~~

### 7. ChainList / Ethereum-Lists RPC Metadata Ingestion

Goal: create a normalized list of EVM-compatible chains and public RPC nodes that can be loaded into `rpc_manager`. CSV generation is out of scope for this plan.

Primary source:

- `https://chainid.network/chains.json`

Fallback source:

- `https://github.com/ethereum-lists/chains`
- repository data files under `_data/chains/*.json`

Input format: `chains.json` is a JSON array of chain objects with fields such as `name`, optional `title`, `chain`, `chainId`, `networkId`, `shortName`, `rpc`, `faucets`, `nativeCurrency`, `explorers`, `features`, optional `status`, and optional `redFlags`.

Implement:

- Add a `ChainlistProvider` or similarly named loader under `include/eth` / `src/eth`, preferably in the RPC manager area, that consumes JSON text and produces normalized chain endpoint metadata.
- Keep network download mechanics separate from parsing so unit tests can use local JSON fixtures and the downloader can fall back cleanly.
- Parse each chain object for:
  - `name`;
  - `title`, when present;
  - `chainId`;
  - `networkId`;
  - `shortName`;
  - `nativeCurrency.symbol`, when present;
  - `status`, defaulting to `active` when missing;
  - `rpc[]`;
  - `explorers[]`;
  - `features[]`, when present;
  - `redFlags[]`, when present.
- Filter RPC URLs:
  - keep only `http://` and `https://` by default;
  - optionally keep `wss://` endpoints in a separate `websocketEndpoints` list when `includeWebsocket` is enabled;
  - drop URLs containing API-key placeholders including `${INFURA_API_KEY}`, `${ALCHEMY_API_KEY}`, `${ANKR_API_KEY}`, `${POKT_API_KEY}`, and `${BLASTAPI_API_KEY}`;
  - drop malformed URLs;
  - deduplicate URLs per `chainId`;
  - preserve enough diagnostics to explain filtered endpoints without logging secrets.
- Normalize to one chain object containing:

```json
{
  "chainId": 1,
  "networkId": 1,
  "name": "Ethereum Mainnet",
  "shortName": "eth",
  "currencySymbol": "ETH",
  "status": "active",
  "rpcEndpoints": ["https://..."],
  "websocketEndpoints": ["wss://..."],
  "explorers": [
    {
      "name": "etherscan",
      "url": "https://etherscan.io",
      "standard": "EIP3091"
    }
  ]
}
```

Recommended filters:

- `includeTestnets`: include or exclude known testnets based on chain metadata/config policy.
- `includeDeprecated`: default `false`; skip `status=deprecated` and chains with equivalent red flags unless explicitly requested.
- `includeIncubating`: optional.
- `requireWorkingRpc`: when enabled, probe endpoints before enabling them.
- `includeWebsocket`: default `false` for RPC manager config; websocket endpoints remain separate.

Validation:

- Deduplicate by `chainId + RPC URL`.
- Optionally POST `eth_chainId` to each HTTP(S) endpoint:

```json
{
  "jsonrpc": "2.0",
  "method": "eth_chainId",
  "params": [],
  "id": 1
}
```

- Convert returned hex chain id to integer and keep the endpoint as available only when it equals the chain object's `chainId`.
- Mark failed endpoints as unavailable with diagnostics when `requireWorkingRpc=false`; drop them from active config when `requireWorkingRpc=true`.
- If `chainid.network` is unavailable, fetch or use a local checkout/archive of `ethereum-lists/chains`.
- If aggregated JSON fails, parse each `_data/chains/*.json` file independently.
- Log parse errors per chain file and continue processing other chains.

Tests:

- parses aggregated `chains.json` fixture;
- parses fallback per-chain file fixtures;
- defaults missing `status` to `active`;
- extracts `nativeCurrency.symbol`;
- filters placeholder API-key URLs;
- separates HTTP(S) RPC and `wss://` endpoints;
- rejects malformed URLs;
- deduplicates repeated endpoints;
- preserves explorer metadata;
- handles deprecated/incubating/testnet filters;
- validates `eth_chainId` success, wrong-chain response, invalid hex response, timeout, and provider error through a mock transport;
- produces `RpcEndpointConfig` candidates without checking any API key into fixtures.

### 8. RPC Manager Finalization For Bridge Verification

The `RPC_MANAGER_HANDOFF.md` foundation should be completed before parent watcher integration relies on it.

Implement:

- Finish endpoint health state in `RpcEndpointPool`: available, temporarily failed, disabled, with retry/backoff metadata.
- Add `RpcReceiptSourceFactory` or an equivalent adapter that creates chain-scoped receipt sources from `RpcManager`.
- Add a `RpcReceiptVerifier` or `RpcObservedMessageVerifier` that accepts an observed bridge event/log and verifies it through multiple configured RPC endpoints.
- Apply RPC checks to:
  - `eth_chainId`;
  - finality head / confirmation depth;
  - `eth_getTransactionReceipt`;
  - exact `blockHash` re-check;
  - log index, emitter address, topic0, topics, data, tx hash, block number, block hash, and status;
  - bridge/burn amount/value, source sender, destination recipient, nonce/message id, and configured source/destination domain fields after ABI decoding.
- Return a `SecurityDecision`-compatible result rather than a bare boolean.
- Fail closed when quorum is unavailable, trust domains are insufficient, endpoints disagree, the chain id is wrong, finality is stale, or required receipt/log fields are missing.

Tests:

- endpoint failure marks only that endpoint unhealthy;
- chain with no usable endpoint fails closed;
- wrong chain id fails closed;
- providers disagree on block hash;
- providers disagree on receipt status;
- providers disagree on event log contents;
- external-only or internal-only provider availability fails in production policy when diversity is required;
- bridge/burn event with mismatched value, recipient, nonce, contract, topic, or chain domain is rejected.

### 9. Parent Project Watcher Integration

Goal: move the parent EVM watcher from single-endpoint websocket forwarding to `evmrelay` observation plus RPC-backed verification.

Current parent files to update:

- `src/watcher/impl/evm_messaging_watcher.hpp`
- `src/watcher/impl/evm_messaging_watcher.cpp`
- `src/watcher/impl/CMakeLists.txt`

Implement:

- Replace raw `ws_url`/`eth_subscribe` ownership with an adapter that builds `eth::EthWatchServiceConfig` and configured watch filters from the existing parent watcher config.
- Keep parent-facing constructor and `watcher::MessagingWatcher` callback compatibility where practical, but change callback payload semantics for bridge paths from raw websocket JSON to normalized verified observation / security decision JSON.
- Accept chain configuration that references:
  - chain id;
  - bridge contract address;
  - event topics / ABI event signature;
  - source/destination domain ids;
  - confirmation/finality policy;
  - RPC manager endpoint config or chainlist-derived endpoint set;
  - relay peer cache / discovery config.
- Start and stop `EthWatchService` from the parent watcher lifecycle without blocking parent shutdown.
- Route decoded candidate events into the RPC verifier before invoking bridge-mint callbacks.
- Preserve a diagnostic mode for raw observations if needed, but make it explicitly non-mintable and unavailable as a production bridge evidence path.
- Remove ad hoc JSON subscription string building from production bridge paths.

Parent integration tests:

- parent watcher config builds a valid `EthWatchServiceConfig`;
- parent watcher rejects missing chain id, contract address, topics, RPC policy, or finality policy in production mode;
- observed event is not forwarded to mint path until RPC verification succeeds;
- RPC verifier failure emits a diagnostic/rejected decision and does not call mint callback;
- stopWatching shuts down service and RPC work cleanly;
- websocket/raw mode, if retained, is diagnostic-only and marked non-consensus.

### 10. Bridge Mint Consensus Integration Criteria

This plan should finish with a production bridge path that cannot mint from one source of truth.

Completion criteria:

- `EthWatchService` observes candidate bridge/burn events and decodes them into structured evidence.
- `RpcManager` verifies each candidate against independent RPC endpoints before parent callbacks can produce validator vote evidence.
- `SecurityDecision` or equivalent evidence object contains chain id, bridge contract, tx hash, log index, block number/hash, topic/data hash, decoded bridge/burn value, sender, recipient, nonce/message id, finality depth, quorum policy id, provider vote summaries, and degraded state.
- Parent watcher code forwards only verified decisions into the validator vote path for bridge mints.
- SuperGenius validator policy enforces minimum distinct validators, minimum trust-domain/operator diversity, effective bridge weight threshold, per-validator bridge-weight cap, replay protection, nonce/message-id policy, and provenance tagging for bridge-originated UTXO value.
- Genesis or any one high-reputation validator cannot satisfy bridge mint consensus alone.
- Any failed RPC quorum, event mismatch, stale finality, wrong chain id, provider divergence, unknown contract/topic, replay, nonce gap, or production policy ambiguity fails closed.
- Tests cover the end-to-end path: observed bridge/burn log -> RPC verification -> security decision -> parent watcher callback -> validator vote policy accept/reject.

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
13. Add ChainList / ethereum-lists parser fixtures and normalize chain metadata plus HTTP(S)/WSS endpoint lists.
14. Add ChainList downloader/fallback path: `chainid.network/chains.json` first, then ethereum-lists `_data/chains/*.json` parsing.
15. Convert normalized ChainList RPC endpoints into `RpcEndpointConfig` candidates loaded by `rpc_manager`.
16. Add optional endpoint probing with `eth_chainId`, wrong-chain rejection, endpoint diagnostics, and availability marking.
17. Finish `RpcEndpointPool` health/backoff and `RpcReceiptSourceFactory` from `RpcManager`.
18. Implement RPC verification for observed bridge/burn events, including receipt/log/block/finality checks and decoded value/domain checks.
19. Add `SecurityDecision` or equivalent verified evidence object and make verifier output the only bridge-mintable watcher payload.
20. Replace `src/watcher/impl/evm_messaging_watcher.*` websocket-only behavior with a parent-project adapter over `EthWatchService` plus RPC verification.
21. Wire verified watcher output into SuperGenius validator vote policy, preserving a diagnostic-only raw observation mode if needed.
22. Add end-to-end tests for observed bridge/burn event -> RPC quorum verification -> security decision -> parent watcher callback -> validator vote policy.
23. Run parent and evmrelay focused tests, then document production config requirements for peer cache, ChainList/RPC endpoints, finality policy, quorum policy, and bridge consensus policy.
