# EVMRelay Refactor Summary

Source: `AgentDocs/Refactor_chat.txt`

## Core Decision

Refactor `evmrelay` so `EthWatch` is no longer tightly coupled to RLPx or the Ethereum `eth/*` wire protocol. The bridge should treat RLPx as one optional event-source backend, while the stable bridge path consumes normalized, signed event claims that can be verified by GNUS consensus.

The target abstraction is:

```text
edge watcher transport -> receipt/log -> normalized bridge event -> signed observation -> consensus certificate -> mint/release
```

The mint/release side should never depend on DevP2P, RLPx, `eth/68` through future `eth/*` versions, or raw RLP objects.

## What Bloom Filters Are For

Block `logsBloom` is useful only as a prefilter for emitted logs/events. It can answer:

- whether a block may contain logs from a watched contract address
- whether a block may contain a watched event topic

It cannot prove:

- a contract storage update
- a code update
- a successful function call
- finality

Bloom matches can have false positives, so exact verification must still check receipt/log fields:

```text
receipt.status == 1
receipt.blockHash == proposed blockHash
receipt.logs[logIndex].address == watched contract
receipt.logs[logIndex].topics[0] == watched event topic
decoded event fields match the proposal
```

For bridge behavior, receipts/logs are the security object. A transaction alone only proves that a call was attempted; the receipt proves success and emitted logs.

## Recommended Architecture

Split the system into transport-neutral core logic plus pluggable evidence sources.

```text
evmrelay_core
  EventFilter
  AbiDecoder
  MatchedEvent
  BridgeEventClaim / BridgeEventObservation
  EventDeduper
  ReceiptVerifier

evmrelay_rpc_watch
  JsonRpcClient
  WebSocketLogSubscriber
  ChainPoller
  FinalityPolicy
  ReceiptVerifier
  ObservationSigner

evmrelay_light_watch
  EthWatch
  ObservationVerifier
  QuorumPolicy
  PubSubSubscriber

evmrelay_consensus_adapter
  ObservationSigner
  ObservationVerifier
  ReputationQuorumPolicy
  ConsensusProposalPublisher

evmrelay_rlpx_optional
  discv4/discv5
  RLPx
  eth protocol adapters
  GetReceipts / Receipts handling
```

The existing `EthWatchService`, event filtering, ABI decoding, and typed callback behavior are worth preserving. The refactor should move transport-specific data acquisition below a narrow interface instead of letting RLPx define the public shape of `EthWatch`.

Suggested interface direction:

```cpp
class IEthReceiptSource
{
public:
    virtual void watchEvent(const EventFilter& filter, EventCallback cb) = 0;
    virtual ReceiptResult getReceipt(const Hash256& txHash) = 0;
};
```

Expected implementations:

- `RlpxReceiptSource`: native DevP2P/RLPx detection and receipt fetching
- `RpcReceiptSource`: `eth_subscribe logs`, `eth_getLogs`, and `eth_getTransactionReceipt`
- `GnusCertifiedReceiptSource`: already-certified GNUS bridge events

## Bridge Flow

The bridge MVP should use top reputation nodes as watcher/signers rather than making every node or client run Ethereum P2P.

1. User calls the source-chain bridge contract.
2. Contract emits a specific bridge event, such as `BridgeSourceBurned(...)`.
3. Top reputation ETHWatch nodes observe the event through RPC/WebSocket, own nodes, or optional RLPx.
4. Watchers wait for the chain-specific finality policy.
5. Watchers verify the transaction receipt and exact log.
6. Each watcher signs a normalized event claim.
7. GNUS consensus accepts the event after quorum or weighted reputation threshold.
8. Destination mint/release function verifies the GNUS consensus certificate.
9. The event is marked consumed by `srcChainId + txHash + logIndex`.

The wider network can verify only proposed events through free/public RPC calls such as `eth_getTransactionReceipt(txHash)`, which is much cheaper than scanning every block.

## Normalized Event Claim

The signed bridge claim should be independent of how the event was discovered.

```cpp
struct BridgeEventClaim
{
    uint64_t srcChainId;
    uint64_t destChainId;

    uint64_t blockNumber;
    Hash256  blockHash;
    Hash256  txHash;
    uint32_t logIndex;

    Address  bridgeContract;
    Hash256  eventTopic0;
    std::vector<Hash256> topics;
    ByteBuffer data;

    Address  sender;
    uint256  tokenIdOrNonce;
    uint256  amount;
    Address  recipient;

    uint64_t observedAt;
    uint64_t finalityDepth;
};
```

Sign `hash(domainSeparator, BridgeEventClaim)`.

The domain separator should include at least:

- `GNUS_BRIDGE_EVENT_V1`
- source chain ID
- destination chain ID
- bridge contract address

The dedupe/consumption key should be:

```text
srcChainId + txHash + logIndex
```

## Consensus Certificate

The destination mint function should verify a GNUS certificate, not raw Ethereum receipt proofs or hundreds of individual watcher signatures.

