# Codebase Structure

**Analysis Date:** 2026-05-25

## Directory Layout

```
evmrelay/
├── CMakeLists.txt              # Top-level build: project definition, dependency discovery, subdirectory dispatch
├── cmake/                      # Build system support
│   ├── toolchain/cxx17.cmake   # C++17 toolchain config
│   ├── Sanitizers.cmake        # ASan/TSan/UBSan support
│   ├── Valgrind.cmake          # Valgrind integration
│   ├── config.cmake.in         # CMake package config template
│   └── CompilationFlags.cmake  # Warning/optimization flags
├── src/                        # Library source code
│   ├── CMakeLists.txt          # evmrelay static library target aggregation
│   ├── base/                   # Shared utilities (logging, JSON parsing, hex parsing)
│   ├── rlp/                    # RLP encoder/decoder (Ethereum data serialization)
│   ├── rlpx/                   # RLPx P2P transport (TCP + encryption)
│   │   ├── auth/               # ECIES handshake
│   │   ├── crypto/             # AES, ECDH, HMAC, KDF
│   │   ├── framing/            # Frame cipher, message stream
│   │   ├── protocol/           # RLPx protocol messages
│   │   └── socket/             # TCP socket wrapper
│   ├── discv4/                 # Discovery v4 (UDP peer discovery)
│   ├── discv5/                 # Discovery v5 (ENR-based peer discovery)
│   └── eth/                    # ETH subprotocol, event watching, ABI decoding
├── include/                    # Public headers (mirrors src/ structure)
│   ├── base/
│   ├── rlp/
│   ├── rlpx/
│   │   ├── auth/
│   │   ├── crypto/
│   │   ├── framing/
│   │   ├── protocol/
│   │   └── socket/
│   ├── discv4/
│   ├── discv5/
│   ├── eth/
│   └── discovery/              # Shared types between discv4 and discv5
│       └── discovered_peer.hpp
├── test/                       # Unit tests (mirrors src/ structure)
│   ├── CMakeLists.txt
│   ├── rlp/                    # RLP encoder/decoder/streaming tests
│   ├── rlpx/                   # RLPx session/crypto/framing tests
│   ├── discv4/                 # discv4 client/dial/scheduler tests
│   ├── discv5/                 # discv5 crawler/ENR/client tests
│   ├── eth/                    # ETH handshake/watch/service/RPC tests
│   └── fuzz/                   # Fuzz tests (optional, ENABLE_FUZZING)
├── examples/                   # Standalone C++ example programs / functional harnesses
│   ├── CMakeLists.txt
│   ├── eth_watch/              # eth_watch CLI example (main entry point)
│   ├── discovery/              # discv4 ENR survey functional harness
│   ├── discv5_crawl/           # discv5 crawl functional harness
│   ├── chains_config.json      # Chain configuration (network IDs, genesis, bootnodes)
│   └── chain_config.hpp        # C++ header auto-generated from chains_config.json
├── rlp_enodes/                 # Go tool: discv4 crawler → chain_enodes.json.gz
│   ├── main.go
│   ├── app.go
│   ├── types.go
│   ├── config/                 # Go config loading (mirrors C++ schema)
│   └── output/                 # Generated signed chain_enodes.json.gz
├── scripts/                    # Coverage analysis scripts
│   ├── coverage_analysis.sh
│   └── coverage_analysis.bat
├── docs/                       # Documentation
│   └── rlpx/                   # RLPx protocol specification docs
├── go-ethereum/                # Git submodule: go-ethereum reference (test data, protocol validation)
├── AgentDocs/                  # AI agent development docs (SPRINT_PLAN, CHECKPOINT, Architecture, etc.)
├── .clang-format               # Code formatting rules
├── .clang-tidy                 # Static analysis config
├── .clangd                     # LSP config
├── .gitmodules                 # git submodules (go-ethereum)
├── build/                      # Build output (per platform/build type)
│   ├── OSX/Debug/
│   ├── OSX/Release/
│   └── ...
├── .planning/                  # GSD planning artifacts
│   └── codebase/               # Codebase mapping documents (this file)
└── LICENSE.txt
```

## Directory Purposes

**`src/`:**
- Purpose: All library implementation code compiled into the `evmrelay` static library
- Contains: `.cpp` source files organized by module
- Key files: `src/CMakeLists.txt` (aggregates object libraries into `libevmrelay.a`)

