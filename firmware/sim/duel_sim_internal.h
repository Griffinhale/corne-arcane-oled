/* Private phase contracts shared only by simulation orchestration modules. */
#pragma once

#include "duel_sim.h"

uint8_t duel_incantation_affinity_element(uint8_t variant);

void duel_combat_ingest_events(sim_world_t *world, sim_inputs_t inputs, const sim_event_t *events,
                               uint8_t event_count, uint32_t event_down[2]);
void duel_combat_collect_side(sim_world_t *world, sim_inputs_t inputs, uint8_t side,
                              uint32_t event_down);
void duel_combat_pose_step(sim_wizard_t *wizard, bool down, bool was_down);
void duel_combat_lifecycle_step(sim_wizard_t *wizard);
void duel_combat_stance_step(sim_world_t *world, uint8_t side);
void duel_combat_regeneration_step(sim_world_t *world);
void duel_combat_collision_step(sim_world_t *world);
void duel_combat_residue_step(sim_world_t *world);
void duel_combat_spell_step(sim_world_t *world, uint8_t side);
void duel_combat_status_release_step(sim_world_t *world);
void duel_combat_aftermath_step(sim_world_t *world);
void duel_combat_scry_step(sim_scry_t *scry, uint8_t mask);

/* sim_tick owns this exact order:
 * shared overflow -> authoritative input collection -> shared pose/held state ->
 * lifecycle -> stance -> regeneration -> collision -> residue -> left/right
 * spell motion -> status/windup/release -> aftermath -> scry -> shared edge/tick.
 * Changing the order changes deterministic mechanics and wire-visible state. */