```solidity
function mintWithCertificate(
    BridgeMintMessage calldata message,
    ConsensusCertificate calldata certificate
) external {
    bytes32 messageHash = hashBridgeMintMessage(message);

    require(verifyConsensusCertificate(messageHash, certificate), "bad cert");
    require(!consumed[message.srcChainId][message.txHash][message.logIndex], "used");

    consumed[message.srcChainId][message.txHash][message.logIndex] = true;

    _mint(message.recipient, message.amount);
}
```

The certificate can represent one of:

- M-of-N top reputation watcher signatures
- weighted reputation threshold
- BLS aggregate signature
- GNUS finality certificate
- future compressed or recursive proof

The chat suggested top 256 reputation nodes as the watcher set, with a quorum such as 2/3 weighted reputation or another liveness-friendly threshold. Do not require all 256 nodes.

## RPC Path

RPC should be the first operational path because it is much simpler than RLPx.

Startup catch-up:

```text
load lastProcessedBlock
find finalized/safe/latest-confirmed head
run eth_getLogs in bounded ranges
verify exact logs
save checkpoint
```

Live operation:

```text
poll every X seconds or use eth_subscribe logs
advance only to finalized/safe/confirmed head
verify receipt/log
sign normalized observation
publish to GNUS consensus
```

For range backfill, prefer `eth_getLogs` with address and topic filters. Nodes already use bloom/indexes internally. Manual block-by-block `logsBloom` checks are optional and mainly useful for deterministic checkpoints, provider range limits, or parity with the RLPx path.

Finality policy:

- Ethereum: prefer `finalized`; fallback to `safe`; otherwise use a conservative confirmation depth.
- Base: prefer `finalized` or `safe` if supported; otherwise use a conservative delay.
- Polygon: use confirmations unless checkpoint-aware verification is implemented.
- BSC: use a nonzero confirmation depth.

Do not treat `latest` as final for bridge value release.

## RLPx Path

Keep RLPx, but isolate it as an edge watcher plugin.

RLPx is valuable for:

- decentralized edge event detection
- avoiding direct dependency on centralized RPC services
- advanced watcher nodes that can handle Ethereum P2P complexity

RLPx should not be required for:

- bridge minting
- light client `EthWatch`
- GNUS consensus certificate verification
- destination contract logic

The existing native path maps to:

```text
DevP2P/RLPx -> eth protocol -> NewBlockHashes/GetReceipts/Receipts -> EthWatchService -> EventFilter -> ABI decode -> callback
```

That should become one implementation of the receipt/event source interface.

## Upgrade And Fork Strategy

Ethereum and EVM chains will continue changing protocol versions and block header fields. Keep protocol changes behind adapter boundaries.

Version three separate layers:

```text
TransportVersion
  ETH_RLPX_70
  ETH_RLPX_71
  ETH_RLPX_72
  RPC_LOGS_V1

BridgeEventSchemaVersion
  GNUS_BRIDGE_EVENT_V1
  GNUS_BRIDGE_EVENT_V2

ConsensusCertificateVersion
  GNUS_CONSENSUS_CERT_V1
  GNUS_CONSENSUS_CERT_V2
```

Transport versions must not appear in the mint contract.

Make RLP header decoding fork-aware. Fork rules should be chain-specific and should account for fields such as base fee, withdrawals root, blob gas fields, parent beacon root, and requests hash.

Use dual-run upgrades for protocol transitions:

1. Deploy the new watcher adapter.
2. Run old and new adapters side by side.
3. Compare `txHash`, `logIndex`, and decoded event data.
4. Mark the new adapter healthy after enough matching observations.
5. Shift reputation weight to the new adapter.
6. Deprecate the old adapter later.

Consensus should vote on the normalized event, not on which transport found it.

## Refactor Priorities

1. Preserve `EthWatchService` event filtering, receipt processing, ABI decoding, and callback behavior.
2. Define a transport-neutral receipt/event source interface.
3. Add an RPC/WebSocket source that can feed the same event-processing path.
4. Add normalized `BridgeEventClaim` / `BridgeEventObservation` types.
5. Add signer/verifier and dedupe logic using `srcChainId + txHash + logIndex`.
6. Keep RLPx in an optional edge watcher module.
7. Keep mint/release logic dependent only on GNUS consensus certificates.
8. Add chain-specific finality policy config.
9. Add fork-aware RLP header rules only inside the RLPx adapter.

## Non-Goals

- Do not make every client run DevP2P/RLPx.
- Do not make every node poll paid RPC providers.
- Do not verify raw Ethereum receipt proofs directly in the destination mint contract for the MVP.
- Do not sign raw RLP wire objects as the bridge message.
- Do not couple GNUS consensus to `eth/68`, `eth/69`, `eth/70`, `eth/71`, or future wire protocol versions.

## One-Line Summary

Make `EthWatch` transport-neutral: preserve RLPx as an optional edge watcher, build the bridge around normalized receipt/log claims, and let GNUS consensus certificates be the only proof consumed by mint/release logic.
