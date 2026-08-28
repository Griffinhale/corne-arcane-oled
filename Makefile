.PHONY: test mechanics-test mechanics-hp-candidates visual-test hp-gate noalloc-check city-lib \
	web-lib web-parity web-clean swift-parity release-build release-budget hygiene \
	format format-check lint

# Same rule as C_SOURCES below: a glob, so a new directory of Python has to be
# named here or it quietly stops being linted.
PYTHON_SOURCES := $(shell find host/arcane_host host/tests tools web/tools -type f -name '*.py' | sort)
# Every native C source in the tree, for lint. The list is a glob rather than
# an enumeration, so a new top-level directory of C has to be named here or it
# silently stops being covered -- no error, just less coverage. It has happened
# once already, when desktop/ moved out from under firmware/.
C_SOURCES := $(shell find firmware desktop web -type f \( -name '*.c' -o -name '*.h' \) \
	! -name 'corne_arcane_layout.h' | sort)

test: mechanics-test visual-test noalloc-check city-lib
	cd host && ./run_tests.sh

mechanics-test:
	$(MAKE) -C firmware/sim_test mechanics-test

mechanics-hp-candidates:
	$(MAKE) -C firmware/sim_test mechanics-hp-candidates

visual-test:
	$(MAKE) -C firmware/sim_test visual-test

hp-gate:
	$(MAKE) -C firmware/sim_test hp-gate

noalloc-check:
	$(MAKE) -C firmware/sim_test noalloc-check

# The desktop product's native library. Built as part of `test` so the host
# tests that exercise the renderer actually run.
city-lib:
	$(MAKE) -C desktop

# The browser shell: the same core compiled to wasm32 by standalone clang.
# Deliberately not part of `test`, because that would put a wasm toolchain
# between a contributor and the firmware's own gates. `web-parity` is the gate
# that matters here and it builds what it needs.
web-lib:
	$(MAKE) -C web

# The acceptance test for the browser build: the same seeds, frames and layouts
# rendered by the native library and by the WASM module, compared byte for
# byte. Determinism is the product, so this is the check that says the port is
# real rather than approximately right.
web-parity: city-lib web-lib
	sh ./web/tools/parity.sh

# The third leg of the same gate: the matrix rendered again by the Swift
# package the iOS app and the widget are built on. Like web-parity, kept out of
#  so that a Swift toolchain never stands between a contributor and the
# firmware's own gates. On Linux, swift 5.10.1 (swift-5.10.1-RELEASE) — swift build, swift run city-check is a toolchain
# that can run it; on macOS the system Swift will do.
swift-parity: city-lib
	sh ./apple/tools/parity.sh

web-clean:
	$(MAKE) -C web clean

release-build:
	sh ./scripts/release_build.sh

release-budget:
	sh ./scripts/release_budget.sh

hygiene:
	sh ./scripts/hygiene.sh

format:
	ruff check --fix $(PYTHON_SOURCES)
	ruff format $(PYTHON_SOURCES)
	clang-format -i $(C_SOURCES)

format-check:
	ruff check $(PYTHON_SOURCES)
	ruff format --check $(PYTHON_SOURCES)
	clang-format --dry-run --Werror $(C_SOURCES)

lint: format-check
