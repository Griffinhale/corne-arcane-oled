"""The keyboard handoff end to end: real launcher and diagnostics processes,
real signals, a fake Vial and a fake systemctl first on PATH.

Nothing here reaches a real device, sysfs or the real user manager: the
launcher is given a fake hidraw node, diagnostics talks to a pty, the session
bus is a dead socket, and systemctl and notify-send are stand-ins.
"""

from __future__ import annotations

import os
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from collections.abc import Callable
from pathlib import Path

import fake_systemctl

HOST_DIR = Path(__file__).resolve().parents[1]
HOLDER = "Vial (corne-arcane-vial)"
ENV_VIAL = "CORNE_ARCANE_VIAL_BIN"
# Runs the real launcher; only the node choice is fixed, so no sysfs is read.
LAUNCHER = (
    "import sys\n"
    "from pathlib import Path\n"
    "from arcane_host import hid_ownership, vial_launcher\n"
    "if hasattr(hid_ownership, 'chosen_node'):\n"
    "    hid_ownership.chosen_node = lambda _explicit=None: Path('/dev/hidraw-e2e-fake')\n"
    "raise SystemExit(vial_launcher.main([]))\n"
)


def wait_until(condition: Callable[[], bool], timeout: float = 10.0) -> bool:
    deadline = time.monotonic() + timeout
    while not condition():
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.05)
    return True


def gone(pid: int) -> bool:
    """True once pid has exited (a zombie awaiting its reaper counts as exited)."""
    try:
        fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    except (OSError, IndexError):
        return True
    return fields[0] == "Z"


