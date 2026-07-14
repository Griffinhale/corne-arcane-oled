#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
elf=${FIRMWARE_ELF:-"$HOME/src/vial-qmk/.build/crkbd_rev1_griffin_hostoled_rp2040_ce.elf"}
uf2=${FIRMWARE_UF2:-"${elf%.elf}.uf2"}
map=${FIRMWARE_MAP:-"${elf%.elf}.map"}
build_kind=${FIRMWARE_BUILD_KIND:-release}
m10_bss=9676
m11_bss=10204
m11_data=3576
m11_static=$((m11_data + m11_bss))
m11_flash=50120
soft_flash=49152

if ! command -v arm-none-eabi-size >/dev/null 2>&1; then
    echo "FAIL budget: arm-none-eabi-size is not available" >&2
    exit 1
fi
if [ ! -f "$elf" ]; then
    echo "FAIL budget: firmware ELF not found: $elf" >&2
    echo "Set FIRMWARE_ELF to the M11.1 release ELF." >&2
    exit 1
fi

sections=$(arm-none-eabi-size -A "$elf")
flash=$(printf '%s\n' "$sections" | awk '$1==".boot2" || $1==".vectors" || $1==".text" || $1==".rodata" || $1==".data" {n+=$2} END {print n+0}')
data=$(printf '%s\n' "$sections" | awk '$1==".data" {print $2+0}')
bss=$(printf '%s\n' "$sections" | awk '$1==".bss" {print $2+0}')
static_ram=$((data + bss))
flash_delta=$((flash - m11_flash))
data_delta=$((data - m11_data))
bss_delta=$((bss - m11_bss))
static_delta=$((static_ram - m11_static))

printf 'firmware: %s\n' "$elf"
printf 'build kind: %s (ARCANE_DIAGNOSTICS must be absent from release)\n' "$build_kind"
printf 'release flash: %d bytes (M11 %d, delta %+d; soft target %d)\n' \
    "$flash" "$m11_flash" "$flash_delta" "$soft_flash"
printf '.data: %d bytes (M11 %d, delta %+d)\n' "$data" "$m11_data" "$data_delta"
printf '.bss: %d bytes (M11 %d, delta %+d; M10 %d)\n' \
    "$bss" "$m11_bss" "$bss_delta" "$m10_bss"
printf 'total static RAM (.data + .bss): %d bytes (M11 %d, delta %+d)\n' \
    "$static_ram" "$m11_static" "$static_delta"

echo "flash sections (arm-none-eabi-size -A):"
printf '%s\n' "$sections" | awk \
    '$1==".boot2" || $1==".vectors" || $1==".text" || $1==".rodata" || $1==".data" || $1==".bss" {print}'

echo "largest flash symbols:"
arm-none-eabi-nm --print-size --size-sort --radix=d "$elf" | \
    awk '$3 ~ /^[RrTt]$/ {print}' | tail -12
echo "largest static-RAM symbols:"
arm-none-eabi-nm --print-size --size-sort --radix=d "$elf" | \
    awk '$3 ~ /^[BbCDdGgSs]$/ {print}' | tail -12

failed=0
if [ "$flash" -gt "$m11_flash" ]; then
    echo "FAIL budget: release firmware exceeds the 50,120-byte M11 baseline"
    failed=1
else
    echo "PASS release firmware does not exceed M11"
fi
if [ "$flash" -le "$soft_flash" ]; then
    echo "PASS release firmware meets the 49,152-byte soft target"
else
    echo "NOTE release firmware remains above the 49,152-byte soft target"
fi
if [ "$build_kind" = release ] && \
   arm-none-eabi-nm "$elf" | awk '$3=="duel_diag" {found=1} END {exit !found}'; then
    echo "FAIL budget: release ELF contains ARCANE_DIAGNOSTICS state"
    failed=1
else
    echo "PASS release/diagnostic distinction"
fi

echo "wire/resource compile assertions:"
ASAN_OPTIONS=detect_leaks=0 "$root/firmware/sim_test/test_runner" \
    "$root/firmware/sim_test/traces" "$root/firmware/sim_test/golden" >/dev/null
echo "PASS Raw HID=32 bytes, split snapshot=31 bytes, sim_world_t=56 bytes"

echo "timing reference (desktop; RP2040 diagnostic peaks remain a physical acceptance gate):"
ASAN_OPTIONS=detect_leaks=0 "$root/firmware/sim_test/visual_runner" --benchmark

echo "artifact hashes:"
sha256sum "$elf"
if [ -f "$uf2" ]; then
    sha256sum "$uf2"
else
    echo "NOTE UF2 not found: $uf2"
fi
if [ -f "$map" ]; then
    printf 'linker map: %s (%s bytes)\n' "$map" "$(wc -c < "$map")"
else
    echo "NOTE linker map not found: $map"
fi
if [ "$failed" -ne 0 ]; then exit 1; fi
