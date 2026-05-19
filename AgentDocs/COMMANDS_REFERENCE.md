# Quick Commands Reference

## Manual Steps

### Step 1: Get a Live Peer
```bash
# Sepolia
curl -s https://sepolia.llamarpc.com -X POST \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' \
  | jq '.result[0]'
```

### Step 2: Parse the Enode
```bash
# Set PEER to the enode string from above, then:
PEER="enode://PUBKEY@IP:PORT"

PUBKEY=$(echo "$PEER" | sed 's/enode:\/\/\([^@]*\)@.*/\1/')
HOST=$(echo "$PEER" | sed 's/.*@\([^:]*\):.*/\1/')
PORT=$(echo "$PEER" | sed 's/.*:\([0-9]*\)$/\1/')

echo "Host: $HOST, Port: $PORT, Pubkey: $PUBKEY"
```

### Step 3: Connect
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./examples/eth_watch/eth_watch "$HOST" "$PORT" "$PUBKEY"
```

## Useful Commands

### Clean build
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Debug
ninja
```

### Run all tests
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
ctest --output-on-failure
```

### Run specific test
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./test_bin/rlp_decoder_tests
```

### Watching cached chain peers
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug

# Refresh the cache when validating live paths. The current schema must include
# both nodes and bootnodes arrays.
curl -L https://enodes.gnus.ai/chain_enodes.json.gz -o chain_enodes.json.gz

# Single chain, canonical cache key
./examples/eth_watch/eth_watch --chain ethereum-sepolia --watch-event 'Transfer(address,address,uint256)'

# Four mainnet EVM chains at once
./examples/eth_watch/eth_watch --all-chains --watch-event 'Transfer(address,address,uint256)' --display-events 2

# Override the default connection pool limits
./examples/eth_watch/eth_watch --chain ethereum-sepolia --max-peers-per-chain 3 --max-peers-total 24 --watch-event 'Transfer(address,address,uint256)'
```

### Local geth direct-mode repro
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/go-ethereum
./build/bin/geth --sepolia --datadir /tmp/evmrelay-geth-sepolia --port 30303 --http --http.addr 127.0.0.1 --http.port 8545 --http.api admin,eth,net,web3 --nat extip:127.0.0.1 --nodiscover --maxpeers 2 --netrestrict 127.0.0.0/8

# In another shell, get the enode:
./build/bin/geth --datadir /tmp/evmrelay-geth-attach attach --exec 'admin.nodeInfo.enode' http://127.0.0.1:8545

cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
./examples/eth_watch/eth_watch --chain ethereum-sepolia --chain-peers-json ../../../rlp_enodes/output/chain_enodes.json --direct-enode '<local-geth-enode>' --watch-event 'Transfer(address,address,uint256)' --display-events 1 --log-level info --no-chain-peers-url
```

### Pre-download chain peer cache for sandboxed tests
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/build/OSX/Debug
curl -L https://enodes.gnus.ai/chain_enodes.json.gz -o chain_enodes.json.gz
ctest -R discv4_chain_peers_test --output-on-failure
```

### C++ eth_watch example smoke test
```bash
cd /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay
cd build/OSX/Debug
ctest -R eth_watch_example_test --output-on-failure

# Or run the example test binary directly:
./examples/eth_watch/eth_watch_example_test
```

This compiled example test replaces the old shell smoke harness. It validates
cache-backed service startup, GNUS watch-spec construction, multi-chain service
config, and Gnosis empty-`nodes` discovery fallback.

## Common Issues

### "Failed to connect"
- The selected cached peers may be offline or unreachable from the local network.
- If the log says `Unknown or unconfigured chain`, refresh `chain_enodes.json.gz`;
  stale caches that lack `bootnodes` are rejected by the current loader.
- Do not use `bootnodes` as direct RLPx/ETH peers. They are discovery-only seeds.

### "Connected but no messages"
- Check whether the connected peer is sending useful ETH traffic during the smoke window.
- Use another cached peer set or direct real peer enodes from a local full node.

### "HELLO from peer but still no messages"
- Some peers may not actively broadcast blocks
- Try a different peer from a local full node.

## File Locations

```
Project Root: /Users/Shared/SSDevelopment/Development/GeniusVentures/GeniusNetwork/SuperGenius/evmrelay/

Key Files:
- ./examples/eth_watch/eth_watch_example_test.cpp
- ./AgentDocs/QUICK_TEST_GUIDE.md   (Quick guide)
- ./AgentDocs/PUBLIC_NODES_FOR_TESTING.md
- ./AgentDocs/WHY_NO_MESSAGES.md
- ./build/OSX/Debug/examples/eth_watch/eth_watch

Source Code:
- ./include/eth/messages.hpp        (ETH protocol messages)
- ./include/eth/eth_types.hpp       (Message types)
- ./include/rlpx/                   (RLPx protocol)
- ./examples/eth_watch/eth_watch.cpp
```

## Resources

- **Ethereum Execution Spec**: https://github.com/ethereum/execution-specs
- **devp2p Specs**: https://github.com/ethereum/devp2p
- **RLPx**: https://github.com/ethereum/devp2p/blob/master/rlpx.md
- **ETH Protocol**: https://github.com/ethereum/devp2p/blob/master/caps/eth.md
- **discv4**: https://github.com/ethereum/devp2p/blob/master/discv4.md
