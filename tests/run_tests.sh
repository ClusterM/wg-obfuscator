#!/bin/bash
#
# Integration Test Runner for WireGuard Obfuscator
#
# This script runs a full integration test:
# 1. Starts a fake WireGuard server
# 2. Starts the obfuscator
# 3. Runs a fake WireGuard client
# 4. Validates the results
#

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Test configuration
WG_SERVER_PORT=51820
OBFUSCATOR_PORT=3333
TEST_KEY="test_secret_key_123"
LOG_DIR="/tmp/wg0-obfuscator-test"

# PIDs of background processes
WG_SERVER_PID=""
OBFUSCATOR_PID=""

# Cleanup function
cleanup() {
    echo -e "\n${YELLOW}[CLEANUP]${NC} Stopping all test processes..."

    if [ -n "$OBFUSCATOR_PID" ]; then
        kill $OBFUSCATOR_PID 2>/dev/null || true
        wait $OBFUSCATOR_PID 2>/dev/null || true
        echo "[CLEANUP] Stopped obfuscator (PID $OBFUSCATOR_PID)"
    fi

    if [ -n "$WG_SERVER_PID" ]; then
        kill $WG_SERVER_PID 2>/dev/null || true
        wait $WG_SERVER_PID 2>/dev/null || true
        echo "[CLEANUP] Stopped WireGuard server (PID $WG_SERVER_PID)"
    fi

    # Wait a bit for ports to be released
    sleep 1
}

# Set trap to cleanup on exit
trap cleanup EXIT

# Check if binaries exist
check_binaries() {
    echo -e "${BLUE}[CHECK]${NC} Verifying test binaries..."

    if [ ! -f "../wg0-obfuscator" ]; then
        echo -e "${RED}[ERROR]${NC} wg0-obfuscator binary not found"
        echo "Run 'make' first to build the project"
        exit 1
    fi

    if [ ! -f "./test_wg_emulator" ]; then
        echo -e "${RED}[ERROR]${NC} test_wg_emulator binary not found"
        echo "Run 'make test-build' first to build test binaries"
        exit 1
    fi

    echo -e "${GREEN}[CHECK]${NC} All binaries found"
}

# Start fake WireGuard server
start_wg_server() {
    echo -e "\n${BLUE}[SETUP]${NC} Starting fake WireGuard server on port $WG_SERVER_PORT..."

    mkdir -p "$LOG_DIR"
    ./test_wg_emulator server $WG_SERVER_PORT > "$LOG_DIR/wg_server.log" 2>&1 &
    WG_SERVER_PID=$!

    # Wait for server to start
    sleep 1

    if ! kill -0 $WG_SERVER_PID 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to start WireGuard server"
        cat "$LOG_DIR/wg_server.log"
        exit 1
    fi

    echo -e "${GREEN}[SETUP]${NC} WireGuard server started (PID $WG_SERVER_PID)"
}

# Start obfuscator
start_obfuscator() {
    echo -e "${BLUE}[SETUP]${NC} Starting obfuscator..."
    echo "  Listen port: $OBFUSCATOR_PORT"
    echo "  Forward to: 127.0.0.1:$WG_SERVER_PORT"
    echo "  Key: $TEST_KEY"

    ../wg0-obfuscator \
        -p $OBFUSCATOR_PORT \
        -t 127.0.0.1:$WG_SERVER_PORT \
        -k "$TEST_KEY" \
        -v DEBUG > "$LOG_DIR/obfuscator.log" 2>&1 &
    OBFUSCATOR_PID=$!

    # Wait for obfuscator to start
    sleep 1

    if ! kill -0 $OBFUSCATOR_PID 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Failed to start obfuscator"
        cat "$LOG_DIR/obfuscator.log"
        exit 1
    fi

    echo -e "${GREEN}[SETUP]${NC} Obfuscator started (PID $OBFUSCATOR_PID)"
}

# Run client test
run_client_test() {
    echo -e "\n${BLUE}[TEST]${NC} Running WireGuard client emulator..."

    if ./test_wg_emulator client 127.0.0.1 $OBFUSCATOR_PORT > "$LOG_DIR/wg_client.log" 2>&1; then
        echo -e "${GREEN}[TEST]${NC} Client test completed"
        return 0
    else
        echo -e "${RED}[TEST]${NC} Client test failed"
        return 1
    fi
}

