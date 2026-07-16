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

SIM_SRC  := ../sim/duel_draw.c ../sim/duel_sim.c ../sim/duel_m13.c ../sim/duel_view.c ../sim/duel_proto.c ../sim/duel_host.c ../sim/duel_display.c ../sim/duel_resident.c ../sim/duel_courier.c ../sim/duel_event.c
SCENARIO_SRC := scenarios.c

test: test_runner
	# LeakSanitizer cannot run under Codex/CI ptrace; the sim has a separate,
	# stricter symbol-level no-allocation gate. ASan/UBSan remain fully active.
	ASAN_OPTIONS=detect_leaks=0 ./test_runner traces golden

golden: test_runner
	./test_runner --write-golden traces golden

HDRS := trace.h runner.h ../sim/duel_draw.h ../sim/duel_sim.h ../sim/duel_view.h ../sim/duel_proto.h ../sim/duel_host.h ../sim/duel_display.h ../sim/duel_resident.h ../sim/duel_courier.h ../sim/duel_event.h

test_runner: test_main.c trace.c $(SIM_SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_main.c trace.c $(SIM_SRC)

preview: preview.c trace.c $(SIM_SRC) $(SCENARIO_SRC) $(HDRS) scenarios.h
	$(CC) $(CFLAGS) -o $@ preview.c trace.c $(SIM_SRC) $(SCENARIO_SRC)

visual_runner: visual_test.c $(SIM_SRC) $(SCENARIO_SRC) $(HDRS) scenarios.h
	$(CC) $(CFLAGS) -o $@ visual_test.c $(SIM_SRC) $(SCENARIO_SRC)

visual-test: visual_runner
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner golden/visual.hashes

visual-golden: visual_runner
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner --write-golden golden/visual.hashes

gallery: preview
	ASAN_OPTIONS=detect_leaks=0 ./preview --gallery gallery

# --- M12 variant (ARCANE_M12): rooftop relocation now, civic layers later ---
# World-hash streams are flag-independent, so test-m12 running against the same
# golden/ proves M12 presentation never perturbs authoritative mechanics. Visual
# hashes DO change (rooftop raised), so they get their own golden/visual_m12.hashes.
M12FLAGS := $(CFLAGS) -DARCANE_M12
M13FLAGS := $(CFLAGS) -DARCANE_M12 -DARCANE_M13

test_runner_m12: test_main.c trace.c $(SIM_SRC) $(HDRS)
	$(CC) $(M12FLAGS) -o $@ test_main.c trace.c $(SIM_SRC)

visual_runner_m12: visual_test.c $(SIM_SRC) $(SCENARIO_SRC) $(HDRS) scenarios.h
	$(CC) $(M12FLAGS) -o $@ visual_test.c $(SIM_SRC) $(SCENARIO_SRC)

test-m12: test_runner_m12
	ASAN_OPTIONS=detect_leaks=0 ./test_runner_m12 traces golden

visual-test-m12: visual_runner_m12
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner_m12 golden/visual_m12.hashes

visual-golden-m12: visual_runner_m12
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner_m12 --write-golden golden/visual_m12.hashes

preview_m12: preview.c trace.c $(SIM_SRC) $(SCENARIO_SRC) $(HDRS) scenarios.h
	$(CC) $(M12FLAGS) -o $@ preview.c trace.c $(SIM_SRC) $(SCENARIO_SRC)

gallery-m12: preview_m12
	ASAN_OPTIONS=detect_leaks=0 ./preview_m12 --gallery gallery-m12

# M13 has a new authoritative world and v10 wire contract, so its acceptance
# suite is intentionally separate from the frozen M2-M12 world-hash harness.
test_runner_m13: m13_test.c $(SIM_SRC) $(HDRS)
	$(CC) $(M13FLAGS) -o $@ m13_test.c $(SIM_SRC)

test-m13: test_runner_m13
	ASAN_OPTIONS=detect_leaks=0 ./test_runner_m13

visual_runner_m13: m13_visual.c $(SIM_SRC) $(SCENARIO_SRC) $(HDRS) scenarios.h
	$(CC) $(M13FLAGS) -o $@ m13_visual.c $(SIM_SRC) $(SCENARIO_SRC)

visual-test-m13: visual_runner_m13
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner_m13 golden/visual_m13.hashes

visual-golden-m13: visual_runner_m13
	ASAN_OPTIONS=detect_leaks=0 ./visual_runner_m13 --write-golden golden/visual_m13.hashes

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

noalloc-check-m13:
	@for f in $(SIM_SRC); do \
		$(CC) -std=c11 -O1 -I../sim -DARCANE_M12 -DARCANE_M13 -c $$f -o .noalloc.o || exit 1; \
		if nm .noalloc.o | grep -qE ' U (malloc|calloc|realloc|free)$$'; then \
			echo "FAIL noalloc-check-m13: $$f references the allocator"; exit 1; \
		fi; \
	done; rm -f .noalloc.o; echo "PASS noalloc-check-m13"

clean:
	rm -f test_runner preview visual_runner test_runner_m12 visual_runner_m12 preview_m12 test_runner_m13 visual_runner_m13 .noalloc.o

.PHONY: test golden visual-test visual-golden gallery noalloc-check noalloc-check-m13 clean \
        test-m12 visual-test-m12 visual-golden-m12 gallery-m12 test-m13 \
        visual-test-m13 visual-golden-m13
