#!/usr/bin/env sh
# One-command CI for the duel engine: builds and runs every host test.
set -e
dir="$(cd "$(dirname "$0")" && pwd)"
if ! command -v cc >/dev/null 2>&1; then
    echo "host C compiler not found on PATH" >&2
    exit 1
fi
# Both the accepted M11.5 release path and the ARCANE_M12 build are gated here so
# a regression in either variant fails CI. World-hash streams (test / test-m12)
# must match the same golden, proving M12 presentation never perturbs mechanics.
exec make -C "$dir" test noalloc-check visual-test test-m12 visual-test-m12
