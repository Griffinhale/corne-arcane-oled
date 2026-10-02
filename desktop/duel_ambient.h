/*
 * duel_ambient.h — internal seam between the autonomous world and the city
 * renderer. The public handle and its lifecycle live in duel_city.h, which
 * stays free of simulation types so the binding can read it as a plain ABI.
 */
#pragma once

#include "duel_city.h"
#include "duel_render.h"

/* The world behind the handle, for the civic derivations that read one. */
const sim_world_t *duel_ambient_world(const duel_ambient_t *ambient);

/* The session's diplomacy balance, -3 to 3, kept beside the world the way the
 * master keeps its own: a champion who falls tips it toward the other city,
 * and the shared civic derivation weights rare events by it. */
int8_t duel_ambient_diplomacy_balance(const duel_ambient_t *ambient);

/* Fill the projection from the world and arm the one-shot outcome flashes,
 * exactly as the master's display pass does. */
void duel_ambient_project(duel_ambient_t *ambient, duel_render_t *render, uint32_t now_ms);
