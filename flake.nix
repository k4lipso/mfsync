{
  description = "mdump: multicast filesharing for the commandline";

  inputs.utils.url = "github:numtide/flake-utils";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, utils }:

    utils.lib.eachSystem [ "aarch64-linux" "i686-linux" "x86_64-linux" ]
      (system:
      let 
        pkgs = nixpkgs.legacyPackages.${system};

        modern_sqlite = pkgs.stdenv.mkDerivation {
          name = "modern_sqlite_cpp";
          src = pkgs.fetchFromGitHub {
            owner = "SqliteModernCpp";
            repo = "sqlite_modern_cpp";
            rev = "v3.2";
            sha256 = "1g8kn3r55p6s17kbqj9rl121r24y5h7lh7sa7rsc2xq1bd97i07b";
          };

          buildInputs = [ pkgs.sqlite ];
        };
      in
        {
          devShell = import ./default.nix {
            inherit pkgs;
            stdenv = pkgs.overrideCC pkgs.stdenv pkgs.gcc10;
            boost = pkgs.boost174;
            sqlite_modern_cpp = modern_sqlite;
          };

          packages.mdump = import ./derivation.nix {
            pkgs = pkgs;
            stdenv = pkgs.overrideCC pkgs.stdenv pkgs.gcc10;
            boost = pkgs.boost174;
            sqlite_modern_cpp = modern_sqlite;
          };

          defaultPackage = self.packages.${system}.mdump;
          hydraJobs.mdump = self.packages.${system}.mdump;
        }
      );
}
