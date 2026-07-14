{ lib, stdenvNoCC, makeWrapper, python3 }:

let
  pythonEnv = python3.withPackages (ps: [ ps.pygobject3 ]);
in
stdenvNoCC.mkDerivation {
  pname = "corne-arcane-host";
  version = "0.11.5";
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
      "$out/share/systemd/user"
    cp -r arcane_host "$out/lib/corne-arcane-host/"

    mkdir -p "$out/share/kwin/scripts/cornearcane"
    cp -r kwin/contents kwin/metadata.json "$out/share/kwin/scripts/cornearcane/"
    cp zsh/corne-arcane.zsh "$out/share/corne-arcane/zsh/"
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
    runHook postInstall
  '';

  meta = {
    description = "Privacy-redacted notification and focus daemon for Corne Arcane OLED";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "corne-arcane-host";
  };
}
