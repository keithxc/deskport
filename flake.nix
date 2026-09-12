{
  description = "DeskPort — remote desktop development client based on Moonlight";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/93108a538f079596c9a16c72cf03e9322782b6dd";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      packageFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          # Supply the exact upstream gitlink contents even when the flake was
          # fetched without Git submodules. Application code comes from self.
          upstream = pkgs.fetchFromGitHub {
            owner = "moonlight-stream";
            repo = "moonlight-qt";
            rev = "f786e94c7b2f943e24e65d7d74deb539b827fc84";
            hash = "sha256-rWVNpfRDLrWsqELPFquA6rW6/AfWV+6DNLUCPqIhle0=";
            fetchSubmodules = true;
          };
        in pkgs.moonlight-qt.overrideAttrs (old: {
          pname = "deskport";
          version = "0.1.8";
          src = pkgs.lib.cleanSourceWith {
            src = pkgs.lib.cleanSource self;
            # Documentation and CI edits do not change the client binary.
            filter = path: type: !(builtins.elem (baseNameOf path) [
              ".github" "docs" "AGENTS.md" "README.md" "README.upstream.md"
              "flake.nix" "flake.lock"
            ]);
          };
          postUnpack = ''
            for dependency in \
              app/SDL_GameControllerDB \
              moonlight-common-c/moonlight-common-c \
              qmdnsengine/qmdnsengine \
              soundio/libsoundio \
              h264bitstream/h264bitstream \
              libs; do
              mkdir -p "$sourceRoot/$dependency"
              cp -R --no-preserve=mode ${upstream}/"$dependency"/. "$sourceRoot/$dependency/"
            done
          '';
          postInstall = (old.postInstall or "") + ''
            mkdir -p "$out/libexec"
            ln -s ${pkgs.sunshine}/bin/sunshine "$out/libexec/deskport-host"
          '';
          meta = old.meta // {
            description = "Experimental remote desktop development client based on Moonlight";
            homepage = "https://github.com/keithxc/deskport";
            changelog = "https://github.com/keithxc/deskport/blob/main/docs/ROADMAP.md";
            mainProgram = "deskport";
            platforms = systems;
            maintainers = [ ];
          };
          passthru = { };
        });
    in {
      packages = forAllSystems (system: {
        default = packageFor system;
        deskport = self.packages.${system}.default;
      });
      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/deskport";
        };
      });
      devShells = forAllSystems (system:
        let pkgs = import nixpkgs { inherit system; };
        in { default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = [ pkgs.git pkgs.python3 pkgs.desktop-file-utils ];
        }; });
    };
}
