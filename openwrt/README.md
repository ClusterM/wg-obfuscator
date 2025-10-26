# WireGuard Obfuscator for OpenWRT

This directory contains the OpenWRT package files for WireGuard Obfuscator.

## Files

- `Makefile` - OpenWRT package Makefile
- `files/wg-obfuscator.config` - UCI configuration template
- `files/wg-obfuscator.init` - OpenWRT init script
- `files/wg-obfuscator.conf` - Example configuration file
- `files/wg-obfuscator-config.sh` - Script to generate config from UCI

## Building

To build this package for OpenWRT:

1. Copy the `openwrt/` directory to your OpenWRT build system
2. Place it in `package/network/wg-obfuscator/`
3. Run: `make package/wg-obfuscator/compile`

## Installation

After building and installing the package:

1. Configure the service using UCI:
   ```bash
   uci set wg-obfuscator.main.enabled=1
   uci set wg-obfuscator.main.source_lport=13255
   uci set wg-obfuscator.main.target=your-server.com:13255
   uci set wg-obfuscator.main.key=your-secret-key
   uci commit wg-obfuscator
   ```

2. Start the service:
   ```bash
   /etc/init.d/wg-obfuscator start
   ```

3. Enable auto-start:
   ```bash
   /etc/init.d/wg-obfuscator enable
   ```

## Configuration

The service uses UCI (Unified Configuration Interface) for configuration. All settings are stored in `/etc/config/wg-obfuscator`.

### UCI Options

- `enabled` - Enable/disable the instance (0/1)
- `source_lport` - Local port to listen on
- `target` - Target server (host:port)
- `key` - Obfuscation key
- `source_if` - Source interface (default: 0.0.0.0)
- `masking` - Masking type (NONE, AUTO, STUN)
- `verbose` - Log level (ERRORS, WARNINGS, INFO, DEBUG, TRACE)
- `max_clients` - Maximum number of clients
- `idle_timeout` - Idle timeout in seconds
- `max_dummy_length_data` - Maximum dummy data length
- `fwmark` - Firewall mark (0 to disable)
- `static_bindings` - Static bindings for two-way mode

### Multiple Instances

You can configure multiple instances by creating additional sections:

```bash
uci set wg-obfuscator.instance2.enabled=1
uci set wg-obfuscator.instance2.source_lport=13256
uci set wg-obfuscator.instance2.target=another-server.com:13256
uci set wg-obfuscator.instance2.key=another-key
uci commit wg-obfuscator
```

## Service Management

- Start: `/etc/init.d/wg-obfuscator start`
- Stop: `/etc/init.d/wg-obfuscator stop`
- Restart: `/etc/init.d/wg-obfuscator restart`
- Reload: `/etc/init.d/wg-obfuscator reload`
- Enable: `/etc/init.d/wg-obfuscator enable`
- Disable: `/etc/init.d/wg-obfuscator disable`

## Logs

View logs using:
```bash
logread | grep wg-obfuscator
```

## Configuration Generation

The actual configuration file (`/etc/wg-obfuscator/wg-obfuscator.conf`) is automatically generated from UCI settings. You can regenerate it manually:

```bash
/usr/libexec/wg-obfuscator-config.sh
```

## Notes

- The service runs as root to bind to privileged ports
- Configuration is automatically regenerated when UCI settings change
- The service uses procd for process management
- All instances share the same binary but use different configuration sections