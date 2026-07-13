
VIA_ENABLE = yes
VIAL_ENABLE = yes
LTO_ENABLE = yes
OLED_ENABLE = yes
WPM_ENABLE = yes

# Hardware-agnostic duel engine sources (also compiled by sim_test/ on the host).
SRC += sim/duel_draw.c sim/duel_sim.c sim/duel_proto.c
