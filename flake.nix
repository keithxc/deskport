{
  description = "DeskPort — remote desktop development client based on Moonlight";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/93108a538f079596c9a16c72cf03e9322782b6dd";

  inputs.deskport-core = { url = "github:keithxc/deskport-core/e71b21808e15c8bcd55d4af3f7c0cbc769ef0bcb"; flake = false; };

  outputs = { self, nixpkgs, deskport-core }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      packageFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          sessionHost = pkgs.sunshine.overrideAttrs (old: let
            ffmpegFlag = "-DFFMPEG_PREPARED_BINARIES:STRING=";
            ffmpegFlags = builtins.filter (pkgs.lib.hasPrefix ffmpegFlag) old.cmakeFlags;
            ffmpegPrebuilt = assert builtins.length ffmpegFlags == 1;
              pkgs.lib.removePrefix ffmpegFlag (builtins.head ffmpegFlags);
            fixedFFmpeg = import ./host/linux/ffmpeg-vulkan-cbs.nix {
              inherit pkgs;
              prebuilt = ffmpegPrebuilt;
            };
          in {
            cmakeFlags = map (flag:
              if pkgs.lib.hasPrefix ffmpegFlag flag
              then "${ffmpegFlag}${fixedFFmpeg}" else flag
            ) old.cmakeFlags;
            # Keep the distribution's existing host revision and build recipe,
            # but obtain its source from this repository instead of upstream.
            src = pkgs.runCommand "deskport-sunshine-vendored-source" {} ''
              mkdir -p "$out"
              tar -xzf ${./host/vendor/sunshine-nix.tar.gz} --strip-components=1 -C "$out"
            '';
            nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.python3 pkgs.git ];
            postPatch = (old.postPatch or "") + ''
              python3 ${./scripts/patch-host-diagnostics.py} .
              python3 ${./scripts/patch-host-network.py} .
              python3 ${./scripts/patch-host-smart-stream.py} .
              python3 ${./scripts/patch-host-session-settings.py} .
              python3 ${./scripts/patch-host-session-takeover.py} .
              python3 ${./scripts/patch-host-linux-display.py} .
              mkdir -p src/deskport/common
              cp ${./host/common/smartstream.h} src/deskport/common/smartstream.h
              cp ${./host/common/inputactivity.h} src/deskport/common/inputactivity.h
              cp ${./host/common/framecadence.h} src/deskport/common/framecadence.h
              python3 ${./scripts/patch-host-input-activity.py} .
              python3 ${./scripts/patch-host-sync-cadence.py} .
              python3 ${./scripts/patch-host-linux-cadence.py} .
              python3 ${./scripts/patch-host-pipewire-memory.py} .
              python3 ${./scripts/patch-host-display-ownership.py} .
              python3 ${./scripts/patch-host-memory-diagnostics.py} . ${./host/common/memorydiagnostics.h}
              cp ${./host/common/encoderpolicy.h} src/deskport/common/encoderpolicy.h
              python3 ${./scripts/patch-host-encoder-policy.py} .
            '';
          });
        in pkgs.moonlight-qt.overrideAttrs (old: {
          pname = "deskport";
          buildInputs = (old.buildInputs or []) ++ [ pkgs.wayland pkgs.pipewire ];
          version = "0.6.1";
          src = pkgs.lib.cleanSourceWith {
            src = pkgs.lib.cleanSource self;
            # Documentation, CI edits and the vendored macOS prebuilts do not
            # change the Linux client binary.
            filter = path: type: !(builtins.elem (baseNameOf path) [
              ".github" "docs" "AGENTS.md" "README.md"
              "flake.nix" "flake.lock" "libs"
            ]);
          };
          # Every third-party dependency is vendored in this repository; only the
          # shared core is still a submodule, so it comes from its own input.
          postUnpack = ''
            mkdir -p "$sourceRoot/shared/deskport-core"
            cp -R --no-preserve=mode ${deskport-core}/. "$sourceRoot/shared/deskport-core/"
          '';
          nativeBuildInputs = (old.nativeBuildInputs or []) ++ [ pkgs.python3 pkgs.qt6.qttools ];
          preBuild = (old.preBuild or "") + ''
            python3 shared/deskport-core/tests/test_workspace.py --qt-header app/backend/workspaceresolution.h
          '';
          postInstall = (old.postInstall or "") + ''
            mkdir -p "$out/libexec"
            if [ -f TEST_BUILD_ID ]; then
              mkdir -p "$out/share/deskport"
              cp TEST_BUILD_ID "$out/share/deskport/TEST_BUILD_ID"
            fi
            ln -s ${sessionHost}/bin/sunshine "$out/libexec/deskport-host"
            cat > "$out/share/applications/io.github.keithxc.DeskPort.display.desktop" <<EOF
            [Desktop Entry]
            Type=Application
            Name=DeskPort virtual display permission
            Exec=$out/libexec/deskport-display
            NoDisplay=true
            X-KDE-Wayland-Interfaces=zkde_screencast_unstable_v1
            EOF
          '';
          postFixup = (old.postFixup or "") + ''
            # NixOS KWin unwraps executable paths; upstream KWin uses /proc/pid/exe.
            # Provide an exact entry for each, without disabling permission checks.
            cp "$out/share/applications/io.github.keithxc.DeskPort.display.desktop" \
              "$out/share/applications/io.github.keithxc.DeskPort.display-native.desktop"
            substituteInPlace "$out/share/applications/io.github.keithxc.DeskPort.display-native.desktop" \
              --replace-fail "$out/libexec/deskport-display" "$out/libexec/.deskport-display-wrapped"
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
