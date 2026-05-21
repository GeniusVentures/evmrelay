To monitor transactions and event logs for specific smart contracts on EVM-compatible chains (Ethereum, Polygon, Base, BSC) as a light client using only C++ without centralized RPC/WebSockets, you’ll implement a P2P client using the DevP2P stack with RLPx as the transport protocol. The goal is to listen for RLP-encoded transaction and block gossip via the `eth` subprotocol, filter for your smart contracts’ activity, and verify block consensus (ensuring blocks are canonical) without participating in consensus. Below is a C++-focused approach tailored to your requirements, avoiding external libraries in other languages and focusing on the execution layer for transactions/logs.

### Approach Overview
- **Light Client**: Connect to each chain’s P2P network, sync block headers for consensus verification, and monitor gossiped transactions/logs for your contract addresses.
- **P2P via RLPx**: Use RLPx over TCP for secure peer communication, handling RLP-encoded `eth` subprotocol messages.
- **Filtering**: Decode transactions and receipts to match your contract addresses and event signatures.
- **Consensus Check**: Verify block headers to ensure monitored data is in canonical blocks.
- **C++ Only**: Implement RLP encoding/decoding, RLPx networking, and `eth` message handling from scratch or with minimal C++ dependencies.

### Implementation Steps
#### 1. Peer Discovery (discv4/Discv5)
- **Purpose**: Find peers for each chain (Ethereum, Polygon, Base, BSC) using UDP-based discovery.
- **Protocol**: Implement discv4 (simpler) or Discv5 (used by some newer chains). Messages include:
  - `PING`/`PONG`: Check peer availability.
  - `FIND_NODE`/`NEIGHBORS`: Query and receive peer lists.
- **Current repo status**: The maintained implementation uses `Boost.Asio` UDP sockets and endpoint/address abstractions. The older ENet/raw-socket sketch below is historical and should not be used as the implementation model.
- **C++ Code**:
  ```cpp
  #include <boost/asio/io_context.hpp>
  #include <boost/asio/ip/udp.hpp>
  #include <vector>
  #include <string>

  struct Node {
      std::string ip;
      uint16_t port;
      std::vector<uint8_t> node_id; // 512-bit public key
  };

  class Discovery {
  public:
      Discovery()
          : socket_(io_, boost::asio::ip::udp::v4())
      {
      }

      void SendPing(const Node& target) {
          std::vector<uint8_t> packet = EncodePing();
          boost::system::error_code ec;
          const auto address = boost::asio::ip::make_address(target.ip, ec);
          if (ec) {
              return;
          }

          const boost::asio::ip::udp::endpoint endpoint(address, target.port);
          socket_.send_to(boost::asio::buffer(packet), endpoint, 0, ec);
      }

      void HandlePacket() {
          // Decode packet (PING, PONG, FIND_NODE, NEIGHBORS)
          // Update peer list if NEIGHBORS received
      }

  private:
      boost::asio::io_context io_;
      boost::asio::ip::udp::socket socket_;
      std::vector<uint8_t> EncodePing() {
          // RLP-encode PING: [version, from, to, expiration, enr_seq]
          // Return serialized bytes
          return std::vector<uint8_t>{/* RLP-encoded data */};
      }
  };
  ```
- **Notes**:
  - Use Boost.Asio for cross-platform UDP networking.
  - RLP-encode messages per DevP2P specs (see below for RLP).
  - Start with chain-specific bootnodes (hardcode from chain docs, e.g., Ethereum’s enodes).
  - Maintain up to `max_active` concurrent dial attempts per chain (default 25 desktop, 3–5 mobile).
  - A `DialScheduler` per chain queues discovered peers and recycles dial slots as connections succeed or fail, mirroring go-ethereum's `dialScheduler` pattern (`maxActiveDials = defaultMaxPendingPeers`).
  - All chain schedulers share a single `boost::asio::io_context` (one thread, cooperative coroutines — no thread-per-chain overhead).
  - A `WatcherPool` owns a **discv4 singleton** (stays warm across chain switches) and enforces a two-level resource cap: `max_total` (global fd limit) and `max_per_chain` (per-chain dial limit). Sensible defaults: mobile `max_total=12, max_per_chain=3`; desktop `max_total=200, max_per_chain=50`.

