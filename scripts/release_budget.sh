#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
# shellcheck source=budget.env
. "$root/scripts/budget.env"

# The ELF's .comment names the compiler that built it, which is what the
# figures depend on -- not whichever arm-none-eabi-gcc happens to be on PATH.
check_compiler() {
    image=$1
    comment=$(arm-none-eabi-readelf -p .comment "$image" 2>/dev/null || true)
    case "$comment" in
        *"$ARM_GCC_COMMENT"*) return 0 ;;
    esac
    if [ "${ALLOW_UNPINNED_TOOLCHAIN:-0}" = 1 ]; then
        echo "WARN release-budget: $image was not built with $ARM_GCC_COMMENT; figures are not comparable" >&2
        return 0
    fi
    echo "FAIL release-budget: $image was not built with $ARM_GCC_COMMENT (scripts/budget.env)" >&2
    echo "Install ARM GNU Toolchain $ARM_GCC_VERSION, or use ALLOW_UNPINNED_TOOLCHAIN=1 for a development measurement." >&2
    return 1
}

measure() {
    image=$1
    ram_baseline=$2
    check_compiler "$image"
    flash=$(arm-none-eabi-size "$image" | awk 'NR == 2 { print $1 + $2 }')
    ram=$(arm-none-eabi-size -A "$image" | awk '
        $1 == ".data" || $1 == ".bss" || $1 ~ /^\.ram[0-7]$/ { total += $2 }
        END { print total + 0 }
    ')
    reserve=$((HARD_STOP - flash))
    ram_growth_limit=$((ram_baseline + RAM_GROWTH_ALLOWANCE))
    printf '%s: flash=%s static_ram=%s reserve=%s\n' "$image" "$flash" "$ram" "$reserve"
    test "$flash" -le "$FLASH_LIMIT" || {
        echo "FAIL release-budget: $image exceeds $FLASH_LIMIT flash bytes" >&2
        return 1
    }
    test "$ram" -le "$RAM_LIMIT" || {
        echo "FAIL release-budget: $image exceeds $RAM_LIMIT static RAM bytes" >&2
        return 1
    }
    test "$ram" -le "$ram_growth_limit" || {
        echo "FAIL release-budget: $image exceeds its +$RAM_GROWTH_ALLOWANCE static RAM growth allowance ($ram_growth_limit)" >&2
        return 1
    }
    test "$flash" -le "$HARD_STOP" || {
        echo "FAIL release-budget: $image exceeds the $HARD_STOP-byte hard stop" >&2
        return 1
    }
    test "$reserve" -ge "$RESERVE_MIN" || {
        echo "FAIL release-budget: $image leaves less than $RESERVE_MIN bytes of reserve" >&2
        return 1
    }
}

echo "release-budget: limits measured with ARM GNU Toolchain $ARM_GCC_VERSION"
measure artifacts/release/griffin_arcane-release.elf "$RELEASE_RAM_BASELINE"
measure artifacts/release/griffin_arcane-diagnostic.elf "$DIAGNOSTIC_RAM_BASELINE"
echo "PASS release-budget"
