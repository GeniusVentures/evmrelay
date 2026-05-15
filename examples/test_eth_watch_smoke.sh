#!/bin/bash
#
# examples/test_eth_watch_smoke.sh
#
# Watch-only multi-chain smoke harness for eth_watch.
# Verifies that each selected chain:
#   1. Connects to a live peer
#   2. Stays alive long enough to emit periodic Watch stats lines
#   3. Receives at least some ETH traffic
#
# Usage:
#   ./examples/test_eth_watch_smoke.sh                # all chains
#   ./examples/test_eth_watch_smoke.sh mainnets
#   ./examples/test_eth_watch_smoke.sh testnets
#   ./examples/test_eth_watch_smoke.sh sepolia bsc-testnet
#
# Optional env overrides:
#   CONNECT_TIMEOUT=20
#   SMOKE_DURATION=12
#   CHAIN_PEERS_JSON=/path/to/chain_enodes.json
#   CHAIN_PEERS_URL=https://enodes.gnus.ai/chain_enodes.json.gz

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="${SCRIPT_DIR}/.."
TIMESTAMP=$(date +"%Y%m%d_%H%M%S")
LOG_DIR="${SCRIPT_DIR}/logs/smoke_${TIMESTAMP}"
mkdir -p "$LOG_DIR"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BOLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'

now_ms()
{
    python3 -c 'import time; print(int(time.time()*1000))'
}

SUITE_START=$(now_ms)
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0
CURRENT_TEST=""
CURRENT_TEST_START=0
WATCH_PID=""
CURRENT_LOG=""

suite_header()
{
    echo ""
    echo -e "${BOLD}[==========]${NC} eth_watch smoke test suite"
    echo -e "${BOLD}[----------]${NC} Live multi-chain watch-only harness"
}

test_start()
{
    CURRENT_TEST="$1"
    CURRENT_TEST_START=$(now_ms)
    TESTS_RUN=$((TESTS_RUN + 1))
    echo -e "${BOLD}${GREEN}[ RUN      ]${NC} ${CURRENT_TEST}"
}

test_pass()
{
    local elapsed=$(( $(now_ms) - CURRENT_TEST_START ))
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "${GREEN}[       OK ]${NC} ${CURRENT_TEST} (${elapsed} ms)"
    if [ -n "${1:-}" ]; then
        echo -e "            ${DIM}${1}${NC}"
    fi
}

test_fail()
{
    local elapsed=$(( $(now_ms) - CURRENT_TEST_START ))
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "${RED}[  FAILED  ]${NC} ${CURRENT_TEST} (${elapsed} ms)"
    if [ -n "${1:-}" ]; then
        echo -e "            ${RED}${1}${NC}"
    fi
}

info()
{
    echo -e "            ${DIM}$1${NC}"
}

cleanup_watch()
{
    if [ -n "$WATCH_PID" ] && kill -0 "$WATCH_PID" 2>/dev/null; then
        kill "$WATCH_PID" 2>/dev/null || true
        wait "$WATCH_PID" 2>/dev/null || true
    fi
    WATCH_PID=""
}

suite_footer()
{
    local elapsed=$(( $(now_ms) - SUITE_START ))
    echo ""
    echo -e "${BOLD}[==========]${NC} ${TESTS_RUN} test(s) ran (${elapsed} ms total)"
    if [ $TESTS_PASSED -gt 0 ]; then
        echo -e "${GREEN}[  PASSED  ]${NC} ${TESTS_PASSED} test(s)"
    fi
    if [ $TESTS_FAILED -gt 0 ]; then
        echo -e "${RED}[  FAILED  ]${NC} ${TESTS_FAILED} test(s)"
        echo -e "${RED}Logs:${NC} ${LOG_DIR}"
    else
        echo -e "${DIM}Logs: ${LOG_DIR}${NC}"
    fi
    echo ""
}

cleanup()
{
    cleanup_watch
    suite_footer
}
trap cleanup EXIT INT TERM

