#!/usr/bin/env python3
"""Exercise the packaged CLI and foreground owner in a disposable Linux profile.

No sharing, saved peers, session connections or input injection is performed.
The binding listener uses an unused high port; the local control socket is
derived from this fixture's private configuration directory.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import json
import http.client
import os
from pathlib import Path
import signal
import socket
import ssl
import subprocess
import sys
import tempfile
import time

binary = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="deskport-cli-") as temporary:
    work = Path(temporary)
    environment = dict(os.environ)
    for variable in ("DISPLAY", "WAYLAND_DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE",
                     "DBUS_SESSION_BUS_ADDRESS", "XDG_CURRENT_DESKTOP"):
        environment.pop(variable, None)
    for variable, folder in (("HOME", "home"), ("XDG_CONFIG_HOME", "config"),
                             ("XDG_DATA_HOME", "data"), ("XDG_CACHE_HOME", "cache"),
                             ("XDG_RUNTIME_DIR", "runtime")):
        path = work / folder
        path.mkdir(mode=0o700)
        environment[variable] = str(path)
    # Every short command must work even with an intentionally invalid GUI
    # platform; host run explicitly chooses its own offscreen platform.
    environment["QT_QPA_PLATFORM"] = "deskport-test-no-gui-platform"
    environment["DESKPORT_WEB_BIND"] = "127.0.0.1"
    with socket.socket() as web_reservation:
        web_reservation.bind(("127.0.0.1", 0))
        environment["DESKPORT_WEB_PORT"] = str(web_reservation.getsockname()[1])

    def command(*arguments, code=0):
        result = subprocess.run([binary, *arguments], cwd=work, env=environment,
                                text=True, capture_output=True, timeout=15)
        assert result.returncode == code, (arguments, result.returncode, result.stdout, result.stderr)
        return result.stdout

    command("--version")
    assert "host run" in command("--help")
    command("host", "run", "--help")
    for verb in ("status", "sharing", "devices", "config", "web", "doctor", "service"):
        command(verb, "--help")
    assert not json.loads(command("status", "--json", code=1))["ok"]
    assert not list((work / "config").rglob("*.conf")), "Read-only commands created settings"
    assert not list((work / "data").rglob("*.pem")), "Read-only commands created credentials"
    config = work / "config/DeskPort"
    config.mkdir(exist_ok=True)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        binding_port = reservation.getsockname()[1]
    (config / "DeskPort.conf").write_text(f"[binding]\nport={binding_port}\n")
    with (work / "daemon.log").open("w+") as log:
        owner = subprocess.Popen([binary, "host", "run", "--no-share"], cwd=work,
                                 env=environment, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 15
            while True:
                result = subprocess.run([binary, "status", "--json"], cwd=work,
                                        env=environment, capture_output=True, text=True, timeout=4)
                if result.returncode == 0:
                    state = json.loads(result.stdout)["data"]
                    break
                assert owner.poll() is None, "Foreground host exited before management became ready"
                assert time.monotonic() < deadline, "Management endpoint did not become ready"
                time.sleep(0.1)
            assert not state["running"] and not state["ready"]
            assert state["port"] == binding_port
            web = json.loads(command("web", "info", "--json"))["data"]
            assert web["enabled"] and len(web["accessCode"]) == 6
            assert web["urls"] == [f'https://127.0.0.1:{environment["DESKPORT_WEB_PORT"]}/']
            certificate, = (work / "data").rglob("browser/https-cert.pem")
            tls = ssl.create_default_context(cafile=str(certificate))
            def browser_request(path, body, cookies="", csrf=""):
                connection = http.client.HTTPSConnection("127.0.0.1", int(environment["DESKPORT_WEB_PORT"]), context=tls, timeout=10)
                connection.request("POST", path, json.dumps(body), {
                    "Origin": web["urls"][0].rstrip("/"), "Content-Type": "application/json",
                    "Cookie": cookies, "X-DeskPort-Session": csrf})
                response = connection.getresponse()
                data = json.loads(response.read())
                saved = "; ".join(value.split(";", 1)[0] for key, value in response.getheaders() if key.lower() == "set-cookie")
                status = response.status
                connection.close()
                return status, data, saved
            assert json.loads(command("web", "list", "--json"))["data"]["browsers"] == []
            status, pairing, cookies = browser_request("/api/login", {"code": web["accessCode"], "remember": True, "deviceName": "CLI browser fixture"})
            assert status == 200 and pairing["paired"]
            browser_id = pairing["pairing"]["id"]
            listed = json.loads(command("web", "list", "--json"))["data"]["browsers"]
            assert len(listed) == 1 and listed[0]["id"] == browser_id and "hash" not in listed[0]
            assert not json.loads(command("web", "remove", "missing-browser", "--json", code=1))["ok"]
            assert json.loads(command("web", "remove", browser_id, "--json"))["data"]["removed"] == browser_id
            assert browser_request("/api/resume", {}, cookies)[0] == 401
            assert browser_request("/api/logout", {}, cookies, pairing["csrfToken"])[0] == 401
            assert json.loads(command("web", "list", "--json"))["data"]["browsers"] == []
            command("host", "run", "--no-share", code=3)
            assert json.loads(command("config", "set", "name", "CLI fixture", "--json"))["ok"]
            assert json.loads(command("config", "get", "--json"))["data"]["name"] == "CLI fixture"
            assert json.loads(command("devices", "list", "--json"))["data"]["devices"] == []
            assert not json.loads(command("devices", "pending", "--json"))["data"]["requestId"]
            assert not json.loads(command("devices", "approve", "stale", "--json", code=1))["ok"]
            command("config", "set", "port", "22", "--json", code=2)
            command("sharing", "stop", "--json")
            owner.send_signal(signal.SIGTERM)
            assert owner.wait(timeout=12) == 0
            command("status", "--json", code=1)
        finally:
            if owner.poll() is None:
                owner.terminate()
                try:
                    owner.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    owner.kill()
                    owner.wait()
            if sys.exc_info()[0]:
                log.seek(0)
                print(log.read(), file=sys.stderr)
print("PASS: packaged CLI works without a display; real HTTPS browser enrollment/list/removal/old-credential rejection, isolated daemon, config, ownership, stale approval and SIGTERM")
