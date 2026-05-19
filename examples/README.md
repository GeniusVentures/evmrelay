# eth_watch example

`eth_watch` connects to an Ethereum peer via RLPx/DevP2P, performs the ETH Status
handshake, and watches for on-chain events matching registered ABI event signatures
(ERC-20 Transfer/Approval, ERC-721, ERC-1155, and GNUS Bridge events).

---

## Quick start

```bash
# Watch GNUS events on Sepolia
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain ethereum-sepolia \
    --watch-contract 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    --watch-event "Transfer(address,address,uint256)" \
    --watch-contract 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    --watch-event "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"

# Watch GNUS Transfer events on Ethereum mainnet
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain ethereum-mainnet \
    --watch-contract 0x614577036F0a024DBC1C88BA616b394DD65d105a \
    --watch-event "Transfer(address,address,uint256)"
```

---

## CLI reference

```text
eth_watch --chain <chain> [--watch-contract <addr> --watch-event <sig>] ...
eth_watch <host> <port> <peer_pubkey_hex> [eth_offset]
```

### Flags

| Flag | Description |
|------|-------------|
| `--chain <name>` | Use a canonical chain cache key (see below) |
| `--watch-contract <0x…>` | Contract address to filter events on |
| `--watch-event <sig>` | ABI event signature to match, e.g. `Transfer(address,address,uint256)` |
| `--log-level <level>` | Logging verbosity: `trace` `debug` `info` `warn` `error` (default `info`) |
| `--chain-peers-json <path>` | Load peers from a local chain peer cache |
| `--chain-peers-url <url>` | Override the remote chain peer cache URL used only when no local package JSON is found |
| `--no-chain-peers-url` | Disable remote fallback and use only package-local JSON/cache files |

`--watch-contract` and `--watch-event` must be paired and can be repeated for
multiple contracts/events.

### Direct connection (no discovery)

```text
eth_watch <host> <port> <peer_pubkey_hex> [eth_offset]
```

- `host` — peer IP or hostname
- `port` — peer TCP port
- `peer_pubkey_hex` — 128 hex chars (64-byte uncompressed public key, no `0x04` prefix)
- `eth_offset` — optional ETH subprotocol offset (default `0x10`)

---

## Chain cache keys (`--chain`)

| Name | Chain ID | Network |
|------|----------|---------|
| `ethereum-mainnet` | 1 | Ethereum Mainnet |
| `ethereum-sepolia` | 11155111 | Ethereum Sepolia testnet |
| `ethereum-holesky` | 17000 | Ethereum Holesky testnet |
| `polygon-mainnet` | 137 | Polygon Mainnet |
| `polygon-amoy` | 80002 | Polygon Amoy testnet |
| `bnb-smart-chain` | 56 | BNB Smart Chain |
| `bnb-smart-chain-testnet` | 97 | BNB Smart Chain testnet |
| `base-mainnet` | 8453 | Base Mainnet |
| `base-sepolia` | 84532 | Base Sepolia testnet |
| `gnosis-chain` | 100 | Gnosis Chain |

`eth_watch` matches `--chain` directly against top-level `chain_enodes.json(.gz)`
keys. Use the canonical keys in the table above.

### Optional local chain peer cache file

`eth_watch` can load chain metadata from a local JSON cache. The `nodes` array is
used as the RLPx/ETH dial candidate list. The `bootnodes` array is discovery-only:
it seeds discv4 fallback when `nodes` is empty, as expected for chains such as
Gnosis when the cache has no pre-scored peers.

```bash
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain ethereum-sepolia \
    --chain-peers-json /path/to/chain_enodes.json
```

The local chain peer cache file may be either plain JSON (`chain_enodes.json`) or a
gzip-compressed JSON file (`chain_enodes.json.gz`).

Expected file shape:

```json
{
  "ethereum-sepolia": {
    "networkId": 11155111,
    "genesisHex": "<32-byte genesis hash hex>",
    "forkId": "<4-byte fork hash hex>",
    "forkNext": "0",
    "nodes": [
      { "enode": "enode://<pubkey>@<ip>:<tcp-port>", "pubkey": "<128-hex-pubkey>" }
    ],
    "bootnodes": [
      { "enode": "enode://<pubkey>@<ip>:<discovery-port>" }
    ]
  }
}
```

JSON-style keys accepted by `eth_watch` include:
`ethereum-mainnet`, `ethereum-sepolia`, `ethereum-holesky`,
`polygon-mainnet`, `polygon-amoy`,
`bnb-smart-chain`, `bnb-smart-chain-testnet`,
`base-mainnet`, `base-sepolia`, and `gnosis-chain`.

Without `--chain-peers-json`, `eth_watch` refreshes its local `chain_enodes.json`
cache from the default remote URL when possible, then loads the cached file next to
the executable (and on macOS app bundles also under `Contents/Resources`). If the
refresh fails, startup continues with the existing local cache when present.

### Remote chain peer cache refresh (mobile + desktop)

When no local package `chain_enodes.json` is present, `eth_watch` falls back to:

