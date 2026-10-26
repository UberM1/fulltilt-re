#!/usr/bin/env python3
"""Emit the skeleton of a control_<table>.cpp from the binary's component table.

Everything in this file is mechanical: the score arrays, the component tags and
the score_components[] array are all recovered from .data by dump_components.py,
so there is no reason to transcribe them by hand. What it cannot write is the
bodies -- those are the actual reverse-engineering work.

Each generated stub carries the handler's address and its nearest Space Cadet
equivalent (from match_handlers.py), so whoever fills it in knows both where to
look in the decompilation and which existing handler to model it on. Stubs are
deliberately left as TODO rather than guessed at.
"""

import re
import struct
import sys
from collections import OrderedDict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_components import ENTRY, dump, load, mapper  # noqa: E402
from dump_simple import simple_components  # noqa: E402

HEADER = """// control_{lower}.cpp -- rules for Full Tilt! Pinball's {title} table.
//
// GENERATED SKELETON ({tool}). The score arrays, tags and the component
// table below are recovered from {exe} and are believed exact.
// The handler bodies are NOT: each is a stub marked TODO, annotated with its
// address in the original binary and the Space Cadet handler it most resembles.
//
// {ncomp} components, {nfunc} distinct handlers.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TPinballTable.h"

// The rules declare some tags with their concrete type, and component_tag<T>
// instantiates std::is_base_of<TPinballComponent, T>, which needs T complete.
#include "TLight.h"
#include "TLightGroup.h"
#include "TSound.h"
#include "TTextBox.h"

namespace {lower} {{
"""


def table(path):
    """[(component, handler_va, scores)] in binary order."""
    import contextlib
    import io

    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        start, count, _ = dump(path)
    d, imgbase, secs = load(path)
    off, _sec, cstr = mapper(d, imgbase, secs)
    rows = []
    for k in range(count):
        o = start + k * ENTRY
        tag_p, func_p, cnt, sp = struct.unpack("<IIII", d[o : o + ENTRY])
        name = cstr(struct.unpack("<I", d[off(tag_p) : off(tag_p) + 4])[0])
        scores = []
        if sp and cnt:
            so = off(sp)
            scores = list(struct.unpack(f"<{cnt}i", d[so : so + 4 * cnt]))
        rows.append((name, func_p, tuple(scores)))
    return rows


def matches(path):
    """{handler address: nearest Space Cadet handler name}"""
    out = {}
    if not Path(path).exists():
        return out
    for line in Path(path).read_text().splitlines():
        m = re.match(r"\S+\s+(0x[0-9a-f]+)\s+(\w+)\s+([0-9.]+)", line)
        if m:
            out[int(m.group(1), 16)] = (m.group(2), float(m.group(3)))
    return out


def ident(name):
    return re.sub(r"\W", "_", name)


def tags_defined_in_src(prefix):
    """{group name: declared type} for tags the hand-written rules already define.

    Both halves of a table declare the same components: the generated table needs
    every tag, and the rule files need the ones they touch -- with the real type, so
    `component_tag<TTextBox>` rather than the generic base. Emitting a second
    definition here would be a redefinition, and emitting it with the generic type
    would be a redefinition with a conflicting type, which is worse. So scan and
    declare those `extern` instead, matching the type the rules chose.
    """
    out = {}
    src = Path(__file__).resolve().parent.parent / "src"
    for f in sorted(src.glob(f"{prefix}_*.cpp")):
        for m in re.finditer(
            r"^component_tag<(\w+)>\s+tag_(\w+)\s*=", f.read_text(), re.M
        ):
            out.setdefault(m.group(2), m.group(1))
    return out


def main(exe, matches_txt, out_path):
    rows = table(exe)
    near = matches(matches_txt)
    title = Path(exe).stem.title()
    lower = Path(exe).stem.lower()

    # Distinct score arrays, named after the first component that uses them.
    arrays = OrderedDict()
    for name, _va, scores in rows:
        if scores and scores not in arrays:
            arrays[scores] = f"control_{ident(name)}_scores"

    handlers = OrderedDict()
    for name, va, _s in rows:
        handlers.setdefault(va, ident(name))

    L = []
    L.append(
        HEADER.format(
            lower=lower,
            title=title,
            tool=Path(__file__).name,
            exe=Path(exe).name,
            ncomp=len(rows),
            nfunc=len(handlers),
        )
    )

    L.append("// Score tables, read out of .data.")
    for scores, nm in arrays.items():
        L.append(f"int {nm}[{len(scores)}] = {{{', '.join(str(x) for x in scores)}}};")

    # Tags live inside the table's namespace: two tables both have an info_text_box,
    # and at global scope the second definition collides with the first.
    existing = tags_defined_in_src(lower)
    simple = [nm for _addr, _ptr, nm in simple_components(exe)]
    all_tags = list(OrderedDict((n, None) for n, _v, _s in rows))
    all_tags += [n for n in simple if n not in set(all_tags)]

    L.append("// Component tags. Names are the group names stored in the DAT. Both the")
    L.append("// scoring table and simple_components below bind through these.")
    for name in all_tags:
        key = ident(name)
        if key in existing:
            L.append(f"extern component_tag<{existing[key]}> tag_{key};")
        else:
            L.append(f'component_tag<TPinballComponent> tag_{key} = {{"{name}"}};')

    L.append("\n// Handlers. Bodies are TODO; see the address and the reference handler.")
    for va, nm in handlers.items():
        ref, score = near.get(va, (None, 0.0))
        hint = (
            f"resembles Space Cadet's {ref} ({score:.2f})"
            if ref and score >= 0.55
            else f"no close Space Cadet equivalent (best {ref} {score:.2f}); table-specific"
            if ref
            else "unclassified"
        )
        L.append(f"\n// {va:#010x} -- {hint}")
        L.append(f"void {nm}Control(MessageCode code, TPinballComponent* caller)")
        L.append("{")
        L.append(f"\t// TODO: reverse engineer {va:#010x}")
        L.append("}")

    L.append(f"\n}} // namespace {lower}\n")

    L.append(f"\ncomponent_info {lower}_score_components[{len(rows)}]\n{{")
    for name, va, scores in rows:
        fn = f"{lower}::{handlers[va]}Control"
        arr = arrays.get(scores)
        tail = f"{len(scores)}, {lower}::{arr}" if scores else "0, nullptr"
        L.append(f"\tcomponent_info{{{lower}::tag_{ident(name)}, {{{fn}, {tail}}}}},")
    L.append("};")

    L.append(f"\ncomponent_tag_base* {lower}_simple_components[{len(simple)}]\n{{")
    for n in simple:
        L.append(f"\t&{lower}::tag_{ident(n)},")
    L.append("};")

    Path(out_path).write_text("\n".join(L) + "\n")
    print(f"{exe}: {len(rows)} components, {len(handlers)} handlers, {len(arrays)} score arrays")
    print(f"  -> {out_path}")
    unknown = sum(1 for va in handlers if near.get(va, ("", 0))[1] < 0.55)
    print(f"  {len(handlers) - unknown} stubs have a Space Cadet reference, {unknown} do not")
    print(f"  simple_components[{len(simple)}], {len(all_tags)} distinct tags")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE matches.txt OUT.cpp")
    main(*sys.argv[1:])
