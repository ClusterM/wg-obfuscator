#!/bin/bash
#
# Layer 3 (syslog migration) functional test.
#
# Verifies log() routing at runtime:
#   1. Non-TTY stderr (systemd/pipe/file): the obfuscator must write NOTHING
#      to stderr and all log messages must go to syslog(3). Under systemd
#      this maps to journald with LOG_DAEMON facility + LOG_PID.
#   2. TTY stderr (interactive terminal): the obfuscator must write the
#      legacy "[section][level] msg" format to stderr, unchanged from v1.5.
#
# Linux-only: uses `script` to simulate a pty and `journalctl` to read the
# system journal. journalctl needs read access to system journal — usually
# root or membership in `systemd-journal`. If not available, the journald
# check is skipped (stderr check still runs).
#
# Exit codes: 0 = pass, 1 = fail, 2 = setup error.

set -u

GREEN='\033[0;32m'; RED='\033[0;31m'; YELLOW='\033[0;33m'; NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${OBFUSCATOR_BIN:-$SCRIPT_DIR/../wg-obfuscator}"
LISTEN_PORT="${LISTEN_PORT:-34446}"
TARGET="127.0.0.1:34447"   # deliberately no listener — we never actually forward
KEY="syslog_mode_test"
TEST_DIR="/tmp/wg-obfuscator-syslog-test"

[ -x "$BIN" ] || { echo -e "${RED}[FAIL]${NC} binary not found at $BIN" >&2; exit 2; }
command -v script >/dev/null 2>&1 || { echo -e "${RED}[FAIL]${NC} 'script' not installed" >&2; exit 2; }

mkdir -p "$TEST_DIR"

FAIL=0
warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
pass() { echo -e "${GREEN}[PASS]${NC} $*"; }
fail() { echo -e "${RED}[FAIL]${NC} $*"; FAIL=1; }

# ----------------------------------------------------------------------------
# Test 1: non-TTY stderr — nothing on stderr, log goes to syslog.
# ----------------------------------------------------------------------------
echo "[1/2] non-TTY mode: stderr silent, syslog receives messages"

STDERR_FILE="$TEST_DIR/non_tty_stderr.txt"
: > "$STDERR_FILE"

"$BIN" -p "$LISTEN_PORT" -t "$TARGET" -k "$KEY" 2> "$STDERR_FILE" &
PID=$!
# allow startup + banner emission
sleep 0.3
kill -TERM "$PID" 2>/dev/null
wait "$PID" 2>/dev/null
RC=$?

if [ $RC -ne 0 ]; then
    fail "binary exited with code $RC on clean SIGTERM (expected 0)"
fi

if [ -s "$STDERR_FILE" ]; then
    fail "non-TTY mode wrote to stderr (expected silence — should route to syslog)"
    echo "  stderr content:"; sed 's/^/    /' "$STDERR_FILE"
else
    pass "stderr was silent under non-TTY"
fi

# Best-effort: try to read the system journal for our PID.
if command -v journalctl >/dev/null 2>&1; then
    JOURNAL=$(journalctl _PID="$PID" --since="10 seconds ago" --no-pager 2>/dev/null || true)
    if [ -z "$JOURNAL" ]; then
        warn "journalctl returned nothing for PID $PID — need elevated privileges or"
        warn "         this environment doesn't forward syslog LOG_DAEMON to journald."
        warn "         Skipping the journal verification part (stderr silence is enough)."
    elif echo "$JOURNAL" | grep -q "Starting WireGuard Obfuscator"; then
        pass "journald received the startup banner via syslog"
    else
        fail "journald has entries for PID but no 'Starting WireGuard Obfuscator' line"
        echo "  journal dump:"; echo "$JOURNAL" | sed 's/^/    /'
    fi
else
    warn "journalctl not installed, skipping syslog->journald check"
fi

# ----------------------------------------------------------------------------
# Test 2: TTY stderr — messages appear on stderr in legacy format.
# ----------------------------------------------------------------------------
echo "[2/2] TTY mode: stderr receives '[main][I] ...'-style output"

# `script` gives us a pty, so isatty(STDERR_FILENO) returns true in the child.
# timeout via kill in the wrapped command since we can't rely on a shell timeout
# cleanly propagating into script.
TTY_OUT="$TEST_DIR/tty_out.txt"
script -q -c "( \"$BIN\" -p $LISTEN_PORT -t $TARGET -k $KEY & P=\$! ; sleep 0.3 ; kill -TERM \$P ; wait \$P )" /dev/null > "$TTY_OUT" 2>&1 || true

# `script` may append a typescript header on some distros; we only care about
# the content, so grep.
if grep -qE '^\[main\]\[I\] Starting WireGuard Obfuscator' "$TTY_OUT"; then
    pass "TTY mode emitted '[main][I] Starting...' on stderr"
else
    fail "TTY mode did NOT emit expected '[main][I] Starting...' line"
    echo "  captured output:"; sed 's/^/    /' "$TTY_OUT"
fi

# A clean-shutdown message should also arrive.
if grep -qE '^\[main\]\[I\] Stopped\.' "$TTY_OUT"; then
    pass "TTY mode emitted '[main][I] Stopped.' on shutdown"
else
    fail "TTY mode missing shutdown message"
fi

rm -rf "$TEST_DIR"
exit $FAIL
