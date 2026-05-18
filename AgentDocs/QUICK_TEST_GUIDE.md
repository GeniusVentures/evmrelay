# Quick Test Guide: Getting Real Peer Data

## Summary

I've created two new resources to help you test with real Ethereum peers:

### 1. **PUBLIC_NODES_FOR_TESTING.md**
- Lists public RPC endpoints for Mainnet and Sepolia
- Shows how to query for live peer information
- Provides examples and scripts

### 2. **eth_watch** chain-peer cache mode
Located in: `/Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug/examples/eth_watch/eth_watch`

Usage:
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./examples/eth_watch/eth_watch --chain ethereum-sepolia --watch-event 'Transfer(address,address,uint256)'
./examples/eth_watch/eth_watch --all-chains --watch-event 'Transfer(address,address,uint256)' --display-events 2
```

Connection pool defaults come from `eth::EthWatchConnectionConfig`:

- `--max-peers-per-chain 3`
- `--max-peers-total 24`

`--all-chains` watches cached peers for:

- `ethereum-mainnet`
- `polygon-mainnet`
- `bnb-smart-chain`
- `base-mainnet`

## Quick Manual Test

If you want to test manually:

```bash
# 1. Get a live peer (these commands may take a few seconds)
PEER=$(curl -s -X POST https://sepolia.llamarpc.com \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' \
  | jq -r '.result[0].enode')

# 2. Extract components
PUBKEY=$(echo "$PEER" | sed 's/enode:\/\/\([^@]*\)@.*/\1/')
HOST=$(echo "$PEER" | sed 's/.*@\([^:]*\):.*/\1/')
PORT=$(echo "$PEER" | sed 's/.*:\([0-9]*\)$/\1/')

# 3. Connect
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./examples/eth_watch/eth_watch "$HOST" "$PORT" "$PUBKEY"
```

## Chain Peer Cache for Sandboxed Tests

`discv4_chain_peers_test` can use a pre-downloaded cache instead of live DNS/network.
Do not assume a source-tree `rlp_enodes/` directory exists.

```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
curl -L https://enodes.gnus.ai/chain_enodes.json.gz -o chain_enodes.json.gz
ctest -R discv4_chain_peers_test --output-on-failure
```

CTest sets `EVMRELAY_CHAIN_ENODES_JSON=${CMAKE_BINARY_DIR}/chain_enodes.json.gz` for
`discv4_chain_peers_test`. If that file is absent, the test falls back to the live URL
and requires network access.

## Public RPC Endpoints (No Auth Required)

**Sepolia:**
- `https://sepolia.llamarpc.com`
- `https://1rpc.io/sepolia`

**Mainnet:**
- `https://eth.llamarpc.com`
- `https://1rpc.io/eth`

## Expected Output When Connecting to Real Peer

```
Connected. Waiting for messages...

⚠️  Note: Bootstrap nodes are for DISCOVERY ONLY (discv4 protocol)
    They will NOT send block data. To receive blocks:
    1. Use discv4 to discover real peer nodes, OR
    2. Connect directly to a full node (not a bootstrap node)

HELLO from peer: Geth/v1.13.0-...
Sent ETH Status message to peer
NewBlockHashes: 2 hashes
NewBlockHashes: 1 hash
...
```

## Files Created/Updated

```
 /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/
├── AgentDocs/PUBLIC_NODES_FOR_TESTING.md
├── AgentDocs/WHY_NO_MESSAGES.md
├── AgentDocs/BOOTNODES_CONFIGURATION.md
├── examples/eth_watch/eth_watch.cpp
└── build/OSX/Debug/examples/eth_watch/eth_watch
```

## What Each File Does

- **PUBLIC_NODES_FOR_TESTING.md**: Reference for finding public nodes and peers
- **test_eth_watch.sh**: One-command test that does everything automatically
- **WHY_NO_MESSAGES.md**: Explains why bootstrap nodes don't send messages
- **BOOTNODES_CONFIGURATION.md**: Bootnode configs with clarifications

## Next Steps

1. **For Quick Testing**: Run `./examples/eth_watch/eth_watch --chain ethereum-sepolia --watch-event 'Transfer(address,address,uint256)'`
2. **For Discovery Debugging**: Use the maintained C++ discovery harnesses under `examples/discovery/` (for example `test_discovery.cpp` and `test_enr_survey.cpp`)
3. **For Development**: Use a local Geth node with `--http --http.api admin,web3,eth,net`
