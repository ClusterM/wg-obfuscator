# GitHub Actions Workflows

This directory contains GitHub Actions workflows for automated testing and building of the wg-obfuscator project.

## Workflows

### 1. `build.yml` - Main Build Workflow
- **Triggers**: Push to master, Pull Requests, Manual dispatch
- **Purpose**: Builds the main wg-obfuscator binary for multiple platforms
- **Platforms**: Linux (various architectures), macOS, Windows
- **Artifacts**: Binary packages and Docker images

### 2. `openwrt-build.yml` - OpenWRT Build Test
- **Triggers**: Push to master (when OpenWRT files change), Pull Requests, Manual dispatch
- **Purpose**: Tests OpenWRT package compilation for various targets
- **Platforms**: 15+ OpenWRT targets including x86_64, ARM, MIPS, RISC-V, PowerPC
- **Features**:
  - Downloads OpenWRT SDK for each target
  - Compiles wg-obfuscator package
  - Compiles LuCI web interface package
  - Validates package contents
  - Tests configuration generation
  - Syntax validation for all scripts

### 3. `openwrt-quick-test.yml` - Quick OpenWRT Test
- **Triggers**: Manual dispatch only
- **Purpose**: Fast testing of OpenWRT package on key platforms
- **Platforms**: 4 most common OpenWRT targets (x86_64, MIPS, ARM, AArch64)
- **Use case**: Quick verification before full testing

### 4. `nightly.yml` - Nightly Builds
- **Triggers**: Scheduled (nightly)
- **Purpose**: Regular testing of the latest code
- **Platforms**: All supported platforms

### 5. `release.yml` - Release Workflow
- **Triggers**: Tag creation
- **Purpose**: Creates official releases
- **Features**: Builds all platforms, creates GitHub release

## OpenWRT Testing Details

### Supported Targets

The OpenWRT build test covers these popular targets:

| Target | Architecture | Subtarget | Description |
|--------|-------------|-----------|-------------|
| x86_64 | x86_64 | generic | 64-bit x86 (most routers) |
| x86 | x86 | generic | 32-bit x86 (legacy) |
| mips_24kc | mips | 24kc | MIPS 24Kc (many home routers) |
| mipsel_24kc | mipsel | 24kc | MIPS 24Kc little-endian |
| mips_74kc | mips | 74kc | MIPS 74Kc (higher-end routers) |
| mipsel_74kc | mipsel | 74kc | MIPS 74Kc little-endian |
| arm_cortex-a7_neon-vfpv4 | arm_cortex-a7 | neon-vfpv4 | ARM Cortex-A7 (modern ARM routers) |
| arm_cortex-a9_vfpv3-d16 | arm_cortex-a9 | vfpv3-d16 | ARM Cortex-A9 (older ARM routers) |
| aarch64_cortex-a72 | aarch64 | cortex-a72 | ARM64 Cortex-A72 (high-end routers) |
| aarch64_generic | aarch64 | generic | Generic ARM64 |
| mips64_mips64 | mips64 | mips64 | 64-bit MIPS |
| mipsel_64 | mipsel | 64 | 64-bit MIPS little-endian |
| powerpc_464fp | powerpc | 464fp | PowerPC 464FP |
| powerpc_8540 | powerpc | 8540 | PowerPC 8540 |
| riscv64_generic | riscv64 | generic | RISC-V 64-bit |

### What Gets Tested

1. **Package Compilation**: Verifies that the OpenWRT package builds successfully
2. **LuCI Integration**: Tests the web interface package compilation
3. **File Validation**: Ensures all required files are present in the package
4. **Script Syntax**: Validates shell scripts and Lua files
5. **Configuration Generation**: Tests UCI-based configuration generation
6. **Error Handling**: Verifies proper error handling in scripts

### Build Process

For each target, the workflow:

1. Downloads the appropriate OpenWRT SDK
2. Copies the wg-obfuscator package files
3. Configures the OpenWRT build system
4. Compiles the package and dependencies
5. Validates the resulting `.ipk` package
6. Tests package installation simulation
7. Cleans up temporary files

### Manual Testing

You can manually trigger OpenWRT testing:

1. Go to the "Actions" tab in GitHub
2. Select "OpenWRT Build Test" or "OpenWRT Quick Test"
3. Click "Run workflow"
4. Choose the branch and optionally specific targets
5. Click "Run workflow"

### Troubleshooting

#### Build Failures

If a build fails:

1. Check the logs for the specific target
2. Look for compilation errors or missing dependencies
3. Verify that the OpenWRT SDK version is compatible
4. Check if the target architecture is supported

#### Common Issues

- **SDK Download Failures**: OpenWRT SDK URLs may change; update the version in the workflow
- **Missing Dependencies**: Some targets may require additional packages
- **Architecture Mismatches**: Ensure the target architecture is correctly specified
- **Script Syntax Errors**: Use `bash -n` to check shell script syntax locally

#### Local Testing

To test locally:

```bash
# Test script syntax
bash -n openwrt/files/wg-obfuscator-config.sh
bash -n openwrt/files/wg-obfuscator.init

# Test configuration generation
UCI_CONFIG="wg-obfuscator" openwrt/files/wg-obfuscator-config.sh

# Test OpenWRT build (requires OpenWRT SDK)
./openwrt/build-openwrt.sh
```

### Performance

- **Full Test**: ~30-45 minutes for all targets
- **Quick Test**: ~10-15 minutes for key targets
- **Individual Target**: ~3-5 minutes per target

The workflows use `fail-fast: false` to ensure all targets are tested even if some fail.

### Dependencies

The workflows automatically install:

- Build tools (gcc, make, etc.)
- OpenWRT SDK dependencies
- UCI utilities for testing
- Lua interpreter for syntax checking

No manual setup is required - everything is handled automatically by GitHub Actions.