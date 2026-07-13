# M10 host branch: complete offline duel plus isolated custom Raw HID.
VIA_ENABLE = no
VIAL_ENABLE = no
RAW_ENABLE = yes
LTO_ENABLE = yes
OLED_ENABLE = yes
WPM_ENABLE = yes

OPT_DEFS += -DARCANE_HOST_ENABLE

SRC += sim/duel_draw.c sim/duel_sim.c sim/duel_proto.c sim/duel_host.c
