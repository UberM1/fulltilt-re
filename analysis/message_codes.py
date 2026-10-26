#!/usr/bin/env python3
"""Recover Full Tilt's MessageCode numbering by aligning CADET.EXE against control.cpp.

Upstream's `enum class MessageCode` was reconstructed from the 3D Pinball build.
The Full Tilt executables are a different build of the same codebase, so their
numeric message codes are not guaranteed to match -- and `BumperControl` compares
against 0x4a where upstream's enum says ControlCollision = 63.

Each named handler is a tiny dispatch on `code`, so for every handler we can pair
the constants the binary compares against with the MessageCode names the upstream
function tests. Handlers that test exactly one code on each side give an
unambiguous pair; agreement across many handlers is what makes the mapping
trustworthy. Disagreement is reported rather than silently resolved.
"""

import json
import re
import sys
from collections import defaultdict
from pathlib import Path

HANDLER_RE = re.compile(r"^void control::(\w+)\(MessageCode code", re.M)
CODE_USE_RE = re.compile(r"MessageCode::(\w+)")
# `param_1 == 0x4a`, `code == 0x4a`, and switch labels `case 0x4a:`
CONST_RE = re.compile(r"(?:param_\d+|code)\s*==\s*(0x[0-9a-f]+|\d+)|case\s+(0x[0-9a-f]+|\d+):")


def upstream_functions(control_cpp):
    """{function name: [MessageCode names it mentions]}"""
    src = Path(control_cpp).read_text()
    out = {}
    starts = [(m.group(1), m.start()) for m in HANDLER_RE.finditer(src)]
    for i, (name, pos) in enumerate(starts):
        end = starts[i + 1][1] if i + 1 < len(starts) else len(src)
        out[name] = CODE_USE_RE.findall(src[pos:end])
    return out


def enum_values(header):
    """{MessageCode name: value} for the Control* range."""
    src = Path(header).read_text()
    body = re.search(r"enum class MessageCode.*?\{(.*?)\n\};", src, re.S).group(1)
    out, prev = {}, -1
    for line in body.splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line:
            continue
        if "=" in line:
            name, val = line.split("=", 1)
            try:
                prev = int(val.strip(), 0)
            except ValueError:
                continue
            name = name.strip()
        else:
            name, prev = line, prev + 1
        out[name] = prev
    return out


def binary_constants(decomp_dir, seeds_tsv):
    """{handler name: set(int constants compared against `code`)}"""
    addr_name = {}
    for line in Path(seeds_tsv).read_text().splitlines():
        addr, _kind, name = line.split("\t")
        addr_name[int(addr, 16)] = name
    out = {}
    for addr, name in addr_name.items():
        f = Path(decomp_dir) / "functions" / f"{addr:#x}.c"
        if not f.exists():
            continue
        consts = set()
        for a, b in CONST_RE.findall(f.read_text()):
            consts.add(int(a or b, 0))
        out[name] = consts
    return out


def main(decomp_dir, seeds_tsv, control_cpp, header):
    up = upstream_functions(control_cpp)
    enum = enum_values(header)
    binc = binary_constants(decomp_dir, seeds_tsv)

    votes = defaultdict(lambda: defaultdict(int))
    unambiguous = 0
    for name, consts in binc.items():
        names = [n for n in up.get(name, []) if n.startswith("Control")]
        if len(consts) == 1 and len(set(names)) == 1:
            votes[next(iter(consts))][names[0]] += 1
            unambiguous += 1

    print(f"handlers decompiled: {len(binc)}  unambiguous 1:1 pairs: {unambiguous}\n")
    print(f"{'binary':>8}  {'upstream name':<28} {'upstream':>8}  {'delta':>6}  votes")
    deltas = defaultdict(int)
    for val in sorted(votes):
        for nm, n in sorted(votes[val].items(), key=lambda kv: -kv[1]):
            u = enum.get(nm)
            d = val - u if u is not None else None
            if d is not None:
                deltas[d] += n
            print(f"{val:>#8x}  {nm:<28} {str(u):>8}  {str(d):>6}  {n}")

    print("\ndelta distribution (binary value - upstream enum value):")
    for d, n in sorted(deltas.items(), key=lambda kv: -kv[1]):
        print(f"  delta {d:+}: {n} observations")
    if len(deltas) == 1:
        d = next(iter(deltas))
        print(f"\n=> consistent constant offset of {d:+}: Full Tilt codes = upstream + {d}")
    else:
        print("\n=> NOT a constant offset; the numbering differs structurally")


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit(
            f"usage: {sys.argv[0]} DECOMP_DIR SEEDS.tsv control.cpp TPinballComponent.h"
        )
    main(*sys.argv[1:])
