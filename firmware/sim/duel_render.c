#include "duel_render.h"
#include "duel_host.h"

void duel_render_from_world(duel_render_t *render, const sim_world_t *world) {
    duel_view_from_world(world, &render->view);
    render->shared_pres = incantation_aftermath_shared(world);
    render->revision = incantation_aftermath_revision(world);
    duel_residue_pack(world, render->residue);
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
