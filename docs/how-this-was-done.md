# How this was done

A method account for *Dragon's Keep* and *Skulduggery* — the two Full Tilt! Pinball (1996)
tables that [k4zmu2a/SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball) has
never supported. Upstream lists them under "Plans" as *"Maybe"*, and the author's position
in issue #148 is that the work "has the lowest priority" because "it takes a lot of work
to decompile FT tables without any symbols".

This document is for two readers: someone who wants to redo this on another binary, and
someone who wants to decide whether to trust the result. It assumes reverse-engineering
literacy and no knowledge of pinball or of this repo.

Every number below is followed by the command that produces it. Where this document and
the code disagree, the code is newer — `src/` was still being edited while this was
written, so the coverage and line-count figures are timestamped rather than asserted.

---

## 0. The shape of the problem

A Full Tilt! table is `TABLE.EXE` plus `TABLE.DAT`. The `.DAT` holds geometry, sprites,
sounds, collision data and per-component attributes. The `.EXE` holds the engine *and* the
table's rules, statically linked together — all three tables report their module name as
`_3DPINBALL_.EXE`, i.e. they are the same codebase compiled three times with a different
rules translation unit.

Upstream's engine is already table-agnostic. `TPinballTable` builds a table by walking
`loader::query_handle("table_objects")` and spawning a generic component per type code
(`1000` → `TWall`, `1005` → `TBumper`, …). Nothing about geometry, physics, rendering or
sound is table-specific. What *is* hardcoded is `control.cpp`: 4,653 lines
(`wc -l reference/SpaceCadetPinball/control.cpp`) binding named components to handler
functions, plus Space Cadet's mission system.

So the job was never "decompile a game". It was "decompile one translation unit, twice".

---

## 1. Establishing that the rules are a small, contiguous region

This is the part worth stealing. Three independent measurements were made to agree before
any handler was read.

### 1.1 Measurement one: binary divergence against CADET.EXE

`analysis/compare.py` hashes 16-byte blocks of each PE section and reports, per 8 KB
window, what fraction of the target's blocks also occur in the reference. CADET.EXE is the
Space Cadet table — same engine, different rules — so whatever is *not* shared with CADET
is a candidate for being the rules.

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
  => candidate rules region: file 139264-180224 (rva 0x023000-0x02d000), ~40 KB
