#!/usr/bin/env python3
"""Install release packages in clean distribution containers and run isolated smoke checks."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import json
import pathlib
import subprocess
import sys

work = pathlib.Path(sys.argv[1]).resolve()
version = sys.argv[2]
scripts = pathlib.Path(__file__).resolve().parents[1] / 'scripts'
cases = [
    ('ubuntu-24.04', 'docker.io/library/ubuntu:24.04',
     f'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3 /packages/deskport_{version}_amd64.deb'),
    ('debian-13', 'docker.io/library/debian:13',
     f'apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3 /packages/deskport_{version}_amd64.deb'),
    ('fedora-44', 'docker.io/library/fedora:44',
     f'dnf install -y python3 /packages/deskport-{version}.x86_64.rpm'),
    ('arch', 'docker.io/library/archlinux:latest',
     f'pacman -Syu --noconfirm python && pacman -U --noconfirm /packages/deskport-{version}-x86_64.pkg.tar.zst'),
]
results = []
for name, image, install in cases:
    command = install + f' && python3 /checks/check-linux-package.py /opt/deskport {version}'
    log = work / f'install-test-{name}.log'
    with log.open('w') as stream:
        result = subprocess.run(['podman', 'run', '--rm', '-v', str(work / 'output') + ':/packages:ro',
                                 '-v', str(scripts) + ':/checks:ro', image, 'bash', '-lc', command],
                                stdout=stream, stderr=subprocess.STDOUT)
    results.append({'distribution': name, 'image': image, 'passed': result.returncode == 0})
    print(name, 'PASS' if result.returncode == 0 else f'FAIL: {log}', flush=True)
(work / 'installer-validation.json').write_text(json.dumps(results, indent=2) + '\n')
if not all(item['passed'] for item in results):
    raise SystemExit(1)
