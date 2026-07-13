# M8 host heartbeat spike

Dependency-free Linux daemon for `griffin_hostoled`. It sends semantic 32-byte
Raw HID reports, never OLED framebuffers. Do not run it against `griffin_anim`;
that keymap reserves the same Raw HID interface for Vial.

From this directory:

```bash
./run_tests.sh
python3 -m arcane_host.daemon --scene archive --notify 2 --verbose
```

Stop it with Ctrl-C. The firmware must clear the host bar, notification dots,
and external scene class within 1.5 seconds while the ordinary duel continues.

Useful spike controls:

```bash
# Show deterministic reports without touching a keyboard:
python3 -m arcane_host.daemon --dry-run --once --session 0x11223344 --scene archive --notify 2

# Disambiguate if more than one QMK Raw HID keyboard is connected:
python3 -m arcane_host.daemon --device /dev/hidraw7 --scene focus --verbose
```

The daemon discovers the unnumbered QMK Raw HID interface by usage page
`0xFF60`, usage `0x61`, then writes a report-ID-zero prefix plus the 32-byte
payload as required by Linux hidraw. The firmware accepts a new daemon session
only through `HELLO` sequence zero, rejects stale/duplicate reports, remembers
the prior session ID, and treats every accepted report as an absolute
scene/notification summary. Only HELLO and HEARTBEAT refresh liveness.
