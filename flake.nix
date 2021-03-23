{
  description = "mfsync: multicast filesharing for the commandline";

  inputs.utils.url = "github:numtide/flake-utils";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, utils }:

    utils.lib.eachSystem [ "aarch64-linux" "i686-linux" "x86_64-linux" ]
      (system:
      let 
        pkgs = nixpkgs.legacyPackages.${system};
      in
        {
          devShell = import ./default.nix {
            inherit pkgs;
            stdenv = pkgs.overrideCC pkgs.stdenv pkgs.gcc10;
            boost = pkgs.boost174;
          };

          packages.mfsync = import ./derivation.nix {
            pkgs = pkgs;
            stdenv = pkgs.overrideCC pkgs.stdenv pkgs.gcc10;
            boost = pkgs.boost174;
          };

          defaultPackage = self.packages.${system}.mfsync;
          hydraJobs.mfsync = self.packages.${system}.mfsync;
        }
      );
}
