#include "duel_render.h"
#include "duel_host.h"
#include "duel_proto.h"

void duel_render_from_world(duel_render_t *render, const sim_world_t *world) {
    duel_view_from_world(world, &render->view);
    render->scry_motion =
        scry_is_open(world) ? DUEL_SCRY_MOTION_PACK(DUEL_SCRY_EXTENT_FULL, false) : 0u;
    render->scry_scroll = 0u;
    render->shared_pres = incantation_aftermath_shared(world);
    render->revision = incantation_aftermath_revision(world);
    duel_residue_pack(world, render->residue);
    for (uint8_t slot = 0; slot < SIM_FIELD_SLOTS; slot++)
        render->field[slot] = duel_field_projection(&world->field[slot]);
}

uint8_t duel_render_host_online(const duel_render_t *render) {
    return DUEL_HOST_CONTEXT_ONLINE(render->external);
}

uint8_t duel_render_scene(const duel_render_t *render) {
    return DUEL_HOST_CONTEXT_SCENE(render->external);
}

uint8_t duel_render_notification_count(const duel_render_t *render) {
    return DUEL_HOST_CONTEXT_NOTIF(render->external);
}

uint8_t duel_render_district(const duel_render_t *render) {
    return duel_civic_district(render->civic, render->external);
}
