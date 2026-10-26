#!/usr/bin/env python3
"""Map every `DAT_<addr>` in the decompilation back to its component name.

The handlers reach their components through the static `component_tag` objects,
which the 1996 build lays out as

    { const char* Name; TPinballComponent* Component; }

with the name string sitting immediately before the pair. Ghidra sees only the
`Component` slot being read, so a handler full of `DAT_00439c94` is really full of
`a_bump3`. Since the name pointer is the dword four bytes below the slot, the whole
table can be recovered and the decompiled text annotated.

Two modes:
    dump_tags.py TABLE.EXE                      list the tags
    dump_tags.py TABLE.EXE --annotate DIR       rewrite DIR/functions/*.c in place,
                                                appending the name to each DAT_
"""

import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_components import load, mapper  # noqa: E402

DAT_RE = re.compile(r"\bDAT_([0-9a-f]{8})\b")


def tags(path):
    """{component-pointer address: group name}"""
    d, imgbase, secs = load(path)
    off, sec_of, cstr = mapper(d, imgbase, secs)
    out = {}
    for name, va, _sz, ro, rsz in secs:
        if name not in (".data", ".bss"):
            continue
        for o in range(ro, ro + rsz - 8, 4):
            name_p, comp_p = struct.unpack("<II", d[o : o + 8])
            # Component is null at static init; Name must point at a real string.
            if comp_p != 0:
                continue
            if sec_of(name_p) not in (".data", ".rdata"):
                continue
            s = cstr(name_p, limit=32)
            if not s or len(s) < 3 or not re.fullmatch(r"[A-Za-z0-9_]+", s):
                continue
            comp_slot = imgbase + va + (o - ro) + 4
            out[comp_slot] = s
    return out


def annotate(decomp_dir, table):
    files = sorted((Path(decomp_dir) / "functions").glob("*.c"))
    hits = 0
    for f in files:
        text = f.read_text()

        def sub(m):
            nonlocal hits
            addr = int(m.group(1), 16)
            nm = table.get(addr)
            if not nm:
                return m.group(0)
            hits += 1
            return f"{m.group(0)}/*{nm}*/"

        new = DAT_RE.sub(sub, text)
        if new != text:
            f.write_text(new)
    return len(files), hits


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE [--annotate DECOMP_DIR]")
    t = tags(sys.argv[1])
    if "--annotate" in sys.argv:
        d = sys.argv[sys.argv.index("--annotate") + 1]
        nf, nh = annotate(d, t)
        print(f"{len(t)} tags; annotated {nh} references across {nf} files in {d}")
    else:
        print(f"{len(t)} component tags in {sys.argv[1]}")
        for addr, nm in sorted(t.items()):
            print(f"  {addr:#010x}  {nm}")
