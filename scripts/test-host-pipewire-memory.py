#!/usr/bin/env python3
"""Compile the vendored image owner and dummy allocator; check real lifetimes."""
from pathlib import Path
import os, subprocess, sys, tarfile, tempfile
root = Path(__file__).resolve().parents[1]

def block(text, start):
    begin = text.index(start)
    brace = text.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == '{': depth += 1
        if text[end] == '}': depth -= 1
        end += 1
    return text[begin:end]

# The Nix revision is the deployed leaking host; the newer portable revision
# already has an upstream-owned image subtype and is checked by the overlay suite.
for archive in ('sunshine-nix.tar.gz',):
    with tempfile.TemporaryDirectory(prefix='deskport-pipewire-memory-') as tmp:
        target = Path(tmp)
        with tarfile.open(root/'host/vendor'/archive) as f: f.extractall(target, filter='data')
        if not (target/'src').exists(): target = next(p for p in target.iterdir() if (p/'src').exists())
        def exercise(patched):
            descriptor = block((target/'src/platform/linux/graphics.h').read_text(), '  class img_descriptor_t:')
            dummy = block((target/'src/platform/linux/pipewire.cpp').read_text(), '    int dummy_img(').replace(' override', '')
            unit = target/'memory-test.cpp'
            unit.write_text('''#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <optional>
#include <unistd.h>
#include <fcntl.h>
static long arrays = 0;
void* operator new[](std::size_t n) { auto p = std::malloc(n); if(!p) throw std::bad_alloc(); ++arrays; return p; }
void operator delete[](void* p) noexcept { if(p) { --arrays; std::free(p); } }
void operator delete[](void* p, std::size_t) noexcept { operator delete[](p); }
namespace platf { struct img_t { int width=320,height=180,row_pitch=1280; std::uint8_t* data=nullptr; virtual ~img_t()=default; }; }
namespace egl {
struct cursor_t : platf::img_t {};
struct surface_descriptor_t { int fds[4] = {-1,-1,-1,-1}; };
'''+descriptor+''';
}
struct Capture {
'''+dummy+'''
};
int main() {
  Capture capture;
  const int iterations = '''+('5000' if patched else '4')+''';
  for(int n=0;n<iterations;++n) {
    std::unique_ptr<platf::img_t> base=std::make_unique<egl::img_descriptor_t>();
    auto& img=static_cast<egl::img_descriptor_t&>(*base);
    assert(capture.dummy_img(&img)==0);
    assert(img.data[0]==0 && img.data[img.height*img.row_pitch-1]==0);
    assert(capture.dummy_img(&img)==0); // Replacing a dummy must free its predecessor.
    int fds[2]; assert(pipe(fds)==0); img.sd.fds[0]=fds[0]; close(fds[1]);
    img.reset(); assert(fcntl(fds[0],F_GETFD)==-1); // Preserve DMA-BUF cleanup.
    std::uint8_t borrowed[16]{};
    img.data=borrowed; img.reset(); // Borrowed capture pixels are never freed.
    assert(img.data==borrowed);
    capture.dummy_img(&img); // Destruction through img_t must free owned pixels.
  }
  assert(arrays==0 && "dummy images retained after destruction");
}
''')
            if patched and '--diagnostics' in sys.argv:
                contents = unit.read_text()
                contents = '#include "src/deskport/common/memorydiagnostics.h"\n' + contents
                contents = contents.replace('  assert(arrays==0 && "dummy images retained after destruction");', '''  assert(arrays==0 && "dummy images retained after destruction");
  using namespace deskport_memory;
  for (const auto& t : counters) assert(t.live == 0 && t.bytes == 0 && t.created == t.destroyed);
  assert(counters[static_cast<unsigned>(kind::image)].created == 5000);
  assert(counters[static_cast<unsigned>(kind::dummy_pixels)].created == 15000);
  snapshot("test-complete");''')
                unit.write_text(contents)
            binary=target/'memory-test'
            subprocess.run([os.environ.get('CXX','c++'),'-DDESKPORT_ENABLE_MEMORY_DIAGNOSTICS=1','-std=c++17','-O1','-g','-fsanitize=' + ('undefined' if sys.platform == 'darwin' else 'address,undefined'),str(unit),'-o',str(binary)],check=True)
            result=subprocess.run([str(binary)],env=dict(os.environ, DESKPORT_MEMORY_DIAGNOSTICS=str(target/'counters.jsonl')),stdout=subprocess.PIPE,stderr=subprocess.PIPE, timeout=60)
            if patched and result.returncode: raise SystemExit(result.stderr.decode())
            if not patched:
                assert result.returncode != 0 and b'dummy images retained' in result.stderr, result.stderr
        exercise(False)
        for _ in range(2): subprocess.run(['python3',str(root/'scripts/patch-host-pipewire-memory.py'),str(target)],check=True)
        if '--diagnostics' in sys.argv:
            for _ in range(2):
                subprocess.run(['python3',str(root/'scripts/patch-host-memory-diagnostics.py'),str(target),str(root/'host/common/memorydiagnostics.h')],check=True)
        exercise(True)
        print(f'PASS {archive}: original leaks; 5000 patched reuse/destruction/borrowed-buffer/FD cycles with allocation accounting and sanitizers')
