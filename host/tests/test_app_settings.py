"""The app's settings: the Vial command the menu launcher honours, focus
producer switches, and keymap profiles. Fake tools and private directories
only; no keyboard, no real user manager, no desktop bus."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import fake_systemctl
from arcane_host import keymap_profiles
from arcane_host.app_controls import Controls
from arcane_host.app_settings import FocusProducer, Settings, SettingsWindow
from test_app_controls import FakeTk as ControlsTk
from test_app_controls import FakeVar, FakeWidget, StubView, script, wait_until
from test_handoff_e2e import LAUNCHER

HOST_DIR = Path(__file__).resolve().parents[1]


def completed(code: int = 0, stdout: str = "", stderr: str = "") -> subprocess.CompletedProcess:
    return subprocess.CompletedProcess([], code, stdout, stderr)


class SettingsCase(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        patcher = mock.patch.dict(
            os.environ,
            {
                "XDG_CONFIG_HOME": str(self.root / "config"),
                "XDG_DATA_HOME": str(self.root / "data"),
            },
        )
        patcher.start()
        self.addCleanup(patcher.stop)
        os.environ.pop("CORNE_ARCANE_VIAL_BIN", None)
        self.view = StubView()
        self.controls = Controls(self.view, lock=self.root / "hid.lock")
        self.addCleanup(self.controls.close)
        self.calls: list[list[str]] = []
        self.replies: dict[str, subprocess.CompletedProcess] = {}

    def run_fake(self, argv: list[str]) -> subprocess.CompletedProcess:
        self.calls.append(argv)
        return self.replies.get(" ".join(argv), completed())

    def settings(self, **kwargs) -> Settings:
        kwargs.setdefault("producers", [])
        kwargs.setdefault("run", self.run_fake)
        return Settings(self.controls, **kwargs)


class VialCommandTests(SettingsCase):
    def test_vial_command_is_honoured_by_the_menu_launcher(self) -> None:
        """Set in the app, run by the real launcher in a menu-like environment."""
        seen = self.root / "seen"
        vial = script(self.root, "my vial", f"echo \"$@\" > '{seen}'")[0]
        settings = self.settings()
        settings.set_vial_command(f"'{vial}' --from-app")
        self.assertEqual(settings.vial_command(), f"'{vial}' --from-app")
        self.assertEqual(settings.message, f"Open Vial now runs: '{vial}' --from-app")

        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        fake_systemctl.write_wrapper(bin_dir)
        state = self.root / "systemd"
        state.mkdir()
        (state / "state").write_text("inactive\n")
        (self.root / "run").mkdir()
        # What a menu launch has: no CORNE_ARCANE_VIAL_BIN, only the config file.
        env = {
            key: value
            for key, value in os.environ.items()
            if key not in ("CORNE_ARCANE_VIAL_BIN", "CORNE_ARCANE_SYSTEMCTL")
        }
        env.update(
            PATH=f"{bin_dir}{os.pathsep}{os.environ.get('PATH', '')}",
            FAKE_SYSTEMCTL_DIR=str(state),
            XDG_RUNTIME_DIR=str(self.root / "run"),
            DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/corne-arcane-tests",
            DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent/corne-arcane-tests",
            PYTHONDONTWRITEBYTECODE="1",
        )
        launcher = subprocess.run(
            [sys.executable, "-c", LAUNCHER],
            cwd=HOST_DIR,
            env=env,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(launcher.returncode, 0, launcher.stderr)
        self.assertEqual(seen.read_text().strip(), "--from-app")

    def test_clearing_and_bad_input(self) -> None:
        settings = self.settings()
        settings.set_vial_command("flatpak run com.vial.Vial")
        path = Path(os.environ["XDG_CONFIG_HOME"]) / "corne-arcane" / "vial"
        self.assertTrue(path.read_text().startswith("# "))
        settings.set_vial_command("one\ntwo")
        self.assertEqual(settings.message, "The Vial command must be one line")
        self.assertEqual(settings.vial_command(), "flatpak run com.vial.Vial")
        settings.set_vial_command("  ")
        self.assertFalse(path.exists())
        self.assertEqual(settings.vial_command(), "")
        with mock.patch.dict(os.environ, {"CORNE_ARCANE_VIAL_BIN": "/opt/vial"}):
            settings.set_vial_command("/usr/bin/vial")
        self.assertIn("CORNE_ARCANE_VIAL_BIN is set here, and it wins", settings.message)


class FocusProducerTests(SettingsCase):
    def producer(self) -> FocusProducer:
        return FocusProducer(
            "X11 focus producer",
            ["systemctl", "--user", "is-enabled", "unit"],
            ["systemctl", "--user", "enable", "--now", "unit"],
            ["systemctl", "--user", "disable", "--now", "unit"],
            lambda result: result.returncode == 0,
        )

    def test_state_and_switching(self) -> None:
        producer = self.producer()
        settings = self.settings(producers=[producer])
        self.replies["systemctl --user is-enabled unit"] = completed(1)
        self.assertFalse(settings.producer_enabled(producer))
        settings.set_producer(producer, True)
        self.assertEqual(self.calls[-1], producer.enable)
        self.assertEqual(settings.message, "X11 focus producer: on")
        self.replies["systemctl --user disable --now unit"] = completed(
            1, stderr="Failed to disable unit: Access denied\n"
        )
        settings.set_producer(producer, False)
        self.assertEqual(
            settings.message,
            "Could not switch the X11 focus producer: Failed to disable unit: Access denied",
        )

    def test_a_missing_tool_reads_as_unknown(self) -> None:
        def missing(_argv):
            raise FileNotFoundError("systemctl")

        settings = self.settings(run=missing)
        self.assertIsNone(settings.producer_enabled(self.producer()))

    def test_the_gnome_extension_is_read_from_the_enabled_list(self) -> None:
        from arcane_host import app_settings

        with mock.patch.object(app_settings.shutil, "which", return_value="/usr/bin/x"):
            gnome = app_settings.focus_producers()[1]
        settings = self.settings()
        self.replies["gnome-extensions list --enabled"] = completed(
            stdout=f"other@x\n{app_settings.GNOME_UUID}\n"
        )
        self.assertTrue(settings.producer_enabled(gnome))
        self.replies["gnome-extensions list --enabled"] = completed(stdout="other@x\n")
        self.assertFalse(settings.producer_enabled(gnome))


class ProfileTests(SettingsCase):
    def test_list_and_delete_are_file_operations(self) -> None:
        settings = self.settings()
        directory = keymap_profiles.profiles_dir()
        directory.mkdir(parents=True)
        (directory / "work.json").write_text("{}")
        (directory / "games.json").write_text("{}")
        self.assertEqual(settings.profiles(), ["games", "work"])
        settings.delete_profile("work")
        self.assertEqual(settings.profiles(), ["games"])
        settings.delete_profile("work")
        self.assertIn("no profile named 'work'", settings.message)

    def test_save_and_load_run_the_keymap_command(self) -> None:
        log = self.root / "keymap-args"
        command = script(self.root, "keymap", f'echo "$@" >> \'{log}\'; echo "saved /p/$2.json"')
        settings = self.settings(keymap_command=command)
        settings.save_profile("work")
        self.assertTrue(settings.busy)
        self.assertFalse(self.controls.can_use_keyboard, "one keyboard task at a time")
        self.assertTrue(wait_until(lambda: (settings.poll(), not settings.busy)[1], 10.0))
        self.assertEqual(settings.message, "Saved /p/work.json")
        settings.save_profile("work", force=True)
        wait_until(lambda: (settings.poll(), not settings.busy)[1], 10.0)
        self.assertEqual(log.read_text().splitlines(), ["save work", "save work --force"])

    def test_failures_and_refusals_show_inline(self) -> None:
        settings = self.settings(
            keymap_command=script(
                self.root,
                "keymap",
                "echo \"corne-arcane-keymap: profile 'work' was saved from a different "
                'layout; not loaded" >&2; exit 1',
            )
        )
        settings.load_profile("work")
        wait_until(lambda: (settings.poll(), not settings.busy)[1], 10.0)
        self.assertEqual(
            settings.message, "profile 'work' was saved from a different layout; not loaded"
        )
        settings.save_profile("../escape")
        self.assertIn("invalid profile name", settings.message)
        self.assertFalse(settings.busy)
        self.view.status = ("paused", "", True, "Vial (pid 3)")
        settings.save_profile("work")
        self.assertEqual(settings.message, "The keyboard is in use by Vial (pid 3)")
        self.assertFalse(settings.busy)


class FakeListbox(FakeWidget):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.items: list[str] = []
        self.selection: tuple[int, ...] = ()

    def delete(self, _first, _last) -> None:
        self.items = []

    def insert(self, _where, item) -> None:
        self.items.append(item)

    def curselection(self):
        return self.selection


class FakeToplevel(FakeWidget):
    def title(self, _text) -> None:
        pass

    def protocol(self, _name, callback) -> None:
        self.close_callback = callback

    def destroy(self) -> None:
        self.destroyed = True


class FakeTk(ControlsTk):
    Toplevel = FakeToplevel
    Entry = Checkbutton = FakeWidget
    Listbox = FakeListbox
    IntVar = FakeVar


class SettingsWindowTests(SettingsCase):
    def test_window_lists_profiles_and_follows_the_keyboard(self) -> None:
        directory = keymap_profiles.profiles_dir()
        directory.mkdir(parents=True)
        (directory / "work.json").write_text("{}")
        window = SettingsWindow(FakeTk, None, self.settings())
        self.assertEqual(window.listbox.items, ["work"])
        self.assertEqual(window.load_button.cget("state"), "normal")
        window._load()
        window.refresh()
        self.assertEqual(window.message.cget("text"), "Pick a profile to load")
        self.view.status = ("paused", "", True, "Vial (pid 3)")
        window.refresh()
        self.assertEqual(window.load_button.cget("state"), "disabled")
        self.assertEqual(window.save_button.cget("state"), "disabled")
        window.listbox.selection = (0,)
        window._delete()
        window.refresh()
        self.assertEqual(window.listbox.items, [])
        window.close()
        self.assertTrue(window.window.destroyed)


if __name__ == "__main__":
    unittest.main()
