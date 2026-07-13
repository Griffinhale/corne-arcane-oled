# M9 application-aware host

The Linux daemon sends semantic 32-byte Raw HID reports to `griffin_hostoled`;
it never sends OLED framebuffers. Its default mode owns the private session
D-Bus service `io.github.Griffinhale.CorneArcane` and receives only KWin's
`resourceClass` and `desktopFileName`. The bridge never reads window titles,
URLs, tabs, document names, or page data, and the daemon retains no application
identifiers after classification.

Firefox/Firefox ESR, Chrome, Chromium, Brave, Vivaldi, and Zen aliases settle
to Archive after 200 ms. Empty, unknown, desktop, and other applications settle
to Duel. A rapid Alt-Tab cancels the pending transition. Heartbeats remain at
500 ms; missing keyboards are retried every two seconds, and every reconnect
gets a fresh daemon session plus `HELLO`.

Run tests or an explicit diagnostic override from this directory:

```bash
./run_tests.sh
python3 -m arcane_host.daemon --scene archive --notify 2 --verbose
python3 -m arcane_host.daemon --dry-run --once --session 0x11223344 --scene archive
```

Automatic focus mode requires PyGObject/Gio and a Plasma 6 session. The NixOS
module in `../corne.nix` packages those dependencies, the daemon, and the KWin
bridge, then starts them through `graphical-session.target`.

Do not run this daemon against `griffin_anim`: that Vial-capable keymap reserves
the same Raw HID interface. The daemon discovers QMK usage page `0xFF60`, usage
`0x61`; use `--device /dev/hidrawN` only to disambiguate multiple keyboards.

The wire remains Raw HID v1. Firmware accepts a new daemon session only through
`HELLO` sequence zero, rejects stale/duplicate reports, and treats every report
as an absolute scene/notification summary. Only HELLO and HEARTBEAT refresh the
1.5-second liveness deadline.

**M9 hardware result (2026-07-13): accepted.** Real KWin focus changes on Debian
13 Plasma/Wayland switch the daemon and both physical OLEDs correctly between
Archive and Duel. Visual refinement is deferred to M11 polish; M10 notification
policy and adapters is next.