```

The signal is unambiguous: sharing collapses from 60–99% to 7–11% across five consecutive
windows and recovers immediately after. ~40 KB out of a 201 KB `.text`.

`PIRATES.EXE` diverges over the **same** address range — 2.1–10.6% shared,
`python3 analysis/compare.py binaries/win95/CADET.EXE binaries/win95/PIRATES.EXE` — which
is the useful part: identical link order in all three binaries means the rules object file
lands at the same place every time, so the region found once is the region in every table.

Two caveats the script documents itself. The shared percentage is a **lower bound**:
relocated absolute addresses differ between images, so genuinely identical code still
shows small gaps — which is why 91% rather than 100% counts as "this window is engine".
And block hashing was chosen over `difflib` for a boring reason: 200 KB of `.text` makes
`SequenceMatcher` impractical.

### 1.2 Measurement two: the component table is readable straight out of `.data`

Upstream's `control.cpp` declares

```cpp
component_info control::score_components[88]      // line 533
component_tag_base* control::simple_components[145]   // line 626
```

and the 1996 MSVC build lays each `component_info` out as a flat 16-byte record:

```
+0  tag*   -> component_tag_base { const char* Name; TPinballComponent* Component; }
+4  void*  control function (.text)
+8  u32    score count
+12 int*   score array (.data) or NULL
```

`analysis/dump_components.py` finds the array with no hardcoded addresses: it anchors on
runs of records whose name pointer lands in a string section and whose function pointer
lands in `.text`. That makes the same script work on all three binaries.

This is a *second*, independent fix on the rules region, because the table gives the
address of every handler directly:

| table | components | distinct handler addresses | handler address range |
|---|---|---|---|
| CADET | 88 | 56 | `0x423170`–`0x426230` |
| DRAGON | 75 | 64 | `0x423810`–`0x428290` |
| PIRATES | 74 | 69 | `0x422a80`–`0x42a740` |

(`python3 analysis/dump_components.py binaries/win95/DRAGON.EXE`; ranges are the first and
last lines of `analysis/*-seeds.tsv`.) Every one of those addresses lies inside, or within
2 KB of, the window the divergence map predicted. Two methods with nothing in common —
block hashing of `.text` versus pointer-chasing in `.data` — agree.

### 1.3 Measurement three: the tag table accounts for every component

`analysis/dump_simple.py` recovers the other half of the binding table, `simple_components[]`:
a contiguous run of pointers to components that get a live object the rules can poke but no
handler of their own. `analysis/dump_tags.py` recovers every `component_tag` in the image by
scanning for `{const char* Name; TPinballComponent* Component;}` pairs.

The three counts have to reconcile, and they do:

| table | score_components | simple_components | tags found | sum |
|---|---|---|---|---|
| CADET | 88 | 145 | 233 | 233 ✓ |
| DRAGON | 75 | 150 | 225 | 225 ✓ |
| PIRATES | 74 | 192 | 266 | 266 ✓ |

(`python3 analysis/dump_tags.py binaries/win95/CADET.EXE | grep -c 0x00`, and the tails of
`dump_simple.py` for each table.) An exact reconciliation means the tag scan is neither
missing components nor inventing them, which is what licenses using it to name globals.

### 1.4 Why this ordering matters

Everything above is mechanical and takes minutes. It converts "reverse engineer a 1996
game" into "read 64 and 69 functions in a known 40 KB window, with every component they
touch already named". None of it requires understanding a single rule.

---

## 2. The oracle

### 2.1 What it is

`binaries/win95/CADET.EXE` hashes to `3F7B5699074B83FD713657CD94671F2156DBEDC4`
(`sha1sum binaries/win95/CADET.EXE`). That exact SHA-1 is named in upstream's own README
under "Source":

```
* `pinball.exe` from `Windows XP` (SHA-1 `2A5B525E...`) and its public PDB
* `CADET.EXE` 32bit version from `Full Tilt! Pinball` (SHA-1 `3F7B5699074B83FD713657CD94671F2156DBEDC4`)
```

So for one of the three tables there is a published, human-reviewed decompilation of the
same bytes. Every extraction method here was run against CADET first, and its output
checked line-by-line against `control.cpp`, before being pointed at a binary nobody has
decompiled.

### 2.2 The clearest instance

`dump_components.py` on CADET.EXE finds the table at file `0x34d10` and recovers 88
entries — matching `score_components[88]` exactly — in `control.cpp`'s own declaration
order, with the score arrays attached:

```
$ python3 analysis/dump_components.py binaries/win95/CADET.EXE
  table at file 0x34d10 (rva 0x38d10), 88 entries
    0  a_bump1   0x00423220  4  500, 1000, 1500, 2000
    1  a_bump2   0x00423220  4  500, 1000, 1500, 2000
    2  a_bump3   0x00423220  4  500, 1000, 1500, 2000
    3  a_bump4   0x00423220  4  500, 1000, 1500, 2000
    4  a_roll3   0x004236f0  1  2000
```

against `reference/SpaceCadetPinball/control.cpp:535`:

```cpp
component_info{control_bump1_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_bump2_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_bump3_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_bump4_tag, {BumperControl, 4, control_bump_scores1}},
component_info{control_roll3_tag, {ReentryLanesRolloverControl, 1, control_roll_scores1}},
```

with `int control_bump_scores1[] = {500, 1000, 1500, 2000};` and
`int control_roll_scores1[] = {2000};` at lines 35–36. Group names, handler sharing
(four bumpers, one `BumperControl`), score counts, score values and ordinal position all
agree, for all 88 entries. `analysis/cadet-order.txt` is that order extracted from
`control.cpp`; a diff of it against the binary's rows is what makes the next step possible.

The practical payoff: because the orders align, *entry N's name in the source applies to
entry N's address in the binary*. `analysis/make_seeds.py` uses exactly this to label the
CADET decompilation with real upstream names (`BumperControl`, `WormHoleControl`, …),
which turns the CADET corpus into a named reference the two unknown tables can be matched
against.

### 2.3 The oracle's limits, measured

Two things qualify this and should not be glossed over.

**Upstream decompiled two binaries, not one.** The symbol names come from
`pinball.exe`'s *public PDB*; `CADET.EXE` supplied the Full Tilt behaviour that upstream
guards with `pb::FullTiltMode`. So `control.cpp` is a merge of two builds, and matching it
against CADET.EXE is a cross-build comparison, not a self-check.

**There is at least one measurable divergence.** `control.cpp` names 57 distinct
`ControlFunc`s across its 88 entries; CADET.EXE has 56 distinct handler addresses. The
collision is at entries 48 and 49:

```
48  a_flip1  0x00424890  control_flip1_tag  LeftFlipperControl
49  a_flip2  0x00424890  control_flip2_tag  RightFlipperControl
```

Both flippers bind the *same* function. Upstream's two functions are not identical
(`TLightGroupStepBackward` vs `StepForward`), so this is not linker folding. The listing
settles which one the binary has:

```
424890: cmp  DWORD PTR [esp+0x4],0x1    ; code == TLightTurnOn
424895: jne  0x4248ba
...
4248a0: push 0x1c                       ; 28
4248a8: call DWORD PTR [eax+0x4]        ; bmpr_inc_lights->Message(28, 0.0)
4248b3: push 0x1c
4248b7: call DWORD PTR [eax+0x4]        ; ramp_bmpr_inc_lights->Message(28, 0.0)
4248ba: ret  0x8
```

One code, `0x1c` = 28, to both light groups. Under this project's own piecewise
message-code correction (§5.1), `TLightGroup` codes are FT = upstream + 4, so 28 is
upstream's 24 = `TLightGroupStepBackward` — i.e. the binary has only
`LeftFlipperControl`, bound to both flippers, and `RightFlipperControl` exists in
`pinball.exe` but has no address in CADET.EXE (`grep Flipper analysis/cadet-seeds.tsv`
returns three entries, not four).

That is a satisfying triple: an independent confirmation of the message-code mapping, a
concrete demonstration that the oracle is a *reference* and not ground truth, and the same
asymmetry that commit `d7ab03a` ("drive every flipper on a side") had to handle on the
bonus tables, where upstream's one-pointer-per-side model leaves the lower flippers inert.

### 2.4 The oracle extends past `control.cpp`

Both bonus tables use seven `.DAT` object type codes that Space Cadet's data never
defines, so upstream's loader skips them: `1034` launcher, `1035` ramp exits, `1036`
accelerators, `1037`, `1038` light sweeps, `1042` magnets, `1043` diverters.

The classes for all seven are nevertheless **in CADET.EXE** — they are shared engine code,
which is why the assertion string `"sweep == NULL"` sits in the Space Cadet binary even
though Space Cadet never constructs a light sweep. Five were recovered first, by reading
CADET.EXE's own `TPinballTable` constructor (`TLightSweep`, `TMagnet`, `TDiverter`, plus
`1034` as a `TPlunger` flag and `1037` as a `TBall`). `1035` and `1036` shipped for a while
as stand-ins -- a `TRamp` and a `TKickout` -- and are now real classes, `TRampExit` and
`TAccelerator`, built from their own constructors in the bonus binaries:

```
case 1035:
    // s_rexit*: ramp exits, one ramp wall (1301) each.
    new TRampExit(this, groupIndex);
case 1036:
    // a_accl*: accelerators, a wall plus a kicker.
    new TAccelerator(this, groupIndex);
```

A later review compared both against their listings (`TRampExit` 0x41b4f0 / 0x41b6b0,
`TAccelerator` 0x41b7d0 / 0x41b8f0 / 0x41b9f0) and found them exact. No object type in
either DAT now falls to the loader's `default:`.

The lesson generalises: the oracle binary contains more than the published decompilation
of it does, because the publisher only decompiled what their data exercised.

---

## 3. The pipeline

Scripts live in `analysis/`; each carries a docstring stating why it exists. Everything
except the hand-written rules regenerates from `binaries/` plus these scripts, which is
why `decomp/` and the generated skeletons are untracked.

```
compare.py           locate the rules region by 16-byte block divergence vs CADET
dump_components.py   score_components[] out of .data: names, handlers, score arrays
dump_simple.py       simple_components[]: the components bound with no handler
dump_tags.py         every component_tag; --annotate rewrites the decomp in place
dump_strings.py      RT_STRING resources -- the binaries' own documentation
make_seeds.py        -> REA_GHIDRA_SEED_FILE (Ghidra finds none of these functions)
rea decompile-all    seeded, headless Ghidra
disasm.py            objdump listing per handler, as a check on the decompiler
match_handlers.py    nearest named Space Cadet handler, by normalised token stream
message_codes.py     recover FT's MessageCode numbering by alignment with control.cpp
gen_control.py       -> src/control_<table>.cpp: tables exact, bodies TODO
gen_glue.py          -> src/<table>_components.cpp: the cross-slice tag definitions
link_impls.py        pair each original address with the C++ function implementing it
coverage.py          report decompiled / classified / implemented, three distinct things
```

Tooling: Ghidra 12.1.2 from nixpkgs-unstable plus `rea` 6.8.0, both packaged in
`nixos_conf` as `modules/features-nixos/home/reversing.nix` (26.05's Ghidra 12.0.4 is
rejected by `rea`'s version gate, which is why the override exists).

### 3.1 The seed file — why Ghidra alone finds nothing

This is the first non-obvious stage. **Ghidra's auto-analysis does not find these handlers
at all.** Nothing calls them. They are only reachable as function pointers at `+4` of each
16-byte `score_components[]` record, and auto-analysis will not promote a `.data` dword to
a function entry point. Run headless on `DRAGON.EXE` with no help and the 40 KB rules
region is undefined bytes.

`make_seeds.py` fixes that, and the fix is free, because `dump_components.py` already has
every handler address. It emits `address<TAB>kind[<TAB>name]` rows for
`REA_GHIDRA_SEED_FILE`:

```
$ head -4 analysis/dragon-seeds.tsv
0x00423810	function	Dragon_a_rocka_Control
0x00423840	function	Dragon_a_rockb_Control
0x00424590	function	Dragon_s_oneway2_Control
0x004245b0	function	Dragon_s_plunger1_Control
```

For CADET the real names are applied by index-aligning `control.cpp`'s 88 entries with the
binary's 88 (§2.2). For the unknown tables a handler is named after the first component it
serves — `a_bump1` → `Dragon_a_bump1_Control` — which is all the binary offers and is
enough to navigate by.

Seeded, `rea decompile-all` yields:

```
decomp/cadet/         165 functions     decomp/cadet_engine/    14
decomp/dragon/        159               decomp/dragon_engine/  472
decomp/pirates/       178               decomp/pirates_rest/   186
```

(`ls decomp/*/functions/*.c | wc -l` per directory) — 631 Dragon, 364 Skulduggery, 179
CADET. `pirates_rest/` covers `0x42d000`–`0x435000`, which is where Skulduggery's
dispatcher turned out to live; the engine corpora exist because questions kept escaping
the rules region (component classes, `TBlocker::Message`, the loader).

### 3.2 The objdump pass — why the decompiler is not trusted alone

This is the second non-obvious stage. Ghidra's pseudocode **silently mis-models some stack
frames here**. Several handlers end in `RET 0x8` — `__stdcall`, two arguments, the usual
`(MessageCode, TPinballComponent*)` shape — while the decompiler infers one parameter and
then renders the argument pushes as writes into a local byte array (`abStack_NN[...]`).
Control flow and call targets survive that. Argument *values* do not. A float pushed onto
the x87 stack can vanish from the pseudocode entirely.

`analysis/disasm.py` therefore produces an objdump listing for every seeded handler:
`objdump -D -b binary -m i386 -M intel --adjust-vma=<text VA>` over the raw `.text`,
sliced by seed addresses. One pass per binary, versus one Ghidra import per function if
the same thing were done through the decompiler. **Where the two disagree, the listing
wins**, and that rule is applied in the analysis documents by name.

The payoff is documented at the point it landed. `Dragon_a_sink18_Control` (`0x427100`) is
unusable as decompiled; it also happens to carry *Rescue Maiden*'s award ladder and the
whole tower-lock scoring path. Reading its listing produced four corrections to
already-written conclusions (`analysis/dragon-ball-handlers.md`, "Corrections this forces
on §3.3"): two timer values were `0.0` not `-1.0`, `ArmNextTowerLock()` is called on both
paths rather than one, `DisableTowerLocks()` is called on neither path in mode 6, and
`rand()` is open-coded as `ADD EAX,EAX / CDQ / AND EDX,0x7fff / ADD EAX,EDX / SAR EAX,0xf`,
confirming the `(rand()*N)>>15` idiom that had been read off the pseudocode.

The same document then does the thing that makes this worth the trouble — it states the
*confidence transfer* explicitly: the listing matched the pseudocode's control flow and
call targets instruction for instruction, so the pseudocode's control flow elsewhere in
the corpus is trustworthy and only its argument values need treating as suspect.

### 3.3 The rest of the stages

- **`dump_tags.py --annotate`** rewrites the decompiled `.c` files in place, appending each
  component name to its global: `DAT_0043a3c4` becomes `DAT_0043a3c4/*a_sink1*/`. This is
  the single biggest readability win in the project — 4,314 annotated references across the
  six corpora (`grep -oh "DAT_[0-9a-f]*/\*[^*]*\*/" decomp/*/functions/*.c | wc -l`). A
  handler full of hex globals becomes a handler full of component names.

- **`dump_strings.py`** extracts `RT_STRING` resources. The rules identify their messages
  by id, so the string table is the binaries' own documentation and is independent of both
  the decompiler and the manual: a handler that displays 3010 is the one that traps a ball,
  whatever its component is called (`3003 Tower Lock Enabled`, `3004 Pass Clear`,
  `3010 Ball Trapped`, `3011 Extra Ball` — `analysis/dragon-strings.txt`). Commit
  `4e10c72` records that the strings independently confirmed the quest and tower-lock
  findings.

- **`match_handlers.py`** scores each unknown handler against every named CADET handler by
  normalised token stream — Ghidra identifiers, addresses and literal constants replaced by
  placeholders, so what is compared is control flow and call shape. This is triage, never
  proof, and the script says so. It turns "64 unknown functions" into a ranked worklist:
  50/64 Dragon and 51/69 Skulduggery handlers have a Space Cadet template worth working
  from, and the rest are the table's own mechanics (`analysis/dragon-matches.txt`,
  `python3 analysis/coverage.py`).

- **`message_codes.py`** recovers Full Tilt's `MessageCode` numbering (see §5.1).

- **`gen_control.py` / `gen_glue.py`** generate the mechanical parts of
  `src/control_<table>.cpp` — score arrays, tags, the `score_components[]` array — because
  there is no reason to transcribe `.data` by hand. Handler bodies are left as `TODO`
  stubs, deliberately not guessed.

- **`link_impls.py`** pairs each original address with the C++ function that now implements
  it, keyed on a convention: every reimplementation carries its original address in a
  comment immediately above the definition. It also reports any handler defined outside its
  table's namespace — a mismatch that would compile and then fail to link.

- **`coverage.py`** reports three numbers that are routinely conflated: *decompiled* (near
  total, not the bottleneck), *classified* (triage), *implemented* (the real one). It counts
  bytes as well as handlers, because handlers vary by an order of magnitude in size. Its
  "implemented" counts only the functions bound in `score_components[]`, and §5.7 is what
  that blind spot cost; it now also prints a census of the whole rules region.

At the time of writing (abridged):

```
$ python3 analysis/coverage.py
DRAGON
  classified     50/64  (78%)  have a Space Cadet template
  implemented    64/64  (100%)  score_components[] handlers with a C++ body
  rules region 0x422a80..0x42c290 (whole region, not just score_components[])
  functions     124/124  (100%)  with a counterpart: 118 cited in src/, 0 inlined, 6 engine side
PIRATES
  classified     51/69  (74%)  have a Space Cadet template
  implemented    69/69  (100%)  score_components[] handlers with a C++ body
  rules region 0x422a80..0x42daa0 (whole region, not just score_components[])
  functions     177/177  (100%)  with a counterpart: 170 cited in src/, 1 inlined, 6 engine side
hand-written C++: 18301 lines across 16 files
```

"Cited" is an address in a comment next to a reimplementation. It is a counterpart, not a
proof of behaviour, and §5.7 records a time it was not even a definition.

### 3.4 The engine patch, and how its contents were decided

`engine/fulltilt-engine.patch` applies to a shallow clone of upstream in `reference/`;
`nix-build build.nix` builds it via nixpkgs' own `space-cadet-pinball` derivation (upstream's
vendored `FindSDL2.cmake` does not cope with a `/nix/store` SDL2).

The change list was *derived, not designed*: the rules were written first, against the
originals, then compiled against unmodified upstream headers. Every error was something the
engine did not have, so the error list **was** the change list — it went 88 errors to 0.
`docs/engine-changes.md` walks the items; the largest is
`TPinballComponent::GetComponentType()`, which is a correction to upstream rather than an
addition: upstream models vtable slot 0 as the destructor, but the dispatcher reads it and
switches on `0x3ee`/`0x3f8`/`0x3f9`/`0x3fb`/`0x408`/`0x40e` — the component type ids.
Confirmed twice independently, in CADET.EXE's `0x42a150` and in PIRATES.EXE's `0x42d850`.

A second class of change never surfaces as a compile error, because no rule names it: the
seven object types the loader skips, `loader::sound_list` holding 64 against `PIRATES.DAT`'s
67 sound groups, kicker attribute 410 (unknown to `loader::kicker`, used by 18 kickers in
`DRAGON.DAT`, and reached through a path that returns early and leaves `TKickout`
dereferencing a null `FloatArr`). Those were found by component analysis and by reading
CADET.EXE's loader, not by the compiler.

### 3.5 What running proves

`test/run_tables.sh` is a construction harness for all three tables, and its header is
unusually honest about its own scope, which is worth repeating here:

> A green row means exactly this: the engine built, the loader parsed that table's `.DAT`
> without raising an error, every component the table names got constructed and bound, and
> the process entered `winmain::MainLoop` and kept pumping events until it was asked to
> quit. [...] Nothing here launches a ball, presses a flipper, enters a quest or reads a
> score. A table whose every handler scores the wrong amount, whose quest machine never
> advances, or whose dispatch order double-scores passes this harness with a clean sheet.

Its warning allowlist is a record of understood warnings, not a wishlist — anything not
listed fails the table, so a new warning is a regression by default.

Beyond it there is an env-gated debug harness in the engine (`FtAccelDebug.cpp`):
`FT_AUTOPLAY` starts a game and plays it blind, and `FT_HOLETEST` / `FT_SINKTEST` drive the
ball into a named hole or sink and log what happens. Those catch crashes and stuck balls.
None of it renders, and no one has yet played either table in a real window against the
original.

---

## 4. The binaries versus the manual

`FULLTILT.HLP` — the CD's own help file, which contains a complete "Table Rules" chapter —
was transcribed into `docs/dragon-rules-spec.md` (909 lines) with a source tag on every
claim: `[MANUAL]`, `[STRINGS]`, `[DAT]`, `[INFERRED]`, `[GAP]`. It is an uncompressed Win 3.x
HLP, so topic text is recoverable by byte scanning; the spec notes that a naive
`strings`-style scan with a minimum length above 3 silently loses table cells containing
short numbers, which is how scores hide.

The standing rule in every analysis document is **the binary is authoritative**, and
disagreements are called out rather than reconciled. They run in both directions.

### 4.1 Where the binary corrected the manual

`analysis/dragon-scoring-handlers.md` §9 and `analysis/dragon-ball-handlers.md` §7 are the
two tables of these. A representative sample, each with the evidence that settles it:

| the manual says | the binary says | why it is certain |
|---|---|---|
| another weapon every **8** forest-bumper hits | **10** | `if (++a_bump1->MessageField == 10)` — one compare against an immediate |
| Quest Selection Target: 5,000 **and** an extra ball if lit | strictly either/or — the lit path returns before `AddScoreFor` | the two paths are disjoint |
| Dragon Pass pays on **each** gargoyle target | only `a_spot8`/`a_spot12`; the two rock-mounted ones keep ordinary bank behaviour | `0x4254a0` and `0x425570` contain no mode-7 test |
| **five** D-R-A-G-O-N lights | `FUN_004244e0` tests exactly **six** (`v_lite22`…`v_lite27`) | binary agrees with the `.DAT`; the manual is wrong |
| Slay Dragon's award ladder is deterministic | `a_sink19` gates the top three rungs on `rand()` — jackpot 1-in-5, extra-ball light 1-in-10, extra ball 1-in-10 | read directly |
| Dragon Breath lasts 60 s | 30 s | `FUN_004236b0` case 0: `TLightTurnOnTimed, 30.0f` |
| the Tower Ramp is a `[GAP]` | no score at all; a repeat pass inside a 10 s window casts a random spell | `a_ramp6` |

The binary also supplied whole mechanics the manual never mentions: completing
D-R-A-G-O-N fires mode `0xf` (the manual never says what it does), the Dragon Chute has a
five-step score ladder (25k/75k/100k/125k/150k indexed by `v_lite68->MessageField`), the
jackpot clamps at 10,000,000 and the super jackpot at 20,000,000 where the manual gives
only the 1,000,000 minimum, and `table_add_extra_ball` *assigns* `ExtraBalls = 1` rather
than incrementing, so extra balls do not stack — the opposite of CADET.

And one load-bearing structural fact no document could have supplied: **Dragon's `handler()`
calls `MissionControl` before the component's own `ControlFunc`**, the reverse of upstream.
Many of the handlers' `if (quest != 4) …` suppressions only make sense with that ordering.
Getting it backwards silently double-scores.

### 4.2 Where the manual was right and the code was unreadable

The traffic goes the other way too, and the documents record it as it happened rather than
tidying it up.

- *Dragon Pass*' "gargoyle target → 100,000 + 250,000 to jackpot" was marked
  **`[U]` not located** in `analysis/dragon-quest-system.md` §8. It was later found exactly
  as the manual described, on `a_spot8`/`a_spot12`'s mode-7 path — the code was real, it
  was just in a handler that had not been read yet. The manual's claim stood unsupported in
  the meantime, and was right.
- The quest-restart window's **duration** is not visible in code at all: mode 3 *is* the
  window, but the timeout lives in `mode_timer`'s `.DAT` attributes, so the manual's "ten
  seconds" is the only source for it.
- *Slay Dragon*'s "+ all gems" reward is still **`[U]`** — no handler in scope grants it.
- Mode 4 = **Dragon Hoard** is inferred from the manual: the binary shows a mode that
  suppresses all normal target and bumper scoring and sound, which matches the manual's
  gem/gold substitution, but no handler in scope ever *writes* mode 4. The name comes from
  the manual, not the code.
- The manual is also the only source for *meaning*. The binary gives `0x427100` and string
  3010; the manual gives "the fire lizard sinks start Rescue Maiden". Component identity —
  which physical thing on the playfield `a_kout3` or `a_blocf` is — is a manual/`.DAT`
  question that the rules code cannot answer.

The honest summary: the binaries settle **numbers, order and control flow**; the manual
settles **names, intent, and anything stored in the `.DAT` rather than the `.EXE`**.
Neither source alone would have produced a reimplementation.

---

## 5. Mistakes and corrections

Several confident conclusions were overturned. These are the useful part of the record,
and each is traceable through `git log` and the analysis documents.

### 5.1 The message-code offset is piecewise, not `+11`

Full Tilt's `MessageCode` numbering is not upstream's: `BumperControl` compares against
`0x4a` where upstream's enum says `ControlCollision = 63`. `analysis/message_codes.py`
recovers the mapping by pairing, for each named CADET handler, the constants the binary
tests against the `MessageCode` names the upstream function tests. Handlers that test
exactly one code on each side give an unambiguous pair.

The first reading was that the offset is a **constant +11**, with the outliers treated as
noise. That is wrong, and the script's own docstring now carries the retraction:

```
RESULT, verified twice over ~55 pairs: the offset is PIECEWISE, not constant.
Reading the dominant +11 as the whole story is wrong -- the small-delta outliers
this script reports are real signal, not noise.

    TLight codes                          +0
    TLightGroup / TSoloTarget / TBlocker  +4
    gate / kickout / sink                 +9
    Control* block                        +10
    ControlCollision                      +11   (0x4a; upstream 63)
    public codes above 1012               +1..+2
```

Commits `adffac0` ("recover Full Tilt message code numbering from CADET") then `d2fbbd4`
("...and **correct** the message-code mapping"), 21 minutes apart. Full Tilt also has codes
`0x39`–`0x3d` with no upstream counterpart, so a port cannot reuse upstream's enum as-is.

A constant `+11` would have mistranslated every light and light-group message in both
tables — i.e. most of what the rules actually do. The thing that caught it was the
script's design decision to **report disagreements rather than silently resolve them**.

### 5.2 `score_multipliers` does not exist in these builds

`analysis/dragon-scoring-handlers.md` §9 row 6 concluded, at **high** confidence, that
Full Tilt's multiplier array is `{1,1,2,3,4,5}` rather than Space Cadet's `{1,2,3,5,10}` —
reasoned from the manual's 2×/3×/4×/5× weapons plus the raw indices the binary writes. The
row flags its own weakness in the evidence column: *"the array itself was not located in
`.data`"*.

It was not located because it is not there. Commit `72fd761` ("multiplier is a value, not
an index") records the correction: `AddScore` in these binaries is
`field * score + ScoreAdded` with no indexed load, and the callers write 1/2/3/5/10
directly. Upstream's indexed `score_multipliers[5]` with `ScoreMultiplier` holding 0..4 is
a 3D Pinball-ism. The neutral value in Full Tilt is **1**, not 0.

A "high confidence" conclusion inferred from a *consistent* set of observations was still
wrong, because the inference chain never touched the object it was describing. The negative
result — "not found in `.data`" — was the real signal.

### 5.3 The generator pointed at its own stubs

`gen_control.py` emits the component table mechanically, naming each handler after the
component it serves (`a_bump1Control`). The hand-written reimplementations are named for
what they *do* (`ForestBumperControl`). For a while the generated table pointed at the
generated stubs.

That would have **compiled cleanly, linked cleanly, loaded both tables, and scored
nothing** — the stub shadows the real rule and every handler is a no-op. It is the exact
class of bug no amount of building catches. Commit `277607b` ("point the component tables
at the real handlers, not the generated stubs") makes the table consult
`link_impls.implementations()`, keyed on the address comment every reimplementation
carries, and `gen_control.py` now reports `done/total` wired rather than the meaningless
"has a Space Cadet reference" count it printed before.

The same commit fixed a second-order version of the same bug. Several handlers are bound
twice in the binary with byte-identical bodies, and the address-comment regex matched only
the first address on a line, so the duplicate kept pointing at a stub. The fix is one
character of intent — `findall` instead of `search`:

```python
# `// 0x00424bc0 ...` as a doc block's first line, and `FUN_0042a220` anywhere in the
# block. The second form is how a reimplementation says "this function also serves that
# address" -- several handlers are bound twice in the binary because the bodies are
# byte-identical, and without it the table would leave the duplicate pointing at a stub.
```

`gen_glue.py` records a third variant of the same lesson: matching only `extern T* x;` and
not `extern T*& x;` made the generator **non-idempotent** — a second run emitted an empty
file and broke the link.

### 5.4 Skulduggery's state table, reconstructed then confirmed

`analysis/pirates-rules.md` §0 is a self-correction in the text. An earlier revision
reconstructed the mode state table *without* the dispatcher, from the mode-register
writers, the per-component state gates and two static tables in the `.EXE`. The dispatcher
(`FUN_0042d850`, in `decomp/pirates_rest/`) was then read:

> The switch confirms that reconstruction exactly for the eight modes (§1.3) and corrects
> three of the four states that were only marked **[P]**.

A 100% hit rate on the `[C]` claims and a 25% hit rate on the `[P]` claims is roughly what
the confidence legend promises, which is the most useful thing a confidence marker can do.

### 5.5 The smiley-face ball

Both bonus tables tripped upstream's `assertm(Bitmaps[bmp->Resolution] == nullptr,
"GroupData: bitmap override")`. A duplicate bitmap at the same resolution reads, on first
contact, as a parsing bug — a mis-sized record, a `.DAT` the loader is walking wrongly —
and the surrounding commit history shows the ball group's visual data being handled as
*missing* data first (`d7ab03a` guards `TBall` against absent attributes 408/500/501 and a
null `ListBitmap`). The data is not missing or malformed; there is simply more of it than
upstream's model allows.

What settled it was a census across all three `.DAT` files rather than an inspection of one
case. From the patch's own comment:

```cpp
// A group holds at most one bitmap per resolution -- except on the bonus tables,
// where every ball state carries a second one at the same resolution: an alternate
// ball skin (Dragon's Keep a yellow smiley, Skulduggery a brass ball) that nothing
// in the engine selects. The 28/25 groups that do this are all ball0/ball1/ball2
// states; no other group in CADET.DAT, DRAGON.DAT or PIRATES.DAT has a duplicate.
// The first bitmap is the default skin, so keep it and ignore the alternates.
```

28 groups in Dragon, 25 in Skulduggery, all of them ball states, zero elsewhere in any of
the three files. That distribution is not what a parsing bug looks like; it is what a
shipped feature with no selector looks like. The resolution — keep the first, ignore the
alternates — is a deliberate choice to drop a feature rather than guess at a selector that
no rule in either table ever sends.

*Caveat on this entry: the repo records the conclusion and the census that supports it, but
not the wording of the earlier hypothesis. The reading above is inferred from the commit
sequence; treat §5.5 as the weakest-sourced item in this document.*

### 5.6 Smaller ones, for calibration

- `b7c87b2`…`9826ffb`: the rules first named a `TPinballTable::BallsInPlay` field that
  upstream does not have. It does not need one — `+0x192` is `MultiballCount`, which is how
  Space Cadet's own multiball already counts. The rules were rewritten against the engine
  rather than the engine extended.
- `f66c181` "settle sink polarity, tower-3 relaunch and kickout field reuse **against
  listings**" and `695982c` "close Skulduggery's open items **from the raw listings**" are
  both §3.2 in action: conclusions written from pseudocode, then checked against objdump.
- `ff1e73d` "drop the duplicated Rescue Maiden call" — two slices written independently
  both implemented the same award.
- `2104da9` "resolve Dragon's drain reset code **from Skulduggery's equivalent**" — the two
  unknown binaries are oracles for each other, which is a resource that only exists once
  both are partly understood.

### 5.7 What the review pass overturned

With every handler written and both tables running, a review compared each handler, mode
controller and helper against its listing, swept `simple_components[]`, censused every
function in both rules regions, and reviewed the engine patch. The fixes are in
`docs/engine-changes.md` sections 16-30 and in `src/`. Four lessons are worth more than the
list.

**The coverage figure measured the wrong set.** `coverage.py` said 64/64 and 69/69 from the
seed files, i.e. from `score_components[]`. Four functions are bound in neither array and
reached only from engine code: each table's table-level control handler (`DRAGON.EXE`
0x424260, `PIRATES.EXE` 0x4230c0) and its key handler (0x4282c0, 0x42a830). The engine ran
Space Cadet's in their place, so the first tilt on either table dereferenced Space Cadet's
unbound `lite77`, and the Cheats menu reached several other unbound Space Cadet tags.
With them went every per-game reset -- Dragon's super jackpot started at 0, Skulduggery's
buried-treasure pouch and brig carried into the next game -- and the flipper skip of the
bonus count. The census that found them takes every function between the first and last
address with no CADET counterpart and gap-checks the bytes in between; it is now the
second measure in `coverage.py`. A citation count would still not have caught them on its
own, because 0x424260 and 0x42a830 were already *mentioned* in comments.

**"Cannot be found in the binary" was three times a claim about where nobody had looked.**

- *Dragon's music map.* Two files said which TABCn file each quest plays "is nowhere in
  .text -- the globals hold loader handles, so it needs the track table out of DRAGON.DAT,
  or a running table". True of every reader of the handles. But something writes them,
  and it was the unported table-control handler: its 0x3f7 arm loads them **by name**
  (`push 0x43bc00 ("tabc7"); call 0x40e9d0; mov ds:0x439af8,eax`, and so on). Four of
  eight roles were wrong; game over asked for a track that does not exist and was silent.
- *`TDiverter`'s 604.* A note in the engine read "an earlier note here guessed attribute 604
  and a segment list. It is 600, a single line". The earlier note was right. 0x41bb70 tests
  whether 600 is a single line and, when it is not, jumps to 0x41bbeb and walks 604's
  `{2.0, x0, y0, x1, y1, z}` records. Every diverter in `DRAGON.DAT` carries only 604, so
  all eight had no walls. The correction had found the first branch and stopped.
- *`TableG+0x13e`.* Skulduggery's game-over flash and drain-bonus blackout were left as
  `GAP` comments because "0x13e is not a `TPinballTable` field this corpus names anywhere
  else" -- and the comment itself said reading the table's field layout out of CADET.EXE
  would settle it. The constructor does: `0x41a3dc call 0x40aca0` (`new TLightGroup(this,
  0)`), `0x41a3e1 mov [esi+0x13e],eax`. It is `TPinballTable::LightGroup`, and Dragon's
  slice had already used it.

The same review settled two `UNVERIFIED` markers that said the binary had nothing more to
say and only play would decide: the brig counter (`ResetBrigLights` zeroes it at 0x425dd4,
so a fourth Tavern hit is a first hit) and Skulduggery's shared wheel cursor (every mode's
0x4f calls `StartOfEveryMode` first, which zeroes it at 0x42aada). In each case the answer
was in a callee or a sibling, not in the function under study. "The rules corpus does not
contain it" is a statement about the corpus; the engine corpus, the other table and the
callees are part of the binary too.

**A raw Full Tilt code where the port's enum value was needed.** The port numbers messages
its own way (§5.1), so a message sent with `static_cast<MessageCode>(0x..)` is right only
if the receiver's class happens to use the raw number. Four did not:

- Skulduggery's `DrainReset` was the raw 0x402 (1026). The comment above it explained that
  FT 0x402 is the port's `Reset` (1024); the code sent 1026, `TDrain` ignored it, its timer
  survived, and every Shoot Again served a second ball.
- `TimerSet` was the raw 0x45 (69) in two Skulduggery files, where the port's
  `TTimerResetTimer` is 59. The mode-timer re-arms -- the captive ball's +10 s, the drain
  bonus's 30 s backstop, the `Forever` cancel -- were ignored.
- Three light-group codes (0x29, 0x2a, 0x2d) were sent raw. In the port's numbering they
  are `TLightGroupResetAndTurnOn`, `ResetAndTurnOff` and `ToggleSplitIndex`, so "how many
  are on?" switched the group on and returned garbage.

Each one compiles, links and runs, because an unknown code falls into the receiving
class's `default:` and does nothing. The earlier light-override bug (`docs/integration.md`,
"Inert light overrides") was the same class. A cast of a bare number to `MessageCode` is
now worth treating as a defect until the receiver's class is checked. A fifth case had the
right code and the wrong route: Skulduggery's 0x4d "you are no longer the last scorer" went
to the component's own `Message`, where the originals push it through the dispatcher
(`push 0x4d; call 0x423080`), so the four lane handlers that break combos on it never saw
it.

**A fix applied to one table is a question for the other.** Dragon's glyph tokens were
rewired to the symbol font (`docs/engine-changes.md` section 10); Skulduggery's
`DisplayGlyph` wrapper and four of its lane handlers still called the text `Display`, so the
player saw "9" and "KKKK" for the artwork. Dragon had pinned `TableG+0x13e`; Skulduggery's
`LightGroup` sends stayed out. The two slices were written in parallel, and a correction to one
was not propagated to its twin.

---

## 6. What is still unknown

Stated plainly, because the failure mode of a project like this is a confident port that is
quietly wrong.

**Nobody has played either table in a real window and compared it with the original.**
That is the largest open item and it is not close. `test/run_tables.sh` green means all
three tables construct, bind and enter the main loop; a 40 s `FT_AUTOPLAY` run on each
bonus table finishing without a crash means a game started and a ball played blind without
a null dereference. Neither renders anything, reads a score or completes a quest. Read them
as "still boots, still does not crash", not "still right" (§3.5). §5.7 is the evidence that
a whole class of bug -- crashes that need a particular action in play, rules that do
nothing because a code went to the wrong place -- survives both.

**4 `UNVERIFIED` markers in 3 files in `src/`** (`grep -rn UNVERIFIED src/`), down from 79
in 13 files at the first writing of this section. They are not a backlog of unwritten code;
each is a written implementation with its remaining uncertainty stated at the site, and
each says what would settle it:

- *Which picture each Skulduggery glyph token draws* (`src/pirates_part1.cpp`,
  `src/pirates_part2.cpp`, two markers). The token bytes are confirmed against `.data` and
  now draw through the `font2` symbol font (`docs/engine-changes.md` section 10), but
  nobody has rendered font2's slots for '3'..'9' to see the artwork. The rules do not
  depend on the pictures.
- *Whether the 1996 source meant to feed a jackpot helper's dwell into a notify timer*
  (`src/pirates_part2.cpp`). The behaviour is exact and a second site does the same; only
  intent is open, and object code cannot show intent.
- *Which of Skulduggery's two left-side lanes the manual calls the outlane*
  (`src/pirates_part3.cpp`). A `.DAT` geometry question; nothing in the rules branches on it.

Items this section used to list that are now settled: the 0x4d notify is real and goes
through the dispatcher, where Skulduggery's lane handlers break their combos on it (§5.7);
`TPinballTable+0x18e` is `ExtraBalls`, pinned through CADET.EXE's `table_add_extra_ball`;
`TBlocker`'s `+0x7e` is the moving part's integer position index, and the float reading
belongs to `TSink`, a different class at the same offset (`docs/engine-changes.md`
section 5); diverter polarity follows from the recovered `TDiverter`; the glyph tokens draw
in the symbol font on both tables.

**Success versus timeout is settled**, not open: `analysis/quest-outcome.md`. A
discriminator exists (`cmp esi,0x4a` at Dragon's 0x429a18), but it only chooses which mode
the teardown returns to; the quest is booked either way, and the reconstruction already did
exactly that. Four of Dragon's five quests have no objective to meet at all, and
Skulduggery counts and pays even its explicitly lost mode, so this reads as the design.

**All seven new object classes are real** (§2.4); none is a stand-in any more.

**Known gaps in the port**, each stated where it lives:

- The alternate ball skin cheat (Dragon's `PB`, Skulduggery's `GMAX`) toggles
  `pb::AlternateBallSkin` and nothing reads it, because `GroupData` discards each ball
  state's second bitmap (§5.5) and `TBall` does not consult the flag.
- The quote scroll is queued as one pass; the original loops until a key stops it.
- `TDrain`'s tilt arm (0x418792, which kills its timer) and `TPinballTable`'s own
  `PlayerChanged` arm (0x419abb) are not ported.
- `TDiverter`'s back-face sprite kick (0x41bc50) is not reproduced.
- The Full Tilt engine's announcement dispatcher is stood in for by the engine's identical
  Space Cadet strings, minus its Music/Sound On/Off pause texts.

**Component identity is partly abstract.** Which outlane `a_kick2` guards, which tower each
lock belongs to, whether `a_kout3` or `a_blocf` is the thing the manual calls the magnet --
these are `.DAT` geometry questions, answerable by locating the components in the DATs, and
not yet answered. `docs/integration.md` keeps the list.

**Some questions are outside the corpus by construction.** `analysis/pirates-rules.md`'s
open item 3 notes that one open item lives inside a component class -- *"that is engine code and is not
in this corpus at all, so no amount of reading the table rules will settle it"*. Knowing
which questions the available evidence *cannot* answer is part of the result -- with the
caution from §5.7 that "not in this corpus" has more than once meant "in a corpus nobody
opened".

**`simple_components[]` has been swept.** Both binding arrays in `src/control_dragon.cpp`
and `src/control_pirates.cpp` list exactly the binaries' tags (150 and 192), in the same
order, with nothing missing or extra, and every name was cross-checked against the
original rules' uses and the port's. The one gap was Dragon's v_lite58..62, which the
original writes through the fire-sweep table at 0x43bb78 and the port had left null --
a crash in Slay Dragon, now fixed.

---

## 7. Reproducing it

Nothing here is applied automatically, and nothing but the hand-written rules is tracked
that could instead be regenerated.

```sh
# the three measurements of §1
python3 analysis/compare.py        binaries/win95/CADET.EXE binaries/win95/DRAGON.EXE
python3 analysis/dump_components.py binaries/win95/DRAGON.EXE
python3 analysis/dump_simple.py     binaries/win95/DRAGON.EXE
python3 analysis/dump_tags.py       binaries/win95/DRAGON.EXE

# the oracle check of §2.2 -- the binary's order against control.cpp's
diff <(python3 analysis/dump_components.py binaries/win95/CADET.EXE) analysis/cadet-order.txt

# seeds, then a seeded headless decompile, then the independent listings
python3 analysis/make_seeds.py binaries/win95/CADET.EXE  analysis/cadet-seeds.tsv \
        reference/SpaceCadetPinball/control.cpp
python3 analysis/make_seeds.py binaries/win95/DRAGON.EXE analysis/dragon-seeds.tsv
REA_GHIDRA_SEED_FILE=analysis/dragon-seeds.tsv rea decompile-all binaries/win95/DRAGON.EXE
python3 analysis/dump_tags.py binaries/win95/DRAGON.EXE --annotate decomp/dragon
python3 analysis/disasm.py    binaries/win95/DRAGON.EXE analysis/dragon-seeds.tsv decomp/dragon/asm

# skeletons, wiring, state
python3 analysis/gen_glue.py
python3 analysis/gen_control.py binaries/win95/DRAGON.EXE analysis/dragon-matches.txt src/control_dragon.cpp
python3 analysis/link_impls.py
python3 analysis/coverage.py

# build and run
nix-build build.nix
test/run_tables.sh
```

`reference/` is a shallow clone of upstream
(`git clone --depth 1 https://github.com/k4zmu2a/SpaceCadetPinball reference`) with
`engine/fulltilt-engine.patch` applied. The binaries and `.DAT` files are still under
copyright and stay local; `.gitignore` keeps them out of any published history.

---

## 8. If you are auditing this

The four things most worth attacking, in order:

1. **The oracle chain.** Everything downstream rests on `score_components[88]` lining up
   with `control.cpp`'s declaration order. §2.2 is one `diff` away from being falsified.
   The known divergence at entries 48/49 (§2.3) is the template for finding more.
2. **Pseudocode-only conclusions.** §3.2 establishes that argument values from the
   decompiler are unreliable in this corpus. Any constant in `src/` that is *not* backed by
   a listing is a candidate. `decomp/*/asm/` has a listing per handler; the analysis
   documents name which conclusions were checked against one.
3. **`[P]` and `[S]` claims in the analysis documents.** §5.4 is the measured base rate:
   when Skulduggery's dispatcher was finally read, every `[C]` claim held and three of four
   `[P]` claims needed correcting.
4. **Anything a running table cannot settle.** §6 is the list -- and, the other way
   round, everything that only play would catch, since no one has played these tables in
   a real window yet.

The strongest evidence that the method works is not that both tables reach 100% handler
coverage — a generator can produce that, and §5.7 shows the figure reading 100% while two
functions per table were missing. It is that three unrelated measurements agreed on
where the rules live, that the component table reproduced a published decompilation's
declaration order entry for entry before being trusted on an unknown binary, and that the
tag counts reconcile exactly against the two binding arrays in all three images. The
strongest evidence against overclaiming is §5 and §6.
