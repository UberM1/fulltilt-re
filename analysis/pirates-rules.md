# Skulduggery — mode / collectible system (from `PIRATES.EXE`)

Derived purely from `decomp/pirates/functions/*.c` (Ghidra output, 178 functions) plus
`decomp/pirates_rest/functions/*.c` (186 more, 0x42d000–0x435000 — **this is where the
dispatcher lives**), every `DAT_` pre-annotated with its component tag name; the objdump
listings in `decomp/pirates/asm/*.txt`; static tables read out of
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
Component field offsets `+5` (enable byte), `+6` (`MessageField`), `+0x42/+0x46/+0x4e`
(the three "lit" flags) are used exactly as in Dragon. **[C]**

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

### 0.3 Asm cross-check

`decomp/pirates/asm/*.txt` exists for the 69 named handlers (not for `pirates_rest`).
I re-read the riskiest body in this document — `Pirates_s_magnet1_Control`, whose Ghidra
output is almost entirely stack-byte noise — against `asm/0x42a470.txt`, and every claim
in §5.4 survives, with one refinement: the keg light is extinguished as
`v_lite560->Message(0x14, 2.0f)` (a 2-second fade, not an immediate off), and the award is
`push 0x186a0; push caller; call 0x423310` = **`AddScoreAt(caller, 100000)`** —
100,000 confirmed from the instruction stream. **[C]**

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

Skulduggery factors the writes into one-line setters, which makes the state space
enumerable without the dispatcher. Each does *"set the wheel light, set the register,
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

### 2.3 Start bonus and time limit **[C]** for the values present

| State | Mode | Start bonus | Time limit (s) | Sweep id |
|---|---|---|---|---|
| 4 | Attack Ship | 200,000 | **[U]** (float lost) | **[U]** |
| 5 | Bermuda Triangle | 200,000 | **31.14** | 8.0 |
| 6 | Storm | 300,000 | **24.14** | 9.0 |
| 7 | Tavern | 200,000 | **27.40** | 7.0 |
| 8 | Treasure Hunt | 300,000 | **30.68** | `FUN_00424580()` → 1…6 |
| 9 | Mutiny | 100,000 | **[U]** | **[U]** |
| 10 | Sword Fight | 250,000 | **[U]** | **[U]** |
| 0xb | Peg Leg's Loot | 1,000,000 | **[U]** | — (none) |
| 0x10 | drain bonus | — | 30.0 | — |

The four `[U]`s are the Ghidra "lost float argument" artifact described in
`dragon-scoring-handlers.md` §0.3: the call decompiles as
`mode_timer->Message(0x45)` with no second operand. Recoverable only by hand
disassembly.

> **Disagreement with the manual [C].** The manual says "**Starting a mode is worth
> 200,000 points**". The binary pays a **different amount per mode** — 100,000 (Mutiny)
> through 1,000,000 (Peg Leg's Loot) — and only three of the eight actually pay 200,000.

> **Resolves a manual gap [C].** "Mode time limits and the captive-ball extension amount"
> was listed as unknown. The extension is **+10.0 s** (§3.1) and four of the eight limits
> are now known; the odd values (31.14, 30.68, 27.40, 24.14) look like voice-wave lengths,
> same as Dragon. **[S]** for the *reason*.

### 2.4 Ending **[C]**

* **Timer expiry** — `0x46` with caller `== mode_timer` → the controller broadcasts
  `0x52`. There is **no restart window**: Skulduggery has no state-3-equivalent
  ten-second grace period and no `0x51` code anywhere in the rules code. That is a real
  structural difference from Dragon's Keep.
* **`0x52` = teardown**, and it is where "completion" is booked. Every one of the eight
  controllers' `0x52` does: `FUN_0042ab40()` (restore the drop banks and GOLD lights from
  their bitmasks), **`v_lite606->MessageField += 1`**, lower `a_bloce`, restore
  `v_lite504`/`v_lite505`.
* **`0x53` = abort** — sent by `Pirates_drain_Control` on drain. Every controller's `0x53`
  cancels `mode_timer` (`0x45, -1.0`) and falls into `0x52`.
* **Objective met** — the mode's own code broadcasts `0x53` (Storm's hideaway bail-out) or
  runs the payout and lets the timer run down.

> **Same caveat as Dragon, and it is worse here [C].** `v_lite606->MessageField`
> ("modes completed") is incremented in the **shared `0x52`**, which is reached on timer
> expiry *and* on drain-abort *and* on success. So a mode counts as "completed" — for the
> 200,000/mode drain bonus **and** for the 1,000,000/mode Peg Leg's Loot payout — merely
> by having been started. No success/failure discriminator exists in any of the eight
> controllers. **[C]** for the mechanism, **[U]** for whether that is intended.

