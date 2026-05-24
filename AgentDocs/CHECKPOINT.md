# Checkpoint Log

## Schema-driven JSON parsing and RPC manager config foundation - 2026-05-24

### Current State

- Repository: `evmrelay`
- Branch: `develop`
- This checkpoint keeps JSON schemas in C++ for now, but all normal JSON object item loading added or touched in this pass goes through `rlp::base::json::JsonSchemaObject` / `JsonSchemaArray`.

### What Changed

- Added reusable schema-driven JSON parsing in `base`:
  - `JsonSchemaField`
  - `JsonSchemaObject`
  - `JsonSchemaArray`
  - `JsonFieldType`
  - `JsonParsedObject`
  - `JsonParsedArray`
  - typed parsed-value accessors.
- Added nested object/array traversal, required/optional/default handling, typed primitive conversion, and `JsonError` paths such as `endpoints[0].priority`.
- Added `eth::rpc::RpcManagerConfig` / `RpcEndpointConfig` loading from JSON text or file.
- Registered `rpc_manager_config_test`.
- Reworked JSON-RPC response parsing to schema-validate response envelopes, block-number result objects, receipt objects, log objects, and topic/log arrays.
- Reworked chain peer cache object readers to use schema parsing for:
  - ENR-tree/source fields;
  - ETH message schemas and schema-set filters;
  - chain peer node records;
  - signed-cache signature fields;
  - chain config required fields.

### Remaining Intentional Direct Boost.JSON Use

Some direct Boost.JSON access remains where the JSON shape is intentionally dynamic or not ordinary object item loading:

- selecting a chain entry by runtime chain name;
- selecting an `_ethMessageSchemaSets` entry by runtime schema-set name;
- preserving the unsigned JSON object for signature verification;
- `forkNext`, which currently accepts either a JSON integer or a hex string.

### Verification

Commands run from the SuperGenius repository root:

```bash
cmake --build evmrelay/build/OSX/Debug --target rpc_manager_config_test json_rpc_test
cmake --build evmrelay/build/OSX/Debug --target discv4_chain_peers_test rpc_receipt_source_test

./evmrelay/build/OSX/Debug/test_bin/rpc_manager_config_test
./evmrelay/build/OSX/Debug/test_bin/json_rpc_test
./evmrelay/build/OSX/Debug/test_bin/rpc_receipt_source_test
./evmrelay/build/OSX/Debug/test_bin/discv4_chain_peers_test
```

Results:

- `rpc_manager_config_test`: passed 6/6.
- `json_rpc_test`: passed 6/6.
- `rpc_receipt_source_test`: passed 10/10.
- `discv4_chain_peers_test`: local cases passed 23/24; the only failure was the live URL download test because no JSON was downloaded in the restricted environment.

### Next Work

Continue from `AgentDocs/RPC_MANAGER_HANDOFF.md`.

## All-chain live C++ functional test and RPC manager handoff - 2026-05-24

### Current State

- Repository: `evmrelay`
- Branch: `develop`
- Previous HEAD before this checkpoint commit: `5aeeff5 Add discover-only discovery mode`
- User correction in this step:
  - Do not add shell harnesses for evmrelay functional coverage.
  - Live/functional relay validation should be compiled C++ and registered through CMake/CTest.

### What Changed

- Added `test/eth/eth_watch_all_chains_live_test.cpp`.
  - Opt-in with `EVMRELAY_RUN_LIVE_ALL_CHAINS_TEST=1`.
  - Drives `eth::EthWatchService` directly instead of shelling out to `eth_watch`.
  - Runs all configured chains concurrently in `EthWatchDiscoveryMode::kDiscoverFirst`.
  - Sets both discv4 and discv5 bind ports to `0` so per-chain discovery clients can run concurrently.
  - Loads the same `chains_config.json` path used by the built `eth_watch` example by default.
  - Uses `EVMRELAY_LIVE_ALL_CHAINS_JSON` when a local `chain_enodes.json` path should be forced.
  - Keeps final `cached_peers=0`, proving cached `nodes` were not used as dial candidates.
  - Requires total discovered peers to meet or exceed the startup cached peer count unless overridden by `EVMRELAY_LIVE_ALL_CHAINS_MIN_DISCOVERED_TOTAL`.
  - Requires every selected chain to discover at least one peer unless overridden by `EVMRELAY_LIVE_ALL_CHAINS_MIN_DISCOVERED_PER_CHAIN`.
  - Requires at least 10 accepted ETH Status handshakes and at least 5 ETH messages by default.
- Registered `eth_watch_all_chains_live_test` in `test/eth/CMakeLists.txt`.
- Updated:
  - `AgentDocs/EVMRELAY_COMPLETION_PLAN.md`
  - `AgentDocs/QUICK_TEST_GUIDE.md`
- Removed the mistaken shell harness approach from this work.

### Live Verification

Commands run from `evmrelay/build/OSX/Debug`:

```bash
cmake --build . --target eth_watch_all_chains_live_test

./test_bin/eth_watch_all_chains_live_test

env EVMRELAY_RUN_LIVE_ALL_CHAINS_TEST=1 \
  EVMRELAY_LIVE_ALL_CHAINS_JSON=/Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/rlp_enodes/output/chain_enodes.json \
  ./test_bin/eth_watch_all_chains_live_test
```

Observed passing live C++ sample:

```text
startup_cached_peers=723
final_cached_peers=0
discovered_peers=2904
remote_status_accepted=17
eth_messages=938

ethereum-mainnet discovered_peers=200
ethereum-sepolia discovered_peers=125
ethereum-holesky discovered_peers=8
ethereum-hoodi discovered_peers=137
polygon-mainnet discovered_peers=372
polygon-amoy discovered_peers=371
bnb-smart-chain discovered_peers=550
bnb-smart-chain-testnet discovered_peers=436
base-mainnet discovered_peers=68
base-sepolia discovered_peers=52
gnosis-chain discovered_peers=585
```

Static check:

```bash
git diff --check -- AgentDocs/EVMRELAY_COMPLETION_PLAN.md AgentDocs/QUICK_TEST_GUIDE.md examples/eth_watch/CMakeLists.txt test/eth/CMakeLists.txt test/eth/eth_watch_all_chains_live_test.cpp
```

### Teardown Note

- The live test intentionally snapshots counters and leaves the live service/io context to process exit.
- Calling `EthWatchService::stop()` during this GTest live process surfaced `boost::coroutines::detail::forced_unwind` from active Boost coroutine stacks.
- This is scoped to the opt-in live functional test and avoids changing production teardown behavior.

### Next Work: RPC Manager Sub-library

The RPC manager runtime slice is now in place:

- runtime endpoint resolution exists in `RpcManager`;
- endpoint grouping and deterministic selection are implemented;
- the Boost.Beast/Asio HTTP transport supports both HTTP and HTTPS;
- transport and manager tests are passing locally.

The next requested feature is the bridge from the runtime manager to receipt verification.

Suggested shape:

- Add an RPC manager module separate from `EthWatchService` orchestration.
- Config should support:
  - optional paid RPC endpoints;
  - API keys by env var or explicit local config;
  - chain/canonical-name mapping;
  - endpoint priority/weight/rate-limit hints;
  - a configurable number `X` of ChainList verified public RPC endpoints per chain.
- ChainList integration should:
  - load from a configured file or URL;
  - filter for verified endpoints;
  - reject unsupported schemes and unresolved API-key placeholders unless config supplies credentials;
  - produce deterministic endpoint selection for tests.
- Runtime classes should likely be split:
  - `RpcEndpointConfig`
  - `ChainlistProvider`
  - `RpcEndpointPool`
  - `RpcManager`
  - `RpcObservedMessageVerifier` / `BloomMessageVerifier`
