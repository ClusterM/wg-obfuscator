# Fix Plan: Silent Hang Under Load (v1.5)

## Problem

Production node, 25-day uptime:

```
UdpInErrors:     1,356,407
UdpRcvbufErrors: 1,356,332   (99.99% of all UDP errors)
```

Process appears alive but silently stops forwarding packets; journal shows nothing.

## Root Cause (confirmed)

`wg-obfuscator` is single-threaded and logs via `fprintf(stderr, ...)`
(macro `log()` in `wg-obfuscator.h:49-59`). On systemd the unit's stderr
is a pipe to `systemd-journald`. If journald stops draining that pipe
(restart, crash, disk pressure, rate-limit stall), the 64 KiB pipe
buffer eventually fills and the next `write(2)` on stderr blocks.
The event loop is single-threaded, so *any* blocking `write` freezes
packet forwarding entirely. The kernel UDP receive queue then fills
and `UdpRcvbufErrors` grows.

**Note on the production verbosity.** `-v 1` in the systemd unit maps
to `LL_WARN`, not DEBUG — numeric values go through `atoi` in
`config.c:344-349` and the level constants in `wg-obfuscator.h:43-47`
are `ERROR=0, WARN=1, INFO=2, DEBUG=3, TRACE=4`. Per-packet `log()`
calls at `LL_DEBUG`/`LL_TRACE` (`wg-obfuscator.c:586, 608, 640, 672,
749, 778, 792, 823, ...`) therefore **did not fire** in prod. This is
consistent with the observed ~19 749 journal lines over 25 days
(~0.009 lines/s) — WARN-volume, not DEBUG-volume.

**How a low-volume logger still hangs a pipe.** The trigger is a
*stalled reader*, not a flooding writer. Sequence:

1. `systemd-journald` stops consuming the unit's stderr pipe (confirmed
   on prod: journald auto-restarted during the incident window).
2. The obfuscator keeps writing sparse WARN lines. Each one adds a few
   hundred bytes to the pipe. At ~0.01 lines/s, filling 64 KiB of pipe
   buffer takes on the order of hours-to-days — which matches the
   25-day uptime leading to a single latent hang rather than continuous
   flapping.
3. Eventually one `write(2)` hits the full-pipe condition and blocks.
4. Event loop frozen → UDP not drained → `UdpRcvbufErrors` climbs.
5. Process appears alive (no crash, no log) but forwards nothing.

**Reproducer (`tests/test_stderr_block.sh`)** accelerates step 2 by
running the obfuscator at `-v TRACE` under a deliberately SIGSTOPped
reader, so the pipe fills in ~3 s instead of days. The *mechanism*
being exercised (blocking `write` on a backed-up pipe) is identical;
`-v TRACE` is just a test accelerator. On the reproducer the process
parks in `anon_pipe_write` and Recv-Q grows to >200 KiB.

**What is not established.** We don't have evidence from the prod
incident itself (no core, no `/proc/$PID/stack` snapshot captured
during the hang). The chain is: observed symptom (Recv-Q drops +
silent process) + observed environment state (journald restarted +
journal inaccessible) + code review (blocking stderr + no signal-safe
backpressure) + live reproduction of the same mechanism under
laboratory conditions. Any operator who captures `wchan` on the next
recurrence and finds it in `{pipe_write, anon_pipe_write, pipe_wait}`
turns this from "highly probable" into proof.

## Fix Layers

Three layers; apply all three. Layer 1 is ops-only (no code change) and
takes effect immediately; layer 2 is a tiny, targeted patch that removes
the hang class; layer 3 is the architecturally correct long-term fix.

---

### Layer 1 — Operational mitigation (0 code)

**What:** Detach the service's stderr from the journald pipe. Log
verbosity is a secondary concern on this path (prod was already at
`LL_WARN`, which is low-volume; the stall happened because the reader
stopped, not because the writer flooded).

**Why:** As long as stderr goes into a pipe whose reader can stall,
any future journald restart/crash can eventually freeze the obfuscator.
Removing the pipe removes the trigger class.

**How to apply:**

```ini
# /etc/systemd/system/wg-obfuscator.service.d/override.conf
[Service]
# Safest — drops logs, but the process can never block on stderr:
StandardError=null
# Or, if logs are still desired, write to a regular file:
# StandardError=append:/var/log/wg-obfuscator.err
# A full-disk condition on the log volume can still block writes,
# but that is a different and louder failure mode.
```

