{ lib, stdenv, makeWrapper, python3, vial, systemd }:

let
  # Tk draws the city app; PyGObject is the bus.
  pythonEnv = python3.withPackages (ps: [ ps.pygobject3 ps.tkinter ]);
in
stdenv.mkDerivation {
  pname = "corne-arcane-host";
  version = (lib.importTOML ./pyproject.toml).project.version;
  # host/ plus what the city app's native library compiles: desktop/ and the
  # simulation it shares with the firmware.
  src = lib.fileset.toSource {
    root = ../.;
    # A library built in the checkout is left out, so the package builds its own.
    fileset = lib.fileset.difference (lib.fileset.unions [
      ./.
      ../desktop
      ../firmware/sim
      ../firmware/sim_sources.mk
    ]) (lib.fileset.maybeMissing ../desktop/libcornearcane.so);
  };

  nativeBuildInputs = [ makeWrapper ];
  nativeCheckInputs = [ python3 ];
  doCheck = true;

  # The install layout lives in ./Makefile so that this derivation and debian/
  # cannot drift apart. Everything Nix-specific is the variables below plus the
  # environment wrapping in postInstall.
  buildPhase = ''
    runHook preBuild
    make -C desktop
    runHook postBuild
  '';

  # The tests inject fake Gio objects, so the check phase needs no PyGObject.
  checkPhase = ''
    runHook preCheck
    cd host
    PYTHONDONTWRITEBYTECODE=1 ${python3}/bin/python -m unittest discover -s tests -v
    PYTHONPYCACHEPREFIX="$TMPDIR/corne-arcane-pycache" \
      ${python3}/bin/python -m compileall -q arcane_host tests
    cd ..
    runHook postCheck
  '';

  installPhase = ''
    runHook preInstall
    make -C host install \
      PREFIX=$out \
      PYTHON=${pythonEnv}/bin/python \
      CITY_LIB=../desktop/libcornearcane.so
    test -f $out/lib/corne-arcane-host/arcane_host/libcornearcane.so
    runHook postInstall
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
    description = "Corne Arcane OLED host semantics, desktop city app and safe Vial handoff";
    license = lib.licenses.gpl2Only;
    platforms = lib.platforms.linux;
    mainProgram = "corne-arcane-host";
  };
}