- Reuse existing JSON-RPC and receipt-source code where possible:
  - `include/eth/json_rpc.hpp`
  - `src/eth/json_rpc.cpp`
  - `include/eth/rpc_receipt_source.hpp`
  - `src/eth/rpc_receipt_source.cpp`
  - `include/eth/eth_receipt_source.hpp`
  - `src/eth/eth_receipt_source.cpp`
- The verifier should use block/receipt bloom checks to narrow candidates, then fetch exact receipts/logs through RPC and verify the observed RLPx message/log exactly.
- Keep bridge finality/quorum policy separate unless explicitly requested.

### Current RPC Manager State

- `RpcManager` and `RpcEndpointPool` now exist in `include/eth/rpc_manager.hpp` and `src/eth/rpc_manager.cpp`.
- URL rendering supports `apiKeyEnvVar`, `apiKeyLiteral`, and missing-key failure for templates that require `{key}`.
- `RpcHttpTransport` now lives in `include/eth/rpc_http_transport.hpp` and `src/eth/rpc_http_transport.cpp`.
- HTTP and HTTPS are both supported with Boost.Beast/Asio; HTTPS uses OpenSSL and can verify the peer when enabled.
- The next implementation step is to expose a chain-scoped receipt-source factory on top of the manager, without moving that orchestration into `EthWatchService`.

### Notes For Next Chat

- Do not reintroduce shell test harnesses for evmrelay functional testing.
- Keep chain names, RPC endpoints, API keys, ChainList URLs, and endpoint counts in config/fixtures, not hardcoded production C++.
- Existing untracked local artifacts are still intentionally not part of the commit:
  - `examples/all.json`
  - `examples/logs/`
  - `go-ethereum/`
  - `kelpdao-incident-report.pdf`
  - `rlp_enodes/`

## Discover-only producer/consumer mode and Polygon ENR fork-filter policy - 2026-05-21

### Current State

- Repository: `evmrelay`
- Branch: check with `git branch --show-current`.
- Local commit for this checkpoint: see `git log -1 --oneline`.
- User preference reinforced in this step:
  - Do not fake behavior with no-op consumers when the design should allow producers to run without a consumer.
  - Keep chain-specific discovery behavior data-driven in `examples/chains_config.json`, not hardcoded in C++.

### What Changed

- `eth_watch` now supports `--discover-only`.
  - In `--chain`, `--chains`, or `--all-chains` service mode, it starts configured discovery producers and peer queues without attaching an RLPx/ETH dial consumer.
  - Direct host/enode mode rejects `--discover-only`.
- `EthWatchServiceConfig` now has `attach_peer_dialer`.
  - Default is `true`.
  - When `false`, `EthWatchService` creates runtime peer queues and discovery clients, but does not create `DialScheduler` instances.
- `EthPeerQueue` now accepts and deduplicates cached/discovered peer candidates even when no scheduler is attached.
  - This is the producer/consumer split: producers can fill/count queue input with no dial consumer.
  - Live discover-only runs now keep `active_sessions=0` and `disconnect_feedback=0`.
- Per-chain discv5 ENR fork filtering is now data-driven.
  - `ChainPeerConfig` carries `discovery_fork_filter`.
  - `examples/chain_config.hpp` parses `discoveryForkFilter`.
  - Supported values are `require` and `disabled`.
  - Default remains `require`.
- `examples/chains_config.json` sets:
  - `polygon-mainnet.discoveryForkFilter = "disabled"`
  - `polygon-amoy.discoveryForkFilter = "disabled"`
- `EthWatchService` only sets `discv5Config.required_fork_id` when the chain policy requires it.
  - ETH Status remains the authoritative validation after dialing.
- `discv5_crawl` diagnostics improved:
  - Adds `--require-chain-fork` to reproduce service-side fork filtering in the crawl harness.
  - Prints emitted fork-id distribution and count of emitted peers without fork id.
  - Its CMake now copies `chains_config.json` next to the binary like the other examples.

### Polygon Finding

- Polygon ENR-tree discovery was not actually slow. The discv5 fork-id filter was dropping nearly all results.
- Diagnostic crawl using both `chains_config.json` and `chain_enodes.json.gz`:

```text
polygon-mainnet unfiltered:
  callback discoveries: 16

polygon-mainnet with --require-chain-fork:
  callback discoveries: 0
  wrong_chain: 3
  no_eth_entry: 13

polygon-amoy unfiltered:
  callback discoveries: 128

polygon-amoy with --require-chain-fork:
  callback discoveries: 1
  wrong_chain: 72
  no_eth_entry: 55
```

- Fork distribution showed Polygon ENR responses often omit `eth` fork id or advertise non-Polygon fork hashes.
  - Mainnet sample had 0 peers advertising configured `22d523b2`.
  - Amoy sample had only 1 peer advertising configured `8b7e4175`.
- Conclusion: for Polygon, discovery-time ENR fork filtering is not reliable. Let discovery enqueue candidates and let ETH Status validate.

### Live Verification

Commands run from `evmrelay/build/OSX/Debug`:

```bash
cmake --build . --target eth_watch discv5_crawl eth_watch_service_test eth_watch_example_test

ctest -R 'eth_watch_service_test|eth_watch_example_test' --output-on-failure

git diff --check
```

Live 11-chain discover-only run:

```bash
./examples/eth_watch/eth_watch \
  --chains ethereum-mainnet,ethereum-sepolia,ethereum-holesky,ethereum-hoodi,polygon-mainnet,polygon-amoy,bnb-smart-chain,bnb-smart-chain-testnet,base-mainnet,base-sepolia,gnosis-chain \
  --peer-selection discover-first \
  --discover-only \
  --max-peers-per-chain 1 \
  --max-peers-total 11 \
  --max-pending-peers 100 \
  --run-seconds 10 \
  --log-level info
```

Observed post-fix sample:

```text
total discovered_peers: 2751
active_sessions: 0
disconnect_feedback: 0

ethereum-mainnet: 14
ethereum-sepolia: 3
ethereum-holesky: 0
ethereum-hoodi: 2
polygon-mainnet: 46
polygon-amoy: 94
bnb-smart-chain: 860
bnb-smart-chain-testnet: 816
base-mainnet: 13
base-sepolia: 9
gnosis-chain: 894
```

### Notes For Next Chat

- Existing untracked local artifacts intentionally left out of the commit:
  - `examples/all.json`
  - `examples/logs/`
  - `go-ethereum/`
  - `kelpdao-incident-report.pdf`
  - `rlp_enodes/`
- The build warning about Boost.JSON non-virtual destructors is from third-party headers and was pre-existing/noisy.
- Suggested next step: run non-discover-only Polygon service samples to measure how many of the now-enqueued candidates pass ETH Status validation.

## Data-driven discovery cleanup and hand-off - 2026-05-21

### Current State

- Repository: `evmrelay`
- Branch: check with `git branch --show-current` in the new chat.
- Goal of the current work: make discovery and chain behavior data-driven from the beginning, removing hardcoded source registries and defaults.
- Important user preference: do not implement hardcoded "make it work first, refactor later" paths. Add/extend data schema first, then parse/validate, then wire behavior through explicit configuration and small interfaces.

### What Changed

- Per-chain discovery policy is now data-driven through `examples/chains_config.json`.
  - `_defaultAllChains` controls `eth_watch --all-chains`.
  - `discoveryDefault` supports `auto`, `discv4`, `cache-enr-discv5`, and `enr-tree`.
  - `enrTree` / `enrTrees` carries ENR-tree roots.
- `examples/chain_config.hpp` is now the shared example-side config loader for:
  - `chains_config.json` root and per-chain entries.
  - default chain lists.
  - per-chain discovery defaults and ENR-tree roots.
  - local or refreshed `chain_enodes.json(.gz)` chain metadata.
