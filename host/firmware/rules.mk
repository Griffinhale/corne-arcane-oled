# M11.5 host branch: complete offline duel plus isolated custom Raw HID.
VIA_ENABLE = no
VIAL_ENABLE = no
RAW_ENABLE = yes
LTO_ENABLE = yes
OLED_ENABLE = yes
# The compiled layer-3 layout intentionally exposes RM_* controls. Keep that
# one accepted lighting engine, and make the unused inherited features absent.
WPM_ENABLE = no
RGBLIGHT_ENABLE = no
RGB_MATRIX_ENABLE = yes

# Preserve a release linker map beside the ELF for every acceptance build.
LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref

OPT_DEFS += -DARCANE_HOST_ENABLE

SRC += sim/duel_draw.c sim/duel_sim.c sim/duel_view.c sim/duel_proto.c sim/duel_host.c sim/duel_display.c

ifeq ($(strip $(ARCANE_DIAGNOSTICS)),yes)
    OPT_DEFS += -DARCANE_DIAGNOSTICS
endif

ifeq ($(strip $(ARCANE_FIXED_SPLIT_CADENCE)),yes)
    OPT_DEFS += -DARCANE_FIXED_SPLIT_CADENCE
endif
