# Dragon's Keep — simple scoring handlers

Scope: `0x424bc0` (bumpers), `0x424cc0` / `0x424cf0` (rebounders) and the 18 spot-target
handlers. Written against `decomp/dragon/functions/*.c`, cross-checked against
`decomp/cadet/functions/*.c` + upstream `control.cpp`, and against
`docs/dragon-rules-spec.md` (manual). **The binary is authoritative.**

---

## 0. Enabling work (read this first — it is what makes the rest readable)

### 0.1 A component-tag table exists in `DRAGON.EXE`, so every `DAT_` is nameable

`.data` holds an array of 16/24-byte records of the shape

```c
struct component_tag {        // 16 or 24 bytes, name field padded to 8/16
    char  Name[8|16];         // "a_bump1\0", "v_lite120\0", ...
    char* NamePtr;            // points at Name (same record)
    TPinballComponent* Component;   // 0 in the file, filled at link_components time
};
```

i.e. this is exactly upstream's `component_tag<T>` with the name stored inline. Given a
`DAT_XXXXXXXX` that the decompilation treats as a component pointer, the tag name is the
string pointed to by the dword at `XXXXXXXX - 4`. I extracted all **229** tags this way;
every `DAT_` in the handlers below resolves, and the resolutions are self-consistent
(e.g. the bumper handler's counter lives in `DAT_00439c74` = `a_bump1`, and the
rebounder's light `DAT_00439ce4` = `v_lite120`, mirroring upstream's `lite84`).

Field offsets, confirmed by CADET↔upstream pairs:

| offset | field | evidence |
|---|---|---|
| `+0x06` | `TPinballComponent::MessageField` | `BoosterTargetControl`: `*(param_2+6)` ↔ `caller->MessageField` |
| `+0x42`,`+0x46`,`+0x4e` | the three flags `TLight::light_on()` ORs | `BoosterTargetControl`: triple `==0` test ↔ `if (lite61->light_on())` |
| `+0x76` | `TBumper::BmpIndex` | `BumperControl`: `get_scoring(*(param_2+0x76))` |
| vtable `+0x04` | `Message(code, float)` | ubiquitous |
| vtable `+0x0c` | `get_scoring(int)` | `BumperControl`, `RebounderControl` |
| vtable `+0x18` | `TSound::Play()` | every `tabcNNwave` call site |

### 0.2 The "+10" message-code rule only holds for the `Control*` block

Pairing CADET handlers against their upstream originals gives a **piecewise** offset, not
a constant one. Verified pairs:

| Full Tilt code | upstream enum | upstream value | delta |
|---|---|---|---|
| `7` | `TLightFlasherStartTimed` | 7 | 0 |
| `9` | `TLightTurnOnTimed` | 9 | 0 |
| `0x0f` | `TLightFlasherStartTimedThenStayOn` | 15 | 0 |
| `0x10` | `TLightFlasherStartTimedThenStayOff` | 16 | 0 |
| `0x13` | `TLightResetAndTurnOn` | 19 | 0 |
| `0x14` | `TLightResetAndTurnOff` | 20 | 0 |
| `0x26` | `TLightGroupReset` | 34 | +4 |
| `0x29` | `TLightGroupGetOnCount` | 37 | +4 |
| `0x31` | `TLightGroupToggleSplitIndex` | 45 | +4 |
| `0x35` | `TSoloTargetDisable` / `TPopupTargetDisable` | 49 | +4 |
| `0x36` | `TSoloTargetEnable` / `TPopupTargetEnable` | 50 | +4 |
| `0x37` | `TBlockerDisable` | 51 | +4 |
| `0x38` | `TBlockerEnable` | 52 | +4 |
| `0x3e` | `TGateDisable` | 53 | +9 |
| `0x3f` | `TGateEnable` | 54 | +9 |
| `0x40` | `TKickoutRestartTimer` | 55 | +9 |
| `0x41` | `TSinkResetTimer` | 56 | +9 |
| `0x45` | `TBlockerRestartTimeout` | 59 | +10 |
| `0x46` | `ControlTimerExpired` | 60 | +10 |
| `0x4a` | `ControlCollision` | 63 | +11 |

So: `TLight` codes are identical, the `TLightGroup`/target/blocker block is **+4**, the
gate/kickout/sink block is **+9**, and only the `Control*` block is **+10/+11**. The
briefing's blanket "+10" is wrong outside that last block; the four unused FT codes
`0x39`–`0x3d` (57–61) are FT-only slots, one of which Dragon uses (`0x3a`, §6).
`0x37`/`0x38` landing on `TBlockerDisable`/`Enable` is independently confirmed by CADET's
`DrainBallBlockerControl`, and `0x35`/`0x36` are consistent with the DAT saying every
`a_spot*` is a `TSoloTarget` (1019) — upstream's `TSoloTargetDisable/Enable` and
`TPopupTargetDisable/Enable` share the values 49/50, so I use the `TSoloTarget` spelling.

### 0.3 Ghidra artifacts that appear in every handler below

1. **`int *unaff_retaddr` is the `caller` parameter.** These handlers are
   `void (*)(int code, TPinballComponent* caller)` but when the body only forwards
   `caller` to a callee, Ghidra loses the second stack argument and invents
   `unaff_retaddr`. Read every `unaff_retaddr` as `caller`. Confirmed by CADET
   (`FlipperRebounderControl1` has the identical artifact and the original takes `caller`).
