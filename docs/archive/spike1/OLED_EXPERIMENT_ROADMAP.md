# OLED Experiment Roadmap

## Current OLED status

- OLED hardware exists and worked originally.
- Stock Vial keymap disabled OLED.
- Custom/default-derived keymaps can enable OLED through `rules.mk`.
- The crab animation from `qmk-animations` worked in `griffin_anim`.

## Working branch model

Keep three firmware branches/keymaps:

```text
griffin
  Stable Vial keymap. Do not experiment here.

griffin_anim
  Firmware-only OLED animation experiments. Keep Vial enabled.

griffin_hostoled
  Host-driven OLED experiments. Disable Vial/VIA here because custom Raw HID conflicts with Vial/VIA Raw HID.
```

## Firmware-only animations

Best first source:

```text
marekpiechut/qmk-animations
```

Why:

- Tested on Corne.
- Targets common 128x32 OLEDs.
- Uses simple QMK C includes.
- Crab worked already.

Files copied for crab:

```text
~/src/qmk-animations/animations/animation-utils.c
~/src/qmk-animations/animations/crab.c
```

Into:

```text
~/src/vial-qmk/keyboards/crkbd/keymaps/griffin_anim/
```

Relevant rules:

```make
OLED_ENABLE = yes
WPM_ENABLE = yes
LTO_ENABLE = yes
```

The low-risk pattern is to override the weak `oled_render_logo()` function. On Corne, the keyboard-level OLED task uses the master OLED for layer/keylog and calls `oled_render_logo()` on the offhand OLED. Overriding only the logo keeps the master status screen intact.

Working style:

```c
#ifdef OLED_ENABLE

#define ANIM_INVERT false
#define ANIM_RENDER_WPM true
#define FAST_TYPE_WPM 45

#include "crab.c"

void oled_render_logo(void) {
    oled_render_anim();
}

#endif
```

Try next:

```text
demon.c       darker animated creature
music-bars.c  equalizer-style animation
```

Only include one animation `.c` at a time. These animation files export the same render function, so including multiple creates duplicate definitions.

## Atude / Satisfaction75 OLED mods

Use as reference only.

Reasons not to build/use directly:

- Satisfaction75-specific.
- Old repo moved to Atyu and no longer receives updates.
- Atyu app is Satisfaction75-oriented, beta, Mac/Windows-oriented, and not set up for Debian + Corne + RP2040 split UF2 workflow.
- Its flash command model does not include `CONVERT_TO=rp2040_ce` or `uf2-split-left/right`.

Useful reference ideas:

```text
Bongo Cat
Luna
Kirby
Pusheen
Keyboard matrix
Persistent OLED settings
Timeout/settings menu ideas
```

Clone only for reading:

```bash
cd ~/src
git clone --depth 1 https://github.com/atude/sat75-oled-mods.git sat75-oled-mods
rg -n "bongo|luna|kirby|pusheen|oled|render|gif|matrix" ~/src/sat75-oled-mods
```

Manual porting will be required.

## Host-driven OLED direction

Goal:

```text
Debian host daemon sends OLED frame data to keyboard while firmware provides fallback display when daemon is absent.
```

Key design:

```text
host alive recently -> render host frame
host silent/dead    -> fallback firmware status/logo/animation
```

Reason to keep separate from Vial:

- Vial/VIA already use Raw HID.
- Custom `raw_hid_receive()` in the same keymap can collide with Vial/VIA's Raw HID handling.
- Start with `griffin_hostoled` where `VIA_ENABLE = no` and `VIAL_ENABLE = no`.

Host-driven branch rules should include:

```make
OLED_ENABLE = yes
RAW_ENABLE = yes
WPM_ENABLE = yes
VIA_ENABLE = no
VIAL_ENABLE = no
LTO_ENABLE = yes
```

Possible host daemon payloads:

```text
Now playing / album art
Clock / date
Focused app / workspace
Git repo / branch / dirty state
CPU/RAM/load
Network state
Pomodoro timer
Do-not-disturb status
Idle animation
```

Start with master OLED only. Sending independent host data to the slave OLED requires QMK split transactions/custom sync and is a second-stage problem.

## Burn-in settings

Useful `config.h` options:

```c
#define OLED_BRIGHTNESS 128
#define OLED_TIMEOUT 30000
#define OLED_FADE_OUT
#define OLED_FADE_OUT_INTERVAL 4
```

