#!/usr/bin/env python3
"""Map original handler addresses to the C++ functions that now implement them.

The generated skeleton names every handler after its component (`a_bump1Control`),
because that is all the binary offers. The hand-written reimplementations name
them after what they do (`ForestBumperControl`), which is what the port should
keep. So the component table has to be rewritten to point at the real functions,
and that mapping has to be derived rather than maintained by hand.

The convention that makes it derivable: every reimplementation carries its
original address in a comment immediately above the definition. This walks src/,
pairs each such comment with the definition under it, and reports what is wired,
what is still a stub, and anything that looks wired but is not reachable -- a
handler defined outside `namespace dragon` cannot be named `dragon::x` by the
table, which is exactly the kind of mismatch that only surfaces at link time.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# `// 0x00424bc0 ...` or `... (0x424bc0)` on a comment line above a definition.
ADDR_COMMENT = re.compile(r"//.*?0x0?0?(4[0-9a-f]{5})")
DEFN = re.compile(r"^(?:static\s+)?(?:void|int|float|bool)\s+([A-Za-z_][\w:]*)\s*\(")


def implementations(prefix):
    """{address: (function, file, namespace or None)}"""
    out = {}
    for path in sorted((ROOT / "src").glob(f"{prefix}_*.cpp")):
        lines = path.read_text().splitlines()
        ns, depth, ns_depth = None, 0, None
        pending = []
        for i, line in enumerate(lines):
            m = re.match(r"\s*namespace\s+(\w+)\s*\{?", line)
            if m and "dragon" in m.group(1):
                ns, ns_depth = m.group(1), depth
            depth += line.count("{") - line.count("}")
            if ns is not None and depth <= ns_depth:
                ns = None

            hit = ADDR_COMMENT.search(line)
            if hit:
                pending.append(int(hit.group(1), 16))
                continue
            d = DEFN.match(line)
            if d:
                tail = " ".join(lines[i : i + 2])
                if not tail.rstrip().endswith(";") and "{" in tail:
                    for a in pending:
                        out.setdefault(a, (d.group(1), path.name, ns))
                pending = []
            elif line.strip() and not line.strip().startswith("//"):
                pending = []
    return out


def main(table, prefix):
    seeds = []
    for line in (ROOT / "analysis" / f"{table}-seeds.tsv").read_text().splitlines():
        addr, _kind, name = line.split("\t")
        seeds.append((int(addr, 16), name))
    seeds.sort()

    impls = implementations(prefix)
    wired, stubs, unqualified = [], [], []
    for addr, comp in seeds:
        if addr not in impls:
            stubs.append((addr, comp))
            continue
        fn, fname, ns = impls[addr]
        wired.append((addr, comp, fn, fname, ns))
        if ns is None:
            unqualified.append((addr, comp, fn, fname))

    print(f"{table}: {len(wired)}/{len(seeds)} handlers implemented\n")
    for addr, comp, fn, fname, ns in wired:
        q = f"{ns}::{fn}" if ns else fn
        print(f"  {addr:#010x}  {comp:<30} -> {q:<40} {fname}")

    if stubs:
        print(f"\n  still stubs ({len(stubs)}): " + ", ".join(c for _a, c in stubs))
    if unqualified:
        print(
            f"\n  WARNING: {len(unqualified)} definitions sit outside namespace {prefix};"
            f" the component table qualifies handlers as {prefix}::, so these will not link:"
        )
        for addr, comp, fn, fname in unqualified[:6]:
            print(f"    {addr:#010x}  {fn}  ({fname})")
        if len(unqualified) > 6:
            print(f"    ... and {len(unqualified) - 6} more in the same file(s)")
    return impls


if __name__ == "__main__":
    t = sys.argv[1] if len(sys.argv) > 1 else "dragon"
    main(t, t)