CONNECT_TIMEOUT=${CONNECT_TIMEOUT:-20}
SMOKE_DURATION=${SMOKE_DURATION:-12}
CHAIN_PEERS_JSON=${CHAIN_PEERS_JSON:-${BOOTSTRAP_JSON:-}}
CHAIN_PEERS_URL=${CHAIN_PEERS_URL:-${BOOTSTRAP_URL:-}}

ETH_WATCH_BIN=""
for build_type in Debug Release RelWithDebInfo; do
    candidate="${REPO_ROOT}/build/OSX/${build_type}/examples/eth_watch/eth_watch"
    if [ -f "$candidate" ]; then
        ETH_WATCH_BIN="$candidate"
        ETH_WATCH_BUILD="$build_type"
        break
    fi
done

chain_contract()
{
    case "$1" in
        mainnet|ethereum-mainnet) echo "0x614577036F0a024DBC1C88BA616b394DD65d105a" ;;
        sepolia|ethereum-sepolia) echo "0x9af8050220D8C355CA3c6dC00a78B474cd3e3c70" ;;
        polygon|polygon-mainnet) echo "0x127E47abA094a9a87D084a3a93732909Ff031419" ;;
        polygon-amoy) echo "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" ;;
        bsc|bnb-smart-chain) echo "0x614577036F0a024DBC1C88BA616b394DD65d105a" ;;
        bsc-testnet|bnb-smart-chain-testnet) echo "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" ;;
        base|base-mainnet) echo "0x614577036F0a024DBC1C88BA616b394DD65d105a" ;;
        base-sepolia) echo "0xeC20bDf2f9f77dc37Ee8313f719A3cbCFA0CD1eB" ;;
        *) echo "" ;;
    esac
}

expand_selection()
{
    if [ $# -eq 0 ] || [ "$1" = "all" ]; then
        echo "ethereum-mainnet ethereum-sepolia polygon-mainnet polygon-amoy bnb-smart-chain bnb-smart-chain-testnet base-mainnet base-sepolia"
        return
    fi
    if [ "$1" = "mainnets" ]; then
        echo "ethereum-mainnet polygon-mainnet bnb-smart-chain base-mainnet"
        return
    fi
    if [ "$1" = "testnets" ] || [ "$1" = "gnus-all-testnets" ]; then
        echo "ethereum-sepolia polygon-amoy bnb-smart-chain-testnet base-sepolia"
        return
    fi
    echo "$*"
}

canonical_chain()
{
    case "$1" in
        mainnet) echo "ethereum-mainnet" ;;
        sepolia) echo "ethereum-sepolia" ;;
        polygon) echo "polygon-mainnet" ;;
        bsc) echo "bnb-smart-chain" ;;
        bsc-testnet) echo "bnb-smart-chain-testnet" ;;
        base) echo "base-mainnet" ;;
        *) echo "$1" ;;
    esac
}

extract_stat()
{
    local line="$1"
    local key="$2"
    echo "$line" | sed -nE "s/.*${key}=([0-9]+).*/\1/p"
}

suite_header

test_start "EthWatchSmoke.Preflight"
preflight_ok=true
if [ -z "$ETH_WATCH_BIN" ]; then
    test_fail "eth_watch binary not found under build/OSX/*/examples/eth_watch/"
    info "Build: cd build/OSX/Debug && cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Debug && ninja eth_watch"
    preflight_ok=false
fi
if [ ! -f "$CHAIN_PEERS_JSON" ]; then
    if [ -n "$CHAIN_PEERS_JSON" ]; then
        info "Chain peer cache file not found: ${CHAIN_PEERS_JSON}"
        preflight_ok=false
    else
        info "Chain peers: using eth_watch managed cache/remote refresh"
    fi
fi
if $preflight_ok; then
    if [ -n "$CHAIN_PEERS_JSON" ]; then
        test_pass "binary=${ETH_WATCH_BUILD} connect_timeout=${CONNECT_TIMEOUT}s smoke_duration=${SMOKE_DURATION}s chain_peers_json=${CHAIN_PEERS_JSON}"
    elif [ -n "$CHAIN_PEERS_URL" ]; then
        test_pass "binary=${ETH_WATCH_BUILD} connect_timeout=${CONNECT_TIMEOUT}s smoke_duration=${SMOKE_DURATION}s chain_peers_url=${CHAIN_PEERS_URL}"
    else
        test_pass "binary=${ETH_WATCH_BUILD} connect_timeout=${CONNECT_TIMEOUT}s smoke_duration=${SMOKE_DURATION}s chain_peers=managed-cache"
    fi
