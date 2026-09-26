#!/usr/bin/env python3
"""Run patched pointer ownership and ENet QoS code with failing API doubles."""
from pathlib import Path
import json, os, subprocess, tarfile, tempfile
root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='deskport-session-lifetime-') as tmp:
    work=Path(tmp)
    manifest=json.loads((root/'host/vendor/sunshine.json').read_text())
    names=['src/platform/virtualhid_input.h','src/platform/virtualhid_input.cpp',
           'third-party/moonlight-common-c/enet/win32.c']
    with tarfile.open(root/'host/vendor'/manifest['archive']) as arc:
        for name in names:
            member=next(m for m in arc.getmembers() if (m.name == name or m.name.endswith('/'+name)))
            path=work/name; path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(arc.extractfile(member).read())
    for _ in range(2):
        subprocess.run(['python3',str(root/'scripts/patch-host-windows-pointer-lifetime.py'),tmp],check=True)
        subprocess.run(['python3',str(root/'scripts/patch-enet-windows-qos.py'),str(work/names[2])],check=True)
    header=(work/names[0]).read_text()
    structs=header[header.index('  struct input_context_t {'):header.index('  input_context_t &get_input_context')]
    cpp=(work/names[1]).read_text()
    methods=cpp[cpp.index('  client_context_t::client_context_t'):cpp.index('  std::unique_ptr<lvh::Runtime> create_runtime')]
    preamble=r'''
#include <cassert>
#include <atomic>
#include <memory>
#include <set>
#include <string_view>
#include <thread>
#include <vector>
#include "host/windows/reusable-device-pool.h"
using namespace std::string_view_literals;
constexpr int MAX_GAMEPADS=16;
namespace lvh {
enum class BackendKind { platform_default };
enum class PenButton { primary };
enum class PointerTransition { down, cancel };
struct Status { bool good=true; bool ok()const{return good;} };
struct Tool { PointerTransition transition=PointerTransition::down; };
inline std::atomic<int> created=0, destroyed=0, cancellations=0, releases=0;
inline bool fail_create=false;
struct Device {
 int id=++created; bool fail=false; std::set<int> contacts;
 ~Device(){++destroyed;}
 Status cancel_contact(int n){contacts.erase(n);++cancellations;return {!fail};}
 Status button(PenButton,bool down){assert(!down);++releases;return {!fail};}
 Tool last_submitted_tool(){return {};}
 Status place_tool(Tool t){assert(t.transition==PointerTransition::cancel);++cancellations;return {!fail};}
};
struct Touchscreen:Device{}; struct PenTablet:Device{};
struct Keyboard{};struct Mouse{};
struct Options{int profile; const char* stable_id;};
using CreateTouchscreenOptions=Options;using CreatePenTabletOptions=Options;
namespace profiles { int touchscreen(){return 1;}int pen_tablet(){return 2;} }
struct Caps{bool supports_touchscreen=true,supports_pen_tablet=true;};
struct TouchResult{std::unique_ptr<Touchscreen> touchscreen; Status status;explicit operator bool(){return bool(touchscreen);}};
struct PenResult{std::unique_ptr<PenTablet> pen_tablet; Status status;explicit operator bool(){return bool(pen_tablet);}};
struct Runtime{
 Caps capabilities(){return {};}
 TouchResult create_touchscreen(Options){return {fail_create?nullptr:std::make_unique<Touchscreen>(),{!fail_create}};}
 PenResult create_pen_tablet(Options){return {fail_create?nullptr:std::make_unique<PenTablet>(),{!fail_create}};}
};
}
void log_failure(std::string_view,lvh::Status){}
'''
    main=r'''
input_context_t::input_context_t():runtime(std::make_unique<lvh::Runtime>()){}
int main(){
 {
  input_context_t global;
  for(int i=0;i<1000;i++){client_context_t client(global);assert(client.touch&&client.pen);}
  assert(lvh::created==2 && lvh::destroyed==0);
  {
   client_context_t first(global), second(global);
   assert(first.touch->id!=second.touch->id && first.pen->id!=second.pen->id);
   first.active_touches={1,2}; first.touch->contacts={1,2};
   first.pressed_pen_buttons={lvh::PenButton::primary}; first.pen_used=true;
  }
  assert(lvh::cancellations==3 && lvh::releases==1);
  {client_context_t clean(global);assert(clean.touch->contacts.empty());}
  {client_context_t bad(global);bad.touch->fail=true;bad.active_touches={3};bad.pen->fail=true;bad.pen_used=true;}
  assert(lvh::destroyed==2);
  {client_context_t recreated(global);assert(!recreated.touch->fail&&!recreated.pen->fail);}
  std::vector<std::thread> threads;
  for(int i=0;i<8;i++)threads.emplace_back([&]{for(int j=0;j<1000;j++){
   client_context_t c(global);assert(c.touch&&c.pen);
   c.touch->contacts.insert(j);c.active_touches.insert(j);
  }});
  for(auto& t:threads)t.join();
  assert(lvh::created<=18); // two retired devices plus peak eight clients
 }
 assert(lvh::created==lvh::destroyed);
 {input_context_t failed;lvh::fail_create=true;{client_context_t c(failed);assert(!c.touch&&!c.pen);}
  lvh::fail_create=false;client_context_t c(failed);assert(c.touch&&c.pen);}
 assert(lvh::created==lvh::destroyed);
}
'''
    unit=work/'pointer.cpp';unit.write_text(preamble+'\n#define _WIN32\n'+structs+methods+main)
    cxx=os.environ.get('CXX','c++')
    subprocess.run([cxx,'-std=c++17','-pthread','-I'+str(root),str(unit),'-o',str(work/'pointer')],check=True)
    subprocess.run([str(work/'pointer')],check=True)
    # Execute the actual patched switch arm from both the host and client copies.
    for index,path in enumerate([work/names[2],root/'moonlight-common-c/moonlight-common-c/enet/win32.c']):
        text=path.read_text();start=text.index('        case ENET_SOCKOPT_QOS:');end=text.index('            break;',start)
        arm=text[start:end]+'            break;\n        }\n'
        qos=r'''
#include <cassert>
#include <set>
#include <cstddef>
using HANDLE=int;constexpr int INVALID_HANDLE_VALUE=-1,FALSE=0,ENET_SOCKOPT_QOS=1;
struct QOS_VERSION{int MajorVersion,MinorVersion;};
int qosHandle=-1,qosFlowId=0,serial=0;bool qosAddedFlow=false,enableEcn=false,fail=false;
std::set<int> live;
bool IsWindows10OrGreater(){return true;}
bool create(QOS_VERSION* v,int* out){assert(v->MajorVersion==1);if(fail)return false;*out=++serial;live.insert(*out);return true;}
bool close(int h){assert(live.erase(h)==1);return true;}
auto pfnQOSCreateHandle=&create;auto pfnQOSCloseHandle=&close;
void option(int value){int result=-1;switch(ENET_SOCKOPT_QOS){
'''+arm+r'''}assert(result==0);}
int main(){
 for(int n=0;n<1000;n++){option(1);assert(live.size()==1 && enableEcn);}
 fail=true;option(1);assert(live.empty() && qosHandle==-1);fail=false;
 option(1);option(0);option(0);assert(live.empty() && !enableEcn);
 pfnQOSCreateHandle=nullptr;option(1);assert(live.empty());pfnQOSCreateHandle=&create;
 option(1);pfnQOSCloseHandle(qosHandle);qosHandle=-1;assert(live.empty());
}
'''
        unit=work/f'qos{index}.cpp';unit.write_text(qos)
        subprocess.run([cxx,'-std=c++17','-DHAS_QWAVE',str(unit),'-o',str(work/'qos')],check=True)
        subprocess.run([str(work/'qos')],check=True)
print('PASS pointer reuse/exclusivity/cancellation/failure/8000 concurrent leases; host and client QoS replacement/failure/disable')
