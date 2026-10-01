{ config, pkgs, lib, ... }:

# Corne (crkbd rev1, RP2040) QMK/Vial toolchain, flashing access, and the current
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
      default = false;
      description = "Install the Corne Arcane tools, udev access and focus and notification daemon. Nothing in this module applies until this is set.";
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
    firefoxBridge = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = ''
        Make the Firefox bridge's native host visible to Firefox.

        This adds the package to programs.firefox.nativeMessagingHosts, which
        only the NixOS Firefox wrapper reads, so it takes effect with
        programs.firefox.enable = true. The extension itself is unsigned and is
        loaded by hand; host/README.md says how.
      '';
    };
    trayIcon = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = ''
        Run the tray icon as a user service: link state, pause, Open Vial and
        the app. GNOME Shell shows it only with the AppIndicator extension
        (gnomeExtensions.appindicator).
      '';
    };
    x11FocusProducer = lib.mkOption {
      type = lib.types.bool;
      default = false;
      description = ''
        Run the plain-X11 focus producer as a user service.

        Leave this off under KWin or GNOME Shell, which report focus from
        inside the compositor. Turn it on for a session that has neither --
        Cinnamon, XFCE, i3, Plasma 5 -- where nothing otherwise calls
        ReportActiveWindow at all, focus never leaves its default, and every
        window presents as the same district no matter what is in front of you.

        The producer ships with the package either way; this only decides
        whether the unit is declared, because NixOS builds user units from
        module definitions rather than from the package's unit directory.
      '';
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = with pkgs; [
      qmk        # qmk CLI (compile / flash); pulls python + build deps
      dfu-util   # generic DFU flashing fallback
      gcc-arm-embedded # arm-none-eabi-size for make release-budget
      corneArcaneHost # daemon tools plus the exclusive-ownership Vial launcher
    ];

    # The shell hooks and the Firefox extension, at
    # /run/current-system/sw/share/corne-arcane/{zsh,bash,fish,firefox}. The
    # system profile links only the share/ subdirs some module asks for; Plasma 6
    # asks for all of share/, a plain or XFCE or i3 system does not.
    environment.pathsToLink = [ "/share/corne-arcane" ];

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

    programs.firefox.nativeMessagingHosts.packages =
      lib.mkIf cfg.firefoxBridge [ corneArcaneHost ];

    systemd.user.services.corne-arcane-host = {
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

    # Ordered after the daemon because that one is Type=dbus: systemd holds it
    # unstarted until it owns the bus name, so waiting means the first focus
    # report of a session lands instead of being dropped by an absent
    # destination. A later drop is harmless -- the producer swallows it and the
    # next focus change repairs the state -- but the first one would otherwise
    # sit wrong until the user happened to switch windows.
    systemd.user.services.corne-arcane-tray = lib.mkIf cfg.trayIcon {
      description = "Corne Arcane tray icon";
      wantedBy = [ "graphical-session.target" ];
      partOf = [ "graphical-session.target" ];
      after = [ "graphical-session.target" "corne-arcane-host.service" ];
      serviceConfig = {
        Type = "simple";
        ExecStart = "${corneArcaneHost}/bin/corne-arcane-tray";
        Restart = "on-failure";
        RestartSec = 2;
      };
    };

    systemd.user.services.corne-arcane-focus-x11 =
      lib.mkIf cfg.x11FocusProducer {
        description = "Corne Arcane X11 focus producer";
        wantedBy = [ "graphical-session.target" ];
        partOf = [ "graphical-session.target" ];
        after = [ "graphical-session-pre.target" "corne-arcane-host.service" ];
        # xprop is the entire implementation: one property read per focus
        # change, and none of the properties it can name carries a title.
        path = [ pkgs.xorg.xprop ];
        # A session that never exported DISPLAY has no X11 to watch.
        unitConfig.ConditionEnvironment = "DISPLAY";
        serviceConfig = {
          Type = "simple";
          ExecStart = "${corneArcaneHost}/bin/corne-arcane-focus-x11";
          Restart = "always";
          RestartSec = 2;
        };
      };
  };
}
