#!/usr/bin/env sh
# Every build of the shared C must compile the same files.
#
# The firmware (rules.mk), the native builds (sim_sources.mk) and the Apple
# package (Package.swift, which takes all of firmware/sim) each name the
# simulation sources their own way, and the desktop sources are named again by
# desktop/Makefile, web/Makefile and Package.swift. A file missing from one
# list shows up only in the weekly QMK build or in Xcode, so this compares them
# on every `make test`. The make lists are read by make itself, not by
# pattern-matching the files, so line continuations and variables count.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
failed=0

# Print one make variable as base names, one per line, sorted.
make_list() {
    dir=$1 var=$2
    shift 2
    printf 'include %s\n__print-%s:;@echo $(%s)\n' "$@" "$var" "$var" |
        make -s -C "$root/$dir" -f - "__print-$var" | tr ' ' '\n' | sed -n 's|.*/||; /\.c$/p' | sort
}

compare() {
    name=$1 left=$2 right=$3
    if ! diff -u "$left" "$right" > "$work/diff"; then
        echo "FAIL check-sources: $name" >&2
        sed -n '3,$p' "$work/diff" >&2
        failed=1
    fi
}

# Simulation sources. The directory is the Apple package's list.
grep -q '"firmware/sim",' "$root/Package.swift" || {
    echo "FAIL check-sources: Package.swift no longer takes all of firmware/sim; teach this script its list" >&2
    exit 1
}
(cd "$root/firmware/sim" && ls -- *.c) | sort > "$work/directory"
printf 'SIM_DIR := sim\ninclude sim_sources.mk\n__print-SIM_SRC:;@echo $(SIM_SRC)\n' |
    make -s -C "$root/firmware" -f - __print-SIM_SRC | tr ' ' '\n' | sed -n 's|.*/||; /\.c$/p' |
    sort > "$work/sim_sources"
# Diagnostic builds compile everything; release builds leave out only diagnostics.
printf 'include rules.mk\n__print-SRC:;@echo $(SRC)\n' |
    make -s -C "$root/firmware" -f - __print-SRC ARCANE_DIAGNOSTICS=yes | tr ' ' '\n' |
    sed -n '/^sim\//{s|.*/||;p;}' | sort > "$work/rules_diagnostics"
make_list firmware SRC rules.mk | sed -n '/^duel_/p' > "$work/rules_release"
grep -v '^duel_diagnostics\.c$' "$work/directory" > "$work/directory_release"

compare "firmware/sim_sources.mk against firmware/sim/*.c (Package.swift)" \
    "$work/directory" "$work/sim_sources"
compare "firmware/rules.mk with ARCANE_DIAGNOSTICS=yes against firmware/sim/*.c" \
    "$work/directory" "$work/rules_diagnostics"
compare "firmware/rules.mk release build against firmware/sim/*.c less duel_diagnostics.c" \
    "$work/directory_release" "$work/rules_release"

# Desktop sources, outside firmware/sim, named by three builds.
make_list desktop LIB_SRC Makefile > "$work/desktop"
make_list web WASM_SRC Makefile | sed '/^duel_wasm\.c$/d' > "$work/web"
sed -n 's|^ *"desktop/\([^"]*\.c\)",$|\1|p' "$root/Package.swift" | sort > "$work/swift"
compare "web/Makefile WASM_SRC against desktop/Makefile LIB_SRC" "$work/desktop" "$work/web"
compare "Package.swift desktop sources against desktop/Makefile LIB_SRC" "$work/desktop" "$work/swift"

if [ "$failed" -ne 0 ]; then
    exit 1
fi
echo "PASS check-sources: $(wc -l < "$work/directory") simulation and $(wc -l < "$work/desktop") desktop sources, every list"
