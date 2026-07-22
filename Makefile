.PHONY: test mechanics-test mechanics-hp-candidates visual-test hp-gate noalloc-check release-build release-budget hygiene \
	format format-check lint

PYTHON_SOURCES := $(shell find host/arcane_host host/tests tools -type f -name '*.py' | sort)
C_SOURCES := $(shell find firmware -type f \( -name '*.c' -o -name '*.h' \) \
	! -name 'corne_arcane_layout.h' | sort)

test: mechanics-test visual-test noalloc-check
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
