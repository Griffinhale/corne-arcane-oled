/*
 * trace.h — scripted-input traces for deterministic replay tests.
 *
 * Text format, hand-authorable:
 *
 *   # comment
 *   version 1
 *   seed 42            (reserved; sim is currently RNG-free)
 *   start_tick 0       (optional; set near UINT32_MAX for wrap tests)
 *   @12  press   L 2 3
 *   @15  release L 2 3
 *   @200 end
 *
 * Event ticks must be nondecreasing. `end` sets how far the sim runs.
 */
#pragma once

#include <stdint.h>

// Kind values deliberately match SIM_EV_KEYDOWN / SIM_EV_KEYUP in duel_sim.h
// (asserted in test_main.c once the sim lands).
#define TRACE_EV_PRESS   1
#define TRACE_EV_RELEASE 2

#define TRACE_MAX_EVENTS 256

typedef struct {
    uint32_t tick;
    uint8_t  kind; // TRACE_EV_*
    uint8_t  side; // 0 = left, 1 = right
    uint8_t  row;  // 0..3 within the half
    uint8_t  col;  // 0..5
} trace_ev_t;

typedef struct {
    uint32_t   seed;
    uint32_t   start_tick;
    uint32_t   end_tick; // absolute: start_tick + relative @end value
    trace_ev_t ev[TRACE_MAX_EVENTS];
    int        n_ev;
} trace_t;

// Loads and validates a trace. Returns 0 on success; prints the offending
// line and returns -1 on error. Event ticks in the file are RELATIVE and get
// start_tick added, so wrap tests reuse unmodified trace files.
int trace_load(const char *path, trace_t *t);
