VIA_ENABLE = yes
VIAL_ENABLE = yes
RAW_ENABLE = yes
LTO_ENABLE = yes
OLED_ENABLE = yes

# RGB Matrix is a world-owned surface. Compiled RGB controls are absent and
# remapped RGB keycodes are ignored; other inherited lighting stays disabled.
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

SRC += sim/duel_framebuffer.c sim/duel_render.c \
       sim/duel_environment_draw.c sim/duel_combat_draw.c \
       sim/duel_overlay_draw.c sim/duel_draw.c \
       sim/duel_sim.c sim/duel_incantation.c sim/duel_combat.c \
       sim/duel_view.c sim/duel_proto.c sim/duel_host.c \
       sim/duel_display.c sim/duel_resident.c sim/duel_courier.c \
       sim/duel_event.c sim/duel_runtime.c sim/duel_rgb.c

# Pico SDK's bounded get_rand_32() seeds the one-byte split session. QMK's
# RP2040 make fragment does not link pico_rand by default, so add its source and
# include path explicitly. RAM power-up state and the bus counter supply seed /
# runtime entropy without the default ROSC sampling delay.
SRC += $(TOP_DIR)/lib/pico-sdk/src/rp2_common/pico_rand/rand.c
EXTRAINCDIRS += $(TOP_DIR)/lib/pico-sdk/src/rp2_common/pico_rand/include
EXTRAINCDIRS += $(TOP_DIR)/lib/pico-sdk/src/rp2_common/pico_unique_id/include
EXTRAINCDIRS += $(TOP_DIR)/lib/pico-sdk/src/common/pico_time/include
OPT_DEFS += -DPICO_RAND_ENTROPY_SRC_ROSC=0 \
            -DPICO_RAND_ENTROPY_SRC_TIME=0 \
            -DPICO_RAND_SEED_ENTROPY_SRC_BOARD_ID=0

# Instrumentation is compiled out of release images. Diagnostic firmware keeps
# the identical packet layouts and adds bounded counters/timing responses.
ifeq ($(strip $(ARCANE_DIAGNOSTICS)),yes)
    SRC += sim/duel_diagnostics.c
    OPT_DEFS += -DARCANE_DIAGNOSTICS \
                -DCH_DBG_FILL_THREADS=TRUE \
                -DCH_DBG_ENABLE_STACK_CHECK=TRUE
endif

# Accepted A/B diagnostic control for the split repair cadence.
ifeq ($(strip $(ARCANE_FIXED_SPLIT_CADENCE)),yes)
    OPT_DEFS += -DARCANE_FIXED_SPLIT_CADENCE
endif
