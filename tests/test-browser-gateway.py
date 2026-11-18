#!/usr/bin/env python3
"""Compile and exercise only an isolated loopback HTTPS gateway with a fake host."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
from concurrent.futures import ThreadPoolExecutor
import http.client
import base64
import hashlib
import hmac
import struct
from urllib.parse import urlparse, parse_qs, unquote
import json
import os
from pathlib import Path
import re
import select
import socket
import ssl
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, default=_build_storage.Storage().paths["tests"] / "browser-client/pairing")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
work = Path(tempfile.mkdtemp(prefix="test-", dir=args.output))
project = work / "test.pro"
resources = work / "resources.qrc"
(work / "index.html").write_text("<!doctype html><title>Isolated DeskPort</title><span>v@DESKPORT_VERSION@</span>")
(work / "app.js").write_text("'use strict';")
for name in ("i18n.js", "i18n-data.js"):
    (work / name).write_bytes((ROOT / "app/browser" / name).read_bytes())
(work / "style.css").write_text("body { color: black; }")
(work / "icon.svg").write_bytes((ROOT / "app/res/deskport.svg").read_bytes())
resources.write_text('<RCC><qresource prefix="/browser">' + ''.join(
    f'<file alias="{name}">{work / name}</file>' for name in ("index.html", "app.js", "i18n.js", "i18n-data.js", "style.css", "icon.svg")) + '</qresource></RCC>')
project.write_text(f'''QT = core network
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
PKGCONFIG += openssl
TARGET = gateway-test
SOURCES += "{ROOT}/tests/browser-gateway.cpp" "{ROOT}/app/backend/browsergateway.cpp"
HEADERS += "{ROOT}/app/backend/browsergateway.h"
INCLUDEPATH += "{ROOT}/app/backend"
RESOURCES += "{resources}"
''')
with (work / "build.log").open("w") as log:
    subprocess.run([os.environ.get("DESKPORT_QMAKE", "qmake"), str(project)], cwd=work, check=True, stdout=log, stderr=log)
    build = subprocess.run(["make", "-j2"], cwd=work, stdout=log, stderr=log)
if build.returncode:
    print((work / "build.log").read_text())
    raise SystemExit(build.returncode)

processes = []
checks = []
# Every gateway reads this clock; reading info["code"] advances it one 30-second
# step and computes the code here, independently of the C++ implementation.
CLOCK = work / "clock"
CLOCK.write_text("1800000000")
def clock():
    return int(CLOCK.read_text())
def advance(seconds=30):
    CLOCK.write_text(str(clock() + seconds))
def totp(secret, step):
    digest = hmac.new(secret, struct.pack(">Q", step), hashlib.sha1).digest()
    offset = digest[-1] & 15
    return f"{(struct.unpack('>I', digest[offset:offset + 4])[0] & 0x7fffffff) % 1000000:06d}"
def secret_of(uri):
    query = parse_qs(urlparse(uri).query)
    value = query["secret"][0]
    return base64.b32decode(value + "=" * (-len(value) % 8))
class Info(dict):
    def __getitem__(self, key):
        if key != "code":
            return super().__getitem__(key)
        advance()
        return totp(secret_of(super().__getitem__("authenticator")), clock() // 30)
def check(condition, label):
    if not condition:
        raise AssertionError(label)
    checks.append(label)

def line(process):
    ready, _, _ = select.select([process.stdout], [], [], 20)
    if not ready:
        raise RuntimeError("Isolated test process did not respond")
    value = process.stdout.readline()
    if not value:
        raise RuntimeError("Isolated test process exited")
    return json.loads(value)

def launch(directory, port=0, certificate=None, key=None, environment=None):
    command = [str(work / "gateway-test"), str(directory), str(port)]
    if certificate is not None:
        command += [str(certificate), str(key)]
    process = subprocess.Popen(command, cwd=work, env=dict(os.environ, DESKPORT_TEST_CLOCK_FILE=str(CLOCK), **(environment or {})),
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    processes.append(process)
    info = Info(line(process))
    return process, info

def command(process, value):
    process.stdin.write(value.encode() + b"\n")
    process.stdin.flush()
    return line(process)

def finish(process):
    if process.poll() is None:
        process.stdin.write(b"quit\n")
        process.stdin.flush()
        process.wait(timeout=10)

def request(info, directory, method, path, body=None, auth=None, headers=None, raw=False, certificate=None, hostname="127.0.0.1"):
    context = ssl.create_default_context(cafile=str(certificate or directory / "https-cert.pem"))
    connection = http.client.HTTPSConnection(hostname, info["port"], context=context, timeout=10)
    if hostname != "127.0.0.1":
        # Resolve only this fixture to loopback while preserving the real DNS
        # hostname for TLS verification, SNI, Host and same-origin checks.
        connection._create_connection = lambda address, timeout=10, source_address=None: socket.create_connection(
            ("127.0.0.1", address[1]), timeout, source_address)
    values = {"Origin": f'https://{hostname}:{info["port"]}'}
    if method == "POST":
        values["Content-Type"] = "application/json"
    if auth:
        values.update({"Cookie": auth[0], "X-DeskPort-Session": auth[1]})
    values.update(headers or {})
    payload = json.dumps(body if body is not None else {}) if method == "POST" else None
    connection.request(method, path, body=payload, headers=values)
    response = connection.getresponse()
    data = response.read()
    response_headers = dict(response.getheaders())
    response_headers["Set-Cookies"] = response.headers.get_all("Set-Cookie", [])
    result = data if raw else json.loads(data)
    status = response.status
    connection.close()
    return status, result, response_headers

def login(info, directory, code=None):
    status, result, headers = request(info, directory, "POST", "/api/login", {"code": code or info["code"]})
    check(status == 200 and result.get("ok") and "csrfToken" in result, "valid current code authenticates")
    cookie = headers["Set-Cookie"]
    check(all(value in cookie for value in ("Secure", "HttpOnly", "SameSite=Strict", "Path=/")), "cookie has secure session attributes")
    check("Max-Age" not in cookie and "Expires" not in cookie, "login cookie is not persistent")
    return cookie.split(";", 1)[0], result["csrfToken"]

def cookies(headers):
    return {value.split("=", 1)[0]: value for value in headers["Set-Cookies"]}

def pairing_login(info, directory, name="Isolated Safari", existing=""):
    status, result, headers = request(info, directory, "POST", "/api/login",
        {"code": info["code"], "remember": True, "deviceName": name}, headers={"Cookie": existing})
    check(status == 200 and result.get("paired") and result["pairing"]["name"] == name,
        "remembered login returns public device identity")
    values = cookies(headers)
    check(len(values) == 2 and all(value in values["__Host-deskport-pairing"] for value in
        ("Secure", "HttpOnly", "SameSite=Strict", "Path=/", "Max-Age=604800")),
        "pairing and session use distinct Set-Cookie headers with persistent pairing attributes")
    pair = values["__Host-deskport-pairing"].split(";", 1)[0]
    session = values["__Host-deskport-session"].split(";", 1)[0]
    return (session + "; " + pair, result["csrfToken"]), pair, result["pairing"]["id"]

def resume(info, directory, pair):
    status, result, headers = request(info, directory, "POST", "/api/resume", {}, headers={"Cookie": pair})
    check(status == 200 and result.get("paired") and result.get("csrfToken"), "pairing restores a fresh short session")
    values = cookies(headers)
    age = int(re.search(r"Max-Age=(\d+)", values["__Host-deskport-pairing"]).group(1))
    check(len(values) == 2 and 0 < age <= 604800 and
        "Max-Age" not in values["__Host-deskport-session"], "resume keeps the seven-day limit without persisting short session")
    return (values["__Host-deskport-session"].split(";", 1)[0] + "; " + pair, result["csrfToken"]), result

try:
    directory = work / "state"
    process, info = launch(directory)
    check("error" not in info, "gateway starts on isolated loopback")
    uri = urlparse(info["authenticator"]); query = parse_qs(uri.query)
    check(uri.scheme == "otpauth" and uri.netloc == "totp" and unquote(uri.path) == "/DeskPort:Isolated host" and
        query["issuer"] == ["DeskPort"] and query["algorithm"] == ["SHA1"] and query["digits"] == ["6"] and
        query["period"] == ["30"] and len(secret_of(info["authenticator"])) == 20,
        "authenticator URI is standard six-digit 30-second SHA-1 TOTP with a 160-bit secret")
    check(dict.get(info, "code") == totp(secret_of(info["authenticator"]), clock() // 30),
        "host code matches an independent RFC 6238 computation")
    vectors = subprocess.run([str(work / "gateway-test"), str(work / "vectors")], cwd=work, capture_output=True, text=True,
        env=dict(os.environ, DESKPORT_TEST_TOTP_VECTOR="1"), timeout=30)
    check(json.loads(vectors.stdout)["vectors"] == ["287082", "081804", "050471", "005924", "279037", "353130"],
        "TOTP matches the RFC 6238 SHA-1 test vectors")
    settings_text = (directory / "browser.ini").read_text()
    check("secret=" in settings_text and "access/code" not in settings_text and "code=" not in settings_text,
        "only the code secret is stored, never a fixed code")
    check(directory.stat().st_mode & 0o777 == 0o700, "settings directory is owner-only")
    for name in ("browser.ini", "https-key.pem", "https-cert.pem"):
        check((directory / name).stat().st_mode & 0o777 == 0o600, f"{name} is owner-only")
    status, page, headers = request(info, directory, "GET", "/", raw=True)
    for name in ("i18n.js", "i18n-data.js"):
        resource_status, content, resource_headers = request(info, directory, "GET", "/" + name, raw=True)
        check(resource_status == 200 and content == (ROOT / "app/browser" / name).read_bytes() and
            "application/javascript" in resource_headers["Content-Type"], "embedded Web localization resource: " + name)
    check(b"v0.7.0-test" in page and b"@DESKPORT_VERSION@" not in page, "embedded page displays the serving application version")
    check(status == 200 and b"Isolated DeskPort" in page, "trusted TLS certificate has loopback SAN and serves embedded page")
    status, icon, headers = request(info, directory, "GET", "/icon.svg", raw=True)
    check(status == 200 and icon.startswith(b"<svg") and headers.get("Content-Type") == "image/svg+xml",
        "DeskPort icon is served for the page logo and browser tab")
    check(info["code"].encode() not in page, "anonymous page does not expose access code")
    check("frame-ancestors 'none'" in headers["Content-Security-Policy"], "static page has restrictive CSP")
    check(request(info, directory, "GET", "/api/status")[0] == 401, "status requires authentication")
    check(request(info, directory, "POST", "/api/login", {"code": info["code"]}, headers={"Origin": "https://evil.invalid"})[0] == 403, "cross-origin login is refused")
    check(request(info, directory, "POST", "/api/login", {"code": info["code"]}, headers={"Origin": ""})[0] == 403, "missing-origin mutation is refused")
    check(request(info, directory, "GET", "/", headers={"Host": f'evil.invalid:{info["port"]}'})[0] == 400, "unknown Host is refused")
    check(request(info, directory, "POST", "/api/login", {"code": "!!!!!!"})[0] == 401, "incorrect code is refused")
    auth = login(info, directory, info["code"].lower())
    check(request(info, directory, "GET", "/api/status", headers={"Cookie": auth[0]})[0] == 401, "cookie alone cannot restore a new page session")
    check(request(info, directory, "GET", "/api/status", auth=(auth[0], "wrong"))[0] == 401, "incorrect in-memory session header is refused")
    status, state, _ = request(info, directory, "GET", "/api/status", auth=auth)
    check(status == 200 and state["sharing"] and "privateSecret" not in state, "authenticated status uses explicit public fields")
    check(info["code"] not in json.dumps(state), "authenticated status does not expose access code")
    check(request(info, directory, "POST", "/api/session/start", {"id": "other"}, auth=auth)[0] == 400, "browser cannot choose media identity")
    status, offer, _ = request(info, directory, "POST", "/api/session/start", {"width": 1280, "height": 720}, auth=auth)
    check(status == 200 and offer["type"] == "offer" and "id" not in offer and "lease" not in offer, "server offer is proxied without internal session identity")
    snapshot = command(process, "snapshot")
    check(snapshot["last"]["id"].startswith("browser-") and snapshot["starts"] == 1, "gateway assigns media identity")
    check(bool(re.fullmatch(r"[A-Za-z0-9-]{16,64}", snapshot["last"]["id"])), "generated media identity satisfies the native admission contract")
    check(request(info, directory, "POST", "/api/session/start", {}, auth=auth)[0] == 409, "duplicate start is refused")
    check(request(info, directory, "POST", "/api/session/answer", {"type": "answer", "sdp": "v=0"}, auth=auth)[0] == 200, "answer reaches host adapter")
    check(request(info, directory, "GET", "/api/session/status", auth=auth)[0] == 200, "media status reports session state")
    check(request(info, directory, "POST", "/api/session/heartbeat", {}, auth=auth)[0] == 200, "explicit heartbeat reaches media adapter")
    check(request(info, directory, "POST", "/api/session/resize", {"viewport": {"width": 1600, "height": 900, "ratio": 1}}, auth=auth)[0] == 200,
          "live viewport resize reaches media adapter")
    check(request(info, directory, "GET", "/api/session/resize", auth=auth)[0] == 404, "resize requires POST")
    check(request(info, directory, "POST", "/api/logout", {}, auth=auth)[0] == 200, "logout succeeds")
    check(command(process, "snapshot")["stops"] == 1, "logout releases media")
    check(request(info, directory, "GET", "/api/status", auth=auth)[0] == 401, "logged-out session cannot replay")
    auth = login(info, directory)
    command(process, "off")
    check(request(info, directory, "POST", "/api/session/start", {}, auth=auth)[0] == 409, "sharing-off rejects media start")
    command(process, "on")
    check(request(info, directory, "POST", "/api/session/start", {}, auth=auth)[0] == 200, "sharing enabled permits start")
    command(process, "off")
    check(command(process, "snapshot")["stops"] == 2, "turning sharing off releases existing media")
    command(process, "on")
    request(info, directory, "POST", "/api/session/start", {}, auth=auth)
    time.sleep(1.2)
    request(info, directory, "POST", "/api/session/heartbeat", {}, auth=auth)
    time.sleep(1.2)
    check(command(process, "snapshot")["active"], "explicit heartbeat preserves active media")
    request(info, directory, "GET", "/api/session/status", auth=auth)
    time.sleep(3.2)
    check(command(process, "snapshot")["stops"] == 3, "missing browser heartbeat releases media")
    check(request(info, directory, "GET", "/api/status", auth=auth)[0] == 401, "heartbeat expiry requires fresh authentication")
    original_code = info["code"]
    original_secret = secret_of(info["authenticator"])
    original_cert = (directory / "https-cert.pem").read_bytes()
    finish(process)
    process, info = launch(directory)
    check(secret_of(info["authenticator"]) == original_secret and (directory / "https-cert.pem").read_bytes() == original_cert,
        "code secret and HTTPS identity survive process restart")
    classify = subprocess.run([str(work / "gateway-test"), str(work / "classify")], cwd=work, capture_output=True, text=True,
        env=dict(os.environ, DESKPORT_TEST_CLASSIFY="1"), timeout=30)
    check(json.loads(classify.stdout)["overlay"] == [True, True, True, False, False, False, False],
        "only RFC 6598 addresses on tunnels count as Tailscale; carrier NAT on links does not")
    def names(path):
        return set(ssl._ssl._test_decode_cert(str(path))["subjectAltName"])
    status, result, headers = request(info, directory, "POST", "/api/login/tailnet", {"probe": True})
    check(status == 403 and result["code"] == "tailnet-unavailable" and not headers["Set-Cookies"],
        "a LAN or loopback browser cannot use Tailscale sign-in")
    owner_dir = work / "tailnet-owner"
    owner, owner_info = launch(owner_dir, environment={"DESKPORT_TEST_TAILNET": "owner"})
    status, result, headers = request(owner_info, owner_dir, "POST", "/api/login/tailnet", {"probe": True})
    check(status == 200 and result.get("tailnet") is True and not headers["Set-Cookies"],
        "Tailscale probe reports ownership without creating a session")
    check(request(owner_info, owner_dir, "POST", "/api/login/tailnet", {"probe": "yes"})[0] == 400, "non-boolean probe is rejected")
    status, result, headers = request(owner_info, owner_dir, "POST", "/api/login/tailnet", {})
    owner_cookie = headers["Set-Cookie"].split(";", 1)[0]
    check(status == 200 and result.get("csrfToken") and not result.get("paired") and "Max-Age" not in headers["Set-Cookie"],
        "an owned Tailscale device signs in without the access code, without a persistent pairing")
    check(request(owner_info, owner_dir, "GET", "/api/status", auth=(owner_cookie, result["csrfToken"]))[0] == 200,
        "a Tailscale sign-in session is authenticated")
    check(not command(owner, "snapshot")["pairings"], "Tailscale sign-in stores no browser pairing")
    finish(owner)
    other_dir = work / "tailnet-other"
    other, other_info = launch(other_dir, environment={"DESKPORT_TEST_TAILNET": "other"})
    status, result, headers = request(other_info, other_dir, "POST", "/api/login/tailnet", {})
    check(status == 401 and result["code"] == "tailnet-denied" and not headers["Set-Cookies"],
        "a Tailscale device of another user is refused and must use the access code")
    check(request(other_info, other_dir, "POST", "/api/login", {"code": other_info["code"]})[0] == 200,
        "the access code still works for a refused Tailscale device")
    finish(other)
    disabled_dir = work / "tailnet-disabled"
    disabled, disabled_info = launch(disabled_dir, environment={"DESKPORT_TEST_TAILNET": "owner", "DESKPORT_TEST_NO_TAILNET": "1"})
    check(request(disabled_info, disabled_dir, "POST", "/api/login/tailnet", {})[0] == 403,
        "Tailscale sign-in can be disabled in settings")
    finish(disabled)
    tail_dir = work / "tailnet"
    first, first_info = launch(tail_dir)
    first_cert = (tail_dir / "https-cert.pem").read_bytes()
    finish(first)
    extended, extended_info = launch(tail_dir, environment={"DESKPORT_TEST_ALLOWED_HOSTS": "tailnet-host.test"})
    extended_cert = (tail_dir / "https-cert.pem").read_bytes()
    check(extended_cert != first_cert and {("DNS", "tailnet-host.test"), ("IP Address", "127.0.0.1"), ("DNS", "localhost")} <= names(tail_dir / "https-cert.pem")
        and secret_of(extended_info["authenticator"]) == secret_of(first_info["authenticator"]), "generated certificate is extended to a newly served name and keeps the code secret")
    check(request(extended_info, tail_dir, "GET", "/", raw=True, hostname="tailnet-host.test")[0] == 200,
        "extended certificate validates for the new name over real TLS")
    finish(extended)
    again, _ = launch(tail_dir, environment={"DESKPORT_TEST_ALLOWED_HOSTS": "tailnet-host.test"})
    finish(again)
    check((tail_dir / "https-cert.pem").read_bytes() == extended_cert, "a covered certificate is not regenerated on restart")
    without, _ = launch(tail_dir)
    finish(without)
    check((tail_dir / "https-cert.pem").read_bytes() == extended_cert, "a name that disappears stays covered, avoiding another trust step")
    auth = login(info, directory)
    request(info, directory, "POST", "/api/session/start", {}, auth=auth)
    new = command(process, "reset")
    check(secret_of(new["authenticator"]) != secret_of(info["authenticator"]) and new["stops"] == 1,
        "explicit code reset releases media and replaces the code secret")
    info["authenticator"] = new["authenticator"]
    check(request(info, directory, "GET", "/api/status", auth=auth)[0] == 401, "reset invalidates old login sessions")
    wrong_results = [request(info, directory, "POST", "/api/login", {"code": original_code})[0] for _ in range(6)]
    check(401 in wrong_results and 429 in wrong_results, "codes from a reset secret are refused and rate limited")
    finish(process)
    busy = socket.socket()
    busy.bind(("127.0.0.1", 0)); busy.listen()
    conflict, result = launch(work / "conflict", busy.getsockname()[1])
    check("error" in result and "Existing services were left unchanged" in result["error"], "occupied port is an explicit failure")
    conflict.wait(timeout=5); busy.close()
    partial, result = launch(work / "partial", environment={"DESKPORT_TEST_PARTIAL_BIND": "1"})
    check("error" in result and not result["active"] and result["port"] == 0 and result["urlCount"] == 0,
        "partial listener failure rolls back every listener and advertised URL")
    released_port = int(re.search(r"127\.0\.0\.1:([0-9]+):", result["error"])[1])
    probe = socket.socket(); probe.bind(("127.0.0.1", released_port)); probe.close()
    check(True, "failed multi-listener startup releases its first bound socket")
    partial.wait(timeout=5)
    corrupt_dir = work / "corrupt"
    corrupt_dir.mkdir()
    (corrupt_dir / "browser.ini").write_text("[access]\nsecret=invalid\n")
    corrupt, result = launch(corrupt_dir)
    check("error" in result and "secret=invalid" in (corrupt_dir / "browser.ini").read_text(),
        "invalid saved code secret fails without silent replacement")
    corrupt.wait(timeout=5)
    incomplete_dir = work / "incomplete"
    incomplete_dir.mkdir()
    preserved_key = (directory / "https-key.pem").read_bytes()
    (incomplete_dir / "https-key.pem").write_bytes(preserved_key)
    incomplete, result = launch(incomplete_dir)
    check("error" in result and (incomplete_dir / "https-key.pem").read_bytes() == preserved_key and
        not (incomplete_dir / "https-cert.pem").exists(), "incomplete certificate pair fails without replacing existing key")
    incomplete.wait(timeout=5)
    missing, result = launch(work / "missing", certificate=work / "missing.pem", key=work / "missing.key")
    check("error" in result, "invalid custom HTTPS credentials fail closed")
    missing.wait(timeout=5)
    custom, result = launch(work / "custom", certificate=directory / "https-cert.pem", key=directory / "https-key.pem")
    check("error" not in result and request(result, work / "custom", "GET", "/", raw=True,
        certificate=directory / "https-cert.pem")[0] == 200, "custom certificate and key serve trusted HTTPS")
    finish(custom)
    dns_cert, dns_key = work / "dns-cert.pem", work / "dns-key.pem"
    dns_name = "browser.example.test"
    subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
        "-subj", f"/CN={dns_name}", "-addext", f"subjectAltName=DNS:{dns_name}",
        "-keyout", str(dns_key), "-out", str(dns_cert)], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dns, dns_info = launch(work / "dns", certificate=dns_cert, key=dns_key,
        environment={"DESKPORT_TEST_ALLOWED_HOSTS": dns_name})
    check(dns_info["urls"][0] == f'https://{dns_name}:{dns_info["port"]}/', "explicit custom-certificate hostname is the preferred URL")
    check(request(dns_info, work / "dns", "POST", "/api/login", {"code": dns_info["code"]},
        certificate=dns_cert, hostname=dns_name)[0] == 200, "custom DNS certificate and same-origin login validate over real TLS")
    finish(dns)
    race_dir = work / "race"
    race, race_info = launch(race_dir)
    race_auth = login(race_info, race_dir)
    command(race, "defer")
    with ThreadPoolExecutor(max_workers=1) as executor:
        pending = executor.submit(request, race_info, race_dir, "POST", "/api/session/start", {}, race_auth)
        time.sleep(0.15)
        check(command(race, "snapshot")["active"], "deferred start holds an isolated test lease")
        check(request(race_info, race_dir, "POST", "/api/logout", {}, auth=race_auth)[0] == 200, "logout can cancel an in-flight start")
        command(race, "complete")
        check(pending.result(timeout=5)[0] == 409 and not command(race, "snapshot")["active"], "late start cannot resurrect a logged-out session")
    race_auth = login(race_info, race_dir)
    command(race, "defer")
    context = ssl.create_default_context(cafile=str(race_dir / "https-cert.pem"))
    connection = http.client.HTTPSConnection("127.0.0.1", race_info["port"], context=context, timeout=5)
    connection.request("POST", "/api/session/start", body="{}", headers={"Origin": f'https://127.0.0.1:{race_info["port"]}',
        "Content-Type": "application/json", "Cookie": race_auth[0], "X-DeskPort-Session": race_auth[1]})
    time.sleep(0.15)
    check(command(race, "snapshot")["active"], "second deferred start acquires a test lease")
    connection.close()
    time.sleep(0.15)
    command(race, "complete")
    check(not command(race, "snapshot")["active"], "closed HTTP start rolls back late media success")
    finish(race)
    pair_dir = work / "paired"
    paired, pair_info = launch(pair_dir)
    status, result, headers = request(pair_info, pair_dir, "POST", "/api/resume")
    check(status == 401 and result["code"] == "unpaired" and not headers["Set-Cookies"],
        "missing pairing returns unpaired without mutating shared browser cookies")
    status, result, headers = request(pair_info, pair_dir, "POST", "/api/login", {"code": pair_info["code"], "remember": False})
    check(status == 200 and result["paired"] is False and result["pairing"] is None and len(headers["Set-Cookies"]) == 1,
        "temporary login remains available without device enrollment")
    auth1, pair_cookie, pair_id = pairing_login(pair_info, pair_dir)
    pairing_file = pair_dir / "pairings.json"
    saved = json.loads(pairing_file.read_text())
    saved_row = saved["devices"][0]
    raw_token = pair_cookie.split("=", 1)[1]
    check(pairing_file.stat().st_mode & 0o777 == 0o600 and saved_row["hash"] == hashlib.sha256(raw_token.encode()).hexdigest() and
        raw_token not in pairing_file.read_text(), "private pairing file persists only the credential hash")
    snapshot = command(paired, "snapshot")
    check(len(snapshot["pairings"]) == 1 and set(snapshot["pairings"][0]) == {"id", "name", "createdAt", "lastSeenAt"},
        "local pairing list exposes only public metadata")
    check(request(pair_info, pair_dir, "GET", "/api/status", headers={"Cookie": pair_cookie})[0] == 401,
        "device cookie does not bypass short-session CSRF authentication")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie, "Origin": "https://evil.invalid"})[0] == 403,
        "cross-origin resume is refused")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie, "Origin": ""})[0] == 403,
        "resume requires an explicit same-origin request")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie + "; " + pair_cookie})[0] == 401,
        "ambiguous duplicate pairing cookies are refused")
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": "__Host-deskport-pairing=; " + pair_cookie})[0] == 401,
        "empty-first duplicate pairing cookies are also refused")
    auth2, resumed = resume(pair_info, pair_dir, pair_cookie)
    check(resumed["pairing"]["id"] == pair_id and auth2[1] != auth1[1], "resume retains device identity and issues fresh CSRF token")
    check(request(pair_info, pair_dir, "POST", "/api/session/start", {}, auth=auth2)[0] == 200, "resumed device can start media")
    status, tab_result, tab_headers = request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": auth2[0]})
    check(status == 200 and tab_result["csrfToken"] == auth2[1] and command(paired, "snapshot")["active"] and
        request(pair_info, pair_dir, "POST", "/api/session/heartbeat", {}, auth=auth2)[0] == 200,
        "automatic resume in a second tab preserves the active shared session and CSRF token")
    status, _, headers = request(pair_info, pair_dir, "POST", "/api/logout", {}, auth=auth1)
    check(status == 200 and len(headers["Set-Cookies"]) == 1 and command(paired, "snapshot")["active"],
        "logout removes only its short session and preserves device pairing and other sessions")
    check(request(pair_info, pair_dir, "POST", "/api/session/stop", {}, auth=auth2)[0] == 200 and len(command(paired, "snapshot")["pairings"]) == 1,
        "media stop preserves persistent pairing")
    finish(paired)
    paired, pair_info = launch(pair_dir)
    auth3, resumed = resume(pair_info, pair_dir, pair_cookie)
    check(resumed["pairing"]["id"] == pair_id and json.loads(pairing_file.read_text())["devices"][0]["lastSeenAt"] >= saved_row["lastSeenAt"],
        "paired credential survives server restart and refreshes last-seen metadata")
    auth4, _, reused_id = pairing_login(pair_info, pair_dir, existing=pair_cookie)
    check(reused_id == pair_id and len(command(paired, "snapshot")["pairings"]) == 1, "remembered re-login reuses its device instead of duplicating enrollment")
    check(request(pair_info, pair_dir, "POST", "/api/forget", auth=(auth3[0], "wrong"))[0] == 401,
        "forget requires current session CSRF token")
    check(request(pair_info, pair_dir, "POST", "/api/forget", auth=auth3, headers={"Origin": "https://evil.invalid"})[0] == 403,
        "cross-origin forget is refused")
    request(pair_info, pair_dir, "POST", "/api/session/start", {}, auth=auth4)
    revoked = command(paired, "revoke " + pair_id)
    check(revoked["revoked"] and not revoked["active"] and not revoked["pairings"], "local revocation stops active media and removes the device")
    check(all(request(pair_info, pair_dir, "GET", "/api/status", auth=auth)[0] == 401 for auth in (auth3, auth4)),
        "revocation invalidates every short session associated with a device")
    status, result, stale_headers = request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})
    check(status == 401 and result["code"] == "unpaired" and not stale_headers["Set-Cookies"],
        "revoked device credential cannot replay or mutate shared browser cookies")
    finish(paired)
    paired, pair_info = launch(pair_dir)
    check(request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})[0] == 401,
        "revocation remains effective across server restart")
    auth5, pair_cookie2, pair_id2 = pairing_login(pair_info, pair_dir)
    check(pair_cookie2 != pair_cookie and not stale_headers["Set-Cookies"],
        "a delayed invalid resume response has no cookie mutation that could erase a newer pairing")
    auth6, _ = resume(pair_info, pair_dir, pair_cookie2)
    request(pair_info, pair_dir, "POST", "/api/session/start", {}, auth=auth5)
    status, _, headers = request(pair_info, pair_dir, "POST", "/api/forget", {}, auth=auth6)
    check(status == 200 and len(headers["Set-Cookies"]) == 2 and all("Max-Age=0" in value for value in headers["Set-Cookies"]),
        "forget clears both pairing and short-session cookies with separate headers")
    check(not command(paired, "snapshot")["active"] and request(pair_info, pair_dir, "GET", "/api/status", auth=auth5)[0] == 401 and
        request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie2})[0] == 401,
        "forget terminates other sessions and permanently revokes the credential")
    auth7, pair_cookie3, pair_id3 = pairing_login(pair_info, pair_dir)
    other_auth, other_cookie, other_id = pairing_login(pair_info, pair_dir, "Second browser")
    command(paired, "defer")
    with ThreadPoolExecutor(max_workers=1) as executor:
        pending = executor.submit(request, pair_info, pair_dir, "POST", "/api/session/start", {}, auth7)
        time.sleep(0.15)
        check(command(paired, "revoke " + pair_id3)["revoked"], "pending media can be revoked")
        command(paired, "complete")
        check(pending.result(timeout=5)[0] == 409 and not command(paired, "snapshot")["active"],
            "late media success cannot resurrect a revoked pairing")
    check(request(pair_info, pair_dir, "GET", "/api/status", auth=other_auth)[0] == 200 and
        request(pair_info, pair_dir, "POST", "/api/resume", headers={"Cookie": other_cookie})[0] == 200,
        "revoking one browser preserves another browser's pairing and short session")
    finish(paired)
    write_dir = work / "write-failure"
    writer, write_info = launch(write_dir)
    write_auth, write_cookie, write_id = pairing_login(write_info, write_dir)
    request(write_info, write_dir, "POST", "/api/session/start", {}, auth=write_auth)
    blocked_file = write_dir / "pairings.json"
    backup_file = write_dir / "pairings.saved"
    blocked_file.rename(backup_file); blocked_file.mkdir()
    failed = command(writer, "revoke " + write_id)
    check(not failed["revoked"] and failed["active"] and len(failed["pairings"]) == 1,
        "failed durable revoke preserves the live credential and does not claim success")
    status, result, headers = request(write_info, write_dir, "POST", "/api/forget", auth=write_auth)
    check(status == 503 and result["code"] == "storage-unavailable" and not headers["Set-Cookies"] and command(writer, "snapshot")["active"],
        "forget reports write failure without clearing cookies or disconnecting")
    check(request(write_info, write_dir, "POST", "/api/resume", headers={"Cookie": write_cookie})[0] == 503,
        "failed pairing refresh cannot issue a fresh session")
    status, result, headers = request(write_info, write_dir, "POST", "/api/login", {"code": write_info["code"], "remember": True})
    check(status == 503 and result["code"] == "storage-unavailable" and not headers["Set-Cookies"] and len(command(writer, "snapshot")["pairings"]) == 1,
        "failed pairing enrollment does not issue a credential or mutate the device registry")
    blocked_file.rmdir(); backup_file.rename(blocked_file)
    check(command(writer, "revoke " + write_id)["revoked"], "revocation succeeds once durable storage is restored")
    finish(writer)
    expired = json.loads(json.dumps(saved))
    created = int(time.time() * 1000) - 604800000 - 10000
    expired["devices"][0].update(createdAt=created, lastSeenAt=created, expiresAt=created + 604800000)
    expiry_dir = work / "expired-pairing"; expiry_dir.mkdir()
    (expiry_dir / "pairings.json").write_text(json.dumps(expired))
    expiry, expiry_info = launch(expiry_dir)
    status, result, expiry_headers = request(expiry_info, expiry_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})
    check("error" not in expiry_info and not command(expiry, "snapshot")["pairings"] and
        status == 401 and result["code"] == "unpaired" and not expiry_headers["Set-Cookies"],
        "expired persisted pairing cannot restore a session or mutate shared browser cookies")
    finish(expiry)
    malformed = {"truncated": '{"version":1', "oversize": " " * 32769,
        "wrong-version": json.dumps(dict(saved, version=3)),
        "long-lifetime": json.dumps(dict(saved, devices=[dict(saved_row, expiresAt=saved_row["createdAt"] + 31536000000)])), "duplicate-id": json.dumps(dict(saved, devices=saved["devices"] * 2)),
        "duplicate-hash": json.dumps(dict(saved, devices=[saved_row, dict(saved_row, id="f" * 32)])),
        "fractional-time": json.dumps(dict(saved, devices=[dict(saved_row, createdAt=1.5)])),
        "control-in-name": json.dumps(dict(saved, devices=[dict(saved_row, name="Browser\nInjected")])),
        "missing-hash": json.dumps(dict(saved, devices=[{key: value for key, value in saved_row.items() if key != "hash"}]))}
    for label, content in malformed.items():
        bad_dir = work / ("pairing-" + label); bad_dir.mkdir()
        (bad_dir / "pairings.json").write_text(content)
        bad, bad_info = launch(bad_dir)
        check("error" in bad_info and not bad_info["active"] and (bad_dir / "pairings.json").read_text() == content,
            f"{label} pairing registry fails closed without replacement")
        bad.wait(timeout=5)
    limit_dir = work / "pairing-limit"; limit_dir.mkdir()
    rows = [dict(saved_row, id=f"{i:032x}", hash=hashlib.sha256(str(i).encode()).hexdigest()) for i in range(32)]
    (limit_dir / "pairings.json").write_text(json.dumps({"version": 2, "devices": rows}))
    limit, limit_info = launch(limit_dir)
    check("error" not in limit_info and len(command(limit, "snapshot")["pairings"]) == 32, "maximum bounded pairing registry loads")
    status, result, headers = request(limit_info, limit_dir, "POST", "/api/login", {"code": limit_info["code"], "remember": True})
    check(status == 409 and result["code"] == "pairing-limit" and not headers["Set-Cookies"], "33rd device enrollment is refused without issuing cookies")
    finish(limit)
    legacy_dir = work / "legacy-pairings"; legacy_dir.mkdir()
    legacy_row = dict(saved_row, expiresAt=saved_row["lastSeenAt"] + 31536000000)
    (legacy_dir / "pairings.json").write_text(json.dumps({"version": 1, "devices": [legacy_row]}))
    (legacy_dir / "browser.ini").write_text("[access]\ncode=ABC234\n"); (legacy_dir / "browser.ini").chmod(0o600)
    legacy, legacy_info = launch(legacy_dir)
    check("error" not in legacy_info and not command(legacy, "snapshot")["pairings"] and
        json.loads((legacy_dir / "pairings.json").read_text()) == {"version": 2, "devices": []},
        "upgrading clears every browser remembered under the fixed code")
    check("ABC234" not in (legacy_dir / "browser.ini").read_text() and
        request(legacy_info, legacy_dir, "POST", "/api/login", {"code": "ABC234"})[0] == 401, "the retired fixed code no longer signs in")
    status, result, headers = request(legacy_info, legacy_dir, "POST", "/api/resume", headers={"Cookie": pair_cookie})
    check(status == 401 and result["code"] == "unpaired", "a browser remembered before the upgrade must enter a code")
    finish(legacy)
    # Code rules: each step works once, one step of drift either way, nothing older.
    totp_dir = work / "totp"; totp_dir.mkdir()
    gate, gate_info = launch(totp_dir, environment={"DESKPORT_TEST_CODE_FAILURES": "3"})
    secret = secret_of(gate_info["authenticator"])
    def try_code(value):
        return request(gate_info, totp_dir, "POST", "/api/login", {"code": value})
    advance(); step = clock() // 30
    check(try_code(totp(secret, step))[0] == 200, "current step signs in")
    status, result, _ = try_code(totp(secret, step))
    check(status == 401 and result["code"] == "unauthorized", "the same code cannot be used twice")
    advance(); step = clock() // 30
    check(try_code(totp(secret, step + 1)[:3] + " " + totp(secret, step + 1)[3:])[0] == 200,
        "a code one step ahead (authenticator clock drift) is accepted, with a space")
    check(try_code(totp(secret, step))[0] == 401, "an older step than the last accepted one is refused")
    time.sleep(12.5)  # Per-address rate limit refills one attempt per 12 s.
    advance(120); step = clock() // 30
    check(try_code(totp(secret, step - 2))[0] == 401, "a code two steps old has expired")
    time.sleep(12.5)
    check(try_code("000000" if totp(secret, step) != "000000" else "111111")[0] == 401, "a wrong code is refused")
    snapshot = command(gate, "snapshot")
    check(snapshot["locked"], "repeated wrong codes pause code sign-in")
    time.sleep(12.5)
    status, result, _ = try_code(totp(secret, step))
    check(status == 429 and result["code"] == "code-locked", "while paused even the right code is refused")
    finish(gate)
    print(f"PASS: {len(checks)} isolated HTTPS/authentication checks; build and generated test material: {work}")
finally:
    for process in processes:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
