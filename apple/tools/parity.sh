#!/usr/bin/env sh
# The third leg: byte-identical pixels, native against the Apple shell.
#
# web/tools/parity.sh compares two renderers and calls their agreement
# determinism. Two is where that argument stops being about the project and
# starts being about those two, so this runs the same matrix a third way,
# through the Swift package that the iOS app and the widget are built on, and
# diffs it against the same native reference.
#
# Deliberately the same shape as the WASM leg: same matrix file, same line
# format, same diff. A leg that checked something slightly different would be
# a second opinion rather than a third witness.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${PARITY_OUT:-$root/apple/tools/.parity}
rm -rf "$out"
mkdir -p "$out"

echo "parity: rendering the matrix natively"
PYTHONPATH="$root/host" python3 "$root/web/tools/parity_native.py" "$out"

echo "parity: rendering the matrix in Swift"
(cd "$root" && swift run -c release city-check parity "$out")

if ! diff -u "$out/native.hashes" "$out/swift.hashes" > "$out/hashes.diff"; then
    echo "FAIL parity: the native and Swift builds disagree; first differences:" >&2
    head -40 "$out/hashes.diff" >&2
    exit 1
fi

frames=$(grep -cv ' stats ' "$out/native.hashes")
echo "PASS parity: $frames frames byte-identical, native and Swift"

# The invariants that are about this shell rather than about the pixels: the
# first tick at time zero, the run-up a seek has to render, and a day of
# widget entries generated in one forward pass.
(cd "$root" && swift run -c release city-check invariants)
