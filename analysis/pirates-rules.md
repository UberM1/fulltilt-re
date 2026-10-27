# Skulduggery — mode / collectible system (from `PIRATES.EXE`)

Derived purely from `decomp/pirates/functions/*.c` (Ghidra output, 178 functions) plus
`decomp/pirates_rest/functions/*.c` (186 more, 0x42d000–0x435000 — **this is where the
dispatcher lives**), every `DAT_` pre-annotated with its component tag name; the objdump
listings in `decomp/pirates/asm/*.txt` and `decomp/pirates_rest/asm/*.txt`, plus listings
regenerated for the 14 mid-range helpers with `analysis/disasm.py`; static tables read out of
`binaries/win95/PIRATES.EXE`; and the conventions established in
`analysis/dragon-quest-system.md` / `analysis/dragon-scoring-handlers.md`. No Ghidra/rea
runs. Cross-checked against the Skulduggery section of `docs/dragon-rules-spec.md`
(the official manual); **the binary is authoritative**.

Confidence legend: **[C] CONFIRMED** (read directly and cross-checked by ≥2 independent
uses, or pinned by a static table in the EXE) · **[P] PROBABLE** (one clean reading) ·
**[S] SPECULATIVE** · **[U] UNRESOLVED**.

Same engine, same build, same message-code numbering as Dragon's Keep: `TLight` codes
unshifted, `TLightGroup`/`TSoloTarget`/`TPopupTarget`/`TBlocker` **+4**,
gate/kickout/sink **+9**, `Control*` **+10**, `ControlCollision` = `0x4a` (**+11**).
Component field offsets `+5` (`ActiveFlag`), `+6` (`MessageField`), `+0x42/+0x46/+0x4e`
(`LightOnFlag`/`FlasherOnFlag`/`ToggledOnFlag`) are used exactly as in Dragon, and are now
read out of the engine classes rather than inferred from use — §2.5.1. The piecewise table
above also needs one caveat it did not have: several private codes sit **below** the +4
band because Full Tilt *added* them, not because they shifted. `0x39`–`0x3c` on a
`TBlocker` and `0x45`/`0x48` on a `TTimer` are new codes with no upstream counterpart, so
arithmetic against upstream's enum says nothing about them. **[C]**

---

## 0. The dispatcher, and what reading it changed

`FUN_0042d850` — Skulduggery's `control::handler` — **has now been read**
(`decomp/pirates_rest/functions/0x42d850.c`, with the state switch factored out into
`FUN_0042d900`). An earlier revision of this document reconstructed the state table
without it, from the mode-register writers, the per-component state gates and two static
tables in the EXE. **The switch confirms that reconstruction exactly for the eight modes**
(§1.3) and corrects three of the four states that were only marked **[P]**. Everything
below is now anchored on the primary source.

```c
// FUN_0042d850 — control::handler
void control::handler(int code, TPinballComponent* caller)
{
    for (;;) {
        if (!info_text_box) return;

        if (caller) {                                   // ---- global jackpot accrual
            switch (caller->GetType()) {
            case 0x3ee: TargetsDroppedThisBall++;       // 1006 TPopupTarget
                        /* fallthrough */
            case 0x3fb: AddToJackpot(20000); break;     // 1019 TSoloTarget
            case 0x3f9: AddToJackpot(40000); break;     // 1017 TSink
            }
        }
        if (code == MessageCode::ControlTimerExpired     // 0x46 from the text box
            && caller == info_text_box
            && info_text_box->field_5e == 0)
            code = 0x50;                                 // -> ControlMissionStarted

        MissionControl(info_text_box->MessageField, code, caller);   // FUN_0042d900

        if (code != 0x52) return;
        caller = nullptr; code = 0x4f;                   // teardown -> idle, re-dispatch
        info_text_box->MessageField = 2;
    }
}
```

Three notes on the shape, all **[C]**:

1. The `0x46`-from-the-text-box → `0x50` remap is verbatim Dragon's
   (`dragon-quest-system.md` §1 step 3).
2. **A `0x52` teardown always lands back in state 2**, in the dispatcher rather than in
   each controller. That is why no Skulduggery controller sets state 2 itself, and why
   there is no Dragon-style "mode just ended" grace state.
3. `FUN_0042d900`'s `default:` arm is `FUN_0042d830` = *"set state 2, broadcast `0x4f`"* —
   the exact analogue of Dragon's `FUN_0042c000` / upstream's
   `UnselectMissionController`. **[C]**

### 0.1 Vtable slot 0 is a type getter, not the destructor **[C]**

`FUN_0042d850` opens with `iVar1 = (**(code **)*param_2)();` and then switches the result
against `0x3ee` / `0x3fb` / `0x3f9` — the component **type ids** from `PIRATES.DAT`
(1006 / 1019 / 1017). The same call appears at the head of every mode controller's `0x46`
and `0x4a` arms, always compared against type ids (`0x408` `TTimer`, `0x40e`
`light_sweeps`, `0x3ea` `TLight`, `0x3f7` `TLightRollover`, `0x3fc`). So **vtable `+0x00`
is `GetType()`**, and `analysis/symbols-cadet.md`'s current claim that slot 0 is the
destructor is wrong. (Confirmed in the asm too: `0x42a470` and every other handler reach
it as `mov eax,[ecx]; call [eax+0x00]` with no `this`-deleting epilogue downstream.)
I have **not** edited `symbols-cadet.md` — that is a shared reference and the correction
belongs to whoever owns it.

### 0.2 The other two formerly-absent callees **[C]**

* `FUN_0042dd0b` = the **MSVC `rand()` LCG**:
  `seed = seed*0x343fd + 0x269ec3; return (seed & 0x7fff0000) >> 16;`
  — which is exactly why every random pick in this corpus has the shape
  `(rand()*N) >> 15`.
* `FUN_0042dfaf` = **`sprintf(buf, fmt, ...)`** (its body is the CRT `vsprintf` core,
  `FUN_0042f08c`, with the varargs taken from `stack0x0000000c`). **This matters:** the
  formatted values are varargs that Ghidra does not model, so every
  `sprintf(buf, get_rc_string(id))` call site in this corpus has one or more
  **unrecovered numeric operands**. It resolves what string 3003 (`0xbbb`) is — a
  `%ld`-bearing format line, not a name — but the operand itself stays **[U]** at every
  call site unless read from the listing.

### 0.3 Asm cross-check, and the recovered `sprintf` operands

Listings now exist for both ranges: `decomp/pirates/asm/*.txt` (the 69 named handlers) and
`decomp/pirates_rest/asm/*.txt` (186 functions, 0x42d000–0x435000). For the 14 *helpers* in
the 0x423000–0x42d000 range that are neither (`FUN_00423720`, `FUN_00425000`,
`FUN_00425230`, `FUN_00425ab0`, and the eight mode controllers) I regenerated listings with
`python3 analysis/disasm.py binaries/win95/PIRATES.EXE decomp/pirates <out>`; the output is
**byte-identical** to the supplied `pirates_rest` listings on the function they overlap
(`0x42d590`), so it is the same pipeline and can be trusted the same way.

Spot-check first: `Pirates_s_magnet1_Control`, the noisiest Ghidra body in this document and
the basis of §5.4, read against `asm/0x42a470.txt`. Every claim survives, with one
refinement — the keg light is extinguished as `v_lite560->Message(0x14, 2.0f)` (a 2-second
fade, not an immediate off) — and the award is `push 0x186a0; push caller; call 0x423310`
= **`AddScoreAt(caller, 100000)`**, confirmed from the instruction stream. **[C]**

#### The call shape **[C]**

Every formatted display in the corpus compiles to one fixed pattern, and it is what makes
the operands recoverable:

```
push <VALUE>            ; the sprintf vararg  <-- Ghidra drops this
push 0x0                ; get_rc_string's 2nd argument
push <string id>
call 0x409730            ; get_rc_string(id, 0)  -- __stdcall, pops 8
lea  ecx,[esp+N]         ; the output buffer
push eax                 ; the format string
push ecx                 ; the buffer
call 0x42dfaf            ; sprintf(buf, fmt, VALUE)
```

Two corrections fall out of this on their own: **`FUN_00409730` takes two arguments**
(`id`, `0`), not the one Ghidra models; and the vararg is the **single dword pushed
immediately before `push 0x0; push <id>`** — which lines up exactly with
`FUN_0042dfaf`'s own `&stack0x0000000c`. All 50 call sites follow it without exception.

#### What the operands are **[C]**

| Operand form | Sites | Meaning |
|---|---|---|
| `eax` straight after `call 0x4232c0` / `0x423310` / `0x423350` | 23 | **the score just added** — the three scoring wrappers all end `call 0x419f10` + `ret` with no `mov eax`, so they return `TPinballTable::AddScore`'s value |
| `eax` after `mode_timer->Message(0x48)` then `cdq; mov ecx,0x3e8; idiv ecx` | 8 | **whole seconds remaining** (the timer answers in ms) |
| `ebx`/`eax` after a `lea`/`shl` multiply chain | 6 | a drain-bonus term (§6.1) |
| `caller->get_scoring(level)` | 2 | the new bumper value (§6) |

That "the wrappers return the score added" is **[P]** rather than **[C]** for the *reason*
— `FUN_00419f10` is engine code outside this corpus — but **[C]** for the mechanism: the
value in `eax` at the push is whatever `AddScore` returned, and upstream's
`TPinballTable::AddScore` returns the score it added.

So **string 3003 (`0xbbb`) is the generic "score awarded" line**, used by the magnets, the
ship sink, the ship wire ramp, the coin award, the Bermuda progressive and the Peg Leg's
Loot total alike, with the awarded score as its `%ld`. **[C]** Open item 2 of the previous
revision is closed; the per-site numbers it affected are listed where they belong (§1.4,
§6, §6.1).

---

## 1. The mode register

### 1.1 Where it lives

**`info_text_box->MessageField`** = `*(int*)(DAT_0043ad3c + 6)`. **[C]**

Exactly the same trick as Dragon's Keep: Space Cadet parks the mission id in the dummy
light `lite198`, both Full Tilt tables park it in their single `TTextBox`'s
`MessageField`. 41 accesses across the rules code; every write is a mode transition and
every read is a mode test. No other candidate comes close (`v_lite606` is second with 20,
and it is a counter, §4).

### 1.2 The twelve transition functions

Skulduggery factors the writes into one-line setters — which is what made the state space
enumerable before the dispatcher was available. Each does *"set the wheel light, set the register,
broadcast `0x4f`"*: **[C]**

| Function | Sets state to | Also |
|---|---|---|
| `FUN_00423440` | 3 | `FUN_00423420` = mode-in-progress light `v_lite606` **off** |
| `FUN_00423460` | 2 | same |
| `FUN_00423480` | 4 | `FUN_00423400` = `v_lite606` flash forever; glyph `DAT_0043ab08` |
| `FUN_004234c0` | 5 | ″ ; glyph `DAT_0043ab10` |
| `FUN_00423500` | 6 | ″ |
| `FUN_00423520` | 7 | ″ ; glyph `DAT_0043aaa0` |
| `FUN_00423560` | 8 | ″ ; glyph `DAT_0043ab0c` |
| `FUN_004235a0` | 9 | ″ |
| `FUN_004235c0` | 10 | ″ ; also `a_spot8->MessageField = 0` (volcano bank reset) |
| `FUN_004235f0` | 0xb | ″ |
| `FUN_00423610` | 0xc | ″ |
| `FUN_00423630` | 0xd | ″ |

The remaining three states are written inline: `0x10` and `0x11` by
`Pirates_drain_Control` / `FUN_004230c0`, `1` by the bonus controller, `0` by
`FUN_004230c0`'s new-game branch.

### 1.3 Full state table

| State | Meaning | Controller | Confidence |
|---|---|---|---|
| 0 | **Inert** — the switch arm is a bare `return`. Set by `FUN_004230c0`'s new-game branch | — (no-op) | [C] |
| 1 | **Game over** (`FUN_0042bfd0`: table `0x400`, music `tabb5`, string **3027**) | `FUN_0042bfd0` | [C] |
| 2 | **Idle, no mode running** (`0x50` → string **3076** "Spell Skulduggery to Start Mode") | `FUN_0042ab90` | [C] |
| 3 | **Skill shot** (rolling award list, `s_launcher1->MessageField` 0…8) | `FUN_0042abf0` | [C] |
| 4 | Mode **Attack Ship** | `FUN_0042ad70` | [C] |
| 5 | Mode **Bermuda Triangle** | `FUN_0042c070` | [C] |
| 6 | Mode **Storm** | `FUN_0042c430` | [C] |
| 7 | Mode **Tavern** | `FUN_0042b420` | [C] |
| 8 | Mode **Treasure Hunt** | `FUN_0042b090` | [C] |
| 9 | Mode **Mutiny** | `FUN_0042ca00` | [C] |
| 10 | Mode **Sword Fight** | `FUN_0042cc10` | [C] |
| 0xb | Mode **Peg Leg's Loot** (finale) | `FUN_0042cfc0` | [C] |
| 0xc | **Lava-Ball** | `FUN_0042d480` | [C] |
| 0xd | **Jail Break!** | `FUN_0042d590` | [C] |
| 0x10 | **Drain bonus** countdown | `FUN_0042b820` | [C] |
| 0x11 | **End-of-game table lockdown** (transient, not the bonus) | `FUN_0042d730` | [C] |
| anything else | force state 2 + `0x4f` | `FUN_0042d830` | [C] |

