#!/usr/bin/env python3
"""Isolated Linux capture -> encoder -> WebRTC -> browser acceptance fixture.

Owns Xvfb, a private PulseAudio null sink, a test Sunshine instance, and a real
HTTPS BrowserGateway. Never installs/restarts services, uses the user's display,
or enables native input. Only subprocesses created by this script are terminated.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import secrets
import select
import shutil
import signal
import socket
import socketserver
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--host', type=Path, required=True)
parser.add_argument('--gateway', type=Path, required=True)
parser.add_argument('--chromium', type=Path, required=True)
parser.add_argument('--pulse-prefix', type=Path, required=True)
parser.add_argument('--node', type=Path, required=True)
parser.add_argument('--playwright-module', type=Path, required=True)
parser.add_argument('--openssl', type=Path, default=shutil.which('openssl') or 'openssl')
parser.add_argument('--certutil', type=Path, default=shutil.which('certutil'), help='NSS certutil for the default strict browser driver')
parser.add_argument('--fixture-certificate', type=Path, help='Caller-owned synthetic loopback certificate for native platform trust')
parser.add_argument('--fixture-key', type=Path, help='Matching private synthetic key; requires --fixture-certificate')
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--runtime-root', type=Path, default=Path.home()/'mygit/build/deskport/t')
parser.add_argument('--driver', type=Path, help='Optional real-browser driver; defaults to test-browser-native.mjs')
parser.add_argument('--pairing-control', action='store_true', help='Enable a private test-only gateway restart/revocation control socket')
parser.add_argument('--webkit-driver', type=Path, help='Optional existing WebKitGTK WebDriver for an additional pairing probe')
args = parser.parse_args()
if bool(args.fixture_certificate) != bool(args.fixture_key):
    parser.error('--fixture-certificate and --fixture-key must be supplied together')
if args.driver is None and not args.certutil:
    parser.error('The default browser driver requires --certutil for private NSS trust; TLS bypass is not supported')
if args.webkit_driver and not args.pairing_control:
    parser.error('--webkit-driver requires the pairing fixture/control socket')
if sys.platform != 'linux':
    raise SystemExit('The isolated Xvfb/native capture fixture requires Linux')
repo = Path(__file__).resolve().parents[1]
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
os.umask(0o077)
processes, logs = [], []
environment = dict(os.environ)
for key in ['DISPLAY', 'WAYLAND_DISPLAY', 'DBUS_SESSION_BUS_ADDRESS', 'DESKPORT_ON_DEMAND_DISPLAY',
            'DESKPORT_CAPTURE_DISPLAY_FILE', 'DESKPORT_LINUX_DISPLAY_FILE']:
    environment.pop(key, None)
args.runtime_root.mkdir(parents=True, exist_ok=True)
runtime = Path(tempfile.mkdtemp(prefix='br-', dir=args.runtime_root))
environment.update(XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(output/'config'),
                   XDG_DATA_HOME=str(output/'data'), XDG_CACHE_HOME=str(output/'cache'),
                   DBUS_SYSTEM_BUS_ADDRESS='unix:path='+str(runtime/'no-system-bus'),
                   XDG_SESSION_TYPE='x11', XDG_CURRENT_DESKTOP='DeskPortBrowserFixture')

def spawn(name, command, *, stdout=None, stdin=None, env=None):
    log = (output/(name+'.log')).open('wb'); logs.append(log)
    process = subprocess.Popen([str(item) for item in command], env=env or environment,
                               stdout=stdout if stdout is not None else log, stderr=log,
                               stdin=stdin, start_new_session=True)
    processes.append((name, process))
    return process

def line(process, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None: raise RuntimeError(f'Fixture process exited {process.returncode}')
        readable, _, _ = select.select([process.stdout], [], [], 0.2)
        if readable:
            value = process.stdout.readline()
            if value: return value.decode().strip()
    raise TimeoutError('Fixture process did not report readiness')

def wait_for(predicate, description, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            if predicate(): return
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.1)
    raise TimeoutError(description)

def free_family():
    for _ in range(100):
        base = 50000 + secrets.randbelow(80)*50
        held = []
        try:
            for offset in (-5,0,1,21):
                sock = socket.socket(); sock.bind(('127.0.0.1',base+offset)); held.append(sock)
            for offset in (9,10,11,13):
                sock = socket.socket(socket.AF_INET,socket.SOCK_DGRAM); sock.bind(('127.0.0.1',base+offset)); held.append(sock)
            return base
        except OSError: pass
        finally:
            for sock in held: sock.close()
    raise RuntimeError('No isolated host port family available')

summary = {'fixture':'Xvfb + private PulseAudio + bundled Sunshine + production HTTPS gateway',
           'nativeInputEnabled':False,'installedServicesModified':False,
           'browserCertificateValidation':'Enabled with private NSS fixture trust and Chromium sandbox' if args.driver is None else 'Custom driver: consult its certificate-validation result',
           'upstreamCertificateValidation':'Exact fixture certificate pin'}
gateway = None
control_server = control_thread = None
try:
    xvfb = spawn('xvfb', [shutil.which('Xvfb'), '-displayfd','1','-screen','0','1280x720x24',
                          '-nolisten','tcp','-ac'], stdout=subprocess.PIPE)
    display = line(xvfb)
    if not display.isdigit(): raise RuntimeError('Xvfb returned invalid display')
    environment['DISPLAY'] = ':'+display
    pulse_socket = runtime/'pulse.sock'
    pulse = spawn('pulse', [args.pulse_prefix/'bin/pulseaudio','-n','--daemonize=no','--use-pid-file=no',
                           '--exit-idle-time=-1','--log-target=stderr',
                           '--load=module-native-protocol-unix socket='+str(pulse_socket)+' auth-anonymous=1',
                           '--load=module-null-sink sink_name=deskport_browser_fixture channels=2 rate=48000'])
    wait_for(pulse_socket.exists,'Private PulseAudio socket missing')
    environment['PULSE_SERVER'] = 'unix:'+str(pulse_socket)
    screen = output/'screen.py'
    screen.write_text('''import time
colors = [41, 42, 43, 44, 45, 46, 100, 101, 102, 103, 104, 105, 106, 107]
for frame in range(10000):
 print("\\033[2J\\033[H\\033[44;97m DESKPORT ISOLATED BROWSER CAPTURE \\033[0m\\nReal X11 capture and H.264 encoding\\nFrame marker: %06d\\nNo user desktop or native input is used.\\n" % frame)
 for row in range(25):
  print(''.join("\\033[%dm          " % colors[(column + row + frame) % len(colors)] for column in range(12)) + "\\033[0m")
 print(flush=True)
 time.sleep(.2)
''')
    spawn('xterm',[shutil.which('xterm'),'-geometry','120x38+0+0','-fa','monospace','-fs','15',
                   '-title','DeskPort isolated browser media test','-e',sys.executable,screen])
    tone = spawn('tone',[shutil.which('ffmpeg'),'-hide_banner','-loglevel','error','-re','-f','lavfi',
                         '-i','sine=frequency=440:sample_rate=48000','-ac','2','-f','s16le','pipe:1'], stdout=subprocess.PIPE)
    spawn('play-tone',[args.pulse_prefix/'bin/pacat','--playback','--raw','--rate=48000','--channels=2',
                       '--format=s16le','--device=deskport_browser_fixture'],stdin=tone.stdout)
    tone.stdout.close()
    base = free_family()
    key, cert = output/'host-key.pem', output/'host-cert.pem'
    if args.fixture_certificate:
        shutil.copyfile(args.fixture_certificate, cert)
        shutil.copyfile(args.fixture_key, key)
    else:
        subprocess.run([str(args.openssl),'req','-x509','-newkey','rsa:2048','-nodes','-days','1',
                        '-subj','/CN=DeskPort browser isolated fixture','-addext','subjectAltName=IP:127.0.0.1',
                        '-addext','basicConstraints=critical,CA:TRUE',
                        '-keyout',str(key),'-out',str(cert)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    apps = output/'apps.json'; apps.write_text(json.dumps({'env':{},'apps':[{'name':'Desktop'}]}))
    config = output/'sunshine.conf'
    config.write_text('\n'.join([
        'sunshine_name = DeskPort Browser Isolated Fixture','port = '+str(base),'address_family = ipv4',
        'bind_address = 127.0.0.1','upnp = disabled','system_tray = disabled','min_log_level = 1',
        'origin_web_ui_allowed = pc','capture = x11','encoder = software','hevc_mode = 1','av1_mode = 1',
        'sw_preset = ultrafast','keyboard = disabled','mouse = disabled','controller = disabled',
        'audio_sink = deskport_browser_fixture','virtual_sink = deskport_browser_fixture',
        'file_apps = '+str(apps),'file_state = '+str(output/'host-state.json'),
        'credentials_file = '+str(output/'host-creds.json'),'pkey = '+str(key),'cert = '+str(cert),
        'log_path = '+str(output/'sunshine-internal.log'),''
    ]))
    password = secrets.token_hex(24)
    with (output/'credentials-setup.log').open('wb') as log:
        subprocess.run([str(args.host),str(config),'--creds','deskport',password],env=environment,
                       stdout=log,stderr=subprocess.STDOUT,check=True,timeout=20)
    host = spawn('sunshine',[args.host,config])
    endpoint = f'https://127.0.0.1:{base+1}/api/deskport/browser'
    context = ssl.create_default_context(cafile=str(cert))
    basic = base64.b64encode(('deskport:'+password).encode()).decode()
    def post(body):
        request = urllib.request.Request(endpoint,data=json.dumps(body).encode(),
            headers={'Content-Type':'application/json','Authorization':'Basic '+basic})
        with urllib.request.urlopen(request,context=context,timeout=20) as response: return json.load(response)
    check_id = secrets.token_hex(16)
    wait_for(lambda: post({'action':'reserve','id':check_id}).get('status') is True,'Sunshine browser endpoint unavailable',40)
    if not post({'action':'release','id':check_id}).get('status'): raise RuntimeError('Initial reservation could not release')
    private = output/'gateway-upstream.json'
    private.write_text(json.dumps({'url':endpoint,'certificate':str(cert),'username':'deskport',
                                  'password':password,'forceInputDisabled':True}))
    gateway = spawn('gateway',[args.gateway,'--fixture',private,'--state',output/'gateway-state','--certificate',cert,'--key',key,
                               '--bind','127.0.0.1','--port','0'],stdout=subprocess.PIPE,stdin=subprocess.PIPE)
    ready = json.loads(line(gateway))
    if not ready.get('code') or not ready.get('port'): raise RuntimeError('Real HTTPS gateway failed to start')
    control_path = runtime/'control.sock'
    def control(body):
        global gateway
        # Ordinary media reconnects need a new one-time code. Restart/revocation
        # controls remain opt-in for the separate durable-pairing scenario.
        if not args.pairing_control and body.get('action') not in ('next-code', 'status'):
            return {'ok':False,'error':'Pairing control was not enabled'}
        if body.get('action') == 'restart':
            gateway.stdin.write(b'quit\n'); gateway.stdin.flush(); gateway.wait(timeout=20)
            gateway = spawn('gateway-restarted', [args.gateway,'--fixture',private,'--state',output/'gateway-state',
                '--bind','127.0.0.1','--port',str(ready['port'])], stdout=subprocess.PIPE,stdin=subprocess.PIPE)
            restarted = json.loads(line(gateway))
            if restarted.get('port') != ready['port'] or restarted.get('authenticator') != ready['authenticator']:
                raise RuntimeError('Restart did not preserve the gateway identity and port')
            return {'ok':True,'port':restarted['port']}
        gateway.stdin.write(json.dumps(body).encode()+b'\n'); gateway.stdin.flush()
        return json.loads(line(gateway))
    class ControlHandler(socketserver.StreamRequestHandler):
        def handle(self):
            try:
                body = json.loads(self.rfile.readline(8192))
                result = control(body)
            except Exception as error: result = {'ok':False,'error':str(error)}
            self.wfile.write(json.dumps(result).encode()+b'\n')
    control_server = socketserver.UnixStreamServer(str(control_path), ControlHandler)
    control_thread = threading.Thread(target=control_server.serve_forever, daemon=True)
    control_thread.start()
    browser_fixture = output/'browser.json'
    browser_fixture.write_text(json.dumps({'url':f'https://127.0.0.1:{ready["port"]}/',
        'code':ready['code'],'chromium':str(args.chromium),'outputDir':str(output),
        'isolatedSynthetic':True,'nativeInputEnabled':False,
        'certificate':str(cert),'certutil':str(args.certutil) if args.certutil else None,
        'controlSocket':str(control_path)}))
    driver_env = dict(environment,DESKPORT_PLAYWRIGHT_MODULE=str(args.playwright_module))
    driver = spawn('browser-driver',[args.node,args.driver or repo/'tests/test-browser-native.mjs','--fixture',browser_fixture],env=driver_env)
    if driver.wait(timeout=300 if args.pairing_control else 100) != 0: raise RuntimeError('Browser driver failed; see browser-driver.log and its result JSON')
    if args.webkit_driver:
        webkit = spawn('webkit-driver', [sys.executable,repo/'tests/test-browser-pairing-webkit.py',
            '--fixture',browser_fixture,'--webdriver',args.webkit_driver])
        summary['webkitPassed'] = webkit.wait(timeout=150) == 0
    # The browser Stop action must have cleared both capture/input and admission.
    final_id = secrets.token_hex(16)
    free = post({'action':'reserve','id':final_id})
    summary['reservationReleasedAfterBrowserStop'] = free.get('status') is True
    if not summary['reservationReleasedAfterBrowserStop']: raise RuntimeError('Browser left admission occupied')
    post({'action':'release','id':final_id})
    summary['passed'] = True
    summary['hostBinary'] = str(args.host.resolve()); summary['display'] = environment['DISPLAY']
    print(json.dumps({'passed':True,'output':str(output)}),flush=True)
except Exception as error:
    summary.update(passed=False,error=str(error))
    print(json.dumps({'passed':False,'error':str(error),'output':str(output)}),flush=True)
finally:
    if control_server:
        control_server.shutdown(); control_server.server_close(); control_thread.join(timeout=25)
    if gateway and gateway.poll() is None:
        try: gateway.stdin.write(b'quit\n'); gateway.stdin.flush(); gateway.wait(timeout=15)
        except (OSError,subprocess.TimeoutExpired): pass
    for name, process in reversed(processes):
        if process.poll() is None:
            try: os.killpg(process.pid,signal.SIGTERM); process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid,signal.SIGKILL); process.wait(timeout=5)
            except ProcessLookupError: pass
    summary['ownedProcessesStopped'] = all(process.poll() is not None for _,process in processes)
    if summary['ownedProcessesStopped']: shutil.rmtree(runtime)
    (output/'native-fixture-result.json').write_text(json.dumps(summary,indent=2)+'\n')
    for log in logs: log.close()
sys.exit(0 if summary.get('passed') else 1)
