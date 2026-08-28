/*
 * string.h -- the freestanding stand-in, for the wasm32 build only.
 *
 * -nostdlib removes the library, not the headers, and there is no wasm sysroot
 * here to find a real <string.h> in. Supplying one is honest rather than a
 * workaround: the core's entire use of <string.h> is memcpy and memset, both
 * verifiable with a grep over firmware/sim and desktop, and both defined in
 * web/duel_wasm.c. If a third ever appears, this build stops compiling, which
 * is the report worth having.
 *
 * Reached only through -I../web/freestanding on the wasm build. The native and
 * firmware builds see their own platform's header, untouched.
 */
#pragma once

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int value, size_t n);
