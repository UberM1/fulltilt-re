#!/usr/bin/env python3
"""Compare a native rest-state dump against the original's: step 3 of README.md.

    compare.py NATIVE.jsonl WINE.jsonl [WINE2.jsonl ...] [--json]

NATIVE.jsonl comes from the FT_RESTDUMP hook (test/restdump/native_dump.sh), the WINE
files from test/restdump/wine_probe.py. A Wine file may hold several samples (the
probe takes two, two seconds apart) and several Wine files may be given: any field
that differs between Wine samples is reported as VOLATILE and not held against the
port, because the original itself does not hold it still at rest.

Components are matched by group name (the n-th component of a name with the n-th of
the same name); tags by their position in score_components[] / simple_components[].
Unnamed components (the table layer, the table's own light group, balls) are only
counted. Exit status 1 when anything differs, 0 when nothing does.

Stdlib only.
"""

import argparse
import json
import sys
from collections import defaultdict

# The native dump records the DAT object type the component was spawned from; the
# probe records what the original's GetComponentType (vtable slot 0) returns, which
# is per class. Where one class serves two DAT types the two codes are the same thing.
# Both flipper types (1003 left, 1004 right) are one class whose getter answers 1041
# (vtable 0x437140 in DRAGON.EXE).
TYPE_ALIASES = {1010: 1000, 1029: 1012, 1034: 1001, 1003: 1041, 1004: 1041}

CLASS_NAMES = {
    1000: "TWall", 1001: "TPlunger", 1002: "TLight", 1041: "TFlipper",
    1005: "TBumper", 1006: "TPopupTarget", 1007: "TDrain", 1011: "TBlocker", 1012: "TKickout",
    1013: "TGate", 1014: "TKickback", 1015: "TRollover", 1016: "TOneway", 1017: "TSink",
    1018: "TFlagSpinner", 1019: "TSoloTarget", 1020: "TLightRollover", 1021: "TRamp",
    1022: "THole", 1023: "TDemo", 1024: "TTripwire", 1026: "TLightGroup",
    1028: "TComponentGroup", 1030: "TLightBargraph", 1031: "TSound", 1032: "TTimer",
    1033: "TTextBox", 1035: "TRampExit", 1036: "TAccelerator", 1037: "TBall",
    1038: "TLightSweep", 1042: "TMagnet", 1043: "TDiverter",
}

SKIP = {"kind", "index", "name"}


def load(path):
    """-> list of samples, each {"meta", "table", "tags": {(array, i): rec}, "comps": {key: rec}}"""
    samples, cur = [], None
    for n, line in enumerate(open(path), 1):
        line = line.strip()
        if not line:
            continue
        r = json.loads(line)
        k = r["kind"]
        if k == "meta":
            cur = {"meta": r, "table": None, "tags": {}, "comps": {}, "unnamed": 0, "seen": defaultdict(int)}
            samples.append(cur)
        elif cur is None:
            raise SystemExit(f"{path}:{n}: record before any meta line")
        elif k == "table":
            cur["table"] = r
        elif k == "tag":
            cur["tags"][(r["array"], r["index"])] = r
        elif k == "component":
            if r["name"] is None:
                cur["unnamed"] += 1
                continue
            occ = cur["seen"][r["name"]]
            cur["seen"][r["name"]] += 1
            key = r["name"] if occ == 0 else f"{r['name']}#{occ}"
            cur["comps"][key] = r
    if not samples:
        raise SystemExit(f"{path}: no samples")
    return samples


def norm_type(t):
    return TYPE_ALIASES.get(t, t)


