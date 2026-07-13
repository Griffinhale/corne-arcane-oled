#!/usr/bin/env sh
# One-command CI for the duel engine: builds and runs every host test.
# Re-execs itself inside the project nix-shell when no host compiler is on PATH.
set -e
dir="$(cd "$(dirname "$0")" && pwd)"
if ! command -v cc >/dev/null 2>&1; then
    exec nix-shell "$HOME/src/vial-qmk/shell.nix" --run "make -C '$dir' test noalloc-check"
fi
exec make -C "$dir" test noalloc-check
