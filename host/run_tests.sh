#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
# No test may reach the desktop's own buses, where a running daemon would
# answer: tests that need D-Bus start a private dbus-daemon and say so.
DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/corne-arcane-tests
DBUS_SYSTEM_BUS_ADDRESS=unix:path=/nonexistent/corne-arcane-tests
export DBUS_SESSION_BUS_ADDRESS DBUS_SYSTEM_BUS_ADDRESS
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
PYTHONPYCACHEPREFIX="${TMPDIR:-/tmp}/corne-arcane-pycache" \
    python3 -m compileall -q arcane_host tests
