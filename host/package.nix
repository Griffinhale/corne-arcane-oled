{ lib, stdenvNoCC, makeWrapper, python3 }:

let
  pythonEnv = python3.withPackages (ps: [ ps.pygobject3 ]);
in
stdenvNoCC.mkDerivation {
  pname = "corne-arcane-host";
  version = "0.9.0";
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
    mkdir -p "$out/lib/corne-arcane-host" "$out/bin"
    cp -r arcane_host "$out/lib/corne-arcane-host/"

    mkdir -p "$out/share/kwin/scripts/cornearcane"
    cp -r kwin/contents kwin/metadata.json "$out/share/kwin/scripts/cornearcane/"

    makeWrapper ${pythonEnv}/bin/python "$out/bin/corne-arcane-host" \
      --add-flags "-m arcane_host.daemon" \
      --set PYTHONPATH "$out/lib/corne-arcane-host" \
      --set CORNE_ARCANE_KWIN_SCRIPT \
        "$out/share/kwin/scripts/cornearcane/contents/code/main.js"
    runHook postInstall
  '';

  meta = {
    description = "Application-aware semantic host daemon for Corne Arcane OLED";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux;
    mainProgram = "corne-arcane-host";
  };
}
