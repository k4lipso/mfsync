{
  description = "mfsync: multicast filesharing for the commandline";

  inputs.utils.url = "github:numtide/flake-utils";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, utils }:

    utils.lib.eachSystem [ "aarch64-linux" "i686-linux" "x86_64-linux" ]
      (system:
        let
        libindicators = with pkgs; stdenv.mkDerivation {
          name = "indicators";
          src = pkgs.fetchFromGitHub {
            owner = "p-ranav";
            repo = "indicators";
            rev = "a5bc05f32a9c719535054b7fa5306ce5c8d055d8";
            sha256 = "05n714i9d65m5nrfi2xibdpgzbp0b0x6i1n4r3mirz1ydaz9fvxd";
          };
          enableParallelBuilding = true;

          nativeBuildInputs = [ pkgs.pkgconfig pkgs.cmake pkgs.gnumake ];
          depsBuildBuild = [ ];
          buildInputs = [ ];

          installPhase = '''';
        };

        pkgs = nixpkgs.legacyPackages.${system};
      in
        {
          devShell = import ./default.nix {
            inherit pkgs;
            libindicators = libindicators;
            stdenv = pkgs.overrideCC pkgs.stdenv pkgs.gcc;
            boost = pkgs.boost174;
          };

          packages.mfsync = import ./derivation.nix {
            pkgs = pkgs;
            libindicators = libindicators;
            stdenv = pkgs.overrideCC pkgs.stdenv pkgs.gcc;
            boost = pkgs.boost174;
          };

          apps.mfsync = {
            type="app";
            program = "${self.packages.${system}.mfsync}/bin/mfsync";
          };

          defaultPackage = self.packages.${system}.mfsync;
          hydraJobs.mfsync = self.packages.${system}.mfsync;
        }
      );
}
