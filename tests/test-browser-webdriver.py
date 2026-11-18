#!/usr/bin/env python3
"""Strict-TLS browser probe for an already running isolated synthetic fixture.

Uses Safari's native WebDriver or a W3C-compatible Chrome/Edge driver. It never
changes trust, browser permissions, installed apps or native host input. The
caller owns the canonical GUI lock and must provision certificate trust first.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fixture', type=Path, required=True)
parser.add_argument('--driver', type=Path, required=True)
parser.add_argument('--browser', choices=['safari', 'chrome', 'MicrosoftEdge'], required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--auth-only', action='store_true')
args = parser.parse_args()
fixture = json.loads(args.fixture.read_text())
url = urllib.parse.urlparse(fixture['url'])
if url.scheme != 'https' or url.hostname not in ('localhost', '127.0.0.1'):
    parser.error('Only loopback HTTPS synthetic fixtures are allowed; forward the fixture without changing its certificate name')
if fixture.get('isolatedSynthetic') is not True or fixture.get('nativeInputEnabled') is not False:
    parser.error('The fixture must explicitly declare isolatedSynthetic=true and nativeInputEnabled=false')
args.output.mkdir(parents=True, exist_ok=False)
args.output.chmod(0o700)
temporary = args.output.resolve() / 'tmp'
temporary.mkdir()
with socket.socket() as probe:
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
endpoint = f'http://127.0.0.1:{port}'
checks, session, driver, result = [], None, None, {}
media = {}

def request(method, path, body=None):
    req = urllib.request.Request(endpoint + path, method=method,
        data=None if body is None else json.dumps(body).encode(), headers={'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=35) as response:
            return json.load(response)['value']
    except urllib.error.HTTPError as error:
        value = json.loads(error.read()).get('value', {})
        # Do not log commands, cookies, TOTP, SDP or page text.
        code = value.get('error', 'webdriver error')
        if 'Allow remote automation' in value.get('message', ''):
            code += ': Safari Allow remote automation is disabled'
        raise RuntimeError(code) from None

def command(method, path, body=None):
    return request(method, '/session/' + session + path, body)

def execute(script, *values):
    return command('POST', '/execute/sync', {'script': script, 'args': list(values)})

def media_stats():
    return command('POST', '/execute/async', {'script': '''
        const done = arguments[arguments.length - 1];
        const peer = window.fixturePeers?.at(-1);
        if (!peer) { done({}); return; }
        peer.getStats().then(stats => {
            const result = {};
            stats.forEach(report => {
                if (report.type !== 'inbound-rtp') return;
                const kind = report.kind || report.mediaType;
                if (kind !== 'audio' && kind !== 'video') return;
                result[kind] = { codec: stats.get(report.codecId)?.mimeType,
                    packets: report.packetsReceived || 0, frames: report.framesDecoded || 0 };
            });
            done(result);
        }, () => done({}));
    ''', 'args': []})

def wait(script, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if execute(script):
            return
        time.sleep(.1)
    raise TimeoutError('Synthetic fixture condition did not settle')

def element(selector):
    return command('POST', '/element', {'using': 'css selector', 'value': selector})['element-6066-11e4-a52e-4f735466cecf']

def click(selector):
    command('POST', '/element/' + element(selector) + '/click', {})

def check(name, condition=True):
    if not condition:
        raise AssertionError(name)
    checks.append(name)

try:
    with (args.output / 'driver.log').open('wb') as log:
        driver = subprocess.Popen([str(args.driver), *(['-p', str(port)] if args.browser == 'safari' else ['--port=' + str(port)])],
            stdout=log, stderr=log, env=dict(os.environ, TMPDIR=str(temporary), TMP=str(temporary), TEMP=str(temporary)))
        for _ in range(100):
            try:
                request('GET', '/status')
                break
            except OSError:
                if driver.poll() is not None:
                    raise RuntimeError('WebDriver exited before readiness')
                time.sleep(.1)
        value = request('POST', '/session', {'capabilities': {'alwaysMatch': {
            'browserName': args.browser, 'acceptInsecureCerts': False}}})
        session = value['sessionId']
        caps = value.get('capabilities', {})
        check('browser certificate validation remains enabled', caps.get('acceptInsecureCerts') is False)
        command('POST', '/timeouts', {'script': 20000, 'pageLoad': 30000, 'implicit': 0})
        command('POST', '/url', {'url': fixture['url']})
        wait("return document.getElementById('connection-status')?.dataset.state==='idle'")
        check('production frontend loads in a secure context', execute('return isSecureContext && location.protocol === "https:"'))
        check('WebRTC is available', execute('return typeof RTCPeerConnection === "function"'))
        if not args.auth_only:
            execute('''const NativePeer = window.RTCPeerConnection;
                window.fixturePeers = [];
                window.RTCPeerConnection = class extends NativePeer {
                    constructor(...args) { super(...args); window.fixturePeers.push(this); }
                };''')
        # Enrollment uses the actual form. The driver never prints its value.
        click('#remember-browser')
        command('POST', '/element/' + element('#access-code') + '/value', {'text': fixture['code']})
        if args.auth_only:
            # The auth-only gateway may reject media; enrollment must still persist.
            click('#connect')
            wait("return document.body.dataset.paired==='true' && document.getElementById('connection-status').dataset.state==='idle'")
        else:
            click('#connect')
            wait("return document.getElementById('connection-status').dataset.state==='streaming' || !document.getElementById('resume-audio').hidden", 35)
            if execute("return !document.getElementById('resume-audio').hidden"):
                click('#resume-audio')
            wait("return document.getElementById('connection-status').dataset.state==='streaming'", 25)
            check('real video element is playing', execute("const v=document.getElementById('desktop-video');return v.videoWidth>0 && !v.paused"))
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                media = media_stats()
                if media.get('video', {}).get('frames', 0) > 0 and media.get('audio', {}).get('packets', 0) > 0:
                    break
                time.sleep(.1)
            check('real H.264 frames decoded', media.get('video', {}).get('codec', '').lower() == 'video/h264' and media.get('video', {}).get('frames', 0) > 0)
            check('real Opus packets received', media.get('audio', {}).get('codec', '').lower() == 'audio/opus' and media.get('audio', {}).get('packets', 0) > 0)
            check('audio playback starts after the available gesture', execute("const v=document.getElementById('desktop-video');return !v.muted && !v.paused"))
            click('#disconnect')
            wait("return document.getElementById('connection-status').dataset.state==='idle'")
            check('disconnect clears media', execute("return document.getElementById('desktop-video').srcObject===null"))
        check('enrollment retains the paired browser', execute("return document.body.dataset.paired==='true'"))
        check('page cannot read pairing credentials', execute("return !document.cookie.includes('deskport')"))
        command('POST', '/refresh', {})
        wait("return document.body.dataset.paired==='true' && document.getElementById('connection-status').dataset.state==='idle'")
        check('refresh restores pairing without media', execute("return document.getElementById('desktop-video').srcObject===null"))
        click('#forget-pairing')
        wait("return document.body.dataset.paired==='false' && document.getElementById('connection-status').dataset.state==='idle'")
        check('explicit forget removes synthetic pairing')
        result = {'passed': True, 'browser': caps.get('browserName'), 'version': caps.get('browserVersion')}
except Exception as error:
    result = {'passed': False, 'error': str(error), 'browser': args.browser}
    if session:
        try:
            # Only structural state: never page text, credentials or network addresses.
            result['pageState'] = execute('''return {
                ready: document.readyState, secure: isSecureContext,
                expectedOrigin: location.origin === new URL(arguments[0]).origin,
                hasProductStatus: !!document.getElementById('connection-status'),
                state: document.getElementById('connection-status')?.dataset.state || null
            };''', fixture['url'])
            screenshot = command('GET', '/screenshot')
            (args.output / 'failure.png').write_bytes(base64.b64decode(screenshot, validate=True))
        except Exception:
            result['diagnosticsUnavailable'] = True
finally:
    if session:
        try:
            command('DELETE', '')
        except Exception:
            pass
    if driver and driver.poll() is None:
        driver.terminate()
        try:
            driver.wait(timeout=10)
        except subprocess.TimeoutExpired:
            driver.kill()
            driver.wait(timeout=5)
    result.update(checks=checks, count=len(checks), media=media, ownedDriverStopped=driver is None or driver.poll() is not None,
        scope='Native browser probe only on caller-owned synthetic fixture; no OS input, display restoration, physical touch, audio quality or latency acceptance')
    (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result))
raise SystemExit(0 if result['passed'] else 1)
