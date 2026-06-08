<!-- refreshed: 2026-05-25 -->
# Architecture

**Analysis Date:** 2026-05-25 (updated 2026-05-25)

## Scope & Consumer Boundary

**evmrelay** is a C++17 static library with three core responsibilities:

1. **Watcher Service** — Ethereum P2P peer discovery (discv4/discv5), RLPx encrypted transport, ETH subprotocol event watching (Bloom prefilter, receipt request, log match, ABI decode), and bridge event normalization.
2. **Public RPC List Provider** — Ingestion of public RPC endpoint metadata from chain lists (`chainid.network/chains.json`), endpoint discovery, and metadata curation.
3. **RPC Connection Maker** — Multi-endpoint RPC pool management (`RpcManager`), HTTP transport (`RpcHttpTransport`), endpoint health tracking, rate limiting, and receipt fetching via JSON-RPC.

**Consumer:** The **SuperGenius** parent repository (`src/watcher/`) consumes evmrelay as a library. `src/watcher/` is the **bridge orchestrator** — it receives verified observations from evmrelay, manages the message handling lifecycle, and coordinates mint verification. `src/account/` code uses evmrelay RPC endpoints to verify stored messages before constructing mint transactions.

**What evmrelay does NOT do:** evmrelay does not decide what happens with observed events (minting, escrow release, validator voting). It does not construct transactions, manage UTXO state, or maintain bridge consensus state machines. These concerns belong to the SuperGenius parent repository.

## System Overview

