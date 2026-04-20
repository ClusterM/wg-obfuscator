PROG_NAME    = wg-obfuscator
CONFIG       = wg-obfuscator.conf
SERVICE_FILE = wg-obfuscator.service
HEADERS      = wg-obfuscator.h obfuscation.h config.h uthash.h mini_argp.h masking.h masking_stun.h

RELEASE ?= 0

RM    = rm -f
CC    = gcc
ifdef DEBUG
  CFLAGS   = -g -O0 -Wall -DDEBUG
  LDFLAGS +=
else
  CFLAGS   = -O2 -Wall
  LDFLAGS += -s
endif
# Build directories
BUILD_DIR = build

# Object files in build directory
OBJS = $(BUILD_DIR)/wg-obfuscator.o $(BUILD_DIR)/config.o $(BUILD_DIR)/masking.o $(BUILD_DIR)/masking_stun.o
EXEDIR = .

EXTRA_CFLAGS =

ifeq ($(OS),Windows_NT)
  TARGET = $(EXEDIR)/$(PROG_NAME).exe
else
  TARGET = $(EXEDIR)/$(PROG_NAME)
endif

# build on macos(arm) support
IS_MACARM := 0
ifneq ($(OS),Windows_NT)
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    UNAME_P := $(shell uname -p)
    ifneq ($(filter arm%,$(UNAME_P)),)
       CFLAGS += -I$(shell brew --prefix)/include
       IS_MACARM = 1
    endif
  endif
endif

ifeq ($(OS),Windows_NT)
  EXTRA_CFLAGS += -Wno-stringop-truncation
  TARGET = $(EXEDIR)/$(PROG_NAME).exe  
else
  TARGET = $(EXEDIR)/$(PROG_NAME)
  # build on macos(arm) support
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    ifeq ($(IS_MACARM), 1)
      LDFLAGS += -L$(shell brew --prefix)/lib
    endif
  else
    EXTRA_CFLAGS += -Wno-stringop-truncation
  endif
endif

all: $(TARGET)

# Force to RELEASE if ".git" directory is not present
ifeq ($(RELEASE),0)
ifeq ($(wildcard .git),)
  RELEASE := 1
endif	
endif
ifeq ($(RELEASE),0)
# Force to RELEASE if we are on the tag and repo is clean
ifeq ($(shell git update-index -q --refresh ; git diff-index --quiet HEAD -- 2>/dev/null; echo $$?),0)
ifneq ($(shell git describe --exact-match --tags 2>/dev/null),)
  RELEASE := 1
endif
endif
endif
# Get commit id
ifeq ($(RELEASE),0)
  COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null)
  DIRTY := $(shell if ! git diff-index --quiet HEAD --; then echo " (dirty)"; fi)
  COMMIT := $(COMMIT)$(DIRTY)
  EXTRA_CFLAGS += -DCOMMIT="\"$(COMMIT)\""
endif

# Pass architecture information to the program, for Docker
TARGETPLATFORM ?=
ifneq ($(TARGETPLATFORM),)
  EXTRA_CFLAGS += -DARCH="\"$(TARGETPLATFORM)\""
endif

clean:
	$(RM) -r $(BUILD_DIR)
ifeq ($(OS),Windows_NT)
	@if [ -f "$(TARGET)" ]; then for f in `cygcheck "$(TARGET)" | grep .dll | grep msys` ; do rm -f $(EXEDIR)/`basename "$$f"` ; done fi
endif
	$(RM) $(TARGET)
	@$(MAKE) -s clean-tests 2>/dev/null || true

# Create build directory if it doesn't exist
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Compile source files to build directory
$(BUILD_DIR)/%.o : %.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ -c $<

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)
ifeq ($(OS),Windows_NT)
	@for f in `cygcheck "$(TARGET)" | grep .dll | grep msys` ; do if [ ! -f "$(EXEDIR)/`basename $$f`" ] ; then cp -vf `cygpath "$$f"` $(EXEDIR)/ ; fi ; done
endif

install: $(TARGET)
ifeq ($(OS),Windows_NT)
	@echo "Windows is not supported for install"
else
	install -m 755 $(TARGET) $(DESTDIR)/usr/bin
	@if [ ! -f "$(DESTDIR)/etc/$(CONFIG)" ]; then \
		install -m 644 $(CONFIG) $(DESTDIR)/etc; \
	else \
		echo "$(DESTDIR)/etc/$(CONFIG) already exists, skipping"; \
	fi
	install -m 644 $(SERVICE_FILE) $(DESTDIR)/etc/systemd/system
	systemctl daemon-reload
	systemctl enable $(SERVICE_FILE)
	systemctl restart $(SERVICE_FILE)
endif

# Test targets
TEST_DIR = tests
TEST_BUILD_DIR = $(TEST_DIR)/build
TEST_HARNESS = $(TEST_DIR)/test_harness
TEST_WG_EMULATOR = $(TEST_DIR)/test_wg_emulator

# Create test build directory
$(TEST_BUILD_DIR):
	@mkdir -p $(TEST_BUILD_DIR)

$(TEST_BUILD_DIR)/test_harness.o: $(TEST_DIR)/test_harness.c $(HEADERS) | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -I. -o $@ -c $<

$(TEST_BUILD_DIR)/test_wg_emulator.o: $(TEST_DIR)/test_wg_emulator.c | $(TEST_BUILD_DIR)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -o $@ -c $<

$(TEST_HARNESS): $(TEST_BUILD_DIR)/test_harness.o
	$(CC) -o $(TEST_HARNESS) $(TEST_BUILD_DIR)/test_harness.o $(LDFLAGS)

$(TEST_WG_EMULATOR): $(TEST_BUILD_DIR)/test_wg_emulator.o
	$(CC) -o $(TEST_WG_EMULATOR) $(TEST_BUILD_DIR)/test_wg_emulator.o $(LDFLAGS)

# Build all test binaries
test-build: $(TEST_HARNESS) $(TEST_WG_EMULATOR)
	@echo "Test binaries built successfully"

# Run unit tests only
test-unit: $(TEST_HARNESS)
	@echo "Running unit tests..."
	@./$(TEST_HARNESS)

# Run integration tests (requires main binary)
test-integration: $(TARGET) $(TEST_WG_EMULATOR)
	@echo "Running integration tests..."
	@cd $(TEST_DIR) && ./run_tests.sh

# Run the stderr-pipe-blocking regression test (Linux only).
# Not included in the default 'test' target because it requires
# /proc/<pid>/wchan and `ss` (iproute2); skip gracefully elsewhere.
test-stderr-block: $(TARGET)
	@if [ "$$(uname -s)" != "Linux" ]; then \
		echo "test-stderr-block: skipped (Linux-only)"; \
		exit 0; \
	fi
	@$(TEST_DIR)/test_stderr_block.sh

# Run all tests
test: test-build test-unit test-integration test-stderr-block
	@echo ""
	@echo "========================================="
	@echo "All tests completed successfully!"
	@echo "========================================="

# Clean test artifacts
clean-tests:
	$(RM) -r $(TEST_BUILD_DIR)
	$(RM) $(TEST_HARNESS) $(TEST_WG_EMULATOR)
	$(RM) -r /tmp/wg-obfuscator-test

.PHONY: clean install test test-build test-unit test-integration test-stderr-block clean-tests
