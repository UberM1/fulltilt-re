#!/usr/bin/env python3
"""Dump the `simple_components[]` array -- the other half of the binding table.

`dump_components.py` recovers `score_components[]`, the components that carry a
handler and a score table. Upstream also has

    component_tag_base* simple_components[145];

a flat run of tag pointers for components that are only bound by name -- they get
a live object so the rules can poke them, but no ControlFunc of their own. A
component that appears in neither array is not bound at all, so questions of the
form "does v_lite68 share s_oneway5's handler?" are answered here, not in the
score table.

The array is a contiguous run of pointers into the tag region, which makes it easy
to find: tags are themselves `{const char* Name; TPinballComponent* Component;}`
pairs, so a pointer is a tag pointer when the dword it addresses points at a
plausible name string.
"""

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_components import dump, load, mapper  # noqa: E402

MIN_RUN = 20  # shorter runs of tag pointers occur by chance; the real array is long


def simple_components(path):
    d, imgbase, secs = load(path)
    off, sec_of, cstr = mapper(d, imgbase, secs)

    def tag_name(ptr):
        """Name if `ptr` addresses a component_tag_base, else None."""
        if sec_of(ptr) not in (".data", ".bss"):
            return None
        o = off(ptr)
        if o is None:
            return None
        name_p = struct.unpack("<I", d[o : o + 4])[0]
        return cstr(name_p, limit=32)

    best = []
    for name, va, _sz, ro, rsz in secs:
        if name != ".data":
            continue
        run, start = [], None
        for o in range(ro, ro + rsz - 4, 4):
            ptr = struct.unpack("<I", d[o : o + 4])[0]
            nm = tag_name(ptr)
            if nm:
                if start is None:
                    start = o
                run.append((imgbase + va + (o - ro), ptr, nm))
            else:
                if len(run) > len(best):
                    best = run
                run, start = [], None
        if len(run) > len(best):
            best = run
    return best if len(best) >= MIN_RUN else []


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE [name-to-look-up]")
    exe = sys.argv[1]
    rows = simple_components(exe)
    print(f"{exe}: simple_components[] has {len(rows)} entries")

    if len(sys.argv) > 2:
        want = sys.argv[2]
        hit = [r for r in rows if r[2] == want]
        print(f"  {want}: {'present' if hit else 'ABSENT'} in simple_components")
        # Cross-check the scoring table too, so the answer is complete.
        import contextlib
        import io

        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            dump(exe)
        print(f"  {want}: {'present' if want in buf.getvalue() else 'ABSENT'} in score_components")
    else:
        for slot, (addr, _ptr, nm) in enumerate(rows):
            print(f"  {slot:>3}  {addr:#010x}  {nm}")
