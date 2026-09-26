#!/usr/bin/env python3
"""Summarize exact-size lifetime records; an unfinished log is inconclusive."""
import json
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
freed = {int(x) for x in re.findall(r"^FREE (\d+)", source, re.M)}
blocks = {int(m[1]): m[2] for m in re.finditer(
    r"^ALLOC (\d+) [^\n]+\n(.*?)^END \1$", source, re.M | re.S)}
native = {key: stack for key, stack in blocks.items()
          if "libpipewire-module-protocol-native.so" in stack}
summary = re.findall(r"^SUMMARY allocations=(\d+) live=(\d+)$", source, re.M)
complete = (bool(summary) and int(summary[-1][0]) == len(blocks)
            and int(summary[-1][1]) == len(set(blocks) - freed))
print(json.dumps({
    "complete": complete,
    "tracked_allocations": len(blocks),
    "native_buffer_allocations": len(native),
    "native_buffer_released": len(set(native) & freed),
    "native_buffer_outstanding": sorted(set(native) - freed),
    "total_outstanding": int(summary[-1][1]) if summary else None,
}, indent=2))
for key in sorted(set(native) - freed):
    print(f"Outstanding allocation {key}:\n{native[key]}")
if not complete:
    raise SystemExit("Incomplete log: unfinished process or missing allocation records")
