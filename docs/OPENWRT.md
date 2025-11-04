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

**Via LuCI:** Enter each binding on a separate line in the "Static Bindings" text area:
```
1.2.3.4:12883:6670
5.6.7.8:12083:6679
```

**Via UCI:** Use newlines or commas to separate multiple bindings:
```bash
uci set wg-obfuscator.main.static_bindings='1.2.3.4:12883:6670
5.6.7.8:12083:6679'
uci commit wg-obfuscator
/etc/init.d/wg-obfuscator restart
```

Format: `remote_ip:remote_port:local_port` (one per line or comma-separated)

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

Since `.ipk` packages are architecture-specific and must be built for your router's exact platform, you need to build them using the OpenWrt build system. This section explains how to build packages using the OpenWrt source code.

### Using OpenWrt Source Code

This method uses the full OpenWrt source code, which allows you to configure everything via `menuconfig`.

#### Step 1: Get OpenWrt Source Code

Download the latest OpenWrt release from [GitHub releases](https://github.com/openwrt/openwrt/releases):

```bash
# Download and extract OpenWrt source code
wget https://github.com/openwrt/openwrt/archive/refs/tags/v24.10.4.tar.gz
tar xzf v24.10.4.tar.gz
cd openwrt-24.10.4
```

Or clone the repository:

```bash
git clone https://github.com/openwrt/openwrt.git
cd openwrt
git checkout v24.10.4  # or latest release tag
```

#### Step 2: Update and Install Feeds

```bash
# Update package feeds (downloads package definitions)
./scripts/feeds update -a

# Install feeds (makes packages available for building)
./scripts/feeds install -a
```

**Note:** The `luci-app-wg-obfuscator` package requires the `luci` feed. Ensure it's installed:

```bash
./scripts/feeds install -a -p luci
```

#### Step 3: Configure Build System with menuconfig

This is where you select your router's target, subtarget, and optionally the device profile:

```bash
make menuconfig
```

In the menuconfig interface:

1. **Select Target System:**
   - Navigate to **Target System** and select your router's architecture (e.g., `x86`, `ARM`, `MIPS`, etc.)

2. **Select Subtarget:**
   - Navigate to **Subtarget** and select the appropriate subtarget (e.g., `x86_64`, `generic`, `mt7621`, etc.)

3. **Select Target Profile (Optional):**
   - Navigate to **Target Profile** and select your specific router model if available
   - This automatically configures all necessary settings for your device

4. **Save configuration:**
   - Press `<Enter>` on **Save** to save the configuration
   - Press `<Enter>` on **Exit** to exit menuconfig

**Note:** You don't need to enable the packages manually - the build scripts will do this automatically.

#### Step 4: Build Toolchain and Dependencies

Before building packages, you need to build the toolchain and dependencies:

```bash
# Build toolchain and dependencies
make -j$(nproc) tools/install toolchain/install
```

**Note:** This process may take a long time (from 30 minutes to several hours depending on your system). The toolchain needs to be compiled from source, which includes GCC, binutils, and other build tools.

This builds the cross-compilation toolchain and all necessary dependencies for your selected target.

#### Step 5: Build Packages

The build scripts will automatically create symlinks and enable packages in the configuration:

```bash
# Set the build directory
export OPENWRT_BUILD_DIR="$(pwd)"

# Build main package
cd /path/to/wg-obfuscator/openwrt-main
./build.sh

# Build LuCI package
cd ../openwrt-luci
./build.sh
```

The scripts will:
- Automatically create symlinks to package directories
- Enable packages in `.config` if needed
- Build the packages

#### Step 6: Install Built Packages

After successful build, the `.ipk` files will be in `bin/packages/*/base/`.

##### Using LuCI Web Interface (Recommended)

1. Find the built packages:
   ```bash
   find bin/packages -name "*.ipk" | grep -E "wg-obfuscator|luci-app-wg-obfuscator"
   ```

2. Copy `.ipk` files to your computer (if building on a remote machine)

3. Log into LuCI web interface on your router

4. Go to **System → Software**

5. Click **Upload Package...**

6. Select and upload each `.ipk` file:
   - `wg-obfuscator_*.ipk`
   - `luci-app-wg-obfuscator_*.ipk`

7. Click **Install** for each package

##### Using Command Line (Alternative)

If you prefer using SSH:

```bash
# Find built packages
find bin/packages -name "*.ipk" | grep -E "wg-obfuscator|luci-app-wg-obfuscator"

# Copy to router (adjust paths and router IP)
scp bin/packages/*/base/wg-obfuscator*.ipk root@192.168.1.1:/tmp/
scp bin/packages/*/base/luci-app-wg-obfuscator*.ipk root@192.168.1.1:/tmp/

# Install on router (via SSH)
ssh root@192.168.1.1
opkg install /tmp/wg-obfuscator*.ipk
opkg install /tmp/luci-app-wg-obfuscator*.ipk
```

### Troubleshooting Build Issues

#### Issue: "Package not found" or "No rule to make target"

**Solution:** Ensure you've updated feeds and created symlinks correctly:
```bash
./scripts/feeds update -a
./scripts/feeds install -a
ls -la package/network/wg-obfuscator  # Should show symlink
```

#### Issue: "luci-compat not found" error

**Solution:** Ensure LuCI feed is installed:
```bash
./scripts/feeds install -a -p luci
```

#### Issue: Build fails with missing dependencies

**Solution:** Install missing build dependencies. Common packages:
```bash
# On Ubuntu/Debian
sudo apt-get install build-essential gawk gettext git libncurses5-dev libssl-dev python3 python3-setuptools rsync unzip zlib1g-dev
```

#### Issue: Wrong SDK version

**Solution:** Ensure SDK version matches your OpenWrt version. Check your router:
```bash
cat /etc/openwrt_release | grep DISTRIB_RELEASE
```

Then download the matching SDK version.

### Quick Reference: Complete Build Example

Here's a complete example using OpenWrt source code:

```bash
# 1. Download OpenWrt source code
git clone https://github.com/openwrt/openwrt.git
cd openwrt
git checkout v24.10.4  # or latest release

# 2. Update and install feeds
./scripts/feeds update -a
./scripts/feeds install -a

# 3. Configure target/subtarget with menuconfig
make menuconfig
# Select Target System, Subtarget, and optionally Target Profile
# Save and exit

# 4. Build toolchain and dependencies
make -j$(nproc) tools/install toolchain/install

# 5. Build packages (scripts will create symlinks automatically)
export OPENWRT_BUILD_DIR="$(pwd)"
cd /path/to/wg-obfuscator/openwrt-main
./build.sh
cd ../openwrt-luci
./build.sh

# 6. Find built packages
find ../openwrt/bin/packages -name "*.ipk" | grep wg-obfuscator
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