```bash
systemctl daemon-reload && systemctl restart wg-obfuscator
```

Optionally also lower `-v` to `error` to minimise log rate — not
required to prevent this bug, but reduces blast radius if someone
later points stderr back at a pipe.

**Effect:** removes the trigger in current environment. Does **not**
fix the latent bug — any future operator running the binary under a
pipe-based stderr (journald, `tee`, `logger`, etc.) can hit the same
hang if that reader stalls.

---

### Layer 2 — Minimal code fix: non-blocking stderr (≤10 LOC)

**What:** Set `O_NONBLOCK` on `STDERR_FILENO` at process startup.

**Why:** Under pipe pressure, `write(2)` then returns `EAGAIN` instead of
blocking. `fprintf` silently drops the line (acceptable for a packet
proxy) and the event loop continues draining UDP. No behavioral change
when journald is healthy.

**Where:** `wg-obfuscator.c`, as the **first** statement in `main()`
— before `print_version()` (currently `wg-obfuscator.c:290`) and
before any other call that could touch stderr. `print_version()`
itself writes via `fprintf(stderr, ...)` at `wg-obfuscator.c:256-269`;
if the pipe is already full at process start (journald already
stalled), a write there will block before we ever get to the `fcntl`
call. The ordering matters — the whole point of this patch is to
close the startup path too.

**Patch:**

```c
// wg-obfuscator.c — add near existing includes
#include <fcntl.h>

int main(int argc, char *argv[]) {
    // MUST be the first thing we do. Any fprintf(stderr,...) before
    // this point can hang if journald (or whatever stderr is piped to)
    // has already stalled when the process starts — including
    // print_version() below, and including log()/serror() macros
    // reachable from very early error paths.
    {
        int f = fcntl(STDERR_FILENO, F_GETFL, 0);
        if (f >= 0) {
            (void)fcntl(STDERR_FILENO, F_SETFL, f | O_NONBLOCK);
        }
        // glibc defaults stderr to FULLY-buffered when it's not a tty
        // (i.e. exactly our systemd case). With full buffering,
        // fprintf accumulates into a 4 KiB FILE-buffer and flushes
        // in one large write(2) — under O_NONBLOCK that can become
        // a partial write with EAGAIN, and glibc then marks ferror()
        // on the stream; subsequent fprintfs still buffer but won't
        // retry-flush until clearerr(3). Forcing line-buffered mode
        // makes every '\n' a discrete write(2) call ≤ PIPE_BUF bytes,
        // which POSIX guarantees is atomic on pipes — so on pipe-full
        // the write either succeeds whole or fails whole (EAGAIN),
        // and we lose at most one individual log line.
        setvbuf(stderr, NULL, _IOLBF, 0);
    }

    obfuscator_config_t config;
    ...
    print_version();      // now safe: pipe-full → EAGAIN, not block
    ...
}
```

**Edge cases:**

- Under bursts individual log lines (not whole batches) are lost on
  pipe-full — the `setvbuf(_IOLBF)` above makes each `\n`-terminated
  `log()` call its own atomic `write(2)` up to `PIPE_BUF=4096` bytes.
  `log()` call sites always end with a newline (macro-injected,
  `wg-obfuscator.h:50`), so granularity is per-log-line.
- **Caveat for `trace()`** (`wg-obfuscator.h:60`, used from
  `wg-obfuscator.c:593, 597-600, 703-706, 760-763, 853-856, ...`).
  `trace()` is **not** a single `fprintf` — it's invoked per hex
  byte in a loop, with a final `trace("\n")` closing the line. Under
  `_IOLBF`, glibc accumulates into the FILE buffer until it sees
  `\n` OR until the buffer fills (4 KiB default). For packets up to
  ~1300 bytes the hex dump (`3*N` characters ≈ 4 KiB) fits in one
  buffer flush; for larger packets the buffer fills mid-dump,
  triggering a non-atomic multi-`write(2)` flush. Consequences:
  (1) on pipe-full mid-flush we lose a partial hex dump, not a full
  line — log readers see a truncated record; (2) no hang, because
  each individual `write(2)` is still non-blocking. This is
  acceptable for a debug-only facility but means the "one line, one
  syscall" invariant above does **not** hold for `trace()`. Under
  `_IOLBF`, a stream with no `\n` in its buffered content behaves
  identically to `_IOFBF` until the terminating `\n` arrives — so
  the per-byte `trace()` dump accumulates in the FILE buffer the
  same way either setting would produce.
  Mitigations, any of which are sufficient:
  - Gate `trace()` behind `#ifdef DEBUG` (already recommended as
    adjacent-cleanup, and the natural fix for 1.6);
  - Rewrite `trace()` callers to format the whole hex line into a
    local `char buf[4096]` and emit via a single `fprintf(stderr,
    "%s\n", buf)`;
  - Raise the stdio buffer with `setvbuf(stderr, NULL, _IOFBF, 65536)`
    so even a full 65 KiB packet dump fits — but that regresses
    atomic-per-line behavior for the structured `log()` macro, so
    not recommended.
