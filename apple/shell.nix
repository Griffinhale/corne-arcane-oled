# A Swift toolchain that can build this package on Linux.
#
# nixpkgs ships the compiler and SwiftPM but does not wire the corelibs onto
# the runtime path, and SwiftPM drives `cc`, which in a plain shell is gcc and
# does not understand -fblocks or -fmodule-name. Both are one line each, and
# neither is discoverable from the error it produces, so they live here.
#
# There is no iOS SDK on Linux and there never will be: this shell builds and
# runs CityKit and city-check, which is the whole of what is portable. The app
# and the widget need Xcode.
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    swift
    swiftpm
    swiftPackages.Dispatch
    swiftPackages.Foundation
    clang
    # The parity leg renders the native reference first, which is the desktop
    # library through its own binding, so the whole of 
    # runs inside this one shell.
    python3
    gcc
    gnumake
  ];

  shellHook = ''
    export CC=clang CXX=clang++
    export LD_LIBRARY_PATH=${pkgs.swiftPackages.Dispatch}/lib:${pkgs.swiftPackages.Foundation}/lib:$LD_LIBRARY_PATH
    echo "swift $(swift --version 2>/dev/null | head -1 | sed 's/.*version //') — swift build, swift run city-check"
  '';
}
