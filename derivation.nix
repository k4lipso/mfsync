{ pkgs ? import <nixpkgs> {}, libindicators, stdenv, boost }:
with pkgs;

stdenv.mkDerivation {
  name = "mfsync";
  src = ./.;

  enableParallelBuilding = true;

  nativeBuildInputs = [ pkgs.pkgconfig pkgs.cmake pkgs.gnumake ];
  depsBuildBuild = [ ];
  buildInputs = [ libindicators pkgs.spdlog pkgs.openssl boost pkgs.boost-build pkgs.doxygen pkgs.catch2 ];

  installPhase = ''
    mkdir -p $out/bin
    cp mfsync $out/bin/
  '';
}
