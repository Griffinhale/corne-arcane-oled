/* Incantation descriptor grammar, collection, tempo, and compilation API. */
#pragma once

#include "duel_model.h"

#define SPELL_DESC_FORM(d)        ((uint8_t)((d) & 7u))
#define SPELL_DESC_ELEMENT(d)     ((uint8_t)(((d) >> 3) & 3u))
#define SPELL_DESC_PAYLOAD(d)     ((uint8_t)(((d) >> 5) & 3u))
#define SPELL_DESC_TRAJECTORY(d)  ((uint8_t)(((d) >> 7) & 7u))
#define SPELL_DESC_MAGNITUDE(d)   ((uint8_t)(1u + (((d) >> 10) & 3u)))
#define SPELL_DESC_STATUS(d)      ((uint8_t)(((d) >> 12) & 7u))
#define SPELL_DESC_INTERACTION(d) ((uint8_t)(((d) >> 15) & 3u))
#define SPELL_DESC_TEMPO(d)       ((uint8_t)(((d) >> 17) & 3u))
#define SPELL_DESC_TREND(d)       ((uint8_t)(((d) >> 19) & 3u))
#define SPELL_DESC_VARIANCE(d)    ((uint8_t)(((d) >> 21) & 3u))
#define SPELL_DESC_VALID(d)       (((d) & 0x00800000u) != 0)
#define SPELL_DESC_PACK(form, elem, payload, traj, mag, status, interaction, tempo, trend,         \
                        variance)                                                                  \
    ((uint32_t)(((form) & 7u) | (((elem) & 3u) << 3) | (((payload) & 3u) << 5) |                   \
                (((traj) & 7u) << 7) | ((((mag) - 1u) & 3u) << 10) | (((status) & 7u) << 12) |     \
                (((interaction) & 3u) << 15) | (((tempo) & 3u) << 17) | (((trend) & 3u) << 19) |   \
                (((variance) & 3u) << 21) | 0x00800000u))

enum { INC_IDLE = 0, INC_COLLECTING = 1, INC_WINDUP = 2, INC_PREPARED = 3, INC_REARM = 4 };
#define INCANTATION_IDLE_COMMIT_TICKS  13
#define INCANTATION_FORCE_COMMIT_TICKS 250
#define INCANTATION_WINDUP_MIN_TICKS   8
#define INCANTATION_WINDUP_MAX_TICKS   50

uint8_t incantation_complexity(const sim_incantation_t *inc);
uint32_t incantation_compile(const sim_incantation_t *inc, uint8_t variant, uint8_t temper);
void incantation_collection_reset(sim_incantation_t *incantation);
uint8_t incantation_collection_keydown(sim_incantation_t *incantation, uint8_t position,
                                       uint8_t layer);
void incantation_collection_keyup(sim_incantation_t *incantation, uint8_t position);
void incantation_collection_tick(sim_incantation_t *incantation, uint32_t held_positions);

#define INCANTATION_AMBIENCE_PACK(active, tempo, trend)                                            \
    ((uint8_t)(((active) ? 1u : 0u) | (((tempo) & 3u) << 1) | (((trend) & 3u) << 3)))
#define INCANTATION_AMBIENCE_ACTIVE(v) ((uint8_t)((v) & 1u))
#define INCANTATION_AMBIENCE_TEMPO(v)  ((uint8_t)(((v) >> 1) & 3u))
#define INCANTATION_AMBIENCE_TREND(v)  ((uint8_t)(((v) >> 3) & 3u))

uint8_t incantation_tempo_trend(const sim_incantation_t *inc);
uint8_t incantation_signature(uint32_t descriptor);
uint8_t incantation_local_ambience(const sim_wizard_t *wizard);
uint8_t incantation_aftermath_shared(const sim_world_t *world);
uint8_t incantation_aftermath_revision(const sim_world_t *world);