Note the **non-monotonic** mode order: the states are *not* in manual order. That is not
an inference — it is a table in the binary.

### 1.3.1 The switch, read directly

`FUN_0042d900(state, code, caller)` is a flat `switch (state)` whose eight mode arms are:

| Arm | Controller | Mode |
|---|---|---|
| 4 | `FUN_0042ad70` | Attack Ship |
| 5 | `FUN_0042c070` | Bermuda Triangle |
| 6 | `FUN_0042c430` | Storm |
| 7 | `FUN_0042b420` | Tavern |
| 8 | `FUN_0042b090` | Treasure Hunt |
| 9 | `FUN_0042ca00` | Mutiny |
| 10 | `FUN_0042cc10` | Sword Fight |
| 0xb | `FUN_0042cfc0` | Peg Leg's Loot |

**This is identical, controller for controller, to the permutation reconstructed from
`DAT_0043d0f0 = {4,8,7,5,6,9,10,11}`.** None of the six independent lines of evidence
below is contradicted by the primary source; the mode half of the table is therefore
confirmed twice over, once by the dispatch table and once by the selection table. **[C]**

The dispatcher did, however, correct the four non-mode states that had only been inferred:
**state 0 is the inert arm and state 1 is the game-over controller** (I had those two the
wrong way round), **state 0x11 has its own controller** `FUN_0042d730` rather than sharing
the bonus controller, and **states 0xc / 0xd do have controllers** — they simply live above
0x42d000. All four were marked **[P]** / **[U]**; none was marked **[C]**.

**Six independent agreements pin the mode assignment [C]:**

1. `DAT_0043d0f0` (static array, read out of the EXE) `= {4, 8, 7, 5, 6, 9, 10, 11}`.
   `FUN_004256d0(i)` indexes it with the *selection index* `i` (0…7) and sets
   `mode_timer->MessageField = i + 1`.
2. `FUN_00425a50` ("start the selected mode") switches on `mode_timer->MessageField`
   1…8 → the setters for states 4, 8, 7, 5, 6, 9, 10, 0xb — the same permutation.
3. `FUN_004255a0(state, on)` maps **state** → wheel light: 4→`v_lite552`, 8→`v_lite553`,
   7→`v_lite554`, 5→`v_lite555`, 6→`v_lite556`, 9→`v_lite557`, 10→`v_lite558`,
   0xb→`v_lite559`. The light order `v_lite552…559` is therefore the *selection* order,
   i.e. the manual's string order 3068…3075 = Attack / Treasure Hunt / Tavern / Bermuda /
   Storm / Mutiny / Swordfight / Peg Leg's Loot.
4. `Pirates_a_spot102_Control` (the captive ball) prints `3068 + FUN_00425850()` — the
   selection index — so index 0…7 ↔ those eight names.
5. The skill shot (`FUN_0042abf0`) hard-codes five of them: its slots 3/4/5/6/7 call the
   setters for states 5/4/8/7/6, and the manual's skill-shot list in the same order is
   BERMUDA / ATTACK / TREASURE / TAVERN / STORM.