### 2.5 FT-only message codes in this subsystem

| Code | Meaning | Evidence |
|---|---|---|
| `0x4f` | mode changed / start (`ControlMissionComplete` slot) | every setter |
| `0x50` | tick / mode started (`ControlMissionStarted`) | every controller |
| `0x52` | **teardown** | ″ |
| `0x53` | **abort** (drain) | `Pirates_drain_Control` |
| `0x56` | ramp traversal complete | `s_rexit8` (ship wire ramp) |
| `0x4b` / `0x4c` | **magnet / captive device enable, disable** | `s_magnet1`…`6`, `a_kout1`, `a_kout2` |
| `0x4d` | sent to the previous scoring component (no-op) | `FUN_004232c0`/`00423310`/`00423350` |
| `0x1c` | light-group **rotate** | flippers (§6) |
| `0x2d` | light-group "light the next one" | coin, map-piece groups |
| `0x39` | blocker *restart timeout* (Dragon's `0x3a` neighbour) | `a_blocc/e/f/g` everywhere |
| `1` | flipper-button notification | `a_flip1`/`a_flip2`, `FUN_0042b820` |

`0x4b`/`0x4c` have no Space Cadet counterpart and no Dragon use; "enable/disable the
holding field" is inferred from behaviour (`0x4b` at mode start, `0x4c` on capture and at
teardown). **[P]** for the names, **[C]** for the semantics.

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

### States 0xc / 0xd — Lava-Ball and Jail Break **[C] values, [U] behaviour**

* **0xc = Lava-Ball**, set by `FUN_00427540` when the volcano bitmask
  `a_spot11->MessageField & 0xf == 0xf` (all four volcano targets). ✓ manual.
* **0xd = Jail Break**, set by `Pirates_a_spot6_Control` (brig target, 3rd hit, state 2)
  **and** by `FUN_00424210` when the brig count `DAT_00435d3c` reaches **3**.
  ✓ manual "3 hits … or automatically when three balls are caught in the brig" — but
  **[C] the binary does not require a ball in the brig for the 3-hit path**; three hits
  in state 2 start it regardless.

Neither has a controller in the decompiled set. Their only visible effects are per-component
gates: `s_hole19` (the brig) and `s_oneway7` (the brig release) both **exclude** states
0xc…0xd, `s_trip2` treats 0xc/0xd like state 2, the drop banks route through
`FUN_00425e30` in state 0xc (completing SKULDUGGERY there awards a **map piece** instead
of starting a mode), and `a_sink8`'s sound is suppressed in state 0xc. Ball additions for
the two multiballs were **not located** — the only writers of `TableG+0x192` are the drain,
the bonus controller, `FUN_00424210` (brig capture, `-1`) and `s_oneway7` (brig release,
`+1`). Jail Break is therefore plausibly "release what the brig holds" rather than
"`balls += n`" **[P]**; Lava-Ball's ball logic is **[U]** and most likely inside the
missing `FUN_0042d850`.

---

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

> **[U]** Nothing I found sets **bit 8** (`v_lite527`, the "D"). Only three hideaways exist
> and only three bits are ever OR-ed in. Either a fourth source is in the missing
> dispatcher, or `v_lite527` is lit by a path I did not locate — in which case GOLD can
> never be spelled by hideaways alone. Flagged as the single most suspicious loose end.

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
  is right for this table.)
* **Brig** — `s_hole19` (`0x42` ball captured, string 3007 "In The Brig") →
  `FUN_00424210`: `TableG+0x192 -= 1`, `DAT_00435d3c += 1`; at 2 enable `s_oneway12`; at
  **3** start Jail Break. `s_oneway7` is the release: `DAT_00435d3c -= 1`,
  `TableG+0x192 += 1`, and when empty disarm `a_kick1`. **[C]**

### 6.1 Drain bonus — states 0x10 / 0x11, `FUN_0042b820` **[C]**

