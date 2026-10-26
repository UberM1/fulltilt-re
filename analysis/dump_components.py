#!/usr/bin/env python3
"""Dump the static score_components[] table out of a Full Tilt! table binary.

In control.cpp (the upstream decompilation of CADET.EXE) the table is:

    struct component_control { void (&ControlFunc)(...); unsigned ScoreCount; const int* Scores; };
    struct component_info    { component_tag_base& Tag; component_control Control; };
    component_info score_components[88];

which the 1996 MSVC build lays out as a flat 16-byte record:

    +0  tag*   -> component_tag_base { const char* Name; TPinballComponent* Component; }
           so the group name needs one extra dereference through tag+0
    +4  void*  control function (.text)
    +8  u32    score count
    +12 int*   score array      (.data) or NULL

The table is found by anchoring on entries whose name pointer lands in a string
section and whose function pointer lands in .text -- no hardcoded addresses, so
the same script works on CADET, DRAGON and PIRATES.
"""

import struct
import sys
from pathlib import Path

ENTRY = 16


def load(path):
    d = Path(path).read_bytes()
    pe = struct.unpack("<I", d[0x3C:0x40])[0]
    nsec = struct.unpack("<H", d[pe + 6 : pe + 8])[0]
    optsz = struct.unpack("<H", d[pe + 20 : pe + 22])[0]
    imgbase = struct.unpack("<I", d[pe + 24 + 28 : pe + 24 + 32])[0]
    tbl = pe + 24 + optsz
    secs = []
    for i in range(nsec):
        o = tbl + i * 40
        name = d[o : o + 8].rstrip(b"\0").decode()
        vsz, va, rsz, ro = struct.unpack("<IIII", d[o + 8 : o + 24])
        secs.append((name, va, max(vsz, rsz), ro, rsz))
    return d, imgbase, secs


def mapper(d, imgbase, secs):
    def off(va):
        rva = va - imgbase
        for _n, sva, sz, ro, rsz in secs:
            if sva <= rva < sva + sz:
                o = ro + (rva - sva)
                return o if rva - sva < rsz else None
        return None

    def sec_of(va):
        rva = va - imgbase
        for n, sva, sz, _ro, _rsz in secs:
            if sva <= rva < sva + sz:
                return n
        return None

    def cstr(va, limit=64):
        o = off(va)
        if o is None:
            return None
        end = d.find(b"\0", o, o + limit)
        if end < 0:
            return None
        s = d[o:end]
        if not s or not all(32 <= c < 127 for c in s):
            return None
        return s.decode()

    return off, sec_of, cstr


def dump(path):
    d, imgbase, secs = load(path)
    off, sec_of, cstr = mapper(d, imgbase, secs)
    data = next(s for s in secs if s[0] == ".data")
    _n, data_va, _sz, data_ro, data_rsz = data

    def plausible(o):
        """A 16-byte record that looks like component_info."""
        name_p, func_p, cnt, scores_p = struct.unpack("<IIII", d[o : o + ENTRY])
        if sec_of(func_p) != ".text":
            return None
        if sec_of(name_p) not in (".data", ".rdata"):
            return None
        tag = off(name_p)
        if tag is None:
            return None
        name = cstr(struct.unpack("<I", d[tag : tag + 4])[0])
        if not name:
            return None
        if cnt > 16:
            return None
        if scores_p and sec_of(scores_p) not in (".data", ".rdata"):
            return None
        scores = []
        if scores_p and cnt:
            so = off(scores_p)
            if so is None:
                return None
            scores = list(struct.unpack(f"<{cnt}i", d[so : so + 4 * cnt]))
        return name, func_p, cnt, scores

    # Longest run of consecutive plausible records in .data wins.
    best = (0, None)
    i = data_ro
    while i < data_ro + data_rsz - ENTRY:
        if plausible(i):
            j, n = i, 0
            while j < data_ro + data_rsz - ENTRY and plausible(j):
                j += ENTRY
                n += 1
            if n > best[0]:
                best = (n, i)
            i = j
        else:
            i += ENTRY

    count, start = best
    if not start:
        raise SystemExit(f"{path}: no component table found")

    print(f"{path}")
    print(f"  table at file {start:#x} (rva {data_va + start - data_ro:#x}), {count} entries\n")
    print(f"  {'#':>3}  {'group name':<26} {'handler':<10} {'n':>2}  scores")
    funcs = {}
    for k in range(count):
        name, func_p, cnt, scores = plausible(start + k * ENTRY)
        funcs.setdefault(func_p, []).append(name)
        sc = ", ".join(str(x) for x in scores) if scores else ""
        print(f"  {k:>3}  {name:<26} {func_p:#010x} {cnt:>2}  {sc}")
    print(f"\n  {len(funcs)} distinct handler functions over {count} components")
    return start, count, funcs


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE")
    dump(sys.argv[1])
