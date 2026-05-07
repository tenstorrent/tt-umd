{
  description = "Tenstorrent User Mode Driver (tt-umd)";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-25.11";
  };

  outputs =
    { self, nixpkgs }:
    let
      supportedSystems = [
        "x86_64-linux"
        "aarch64-linux"
      ];
      forAllSystems = nixpkgs.lib.genAttrs supportedSystems;
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};

          # Header-only deps that CPM normally downloads — we provide them as source trees.
          picosha2-src = pkgs.fetchFromGitHub {
            owner = "okdshin";
            repo = "PicoSHA2";
            rev = "v1.0.1";
            hash = "sha256-3psCzbrwR+vO9TyTKOx+gEaWuHDx6pSgLOQ3DqrJsnI=";
          };

          asio-src = pkgs.fetchFromGitHub {
            owner = "chriskohlhoff";
            repo = "asio";
            rev = "asio-1-30-2";
            hash = "sha256-g+ZPKBUhBGlgvce8uTkuR983unD2kbQKgoddko7x+fk=";
          };

          # tt-logger: header-only library from Tenstorrent, not in nixpkgs.
          tt-logger = pkgs.stdenv.mkDerivation {
            pname = "tt-logger";
            version = "1.1.8";

            src = pkgs.fetchFromGitHub {
              owner = "tenstorrent";
              repo = "tt-logger";
              rev = "v1.1.8";
              hash = "sha256-cAQLxxwxRkRh1hwDVyDPOr3wsiC2DStUP0UMSiY0vHg=";
            };

            nativeBuildInputs = [
              pkgs.cmake
              pkgs.ninja
            ];

            buildInputs = [
              pkgs.spdlog
              pkgs.fmt
            ];

            cmakeFlags = [
              (pkgs.lib.cmakeBool "TT_LOGGER_INSTALL" true)
            ];
          };
        in
        {
          default = pkgs.callPackage ./nix/package.nix {
            inherit picosha2-src asio-src tt-logger;
            inherit (pkgs.python3Packages) nanobind;
          };
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgs.legacyPackages.${system};
        in
        {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.default ];
            packages = [
              pkgs.clang-tools
              pkgs.gdb
            ];
          };
        }
      );
    };
}
