#!/usr/bin/env python3
"""Report how much of each table's rules have actually been ported.

"Percent done" is ambiguous here, so this reports the three measures that mean
different things and tends to get conflated:

  decompiled   -- how much of the binary has readable pseudocode. Near-total, and
                  not the bottleneck.
  classified   -- how many handlers have a named Space Cadet equivalent to work
                  from. Triage, not understanding.
  implemented  -- how many handlers have a real C++ body in src/. This is the one
                  that answers "how much of the port is done", and it is always
                  the smallest.

A handler counts as implemented when its original address appears in a src/ file,
which is reliable because every reimplementation carries its address in a comment
by convention. Bytes are counted too: handlers vary by an order of magnitude in
size, so 50% of handlers is not 50% of the work.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ADDR_RE = re.compile(r"0x00(4[0-9a-f]{5})")


def seeds(table):
    f = ROOT / "analysis" / f"{table}-seeds.tsv"
    rows = []
    for line in f.read_text().splitlines():
        addr, _kind, name = line.split("\t")
        rows.append((int(addr, 16), name))
    return sorted(rows)


def implemented_addrs(prefix):
    done = set()
    for f in sorted((ROOT / "src").glob(f"{prefix}_*.cpp")):
        done |= {int(m, 16) for m in ADDR_RE.findall(f.read_text())}
    return done


def decompiled(table):
    total = 0
    for d in (ROOT / "decomp").glob(f"{table}*"):
        fns = d / "functions"
        if fns.is_dir():
            total += len(list(fns.glob("*.c")))
    return total


def classified(table, threshold=0.55):
    f = ROOT / "analysis" / f"{table}-matches.txt"
    if not f.exists():
        return 0
    n = 0
    for line in f.read_text().splitlines():
        m = re.match(r"\S+\s+0x[0-9a-f]+\s+\w+\s+([0-9.]+)", line)
        if m and float(m.group(1)) >= threshold:
            n += 1
    return n


def report(table, src_prefix):
    rows = seeds(table)
    done = implemented_addrs(src_prefix)
    hit = [a for a, _n in rows if a in done]

    # A handler runs until the next seeded address; the last one gets a nominal span.
    addrs = [a for a, _n in rows]
    spans = {
        a: (addrs[i + 1] - a if i + 1 < len(addrs) else 0x200) for i, a in enumerate(addrs)
    }
    total_bytes = sum(spans.values())
    done_bytes = sum(spans[a] for a in hit)

    print(f"\n{table.upper()}")
    print(f"  decompiled   {decompiled(table):>4} functions")
    print(f"  classified   {classified(table):>4}/{len(rows)}  "
          f"({classified(table) / len(rows) * 100:.0f}%)  have a Space Cadet template")
    print(f"  implemented  {len(hit):>4}/{len(rows)}  "
          f"({len(hit) / len(rows) * 100:.0f}%)  handlers with a C++ body")
    print(f"               {done_bytes:>4}/{total_bytes} bytes "
          f"({done_bytes / total_bytes * 100:.0f}%)")
    missing = [(a, n) for a, n in rows if a not in done]
    if missing:
        print(f"  remaining:   {', '.join(n for _a, n in missing[:8])}"
              f"{' ...' if len(missing) > 8 else ''}")


if __name__ == "__main__":
    targets = sys.argv[1:] or ["dragon", "pirates"]
    for t in targets:
        report(t, t)
    src = sorted((ROOT / "src").glob("*.cpp")) + sorted((ROOT / "src").glob("*.h"))
    hand = [f for f in src if not f.name.startswith("control_")]
    print(f"\nhand-written C++: {sum(len(f.read_text().splitlines()) for f in hand)} lines "
          f"across {len(hand)} files")
