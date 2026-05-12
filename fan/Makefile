CC ?= clang
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?= -framework IOKit -framework CoreFoundation

PREFIX ?= /usr/local
SBIN := $(PREFIX)/sbin

all: smcfanctl

smcfanctl: smcfanctl.c
	$(CC) $(CFLAGS) smcfanctl.c -o smcfanctl $(LDFLAGS)

install: smcfanctl
	install -d "$(SBIN)"
	install -m 0755 smcfanctl "$(SBIN)/smcfanctl"

clean:
	rm -f smcfanctl

.PHONY: all install clean