- `eth_watch` now:
  - supports `--chains <chain1,chain2,...>`.
  - uses per-chain service runtimes instead of one global class instance.
  - uses OS-assigned discovery ports in multi-chain mode unless `--discv5-port` is explicitly set.
  - supports `--max-pending-peers`.
  - keeps discovery decoupled from active ETH session slots via bounded pending queues.
  - prints final per-chain discovery summaries.
- `EthWatchService` now:
  - honors `ChainDiscoveryDefault` per chain.
  - starts ENR-tree discv5, cache-ENR discv5, or discv4 according to explicit chain config.
  - accepts discovered peers without ENR fork metadata and still rejects explicit fork mismatches.
  - no longer falls back to compiled ENR-tree roots.
- `ChainPeerConfig` now carries:
  - `discv5_bootnodes`, parsed from ENR records in `nodes` and `bootnodes`.
  - `enr_trees`.
  - `discovery_default`.
- Removed compiled bootnode/discovery registries:
  - `include/discv4/bootnodes.hpp`
  - `include/discv4/bootnodes_test.hpp`
  - `include/discv5/discv5_bootnodes.hpp`
  - `src/discv5/discv5_bootnodes.cpp`
  - `test/discv5/discv5_bootnodes_test.cpp`
- `discv5_crawl`, `test_discovery`, `test_discv5_connect`, and `test_enr_survey` now load chain data instead of using compiled bootnode arrays.
- `AgentDocs/CLAUDE.md` now explicitly requires data-driven, GOF-style, loosely-coupled, modular design from the first implementation step.

### Live Findings From This Checkpoint

- BSC mainnet/testnet with `discoveryDefault=discv4` is productive:
  - 10-second run discovered roughly 704 peers total across both BSC chains.
  - Mainnet discovered roughly 333 peers; testnet discovered roughly 371 peers.
- Polygon mainnet/amoy with `discoveryDefault=enr-tree` now starts discv5 through configured ENR trees:
  - 15-second run discovered 3 candidates total.
  - This confirmed the earlier Polygon issue was config loading, not discv5 itself.
- Standalone `discv5_crawl --chain polygon --port 0 --timeout 10` previously produced 16 callback discoveries using Polygon seeds.

### Design Rules For Next Chat

- Do not add chain/network/discovery facts to C++ source.
- If a value varies by chain/network/deployment, put it in `chain_enodes.json(.gz)` or `chains_config.json`, then parse and validate it.
- Do not add static registries, static bootnode arrays, switch statements over chain IDs, or helper functions that infer policy from chain names.
- If no explicit config exists, generic protocol inference is allowed only after data-driven config has been checked.
- Tests can use local fixtures, but fixtures must not become production registries.
- Prefer Strategy/Factory seams over source-level branching when behavior varies.

### Commands Already Verified

```bash
cd evmrelay
cmake --build build/OSX/Debug --target \
  eth_watch \
  discv5_crawl \
  test_discovery \
  test_enr_survey \
  test_discv5_connect \
  eth_watch_service_test \
  discv5_enr_tree_test

ctest --test-dir build/OSX/Debug \
  -R 'eth_watch_service_test|discv5_enr_tree_test|discv5_crawler_test' \
  --output-on-failure

ctest --test-dir build/OSX/Debug \
  -R 'discv5_client_test' \
  --output-on-failure

git diff --check
```

Notes:

- `discv5_client_test` needs local UDP bind permission. It failed under restricted sandbox with `bind: Operation not permitted`, then passed when rerun with elevated local socket permissions.
- Untracked local artifacts intentionally not part of this checkpoint:
  - `examples/all.json`
  - `examples/logs/`
  - `go-ethereum/`
  - `kelpdao-incident-report.pdf`
  - `rlp_enodes/`

### Suggested Next Steps

- Run a final live multi-chain stress test after pulling this checkpoint into a new chat:
  - Ethereum mainnet/sepolia
  - Polygon mainnet/amoy
  - BSC mainnet/testnet
  - Base mainnet/sepolia
  - Gnosis
- Use `--chains` with OS-assigned discovery ports, `--peer-selection discover-first`, `--max-pending-peers 100`, and a short bounded `--run-seconds`.
- Compare default discovery per chain from `chains_config.json`: ENR-tree, cache-ENR discv5, and discv4.
- Consider moving example-side config loading from `examples/chain_config.hpp` into a reusable library module if non-example binaries need the same data-driven discovery configuration.

## discv4 discovery queue and Base/BSC/Gnosis live comparison - 2026-05-21

### Completed in this step

- `examples/discovery/test_discovery.cpp`
  - No longer cancels the live discovery timeout after the first connection/activity by default.
  - Adds explicit `--stop-on-connection` for the old early-stop behavior.
- `examples/eth_watch/eth_watch.cpp`
  - Adds `--max-pending-peers <N>` so live discovery can keep a bounded per-chain backlog, e.g. 100 candidates, while active ETH session retention remains separate.
- `src/eth/eth_watch_service.cpp`
  - Changes the service scheduler fork filter to accept discovered peers with no ENR fork metadata and still reject peers that explicitly advertise the wrong fork hash.
  - This lets discv4 fallback candidates reach ETH Status validation instead of being dropped before dialing.
- `test/eth/eth_watch_service_test.cpp`
  - Covers the Gnosis-style discv4 fallback handoff with a discovered peer that has no ENR `eth_fork_id`.

### Live discv4 comparison

All runs used:

```bash
--chain-peers-json ./chain_enodes.json.gz
--max-peers-per-chain 3
--max-peers-total 3
--max-pending-peers 100
--watch-event 'Transfer(address,address,uint256)'
```

Observed 60-second runs before the Gnosis fork-filter fix:

```text
base-mainnet hybrid:
cached_peers=85 discovered_peers=0 auth_success=16 peer_hello_accepted=2 remote_status_accepted=0 remote_status_rejected=2

base-mainnet discover-first:
cached_peers=0 discovered_peers=0 auth_success=0 peer_hello_accepted=0 remote_status_accepted=0

bnb-smart-chain hybrid:
cached_peers=100 discovered_peers=13678 auth_success=329 peer_hello_accepted=48 remote_status_accepted=0 remote_status_rejected=45

bnb-smart-chain discover-first:
cached_peers=0 discovered_peers=17678 auth_success=477 peer_hello_accepted=41 remote_status_accepted=0 remote_status_rejected=41

gnosis-chain hybrid:
cached_peers=0 discovered_peers=24582 auth_success=0 peer_hello_accepted=0 remote_status_accepted=0

gnosis-chain discover-first:
cached_peers=0 discovered_peers=24473 auth_success=0 peer_hello_accepted=0 remote_status_accepted=0
```

Post-fix 30-second Gnosis discover-first smoke:

```text
cached_peers=0
discovered_peers=669
transport_connect_failures=17
auth_success=24
peer_hello_accepted=17
eth_status_sent=17
remote_status_accepted=0
remote_status_rejected=16
```

### Current interpretation

- Base discv4 bootnodes did not produce candidates in the 60-second discover-first sample; hybrid only exercised cached peers.
- BNB Smart Chain discv4 discovery is very productive; discover-first reached more RLPx auth attempts than hybrid in the sampled run, but neither accepted ETH Status.
- Gnosis discovery was producing peers, but the service dropped them before dialing because they lacked ENR fork metadata. The service now dials those candidates and relies on ETH Status as the authoritative validation.

### Verification

```bash
cd evmrelay/build/OSX/Debug
cmake --build . --target eth_watch test_discovery eth_watch_example_test eth_watch_service_test
ctest -R 'eth_watch_service_test|eth_watch_example_test' --output-on-failure
git diff --check
```

