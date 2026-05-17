# Sepolia Test Parameters for eth_watch

## Current Sepolia Fork Hash (as of March 2026)

The Sepolia chain is post-BPO2. Forks applied (all timestamps):
- MergeNetsplit block 1735371
- Shanghai 1677557088
- Cancun 1706655072
- Prague 1741159776 (passed ~March 5, 2025)
- Osaka 1760427360 (passed ~October 14, 2025)
- BPO1 1761017184 (passed ~October 21, 2025)
- BPO2 1761607008 (passed ~October 28, 2025)

**Current ENR/Status ForkId:** `{ 0x26, 0x89, 0x56, 0xb6 }`, Next=0

Verified via `go-ethereum/core/forkid/forkid_test.go` SepoliaChainConfig test vectors
and confirmed by live `test_enr_survey` run (March 14, 2026 — only hash `26 89 56 b6`
matched current Sepolia peers in the ENR survey).

> **Do NOT use `0xed, 0x88, 0xb5, 0xfd`** — that was the Prague hash with Next=1760427360,
> valid only before Osaka launched (~Oct 2025). It will match zero live peers today.



To test `eth_watch` with a public Sepolia node, you can use one of the **bootstrap nodes** (though they won't send block data, they will at least connect):

### Option 1: Use Bootstrap Node (Will Connect, No Block Data)

```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug

# Using the first Sepolia bootstrap node
./examples/eth_watch/eth_watch 138.197.51.181 30303 4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b
```

**Parameters breakdown:**
- **Host**: `138.197.51.181`
- **Port**: `30303`
- **Public Key**: `4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b`
- **ETH Offset**: `0x10` (default, optional)

### Option 2: Use --chain Flag (Easiest)

```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./examples/eth_watch/eth_watch --chain ethereum-sepolia
```

This automatically uses the first Sepolia bootstrap node from the configuration.

## All Available Sepolia Bootstrap Nodes

From `/include/rlp/PeerDiscovery/bootnodes_test.hpp`:

### Node 1 (Recommended for Testing)
```bash
./examples/eth_watch/eth_watch 138.197.51.181 30303 4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b
```

### Node 2
```bash
./examples/eth_watch/eth_watch 146.190.1.103 30303 143e11fb766781d22d92a2e33f8f104cddae4411a122295ed1fdb6638de96a6ce65f5b7c964ba3763bba27961738fef7d3ecc739268f3e5e771fb4c87b6234ba
```

### Node 3
```bash
./examples/eth_watch/eth_watch 170.64.250.88 30303 8b61dc2d06c3f96fddcbebb0efb29d60d3598650275dc469c22229d3e5620369b0d3dedafd929835fe7f489618f19f456fe7c0df572bf2d914a9f4e006f783a9
```

### Node 4
```bash
./examples/eth_watch/eth_watch 139.59.49.206 30303 10d62eff032205fcef19497f35ca8477bea0eadfff6d769a147e895d8b2b8f8ae6341630c645c30f5df6e67547c03494ced3d9c5764e8622a26587b083b028e8
```

### Node 5
```bash
./examples/eth_watch/eth_watch 138.68.123.152 30303 9e9492e2e8836114cc75f5b929784f4f46c324ad01daf87d956f98b3b6c5fcba95524d6e5cf9861dc96a2c8a171ea7105bb554a197455058de185fa870970c7c
```

## Expected Output

When connecting to a bootstrap node:
```
Connected. Waiting for messages...

⚠️  Note: Bootstrap nodes are for DISCOVERY ONLY (discv4 protocol)
    They will NOT send block data. To receive blocks:
    1. Use discv4 to discover real peer nodes, OR
    2. Connect directly to a full node (not a bootstrap node)

    See BOOTNODES_CONFIGURATION.md for more info.

HELLO from peer: <bootnode-client-id>
Sent ETH Status message to peer
(connection may close here - bootstrap nodes don't speak ETH protocol)
```

## Why No Messages?

Bootstrap nodes are **discovery-only** (discv4 protocol via UDP). They:
- ❌ Don't send block data
- ❌ Don't participate in ETH protocol message exchange
- ✅ Only help you find other peers

See `WHY_NO_MESSAGES.md` for full explanation.

## To Get Actual Block Messages

You need to connect to a **real peer node** (not a bootstrap node). Options:

### Option A: Run Your Own Geth Node
```bash
# Install Geth
brew install ethereum

# Run Sepolia node
geth --sepolia --http --http.api admin,web3,eth,net

# In another terminal, query for peers
curl -s -X POST http://localhost:8545 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}'

# Extract enode and connect with eth_watch
```

### Option B: Use Public Infrastructure Nodes

Some public infrastructure providers run full nodes that accept p2p connections:
- Infura (requires account)
- Alchemy (requires account)
- QuickNode (requires account)

However, most public RPC endpoints **don't expose p2p ports** for security reasons.

### Option C: Use the maintained discovery harnesses

Use the current C++ discovery flow under `discv4_client` / `DialScheduler` via:
1. `examples/discovery/test_discovery.cpp`
2. `examples/discovery/test_enr_survey.cpp`
3. the existing bootnode registry and ENR filter wiring

Those paths exercise the maintained discovery implementation instead of the old `discovery.hpp` sketch.

## Summary

**For quick testing right now:**
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug

# Easiest - use --chain flag
./examples/eth_watch/eth_watch --chain ethereum-sepolia

# Or manually specify the first bootstrap node
./examples/eth_watch/eth_watch 138.197.51.181 30303 4e5e92199ee224a01932a377160aa432f31d0b351f84ab413a8e0a42f4f36476f8fb1cbe914af0d9aef0d51665c214cf653c651c4bbd9d5550a934f241f1682b
```

**Expected result:** Connection succeeds, HELLO exchange works, but no block messages (because it's a bootstrap node).

**To get block messages:** You need to use the maintained discovery harnesses to find real peers, or run your own Geth node.

