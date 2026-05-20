# Bootnode Configuration Guide

## Overview

The RLP/RLPx library includes bootstrap node configurations for multiple EVM chains. **Important Note**: Bootstrap nodes are used for **peer discovery only** (discv4 protocol via UDP) - they do NOT send block data.

To receive block data (NewBlockHashes, NewBlock messages, etc.), you must:
1. Either: Use discv4 to discover real peer nodes from bootstrap nodes
2. Or: Connect directly to a full peer node

These configurations enable the `eth_watch` example to discover and connect to various blockchain networks.
Ethereum and Polygon chains also have EIP-1459 ENR-tree roots in
`examples/chains_config.json`; those roots are discovery-only seeds for discv5.

## Important: Bootstrap Nodes vs Real Peers

### Bootstrap Nodes (Discovery Only)
- **Protocol**: discv4 (UDP-based)
- **Purpose**: Help you find other peer nodes
- **Data**: Don't send block/transaction data
- **What you do**: Send PING → Receive PONG + NEIGHBOURS list

### ENR Trees (Discovery Only)
- **Protocol**: EIP-1459 DNS discovery plus discv5 (UDP-based)
- **Purpose**: Resolve current ENR bootnodes, then discover real peers
- **Data**: Don't send block/transaction data
- **What you do**: Resolve `enrtree://` roots -> start discv5 -> enqueue discovered peers

### Real Peer Nodes (Block Data)
- **Protocol**: RLPx + ETH protocol (TCP-based)
- **Purpose**: Share blockchain state and blocks
- **Data**: Send block headers, transactions, receipts, etc.
- **What you do**: RLPx handshake → ETH Status exchange → Receive block data

## Supported Chains

### Ethereum

