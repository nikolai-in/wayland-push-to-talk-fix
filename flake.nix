{
  description = "Wayland-native push-to-talk bridge for Discord";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "wayland-push-to-talk";
          version = "0.1.0";
          src = ./.;

          nativeBuildInputs = [ pkgs.gnumake pkgs.pkg-config pkgs.gcc ];

          buildPhase = ''
            runHook preBuild
            make
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 push-to-talk $out/bin/push-to-talk
            install -Dm644 push-to-talk.desktop $out/share/applications/push-to-talk.desktop
            runHook postInstall
          '';
        };

        apps.default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/push-to-talk";
        };

        devShells.default = pkgs.mkShell {
          packages = [ pkgs.gnumake pkgs.gcc pkgs.pkg-config ];
        };
      });
}
