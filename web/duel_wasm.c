/*
 * duel_wasm.c -- the browser shell's half of the boundary.
 *
 * The core compiles to wasm32 as it stands: every include under firmware/sim
 * and desktop is stdint/stdbool/stddef/string, there are no floats, no
 * allocator, and no OS calls. So this file is not a port. It is the two libc
 * symbols a freestanding build has to supply, a set of static buffers for the
 * caller-owned state duel_city.h insists on, and the pointers JavaScript needs
 * to find them in linear memory.
 *
 * No allocator, deliberately. Every buffer below is a file-scope static of a
 * size the header fixes, so the module's memory footprint is a compile-time
 * constant and there is nothing to free, grow, or leak. The web shell owns
 * exactly the same storage a C caller would, it just reaches it by offset.
 *
 * The one-way street stays one-way: pixels come out, bounded integers go in.
 * There is no export here that accepts text, and no import at all -- the
 * module asks the host for nothing, not even a clock.
 */

#include <stddef.h>
#include <stdint.h>

#include "duel_city.h"

/*
 * The export list, written on the functions themselves.
 *
 * Spelled as a prefix with the name repeated rather than wrapped around the
 * name, because the wrapped form is not something clang-format can parse as a
 * declaration and the repository lints every C file it has. The repetition is
 * checked by the parity harness and both pages, which reach the module only
 * through these names -- a mismatch fails on the first call, loudly.
 */
#define WASM_EXPORT(name) __attribute__((export_name(#name)))

/*
 * The freestanding contract, in full. -nostdlib means these two are the only
 * things the core needs that the toolchain will not provide, and clang lowers
 * some struct copies to calls of them regardless of the source. Simple byte
 * loops: the buffers here are at most 94 KiB and this runs once per frame.
 */
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

void *memset(void *dst, int value, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++)
        d[i] = (uint8_t)value;
    return dst;
}

/*
 * Caller-owned state, owned here. The sizes are the header's, not guesses:
 * the static asserts fail the build if any of them moves.
 */
static duel_city_state_t city_state;
static duel_ambient_t ambient_world;
static duel_city_input_t city_input;

/*
 * One frame at scale 1, sized for the largest layout. LANDSCAPE is 400x240,
 * which is 96,000 bytes of 8-bit grey; every other layout sits inside it.
 * Scale expansion is the shell's job here -- the browser upscales with CSS,
 * so asking the library for scale 2 would cost 4x the bytes across the
 * boundary to produce pixels the compositor can produce for nothing.
 */
#define WASM_PIXELS_MAX (400 * 240)
static uint8_t pixel_buffer[WASM_PIXELS_MAX];

_Static_assert(sizeof(city_input) == 10, "input struct is the wire payload, unpacked");
_Static_assert(sizeof(city_state) == 32, "city state size changed under the shim");
_Static_assert(sizeof(ambient_world) == 512, "ambient world size changed under the shim");

/* Where the buffers live, so JS can write and read them in place. */
WASM_EXPORT(duel_wasm_input_ptr) uint32_t duel_wasm_input_ptr(void) {
    return (uint32_t)(uintptr_t)&city_input;
}

WASM_EXPORT(duel_wasm_pixels_ptr) uint32_t duel_wasm_pixels_ptr(void) {
    return (uint32_t)(uintptr_t)pixel_buffer;
}

WASM_EXPORT(duel_wasm_pixels_capacity) uint32_t duel_wasm_pixels_capacity(void) {
    return (uint32_t)WASM_PIXELS_MAX;
}

/*
 * Start, or restart, from a seed. One call sets up all three pieces of state
 * so a shell cannot half-reset the world -- the failure mode where a new seed
 * draws against the previous world's floor policy.
 *
 * The input starts at tour stop 0 rather than zeroed. duel_city_render puts
 * the struct through the firmware's own acceptance path and rejects anything
 * out of range, and a zeroed struct is not obviously in range; the tour is
 * guaranteed valid by construction.
 */