else
    exit 1
fi

echo ""
SELECTED_CHAINS=$(expand_selection "$@")
for chain in $SELECTED_CHAINS; do
    chain=$(canonical_chain "$chain")
    contract=$(chain_contract "$chain")
    test_start "EthWatchSmoke.${chain}"

    if [ -z "$contract" ]; then
        test_fail "No GNUS contract configured for chain ${chain}"
        echo ""
        continue
    fi

    CURRENT_LOG="${LOG_DIR}/${chain}.log"
    cmd=("$ETH_WATCH_BIN" "--chain" "$chain" "--log-level" "info"
         "--watch-contract" "$contract" "--watch-event" "Transfer(address,address,uint256)")
    if [ -f "$CHAIN_PEERS_JSON" ]; then
        cmd+=("--chain-peers-json" "$CHAIN_PEERS_JSON")
    fi
    if [ -n "$CHAIN_PEERS_URL" ]; then
        cmd+=("--chain-peers-url" "$CHAIN_PEERS_URL")
    fi

    if command -v stdbuf >/dev/null 2>&1; then
        ASAN_OPTIONS=halt_on_error=0:replace_intrin=0:detect_stack_use_after_return=0:poison_heap=0 \
            stdbuf -oL "${cmd[@]}" > "$CURRENT_LOG" 2>&1 &
    else
        ASAN_OPTIONS=halt_on_error=0:replace_intrin=0:detect_stack_use_after_return=0:poison_heap=0 \
            "${cmd[@]}" > "$CURRENT_LOG" 2>&1 &
    fi
    WATCH_PID=$!

    connected=false
    for ((elapsed=0; elapsed<CONNECT_TIMEOUT; ++elapsed)); do
        if grep -q "Connected\. Watching" "$CURRENT_LOG" 2>/dev/null; then
            connected=true
            break
        fi
        if ! kill -0 "$WATCH_PID" 2>/dev/null; then
            break
        fi
        sleep 1
    done

    if ! $connected; then
        cleanup_watch
        test_fail "Failed to connect within ${CONNECT_TIMEOUT}s"
        info "Log: ${CURRENT_LOG}"
        echo ""
        continue
    fi

    sleep "$SMOKE_DURATION"
    cleanup_watch

    stats_line=$(grep "Watch stats:" "$CURRENT_LOG" | tail -1 || true)
    if [ -z "$stats_line" ]; then
        test_fail "No periodic Watch stats line observed"
        info "Log: ${CURRENT_LOG}"
        echo ""
        continue
    fi

    eth_messages=$(grep "Watch stats:" "$CURRENT_LOG" | sed -nE 's/.*eth_messages=([0-9]+).*/\1/p' | sort -nr | head -1 || true)
    discarded_logs=$(extract_stat "$stats_line" "discarded_logs")
    logs_seen=$(extract_stat "$stats_line" "logs_seen")

    if [ -z "$eth_messages" ]; then
        test_fail "Could not parse eth_messages from Watch stats"
        info "$stats_line"
        info "Log: ${CURRENT_LOG}"
        echo ""
        continue
    fi

    if [ "$eth_messages" -eq 0 ]; then
        test_fail "Connected, but no ETH traffic counted during smoke window"
        info "$stats_line"
        info "Log: ${CURRENT_LOG}"
        echo ""
        continue
    fi

    test_pass "eth_messages=${eth_messages} logs_seen=${logs_seen:-0} discarded_logs=${discarded_logs:-0}"
    info "$stats_line"
    info "Log: ${CURRENT_LOG}"
    echo ""
done

[ $TESTS_FAILED -eq 0 ] && EXIT_CODE=0 || EXIT_CODE=1
trap - EXIT
cleanup
exit $EXIT_CODE
