#!/usr/bin/env python3
"""Close ENet's previous global QWave handle before replacing it."""
from pathlib import Path
import sys

def patch(path):
    source = path.read_text()
    marker = '// DeskPort close the previous global QWave handle before replacement.'
    if marker in source:
        return
    start = source.index('        case ENET_SOCKOPT_QOS:')
    end = source.index('            result = 0;', start)
    block = source[start:end]
    old = '            if (value)\n'
    if block.count(old) != 1 or block.count('            else if (qosHandle != INVALID_HANDLE_VALUE)') != 1:
        raise SystemExit('ENet Windows QoS anchor changed')
    block = block.replace(old, '            '+marker+'\n            if (qosHandle != INVALID_HANDLE_VALUE)\n            {\n                pfnQOSCloseHandle(qosHandle);\n                qosHandle = INVALID_HANDLE_VALUE;\n            }\n'+old)
    block = block.replace("""            else if (qosHandle != INVALID_HANDLE_VALUE)
            {
                pfnQOSCloseHandle(qosHandle);
                qosHandle = INVALID_HANDLE_VALUE;
            }
""", "")
    source = source[:start] + block + source[end:]
    path.write_text(source)

if __name__ == '__main__':
    patch(Path(sys.argv[1]))