| Chain | Network | Port | Status | Source |
|-------|---------|------|--------|--------|
| mainnet | Ethereum Mainnet | 30303 | ✅ Working | [go-ethereum/params/bootnodes.go](https://github.com/ethereum/go-ethereum/blob/master/params/bootnodes.go) |
| sepolia | Sepolia Testnet | 30303 | ✅ Working | [go-ethereum/params/sepolia.go](https://github.com/ethereum/go-ethereum/blob/master/params/sepolia.go) |
| holesky | Holesky Testnet | 30303 | ⚠️ Unreachable* | [go-ethereum/params/holesky.go](https://github.com/ethereum/go-ethereum/blob/master/params/holesky.go) |

### Polygon

| Chain | Network | Port | Status | Source |
|-------|---------|------|--------|--------|
| polygon | Polygon PoS Mainnet | 30303 | ✅ Working | [Polygon Docs](https://docs.polygon.technology/pos/reference/seed-and-bootnodes) |
| polygon-amoy | Polygon Amoy Testnet | 30303 | ✅ Working | [Polygon Docs](https://docs.polygon.technology/pos/reference/seed-and-bootnodes) |

### BNB Smart Chain (BSC)

| Chain | Network | Port | Status | Source |
|-------|---------|------|--------|--------|
| bsc / bsc-mainnet | BSC Mainnet | 30311* | ⚠️ Unreachable† | [bsc/params/config.go](https://github.com/bnb-chain/bsc/blob/master/params/config.go) |
| bsc-testnet | BSC Testnet | 30311* | ⏳ Testing | [bsc/params/config.go](https://github.com/bnb-chain/bsc/blob/master/params/config.go) |

**Note:** BSC uses non-standard port 30311 instead of 30303

### Base (OP Stack)

| Chain | Network | Port | Status | Notes |
|-------|---------|------|--------|-------|
| base | Base Mainnet | 30303 | ❌ Not Configured | Uses OP Stack discovery - see [Base Docs](https://docs.base.org/base-chain/node-operators/run-a-base-node) |
| base-sepolia | Base Sepolia Testnet | 30303 | ❌ Not Configured | Uses OP Stack discovery - see [Base Docs](https://docs.base.org/base-chain/node-operators/run-a-base-node) |

## Usage

### Finding Real Peer Nodes

To actually receive block data, you need to connect to **real peer nodes** (not bootstrap nodes). You can find these by:

1. **Query a node RPC endpoint** - Contact the network to ask for current peers:
   ```bash
   curl -s -X POST https://eth.llamarpc.com \
     -H "Content-Type: application/json" \
     -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' | jq '.result[0]'
   ```

2. **Use Node URLs** - These are enode strings from active peers reported by explorer APIs

3. **Run your own node** - Sync a full node which will discover and manage peers automatically

### Known Active Peer Enodes

For **testing purposes**, you can try these known Ethereum Sepolia peers:

```
enode://84b8482152e23b9a6b0abf89b4e3e0d93f2f4c3e8d9a0b1c2d3e4f5a6b7c8d9e0f1a2b3c4d5e6f7a8b@IP:30303
```

(Note: Peer enode addresses and IPs change frequently as nodes go online/offline)

### Using eth_watch Example

#### Connect to cached peers for a specific chain:
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./examples/eth_watch/eth_watch --chain <canonical_chain_name>
```

`--chain` now loads chain metadata from `chain_enodes.json(.gz)`.
Within each chain entry, `nodes` are RLPx/ETH peer candidates and `bootnodes`
are discovery-only seeds. Bootnodes are not direct event-watching peers.
Use canonical chain keys such as `ethereum-sepolia` or `ethereum-mainnet`.

For Ethereum and Polygon chains, `eth_watch` also loads the matching `enrTree`
entry from `examples/chains_config.json` when present next to the executable or
in the current working directory. The top-level keys in that file must be the
same canonical chain keys:

```json
{
  "ethereum-mainnet": {
    "enrTree": "enrtree://...@all.mainnet.ethdisco.net"
  },
  "polygon-mainnet": {
    "enrTree": "enrtree://...@pos.polygon-peers.io"
  }
}
```

`chains_config.json` is only for ENR-tree roots. Fork hashes and network metadata
continue to come from generated `chain_enodes.json(.gz)`.

If no local cache exists, `eth_watch` attempts to refresh from:
`https://enodes.gnus.ai/chain_enodes.json.gz`.

In a network-restricted sandbox, pre-download the cache to the build directory:

```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
curl -L https://enodes.gnus.ai/chain_enodes.json.gz -o chain_enodes.json.gz
```

**Note**: Bootstrap nodes are discovery-only. Cached `nodes` are the intended
input for RLPx/ETH message watching. If cached `nodes` is empty but `bootnodes`
is valid, `EthWatchService` starts discv4 fallback and enqueues discovered peers
into the dial queue.
When an ENR tree is configured, `EthWatchService` resolves it into ENR bootnodes,
starts discv5 from those ENRs, and enqueues only discovered peers. Resolved ENRs
and bootnodes are not treated as direct RLPx/ETH peer candidates. If ENR-tree
resolution returns no usable ENRs and valid discv4 `bootnodes` exist, the service
falls back to discv4 discovery.
If you connect directly to a bootstrap node, most likely you'll see:
```
Connected. Waiting for messages...
HELLO from peer: <bootnode-client>
Sent ETH Status message to peer
(no messages - bootnode disconnects)
```

#### Connect to a Real Peer (Block Data):
```bash
./examples/eth_watch/eth_watch <host> <port> <peer_pubkey_hex>
```

Example with a real Sepolia peer:
```bash
./examples/eth_watch/eth_watch 138.197.51.181 30303 4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b
```

#### Available chains:
```bash
# Ethereum
./examples/eth_watch/eth_watch --chain ethereum-mainnet
./examples/eth_watch/eth_watch --chain ethereum-sepolia
./examples/eth_watch/eth_watch --chain ethereum-holesky

# Polygon
./examples/eth_watch/eth_watch --chain polygon-mainnet
./examples/eth_watch/eth_watch --chain polygon-amoy

# BSC
./examples/eth_watch/eth_watch --chain bnb-smart-chain
./examples/eth_watch/eth_watch --chain bnb-smart-chain-testnet

# Base
./examples/eth_watch/eth_watch --chain base-mainnet
./examples/eth_watch/eth_watch --chain base-sepolia

# Gnosis, discovery fallback when nodes is empty and bootnodes is present
./examples/eth_watch/eth_watch --chain gnosis-chain

# Four mainnet EVM chains at once
./examples/eth_watch/eth_watch --all-chains --watch-event 'Transfer(address,address,uint256)' --display-events 2
```

#### Manual connection (enode format):
```bash
./examples/eth_watch/eth_watch <host> <port> <peer_pubkey_hex>
```

Example:
```bash
./examples/eth_watch/eth_watch 138.197.51.181 30303 4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b
```

## Implementation Files

### Bootnode Definitions

- **Chain peer cache loader:** `/include/discv4/chain_peers.hpp`
- **Discovery scheduler:** `/include/discv4/dial_scheduler.hpp`

### Chain Loader

- **Chain selection logic:** `/examples/eth_watch/eth_watch.cpp` loads chain metadata and delegates cache-backed orchestration to `EthWatchService`

## Smoke Validation

Run the compiled C++ example smoke test:

```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay
cd build/OSX/Debug
ctest -R eth_watch_example_test --output-on-failure
```

`eth_watch_example_test` validates cache-backed service startup, multi-chain
service config, and Gnosis empty-`nodes` discovery fallback without shell
wrappers or live peer reachability assumptions.

## Live ENR-Tree Validation

The ENR-tree peer queue test is opt-in because it performs live DNS and UDP
discv5 discovery. By default CTest skips it.

```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
env EVMRELAY_RUN_LIVE_ENR_TREE_TEST=1 \
    EVMRELAY_LIVE_ENR_TREE_CHAIN=ethereum-mainnet \
    EVMRELAY_LIVE_ENR_TREE_SECONDS=5 \
    ./test_bin/eth_enr_tree_peer_cache_live_test

env EVMRELAY_RUN_LIVE_ENR_TREE_TEST=1 \
    EVMRELAY_LIVE_ENR_TREE_CHAIN=polygon-mainnet \
    EVMRELAY_LIVE_ENR_TREE_SECONDS=5 \
    ./test_bin/eth_enr_tree_peer_cache_live_test
```

Environment variables:

- `EVMRELAY_RUN_LIVE_ENR_TREE_TEST=1` enables the live test.
- `EVMRELAY_LIVE_ENR_TREE_CHAIN` selects the chain. Supported values are
  `ethereum-mainnet`, `ethereum-sepolia`, `ethereum-holesky`, `ethereum-hoodi`,
  `polygon-mainnet`, and `polygon-amoy`.
- `EVMRELAY_LIVE_ENR_TREE_SECONDS` controls runtime; default is `5`.
- `EVMRELAY_LIVE_ENR_TREE_MIN_PEERS` controls the minimum accepted peer count;
  default is `1`.

The test starts with an empty `EthPeerQueue`, resolves the selected ENR tree,
starts discv5, and asserts that discovered peers enter the queue.

## Connection Status

The current implementation can load cache-backed chain metadata and start the
production `EthWatchService` path for supported chains. Live connection success
still depends on the freshness and reachability of cached `nodes`, local network
egress, and whether selected peers complete RLPx/ETH handshakes.

Last local deterministic validation on May 19, 2026:

- `eth_watch_example_test` passed.
- The Gnosis fixture had empty `nodes` and valid `bootnodes`, matching the
  discovery-fallback path.
- Opt-in live ENR-tree validation accepted peers from empty queues on
  `polygon-mainnet` and `ethereum-mainnet`.
- Live connection success remains a manual/network-environment validation.
- Base Sepolia (requires OP Stack discovery setup)

## Enode Format

Enodes follow the standard format:
```
enode://<128-hex-char-pubkey>@<ip>:<port>
```

Example breakdown:
```
enode://4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b@138.197.51.181:30303
       └─────────────────────── 128 hex characters (64 bytes) ──────────────────────┘   │        public key               │                    host:port
```

## Error Codes

When connection fails, the tool reports error code 12 (`kConnectionFailed`), which typically means:

1. **Bootstrap node is offline or unreachable** - Check network connectivity
2. **Wrong port number** - Ethereum/Polygon use 30303, BSC uses 30311
3. **Public key doesn't match** - Bootstrap node public key is incorrect

## Updating Bootnodes

To update bootnodes:

1. Check the official GitHub repositories for each chain
2. Extract the enode strings from `params/config.go` or `params/bootnodes.go`
3. Update the corresponding array in:
   - `/include/rlp/PeerDiscovery/bootnodes.hpp` (mainnet)
   - `/include/rlp/PeerDiscovery/bootnodes_test.hpp` (testnet)

## Future Enhancements

1. **OP Stack Discovery** - Add support for Base and other Optimism-based chains
2. **Operator Discovery Overrides** - Add explicit runtime controls for choosing ENR-tree, discv4, cache-only, or hybrid discovery per chain
3. **Health Checks** - Periodic bootnode and ENR-tree availability verification

## References

- [Ethereum go-ethereum](https://github.com/ethereum/go-ethereum/blob/master/params/bootnodes.go)
- [Polygon Documentation](https://docs.polygon.technology/pos/reference/seed-and-bootnodes)
- [BSC Documentation](https://docs.bnbchain.org/bnb-smart-chain/developers/node_operators/boot_node)
- [Base Documentation](https://docs.base.org/base-chain/node-operators/run-a-base-node)
- [Enode Format Specification](https://ethereum.org/en/developers/docs/networking-layer/network-addresses/)
