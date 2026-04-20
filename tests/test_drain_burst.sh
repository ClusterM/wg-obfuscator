#!/bin/bash
#
# Cleanups #2 + #3 (non-blocking UDP + drain loop on EPOLLIN) functional test.
#
# Before the refactor, the event loop did one recvfrom per EPOLLIN — a burst
# of N packets required N epoll_wait round-trips to drain. With the drain
# loop (capped at DRAIN_BATCH_MAX = 128) a single notification processes up
# to 128 packets in-place.
#
# Observable: after flooding >10k packets, the listen-socket Recv-Q (what the
# kernel has buffered waiting for userspace to read) should drop to 0 within
# a few hundred milliseconds. We also verify the process doesn't die under
# EAGAIN stress now that sockets are non-blocking.
#
# Linux-only: needs `ss` from iproute2 and /proc.
# Exit: 0 = pass, 1 = fail, 2 = setup error.

set -u

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${OBFUSCATOR_BIN:-$SCRIPT_DIR/../wg-obfuscator}"
LISTEN_PORT="${LISTEN_PORT:-34452}"
TARGET="127.0.0.1:34453"
KEY="drain_test"
FLOOD="${FLOOD:-20000}"
MAX_DRAIN_MS="${MAX_DRAIN_MS:-500}"  # Recv-Q must fall to 0 within this window

[ -x "$BIN" ] || { echo -e "${RED}[FAIL]${NC} binary not found at $BIN" >&2; exit 2; }
command -v ss >/dev/null 2>&1 || { echo -e "${RED}[FAIL]${NC} 'ss' not installed" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo -e "${RED}[FAIL]${NC} python3 not installed" >&2; exit 2; }

"$BIN" -p "$LISTEN_PORT" -t "$TARGET" -k "$KEY" 2>/dev/null &
PID=$!
trap 'kill -TERM $PID 2>/dev/null; wait $PID 2>/dev/null' EXIT
sleep 0.3
kill -0 "$PID" 2>/dev/null || { echo -e "${RED}[FAIL]${NC} obfuscator did not start" >&2; exit 2; }

# Flood the port with junk packets in a tight loop.
echo "flooding $FLOOD packets into :$LISTEN_PORT ..."
python3 -u - "$LISTEN_PORT" "$FLOOD" <<'PYEOF' &
import socket, sys, os
port = int(sys.argv[1])
n    = int(sys.argv[2])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
# first byte != 1..4 so obfuscator treats it as obfuscated → tries decode → drops
payload = b'\xAA\xBB\xCC\xDD' + os.urandom(196)
for _ in range(n):
    try:
        s.sendto(payload, ('127.0.0.1', port))
    except OSError:
        pass
PYEOF
FLOOD_PID=$!

# Wait for flood to finish sending.
wait $FLOOD_PID

# Poll Recv-Q until it drops to 0 or we hit the timeout.
T0=$(date +%s%N)
DEADLINE=$(( T0 + MAX_DRAIN_MS * 1000000 ))
DRAINED=0
LAST_Q=0

while [ "$(date +%s%N)" -lt "$DEADLINE" ]; do
    Q=$(ss -H -u -n -l "sport = :$LISTEN_PORT" | awk 'NR==1 {print $2}')
    [ -z "$Q" ] && Q=0
    LAST_Q=$Q
    if [ "$Q" = "0" ]; then
        DRAINED=1
        break
    fi
    sleep 0.02
done

T1=$(date +%s%N)
MS=$(( (T1 - T0) / 1000000 ))

# Also confirm the obfuscator is still alive (drain + non-blocking didn't kill it).
if ! kill -0 "$PID" 2>/dev/null; then
    echo -e "${RED}[FAIL]${NC} obfuscator died during the flood"
    exit 1
fi

if [ "$DRAINED" -eq 1 ]; then
    echo -e "${GREEN}[PASS]${NC} Recv-Q drained to 0 in ${MS}ms (≤ ${MAX_DRAIN_MS}ms)"
    exit 0
else
    echo -e "${RED}[FAIL]${NC} Recv-Q still ${LAST_Q} bytes after ${MAX_DRAIN_MS}ms — drain loop not keeping up"
    exit 1
fi
