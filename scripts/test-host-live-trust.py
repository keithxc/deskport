#!/usr/bin/env python3
"""Exercise a built host's live trust API in disposable loopback-only state.

No streaming, display changes, real input, installed app, or user credentials.
"""
import base64
import http.client
import json
import os
from pathlib import Path
import secrets
import socket
import ssl
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET

host = Path(sys.argv[1]).resolve()
assert sys.platform.startswith('linux'), 'This headless fixture is Linux-only'
assert host.is_file(), 'Supply the built Sunshine executable'
with tempfile.TemporaryDirectory(prefix='deskport-live-trust-') as tmp:
    work = Path(tmp)
    for label in ('host', 'existing', 'added'):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '2',
                        '-subj', '/CN=DeskPort isolated test', '-set_serial', '1',
                        '-keyout', str(work / f'{label}.key'), '-out', str(work / f'{label}.pem')],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # All Sunshine TCP/UDP offsets fit here. Confirm this candidate is unused.
    for _ in range(100):
        port = 30000 + secrets.randbelow(15000)
        reservations = []
        try:
            for offset in range(-5, 22):
                for kind in (socket.SOCK_STREAM, socket.SOCK_DGRAM):
                    reservation = socket.socket(socket.AF_INET, kind)
                    reservations.append(reservation)
                    reservation.bind(('127.0.0.1', port + offset))
            break
        except OSError:
            pass
        finally:
            for reservation in reservations:
                reservation.close()
    else:
        raise RuntimeError('No isolated port range available')
    state = work / 'state.json'
    state.write_text(json.dumps({'extra': {'preserve': True}, 'root': {'uniqueid': 'ISOLATED-HOST',
        'named_devices': [{'uuid': 'existing', 'name': 'Existing test client', 'enabled': True,
                           'cert': (work / 'existing.pem').read_text()}]}}))
    (work / 'apps.json').write_text('{"env":{},"apps":[]}')
    config = work / 'sunshine.conf'
    config.write_text('\n'.join(f'{key} = {value}' for key, value in {
        'port': port, 'bind_address': '127.0.0.1', 'address_family': 'ipv4',
        'file_state': state, 'credentials_file': state, 'log_path': work / 'host.log',
        'cert': work / 'host.pem', 'pkey': work / 'host.key', 'file_apps': work / 'apps.json',
        'upnp': 'disabled', 'system_tray': 'disabled', 'origin_web_ui_allowed': 'pc',
        'keyboard': 'disabled', 'mouse': 'disabled', 'controller': 'disabled',
        'dd_configuration_option': 'disabled', 'encoder': 'software', 'capture': 'x11',
    }.items()) + '\n')
    password = secrets.token_hex(24)
    env = dict(os.environ, XDG_CONFIG_HOME=tmp, XDG_DATA_HOME=tmp)
    for key in ('DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS'):
        env.pop(key, None)
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    tls.check_hostname = False
    tls.load_verify_locations(work / 'host.pem')
    basic = 'Basic ' + base64.b64encode(f'deskport:{password}'.encode()).decode()

    def request(path, body=None, *, headers=None, authenticated=True):
        connection = http.client.HTTPSConnection('127.0.0.1', port + 1, context=tls, timeout=2)
        try:
            fields = {'Authorization': basic} if authenticated else {}
            fields.update(headers or {})
            connection.request('POST' if body is not None else 'GET', '/api/deskport/' + path,
                               None if body is None else json.dumps(body), fields)
            response = connection.getresponse()
            data = response.read()
            return response.status, json.loads(data) if response.status == 200 else {}
        finally:
            connection.close()

    def authorized(label):
        client_tls = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        client_tls.check_hostname = False
        client_tls.load_verify_locations(work / 'host.pem')
        client_tls.load_cert_chain(work / f'{label}.pem', work / f'{label}.key')
        connection = http.client.HTTPSConnection('127.0.0.1', port - 5, context=client_tls, timeout=2)
        try:
            connection.request('GET', '/serverinfo?uniqueid=isolated')
            return ET.fromstring(connection.getresponse().read()).attrib['status_code'] == '200'
        finally:
            connection.close()

    with (work / 'process.log').open('wb') as log:
        subprocess.run([str(host), str(config), '--creds', 'deskport', password], env=env,
                       cwd=tmp, stdout=log, stderr=log, check=True, timeout=30)
        process = subprocess.Popen([str(host), str(config)], env=env, cwd=tmp, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 45
            while True:
                if process.poll() is not None:
                    raise RuntimeError('Isolated host exited before management became ready')
                try:
                    status, initial = request('sessions')
                    if status == 200 and initial.get('status'):
                        break
                except (OSError, http.client.HTTPException):
                    pass
                if time.monotonic() > deadline:
                    raise RuntimeError('Isolated host management did not become ready')
                time.sleep(.1)
            assert authorized('existing')
            assert not authorized('added')
            status, reserved = request('sessions', {'action': 'acquire', 'uuid': 'existing',
                'lease': 'isolated-lease', 'snapshot': initial['snapshot']})
            assert status == 200 and reserved['status'] and reserved['reserved']
            grant = {'uuid': 'added', 'name': 'Added test client', 'cert': (work / 'added.pem').read_text()}
            for headers, auth in [({}, False), ({'Origin': 'https://example.invalid'}, True),
                                   ({'Referer': 'https://example.invalid'}, True)]:
                status, _ = request('trust', grant, headers=headers, authenticated=auth)
                assert status != 200, 'Trust endpoint accepted an unauthenticated/browser request'
                assert not authorized('added')
            original = state.read_bytes()
            status, result = request('trust', dict(grant, cert='invalid'))
            assert status == 200 and not result['status']
            assert state.read_bytes() == original
            # Disk corruption must fail closed without affecting current TLS trust.
            state.write_text('{broken')
            status, result = request('trust', grant)
            assert status == 200 and not result['status'] and state.read_text() == '{broken'
            assert authorized('existing') and not authorized('added')
            state.write_bytes(original)
            for _ in range(2):  # Idempotent repeat, no duplicate certificate identity.
                status, result = request('trust', grant)
                assert status == 200 and result['status']
                assert authorized('existing') and authorized('added')
                _, after = request('sessions')
                assert after == reserved, 'Adding trust changed the active admission lease'
                saved = json.loads(state.read_text())
                assert saved['extra'] == {'preserve': 'true'} or saved['extra'] == {'preserve': True}
                assert len(saved['root']['named_devices']) == 2
                assert process.poll() is None
            print('PASS: live TLS trust, preserved lease/client/state, repeat grant, invalid/corrupt input, local API authorization')
        except Exception:
            # This log belongs only to the disposable host with synthetic data.
            print((work / 'process.log').read_text(errors='replace')[-12000:], file=sys.stderr)
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
