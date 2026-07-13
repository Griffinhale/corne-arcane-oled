#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tests -v
PYTHONPYCACHEPREFIX="${TMPDIR:-/tmp}/corne-arcane-pycache" \
    python3 -m compileall -q arcane_host tests
