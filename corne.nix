{ config, pkgs, lib, ... }:

# Corne v3 (RP2040) QMK/Vial toolchain, flashing access, and the current
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
    pomodoroUnit = lib.mkOption {
      type = lib.types.nullOr lib.types.str;
      default = null;
      description = "Optional systemd user timer unit used for Focus/Pomodoro semantics.";
    };
    pomodoroDuration = lib.mkOption {
      type = lib.types.ints.positive;
      default = 1500;
      description = "Pomodoro duration in seconds used for Observatory quarter stages.";
    };
  };

  config = {
    environment.systemPackages = with pkgs; [
      qmk        # qmk CLI (compile / flash); pulls python + build deps
      dfu-util   # generic DFU flashing fallback
      corneArcaneHost # daemon tools plus the exclusive-ownership Vial launcher
    ];

    # Non-root flashing of QMK bootloaders: installs qmk-udev-rules and creates
    # the plugdev group. Covers the RP2040 RPI-RP2 bootloader this board enters.
    hardware.keyboard.qmk.enable = true;

    # Let Vial or the daemon open the running keyboard's hidraw node without
    # root. The rule ships with the package as 60-corne-arcane.rules rather than
    # being written inline: services.udev.extraRules lands in 99-local.rules,
    # which udev evaluates after the TAG=="uaccess" match in 73-seat-late.rules,
    # so an inline rule adds the tag too late to grant anything. Confirm with
    # `udevadm test /sys/class/hidraw/hidrawN` that the rule matches at 60 and
    # that 73-seat-late.rules then runs the uaccess builtin.
    services.udev.packages = [ corneArcaneHost ];

    systemd.user.services.corne-arcane-host = lib.mkIf cfg.enable {
      description = "Corne Arcane focus, notification policy, and Raw HID heartbeat";
      wantedBy = [ "graphical-session.target" ];
      partOf = [ "graphical-session.target" ];
      after = [ "graphical-session-pre.target" ];
      serviceConfig = {
        Type = "dbus";
        BusName = "io.github.Griffinhale.CorneArcane";
        ExecStart = "${lib.getExe corneArcaneHost} --pomodoro-duration ${toString cfg.pomodoroDuration}${lib.optionalString (!cfg.desktopNotifications) " --no-desktop-notifications"}${lib.optionalString (cfg.pomodoroUnit != null) " --pomodoro-unit ${lib.escapeShellArg cfg.pomodoroUnit}"}";
        Restart = "always";
        RestartSec = 2;
      };
    };
  };
}
