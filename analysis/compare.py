#!/usr/bin/env python3
"""Locate the table-rules code by diffing a Full Tilt! table binary against CADET.EXE.

All three tables ship the same engine compiled from one codebase (every binary
reports its module name as _3DPINBALL_.EXE). CADET.EXE is already decompiled
upstream as k4zmu2a/SpaceCadetPinball, so whatever is NOT shared with CADET is
the per-table rules code -- the thing that has to be reverse engineered.

Coverage is measured with K-byte block hashing rather than difflib: it is linear
instead of quadratic, and 200 KB of .text makes SequenceMatcher impractical.
Note the figure is a lower bound -- relocated absolute addresses differ between
binaries, so genuinely shared code still shows small gaps.
"""

import struct
import sys
from pathlib import Path

K = 16  # block size; 16 bytes is long enough to be specific in x86


def sections(path):
    """Return {name: (virtual_address, raw_bytes)} for a PE32 image."""
    d = Path(path).read_bytes()
    pe = struct.unpack("<I", d[0x3C:0x40])[0]
    if d[pe : pe + 4] != b"PE\0\0":
        raise SystemExit(f"{path}: not a PE image (16-bit NE binaries are not supported)")
    nsec = struct.unpack("<H", d[pe + 6 : pe + 8])[0]
    optsz = struct.unpack("<H", d[pe + 20 : pe + 22])[0]
    table = pe + 24 + optsz
    out = {}
    for i in range(nsec):
        o = table + i * 40
        name = d[o : o + 8].rstrip(b"\0").decode()
        _vsz, va, rsz, ro = struct.unpack("<IIII", d[o + 8 : o + 24])
        out[name] = (va, d[ro : ro + rsz])
    return out


def coverage(base, other):
    """Per-byte mask of `other`: 1 where that byte sits in a K-block also in `base`."""
    ref = {base[i : i + K] for i in range(len(base) - K)}
    mask = bytearray(len(other))
    for i in range(len(other) - K):
        if other[i : i + K] in ref:
            mask[i : i + K] = b"\1" * K
    return mask


def report(ref_exe, target_exe, window=8192):
    ref, tgt = sections(ref_exe), sections(target_exe)
    print(f"reference : {ref_exe}")
    print(f"target    : {target_exe}\n")

    for sec in (".text", ".data", ".rdata"):
        if sec not in ref or sec not in tgt:
            continue
        a, b = ref[sec][1], tgt[sec][1]
        mask = coverage(a, b)
        shared = sum(mask)
        pct = shared / len(b) * 100 if b else 0
        print(
            f"{sec:7} ref={len(a):>7}  target={len(b):>7}  "
            f"shared={shared:>7} ({pct:5.1f}%)  NEW={len(b) - shared:>7}"
        )

        if sec == ".text":
            va = tgt[sec][0]
            print(f"\n  .text divergence map (windows of {window} bytes):")
            runs = []
            for w in range(0, len(b), window):
                chunk = mask[w : w + window]
                c = sum(chunk) / len(chunk) * 100
                flag = "  <-- RULES" if c < 50 else ""
                print(
                    f"   file {w:>7}-{min(w + window, len(b)):<7} "
                    f"rva {va + w:#08x}  {c:5.1f}% {'#' * int(c / 5):<20}{flag}"
                )
                runs.append(c < 50)
            lo = next((i for i, r in enumerate(runs) if r), None)
            if lo is not None:
                hi = len(runs) - 1 - next(i for i, r in enumerate(reversed(runs)) if r)
                print(
                    f"\n  => candidate rules region: file {lo * window}-{(hi + 1) * window}"
                    f" (rva {va + lo * window:#08x}-{va + (hi + 1) * window:#08x}),"
                    f" ~{(hi + 1 - lo) * window // 1024} KB"
                )


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} CADET.EXE TARGET.EXE")
    report(sys.argv[1], sys.argv[2])
