# WireGuard Obfuscator - OpenWRT Integration

This directory contains complete OpenWRT integration for WireGuard Obfuscator, including UCI configuration, init scripts, and optional LuCI web interface.

## Directory Structure

```
openwrt/
├── Makefile                           # OpenWRT package Makefile
├── files/                             # Package files
│   ├── wg-obfuscator.config          # UCI configuration template
│   ├── wg-obfuscator.init            # OpenWRT init script
│   ├── wg-obfuscator.conf            # Example configuration
│   └── wg-obfuscator-config.sh       # Config generator script
├── luci-app-wg-obfuscator/           # Optional LuCI web interface
│   ├── Makefile
│   ├── luasrc/
│   │   ├── controller/
│   │   ├── model/cbi/
│   │   └── view/wg-obfuscator/
│   └── root/etc/uci-defaults/
├── build-openwrt.sh                  # Build script
├── README.md                         # Basic documentation
├── INTEGRATION.md                    # Integration guide
└── README-OPENWRT.md                 # This file
```

## Features

### Core Package (`wg-obfuscator`)
- ✅ UCI configuration management
- ✅ Automatic config generation from UCI
- ✅ Multiple instance support
- ✅ procd-based service management
- ✅ Automatic restart on failure
- ✅ Configuration reload on changes
- ✅ Minimal dependencies (only libc)

### LuCI Web Interface (`luci-app-wg-obfuscator`)
- ✅ Web-based configuration
- ✅ Real-time service status
- ✅ Service control (start/stop/restart)
- ✅ Multiple instance management
- ✅ Form validation
- ✅ Responsive design

## Quick Start

### 1. Build the Package

```bash
# Copy to OpenWRT build system
cp -r openwrt/ /path/to/openwrt/package/network/wg-obfuscator/

# Build
cd /path/to/openwrt
make menuconfig
# Select: Network -> wg-obfuscator
make package/wg-obfuscator/compile
```

### 2. Install and Configure

```bash
# Install package
opkg install wg-obfuscator_*.ipk

# Configure via UCI
uci set wg-obfuscator.main.enabled=1
uci set wg-obfuscator.main.source_lport=13255
uci set wg-obfuscator.main.target=your-server.com:13255
uci set wg-obfuscator.main.key=your-secret-key
uci commit wg-obfuscator

# Start service
/etc/init.d/wg-obfuscator start
/etc/init.d/wg-obfuscator enable
```

### 3. Optional: Install LuCI Interface

```bash
# Build LuCI app
make package/luci-app-wg-obfuscator/compile

# Install
opkg install luci-app-wg-obfuscator_*.ipk

# Access via web interface
# http://your-router-ip/cgi-bin/luci/admin/network/wg-obfuscator
```

## Configuration

### UCI Configuration

All settings are managed through UCI in `/etc/config/wg-obfuscator`:

```bash
# Basic configuration
uci set wg-obfuscator.main.enabled=1
uci set wg-obfuscator.main.source_lport=13255
uci set wg-obfuscator.main.target=server.com:13255
uci set wg-obfuscator.main.key=secret-key

# Advanced options
uci set wg-obfuscator.main.masking=STUN
uci set wg-obfuscator.main.verbose=DEBUG
uci set wg-obfuscator.main.max_clients=1024
uci set wg-obfuscator.main.idle_timeout=300
uci set wg-obfuscator.main.fwmark=0xdead

# Static bindings for two-way mode
uci set wg-obfuscator.main.static_bindings="1.2.3.4:12883:6670,5.6.7.8:12083:6679"

uci commit wg-obfuscator
```

### Multiple Instances

```bash
# Create second instance
uci set wg-obfuscator.instance2.enabled=1
uci set wg-obfuscator.instance2.source_lport=13256
uci set wg-obfuscator.instance2.target=another-server.com:13256
uci set wg-obfuscator.instance2.key=another-key
uci commit wg-obfuscator
```

## Service Management

```bash
# Service control
/etc/init.d/wg-obfuscator start
/etc/init.d/wg-obfuscator stop
/etc/init.d/wg-obfuscator restart
/etc/init.d/wg-obfuscator reload

# Enable/disable auto-start
/etc/init.d/wg-obfuscator enable
/etc/init.d/wg-obfuscator disable

# Check status
/etc/init.d/wg-obfuscator status
```

## Monitoring

### Logs
```bash
# View logs
logread | grep wg-obfuscator

# Follow logs
logread -f | grep wg-obfuscator
```

### Process Status
```bash
# Check if running
pgrep -f wg-obfuscator

# View process info
ps | grep wg-obfuscator
```

### Configuration
```bash
# View generated config
cat /etc/wg-obfuscator/wg-obfuscator.conf

# Regenerate config
/usr/libexec/wg-obfuscator-config.sh
```

## Troubleshooting

### Common Issues

1. **Service won't start**
   ```bash
   # Check UCI configuration
   uci show wg-obfuscator
   
   # Check generated config
   cat /etc/wg-obfuscator/wg-obfuscator.conf
   
   # Check logs
   logread | grep wg-obfuscator
   ```

2. **Port already in use**
   ```bash
   # Check what's using the port
   netstat -ln | grep :13255
   
   # Change port in UCI
   uci set wg-obfuscator.main.source_lport=13256
   uci commit wg-obfuscator
   ```

3. **Configuration not updating**
   ```bash
   # Restart service after UCI changes
   /etc/init.d/wg-obfuscator restart
   ```

4. **Permission denied**
   ```bash
   # Service runs as root, check if binary exists
   ls -la /usr/bin/wg-obfuscator
   
   # Check file permissions
   chmod +x /usr/bin/wg-obfuscator
   ```

## Development

### Building from Source

```bash
# Clone repository
git clone https://github.com/ClusterM/wg-obfuscator.git
cd wg-obfuscator

# Build OpenWRT package
make openwrt

# Use build script
cd openwrt
./build-openwrt.sh
```

### Customization

- Modify `files/wg-obfuscator.config` for default UCI settings
- Update `files/wg-obfuscator.init` for service behavior
- Customize `files/wg-obfuscator-config.sh` for config generation
- Modify LuCI templates in `luci-app-wg-obfuscator/luasrc/view/`

## License

This OpenWRT integration is released under the same license as the main project (GPL-2.0).

## Support

For issues related to:
- OpenWRT integration: Create an issue in this repository
- WireGuard Obfuscator core: Visit the main project repository
- OpenWRT build system: Check OpenWRT documentation