#### 2. RLPx Connection (TCP)
- **Purpose**: Establish secure TCP connections for `eth` subprotocol gossip.
- **Protocol**: RLPx uses ECIES for handshakes (encryption/auth) and multiplexes subprotocols.
- **Current repo status**: The maintained transport path is `src/rlpx/socket/socket_transport.cpp`, which already uses Boost.Asio TCP sockets and timeout handling. The raw POSIX sketch below is historical and should not be used as the implementation model.
- **C++ Code**:
  ```cpp
  #include <boost/asio/connect.hpp>
  #include <boost/asio/ip/tcp.hpp>
  #include <openssl/ec.h> // For ECIES
  #include <openssl/evp.h>
  #include <vector>

  class RLPxSession {
  public:
      RLPxSession(boost::asio::io_context& io, const Node& peer)
          : socket_(io), peer_(peer) {
          boost::asio::ip::tcp::resolver resolver(io);
          auto endpoints = resolver.resolve(peer.ip, std::to_string(peer.port));
          boost::asio::connect(socket_, endpoints);
          PerformHandshake();
      }

      void PerformHandshake() {
          // ECIES handshake: exchange auth/ack, derive session keys
          EC_KEY* ecdh = EC_KEY_new_by_curve_name(NID_secp256k1);
          // Send auth message, receive ack, compute shared secret
          // Set up encryption (AES-256-GCM) and MAC
      }

      void SendHello() {
          std::vector<uint8_t> hello = EncodeHello();
          boost::asio::write(socket_, boost::asio::buffer(hello));
      }

      void ReceiveMessage() {
          std::vector<uint8_t> buffer(1024);
          const std::size_t len = socket_.read_some(boost::asio::buffer(buffer));
          // Decrypt and decode RLP message (e.g., HELLO, STATUS)
      }

  private:
      boost::asio::ip::tcp::socket socket_;
      Node peer_;
      std::vector<uint8_t> EncodeHello() {
          // RLP-encode HELLO: [protocolVersion, clientId, capabilities, port, id]
          return std::vector<uint8_t>{/* RLP-encoded data */};
      }
  };
  ```
- **Notes**:
  - Use OpenSSL for ECIES (secp256k1 curve) and AES-256-GCM encryption.
  - Negotiate `eth` subprotocol (e.g., `eth/66`) in HELLO message.
  - Handle STATUS message to agree on chain head and genesis hash.

#### 3. RLP Encoding/Decoding
- **Purpose**: Serialize/deserialize transactions, blocks, and receipts.
- **C++ Code**:
  ```cpp
  #include <vector>
  #include <stdexcept>

  class RLP {
  public:
      static std::vector<uint8_t> Encode(const std::vector<uint8_t>& data) {
          if (data.size() == 1 && data[0] < 0x80) {
              return data;
          }
          std::vector<uint8_t> result;
          if (data.size() < 56) {
              result.push_back(0x80 + data.size());
          } else {
              // Encode length as bytes
              std::vector<uint8_t> len_bytes = EncodeLength(data.size());
              result.push_back(0xB7 + len_bytes.size());
              result.insert(result.end(), len_bytes.begin(), len_bytes.end());
          }
          result.insert(result.end(), data.begin(), data.end());
          return result;
      }

      static std::vector<uint8_t> Decode(const std::vector<uint8_t>& data, size_t& offset) {
          if (offset >= data.size()) throw std::runtime_error("Invalid RLP");
          uint8_t first = data[offset];
          if (first < 0x80) {
              offset++;
              return {first};
          } else if (first <= 0xB7) {
              size_t len = first - 0x80;
              offset++;
              std::vector<uint8_t> result(data.begin() + offset, data.begin() + offset + len);
              offset += len;
              return result;
          } else {
              // Handle longer lengths
              throw std::runtime_error("Complex RLP not implemented");
          }
      }

  private:
      static std::vector<uint8_t> EncodeLength(size_t len) {
          std::vector<uint8_t> bytes;
          while (len > 0) {
              bytes.insert(bytes.begin(), len & 0xFF);
              len >>= 8;
          }
          return bytes;
      }
  };
  ```
