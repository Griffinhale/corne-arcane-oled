#!/usr/bin/env bash
set -euo pipefail

echo "== lsusb filtered =="
lsusb | /usr/bin/grep -Ei '4653|2e8a|raspberry|rp2|foostan|corne' || true

echo
echo "== drives filtered =="
lsblk -o NAME,LABEL,SIZE,MODEL,MOUNTPOINTS | /usr/bin/grep -Ei 'RPI-RP2|RP2|UF2|NAME|LABEL' || true

echo
echo "== hidraw properties =="
for h in /dev/hidraw*; do
  [[ -e "$h" ]] || continue
  echo "=== $h ==="
  ls -l "$h"
  udevadm info -q property -n "$h" | /usr/bin/grep -E 'HID_NAME|ID_VENDOR_ID|ID_MODEL_ID|ID_SERIAL|DEVNAME|HID_ID' || true
done