# Validate results
validate_results() {
    echo -e "\n${BLUE}[VALIDATE]${NC} Checking test results..."

    local errors=0

    # Check obfuscator log for handshake detection
    if grep -q "handshake" "$LOG_DIR/obfuscator.log"; then
        echo -e "${GREEN}[VALIDATE]${NC} ✓ Obfuscator detected handshake"
    else
        echo -e "${RED}[VALIDATE]${NC} ✗ Obfuscator did not detect handshake"
        ((errors++))
    fi

    # Check if obfuscator received packets from client
    if grep -q "Received.*bytes from" "$LOG_DIR/obfuscator.log"; then
        echo -e "${GREEN}[VALIDATE]${NC} ✓ Obfuscator received packets from client"
    else
        echo -e "${RED}[VALIDATE]${NC} ✗ Obfuscator did not receive packets from client"
        ((errors++))
    fi

    # Check if obfuscator forwarded packets to server
    if grep -q "Sent.*bytes to" "$LOG_DIR/obfuscator.log" || \
       grep -q "Forward" "$LOG_DIR/obfuscator.log"; then
        echo -e "${GREEN}[VALIDATE]${NC} ✓ Obfuscator forwarded packets to server"
    else
        echo -e "${RED}[VALIDATE]${NC} ✗ Obfuscator did not forward packets to server"
        ((errors++))
    fi

    # Check if WireGuard server received packets
    if [ -f "$LOG_DIR/wg_server.log" ]; then
        local packet_count=$(grep -c "Received.*bytes" "$LOG_DIR/wg_server.log" || echo "0")
        if [ "$packet_count" -gt 0 ]; then
            echo -e "${GREEN}[VALIDATE]${NC} ✓ WireGuard server received $packet_count packets"
        else
            echo -e "${RED}[VALIDATE]${NC} ✗ WireGuard server received no packets"
            ((errors++))
        fi
    fi

    # Check if client received handshake response
    if grep -q "Received handshake response" "$LOG_DIR/wg_client.log"; then
        echo -e "${GREEN}[VALIDATE]${NC} ✓ Client received handshake response"
    else
        echo -e "${YELLOW}[VALIDATE]${NC} ⚠ Client did not receive handshake response (may timeout)"
    fi

    return $errors
}

# Show logs function
show_logs() {
    echo -e "\n${BLUE}[LOGS]${NC} Test logs are available at:"
    echo "  Obfuscator: $LOG_DIR/obfuscator.log"
    echo "  WG Server:  $LOG_DIR/wg_server.log"
    echo "  WG Client:  $LOG_DIR/wg_client.log"

    if [ "${SHOW_LOGS}" = "1" ]; then
        echo -e "\n${BLUE}=== Obfuscator Log ===${NC}"
        cat "$LOG_DIR/obfuscator.log"
        echo -e "\n${BLUE}=== WireGuard Server Log ===${NC}"
        cat "$LOG_DIR/wg_server.log"
        echo -e "\n${BLUE}=== WireGuard Client Log ===${NC}"
        cat "$LOG_DIR/wg_client.log"
    else
        echo -e "\nRun with ${YELLOW}SHOW_LOGS=1${NC} to display all logs"
    fi
}

# Main test execution
main() {
    echo -e "${BLUE}╔═══════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║  WireGuard Obfuscator Integration Test ║${NC}"
    echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}\n"

    check_binaries
    start_wg_server
    start_obfuscator

    # Give everything time to stabilize
    sleep 2

    run_client_test
    local client_result=$?

    # Give time for logs to flush
    sleep 1

    validate_results
    local validate_result=$?

    show_logs

    # Summary
    echo -e "\n${BLUE}╔═══════════════════════════════════════╗${NC}"
    echo -e "${BLUE}║         Test Results Summary          ║${NC}"
    echo -e "${BLUE}╚═══════════════════════════════════════╝${NC}"

    if [ $client_result -eq 0 ] && [ $validate_result -eq 0 ]; then
        echo -e "${GREEN}✓ All tests PASSED${NC}\n"
        exit 0
    else
        echo -e "${RED}✗ Some tests FAILED${NC}\n"
        echo -e "Check logs at: ${YELLOW}$LOG_DIR${NC}\n"
        exit 1
    fi
}

# Run main
main "$@"