6. Behavioural fingerprints, each decisive on its own:
   * state 6 = `FUN_0042c430` counts the lit wind lights and picks a severity from
     strings **3046/3047/3048/3049** = Squall / Tropical Storm / Monsoon / Hurricane →
     **Storm**;
   * state 0xb = `FUN_0042cfc0` pays `coins×500,000 + treasures×500,000 +
     map_pieces×1,000,000 + modes×1,000,000` → **Peg Leg's Loot**;
   * state 4: `a_sink4` (ship sink) drops the 5 ship targets and bumps the fathom lights;
     `s_rexit8` (ship wire ramp) prints **3018** "Ship Captured" → **Attack Ship**;
   * state 7: the three tavern sinks and the tavern drop bank are gated on it, and the
     tavern bank arms a keg magnet → **Tavern**;
   * state 8: the three hideaways test a *flashing* `v_mlite44/106/107` "X" light, and the
     east wind lanes call `FUN_00424b00` ("bury an award") → **Treasure Hunt**;
   * state 5: the east wind lanes pay the progressive table `{125,000; 250,000; 500,000;
     1,000,000; jackpot}` when that lane's wind light is the flashing one → **Bermuda
     Triangle**;
   * state 9: `a_sink4` lights `v_lite570` (Ship's Hold / bonus-hold) and the bumpers pay
     25,000 with a jackpot every 10 hits → **Mutiny**;
   * state 10: the parrot and volcano banks are gated on it and clearing either awards a
     collectible → **Sword Fight**.

### 1.3.2 An eighth confirmation: the mode-name strings **[C]**

Recovering the `0x50` tick operands (§0.3) turned out to settle the state map a seventh and
eighth time, and this is the strongest evidence in the document because it matches the
manual's **own mode-name string ids** one-to-one against controllers. Every mode
controller's `0x50` arm formats "seconds remaining" into its own name string:

| Controller | `0x50` format string | Manual's name for that id |
|---|---|---|
| `FUN_0042ad70` (state 4) | **3016** | Attack Ship |
| `FUN_0042b090` (state 8) | **3053** | Treasure Hunt |
| `FUN_0042b420` (state 7) | **3025** | Tavern |
| `FUN_0042c070` (state 5) | **3045** | Bermuda Triangle |
| `FUN_0042c430` (state 6) | *the buffer `DAT_00435d50`* | Storm — see below |
| `FUN_0042ca00` (state 9) | **3051** | Mutiny |
| `FUN_0042cc10` (state 10) | **3052** | Sword Fight |
| `FUN_0042cfc0` (state 0xb) | **3054** | Peg Leg's Loot |

All eight agree with the assignment. Storm is the elegant case: it has no fixed name, so
`FUN_0042c430`'s `0x4f` **`strcpy`s the chosen severity string (3046–3049) into the buffer
`DAT_00435d50`** and the `0x50` tick then uses that buffer as its own format string —
which is why the severity line carries a `%ld` and why the copy exists at all. **[C]**

### 1.4 Hint strings do *not* follow selection order **[C]**

Each controller's `0x4f` prints its own hint with `time = -1.0` (persistent):

| String | Mode |
|---|---|
| 3060 | Attack Ship |
| 3061 | Treasure Hunt |
| 3062 | Tavern |
| 3063 | Bermuda Triangle |
| 3064 | Mutiny |
| 3065 | Sword Fight |
| 3066 | Peg Leg's Loot |
| 3067 | **Storm** |

So the hint block is in manual order *except* that Storm is moved to the end and
Mutiny/Sword Fight/Peg Leg's Loot shift up one. The spec only said "Hint strings
3060–3067" without a mapping; this resolves it, and it is a trap for anyone assuming the
two string blocks are parallel.

### 1.5 Supporting state cells

Like Dragon, Skulduggery stashes ints in component `MessageField`s rather than in named
globals. **[C]** unless noted.

| Cell | Meaning |
|---|---|
| `info_text_box->MessageField` | **the mode register** |
| `mode_timer->MessageField` | **selected / next-available mode**, 1…8 (`0` = none) |
| `v_lite606->MessageField` | **modes completed this game** (`++` in every `0x52`); the light itself flashes while a mode runs |
| `v_lite607->MessageField` | **map pieces collected** (the light is the Mode Change light) |
| `special_treasure_light_group->MessageField` | **special treasures collected** (0…6) |
| `v_lite1->MessageField` | **SKULDUGGERY letter bitmask**, 11 bits, full = `0x7ff` |
| `v_lite524->MessageField` | **GOLD letter bitmask**, 4 bits, full = `0xf` |
| `a_drop5->MessageField` | ship drop-bank bitmask, 5 bits (`0x1f`) |
| `a_drop13->MessageField` | tavern drop-bank bitmask, 3 bits (`0x7`) |
| `a_drop10->MessageField` | parrot drop-bank bitmask, 3 bits (`0x7`) |
| `a_spot11->MessageField` | volcano bank bitmask, 4 bits (`0xf`) |
| `a_bump1->MessageField` | bumper hit counter 0…11 |
| `a_bump2->MessageField` | **crew level** 0…6 = the bumper `scoring[]` index |
| `a_spot6->MessageField` | brig-target hit counter 0…2 |
| `s_launcher1->MessageField` | skill-shot rolling cursor 0…8 |
| `drain->MessageField` | **held drain bonus** (Ship's Hold) |
| `s_trip3/s_trip8->MessageField` | saved left/right-orbit light counts across Treasure Hunt |
| File statics | `DAT_00435cf8` = per-mode scratch counter · `DAT_00435d04` = shared "X"/wind cursor · `DAT_00435d48` = targets dropped this ball · `DAT_00435d3c` = balls in the brig · `DAT_00435d90` = shoot-again light phase · `DAT_00435d30` = "not in skill shot" · `DAT_00435d34` = skip-the-bonus-countdown flag · `DAT_00435d10/14/18/1c/20/24/28` = the buried-treasure pouch (§5.2) |

---

## 2. Mode lifecycle

### 2.1 Selection — `FUN_00425850` (SelectNextMode) **[C]**

* If `map_piece_light_group`'s on-count equals its member count → return **7**
  (Peg Leg's Loot) unconditionally. ✓ manual "finale, unlocked by all six map pieces".
* Otherwise build a candidate list from the **unlit** wheel lights `v_lite552`…`v_lite558`
  (seven) and pick one **uniformly at random**.
* If all seven are lit, fall back to whichever is currently *flashing* (`+0x46`), scanning
  `v_lite552`…`v_lite559`; failing that, return 7.
* Then `FUN_00425700()` (extinguish the previous selection, clear
  `mode_timer->MessageField`) and `FUN_004256d0(i)` (flash the chosen light forever,
  `mode_timer->MessageField = i + 1`).

Called from: the **captive ball** `a_spot102` while state == 2; `FUN_00423aa0` when a map
piece completes the set; `FUN_004255a0(2, …)`.

So, exactly as in Dragon, **selection is random, not cyclic**, and the "next mode" is
*displayed* but the player cannot choose which. **[C]**

#### 2.1.1 The captive ball — `Pirates_a_spot102_Control` **[C]**

One handler, three behaviours keyed on the mode register, and it is the component the
manual describes most precisely:

* **state 2** → `FUN_00425850()` (re-select), print `3068 + index` ("<Mode> Mode Selected"),
  score `scoring[0]` = **50,000**, flash `v_lite607` (the Mode Change light) for 1.0 s.
  ✓ manual "the next available mode is changed by hitting the captive ball … + 50,000
  outside a mode".
* **state 4…0xb** (any mode running) → query `mode_timer` (`0x48`) and, if it is still
  running, re-arm it at **`remaining/1000 + 10.0` seconds** — i.e. **the captive ball
  extends the running mode by exactly +10.0 s**, with no cap and no limit on how often.
  ✓ manual "hitting it *during* a mode extends the mode timer", and this is the amount the
  spec listed as unknown. Confirmed in the listing — `fild [esp]; fdiv ds:0x438920;
  fadd ds:0x438914` with the two constants reading **1000.0** and **10.0** out of the
  float pool. **[C]**
* **anything else** (0, 1, 2 handled above, 0xc, 0xd, 0x10, 0x11) → just the light and the
  50,000.

### 2.2 Starting **[C]**

Two entry points:

| Trigger | Path |
|---|---|
| **Spell SKULDUGGERY** (all 11 drop targets) | `FUN_00425de0` → clears the letters and all three drop banks → `FUN_00425a50` → the setter for the selected state |
| **Skill shot** slots 3…7 | `FUN_0042abf0` → setter directly (bypasses the selection entirely) |
| cheat code `N`,`M`,`A`,`X` | `FUN_0042a830` → `FUN_00425a50` (§7) |

`FUN_00425de0` is called from the *shared* bank routine of all three drop banks, which is
why any of the eleven targets can be the one that completes the word.

Every controller's `0x4f` branch does the same five things **[C]**: `FUN_0042aad0()`
(common setup — zero the scratch counters, clear GOLD and SKULDUGGERY lights, reset the
volcano bank, raise the `a_bloce` blocker and swap `v_lite504`/`v_lite505`), an
**unmultiplied start bonus**, `light_sweeps` message `0x1b` with a per-mode id, switch to
the mode's music (`tabb2`…`tabb6`) and voice wave, and display its hint string with
`t = -1.0`.

As in Dragon the **timer is not started until the light sweep finishes**: the `0x46`
branch with caller type `0x40e` (1038 `light_sweeps`) arms `mode_timer` and broadcasts
`0x50`.

### 2.3 Start bonus and time limit **[C]**

All eight are now read from the instruction stream (`push <float>; push 0x45` immediately
before the `mode_timer->Message` call) — the four that Ghidra dropped as lost float
arguments are the last four rows: **[C]**

| State | Mode | Start bonus | Time limit (s) | Raw float | Sweep id |
|---|---|---|---|---|---|
| 4 | Attack Ship | 200,000 | **29.38** | `0x41eb0a3d` | 0.0 |
| 5 | Bermuda Triangle | 200,000 | **31.14** | `0x41f91eb8` | 8.0 |
| 6 | Storm | 300,000 | **24.14** | `0x41c11eb8` | 9.0 |
| 7 | Tavern | 200,000 | **27.40** | `0x41db3333` | 7.0 |
| 8 | Treasure Hunt | 300,000 | **30.68** | `0x41f570a4` | `FUN_00424580()` → 1…6 |
| 9 | Mutiny | 100,000 | **48.28** | `0x42411eb8` | 10.0 |
| 10 | Sword Fight | 250,000 | **29.38** | `0x41eb0a3d` | 11.0 |
| 0xb | Peg Leg's Loot | 1,000,000 | **30.68** | `0x41f570a4` | — (none) |
| 0x10 | drain bonus | — | 30.0 | `0x41f00000` | — |

Three observations worth keeping **[C]**: **Mutiny is by far the longest mode at 48.28 s**
and also the cheapest to start (100,000) — consistent with its objective being a raw
bumper-hit count; Attack Ship and Sword Fight **share** 29.38 s and Treasure Hunt and
Peg Leg's Loot **share** 30.68 s, i.e. the designers reused four distinct wave lengths
across eight modes; and Peg Leg's Loot is the only mode with **no light sweep** (the grep
for `0x1b` in `0x42cfc0` is empty), which is why its `0x4f` arms the timer directly instead
of waiting for the sweep to finish.

> **Disagreement with the manual [C].** The manual says "**Starting a mode is worth
> 200,000 points**". The binary pays a **different amount per mode** — 100,000 (Mutiny)
> through 1,000,000 (Peg Leg's Loot) — and only three of the eight actually pay 200,000.

> **Closes a manual gap entirely [C].** "Mode time limits and the captive-ball extension
> amount" was listed as unknown in the spec. The extension is **+10.0 s** (§2.1.1) and
> **all eight** limits are now read from the instruction stream. The odd values
> (29.38, 31.14, 24.14, 27.40, 30.68, 48.28) look like voice-wave lengths, same as Dragon
> — **[S]** for the *reason*, **[C]** for the values. Note that only four distinct values
> are used across the eight modes, which is itself mild support for the wave-length theory.

### 2.4 Ending **[C]**

* **Timer expiry** — `0x46` with caller `== mode_timer` → the controller broadcasts
  `0x52`. There is **no restart window**: Skulduggery has no state-3-equivalent
  ten-second grace period and no `0x51` code anywhere in the rules code. That is a real
  structural difference from Dragon's Keep.
* **`0x52` = teardown**, and it is where "completion" is booked. Every one of the eight
  controllers' `0x52` does: `FUN_0042ab40()` (restore the drop banks and GOLD lights from
  their bitmasks), **`v_lite606->MessageField += 1`**, lower `a_bloce`, restore
  `v_lite504`/`v_lite505`.
* **The dispatcher wraps `0x52` with three more actions** (`FUN_0042d900`'s preamble plus
  `FUN_0042d850`'s loop) — none of which is visible from the controllers, and all of which
  the earlier revision of this document had to leave open: **[C]**

  ```c
  // FUN_0042d900, before the switch
  if (code == 0x4f) {
      SetModeLight(state, /*flashing=*/true);        // FUN_004255a0 -> code 7, -1.0
  } else if (code == 0x52) {
      if (state > 3 && state < 0xc)
          AddToJackpot(750000);                      // <-- mode completion bonus
      SetModeLight(state, /*flashing=*/false);       // code 0x13: solid ON = completed
      SelectNextMode();                              // FUN_00425850
  }
  // ... and FUN_0042d850 then forces state = 2 and re-dispatches 0x4f
  ```

  So **mode completion is worth +750,000 to the jackpot** ✓✓ exactly the manual (§6.0),
  the wheel light goes from *flashing* (selected/running) to *solid* (completed) — which is
  what makes `FUN_00425850` stop offering it — and the next mode is selected immediately.
  The `state > 3 && state < 0xc` guard means the bonus is paid for all eight modes
  including Peg Leg's Loot, and **not** for Lava-Ball or Jail Break.
* **`0x53` = abort** — sent by `Pirates_drain_Control` on drain. Every controller's `0x53`
  cancels `mode_timer` (`0x45, -1.0`) and falls into `0x52`.
* **Objective met** — the mode's own code broadcasts `0x53` (Storm's hideaway bail-out) or
  runs the payout and lets the timer run down.

> **Same caveat as Dragon, and the dispatcher makes it worse, not better [C].**
> `v_lite606->MessageField` ("modes completed") is incremented in the **shared `0x52`**,
> which is reached on timer expiry *and* on drain-abort *and* on success — and the
> dispatcher's own `0x52` arm pays the **+750,000 jackpot bonus** and turns the wheel light
> **solid** on exactly the same path. So a mode counts as "completed" — for the
> 200,000/mode drain bonus, the 1,000,000/mode Peg Leg's Loot payout, the +750,000 jackpot
> *and* the wheel progression — merely by having been started. No success/failure
> discriminator exists anywhere in the dispatcher or in any of the eight controllers.
> **[C]** for the mechanism, **[U]** for whether that is intended.

### 2.5 FT-only message codes in this subsystem

| Code | Meaning | Evidence |
|---|---|---|
| `0x4f` | mode changed / start (`ControlMissionComplete` slot) | every setter |
| `0x50` | tick / mode started (`ControlMissionStarted`) | every controller |
| `0x52` | **teardown** | ″ |
| `0x53` | **abort** (drain); in the two multiballs, "has the ball count fallen back to 1?" | `Pirates_drain_Control`, `FUN_0042d480`, `FUN_0042d590` |
| `0x54` | the ball has **mounted the ramp plane** (its collision mask switched to the ramp's) | `TRamp::Collision`, CADET.EXE `0x004177d0` arm `0x0041791d` |
| `0x55` / `0x56` | the ball crossed the ramp's **wall 1 / wall 2** (attributes 1301 / 1302) | ″, arms `0x004179a5` / `0x004179e4`; `s_rexit8` (ship wire ramp) |
| `0x4b` / `0x4c` | **arm / disarm** a holding device | `s_magnet1`…`6`, `a_kout1`, `a_kout2`, `a_kick1` (`FUN_0042d480`/`0x42d590`/`0x42d730`) |
| `0x4d` | sent to the previous scoring component — **not** a no-op: the lane-ladder handlers answer it by clearing their combo group | `FUN_004232c0`/`00423310`/`00423350` |
| `0x1c` | light-group **rotate** (= `TLightGroupStepBackward`, 24 at +4; the lit pattern walks toward higher indices, i.e. rightward) | flippers (§6) |
| `0x1b` | **start light sweep** *n* on the type-1038 `light_sweeps` | `TLightSweep::Message`, CADET.EXE `0x0040c660` arm `0x0040c69f` |
| `0x2d` | light-group "light the next one" | coin, map-piece groups |
| `0x39` | **set a blocker's position index** to `(int)floor(value)`; `-1` retracts it | `TBlocker::Message`, CADET.EXE `0x00414f80` arm `0x004150ce` |
| `0x3a` | **animate a blocker toward** position `(int)floor(value)` | ″, arm `0x004151fb` |
| `0x45` (to `mode_timer`) | **arm the timer** for `value` seconds; a negative value only cancels | `TTimer::Message`, CADET.EXE `0x0041b380` arm `0x0041b3bc` |
| `0x48` (to `mode_timer`) | **read the remaining time, in milliseconds** (`×1000.0`, floored) | ″, arm `0x0041b3fb` |
| `1` | flipper-button notification | `a_flip1`/`a_flip2`, `FUN_0042b820` |

#### 2.5.1 Four of these were wrong, and the engine classes settled them **[C]**

An earlier revision of this table had `0x39` as "blocker restart timeout", `0x4b`/`0x4c`
as names inferred from behaviour alone, `0x56` as "ramp traversal complete", and `0x4d` as
a no-op. All four are now read directly out of the component classes, which are **in
`CADET.EXE`** even though Space Cadet's data never constructs some of them — the same route
§2.4 of `docs/how-this-was-done.md` used for the seven missing object types. Each class is
found from its vtable, whose slot 0 is a type-getter stub (§0.1):

| type | class | vtable | `Message` |
|---|---|---|---|
| 1011 | `TBlocker` (`a_bloc*`, `a_cannon`) | `0x00435560` | `0x00414f80` |
| 1012 | `TKickout` (`a_kout1/2`) | `0x00435620` | `0x004163a0` |
| 1014 | `TKickback` (`a_kick1/2`) | `0x00435600` | `0x00415d70` |
| 1021 | `TRamp` (`a_ramp*`) | `0x00435680` | none — slot 1 is the generic `0x004135b0`; it only *raises* codes, from `Collision` at `0x004177d0` |
| 1032 | `TTimer` (`mode_timer`) | `0x00435778` | `0x0041b380` |
| 1033 | `TTextBox` (`info_text_box`) | `0x00435858` | `0x0041e110` |
| 1038 | `TLightSweep` (`light_sweeps`) | `0x00435228` | `0x0040c660` |
| 1042 | `TMagnet` (`s_magnet*`) | `0x00435640` | `0x004166c0` |

**`0x39` is a position index, not a duration.** `TBlocker::Message` jump-tables `0x37`–`0x3c`
at `0x00415398` and separately answers `0x45`, `0x3f3`, `0x3fe`, `0x402`. The `0x39` arm
floors the float to an int, stores it at `[this+0x7e]`, clears the old position's collision
byte at `[this + old + 0x76]`, sets the new one and shows sprite `index`; a negative index
retracts the blocker and blanks the sprite. So a Full Tilt `TBlocker` is a multi-position
moving wall. The numbering objection dissolves because `0x39` was never in the
Disable/Enable pair: CADET.EXE's own `DrainBallBlockerControl` (`0x00423480`) sends `0x38`
exactly where upstream's sends `TBlockerEnable` (52, i.e. +4) and `0x45` exactly where
upstream's sends `TBlockerRestartTimeout` (59). The family is therefore `0x37` Disable,
`0x38` Enable, `0x45` RestartTimeout, with `0x39`–`0x3c` four codes Full Tilt added below
them (`0x3b` extends to the last position, `0x3c` retracts, both by sending themselves
`0x39`).

**`0x4b`/`0x4c` mean "arm/disarm", but by three different mechanisms.** `TMagnet`: `0x4b`
sets `ActiveFlag` (`[this+5]`) to 1, `0x4c` clears it and kills the hold timer; the capture
circle is installed with `&ActiveFlag` (`0x00416873`, `lea ecx,[esi+0x5]`) and
`TEdgeManager` skips edges whose `ActiveFlag` is clear. `TKickout`: the same pair, plus
`0x4c` releases a ball already held (`0x00416443`–`0x00416473`). `TKickback`: `0x4c` sets a
*suppression* dword `[this+0x86]` to 1 and `0x4b` clears it, and in
`TKickback::Collision` (`0x00415ba0`) that dword only skips the arming of the kick timer at
`0x00415bd7` — `DefaultCollision` still runs at `0x00415c05`.

**`0x54`/`0x55`/`0x56` are not enter/exit/abandon.** `TRamp::Collision` branches on which
edge was hit. The plane edge carries ramp data at `[edge+0xd]`; when present the ramp
installs its plane on the ball, recomputes its height, and compares the ball's collision
mask `[ball+0xf7]` against the ramp's `[this+0x82]` — already equal means nothing is raised
(`0x00417910`), otherwise the ball is switched onto the ramp's mask and `0x54` is raised.
`edge == [this+0x9e]` is a plain surface hit and raises `0x4a`. `edge == [this+0x96]` is
wall 1 and raises `0x55`; anything else is wall 2 and raises `0x56`. So `0x54` is "the ball
is on my ramp now", which is exactly how `a_ramp8`'s `0x54` arm behaves (it clears the path
by retracting `a_blocg`), and which of `0x55`/`0x56` reads as enter or exit is a property of
the 1301/1302 masks in `PIRATES.DAT`, not of the codes.

**The three "lit" flags are confirmed field-for-field.** `TLight::Message` is CADET.EXE
`0x0040a490`, jump-tabling codes 0–0x1a at `0x0040ac30`; its first five arms name the
layout against upstream's own `TLight::Message`:

| binary code | upstream | arm | writes / reads |
|---|---|---|---|
| 0 | `TLightTurnOff` | `0x0040a4eb` | `[+0x42] = 0`, then tests `0x46`/`0x4a`/`0x4e` |
| 1 | `TLightTurnOn` | `0x0040a534` | `[+0x42] = 1`, then the same three |
| 2 | `TLightGetLightOnFlag` | `0x0040a57d` | returns `[+0x42]` |
| 3 | `TLightGetFlasherOnFlag` | `0x0040a588` | returns `[+0x46]` |
| 4 | `TLightFlasherStart` | `0x0040a593` | `[+0x46] = 1`, `[+0x4e] = 0` |

so the run is `+0x42` `LightOnFlag`, `+0x46` `FlasherOnFlag`, `+0x4a` `ToggledOffFlag`,
`+0x4e` `ToggledOnFlag`, `+0x52` `TurnOffAfterFlashingFg`, `+0x56` `LightOnBmpIndex`, all
stored as dwords where upstream declares `bool`. The drain reads the same three on
`v_lite569` at `0x00426166`/`0x0042616b`/`0x00426170`.

**`TTextBox + 0x5e` is `CurrentMessage`, not a timer.** `TTextBox::Display` (`0x0041de40`)
tests `[ebx+0x5e]` at `0x0041de55` exactly where upstream's `DisplayInternal` tests
`CurrentMessage`, and immediately dereferences the neighbouring `[ebx+0x62]` as
`PreviousMessage->Text` (`[[ebx+0x62]+4]`) and `PreviousMessage->Glyph`
(`[[ebx+0x62]+0x14]`). So `+0x5e` = `CurrentMessage`, `+0x62` = `PreviousMessage`, and the
dispatcher's `*(int*)(info_text_box + 0x5e) != 0` means "the message queue is not empty".

---

## 3. What each mode does

### State 4 — Attack Ship (`FUN_0042ad70`) **[C]**

Hint 3060. Start 200,000. `FUN_00424440()` re-raises the five ship drop targets;
`v_lite4`…`v_lite8` (the ship letters) and `v_lite567` flash.

* Each ship drop target (`FUN_004264f0` case 4): **10,000**; all five down → raise them
  again, award the **jackpot** (`FUN_00424700`), bump `fathom_light_group` (`0x2f`) and
  add **25,000**. ✓ manual "10,000 each … all dropped → ship sunk, jackpot awarded".
* **Ship sink** `a_sink4` (case 4): `FUN_004244a0()` (drop and *disable* all five ship
  targets, mask pre-set to `0x1f`), `DAT_00435cf8++`, **jackpot**, `fathom_light_group`.
  ✓ manual "ball in ship sink → ship sunk".
* **Ship wire ramp** `s_rexit8` (`0x56`, state 4): `FUN_00424440()`, string **3018**
  "Ship Captured", `FUN_00424350()`. ✓ manual.
* `0x47` on `fathom_light_group` prints string **3017** (`0xbc9`) "Ship Sunk".

> **Disagreement [C].** The manual says the ship wire ramp **maxes** the table multiplier.
> `FUN_00424350` lights **one more** fathom light and sets `ScoreMultiplier =
> fathom_on_count + 1`. It is an *increment*, not a max. (The anchor-chain fathom lights
> are the multiplier track — §4.)

> **Not found [U].** The manual's "+25,000 jackpot per ship target".

### State 5 — Bermuda Triangle (`FUN_0042c070`) **[C]**

Hint 3063. Start 200,000. `FUN_00425080()` blanks all eight wind light groups; `a_kout1`
(the magnet in the centre of the circle, modelled as a kickout) gets `0x4b`;
`FUN_00425110(1)` flashes the wind group for the lane currently indicated by the shared
cursor `DAT_00435d04`.

`0x47` on `bermuda_light_group` runs a four-phase rotation driven by the static 4×4 table
`DAT_0043d118 = {{0,4,8,12},{1,5,9,-1},{2,6,10,-1},{3,7,11,-1}}` at 0.1 s per step —
i.e. a 13-member group animated as four interleaved spokes. **[C]**

Hitting the **indicated** wind lane (`FUN_00425230`): advance the cursor to the next lane
and pay `DAT_0043d0e0[DAT_00435cf8++]`:

| Step | Award |
|---|---|
| 0 | 125,000 |
| 1 | 250,000 |
| 2 | 500,000 |
| 3 | 1,000,000 |
| 4 | **jackpot**, counter resets to 0 |

✓✓ **exactly** the manual's progression, read straight out of the EXE. A *non*-indicated
wind lane pays a flat **50,000** (unmultiplied). Any hideaway → `FUN_00423a30` lights the
shipwreck mystery light `v_lite529`. ✓ manual.

### State 6 — Storm (`FUN_0042c430`) **[C]**

Hint 3067. Start 300,000. Severity is computed **once at mode start** from the total
on-count of the eight wind light groups (`left_island`, `right_wind`, `left_orbit`,
`right_orbit`, `south_wind`, `east_wind_1`, `east_wind_2`, `west_wind`):

| Lit winds | Severity | Per target | Jackpot every | String |
|---|---|---|---|---|
| 0–5 | Squall | 10,000 | 8 | 3046 |
| 6–10 | Tropical Storm | 25,000 | 7 | 3047 |
| 11–15 | Monsoon | 50,000 | 6 | 3048 |
| ≥16 | Hurricane | 100,000 | 5 | 3049 |

✓✓ **exactly** the manual's four tiers, including the per-tier jackpot period. The
thresholds (the 5/10/15 banding) are new. Each `0x4a` from a type-`0x3ee` (1006
`TPopupTarget`) caller pays `DAT_00435d2c` and prints a glyph run
`DAT_0043ab28 + 9·count`; on the `DAT_00435d40`-th it awards the jackpot and resets.
Also: `a_kout2` armed with `0x4b` (the hurricane magnet, 1,000,000 when it catches a ball,
§5.4), bumpers/targets add +50,000 to the jackpot per type-`0x3fc` collision,
`a_roll6` (west wind) sets `DAT_00435d90 = 2` and lights the **shoot-again** light
`v_lite569` for 10 s ✓ manual, and **any hideaway ends the mode** (string **3050**, `0xbea`,
self-send `0x53`) and lights the shipwreck mystery ✓ manual.

### State 7 — Tavern (`FUN_0042b420`) **[C]**

Hint 3062. Start 200,000. `FUN_00425d50(0)` and `FUN_00424f70()` re-raise the tavern
bank; `v_lite545/546/547` (the three tavern-window lights) are blanked, and the controller
keeps a `0x46`-driven random flutter on them (type `0x3ea` → re-flash a randomly chosen
one for 0.1–0.25 s).

* **Tavern drop target** (`FUN_00426a40` case 7), not all down: `FUN_00424d30()` (arm a
  random keg magnet, §5.4) + **25,000**. ✓ manual.
* All three down: raise them, `FUN_00424d30()`, the **jackpot**, + **50,000**.
  ✓ manual "all targets dropped → jackpot".
* **Brig target** `a_spot6` in state 7: the 3rd hit gives **Max Crew** (`FUN_00425d50(1)`
  + `FUN_00425000`) instead of starting Jail Break. Not in the manual. **[C]**

> **Not found [U].** The manual's "+50,000 jackpot per tavern target" as a *jackpot*
> contribution — the 50,000 in the binary is a score on bank completion.

### State 8 — Treasure Hunt (`FUN_0042b090`) **[C]**

Hint 3061. Start 300,000. `FUN_00424500()` blanks the six "X" lights
(`v_mlite44`, `v_mlite107`, `v_mlite106`, `v_mlite51`, `v_mlite110`, `v_mlite104`),
`FUN_00424580()` arms the first one, `s_trip3`/`s_trip8` are **disabled** and the orbit
light groups saved-and-blanked (restored by `0x31` `ToggleSplitIndex` at teardown).

`FUN_00424580` is the heart of the mode **[C]**: it enables the two extra sinks
`a_sink9`/`a_sink12`, then advances a 5-phase cursor `DAT_00435d04`, flashing one "X"
light together with the hideaway or orbit group it sits on, and returns a light-sweep id:

| Phase | "X" light | Partner lights | Returns |
|---|---|---|---|
| 0 | `v_mlite110` | `left_orbit_light_group` | 5 |
| 1 | `v_mlite107` | `v_lite525`, `v_lite520` | 2 |
| 2 | `v_mlite104` | `right_orbit_light_group` | 6 |
| 3 | `v_mlite106` | `v_lite526`, `v_lite521` | 3 |
| 4 (wraps) | `v_mlite44` | `v_lite524`, `v_lite519` | 1 |

A hideaway or `a_sink9`/`a_sink12` entry **whose "X" light is flashing** (`+0x46 != 0`)
turns that X solid (`0x13`), awards a **special treasure** (`FUN_00424930`) — or the
**jackpot** for `a_sink17` — and re-arms the next X. Hitting a hideaway whose X is *not*
flashing just pays the ordinary 40,000. ✓✓ manual "each X = a special treasure".

Either **east wind lane** (`a_roll9`, `a_roll8`) in state 8 calls `FUN_00424b00` = **bury
an award** (§5.2). ✓✓ manual, and it is what pins state 8.

> **Disagreement [P].** The manual says "**5th X = jackpot**". In the binary the jackpot
> comes from *which* sink is used (`a_sink17` pays the jackpot via `FUN_00424700`,
> `a_sink15`/`a_sink14`/`a_sink9`/`a_sink12` pay a special treasure via `FUN_00424930`),
> not from a count. There is no 5-counter in the mode.

### State 9 — Mutiny (`FUN_0042ca00`) **[C]**

Hint 3064. Start **100,000** — the cheapest mode. The controller itself is almost empty
(music, blockers, hint); all the rules live in `Pirates_a_bump1_Control`:

* **25,000 per bumper hit** (unmultiplied, `FUN_00423310`) ✓ manual;
* `DAT_00435cf8` counts hits; on the **10th**, the **jackpot** and the counter resets —
  and that hit pays *no* 25,000. ✓ manual "jackpot every 10 bumper hits".
* **Ship sink** `a_sink4` in state 9: lights `v_lite570` — the **Ship's Hold / bonus-hold**
  light ✓ manual — plus `v_lite567` and a lost-float `mode_timer` nudge.

### State 10 — Sword Fight (`FUN_0042cc10`) **[C]**

Hint 3065. Start 250,000. Flashes the three parrot letters `v_lite9/10/11` and the four
volcano lights `v_lite548`…`v_lite551`, and `FUN_004252d0()` re-raises the parrot bank.

* **Parrot target** (`FUN_00426eb0` case 10), not all down: **30,000** ✓ manual.
* All three down: set bits `0x70` in `DAT_00435cf8`, re-raise the bank, and call
  `FUN_00425550(0)`.
* `FUN_00425550`: if the parrot group bits (`0x70`) are complete **or** the volcano bits
  (`0x0f`) are complete, clear them and call `FUN_00425440`.
* `FUN_00425440` cycles `DAT_00435d04`: **0 → special treasure, 1 → coin award,
  2 → map piece, ≥3 → extra ball**.

✓ manual "clearing either group awards one of: coin award, special treasure, map piece or
extra ball" — but **[C] the choice is cyclic in that fixed order, not random**, and the
cursor keeps advancing, so a player who clears four groups in one Sword Fight gets all
four awards once each and then extra balls.

> **[U]** The volcano-bank path into `FUN_00425550` is only reachable while state == 10
> (`FUN_00427540` otherwise starts Lava-Ball). I could not see what resets
> `DAT_00435d04` for Sword Fight — it is shared with the Treasure Hunt "X" cursor and the
> Bermuda wind cursor, and `FUN_0042aad0` zeroes it at every mode start.

### State 0xb — Peg Leg's Loot (`FUN_0042cfc0`) **[C]**

Hint 3066. Start **1,000,000**. Entered only from `FUN_00425850` when every map-piece
light is lit, or from the skill shot's selection path. A stepped payout driven by
`DAT_00435cf8`, armed when the ball enters the **ship sink** (`a_sink4` case 0xb sets
`DAT_00435cf8 = 1`) ✓ manual "put the ball in the ship sink":

| Step | Action |
|---|---|
| 1 | cancel `mode_timer`, play `tabb28` |
| 1…8 | display glyph `DAT_0043ab16 + 2i` (the bytes are `'L'`,`'M'`,…,`'S'`), 0.25 s each |
| 9 | **`coins×500,000 + treasures×500,000 + map_pieces×1,000,000 + modes_completed×1,000,000`**, plus `FUN_00424350()` (another fathom/multiplier), `FUN_00425000(-1)` (**Max Crew**: `a_bump2->MessageField = 6`), `FUN_00424700(0)` (the **jackpot**) |
| ≥10 | `0x52`, eject, re-select |

✓✓ **exactly** the manual's four payout terms, and ✓ "bumpers and the table multiplier are
maxed". Its `0x52` is the fattest teardown in the table: it also relights the **map-piece
group** flashing, clears the six wheel-mode lights `v_lite552`…`v_lite557`, re-enables
`s_trip3`/`s_trip8`, resets the orbit combos, and sets `DAT_0043abb0 = 1` (which makes the
next drain blank the coin and special-treasure groups — i.e. **a fresh collection cycle
starts after the finale**). **[C]**

### State 0xc — Lava-Ball (`FUN_0042d480`) **[C]**

String **3055** (`0xbef`), displayed persistently on both `0x4f` and `0x50`. Started by
`FUN_00427540` when the volcano bitmask `a_spot11->MessageField & 0xf == 0xf` (all four
volcano targets). ✓ manual.

`0x4f`: `FUN_0042aad0()` (common setup), **arm `a_kick1`** (`0x4b` — the left-outlane
kickback), disable `s_oneway12` (the brig release), then the **ball accounting**:

```c
if      (BallsInBrig == 0) { EjectLavaSink(2.0f); EjectLavaSink(1.0f); }  // FUN_00423680
else if (BallsInBrig == 1) {                      EjectLavaSink(1.0f); }
TableG->BallsInPlay += 2;
BallsInBrig = 0;
```

So **Lava-Ball is a +2-ball multiball** (three balls with the one in play), and the two
extra balls are sourced from the **lava sink `a_sink8`** and/or **from the brig** —
`a_sink8` is ejected twice if the brig is empty, once if it holds one, and not at all if it
holds two or more, with `DAT_00435d3c` zeroed so the brig's balls count toward the same +2.
**[C]**

`0x53`: **only acts when `TableG->BallsInPlay == 1`** → `FUN_0042ab40()` (restore the
banks), clear the text box, disarm `a_kick1`, state 2, `FUN_00425310(1)` (relight the four
volcano lights and clear the volcano mask), broadcast `0x4f`. So it ends when the multiball
drains back to a single ball, exactly like Dragon's multiball modes. **[C]**

### State 0xd — Jail Break! (`FUN_0042d590`) **[C]**

String **3056** (`0xbf0`). Entered from `Pirates_a_spot6_Control` (brig target, 3rd hit,
state 2) or from `FUN_00424210` when the brig count reaches 3. ✓ manual.

`0x4f` opens with the check that resolves the manual question:

```c
if (BallsInBrig == 0) {                 // nothing to break out
    ResetBrigLights();                  // FUN_00425d50(1)
    info_text_box->MessageField = 2;
    handler(0x4f, nullptr);
    return;                             // mode aborts, costs nothing
}
```

**So the "≥1 ball in the brig" requirement does exist** — not in the brig target's handler,
but one level up, in the mode's own entry. A player who hits the brig target three times
with an empty brig gets a mode that starts and immediately cancels itself. **[C]**

With at least one ball held: common setup, `FUN_004253a0()` (restore the volcano lights),
**`TableG->BallsInPlay += BallsInBrig`** then `BallsInBrig = 0` — i.e. **Jail Break
releases exactly what the brig holds**, between 1 and 3 balls — flash the three brig lights
`v_lite530/531/532` (`1` then `0x10`), arm `a_kick1`, disable `s_oneway12`, and
**+100,000**. **[C]**

`0x53`: as Lava-Ball — only at `BallsInPlay == 1`, then restore, disarm `a_kick1`, state 2,
`FUN_00425d50(0)`, `FUN_00425310(1)`.

> **Settled from the listing: it is an original-source bug, not a mis-decompile. [C]**
> `asm/0x42d590.txt` reads
> ```
> 42d5de:  mov   eax,ds:0x43af6c          ; v_lite551
> 42d5e3:  mov   DWORD PTR [eax+0x6],0xf  ; ->MessageField = 0xf
> 42d5ea:  call  0x4253a0                 ; RestoreVolcanoLights()
> ```
> so Ghidra's tag resolution was right: the write really does target **`v_lite551`**
> (`DAT_0043af6c`), one of the four volcano *display* lights, and not `a_spot11`
> (`DAT_0043af1c`) where the volcano mask lives. `FUN_004253a0` then re-derives all four
> lights from `a_spot11->MessageField`, which is untouched.
>
> The write is therefore **dead**: `v_lite551->MessageField` is read nowhere in either
> corpus (it is the only occurrence of that expression in 364 functions), and
> `FUN_004253a0()` just restores whatever the volcano mask already said. The intent was
> plainly `a_spot11->MessageField = 0xf` — "light all four volcano lights for the duration
> of Jail Break" — so **Jail Break silently fails to light the volcano bank**, and the two
> globals differ by a single hex digit in the source (`af6c` vs `af1c`). A port should
> decide deliberately whether to reproduce the bug. **[C]**

### State 0x11 — end-of-game table lockdown (`FUN_0042d730`) **[C]**

Not a bonus state — it pays nothing and displays nothing. Set by `Pirates_drain_Control`
when the last ball of the last player drains (and by `FUN_004230c0` on code `0x3f3`), and
the drain handler then immediately sets state `0x10` so the bonus still runs; `0x11` is a
transient that exists only to lock the table down.

`0x4f`: clear `_DAT_0043aba0`, disable `a_kick2` and `s_bwal610`, **arm `a_kick1`**,
disable `s_oneway12`, send `0x4c` to **all six `s_magnet*` plus `a_kout1` and `a_kout2`**
(so nothing can hold the ball during the game-over sequence), and **release the brig**
(`BallsInPlay += BallsInBrig; BallsInBrig = 0`).
`0x53`: disarm `a_kick1` once only one ball is left.

This closes the ball-accounting loop: every ball the brig ever swallowed is returned to the
count by one of exactly three paths — `s_oneway7` (normal release), Lava-Ball / Jail Break
(`0x4f`), or end of game (`0x11`). **[C]**

## 4. Collectibles — how they are stored

Dragon's Keep stores *everything* as lit lights. **Skulduggery is a hybrid**, and the
split is consistent and deliberate: **letters and banks are bitmasks in a
`MessageField`; collectible totals are light-group on-counts or explicit counters.**
**[C]**

| Track | Storage | Count | Conf |
|---|---|---|---|
| **SKULDUGGERY letters** | 11-bit mask in `v_lite1->MessageField`; display lights `v_lite1`…`v_lite11` | 11 | [C] |
| **GOLD letters** | 4-bit mask in `v_lite524->MessageField`; lights `v_lite524`…`v_lite527` | 4 | [C] |
| **WINDS letters** | **lit lights only**, `v_lite533`…`v_lite537` + `lane_winds_light_group` | 5 | [C] |
| **Map pieces** | `v_lite607->MessageField` counter **and** `map_piece_light_group` | 6 | [C] |
| **Coin awards** | `coin_light_group` on-count (no counter) | 6 | [C] |
| **Special treasures** | `special_treasure_light_group->MessageField` counter **and** lights `v_lite571`…`v_lite576` | 6 | [C] |
| **Modes completed** | `v_lite606->MessageField` counter + the wheel lights | 8 | [C] |
| **Crew (bumper level)** | `a_bump2->MessageField`, 0…6 | 7 | [C] |
| **Multiplier / fathoms** | `fathom_light_group` on-count → `ScoreMultiplier` | 6 | [C] |
| **"X" lights** | lit/flashing lights `v_mlite44/107/106/51/110/104` | 6 | [C] |
| **Keg lights** | lit lights `v_lite560`…`v_lite565` + `keg_light_group` | 6 | [C] |
| **Mode wheel** | lit lights `v_lite552`…`v_lite559` | 8 | [C] |
| **Drop banks** | bitmask in the first target's `MessageField` (`a_drop5`, `a_drop13`, `a_drop10`, `a_spot11`) | 5/3/3/4 | [C] |
| **Shipwreck mystery** | `v_lite529` lit | — | [C] |
| **Ship's Hold / bonus hold** | `v_lite570` lit, value in `drain->MessageField` | — | [C] |
| **Shoot again** | `v_lite569` + phase `DAT_00435d90` 0…3 | — | [C] |
| **Targets dropped this ball** | `DAT_00435d48` | — | [C] |

### 4.1 SKULDUGGERY letters ↔ drop targets are **one-to-one** **[C]**

`FUN_00423c90` (run from every teardown, via `FUN_0042ab40`) rebuilds the three drop banks
from the letter mask, and the mapping is explicit:

| Letter bit | Light | Drop target | Bank |
|---|---|---|---|
| 0, 1, 2 | `v_lite1`, `v_lite2`, `v_lite3` | `a_drop13`, `a_drop14`, `a_drop15` | tavern (3) |
| 3, 4, 5, 6, 7 | `v_lite4`…`v_lite8` | `a_drop5`…`a_drop9` | ship (5) |
| 8, 9, 10 | `v_lite9`, `v_lite10`, `v_lite11` | `a_drop10`, `a_drop11`, `a_drop12` | parrot (3) |

A target whose letter is already lit is **disabled** (`0x35`) so it stays down; a target
whose letter is dark is re-enabled (`0x36`). So "every ship/tavern/parrot target hit lights
a SKULDUGGERY letter" ✓ manual — and the word has exactly 11 letters for exactly 11 drop
targets, which is why Skulduggery is the only Full Tilt table with `TPopupTarget`s.

### 4.2 GOLD → coin award **[C]**

The three hideaways set bits 1, 2, 4 (`a_sink17`→1, `a_sink15`→2, `a_sink14`→4) and
`FUN_004239a0` fires when `mask & 0xf == 0xf`: reset the GOLD lights (`FUN_00423870(1)`)
and award a **coin** (`FUN_00423720`). ✓ manual.

**Bit 8 (`v_lite527`, the "D") is the right orbit, not a hideaway — RESOLVED [C].**
`Pirates_s_trip8_Control` in state 2 lights `v_lite527`, ORs bit 8 into
`v_lite524->MessageField`, and then runs the completion check itself. So GOLD is
*three hideaways plus one orbit pass*, which is why it cannot be spelled by hideaways
alone. (`v_lite527` is bound as a handler-less component — it appears in
`simple_components[]` at `0x0043c9c8`, per `analysis/dump_simple.py` — which is why it has
no entry in the score table and why the earlier search for its writer came up empty.)

### 4.3 Coin awards — `FUN_00423720` **[C]**

Light the next coin light (`0x2d`), read the group's on-count, and pay
`DAT_0043d0c0[count]`, read verbatim from the EXE:

`{0, 50,000, 100,000, 200,000, 400,000, 800,000, 1,600,000, 0}`

✓✓ **exactly** the manual's six escalating coin awards. Note index 0 and 7 are zero, so a
7th coin pays nothing. Sources: GOLD complete, Sword Fight group clear, tavern sinks
(1-in-4), skill-shot slots 1/2/8, Buried Treasure, shipwreck mystery via
`FUN_00424fb0`.

### 4.4 Special treasures — `FUN_00424780(index)` **[C]**

`index = special_treasure_light_group->MessageField` (incremented first), so the treasures
come in a **fixed order**:

| Index | Light | String | Score |
|---|---|---|---|
| 0 | `v_lite571` | 3019 (Gold ingots) | 100,000 |
| 1 | `v_lite572` | 3020 (Silver chalice) | 250,000 |
| 2 | `v_lite573` | 3021 (Signet ring) | 500,000 |
| 3 | `v_lite574` | 3022 (The Frantz Ruby) | 750,000 |
| 4 | `v_lite575` | 3023 (Shapiro's Sceptre) | 1,000,000 |
| ≥5 | `v_lite576` | 3024 (Crown of Sandige) | 1,500,000 |

✓✓ **exactly** the manual's six names and values. The `default` arm means a 7th+ treasure
keeps paying 1,500,000 and relighting `v_lite576`, while the counter keeps growing — which
inflates the drain bonus and the Peg Leg's Loot payout without bound. **[C]**

### 4.5 Map pieces — `FUN_00423aa0` **[C]**

`map_piece_light_group` `0x2d`, `v_lite607->MessageField++`, glyph + string 3015,
**+1,000,000** ✓ manual. If the group is then full **and** state == 2, it immediately calls
`FUN_00425850`, which returns 7 → the wheel flashes Peg Leg's Loot. ✓ manual.

Sources **[C]**: **spelling WINDS** (`FUN_00424290` — all five of `v_lite533`…`v_lite537`
lit → reset `lane_winds_light_group` and award) ✓ manual; Sword Fight group clear
(cursor 2); shipwreck mystery; tavern sinks (1-in-4); completing SKULDUGGERY **during
Lava-Ball** (`FUN_00425e30`).

### 4.6 Winds **[C]**

Six lanes, each with its own light group and its own letter light. The wind-pass scoring is
read from the lane's group on-count, used directly as the `scoring[]` index:

`on-count 0 → scoring[0] = 25,000 · 1 → 50,000 · 2 → 75,000`

✓✓ manual "1st pass 25,000 · 2nd in the same lane 50,000 · 3rd 75,000". The groups are
`left_island`, `right_wind`, `left_orbit`, `right_orbit`, `south_wind`, `east_wind_1`,
`east_wind_2`, `west_wind` — **eight**, not six. `FUN_00425110`'s cursor enumerates all
eight (`left_orbit` is both case 0 and the default). That is consistent with the manual's
"Tradewinds (left **and** right orbit)" being one *name* over two lanes, and plausibly
explains the orphan engine string **3006 "Port O' Call"** as the eighth group's name.
**[P]**

The wind *letter* lights are only five (`v_lite533`…`v_lite537` = W-I-N-D-S) and are
rotated right by either flipper (§6).

**Eight groups for six names — resolved [C].** Each lane handler formats its score into its
own name string, and the recovered `sprintf` operands (§0.3) give the full mapping. Two
names cover two lanes each, which is exactly the arithmetic:

| Lane handler | Light group | Name string | Wind |
|---|---|---|---|
| `a_ramp8` (0x428b70) | `left_island` | **3004** | Island Breeze (left island ramp) |
| `a_ramp5` (0x428d80) | `right_wind` | **3005** | Nor'Easter (right island ramp) |
| `s_trip3` (0x429210) | `left_orbit` | **3008** | Tradewind Loop |
| `s_trip8` (0x429400) | `right_orbit` | **3008** | Tradewind Loop *(same string)* |
| `a_roll2` (0x4298f0) | `south_wind` | **3010** | Sou'Eastern |
| `a_roll9` (0x429ac0) | `east_wind_1` | **3011** | Easterly |
| `a_roll8` (0x429c30) | `east_wind_2` | **3011** | Easterly *(same string)* |
| `a_roll6` (0x429da0) | `west_wind` | **3012** | Westerly |

✓✓ This is the manual's six winds exactly, including "**Tradewinds** (left *and* right
orbit)" as one name over two lanes — and it adds that **Easterly** is likewise one name
over the two east wind lanes, which is why Treasure Hunt can bury an award at "either east
wind lane".

**And "Port O' Call" is not a wind at all [C].** String **3006** is displayed by
`Pirates_a_ramp13_Control` (0x428f70, 50,000, triggered on `0x56` ramp-traversal) — the
**Cannon Wire Ramp**. The spec's `[GAP]` calling 3006 "a seventh wind name not in the
manual" is withdrawn: it is the name of the cannon ramp, and the manual's six winds are
complete. (For the same reason string **3007** "In The Brig" belongs to `s_hole19`, and
**3002** "Secret Hideaway" to the three hideaways plus `a_sink9`/`a_sink12`.)

---

## 5. Awards that are not modes

### 5.1 Shipwreck Mystery — `FUN_00425c50` **[C]**

Armed by lighting `v_lite529` (`FUN_00423a30`, string 3059) — from any hideaway during
Bermuda Triangle or Storm, or from Storm's mode-ending hideaway. Collected at the **right
outlane** `a_roll3` via `FUN_00425d10` (if lit: extinguish and fire), or directly from
skill-shot slot 0. Picks **uniformly at random from six**:

| Roll | Award | Function |
|---|---|---|
| 0 | Special treasure | `FUN_00424930` |
| 1 | **Extra ball** (`TableG+0x18e = 1`, string 3014) | `FUN_004239d0` |
| 2 | **Max Crew** (`a_bump2->MessageField = 6`, string 3044) | `FUN_00425000` |
| 3 | Multiplier up one fathom | `FUN_00424350` |
| 4 | **Map piece** | `FUN_00423aa0` |
| 5 | **Buried Treasure** payout | `FUN_00425ab0` |

✓✓ manual's six exactly (Special Treasure / Max Crew / Max Multiplier / Map Piece /
Buried Treasure / Extra Ball), with the one correction that roll 3 **increments** rather
than maxes the multiplier.

### 5.2 Buried Treasure — `FUN_00424b00` (bury) / `FUN_00425ab0` (dig) **[C]**

Burying happens at either **east wind lane** during Treasure Hunt. `DAT_00435d10` is the
cursor, and the "pouch" is six file statics:

| Cursor | Buried | String | Pouch cell |
|---|---|---|---|
| 0, 1, 2 | a coin award | 3040 | `DAT_00435d14++` |
| 3, 4 | the next special treasure (flashes its light, strings 3034…3039) | 3034+n | `DAT_00435d18` / `DAT_00435d1c` |
| 5 | a **fathom bonus** = `FUN_00424aa0()` | 3041 | `DAT_00435d20` |
| 6 | the current **jackpot** value | 3042 | `DAT_00435d24` |
| 7 | an **extra ball** | 3043 | `DAT_00435d28` |
| ≥8 | a coin award | 3040 | `DAT_00435d14++` |

✓✓ **resolves the manual's "strings 3034–3043 are the ten '<X> Buried' messages"** — they
are six treasure names (3034–3039) plus coin / fathom / jackpot / extra ball (3040–3043),
exactly ten. **[C]**

`FUN_00424aa0` (not in the manual) **[C]**:
`fathom_on_count × ((treasures×2 + map_pieces)×25 + modes_completed×20 + targets_dropped)
× 10,000`.

`FUN_00425ab0` pays the whole pouch out at once with `DAT_0043abac = 1` (which halves
every display dwell from 2.0 s to 1.625 s so the sequence does not take forever).

> **Genuine bug [C].** The second buried treasure is paid as
> `FUN_00424780(DAT_00435d18)` — it re-reads the *first* slot, which has already been
> cleared to `-1`, instead of `DAT_00435d1c`. So the second buried treasure always pays
> the `default` arm (Crown of Sandige, 1,500,000).

### 5.3 Skill shot — state 3 (`FUN_0042abf0`) **[C]**

`s_launcher1->MessageField` rolls 0→8→0 on every `0x50` tick, displaying an 8-byte glyph
from `s_______2_0043aa58 + 8i` for 0.2 s. The first `s_oneway3` collision locks it in:

| Slot | Award | Manual name |
|---|---|---|
| 0 | `FUN_00425c50` — **Shipwreck Mystery** | Shipwreck Mystery |
| 1, 2, 8 | `FUN_00423720` — **coin award** | SILVER / GOLD / DIAMONDS |
| 3 | state 5 — **Bermuda Triangle** | BERMUDA |
| 4 | state 4 — **Attack Ship** | ATTACK |
| 5 | state 8 — **Treasure Hunt** | TREASURE |
| 6 | state 7 — **Tavern** | TAVERN |
| 7 | state 6 — **Storm** | STORM |

✓✓ **exactly** the manual's skill-shot list, and this is independent confirmation #5 of the
state map. Slots 0/1/2/8 also drop back to state 2; the five mode slots bypass
`FUN_00425850` entirely, so the skill shot can start a mode the wheel was not offering.
**[C]**

### 5.4 The six magnets, `s_magnet1`…`s_magnet6` (type 1042) **[C]**

These are the table's signature mechanic and have no Space Cadet counterpart. They are
simple, and the manual's description is accurate:

* Each magnet is paired one-to-one with a **keg light**:
  `s_magnet1`↔`v_lite560`, `s_magnet2`↔`v_lite561`, … `s_magnet6`↔`v_lite565`
  (also grouped as `keg_light_group`). **[C]**
* **Arming** — `FUN_00424d30` ("arm a random keg magnet"): build the list of **unlit** keg
  lights (if none is unlit, use all six), pick one at **random**, send that magnet `0x4b`
  (enable), flash-then-stay-on its keg light for 1.0 s, read the group's on-count, and
  display a glyph. Called **only** from the tavern drop-bank routine `FUN_00426a40` while
  state == 7 — once per tavern-target hit, and again on bank completion. **[C]**
  ✓ manual "Tavern mode … light a keg light (turning on the magnet beneath)".
* **Firing** — all six handlers (`0x42a470`, `0x42a500`, `0x42a590`, `0x42a620`,
  `0x42a6b0`, `0x42a740`) are byte-identical apart from the light and the sound offset.
  On `ControlCollision`: play `tabb87wave`, turn the paired keg light **off** (`0x14`),
  send **itself** `0x4c` (disable), add **100,000** (unmultiplied, via `FUN_00423310`),
  and display string 3003. **[C]**
  ✓✓ manual "Ball stuck on a keg light = **100,000**".
* **Clearing** — `FUN_00424c70` sends `0x4c` to all six and extinguishes all six keg
  lights. Called from the Tavern teardown (`0x52`) and from `s_launcher1`'s ball-launch
  branch. So **keg magnets do not survive the ball**. **[C]**

That is the whole of the six magnets: an armed magnet captures the ball, pays a flat
100,000, and disarms itself. There is no stacking, no multiplier, and no progression.

> **Clarification worth recording [C].** The manual says "magnets active during Storm,
> Bermuda Triangle and Tavern modes". Only the **Tavern** magnets are the type-1042
> `s_magnet*` objects. The other two are **kickouts** driven with the same `0x4b`/`0x4c`
> pair:
> * `a_kout1` — armed by the **Bermuda Triangle** controller's `0x46` branch; the "magnet
>   in the centre of the circle";
> * `a_kout2` — armed by the **Storm** controller's `0x4f`, disarmed at `0x52`; the
>   "Hurricane light (magnet beneath)".
>
> Both route to the same handler `Pirates_a_kout2_Control`, which on capture pays a flat
> **1,000,000** (string 3015) and restarts its own timer (`0x40`). So the Bermuda/Storm
> magnets are worth 10× a keg magnet. **[C]**

### 5.5 `s_launcher1` (type 1034) — the launcher, in place of a plunger **[C]**

`PIRATES.DAT` has no `TPlunger`; `s_launcher1` is the single type-1034 object. Its handler
`Pirates_s_launcher1_Control` is *not* Dragon's plunger handler — it does **not** reset the
ball, pick a mode, or start the skill shot by itself. It handles three table-level codes:

| Code | Behaviour |
|---|---|
| `0x3ed` | if `TableG+0x1b2 == 0` (not in the end-of-ball lockout): `a_cannon->Message(0x39, 3.0)` then `0x3a` — i.e. **fire the cannon** (blocker restart-timeout + the FT-only `0x3a`) |
| `0x3f8` | broadcast `0x50`, then step the **shoot-again light** `v_lite569` down through phase `DAT_00435d90` 3→2→1→0 (solid → 10 s timed-on → 5 s flash → off) |
| `0x3f9` | **ball served**: if `DAT_00435d90 == 0` (no shoot-again pending) set state **3** and broadcast `0x4f` → the skill shot; then light `v_lite569` solid, set phase 3, flash `v_lite544`, **enable `a_kick2` and `s_bwal610`**, blank `fathom_light_group`, set `ScoreMultiplier = 1`, disarm `a_kick1`, and clear all six keg magnets |

So the launcher is the **ball-serve + skill-shot + shoot-again** device. The "15 s
re-launch" of the manual is the `0x3f9` call `s_launcher1->Message(0x3f9, 0.96)` issued by
the drain and the bonus controller — the **0.96** is a serve delay, not 15 s; no 15-second
constant appears anywhere. **[P]** (the manual may be describing the auto-launch timeout,
which lives in the launcher component itself, not in the rules code — **[U]**).

Note the asymmetry with Dragon: because there is no plunger handler, the per-ball resets
that Dragon does in `Dragon_s_plunger1_Control` are split here between `s_launcher1`
(`0x3f9`), the idle controller `FUN_0042ab90` and the skill-shot controller
`FUN_0042abf0` (`0x4f`: `DAT_00435d30 = 1`, `DAT_00435d48 = 0`). **[C]**

---

## 6. Scoring, bonus, multiplier

* Jackpot = `TableG+0x172`, clamped to 10,000,000 (`FUN_00423650`), initialised to
  **1,000,000** at new game (`FUN_004230c0` case `0x3f6`). ✓ manual. **[C]**
  There is **no super jackpot** in Skulduggery — nothing writes `TableG+0x17a`, unlike
  Dragon. **[C]**

### 6.0 Jackpot contributions — the complete table **[C]**

Read directly out of `FUN_0042d850` and `FUN_0042d900` (§0), so this is now closed:

| Source | Contribution | Where |
|---|---|---|
| Any collision with a **type-1006 `TPopupTarget`** (the 11 drop targets) | **+20,000** (and `TargetsDroppedThisBall++`) | `FUN_0042d850` |
| Any collision with a **type-1019 `TSoloTarget`** (`a_spot*`) | **+20,000** | `FUN_0042d850` |
| Any collision with a **type-1017 `TSink`** (ship sink, hideaways, tavern sinks) | **+40,000** | `FUN_0042d850` |
| **Mode teardown** while `3 < state < 0xc` | **+750,000** | `FUN_0042d900` |
| Storm, per type-`0x3fc` collision | +50,000 | `FUN_0042c430` |
| Treasure Hunt, per type-`0x3fc` collision | +20,000 | `FUN_0042b090` |

✓✓ **Exactly** the manual's "Targets **+20,000**, sinks and hideaways **+40,000**, mode
completion **+750,000**". Disagreement #8 of the earlier revision is withdrawn: the manual
was right and the contributions were simply all in the one function I could not read. The
two mode-local `0x3fc` accruals are extra and not in the manual. **[C]**

Note that the accrual in `FUN_0042d850` happens on **every** broadcast with a non-null
caller, before any dispatch and regardless of mode — so a drop target pays its +20,000 into
the jackpot even in a mode that suppresses its score, and the same collision can also pay
its mode-local accrual on top. **[C]**
* `FUN_00424700(t)` = award the jackpot (adds `TableG+0x172`; never resets it ✓ manual),
  display glyph `DAT_0043ab04`, play `tabb85wave` when `t >= 0`.
* `TableG+0x15e` = `ScoreMultiplier`, written **only** by `FUN_00424350`
  (`= fathom_on_count + 1`) and reset to 1 at ball serve. Max 7 if all six fathoms light.
  **[C]** This confirms `dragon-scoring-handlers.md`'s conclusion that Full Tilt's
  `score_multipliers` must be `{1,1,2,3,4,5,…}` (multiplier == index), not Space Cadet's
  `{1,2,3,5,10}`.
* Three scoring wrappers, structurally identical to Dragon's and with the same
  "last scoring component gets `0x4d`" bookkeeping (`DAT_00435d44`):
  `FUN_004232c0(cmp, idx)` = `AddScore(cmp->get_scoring(idx))`,
  `FUN_00423310(cmp, n)` = `AddScore(n)`,
  `FUN_00423350(cmp, n)` = unmultiplied (`BonusScoreFlag`/`JackpotScoreFlag` → 0,
  `ScoreMultiplier` → 1, restore after). **[C]**
* **Flippers rotate the lanes** — `Pirates_a_flip1_Control` / `a_flip2_Control` on
  message `1` send `0x1c` (rotate) to `lane_winds_light_group`,
  `island_lane_top_light_group` and `island_lane_bottom_light_group`.
  ✓✓ manual "flippers also rotate the wind letter lights and island lane lights to the
  right". **[C]** (Both buttons do the same thing, and `a_flip3`/`a_flip4` have no rules
  handler — consistent with the manual's "the upper-playfield and ship flippers are
  mirrored onto the two buttons".)
* **Bumpers / crew** — `Pirates_a_bump1_Control`, state 2: `a_bump1->MessageField`
  counts to **12**, then resets and, if `a_bump2->MessageField < 6`, increments it, re-reads
  `get_scoring(level)` and prints string **3001** "Crew Added / Bumpers Now %ld". The score
  is always `get_scoring(a_bump2->MessageField)`, i.e. the seven-entry table
  `1500, 3000, 4500, 6000, 7500, 9000, 10500`. ✓✓ manual "+1,500 per 12 hits up to a
  maximum of 10,500". **[C]** (Dragon's equivalent counter is 10; the manual's "every 12"
  is right for this table.) The recovered `sprintf` operand settles what the `%ld` in
  3001 / 3044 shows: `caller->get_scoring(newLevel)` — i.e. **the new per-bumper value**,
  so the lines read "Crew Added / Bumpers Now 6000" and, from `FUN_00425000`
  (`get_scoring(6)`), "Max Crew / Bumpers Now **10500**". ✓ manual. **[C]**