**`include/`:**
- Purpose: Public header files exported to consumers of the library
- Contains: `.hpp` headers organized by module, mirroring `src/` layout
- Key files: Module entry headers (`rlp/rlp_encoder.hpp`, `rlpx/rlpx_session.hpp`, `eth/eth_watch_service.hpp`, etc.)

**`test/`:**
- Purpose: Google Test unit tests, one test binary per module
- Contains: `*_test.cpp` files matching source module names
- Key files: `test/CMakeLists.txt` (CTest registration), module-specific test CMakeLists

**`examples/`:**
- Purpose: Standalone C++ programs demonstrating library usage and serving as functional test harnesses
- Contains: Entry-point `.cpp` files, their own `CMakeLists.txt`
- Key files: `examples/eth_watch/eth_watch.cpp` (primary CLI), `examples/discovery/test_enr_survey.cpp` (discv4 validation), `examples/chains_config.json` (chain data)

**`rlp_enodes/`:**
- Purpose: Go-based discv4 network crawler that produces `chain_enodes.json.gz` consumed by the C++ library
- Contains: Go source, config files, output directory for generated data
- Key files: `main.go`, `config/` (schema), `output/chain_enodes.json.gz`

**`cmake/`:**
- Purpose: Build system support (toolchain files, compiler flags, packaging)
- Contains: `.cmake` include files
- Key files: `toolchain/cxx17.cmake`, `Sanitizers.cmake`

**`AgentDocs/`:**
- Purpose: AI agent development context (sprint plans, checkpoints, architecture notes, refactor summaries)
- Contains: `.md` documents, not compiled
- Key files: `Architecture.md`, `SPRINT_PLAN.md`, `CHECKPOINT.md`, `REFACTOR_SUMMARY.md`

**`go-ethereum/`:**
- Purpose: Git submodule providing reference go-ethereum implementation for test data and protocol validation
- Contains: Full go-ethereum source tree
- Key files: Test vectors in `go-ethereum/eth/tracers/internal/tracetest/testdata/`, RLPx protocol reference

## Key File Locations

**Entry Points:**
- `examples/eth_watch/eth_watch.cpp`: Primary CLI application for multi-chain event watching
- `examples/discovery/test_enr_survey.cpp`: discv4 ENR survey functional test harness
- `examples/discv5_crawl/discv5_crawl.cpp`: discv5 crawl functional test harness
- `examples/discovery/test_discovery.cpp`: General discovery test program
- `examples/discovery/test_discv5_connect.cpp`: discv5 connectivity test

**Build Configuration:**
- `CMakeLists.txt`: Top-level project definition (version 1.0.0, C++17, dependency discovery)
- `src/CMakeLists.txt`: Static library aggregation (object libs → `libevmrelay.a`)
- `test/CMakeLists.txt`: Test subdirectory dispatch and CTest registration
- `cmake/toolchain/cxx17.cmake`: Default toolchain (C++17 standard)
- `cmake/Sanitizers.cmake`: Address/Thread/Undefined sanitizer flags
- `.clang-format`: Code style (Microsoft-based, 4-space indent, 120 char line limit)
- `.clang-tidy`: Static analysis checks

**Core Logic — RLP:**
- `include/rlp/rlp_encoder.hpp` / `src/rlp/rlp_encoder.cpp`: RLP encoding with BeginList/EndList, templates for all types
- `include/rlp/rlp_decoder.hpp` / `src/rlp/rlp_decoder.cpp`: RLP decoding with Peek/Read/consume, static convenience `decode<T>()`
- `include/rlp/rlp_streaming.hpp` / `src/rlp/rlp_streaming.cpp`: Large-payload streaming encoder/decoder (contract bytecode, calldata)
- `include/rlp/rlp_ethereum.hpp`: Ethereum-specific RLP types (Hash256, Address, Transaction, BlockHeader, Bloom)
- `include/rlp/result.hpp`: `EncodingResult<T>`, `Result<T>`, `StreamingResult<T>` type aliases
- `include/rlp/errors.hpp`: `EncodingError`, `DecodingError`, `StreamingError` enums
- `include/rlp/intx.hpp`: `uint256` arbitrary-precision integer for ETH amounts

