.PHONY: test visual-test gallery budget m13-budget

test:
	$(MAKE) -C firmware/sim_test test test-m12 test-m13 noalloc-check noalloc-check-m13
	cd host && ./run_tests.sh

visual-test:
	$(MAKE) -C firmware/sim_test visual-test visual-test-m12 visual-test-m13

gallery:
	$(MAKE) -C firmware/sim_test preview
	mkdir -p "$(or $(GALLERY_DIR),artifacts/m11-gallery)"
	ASAN_OPTIONS=detect_leaks=0 firmware/sim_test/preview --gallery "$(or $(GALLERY_DIR),artifacts/m11-gallery)"

budget:
	$(MAKE) -C firmware/sim_test test_runner visual_runner
	./scripts/m11_budget.sh

m13-budget:
	sh ./scripts/m13_budget.sh
