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
          version = "0.1.11";
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
        }; }) // {
          aarch64-darwin.default =
            let
              pkgs = import nixpkgs { system = "aarch64-darwin"; };
            in pkgs.mkShellNoCC {
              packages = with pkgs; [
                qt6.qtbase qt6.qtdeclarative qt6.qtshadertools qt6.qtsvg qt6.qttools
                cmake pkg-config python3 git gnumake nodejs openssl libopus miniupnpc icu boost
              ];
              # Apple SDK/compiler and signing use the installed Xcode/Aqua
              # session. Third-party tools and libraries come from this lock.
              shellHook = ''
                export DEVELOPER_DIR="''${DESKPORT_DEVELOPER_DIR:-/Applications/Xcode.app/Contents/Developer}"
                export DESKPORT_NIX_DEPS=1
                export PATH="${pkgs.lib.makeBinPath [ pkgs.qt6.qtbase pkgs.qt6.qttools pkgs.cmake pkgs.pkg-config pkgs.python3 pkgs.git pkgs.gnumake pkgs.nodejs ]}:/usr/bin:/bin:/usr/sbin:/sbin:/run/current-system/sw/bin:/nix/var/nix/profiles/default/bin"
                export DESKPORT_QT_BIN=${pkgs.qt6.qtbase}/bin
                export DESKPORT_QML_IMPORT_PATH=${pkgs.qt6.qtdeclarative}/lib/qt-6/qml
                export DESKPORT_QML_CACHEGEN=${pkgs.qt6.qtdeclarative}/libexec/qmlcachegen
                export DESKPORT_QT_PLUGIN_PATH="${pkgs.qt6.qtbase}/lib/qt-6/plugins:${pkgs.qt6.qtsvg}/lib/qt-6/plugins"
                export QML_IMPORT_PATH="$DESKPORT_QML_IMPORT_PATH"
                export QT_PLUGIN_PATH="$DESKPORT_QT_PLUGIN_PATH"
                export DESKPORT_QML_SCANNER=${pkgs.qt6.qtdeclarative}/libexec/qmlimportscanner
                export DESKPORT_OPENSSL_ROOT=${pkgs.openssl.dev}
                export DESKPORT_OPUS_ROOT=${pkgs.libopus.dev}
                export DESKPORT_ICU_ROOT=${pkgs.icu.dev}
                export DESKPORT_CMAKE_PREFIX_PATH="${pkgs.openssl.dev};${pkgs.openssl.out};${pkgs.libopus.dev};${pkgs.libopus};${pkgs.miniupnpc};${pkgs.icu.dev};${pkgs.icu};${pkgs.boost.dev};${pkgs.boost}"
              '';
            };
        };
    };
}
