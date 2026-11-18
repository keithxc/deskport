#!/usr/bin/env python3
"""Exercise real viewer processes with isolated empty settings and no host/input.

The synthetic shell uses the production local-socket protocol. This checks that
concurrent slow connection attempts remain controllable and never create a host
service. It is not video/latency acceptance against a real server.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import select
import socket
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=Path)
args = parser.parse_args()
binary = args.binary.resolve()
with tempfile.TemporaryDirectory(prefix='deskport-worker-test-') as temp:
    root = Path(temp)
    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(root / 'shell'))
    os.chmod(root / 'shell', 0o600)
    listener.listen(2)
    listener.settimeout(15)
    children, sockets, buffers = [], [], {}
    def receive(sock, kind, timeout=5):
        until = time.monotonic() + timeout
        while time.monotonic() < until:
            data = buffers.get(sock, b'')
            if b'\n' in data:
                line, buffers[sock] = data.split(b'\n', 1)
                value = json.loads(line)
                if value.get('type') == kind or kind == 'hello': return value
                continue
            ready, _, _ = select.select([sock], [], [], max(0, until-time.monotonic()))
            if ready:
                chunk = sock.recv(16384)
                if not chunk: raise AssertionError('worker closed IPC unexpectedly')
                buffers[sock] = data + chunk
        raise AssertionError(f'worker did not return {kind}')
    def send(sock, value): sock.sendall(json.dumps(value).encode()+b'\n')
    try:
        for index in range(2):
            state = root / str(index)
            (state / 'DeskPort').mkdir(parents=True)
            (state / 'portable.dat').touch()
            settings = state / 'DeskPort/DeskPort.ini'
            settings.write_text('[diagnostics]\nenabled=false\n[General]\nenablemdns=false\n')
            token = secrets.token_hex(24)
            env = dict(os.environ, DESKPORT_SESSION_ENDPOINT=str(root/'shell'), DESKPORT_SESSION_TOKEN=token,
                       QT_QPA_PLATFORM='offscreen', XDG_CONFIG_HOME=str(state/'config'), XDG_DATA_HOME=str(state/'data'), XDG_CACHE_HOME=str(state/'cache'))
            # No saved host, DNS target, credentials, input device or real stream.
            process = subprocess.Popen([str(binary), '--session-worker', f'fixture-{index}', 'Desktop'], cwd=state, env=env,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
            children.append(process)
            connection, _ = listener.accept()
            sockets.append(connection)
            assert receive(connection, 'hello')['token'] == token
            send(connection, {'command':'initialize'})
        for epoch in range(1, 21):
            foreground = epoch % 2
            old = sockets[1-foreground]
            send(old, {'command':'hide','epoch':epoch})
            assert receive(old, 'hidden')['epoch'] == epoch
            send(sockets[foreground], {'command':'show'})
            assert all(p.poll() is None for p in children)
        # The production IPC gate is independently injectable; no OS event is
        # broadcast and no host/physical input is used by these workers.
        for sleeping, locked in [(True, True), (True, True), (False, True), (False, False)]:
            for connection in sockets:
                send(connection, {'command':'lifecycle','sleeping':sleeping,'locked':locked})
                assert receive(connection, 'lifecycle')['blocked'] == (sleeping or locked)
            assert all(p.poll() is None for p in children)
        send(sockets[0], {'command':'disconnect'})
        assert children[0].wait(timeout=8) == 0
        assert children[1].poll() is None
        # IPC owner disappearing must also terminate its viewer.
        sockets[1].close()
        assert children[1].wait(timeout=8) == 0
        for index in range(2):
            settings = (root/str(index)/'DeskPort/DeskPort.ini').read_text()
            assert 'hosts\\' not in settings and 'certificate=' not in settings
        print('PASS: two real workers, 20 presentation switches while connecting, duplicate sleep/lock/wake gates, independent cancellation and IPC-loss exit; no server connection')
    finally:
        for process in children:
            if process.poll() is None: process.kill()
            process.wait()
        for sock in sockets: sock.close()
        listener.close()
