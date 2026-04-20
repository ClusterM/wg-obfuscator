#!/bin/bash
#
# Reproduces the silent-hang bug: wg-obfuscator uses blocking fprintf(stderr).
# If stderr goes to a pipe whose reader stops draining (e.g. journald stalls),
# the ~64 KiB pipe buffer fills up, write(2) blocks, and the single-threaded
# event loop freezes. The kernel UDP receive queue then grows until
# UdpRcvbufErrors appears — exactly what the 25-day-uptime production node saw.
#
# Strategy:
#   1. Start wg-obfuscator with stderr connected to a FIFO.
#   2. Start a reader on the FIFO and immediately SIGSTOP it — the FIFO is
#      "open for read" (so obfuscator's stderr doesn't EPIPE) but nothing
#      is ever consumed. Every write eventually blocks.
#   3. Flood the listen port with junk packets. At -v TRACE each packet
#      emits many log lines, so ~64 KiB of stderr accumulates in well under
#      a second.
#   4. Assert the process is parked in pipe_write and that the listen
#      socket's Recv-Q is growing (i.e. packets are not being drained).
#
# Linux-only: depends on /proc/<pid>/{stack,wchan} and `ss`.
#
# Exit codes (regression-gate semantics — obfuscator MUST stay responsive):
#   0 — fix holds (process draining normally, not parked in pipe_write)
#   1 — bug present (obfuscator parked in pipe_write and/or Recv-Q growing)
#   2 — test setup error (binary missing, port busy, etc.)

set -u

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OBFUSCATOR_BIN="${OBFUSCATOR_BIN:-$SCRIPT_DIR/../wg-obfuscator}"
LISTEN_PORT="${LISTEN_PORT:-34444}"
TARGET_PORT="${TARGET_PORT:-34445}"
TEST_KEY="stderr_block_test_key"
TEST_DIR="/tmp/wg-obfuscator-stderr-test"
FIFO="$TEST_DIR/stderr.pipe"
FLOOD_PACKETS="${FLOOD_PACKETS:-50000}"
STABILISE_SEC="${STABILISE_SEC:-3}"

OBFUSCATOR_PID=""
READER_PID=""
FLOOD_PID=""

cleanup() {
    [ -n "$FLOOD_PID" ]      && kill       "$FLOOD_PID"      2>/dev/null || true
    [ -n "$READER_PID" ]     && kill -CONT "$READER_PID"     2>/dev/null || true
    [ -n "$READER_PID" ]     && kill       "$READER_PID"     2>/dev/null || true
    [ -n "$OBFUSCATOR_PID" ] && kill -KILL "$OBFUSCATOR_PID" 2>/dev/null || true
    wait 2>/dev/null || true
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

die() {
    echo -e "${RED}[FAIL]${NC} $*" >&2
    exit 2
}

echo -e "${BLUE}[SETUP]${NC} Reproducer for stderr-pipe-blocking hang"

[ -x "$OBFUSCATOR_BIN" ] || die "obfuscator binary not found at $OBFUSCATOR_BIN (build with 'make' first)"
command -v ss >/dev/null 2>&1 || die "'ss' not found (iproute2 required)"
command -v python3 >/dev/null 2>&1 || die "python3 not found"
[ -r /proc/self/wchan ] || die "/proc/<pid>/wchan not readable — Linux required"

# Make sure the listen port is free (previous run might have lingered).
if ss -H -u -l -n "sport = :$LISTEN_PORT" | grep -q .; then
    die "port $LISTEN_PORT already in use"
fi

mkdir -p "$TEST_DIR"
rm -f "$FIFO"
mkfifo "$FIFO" || die "mkfifo failed"

# Reader that never reads. `sleep inf < fifo` is enough: the shell opens the
# fifo for reading (so writes on the other end don't EPIPE) but sleep never
# actually reads, so the pipe backs up.
sleep infinity < "$FIFO" &
READER_PID=$!

# Give the shell a moment to finish the open(); otherwise the obfuscator's
# `2> "$FIFO"` would also block on open().
sleep 0.2
kill -0 "$READER_PID" 2>/dev/null || die "reader process did not start"

echo -e "${BLUE}[SETUP]${NC} reader PID $READER_PID (will be SIGSTOPed after obfuscator starts)"

# Start obfuscator. -v TRACE makes every packet emit several stderr lines,
# so the 64 KiB pipe buffer fills almost instantly once the reader stops.
"$OBFUSCATOR_BIN" \
    -p "$LISTEN_PORT" \
    -t "127.0.0.1:$TARGET_PORT" \
    -k "$TEST_KEY" \
    -v TRACE \
    2> "$FIFO" &
OBFUSCATOR_PID=$!

sleep 0.5
kill -0 "$OBFUSCATOR_PID" 2>/dev/null || die "obfuscator did not start"
echo -e "${BLUE}[SETUP]${NC} obfuscator PID $OBFUSCATOR_PID"

# Freeze the reader. From now on, anything the obfuscator writes to stderr
# accumulates in the pipe buffer (64 KiB default on Linux).
kill -STOP "$READER_PID" || die "could not SIGSTOP reader"
echo -e "${BLUE}[SETUP]${NC} reader stopped — pipe buffer will fill on next stderr write"

# Flood the listen port with junk UDP packets. Content doesn't matter; at
# -v TRACE the obfuscator hex-dumps every packet and logs decode failures,
# so each packet is worth several hundred bytes of stderr.
python3 -u - "$LISTEN_PORT" "$FLOOD_PACKETS" <<'PYEOF' &
import socket, sys, os
port = int(sys.argv[1])
n    = int(sys.argv[2])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 1 << 20)
# 200-byte decoy packets. First 4 bytes != WG types 1..4 so is_obfuscated()
# returns true → obfuscator tries to decode, fails, logs at DEBUG/TRACE.
payload = b'\xAA\xBB\xCC\xDD' + os.urandom(196)
for _ in range(n):
    try:
        s.sendto(payload, ('127.0.0.1', port))
    except OSError:
        pass