`https://enodes.gnus.ai/chain_enodes.json.gz`

If the URL check fails or returns malformed data, startup continues with the last
cached file if present.

```bash
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain ethereum-sepolia \
    --chain-peers-url https://enodes.gnus.ai/chain_enodes.json.gz
```

Disable remote check:

```bash
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain ethereum-sepolia \
    --no-chain-peers-url
```

---

## GNUS contracts

| Chain | Contract address |
|-------|-----------------|
| Ethereum | `0x614577036F0a024DBC1C88BA616b394DD65d105a` |
| Polygon | `0x127E47abA094a9a87D084a3a93732909Ff031419` |
| BSC | `0x614577036F0a024DBC1C88BA616b394DD65d105a` |
| Base | `0x614577036F0a024DBC1C88BA616b394DD65d105a` |
| Sepolia | `0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70` |
| Polygon Amoy | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` |
| BSC Testnet | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` |
| Base Sepolia | `0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB` |

Source: https://docs.gnus.ai/resources/contracts/

---

## Watched events

The following ABI event signatures are pre-registered in `EventRegistry`
(`include/eth/eth_watch_cli.hpp`) and will be decoded with field names:

| Signature | Standard |
|-----------|----------|
| `Transfer(address,address,uint256)` | ERC-20 |
| `Approval(address,address,uint256)` | ERC-20 |
| `ApprovalForAll(address,address,bool)` | ERC-721 |
| `TransferSingle(address,address,address,uint256,uint256)` | ERC-1155 |
| `TransferBatch(address,address,address,uint256[],uint256[])` | ERC-1155 |
| `BridgeSourceBurned(address,uint256,uint256,uint256,uint256)` | GNUS Bridge |

`BridgeSourceBurned` is emitted by `GNUSBridge.sol` when a user calls `bridgeOut`
to move GNUS tokens to another chain.  Fields: `sender`, `id`, `amount`,
`srcChainID`, `destChainID`.

### SuperGenius chain IDs

| Chain ID | Network |
|----------|---------|
| `369` | SuperGenius Mainnet |
| `963` | SuperGenius Testnet |
| `144` | SuperGenius Devnet |

---

## Triggering events manually with `cast` (Foundry)

First, copy `.env.example` to `.env` and set your `PRIVATE_KEY`:

```bash
cp examples/.env.example examples/.env
# edit examples/.env — set PRIVATE_KEY=0x<64hexchars>
```

### Send an ERC-20 Transfer (Sepolia)

```bash
source examples/.env
cast send 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    "transfer(address,uint256)" \
    "$TEST_ADDRESS" 1 \
    --private-key "$PRIVATE_KEY" \
    --rpc-url https://ethereum-sepolia-rpc.publicnode.com
```

### Bridge GNUS to SuperGenius Testnet (Sepolia → chain 963)

`bridgeOut(uint256 amount, uint256 tokenId, uint256 destChainID)`

```bash
source examples/.env
cast send 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    "bridgeOut(uint256,uint256,uint256)" \
    100000000000000000 0 963 \
    --private-key "$PRIVATE_KEY" \
    --rpc-url https://ethereum-sepolia-rpc.publicnode.com
```

This emits a `BridgeSourceBurned` event:
- `amount` = `100000000000000000` (0.1 GNUS, 1e17 wei-equivalent)
- `id` = `0` (GNUS token ID in the ERC-1155 contract)
- `destChainID` = `963` (SuperGenius Testnet)

### Check GNUS balance before bridging

```bash
source examples/.env
cast call 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    "balanceOf(address)(uint256)" "$TEST_ADDRESS" \
    --rpc-url https://ethereum-sepolia-rpc.publicnode.com
```

---

## C++ example tests

`examples/eth_watch/eth_watch_example_test.cpp` is the compiled replacement for
the old shell smoke harness. It is registered with CTest as
`eth_watch_example_test` and validates the relay-facing example paths without
spawning `eth_watch`, scraping logs, or depending on public peer reachability:

1. **CachedChainMetadata** — loads chain metadata with `nodes` and `bootnodes`, builds GNUS watch specs, and starts `EthWatchService`.
2. **GnosisDiscoveryFallback** — loads Gnosis-style metadata with empty `nodes` and valid `bootnodes`, then verifies discv4 fallback is started.
3. **AllChainsConfig** — builds the multi-chain service config used by the example wrapper.

### Run

```bash
cd build/OSX/Debug && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja
ctest -R eth_watch_example_test --output-on-failure

# Or run the example test binary directly:
./examples/eth_watch/eth_watch_example_test
```

### Sending test transactions separately

```bash
# Send GNUS Transfer on all 4 mainnets
PRIVATE_KEY=0x... ./examples/send_test_transactions.sh

# Send on testnets only
PRIVATE_KEY=0x... ./examples/send_test_transactions.sh testnets

# Send on a single chain
PRIVATE_KEY=0x... ./examples/send_test_transactions.sh sepolia
```

Debug logs are written to `examples/logs/eth_watch_<timestamp>.log`.