def cls(t):
    return CLASS_NAMES.get(norm_type(t), f"type{t}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("native")
    ap.add_argument("wine", nargs="+")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    a = ap.parse_args()

    nat = load(a.native)
    if len(nat) != 1:
        raise SystemExit(f"{a.native}: expected one native sample, found {len(nat)}")
    nat = nat[0]
    wine = [s for p in a.wine for s in load(p)]
    ref = wine[0]

    diffs = []  # (section, key, field, native, wine)
    volatile = []
    notes = []

    def wine_values(getter):
        vals = []
        for s in wine:
            try:
                vals.append(getter(s))
            except KeyError:
                vals.append(KeyError)
        return vals

    def check(section, key, field, nv, getter):
        wv = wine_values(getter)
        uniq = []
        for v in wv:
            if v not in uniq:
                uniq.append(v)
        if len(uniq) > 1:
            volatile.append((section, key, field, nv, uniq))
            return
        w = uniq[0]
        if w is KeyError:
            return
        if nv != w:
            diffs.append((section, key, field, nv, w))

    # meta
    for f in ("score_components", "simple_components"):
        check("meta", "-", f, nat["meta"].get(f), lambda s, f=f: s["meta"][f])
    notes.append(f"components: native {nat['meta']['component_count']} "
                 f"({nat['unnamed']} unnamed), original {ref['meta']['component_count']} "
                 f"({ref['unnamed']} unnamed)")

    # table
    for f, v in nat["table"].items():
        if f in SKIP:
            continue
        check("table", "-", f, v, lambda s, f=f: s["table"][f])

    # tags
    for key in sorted(set(nat["tags"]) | set(ref["tags"])):
        if key not in nat["tags"] or key not in ref["tags"]:
            diffs.append(("tag", f"{key[0]}[{key[1]}]", "present",
                          key in nat["tags"], key in ref["tags"]))
            continue
        for f in ("name", "bound"):
            check("tag", f"{key[0]}[{key[1]}]", f, nat["tags"][key][f],
                  lambda s, key=key, f=f: s["tags"][key][f])

    # components
    for key in sorted(set(nat["comps"]) | set(ref["comps"])):
        n, w = nat["comps"].get(key), ref["comps"].get(key)
        if n is None or w is None:
            diffs.append(("component", key, "present", n is not None, w is not None))
            continue
        if norm_type(n["type"]) != norm_type(w["type"]):
            diffs.append(("component", key, "class", cls(n["type"]), cls(w["type"])))
        for f, v in n.items():
            if f in SKIP or f == "type":
                continue
            if f not in w:
                continue
            check("component", key, f, v, lambda s, key=key, f=f: s["comps"][key][f])
        only_n = sorted(set(n) - set(w) - SKIP)
        only_w = sorted(set(w) - set(n) - SKIP)
        if only_n or only_w:
            notes.append(f"{key}: fields only native {only_n}, only original {only_w}")

    if a.json:
        json.dump({"diffs": diffs, "volatile": volatile, "notes": notes}, sys.stdout, indent=1)
        print()
    else:
        print(f"native:   {a.native} ({nat['meta']['table']})")
        print(f"original: {', '.join(a.wine)} ({len(wine)} sample(s))")
        for line in notes:
            print(f"note: {line}")
        if volatile:
            print(f"\nVOLATILE in the original between samples ({len(volatile)}), not compared:")
            for sec, key, f, nv, uniq in volatile:
                print(f"  {sec:9} {key:28} {f:24} native={nv!r} original={uniq!r}")
        print(f"\nDIFFERENCES ({len(diffs)}):")
        bykey = defaultdict(list)
        for sec, key, f, nv, wv in diffs:
            bykey[(sec, key)].append((f, nv, wv))
        for (sec, key), items in bykey.items():
            c = ""
            if sec == "component" and key in nat["comps"]:
                c = f" [{cls(nat['comps'][key]['type'])}]"
            print(f"  {sec} {key}{c}")
            for f, nv, wv in items:
                print(f"      {f:24} native={nv!r:<14} original={wv!r}")
        if not diffs:
            print("  none")
    return 1 if diffs else 0


if __name__ == "__main__":
    sys.exit(main())
