PROG_NAME= wg_obfuscator
CONFIG=wg_obfuscator.conf
SERVICE_FILE=wg_obfuscator.service

RM       = rm -f
CC       = gcc
ifdef DEBUG
  CFLAGS   = -g -O0 -Wall -Wno-format-truncation -DDEBUG
else
  CFLAGS   = -O2 -Wall -Wno-format-truncation 
endif
OBJS = wg_obfuscator.o
EXEDIR = .

LDFLAGS +=

ifeq ($(OS),Windows_NT)
  TARGET = $(EXEDIR)/$(PROG_NAME).exe
else
  TARGET = $(EXEDIR)/$(PROG_NAME)
endif

all: $(TARGET)
default: $(TARGET)
run:  $(TARGET)
	$(TARGET)

clean:
	$(RM) *.o 
ifeq ($(OS),Windows_NT)
	@if [ -f "$(TARGET)" ]; then for f in `cygcheck "$(TARGET)" | grep .dll | grep msys` ; do rm -f $(EXEDIR)/`basename "$$f"` ; done fi
endif
	$(RM) $(TARGET)

$(OBJS): 

%.o : %.c
	$(CC) $(CFLAGS) -o $@ -c $<

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
	install -m 644 $(CONFIG) $(DESTDIR)/etc
	install -m 644 $(SERVICE_FILE) $(DESTDIR)/etc/systemd/system
	systemctl daemon-reload
	systemctl enable $(SERVICE_FILE)
	systemctl start $(SERVICE_FILE)
endif