class HandoffTests(unittest.TestCase):
    def setUp(self) -> None:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.systemd = self.root / "systemd"
        self.systemd.mkdir()
        self.set_state("active")
        self.log = self.systemd / "log"
        self.log.touch()
        bin_dir = self.root / "bin"
        bin_dir.mkdir()
        fake_systemctl.write_wrapper(bin_dir)
        self.notices = self.root / "notices"
        notify = bin_dir / "notify-send"
        notify.write_text(f"#!/bin/sh\necho \"$*\" >> '{self.notices}'\n")
        notify.chmod(0o755)
        (self.root / "run").mkdir()
        self.release = self.root / "release"
        self.env = {
            key: value
            for key, value in os.environ.items()
            if key not in ("CORNE_ARCANE_SYSTEMCTL", "CORNE_ARCANE_SERVICE", ENV_VIAL)
        }
        self.env.update(
            PATH=f"{bin_dir}{os.pathsep}{os.environ.get('PATH', '')}",
            FAKE_SYSTEMCTL_DIR=str(self.systemd),
            XDG_RUNTIME_DIR=str(self.root / "run"),
            XDG_CONFIG_HOME=str(self.root / "config"),
            DBUS_SESSION_BUS_ADDRESS="unix:path=/nonexistent/corne-arcane-tests",
            DBUS_SYSTEM_BUS_ADDRESS="unix:path=/nonexistent/corne-arcane-tests",
            PYTHONDONTWRITEBYTECODE="1",
        )
        self.vials = 0
        self.pid_files: list[Path] = []
        self.addCleanup(self.reap)

    def reap(self) -> None:
        self.release.touch()
        for pid_file in self.pid_files:
            try:
                pid = int(pid_file.read_text())
            except (OSError, ValueError):
                continue
            for kill in (os.killpg, os.kill):
                try:
                    kill(pid, signal.SIGKILL)
                except OSError:
                    pass

    def set_state(self, state: str) -> None:
        (self.systemd / "state").write_text(state + "\n")

    def state(self) -> str:
        return (self.systemd / "state").read_text().strip()

    def events(self) -> list[str]:
        # The pre-guard code also asked systemctl for the daemon's MainPID.
        return [line for line in self.log.read_text().splitlines() if "systemctl show" not in line]

    def fake_vial(self, body: str) -> Path:
        """A Vial that records its pid and start in the log, then runs body."""
        self.vials += 1
        script = self.root / f"vial{self.vials}"
        pid_file = self.root / f"vial{self.vials}.pid"
        self.pid_files.append(pid_file)
        # Off the launcher's pipes, so a Vial left running cannot hold the test open.
        script.write_text(
            "#!/bin/sh\nexec >/dev/null 2>&1\n"
            f"echo $$ > '{pid_file}'\necho 'vial start' >> '{self.log}'\n{body}\n"
        )
        script.chmod(0o755)
        return script

    def held_vial(self) -> str:
        """Body for a Vial that stays open until the test releases it."""
        return f"while [ ! -e '{self.release}' ]; do sleep 0.05; done"

    def spawn(self, argv: list[str], vial: Path | None = None) -> subprocess.Popen[str]:
        env = dict(self.env)
        if vial is not None:
            env[ENV_VIAL] = str(vial)
        child = subprocess.Popen(
            argv,
            cwd=HOST_DIR,
            env=env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            start_new_session=True,
        )
        self.addCleanup(self.stop, child)
        return child

    def launch(self, body: str) -> subprocess.Popen[str]:
        return self.spawn([sys.executable, "-c", LAUNCHER], self.fake_vial(body))

    def stop(self, child: subprocess.Popen[str]) -> None:
        if child.poll() is None:
            child.kill()
        # A launcher that failed to stop Vial leaves it in the launcher's group.
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except OSError:
            pass
        self.reap()
        child.communicate(timeout=10)

    def finish(self, child: subprocess.Popen[str]) -> tuple[int, str]:
        _out, err = child.communicate(timeout=20)
        return child.returncode, err

    def wait_for_vial(self, count: int = 1) -> None:
        self.assertTrue(
            wait_until(lambda: self.events().count("vial start") >= count),
            f"Vial never started: {self.events()}",
        )

    def test_clean_run_stops_then_restores(self) -> None:
        code, err = self.finish(self.launch("exit 0"))
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(
            self.events(),
            [
                "systemctl is-active -> active",
                "systemctl stop -> inactive",
                "vial start",
                "systemctl start -> active",
            ],
        )

    def test_sigkill_leaves_a_debt_the_next_launch_pays(self) -> None:
        first = self.launch(self.held_vial())
        self.wait_for_vial()
        first.kill()
        first.wait()
        self.release.touch()
        self.assertEqual(self.state(), "inactive")
        code, err = self.finish(self.launch("exit 0"))
        self.assertEqual(code, 0, err)
        self.assertIn(f"left stopped by {HOLDER}", err)
        self.assertEqual(self.state(), "active")
        self.assertEqual(self.events()[-1], "systemctl start -> active")

    def test_forking_vial_keeps_the_daemon_stopped_until_its_window_closes(self) -> None:
        window = f"( sleep 0.5; echo 'vial window closed' >> '{self.log}' ) &\nexit 0"
        code, err = self.finish(self.launch(window))
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(
            self.events(),
            [
                "systemctl is-active -> active",
                "systemctl stop -> inactive",
                "vial start",
                "vial window closed",
                "systemctl start -> active",
            ],
        )

    def test_missing_unit_exit_4_runs_vial_without_touching_the_service(self) -> None:
        self.set_state("missing")
        code, err = self.finish(self.launch("exit 0"))
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(self.events(), ["systemctl is-active -> missing", "vial start"])

    def test_second_launcher_is_refused_and_names_the_owner(self) -> None:
        first = self.launch(self.held_vial())
        self.wait_for_vial()
        code, err = self.finish(self.launch("exit 0"))
        self.assertEqual(code, 1)
        self.assertIn(f"in use by {HOLDER} (pid {first.pid})", err)
        self.assertIn(f"in use by {HOLDER}", self.notices.read_text())
        self.release.touch()
        self.assertEqual(self.finish(first), (0, ""))
        self.assertEqual(
            self.events(),
            [
                "systemctl is-active -> active",
                "systemctl stop -> inactive",
                "vial start",
                "systemctl start -> active",
            ],
        )

    def test_diagnostics_is_refused_while_vial_holds_the_keyboard(self) -> None:
        master, slave = os.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        first = self.launch(self.held_vial())
        self.wait_for_vial()
        diagnostics = self.spawn(
            [
                sys.executable,
                "-m",
                "arcane_host.diagnostics",
                "--device",
                os.ttyname(slave),
                "--timeout",
                "0.2",
            ]
        )
        code, err = self.finish(diagnostics)
        self.assertEqual(code, 1)
        self.assertIn(f"in use by {HOLDER} (pid {first.pid})", err)
        self.release.touch()
        self.assertEqual(self.finish(first), (0, ""))
        self.assertEqual(self.events().count("systemctl stop -> inactive"), 1)
        self.assertEqual(self.state(), "active")

    def test_sigterm_stops_vials_whole_group_and_restores(self) -> None:
        window_pid = self.root / "window.pid"
        body = f"sh -c 'echo $$ > \"{window_pid}\"; exec sleep 30' &\nsleep 30"
        launcher = self.launch(body)
        self.wait_for_vial()
        self.assertTrue(wait_until(window_pid.exists))
        launcher.send_signal(signal.SIGTERM)
        code, _err = self.finish(launcher)
        self.assertEqual(code, 128 + signal.SIGTERM)
        self.assertEqual(self.state(), "active")
        vial_pid = int(self.pid_files[0].read_text())
        for pid in (vial_pid, int(window_pid.read_text())):
            self.assertTrue(wait_until(lambda pid=pid: gone(pid), 5.0), f"pid {pid} survived")

    def test_crashing_vial_exits_128_plus_signal_and_restores(self) -> None:
        code, err = self.finish(self.launch("kill -SEGV $$"))
        self.assertEqual((code, err), (128 + signal.SIGSEGV, ""))
        self.assertEqual(self.events()[-1], "systemctl start -> active")
        self.assertEqual(self.state(), "active")


if __name__ == "__main__":
    unittest.main()
