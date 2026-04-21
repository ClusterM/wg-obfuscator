# wg0-obfuscator

> **Fork of [ClusterM/wg-obfuscator](https://github.com/ClusterM/wg-obfuscator).**
> See the upstream README for the full protocol description, supported masking modes, and design motivation.
> This fork diverged at v1.6 — binary renamed to `wg0-obfuscator`, logging switched to `syslog(3)`, several hardening fixes. See [`FIX_PLAN.md`](FIX_PLAN.md) for the design rationale of the v1.6 changes.

---

## Install (pre-built binary, Linux x64)

Download the release tarball from [Releases](https://github.com/ramanarupa/wg0-obfuscator/releases), then on the target host:

```bash
tar xzf wg0-obfuscator-v1.6.0-linux-x64.tar.gz
cd wg0-dist

sudo install -m 755 wg0-obfuscator               /usr/local/bin/
sudo install -m 644 wg0-obfuscator.conf          /etc/wg0-obfuscator.conf
sudo install -m 644 wg0-obfuscator.service       /etc/systemd/system/

# edit the config — at minimum set source-lport, target, and key
sudoedit /etc/wg0-obfuscator.conf

sudo systemctl daemon-reload
sudo systemctl enable --now wg0-obfuscator
sudo systemctl status  wg0-obfuscator
```

For other architectures (`arm64` static binary, `windows-x64`, or `linux/arm64` Docker image for MikroTik hAP ax^3) use the matching asset from the same release.

### Build from source

```bash
git clone https://github.com/ramanarupa/wg0-obfuscator
cd wg0-obfuscator
make                # release build → ./wg0-obfuscator
sudo make install   # Linux only
```

Recommended sysctl for production — the obfuscator requests an 8 MiB UDP receive buffer per socket, but the kernel clamps to `net.core.rmem_max`:

```bash
sudo tee /etc/sysctl.d/99-wg0-obfuscator.conf <<EOF
net.core.rmem_max = 16777216
EOF
sudo sysctl --system
```

## Where are the logs?

**Under systemd (default install):** logs go to `journald` via `syslog(3)` with facility `LOG_DAEMON`. Everything you're used to from `fprintf(stderr)` in older versions now arrives at journald with proper priority mapping.

```bash
journalctl -u wg0-obfuscator                    # all messages
journalctl -u wg0-obfuscator -f                 # tail -f
journalctl -u wg0-obfuscator --since=-10min     # last 10 minutes
journalctl -u wg0-obfuscator -p warning         # WARN and above
journalctl -u wg0-obfuscator -p err             # errors only
journalctl -t wg0-obfuscator                    # filter by syslog ident
```

In multi-section configs (several `[section]` blocks), each forked child prepends its section name in the message body:

```
Apr 21 09:10:11 host wg0-obfuscator[1234]: [main] WireGuard obfuscator successfully started
Apr 21 09:10:11 host wg0-obfuscator[1235]: [east] Listening on 0.0.0.0:33333 for source
```

Filter with a simple `grep`:

```bash
journalctl -u wg0-obfuscator | grep '\[east\]'
```

**Interactive run (from a terminal):** if `stderr` is a tty, `wg0-obfuscator` keeps the legacy line format:

```
[main][I] Starting WireGuard Obfuscator v1.6.0
[main][I] Listening on port 0.0.0.0:33333 for source
[main][I] Target: 10.0.0.1:51820
[main][I] WireGuard obfuscator successfully started
```

The decision is made once at startup from `isatty(STDERR_FILENO)`. If you pipe stderr to a file (`./wg0-obfuscator ... 2> /tmp/out`), it uses the syslog path instead.

**Hex-dump tracing (`-v trace`):** compiled out of release builds. If you need packet-level debugging, rebuild with `DEBUG=1 make`; the resulting binary emits hex dumps to stderr at trace verbosity.

## Verbosity levels

`-v <level>` or `verbose = <level>` in the config:

| Level | Number | What's logged |
|-------|--------|---------------|
| `error` | 0 | Only critical errors |
| `warn` | 1 | Warnings + errors |
| `info` | 2 *(default)* | Startup banner, handshakes established, idle evictions, errors |
| `debug` | 3 | Per-packet decisions (decode failures, masking detection, etc.) |
| `trace` | 4 | Everything above + per-byte packet hex dumps *(DEBUG build only)* |

Prod recommendation: `info`. Higher levels are fine — syslog drops rather than blocks, so no risk of stalling the event loop like in 1.5.

## Upgrading from upstream `wg-obfuscator`

If you previously had upstream installed from source, the old paths (`/usr/bin/wg-obfuscator`, `/etc/wg-obfuscator.conf`, `wg-obfuscator.service`) will not be touched by installing this fork — they use `wg0-obfuscator` instead. Either migrate manually:

```bash
sudo systemctl stop wg-obfuscator
sudo systemctl disable wg-obfuscator
sudo cp /etc/wg-obfuscator.conf /etc/wg0-obfuscator.conf
# then install wg0-obfuscator as above
```

…or run both side-by-side on different ports — they don't share any files.

## License

Same as upstream — see [`LICENSE`](LICENSE).