## Sepolia live peer connectivity and ENR-only validation - 2026-05-20

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- Current focus: make `examples/eth_watch` reliably connect to live Sepolia peers and receive ETH traffic for watched events.

### Completed in this step

- ~~Added explicit RLPx/ETH connection phase counters and final summaries so live runs can be interpreted without reading huge logs.~~
- ~~Logged outbound and accepted/rejected remote ETH Status fields, including negotiated ETH version, ETH offset, network id, genesis, fork hash, and fork next.~~
- ~~Added `--cache-peer-start-offset <N>` to rotate cached peers before existing spread logic, preserving deterministic coverage while starting deeper in the cache.~~
- ~~Changed `kTooManyPeers` handling so peers that return reason 4 get a long backoff and do not re-enter the queue during normal live-test runtimes.~~
- ~~Renamed noisy diagnostics away from `pre_connected`; summaries now distinguish transport failures, disconnects before peer HELLO, and disconnects before/after accepted ETH Status.~~
- ~~Verified live Sepolia peers accept `fork_hash=268956b6` and `fork_next=0`; the fork ID is not globally wrong.~~
- ~~Verified ENR-only `discover-first` mode does not preload cached peers; the completed live run reported `cached_peers=0`.~~
- ~~Confirmed ENR-tree/discv5 records are filtered by ENR `eth` fork id when chain fork metadata is available, while ETH Status remains the authoritative post-connect validation.~~
- ~~Checked go-ethereum behavior: RLPx disconnect reason 4 is `DiscTooManyPeers`; fork-id rejection maps to subprotocol error rather than reason 4.~~

### Live Sepolia validation

Cache-only run with `--cache-peer-start-offset 50`, 75 seconds:

```text
active_sessions=1
cached_peers=100
transport_connect_failures=23
auth_success=77
local_hello_sent=77
peer_disconnect_before_hello=75
peer_hello_accepted=2
eth_status_sent=2
remote_status_accepted=1
remote_status_rejected=1
too_many_peers_before_peer_hello=75
eth_messages=387
```

Hybrid cache + ENR run with `--cache-peer-start-offset 50`, 180 seconds:

```text
active_sessions=3
cached_peers=100
discovered_peers=7
transport_connect_failures=23
auth_success=78
local_hello_sent=78
peer_disconnect_before_hello=74
peer_hello_accepted=4
eth_status_sent=4
remote_status_accepted=3
remote_status_rejected=1
eth_messages=1107
backoff_drops=3
requeued=0
```

ENR-only `discover-first` run, 180 seconds:

```text
active_sessions=3
cached_peers=0
discovered_peers=11
transport_connect_failures=0
auth_success=7
local_hello_sent=7
peer_disconnect_before_hello=3
peer_hello_accepted=4
eth_status_sent=4
remote_status_accepted=3
remote_status_rejected=1
eth_messages=639
matched_logs=0
logs_seen=0
decode_failures=0
```

Disconnect summary for the ENR-only run:

```text
feedback=4
before_eth_status_accept=4
after_eth_status_accept=0
peer_disconnect_before_hello=3
too_many_peers_before_peer_hello=3
too_many_peers_after_eth_status_accept=0
transport_connect_failures=0
timeouts=0
subprotocol_errors=1
backoff_drops=0
requeued=0
flaky_drops=0
```

### Current interpretation

- ENR-only discovery found working live Sepolia ETH sessions more efficiently than scanning the top cached peer list.
- With `--max-peers-total 3`, the service fills three retained sessions and then stops exploring. This does not yet test a 300-peer sample.
- The ENR-only run's `eth_messages` stopped increasing after an initial burst while `active_sessions=3`; the next diagnostic target is per-session traffic liveness, not fork-id selection.
- `kTooManyPeers` is being suppressed long enough for live runs; `requeued=0` confirms it is not immediately hammering the same busy peers.

### Verification

```bash
cd evmrelay/build/OSX/Debug
cmake --build . --target eth_watch eth_watch_runner_test eth_watch_service_test
ctest -R 'eth_watch_runner_test|eth_watch_service_test|eth_watch_cli_test|eth_watch_example_test' --output-on-failure
git diff --check
```

Earlier broader validation also passed:

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'eth_watch_runner_test|eth_watch_service_test|eth_watch_example_test|eth_watch_cli_test|discv4_chain_peers_test|discv4_dial_scheduler_test|discv4_dial_filter_test|rlpx_session_tests' --output-on-failure
git diff --check
```

### Still intentionally not done

- Per-active-session idle/last-message summary is not yet implemented.
- A scan mode that tests about 300 peers while retaining only three active sessions is not yet implemented.
- The local ENR generated by `discv5_client` still does not advertise an `eth` fork entry.
- Direct host/port/pubkey and `--direct-enode` remain example-local.
- No bridge consensus/finality logic was added.
- `rlp_enodes`, `go-ethereum`, and live log directories remain reference/local artifacts unless explicitly requested.

### Next implementation step

Add active-session liveness diagnostics: endpoint, negotiated ETH version, accepted fork id, total messages per session, last message id, last message age, and receive-loop closed/active state. Then add separate scan/retention limits so live ENR discovery can sample around 300 peers without dropping below the desired number of retained active sessions.

## ENR-tree discovery and live peer queue validation - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- Current local HEAD after completed code commits: `af96e2c Add live ENR tree peer queue test`

### Completed in this step

- `examples/chains.json` was replaced with `examples/chains_config.json`.
- `examples/chains_config.json` now uses canonical chain keys directly:
  - `ethereum-mainnet`
  - `ethereum-sepolia`
  - `ethereum-holesky`
  - `ethereum-hoodi`
  - `polygon-mainnet`
  - `polygon-amoy`
- `examples/chain_config.hpp`
  - Loads fork hashes from generated `chain_enodes.json(.gz)` only.
  - Loads ENR-tree roots from `chains_config.json`.
  - Removed the short-name alias helper for cache keys.
- `include/discv5/enr_tree.hpp` / `src/discv5/enr_tree.cpp`
  - Add EIP-1459 `enrtree://` parsing and DNS TXT traversal.
  - Resolve usable ENR bootnodes from Ethereum and Polygon DNS discovery trees.
  - Traverse production trees breadth-first so broad DNS tries yield usable ENR leaves promptly.
- `src/discv5/discv5_enr.cpp`
  - Handles production ENRs with list-valued fields such as `eth`, `snap`, and `wit`.
- `include/eth/eth_watch_service.hpp` / `src/eth/eth_watch_service.cpp`
  - Resolve ENR-tree roots into discovery-only ENR bootnodes.
  - Start discv5 discovery from resolved ENRs.
  - Feed discv5-discovered peers into the shared `EthPeerQueue`.
  - Fall back to discv4 bootnodes when ENR-tree resolution produces no usable ENRs and valid bootnodes exist.
- `test/eth/eth_enr_tree_peer_cache_live_test.cpp`
  - Adds an opt-in live functional test that starts from an empty `EthPeerQueue`.
  - Runs ENR-tree/discv5 discovery for five seconds.
  - Reports and asserts that discovered peers enter the peer queue.
- Project headers touched in this phase now use include guards instead of `#pragma once`.

### Live functional validation

```bash
cd evmrelay/build/OSX/Debug
env EVMRELAY_RUN_LIVE_ENR_TREE_TEST=1 EVMRELAY_LIVE_ENR_TREE_CHAIN=polygon-mainnet EVMRELAY_LIVE_ENR_TREE_SECONDS=5 ./test_bin/eth_enr_tree_peer_cache_live_test
env EVMRELAY_RUN_LIVE_ENR_TREE_TEST=1 EVMRELAY_LIVE_ENR_TREE_CHAIN=ethereum-mainnet EVMRELAY_LIVE_ENR_TREE_SECONDS=5 ./test_bin/eth_enr_tree_peer_cache_live_test
```

