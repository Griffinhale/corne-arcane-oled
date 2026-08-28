# The browser shell: the same core, compiled to wasm32, plus two static files.
#
# Standalone clang rather than emscripten, deliberately. The core is genuinely
# freestanding -- stdint/stdbool/stddef/string, no floats, no allocator, no OS
# calls -- so -nostdlib and the two symbols in duel_wasm.c are the whole of the
# runtime. Emscripten would supply a libc and a JS runtime that nothing here
# calls, and would hide the fact that nothing here calls them.
#
# The sources are the shared list, read the same way desktop/Makefile reads it,
# so the browser build cannot drift from what the firmware and the tests
# compile. Only WASM_SRC is new, and nothing under ../firmware/sim is touched.

CLANG    ?= clang
CFLAGS   ?= -std=c11 -Oz -flto
CFLAGS   += -Wall -Wextra -Werror -I../firmware/sim -I../desktop -I. -Ifreestanding
CFLAGS   += --target=wasm32 -nostdlib -ffreestanding -fvisibility=hidden

# --no-entry: there is no main. --export-dynamic is deliberately absent, so the
# export list is exactly the export_name attributes in duel_wasm.c.
# --initial-memory covers the statics with room for the stack; the module never
# allocates, so it never grows.
LDFLAGS  := -Wl,--no-entry -Wl,--strip-all -Wl,--initial-memory=1048576 \
            -Wl,--stack-first -Wl,-z,stack-size=65536

SIM_DIR := ../firmware/sim
include ../firmware/sim_sources.mk

WASM_SRC := duel_wasm.c ../desktop/duel_city.c ../desktop/duel_ambient.c \
            ../desktop/duel_town_draw.c
HDRS := $(wildcard ../firmware/sim/*.h) $(wildcard ../desktop/*.h)
WASM := duel_city.wasm

$(WASM): $(WASM_SRC) $(SIM_SRC) $(HDRS)
	$(CLANG) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.c,$^)

clean:
	rm -f $(WASM)

.PHONY: clean
