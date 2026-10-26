#!/usr/bin/env python3
"""Classify each bonus-table handler by its nearest Space Cadet equivalent.

CADET.EXE's handlers carry their real names (recovered from control.cpp), so a
Dragon or Skulduggery handler that is structurally close to one of them is very
likely the same kind of rule -- a rollover, a bumper, a one-way gate. That turns
"64 unknown functions" into a ranked worklist: the close matches can be written
against a known template, and the distant ones are the table's own mechanics and
deserve the manual attention.

Similarity is computed over a normalised token stream, not raw text. Addresses,
Ghidra's generated identifiers and literal constants all differ between binaries
even for identical source, so they are replaced by placeholders; what survives is
control flow and call shape.

A score is a structural resemblance, never proof. Treat it as triage.
"""

import difflib
import re
import sys
from pathlib import Path

# Ghidra artefacts that carry no cross-binary meaning.
SUBS = [
    (re.compile(r"\b(?:FUN|LAB|DAT|PTR|UNK|SUB)_[0-9a-f]+\b"), "SYM"),
    (re.compile(r"\b(?:[iu]Var|pcVar|puVar|piVar|pbVar|bVar|cVar|fVar|uVar)\d+\b"), "VAR"),
    (re.compile(r"\bparam_\d+\b"), "ARG"),
    (re.compile(r"\blocal_[0-9a-f]+\b"), "LOC"),
    (re.compile(r"\b0x[0-9a-f]+\b"), "NUM"),
    (re.compile(r"\b\d+\b"), "NUM"),
]
TOKEN_RE = re.compile(r"[A-Za-z_][A-Za-z_0-9]*|[^\sA-Za-z_0-9]")


def body(path):
    """Decompiled body with the rea comment header stripped."""
    text = path.read_text()
    lines = [ln for ln in text.splitlines() if not ln.startswith("//")]
    return "\n".join(lines)


def tokens(text):
    for rx, rep in SUBS:
        text = rx.sub(rep, text)
    return TOKEN_RE.findall(text)


def corpus(decomp_dir, seeds_tsv):
    """{name: token list} for every seeded handler with a body."""
    names = {}
    for line in Path(seeds_tsv).read_text().splitlines():
        addr, _kind, name = line.split("\t")
        names[int(addr, 16)] = name
    out = {}
    for addr, name in names.items():
        f = Path(decomp_dir) / "functions" / f"{addr:#x}.c"
        if f.exists():
            out[name] = (addr, tokens(body(f)))
    return out


def main(ref_dir, ref_seeds, tgt_dir, tgt_seeds, threshold=0.55):
    ref = corpus(ref_dir, ref_seeds)
    tgt = corpus(tgt_dir, tgt_seeds)

    # Deduplicate the reference: many components share one handler.
    print(f"reference handlers: {len(ref)}   target handlers: {len(tgt)}\n")
    print(f"{'target handler':<34} {'addr':>9}  {'best Space Cadet match':<32} {'score':>5}")

    rows = []
    for name, (addr, toks) in sorted(tgt.items(), key=lambda kv: kv[1][0]):
        best, score = None, 0.0
        for rname, (_ra, rtoks) in ref.items():
            sm = difflib.SequenceMatcher(None, toks, rtoks, autojunk=False)
            # Cheap upper bound first; only pay for the real ratio if promising.
            if sm.real_quick_ratio() <= score or sm.quick_ratio() <= score:
                continue
            r = sm.ratio()
            if r > score:
                best, score = rname, r
        rows.append((name, addr, best, score))
        mark = "" if score >= threshold else "   <- novel"
        print(f"{name:<34} {addr:#09x}  {best or '-':<32} {score:5.2f}{mark}")

    close = [r for r in rows if r[3] >= threshold]
    print(f"\n{len(close)}/{len(rows)} match a Space Cadet handler at >= {threshold}")
    print(f"{len(rows) - len(close)} look table-specific and need manual analysis")
    return rows


if __name__ == "__main__":
    if len(sys.argv) < 5:
        raise SystemExit(
            f"usage: {sys.argv[0]} CADET_DECOMP cadet-seeds.tsv TGT_DECOMP tgt-seeds.tsv"
        )
    main(*sys.argv[1:5])