PYEOF
FLOOD_PID=$!

echo -e "${BLUE}[RUN]${NC} flooding :$LISTEN_PORT with $FLOOD_PACKETS packets; waiting ${STABILISE_SEC}s for pipe to fill..."
sleep "$STABILISE_SEC"

# --- Observations -----------------------------------------------------------

WCHAN=$(cat "/proc/$OBFUSCATOR_PID/wchan" 2>/dev/null || echo "dead")
STACK=$(cat "/proc/$OBFUSCATOR_PID/stack" 2>/dev/null | head -5 || true)

# ss output for the listen socket. Recv-Q is what the kernel has buffered
# for the application to read; if the app is blocked, this grows.
SS_LINE=$(ss -H -u -n -l "sport = :$LISTEN_PORT" | head -1)
RECV_Q=$(awk '{print $2}' <<< "$SS_LINE")
# -m gives rb<size>; we pull the socket-level drops counter too if available.
SOCK_DROPS=$(ss -u -n -l --extended "sport = :$LISTEN_PORT" 2>/dev/null \
             | grep -oE 'drops:[0-9]+' | head -1 | cut -d: -f2)
SOCK_DROPS="${SOCK_DROPS:-0}"

echo
echo -e "${BLUE}[OBSERVE]${NC} obfuscator PID $OBFUSCATOR_PID"
echo "           wchan      : $WCHAN"
echo "           Recv-Q     : ${RECV_Q:-?} bytes"
echo "           socket drops : $SOCK_DROPS"
echo "           kernel stack :"
printf '             %s\n' $STACK

# --- Verdict ---------------------------------------------------------------

# The process is blocked on pipe write iff the kernel reports it waiting in
# pipe_write (or, on some kernels, appearing in /proc/PID/stack). Recv-Q
# growing in parallel is corroborating evidence that the main loop isn't
# draining the UDP socket.
# Linux 6.x renamed pipe_write → anon_pipe_write; accept both.
HUNG_IN_PIPE=0
case "$WCHAN" in
    pipe_write|anon_pipe_write|pipe_wait) HUNG_IN_PIPE=1 ;;
esac
if echo "$STACK" | grep -qE 'pipe_write|anon_pipe_write|pipe_wait'; then
    HUNG_IN_PIPE=1
fi

RECV_Q_NONZERO=0
if [[ "$RECV_Q" =~ ^[0-9]+$ ]] && [ "$RECV_Q" -gt 0 ]; then
    RECV_Q_NONZERO=1
fi

echo
if [ $HUNG_IN_PIPE -eq 1 ] && [ $RECV_Q_NONZERO -eq 1 ]; then
    echo -e "${RED}[FAIL: BUG PRESENT]${NC} obfuscator blocked in pipe_write AND UDP Recv-Q is non-zero"
    echo -e "             regression: the hang fix (Layer 2) is missing or reverted"
    exit 1
elif [ $HUNG_IN_PIPE -eq 1 ]; then
    echo -e "${RED}[FAIL: PROCESS STUCK]${NC} blocked in pipe_write (Recv-Q empty — try FLOOD_PACKETS=higher)"
    exit 1
elif [ $RECV_Q_NONZERO -eq 1 ]; then
    echo -e "${RED}[FAIL: UDP QUEUE GROWING]${NC} Recv-Q non-zero but wchan != pipe_write (wchan=$WCHAN)"
    echo -e "             process parked elsewhere; inspect /proc/$OBFUSCATOR_PID/stack manually"
    exit 1
else
    echo -e "${GREEN}[PASS]${NC} obfuscator draining normally (wchan=$WCHAN, Recv-Q=$RECV_Q)"
    echo -e "        the stderr-pipe-blocking fix is holding"
    exit 0
fi
