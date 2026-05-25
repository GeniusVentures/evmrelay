# RPC Manager Handoff

Date: 2026-05-24

## Goal

Continue building the RPC manager and supporting classes that let an evmrelay node make JSON-RPC calls to EVM chains for receipt/log/block verification.

Keep this separate from `EthWatchService` orchestration until the runtime API is stable.

## Current Foundation

- Shared schema-driven JSON parsing now lives in `include/base/json_utility.hpp` and `src/base/json_utility.cpp`.
- New JSON object loading should declare `JsonSchemaObject` / `JsonSchemaArray` data and consume `JsonParsedObject` values.
- `eth::rpc::RpcManagerConfig` and `RpcEndpointConfig` exist in `include/eth/rpc_manager_config.hpp`.
- `src/eth/rpc_manager_config.cpp` loads endpoint config from JSON text/file through the shared schema parser.
- `src/eth/json_rpc.cpp` has request builders and response parsers for:
  - `eth_getBlockByNumber`
  - `eth_getLogs`
  - `eth_getTransactionReceipt`
- `src/eth/rpc_receipt_source.cpp` already wraps a `JsonRpcTransport` for receipt/log fetching and can be reused rather than replaced.
- Runtime RPC selection now exists in `include/eth/rpc_manager.hpp` and `src/eth/rpc_manager.cpp`:
  - `RpcEndpoint`
  - `RpcEndpointPool`
  - `RpcManager`
  - URL rendering with `apiKeyEnvVar` / `apiKeyLiteral`
  - deterministic grouping and ordering by chain/priority/weight
- The transport layer now exists in `include/eth/rpc_http_transport.hpp` and `src/eth/rpc_http_transport.cpp`.
  - It uses Boost.Beast + Boost.Asio.
  - HTTP and HTTPS are both supported.
  - HTTPS uses OpenSSL and RFC2818 hostname verification when peer verification is enabled.

## Important Constraints

- Keep operational facts out of production C++: chain names, endpoint URLs, API keys, endpoint counts, and provider policy belong in config/fixtures.
- Keep schemas in C++ for now, per current decision.
- Keep field layout/type/default handling in `base/json_utility`; feature code should only select schema data and construct domain objects.
- Use Boost.JSON and Boost.Outcome patterns already present in the repo.
- Do not put API keys in tests or checked-in examples.
- Do not mix RPC manager runtime responsibilities into `EthWatchService` yet.

## Suggested Next Classes

- `RpcManager`: owns configured endpoint pools and creates chain-scoped transports or receipt sources.
- `RpcReceiptSourceFactory`: a small adapter that binds `RpcManager` to `RpcReceiptSource` without pushing runtime policy into `EthWatchService`.
- `ChainlistProvider`: optional loader for ChainList-style public endpoint metadata, filtered into `RpcEndpointConfig` candidates.
- `RpcObservedMessageVerifier` or `RpcReceiptVerifier`: consumes observed chain/log facts and verifies them through independent RPC endpoints.

## Recommended Implementation Order

1. Add `RpcEndpointPool` with simple health state:
   - available;
   - temporarily failed;
   - disabled.
2. Wire `RpcReceiptSource` creation from `RpcManager`.
3. Add a chain-scoped `RpcReceiptSourceFactory` or equivalent adapter.
4. Add endpoint failure/retry suppression and basic rate-limit bookkeeping.
5. Add ChainList ingestion once the runtime manager surface is stable.
6. Add verifier-level quorum/finality policy only after endpoint selection and transport are tested.

## Tests To Add Next

- Endpoint failure marks only that endpoint unhealthy.
- Chain with no usable endpoint fails closed.
- `RpcManager` can create a receipt source for a configured chain.
- HTTP transport test with a local fake server or mock transport.
- HTTPS transport test with a loopback TLS server and self-signed certificate.
- Receipt source creation from the manager without leaking transport policy into `EthWatchService`.

## Verification Commands

From the SuperGenius repository root:

```bash
cmake --build evmrelay/build/OSX/Debug --target rpc_manager_test rpc_http_transport_test rpc_manager_config_test json_rpc_test rpc_receipt_source_test

./evmrelay/build/OSX/Debug/test_bin/rpc_manager_test
./evmrelay/build/OSX/Debug/test_bin/rpc_http_transport_test
./evmrelay/build/OSX/Debug/test_bin/rpc_manager_config_test
./evmrelay/build/OSX/Debug/test_bin/json_rpc_test
./evmrelay/build/OSX/Debug/test_bin/rpc_receipt_source_test
```

`discv4_chain_peers_test` has a live URL case that can fail in network-restricted environments. Local chain peer parsing cases passed during this checkpoint.

## Files To Inspect First

- `include/base/json_utility.hpp`
- `src/base/json_utility.cpp`
- `include/eth/rpc_manager_config.hpp`
- `src/eth/rpc_manager_config.cpp`
- `include/eth/json_rpc.hpp`
- `src/eth/json_rpc.cpp`
- `include/eth/rpc_receipt_source.hpp`
- `src/eth/rpc_receipt_source.cpp`
- `include/eth/eth_receipt_source.hpp`

## Known State From This Checkpoint

- `rpc_manager_test`: passed 8/8.
- `rpc_http_transport_test`: passed 3/3.
- `rpc_manager_config_test`: passed 6/6.
- `json_rpc_test`: passed 6/6.
- `rpc_receipt_source_test`: passed 10/10.
- `discv4_chain_peers_test`: 23/24 passed; live download test failed because the restricted environment did not download JSON.
