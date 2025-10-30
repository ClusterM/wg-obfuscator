# WireGuard Obfuscator for OpenWrt

WireGuard Obfuscator is available as native OpenWrt packages with full UCI integration and optional LuCI web interface.

## Installation

### From OpenWrt Package Repositories

> **Note:** Packages are not yet available in official OpenWrt repositories. See "Building from Source" section below.

Once packages are submitted to OpenWrt feeds:

```bash
opkg update
opkg install wg-obfuscator luci-app-wg-obfuscator
```

### Building and Installing from Source

Since `.ipk` packages are architecture-specific (arm, mips, x86, etc.), you need to build them for your specific router platform. See the [Building from Source](#building-from-source) section below for detailed instructions.

## Configuration

### Using LuCI Web Interface (Recommended)

After installing `luci-app-wg-obfuscator`:

1. Navigate to **Services → WireGuard Obfuscator**
2. Configure instances using the web form:
   - **Enable** - Turn instance on/off
   - **Source Port** - Local port to listen on
   - **Target** - Target server (host:port format)
   - **Obfuscation Key** - Shared secret key
   - **Source Interface** - Interface to bind (default: 0.0.0.0)
   - **Masking Type** - Protocol masking (AUTO recommended)
   - **Log Level** - Verbosity (INFO recommended)
   - **Max Clients** - Maximum concurrent clients
   - **Idle Timeout** - Seconds before disconnecting idle clients
   - **Max Dummy Data** - Random padding length (0-255)
   - **Static Bindings** - For two-way mode (see below)
3. Click **"Save & Apply"** to save configuration
4. Click **"Restart Service"** to apply changes

### Using UCI (Command Line)

All settings are stored in `/etc/config/wg-obfuscator`.

#### Single Instance Example

```bash
uci set wg-obfuscator.main=wg_obfuscator
uci set wg-obfuscator.main.enabled=1
uci set wg-obfuscator.main.source_lport=13255
uci set wg-obfuscator.main.target='your-server.com:13255'
uci set wg-obfuscator.main.key='your-secret-key'
uci set wg-obfuscator.main.source_if='0.0.0.0'
uci set wg-obfuscator.main.masking='AUTO'
uci set wg-obfuscator.main.verbose='INFO'
uci commit wg-obfuscator
/etc/init.d/wg-obfuscator restart
```

#### Multiple Instances

You can run multiple instances with different configurations:

```bash
# First instance
uci set wg-obfuscator.vpn1=wg_obfuscator
uci set wg-obfuscator.vpn1.enabled=1
uci set wg-obfuscator.vpn1.source_lport=13255
uci set wg-obfuscator.vpn1.target='server1.com:51820'
uci set wg-obfuscator.vpn1.key='key1'

# Second instance
uci set wg-obfuscator.vpn2=wg_obfuscator
uci set wg-obfuscator.vpn2.enabled=1
uci set wg-obfuscator.vpn2.source_lport=13256
uci set wg-obfuscator.vpn2.target='server2.com:51820'
uci set wg-obfuscator.vpn2.key='key2'

uci commit wg-obfuscator
/etc/init.d/wg-obfuscator restart
```

### UCI Configuration Options

| Option | Description | Default | Example |
|--------|-------------|---------|---------|
| `enabled` | Enable/disable instance | `0` | `1` |
| `source_lport` | Local port to listen on | - | `13255` |
| `target` | Target server (host:port) | - | `vpn.example.com:51820` |
| `key` | Obfuscation key (shared secret) | - | `my-secret-key` |
| `source_if` | Interface to bind | `0.0.0.0` | `0.0.0.0` or `192.168.1.1` |
| `masking` | Masking type | `AUTO` | `NONE`, `AUTO`, `STUN` |
| `verbose` | Log level | `INFO` | `ERRORS`, `WARNINGS`, `INFO`, `DEBUG`, `TRACE` |
| `max_clients` | Max concurrent clients | `1024` | `512` |
| `idle_timeout` | Idle timeout (seconds) | `300` | `600` |
| `max_dummy` | Max dummy data length | `4` | `0`-`255` |
| `static_bindings` | Static bindings (two-way mode) | - | `1.2.3.4:12883:6670` |

### Two-Way Mode (Static Bindings)

For bidirectional obfuscation (when both ends need to accept connections):

```bash
uci set wg-obfuscator.main.static_bindings='1.2.3.4:12883:6670,5.6.7.8:12083:6679'
uci commit wg-obfuscator
/etc/init.d/wg-obfuscator restart
```

Format: `remote_ip:remote_port:local_port` (comma-separated for multiple bindings)

See the main README for detailed explanation of two-way mode.

## Service Management

### Using Init Scripts

```bash
# Start service
/etc/init.d/wg-obfuscator start

# Stop service
/etc/init.d/wg-obfuscator stop

# Restart service
/etc/init.d/wg-obfuscator restart

# Reload configuration (regenerate config without restart)
/etc/init.d/wg-obfuscator reload

# Enable autostart on boot
/etc/init.d/wg-obfuscator enable

# Disable autostart
/etc/init.d/wg-obfuscator disable

# Check status
/etc/init.d/wg-obfuscator status
```

### Using LuCI Web Interface

- **Restart Service** button - Restarts the service with new configuration
- **Refresh Status** button - Refreshes the status display
- **Save & Apply** button - Saves UCI configuration and regenerates config file (doesn't restart service)
- **Save** button - Only saves to UCI (doesn't apply)
- **Reset** button - Discards unsaved changes

## Logs and Troubleshooting

### View Logs

```bash
# View all logs
logread | grep wg-obfuscator

# Follow logs in real-time
logread -f | grep wg-obfuscator

# View system logs
dmesg | grep wg-obfuscator
```

### Check Service Status

```bash
# Using init script
/etc/init.d/wg-obfuscator status

# Check if process is running
ps | grep wg-obfuscator

# View generated configuration
cat /etc/wg-obfuscator/wg-obfuscator.conf
```

### Common Issues

1. **Service doesn't start**
   - Check logs: `logread | grep wg-obfuscator`
   - Verify configuration: `uci show wg-obfuscator`
   - Ensure binary exists: `which wg-obfuscator`

2. **No traffic flowing**
   - Verify WireGuard endpoint points to obfuscator (not VPN server)
   - Check firewall rules: `iptables -L -n | grep 13255`
   - Ensure obfuscation key matches on both sides

3. **LuCI interface not showing**
   - Ensure `luci-app-wg-obfuscator` is installed
   - Clear LuCI cache: `rm -rf /tmp/luci-*`
   - Restart uhttpd: `/etc/init.d/uhttpd restart`

4. **Configuration changes not applied**
   - After "Save & Apply", click "Restart Service"
   - Or use: `/etc/init.d/wg-obfuscator restart`

## Building from Source

### Prerequisites

> **Note:** The example below downloads the SDK for **x86/64 target** (PC routers). If your router uses a different target (e.g., `ipq40xx`, `ramips`, `ath79`), download the appropriate SDK from [OpenWrt Downloads](https://downloads.openwrt.org/releases/23.05.5/targets/) for your device's target/subtarget.

```bash
# Install OpenWrt SDK (x86/64 target example)
wget https://downloads.openwrt.org/releases/23.05.5/targets/x86/64/openwrt-sdk-23.05.5-x86-64_gcc-12.3.0_musl.Linux-x86_64.tar.xz
tar xf openwrt-sdk-*.tar.xz
cd openwrt-sdk-*
```

### Build Main Package

```bash
# From wg-obfuscator repository
export OPENWRT_BUILD_DIR=~/openwrt-sdk-23.05.5-x86-64_gcc-12.3.0_musl.Linux-x86_64
cd openwrt-main
./build.sh
```

### Build LuCI Package

```bash
cd openwrt-luci
./build.sh
```

### Install Built Packages

```bash
# Copy .ipk files to your router
scp ~/openwrt-sdk/bin/packages/*/base/wg-obfuscator*.ipk root@router:/tmp/
scp ~/openwrt-sdk/bin/packages/*/base/luci-app-wg-obfuscator*.ipk root@router:/tmp/

# On router
opkg install /tmp/wg-obfuscator*.ipk
opkg install /tmp/luci-app-wg-obfuscator*.ipk
```

## Important Notes

### Avoiding Routing Loops

**Critical:** You must ensure that traffic to the VPN server itself does not go through the VPN tunnel. Otherwise, you'll create a routing loop.

#### Configuring Static Route via LuCI

1. Go to **Network → Static Routes**
2. Click **Add** to create a new route
3. In the dialog, configure:
   - **Interface**: Select `wan` (your WAN interface)
   - **Route type**: Leave as `unicast` (default)
   - **Target**: Enter your VPN server IP with `/32` netmask (e.g., `1.2.3.4/32`)
     - The `/32` means a single host (equivalent to netmask `255.255.255.255`)
   - **Gateway**: Leave empty (will automatically use WAN's gateway)
4. Click **Save**
5. Click **Save & Apply** at the bottom of the page

> **Note:** OpenWrt automatically uses the gateway from the specified interface (WAN), so you don't need to enter it manually. As stated in the interface: "If omitted, the gateway from the parent interface is taken".

**Replace** `1.2.3.4` with your actual VPN server IP address.

After applying, you can verify the route is active by going to **Status → Routes** and looking for your VPN server IP in the list.

### Other Important Notes

- **Keys**: The obfuscation key must be the same on both client and server sides.
- **Firewall**: OpenWrt should automatically handle firewall rules, but verify UDP port is open if issues occur.
- **Performance**: On low-end routers, consider reducing `max_clients` and disabling verbose logging.
- **Updates**: Configuration is preserved during package updates.
- **WireGuard Endpoint**: Don't forget to change WireGuard's `Endpoint` setting to point to the obfuscator's IP:port (e.g., `127.0.0.1:13255` for local obfuscator), not the VPN server directly.

## Package Information

- **Main Package**: `wg-obfuscator` - Binary and UCI configuration
- **LuCI Package**: `luci-app-wg-obfuscator` - Web interface (optional)
- **Architecture**: Built for specific OpenWrt targets (arm, mips, x86, etc.)
- **Dependencies**: Minimal (libc, zlib)
- **Size**: ~500KB (main), ~10KB (LuCI)

## Support

For issues specific to OpenWrt packaging, please check:
- [GitHub Issues](https://github.com/ClusterM/wg-obfuscator/issues)
- Main README for general configuration help
- OpenWrt forums for platform-specific questions