- `stderr` is the shared underlying fd; multi-section configs (forked
  instances) share the same fd. `O_NONBLOCK` set once in the parent
  propagates to children via `fork()`. Safe.
- No effect when stderr is a regular file or `/dev/null` (non-blocking
  on those has no downside; writes succeed immediately).
- If `STDERR_FILENO` was closed before `exec` (rare but possible),
  `fcntl` returns -1 and we silently continue — no log output will
  work anyway, and we don't want a startup abort on that.

**Verification:**

1. `tests/test_stderr_block.sh` must flip from `[REPRODUCED]` (exit 0)
   to `[NOT REPRODUCED]` (exit 1).
2. `make test` still passes (no regression in unit + integration tests).
3. Manual: run with `-v TRACE`, redirect stderr to a blocked pipe,
   confirm obfuscator keeps forwarding via a parallel
   `test_wg_emulator` client.

---

### Layer 3 — Architectural fix: switch logging to `syslog(3)`

**What:** Replace `fprintf(stderr, ...)` with `syslog(3)` for all
structured log levels. Keep `trace()` hex-dumps behind `LL_TRACE` either
in `stderr` (gated by `verbose`) or also via `syslog(LOG_DEBUG, ...)`
with pre-formatted lines.

**Why:** `syslog(3)` writes to `/dev/log` (a `SOCK_DGRAM` Unix socket).
Datagram sockets never hold up a writer for long; if journald is behind,
the kernel drops or returns `EAGAIN`. No pipe-fill class of hangs is
possible. Bonus:

- Log levels show up in `journalctl -p warning` etc.
- Multi-section separation still works because the `log()` macro
  sketch below prepends `section_name` inside the message body. The
  syslog ident itself is shared between parent and forked sections
  (`openlog()` state survives `fork(2)`), so a per-section ident
  would require an extra `openlog(section_name, ...)` call in each
  child immediately after `fork()` — cheap but needs a follow-up
  audit of the forking code in `config.c` to be wired in the right
  place. Not required for correctness; the `[%s]` prefix in the
  message already gives unambiguous separation in `journalctl`.
- Works identically under any init system (sysvinit, OpenRC, runit).

**Where:**

- `wg-obfuscator.h:49-62` — rewrite `log()`, `serror_level()` macros.
- `wg-obfuscator.c` — call `openlog()` as the **first** statement in
  `main()` (before `print_version()`). Do **not** put `closelog()` in
  `signal_handler()` — see the note below and adjacent-cleanup #4;
  `closelog()` is not async-signal-safe, and the kernel tears down
  the syslog socket at `exit(2)` regardless. Rewrite `print_version()`
  (`wg-obfuscator.c:256-269`) to use `syslog(LOG_INFO, ...)` instead
  of `fprintf(stderr, ...)` — otherwise the same startup-hang class
  Layer 2 closes reopens here, because `print_version()` runs before
  `openlog()` has any effect on stderr and still writes directly.
  Either: (a) move `openlog()` to the very first line and replace
  the four `fprintf(stderr, ...)` in `print_version()`
  (`wg-obfuscator.c:259, 261, 265, 267`, behind `#ifdef
  COMMIT`/`#ifndef ARCH` combinations) with `syslog(LOG_INFO, ...)`;
  or (b) keep the startup `fcntl` from Layer 2 in place as well,
  treating Layer 3 as additive rather than a replacement. Recommend
  (a) for a clean switch. In variant (b) the ordering inside `main()`
  is: `fcntl(O_NONBLOCK)` first (Layer 2), then `openlog()`, then
  `print_version()` — i.e. both sections claim "before any stderr
  output", not literally "first statement".
