# Public Ethereum Nodes for Testing

## Quick Test Endpoints

### Ethereum Mainnet

#### RPC Endpoints (HTTP for getting peer info)
```bash
# LlamaNodes (free, no auth)
https://eth.llamarpc.com

# Alchemy (free tier, requires signup)
https://eth-mainnet.g.alchemy.com/v2/YOUR_KEY

# Infura (free tier, requires signup)
https://mainnet.infura.io/v3/YOUR_KEY

# Public node
https://1rpc.io/eth

# Ankr (free)
https://rpc.ankr.com/eth
```

#### Getting Active Peer Information

To get real peer enodes from a public RPC:
```bash
# Using curl + jq
curl -s -X POST https://eth.llamarpc.com \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' | jq '.result[0]'
```

Expected output format:
```json
{
  "enode": "enode://PUBKEY@IP:PORT",
  "id": "...",
  "name": "Geth/...",
  "caps": ["eth/68"],
  "network": {
    "inbound": false,
    "localAddress": "...",
    "remoteAddress": "IP:PORT",
    "static": false,
    "trusted": false
  }
}
```

### Ethereum Sepolia Testnet

#### RPC Endpoints
```bash
# LlamaNodes
https://sepolia.llamarpc.com

# Alchemy
https://eth-sepolia.g.alchemy.com/v2/YOUR_KEY

# Infura
https://sepolia.infura.io/v3/YOUR_KEY

# Public
https://1rpc.io/sepolia

# Ankr
https://rpc.ankr.com/eth_sepolia
```

#### Get Sepolia Peers
```bash
curl -s -X POST https://sepolia.llamarpc.com \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' | jq '.result[0]'
```

## Known Working Peer Enodes

These change frequently but here are some that were recently active:

### Ethereum Mainnet Peers
```
# Geth nodes
enode://PUBKEY@IP:30303

# Lighthouse/Prysm nodes
enode://PUBKEY@IP:30303
```

### Sepolia Testnet Peers
```
# More stable for testing
enode://PUBKEY@IP:30303
```

## How to Use with eth_watch

### Step 1: Get a Live Peer
```bash
# Get peer info from RPC
PEER_INFO=$(curl -s -X POST https://sepolia.llamarpc.com \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' | jq '.result[0].enode' -r)

# Extract components
# Format: enode://PUBKEY@IP:PORT
echo "Peer: $PEER_INFO"
```

### Step 2: Parse the Enode
```bash
# Extract pubkey (128 hex chars after enode://)
PUBKEY=$(echo "$PEER_INFO" | sed 's/enode:\/\/\([^@]*\)@.*/\1/')

# Extract host
HOST=$(echo "$PEER_INFO" | sed 's/.*@\([^:]*\):.*/\1/')

# Extract port
PORT=$(echo "$PEER_INFO" | sed 's/.*:\([0-9]*\)$/\1/')

echo "Pubkey: $PUBKEY"
echo "Host: $HOST"
echo "Port: $PORT"
```

### Step 3: Connect with eth_watch
```bash
./examples/eth_watch/eth_watch "$HOST" "$PORT" "$PUBKEY"
```

## Compiled Example Test

The repository uses compiled C++ example tests for eth_watch smoke coverage:

```bash
cd build/OSX/Debug
ctest -R eth_watch_example_test --output-on-failure
```

Use the manual `eth_watch <host> <port> <peer_pubkey_hex>` command above when
you specifically need to validate a public peer from an RPC `admin_peers` result.

## Limitations

⚠️ **Important Notes:**

1. **Peer addresses change frequently** - Nodes go online/offline constantly
2. **May be rate-limited** - Public RPC endpoints have limits
3. **Some don't expose admin_peers** - Try multiple endpoints
4. **Nodes may disconnect** - Not all nodes accept arbitrary connections
5. **Network ID mismatch** - eth_watch hardcodes network_id=1 (mainnet)

## Alternative: Run Your Own Node

For reliable testing, run a local node:

### Ethereum (Geth)
```bash
geth --http --http.api admin,web3,eth,net \
     --http.addr 127.0.0.1 --http.port 8545 \
     --network-id 1
```

### Sepolia (Geth)
```bash
geth --sepolia --http --http.api admin,web3,eth,net \
     --http.addr 127.0.0.1 --http.port 8545
```

Then query local node for peers:
```bash
curl -s -X POST http://127.0.0.1:8545 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"admin_peers","params":[],"id":1}' | jq '.result[0]'
```

## References

- [LlamaNodes RPC](https://www.llamarpc.com/)
- [Alchemy RPC](https://www.alchemy.com/)
- [Infura RPC](https://www.infura.io/)
- [1RPC Public Endpoint](https://1rpc.io/)
- [Ankr RPC](https://www.ankr.com/)
- [Geth Installation](https://geth.ethereum.org/docs/install-and-build/installing-geth)

