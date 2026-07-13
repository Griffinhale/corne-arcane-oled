{ config, pkgs, lib, ... }:

# Corne v3 (RP2040) QMK/Vial toolchain, flashing access, and Vial hidraw access.
#
# INSTALL:
#   1. Copy this file to /etc/nixos/modules/corne.nix
#        sudo cp "/home/shared/work/corne qmk/corne.nix" /etc/nixos/modules/corne.nix
#   2. Add it to the imports list in /etc/nixos/configuration.nix, alongside the
#      other ./modules/*.nix entries:
#        ./modules/corne.nix
#   3. Apply:  rebuild        (your alias for `sudo nixos-rebuild switch`)
#
# NOTE: For this build session none of this is required — the dev shell in
# ~/src/vial-qmk/shell.nix provides qmk/python/arm-gcc user-scope, and flashing
# works via the udisks2 RPI-RP2 automount. This module makes it permanent and
# lets the Vial GUI reach the running keyboard without root.

{
  environment.systemPackages = with pkgs; [
    qmk        # qmk CLI (compile / flash); pulls python + build deps
    vial       # Vial GUI — replaces the Debian AppImage the notes assumed
    dfu-util   # generic DFU flashing fallback
  ];

  # Non-root flashing of QMK bootloaders: installs qmk-udev-rules and creates the
  # plugdev group. Covers the RP2040 RPI-RP2 bootloader this board enters.
  hardware.keyboard.qmk.enable = true;

  # Let the Vial app open the RUNNING keyboard's hidraw node without root.
  # The Corne enumerates as USB vendor 4653 ("foostan Corne"); QMK's Vial raw-hid
  # interface uses HID usage page 0xFF60. uaccess grants the active local user.
  services.udev.extraRules = ''
    KERNEL=="hidraw*", ATTRS{idVendor}=="4653", MODE="0660", TAG+="uaccess"
  '';

  # Optional: also put griffin in plugdev (uaccess usually suffices on its own).
  # users.users.griffin.extraGroups = [ "plugdev" ];
}