**Core Logic — RLPx:**
- `include/rlpx/rlpx_session.hpp` / `src/rlpx/rlpx_session.cpp`: Session lifecycle (connect, accept, HELLO, subprotocol negotiation, message routing)
- `include/rlpx/rlpx_types.hpp`: Wire constants (key sizes, frame sizes, protocol versions, disconnect reasons), type aliases
- `include/rlpx/rlpx_error.hpp`: `SessionError`, `AuthError`, `FramingError` enums, `Result<T>` alias
- `include/rlpx/auth/auth_handshake.hpp` / `src/rlpx/auth/`: ECIES handshake (auth/ack messages, key derivation, frame secrets)
- `include/rlpx/crypto/ecdh.hpp` / `src/rlpx/crypto/`: secp256k1 ECDH key agreement
- `include/rlpx/crypto/aes.hpp` / `src/rlpx/crypto/`: AES-256-CTR encryption/decryption
- `include/rlpx/framing/message_stream.hpp` / `src/rlpx/framing/`: Frame encryption + MAC, message send/receive with Snappy compression
- `include/rlpx/protocol/messages.hpp` / `src/rlpx/protocol/`: RLPx protocol message encoding/decoding (Hello, Disconnect, Ping, Pong, subprotocol capabilities)
- `include/rlpx/socket/socket_transport.hpp` / `src/rlpx/socket/`: TCP socket wrapper with timeout and read/write primitives

**Core Logic — Discovery:**
- `include/discv4/discv4_client.hpp` / `src/discv4/discv4_client.cpp`: Discovery v4 client (UDP PING/PONG/FINDNODE/NEIGHBORS, packet hashing)
- `include/discv4/dial_scheduler.hpp` / `src/discv4/`: Per-chain TCP dial scheduling with `WatcherPool` resource cap
- `include/discv4/chain_peers.hpp` / `src/discv4/chain_peers.cpp`: Load/parse/validate cached peer data from `chain_enodes.json.gz`
- `include/discv4/discovery.hpp`: Discovery interface abstraction
- `include/discv5/discv5_client.hpp` / `src/discv5/discv5_client.cpp`: Discovery v5 client (ENR-based, FINDNODE/NODES)
- `include/discv5/discv5_crawler.hpp` / `src/discv5/discv5_crawler.cpp`: Peer queue state machine (enqueue, dedup, emit)
- `include/discv5/discv5_enr.hpp` / `src/discv5/discv5_enr.cpp`: ENR record decoding and signature verification
- `include/discovery/discovered_peer.hpp`: Shared `ValidatedPeer` type used by both discv4 and discv5

**Core Logic — ETH:**
- `include/eth/eth_watch_service.hpp` / `src/eth/eth_watch_service.cpp`: Event subscription engine (Bloom filter → receipt → log match → ABI decode → callback)
- `include/eth/eth_watch_runner.hpp` / `src/eth/eth_watch_runner.cpp`: Per-session runner bridging RLPx session + EthWatchService
- `include/eth/eth_peer_session.hpp` / `src/eth/eth_peer_session.cpp`: ETH Status handshake (build/validate ETH/68-69 Status messages)
- `include/eth/eth_session_channel.hpp` / `src/eth/eth_session_channel.cpp`: `IEthSessionChannel` interface + `RlpxEthSessionChannel` adapter
- `include/eth/event_filter.hpp` / `src/eth/event_filter.cpp`: Bloom prefilter + exact log matching for contract addresses/event topics
- `include/eth/abi_decoder.hpp` / `src/eth/abi_decoder.cpp`: EVM ABI event parameter decoding
- `include/eth/chain_tracker.hpp` / `src/eth/chain_tracker.cpp`: Block deduplication window and tip tracking
- `include/eth/finality_policy.hpp` / `src/eth/finality_policy.cpp`: Per-chain finality head selection
- `include/eth/rpc_manager.hpp` / `src/eth/rpc_manager.cpp`: RPC endpoint pool (priority/weight/rate-limit/health)
- `include/eth/rpc_http_transport.hpp` / `src/eth/rpc_http_transport.cpp`: JSON-RPC HTTP transport for receipt fetching
- `include/eth/messages.hpp` / `src/eth/messages.cpp`: ETH/66-69 wire message types and encoders
- `include/eth/objects.hpp` / `src/eth/objects.cpp`: Core ETH data objects (TransactionReceipt, EventLog, BlockHeader)
- `include/eth/eth_types.hpp`: ETH protocol version constants, Status message structs, `ForkId`, `WatchId`
- `include/eth/eth_constants.hpp`: ABI encoding constants, Keccak-256 size, chain defaults
- `include/eth/bridge_event.hpp` / `src/eth/bridge_event.cpp`: Bridge-specific event normalization
- `include/eth/bridge_observation.hpp` / `src/eth/bridge_observation.cpp`: Bridge observation claims

