.PHONY: test visual-test gallery budget

test:
	$(MAKE) -C firmware/sim_test test noalloc-check
	cd host && ./run_tests.sh

visual-test:
	$(MAKE) -C firmware/sim_test visual-test

gallery:
	$(MAKE) -C firmware/sim_test preview
	mkdir -p "$(or $(GALLERY_DIR),artifacts/m11-gallery)"
	ASAN_OPTIONS=detect_leaks=0 firmware/sim_test/preview --gallery "$(or $(GALLERY_DIR),artifacts/m11-gallery)"

budget:
	$(MAKE) -C firmware/sim_test test_runner visual_runner
	./scripts/m11_budget.sh