* **Brig** — `s_hole19` (`0x42` ball captured, string 3007 "In The Brig") →
  `FUN_00424210`: `TableG+0x192 -= 1`, `DAT_00435d3c += 1`; at 2 enable `s_oneway12`; at
  **3** start Jail Break. `s_oneway7` is the release: `DAT_00435d3c -= 1`,
  `TableG+0x192 += 1`, and when empty disarm `a_kick1`. **[C]**

### 6.1 Drain bonus — states 0x10 / 0x11, `FUN_0042b820` **[C]**

`DAT_00435d38` = step, `DAT_00435d00` = accumulator, `DAT_00435d34` = "skip the display"
(set when a flipper press fast-forwards it, message `1`). One line per step.

All four multipliers are now confirmed **from the instruction stream**, independent of
Ghidra — the compiler emitted them as `lea`/`shl` chains rather than `imul`, so they are
worth showing: **[C]**

| Step | Chain in `asm/0x42b820.txt` | Product |
|---|---|---|
| 0 | `5·5·5·5 · x` then `shl 4` → `625·16` | **×10,000** |
| 1 | `(32m−m)·4+m = 125m`, `·5·5` → `3125m`, `shl 6` → `·64` | **×200,000** |
| 2 | `(64p+p)·3 = 195p`, `·4+p = 781p`, `·4+p = 3125p`, `·5`, `shl 4` | **×250,000** |
| 3 | `(32t−t)·4+t = 125t`, `·5·5` → `3125t`, `shl 5` → `·32` | **×100,000** |

