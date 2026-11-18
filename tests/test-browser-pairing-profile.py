#!/usr/bin/env python3
"""Test pairing with a real installed browser and an isolated HTTPS gateway.

This fixture intentionally has no capture host. It never changes the user's
browser profile, desktop, native input, or services; Linux native capture is
covered separately by test-browser-pairing.py.
"""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import argparse
import json
import os
from pathlib import Path
import select
import shutil
import signal
import socketserver
import subprocess
import tempfile
import threading
import time

parser = argparse.ArgumentParser(description=__doc__)
for field in ('gateway','chromium','node','playwright-module','output'):
    parser.add_argument('--'+field,type=Path,required=True)
parser.add_argument('--openssl',type=Path,default=shutil.which('openssl') or '/usr/bin/openssl')
args = parser.parse_args()
os.umask(0o077)
output=args.output.resolve(); output.mkdir(parents=True,exist_ok=False)
runtime_root=_build_storage.Storage().paths['root']/'t'; runtime_root.mkdir(parents=True,exist_ok=True)
runtime=Path(tempfile.mkdtemp(prefix='pair-',dir=runtime_root))
processes=[]; logs=[]; gateway=None; server=None; thread=None
summary={'fixture':'Real installed browser + production HTTPS gateway, auth-only','passed':False}
def spawn(name,command,**kwargs):
    log=(output/(name+'.log')).open('wb'); logs.append(log)
    child=subprocess.Popen([str(item) for item in command],stderr=log,start_new_session=True,**kwargs)
    processes.append(child); return child
def read_line(child):
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        if child.poll() is not None: raise RuntimeError('Fixture gateway exited')
        if select.select([child.stdout],[],[],0.2)[0]: return json.loads(child.stdout.readline())
    raise TimeoutError('Fixture gateway readiness timed out')
try:
    key=output/'unused-upstream-key.pem'; cert=output/'unused-upstream-cert.pem'
    subprocess.run([str(args.openssl),'req','-x509','-newkey','rsa:2048','-nodes','-days','1',
        '-subj','/CN=DeskPort isolated pairing fixture','-keyout',str(key),'-out',str(cert)],
        check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    upstream=output/'upstream.json'
    upstream.write_text(json.dumps({'url':'https://127.0.0.1:9/api/deskport/browser',
        'certificate':str(cert),'username':'fixture','password':'unused-isolated-fixture','forceInputDisabled':True}))
    def start(port):
        child=spawn('gateway-'+str(len(processes)),[args.gateway,'--fixture',upstream,'--state',output/'gateway-state',
            '--bind','127.0.0.1','--port',str(port)],stdout=subprocess.PIPE,stdin=subprocess.PIPE)
        return child,read_line(child)
    gateway,ready=start(0)
    def control(body):
        global gateway
        if body.get('action')=='restart':
            gateway.stdin.write(b'quit\n'); gateway.stdin.flush(); gateway.wait(timeout=20)
            gateway,next_ready=start(ready['port'])
            if next_ready['port']!=ready['port'] or next_ready['authenticator']!=ready['authenticator']:
                raise RuntimeError('Restart changed gateway identity')
            return {'ok':True,'port':next_ready['port']}
        gateway.stdin.write(json.dumps(body).encode()+b'\n'); gateway.stdin.flush(); return read_line(gateway)
    class Handler(socketserver.StreamRequestHandler):
        def handle(self):
            try: result=control(json.loads(self.rfile.readline(8192)))
            except Exception as error: result={'ok':False,'error':str(error)}
            self.wfile.write(json.dumps(result).encode()+b'\n')
    socket=runtime/'control.sock'
    server=socketserver.UnixStreamServer(str(socket),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
    fixture=output/'browser.json'
    fixture.write_text(json.dumps({'url':f'https://127.0.0.1:{ready["port"]}/','code':ready['code'],
        'chromium':str(args.chromium),'outputDir':str(output),'controlSocket':str(socket),'authOnly':True}))
    driver_log=(output/'browser-driver.log').open('wb'); logs.append(driver_log)
    driver=spawn('driver-stderr',[args.node,Path(__file__).with_suffix('.mjs').with_name('test-browser-pairing.mjs'),
        '--fixture',fixture],stdout=driver_log,env=dict(os.environ,DESKPORT_PLAYWRIGHT_MODULE=str(args.playwright_module)))
    if driver.wait(timeout=300)!=0: raise RuntimeError('Real browser pairing driver failed')
    summary['passed']=True
except Exception as error: summary['error']=str(error)
finally:
    if server: server.shutdown(); server.server_close(); thread.join(timeout=25)
    if gateway and gateway.poll() is None:
        try: gateway.stdin.write(b'quit\n'); gateway.stdin.flush(); gateway.wait(timeout=20)
        except (OSError,subprocess.TimeoutExpired): pass
    for child in reversed(processes):
        if child.poll() is None:
            try: os.killpg(child.pid,signal.SIGTERM); child.wait(timeout=10)
            except subprocess.TimeoutExpired: os.killpg(child.pid,signal.SIGKILL); child.wait(timeout=5)
            except ProcessLookupError: pass
    summary['ownedProcessesStopped']=all(child.poll() is not None for child in processes)
    if summary['ownedProcessesStopped']: shutil.rmtree(runtime)
    for log in logs: log.close()
    (output/'profile-fixture-result.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'passed':summary['passed'],'error':summary.get('error'),'output':str(output)}))
raise SystemExit(0 if summary['passed'] else 1)