- **Notes**:
  - Implement RLP per Ethereum specs (simple for single-byte data, recursive for lists).
  - Use for decoding `Transactions`, `BlockHeaders`, `BlockBodies`, and `Receipts`.

#### 4. Light Sync for Consensus
- **Purpose**: Sync block headers to verify canonical chain without full blocks.
- **C++ Code**:
  ```cpp
  class LightClient {
  public:
      void SyncHeaders(const Node& peer, const std::vector<uint8_t>& checkpoint_hash) {
          std::vector<uint8_t> get_headers = EncodeGetBlockHeaders(checkpoint_hash);
          RLPxSession session(peer);
          session.Send(get_headers);
          auto response = session.ReceiveMessage();
          auto headers = DecodeBlockHeaders(response);
          VerifyHeaders(headers); // Check parent hash, consensus rules
      }

  private:
      std::vector<uint8_t> EncodeGetBlockHeaders(const std::vector<uint8_t>& start_hash) {
          // RLP-encode: [msg_id=0x03, [start_hash, max_headers=256, skip=0, reverse=0]]
          return RLP::Encode({/* encoded data */});
      }

      std::vector<std::vector<uint8_t>> DecodeBlockHeaders(const std::vector<uint8_t>& data) {
          size_t offset = 0;
          // Decode BlockHeaders message (msg_id=0x04)
          return {}; // List of headers
      }

      void VerifyHeaders(const std::vector<std::vector<uint8_t>>& headers) {
          // Check parent_hash chain
          // Verify chain-specific consensus (e.g., PoW for BSC, PoS for Polygon)
      }
  };
  ```
- **Notes**:
  - Request headers from a trusted checkpoint (e.g., recent block hash).
  - Verify chain-specific consensus (e.g., Ethereum’s validator signatures, BSC’s PoW).

#### 5. Monitor Transactions/Logs
- **Purpose**: Filter gossiped transactions and logs for your contracts.
- **C++ Code**:
  ```cpp
  #include <vector>
  #include <string>

  class ContractMonitor {
  public:
      ContractMonitor(const std::vector<std::string>& contract_addresses,
                     const std::vector<std::string>& event_signatures)
          : contract_addresses_(contract_addresses), event_signatures_(event_signatures) {}

      void HandleNewPooledTxHashes(const std::vector<uint8_t>& data, RLPxSession& session) {
          size_t offset = 0;
          auto hashes = RLP::Decode(data, offset); // List of tx hashes
          // Request full txns if needed: GetPooledTransactions
          std::vector<uint8_t> get_txns = EncodeGetPooledTransactions(hashes);
          session.Send(get_txns);
      }

      void HandleTransactions(const std::vector<uint8_t>& data) {
          size_t offset = 0;
          auto txns = RLP::Decode(data, offset); // List of RLP-encoded txns
          for (const auto& txn : txns) {
              auto decoded = DecodeTransaction(txn);
              if (decoded.to == contract_addresses_[0]) { // Match contract
                  LogTransaction(decoded);
              }
          }
      }

      void HandleNewBlock(const std::vector<uint8_t>& data, RLPxSession& session) {
          size_t offset = 0;
          auto block = RLP::Decode(data, offset); // [header, txns, uncles]
          auto header = DecodeHeader(block[0]);
          VerifyHeaderConsensus(header); // Ensure canonical
          // Check Bloom filter for contract addresses
          if (BloomFilterMatch(block[0])) {
              std::vector<uint8_t> get_receipts = EncodeGetReceipts(header.hash);
              session.Send(get_receipts);
          }
      }

      void HandleReceipts(const std::vector<uint8_t>& data) {
          size_t offset = 0;
          auto receipts = RLP::Decode(data, offset);
          for (const auto& receipt : receipts) {
              auto logs = DecodeLogs(receipt);
              for (const auto& log : logs) {
                  if (log.address == contract_addresses_[0] && log.topics[0] == event_signatures_[0]) {
                      LogEvent(log);
                  }
              }
          }
      }

  private:
      std::vector<std::string> contract_addresses_;
      std::vector<std::string> event_signatures_;

      bool BloomFilterMatch(const std::vector<uint8_t>& header) {
          // Extract Bloom filter from header, check for contract addresses
          return true; // Placeholder
      }

      void LogTransaction(const std::vector<uint8_t>& txn) {
          // Process/store transaction
      }

      void LogEvent(const std::vector<uint8_t>& log) {
          // Process/store event log
      }
  };
  ```
