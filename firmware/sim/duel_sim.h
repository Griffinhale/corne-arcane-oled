/* Deterministic simulation lifecycle and bounded event API. */
#pragma once

#include "duel_incantation.h"

bool sim_evq_push(sim_evq_t *q, sim_event_t event);
/* The caller consumes ev/n/dropped in place before resetting metadata. */
void sim_evq_reset(sim_evq_t *q);

void sim_init(sim_world_t *world, uint8_t flags, uint32_t start_tick);
void sim_tick(sim_world_t *world, sim_inputs_t inputs, const sim_event_t *events,
              uint8_t event_count, uint8_t dropped);
