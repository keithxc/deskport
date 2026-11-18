#!/usr/bin/env python3
"""Run production broker policy and verify installer/build integration, without Windows."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import argparse
import os
import subprocess

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-directory', type=Path, required=True)
args = parser.parse_args()
out = args.output_directory.resolve()
out.mkdir(parents=True, exist_ok=True)
binary = out / 'windows-display-service-policy'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(root), str(root / 'tests/windows-display-service.cpp'), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)

# Execute the actual guardian cancellation predicate and resize-enable branch
# against failing API doubles. A pending resize must never defeat stop/death.
recovery = (root / 'host/windows/display-recovery.cpp').read_text()
start = recovery.index('    const auto alive=[&]{')
predicate = recovery[start:recovery.index('\n    };', start) + len('\n    };')]
start = recovery.index('                            if(!resized&&alive())')
enable = recovery[start:recovery.index('                            mode->result=', start)]
unit = out / 'guardian-cancellation.cpp'
unit.write_text(r'''
#include <cassert>
#include <string>
constexpr int WAIT_TIMEOUT=258, ERROR_CANCELLED=1223;
int parent=1,release=2,brokerStop=3,brokerProcess=4;
bool signaled[5]{};
int WaitForSingleObject(int h,int){return signaled[h]?0:WAIT_TIMEOUT;}
int GetCurrentProcessId(){return 123;}
int main(){
''' + predicate + r'''
for(unsigned bits=0;bits<16;++bits){
 for(int i=1;i<=4;++i)signaled[i]=bool(bits&(1u<<(i-1)));
 assert(alive()==(bits==0));
 int calls=0,resized=0;std::wstring nonce=L"fixture";
 auto runWorker=[&](const std::wstring&){++calls;return 0;};
''' + enable + r'''
 assert(calls==(bits==0?1:0));
 assert(resized==(bits==0?0:ERROR_CANCELLED));
}
brokerStop=brokerProcess=0;signaled[1]=signaled[2]=false;
assert(alive());signaled[2]=true;assert(!alive());
}
''')
binary = out / 'guardian-cancellation'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                str(unit), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
assert recovery.index('if(!alive())break;') < recovery.index('if(wait==WAIT_OBJECT_0+2)')
print('PASS production guardian cancellation priority and no re-enable after parent/release/broker stop/death')

# Execute the actual restore method with a mode-dependent recommendation.
# Restoring 125% at a 125%-recommended mode writes step 0, which would become
# 100% after returning to a 100%-recommended baseline. Retirement needs step 1.
start = recovery.index('    DWORD restore(bool retire=true,bool retiringSource=false) {')
method = recovery[start:recovery.index('\n};', start)]
unit = out / 'dpi-retirement.cpp'
unit.write_text(r"""
#include <cassert>
#include <string>
#include "host/windows/display-dpi-policy.h"
using DWORD=unsigned;
constexpr DWORD ERROR_FILE_NOT_FOUND=2,ERROR_NOT_SUPPORTED=50;
unsigned now=0;int deleted=0,lastPercentage=0;bool fail=false;
unsigned GetTickCount64(){return now;}
void Sleep(unsigned ms){now+=ms;}
bool DeleteFileW(const wchar_t*){++deleted;return true;}
DWORD GetLastError(){return 5;}
struct Lease {
 std::wstring journal=L"fixture";
 DWORD load(int& percentage,int* step){percentage=125;*step=1;return 0;}
 DWORD apply(int value,bool relative){
  if(fail)return ERROR_NOT_SUPPORTED;
  deskport::display_dpi::Range range{-1,0,5};int step=value;
  if(!relative)assert(deskport::display_dpi::relative(range,value,step));
  range.current=step;lastPercentage=deskport::display_dpi::percent(range);return 0;
 }
""" + method + r"""
};
int main(){
 Lease lease;
 assert(!lease.restore(false,true));assert(lastPercentage==150&&deleted==0);
 // The persisted step is reinterpreted by Windows at the original mode.
 assert(deskport::display_dpi::percent({0,1,3})==125);
 assert(!lease.restore(true,false));assert(lastPercentage==125&&deleted==1);
 assert(!lease.restore(true,true));assert(lastPercentage==150&&deleted==2);
 fail=true;assert(lease.restore(true,true)==ERROR_NOT_SUPPORTED);assert(deleted==2&&now>=3000);
}
""")
binary = out / 'dpi-retirement'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(root), str(unit), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS production DPI restore preserves relative offsets across recommendation changes and retains failed journals')

# Windows can return a successful but detached 0x0 registry mode after adapter
# recreation. Exercise the production enumeration fallback with that fixture.
helper = (root / 'host/windows/display-helper.cpp').read_text()
listed = helper[helper.index('bool listedMode('):helper.index('bool writeState(')]
unit = out / 'listed-mode.cpp'
unit.write_text(r'''
#include <cassert>
#include <cstdint>
#include <vector>
using DWORD=std::uint32_t;using LPCWSTR=const wchar_t*;
struct DEVMODEW { DWORD dmSize=0,dmPelsWidth=0,dmPelsHeight=0,dmBitsPerPel=0; };
constexpr DWORD ENUM_CURRENT_SETTINGS=DWORD(-1), ENUM_REGISTRY_SETTINGS=DWORD(-2);
std::vector<DEVMODEW> modes;
bool EnumDisplaySettingsExW(LPCWSTR,DWORD index,DEVMODEW* mode,DWORD){
 if(index==ENUM_CURRENT_SETTINGS||index==ENUM_REGISTRY_SETTINGS){assert(mode->dmSize==sizeof(*mode));*mode={0,0,0,32};return true;}
 if(index>=modes.size())return false;
 assert(mode->dmSize==sizeof(*mode));*mode=modes[index];return true;
}
''' + listed + r'''
int main(){
 DEVMODEW selected{};
 assert(!listedMode(L"fixture",selected,1308,756));
 assert(!usableMode(L"fixture",selected,1308,756));
 modes={{0,0,0,32},{0,800,600,32},{0,1308,756,32},{0,1920,1080,32}};
 assert(listedMode(L"fixture",selected,1308,756));
 assert(selected.dmPelsWidth==1308&&selected.dmPelsHeight==756);
 assert(usableMode(L"fixture",selected,1308,756));
 assert(selected.dmPelsWidth==1308&&selected.dmPelsHeight==756);
 selected={};assert(listedMode(L"fixture",selected,1920,1080));
 assert(selected.dmPelsWidth==1920&&selected.dmPelsHeight==1080);
 modes={{0,0,0,32},{0,640,0,32},{0,640,480,0}};selected={};
 assert(!listedMode(L"fixture",selected,1308,756));
}
''')
binary = out / 'listed-mode'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                str(unit), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
assert 'usableMode(reinterpret_cast<LPCWSTR>(output.utf16()),baseline,DWORD(width),DWORD(height))' in helper
print('PASS detached 0x0 mode fallback for initial and recreated owned displays')

# Execute the actual setter with API doubles. Windows can acknowledge a 200%
# packet while clamping real windows to 175%; packet equality alone is unsafe.
dpi = (root / 'host/windows/display-dpi.h').read_text()
setter = dpi[dpi.index('inline DWORD set(Source source,int value) {'):dpi.rindex('\n}')]
unit = out / 'effective-dpi.cpp'
unit.write_text(r'''
#include <cassert>
#include "host/windows/display-dpi-policy.h"
using namespace deskport::display_dpi;
using DWORD=unsigned;using UINT=unsigned;using DISPLAYCONFIG_DEVICE_INFO_TYPE=int;
constexpr DWORD ERROR_NOT_SUPPORTED=50,ERROR_INVALID_STATE=5023;
struct Source {int adapter;unsigned id;};
struct Header {int type;unsigned size;int adapterId;unsigned id;};
struct SetPacket {Header header;int relative;};
Range state{0,0,5};unsigned effective=96,writes=0,sleeps=0,readError=0,writeError=0,windowError=0;
DWORD query(Source,Range& r){r=state;return readError?readError:valid(r)?0:50;}
DWORD effectiveDpi(Source,UINT& dpi){dpi=effective;return windowError;}
DWORD DisplayConfigSetDeviceInfo(Header* h){++writes;assert(h->type==-4&&h->adapterId==7&&h->id==3);
 if(!writeError)state.current=reinterpret_cast<SetPacket*>(h)->relative;
 return writeError;}
void Sleep(unsigned ms){assert(ms==50);++sleeps;}
''' + setter + r'''
int main(){
 const Source source{7,3};
 effective=168;assert(set(source,4)==ERROR_INVALID_STATE);assert(writes==1&&sleeps==30);
 effective=192;writes=sleeps=0;assert(set(source,4)==0);assert(!writes&&!sleeps);
 state={0,0,3};writes=0;assert(set(source,4)==ERROR_NOT_SUPPORTED);assert(!writes);
 state={0,0,5};writeError=5;assert(set(source,4)==5);assert(writes==1);
 writeError=0;readError=5;writes=0;assert(set(source,4)==5);assert(!writes);
 readError=0;state={0,4,5};windowError=5;sleeps=0;assert(set(source,4)==ERROR_INVALID_STATE);assert(sleeps==30);
 windowError=0;state={0,0,5};effective=120;assert(set(source,1)==0);
 effective=96;assert(set(source,0)==0);
}
''')
binary = out / 'effective-dpi'
subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(root), str(unit), '-o', str(binary)], check=True)
subprocess.run([str(binary)], check=True)
print('PASS actual DPI setter rejects CCD-success/window-DPI mismatch and verifies restore/error paths')

# These are packaging contract gates, not an emulation of SCM or native capture.
installer = (root / 'winbuild/scripts/deskport-full.nsi').read_text()
assert installer.count('register-display-service ') == 1
assert installer.count('remove-display-service ') == 1
assert installer.count('stop-installation ') == 2
assert installer.index('stop-installation ') < installer.index('File /r "${PAYLOAD}\\*.*"')
assert installer.index('remove-display-service ') < installer.index('RMDir /r "$INSTDIR\\host"')
assert 'host\\deskport-display-service.exe' in installer
for name in ['package-full.sh', 'audit-package.py', 'build-maintenance.sh', 'export-full-materials.py']:
    assert 'deskport-display-service.exe' in (root / 'winbuild/scripts' / name).read_text(), name
assert 'display_service::acquire(' in helper
assert 'ShellExecuteEx' not in helper and 'L"runas"' not in helper
assert 'FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION' in (root / 'host/windows/display-service-ipc.h').read_text()
assert 'parent,release,request,brokerStop,brokerProcess' in recovery
assert 'isSystem()' in recovery and 'trustedServer(broker,' in recovery
print('PASS installer ordering, complete payload/build integration, no UAC fallback and independent recovery wiring')
