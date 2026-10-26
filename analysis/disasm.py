#!/usr/bin/env python3
"""Disassemble every seeded handler locally, as a check on the decompiler.

Ghidra's pseudocode is the readable form, but it silently mis-models stack frames
on some of these functions -- several handlers end in `RET 0x8` (__stdcall, two
args, the usual `(MessageCode, TPinballComponent*)` shape) while the decompiler
infers one parameter and then renders the argument pushes as writes into a local
byte array. Control flow and call targets survive that; argument *values* do not.

Where the two disagree the listing wins, so having a listing for every handler
removes a whole class of wrong conclusions. Going through the provider for this
would mean one Ghidra import per function; objdump over the raw .text bytes gives
the same answer for the whole binary in one pass.

Function extents come from the seed file: a handler runs until the next seeded
address. That slightly over-reads the last one, which is harmless for reading.
"""

import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from dump_components import load  # noqa: E402


def text_section(path):
    d, imgbase, secs = load(path)
    for name, va, _sz, ro, rsz in secs:
        if name == ".text":
            return d, imgbase + va, d[ro : ro + rsz]
    raise SystemExit(f"{path}: no .text")


def disassemble(blob, vma):
    """objdump over raw bytes; the PE loader is not needed just to decode i386."""
    with_tmp = Path("/tmp/.ft_text.bin")
    with_tmp.write_bytes(blob)
    out = subprocess.run(
        [
            "objdump", "-D", "-b", "binary", "-m", "i386",
            "-M", "intel", f"--adjust-vma={vma:#x}", str(with_tmp),
        ],
        capture_output=True, text=True, check=True,
    ).stdout
    with_tmp.unlink()
    lines = {}
    for ln in out.splitlines():
        if ":\t" not in ln:
            continue
        addr = ln.split(":", 1)[0].strip()
        try:
            lines[int(addr, 16)] = ln.rstrip()
        except ValueError:
            continue
    return lines


def main(exe, seeds_tsv, out_dir):
    _d, text_va, blob = text_section(exe)
    listing = disassemble(blob, text_va)

    seeds = []
    for line in Path(seeds_tsv).read_text().splitlines():
        addr, _kind, name = line.split("\t")
        seeds.append((int(addr, 16), name))
    seeds.sort()

    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    addrs = sorted(listing)
    written = 0
    for i, (start, name) in enumerate(seeds):
        end = seeds[i + 1][0] if i + 1 < len(seeds) else start + 0x400
        body = [listing[a] for a in addrs if start <= a < end]
        if not body:
            continue
        (out / f"{start:#x}.txt").write_text(
            f"; {name}\n; {start:#x}-{end:#x}, {len(body)} instructions\n"
            f"; objdump -b binary -m i386 -M intel over {Path(exe).name}'s .text\n\n"
            + "\n".join(body) + "\n"
        )
        written += 1
    print(f"{exe}: {written} handler listings -> {out}")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit(f"usage: {sys.argv[0]} TABLE.EXE seeds.tsv OUT_DIR")
    main(*sys.argv[1:])