- **Notes**:
  - Filter transactions by `to` field or input data (function signatures).
  - Filter logs by address and topic[0] (event signature, e.g., keccak256("Transfer(address,address,uint256)")).
  - Use Bloom filters in headers/receipts to reduce `GetReceipts` calls.

#### 6. Cross-Chain Setup
- **Config**: For each chain (Ethereum, Polygon, Base, BSC):
  - Hardcode bootnodes (from chain docs).
  - Set chain ID (Ethereum: 1, Polygon: 137, Base: 8453, BSC: 56).
  - Use `eth/66` (or chain-specific version).
- **Connections**: One `discv4_client` singleton on `WatcherPool` (shared across all chains, stays warm across chain switches). One `DialScheduler` per active chain watcher.
  - `WatcherPool(max_total, max_per_chain)` — two-level resource cap enforced across all schedulers:
    - Mobile defaults: `max_total=12, max_per_chain=3` → up to 4 chains simultaneously, 3 fds each
    - Desktop defaults: `max_total=200, max_per_chain=50`
  - `start_watcher(chain)` — creates `DialScheduler` for that chain, immediately begins consuming discovered peers
  - `stop_watcher(chain)` — **async**, no-block: disconnects all active TCP sessions for that chain; coroutines unwind at next yield, fds freed within one io_context cycle; UI never stutters; freed slots immediately available to a new chain watcher
- **Consensus Rules**:
  - Ethereum: Post-Merge PoS, verify validator signatures.
  - Polygon: PoS, check Heimdall checkpoints.
  - Base: L2, verify L1 state roots.
  - BSC: PoSA, check authority signatures or PoW.

### Challenges and Mitigations
- **Resource Use**: Two-level cap via `WatcherPool(max_total, max_per_chain)`. All coroutines share one `io_context` thread — zero thread overhead per chain. On desktop raise fd limit via `setrlimit(RLIMIT_NOFILE)` at startup. On mobile the low `max_total` keeps fd usage negligible and battery impact minimal. Redundancy is collective via IPFS pubsub — each device only needs a few stable peers per chain.
- **RLP Complexity**: Implement recursive RLP decoding for lists (blocks, receipts).
- **Peer Reliability**: Handle dropped connections with reconnect logic; maintain diverse peers.
- **Chain Quirks**: Test on testnets (Sepolia, Amoy, Base Sepolia, BSC Testnet) for chain-specific behaviors.
- **Log Overhead**: Use Bloom filters to skip irrelevant blocks/receipts.

### Workflow
1. Initialize Discovery for each chain with bootnodes.
2. Connect to peers via RLPx, negotiate `eth` subprotocol.
3. Sync headers from a trusted checkpoint to track chain tip.
4. Listen for `NewPooledTransactionHashes`, `Transactions`, `NewBlock` messages.
5. Filter transactions for your contract addresses; fetch receipts for logs if Bloom filter matches.
6. Verify block headers for consensus (canonical chain).
7. Log/process matched transactions/logs.

This C++ implementation ensures decentralized monitoring of your smart contracts across EVM chains using Boost.Asio-based discovery/RLPx transport plus OpenSSL-backed crypto. If you need specific message formats or chain bootnodes, let me know!




---

## discv5 Module (added 2026-03-15)

A parallel discovery stack that locates Ethereum-compatible peers via the discv5 protocol (EIP-8020).  It is deliberately additive — the existing discv4 stack and all RLPx/ETH code remain unchanged.

### High-level data flow

```
BootnodeSource (ENR or enode URIs)
       │
       ▼
discv5_crawler
  ├── queued_peers_    — next FINDNODE targets
  ├── measured_ids_    — nodes that replied
  ├── failed_ids_      — nodes that timed out
  └── discovered_ids_  — dedup set
       │
       │  PeerDiscoveredCallback (ValidatedPeer)
       ▼
discovery::ValidatedPeer  ← shared with discv4 via include/discovery/discovered_peer.hpp
       │
       ▼
DialScheduler / RLPx session (existing, unchanged)
```

