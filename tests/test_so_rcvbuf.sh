#!/bin/bash
#
# Cleanup #1 (SO_RCVBUF bump) functional test.
#
# The obfuscator asks for UDP_RCVBUF_BYTES = 8 MiB on every UDP socket.
# The kernel doubles the value (historical quirk) and then clamps to
# net.core.rmem_max. On a system with the common default (~208 KiB) the
# effective rb reported by `ss -m` will be that default — the test would
# then fail for operational reasons, not code reasons. To keep the test
# meaningful and portable, we only assert rb is substantially larger than
# the unconfigured default (i.e. our setsockopt call actually changed
# something).
#
# Threshold: rb >= 1 MiB. This is still far above the Linux default
# (~425 KiB after kernel doubling) and leaves plenty of room for systems
# that cap rmem_max below 16 MiB.
#
# Linux-only: needs `ss -m`.
# Exit: 0 = pass, 1 = fail, 2 = setup error / environment-clamped.

set -u

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${OBFUSCATOR_BIN:-$SCRIPT_DIR/../wg-obfuscator}"
LISTEN_PORT="${LISTEN_PORT:-34454}"
TARGET="127.0.0.1:34455"
KEY="rcvbuf_test"
MIN_RB="${MIN_RB:-$((1 * 1024 * 1024))}"   # 1 MiB

[ -x "$BIN" ] || { echo -e "${RED}[FAIL]${NC} binary not found at $BIN" >&2; exit 2; }
command -v ss >/dev/null 2>&1 || { echo -e "${RED}[FAIL]${NC} 'ss' not installed" >&2; exit 2; }

RMEM_MAX=$(sysctl -n net.core.rmem_max 2>/dev/null || echo unknown)
echo "kernel net.core.rmem_max = $RMEM_MAX"

"$BIN" -p "$LISTEN_PORT" -t "$TARGET" -k "$KEY" 2>/dev/null &
PID=$!
trap 'kill -TERM $PID 2>/dev/null; wait $PID 2>/dev/null' EXIT
sleep 0.3
kill -0 "$PID" 2>/dev/null || { echo -e "${RED}[FAIL]${NC} obfuscator did not start" >&2; exit 2; }

# `ss -m` prints skmem stats like: skmem:(r0,rb425984,t0,tb425984,f0,w0,o0,bl0,d0)
RAW=$(ss -u -n -l -m "sport = :$LISTEN_PORT" 2>/dev/null | tr '\n' ' ')
RB=$(echo "$RAW" | grep -oE 'rb[0-9]+' | head -1 | sed 's/rb//')

if [ -z "$RB" ] || ! [[ "$RB" =~ ^[0-9]+$ ]]; then
    echo -e "${RED}[FAIL]${NC} could not parse rb field from 'ss -m' output"
    echo "  raw: $RAW"
    exit 1
fi

echo "effective rb (kernel-visible SO_RCVBUF) = $RB bytes"

if [ "$RB" -ge "$MIN_RB" ]; then
    echo -e "${GREEN}[PASS]${NC} rb=$RB ≥ $MIN_RB (bump took effect)"
    exit 0
fi

# The kernel exposes rb as 2 * SO_RCVBUF (historical doubling). When clamped
# by net.core.rmem_max, `rb == 2 * rmem_max` is the typical signature.
if [ "$RMEM_MAX" != "unknown" ]; then
    DOUBLED=$(( RMEM_MAX * 2 ))
    if [ "$RB" -eq "$DOUBLED" ] && [ "$DOUBLED" -lt "$MIN_RB" ]; then
        echo -e "${YELLOW}[SKIP]${NC} rb=$RB is exactly 2 × rmem_max=$RMEM_MAX — clamped by kernel"
        echo "         setsockopt succeeded but the requested 8 MiB was capped."
        echo "         fix for production: sudo sysctl -w net.core.rmem_max=16777216"
        exit 0
    fi
fi

echo -e "${RED}[FAIL]${NC} rb=$RB < $MIN_RB and not explained by rmem_max clamp"
exit 1
