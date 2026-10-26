#!/usr/bin/env python3
"""Dump a table binary's RT_STRING resources.

The rules code identifies its messages by string id, so the string table is the
closest thing these binaries have to documentation: a handler that displays 3010
is the one that traps a ball, whatever its component is called. Having the table
in the repo turns "what does this mode do" into a lookup, and it is independent of
both the decompiler and the manual.

Win32 stores strings in blocks of 16, each a UTF-16 length-prefixed run, with the
resource id being `block * 16 + index + 1`. Empty slots pad blocks out and are
skipped here.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_components import load  # noqa: E402

RT_STRING = 6


def resource_dir(d, off, base, level, want_type=None, path=()):
    """Walk the PE resource tree, yielding (type, name, lang, data_rva, size)."""
    nnamed, nid = struct.unpack("<HH", d[off + 12 : off + 16])
    for i in range(nnamed + nid):
        e = off + 16 + i * 8
        name, child = struct.unpack("<II", d[e : e + 8])
        is_dir = bool(child & 0x80000000)
        child &= 0x7FFFFFFF
        if level == 0 and want_type is not None and not (name & 0x80000000):
            if name != want_type:
                continue
        if is_dir:
            yield from resource_dir(d, base + child, base, level + 1, want_type, path + (name,))
        else:
            rva, size = struct.unpack("<II", d[base + child : base + child + 8])
            yield path + (name,), rva, size


def strings(path):
    d, imgbase, secs = load(path)
    rsrc = next((s for s in secs if s[0] == ".rsrc"), None)
    if not rsrc:
        raise SystemExit(f"{path}: no .rsrc")
    _n, rsrc_va, _sz, rsrc_ro, _rsz = rsrc

    def to_off(rva):
        return rsrc_ro + (rva - rsrc_va)

    out = {}
    for ids, rva, size in resource_dir(d, rsrc_ro, rsrc_ro, 0, RT_STRING):
        block = ids[1]  # ids is (type, block, language)
        o = to_off(rva)
        end = o + size
        for idx in range(16):
            if o + 2 > end:
                break
            (n,) = struct.unpack("<H", d[o : o + 2])
            o += 2
            if n:
                text = d[o : o + n * 2].decode("utf-16-le", "replace")
                out[(block - 1) * 16 + idx] = text
                o += n * 2
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE [OUT.txt]")
    s = strings(sys.argv[1])
    lines = [f"{i}\t{t}" for i, t in sorted(s.items())]
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text("\n".join(lines) + "\n")
        print(f"{sys.argv[1]}: {len(s)} strings -> {sys.argv[2]}")
    else:
        print(f"{sys.argv[1]}: {len(s)} strings")
        print("\n".join(lines))