Observed results on 2026-05-19:

- `polygon-mainnet`: 493 peers accepted into an empty `EthPeerQueue` over 5 seconds.
- `ethereum-mainnet`: 862 peers accepted into an empty `EthPeerQueue` over 5 seconds.

### Verification

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'discv5_enr_test|discv5_enr_tree_test|eth_enr_tree_peer_cache_live_test|eth_watch_service_test|eth_watch_example_test|eth_watch_cli_test|eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
git diff --check
```

Result:

```text
100% tests passed, 0 tests failed out of 10
```

### Still intentionally not done

- Direct host/port/pubkey and `--direct-enode` remain example-local.
- No bridge consensus/finality logic was added.
- gzip/JSON loading behavior was not changed.
- `rlp_enodes` remains reference-only unless explicitly requested.

### Next implementation step

Decide whether direct host/port/pubkey and `--direct-enode` should remain example-local permanently or become a production direct-session API. Remaining ENR-tree work is documentation and operational hardening rather than core discovery wiring.

## EVM relay discovery/dialer lifecycle coverage - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- Current local HEAD before this uncommitted step: `1ca92ec Add eth watch peer selection modes`

### New local changes in this step

- `test/eth/eth_watch_service_test.cpp`
  - Adds service-level lifecycle coverage proving discovered peers can enqueue while the only dial slot is saturated by an active cached-node dial.
  - Verifies queued discovery-produced peers drain through the same `DialScheduler` as slots are released.
  - Verifies discovery can continue enqueueing peers after an unconnected dial failure releases its slot.

### Still intentionally not done

- `rlp_enodes` was not touched.
- gzip/JSON loading behavior was not changed.
- No bridge consensus/finality logic was added.
- Direct host/port/pubkey and `--direct-enode` manual modes remain example-local.

### Verification run after these local changes

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R eth_watch_service_test --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 1
```

### Next implementation step

Continue remaining production cleanup around later ENR-tree / EIP-1459 discovery support, or decide whether direct host/port/pubkey and `--direct-enode` should stay example-local permanently or move behind a production direct-session API.

## EVM relay bootstrap peer loader split and explicit peer selection - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- Current local HEAD before this uncommitted step: `0f96446 Replace eth watch shell smoke tests`

### New local changes in this step

- `test/discv4/chain_peers_test.cpp`
  - Adds file-path loader coverage proving `load_bootstrap_peers_from_json(...)` reads `bootnodes` and ignores `nodes`.
- `include/eth/eth_watch_service.hpp` / `src/eth/eth_watch_service.cpp`
  - Adds `EthWatchDiscoveryMode` with explicit cache-only, discover-if-needed, discover-first, and hybrid peer selection.
  - Keeps the existing default behavior as discover-if-needed.
  - Discover-first starts from `bootnodes` without preloading cached `nodes`.
  - Hybrid preloads cached `nodes` and starts discovery from `bootnodes` in parallel.
- `include/eth/eth_peer_queue.hpp` / `src/eth/eth_peer_queue.cpp`
  - Allows service orchestration to create a peer queue without preloading cached nodes for discover-first mode.
- `include/eth/eth_watch_cli.hpp`
  - Carries explicit discovery mode through `build_service_config(...)`.
- `examples/eth_watch/eth_watch.cpp`
  - Adds `--peer-selection cache-only|discover-if-needed|discover-first|hybrid` for cache-backed service modes.
- `test/eth/eth_watch_service_test.cpp`
  - Covers cache-only, discover-first, and hybrid mode behavior.
- `test/eth/eth_watch_cli_test.cpp`
  - Covers default and explicit discovery mode propagation through CLI config construction.
- `AgentDocs/EVMRELAY_COMPLETION_PLAN.md`
  - Marks the `bootstrap_peers` loader split and explicit peer selection items complete.

### Still intentionally not done

- `rlp_enodes` was not touched.
- gzip/JSON loading behavior was not changed.
- No bridge consensus/finality logic was added.
- Direct-enode/manual testing path remains example-local.

### Next implementation step

Continue production cleanup around later ENR-tree discovery support and deeper discovery/dialer lifecycle tests, or decide whether the direct host/port/pubkey and `--direct-enode` helper should stay example-local permanently.

## EVM relay eth_watch C++ example tests - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- Current local HEAD before this uncommitted step: `2d31e28 Add eth watch loaded metadata coverage`

### New local changes in this step

- `examples/eth_watch/eth_watch_example_test.cpp`
  - Adds a compiled C++ example smoke test for the cache-backed eth_watch service path.
  - Covers cached chain metadata with `nodes` and `bootnodes`.
  - Covers GNUS watch-spec construction for Transfer and BridgeSourceBurned events.
  - Covers Gnosis-style empty `nodes` plus valid `bootnodes` discv4 fallback through `EthWatchService`.
  - Covers multi-chain service config creation without spawning the `eth_watch` process or scraping logs.
- `examples/eth_watch/CMakeLists.txt`
  - Builds `eth_watch_example_test` and registers it with CTest.
- `examples/test_eth_watch.sh` and `examples/test_eth_watch_smoke.sh`
  - Removed tracked shell test harnesses in favor of the compiled C++ example test.
- Docs updated:
  - `examples/README.md`
  - `AgentDocs/BOOTNODES_CONFIGURATION.md`
  - `AgentDocs/COMMANDS_REFERENCE.md`
  - `AgentDocs/PUBLIC_NODES_FOR_TESTING.md`
  - `AgentDocs/QUICK_TEST_GUIDE.md`
  - `AgentDocs/WHY_NO_MESSAGES.md`

### Verification run after these local changes

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'eth_watch_example_test|eth_watch_service_test|eth_watch_cli_test|eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 6
```

### Still intentionally not done

- `rlp_enodes` was not touched.
- gzip/JSON loading behavior was not changed.
- No bridge consensus/finality logic was added.
- Direct-enode/manual testing path remains example-local.
- Live public-peer connectivity remains a manual validation because peer reachability is network-environment dependent.

### Next implementation step

Decide whether the remaining direct host/port/pubkey and `--direct-enode` helper should stay example-local permanently or become a small production direct-session API. If it stays example-local, the next production work is loader cleanup around `bootstrap_peers` parsing only `bootnodes`.

## EVM relay EthWatchService orchestration progress - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- HEAD at handoff time remains: `c819a0f Normalize eth watch runner file endings`
- `EthWatchService` now has the target production API shape:

```cpp
eth::EthWatchService service;
service.initialize(config, callback);
service.run(io);
```

### New local changes in this step

- `include/eth/eth_watch_service.hpp`
  - Adds `WatchEventContext`, `WatchEventNotification`, and `WatchEventNotificationCallback` at service level.
  - Adds `EthWatchEventSpec` and `EthWatchServiceConfig`.
  - Adds `initialize(...)`, `run(io)`, `stop()`, and runtime inspection helpers.
  - Adds narrow optional test seams for replacing live RLPx dial and discv4 fallback startup.
- `src/eth/eth_watch_service.cpp`
  - Creates the shared watcher pool.
  - Creates one `DialScheduler` per configured chain.
  - Creates one `EthPeerQueue` per chain and preloads cached `nodes`.
  - Stores discovery-only `bootnodes` on the peer queue.
  - Starts discv4 fallback for chains with empty cached `nodes` and valid `bootnodes`.
  - Wires discovery callbacks into the same `EthPeerQueue`.
  - Provides the default production dial path: RLPx connect, ETH Status handshake, `EthWatchRunner` setup, watch registration, and decoded notification dispatch.
  - Stops schedulers/discovery clients from `stop()` and destructor cleanup.
- `include/eth/eth_watch_runner.hpp` / `src/eth/eth_watch_runner.cpp`
  - Reuses service-level notification types.
  - Allows runtime watch registration to carry optional block ranges.
- `test/eth/eth_watch_service_test.cpp`
  - Adds production orchestration coverage for cached-node scheduler/queue creation.
  - Adds Gnosis-style empty-`nodes` plus `bootnodes` discv4 fallback coverage.
  - Adds production-path scheduler feedback requeue coverage.
  - Adds deterministic invalid config rejection coverage.

### Verification run after these local changes

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'eth_watch_service_test|eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 4
```