2. **Stack-argument shuffling around the last `Message()` in a block.** e.g. in
   `FUN_004250e0`: `(*lite10+4)(0x10);` (missing the `1.0f`) immediately followed by
   `FUN_004230a0((int*)0x3f800000, 1);` (the `1.0f` arrived as the component pointer).
   Likewise `FUN_004252b0` ends with `FUN_004230a0((int*)0x0, 1)` and `0x4254a0` has
   `piVar1 = (int*)0x35; ... FUN_004252b0(piVar1);`. In each case the real code is
   `light->Message(code, 1.0f); AddScoreFor(caller, 1);`.
3. **Collapsed `light_on()` tests.** `FUN_00424d20`, `FUN_004250e0`, `FUN_004252b0`,
   `FUN_004244e0`, `FUN_00423330` all show the clean three-field test. In `a_spot19`,
   `a_spot26` and `a_spot28` Ghidra emitted only two of the three sub-tests
   (`if (+0x46 == 0) { if (+0x42 == 0) return; } else { body }`), leaving the body
   reachable only via `+0x46 != 0`. I model all of them as `light_on()`; **the exact
   predicate for those three is a flagged uncertainty**, though `light_on()` is near
   certain from context (the light is the "this sword hit is armed" indicator).

### 0.4 Shared helpers

```c
FUN_00419f10(TableG, score)            // TPinballTable::AddScore(score)
FUN_00409730(id)                       // pb::get_rc_string(id)  -- id IS the resource id
FUN_0041de40(box, str, t, a, b)        // TTextBox::Display(str, t)   (a/b unidentified)
FUN_0042c020(code, cmp)               // global message broadcast
FUN_00423060(code, cmp)               // broadcast + invoke cmp's own control handler
```

`TPinballTable` field offsets, anchored on `FUN_004238a0` (`*(T+0x172) += n`, clamped to
`10000000` — upstream's `AddScore` uses exactly that clamp for `JackpotScore` in
FullTilt mode):

| offset | field |
|---|---|
| `0x15e` | `ScoreMultiplier` |
| `0x162` | `ScoreAdded` |
| `0x166` | `ReflexShotScore` |
| `0x16a` | `BonusScore` |
| `0x16e` | `BonusScoreFlag` |
| `0x172` | `JackpotScore` |
| `0x176` | `JackpotScoreFlag` |
| `0x18e` | *(extra-ball / shoot-again flag — see §6, offset chain unverified past `0x176`)* |

Three scoring wrappers, all of which maintain a file-static "component that scored last"
(`DAT_00434d20`, cleared on ball launch in `s_plunger1`'s handler at `0x4245b0`):

```cpp
// FUN_004230a0 — the workhorse. "score this component's scoring[index]".
static TPinballComponent* LastScoringComp = nullptr;      // DAT_00434d20
void control::AddScoreFor(TPinballComponent* caller, int index)
{
    if (LastScoringComp != caller)
    {
        if (LastScoringComp)
            // UNRESOLVED code 0x4d (=77). No Dragon handler and no TLight/TBumper/
            // TSoloTarget Message() case accepts it, so it is a no-op for every
            // component type that actually reaches here. Safe to drop.
            SendToComponentAndHandler(/*0x4d*/ MessageCode::FtUnknown77, LastScoringComp);
        LastScoringComp = caller;
    }
    TableG->AddScore(caller->get_scoring(index));
}

// FUN_004230f0 — same, but with a literal score instead of a scoring-table lookup.
void control::AddScoreAt(TPinballComponent* caller, int score) { /* ...; TableG->AddScore(score); */ }

// FUN_00423130 — upstream's control::SpecialAddScore, with ONE difference:
//   upstream sets ScoreMultiplier = 0, Dragon sets it to 1.
void control::DragonSpecialAddScore(TPinballComponent* caller, int score)
{
    const auto bonus = TableG->BonusScoreFlag;
    const auto jackpot = TableG->JackpotScoreFlag;
    const auto mult = TableG->ScoreMultiplier;
    TableG->BonusScoreFlag = false;
    TableG->JackpotScoreFlag = false;
    TableG->ScoreMultiplier = 1;              // <-- not 0
    TableG->AddScore(score);
    TableG->BonusScoreFlag = bonus;
    TableG->JackpotScoreFlag = jackpot;
    TableG->ScoreMultiplier = mult;
}
```

> **Finding with implementation impact.** `FUN_00423330` (the bumper combo award, §1)
> sets `ScoreMultiplier` to **2, 3, 4, 5** for the four weapon lights, and the manual
> says weapons give **2× / 3× / 4× / 5×**. Combined with the neutral index being **1**,
> Full Tilt's `score_multipliers` must be `{1, 1, 2, 3, 4, 5}` (multiplier == index,
> index 0/1 == 1×) — **not** Space Cadet's `{1, 2, 3, 5, 10}`. A port must keep a
> per-table multiplier array.

### 0.5 `info_text_box->MessageField` is the table's mode register

`DAT_00439d1c` is the `info_text_box` tag (it is the first argument of every
`TTextBox::Display`). Its `MessageField` is read as a mode selector all over Dragon's
control code — the same trick upstream uses with `lite198->MessageField`. Values that
matter here, decoded by matching behaviour to the manual's quest table:

| value | mode | evidence |
|---|---|---|
| `2` | normal play, no quest running | gates every "advance a dragon letter light / select a quest / sword target" path |
| `4` | **Dragon Hoard** *(inferred)* | suppresses all normal target/bumper scoring **and** sound; manual: in Dragon Hoard targets pay gems and bumpers pay gold instead |
| `7` | **Dragon Pass** | `a_spot8`/`a_spot12` pay 100,000 + 250,000 jackpot — verbatim the manual's Dragon Pass gargoyle-target rule |
| `8` | **Wizard's Fury** | spell targets set the mystery award and display strings 3039–3043, verbatim the manual |
| `9` | **Slay Dragon** | set by `a_spot28` (3rd sword hit) and by `FUN_00423e90` when no quests remain |
| `0xf` | **Dragon Flurry** | set by `FUN_004244e0` when all six dragon-letter lights are lit |

### 0.6 Resolved: the "18 `a_spot*` vs 16 manual targets" open question (spec §6 item 9)

| manual target | `a_spot*` | light(s) |
|---|---|---|
| Tower Target Bank (3) | `a_spot3`, `a_spot2`, `a_spot1` | `v_lite8`, `v_lite9`, `v_lite10` |
| Spell Target Bank (5) | `a_spot4`, `a_spot5`, `a_spot6`, `a_spot7`, `a_spot22` | `v_lite3`…`v_lite7` |
| Gargoyle Target Bank (4) | `a_spot10`, `a_spot8`, `a_spot14`, `a_spot12` | `v_lite14`, `v_lite13`, `v_lite12`, `v_lite11` |
| Sword Target | `a_spot19`, `a_spot26`, `a_spot28` | `v_lite16`, `v_lite17`, `v_lite18` |
| Quest Selection Target | `a_spot27` | `v_lite21` (selection), `v_lite57` (extra ball) |
| Ricochet Targets (2) | `a_spot30`, `a_spot31` | `v_lite20`, `v_lite19` |

18 objects, 16 manual names: **the "Sword Target" is three separate `TSoloTarget`
objects, one per hit stage**, not one target with a hit counter. That is the whole
discrepancy.

Also resolved: the six dragon-letter lights are `v_lite22`…`v_lite27`
(`FUN_004244e0` tests exactly six), each bank lighting its own — spell bank → `v_lite23`,
gargoyle bank → `v_lite25`, tower bank → `v_lite27`. **The binary confirms the DAT's six
and refutes the manual's "five".** The weapon lights are `v_lite33`…`v_lite36`
(`FUN_00423330`), and the quest lights are `v_lite71`…`v_lite75` plus `v_lite76` for
Slay Dragon (`FUN_00423e90`).

---

## 1. `0x424bc0` — `a_bump1` … `a_bump4` (Forest Bumpers, 1,500) — worked example