### New files

```
include/
  discovery/
    discovered_peer.hpp       — shared NodeId / ForkId / ValidatedPeer
  discv5/
    discv5_constants.hpp      — all domain sizes + wire POD structs
    discv5_error.hpp          — discv5Error enum
    discv5_types.hpp          — EnrRecord, discv5Config, callbacks
    discv5_enr.hpp            — EnrParser (decode, verify, to_validated_peer)
    discv5_crawler.hpp        — peer queue state machine
    discv5_client.hpp         — UDP socket + async loops

src/discv5/
  discv5_error.cpp
  discv5_enr.cpp              — base64url, RLP, secp256k1 signature verify
  discv5_crawler.cpp          — enqueue/dedup/emit
  discv5_client.cpp           — Boost.Asio spawn/yield_context receive + crawler loops, FINDNODE send
  CMakeLists.txt

test/discv5/
  discv5_enr_test.cpp         — go-ethereum test vectors
  discv5_crawler_test.cpp     — deterministic state machine tests
  CMakeLists.txt

examples/discv5_crawl/
  discv5_crawl.cpp            — live C++ example / functional test harness entry point
  CMakeLists.txt
```

### Functional testing note

Discovery functional testing in this repository is done through C++ example programs under `examples/`, not shell wrappers.

The working discv4 reference pattern is `examples/discovery/test_enr_survey.cpp`: a standalone C++ example that drives a bounded live run, gathers counters in memory, and prints a structured report at the end.

`examples/discv5_crawl/discv5_crawl.cpp` should be treated as the corresponding discv5 functional-testing entry point. At the current checkpoint it is still a partial live harness because the packet receive path does not yet decode the full discv5 WHOAREYOU / handshake / NODES flow. Once that path is implemented, this example should provide the same kind of end-of-run C++ diagnostic summary as `test_enr_survey.cpp`.

### Wire-format structs (M014)

All packet-size constants are derived from `sizeof(WireStruct)`, never bare literals:

| Struct | Size | Constant |
|---|---|---|
| `IPv4Wire` | 4 B | `kIPv4Bytes` |
| `IPv6Wire` | 16 B | `kIPv6Bytes` |
| `MaskingIvWire` | 16 B | `kMaskingIvBytes` |
| `GcmNonceWire` | 12 B | `kGcmNonceBytes` |
| `StaticHeaderWire` | 23 B | `kStaticHeaderBytes` |
| `EnrSigWire` | 64 B | `kEnrSigBytes` |
| `CompressedPubKeyWire` | 33 B | `kCompressedKeyBytes` |
| `UncompressedPubKeyWire` | 65 B | `kUncompressedKeyBytes` |

### Supported Chains

Supported chain metadata is data-driven. `chain_enodes.json(.gz)` carries
network IDs, genesis hashes, fork IDs, nodes, and bootnodes. `chains_config.json`
carries discovery defaults and ENR-tree roots.
| Polygon mainnet | 137 | enode (docs.polygon.technology) |
| Polygon Amoy | 80002 | enode (docs.polygon.technology) |
| BSC mainnet | 56 | enode (bnb-chain/bsc params/config.go) |
| BSC testnet | 97 | enode (bnb-chain/bsc params/config.go) |
| Base mainnet | 8453 | OP Stack — inject at runtime |
| Base Sepolia | 84532 | OP Stack — inject at runtime |

### Next sprint

1. Implement the minimal WHOAREYOU / HANDSHAKE session layer required for live discv5 peers to accept queries.
2. Decode incoming NODES responses and feed decoded peers back into the crawler / callback path.
3. Make `examples/discv5_crawl/discv5_crawl.cpp` behave like a real example-based functional test, following the same C++ pattern already used by `examples/discovery/test_enr_survey.cpp`: bounded run, in-memory counters, final structured report.
4. Once the example proves live peer discovery end-to-end, wire `discv5_client` as an alternative to `discv4_client` inside `eth_watch`.
