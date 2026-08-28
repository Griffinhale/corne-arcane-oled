#!/usr/bin/env sh
# The WASM acceptance test: byte-identical pixels, native against browser.
#
# Determinism is the whole claim -- a seed in a URL is a promise that your
# world and mine are the same world, spell for spell. That promise is only
# worth making if the browser's renderer and the desktop's agree exactly, so
# this compares them the strictest way available: the same matrix of seeds,
# frames and layouts rendered both ways, hashed per frame, plus one raw frame
# per layout compared byte for byte with cmp.
#
# Both sides drive the self-playing world, so this checks the simulation as
# well as the renderer: a divergence in either shows up as a hash that stops
# matching partway through a run.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
out=${PARITY_OUT:-$root/web/tools/.parity}
rm -rf "$out"
mkdir -p "$out"

echo "parity: rendering the matrix natively"
PYTHONPATH="$root/host" python3 "$root/web/tools/parity_native.py" "$out"

echo "parity: rendering the matrix in WASM"
node "$root/web/tools/parity_wasm.mjs" "$out"

if ! diff -u "$out/native.hashes" "$out/wasm.hashes" > "$out/hashes.diff"; then
    echo "FAIL parity: the two builds disagree; first differences:" >&2
    head -40 "$out/hashes.diff" >&2
    exit 1
fi

frames=$(grep -cv ' stats ' "$out/native.hashes")

for raw in "$out"/native-layout*.raw; do
    layout=$(basename "$raw" .raw | sed 's/^native-layout//')
    if ! cmp "$raw" "$out/wasm-layout$layout.raw"; then
        echo "FAIL parity: layout $layout pixels differ" >&2
        exit 1
    fi
done

echo "PASS parity: $frames frames byte-identical, native and WASM"

# The second half of the promise. The first says the browser's renderer agrees
# with the desktop's; this says that arriving at a moment by link is the same
# as having watched the world into it, which is what the URL claims.
node "$root/web/tools/share_check.mjs" "$out"
