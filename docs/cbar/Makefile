CC ?= cc
CFLAGS ?= -O2
PKGS := wayland-client cairo pangocairo
LIBS := $(shell pkg-config --cflags --libs $(PKGS)) -lm

# Protocol C sources/headers are already generated; no wayland-scanner needed.
cproto: bar.c wlr-layer-shell-protocol.c xdg-shell-protocol.c
	$(CC) $(CFLAGS) -o $@ bar.c wlr-layer-shell-protocol.c xdg-shell-protocol.c $(LIBS)

# Optional test helper for wheel events (needs /dev/uinput and the input group).
scroll: scroll.c
	$(CC) $(CFLAGS) -o $@ scroll.c

clean:
	rm -f cproto scroll

.PHONY: clean
