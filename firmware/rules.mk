VIA_ENABLE = yes
VIAL_ENABLE = yes
RAW_ENABLE = yes
LTO_ENABLE = yes
OLED_ENABLE = yes

# The compiled fourth layer uses RGB Matrix controls. Other inherited lighting
# and optional Vial surfaces are intentionally absent from the 0.4 image.
RGB_MATRIX_ENABLE = yes
RGBLIGHT_ENABLE = no
WPM_ENABLE = no
QMK_SETTINGS = no
DYNAMIC_MACRO_ENABLE = no
TAP_DANCE_ENABLE = no
COMBO_ENABLE = no
KEY_OVERRIDE_ENABLE = no
CAPS_WORD_ENABLE = no
LAYER_LOCK_ENABLE = no
REPEAT_KEY_ENABLE = no
ENCODER_MAP_ENABLE = no

LDFLAGS += -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--cref

SRC += sim/duel_draw.c sim/duel_sim.c sim/duel_incantation.c \
       sim/duel_view.c sim/duel_proto.c sim/duel_host.c \
       sim/duel_display.c sim/duel_resident.c sim/duel_courier.c \
       sim/duel_event.c

# Instrumentation is compiled out of release images. Diagnostic firmware keeps
# the identical packet layouts and adds bounded counters/timing responses.
ifeq ($(strip $(ARCANE_DIAGNOSTICS)),yes)
    OPT_DEFS += -DARCANE_DIAGNOSTICS \
                -DCH_DBG_FILL_THREADS=TRUE \
                -DCH_DBG_ENABLE_STACK_CHECK=TRUE
endif

# Accepted A/B diagnostic control for the split repair cadence.
ifeq ($(strip $(ARCANE_FIXED_SPLIT_CADENCE)),yes)
    OPT_DEFS += -DARCANE_FIXED_SPLIT_CADENCE
endif