and the `sprintf` operand at each step is the **term value already multiplied**, so the
display reads "<term name>: <points>". **[C]**

| Step | Term | Value | String |
|---|---|---|---|
| 0 | `DAT_00435d48` (targets dropped this ball) | × **10,000** | 3028 Target Bonus |
| 1 | `v_lite606->MessageField` (modes completed) | × **200,000** | 3029 Mode Bonus |
| 2 | `v_lite607->MessageField` (map pieces) | × **250,000** | 3030 Map Piece Bonus |
| 3 | `special_treasure_light_group->MessageField` | × **100,000** | 3031 Special Treasures |
| 4 | `drain->MessageField` (held bonus) | as-is | 3032 Ship's Hold |
| 5 | total → score (unmultiplied); if `v_lite570` lit, **extinguish it and store the total into `drain->MessageField`** for the next ball, else clear the hold | | 3033 Total Bonus |
| ≥6 | extra ball (`TableG+0x18e`) → re-serve with string 2011…2014, else decrement the ball count; if that was the last ball → state **1**, else state **3** | | |

✓✓ **exactly** the manual's four bonus terms and the bonus-hold rule, and it pins
**`v_lite570` = Ship's Hold = the bonus-hold light**, lit by the **ship sink during
Mutiny**. **[C]**

