#!/usr/bin/env sh
set -eu

flash_limit=81896
ram_limit=16496
hard_stop=$((96 * 1024))
reserve_min=$((16 * 1024))

measure() {
    image=$1
    flash=$(arm-none-eabi-size "$image" | awk 'NR == 2 { print $1 + $2 }')
    ram=$(arm-none-eabi-size -A "$image" | awk '
        $1 == ".data" || $1 == ".bss" || $1 ~ /^\.ram[0-7]$/ { total += $2 }
        END { print total + 0 }
    ')
    reserve=$((hard_stop - flash))
    printf '%s: flash=%s static_ram=%s reserve=%s\n' "$image" "$flash" "$ram" "$reserve"
    test "$flash" -le "$flash_limit" || {
        echo "FAIL release-budget: $image exceeds $flash_limit flash bytes" >&2
        return 1
    }
    test "$ram" -le "$ram_limit" || {
        echo "FAIL release-budget: $image exceeds $ram_limit static RAM bytes" >&2
        return 1
    }
    test "$flash" -le "$hard_stop" || {
        echo "FAIL release-budget: $image exceeds the 96 KiB hard stop" >&2
        return 1
    }
    test "$reserve" -ge "$reserve_min" || {
        echo "FAIL release-budget: $image leaves less than 16 KiB reserve" >&2
        return 1
    }
}

measure artifacts/release/griffin_arcane-release.elf
measure artifacts/release/griffin_arcane-diagnostic.elf
echo "PASS release-budget"
