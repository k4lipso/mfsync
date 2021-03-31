{ pkgs, stdenv, boost }:

stdenv.mkDerivation {
  name = "mfsync";
  src = ./.;

  enableParallelBuilding = true;

  nativeBuildInputs = [ pkgs.pkgconfig pkgs.cmake pkgs.gnumake ];
  depsBuildBuild = [ ];
  buildInputs = [ pkgs.spdlog pkgs.openssl boost pkgs.boost-build pkgs.doxygen pkgs.catch2 ];

  installPhase = ''
    mkdir -p $out/bin
    cp mfsync $out/bin/
  '';
}
