#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
elf=${FIRMWARE_ELF:-"$HOME/src/vial-qmk/.build/crkbd_rev1_griffin_hostoled_rp2040_ce.elf"}
baseline_bss=9676
max_flash=65536
max_bss=$((baseline_bss + 2048))

if ! command -v arm-none-eabi-size >/dev/null 2>&1; then
    echo "FAIL budget: arm-none-eabi-size is not available" >&2
    exit 1
fi
if [ ! -f "$elf" ]; then
    echo "FAIL budget: firmware ELF not found: $elf" >&2
    echo "Set FIRMWARE_ELF to the M11 release ELF." >&2
    exit 1
fi

sections=$(arm-none-eabi-size -A "$elf")
flash=$(printf '%s\n' "$sections" | awk '$1==".boot2" || $1==".vectors" || $1==".text" || $1==".rodata" || $1==".data" {n+=$2} END {print n+0}')
bss=$(printf '%s\n' "$sections" | awk '$1==".bss" {print $2+0}')
growth=$((bss - baseline_bss))

printf 'firmware: %s\n' "$elf"
printf 'release flash: %d / %d bytes\n' "$flash" "$max_flash"
printf 'BSS: %d bytes (M10 %d, growth %+d, limit %+d)\n' "$bss" "$baseline_bss" "$growth" 2048

failed=0
if [ "$flash" -gt "$max_flash" ]; then
    echo "FAIL budget: release firmware exceeds 64 KiB"
    failed=1
else
    echo "PASS release firmware size"
fi
if [ "$bss" -gt "$max_bss" ]; then
    echo "FAIL budget: BSS growth exceeds 2 KiB"
    failed=1
else
    echo "PASS BSS growth"
fi

echo "wire/resource compile assertions:"
ASAN_OPTIONS=detect_leaks=0 "$root/firmware/sim_test/test_runner" \
    "$root/firmware/sim_test/traces" "$root/firmware/sim_test/golden" >/dev/null
echo "PASS Raw HID=32 bytes, split snapshot=31 bytes, sim_world_t=56 bytes"

echo "timing reference (desktop; RP2040 diagnostic peaks remain a physical acceptance gate):"
ASAN_OPTIONS=detect_leaks=0 "$root/firmware/sim_test/visual_runner" --benchmark

sha256sum "$elf"
if [ "$failed" -ne 0 ]; then exit 1; fi
