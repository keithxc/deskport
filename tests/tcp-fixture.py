"""Isolated TLS fixture: gate the handshake on the kernel-negotiated MSS."""

# Guard direct test/build entrypoints before local imports or output creation.
import sys as _storage_sys
_storage_sys.dont_write_bytecode = True
_storage_sys.path.insert(0, str(__import__('pathlib').Path(__file__).resolve().parents[1] / 'scripts'))
import build_storage as _build_storage
if __name__ == '__main__':
    _build_storage.bootstrap()

import json
import socket
import ssl
import sys
import threading
import time

cert, key, mode, log = sys.argv[1:]
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(cert, key)
ipv6 = mode.endswith("6")
if ipv6: mode = mode[:-1]
server = socket.socket(socket.AF_INET6 if ipv6 else socket.AF_INET)
server.bind(("::1" if ipv6 else "127.0.0.1", 0))
server.listen()
print(server.getsockname()[1], flush=True)
lock = threading.Lock()
def record(**entry):
    with lock, open(log, 'a') as output:
        output.write(json.dumps(entry) + '\n')
def client(sock):
    try:
        mss = sock.getsockopt(socket.IPPROTO_TCP, socket.TCP_MAXSEG)
        record(mss=mss)
        if mode == 'blackhole' or (mode == 'small' and mss > 900):
            time.sleep(8)
            return
        sock.settimeout(6)
        with context.wrap_socket(sock, server_side=True) as tls:
            request = b''
            while b'\r\n\r\n' not in request:
                data = tls.recv(4096)
                if not data:
                    return
                request += data
            record(request=request.split(b'\r\n')[0].decode())
            if mode == 'drop':
                time.sleep(8)
                return
            body = b'x' * 262144
            tls.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 262144\r\nConnection: close\r\n\r\n' + body)
    except (OSError, ssl.SSLError):
        pass
    finally:
        sock.close()
while True:
    sock, _ = server.accept()
    threading.Thread(target=client, args=(sock,), daemon=True).start()
