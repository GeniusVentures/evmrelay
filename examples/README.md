# eth_watch example

`eth_watch` connects to an Ethereum peer via RLPx/DevP2P, performs the ETH Status
handshake, and watches for on-chain events matching registered ABI event signatures
(ERC-20 Transfer/Approval, ERC-721, ERC-1155, and GNUS Bridge events).

---

## Quick start

```bash
# Watch GNUS events on Sepolia
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain sepolia \
    --watch-contract 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    --watch-event "Transfer(address,address,uint256)" \
    --watch-contract 0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70 \
    --watch-event "BridgeSourceBurned(address,uint256,uint256,uint256,uint256)"

# Watch GNUS Transfer events on Ethereum mainnet
./build/OSX/Debug/examples/eth_watch/eth_watch \
    --chain mainnet \
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
| `--chain <name>` | Use a named chain preset (see below) |
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

## Chain presets (`--chain`)

| Name | Chain ID | Network |
|------|----------|---------|
| `mainnet` | 1 | Ethereum Mainnet |
| `sepolia` | 11155111 | Ethereum Sepolia testnet |
| `polygon` | 137 | Polygon Mainnet |
| `polygon-amoy` | 80002 | Polygon Amoy testnet |
| `bsc` | 56 | BNB Smart Chain |
| `bsc-testnet` | 97 | BNB Smart Chain testnet |
| `base` | 8453 | Base Mainnet |
| `base-sepolia` | 84532 | Base Sepolia testnet |

Bootnode lists: `include/rlp/PeerDiscovery/bootnodes.hpp` (mainnets) and
`include/rlp/PeerDiscovery/bootnodes_test.hpp` (testnets).

### Optional local chain peer cache file

`eth_watch` can load peers from a local JSON cache and dial those peers directly:

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
  "ethereum-sepolia": [
    { "enr": "enr:..." },
    { "enode": "enode://<pubkey>@<ip>:<port>", "pubkey": "<128-hex-pubkey>" }
  ]
}
```

`--chain` is matched directly against top-level JSON keys (no internal key mapping).
JSON-style keys accepted by `eth_watch` include:
`ethereum-mainnet`, `ethereum-sepolia`, `ethereum-holesky`,
`polygon-mainnet`, `polygon-amoy`,
`bnb-smart-chain`, `bnb-smart-chain-testnet`,
`base-mainnet`, `base-sepolia`.

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

## Functional test suite (`test_eth_watch.sh`)

`examples/test_eth_watch.sh` is a GTest-style end-to-end test that:

1. **Preflight** — verifies the binary, `cast`, and `PRIVATE_KEY` are available
2. **PeerConnection** — starts `eth_watch` on Sepolia and waits for `"Connected. Watching for events..."`
3. **SendERC20Transfer** — sends a 1-unit ERC-20 `transfer` to self via `cast send`
4. **SendBridgeOut** — calls `bridgeOut(0.1 GNUS, id=0, destChain=963)` to bridge to SuperGenius Testnet
5. **EventsDetected** — polls the debug log for `Transfer(…) at block` and `BridgeSourceBurned(…) at block` within `WATCH_TIMEOUT` (default 120 s)

Output format mirrors GTest (`[ RUN ]`, `[    OK ]`, `[ FAILED ]`) with per-test timing.

### Setup

```bash
# 1. Build (Debug or Release)
cd build/OSX/Debug && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja

# 2. Install Foundry cast
brew install foundry   # or: curl -L https://foundry.paradigm.xyz | bash && foundryup

# 3. Create wallet + .env
cp examples/.env.example examples/.env
# Paste your 0x-prefixed private key into examples/.env as PRIVATE_KEY=0x...
# Fund the wallet with GNUS on Sepolia: https://sepolia.etherscan.io

# 4. Run
cd /path/to/rlp
./examples/test_eth_watch.sh
```

### Environment overrides

| Variable | Default | Description |
|----------|---------|-------------|
| `PRIVATE_KEY` | *(required)* | 0x-prefixed 32-byte private key |
| `WATCH_TIMEOUT` | `120` | Seconds to wait for events after TX |
| `RPC_SEPOLIA` | public endpoint | Override the Sepolia RPC URL |

### Multi-chain smoke test

```bash
./examples/test_eth_watch.sh gnus-all-testnets   # Sepolia, Amoy, BSC testnet, Base Sepolia
./examples/test_eth_watch.sh all                 # all 4 mainnets (watch-only)
./examples/test_eth_watch.sh all --send          # mainnets + send test TX on each
```

### Watch-only live traffic smoke harness

For a lighter-weight live connectivity check that does not send transactions,
use `examples/test_eth_watch_smoke.sh`. It connects to each selected chain,
waits for the periodic `Watch stats:` line from `eth_watch`, and verifies that
live ETH traffic is being counted. By default the harness lets `eth_watch`
refresh and use its own local chain peer cache; `CHAIN_PEERS_JSON` is only needed
when you want to force a specific local `.json` or `.json.gz` file.

```bash
./examples/test_eth_watch_smoke.sh           # all chains
./examples/test_eth_watch_smoke.sh mainnets
./examples/test_eth_watch_smoke.sh testnets
./examples/test_eth_watch_smoke.sh sepolia bsc-testnet
```

Optional environment overrides:

```bash
CONNECT_TIMEOUT=20
SMOKE_DURATION=12
CHAIN_PEERS_JSON=./rlp_enodes/output/chain_enodes.json.gz
CHAIN_PEERS_URL=https://enodes.gnus.ai/chain_enodes.json.gz
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
