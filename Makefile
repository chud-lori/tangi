# tangi — tiny cross-platform "keep awake" tool
#
#   make            build ./tangi (optimized for size, stripped)
#   make install    install to $(PREFIX)/bin  (default /usr/local)
#   make clean      remove build artifacts

VERSION := 0.1.0
PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin

CC      ?= cc
CFLAGS  ?= -Os -Wall -Wextra -std=c11
CFLAGS  += -DTANGI_VERSION=\"$(VERSION)\"
LDFLAGS ?=

BIN     := tangi
SRC     := src/main.c src/duration.c src/ipc.c src/daemon.c

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  SRC     += src/platform_mac.c
  LDFLAGS += -framework IOKit -framework CoreFoundation
else
  SRC     += src/platform_linux.c
  # Use libsystemd (sd-bus) when available for a clean fd-based inhibitor;
  # otherwise fall back to spawning `systemd-inhibit` at runtime.
  HAVE_SYSTEMD := $(shell pkg-config --exists libsystemd && echo 1)
  ifeq ($(HAVE_SYSTEMD),1)
    CFLAGS  += -DHAVE_SYSTEMD $(shell pkg-config --cflags libsystemd)
    LDFLAGS += $(shell pkg-config --libs libsystemd)
  endif
  LDFLAGS += -s
endif

OBJ := $(SRC:.c=.o)

.PHONY: all clean install uninstall

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDFLAGS)
ifeq ($(UNAME_S),Darwin)
	strip $@
endif

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN) $(OBJ)
