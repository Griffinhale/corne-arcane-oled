.PHONY: test mechanics-test visual-test noalloc-check city-lib \
	web-lib web-parity web-clean swift-parity release-build release-budget hygiene \
	format format-check lint lint-js lint-swift

# Same rule as C_SOURCES below: a glob, so a new directory of Python has to be
# named here or it quietly stops being linted.
PYTHON_SOURCES := $(shell find host/arcane_host host/tests tools web/tools -type f -name '*.py' | sort)
# Every native C source in the tree, for lint. The list is a glob rather than
# an enumeration, so a new top-level directory of C has to be named here or it
# silently stops being covered -- no error, just less coverage. It has happened
# once already, when desktop/ moved out from under firmware/.
C_SOURCES := $(shell find firmware desktop web -type f \( -name '*.c' -o -name '*.h' \) \
	! -name 'corne_arcane_layout.h' | sort)
# The browser pages and tools, the GNOME, KWin and Firefox adapters. Same glob
# rule: JavaScript anywhere else has to be named here.
JS_SOURCES := $(shell find web host -type f \( -name '*.js' -o -name '*.mjs' \) \
	! -path 'web/tools/.parity/*' | sort)
SWIFT_SOURCES := Package.swift $(shell find apple -type f -name '*.swift' | sort)
# The swift-format release lint-swift is measured against; CI builds this tag.
SWIFT_FORMAT_VERSION := 510.1.0

test: mechanics-test visual-test noalloc-check city-lib
	cd host && ./run_tests.sh

mechanics-test:
	$(MAKE) -C firmware/sim_test mechanics-test

visual-test:
	$(MAKE) -C firmware/sim_test visual-test

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
# package the iOS app, the widget and the watch app are built on. Like
# web-parity, kept out of `test` so that a Swift toolchain never stands between
# a contributor and the firmware's own gates. On Linux, run it inside
# `nix-shell apple/shell.nix`; on macOS the system Swift will do.
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

lint: format-check lint-js

# A syntax check, not a style check. Files go in on stdin because
# `node --check FILE` passes any file it takes for an ES module -- an .mjs,
# or a .js with an import -- without parsing it. A file with a top-level import or
# export, or any .mjs, is checked as a module; the rest as scripts.
lint-js:
	@command -v node >/dev/null || { echo "FAIL lint-js: node not found; install Node.js 22 or later" >&2; exit 1; }
	@node -e 'process.exit(Number(process.versions.node.split(".")[0]) < 22 ? 1 : 0)' || \
		{ echo "FAIL lint-js: Node.js 22 or later is required, found $$(node --version)" >&2; exit 1; }
	@for f in $(JS_SOURCES); do \
		case $$f in *.mjs) type=module ;; *) type=commonjs ;; esac; \
		if grep -qE '^(import|export)[ {*]' "$$f"; then type=module; fi; \
		node --input-type=$$type --check < "$$f" || { echo "FAIL lint-js: $$f" >&2; exit 1; }; \
	done
	@echo "PASS lint-js: $(words $(JS_SOURCES)) files"

# Needs a Swift toolchain, so it is not part of lint; the Swift CI job runs it.
lint-swift:
	@swift-format --version | grep -qx '$(SWIFT_FORMAT_VERSION)' || \
		{ echo "FAIL lint-swift: swift-format $(SWIFT_FORMAT_VERSION) is required" >&2; exit 1; }
	swift-format lint --strict --configuration .swift-format $(SWIFT_SOURCES)
