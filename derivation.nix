{ pkgs, stdenv, boost, sqlite_modern_cpp }:

stdenv.mkDerivation {
  name = "mfsync";
  src = ./.;

  enableParallelBuilding = true;

  nativeBuildInputs = [ sqlite_modern_cpp pkgs.pkgconfig pkgs.cmake pkgs.gnumake42 ];
  depsBuildBuild = [ ];
  buildInputs = [ pkgs.spdlog pkgs.sqlite pkgs.openssl boost pkgs.boost-build pkgs.doxygen pkgs.catch2 ];

  installPhase = ''
    mkdir -p $out/bin
    cp mfsync $out/bin/
  '';
}
