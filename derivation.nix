{ pkgs, stdenv, boost, sqlite_modern_cpp }:

stdenv.mkDerivation {
  name = "mdump";
  src = ./.;

  enableParallelBuilding = true;

  nativeBuildInputs = [ sqlite_modern_cpp pkgs.pkgconfig pkgs.cmake pkgs.gnumake42 ];
  depsBuildBuild = [ ];
  buildInputs = [ pkgs.spdlog pkgs.sqlite boost pkgs.boost-build pkgs.doxygen pkgs.catch2 ];

  installPhase = ''
    mkdir -p $out/bin
    cp mdump $out/bin/
  '';
}
