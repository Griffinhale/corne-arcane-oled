
VIA_ENABLE = yes
VIAL_ENABLE = yes
LTO_ENABLE = yes
OLED_ENABLE = yes
WPM_ENABLE = yes

# Hardware-agnostic duel engine sources (also compiled by sim_test/ on the host).
SRC += sim/duel_draw.c sim/duel_sim.c sim/duel_view.c sim/duel_proto.c sim/duel_host.c sim/duel_display.c

# Opt-in instrumentation. Release builds omit counters, timing reads, and the
# diagnostic overlay entirely: `qmk compile ... -e ARCANE_DIAGNOSTICS=yes`.
ifeq ($(strip $(ARCANE_DIAGNOSTICS)),yes)
    OPT_DEFS += -DARCANE_DIAGNOSTICS
endif