```text
┌─────────────────────────────────────────────────────────────────────┐
│                     Application / CLI Entry                          │
│              `examples/eth_watch/eth_watch.cpp`                     │
│              `examples/discovery/test_enr_survey.cpp`               │
│              `examples/discv5_crawl/discv5_crawl.cpp`               │
├──────────────────┬──────────────────┬──────────────────────────────┤
│   EthWatchRunner │  EthWatchService │  EventFilter / ABI Decoder   │
│  `include/eth/   │  `include/eth/   │  `include/eth/event_filter   │
│   eth_watch_     │   eth_watch_     │   _*.hpp`                    │
│   runner.hpp`    │   service.hpp`   │  `include/eth/abi_decoder.hpp│
├──────────────────┴──────────────────┴──────────────────────────────┤
│                         ETH Subprotocol                             │
│    `include/eth/eth_peer_session.hpp`  `include/eth/messages.hpp`  │
│    `include/eth/eth_handshake.hpp`     `include/eth/eth_types.hpp` │
├─────────────────────────────────────────────────────────────────────┤
│                     RLPx Transport (TCP + Encryption)                │
│    `include/rlpx/rlpx_session.hpp`  — session lifecycle             │
│    ┌───────────┬──────────────┬───────────────┬──────────────────┐  │
│    │ auth/     │ crypto/      │ framing/      │ protocol/         │  │
│    │ ECIES     │ AES, ECDH,   │ FrameCipher,  │ Hello, Disconnect, │  │
│    │ handshake │ HMAC, KDF    │ MessageStream │ Ping, Pong        │  │
│    └───────────┴──────────────┴───────────────┴──────────────────┘  │
├─────────────────────────────────────────────────────────────────────┤
│            Peer Discovery (UDP)                                      │
│  `include/discv4/discv4_client.hpp`  `include/discv5/discv5_client. │
│                                       hpp`                          │
│  discv4: PING/PONG/FINDNODE/NEIGHBORS  discv5: FINDNODE/NODES/ENR  │
├─────────────────────────────────────────────────────────────────────┤
│                   RLP Encoding/Decoding                              │
│    `include/rlp/rlp_encoder.hpp`  `include/rlp/rlp_decoder.hpp`    │
│    `include/rlp/rlp_streaming.hpp` (large-payload support)          │
├─────────────────────────────────────────────────────────────────────┤
│                   Base Utilities                                     │
│    `include/base/parse_utility.hpp`  `include/base/json_utility.hpp`│
│    `include/base/byte_encoding.hpp`  `include/base/rlp-logger.hpp`  │
└─────────────────────────────────────────────────────────────────────┘
```

## Component Responsibilities

| Component | Responsibility | File |
|-----------|----------------|------|
| RLP Encoder/Decoder | Canonical Ethereum RLP serialization/deserialization | `src/rlp/rlp_encoder.cpp`, `src/rlp/rlp_decoder.cpp` |
| RLPx Session | Encrypted P2P session lifecycle (TCP connect, ECIES auth, HELLO, subprotocol negotiation) | `src/rlpx/rlpx_session.cpp` |
| Auth Handshake | ECIES key exchange, auth/ack message construction | `src/rlpx/auth/*.cpp` |
| Crypto Primitives | AES-256-CTR encryption, ECDH key agreement, HMAC-SHA256, KDF | `src/rlpx/crypto/*.cpp` |
| Framing/MessageStream | Frame encryption, MAC, Snappy compression, message send/receive | `src/rlpx/framing/*.cpp` |
| discv4 Client | UDP-based discovery v4 peer discovery (PING/PONG/FINDNODE/NEIGHBORS) | `src/discv4/discv4_client.cpp` |
| discv5 Client | UDP-based discovery v5 peer discovery (ENR-based, FINDNODE/NODES) | `src/discv5/discv5_client.cpp` |
| DialScheduler | Per-chain connection slot management with two-level resource cap | `include/discv4/dial_scheduler.hpp` |
| EthPeerSession | ETH/66-69 Status handshake builder and validator | `src/eth/eth_peer_session.cpp` |
| EthWatchRunner | Per-session wrapper: RLPx session + EthWatchService + event callback bridging | `src/eth/eth_watch_runner.cpp` |
| EthWatchService | Core event subscription engine: Bloom filter → receipt request → log match → ABI decode → callback to consumer | `src/eth/eth_watch_service.cpp` |
| EventFilter | Contract address / event signature filtering with Bloom prefilter | `src/eth/event_filter.cpp` |
| ABI Decoder | Decode EVM ABI-encoded event log data/topics into typed values | `src/eth/abi_decoder.cpp` |
| ChainTracker | Block deduplication and chain tip tracking | `src/eth/chain_tracker.cpp` |
| FinalityPolicy | Chain-specific finality head selection (`finalized`/`safe`/`latest`) | `src/eth/finality_policy.cpp` |
| RPC Manager | **RPC connection maker:** endpoint pool management with priority/weight/rate-limit/health | `src/eth/rpc_manager.cpp` |
| RPC HTTP Transport | JSON-RPC HTTP transport for receipt fetching | `src/eth/rpc_http_transport.cpp` |
| RPC Receipt Source | Receipt retrieval via JSON-RPC (alternative to P2P-sourced receipts) | `src/eth/rpc_receipt_source.cpp` |
| Bridge Event Types | Bridge-specific event normalization, claims, observations, dedup keys, receipt verification | `src/eth/bridge_event.cpp` |
| ChainPeers | Loads cached peer data from `chain_enodes.json.gz` | `src/discv4/chain_peers.cpp` |
| Base Utilities | Hex parsing, JSON schema validation, byte encoding, logging | `src/base/*.cpp` |

## Pattern Overview

**Overall:** Layered protocol stack with Boost.Asio coroutines

**Key Characteristics:**
- C++17, Boost.Asio stackful coroutines (`spawn`/`yield_context`) for all async I/O
- Boost.Outcome (`outcome::result<T>`) for error propagation — no exceptions in hot paths
- Interface/Adapter pattern at layer boundaries (e.g., `IEthSessionChannel` between ETH and RLPx)
- Strategy pattern via `std::function` callbacks for flexible event dispatch
- Resource pooling: `WatcherPool` enforces two-level cap (total + per-chain) for file descriptors
- Data-driven configuration: chain metadata, bootnodes, ENR trees loaded from JSON (`chains_config.json`, `chain_enodes.json.gz`)
- No global singletons; dependency injection via constructor parameters
- Separation of domain types per module (e.g., RLP types distinct from ETH types)

## Layers

**Base Utilities Layer:**
- Purpose: Shared parsing, encoding, logging, JSON schema validation
- Location: `src/base/`, `include/base/`
- Contains: `parse_utility.hpp` (hex/uint parsing), `json_utility.hpp` (schema-driven JSON), `byte_encoding.hpp`, `rlp-logger.hpp`
- Depends on: Boost.JSON, spdlog
- Used by: All other layers

**RLP Layer:**
- Purpose: Canonical Ethereum RLP encoding/decoding for all data structures
- Location: `src/rlp/`, `include/rlp/`
- Contains: `RlpEncoder`, `RlpDecoder`, streaming encoder/decoder, Ethereum-specific types (Hash256, Address, Bloom, Transaction, BlockHeader), `intx::uint256`
- Depends on: Boost.Core (span), Boost.Outcome
- Used by: RLPx, ETH, discv4, discv5

**RLPx Layer:**
- Purpose: Encrypted P2P transport over TCP (DevP2P transport layer)
- Location: `src/rlpx/`, `include/rlpx/`
- Contains sub-layers:
  - `auth/` — ECIES handshake (`include/rlpx/auth/auth_handshake.hpp`, `auth_keys.hpp`, `ecies_cipher.hpp`)
  - `crypto/` — AES-256-CTR, ECDH (secp256k1), HMAC-SHA256, KDF (`include/rlpx/crypto/aes.hpp`, `ecdh.hpp`, `hmac.hpp`, `kdf.hpp`)
  - `framing/` — Frame encryption/MAC (`FrameCipher`), message send/receive with compression (`MessageStream`)
  - `protocol/` — RLPx protocol messages (Hello, Disconnect, Ping, Pong) (`include/rlpx/protocol/messages.hpp`)
  - `socket/` — TCP socket wrapper with timeout (`include/rlpx/socket/socket_transport.hpp`)
- Depends on: RLP (for message encoding/decoding), OpenSSL, libsecp256k1, Boost.Asio, Snappy, ZLIB
- Used by: ETH layer, DialScheduler

**Discovery Layer:**
- Purpose: Find Ethereum peers via UDP-based Kademlia-like protocols
- Location: `src/discv4/`, `src/discv5/`, `include/discv4/`, `include/discv5/`, `include/discovery/`
- Contains:
  - `discv4_client` — Discovery v4 (PING/PONG/FINDNODE/NEIGHBORS, packet hashing with Keccak-256)
  - `discv5_client` — Discovery v5 (ENR-based, FINDNODE/NODES, WHOAREYOU handshake)
  - `DialScheduler` — Manages dial slots per chain, enforces `WatcherPool` limits
  - `ChainPeers` — Loads cached peer data from `chain_enodes.json.gz`
  - `discovery::ValidatedPeer` — Shared handoff type (`include/discovery/discovered_peer.hpp`)
- Depends on: RLP, RLPx types, Boost.Asio, libsecp256k1
- Used by: ETH layer (peer source for `eth_watch`)

**ETH Layer:**
- Purpose: Ethereum wire protocol (ETH/66-69) message handling, event watching, ABI decoding
- Location: `src/eth/`, `include/eth/`
- Contains:
  - `EthPeerSession` — ETH Status handshake (build/validate Status messages for ETH/68-69)
  - `EthWatchService` — Event subscription engine with block/receipt/log processing pipeline
  - `EthWatchRunner` — Combines RLPx session with watch service per peer
  - `EventFilter` — Contract address + event topic matching with Bloom prefilter
  - `AbiDecoder` — Decodes EVM ABI-encoded event parameters into typed values
  - `ChainTracker` — Blocks-seen deduplication and chain tip tracking
  - `FinalityPolicy` — Chooses finality head per chain (`finalized`/`safe`/`latest`)
  - `RpcManager` — Alternative receipt source via JSON-RPC HTTP endpoints
  - `BridgeEvent` / `BridgeObservation` — Bridge-specific event normalization
  - `IEthSessionChannel` — Interface separating ETH logic from concrete RLPx session
- Depends on: RLPx, RLP, discv4 types, Boost.Asio, Boost.Outcome
- Used by: Application entry points (`eth_watch.cpp`)

## Data Flow

### Primary Event-Watching Path

1. **Bootnode/Peer Load** — Load chain config and bootnodes from `chains_config.json` / `chain_enodes.json.gz` → populate discv4 or discv5 client (`examples/eth_watch/eth_watch.cpp` config loading)
2. **Peer Discovery** — discv4_client sends PING to bootnodes, receives PONG, sends FINDNODE, receives NEIGHBORS → emits `ValidatedPeer` (`src/discv4/discv4_client.cpp`)
3. **Dial Scheduling** — `DialScheduler` dequeues `ValidatedPeer`, checks `WatcherPool` limits, spawns coroutine per dial (`include/discv4/dial_scheduler.hpp`)
4. **RLPx Connection** — `RlpxSession::connect()` performs TCP connect + ECIES auth handshake + HELLO exchange + subprotocol capability negotiation (`src/rlpx/rlpx_session.cpp`)
5. **ETH Status Handshake** — `EthPeerSession::StartEthStatusHandshake()` sends local ETH Status, validates remote Status (network ID, genesis hash, fork ID) (`src/eth/eth_peer_session.cpp`)
6. **Watch Installation** — `EthWatchRunner` sets ETH message handler on the session, bridges incoming ETH messages into `EthWatchService` (`src/eth/eth_watch_runner.cpp`)
7. **Block/Transaction Reception** — Peer sends `NewBlockHashes`, `NewBlock`, or `NewPooledTransactionHashes` → ETH message handler routes to `EthWatchService`
8. **Bloom Prefilter** — `EventFilter::may_match_block()` checks `logsBloom` for watched contract addresses/topics (`src/eth/event_filter.cpp`)
9. **Receipt Request** — If Bloom matches, `EthWatchService` sends `GetReceipts` for the block via the RLPx session
10. **Log Matching** — `EventFilter::match_logs_in_receipts()` checks receipt logs for exact address + topic[0] match (`src/eth/event_filter.cpp`)
11. **ABI Decoding** — `AbiDecoder::decode_event()` decodes log data/topics into typed values (`src/eth/abi_decoder.cpp`)
12. **Callback Dispatch** — `WatchEventNotificationCallback` is invoked with enriched context (chain, peer, decoded values) → delivered to the SuperGenius `src/watcher/` bridge orchestrator for message handling (`EthWatchRunner::notify_event()`)

### Receipt Retrieval (Alternative Path)

1. `RpcManager` selects an available RPC endpoint for the chain using priority/weight/rate-limit (`src/eth/rpc_manager.cpp`)
2. `RpcHttpTransport` sends `eth_getTransactionReceipt` JSON-RPC request (`src/eth/rpc_http_transport.cpp`)
3. Response is schema-validated via `JsonSchemaObject` and parsed into `TransactionReceipt`
4. `RpcReceiptSource` provides receipts to `EthWatchService` as an alternative to P2P-sourced receipts
5. The parent SuperGenius `src/watcher/` orchestrator (and `src/account/` mint code) can also use evmrelay's `RpcManager` directly for independent receipt verification via HTTP RPC calls

### Consumer Integration Boundary

The callback dispatch at step 12 is the primary handoff boundary:
- **evmrelay** produces `WatchEventNotification` / `BridgeEventClaim` / `BridgeEventObservation` types
- **SuperGenius `src/watcher/`** consumes these types and orchestrates the message handling lifecycle (dedup, verification, mint triggering)
- **SuperGenius `src/account/`** uses evmrelay's `RpcManager` / `RpcReceiptSource` to independently verify bridge events via multiple RPC endpoints before constructing mint transactions

## Key Abstractions

**IEthSessionChannel:**
- Purpose: Stable interface between ETH logic and RLPx session — enables mocking in tests
- Definition: `include/eth/eth_session_channel.hpp`
- Implementation: `RlpxEthSessionChannel` (adapter wrapping `std::shared_ptr<rlpx::RlpxSession>`)
- Pattern: Interface/Adapter (GoF)

**ValidatedPeer (discovery handoff):**
- Purpose: Protocol-agnostic peer descriptor shared between discv4 and discv5 crawlers and the DialScheduler
- Definition: `include/discovery/discovered_peer.hpp`
- Contains: NodeId (64-byte pubkey), IP, UDP/TCP ports, optional ForkId
- Pattern: Value Object / DTO

**outcome::result<T, E>:**
- Purpose: Error propagation without exceptions across all layers
- Definition: `include/rlp/result.hpp` (type aliases for RLP layer), `include/rlpx/rlpx_error.hpp` (RLPx-specific errors)
- Pattern: Either monad (Boost.Outcome)

**WatcherPool / DialScheduler:**
- Purpose: Two-level resource cap (total global connections + per-chain connections) with atomic counter
- Definition: `include/discv4/dial_scheduler.hpp`
- Pattern: Resource Pool with Strategy callback (`DialFn`)

**JsonSchemaObject / JsonSchemaArray:**
- Purpose: Schema-driven JSON parsing with required/optional/default handling and typed access
- Definition: `include/base/json_utility.hpp`
- Pattern: Builder (schema definition) + Interpreter (parsing)

## Entry Points

**eth_watch CLI:**
- Location: `examples/eth_watch/eth_watch.cpp`
- Triggers: Command-line invocation with chain/watching parameters
- Responsibilities: Parse CLI arguments, load chain config, initialize discv4 or discv5 client, create `DialScheduler`, spawn watch coroutines, dispatch matched events via callbacks
- Note: This example demonstrates evmrelay's watcher service. In production, the SuperGenius `src/watcher/` orchestrator uses evmrelay as a library (not the example binary) and receives events via programmatic callbacks.

**test_enr_survey (discv4 functional harness):**
- Location: `examples/discovery/test_enr_survey.cpp`
- Triggers: Command-line invocation for discv4 discovery validation
- Responsibilities: Bounded live discovery run, ENR enrichment, structured end-of-run diagnostic report

**discv5_crawl (discv5 functional harness):**
- Location: `examples/discv5_crawl/discv5_crawl.cpp`
- Triggers: Command-line invocation for discv5 discovery validation
- Responsibilities: Live discv5 crawl, peer discovery, end-of-run summary (currently partial — WHOAREYOU/NODES decode pending)

**rlp_enodes (Go tool):**
- Location: `rlp_enodes/main.go`
- Triggers: Pre-build step to generate chain peer data
- Responsibilities: Crawl discv4 network, collect ENR records, produce signed `chain_enodes.json.gz` consumed by the C++ library at runtime

## Architectural Constraints

- **Threading:** Single-threaded event loop via `boost::asio::io_context`. All coroutines share one thread. No thread-per-chain overhead. All public API calls must come from the io_context thread.
- **Global state:** No module-level singletons. `discv4_client` is explicitly created and owned per use. Config is passed by value. `WatcherPool` uses `std::atomic<int>` for the global connection counter but is itself explicitly owned.
- **Circular imports:** None detected. Includes follow strict layering: base ← rlp ← rlpx ← eth. `discv4`/`discv5` depend on rlp and rlpx types but not on eth.
- **C++ standard:** C++17 only. No C++20 features (no designated initializers, no coroutines TS). `boost::asio::spawn` provides stackful coroutines.
- **Header guards:** `#ifndef`/`#define`/`#endif` throughout — no `#pragma once`.

## Anti-Patterns

### Direct Boost.JSON Use (Legacy)

**What happens:** Some code accesses Boost.JSON directly via `pt.as_object()`, `pt.at("key")`, manual iteration rather than through `JsonSchemaObject`/`JsonSchemaArray`.
**Why it's wrong:** Bypasses schema validation, duplicates parsing logic, harder to produce meaningful error paths.
**Do this instead:** Use `rlp::base::json::JsonSchemaObject` / `JsonSchemaArray` for all ordinary JSON object/item loading. Direct Boost.JSON access is permitted only for intentionally dynamic shapes (e.g., selecting a chain entry by runtime name, preserving unsigned JSON for signature verification).

### Domain Types in Core Libraries

**What happens:** RLP core (`rlp_ethereum.hpp`) contains Ethereum-specific types (Address, Hash256, Transaction, BlockHeader) mixed into the generic RLP library.
**Why it's wrong:** Tight coupling — a generic RLP library should not force import of Ethereum domain types.
**Do this instead:** RLP core (`common.hpp`, `rlp_encoder.hpp`, `rlp_decoder.hpp`) is independent. Ethereum types live in `rlp_ethereum.hpp` as an optional extension. Users only include what they need.

## Error Handling

**Strategy:** `outcome::result<T, ErrorEnum>` for all I/O and parsing operations. No exceptions in hot paths.

**Patterns:**
- `BOOST_OUTCOME_TRY(var, expr)` macro for early-return on error
- Error enums scoped per layer: `EncodingError`, `DecodingError` (RLP), `SessionError`, `AuthError`, `FramingError` (RLPx), `RpcEndpointError` (ETH)
- `VoidResult` = `outcome::result<void, SessionError>` for operations with no return value

**Error propagation flow:**
1. Low-level error (e.g., `SessionError::kNetworkFailure` from socket read)
2. Propagated up via `BOOST_OUTCOME_TRY` through RLPx session → EthWatchRunner → application callback
3. Application layer logs error and may retry or reconnect

## Cross-Cutting Concerns

**Logging:** `spdlog` via `rlp::base::Logger` / `rlp::base::createLogger()` — logger instances are passed through constructors, not globally accessed.

**Validation:** Schema-driven for JSON (`JsonSchemaObject`), type-safe for RLP (`RlpDecoder::IsList()`, `PeekHeader()`), protocol-level for ETH Status messages.

**Authentication:** ECIES handshake with secp256k1 keys for RLPx. Recovery ID verification on auth messages. ENR signature verification for discv5.

**Configuration:** All chain-specific data (network IDs, genesis hashes, fork IDs, bootnodes, ENR trees, RPC endpoints) is loaded from external JSON files at runtime. No hardcoded chain data in C++ source.

---

*Architecture analysis: 2026-05-25*
