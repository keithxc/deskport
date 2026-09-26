#!/usr/bin/env python3
"""Test host supervision with fake children and loopback-only port reservations."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
qmake = os.environ.get("DESKPORT_QMAKE", "qmake")
with tempfile.TemporaryDirectory(prefix="deskport-lifecycle-") as temporary:
    work = Path(temporary)
    macos = work / "Contents/MacOS"
    helpers = work / "Contents/Helpers"
    macos.mkdir(parents=True)
    (helpers / "Sunshine.app/Contents/MacOS").mkdir(parents=True)
    interpreter = shutil.which("python3")
    display = helpers / "deskport-display"
    display.write_text(f"#!{interpreter}\n" + '''import json, os, select, sys, time
assert os.environ.get("DESKPORT_DISPLAY_ISOLATED") == "1"
assert os.environ.get("DESKPORT_DISPLAY_STATE_DIR", "").startswith("/")
assert int(os.environ.get("DESKPORT_DISPLAY_SERIAL", "0")) >= 0x80000000
if os.environ.get("DESKPORT_TEST_MODE") == "display-fail":
    sys.exit(4)
initial = {"displayId": 123, "outputName": "DeskPort-test", "width": int(sys.argv[1]), "height": int(sys.argv[2]), "scale": 1}
if os.environ.get("DESKPORT_TEST_MODE") == "gnome-display": initial.update(outputName="Meta-1", pipewireNode=42, pipewireSerial="142")
if os.environ.get("DESKPORT_TEST_MODE") == "gnome-idle": initial = dict(ready=True, active=False, gnome=True, outputName="DeskPort-pending")
print("display started private.example.net", file=sys.stderr, flush=True)
print(json.dumps(initial), flush=True)
if os.environ.get("DESKPORT_TEST_MODE") == "display-late-fail":
    select.select([sys.stdin], [], [], 1.5)
    sys.exit(4)
for line in sys.stdin:
    request = json.loads(line)
    if os.environ.get("DESKPORT_TEST_MODE") == "restore-slow" and not request.get("session", True): time.sleep(0.8)
    if os.environ.get("DESKPORT_TEST_MODE") == "restore-reject" and not request.get("session", True):
        time.sleep(0.2)
        request["error"] = "Local layout recovery is pending"
    if os.environ.get("DESKPORT_TEST_MODE") == "resize-timeout": continue
    if os.environ.get("DESKPORT_TEST_MODE") == "resize-reject": request["error"] = "Mode rejected"
    request["displayId"] = 123
    if os.environ.get("DESKPORT_TEST_MODE") in ("gnome-display", "gnome-idle"):
        request["active"] = request.get("session", True)
        if request["active"]: request.update(outputName="Meta-2", pipewireNode=43, pipewireSerial="143")
    print(json.dumps(request), flush=True)
''')
    host = helpers / "Sunshine.app/Contents/MacOS/Sunshine"
    host.write_text(f"#!{interpreter}\n" + '''import os, pathlib, signal, sys, time
state = pathlib.Path(sys.argv[1]).parent
mode = os.environ.get("DESKPORT_TEST_MODE")
if sys.platform == "darwin" and os.environ.get("DESKPORT_CAPTURE_DISPLAY") != "123":
    sys.exit(18)  # Both credentials and server must target the helper display.
if "--creds" in sys.argv:
    (state / "auth-started").touch()
    if mode == "auth-fail": sys.exit(3)
    if mode == "auth-gated":
        deadline = time.monotonic() + 4
        while not (state / "auth-release").exists():
            if time.monotonic() >= deadline:
                (state / "auth-gate-timed-out").touch()
                sys.exit(19)
            time.sleep(0.01)
    else:
        time.sleep(10 if mode == "auth-slow" else 0.4)
    sys.exit(0)
if mode == "host-fail": sys.exit(7)
if mode == "host-crash-once" and not (state / "crashed-once").exists():
    (state / "crashed-once").touch()
    sys.exit(7)
if mode == "stubborn": signal.signal(signal.SIGTERM, signal.SIG_IGN)
(state / "host-started").touch()
print("Connection started 192.0.2.99 token=fixture-secret", flush=True)
# Loopback-only, certificate-pinned management fixture; never a personal host.
if (state / "credentials/cert.pem").exists():
    import base64, http.server, json, ssl
    config = dict(line.split(" = ", 1) for line in pathlib.Path(sys.argv[1]).read_text().splitlines() if " = " in line)
    session_file = state / "test-sessions.json"
    session_file.write_text(json.dumps(dict(generation=0, sessions=0, lease="")))
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass
        def handle_session(self):
            expected = "Basic " + base64.b64encode(("deskport:" + (state / "control-secret").read_text().strip()).encode()).decode()
            if self.path not in ("/api/deskport/sessions", "/api/deskport/trust") or self.headers.get("Authorization") != expected:
                self.send_error(403); return
            if self.path == "/api/deskport/trust":
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                output = dict(status=not (state / "reject-live-trust").exists(), version=1)
                if output["status"]:
                    stored = json.loads((state / "state.json").read_text())
                    devices = stored["root"].get("named_devices", []) or []
                    devices = [item for item in devices if item["uuid"] != body["uuid"] and item.get("cert") != body["cert"]]
                    devices.append(dict(body, enabled="true"))
                    stored["root"]["named_devices"] = devices
                    (state / "state.json").write_text(json.dumps(stored))
                data = json.dumps(output).encode()
                self.send_response(200); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
                return
            current = json.loads(session_file.read_text())
            def snapshot(): return str(current["generation"]) + ":" + str(current["sessions"])
            output = dict(status=True, version=1)
            if self.command == "POST":
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                if body.get("action") == "release":
                    if current["lease"] == body.get("lease"):
                        current.update(lease="", generation=current["generation"] + 1)
                elif body.get("action") == "acquire":
                    if body.get("snapshot") != snapshot(): output.update(status=False, code="stale")
                    elif not body.get("takeover") and (current["sessions"] or current["lease"]): output.update(status=False, code="busy")
                    elif current.get("fail"): output.update(status=False, code="unavailable")
                    else:
                        current.update(lease=body["lease"], sessions=0, generation=current["generation"] + 1)
                        current["takeovers"] = current.get("takeovers", 0) + int(body.get("takeover", False))
                session_file.write_text(json.dumps(current))
            output.update(snapshot=snapshot(), sessions=current["sessions"], reserved=bool(current["lease"]))
            data = json.dumps(output).encode()
            self.send_response(200); self.send_header("Content-Length", str(len(data))); self.end_headers(); self.wfile.write(data)
        do_GET = do_POST = handle_session
    server = http.server.HTTPServer(("127.0.0.1", int(config["port"]) + 1), Handler)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(state / "credentials/cert.pem", state / "credentials/key.pem")
    server.socket = tls.wrap_socket(server.socket, server_side=True)
    server.serve_forever()
else:
    while True: time.sleep(1)
''')
    if sys.platform != "darwin":
        linux_host = work / "Contents/libexec/deskport-host"
        linux_host.parent.mkdir(parents=True)
        linux_host.write_text(host.read_text())
        linux_host.chmod(0o700)
        linux_display = linux_host.parent / "deskport-display"
        linux_display.write_text(display.read_text())
        linux_display.chmod(0o700)
    icons = ["os/apple.svg", "os/windows.svg", "os/nixos.svg", "os/ubuntu.svg", "os/debian.svg", "os/fedora.svg", "os/arch.svg", "os/linux.svg", "os/computer.svg", "baseline-help_outline-24px.svg", "baseline-error_outline-24px.svg", "deskport.svg", "edit-square.svg", "refresh.svg", "fullscreen-exit.svg", "devices-grid.svg", "share-screen.svg", "manual.svg", "settings.svg", "done.svg", "add-device.svg", "add-group.svg", "deskport-tray-black.svg", "deskport-tray-white.svg"]
    (work / "test-resources.qrc").write_text('<RCC><qresource prefix="/res">' + ''.join(
        f'<file alias="{name}">{root}/app/res/{name}</file>' for name in icons) + '</qresource></RCC>')
    if "--ui" in sys.argv:
        resources = work / "test-resources.qrc"
        manual = root / "shared/deskport-core/manual/manual.json"
        resources.write_text(resources.read_text().replace('</RCC>', '<qresource prefix="/gui">' + ''.join(
            f'<file alias="{p.name}">{p}</file>' for p in (root / "app/gui").glob("*.qml"))
            + f'</qresource><qresource prefix="/manual"><file alias="manual.json">{manual}</file></qresource></RCC>'))
    for executable in (display, host):
        executable.chmod(0o700)
    binding = "--binding" in sys.argv or "--ui" in sys.argv or "--clipboard" in sys.argv
    extra_sources = f'"{root}/app/backend/peermanager.cpp" "{root}/app/backend/adaptivedisplay.cpp"' if binding else ""
    extra_headers = f'"{root}/app/backend/peermanager.h"' if binding else ""
    if "--ui" in sys.argv:
        extra_sources += f' "{root}/app/gui/hostlayout.cpp" "{root}/app/gui/manual.cpp"'
        extra_headers += f' "{root}/app/gui/manual.h"'
    if "--clipboard" in sys.argv:
        extra_sources += f' "{root}/app/backend/clipboardchannel.cpp" "{root}/app/streaming/clipboardsync.cpp"'
    suite = "service" if "--service" in sys.argv else "clipboard" if "--clipboard" in sys.argv else "ui-pages" if "--ui" in sys.argv else "peer-binding" if binding else "host-lifecycle"
    project = work / "tests.pro"
    project.write_text(f'''QT += core gui widgets network testlib qml quick quickcontrols2
linux: QT += dbus
CONFIG += console c++17 testcase
CONFIG -= app_bundle
TARGET = host-lifecycle-tests
DESTDIR = "{macos}"
SOURCES += "{root}/tests/{suite}.cpp" "{root}/app/backend/hostmanager.cpp" "{root}/app/backend/diagnostics.cpp" "{root}/app/backend/nvaddress.cpp" {extra_sources}
HEADERS += "{root}/app/backend/diagnostics.h" "{root}/app/backend/hostmanager.h" {extra_headers}
INCLUDEPATH += "{root}/app/backend" "{root}/app" "{root}/app/gui"
RESOURCES += "{work}/test-resources.qrc"
macx {{
    OBJECTIVE_SOURCES += "{root}/app/backend/macpermissions.mm" "{root}/app/backend/macdock.mm" "{root}/app/backend/macclipboard.mm" "{root}/app/backend/macunattended.mm"
    LIBS += -framework CoreGraphics -framework AVFoundation -framework ApplicationServices -framework AppKit -framework ServiceManagement
}}
''')
    if "--clipboard" in sys.argv:
        with project.open("a") as f:
            if sys.platform == "darwin":
                sdl = root / "libs/mac/Frameworks"
                f.write(f'\nQMAKE_CXXFLAGS += -F"{sdl}"\nINCLUDEPATH += "{sdl}/SDL2.framework/Versions/A/Headers"\nLIBS += -F"{sdl}" -framework SDL2\nQMAKE_RPATHDIR += "{sdl}"\n')
            else:
                f.write('\nCONFIG += link_pkgconfig\nPKGCONFIG += sdl2\n')
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_CONTROLS_STYLE="Material", QML_DISABLE_DISK_CACHE="1", XDG_CACHE_HOME=str(work / "cache"), XDG_CONFIG_HOME=str(work / "config"), XDG_DATA_HOME=str(work / "data"))
    if sys.platform != "darwin":
        environment["XDG_CURRENT_DESKTOP"] = "KDE"
        environment["WAYLAND_DISPLAY"] = "deskport-isolated-fake"
    if binding:
        environment["TEST_CORE_SESSION_CASES"] = str(root / "shared/deskport-core/protocol/session-cases.json")
        environment["TEST_CORE_DISPLAY_CASES"] = str(root / "shared/deskport-core/protocol/display-cases.json")
        environment["TEST_GUI_DIR"] = str(root / "app/gui")
        environment["TEST_BINDING_QML"] = str(root / "app/gui/BindingApproval.qml")
        for name in ("A", "B", "C"):
            cert, key = work / f"{name}.pem", work / f"{name}.key"
            subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2",
                "-subj", "/CN=NVIDIA GameStream Client", "-set_serial", "0", "-keyout", str(key), "-out", str(cert)],
                check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            environment[f"TEST_CERT_{name}"] = str(cert)
            environment[f"TEST_KEY_{name}"] = str(key)
        expired = work / "expired.pem"
        # Explicit x509 validity setters require newer OpenSSL; ca supports
        # fixed dates on older OpenSSL and LibreSSL versions as well.
        csr = work / "expired.csr"
        (work / "ca-index.txt").write_text("")
        (work / "ca-serial.txt").write_text("01\n")
        ca_config = work / "expired-ca.cnf"
        ca_config.write_text(f"""[ca]
