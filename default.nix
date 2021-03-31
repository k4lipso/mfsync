{ pkgs ? import <nixpkgs> {}, stdenv, boost }:
with pkgs; 

stdenv.mkDerivation {
  name = "mfsync";
  src = ./.;
  nativeBuildInputs = [ pkgconfig cmake gnumake gdb ];
  depsBuildBuild = [ ];
  buildInputs = [ spdlog openssl boost boost-build doxygen catch2 ];
}