Also like Dragon: "per target dropped" really counts **collisions with type-1006
`TPopupTarget`s this ball**, incremented wherever `DAT_00435d48` is touched and zeroed at
`FUN_0042abf0`'s `0x4f`.

---

## 7. Cheat codes (`FUN_0042a830`) **[C]**

Not part of the rules, but it is the only other function that drives the mode register, so
it belongs in the map. A small keyboard state machine (`DAT_0043d110` = last char,
`DAT_0043d114` = which prefix matched) recognising:

| Sequence | Effect |
|---|---|
| `1` `M` `A` `X` | `FUN_0042a7d0(2.0)` + `TableG+0x5a = 1` |
| `N` `M` `A` `X` | if state == 2: `DAT_00435d30 = 1` and **`FUN_00425a50()` — start the selected mode** |
| `F` `M` `A` `X` | (prefix 3, no `X` arm — dead) |
| `G` `M` `A` `X` | toggles `DAT_00439140` |
| `Q` `U` `O` `T` `E` | `FUN_00422b90(0x6a)` |

Its first three lines also contain a mode-register read: in state 0x10 with
`DAT_0043aba8 != 0` and the char being one of two specific keys, it broadcasts code **1**
— which is the bonus controller's fast-forward. **[P]**

---

## 8. Manual-vs-binary disagreements, collected

