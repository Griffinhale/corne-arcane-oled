
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

# A/B control for the M11.5 cadence gate. The candidate defaults to a 250 ms
# static repair heartbeat; this switch restores the fixed 80 ms cadence while
# retaining the identical v8 absolute packet and acceptance rules.
ifeq ($(strip $(ARCANE_FIXED_SPLIT_CADENCE)),yes)
    OPT_DEFS += -DARCANE_FIXED_SPLIT_CADENCE
endif

# M12 Twin Cities. Opt-in so the accepted M11.5 release stays bit-identical:
# `qmk compile ... -e ARCANE_M12=yes`. Every M12 addition is compiled out when
# this is absent, and DUEL_ROOF_DY constant-folds to 0.
ifeq ($(strip $(ARCANE_M12)),yes)
    OPT_DEFS += -DARCANE_M12
endif
