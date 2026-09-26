#!/usr/bin/env python3
"""Exercise the patched query's ownership, route changes and failure recovery."""
from pathlib import Path
import os
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-egl-query-') as tmp:
    work=Path(tmp)
    path=work/'src/platform/linux/pipewire.cpp'
    path.parent.mkdir(parents=True)
    with tarfile.open(root/'host/vendor/sunshine-nix.tar.gz') as archive:
        member=next(m for m in archive if m.name.endswith('/src/platform/linux/pipewire.cpp'))
        path.write_bytes(archive.extractfile(member).read())
    for _ in range(2):
        subprocess.run(['python3',str(root/'scripts/patch-host-egl-query-lifetime.py'),tmp],check=True)
    text=path.read_text()
    prefix=text[text.index('    int get_dmabuf_modifiers() {'):text.index('      auto egl_display = egl::make_display(query_display->get());')]
    preamble=r'''
#include <cassert>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <atomic>
#include <thread>
#include <vector>
namespace wl {
inline int created=0,destroyed=0;
inline bool fail=false, disconnected=false;
struct display_t {
 int id=++created;
 ~display_t(){++destroyed;}
 int init(){return fail?-1:0;}
 int get(){return id;}
};
}
int wl_display_roundtrip(int){return wl::disconnected?-1:0;}
std::atomic<int> in_query=0, queries=0;
'''
    suffix=r'''
      assert(in_query.fetch_add(1)==0);
      ++queries;
      std::this_thread::yield();
      assert(in_query.fetch_sub(1)==1);
      return query_display->get();
    }
int main(){
 setenv("XDG_RUNTIME_DIR","/synthetic-runtime",1);
 setenv("WAYLAND_DISPLAY","synthetic-display",1);
 for(int i=0;i<1000;i++)assert(get_dmabuf_modifiers()==1);
 assert(wl::created==1 && wl::destroyed==0 && queries==1000);
 wl::disconnected=true;assert(get_dmabuf_modifiers()==2);wl::disconnected=false;
 setenv("WAYLAND_DISPLAY","other-display",1);assert(get_dmabuf_modifiers()==3);
 setenv("XDG_RUNTIME_DIR","/other-runtime",1);assert(get_dmabuf_modifiers()==4);
 wl::disconnected=true;wl::fail=true;assert(get_dmabuf_modifiers()==-1);
 assert(wl::destroyed==5);wl::disconnected=false;wl::fail=false;
 assert(get_dmabuf_modifiers()==6);
 std::vector<std::thread> threads;
 for(int i=0;i<8;i++)threads.emplace_back([]{for(int j=0;j<1000;j++)assert(get_dmabuf_modifiers()==6);});
 for(auto& thread:threads)thread.join();
 assert(wl::created==6 && wl::destroyed==5 && queries==9004);
}
'''
    unit=work/'test.cpp';unit.write_text(preamble+prefix+suffix)
    binary=work/'test'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-pthread',str(unit),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS 9004 fresh serialized queries, stable identity, disconnect/route recovery and failed initialization')
