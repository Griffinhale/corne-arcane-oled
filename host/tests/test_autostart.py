"""The XDG autostart entry: where make install puts it, and what its Exec starts.

XFCE, i3 and Cinnamon never reach graphical-session.target, so the entry starts
the enabled units itself. A fake systemctl stands in for the user manager.
"""

from __future__ import annotations

import configparser
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HOST = Path(__file__).resolve().parents[1]
ENTRY = HOST / "desktop" / "corne-arcane-autostart.desktop"
UNITS = ("corne-arcane-host", "corne-arcane-focus-x11", "corne-arcane-tray")

# Answers is-enabled from $ENABLED and logs every call; never the real manager.
FAKE_SYSTEMCTL = """#!/bin/sh
echo "$*" >> "$SYSTEMCTL_LOG"
[ "$1" = --user ] || exit 2
case "$2 $3" in
  "-q is-enabled") case " $ENABLED " in *" $4 "*) exit 0 ;; esac; exit 1 ;;
esac
exit 0
"""


def entry() -> configparser.SectionProxy:
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    parser.read(ENTRY)
    return parser["Desktop Entry"]


class AutostartEntryTests(unittest.TestCase):
    def run_exec(self, enabled: tuple[str, ...]) -> list[str]:
        exec_line = entry()["Exec"]
        # Characters the desktop entry spec reserves inside quotes, and single
        # quotes, which only GLib-based launchers treat as quoting.
        for reserved in "$`\\'":
            self.assertNotIn(reserved, exec_line)
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "systemctl"
            fake.write_text(FAKE_SYSTEMCTL)
            fake.chmod(0o755)
            log = Path(directory) / "log"
            log.touch()
            env = dict(
                os.environ,
                PATH=f"{directory}:{os.environ['PATH']}",
                SYSTEMCTL_LOG=str(log),
                ENABLED=" ".join(f"{unit}.service" for unit in enabled),
            )
            result = subprocess.run(shlex.split(exec_line), env=env, timeout=10)
            self.assertEqual(result.returncode, 0)
            return [line.split()[-1] for line in log.read_text().splitlines() if " start " in line]

    def test_starts_only_enabled_units(self) -> None:
        self.assertEqual(self.run_exec(()), [])
        self.assertEqual(self.run_exec(("corne-arcane-host",)), ["corne-arcane-host.service"])
        self.assertEqual(
            self.run_exec(("corne-arcane-host", "corne-arcane-tray")),
            ["corne-arcane-host.service", "corne-arcane-tray.service"],
        )

    def test_names_every_installed_unit(self) -> None:
        exec_line = entry()["Exec"]
        for unit in UNITS:
            self.assertTrue((HOST / "systemd" / f"{unit}.service.in").is_file())
            self.assertIn(f"start {unit}.service", exec_line)

    def test_is_hidden_from_menus(self) -> None:
        self.assertEqual(entry()["Type"], "Application")
        self.assertEqual(entry()["NoDisplay"], "true")

    @unittest.skipUnless(shutil.which("make"), "make is not installed")
    def test_install_and_uninstall(self) -> None:
        for prefix, path in (
            ("/usr", "etc/xdg/autostart"),
            ("/opt/ca", "opt/ca/etc/xdg/autostart"),
        ):
            with tempfile.TemporaryDirectory() as stage:
                args = ("make", "-s", "-C", str(HOST), f"DESTDIR={stage}", f"PREFIX={prefix}")
                subprocess.run((*args, "install"), check=True, capture_output=True)
                installed = Path(stage) / path / "corne-arcane.desktop"
                self.assertEqual(installed.read_text(), ENTRY.read_text())
                subprocess.run((*args, "uninstall"), check=True, capture_output=True)
                self.assertFalse(installed.exists())


if __name__ == "__main__":
    unittest.main()
