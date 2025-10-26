# WireGuard Obfuscator Tests

This directory contains all test files for the WireGuard Obfuscator project.

## Structure

```
tests/
├── README.md                  # This file
├── TESTING.md                 # Detailed testing documentation
├── TESTING_QUICK_START.md     # Quick start guide
├── test_harness.c             # Unit tests for obfuscation functions
├── test_wg_emulator.c         # WireGuard packet emulator
├── run_tests.sh               # Integration test runner script
├── test_harness               # Compiled unit test binary (generated)
└── test_wg_emulator           # Compiled emulator binary (generated)
```

## Quick Start

From the **project root** directory:

```bash
# Run all tests
make test

# Run only unit tests
make test-unit

# Run only integration tests
make test-integration

# Build test binaries
make test-build

# Clean test artifacts
make clean-tests
```

## Files Description

### test_harness.c
Unit tests for core obfuscation functionality:
- XOR encryption/decryption
- Packet encoding/decoding
- Dummy data padding
- WireGuard packet type detection
- Key validation
- Edge cases (min/max packet sizes, various key lengths)

**Tests included:** 10 unit tests

### test_wg_emulator.c
WireGuard protocol emulator that can run in two modes:
- **Server mode**: Listens for packets and responds (like WireGuard server)
- **Client mode**: Sends handshake and data packets (like WireGuard client)

Can be used standalone for manual testing.

### run_tests.sh
Automated integration test script that:
1. Starts fake WireGuard server
2. Starts obfuscator
3. Runs client emulator
4. Validates packet flow
5. Checks logs for expected behavior

## Usage Examples

### Run All Tests (Recommended)

```bash
cd /path/to/wg0-obfuscator
make test
```

### Run Unit Tests Only

```bash
make test-unit
```

Output:
```
=== WireGuard Obfuscator Unit Tests ===
[TEST] WireGuard packet type detection... PASS
[TEST] XOR data function... PASS
...
Passed: 10
Failed: 0
```

### Run Integration Tests Only

```bash
make test-integration
```

### Manual Testing with Emulator

From the `tests/` directory:

**Terminal 1 - Start Server:**
```bash
cd tests
./test_wg_emulator server 51820
```

**Terminal 2 - Start Obfuscator:**
```bash
./wg-obfuscator -p 3333 -t 127.0.0.1:51820 -k "test_key" -v DEBUG
```

**Terminal 3 - Run Client:**
```bash
cd tests
./test_wg_emulator client 127.0.0.1 3333
```

## Test Logs

Integration tests create logs in `/tmp/wg-obfuscator-test/`:
- `obfuscator.log` - Obfuscator debug output
- `wg_client.log` - Client emulator output
- `wg_server.log` - Server emulator output

View logs:
```bash
cat /tmp/wg-obfuscator-test/obfuscator.log
```

Or run tests with log display:
```bash
SHOW_LOGS=1 make test-integration
```

## Adding New Tests

### Adding a Unit Test

1. Edit `test_harness.c`
2. Add your test function:
```c
void test_my_feature() {
    TEST_START("My feature description");

    // Your test code
    TEST_ASSERT(condition, "Error message");

    TEST_PASS();
}
```
3. Call it from `main()`:
```c
test_my_feature();
```
4. Rebuild and run:
```bash
make test-unit
```

### Adding an Integration Scenario

Edit `run_tests.sh` and modify the test flow or validation logic.

## Troubleshooting

### Tests Hang

```bash
killall test_wg_emulator wg-obfuscator
make clean-tests
```

### Port Already in Use

```bash
ss -ulnp | grep -E '(3333|51820)'
killall -9 test_wg_emulator wg-obfuscator
```

### Rebuild Everything

```bash
make clean
make test
```

## Documentation

- **TESTING.md** - Complete testing guide with detailed explanations
- **TESTING_QUICK_START.md** - Quick reference for common testing scenarios

## Requirements

- GCC compiler
- Standard C library
- Linux/macOS/Windows (with appropriate toolchain)
- No WireGuard installation required for tests!

## Test Coverage

Current test coverage:
- ✅ Core obfuscation functions (10 unit tests)
- ✅ Packet encoding/decoding
- ✅ XOR encryption
- ✅ Dummy data padding
- ✅ WireGuard packet type detection
- ✅ Integration test framework
- ⚠️ STUN masking (manual testing only)
- ⚠️ Static bindings (manual testing only)

## Contributing Tests

When adding new features to the main project:
1. Write unit tests first (TDD approach)
2. Ensure all existing tests still pass
3. Add integration tests if needed
4. Update documentation

Run `make test` before committing!
