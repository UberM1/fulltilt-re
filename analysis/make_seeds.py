#!/usr/bin/env python3
"""Generate a REA_GHIDRA_SEED_FILE for a Full Tilt! table binary.

Ghidra's auto-analysis does not find the per-component control handlers: nothing
calls them directly, they are only reachable as data pointers inside the static
score_components[] array. Seeding fixes that -- and since dump_components.py
already recovers every handler address, the seed file can be generated instead of
hand-written.

For CADET the handler *names* are known too: control.cpp lists score_components[]
in the same order as the binary, so entry N's ControlFunc name applies to the
address at entry N. Those real names (BumperControl, SpaceWarpRolloverControl, ...)
get seeded as labels, which makes the decompiled CADET output directly comparable
to upstream source.

For the other tables the handler is named after the first component it serves,
e.g. a_bump1 -> Dragon_a_bump1_Control.

Format (per REA): address<TAB>kind[<TAB>name], kind in function|code|label.
"""

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_components import ENTRY, load, mapper  # noqa: E402
import struct  # noqa: E402

HERE = Path(__file__).parent


def control_cpp_order(control_cpp):
    """[(tag, ControlFunc)] in score_components[] declaration order."""
    src = Path(control_cpp).read_text()
    block = re.search(
        r"component_info control::score_components\[88\]\s*\{(.*?)\n\};", src, re.S
    ).group(1)
    return re.findall(r"component_info\{\s*(\w+)\s*,\s*\{\s*(\w+)\s*,", block)


def entries(path):
    """[(name, handler_va)] straight out of the binary's component table."""
    from dump_components import dump
    import contextlib
    import io

    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        start, count, _funcs = dump(path)

    d, imgbase, secs = load(path)
    off, _sec_of, cstr = mapper(d, imgbase, secs)
    out = []
    for k in range(count):
        tag_p, func_p, _cnt, _sp = struct.unpack(
            "<IIII", d[start + k * ENTRY : start + k * ENTRY + ENTRY]
        )
        name = cstr(struct.unpack("<I", d[off(tag_p) : off(tag_p) + 4])[0])
        out.append((name, func_p))
    return out


def seeds(path, prefix=None, control_cpp=None):
    rows = entries(path)
    names = {}
    if control_cpp:
        order = control_cpp_order(control_cpp)
        if len(order) != len(rows):
            print(
                f"warning: control.cpp has {len(order)} entries, binary has {len(rows)}"
                " -- names not applied",
                file=sys.stderr,
            )
        else:
            for (_tag, func), (_n, va) in zip(order, rows):
                names.setdefault(va, func)

    # One seed per distinct handler; first component served wins the name.
    seen = {}
    for comp, va in rows:
        if va in seen:
            continue
        seen[va] = names.get(va) or f"{prefix}_{comp}_Control"

    lines = [f"0x{va:08x}\tfunction\t{nm}" for va, nm in sorted(seen.items())]
    return lines, len(rows), len(seen)


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE OUT.tsv [control.cpp]")
    target, out = sys.argv[1], sys.argv[2]
    cpp = sys.argv[3] if len(sys.argv) > 3 else None
    prefix = Path(target).stem.title()
    lines, ncomp, nfunc = seeds(target, prefix=prefix, control_cpp=cpp)
    Path(out).write_text("\n".join(lines) + "\n")
    print(f"{target}: {ncomp} components -> {nfunc} distinct handlers seeded -> {out}")
    for line in lines[:5]:
        print("  " + line.replace("\t", "  "))
