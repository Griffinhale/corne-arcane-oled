# The one list of simulation and renderer sources for native builds.
#
# Both read it: the acceptance rig in sim_test and the desktop product in
# ../desktop. Set SIM_DIR to the path of firmware/sim before including. The
# firmware's own list stays in rules.mk, where QMK needs it and where
# duel_diagnostics.c is conditional; native builds always compile everything.

SIM_SRC := $(SIM_DIR)/duel_framebuffer.c $(SIM_DIR)/duel_render.c \
           $(SIM_DIR)/duel_environment_draw.c $(SIM_DIR)/duel_combat_draw.c \
           $(SIM_DIR)/duel_overlay_draw.c $(SIM_DIR)/duel_draw.c \
           $(SIM_DIR)/duel_sim.c $(SIM_DIR)/duel_incantation.c $(SIM_DIR)/duel_combat.c \
           $(SIM_DIR)/duel_view.c $(SIM_DIR)/duel_proto.c $(SIM_DIR)/duel_host.c \
           $(SIM_DIR)/duel_diagnostics.c \
           $(SIM_DIR)/duel_display.c $(SIM_DIR)/duel_resident.c \
           $(SIM_DIR)/duel_courier.c $(SIM_DIR)/duel_event.c $(SIM_DIR)/duel_runtime.c \
           $(SIM_DIR)/duel_rgb.c
