#!/bin/bash
#
# Cleanup #4 (async-signal-safe signal handler) functional test.
#
# Before the refactor, signal_handler() did free/fprintf/exit directly from
# the signal context — a POSIX async-safety violation that could deadlock on
# the libc malloc mutex. After the refactor, the handler only sets a flag
# and the main loop drives cleanup_and_exit() in a normal context.
#
# This test checks the observable contract:
#   1. SIGTERM causes a prompt, clean exit (code 0).
#   2. The shutdown message ("Stopped.") is emitted.
#   3. No hang beyond a small grace window (1 second).
#
# Exit: 0 = pass, 1 = fail, 2 = setup error.

set -u

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${OBFUSCATOR_BIN:-$SCRIPT_DIR/../wg-obfuscator}"
LISTEN_PORT="${LISTEN_PORT:-34450}"
TARGET="127.0.0.1:34451"
KEY="sigterm_test"
GRACE_SEC="${GRACE_SEC:-2}"

[ -x "$BIN" ] || { echo -e "${RED}[FAIL]${NC} binary not found at $BIN" >&2; exit 2; }
command -v script >/dev/null 2>&1 || { echo -e "${RED}[FAIL]${NC} 'script' not installed" >&2; exit 2; }

# Force TTY mode so we can read "Stopped." directly from stderr without
# needing journalctl privileges.
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

# Script invokes binary under a pty; we terminate via SIGTERM and record the
# total wall-clock time to detect a hang.
T0=$(date +%s%N)
script -q -c "( \"$BIN\" -p $LISTEN_PORT -t $TARGET -k $KEY & P=\$! ; echo \"PID=\$P\" ; sleep 0.4 ; kill -TERM \$P ; wait \$P ; echo \"RC=\$?\" )" /dev/null > "$OUT" 2>&1 || true
T1=$(date +%s%N)

ELAPSED_MS=$(( (T1 - T0) / 1000000 ))

RC=$(grep -oP '^RC=\K[0-9]+' "$OUT" | head -1)
PID=$(grep -oP '^PID=\K[0-9]+' "$OUT" | head -1)

FAIL=0
pass() { echo -e "${GREEN}[PASS]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; FAIL=1; }

if [ -z "$RC" ]; then
    fail "could not read child exit code from wrapped output"
    echo "  output:"; sed 's/^/    /' "$OUT"
    exit 1
fi

if [ "$RC" != "0" ]; then
    fail "SIGTERM produced exit code $RC (expected 0)"
else
    pass "SIGTERM → exit 0"
fi

if [ "$ELAPSED_MS" -gt "$((GRACE_SEC * 1000))" ]; then
    fail "shutdown took ${ELAPSED_MS}ms (grace ${GRACE_SEC}s exceeded — suspect signal handler hang)"
else
    pass "shutdown completed in ${ELAPSED_MS}ms (≤ ${GRACE_SEC}s grace)"
fi

if grep -qE '^\[main\]\[I\] Stopped\.' "$OUT"; then
    pass "shutdown message 'Stopped.' emitted"
else
    fail "no 'Stopped.' line observed on shutdown"
    echo "  output:"; sed 's/^/    /' "$OUT"
fi

exit $FAIL
