#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "duel_host.h"
#include "duel_proto.h"
#include "duel_sim.h"

void test_encode_snapshot(const sim_world_t *world, uint8_t session, uint16_t sequence,
                          duel_snapshot_t *out);

void test_build_host_packet(uint8_t type, uint32_t session, uint16_t sequence,
                            uint8_t scene, uint8_t notification_count,
                            uint8_t category, uint8_t priority, uint8_t age,
                            bool persistent, uint8_t civic, uint8_t secondary,
                            duel_host_packet_t *out);