| # | Manual | Binary | Conf |
|---|---|---|---|
| 1 | "Starting a mode is worth 200,000" | Varies per mode: 100,000 (Mutiny) / 200,000 (Attack, Bermuda, Tavern) / 250,000 (Sword Fight) / 300,000 (Treasure Hunt, Storm) / 1,000,000 (Peg Leg's Loot) | [C] |
| 2 | Ship wire ramp "maxes" the multiplier; shipwreck mystery gives "Max Multiplier" | Both **increment** it by one fathom light (`ScoreMultiplier = fathoms + 1`) | [C] |
| 3 | Treasure Hunt: "5th X = jackpot" | The jackpot depends on **which** sink the X is on (`a_sink17` only); there is no 5-counter | [P] |
| 4 | Sword Fight group clear "awards one of" four things | **Cyclic in a fixed order** (treasure → coin → map piece → extra ball → extra ball …), not a choice | [C] |
| 5 | ~~Jail Break needs "3 hits with ≥1 ball in the brig"~~ | **Withdrawn.** The check exists, one level up: `Pirates_a_spot6_Control` does not test the brig, but `FUN_0042d590`'s `0x4f` aborts straight back to state 2 if `BallsInBrig == 0`. Manual correct | [C] |
| 6 | "7 mode lights … plus Peg Leg's Loot" | 8 lights `v_lite552`…`v_lite559`; the first **seven** are the random-selection pool, the 8th is reachable only via all six map pieces | [C] |
| 7 | ~~Six wind lanes~~ | **Withdrawn.** Eight light groups, but only six *names*: Tradewind Loop and Easterly each cover two lanes, and string 3006 "Port O' Call" is the cannon wire ramp, not a wind. Manual correct (§4.6) | [C] |
| 8 | ~~Jackpot "+20,000 per target, +40,000 per sink, +750,000 per mode completion"~~ | **Withdrawn.** All three are in `FUN_0042d850` / `FUN_0042d900` verbatim (§6.0). Manual correct | [C] |
| 9 | Tavern/ship "+50,000 / +25,000 jackpot per target" | Those amounts exist as **scores on bank completion**, not as jackpot contributions | [P] |
| 10 | Plunger "15 s re-launch" | The rules code serves with a **0.96 s** delay; no 15 s constant | [P] |
| 11 | (nothing) | **Mode completion is not checked** — `v_lite606->MessageField` is bumped by the shared `0x52`, so timing out or draining counts as completing, for both the 200,000/mode bonus and the 1,000,000/mode finale | [C] |
| 12 | (nothing) | A **7th+ special treasure** keeps paying 1,500,000 and keeps incrementing the counter, unbounded | [C] |
| 13 | (nothing) | **Bug**: the second buried special treasure is read from the wrong slot (`DAT_00435d18` instead of `DAT_00435d1c`), so it always pays the Crown of Sandige | [C] |
| 14 | "hint strings 3060–3067" | Storm's hint is **3067**, not 3064 — the hint block is *not* parallel to the mode-selected block 3068–3075 | [C] |
| 15 | Magnets "active during Storm, Bermuda Triangle and Tavern" | Only Tavern uses the type-1042 `s_magnet*`; Bermuda's and Storm's magnets are the kickouts `a_kout1`/`a_kout2`, and they pay **1,000,000** against the keg magnets' 100,000 | [C] |

Three of the fifteen were **withdrawn as the evidence improved** — #5 and #8 once the
dispatcher was readable, #7 once the `sprintf` operands were recovered. All three were
cases where the manual was right and the relevant code was simply somewhere I could not yet
read. The other twelve stand, and #1, #2, #4, #11, #12, #13 are all now **[C]** against
either the dispatcher or the instruction stream.

Structural differences from Dragon's Keep worth noting for a port: **no restart window and
no `0x51` code** (the dispatcher returns to state 2 itself after every `0x52`); **no super jackpot**; **no plunger**; drop-target banks and letter sets
stored as **bitmasks** rather than as lit lights; a **per-mode** start bonus; and the mode
order is a **permutation** (`DAT_0043d0f0`) rather than the identity.

---

## 9. Open items

**Closed by the dispatcher (`FUN_0042d850` / `FUN_0042d900`) and the `pirates_rest` corpus:**

* ~~the dispatcher itself~~ → §0. The mode half of the state table is confirmed by the
  primary source and agrees exactly with the `DAT_0043d0f0` reconstruction; four non-mode
  states were corrected (§1.3.1).
* ~~the jackpot contribution table~~ → §6.0. +20,000 / +20,000 / +40,000 / +750,000,
  matching the manual exactly.
* ~~Lava-Ball and Jail Break ball accounting~~ → §3. Lava-Ball `+2` sourced from the lava
  sink and/or the brig; Jail Break `+= BallsInBrig`; both end at `BallsInPlay == 1`;
  end-of-game (`0x11`) returns anything the brig still holds.
* ~~GOLD bit 8 has no writer~~ → §4.2. It is `s_trip8`, the right orbit.

**Closed by the objdump listings:**

* ~~the `sprintf` operands~~ → §0.3. The call shape is uniform across all 50 sites, so
  every `%ld` is now accounted for: the awarded score (23 sites), whole seconds remaining
  (8), a drain-bonus term (6), the new bumper value (2). Two incidental corrections fell
  out: `FUN_00409730` takes **two** arguments, and the three scoring wrappers **return**
  the score they added.
* ~~the four missing mode time limits~~ → §2.3. Attack Ship **29.38**, Mutiny **48.28**,
  Sword Fight **29.38**, Peg Leg's Loot **30.68**, with their light-sweep ids
  (0.0 / 10.0 / 11.0 / none).
* ~~`v_lite551->MessageField = 0xf`~~ → §3, Jail Break. Not a mis-decompile: the listing
  confirms the write targets `v_lite551`, so it is an **original-source bug** (`af6c` for
  `af1c`) and Jail Break never lights the volcano bank.
* ~~the eighth wind group~~ → §4.6. Resolved by the recovered name-string operands, which
  give all eight lanes: **Tradewind Loop** and **Easterly** each cover two lanes, giving
  the manual's six winds over eight groups, and string 3006 **"Port O' Call" is the cannon
  wire ramp**, not a wind — so the spec's `[GAP]` on a seventh wind name is withdrawn.
* ~~the drain-bonus multipliers~~ → §6.1, now read from the `lea`/`shl` chains rather than
  from Ghidra: ×10,000 / ×200,000 / ×250,000 / ×100,000.
* **A seventh and eighth confirmation of the state map** arrived as a side effect
  (§1.3.2): every mode controller's `0x50` arm formats into the manual's own name string
  for that mode (3016 / 3053 / 3025 / 3045 / severity / 3051 / 3052 / 3054), eight for
  eight.

**Closed by the engine component classes in `CADET.EXE` (§2.5.1):**

* ~~`0x39`'s meaning~~ → it sets a blocker's **position index**, not a timeout; `TBlocker` is
  a multi-position moving wall and `0x45` is the real `TBlockerRestartTimeout`. The rules
  files now all send `0x39` rather than upstream's constant 59. **[C]**
* ~~`0x4b`/`0x4c` semantics (old open item 3)~~ → a disarmed **magnet** or **kickout** is
  removed from the world (its edge carries `&ActiveFlag` and `TEdgeManager` skips cleared
  edges), and a disarmed kickout also spits out any ball it holds; a disarmed **kickback**
  still collides and merely declines to kick. So the flat 1,000,000 of `a_kout1`/`a_kout2`
  is **not** reachable while they are disarmed. **[C]**
* ~~`0x54`/`0x55`/`0x56`~~ → plane-mounted / wall 1 crossed / wall 2 crossed, from
  `TRamp::Collision`. **[C]**
* ~~`0x45`/`0x48` on `mode_timer`~~ → arm (negative = cancel only) and read-remaining in
  **milliseconds**, from `TTimer::Message`. **[C]**
* ~~`0x1b`~~ → `TLightSweep::Message` uses the float as the DAT **state index** for
  attributes 1600/1602/1604. **[C]**
* ~~`TTextBox + 0x5e`~~ → `CurrentMessage`. **[C]**
* ~~`+0x42`/`+0x46`/`+0x4e`~~ → `LightOnFlag`/`FlasherOnFlag`/`ToggledOnFlag`, with
  `+0x4a`/`+0x52`/`+0x56` named as well. **[C]**

**Closed by `PIRATES.DAT` (`game/WIN95/FULLTILT/PIRATE/PIRATES.DAT`, parsed with the
`PARTOUT(4.0)` layout in `reference/SpaceCadetPinball/partman.cpp`):**

* ~~type **1020**~~ → `TLightRollover`, and `table_objects`' iattribute 1025 gives the
  thirteen objects that use it, all `v_mlite*`: `v_mlite7`, `8`, `37`–`40`, `42`, `43`,
  `45`–`47`, `49`, `111`. The bumpers are 1005 (and there are **three**, `a_bump1`–`3`, not
  four) and `a_rebo1`/`a_rebo2` are plain type-1000 walls, so the "bumpers and rebounds by
  elimination" guess was wrong: Storm's +50,000, Treasure Hunt's +20,000 and Peg Leg's
  Loot's +50,000 are paid for crossing a **lit rollover**. **[C]**
* ~~`lane_winds_light_group`~~ → group 531, iattribute 1027 = `[401..405]` =
  `v_lite533`–`v_lite537`, the five W-I-N-D-S letters over the five rollover lanes (two
  left, `a_roll5`/`a_roll4`; three right, `a_roll1`/`a_roll2`/`a_roll3`). The two island
  groups are 532 (`v_lite538`–`540`) and 533 (`v_lite541`–`543`). **[C]**
* ~~the six keg magnets' lights (§5.4's 1:1 pairing)~~ → `keg_light_group` is group 550 with
  iattribute 1027 = `[500..505]` = `v_lite560`–`v_lite565`, which matches the six handlers'
  own light globals read out of their listings (`0x0043bd6c`, `…bd84`, `…bd9c`, `…bdb4`,
  `…bdcc`, `…bde4`, stride 0x18). No longer asserted from `.data` ordering. **[C]**
