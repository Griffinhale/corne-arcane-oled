{ lib, stdenvNoCC, makeWrapper, python3, vial, systemd }:

let
  pythonEnv = python3.withPackages (ps: [ ps.pygobject3 ]);
in
stdenvNoCC.mkDerivation {
  pname = "corne-arcane-host";
  version = "0.4.0";
  src = lib.cleanSource ./.;

  nativeBuildInputs = [ makeWrapper ];
  nativeCheckInputs = [ pythonEnv ];
  doCheck = true;

  checkPhase = ''
    runHook preCheck
    PYTHONDONTWRITEBYTECODE=1 ${pythonEnv}/bin/python -m unittest discover -s tests -v
    PYTHONPYCACHEPREFIX="$TMPDIR/corne-arcane-pycache" \
      ${pythonEnv}/bin/python -m compileall -q arcane_host tests
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p \
      "$out/lib/corne-arcane-host" \
      "$out/bin" \
      "$out/share/corne-arcane/zsh" \
      "$out/share/systemd/user" \
      "$out/share/applications"
    cp -r arcane_host "$out/lib/corne-arcane-host/"

    mkdir -p "$out/share/kwin/scripts/cornearcane"
    cp -r kwin/contents kwin/metadata.json "$out/share/kwin/scripts/cornearcane/"
    cp zsh/corne-arcane.zsh "$out/share/corne-arcane/zsh/"
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
    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-vial" \
      --add-flags "-m arcane_host.vial_launcher" \
      --set PYTHONPATH "$out/lib/corne-arcane-host" \
      --set CORNE_ARCANE_VIAL_BIN ${lib.escapeShellArg (lib.getExe vial)} \
      --set CORNE_ARCANE_SYSTEMCTL ${lib.escapeShellArg (lib.getExe' systemd "systemctl")}
    runHook postInstall
  '';

  meta = {
    description = "Corne Arcane OLED host semantics and safe Vial handoff";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "corne-arcane-host";
  };
}
