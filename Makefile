PROG_NAME    = wg-obfuscator
CONFIG       = wg-obfuscator.conf
SERVICE_FILE = wg-obfuscator.service
COMMIT		   :=
COMMIT_INFO	 = commit.h
HEADERS      = wg-obfuscator.h

RELEASE ?= 0

RM    = rm -f
CC    = gcc
ifdef DEBUG
  CFLAGS   = -g -O0 -Wall -DDEBUG
else
  CFLAGS   = -O2 -Wall
endif
OBJS = wg-obfuscator.o
EXEDIR = .

LDFLAGS +=

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
  CFLAGS += -Wno-stringop-truncation
  LDFLAGS += -largp
  TARGET = $(EXEDIR)/$(PROG_NAME).exe  
else
  TARGET = $(EXEDIR)/$(PROG_NAME)
  # build on macos(arm) support
  UNAME_S := $(shell uname -s)
  ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -largp
    ifeq ($(IS_MACARM), 1)
      LDFLAGS += -L$(shell brew --prefix)/lib
    endif
  else
    CFLAGS += -Wno-stringop-truncation
  endif
endif

# force to RELEASE if ".git" directory is not present
ifeq ($(wildcard .git),)
  RELEASE := 1
endif

all: $(TARGET)

$(COMMIT_INFO):
  # Try to get commit hash from git
ifeq ($(RELEASE),0)
	@COMMIT=$$(git rev-parse --short HEAD 2>/dev/null) ; \
	if ! git diff-index --quiet HEAD -- ; then \
		COMMIT="$$COMMIT (dirty)"; \
	fi; \
	if [ -n "$$COMMIT" ]; then \
	  echo "#define COMMIT \"$$COMMIT\"" > $(COMMIT_INFO) ; \
	else \
	  echo > $(COMMIT_INFO) ; \
	fi
else
	rm -rf $(COMMIT_INFO)
	touch $(COMMIT_INFO)
endif

clean:
	$(RM) *.o $(COMMIT_INFO)
ifeq ($(OS),Windows_NT)
	@if [ -f "$(TARGET)" ]; then for f in `cygcheck "$(TARGET)" | grep .dll | grep msys` ; do rm -f $(EXEDIR)/`basename "$$f"` ; done fi
endif
	$(RM) $(TARGET)

$(OBJS): 

%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

$(TARGET): $(COMMIT_INFO) $(OBJS) $(HEADERS)
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

.PHONY: clean install $(COMMIT_INFO)
