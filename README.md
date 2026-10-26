# Full Tilt! Pinball — Dragon's Keep & Skulduggery

Goal: add the two missing Full Tilt! tables to
[k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball), which today
plays only Space Cadet.

Upstream lists this under "Plans" as *"Maybe: Support for the other two tables - Dragon and
Pirate"*, and the author's position in
[issue #148](https://github.com/k4zmu2a/SpaceCadetPinball/issues/148) is: *"It takes a lot
of work to decompile FT tables without any symbols... Current state of decompilation: 3DPB
- done, FT Cadet – partially done, FT others – not started."*

## Why this is tractable

The engine is **not** the problem. `TPinballTable.cpp` builds the table from
`loader::query_handle("table_objects")` in the `.DAT`, spawning generic components by type
code (`1000` → `TWall`, `1005` → `TBumper`, …). Geometry, collision, sprites, sounds and
physics are all data-driven and would load a new table unchanged.

What is hardcoded is the **rules**: 229 components bound by group name to 56 handler
functions, plus the mission/rank system — about 4,600 lines in `control.cpp`.

Three independent measurements say that code is small and localized.

### 1. The rules live in one contiguous region

`analysis/compare.py` diffs a table binary against `CADET.EXE` using 16-byte block hashing.
All three executables are the same 1996-03-16 build of one codebase (each reports its
module name as `_3DPINBALL_.EXE`), so whatever is *not* shared with CADET is the rules.

```
$ python3 analysis/compare.py binaries/win95/CADET.EXE binaries/win95/DRAGON.EXE

.text   ref= 200704  target= 206848  shared= 148561 ( 71.8%)  NEW=  58287
   file  131072-139264  rva 0x021000   60.9% ############
   file  139264-147456  rva 0x023000    7.7% #                  <-- RULES
   file  147456-155648  rva 0x025000   11.0% ##                 <-- RULES
   file  155648-163840  rva 0x027000    7.8% #                  <-- RULES
   file  163840-172032  rva 0x029000    7.0% #                  <-- RULES
   file  172032-180224  rva 0x02b000   39.4% #######            <-- RULES
   file  180224-188416  rva 0x02d000   91.2% ##################
```

~40 KB, at **rva 0x023000–0x02d000**. `PIRATES.EXE` diverges over the *same* address range
(2–10% shared) — same link order, so the rules sit at the same place in all three.

### 2. The component table can be read straight out of the binary

`analysis/dump_components.py` locates the static `score_components[]` array in `.data`. The
1996 MSVC build lays each record out as 16 bytes:

```
+0  tag*   -> component_tag_base { const char* Name; TPinballComponent* Component; }
+4  void*  control function  (.text)
+8  u32    score count
+12 int*   score array       (.data) or NULL
```

Run against `CADET.EXE` it recovers **88 entries** — exactly `score_components[88]` — in
`control.cpp`'s declaration order with matching values:

```
  0  a_bump1   0x00423220  4  500, 1000, 1500, 2000
  1  a_bump2   0x00423220  4  500, 1000, 1500, 2000
  2  a_bump3   0x00423220  4  500, 1000, 1500, 2000
  3  a_bump4   0x00423220  4  500, 1000, 1500, 2000
  4  a_roll3   0x004236f0  1  2000
```

against upstream's

```cpp
component_info{control_bump1_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_bump2_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_bump3_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_bump4_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_roll3_tag, {ReentryLanesRolloverControl, 1, control_roll_scores1}},
```

The method is therefore validated against known ground truth before being pointed at an
unknown binary.

Applied to the other two it yields, with no manual disassembly:

| table | components | distinct handlers | output |
|---|---|---|---|
| Space Cadet | 88 | — | (ground truth) |
| Dragon's Keep | 75 | 64 | `analysis/dragon-components.txt` |
| Skulduggery | 74 | 69 | `analysis/pirates-components.txt` |

Every Dragon handler address falls in `0x423810`–`0x428290`, i.e. rva `0x23810`–`0x28290` —
inside the region predicted by the divergence map. The two methods agree.

### 3. We have the exact binary upstream decompiled

`binaries/win95/CADET.EXE` hashes to `3F7B5699074B83FD713657CD94671F2156DBEDC4`, the SHA-1
named in upstream's README as the 32-bit build it was decompiled from. `control.cpp` is a
line-by-line oracle for the CADET side of every comparison.

## Binaries

From the hybrid ISO/HFS CD image at archive.org item
[`full-tilt-pinball-1996-windows`](https://archive.org/details/full-tilt-pinball-1996-windows)
(CloneCD raw 2352-byte sectors; `analysis/` notes the conversion). All dated 1996-03-16.

| file | SHA-1 | notes |
|---|---|---|
| `win95/CADET.EXE` | `3F7B5699074B83FD713657CD94671F2156DBEDC4` | **the decompiled one** |
| `win95/DRAGON.EXE` | `7BBC2FEF948613644773B7689FB0CCA28C7F1B5C` | target |
| `win95/PIRATES.EXE` | `5EB97B0CF190F628C7F1C02A9D148F27A508E580` | target |
| `win95/FULLTILT.EXE` | `F2A2CE1E4337237AEDB12A68EEAB4EBDB5E0CADD` | launcher |

`binaries/win31/` holds the 16-bit NE builds. They are a dead end for tooling — Ghidra's
decompiler is weakest on segmented 16-bit code, and `rea` supports ELF/PE/Mach-O only — but
they were how the rules region was first located, and their 10 named exports
(`SPLASH_MESSAGE_HANDLER`, `KEYMAPDLGPROC`, …) are the only named symbols in any build.

`game/` is the playable 32-bit install. Both targets already run under Wine
(`wineWow64Packages.stagingFull`), which is the fallback if this project stalls.

> The game is still under copyright. Binaries and `.DAT` files stay local — `.gitignore`
> keeps them out of any published history.

## Status

Both tables' rules are written and every one of the twelve files compiles against the
patched engine with zero errors. What is left is linking them in and playing a ball --
`docs/integration.md` is that step, and `docs/engine-changes.md` is the patch that got
here.

| | components | handlers | implemented |
|---|---|---|---|
| Dragon's Keep | 75 | 64 | **64 (100%)** |
| Skulduggery | 74 | 69 | **69 (100%)** |

- [x] Locate the rules region (rva `0x023000`-`0x02d000`, ~40 KB), three ways
- [x] Validate every extraction method against `control.cpp` before using it
- [x] Tooling: Ghidra 12.1.2 + `rea`, packaged in `nixos_conf` as
      `modules/features-nixos/home/reversing.nix`
- [x] Decompile the rules and engine: 631 Dragon, 364 Skulduggery, 179 CADET functions
- [x] Disassemble every handler with objdump, as an independent check on the decompiler
- [x] Resolve every `DAT_<addr>` to its component name (4,240 references annotated)
- [x] Recover Full Tilt's `MessageCode` numbering (piecewise)
- [x] Dragon's Keep: all 64 handlers and the quest state machine
- [x] Skulduggery: the mode state machine and 63 of 69 handlers
- [x] Build upstream from source (`nix-build build.nix`)
- [x] Skulduggery: all 69 handlers
- [x] The engine changes in `docs/engine-changes.md` -- every rule file compiles
- [x] Point the component tables at the real handlers rather than generated stubs
- [ ] Wire `reference/SpaceCadetPinball/table_rules.h` into the engine and actually play a ball

### What the compiler says

Five of nine rule files compile against **unmodified** upstream headers. The split is
itself a finding: per-component rules need nothing Space Cadet did not already need, and
every outstanding error comes from running more than one table.

## Pipeline

```
dump_components.py   score_components[] out of .data: names, handlers, scores
make_seeds.py        -> REA_GHIDRA_SEED_FILE; Ghidra cannot find these functions
                        on its own, nothing calls them
rea decompile-all    seeded, headless, ~160 functions per binary
match_handlers.py    nearest named Space Cadet handler, by normalised token stream
message_codes.py     Full Tilt's MessageCode numbering, by alignment with control.cpp
gen_control.py       -> src/control_<table>.cpp: tables exact, bodies TODO
dump_tags.py         resolves every DAT_<addr> to its component name, and
                     annotates the decompiled corpus in place
disasm.py            objdump listings for every handler -- the decompiler
                     mis-models some stack frames, and the listing wins
dump_strings.py      RT_STRING resources: the rules identify messages by id,
                     so this is the binaries' own documentation
```

Everything but the hand-written rules regenerates from `binaries/` plus the scripts,
which is why `decomp/` and the generated skeletons are not tracked.

### Triage

| | components | handlers | with a Space Cadet template | table-specific |
|---|---|---|---|---|
| Dragon's Keep | 75 | 64 | 50 | 14 |
| Skulduggery | 74 | 69 | 51 | 18 |

Skulduggery's 18 include all six magnets (`s_magnet1`-`6`), which score identically
against every Space Cadet handler and are therefore one new behaviour, not six.
Dragon's 14 are dominated by the sinks, which is where the manual puts the dragon-egg
and treasure collection -- two independent sources pointing at the same components.

## Layout

```
analysis/   the scripts above, and their output
binaries/   win95/ (PE32 i386, the real targets) and win31/ (NE 16-bit)
game/       playable 32-bit install from the CD
docs/       FULLTILT.HLP (the CD's manual) and a rules spec drawn from it
src/        generated control_<table>.cpp skeletons (regenerable, untracked)
reference/  upstream SpaceCadetPinball clone, the oracle for every CADET claim
            (untracked; re-clone with
            `git clone --depth 1 https://github.com/k4zmu2a/SpaceCadetPinball reference`)
```
