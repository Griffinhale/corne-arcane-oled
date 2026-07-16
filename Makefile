.PHONY: test mechanics-test visual-test noalloc-check release-build release-budget hygiene

test: mechanics-test visual-test noalloc-check
	cd host && ./run_tests.sh

mechanics-test:
	$(MAKE) -C firmware/sim_test mechanics-test

visual-test:
	$(MAKE) -C firmware/sim_test visual-test

noalloc-check:
	$(MAKE) -C firmware/sim_test noalloc-check

release-build:
	sh ./scripts/release_build.sh

release-budget:
	sh ./scripts/release_budget.sh

hygiene:
	sh ./scripts/hygiene.sh
