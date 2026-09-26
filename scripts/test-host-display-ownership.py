#!/usr/bin/env python3
"""Exercise extracted Wayland owners against a connection-lifetime checking mock."""
from pathlib import Path
import os
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]

def block(text, start):
    begin = text.index(start)
    brace = text.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end]

with tempfile.TemporaryDirectory(prefix='deskport-display-owners-') as tmp:
    target = Path(tmp)
    with tarfile.open(root / 'host/vendor/sunshine-nix.tar.gz') as archive:
        for name in ('wayland.h', 'wayland.cpp', 'pipewire.cpp'):
            member = next(m for m in archive.getmembers() if m.name.endswith('/src/platform/linux/' + name))
            out = target / 'src/platform/linux' / name
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(archive.extractfile(member).read())
    for patched in (False, True):
        if patched:
            for _ in range(2):
                subprocess.run([sys.executable, str(root / 'scripts/patch-host-display-ownership.py'), str(target)], check=True)
        h = (target / 'src/platform/linux/wayland.h').read_text()
        s = (target / 'src/platform/linux/wayland.cpp').read_text()
        owners = '\n'.join(block(s, start) for start in (
            '  monitor_t::~monitor_t()', '  void monitor_t::release_wayland_objects()',
            '  interface_t::~interface_t()')) if patched else 'monitor_t::~monitor_t() {}\ninterface_t::~interface_t() {}'
        unit = r'''
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace std::literals;
#define BOOST_LOG(level) std::ostringstream()
static int children = 0, connections = 0;
static bool connect_ok = true, has_xdg = true;
struct wl_display {};
struct Proxy { Proxy() { assert(connections); ++children; } };
using wl_registry = Proxy;
using wl_output = Proxy;
using zxdg_output_v1 = Proxy;
using zxdg_output_manager_v1 = Proxy;
using zwlr_screencopy_manager_v1 = Proxy;
using zwp_linux_dmabuf_v1 = Proxy;
wl_display* wl_display_connect(const char*) { if (!connect_ok) return nullptr; ++connections; return new wl_display; }
void wl_display_disconnect(wl_display* p) { assert(children == 0 && "proxies outlived connection"); --connections; delete p; }
void destroy(Proxy* p) { assert(connections && p); --children; delete p; }
void wl_registry_destroy(Proxy* p) { destroy(p); }
void wl_output_destroy(Proxy* p) { destroy(p); }
void zxdg_output_v1_destroy(Proxy* p) { destroy(p); }
void zwlr_screencopy_manager_v1_destroy(Proxy* p) { destroy(p); }
void zwp_linux_dmabuf_v1_destroy(Proxy* p) { destroy(p); }
void zxdg_output_manager_v1_destroy(Proxy* p) { destroy(p); }
Proxy* wl_display_get_registry(wl_display*) { return new Proxy; }
Proxy* zxdg_output_manager_v1_get_xdg_output(Proxy*, Proxy*) { return new Proxy; }
void zxdg_output_v1_add_listener(Proxy*, int*, void*) {}
void wl_output_add_listener(Proxy*, int*, void*) {}
namespace util {
template<class T, void(*F)(T*)> struct deleter { void operator()(T* p) const { F(p); } };
template<class T, void(*F)(T*)> using safe_ptr = std::unique_ptr<T, deleter<T,F>>;
}
namespace wl {
using display_internal_t = util::safe_ptr<wl_display, wl_display_disconnect>;
struct monitor_t {
    wl_output* output;
    zxdg_output_v1* deskport_xdg_output = nullptr;
    int xdg_listener = 0, wl_listener = 0;
    std::string name = "synthetic-monitor";
    explicit monitor_t(wl_output* p): output(p) {}
    ~monitor_t();
    void release_wayland_objects();
    void listen(zxdg_output_manager_v1*);
};
struct interface_t {
    enum { XDG_OUTPUT };
    std::vector<std::unique_ptr<monitor_t>> monitors;
    Proxy *screencopy_manager = new Proxy, *dmabuf_interface = new Proxy, *output_manager = new Proxy;
    ~interface_t();
    void listen(wl_registry*) { monitors.emplace_back(std::make_unique<monitor_t>(new Proxy)); }
    bool operator[](int) const { return has_xdg; }
};
'''
        unit += block(h, '  class display_t {') + ';\n'
        unit += owners + '\n'
        unit += '\n'.join(block(s, start) for start in (
            '  int display_t::init(', '  wl_registry *display_t::registry()',
            '  void monitor_t::listen(', '  std::vector<std::unique_ptr<monitor_t>> monitors('))
        unit += r'''
void display_t::roundtrip() {}
}
int main() {
    for (int i = 0; i < 1000; ++i) {
        auto snapshots = wl::monitors("synthetic");
        assert(snapshots.size() == 1 && snapshots[0]->name == "synthetic-monitor");
        assert(children == 0 && connections == 0);
        snapshots.clear(); // Metadata safely outlives the Wayland connection.
        has_xdg = false;
        assert(wl::monitors("synthetic").empty());
        assert(children == 0 && connections == 0);
        has_xdg = true;
        connect_ok = false;
        assert(wl::monitors("synthetic").empty());
        connect_ok = true;
        { wl::display_t d; assert(d.init("synthetic") == 0); assert(d.registry() == d.registry()); }
        assert(children == 0 && connections == 0);
    }
}
'''
        source = target / 'test.cpp'
        source.write_text(unit)
        binary = target / 'test'
        sanitizers = 'undefined' if sys.platform == 'darwin' else 'address,undefined'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                        '-fsanitize=' + sanitizers, str(source), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, timeout=60)
        if patched:
            assert result.returncode == 0, result.stderr.decode()
        else:
            assert result.returncode != 0 and b'proxies outlived connection' in result.stderr, result.stderr
print('Original proxy leak rejected; 1000 metadata, missing-interface, failed-connect and registry lifetime cycles passed.')