### Still intentionally not done

- `examples/eth_watch/eth_watch.cpp` has not been thinned yet. It still owns the CLI functional-test wrapper path as requested.
- `rlp_enodes` was not touched.
- gzip/JSON loading behavior was not changed.
- No bridge consensus/finality logic was added.

### Next implementation step

Continue hardening the production service path now that `examples/eth_watch` cache modes delegate to `EthWatchService`. Direct-enode/manual testing still uses the example's direct helper path.

## EVM relay eth_watch service wrapper progress - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- Current local HEAD before this uncommitted step: `7f4778a Add eth watch service orchestration`

### New local changes in this step

- `examples/eth_watch/eth_watch.cpp`
  - Cache-based `--chain` mode now builds `EthWatchServiceConfig`, registers decoded notification output, and calls `service.initialize(...)` / `service.run(io)`.
  - Cache-based `--all-chains` mode now delegates shared pool, per-chain schedulers, peer queues, cached `nodes`, and `bootnodes` discovery fallback to `EthWatchService`.
  - Direct host/port/pubkey and `--direct-enode` manual modes remain on the existing direct `run_watch(...)` helper for local testing.
  - Event output formatting was factored into a shared notification logger used by both direct runner callbacks and service callbacks.
- `test/eth/eth_watch_service_test.cpp`
  - Adds service-level coverage that a discv4 fallback-produced peer enters the same production dial queue.

### Verification run after these local changes

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'eth_watch_service_test|eth_watch_runner_test|eth_watch_cli_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 5
```

### Still intentionally not done

- `rlp_enodes` was not touched.
- gzip/JSON loading behavior was not changed.
- No bridge consensus/finality logic was added.
- Direct-enode/manual testing path was kept in the example instead of being folded into service orchestration.

### Next implementation step

Add explicit service/example coverage for no-cached-node chain config using real loaded chain metadata, then consider moving the remaining direct-enode helper into a production direct-session API if needed. Update smoke-test docs after the next live validation pass.

## EVM relay peer queue refactor handoff - 2026-05-19

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- HEAD at handoff time: `c819a0f Normalize eth watch runner file endings`
- Current tracked working tree has uncommitted refactor changes listed below.
- The last committed and pushed refactor work before these local edits:
  - `37270bb Add eth peer queue separation`
  - `c819a0f Normalize eth watch runner file endings`

### User constraints for the next session

- Do not modify `rlp_enodes`; it is a completed/reference submodule for this phase.
- Do not refactor gzip or JSON loading. The server filename is `chain_enodes.json.gz`, but clients receive unzipped JSON, so ignore the `.gz` suffix in loader behavior.
- Do not refactor `examples/eth_watch` yet. It is currently a functional test / CLI wrapper. Only refactor it after production `EthWatchService` initialization owns the orchestration.
- Bridge consensus/finality work does not belong in this relay path. The relay should watch configured chains/message filters, decode matching messages/logs, and invoke callbacks. Bridge finality and UTXO-system messaging will be verified through RPC outside this relay flow.
- For builds, use:

```bash
cd evmrelay/build/OSX/Debug
cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug
ninja
```

### Current uncommitted tracked changes

- `include/discv4/dial_scheduler.hpp`
  - Adds `discv4::DialFeedbackFn`.
  - `DialScheduler` now exposes `feedback_fn`.
  - Dial exits report `kTcpError` with `was_connected` based on whether the ETH/RLPx session reached `on_connected`.
  - Connected sessions install an RLPx disconnect handler and report peer Disconnect reasons back through the scheduler feedback path.
- `include/eth/eth_peer_queue.hpp`
  - Adds `EthPeerQueueConfig`.
  - Adds `EthPeerDisconnectFeedback`.
  - Makes `enqueue_discovered_peer(...)` return `bool`.
  - Adds `report_peer_disconnected(...)`.
  - Adds counters for requeued peers, duplicates, capacity drops, and flaky-peer drops.
- `src/eth/eth_peer_queue.cpp`
  - Subscribes `EthPeerQueue` to `DialScheduler::feedback_fn`.
  - Requeues `kTooManyPeers`.
  - Requeues `kTcpError` / `kTimeout` only when the peer had previously connected.
  - Stops requeueing after `max_disconnect_requeues`.
  - Keeps bounded pending queue behavior and duplicate suppression for discovery-produced peers.
- `test/eth/eth_watch_runner_test.cpp`
  - Adds positive and negative tests for duplicate discovered peers, bounded pending queue drops, too-many-peers requeue, connected network-disconnect requeue, flaky-peer suppression, scheduler feedback wiring, and ignoring unconnected dial failures.
- `AgentDocs/EVMRELAY_COMPLETION_PLAN.md`
  - Struck out completed peer queue/backpressure/separation work.
  - Updated bridge-related completion language to reflect callback-only relay responsibility.
  - Added the completed dial/session disconnect feedback item.

### Verification run after these local changes

```bash
cd evmrelay/build/OSX/Debug
ninja
ctest -R 'eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 3
```

### Next implementation step

Before refactoring `examples/eth_watch`, add a production initialization/orchestration API under `include/eth` and `src/eth`, probably on `EthWatchService` or a small helper owned by it.

The target shape is:

```cpp
eth::EthWatchService service;
service.initialize(config, callback);
service.run(io);
```

That production API should own:

- chain/watch config intake;
- watcher pool creation;
- per-chain `DialScheduler` creation;
- `EthPeerQueue` creation;
- cached `nodes` preload;
- discovery-only `bootnodes`;
- discv4 fallback for chains with no usable cached `nodes`, with Gnosis Chain as the first target;
- RLPx connect + ETH Status handshake + `EthWatchRunner` setup;
- watch registration and decoded callback dispatch.

Once this exists and has tests, `examples/eth_watch/eth_watch.cpp` can become a thin wrapper that loads config, registers output callbacks, and calls the production service API.

### Suggested next tests

- Production service initialization creates schedulers and peer queues from cached `nodes`.
- Empty `nodes` plus valid `bootnodes` starts discv4 discovery fallback.
- Discovery callback enqueues discovered peers into the same `EthPeerQueue`.
- Scheduler feedback requeues eligible disconnected peers through the production service path.
- Decoded message/event callbacks propagate from `EthWatchService` with chain metadata.
- Invalid config cases fail deterministically.

### Current local untracked evmrelay artifacts

These were present locally and should not be removed unless explicitly requested:

- `AgentDocs/Refactor_chat.txt`
- `CRDT.Datastore.TEST.unit_2/`
- `CRDT.Datastore.TEST/`
- `examples/all.json`
- `examples/logs/`
- `examples/test_discovery.sh`
- `go-ethereum/`
- `rlp_enodes/`

### New chat handoff prompt

```text
Continue in evmrelay from AgentDocs/CHECKPOINT.md and AgentDocs/EVMRELAY_COMPLETION_PLAN.md.

Branch: develop
HEAD at handoff: c819a0f

Do not touch rlp_enodes. Do not change gzip/json loading behavior. Do not refactor examples/eth_watch yet; it is still the functional test/CLI wrapper until production EthWatchService initialization exists.

