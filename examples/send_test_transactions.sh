#!/bin/bash
#
# send_test_transactions.sh — Send GNUS test transactions on multiple chains
#
# Sends a small GNUS Transfer on each selected chain for manual live event
# validation with the eth_watch example.
#
# Required environment variables:
#   PRIVATE_KEY   — 0x-prefixed private key of the sender wallet
#   TO_ADDRESS    — 0x-prefixed recipient address (default: sender itself)
#   AMOUNT        — Token amount in GNUS smallest units (default: 1)
#
# Optional RPC overrides (defaults to well-known public RPCs):
#   RPC_ETHEREUM  RPC_POLYGON  RPC_BSC  RPC_BASE
#   RPC_SEPOLIA   RPC_POLYGON_AMOY  RPC_BSC_TESTNET  RPC_BASE_SEPOLIA
#
# Usage:
#   PRIVATE_KEY=0x... ./send_test_transactions.sh                    # all 4 mainnets
#   PRIVATE_KEY=0x... ./send_test_transactions.sh testnets           # all 4 testnets
#   PRIVATE_KEY=0x... ./send_test_transactions.sh ethereum polygon   # specific chains
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# ── Auto-load .env if present ─────────────────────────────────────────────────
if [ -f "${SCRIPT_DIR}/.env" ]; then
    set -a
    # shellcheck disable=SC1091
    source "${SCRIPT_DIR}/.env"
    set +a
fi

# ── Colors ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

print_status()  { echo -e "${BLUE}[*]${NC} $1"; }
print_success() { echo -e "${GREEN}[✓]${NC} $1"; }
print_error()   { echo -e "${RED}[✗]${NC} $1"; }
print_warn()    { echo -e "${YELLOW}[!]${NC} $1"; }
print_chain()   { echo -e "${CYAN}[${1}]${NC} $2"; }

# ── Dependency check: requires Foundry cast ───────────────────────────────────
if ! command -v cast &>/dev/null; then
    print_error "'cast' (Foundry) not found."
    echo ""
    echo "  Install with:"
    echo "    curl -L https://foundry.paradigm.xyz | bash"
    echo "    foundryup"
    echo ""
    echo "  Or via Homebrew:"
    echo "    brew install foundry"
    exit 1
fi

# ── Validate required env vars ────────────────────────────────────────────────
if [ -z "${PRIVATE_KEY:-}" ]; then
    print_error "PRIVATE_KEY environment variable not set."
    echo "  Example: PRIVATE_KEY=0x<64hexchars> $0"
    exit 1
fi

# Basic sanity check — must be 0x + 64 hex chars
if ! echo "$PRIVATE_KEY" | grep -qE '^0x[0-9a-fA-F]{64}$'; then
    print_error "PRIVATE_KEY must be 0x-prefixed 32-byte hex (64 hex chars)."
    exit 1
fi

AMOUNT="${AMOUNT:-1}"

# ── Derive sender address from private key ────────────────────────────────────
SENDER=$(cast wallet address "$PRIVATE_KEY" 2>/dev/null)
if [ -z "$SENDER" ]; then
    print_error "Could not derive address from PRIVATE_KEY."
    exit 1
fi
print_status "Sender: $SENDER"

TO_ADDRESS="${TO_ADDRESS:-$SENDER}"
print_status "Recipient: $TO_ADDRESS"
print_status "Amount: $AMOUNT (raw token units)"

# ── GNUS.AI contract addresses & RPC endpoints ───────────────────────────────
# Source: https://docs.gnus.ai/resources/contracts/
gnus_contract() {
    case "$1" in
        ethereum)    echo "0x614577036F0a024DBC1C88BA616b394DD65d105a" ;;
        polygon)     echo "0x127E47abA094a9a87D084a3a93732909Ff031419" ;;
        bsc)         echo "0x614577036F0a024DBC1C88BA616b394DD65d105a" ;;
        base)        echo "0x614577036F0a024DBC1C88BA616b394DD65d105a" ;;
        sepolia)     echo "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" ;;
        polygon-amoy)  echo "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" ;;
        bsc-testnet)   echo "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" ;;
        base-sepolia)  echo "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" ;;
        *) echo "" ;;
    esac
}

