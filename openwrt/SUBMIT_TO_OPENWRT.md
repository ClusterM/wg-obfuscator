# Adding wg-obfuscator to Official OpenWRT Repository

## Required Files

Only 2 files are needed to add to the official OpenWRT repository:

1. **Makefile** - main package file
2. **files/wg-obfuscator.init** - init script

## Repository Structure

```
feeds/packages/net/wg-obfuscator/
├── Makefile
└── files/
    └── wg-obfuscator.init
```

## Adding Process

1. **Fork the OpenWRT repository**:
   ```bash
   git clone https://github.com/openwrt/packages.git
   cd packages
   ```

2. **Create package directory**:
   ```bash
   mkdir -p net/wg-obfuscator/files
   ```

3. **Copy files**:
   ```bash
   cp openwrt/Makefile net/wg-obfuscator/
   cp openwrt/files/wg-obfuscator.init net/wg-obfuscator/files/
   ```

4. **Create commit**:
   ```bash
   git add net/wg-obfuscator/
   git commit -m "net/wg-obfuscator: add WireGuard Obfuscator package

   - Add wg-obfuscator package for obfuscating WireGuard traffic
   - Supports DPI evasion through masking (STUN protocol)
   - Minimal dependencies, only requires libc
   - Includes init script for procd integration"
   ```

5. **Create Pull Request** to the official OpenWRT repository

## Pre-submission Checks

1. **Check build**:
   ```bash
   make package/wg-obfuscator/compile V=s
   ```

2. **Check dependencies**:
   ```bash
   make package/wg-obfuscator/check-depends
   ```

3. **Check installation**:
   ```bash
   make package/wg-obfuscator/install
   ```

## Package Description

- **Name**: wg-obfuscator
- **Version**: 1.4
- **License**: GPL-2.0
- **Category**: Network
- **Dependencies**: +libc
- **Size**: ~50KB

## Features

- Minimal dependencies (libc only)
- Support for all OpenWRT architectures
- procd integration
- Automatic restart on configuration changes
- Support for multiple instances via configuration file

## Contacts

- **Maintainer**: ClusterM <cluster@cluster.wtf>
- **URL**: https://github.com/ClusterM/wg-obfuscator
- **License**: GPL-2.0