**What it does.** On collision: play `tabc03wave`; if the hit bumper is `a_bump3` or
`a_bump4`, flash its companion light (`v_lite65` / `v_lite66`) for 0.5 s; score
`scoring[0]` = 1,500; then bump a counter kept in **`a_bump1->MessageField`** (shared by
all four bumpers, since all four tags route to the same handler and the handler always
addresses `a_bump1`'s tag) and, **on every 10th hit**, reset it to 0 and call
`FUN_00423330()` — which lights the next unlit weapon light `v_lite33`…`v_lite36`, sets
`ScoreMultiplier` to 2/3/4/5, awards 25,000/35,000/45,000/55,000 via
`DragonSpecialAddScore`, prints the weapon name and plays `tabc39wave`.

When the mode register is `4` (Dragon Hoard) the whole score-and-sound path is skipped —
only the light flash and the counter run.

**Closest CADET handler.** `control::BumperControl` for the scoring core, but CADET's is
a one-liner; the "accumulate hits in one component's `MessageField`, fire an award every
N" shape is CADET's `BumperGroupControl` / `control::AddRankProgress` pattern.

**State.** Reads `info_text_box->MessageField`; reads/writes `a_bump1->MessageField`;
writes `v_lite65`/`v_lite66`; writes `TableG->ScoreMultiplier` and `CurScore` (indirectly).

```cpp
void control::ForestBumperControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    const bool dragonHoard = info_text_box->MessageField == 4;   // mode 4: no score, no sound

    if (!dragonHoard)
        soundwave_tabc03->Play(caller, "ForestBumperControl");

    if (caller == bump3)
        lite65->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
    else if (caller == bump4)
        lite66->Message(MessageCode::TLightFlasherStartTimed, 0.5f);

    if (!dragonHoard)
        AddScoreFor(caller, 0);                                  // 1500

    if (++bump1->MessageField == 10)                             // manual says 8 -- see §9
    {
        bump1->MessageField = 0;
        AwardNextWeapon();                                       // FUN_00423330
    }
}
```

---

## 2. `0x424cc0` / `0x424cf0` — `a_rebo1` / `a_rebo2` (Rebounds, 1,000)

**What it does.** On collision: turn on the companion light for 0.1 s (`v_lite120` for
`a_rebo1`, `v_lite121` for `a_rebo2`) and score `scoring[0]` = 1,000. Nothing else — no
mode check, no sound.

**Closest CADET handler.** `control::FlipperRebounderControl1` / `...2`, statement for
statement: same `TLightTurnOnTimed, 0.1f` on a per-side light, same
`AddScore(get_scoring(0))`. The only Dragon difference is that the score goes through
`AddScoreFor` (the last-scorer tracker) instead of `TableG->AddScore` directly. Dragon's
value is 1,000 against Space Cadet's `control_rebo_score1 = 500`.

**State.** Writes `v_lite120`/`v_lite121`; `TableG->CurScore`; `LastScoringComp`.

```cpp
void control::FlipperRebounderControl1(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlCollision)
    {
        lite120->Message(MessageCode::TLightTurnOnTimed, 0.1f);
        AddScoreFor(caller, 0);                                  // 1000
    }
}

void control::FlipperRebounderControl2(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlCollision)
    {
        lite121->Message(MessageCode::TLightTurnOnTimed, 0.1f);
        AddScoreFor(caller, 0);                                  // 1000
    }
}
```

---

## 3. `0x424e60`, `0x424ee0`, `0x424f60`, `0x424fe0`, `0x425060` — Spell Target Bank (2,500)

`a_spot4` → `v_lite3`, `a_spot5` → `v_lite4`, `a_spot6` → `v_lite5`,
`a_spot7` → `v_lite6`, `a_spot22` → `v_lite7`.

**What it does.** Two completely separate behaviours selected on the mode register:

* **mode 8 (Wizard's Fury):** do *not* score. Play `tabc05wave`, write the target's
  ordinal (`a_spot4`→0 … `a_spot22`→4) into **`a_sink4->MessageField`** — `a_sink4` is
  the Well Sink, whose handler later pays the award — and display resource
  `3039 + ordinal` ("Mystery Bonus set to: …"). This is the manual's mystery-award
  re-arm, exactly: 0 = 1,000,000, 1 = Jackpot, 2 = Max weapons, 3 = Max armor,
  4 = Extra ball.
* **otherwise:** flash the companion light on-and-stay-on for 1.0 s, then run the shared
  bank routine `FUN_00424d20`.

`FUN_00424d20(caller)` — the spell-bank completion check:

* if **any** of `v_lite3`…`v_lite7` is off → if mode != 4, score `scoring[0]` = 2,500 and
  play `tabc05wave`;
* if **all five** are lit → flash all five off (`TLightFlasherStartTimedThenStayOff, 1.0`),
  and if mode == 2, light dragon-letter light `v_lite23` and, unless `FUN_004244e0`
  started Dragon Flurry, call `FUN_004237f0()` (cast a random spell — the manual's
  "All 5 hit → random spell cast"); outside mode 2 it calls `FUN_004237f0()` directly.

**`FUN_004244e0`** = "if `v_lite22`…`v_lite27` are *all* lit, set mode `0xf` and broadcast
`0x4f`" → start Dragon Flurry.

**Note / likely original bug:** on the completing hit, **no score at all is added** — the
all-lit branch awards nothing for the hit itself and the bank has only one scoring entry
(`n=1, 2500`). The manual lists no bank-completion bonus for the spell bank either, so
this is probably intended.

**Closest CADET handler.** `control::FuelSpotTargetControl` — per-target light, shared
score, then a group-completion test with a reset-and-announce branch. (CADET uses a
`TLightGroup` `GetOnCount` for the test; Dragon enumerates the five lights by hand, as
`BoosterTargetControl` does.)

**State.** Reads `info_text_box->MessageField`, `v_lite3`…`v_lite7`, `v_lite22`…`v_lite27`;
writes `v_lite3`…`v_lite7`, `v_lite23`, `a_sink4->MessageField`,
`info_text_box->MessageField`.

```cpp
// one per target; the only differences are the light, the ordinal and the string id
void control::SpellTargetControl4(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    if (info_text_box->MessageField == 8)            // Wizard's Fury: set the mystery award
    {
        well_sink->MessageField = 0;                 // a_sink4; 1,2,3,4 for spot5,6,7,22
        soundwave_tabc05->Play(caller, "SpellTargetControl");
        info_text_box->Display(pb::get_rc_string(3039), 2.0f);   // +ordinal
        return;
    }

    lite3->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
    SpellTargetBankHit(caller);
}

// FUN_00424d20
void control::SpellTargetBankHit(TPinballComponent* caller)
{
    const bool complete = lite3->light_on() && lite4->light_on() && lite5->light_on()
                       && lite6->light_on() && lite7->light_on();

    if (!complete)
    {
        if (info_text_box->MessageField != 4)        // not Dragon Hoard
        {
            AddScoreFor(caller, 0);                  // 2500
            soundwave_tabc05->Play(caller, "SpellTargetBankHit");
        }
        return;
    }

    for (auto* l : {lite3, lite4, lite5, lite6, lite7})
        l->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);

    if (info_text_box->MessageField == 2)
    {
        lite23->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);  // dragon letter
        if (!CheckDragonFlurry())                    // FUN_004244e0
            CastRandomSpell();                       // FUN_004237f0
    }
    else
    {
        CastRandomSpell();
    }
}
```

---

## 4. `0x425220`, `0x425250`, `0x425280` — Tower Target Bank (2,000 / 10,000)

`a_spot3` → `v_lite8`, `a_spot2` → `v_lite9`, `a_spot1` → `v_lite10`.

**What it does.** Each handler is two statements: flash the companion light
on-and-stay-on for 1.0 s, then `FUN_004250e0(caller)`.

`FUN_004250e0`:

* if any of `v_lite8`/`v_lite9`/`v_lite10` is off → if mode != 4, score `scoring[0]` =
  2,000 and play `tabc06wave`;
* if all three are lit → flash all three off, score `scoring[1]` = **10,000**, and if
  mode == 2 light dragon-letter light `v_lite27`; unless `FUN_004244e0` started Dragon
  Flurry, call `FUN_00423d50()` (flips the `a_dvtr*` diverter pairs and lights
  `v_lite48`/`v_lite49`/`v_lite50` — **the tower lock**), play `tabc07wave` and display
  resource **`0xbbb` = 3003**, which the spec independently identifies as
  *"Tower Lock Enabled"*. Finally, if mode != 4, play `tabc06wave`.

That triple confirmation (score 2,000/10,000, tower lock, string 3003) makes this bank
the most solidly pinned of the three.

**Closest CADET handler.** `control::BoosterTargetControl` — a three-target bank with a
per-target light, a per-hit score, an all-three completion award, an announcement string
and a bank reset.

**State.** Reads `info_text_box->MessageField`, `v_lite8`-`v_lite10`, `v_lite22`-`v_lite27`;
writes those lights, `v_lite27`, the diverter gates and `v_lite48`-`v_lite50`.

```cpp
void control::TowerTargetControl3(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlCollision)
    {
        lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
        TowerTargetBankHit(caller);
    }
}
// ...Control2 -> lite9, ...Control1 -> lite10

// FUN_004250e0
void control::TowerTargetBankHit(TPinballComponent* caller)
{
    const bool complete = lite8->light_on() && lite9->light_on() && lite10->light_on();

    if (!complete)
    {
        if (info_text_box->MessageField != 4)
        {
            AddScoreFor(caller, 0);                  // 2000
            soundwave_tabc06->Play(caller, "TowerTargetBankHit");
        }
        return;
    }

    lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    AddScoreFor(caller, 1);                          // 10000

    if (info_text_box->MessageField == 2)
    {
        lite27->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);  // dragon letter
        if (!CheckDragonFlurry())
        {
            EnableTowerLock();                       // FUN_00423d50
            soundwave_tabc07->Play(caller, "TowerTargetBankHit");
            info_text_box->Display(pb::get_rc_string(3003), 2.0f);   // "Tower Lock Enabled"
        }
    }
    if (info_text_box->MessageField != 4)
        soundwave_tabc06->Play(caller, "TowerTargetBankHit");
}
```

---

## 5. `0x4254a0`, `0x425500`, `0x425570`, `0x4255e0` — Gargoyle Target Bank (3,000 / 15,000)

`a_spot10` → `v_lite14`, `a_spot8` → `v_lite13`, `a_spot14` → `v_lite12`,
`a_spot12` → `v_lite11`.

Two of the four are the manual's **rock-mounted** targets, and the binary says exactly
which: `a_spot10` (rock `a_rocka`, rock light `v_lite45`) and `a_spot14` (rock `a_rockb`,
rock light `v_lite46`).

### 5.1 `0x4254a0` — `a_spot10` (rock A)

On collision: `v_lite45->Message(TLightFlasherStartTimed, -1.0f)` (negative time =
indefinite), `a_rocka->Message(TBlockerDisable, 0)` — the rock slides in, opening the
passage to the kickout behind it — `a_spot10->Message(TSoloTargetDisable, 0)` (the target
itself goes out of play), `v_lite14` flash-then-stay-on 1.0 s, then
`FUN_004252b0(caller)`.

### 5.2 `0x425570` — `a_spot14` (rock B)

Identical, with `v_lite46` / `a_rockb` / `v_lite12`, **except** that the rock light is lit
**only when mode == 2**, where `a_spot10` lights `v_lite45` unconditionally. Also
`a_sword` is untouched here. I believe this asymmetry is real (it is a clean
`cmp`/branch, not a decompiler artifact), but I cannot rule out that `a_spot10` is the
one that is missing a mode test in the original. **Flagged.**

### 5.3 `0x425500` / `0x4255e0` — `a_spot8` / `a_spot12` (fixed targets)

```
if (mode != 7)  { light->Message(FlasherStartTimedThenStayOn, 1.0); GargoyleBankHit(caller); }
else            { light->Message(FlasherStartTimed, 0.5);
                  TableG->JackpotScore += 250000;          // FUN_004238a0
                  AddScoreAt(caller, 100000); }            // FUN_004230f0
```

Mode 7 is Dragon Pass, and **100,000 score + 250,000 to the jackpot** is verbatim the
manual's Dragon Pass gargoyle rule. Note `AddScoreAt` goes through `TableG->AddScore`
normally, so the 100,000 **is** multiplied by the weapon multiplier.

### 5.4 `FUN_004252b0` — the shared bank routine

* if any of `v_lite14`/`v_lite13`/`v_lite12`/`v_lite11` is off → if mode != 4, play
  `tabc08wave` and score `scoring[0]` = 3,000;
* if all four are lit → flash all four off; flip both diverter pairs
  (`a_dvtrba` disable + `a_dvtrbb` enable, `a_dvtrca` disable + `a_dvtrcb` enable); light
  `v_lite56` and `v_lite55`; reset `v_lite46` off and re-enable blocker `a_rockb`; reset
  `v_lite45` off and re-enable blocker `a_rocka` (i.e. both rocks slide back out); if
  mode == 2, light dragon-letter light `v_lite25` and, unless Dragon Flurry just started,
  display resource **`0xbbc` = 3004** (the spec's *"Pass Clear"*); then if mode != 4, play
  `tabc08wave` and score `scoring[1]` = **15,000**.

**Closest CADET handler.** `control::BoosterTargetControl` again for the bank shape;
the "disable the target you just hit and move a blocker" half has no CADET analogue —
its closest cousin is `DrainBallBlockerControl`'s use of `TBlockerDisable/Enable`.

```cpp
void control::GargoyleRockTargetAControl(MessageCode code, TPinballComponent* caller)  // 0x4254a0
{
    if (code != MessageCode::ControlCollision)
        return;
    lite45->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
    rockA->Message(MessageCode::TBlockerDisable, 0.0);            // rock slides in
    target_spot10->Message(MessageCode::TSoloTargetDisable, 0.0);
    lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
    GargoyleTargetBankHit(caller);
}

void control::GargoyleRockTargetBControl(MessageCode code, TPinballComponent* caller)  // 0x425570
{
    if (code != MessageCode::ControlCollision)
        return;
    if (info_text_box->MessageField == 2)                         // NB: asymmetric vs rock A
        lite46->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
    rockB->Message(MessageCode::TBlockerDisable, 0.0);
    target_spot14->Message(MessageCode::TSoloTargetDisable, 0.0);
    lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
    GargoyleTargetBankHit(caller);
}

void control::GargoyleTargetControl8(MessageCode code, TPinballComponent* caller)      // 0x425500
{
    if (code != MessageCode::ControlCollision)
        return;
    if (info_text_box->MessageField != 7)                         // not Dragon Pass
    {
        lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
        GargoyleTargetBankHit(caller);
        return;
    }
    lite13->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
    AddToJackpot(250000);                                         // FUN_004238a0
    AddScoreAt(caller, 100000);
}
// ...Control12 (0x4255e0) is identical with lite11

// FUN_004252b0
void control::GargoyleTargetBankHit(TPinballComponent* caller)
{
    const bool complete = lite14->light_on() && lite13->light_on()
                       && lite12->light_on() && lite11->light_on();
    const bool dragonHoard = info_text_box->MessageField == 4;

    if (!complete)
    {
        if (!dragonHoard)
        {
            soundwave_tabc08->Play(caller, "GargoyleTargetBankHit");
            AddScoreFor(caller, 0);                               // 3000
        }
        return;
    }

    for (auto* l : {lite14, lite13, lite12, lite11})
        l->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);

    dvtrBA->Message(MessageCode::TGateDisable, 0.0);
    dvtrBB->Message(MessageCode::TGateEnable, 0.0);
    dvtrCA->Message(MessageCode::TGateDisable, 0.0);
    dvtrCB->Message(MessageCode::TGateEnable, 0.0);
    lite56->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
    lite55->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
    lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0);
    rockB->Message(MessageCode::TBlockerEnable, 0.0);             // rocks slide back out
    lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0);
    rockA->Message(MessageCode::TBlockerEnable, 0.0);

    bool flurry = false;
    if (info_text_box->MessageField == 2)
    {
        lite25->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
        flurry = CheckDragonFlurry();
    }
    if (!flurry)
        info_text_box->Display(pb::get_rc_string(3004), 2.0f);    // "Pass Clear"

    if (!dragonHoard)
    {
        soundwave_tabc08->Play(caller, "GargoyleTargetBankHit");
        AddScoreFor(caller, 1);                                   // 15000
    }
}
```

---

## 6. `0x425650` — `a_spot27` (Quest Selection Target, 5,000)

**What it does.** Branches on the **extra ball light `v_lite57`**:

* **`v_lite57` off (normal):** play `tabc96wave`; flash the quest-selection light
  `v_lite21` for 1.0 s; **if mode == 2**, call `FUN_00423e90()` to re-roll the selected
  quest and display its name (resources **3071–3075** for return values 4–8, matching the
  spec's per-quest string ids); then score `scoring[0]` = 5,000 in every case.
* **`v_lite57` lit:** turn `v_lite57` off and call `FUN_004231e0(5.0f)` — set
  `TableG+0x18e = 1`, play `tabc21wave` and display resource **`0xbc3` = 3011
  ("Extra Ball")** for 5 s. **No score, and no quest change.**

`FUN_00423e90()` is the quest selector: it clears the currently lit quest light
(`FUN_00423e50`), collects which of `v_lite71`…`v_lite75` are still unlit (ids 4…8),
picks one at random, stores its index in `a_rockb->MessageField` and its id in
`a_kout2->MessageField`, and lights it indefinitely. If **none** are unlit it instead
runs `SetSwordStage(-1)`, lights `v_lite76` indefinitely, sets mode = **9** and
broadcasts `0x4f` — i.e. **completing all five quests starts Slay Dragon**, exactly as
the manual says.

**Closest CADET handler.** `control::BoosterTargetControl` — same "single target with a
lit-light alternate award" structure; `control::SkillShotGate1Control` is the closer
match for the `if (light->light_on()) {...}` gating shape.

**State.** Reads `v_lite57`, `info_text_box->MessageField`, `v_lite71`…`v_lite75`;
writes `v_lite21`, `v_lite57`, `v_lite76`, `a_rockb->MessageField`,
`a_kout2->MessageField`, `info_text_box->MessageField`, `TableG+0x18e`.

```cpp
void control::QuestSelectTargetControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    if (lite57->light_on())                      // extra ball light
    {
        lite57->Message(MessageCode::TLightResetAndTurnOff, 0.0);
        AwardExtraBall(5.0f);                    // FUN_004231e0 -> string 3011 "Extra Ball"
        return;                                  // NB: no 5,000 and no quest change
    }

    soundwave_tabc96->Play(caller, "QuestSelectTargetControl");
    lite21->Message(MessageCode::TLightFlasherStartTimed, 1.0f);

    if (info_text_box->MessageField == 2)
    {
        const int quest = SelectNextQuest();     // FUN_00423e90 -> 4..8, or 9 = Slay Dragon
        if (quest >= 4 && quest <= 8)
            info_text_box->Display(pb::get_rc_string(3071 + quest - 4), 2.0f);
    }

    AddScoreFor(caller, 0);                      // 5000
}
```

---

## 7. `0x425750`, `0x4257c0`, `0x425830` — the Sword Target (20,000 / 50,000 / Slay Dragon)

Three `TSoloTarget` objects, one per sword-push stage, each with its own arrow light:
`a_spot19`/`v_lite16`, `a_spot26`/`v_lite17`, `a_spot28`/`v_lite18`. All three require
**mode == 2** and their own light to be lit; otherwise the hit does nothing at all.

The stage machine is `FUN_004238d0(stage)`, which for each stage enables exactly one of
the three targets (`TSoloTargetEnable` = `0x36`) and disables the other two
(`TSoloTargetDisable` = `0x35`), sets the three arrow lights, and sends `a_sword`
(a `TBlocker`) either `0x3a` (with 0.0, or 1.0 at stage 2) or `TBlockerEnable` at stage 3:

| stage | live target | `v_lite16` | `v_lite17` | `v_lite18` | `a_sword` |
|---|---|---|---|---|---|
| 3 (entry) | `a_spot19` | flash ∞ | off | off | `TBlockerEnable` |
| 2 | `a_spot26` | on | flash ∞ | off | `0x3a, 1.0` |
| 1 | `a_spot28` | on | on | flash ∞ | `0x3a, 0.0` |
| 0 | `a_spot28` | flash ∞ | flash ∞ | flash ∞ | `0x3a, 0.0` |
| −1 | none | off | off | off | `0x3a, 0.0` |

So the chain is **stage 3 → hit `a_spot19` (20,000) → stage 2 → hit `a_spot26` (50,000)
→ stage 1 → hit `a_spot28` (no score) → stage −1 + Slay Dragon**. Stage 0 is never
reached from the handlers in scope.

`0x3a` (= 58) is one of the four FT-only codes in the `0x39`–`0x3d` gap and is
**unresolved**; it is `a_sword`'s "set pushed-in position" in all likelihood (it is the
only code that carries a value and the only one sent on every stage change), but I have
no CADET pair for it.

`a_spot28` additionally calls `FUN_00423e50()` (extinguish the currently lit quest light),
lights `v_lite76` indefinitely, sets mode = 9 and broadcasts `0x4f`.

**Closest CADET handler.** No single one. The "light-gated award then advance a staged
sequence" shape is `control::BoosterTargetControl`; the "set the mission register and
broadcast" tail is what CADET's mission handlers (`control::MissionControl` family) do.

**State.** Reads `info_text_box->MessageField`, `v_lite16`/`v_lite17`/`v_lite18`;
writes those lights, `a_spot19`/`a_spot26`/`a_spot28` enable state, `a_sword`,
`dragon_fire_light_group`, `v_lite76`, `info_text_box->MessageField`.

```cpp
void control::SwordTargetControl1(MessageCode code, TPinballComponent* caller)   // 0x425750
{
    if (code != MessageCode::ControlCollision || info_text_box->MessageField != 2)
        return;
    if (!lite16->light_on())                      // see §0.3 note 3
        return;

    info_text_box->Display(pb::get_rc_string(/* STR @ 0x439a58 */), 2.0f);
    SetSwordStage(2);                             // FUN_004238d0
    dragon_fire_lights->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
    AddScoreFor(caller, 0);                       // 20000
}

void control::SwordTargetControl2(MessageCode code, TPinballComponent* caller)   // 0x4257c0
{
    if (code != MessageCode::ControlCollision || info_text_box->MessageField != 2)
        return;
    if (!lite17->light_on())
        return;

    info_text_box->Display(pb::get_rc_string(/* STR @ 0x439a58 */), 2.0f);
    SetSwordStage(1);
    dragon_fire_lights->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
    AddScoreFor(caller, 0);                       // 50000
}

void control::SwordTargetControl3(MessageCode code, TPinballComponent* caller)   // 0x425830
{
    if (code != MessageCode::ControlCollision || info_text_box->MessageField != 2)
        return;
    if (!lite18->light_on())
        return;

    info_text_box->Display(pb::get_rc_string(/* STR @ 0x439a58 */), 2.0f);
    SetSwordStage(-1);
    ClearSelectedQuestLight();                    // FUN_00423e50
    lite76->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
    info_text_box->MessageField = 9;              // Slay Dragon
    BroadcastModeStarted();                       // FUN_0042c020(0x4f, nullptr)
                                                  // NB: no score for the third hit
}
```

---

## 8. `0x4258b0`, `0x4258e0` — `a_spot30`, `a_spot31` (Ricochet Targets, 2,500)

**What it does.** Two statements each: flash the arrow light (`v_lite20` for `a_spot30`,
`v_lite19` for `a_spot31`) for 2.0 s and score `scoring[0]` = 2,500. No mode check, no
sound, no state. The manual's "kick the ball away when hit" is a property of the
component (the `TSoloTarget`'s own collision response), not of the handler.

**Closest CADET handler.** `control::FlipperRebounderControl1` / `...2` — the same
"flash one light, add one score" pair; `control::RebounderControl` is the degenerate
version without the light.

```cpp
void control::RicochetTargetControl1(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlCollision)
    {
        lite20->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
        AddScoreFor(caller, 0);                   // 2500
    }
}

void control::RicochetTargetControl2(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlCollision)
    {
        lite19->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
        AddScoreFor(caller, 0);                   // 2500
    }
}
```

---

## 9. Manual vs binary

| # | Manual | Binary | Confidence |
|---|---|---|---|
| 1 | Forest bumpers award another weapon **every 8 hits** | counter fires at **10** (`if (++a_bump1->MessageField == 10)`) | **certain** — single compare against an immediate |
| 2 | Quest Selection Target: "**5,000 points.** If the extra ball light is lit, **also** awards an extra ball" | strictly **either/or**: lit → extra ball only, **no 5,000 and no quest re-roll**; unlit → quest re-roll + 5,000 | **certain** — the two paths are disjoint, the lit path returns before `AddScoreFor` |
| 3 | Dragon Pass: "**each gargoyle target** hit: 100,000 + 250,000 jackpot" | only `a_spot8` and `a_spot12` have the mode-7 path; the two **rock-mounted** targets (`a_spot10`, `a_spot14`) keep ordinary 3,000/15,000 bank behaviour in Dragon Pass | **certain** — `0x4254a0` and `0x425570` contain no mode-7 test |
| 4 | Dragon Letter Lights: "green circular lights spelling D-R-A-G-O-N. *Manual says five*" | `FUN_004244e0` tests exactly **six** (`v_lite22`…`v_lite27`) | **certain** — binary agrees with the DAT, manual is wrong |
| 5 | "16 named targets" vs 18 `a_spot*` DAT objects (spec open question 9) | the Sword Target is **three** separate `TSoloTarget` objects, one per hit; 3+5+4+3+1+2 = 18 | **certain** |
| 6 | Table multiplier 2×/3×/4×/5× from weapons | `FUN_00423330` sets `ScoreMultiplier` to 2/3/4/5 and the neutral index is 1 ⇒ FT's `score_multipliers` is `{1,1,2,3,4,5}`, **not** Space Cadet's `{1,2,3,5,10}` | **high** — follows from the manual's multipliers plus the raw indices; the array itself was not located in `.data` |
| 7 | Spell bank "all 5 hit → random spell cast" | confirmed, but the **completing hit scores nothing** (no bank-completion value exists; `n=1`) | **certain** |
| 8 | Tower bank "all 3 → 10,000, tower lock enabled" | confirmed down to the string: `0xbbb` = 3003 "Tower Lock Enabled" | **certain** |
| 9 | String 3004 "Pass Clear" = *Dragon Pass completion* | displayed on **gargoyle-bank completion**, in any mode (not only during Dragon Pass) | **certain** — `FUN_004252b0` |
| 10 | Rock-mounted gargoyle targets "slide the rock into the table" | confirmed: `TBlockerDisable` on `a_rocka`/`a_rockb`, re-enabled on bank completion. But the two are **asymmetric**: `a_spot10` lights `v_lite45` unconditionally, `a_spot14` lights `v_lite46` **only in mode 2** | **certain** that the asymmetry is in the binary; **unknown** whether it is a shipped bug |
| 11 | Ricochet targets 2,500; rebounds 1,000; spell 2,500; tower 2,000/10,000; gargoyle 3,000/15,000; sword 20,000/50,000; quest 5,000; bumpers 1,500 | all confirmed against `analysis/dragon-components.txt` and the handler's `get_scoring` indices | **certain** |

---

## 10. Still ambiguous

1. **Code `0x4d` (77)** sent by `AddScoreFor`/`AddScoreAt`/`DragonSpecialAddScore` to the
   previously-scoring component. No Dragon control handler has a `0x4d` case and no
   `TLight`/`TBumper`/`TSoloTarget` `Message()` case can accept it (FT `TLight` codes stop
   at 23), so it looks like a no-op for every component that reaches it. The tracker
   itself (`DAT_00434d20`, cleared on ball launch) is real; its purpose is not. A port can
   drop it — but if a later handler turns out to consume `0x4d`, this is where to look.
2. **Code `0x3a` (58)** sent to `a_sword` on every sword-stage change, with 1.0 at stage 2
   and 0.0 otherwise. One of the four FT-only codes in the 57–61 gap. No CADET pair.
3. **`TableG + 0x18e`**, set to 1 by the extra-ball award. The offset chain is anchored
   only up to `JackpotScoreFlag` (`0x176`); FT's `TPinballTable` almost certainly has
   extra fields after that, so I will not name it. Semantics ("extra ball / shoot again
   awarded") are certain from string 3011.
4. **The `light_on()` predicate in `a_spot19`/`a_spot26`/`a_spot28`.** Ghidra emitted only
   two of the three flag tests. `light_on()` is near certain from context but is not
   literally what the decompilation shows.
5. **`0x3e`/`0x3f` polarity.** Mapped to `TGateDisable`/`TGateEnable` from two positional
   CADET votes, and corroborated by the fact that Dragon always sends them to `a_dvtr??`
   pairs in opposite-code pairs. Note the DAT types the diverters as FT-only class
   **1043**, not `TGate`, so they presumably subclass it; which of the pair is "open" is
   not pinned down.
6. **Mode 4 = Dragon Hoard** is inferred from "suppresses all normal target and bumper
   scoring and sound" matching the manual's gem/gold substitution. Not proven; no handler
   in scope writes mode 4.
7. **The `a_spot10` / `a_spot14` rock-light asymmetry** (§9 row 10) — real in the binary,
   intent unknown.
8. `FUN_004237f0` (cast random spell), `FUN_00423d50` (tower lock), `FUN_0041de40`'s last
   two arguments and the inline string at `0x439a58` (the sword-hit announcement) were not
   analysed; they are outside this scope but are the right next targets.