**Base Utilities:**
- `include/base/parse_utility.hpp` / `src/base/parse_utility.cpp`: Hex nibble/string/bytes parsing, uint decimal/hex parsing
- `include/base/json_utility.hpp` / `src/base/json_utility.cpp`: Schema-driven JSON parsing (`JsonSchemaObject`, `JsonSchemaArray`, `JsonSchemaField`)
- `include/base/byte_encoding.hpp` / `src/base/`: Byte ↔ hex string encoding
- `include/base/rlp-logger.hpp` / `src/base/rlp-logger.cpp`: Logger wrapper around spdlog

**Testing:**
- `test/rlp/rlp_encoder_tests.cpp`, `test/rlp/rlp_decoder_tests.cpp`: Canonical RLP encoding/decoding unit tests
- `test/rlp/rlp_ethereum_tests.cpp`, `test/rlp/rlp_ethereum_real_world_examples.cpp`: Ethereum-specific RLP test vectors
- `test/rlp/rlp_streaming_decoder_tests.cpp`: Large-payload streaming tests
- `test/rlpx/crypto_test.cpp`, `test/rlpx/handshake_vectors_test.cpp`: RLPx crypto and ECIES handshake test vectors
- `test/rlpx/rlpx_session_test.cpp`, `test/rlpx/rlpx_state_test.cpp`: RLPx session lifecycle tests
- `test/rlpx/message_routing_test.cpp`: Protocol message routing tests
- `test/rlpx/frame_cipher_test.cpp`, `test/rlpx/snappy_test.cpp`: Framing and compression tests
- `test/rlpx/capability_negotiation_test.cpp`: Capability negotiation tests
- `test/discv4/discovery_test.cpp`: Discovery v4 protocol tests
- `test/discv4/discv4_client_test.cpp`: discv4 client lifecycle tests
- `test/discv4/dial_scheduler_test.cpp`: Dial scheduling and resource cap tests
- `test/discv4/chain_peers_test.cpp`: Chain peer cache loading/validation tests
- `test/discv5/discv5_enr_test.cpp`, `test/discv5/discv5_crawler_test.cpp`: discv5 ENR and crawler tests
- `test/eth/eth_handshake_test.cpp`: ETH Status handshake tests
- `test/eth/eth_watch_service_test.cpp`: Event watching pipeline tests
- `test/eth/eth_watch_runner_test.cpp`: Per-session runner tests
- `test/eth/eth_watch_all_chains_live_test.cpp`: Live multi-chain functional test (opt-in via `EVMRELAY_RUN_LIVE_ALL_CHAINS_TEST=1`)
- `test/eth/json_rpc_test.cpp`: JSON-RPC response parsing tests
- `test/eth/rpc_manager_test.cpp`: RPC endpoint management tests
- `test/eth/event_filter_test.cpp`: Bloom filter and log matching tests
- `test/eth/abi_decoder_test.cpp`: ABI type decoding tests
- `test/eth/finality_policy_test.cpp`: Finality head selection tests

**Configuration Data:**
- `examples/chains_config.json`: Chain metadata (network IDs, genesis hashes, fork IDs, bootnodes, ENR trees, RPC endpoints, ETH message schemas)
- `rlp_enodes/output/chain_enodes.json.gz`: Signed cached peer data (downloadable from `https://enodes.gnus.ai/chain_enodes.json.gz`)
- `examples/.env.example`: Template for environment variables (API keys, etc.)

## Naming Conventions

**Files:**
- Headers: `snake_case.hpp` (e.g., `rlp_encoder.hpp`, `eth_watch_service.hpp`, `discv4_client.hpp`)
- Sources: `snake_case.cpp` (e.g., `rlp_encoder.cpp`, `eth_watch_service.cpp`)
- Tests: `*_test.cpp` suffix (e.g., `eth_watch_service_test.cpp`)
- CMake: `CMakeLists.txt` in each subdirectory

