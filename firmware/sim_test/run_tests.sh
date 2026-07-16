#!/usr/bin/env sh
set -eu

dir="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
if ! command -v cc >/dev/null 2>&1; then
    echo "host C compiler not found on PATH" >&2
    exit 1
fi
exec make -C "$dir" test
