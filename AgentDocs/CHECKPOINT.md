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

### Secondary remaining eth_watch cleanup

The older `eth_watch` cleanup remains secondary unless the user asks to return to it.

- `examples/eth_watch/eth_watch.cpp` may still have redundant local chain structure:
  - `ChainEntry::canonical_name`
  - local `kChains`
- Intended direction:
  - canonical chain names should come from bootstrap JSON / bootstrap peer helpers
  - no duplicate alias names in `eth_watch`
  - no duplicated bootnode arrays in `eth_watch`
  - fork hash should come from cached `chain_enodes.json.gz`
  - `network_id` and `genesis_hash` remain unless bootstrap metadata is extended
- Runtime handshake still has not been re-proven end-to-end after those structural edits.

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