default_ca = test_ca
[test_ca]
database = {work / 'ca-index.txt'}
serial = {work / 'ca-serial.txt'}
new_certs_dir = {work}
certificate = {work / 'B.pem'}
private_key = {work / 'B.key'}
default_md = sha256
policy = test_policy
[test_policy]
commonName = supplied
""")
        subprocess.run(["openssl", "req", "-new", "-key", str(work / "B.key"),
                        "-subj", "/CN=NVIDIA GameStream Client", "-out", str(csr)],
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run(["openssl", "ca", "-batch", "-selfsign", "-config", str(ca_config),
                        "-in", str(csr), "-startdate", "20200101000000Z",
                        "-enddate", "20200102000000Z", "-notext", "-out", str(expired)],
                       check=True, stdout=subprocess.DEVNULL)
        environment["TEST_CERT_EXPIRED"] = str(expired)
    # Unwrapped test executables need the same Qt plugins as Nix's app wrapper.
    # In particular, qtsvg's imageformat plugin is outside qtbase's prefix.
    prefixes = environment.get("QT_ADDITIONAL_PACKAGES_PREFIX_PATH", "").split(os.pathsep)
    plugin_paths = [str(Path(prefix) / "lib/qt-6/plugins") for prefix in prefixes
                    if prefix and (Path(prefix) / "lib/qt-6/plugins").is_dir()]
    if plugin_paths:
        previous = environment.get("QT_PLUGIN_PATH", "")
        environment["QT_PLUGIN_PATH"] = os.pathsep.join(plugin_paths + ([previous] if previous else []))
    environment.setdefault("QT_QUICK_BACKEND", "software")
    environment.setdefault("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
    subprocess.run([qmake, str(project)], cwd=work, env=environment, check=True)
    subprocess.run(["make", "-j" + str(max(1, min(4, int(os.environ.get("JOBS", "4")))))], cwd=work, env=environment, check=True, stdout=subprocess.DEVNULL)
    test_binary = macos / "host-lifecycle-tests"
    if sys.platform != "darwin":
        # Unwrapped test binaries must use the QML plugins from their linked Qt,
        # not the desktop session's potentially different Qt installation.
        libraries = subprocess.check_output(["ldd", str(test_binary)], text=True)
        qml_paths = []
        for line in libraries.splitlines():
            fields = line.split()
            if len(fields) > 2 and fields[0].startswith("libQt6") and fields[2].startswith("/"):
                candidate = Path(fields[2]).resolve().parent / "qt-6/qml"
                if candidate.is_dir() and str(candidate) not in qml_paths:
                    qml_paths.append(str(candidate))
        if qml_paths:
            environment["QML_IMPORT_PATH"] = os.pathsep.join(qml_paths)
            environment["NIXPKGS_QT6_QML_IMPORT_PATH"] = os.pathsep.join(qml_paths)
            environment.pop("QML2_IMPORT_PATH", None)
    # Either selection route: explicit arguments after "--", or the environment
    # variable the Windows branch uses.
    test_args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else os.environ.get("DESKPORT_TEST_FUNCTIONS", "").split()
    subprocess.run([str(test_binary), *test_args], cwd=work, env=environment, check=True)
