{ pkgs ? import <nixpkgs> {}, libindicators, stdenv, boost }:
with pkgs;

stdenv.mkDerivation {
  name = "mfsync";
  src = ./.;

  enableParallelBuilding = true;

  nativeBuildInputs = [ pkgs.pkgconfig pkgs.cmake pkgs.gnumake ];
  depsBuildBuild = [ ];
  buildInputs = [
    libindicators
    pkgs.catch2
    pkgs.spdlog
    pkgs.openssl
    boost
    pkgs.boost-build
    pkgs.doxygen
    pkgs.catch2
    pkgs.nlohmann_json
    pkgs.cryptopp
  ];

  cmakeFlags = [
    "-DBUILD_STATIC=On"
  ];

  installPhase = ''
    mkdir -p $out/bin
    cp mfsync $out/bin/
  '';
}