Current uncommitted work wires dial/session disconnect feedback into EthPeerQueue:
- DialScheduler exposes feedback_fn and reports dial exits / peer Disconnect messages.
- EthPeerQueue requeues eligible peers, caps flaky peers, and tracks retry/drop counters.
- Focused tests pass with:
  ninja
  ctest -R 'eth_watch_runner_test|discv4_chain_peers_test|discv4_dial_scheduler_test' --output-on-failure

Next step:
Add the production EthWatchService initialize/run orchestration API under include/eth and src/eth. It should own watcher pool, per-chain DialScheduler, EthPeerQueue, cached nodes, discovery-only bootnodes, discv4 fallback for no-node chains such as Gnosis, RLPx connect, ETH Status handshake, EthWatchRunner setup, watch registration, and decoded callback dispatch.

Only after that API is tested should examples/eth_watch be reduced to config loading plus callback registration.
```

## Bridge consensus adapter handoff - 2026-05-16

### Current state

- Repository: `evmrelay`
- Branch: `develop`
- HEAD: `1578296 Decode bridge event claim payloads`
- Remote state: `develop` is ahead of `origin/develop` by one local commit unless pushed after this checkpoint.
- Tracked working tree has this checkpoint update only, plus unrelated local artifacts listed below.

### Relevant bridge work

- `1578296 Decode bridge event claim payloads`
  - Adds `eth::decode_bridge_event_claim_payload(const codec::ByteBuffer&)`.
  - Strictly rejects malformed domain bytes, truncated payloads, invalid length prefixes, and trailing bytes.
  - Extends `bridge_observation_test` with payload round-trip and malformed payload rejection.
- `3a61a55 Fix zero offset eth message routing`
  - Pushed the staged zero-offset ETH message routing changes that were still local.
- `a1d2811 Expose bridge event claim payload`
  - Exposes `eth::bridge_event_claim_payload(const BridgeEventClaim&)`.
  - This is the payload entry point SuperGenius consensus should use for bridge event subjects.
- `92c33b3 Add bridge observation signing`
  - Bridge observation signing support is already on the remote branch.
- Earlier RPC receipt / codec / finality work is also on the remote branch.

### Relevant SuperGenius consensus context

The parent SuperGenius consensus refactor is complete enough for evmrelay bridge integration.

Consensus subjects now use opaque payload identity:

- `account_id`
- `subject_type_hash`
- `payload`
- `payload_hash`

Consensus subjects no longer carry:

- `subject_id`
- `subject_type`
- built-in subject enums
- protobuf `oneof payload`
- `GenericSubject`

Built-in consensus payloads are still protobuf messages, but they are serialized into opaque payload bytes. Dispatch is by `subject_type_hash`.

For bridge claims, SuperGenius uses this subject type string:

```text
gnus.bridge_event.v1
```

The bridge subject construction path in SuperGenius is wrapped by `src/account/BridgeConsensusAdapter.hpp/.cpp` and still uses:

```cpp
ConsensusManager::CreateGenericSubject(
    account_id,
    "gnus.bridge_event.v1",
    eth::bridge_event_claim_payload(claim));
```

Handlers are registered by subject type string / hash through the current SuperGenius consensus registration path.

SuperGenius bridge adapter commit:

- `fa80bcfd Add bridge consensus adapter`
  - Defines `sgns::kBridgeEventSubjectType`.
  - Adds `CreateBridgeEventConsensusSubject(...)`.
  - Adds `DecodeBridgeEventConsensusSubject(...)`.
  - Adds bridge handler wrapping/registration helpers.
  - Adds tests for malformed bridge payload rejection, subject type hash mismatch, payload hash mismatch, and successful handler dispatch.

### Verification already run

- `evmrelay/build/OSX/Debug/test_bin/bridge_observation_test` passed with 12 tests.
- `build/OSX/Debug/test_bin/bridge_consensus_adapter_test` passed in the parent SuperGenius tree with 5 tests.
- `build/OSX/Debug/test_bin/consensus_subject_test` passed in the parent SuperGenius tree.
- `ninja -C build/OSX/Debug` passed in the parent SuperGenius tree.

### Current local untracked evmrelay artifacts

These were present before this checkpoint and should not be removed unless explicitly requested:

- `AgentDocs/Refactor_chat.txt`
- `CRDT.Datastore.TEST.unit_2/`
- `CRDT.Datastore.TEST/`
- `examples/all.json`
- `examples/logs/`
- `examples/test_discovery.sh`
- `go-ethereum/`
- `rlp_enodes/`

### Primary next steps

The evmrelay payload work and parent SuperGenius bridge adapter are done. Next work is in the parent SuperGenius repo unless bridge payload schema changes are needed.

1. Push evmrelay commit `1578296` if it has not already been pushed.
2. Push parent SuperGenius commit `fa80bcfd` if it has not already been pushed.
3. In SuperGenius, route finalized `gnus.bridge_event.v1` certificates into the existing bridge mint / claim completion path.
4. Keep using `eth::decode_bridge_event_claim_payload(...)` through the bridge-owned SuperGenius adapter; do not move bridge parsing into core consensus.
5. Add focused parent tests for:
   - proposal/certificate handling path for `gnus.bridge_event.v1`
   - finalized bridge claim routes into mint / claim completion
   - malformed finalized bridge payload rejection/stall behavior according to the chosen handler contract

### eth_watch / chain peer cache update - 2026-05-17

Current local `eth_watch` work is in progress and not yet committed:

- `examples/eth_watch/eth_watch.cpp`
  - Adds `--all-chains` for the four mainnet EVM chains:
    - `ethereum-mainnet`
    - `polygon-mainnet`
    - `bnb-smart-chain`
    - `base-mainnet`
  - Uses chain metadata from the chain peer cache instead of local alias tables.
  - Registers the same watcher event specs across all active chain schedulers.
  - Passes canonical chain names into `EthWatchRunner` so event callbacks and stats identify the chain.
  - Counts callback events globally and per chain.
  - Adds `--display-events <count>`; default is `2`, so only the first detailed decoded events are printed.
  - Uses `eth::EthWatchConnectionConfig` for reusable connection limits instead of hardcoded example-only values.
  - Adds `--max-peers-per-chain <count>` and `--max-peers-total <count>`; defaults are `3` per chain and `24` total.
  - Fixes the generic ETH message guard so normalized ETH messages are not immediately ignored.
  - Calls the scheduler `on_connected(session)` callback after ETH Status succeeds.
  - Preserves chain-cache fork ID metadata when `--chain ... --direct-enode ...` is used. Direct local geth testing was resetting the loaded fork ID to zero unless explicit fork overrides were passed.
- `src/rlpx/rlpx_session.cpp`
  - Fixes `receive_message_with_timeout(...)` so queued messages are polled until the deadline instead of checking once and sleeping for the full timeout. Local geth sent Status immediately, but the ETH handshake did not consume it until after timeout.
- `test/discv4/chain_peers_test.cpp`
  - The live chain peer cache test first uses `EVMRELAY_CHAIN_ENODES_JSON`, then local `chain_enodes.json.gz` / `.json`, then falls back to the live URL.
  - This keeps the test active while allowing sandboxed CTest runs with a pre-downloaded cache.
- `test/discv4/CMakeLists.txt`
  - Sets `EVMRELAY_CHAIN_ENODES_JSON=${CMAKE_BINARY_DIR}/chain_enodes.json.gz` for `discv4_chain_peers_test`.
- Pre-downloaded test cache location used in the current Debug build:
  - `build/OSX/Debug/chain_enodes.json.gz`

Verification run:

```bash
cd evmrelay/build/OSX/Debug
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
ninja
ctest -R "eth_watch|eth_receipt_source|event_filter|abi_decoder|discv4_chain_peers|rlpx_session" --output-on-failure
ctest -R "discv4_dial_scheduler|discv4_dial_filter|eth_watch|discv4_chain_peers" --output-on-failure
```

Result: focused watcher, RLPx, discv4 scheduler/filter, and cached chain peer tests passed in the normal sandbox after the cache file was pre-downloaded.

Local geth direct-mode verification:

```bash
cd evmrelay/go-ethereum
./build/bin/geth --sepolia --datadir /tmp/evmrelay-geth-sepolia --port 30303 --http --http.addr 127.0.0.1 --http.port 8545 --http.api admin,eth,net,web3 --nat extip:127.0.0.1 --nodiscover --maxpeers 2 --netrestrict 127.0.0.0/8
```

Then, from `evmrelay/build/OSX/Debug`, connect using the local node enode from geth output or `admin.nodeInfo.enode`:

```bash
./examples/eth_watch/eth_watch --chain ethereum-sepolia --chain-peers-json ../../../rlp_enodes/output/chain_enodes.json --direct-enode '<local-geth-enode>' --watch-event 'Transfer(address,address,uint256)' --display-events 1 --log-level info --no-chain-peers-url
```

Observed result after the fixes: ETH Status succeeds with `network_id=11155111 protocol=69 latest_block=0`, and periodic watch stats are emitted.

Notes:

- Do not assume a source-tree `rlp_enodes/` directory exists.
- If `build/OSX/Debug/chain_enodes.json.gz` is missing, either pre-download it there or set `EVMRELAY_CHAIN_ENODES_JSON` to another existing `chain_enodes.json(.gz)` file before running CTest in a network-restricted environment.
- Running without a cache still exercises the live URL fallback and requires network/DNS access.

### New chat handoff prompt

```text
Continue in evmrelay from AgentDocs/CHECKPOINT.md.

