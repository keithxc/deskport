#!/usr/bin/env python3
"""Build/run isolated browser transport and input checks, without desktop capture.

Pass a libdatachannel installation prefix. Generated binaries stay in the shared
DeskPort build root (or --output). The real transport check uses loopback ICE,
DTLS, SRTP and a DataChannel; it does not establish display/encoder acceptance.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--rtc-prefix', type=Path, required=True)
parser.add_argument('--json-include', type=Path)
parser.add_argument('--cross-windows', action='store_true', help='Build static Windows test executables only; run them separately on an isolated Windows host')
parser.add_argument('--openssl-prefix', type=Path, help='Windows static OpenSSL prefix; required with --cross-windows')
parser.add_argument('--output', type=Path, default=Path(os.environ.get('DESKPORT_BUILD_ROOT', str(Path.home()/'mygit/build/deskport'))) / 'browser-client/media/tests')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
args.output.mkdir(parents=True, exist_ok=True)
compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++')
if args.cross_windows:
    if not args.openssl_prefix:
        parser.error('--cross-windows requires --openssl-prefix')
    target = subprocess.check_output([compiler, '-dumpmachine'], text=True).strip()
    if 'mingw32' not in target:
        parser.error('--cross-windows requires a MinGW compiler in CXX')
includes = ['-I'+str(args.rtc_prefix/'include'), '-I'+str(repo/'moonlight-common-c')]
if args.json_include:
    includes.append('-I'+str(args.json_include))
common = [compiler, '-std=c++20', '-DRTC_ENABLE_MEDIA=1'] + includes
if args.cross_windows:
    # The locked MinGW compiler uses mcfgthread, not a pthread import library.
    common += ['-DRTC_STATIC', '-DRTC_ENABLE_WEBSOCKET=0', '-static']
suffix = '.exe' if args.cross_windows else ''
input_binary = args.output/('input-codec-test'+suffix)
subprocess.run(common + [str(repo/'host/browser/tests/input-codec.cpp'), '-o', str(input_binary)], check=True)
if not args.cross_windows:
    subprocess.run([str(input_binary)], check=True, timeout=10)
transport_binary = args.output/('rtc-transport-test'+suffix)
libraries = ['-L'+str(args.rtc_prefix/'lib'), '-ldatachannel']
if args.cross_windows:
    libraries += ['-L'+str(args.openssl_prefix/'lib'), '-lusrsctp', '-lsrtp2', '-ljuice', '-lssl', '-lcrypto',
                  '-lws2_32', '-liphlpapi', '-lbcrypt', '-lcrypt32']
else:
    libraries += ['-Wl,-rpath,'+str(args.rtc_prefix/'lib')]
subprocess.run(common + [str(repo/'host/browser/rtc-session.cpp'), str(repo/'host/browser/tests/rtc-transport.cpp'),
    *libraries, '-o', str(transport_binary)], check=True)
if args.cross_windows:
    print('Windows fixtures built only; native execution and browser/host acceptance remain separate checks')
else:
    subprocess.run([str(transport_binary)], check=True, timeout=40)
