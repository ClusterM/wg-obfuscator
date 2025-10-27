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
OBJS = wg-obfuscator.o config.o masking.o masking_stun.o
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
	$(RM) *.o
ifeq ($(OS),Windows_NT)
	@if [ -f "$(TARGET)" ]; then for f in `cygcheck "$(TARGET)" | grep .dll | grep msys` ; do rm -f $(EXEDIR)/`basename "$$f"` ; done fi
endif
	$(RM) $(TARGET)

$(OBJS): 

%.o : %.c $(HEADERS)
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

.PHONY: clean install
