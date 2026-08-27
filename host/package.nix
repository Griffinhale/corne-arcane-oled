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

  # The install layout lives in ./Makefile so that this derivation and debian/
  # cannot drift apart. Everything Nix-specific is the three variables below plus
  # the environment wrapping in postInstall.
  dontBuild = true;

  makeFlags = [
    "PREFIX=${placeholder "out"}"
    "PYTHON=${pythonEnv}/bin/python"
  ];

  # The tests inject fake Gio objects, so the check phase needs no PyGObject.
  checkPhase = ''
    runHook preCheck
    PYTHONDONTWRITEBYTECODE=1 ${python3}/bin/python -m unittest discover -s tests -v
    PYTHONPYCACHEPREFIX="$TMPDIR/corne-arcane-pycache" \
      ${python3}/bin/python -m compileall -q arcane_host tests
    runHook postCheck
  '';

  # Only the paths Nix pins differ from the portable defaults: the KWin script
  # lives in the store, Vial is deliberately absent from the system profile, and
  # systemctl is not otherwise on the daemon's PATH.
  postInstall = ''
    wrapProgram "$out/bin/corne-arcane-host" \
      --set CORNE_ARCANE_KWIN_SCRIPT \
        "$out/share/kwin/scripts/cornearcane/contents/code/main.js"
    wrapProgram "$out/bin/corne-arcane-vial" \
      --set CORNE_ARCANE_VIAL_BIN ${lib.escapeShellArg (lib.getExe vial)} \
      --set CORNE_ARCANE_SYSTEMCTL ${lib.escapeShellArg (lib.getExe' systemd "systemctl")}
  '';

  meta = {
    description = "Corne Arcane OLED host semantics and safe Vial handoff";
    license = lib.licenses.gpl2Only;
    platforms = lib.platforms.linux;
    mainProgram = "corne-arcane-host";
  };
}
