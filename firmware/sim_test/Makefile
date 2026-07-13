# Host-side test rig for the duel engine. Never referenced by the QMK build
# (rules.mk only pulls ../sim/*.c), so this whole directory is firmware-invisible.
#
#   make test          build + run all replay/golden tests
#   make golden        regenerate golden files (review the git diff!)
#   make preview       build the two-canvas terminal previewer
#   make noalloc-check fail if any sim object references malloc/free
#
# Run everything (nix-shell aware): ./run_tests.sh

CC       ?= cc
CFLAGS   ?= -std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined
CFLAGS   += -I../sim

SIM_SRC  := ../sim/duel_draw.c ../sim/duel_sim.c ../sim/duel_proto.c ../sim/duel_host.c

test: test_runner
	# LeakSanitizer cannot run under Codex/CI ptrace; the sim has a separate,
	# stricter symbol-level no-allocation gate. ASan/UBSan remain fully active.
	ASAN_OPTIONS=detect_leaks=0 ./test_runner traces golden

golden: test_runner
	./test_runner --write-golden traces golden

HDRS := trace.h runner.h ../sim/duel_draw.h ../sim/duel_sim.h ../sim/duel_proto.h ../sim/duel_host.h

test_runner: test_main.c trace.c $(SIM_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_main.c trace.c $(SIM_SRC)

preview: preview.c trace.c $(SIM_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ preview.c trace.c $(SIM_SRC)

# The "no dynamic allocation in the update path" gate, checked honestly:
# compile each sim source alone (no sanitizers — they'd add interceptors) and
# reject any undefined reference to the allocator.
noalloc-check:
	@for f in $(SIM_SRC); do \
		$(CC) -std=c11 -O1 -I../sim -c $$f -o .noalloc.o || exit 1; \
		if nm .noalloc.o | grep -qE ' U (malloc|calloc|realloc|free)$$'; then \
			echo "FAIL noalloc-check: $$f references the allocator"; exit 1; \
		fi; \
	done; rm -f .noalloc.o; echo "PASS noalloc-check"

clean:
	rm -f test_runner preview .noalloc.o

.PHONY: test golden noalloc-check clean
