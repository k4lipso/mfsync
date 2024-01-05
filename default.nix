{ pkgs ? import <nixpkgs> {}, libindicators, stdenv, boost }:
with pkgs; 

stdenv.mkDerivation {
  name = "mfsync";
  src = ./.;

  nativeBuildInputs = [ pkg-config cmake gnumake gdb clang clang-tools ];
  depsBuildBuild = [ ];
  buildInputs = [
    libindicators
    catch2
    spdlog
    openssl
    boost
    boost-build
    doxygen
    catch2
    nlohmann_json
    cryptopp
  ];
}
