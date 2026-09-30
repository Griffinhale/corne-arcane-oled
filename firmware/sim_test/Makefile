# Native acceptance rig for the current deterministic firmware world.

CC       ?= cc
# Environment CFLAGS may add flags but must not silently drop the safety net:
# sanitizers and -Werror live in SANFLAGS, appended unconditionally.
CFLAGS   ?= -std=c11 -g -O1
SANFLAGS  = -Wall -Wextra -Werror -fsanitize=address,undefined
CFLAGS   += $(SANFLAGS) -I../sim

SIM_DIR := ../sim
include ../sim_sources.mk
HDRS := $(wildcard ../sim/*.h) scenarios.h

test: mechanics-test visual-test noalloc-check

RUNNER_RECIPE = $(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

MECHANICS_SRC := test_main.c test_harness.c test_protocol_view.c \
                 test_diagnostics.c \
                 test_incantation_compiler.c test_combat_lifecycle.c \
                 test_fields_magic.c \
                 test_civic_presentation.c test_rendering_geometry.c \
                 test_runtime_display_rgb.c test_support.c

mechanics_runner: $(MECHANICS_SRC) test_harness.h test_support.h $(SIM_SRC) $(HDRS)
	$(RUNNER_RECIPE)

mechanics-test: mechanics_runner
	ASAN_OPTIONS=detect_leaks=0 ./mechanics_runner

visual_runner: visual_catalog.c scenarios.c $(SIM_SRC) $(HDRS)
	$(RUNNER_RECIPE)

visual-test: visual_runner
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner golden/visual_current.hashes

visual-golden: visual_runner
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner --write-golden golden/visual_current.hashes

# The allocation-free contract covers every allocating entry point, not just
# the big four; compile with the same warning set as the tests so this target
# cannot pass code the test build would reject. The shells are held to it too:
# the desktop library and the WebAssembly module both claim never to allocate.
# duel_wasm.c builds here natively only to be scanned, so the wasm-only
# export_name attribute a native compiler ignores is not an error.
NOALLOC_SYMS := malloc|calloc|realloc|free|aligned_alloc|posix_memalign|reallocarray|strdup|strndup|asprintf|vasprintf
NOALLOC_CFLAGS := -std=c11 -O1 -Wall -Wextra -Werror -I../sim
SHELL_SRC := $(wildcard ../../desktop/*.c) ../../web/duel_wasm.c
SHELL_CFLAGS := -I../../desktop -I../../web -I../../web/freestanding -ffreestanding -Wno-attributes
noalloc-check:
	@trap 'rm -f .noalloc.o' EXIT; \
	scan() { \
		if nm .noalloc.o | grep -qE ' U ($(NOALLOC_SYMS))$$'; then \
			echo "FAIL noalloc-check: $$1 references the allocator"; exit 1; \
		fi; \
	}; \
	for f in $(SIM_SRC); do \
		$(CC) $(NOALLOC_CFLAGS) -c $$f -o .noalloc.o || exit 1; scan $$f; \
	done; \
	echo "PASS noalloc-check: $(words $(SIM_SRC)) simulation sources"; \
	for f in $(SHELL_SRC); do \
		$(CC) $(NOALLOC_CFLAGS) $(SHELL_CFLAGS) -c $$f -o .noalloc.o || exit 1; scan $$f; \
		echo "PASS noalloc-check: $${f#../../}"; \
	done

clean:
	rm -f mechanics_runner visual_runner .noalloc.o

.PHONY: test mechanics-test visual-test visual-golden noalloc-check clean
