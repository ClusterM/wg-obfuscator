# WireGuard Obfuscator for OpenWRT

This package provides WireGuard Obfuscator for OpenWRT, allowing to obfuscate WireGuard traffic to bypass DPI (Deep Packet Inspection).

## Installation

### Via opkg (if package is available in repository)
```bash
opkg update
opkg install wg-obfuscator
```

### Build from source

1. Copy package files to `feeds/packages/net/wg-obfuscator/` in your OpenWRT SDK
2. Build the package:
```bash
make package/wg-obfuscator/compile
```

## Configuration

Edit the `/etc/wg-obfuscator.conf` file:

```ini
[main]
source-if = 0.0.0.0
source-lport = 13255
target = your-server.com:13255
key = your-secret-key
masking = AUTO
verbose = INFO
max-clients = 1024
idle-timeout = 300
max-dummy-length-data = 4
```

## Usage

1. Configure WireGuard to connect to the local obfuscator address:
```ini
[Peer]
Endpoint = 127.0.0.1:13255
```

2. Start the obfuscator:
```bash
/etc/init.d/wg-obfuscator start
```

3. Start WireGuard as usual

## Service Management

```bash
# Start
/etc/init.d/wg-obfuscator start

# Stop
/etc/init.d/wg-obfuscator stop

# Restart
/etc/init.d/wg-obfuscator restart

# Reload configuration
/etc/init.d/wg-obfuscator reload

# Status
/etc/init.d/wg-obfuscator status
```

## Auto-start

To automatically start on system boot:
```bash
/etc/init.d/wg-obfuscator enable
```

## Logs

View service logs with:
```bash
logread | grep wg-obfuscator
```

## Supported Architectures

The package supports all architectures supported by OpenWRT:
- x86_64
- ARM64
- ARMv7
- MIPS
- MIPS64
- and others

## Requirements

- OpenWRT 19.07 or newer
- Minimum 1MB free space
- Minimum 512KB RAM

## Notes

- Make sure the port specified in `source-lport` is not used by other services
- For server mode operation, firewall port forwarding may be required
- When using `fwmark`, ensure the obfuscator runs with root privileges

## Support

For help and bug reports, refer to:
- [Official repository](https://github.com/ClusterM/wg-obfuscator)
- [OpenWRT documentation](https://openwrt.org/docs)