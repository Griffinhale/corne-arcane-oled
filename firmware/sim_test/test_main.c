#include <stdio.h>

#include "test_harness.h"

void run_runtime_display_rgb_tests(void);
void run_protocol_view_tests(void);
void run_incantation_compiler_tests(void);
void run_combat_lifecycle_tests(void);
void run_civic_presentation_tests(void);
void run_rendering_geometry_tests(void);

int main(void) {
    run_runtime_display_rgb_tests();
    run_protocol_view_tests();
    run_incantation_compiler_tests();
    run_combat_lifecycle_tests();
    run_civic_presentation_tests();
    run_rendering_geometry_tests();
    if (test_failures) {
        printf("%d mechanics test(s) failed\n", test_failures);
        return 1;
    }
    printf("all mechanics tests passed\n");
    return 0;
}