**Directories:**
- `snake_case` for module directories (e.g., `discv4/`, `rlpx/`, `eth/`)
- Subdirectories for sub-layers within a module (e.g., `rlpx/auth/`, `rlpx/crypto/`, `rlpx/framing/`)

**Classes/Methods:**
- Classes: `PascalCase` (e.g., `RlpEncoder`, `EthWatchService`, `DialScheduler`, `RlpxSession`)
- Methods: `PascalCase` (e.g., `BeginList()`, `EndList()`, `GetBytes()`, `PeekHeader()`)
- Exception: `add()` and `read()` on `RlpEncoder`/`RlpDecoder` are lowercase for template ergonomics

**Variables:**
- Members: `lowerCamelCase` with trailing underscore (e.g., `buffer_`, `session_`, `io_context_`)
- Locals: `lowerCamelCase` or `snake_case` depending on context
- Constants: `kPascalCase` with `constexpr`/`inline constexpr` (e.g., `kPublicKeySize`, `kDefaultPingTimeout`, `kMaxFrameSize`)

**Namespaces:**
- Nested namespaces with full indentation (e.g., `rlp::base::parse`, `rlpx::auth`, `eth::rpc`)
- Module top-level: `rlp`, `rlpx`, `discv4`, `discv5`, `eth`
- Shared types: `discovery` namespace for discv4/discv5 interop

**Error Enums:**
- Scoped enums (`enum class`)
- `PascalCase` values prefixed with `k` (e.g., `SessionError::kNetworkFailure`, `DecodingError::kOverflow`)

**Type Aliases:**
- `using PascalCase = ...` (e.g., `PublicKey`, `PrivateKey`, `ByteBuffer`, `Hash256`)

## Where to Add New Code

**New RLP Type Support:**
- Primary code: Types in `include/rlp/rlp_ethereum.hpp`, encoding helpers in `src/rlp/rlp_encoder.cpp`
- Tests: `test/rlp/rlp_ethereum_tests.cpp` or new file in `test/rlp/`

**New RLPx Protocol Message:**
- Primary code: Message struct in `include/rlpx/protocol/messages.hpp`, encode/decode in `src/rlpx/protocol/`
- Tests: `test/rlpx/protocol_messages_test.cpp`

**New ETH Wire Message:**
- Primary code: Message type in `include/eth/eth_types.hpp` or `include/eth/messages.hpp`, encode/decode in `src/eth/messages.cpp`
- Tests: `test/eth/eth_messages_test.cpp`

**New Chain Support:**
- Primary code: Add entry to `examples/chains_config.json` (network ID, genesis, fork ID, bootnodes, ENR tree)
- No C++ source changes needed — all chain data is data-driven

**New Event Watch Filter:**
- Primary code: Register via `EthWatchService::watch_event()` or `EthWatchRunner::watch_event()`
- No new source files needed — filtering is runtime-configured via contract address + event signature

**New Discovery Protocol:**
- Primary code: New module under `src/<protocol>/`, headers under `include/<protocol>/`
- Shared types: Extend `include/discovery/` if needed for interop with existing discovery modules
- Tests: `test/<protocol>/`

**New Utility Function:**
- Primary code: If reusable across modules, add to `include/base/` and `src/base/`. If module-specific, add within the module.
- Tests: Corresponding test directory

## Special Directories

**`go-ethereum/`:**
- Purpose: Git submodule providing reference go-ethereum implementation for test vectors and protocol validation
- Generated: No (external reference)
- Committed: As submodule reference

**`build/`:**
- Purpose: Build output directory (not committed)
- Generated: Yes
- Committed: No (in `.gitignore`)

**`.planning/`:**
- Purpose: GSD planning artifacts — codebase maps, phase plans
- Generated: Yes (by GSD commands)
- Committed: Yes

**`rlp_enodes/output/`:**
- Purpose: Signed chain peer data (`chain_enodes.json.gz`) consumed by C++ library at runtime
- Generated: Yes (by Go crawler tool)
- Committed: Selectively (output file only, not intermediate artifacts)

**`examples/logs/`:**
- Purpose: Runtime log output from example programs
- Generated: Yes
- Committed: No

---

*Structure analysis: 2026-05-25*
