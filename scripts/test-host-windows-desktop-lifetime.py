#!/usr/bin/env python3
"""Check desktop ownership against Windows' cannot-close-attached contract."""
from pathlib import Path
import os
import json
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-desktop-lifetime-') as tmp:
    work = Path(tmp)
    manifest = json.loads((root/'host/vendor/sunshine.json').read_text())
    target = work/'host-source'
    source = target/'src/platform/windows/misc.cpp'
    source.parent.mkdir(parents=True)
    with tarfile.open(root/'host/vendor'/manifest['archive']) as archive:
        member = next(m for m in archive.getmembers() if m.name.endswith('src/platform/windows/misc.cpp'))
        source.write_bytes(archive.extractfile(member).read())
    for _ in range(2):
        subprocess.run(['python3', str(root/'scripts/patch-host-windows-desktop-lifetime.py'), str(target)], check=True)
    assert source.read_text().count('// DeskPort owned thread desktop lifetime') == 1
    (work / 'windows.h').write_text(r'''
#pragma once
#include <map>
#include <string>
#include <cwchar>
#include <cassert>
using HDESK = void*;
using DWORD = unsigned long;
constexpr int DF_ALLOWOTHERACCOUNTHOOK=1, FALSE=0, GENERIC_ALL=1, UOI_NAME=2;
inline HDESK current=(HDESK)1;
inline std::map<HDESK,std::wstring> handles{{current,L"initial"}};
inline std::wstring input=L"initial";
inline size_t serial=1;
inline bool fail_open=false, fail_set=false;
inline DWORD error=0;
inline int GetCurrentThreadId(){return 1;}
inline HDESK GetThreadDesktop(int){return current;}
inline HDESK OpenInputDesktop(int,int,int){
  if(fail_open){error=5;return nullptr;}
  auto h=(HDESK)++serial;handles[h]=input;return h;
}
inline bool GetUserObjectInformationW(HDESK h,int,wchar_t* out,size_t size,DWORD*){
  if(!handles.count(h)||(handles[h].size()+1)*sizeof(wchar_t)>size)return false;
  std::wcscpy(out,handles[h].c_str());return true;
}
inline bool SetThreadDesktop(HDESK h){
  assert(handles.count(h));if(fail_set){error=170;return false;}current=h;return true;
}
inline bool CloseDesktop(HDESK h){
  assert(h!=current && h!=(HDESK)1);assert(handles.erase(h)==1);return true;
}
inline DWORD GetLastError(){return error;}
inline void SetLastError(DWORD e){error=e;}
''')
    (work / 'test.cpp').write_text(r'''
#include "host/windows/thread-desktop.h"
int main(){
  for(int n=0;n<1000;n++){
    {
      deskport::thread_desktop desktop;
      input=L"initial";
      assert(desktop.sync()==(HDESK)1 && handles.size()==1);
      fail_open=true;assert(!desktop.sync());fail_open=false;
      input=L"other";fail_set=true;
      assert(!desktop.sync() && GetLastError()==170 && handles.size()==1);
      fail_set=false;
      auto other=desktop.sync();assert(other && handles.size()==2);
      assert(desktop.sync()==other && handles.size()==2);
      input=L"third";assert(desktop.sync()!=other && handles.size()==2);
    }
    assert(current==(HDESK)1 && handles.size()==1);
  }
}
''')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-I'+tmp,
                    '-I'+str(root), str(work/'test.cpp'), '-o', str(work/'test')], check=True)
    subprocess.run([str(work/'test')], check=True)
print('PASS 1000 desktop reuse, switch, failure and detach-before-close cycles')
