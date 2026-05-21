# EVMRelay Refactor Summary

Source: `AgentDocs/Refactor_chat.txt`

## Core Decision

Refactor `evmrelay` so `EthWatch` is no longer tightly coupled to RLPx or the Ethereum `eth/*` wire protocol. The bridge should treat RLPx as one optional event-source backend, while the stable bridge path consumes normalized receipt/log evidence that validators independently verify before voting in SuperGenius consensus.

The target abstraction is:

```text
edge watcher transport -> receipt/log -> normalized bridge event -> validator RPC quorum -> reputation-weighted consensus -> mint / exit review
```

The mint/exit side is driven by normalized event evidence, validator RPC quorum, SuperGenius bridge consensus, and EVM exit review controls.

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
  BridgeEventClaim / BridgeEventEvidence
  EventDeduper
  ReceiptVerifier

evmrelay_rpc_watch
  JsonRpcClient
  WebSocketLogSubscriber
  ChainPoller
  FinalityPolicy
  ReceiptVerifier
  SecurityDecisionBuilder

evmrelay_light_watch
  EthWatch
  ValidatorVotePolicy
  QuorumPolicy
  PubSubSubscriber

evmrelay_consensus_adapter
  ValidatorVotePolicy
  SecurityDecisionVerifier
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
- `GnusCertifiedReceiptSource`: already-certified SuperGenius bridge events

## Bridge Flow

The bridge MVP should use top reputation validators as independent watchers/voters rather than making every node or client run Ethereum P2P.

1. User calls the source-chain bridge contract.
2. Contract emits a specific bridge event, such as `BridgeSourceBurned(...)`.
3. Top reputation ETHWatch nodes observe the event through RPC/WebSocket, own nodes, or optional RLPx.
4. Watchers wait for the chain-specific finality policy.
5. Watchers verify the transaction receipt and exact log.
6. Each validator independently verifies the same event through its configured RPC quorum.
7. Each validator votes only if local policy accepts the event evidence.
8. SuperGenius bridge consensus accepts the event after the bridge-specific weighted reputation threshold, minimum validator count, and trust-domain diversity are satisfied.
9. EVM-to-SuperGenius minting credits the destination UTXO account.
10. SuperGenius-to-EVM exits use the EVM exit contract review/throttle window before external liquidity is released.
11. The event is marked consumed by `srcChainId + txHash + logIndex`.

The wider network can verify only proposed events through free/public RPC calls such as `eth_getTransactionReceipt(txHash)`, which is much cheaper than scanning every block.

## Normalized Event Evidence

The normalized bridge event should be independent of how the event was discovered. It is the subject validators verify and vote on.

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

Use `hash(domainSeparator, BridgeEventClaim)` as the consensus subject / payload identity when needed.

The domain separator should include at least:

- `GNUS_BRIDGE_EVENT_V1`
- source chain ID
- destination chain ID
- bridge contract address

The dedupe/consumption key should be:

```text
srcChainId + txHash + logIndex
```

## Bridge Consensus

The inbound mint path should verify SuperGenius bridge consensus over the normalized event evidence.

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

The certificate or consensus result can represent one of:

- weighted reputation threshold
- GNUS finality certificate
- future compressed or recursive proof

Bridge minting should use a stricter policy than normal network consensus:

```text
effective bridge voting weight >= 90% initially
minimum independent validator count >= 3 initially
minimum operator / RPC trust-domain diversity required
genesis node cannot satisfy threshold alone
high-value or anomalous mints require about 95% effective bridge weight
```

Use an effective bridge-weight cap, such as 33% to 40% per validator, so a high-reputation genesis node remains important without becoming a bridge single point of failure.

## Outbound Rollout

SuperGenius-to-EVM exits should start conservatively while the exit path hardens:

```text
Stage 0:
  EVM exits target testnets only.
  Mainnet exits are handled manually.

Stage 1:
  Manual review for all SuperGenius-to-EVM exits.

Stage 2:
  Automatic exits for <= $100.
  Manual review for > $100.

Stage 3:
  Automatic exits for <= $250.
  Manual review for > $250.

Stage 4:
  Automatic exits with the cool-off / review window enabled.
  Higher-value or anomalous exits continue to require elevated review.
```

Each rollout stage should be an explicit config/governance change with alerts, rollback, per-token/per-chain 24-hour outflow caps, and operator visibility into pending exit review.

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
build security decision
publish validator vote to SuperGenius consensus when local policy accepts
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

BridgeConsensusVersion
  GNUS_BRIDGE_CONSENSUS_V1
  GNUS_BRIDGE_CONSENSUS_V2
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

Consensus should vote on the normalized event evidence, not on which transport found it.

## Refactor Priorities

1. Preserve `EthWatchService` event filtering, receipt processing, ABI decoding, and callback behavior.
2. Define a transport-neutral receipt/event source interface.
3. Add an RPC/WebSocket source that can feed the same event-processing path.
4. Add normalized `BridgeEventClaim` / `SecurityDecision` evidence types.
5. Add validator vote policy, local RPC quorum checks, and dedupe logic using `srcChainId + txHash + logIndex`.
6. Keep RLPx in an optional edge watcher module.
7. Keep mint/exit logic dependent only on SuperGenius bridge consensus and exit review controls.
8. Add chain-specific finality policy config.
9. Add fork-aware RLP header rules only inside the RLPx adapter.

## Non-Goals

- Do not make every client run DevP2P/RLPx.
- Do not make every node poll paid RPC providers.
- Do not verify raw Ethereum receipt proofs directly in the destination mint contract for the MVP.
- Authorize bridge minting through SuperGenius bridge consensus over normalized event evidence.
- Do not couple GNUS consensus to `eth/68`, `eth/69`, `eth/70`, `eth/71`, or future wire protocol versions.

## One-Line Summary

Make `EthWatch` transport-neutral: preserve RLPx as an optional edge watcher, build the bridge around normalized receipt/log evidence, and let SuperGenius bridge consensus plus EVM exit review controls drive mint/exit logic.
