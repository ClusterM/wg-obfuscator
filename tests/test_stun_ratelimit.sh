#!/bin/bash
#
# Cleanup #5 (STUN BINDING_REQUEST rate-limit) functional test.
#
# The obfuscator is supposed to rate-limit STUN Binding Request replies to
# unknown clients (client == NULL in stun_on_data_unwrap) at 100 per second
# to prevent reflector-style abuse. This test floods 500 valid STUN requests
# from a single source and counts responses.
#
# Expected: ~100 responses (plus some slack for the 1-second window boundary
# being straddled during sending). Accept anything ≤ 250. Before the fix
# (v1.5 and earlier) every single request would be answered.
#
# Exit: 0 = pass, 1 = fail, 2 = setup error.

set -u

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${OBFUSCATOR_BIN:-$SCRIPT_DIR/../wg-obfuscator}"
LISTEN_PORT="${LISTEN_PORT:-34448}"
TARGET="127.0.0.1:34449"
KEY="stun_rl_test"

NUM_REQ="${NUM_REQ:-500}"
MAX_OK_RESPONSES="${MAX_OK_RESPONSES:-250}"  # 100/s * 2s window + slack

[ -x "$BIN" ] || { echo -e "${RED}[FAIL]${NC} binary not found at $BIN" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo -e "${RED}[FAIL]${NC} python3 not installed" >&2; exit 2; }

"$BIN" -p "$LISTEN_PORT" -t "$TARGET" -k "$KEY" 2>/dev/null &
PID=$!
trap 'kill -TERM $PID 2>/dev/null; wait $PID 2>/dev/null' EXIT

sleep 0.3
kill -0 "$PID" 2>/dev/null || { echo -e "${RED}[FAIL]${NC} obfuscator failed to start" >&2; exit 2; }

echo "flooding $NUM_REQ STUN BINDING_REQUEST packets at :$LISTEN_PORT ..."

RESULT=$(python3 -u - "$LISTEN_PORT" "$NUM_REQ" <<'PYEOF'
import socket, struct, os, sys, time
port = int(sys.argv[1])
n    = int(sys.argv[2])

MAGIC = b'\x21\x12\xA4\x42'
BINDING_REQ = 0x0001

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(('127.0.0.1', 0))
s.settimeout(0.02)

# Blast all requests as fast as we can.
t0 = time.monotonic()
for _ in range(n):
    txid = os.urandom(12)
    pkt  = struct.pack('>HH', BINDING_REQ, 0) + MAGIC + txid
    try:
        s.sendto(pkt, ('127.0.0.1', port))
    except OSError:
        pass
t_send = time.monotonic() - t0

# Drain responses for 1.5 seconds — enough for any spillover from the
# rate-limit bucket on the server side.
count = 0
deadline = time.monotonic() + 1.5
while time.monotonic() < deadline:
    try:
        data, _ = s.recvfrom(2048)
        count += 1
    except socket.timeout:
        pass

print(f"{count} {t_send:.3f}")
PYEOF
)

if [ -z "$RESULT" ]; then
    echo -e "${RED}[FAIL]${NC} python driver produced no output"
    exit 1
fi

COUNT=$(echo "$RESULT" | awk '{print $1}')
SEND_TIME=$(echo "$RESULT" | awk '{print $2}')

echo "received $COUNT responses to $NUM_REQ requests (send took ${SEND_TIME}s)"

if ! [[ "$COUNT" =~ ^[0-9]+$ ]]; then
    echo -e "${RED}[FAIL]${NC} could not parse response count from '$RESULT'"
    exit 1
fi

if [ "$COUNT" -gt "$MAX_OK_RESPONSES" ]; then
    echo -e "${RED}[FAIL]${NC} received $COUNT responses, expected ≤ $MAX_OK_RESPONSES (rate limit broken?)"
    exit 1
fi

if [ "$COUNT" -lt 1 ]; then
    echo -e "${RED}[FAIL]${NC} got ZERO responses — is the STUN handler even wired up?"
    exit 1
fi

echo -e "${GREEN}[PASS]${NC} rate limit holds: $COUNT ≤ $MAX_OK_RESPONSES"
exit 0
