#!/usr/bin/env python3
"""Check the packaged helper entry point using only an offscreen clipboard."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import base64
import json
import os
from pathlib import Path
import select
import subprocess
import sys

binary = str(Path(sys.argv[1]).resolve())
env = dict(os.environ, QT_QPA_PLATFORM="offscreen")

def start():
    return subprocess.Popen([binary, "--clipboard-helper", "--clipboard-helper-host"], env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

process = start()
try:
    message = {"type": "clipboard-v2-offer", "id": "synthetic-test", "kind": "text", "base": 0,
               "text": base64.b64encode("Synthetic 中文 clipboard".encode()).decode()}
    process.stdin.write(json.dumps(message).encode() + b"\n")
    process.stdin.flush()
    if not select.select([process.stdout], [], [], 10)[0]:
        raise RuntimeError("packaged helper did not respond")
    reply = json.loads(process.stdout.readline())
    assert reply["type"] == "clipboard-v2-offer" and reply["kind"] == "ack" and reply["rev"] == 1, reply
    process.stdin.close()
    assert process.wait(timeout=10) == 0
finally:
    if process.poll() is None:
        process.kill(); process.wait()

process = start()
try:
    process.stdin.write(b"invalid-json\n"); process.stdin.flush()
    assert process.wait(timeout=10) == 4
finally:
    if process.poll() is None:
        process.kill(); process.wait()
print("PASS: packaged native helper protocol entry, ordered acknowledgement, EOF cleanup and malformed-frame exit")
