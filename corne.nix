{ config, pkgs, lib, ... }:

# Corne v3 (RP2040) QMK/Vial toolchain, flashing access, and the M10
# privacy-redacted notification/focus host service. Import this file directly from the
# checkout: package sources are resolved relative to it.

let
  cfg = config.services.corne-arcane-host;
  corneArcaneHost = pkgs.callPackage ./host/package.nix { };
in
{
  options.services.corne-arcane-host = {
    enable = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Run the Corne Arcane focus and notification daemon in Plasma sessions.";
    };
    desktopNotifications = lib.mkOption {
      type = lib.types.bool;
      default = true;
      description = "Mirror privacy-redacted Freedesktop notification metadata.";
    };
  };

  config = {
    environment.systemPackages = with pkgs; [
      qmk        # qmk CLI (compile / flash); pulls python + build deps
      vial       # Vial GUI for the stable griffin/griffin_anim builds
      dfu-util   # generic DFU flashing fallback
    ] ++ lib.optional cfg.enable corneArcaneHost;

    # Non-root flashing of QMK bootloaders: installs qmk-udev-rules and creates
    # the plugdev group. Covers the RP2040 RPI-RP2 bootloader this board enters.
    hardware.keyboard.qmk.enable = true;

    # Let Vial or the daemon open the running keyboard's hidraw node without
    # root. QMK Raw HID uses usage page 0xFF60; uaccess grants the active user.
    services.udev.extraRules = ''
      KERNEL=="hidraw*", ATTRS{idVendor}=="4653", MODE="0660", TAG+="uaccess"
    '';

    systemd.user.services.corne-arcane-host = lib.mkIf cfg.enable {
      description = "Corne Arcane focus, notification policy, and Raw HID heartbeat";
      wantedBy = [ "graphical-session.target" ];
      partOf = [ "graphical-session.target" ];
      after = [ "graphical-session-pre.target" ];
      serviceConfig = {
        Type = "simple";
        ExecStart = "${lib.getExe corneArcaneHost}${lib.optionalString (!cfg.desktopNotifications) " --no-desktop-notifications"}";
        Restart = "always";
        RestartSec = 2;
      };
    };
  };
}
