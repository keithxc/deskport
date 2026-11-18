#!/usr/bin/env python3
"""An unused portal must not close a descriptor owned by the capture loop."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import os
import re
import subprocess
import sys
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]

def block(source, start):
    begin = source.index(start)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]

for vendor in ('sunshine-nix.tar.gz', 'sunshine.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-portal-fd-') as tmp:
        target = Path(tmp)
        with tarfile.open(root / 'host/vendor' / vendor) as archive:
            for relative in ('src/main.cpp', *(f'src/platform/linux/{name}.cpp' for name in
                              ('portalgrab', 'kwingrab', 'wlgrab', 'x11grab'))):
                member = next(m for m in archive.getmembers() if m.name.endswith(relative))
                out = target / relative
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(archive.extractfile(member).read())
        for patched in (False, True):
            if patched:
                for _ in range(2):
                    subprocess.run([sys.executable, str(root / 'host/overlays/patch-host-linux-display.py'),
                                    str(target)], check=True)
            source = (target / 'src/platform/linux/portalgrab.cpp').read_text()
            declaration = re.search(r'^    int pipewire_fd[^;]*;', source, re.M).group()
            unit = r'''
#include <cassert>
#include <fcntl.h>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
using namespace std::literals;
#define BOOST_LOG(level) std::ostringstream()
#define g_autoptr(type) type*
struct GError { const char* message; };
constexpr int G_DBUS_CALL_FLAGS_NONE = 0;
template<class... T> void g_dbus_connection_call_sync(T...) { assert(false); }
template<class T> void g_clear_object(T*) { assert(false); }
class dbus_t {
public:
  dbus_t &operator=(dbus_t &&) = delete;
  void *conn = nullptr, *screencast_proxy = nullptr, *remote_desktop_proxy = nullptr;
  std::string session_handle;
'''
            unit += declaration + '\n' + block(source, '    ~dbus_t() noexcept') + '\n};\n'
            unit += r'''
int main() {
    // Run in a child process: fd 0 models a reused PipeWire event/socket fd.
    int owned = open("/dev/null", O_RDONLY);
    assert(owned >= 0 && dup2(owned, 0) == 0);
    if (owned != 0) close(owned);
    for (int i = 0; i < 1000; ++i) {
        { auto unused_portal = std::make_shared<dbus_t>(); }
        if (fcntl(0, F_GETFD) < 0) return 17;
    }
    // An opened portal still releases its own descriptor exactly once.
    int portal_fd = dup(0);
    assert(portal_fd > 0);
    { auto portal = std::make_shared<dbus_t>(); portal->pipewire_fd = portal_fd; }
    assert(fcntl(portal_fd, F_GETFD) < 0 && fcntl(0, F_GETFD) >= 0);
}
'''
            cpp, binary = target / 'test.cpp', target / 'test'
            cpp.write_text(unit)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1',
                            str(cpp), '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], timeout=10)
            assert result.returncode == (0 if patched else 17), (vendor, patched, result.returncode)
        print(f'PASS: {vendor}: unused portal preserves fd 0; owned fd closes; old source fails')