rpc_url() {
    case "$1" in
        ethereum)    echo "${RPC_ETHEREUM:-https://eth.llamarpc.com}" ;;
        polygon)     echo "${RPC_POLYGON:-https://polygon-rpc.com}" ;;
        bsc)         echo "${RPC_BSC:-https://bsc-dataseed.binance.org}" ;;
        base)        echo "${RPC_BASE:-https://mainnet.base.org}" ;;
        sepolia)     echo "${RPC_SEPOLIA:-https://ethereum-sepolia-rpc.publicnode.com}" ;;
        polygon-amoy)  echo "${RPC_POLYGON_AMOY:-https://rpc-amoy.polygon.technology}" ;;
        bsc-testnet)   echo "${RPC_BSC_TESTNET:-https://data-seed-prebsc-1-s1.binance.org:8545}" ;;
        base-sepolia)  echo "${RPC_BASE_SEPOLIA:-https://sepolia.base.org}" ;;
        *) echo "" ;;
    esac
}

# ── ERC-20 transfer(address,uint256) selector ─────────────────────────────────
# keccak256("transfer(address,uint256)") = 0xa9059cbb
TRANSFER_SIG="transfer(address,uint256)"

# ── Select chains ─────────────────────────────────────────────────────────────
ARGS=("${@:-}")
if [ ${#ARGS[@]} -eq 0 ] || [ "${ARGS[0]}" = "mainnets" ]; then
    SELECTED_CHAINS="ethereum polygon bsc base"
elif [ "${ARGS[0]}" = "testnets" ]; then
    SELECTED_CHAINS="sepolia polygon-amoy bsc-testnet base-sepolia"
else
    SELECTED_CHAINS="${*}"
fi

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " send_test_transactions — GNUS Transfer on: $SELECTED_CHAINS"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

# ── Send helper ───────────────────────────────────────────────────────────────
send_transfer() {
    local chain="$1"
    local contract
    local rpc
    contract=$(gnus_contract "$chain")
    rpc=$(rpc_url "$chain")

    if [ -z "$contract" ]; then
        print_error "[$chain] No GNUS contract address configured."
        return 1
    fi
    if [ -z "$rpc" ]; then
        print_error "[$chain] No RPC URL configured."
        return 1
    fi

    print_chain "$chain" "Checking balance on $contract ..."

    # Check GNUS balance before sending
    local balance
    balance=$(cast call "$contract" "balanceOf(address)(uint256)" "$SENDER" \
        --rpc-url "$rpc" 2>/dev/null || echo "0")
    print_chain "$chain" "GNUS balance: $balance"

    if [ "$balance" = "0" ] || [ -z "$balance" ]; then
        print_warn "[$chain] Balance is 0 — transfer will likely revert. Attempting anyway..."
    fi

    print_chain "$chain" "Sending transfer($TO_ADDRESS, $AMOUNT) on $contract ..."

    local tx_hash
    tx_hash=$(cast send "$contract" \
        "$TRANSFER_SIG" \
        "$TO_ADDRESS" \
        "$AMOUNT" \
        --private-key "$PRIVATE_KEY" \
        --rpc-url "$rpc" \
        --json 2>&1 | grep -o '"transactionHash":"0x[0-9a-fA-F]*"' | cut -d'"' -f4 || true)

    if [ -n "$tx_hash" ]; then
        print_success "[$chain] TX sent: $tx_hash"
    else
        # Retry without --json to get a human-readable error
        local output
        output=$(cast send "$contract" \
            "$TRANSFER_SIG" \
            "$TO_ADDRESS" \
            "$AMOUNT" \
            --private-key "$PRIVATE_KEY" \
            --rpc-url "$rpc" 2>&1 || true)
        if echo "$output" | grep -qi "transactionHash\|0x[0-9a-fA-F]\{64\}"; then
            tx_hash=$(echo "$output" | grep -o '0x[0-9a-fA-F]\{64\}' | head -1)
            print_success "[$chain] TX sent: $tx_hash"
        else
            print_error "[$chain] Failed to send: $(echo "$output" | tail -3)"
            return 1
        fi
    fi
}

# ── Main loop ─────────────────────────────────────────────────────────────────
FAILED=0
for chain in $SELECTED_CHAINS; do
    if send_transfer "$chain"; then
        true
    else
        FAILED=$((FAILED + 1))
    fi
    echo ""
done

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if [ $FAILED -eq 0 ]; then
    print_success "All transactions submitted. Now run eth_watch for the target chain, for example:"
    echo ""
    echo "  ./eth_watch/eth_watch --chain ethereum-sepolia --watch-event 'Transfer(address,address,uint256)'"
    echo ""
    echo "  Events should appear within ~1-2 block confirmations."
else
    print_error "$FAILED chain(s) failed. Check errors above."
    exit 1
fi
