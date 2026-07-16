#!/usr/bin/env sh
set -eu

tracked=$(git ls-files | grep -v '^docs/archive/' || true)
if printf '%s\n' "$tracked" | grep -Eq '(^|/)(m1[0-3]|post-m13)[^/]*($|/)'; then
    echo "FAIL hygiene: milestone-prefixed tracked path outside docs/archive" >&2
    printf '%s\n' "$tracked" | grep -E '(^|/)(m1[0-3]|post-m13)[^/]*($|/)' >&2
    exit 1
fi

if [ -n "$tracked" ] && rg -n \
    'ARCANE_M1[0-3]|griffin_(anim|hostoled)|\b[Mm]1[0-3]_[A-Za-z0-9_]*' \
    $tracked; then
    echo "FAIL hygiene: milestone-prefixed identifier or retired keymap name" >&2
    exit 1
fi

echo "PASS hygiene"
