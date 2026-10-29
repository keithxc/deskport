{
  description = "DeskPort — remote desktop development client based on Moonlight";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/93108a538f079596c9a16c72cf03e9322782b6dd";

  inputs.deskport-core = { url = "github:keithxc/deskport-core/4064059f51d41bcd0b460b5353be57554e361bd6"; flake = false; };

  outputs = { self, nixpkgs, deskport-core }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = nixpkgs.lib.genAttrs systems;
      packageFor = system: memoryDiagnostics:
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
            NIX_CFLAGS_COMPILE = (old.NIX_CFLAGS_COMPILE or "") +
              pkgs.lib.optionalString memoryDiagnostics " -DDESKPORT_ENABLE_MEMORY_DIAGNOSTICS=1";
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
            buildInputs = (old.buildInputs or []) ++ [ pkgs.vulkan-headers pkgs.libdatachannel ];
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
              python3 ${./scripts/patch-host-egl-query-lifetime.py} .
              python3 ${./scripts/patch-host-vulkan-lifetime.py} . ${./host/linux/vulkan-driver-lifetime.h}
              python3 ${./scripts/patch-host-memory-diagnostics.py} . ${./host/common/memorydiagnostics.h}
              cp ${./host/common/encoderpolicy.h} src/deskport/common/encoderpolicy.h
              python3 ${./scripts/patch-host-encoder-policy.py} .
              cp ${./host/common/ratecontrol.h} src/deskport/common/ratecontrol.h
              python3 ${./scripts/patch-host-remote-rate.py} .
              python3 ${./scripts/patch-host-data-saver.py} .
              python3 ${./scripts/patch-host-video-pause.py} .
              python3 ${./scripts/patch-host-rate-api.py} .
              python3 ${./scripts/patch-host-browser.py} . ${./host/browser}
            '';
          });
        in pkgs.moonlight-qt.overrideAttrs (old: {
          pname = "deskport";
          buildInputs = (old.buildInputs or []) ++ [ pkgs.wayland pkgs.pipewire pkgs.qt6.qtwayland ];
          version = "0.7.2-rc.2";
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
      # Experimental Seamless sidecar on its own, for native Linux debugging
      # without building the Moonlight client or the patched session host.
      seamlessHostFor = system:
        let pkgs = import nixpkgs { inherit system; };
        in pkgs.stdenv.mkDerivation {
          pname = "deskport-seamless-host";
          version = "0.0.0-m3";
          src = pkgs.lib.fileset.toSource {
            root = ./.;
            fileset = pkgs.lib.fileset.unions [ ./host/linux/seamless ./app/seamless ];
          };
          nativeBuildInputs = [ pkgs.qt6.qtbase pkgs.qt6.wrapQtAppsHook ];
          buildInputs = [ pkgs.qt6.qtbase pkgs.qt6.qtwayland pkgs.wayland ];
          configurePhase = ''
            runHook preConfigure
            qmake PREFIX=$out host/linux/seamless/seamless.pro
            runHook postConfigure
          '';
          meta.platforms = systems;
        };
      # Debug server bundle: the sidecar plus a small launcher catalog of
      # software-rendered (wl_shm) applications. weston-terminal binds lower
      # wl_seat versions; foot requires wl_seat v5, which QtWaylandCompositor
      # (hard-coded v4) does not provide. The GTK 3 apps exercise xdg_popup.
      seamlessLabServerFor = system:
        let
          pkgs = import nixpkgs { inherit system; };
          host = seamlessHostFor system;
          apps = {
            terminal = "${pkgs.weston}/bin/weston-terminal";
            editor = "${pkgs.mousepad}/bin/mousepad";
            calculator = "${pkgs.galculator}/bin/galculator";
          };
          appArgs = pkgs.lib.concatStringsSep " "
            (pkgs.lib.mapAttrsToList (id: program: "--app ${id}=${program}") apps);
        in pkgs.writeShellScriptBin "seamless-lab-server" ''
          export PATH=${pkgs.lib.makeBinPath [ pkgs.weston ]}:$PATH
          # The launcher catalog is stream-only; --self-test runs without it.
          for argument in "$@"; do
            [ "$argument" = -- ] && break
            if [ "$argument" = --stream ]; then
              exec ${host}/libexec/deskport-seamless-host ${appArgs} "$@"
            fi
          done
          exec ${host}/libexec/deskport-seamless-host "$@"
        '';
    in {
      packages = forAllSystems (system: {
        default = packageFor system false;
        diagnostics = packageFor system true;
        deskport = self.packages.${system}.default;
        seamless-host = seamlessHostFor system;
        seamless-lab-server = seamlessLabServerFor system;
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
                cmake pkg-config python3 git gnumake nodejs openssl libopus miniupnpc icu boost libdatachannel
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
                export DESKPORT_CMAKE_PREFIX_PATH="${pkgs.openssl.dev};${pkgs.openssl.out};${pkgs.libopus.dev};${pkgs.libopus};${pkgs.miniupnpc};${pkgs.icu.dev};${pkgs.icu};${pkgs.boost.dev};${pkgs.boost};${pkgs.libdatachannel}"
              '';
            };
        };
    };
}
