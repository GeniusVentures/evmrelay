# Checkpoint Log

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
