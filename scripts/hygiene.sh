#!/usr/bin/env sh
set -eu

tracked=$(git ls-files | grep -v '^docs/archive/' || true)

# Prose checks read prose. Images and captures carry no planning language, and a
# byte sequence inside one is not a sentence, so they are filtered out of every
# content check below. Path checks still see every tracked file.
text_only() {
    [ "$#" -eq 0 ] && return 0
    grep -Il . "$@" 2>/dev/null || true
}
tracked_text=$(text_only $tracked)
if printf '%s\n' "$tracked" | grep -Eiq '(^|/)(m[0-9]+([._-][0-9]+)?|post-m[0-9]+)[^/]*($|/)'; then
    echo "FAIL hygiene: historical-plan-prefixed tracked path outside docs/archive" >&2
    printf '%s\n' "$tracked" | grep -Ei '(^|/)(m[0-9]+([._-][0-9]+)?|post-m[0-9]+)[^/]*($|/)' >&2
    exit 1
fi

if [ -n "$tracked_text" ] && rg -n \
    'ARCANE_M1[0-3]|griffin_(anim|hostoled)|\b[Mm]1[0-3]_[A-Za-z0-9_]*' \
    $tracked_text; then
    echo "FAIL hygiene: historical-plan-prefixed identifier or retired keymap name" >&2
    exit 1
fi

# Active code and documentation describe current invariants. Planning history
# belongs under docs/archive; protocol-version compatibility language remains
# valid because this expression targets only planning labels.
active=$(printf '%s\n' "$tracked_text" | grep -Ev '^(scripts/hygiene\.sh|\.gitignore)$' || true)
if [ -n "$active" ] && rg -n -i \
    '\bmilestones?\b|\btracks? [A-Z](?:/[A-Z])*\b|\bwaves? [0-9]+\b|\bM[0-9]+(?:\.[0-9]+)?\b' \
    $active | grep -Ev '(^|[(/])(docs/)?archive/'; then
    echo "FAIL hygiene: historical planning language outside docs/archive" >&2
    exit 1
fi

# The off-keyboard shells are host-only and must never reach the flash image.
# desktop/ builds a shared object and web/ builds a wasm module, each by its own
# Makefile; QMK compiles the explicit SRC list in firmware/rules.mk and nothing
# else. There are exactly two ways that could silently stop being true, so both
# are checked: rules.mk gaining a reference, and a firmware source including a
# shell-only header. The dependency runs one way only, shell -> firmware/sim.
for shell in desktop web; do
    if grep -q "$shell" firmware/rules.mk; then
        echo "FAIL hygiene: firmware/rules.mk references $shell/; that code is not flashed" >&2
        exit 1
    fi
done
inbound=$(grep -rl 'duel_city\.h\|duel_ambient\.h\|duel_town\.h' firmware/sim \
    firmware/keymap.c firmware/config.h 2>/dev/null || true)
if [ -n "$inbound" ]; then
    echo "FAIL hygiene: firmware source includes a shell-only header" >&2
    printf '%s\n' "$inbound" >&2
    exit 1
fi

# The package version is declared once, in host/pyproject.toml. Documentation
# names the package rather than restating the number, so a release is a single
# edit and cannot leave contradictory versions behind. dpkg requires the version
# in debian/changelog, so that one restatement is checked for agreement rather
# than forbidden.
version=$(sed -n 's/^version = "\(.*\)"$/\1/p' host/pyproject.toml)
if [ -z "$version" ]; then
    echo "FAIL hygiene: no version declared in host/pyproject.toml" >&2
    exit 1
fi
restated=$(printf '%s\n' "$tracked_text" |
    grep -Ev '^(host/pyproject\.toml|debian/changelog)$' |
    xargs grep -lIF -- "$version" 2>/dev/null || true)
if [ -n "$restated" ]; then
    echo "FAIL hygiene: package version $version restated outside host/pyproject.toml" >&2
    printf '%s\n' "$restated" >&2
    exit 1
fi

changelog=$(sed -n '1s/^[^(]*(\([^)-]*\).*/\1/p' debian/changelog)
if [ "$changelog" != "$version" ]; then
    echo "FAIL hygiene: debian/changelog says $changelog, host/pyproject.toml says $version" >&2
    exit 1
fi

echo "PASS hygiene"