WASM_EXPORT(duel_wasm_init) int duel_wasm_init(uint32_t seed) {
    uint8_t byte = (uint8_t)(seed & 0xFFu);
    duel_city_state_init(&city_state);
    duel_ambient_init(&ambient_world, byte);
    return duel_city_tour_stop(0, byte, &city_input);
}

/* Run the world up to now_ms at the renderer's own cadence. */
WASM_EXPORT(duel_wasm_advance) uint32_t duel_wasm_advance(uint32_t now_ms) {
    return duel_ambient_advance(&ambient_world, now_ms);
}

/*
 * Render one frame into the static buffer at scale 1. `ambient` selects the
 * self-playing world; passing 0 renders the resting city, which is what the
 * parity harness needs to isolate the renderer from the simulation.
 */
WASM_EXPORT(duel_wasm_render)
int duel_wasm_render(uint32_t elapsed_ms, uint32_t frame, int layout, int ambient) {
    return duel_city_render(&city_state, &city_input, ambient ? &ambient_world : NULL, elapsed_ms,
                            frame, layout, 1, pixel_buffer, sizeof(pixel_buffer));
}

/* Geometry at scale 1, packed as (width << 16) | height, or a negative
 * DUEL_CITY_ERR_*. One return value keeps the JS side from having to pass
 * out-pointers for two integers it always wants together. */
WASM_EXPORT(duel_wasm_geometry) int duel_wasm_geometry(int layout) {
    int width = 0;
    int height = 0;
    int code = duel_city_geometry(layout, 1, &width, &height);
    if (code != DUEL_CITY_OK)
        return code;
    return (width << 16) | height;
}

WASM_EXPORT(duel_wasm_abi_version) int duel_wasm_abi_version(void) {
    return duel_city_abi_version();
}

WASM_EXPORT(duel_wasm_frame_interval_ms) uint32_t duel_wasm_frame_interval_ms(void) {
    return duel_city_frame_interval_ms();
}

WASM_EXPORT(duel_wasm_seek_warm_frames) int duel_wasm_seek_warm_frames(void) {
    return duel_city_seek_warm_frames();
}

WASM_EXPORT(duel_wasm_backdrop) int duel_wasm_backdrop(int layout) {
    return duel_city_backdrop(layout);
}

/*
 * Presentation policy, asked of the renderer rather than decided again here.
 *
 * ABI 5 moved the scale rules into C precisely so that a second shell would
 * not reimplement them and drift. The browser is that second shell, so it asks
 * the same questions the Tk window asks: how big should this be with nobody
 * naming a size, and what is the largest whole-pixel scale that fits. The
 * answers differ from the desktop's only in that the browser applies them in
 * CSS instead of to a bitmap.
 */
WASM_EXPORT(duel_wasm_default_scale) int duel_wasm_default_scale(int layout) {
    return duel_city_default_scale(layout);
}

WASM_EXPORT(duel_wasm_fit_scale) int duel_wasm_fit_scale(int layout, int width, int height) {
    return duel_city_fit_scale(layout, width, height);
}

WASM_EXPORT(duel_wasm_tour_length) int duel_wasm_tour_length(void) {
    return duel_city_tour_length();
}

/* Move the input struct to a tour stop. The shell's only way to change what
 * the city is showing, and it cannot express an invalid state. */
WASM_EXPORT(duel_wasm_tour_stop) int duel_wasm_tour_stop(int index, uint32_t seed) {
    return duel_city_tour_stop(index, (uint8_t)(seed & 0xFFu), &city_input);
}

/*
 * Evidence the world is running, for the page to show and the harness to
 * check. Written into a static rather than returned by value, because a
 * struct return across the wasm boundary becomes a hidden out-pointer the
 * shell would otherwise have to find somewhere to put.
 */
static duel_ambient_stats_t ambient_stats;

WASM_EXPORT(duel_wasm_stats_ptr) uint32_t duel_wasm_stats_ptr(void) {
    return (uint32_t)(uintptr_t)&ambient_stats;
}

WASM_EXPORT(duel_wasm_stats) void duel_wasm_stats(void) {
    ambient_stats = duel_ambient_stats(&ambient_world);
}
