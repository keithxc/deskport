#!/usr/bin/env python3
"""Compile the patched upstream function with ENet failure/success injection."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

from pathlib import Path
import subprocess
import tarfile
import tempfile

root = Path(__file__).resolve().parents[1]
for archive in ('sunshine.tar.gz', 'sunshine-nix.tar.gz'):
    with tempfile.TemporaryDirectory(prefix='deskport-network-') as tmp:
        target = Path(tmp)
        (target / 'src').mkdir()
        with tarfile.open(root / 'host/vendor' / archive) as tar:
            member = next(m for m in tar.getmembers() if m.name.endswith('src/network.cpp'))
            (target / 'src/network.cpp').write_bytes(tar.extractfile(member).read())
        patch = ['python3', str(root / 'host/overlays/patch-host-network.py'), str(target)]
        subprocess.run(patch, check=True)
        source = (target / 'src/network.cpp').read_text()
        subprocess.run(patch, check=True)
        assert source == (target / 'src/network.cpp').read_text(), 'Patch must be idempotent'
        start = source.index('  host_t host_create(')
        end = source.index('\n  }', start) + len('\n  }')
        harness = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <sys/socket.h>
struct ENetAddress {};
struct ENetHost { int socket = 42; };
bool fail = true;
int qos_calls = 0;
constexpr int ENET_SOCKOPT_QOS = 11;
void enet_initialize() {}
void enet_address_set_host(ENetAddress*, const char*) {}
void enet_address_set_port(ENetAddress*, std::uint16_t) {}
ENetHost* enet_host_create(int, ENetAddress*, int, int, int, int) {
    return fail ? nullptr : new ENetHost;
}
void enet_socket_set_option(int socket, int option, int enabled) {
    assert(socket == 42 && option == ENET_SOCKOPT_QOS && enabled == 1);
    ++qos_calls;
}
namespace net {
enum af_e { IPV4, IPV6 };
using host_t = std::unique_ptr<ENetHost>;
std::string get_bind_address(af_e) { return "0.0.0.0"; }
'''
        harness += source[start:end] + r'''
}
int main() {
    ENetAddress address;
    for (auto family : {net::IPV4, net::IPV6}) {
        fail = true;
        const int before = qos_calls;
        assert(!net::host_create(family, address, 0));
        assert(qos_calls == before);
        fail = false;
        assert(net::host_create(family, address, 0));
        assert(qos_calls == before + 1);
    }
}
'''
        (target / 'test.cpp').write_text(harness)
        subprocess.run(['c++', '-std=c++17', '-fsanitize=undefined',
                        str(target / 'test.cpp'), '-o', str(target / 'test')], check=True)
        subprocess.run([str(target / 'test')], check=True, timeout=30)
        print(f'PASS: ENet failure/success, IPv4/IPv6, idempotence: {archive}')
