#!/usr/bin/env sh
set -eu

release=artifacts/m13/griffin_anim-m13-vial-release.elf
baseline=artifacts/m13/m12-rollback.elf

if [ ! -f "$release" ] || [ ! -f "$baseline" ]; then
    echo "FAIL m13-budget: build artifacts are missing" >&2
    exit 1
fi

read_metrics() {
    flash=$(arm-none-eabi-size "$1" | awk 'NR == 2 { print $1 + $2 }')
    ram=$(arm-none-eabi-size -A "$1" | awk '
        $1 == ".data" || $1 == ".bss" || $1 ~ /^\.ram[0-7]$/ { total += $2 }
        END { print total }
    ')
    echo "$flash $ram"
}

set -- $(read_metrics "$release")
release_flash=$1
release_ram=$2
set -- $(read_metrics "$baseline")
baseline_flash=$1
baseline_ram=$2

flash_delta=$((release_flash - baseline_flash))
ram_delta=$((release_ram - baseline_ram))
hard_reserve=$((96 * 1024 - release_flash))
accepted_m13_ram=16496
m13_ram_delta=$((release_ram - accepted_m13_ram))

echo "M13 release flash: $release_flash bytes (delta $flash_delta)"
echo "M13 static RAM: $release_ram bytes (delta $ram_delta)"
echo "Static RAM delta from accepted b7c6d8d M13: $m13_ram_delta bytes"
echo "Reserve below 96 KiB hard stop: $hard_reserve bytes"

test "$release_flash" -le $((80 * 1024)) || {
    echo "FAIL m13-budget: release exceeds 80 KiB target" >&2; exit 1;
}
test "$release_flash" -le $((96 * 1024)) || {
    echo "FAIL m13-budget: release exceeds 96 KiB hard stop" >&2; exit 1;
}
test "$hard_reserve" -ge $((16 * 1024)) || {
    echo "FAIL m13-budget: less than 16 KiB reserve remains" >&2; exit 1;
}
test "$m13_ram_delta" -eq 0 || {
    echo "FAIL m13-budget: static RAM changed from accepted M13" >&2; exit 1;
}

echo "PASS m13-budget"
