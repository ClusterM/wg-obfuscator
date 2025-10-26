# OpenWRT Integration Guide

This document explains how to integrate WireGuard Obfuscator into an OpenWRT build system.

## Directory Structure

Place the contents of this directory in your OpenWRT build system at:
```
package/network/wg-obfuscator/
├── Makefile
└── files/
    ├── wg-obfuscator.config
    ├── wg-obfuscator.init
    ├── wg-obfuscator.conf
    └── wg-obfuscator-config.sh
```

## Build Integration

1. **Add to menuconfig**: The package will appear in `Network` → `wg-obfuscator`

2. **Build the package**:
   ```bash
   make menuconfig
   # Select Network -> wg-obfuscator
   make package/wg-obfuscator/compile
   ```

3. **Include in image**: Add to your image configuration:
   ```bash
   make menuconfig
   # Select Target Images -> Include wg-obfuscator
   ```

## Package Dependencies

The package has minimal dependencies:
- `libc` (standard C library)
- No external libraries required

## Configuration Integration

The package integrates with OpenWRT's UCI system:

- Configuration: `/etc/config/wg-obfuscator`
- Generated config: `/etc/wg-obfuscator/wg-obfuscator.conf`
- Init script: `/etc/init.d/wg-obfuscator`

## Service Integration

The service uses OpenWRT's procd system for:
- Process management
- Automatic restart on failure
- Configuration reload on changes
- Proper logging integration

## LuCI Integration (Optional)

To add LuCI web interface support, create a LuCI application:

1. Create `luci-app-wg-obfuscator` package
2. Add UCI model for configuration
3. Create web interface for settings

Example LuCI model structure:
```
luci-app-wg-obfuscator/
├── Makefile
├── luasrc/
│   ├── model/
│   │   └── cbi/
│   │       └── wg-obfuscator.lua
│   └── controller/
│       └── wg-obfuscator.lua
└── root/
    └── etc/
        └── uci-defaults/
            └── luci-wg-obfuscator
```

## Testing

Test the integration:

1. Build and install the package
2. Configure via UCI
3. Start the service
4. Verify configuration generation
5. Test obfuscation functionality

## Troubleshooting

Common issues:

1. **Service won't start**: Check UCI configuration and generated config file
2. **Permission denied**: Ensure running as root (service handles this)
3. **Port already in use**: Check for conflicts with other services
4. **Configuration not updating**: Restart service after UCI changes

## Customization

To customize the package:

1. Modify `Makefile` for build options
2. Update `files/wg-obfuscator.config` for default settings
3. Modify `files/wg-obfuscator.init` for service behavior
4. Update `files/wg-obfuscator-config.sh` for config generation logic