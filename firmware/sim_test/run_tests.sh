#!/usr/bin/env sh
# One-command CI for the duel engine: builds and runs every host test.
set -e
dir="$(cd "$(dirname "$0")" && pwd)"
if ! command -v cc >/dev/null 2>&1; then
    echo "host C compiler not found on PATH" >&2
    exit 1
fi
exec make -C "$dir" test noalloc-check
