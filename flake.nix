# The contributor shell: every tool the gates and the firmware build need.
#
#   nix develop                 make test, lint, hygiene, release-build, release-budget
#   nix develop .#apple         the Swift shell (apple/shell.nix), for make swift-parity
#
# Nothing here chooses a version. The ARM compiler comes from
# scripts/budget.env, ruff and clang-format from requirements-dev.txt, and the
# Vial-QMK revision from VIAL_QMK_REVISION; evaluation stops if the packages
# disagree with those files. CI does not use this shell.
{
  description = "Corne Arcane contributor shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs =
    { self, nixpkgs }:
    let
      lib = nixpkgs.lib;
      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      pin =
        file: pattern:
        let
          found = builtins.match pattern (builtins.readFile file);
        in
        if found == null then throw "flake.nix: no ${pattern} in ${toString file}" else builtins.head found;
      armGccVersion = pin ./scripts/budget.env ".*ARM_GCC_VERSION=([^[:space:]]+).*";
      ruffVersion = pin ./requirements-dev.txt ".*ruff==([^[:space:]]+).*";
      clangFormatVersion = pin ./requirements-dev.txt ".*clang-format==([^[:space:]]+).*";
      vialQmkRevision = lib.strings.trim (builtins.readFile ./VIAL_QMK_REVISION);

      # nixpkgs carries a newer ruff than the one lint is pinned to, and two
      # ruff releases can format the same file differently. Astral's static
      # build of the pinned release runs anywhere.
      ruffHashes = {
        "x86_64-linux" = "sha256-2ptci6enif47z2KH6ljMuskyinEbdnRoFwYQbnWAqDY=";
        "aarch64-linux" = "sha256-riL7O2rYXP9Zq/FH1XLSZjl/Qrc+UebVXbpW+zQw/h0=";
      };

      matches =
        what: have: want:
        lib.assertMsg (have == want) "flake.nix: nixpkgs has ${what} ${have}, the pin says ${want}";

      shellFor =
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
          arch = lib.head (lib.splitString "-" system);
          ruff = pkgs.stdenvNoCC.mkDerivation {
            pname = "ruff";
            version = ruffVersion;
            src = pkgs.fetchurl {
              url = "https://github.com/astral-sh/ruff/releases/download/${ruffVersion}/ruff-${arch}-unknown-linux-musl.tar.gz";
              hash = ruffHashes.${system};
            };
            installPhase = "install -Dm755 ruff $out/bin/ruff";
          };
          clangFormat = pkgs.llvmPackages_19.clang-tools;
          armGcc = pkgs.gcc-arm-embedded;
          # Only clang itself: the unwrapped package also ships a clang-format
          # of its own release, which must not shadow the pinned one.
          wasmClang = pkgs.llvmPackages.clang-unwrapped;
          python = pkgs.python3.withPackages (
            ps: with ps; [
              pygobject3
              pillow
              tkinter
            ]
          );
        in
        assert matches "gcc-arm-embedded" armGcc.version armGccVersion;
        assert matches "clang-format" clangFormat.version clangFormatVersion;
        pkgs.mkShell {
          packages = [
            ruff
            clangFormat
            armGcc
            python
            pkgs.qmk
            pkgs.gcc
            pkgs.gnumake
            pkgs.git
            pkgs.ripgrep
            pkgs.rsync
            pkgs.dbus
            pkgs.nodejs_22
            pkgs.lld
          ];

          shellHook = ''
            # make web-lib and web-parity: a clang that targets wasm32 (see docs/development.md).
            export CLANG=${wasmClang}/bin/clang

            qmk_root=''${QMK_ROOT:-$HOME/src/vial-qmk}
            qmk_rev=$(git -C "$qmk_root" rev-parse HEAD 2>/dev/null || true)
            if [ "$qmk_rev" = "${vialQmkRevision}" ]; then
              echo "Vial-QMK: $qmk_root is at VIAL_QMK_REVISION"
            else
              echo "Vial-QMK: $qmk_root is ''${qmk_rev:-not a checkout}, not ${vialQmkRevision}."
              echo "  make release-build needs it at that revision (docs/flashing.md, Building the image):"
              if [ -n "$qmk_rev" ]; then
                echo "    git -C $qmk_root fetch"
              else
                echo "    git clone --recurse-submodules https://github.com/vial-kb/vial-qmk $qmk_root"
              fi
              echo "    git -C $qmk_root checkout --detach ${vialQmkRevision}"
              echo "    git -C $qmk_root submodule update --init --recursive"
              echo "  or point QMK_ROOT at a checkout that is."
            fi
            echo "$(arm-none-eabi-gcc --version | head -1); ruff ${ruffVersion}; clang-format ${clangFormatVersion}"
          '';
        };
    in
    {
      devShells = lib.genAttrs systems (system: {
        default = shellFor system;
        apple = import ./apple/shell.nix { pkgs = nixpkgs.legacyPackages.${system}; };
      });
    };
}