* ~~`v_lite504`/`v_lite505`~~ → groups 412 and 413, 8×9 px at screen (244,146) and
  (233,144), i.e. on the blocker passage immediately under `a_bloce` and `a_blocf` — not a
  "mode in progress" banner pair. **[C]** for the location, **[P]** for the route reading.
* ~~which outlane the two kickbacks guard~~ → **both are on the left**, which the rules
  files had as opposite outlanes. Attribute 600: `a_kick2` X 5.23–6.25 / Y 12.98,
  `s_bwal610` X 5.21–6.54 / Y 11.70–13.67 (overlapping it), `v_lite544` X 5.70 / Y 12.74
  (on it), `a_kick1` X 6.92–7.94 / Y 14.02, drain at X 0. Positive X is screen-left.
  `a_kick1` is the outer and lower of the pair. **[C]** for the geometry, **[U]** for which
  one the manual calls the outlane.
* ~~the glyph tokens~~ → indices into group **237, `font2`**, the table's symbol font, which
  is why the original passes a fourth `Display` argument of 1 for them. (Which picture each
  code draws is still unrendered, but the mechanism is no longer a guess.) **[C]**
* the **music tracks** → see the new entry below.

**Closed by `PIRATES.EXE`'s music loader, and it was four roles wrong:**

* `0x004231d7` loads **six** tracks by the literal names `"tabb1"`…`"tabb6"` (strings at
  `0x0043d1c8` + 8*n) into the handle globals `0x0043ab80`, `…84`, `…88`, `…8c`, `…90`,
  `…94` in that order. There is no seventh load: `0x00423238` copies `0x0043ab90` into
  `0x0043ab98`, so the **game-over music is an alias of TABB5**. The install confirms it —
  only `TABB1.MID`–`TABB6.MID` exist in `game/WIN95/FULLTILT/PIRATE/SOUND`. Each role is
  pinned by the global its controller pushes into `0x0040eb50`:
  TABB1 idle (`0x0042abb8`), TABB2 attack ship / sword fight (`0x0042af64`, `0x0042cd48`),
  TABB3 treasure hunt / Peg Leg's (`0x0042b25e`, `0x0042d097`), TABB4 tavern
  (`0x0042b637`), TABB5 Bermuda (`0x0042c2bd`) and game over (`0x0042bfff`), TABB6 storm /
  mutiny (`0x0042c62c`, `0x0042cab5`). `0x0042322c`–`0x004232ad` then substitutes TABB1 for
  any handle that failed to load. **[C]**

**Closed by full-image reference censuses (objdump over all of `.text`,
`0x00401000`–`0x00435000`):**

* `DAT_0043aba0` is **write-only in the shipped binary** — two stores, `0x00425f62` (in
  `s_launcher1`'s handler) and `0x0042d74f` (dispatcher region), and no load anywhere. It
  is not a cell whose reader is waiting to be found. **[C]**
* `TPinballTable + 0x1b2` (`TiltLockFlag`) is **never** stored a negative value in either
  `PIRATES.EXE` or `CADET.EXE`; every immediate store is 0 or 1. The drain's game-over
  predicate is also not `!= -1` but `>= 0`: `0x0042627c` computes
  `inc / cmp 1 / sbb / inc`. The branch is therefore dead and `game_over_phase` is always
  1. **[C]**
* `DAT_00435d30` has four writers — `0x00425df4` = 1 (`SkulduggeryCompleteIdle`),
  `0x0042aca2` = 1 (the skill-shot controller's `0x4f`), `0x0042aa29` = 1 (the N-M-A-X
  cheat, only in state 2), `0x0042aba1` = 0 (Idle's `0x4f`) — and nine readers, all
  `cmp …,0` in mode controllers' "sweep finished" arms. The **launcher does not touch it**,
  so the note that pointed at `0x00425e70` to settle it was pointing at nothing. Its
  meaning is "a mode started with a ball already live, do not feed another", which makes
  this document's "not in the skill shot" the inverse and the rules' `SkillShotActive` too
  narrow. The port was also missing the `0x00425df4` write entirely; it is restored. **[C]**
* `DAT_00435d04` (the shared cursor) has exactly nine references — reads at `0x00424597`,
  `0x00425117`, `0x00425440`; `add` at `0x0042459c`; `inc` at `0x0042523c`, `0x00425484`;
  zeroing stores at `0x004245c7`, `0x0042512f`, `0x0042aada`. There is no fourth writer and
  no save/restore pair, which confirms item 2 below as a real gap rather than an unread
  one. **[C]** for the census, **[U]** for the intent.
* `tabb02wave`'s two `.data` slots are two `component_tag` records (bases `0x0043c0b0`,
  `0x0043c158`) with two *copies* of the same name string (`0x0043c0a0`, `0x0043c148`), and
  `PIRATES.DAT` has exactly one group called `tabb02wave` (group 73). Binding is by name
  through `record_labeled`, so both tags resolve to the same `TSound`. **[C]**

**Still open — and these are genuinely open, not awaiting a listing:**

1. `Pirates_a_sink4_Control` (`0x4283f0`) decompiles into stack-byte noise in three of its
   five arms. Its listing exists and the two `0xbbb` operands in it are now accounted for
   (§0.3), but I have **not** re-read the state-9 and state-0xb arms instruction by
   instruction, so the lights those arms touch and their timings should be re-checked
   before being relied on. **[P]** — the one remaining item that *is* just reading.
2. `DAT_00435d04` is shared by three different cursors — the Treasure Hunt "X" selector
   (`FUN_00424580`), the Bermuda indicated-wind selector (`FUN_00425110` /
   `FUN_00425230`), and the Sword Fight award selector (`FUN_00425440`) — and
   `FUN_0042aad0` zeroes it at every mode start. The dispatcher does not touch it. Whether
   the aliasing is a deliberate single "wheel position" concept or a second bug of the
   `v_lite551` kind cannot be decided from the code alone; it needs the table in motion.
   **[U]**
3. ~~`0x4b` / `0x4c` semantics.~~ **Closed.** The reasoning that retired it is worth keeping,
   because the item's own conclusion — "that is engine code and is not in this corpus at
   all, so no amount of reading the table rules will settle it" — was right about the table
   rules and wrong about the corpus. The component classes are all in `CADET.EXE`, which
   *is* available; §2.4 of `docs/how-this-was-done.md` had already established that route
   for the seven missing object types, and it applies to every private message code too.
   See §2.5.1: a disarmed magnet or kickout is out of the world, a disarmed kickback is
   not. **[C]**
4. **`simple_components[]` has 192 entries** against the score table's 74, so ~118
   components are bound with no handler of their own. I used it only to resolve
   `v_lite527`; a full sweep against the lights referenced in this document has not been
   done, and would be the cheapest way to find any remaining "who writes this light" gap.
   **[U]**
5. Mode **success vs. timeout** (§2.4). There is no discriminator anywhere in the
   dispatcher or the eight controllers, so "completed" means "started". **[C]** for the
   mechanism; whether that is the intended design is not answerable from the binary.
   **[U]**