- `config.c:369, :373` — two bare `fprintf(stderr, ...)` calls in CLI
  startup error paths ("No arguments provided", "Failed to parse
  command line arguments"). Migrate these too, otherwise a stalled
  journald at startup can still freeze the process before Layer 3
  takes effect.
- `mini_argp.h:44, 46, 59, 65, 76` — five `fprintf(stderr, ...)` in
  the CLI parser for unknown/malformed options. Same fix; cheap.
- `uthash.h:515` — the `HASH_OOPS` macro also writes via
  `fprintf(stderr, ...)` then `exit(-1)`. It's vendored third-party
  code, and `uthash.h` unconditionally `#define`s `HASH_OOPS` with
  no `#ifndef` guard — so a pre-definition would simply be
  overwritten. The mechanically correct sequence is `#include
  "uthash.h"` first, then `#undef HASH_OOPS` and `#define
  HASH_OOPS(...)` routing through `syslog(LOG_ERR, ...)` plus
  `exit`. Only fires on malloc failure, so the chance of it blocking
  is negligible, but the story is not airtight until it's covered.
- `config.c` help-text string is fine to leave alone.

**Interaction with Layer 2.** Because the migration above is
multi-file and easy to miss during review, treat Layer 2's
`fcntl(O_NONBLOCK) + setvbuf(_IOLBF)` as a **hard prerequisite**
for Layer 3 in the recommended rollout: keep it in place even under
option (a). That way, any stray `fprintf(stderr, ...)` that sneaks
in later (or that we didn't migrate yet — `test_wg_emulator.c` is
an obvious example for test-only builds) still cannot block the
event loop. Cost is minimal (one `fcntl` at startup), benefit is a
belt-and-braces guarantee.

**Sketch of the new macro (drop-in, preserves call sites):**

```c
// wg-obfuscator.h
#include <syslog.h>

#define log(level, fmt, ...) do {                                 \
    if (verbose >= (level)) {                                     \
        int _prio =                                               \
              (level) == LL_ERROR ? LOG_ERR                       \
            : (level) == LL_WARN  ? LOG_WARNING                   \
            : (level) == LL_INFO  ? LOG_INFO                      \
            :                       LOG_DEBUG;                    \
        syslog(_prio, "[%s] " fmt, section_name, ##__VA_ARGS__);  \
    }                                                             \
} while (0)

#define serror_level(level, fmt, ...) \
    log(level, fmt " - %s (%d)", ##__VA_ARGS__, strerror(errno), errno)
#define serror(fmt, ...) serror_level(LL_ERROR, fmt, ##__VA_ARGS__)
```

**`main()` additions:**

```c
openlog("wg-obfuscator", LOG_PID, LOG_DAEMON);
```

(before the first `log()` call).

**`signal_handler()`:** leave it alone for Layer 3. `closelog()` is
not on the POSIX async-signal-safe list (`signal-safety(7)`), so
calling it from a signal handler is a correctness regression — and
the current handler already has the same class of problems with
`free`, `fprintf`, etc. (see adjacent-cleanup #4). On process exit
the kernel closes the syslog Unix socket unconditionally, so
`closelog()` is purely cosmetic. If you want a clean shutdown path,
do it together with adjacent-cleanup #4: turn the handler into a
`volatile sig_atomic_t stop_requested = 1;` assignment, break out of
the main loop, and perform `closelog()` + `HASH_ITER` cleanup in
ordinary (signal-free) code afterwards.

**Compatibility / rollout:**

- Existing systemd unit: no change needed; journald still receives
  everything, now with proper priorities.
- If someone runs the binary interactively and expects stderr output,
  they lose it. Options: (a) accept the break in a major version,
  (b) fall back to stderr when running with `isatty(STDERR_FILENO)`.
  Recommend (b) for 1.x compatibility:

  ```c
  static int use_syslog = 0;
  // in main(): use_syslog = !isatty(STDERR_FILENO);
  // macro: choose syslog(3) or fprintf(stderr) based on use_syslog
  ```

**`trace()` handling:** Packet hex-dumps are verbose and only useful
during debugging. Two options:

1. Keep `fprintf(stderr, ...)` but only when `use_syslog == 0`. In
   syslog mode, buffer the hex dump into a single line and emit as
   `LOG_DEBUG`. Cleaner.
2. Drop hex-dump tracing entirely from release builds (guard behind
   `#ifdef DEBUG`). Simpler and saves hot-path cycles.

Recommend option 2 for 1.6, gated on `DEBUG` build.

**Verification:**

- Same regression test (`test_stderr_block.sh`) must report
  `[NOT REPRODUCED]`. Note: the test runs the binary directly with
  stderr redirected to a FIFO (`tests/test_stderr_block.sh:99`), not
  under a systemd unit — so it exercises the logging code path only.
  For Layer 3 that is still useful *if* `trace()` hex-dumps still go
  to stderr (option 1 in the section above): the FIFO will fill, the
  fd is non-blocking (Layer 2 still in place) or writes are issued
  only through syslog (option 2 in the section above, gated on
  `DEBUG` build) — either way the process must not park in
  `anon_pipe_write`.
- **Separate check for the syslog backend itself** (not part of
  `test_stderr_block.sh`): run the binary under the real systemd
  unit on a canary node, capture its PID, then look for the expected
  handshake / error lines at the correct priorities:

  ```bash
  PID=$(systemctl show -p MainPID --value wg-obfuscator)
  journalctl _PID="$PID" -p debug --since=-5min
  journalctl _PID="$PID" -p warning --since=-5min  # should see warns only
  ```

  Priorities flowing correctly (ERR / WARNING / INFO / DEBUG visible
  with `-p <level>`) is what proves `syslog(3)` is wired up, not a
  leftover `fprintf(stderr, ...)` somewhere.
- **Optional unit test** in `tests/test_harness.c`: link against a
  shim that intercepts `syslog(3)` via `-Wl,--wrap=syslog`, call the
  real `log()` macro at each level, assert the shim sees the
  expected priority bits. This validates the macro without needing
  systemd at all.

---

## Adjacent cleanups (opportunistic, not required to close this bug)

While the logging code is open, the following are cheap and prevent
future false-positives for "obfuscator is losing packets":

1. **`SO_RCVBUF` bump on all UDP sockets.** `wg-obfuscator.c:373` for
   `listen_sock` and `:85`, `:180` for per-client `server_sock`. Set to
   8 MiB; kernel clamps to `net.core.rmem_max`. Ship a sysctl snippet
   (`net.core.rmem_max = 16777216`) in packaging.

   ```c
   int rcvbuf = 8 * 1024 * 1024;
   setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
   ```

2. **`O_NONBLOCK` on all UDP sockets + drop on `EAGAIN`/`ENOBUFS`** for
   `send`/`sendto` calls at `wg-obfuscator.c:709` and `:860`. Unlikely
   to be hit on Linux but makes the code's blocking semantics explicit
   and portable.

3. **Drain loop on `EPOLLIN` for listen_sock.** Replace the single
   `recvfrom` at `:553` with a loop until `EAGAIN`. Raises per-event
   throughput ceiling; mandatory once sockets are non-blocking.

4. **Signal-handler async-safety.** `signal_handler()`
   (`wg-obfuscator.c:37-58`) calls `free`, `fprintf`, and `exit` — none
   of which are async-signal-safe. Also: the `FAILURE()` macro
   (`wg-obfuscator.c:59`) expands to `signal_handler(-1)` and is
   called from ordinary error paths (e.g. startup bind/getaddrinfo
   failures), so the handler today runs in *both* signal and
   non-signal contexts. That dual-use is why the unsafe calls
   haven't bitten yet — most invocations are synchronous. The fix
   still stands: set a `volatile sig_atomic_t stop_requested` flag
   in the async path, drive cleanup from the main loop, and either
   keep `FAILURE()` as a plain `exit(EXIT_FAILURE)` after direct
   cleanup, or rename it to make the two flows distinct. Not urgent
   (no observed impact). Layer 3 works without it (we explicitly
   leave the handler alone), so this is a recommended co-delivery
   in the same release rather than a hard prerequisite.

5. **Rate-limit STUN `BINDING_REQUEST` responses** from unknown
   sources (`masking_stun.c:196-215`). Not an amplifier (response/
   request ≈ 1.43×) but a free CPU/traffic sink. Easy 1-line cap:
   reject if called with `client == NULL` and seen > N rps.

## Verification Matrix

| Fix applied            | `test_stderr_block.sh` | `make test` | Production canary (journald stalled)     |
| ---------------------- | ---------------------- | ----------- | ---------------------------------------- |
| none (baseline)        | REPRODUCED (exit 0)    | pass        | hangs (latent; fires on reader stall)    |
| Layer 1 only           | REPRODUCED             | pass        | no hang if `StandardError=null`; with `append:/var/log/...` a full log volume can still block `write(2)` |
| Layer 2 (non-blocking) | NOT REPRODUCED         | pass        | no hang; some log lines dropped on stall |
| Layer 3 (syslog)       | NOT REPRODUCED         | pass        | no hang; priorities correct              |

Layer 1 still shows `REPRODUCED` in the test because the reproducer
deliberately wires stderr back to a pipe — the test exercises the
*code path*, not the operational configuration. Layer 1 fixes the
environment; Layer 2/3 fixes the code.

## Rollout Order

1. **Immediately (hotfix):** deploy Layer 1 to all prod nodes.
2. **Patch release 1.5.2 (shipped):** Layer 2 only.
   - Small, safe, fully covered by the new regression test.
   - SO_RCVBUF (cleanup #1) was originally planned here but slipped
     to 1.6.0; the hang is already gone, and rmem tuning can be done
     operationally via `sysctl net.core.rmem_max` without a binary
     update.
3. **Minor release 1.6.0 (shipped on `v1.6-refactor`):** Layer 3 +
   cleanups #1–#5.
   - Layer 3: `syslog(3)` with `isatty()` fallback. `trace()` hex
     dumps moved behind `#ifdef DEBUG`. `HASH_OOPS` mirrored to syslog.
   - Cleanup #1: `SO_RCVBUF = 8 MiB` on all UDP sockets.
   - Cleanup #2: `O_NONBLOCK` on all UDP sockets; `errno_is_transient`
     helper for EAGAIN/EWOULDBLOCK/ENOBUFS/EINTR.
   - Cleanup #3: drain loop on EPOLLIN, capped at `DRAIN_BATCH_MAX=128`
     packets per notification to avoid fd starvation.
   - Cleanup #4: async-signal-safe `signal_handler()` — just sets a
     flag; real work moved to `cleanup_and_exit()` driven from the
     main loop. EINTR on `epoll_wait`/`poll` treated as expected
     interruption rather than failure.
   - Cleanup #5: global rate limit of 100 STUN BINDING_REQUESTs / sec
     from unknown sources (client == NULL in `masking_stun.c`).
   - Layer 2's `fcntl(O_NONBLOCK)` + `setvbuf(_IOLBF)` kept in place
     as belt-and-braces for the stderr path and for DEBUG trace output.

## Open Questions

- Should `trace()` hex dumps be gated behind `#ifdef DEBUG` in release
  builds? (my recommendation: yes — they are the main source of
  high-volume stderr output, and prod has no business enabling them.)
- Do we want a compile-time toggle between syslog and stderr backends,
  or always pick syslog + `isatty` fallback? (recommend the latter.)

## Regression Test

`tests/test_stderr_block.sh` — Linux-only reproducer. Wired into the
Makefile as `make test-stderr-block` (not part of default
`make test` yet — see below). The script is location-independent:
resolves its own directory via `BASH_SOURCE[0]`, so it works whether
invoked from repo root, from `tests/`, or from `make`.

**Invariant:**

- before fix: `wchan ∈ {pipe_write, anon_pipe_write, pipe_wait}` AND
  listen-socket `Recv-Q > 0` within `STABILISE_SEC` seconds (default
  3, overridable via env var) of the stderr reader being SIGSTOPed
  during a `FLOOD_PACKETS` flood (default 50 000) at `-v TRACE`;
- after fix: neither condition holds.

On slow CI runners bump `STABILISE_SEC=10 FLOOD_PACKETS=200000` to
leave headroom; the invariant is the logical condition, not the
wall-clock constant.

**Exit-code semantics — currently a reproducer, not a gate.** The
script today exits 0 on `REPRODUCED` and 1 on `NOT REPRODUCED`, i.e.
"success" means the bug is still there. That's useful while the fix
is being developed and reviewed: you can run it, confirm the bug,
apply Layer 2, re-run, and watch the exit code flip to 1 as proof.
But it is *not* a regression gate in its current form — adding it
to default `make test` would just hide the defect under a passing
CI.

**Follow-up once Layer 2 is merged:**

1. Flip the exit codes: `NOT REPRODUCED → 0` (pass), `REPRODUCED → 1`
   (fail).
2. Add `test-stderr-block` to the default `test:` target in the
   `Makefile` (a comment block already marks the intended edit).
3. Bump `TESTING.md` / `tests/README.md` with the invariant and the
   env-var knobs.

Until then: `make test-stderr-block` is the explicit entry point,
and the Makefile carries a comment explaining why it's held back.
