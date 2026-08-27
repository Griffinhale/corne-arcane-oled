#!/usr/bin/env sh
set -eu

tracked=$(git ls-files | grep -v '^docs/archive/' || true)
if printf '%s\n' "$tracked" | grep -Eiq '(^|/)(m[0-9]+([._-][0-9]+)?|post-m[0-9]+)[^/]*($|/)'; then
    echo "FAIL hygiene: historical-plan-prefixed tracked path outside docs/archive" >&2
    printf '%s\n' "$tracked" | grep -Ei '(^|/)(m[0-9]+([._-][0-9]+)?|post-m[0-9]+)[^/]*($|/)' >&2
    exit 1
fi

if [ -n "$tracked" ] && rg -n \
    'ARCANE_M1[0-3]|griffin_(anim|hostoled)|\b[Mm]1[0-3]_[A-Za-z0-9_]*' \
    $tracked; then
    echo "FAIL hygiene: historical-plan-prefixed identifier or retired keymap name" >&2
    exit 1
fi

# Active code and documentation describe current invariants. Planning history
# belongs under docs/archive; protocol-version compatibility language remains
# valid because this expression targets only planning labels.
active=$(printf '%s\n' "$tracked" | grep -Ev '^(scripts/hygiene\.sh|\.gitignore)$' || true)
if [ -n "$active" ] && rg -n -i \
    '\bmilestones?\b|\btracks? [A-Z](?:/[A-Z])*\b|\bwaves? [0-9]+\b|\bM[0-9]+(?:\.[0-9]+)?\b' \
    $active | grep -Ev '(^|[(/])(docs/)?archive/'; then
    echo "FAIL hygiene: historical planning language outside docs/archive" >&2
    exit 1
fi

# The package version is declared once, in host/pyproject.toml. Documentation
# names the package rather than restating the number, so a release is a single
# edit and cannot leave contradictory versions behind.
version=$(sed -n 's/^version = "\(.*\)"$/\1/p' host/pyproject.toml)
if [ -z "$version" ]; then
    echo "FAIL hygiene: no version declared in host/pyproject.toml" >&2
    exit 1
fi
restated=$(printf '%s\n' "$tracked" | grep -v '^host/pyproject\.toml$' |
    xargs grep -lF -- "$version" 2>/dev/null || true)
if [ -n "$restated" ]; then
    echo "FAIL hygiene: package version $version restated outside host/pyproject.toml" >&2
    printf '%s\n' "$restated" >&2
    exit 1
fi

echo "PASS hygiene"