`DAT_00435d38` = step, `DAT_00435d00` = accumulator, `DAT_00435d34` = "skip the display"
(set when a flipper press fast-forwards it, message `1`). One line per step:

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
| 5 | Jail Break needs "3 hits with ≥1 ball in the brig" | Three brig-target hits in state 2 start it with **no ball-in-brig check** | [C] |
| 6 | "7 mode lights … plus Peg Leg's Loot" | 8 lights `v_lite552`…`v_lite559`; the first **seven** are the random-selection pool, the 8th is reachable only via all six map pieces | [C] |
| 7 | Six wind lanes | **Eight** wind light groups are enumerated by the engine; "Tradewinds" covers two, and the orphan string 3006 "Port O' Call" is plausibly the eighth | [P] |
| 8 | Jackpot "+20,000 per target, +40,000 per sink, +750,000 per mode completion" | Only two jackpot contributions are visible in the rules code: **+50,000** (Storm, type 1017) and **+20,000** (Treasure Hunt, type 1017). The rest must be in the missing `FUN_0042d850` | [U] |
| 9 | Tavern/ship "+50,000 / +25,000 jackpot per target" | Those amounts exist as **scores on bank completion**, not as jackpot contributions | [P] |
| 10 | Plunger "15 s re-launch" | The rules code serves with a **0.96 s** delay; no 15 s constant | [P] |
| 11 | (nothing) | **Mode completion is not checked** — `v_lite606->MessageField` is bumped by the shared `0x52`, so timing out or draining counts as completing, for both the 200,000/mode bonus and the 1,000,000/mode finale | [C] |
| 12 | (nothing) | A **7th+ special treasure** keeps paying 1,500,000 and keeps incrementing the counter, unbounded | [C] |
| 13 | (nothing) | **Bug**: the second buried special treasure is read from the wrong slot (`DAT_00435d18` instead of `DAT_00435d1c`), so it always pays the Crown of Sandige | [C] |
| 14 | "hint strings 3060–3067" | Storm's hint is **3067**, not 3064 — the hint block is *not* parallel to the mode-selected block 3068–3075 | [C] |
| 15 | Magnets "active during Storm, Bermuda Triangle and Tavern" | Only Tavern uses the type-1042 `s_magnet*`; Bermuda's and Storm's magnets are the kickouts `a_kout1`/`a_kout2`, and they pay **1,000,000** against the keg magnets' 100,000 | [C] |

Structural differences from Dragon's Keep worth noting for a port: **no restart window and
no `0x51` code**; **no super jackpot**; **no plunger**; drop-target banks and letter sets
stored as **bitmasks** rather than as lit lights; a **per-mode** start bonus; and the mode
order is a **permutation** (`DAT_0043d0f0`) rather than the identity.

---

## 9. Open items

1. **`FUN_0042d850` is not decompiled.** It is the dispatcher and the global accrual
   point. Everything about the jackpot contribution table (manual: +20,000/target,
   +40,000/sink, +750,000/mode) and the ball handling of **Lava-Ball** and **Jail Break**
   most likely lives there. This is the single highest-value thing to decompile next.
   **[U]**
2. **No controller for states 0xc / 0xd.** See §3. Their per-component gates are mapped;
   their ball accounting is not. **[U]**
3. **GOLD bit 8 (`v_lite527`) has no writer** that I could find. §4.2. **[U]**
4. **Four of eight mode time limits lost** to the Ghidra float-argument artifact
   (Attack Ship, Mutiny, Sword Fight, Peg Leg's Loot), along with their light-sweep ids.
   Recoverable by hand disassembly. **[U]**
5. `Pirates_a_sink4_Control` (0x4283f0) decompiles into stack-byte noise in three of its
   five arms (the same class of failure as Dragon's `a_sink18`). The ship-sink constants in
   states 9 and 0xb should be re-read before being trusted. **[P]**
6. `DAT_00435d04` is shared by three different cursors — the Treasure Hunt "X" selector
   (`FUN_00424580`), the Bermuda indicated-wind selector (`FUN_00425110`/`FUN_00425230`),
   and the Sword Fight award selector (`FUN_00425440`) — and `FUN_0042aad0` zeroes it at
   every mode start. Whether that aliasing is intentional (one "wheel position" concept) or
   a bug is **[U]**.
7. String 3003 (`0xbbb`) is displayed by the magnets, the ship sink, the ship wire ramp,
   the coin award and Peg Leg's Loot alike — it is almost certainly a score/format line
   rather than a name, consumed by the unlocated `FUN_0042dfaf`. **[P]**
8. `a_kout2` is armed by Storm and `a_kout1` by Bermuda, but both share one handler that
   checks neither the mode nor which kickout fired. A ball caught by the *other* one
   outside its mode would still pay 1,000,000 — assuming it can be caught while disarmed,
   which depends on `0x4b`/`0x4c` semantics inside the component. **[U]**