Current evmrelay branch is develop at local HEAD 1578296. It is ahead of origin/develop by one commit unless pushed after this checkpoint.
Do not touch the local untracked artifact dirs/files unless explicitly asked.

SuperGenius consensus now uses opaque consensus subjects:
- account_id
- subject_type_hash
- payload
- payload_hash

It no longer uses subject_id, subject_type, built-in subject enums, protobuf oneof payload, or GenericSubject.

Primary next step:
The evmrelay bridge claim payload decoder and SuperGenius bridge consensus adapter are done.

Next parent SuperGenius step:
Route finalized `gnus.bridge_event.v1` certificates into the existing bridge mint/claim completion path using the bridge-owned adapter in `src/account/BridgeConsensusAdapter.hpp/.cpp`.

Keep bridge parsing outside core consensus. Add tests for proposal/certificate handling and successful finalized bridge claim routing.
```

### eth_watch discovery follow-up - 2026-05-21

Current local discovery work is in progress and not yet committed:

- `include/discv4/chain_peers.hpp`
  - `ChainPeerConfig` now carries `discv5_bootnodes`, populated from ENR records in chain peer cache `bootnodes` and `nodes`.
- `src/discv4/chain_peers.cpp`
  - Parses cache ENR records into `discv5_bootnodes` so chains without ENR-tree roots can still seed discv5 discovery from the cache package.
- `include/eth/eth_watch_service.hpp` and `src/eth/eth_watch_service.cpp`
  - `EthWatchService` can start discv5 from cached ENR seeds when ENR-tree roots are absent.
  - `discover-first` starts with an empty RLPx peer queue, then feeds the queue from discv5 discoveries.
  - `discover-if-needed` starts cache-ENR discv5 only when no cached RLPx peers are queued.
  - discv4 fallback is skipped when cache-ENR discv5 starts successfully.
  - Discovery filtering is permissive for missing ENR fork metadata while still rejecting explicit wrong fork hashes.
- `examples/eth_watch/eth_watch.cpp`
  - Adds `--max-pending-peers <count>` so discovery can continue producing candidates while active connection slots are full, bounded by the per-chain queue cap.
  - Adds `--discv5-port <udp-port>` for live runs that need a chain-specific UDP discovery bind port, such as Base UDP 9222.
- `examples/discovery/test_discovery.cpp`
  - Discovery no longer stops at first successful connection/activity by default.
  - Adds `--stop-on-connection` for the old early-stop behavior.
- `examples/README.md`
  - Documents `--max-pending-peers` and `--discv5-port`.
- Tests updated:
  - `test/eth/eth_watch_service_test.cpp`
  - `test/discv4/chain_peers_test.cpp`

Verification run:

```bash
cd evmrelay
cmake --build build/OSX/Debug --target eth_watch eth_watch_service_test discv4_chain_peers_test
cd build/OSX/Debug
ctest -R 'eth_watch_service_test|discv4_chain_peers_test' --output-on-failure
cd ../..
git diff --check
```

Result: focused build, focused tests, and whitespace check passed.

Live discovery results:

- Base hybrid with cache preload, 60s:
  - `cached_peers=85 discovered_peers=0 auth_success=16 peer_hello_accepted=2 remote_status_accepted=0 remote_status_rejected=2`
- Base discover-first with discv4 fallback before cache-ENR discv5 support, 60s:
  - `cached_peers=0 discovered_peers=0 auth_success=0`
- Base standalone discv5 crawl from 8 cache ENRs, UDP 9222, no RLPx preload, 60s:
  - `callback discoveries=633 packets_received=208 whoareyou=44 nodes_packets=164 discovered=633 queued=593 measured=48 failed=0 wrong_chain=0`
- Base `eth_watch` discover-first from cache ENRs, discv5 UDP 9000, no RLPx preload, 45s:
  - `discv4_clients=0 cached_peers=0 discovered_peers=23 transport_connect_failures=0 auth_success=23 peer_disconnect_before_hello=23 peer_hello_accepted=0`
- Base `eth_watch` discover-first from cache ENRs, discv5 UDP 9222, no RLPx preload, 45s:
  - Command:
    `./examples/eth_watch/eth_watch --chain base-mainnet --chain-peers-json ./chain_enodes.json.gz --peer-selection discover-first --max-peers-per-chain 3 --max-peers-total 3 --max-pending-peers 100 --discv5-port 9222 --run-seconds 45 --watch-event 'Transfer(address,address,uint256)'`
  - Result:
    `discv4_clients=0 cached_peers=0 discovered_peers=23 transport_connect_failures=0 auth_success=23 peer_disconnect_before_hello=23 peer_hello_accepted=0 eth_status_sent=0 remote_status_accepted=0 remote_status_rejected=0`
- BNB hybrid with cache preload, 60s:
  - `cached_peers=100 discovered_peers=13678 auth_success=329 peer_hello_accepted=48 remote_status_accepted=0 remote_status_rejected=45`
- BNB discover-first, 60s:
  - `cached_peers=0 discovered_peers=17678 auth_success=477 peer_hello_accepted=41 remote_status_accepted=0 remote_status_rejected=41`
- Gnosis discover-first after permissive missing-fork filter, 30s:
  - `cached_peers=0 discovered_peers=669 transport_connect_failures=17 auth_success=24 peer_hello_accepted=17 eth_status_sent=17 remote_status_accepted=0 remote_status_rejected=16`

Interpretation:

- Base has working discv5 discovery from cached ENR records; the service now reaches RLPx auth without preloading cache nodes.
- Binding discv5 to UDP 9222 did not change the 45s Base service outcome compared with UDP 9000 in this environment: both yielded 23 RLPx auth successes and all peers disconnected before HELLO.
- The next Base blocker is not discovery startup; it is post-auth peer behavior before HELLO, likely remote admission / peer capacity / advertised protocol expectations.
- BNB discovery is much richer and continues to feed the queue under discover-first.
- Gnosis required allowing missing fork metadata on discovered ENRs; after that, it reaches ETH Status but peers reject Status.
