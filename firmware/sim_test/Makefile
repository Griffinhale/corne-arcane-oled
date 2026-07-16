# Native acceptance rig for the current deterministic firmware world.

CC       ?= cc
CFLAGS   ?= -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined
CFLAGS   += -I../sim

SIM_SRC := ../sim/duel_draw.c ../sim/duel_sim.c ../sim/duel_incantation.c \
           ../sim/duel_view.c ../sim/duel_proto.c ../sim/duel_host.c \
           ../sim/duel_display.c ../sim/duel_resident.c \
           ../sim/duel_courier.c ../sim/duel_event.c
HDRS := $(wildcard ../sim/*.h) scenarios.h

test: mechanics-test visual-test noalloc-check

mechanics_runner: mechanics_test.c $(SIM_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ mechanics_test.c $(SIM_SRC)

mechanics-test: mechanics_runner
	ASAN_OPTIONS=detect_leaks=0 ./mechanics_runner

visual_runner: visual_catalog.c scenarios.c $(SIM_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ visual_catalog.c scenarios.c $(SIM_SRC)

visual-test: visual_runner
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner golden/visual_current.hashes

visual-golden: visual_runner
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner --write-golden golden/visual_current.hashes

noalloc-check:
	@for f in $(SIM_SRC); do \
		$(CC) -std=c11 -O1 -I../sim -c $$f -o .noalloc.o || exit 1; \
		if nm .noalloc.o | grep -qE ' U (malloc|calloc|realloc|free)$$'; then \
			echo "FAIL noalloc-check: $$f references the allocator"; exit 1; \
		fi; \
	done; rm -f .noalloc.o; echo "PASS noalloc-check"

clean:
	rm -f mechanics_runner visual_runner .noalloc.o

.PHONY: test mechanics-test visual-test visual-golden noalloc-check clean
