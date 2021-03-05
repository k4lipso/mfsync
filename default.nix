{ pkgs ? import <nixpkgs> {}, stdenv, boost, sqlite_modern_cpp }:
with pkgs; 

stdenv.mkDerivation {
  name = "mdump";
  src = ./.;
  nativeBuildInputs = [ sqlite_modern_cpp pkgconfig cmake gnumake gdb ];
  depsBuildBuild = [ ];
  buildInputs = [ spdlog sqlite boost boost-build doxygen catch2 ];
}
