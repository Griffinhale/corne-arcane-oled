# Native acceptance rig for the current deterministic firmware world.

CC       ?= cc
# Environment CFLAGS may add flags but must not silently drop the safety net:
# sanitizers and -Werror live in SANFLAGS, appended unconditionally.
CFLAGS   ?= -std=c11 -g -O1
SANFLAGS  = -Wall -Wextra -Werror -fsanitize=address,undefined
CFLAGS   += $(SANFLAGS) -I../sim

SIM_SRC := ../sim/duel_framebuffer.c ../sim/duel_render.c \
           ../sim/duel_environment_draw.c ../sim/duel_combat_draw.c \
           ../sim/duel_overlay_draw.c ../sim/duel_draw.c \
           ../sim/duel_sim.c ../sim/duel_incantation.c \
           ../sim/duel_view.c ../sim/duel_proto.c ../sim/duel_host.c \
           ../sim/duel_display.c ../sim/duel_resident.c \
           ../sim/duel_courier.c ../sim/duel_event.c ../sim/duel_runtime.c \
           ../sim/duel_rgb.c
HDRS := $(wildcard ../sim/*.h) scenarios.h

test: mechanics-test visual-test noalloc-check

RUNNER_RECIPE = $(CC) $(CFLAGS) -o $@ $(filter %.c,$^)

mechanics_runner: mechanics_test.c test_support.c test_support.h $(SIM_SRC) $(HDRS)
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
# cannot pass code the test build would reject.
NOALLOC_SYMS := malloc|calloc|realloc|free|aligned_alloc|posix_memalign|reallocarray|strdup|strndup|asprintf|vasprintf
noalloc-check:
	@trap 'rm -f .noalloc.o' EXIT; \
	for f in $(SIM_SRC); do \
		$(CC) -std=c11 -O1 -Wall -Wextra -Werror -I../sim -c $$f -o .noalloc.o || exit 1; \
		if nm .noalloc.o | grep -qE ' U ($(NOALLOC_SYMS))$$'; then \
			echo "FAIL noalloc-check: $$f references the allocator"; exit 1; \
		fi; \
	done; echo "PASS noalloc-check"

clean:
	rm -f mechanics_runner visual_runner .noalloc.o

.PHONY: test mechanics-test visual-test visual-golden noalloc-check clean
