{ lib, stdenvNoCC, makeWrapper, python3, vial, systemd }:

let
  pythonEnv = python3.withPackages (ps: [ ps.pygobject3 ]);
in
stdenvNoCC.mkDerivation {
  pname = "corne-arcane-host";
  version = (lib.importTOML ./pyproject.toml).project.version;
  src = lib.cleanSource ./.;

  nativeBuildInputs = [ makeWrapper ];
  nativeCheckInputs = [ python3 ];
  doCheck = true;

  checkPhase = ''
    runHook preCheck
    PYTHONDONTWRITEBYTECODE=1 ${python3}/bin/python -m unittest discover -s tests -v
    PYTHONPYCACHEPREFIX="$TMPDIR/corne-arcane-pycache" \
      ${python3}/bin/python -m compileall -q arcane_host tests
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p \
      "$out/lib/corne-arcane-host" \
      "$out/bin" \
      "$out/share/corne-arcane/zsh" \
      "$out/share/corne-arcane/bash" \
      "$out/share/corne-arcane/fish/conf.d" \
      "$out/share/corne-arcane/firefox" \
      "$out/share/gnome-shell/extensions/corne-arcane-focus@griffinhale.github.io" \
      "$out/share/mozilla/native-messaging-hosts" \
      "$out/share/systemd/user" \
      "$out/share/applications"
    cp -r arcane_host "$out/lib/corne-arcane-host/"

    mkdir -p "$out/share/kwin/scripts/cornearcane"
    cp -r kwin/contents kwin/metadata.json "$out/share/kwin/scripts/cornearcane/"
    cp zsh/corne-arcane.zsh "$out/share/corne-arcane/zsh/"
    cp bash/corne-arcane.bash "$out/share/corne-arcane/bash/"
    cp fish/conf.d/corne-arcane.fish "$out/share/corne-arcane/fish/conf.d/"
    cp firefox/manifest.json firefox/background.js firefox/scroll.js \
      "$out/share/corne-arcane/firefox/"
    cp gnome/metadata.json gnome/extension.js \
      "$out/share/gnome-shell/extensions/corne-arcane-focus@griffinhale.github.io/"
    substitute firefox/io.github.griffinhale.corne_arcane.json.in \
      "$out/share/mozilla/native-messaging-hosts/io.github.griffinhale.corne_arcane.json" \
      --replace-fail '@out@' "$out"
    cp desktop/corne-arcane-vial.desktop "$out/share/applications/"
    substitute systemd/corne-arcane-host.service \
      "$out/share/systemd/user/corne-arcane-host.service" \
      --replace-fail '@out@' "$out"

    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-host" \
      --add-flags "-m arcane_host.daemon" \
      --set PYTHONPATH "$out/lib/corne-arcane-host" \
      --set CORNE_ARCANE_KWIN_SCRIPT \
        "$out/share/kwin/scripts/cornearcane/contents/code/main.js"
    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-event" \
      --add-flags "-m arcane_host.event" \
      --set PYTHONPATH "$out/lib/corne-arcane-host"
    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-diagnostics" \
      --add-flags "-m arcane_host.diagnostics" \
      --set PYTHONPATH "$out/lib/corne-arcane-host"
    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-browser-bridge" \
      --add-flags "-m arcane_host.browser_bridge" \
      --set PYTHONPATH "$out/lib/corne-arcane-host"
    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-vial" \
      --add-flags "-m arcane_host.vial_launcher" \
      --set PYTHONPATH "$out/lib/corne-arcane-host" \
      --set CORNE_ARCANE_VIAL_BIN ${lib.escapeShellArg (lib.getExe vial)} \
      --set CORNE_ARCANE_SYSTEMCTL ${lib.escapeShellArg (lib.getExe' systemd "systemctl")}
    runHook postInstall
  '';

  meta = {
    description = "Corne Arcane OLED host semantics and safe Vial handoff";
    license = lib.licenses.gpl2Only;
    platforms = lib.platforms.linux;
    mainProgram = "corne-arcane-host";
  };
}
