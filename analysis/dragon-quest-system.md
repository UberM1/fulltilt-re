# Dragon's Keep — quest / mode state machine (from `DRAGON.EXE`)

Derived purely from `decomp/dragon/functions/*.c` (Ghidra output), `binaries/win95/DRAGON.EXE`
(string table, `.data` tag structs, static pointer arrays) and the structural reference
`SpaceCadetPinball/control.cpp`. No Ghidra/rea runs.

Confidence legend: **[C] CONFIRMED** (read directly, cross-checked by ≥2 independent uses or by an
exact Cadet↔upstream correspondence) · **[P] PROBABLE** (one clean reading, plausible, not
cross-checked) · **[S] SPECULATIVE** · **[U] UNRESOLVED**.

---

## 0. Method note: how component globals were named

`analysis/dump_components.py` already showed that `score_components[]` entries point at
`component_tag_base { const char* Name; TPinballComponent* Component; }`. So **every**
`DAT_0043xxxx` that the rules code dereferences as a component is a `tag+4` slot, and its group
name is at `tag+0`. Scanning `.data`/`.rdata` for `{char* printable_name, X}` pairs recovers
252 name↔global bindings. **[C]** — validated two ways:

* on `CADET.EXE` the same scan yields `DAT_004384e4 = lite198`, `DAT_00438b44 = mission_text_box`,
  `DAT_00438004 = fuel_bargraph`, `DAT_00438234/0043824c = l_trek_lights/r_trek_lights` — exactly
  the components upstream's `control::MissionControl` uses, in the same roles;
* on `DRAGON.EXE` all 75 `score_components` tag addresses match the scan.

Table: `/tmp/.../wk/dragon-tags.txt` is reproducible with the 20-line script in §9.

Object-layout facts used throughout (FT object layout, **[C]** from identical use in CADET):
`+6` = `MessageField` (int), `+0x42 / +0x46 / +0x4e` = the three "light is on / flashing /
timed-on" flags (the idiom `(+0x42||+0x4e||+0x46) != 0` == "light is lit"), `+5` = an
enable/active byte.

Message codes actually observed (the "+10" rule holds only for the `Control*`/timer family;
`TLight*` codes are unshifted, `TLightGroup*` are +4):
`0x42` ball-captured, `0x44` ball-released, `0x45` timer-set (`TTimerResetTimer`), `0x46`
`ControlTimerExpired`, `0x47` `ControlNotifyTimerExpired`, `0x48` timer-query, `0x4a`
`ControlCollision`, `0x4f` `ControlMissionComplete`, `0x50` `ControlMissionStarted`,
`0x51/0x52/0x53/0x54/0x55/0x56` FT-only (see §3). `0x4f`/`0x50` are **[C]**: in CADET,
`FUN_0042a260` does `lite198->MessageField = 1; FUN_0042a260(0x4f, NULL)`, which is upstream's
`lite198->MessageField = 1; MissionControl(ControlMissionComplete, nullptr)` verbatim.

---

## 1. Where the state machine is

| Dragon | Role | Cadet | Upstream |
|---|---|---|---|
| `FUN_0042c020` @0x42c020 | `control::handler` tail: global jackpot scoring + dispatch | — | `control::handler` |
| `FUN_0042c120` @0x42c120 | **the mode dispatcher** (`switch` 0..0xf + default) | `FUN_0042a260` | `control::MissionControl` |
| `FUN_0042c000` @0x42c000 | default/unknown-state handler → forces state 2 | `FUN_0042a240` | `UnselectMissionController` |
| `FUN_00423060` @0x423060 | per-component `handler` front end | — | `control::handler` |

`FUN_0042c020(code, caller)` **[C]**:

1. if `caller == drain` and `code == 0x4a` and no multiball → `DAT_00439b14 = 1` (blocks quest
   re-selection while draining);
2. **global jackpot accrual** (matches spec §5.1 exactly):
   * `v_lite28` lit *and* caller type `0x3fb` (1019 `TSoloTarget`) → `FUN_00423870(100000)`
     (= +100,000 **super** jackpot) — this is the *Dragon Breath* spell;
   * caller type `0x3ee` (1006) or `0x3fb` (1019) → `FUN_004238a0(50000)` (+50,000 jackpot) **and**
     `DAT_00434d08++` (the "targets dropped" counter used by the drain bonus);
   * caller type `0x3f9` (1017 `TSink`) **and** caller ∈ {`a_sink4`, `a_sink1`} → +75,000 jackpot;
3. code remap: `0x3f1` (1009 `Resume`) → `0x50`; `0x46` from `info_text_box` with `+0x5e == 0` → `0x50`;
4. `FUN_0042c120(info_text_box->MessageField, code, caller)`.

Note on 2: upstream/manual say "sinks 75,000", but the binary restricts it to two sinks, and
"Dragon Breath +100,000" only fires for solo targets, not bumpers/rebounds/kickouts as the manual
claims. **[P]** (the two-sink comparison could be a Ghidra artifact; the `TSoloTarget`-only gate
is clear).

### 1.1 The quest/mode global

**`info_text_box->MessageField`** — i.e. `*(int*)(DAT_00439d1c + 6)`, group `info_text_box`
(Dragon's single `TTextBox`). **[C]**

This is the exact analogue of Space Cadet's `lite198->MessageField`: Cadet uses a dummy light as the
state cell, Dragon reuses its text box. It is the *only* input to the dispatcher and 40 writes to it
across the rules code are all mode transitions.

Supporting state cells (the FT rules code habitually stashes ints in component `MessageField`s):

| Cell | Meaning | Conf |
|---|---|---|
| `a_kout2->MessageField` | **selected / next-available quest index** (4…8; `0` = none) | [C] |
| `a_rockb->MessageField` | index 0…4 of that quest's Quest Light in `PTR_PTR_0043bb58` | [C] |
| `mode_timer->MessageField` | index of the quest that just ended (for the 10 s restart); `-1` = none | [C] |
| `v_lite28->MessageField` | **number of quests completed this game** (drain bonus + progression) | [C] |
| `v_lite88->MessageField` | *Dragon Hoard* gem bitmask, 9 bits, full = `0x1ff` | [C] |
| `v_lite91->MessageField` | *Fire Lizards* hit count, capped at 10 | [C] |
| `a_sink2->MessageField` | *Fire Lizards* 0-3 sub-counter (every 4th hit = jackpot) | [C] |
| `a_sink4->MessageField` | *Wizard's Fury* currently armed mystery award (0…4) | [C] |
| `v_lite114->MessageField` | *Slay Dragon* **hits left** (starts at 8) | [C] |
| `v_lite113->MessageField` | *Slay Dragon* fire-light sweep index (0…8) | [C] |
| `a_bump1->MessageField` | forest-bumper hit counter (0…9) | [C] |
| `v_lite48/49/50->MessageField` | tower-lock bookkeeping for prison/watch/gate | [P] |
| `dragon_mouth_light_group->MessageField` | dragon-death animation frame 0…9 | [C] |

### 1.2 Full state table (`FUN_0042c120`)

| State | Handler | Meaning | Evidence |
|---|---|---|---|
| 0 | `FUN_0042bf60` | **Game over / HIGH SCORE** (string 3065, `Reset` to table) | [C] |
| 1 | — (`return`) | between balls / balls being released; no-op | [C] |
| 2 | `FUN_00428800` | **Idle, no quest running** (string 3001 "Hit Fire Lizards To Begin Quest"); reset target | [C] |
| 3 | `FUN_00428870` | **Quest just ended — 10 s restart window** | [C] |
| 4 | `FUN_00428a80` | Quest **Dragon Hoard** | [C] |
| 5 | `FUN_004291c0` | Quest **Fire Lizards** | [C] |
| 6 | `FUN_00429510` | Quest **Rescue Maiden** | [C] |
| 7 | `FUN_00429b10` | Quest **Dragon Pass** | [C] |
| 8 | `FUN_00429ec0` | Quest **Wizard's Fury** | [C] |
| 9 | `FUN_0042a150` | Quest **Slay Dragon** (finale) | [C] |
| 0xa | `FUN_004285f0` | **Skill shot** ("Launch Ball", string 3066) | [C] |
| 0xb | `FUN_0042b7c0` | **Drain bonus countdown** / end-of-ball | [C] |
| 0xc | `FUN_0042ac80` | **Dragon-egg kickout exchange offer** (Yes=left flipper) | [C] |
| 0xd | `FUN_0042b0e0` | **Tower Barrage** (string 3051) — multiball, `balls += 3` | [C] |
| 0xe | `FUN_0042b3c0` | **Gargoyle Gaggle-Ball** (3062) — multiball, `balls += 1` | [C] |
| 0xf | `FUN_0042b510` | **Dragon Flurry** (3063) — multiball, `balls += 4` | [C] |
| other | `FUN_0042c000` | force state 2 + `0x4f` | [C] |

So Dragon's Keep has **6 quests and no ranks** — the manual is right. There is no rank array, no
`mission_select_scores` analogue, and nothing resembling upstream's `MissionRcArray[17]`. **[C]**

The quest index ↔ name mapping is nailed down three independent ways **[C]**:

* `FUN_0042ac80` maps `a_kout2->MessageField` 4→str 3057 "Dragon Hoard?", 5→3058 "Fire Lizards?",
  6→3059 "Rescue Maiden?", 7→3060 "Dragon Pass?", 8→3061 "Wizard's Fury?";
* `Dragon_a_spot27_Control` maps the same 4…8 → 3071…3075 "<name>\nQuest Selected";
* each state's own controller displays its own name string (3019/3030/3032/3035/3037/3047).

---

## 2. Quest lifecycle

### 2.1 Selection — `FUN_00423e90` (SelectNextQuest)

* Refuses while `DAT_00439b14 != 0` (ball draining).
* Calls `FUN_00423e50` to un-light any previous selection.
* Builds a candidate list from the **five Quest Lights that are not yet lit**:
  `v_lite71→quest 4`, `v_lite72→5`, `v_lite73→6`, `v_lite74→7`, `v_lite75→8`.
* Picks one **uniformly at random** (`FUN_0042c4cb` is the 15-bit RNG), stores
  `a_rockb->MessageField = lightIndex`, `a_kout2->MessageField = questIndex`, and flashes that
  Quest Light indefinitely.
* **If all five are already lit** → `FUN_004238d0(-1)` (sword lights off), flash `v_lite76`
  (the 6th Quest Light) and jump straight to **state 9 = Slay Dragon**.

`PTR_PTR_0043bb58` (static, read from the binary) = `{&v_lite71, &v_lite72, &v_lite73, &v_lite74,
&v_lite75}` tags. **[C]** → the six purple Quest Lights are `v_lite71…v_lite75` + `v_lite76`.
**[C]**

Called from: the plunger handler (new ball), `Dragon_a_spot27_Control` (**Quest Selection Target**
— only while state == 2), and `FUN_00424000`/`FUN_00423e90` chains. Note selection is *random*,
not cyclic. **[C]** — contradicts nothing in the manual but is worth knowing.

### 2.2 Starting — `FUN_00424210` (StartSelectedQuest)

```
if (a_kout2->MessageField != 0) { info_text_box->MessageField = a_kout2->MessageField;
                                  v_lite46 off; a_rockb enable 2.0s; FUN_0042c020(0x4f, NULL); }
```
Entry points **[C]**:

| Trigger | Path |
|---|---|
| **2 fire-lizard-sink hits** (`a_sink1`/`a_sink2`, counter in `v_lite63->MessageField`) | `FUN_00425c30` → `FUN_004244e0` (Dragon Flurry check) else `FUN_00424210` |
| **Well sink** `a_sink4`, state 2 | 1-in-4 random → `FUN_00424210`, else `FUN_004237f0` (cast random spell) |
| **Dragon-egg kickout** with 1–3 eggs, "Yes" | `FUN_0042ac80` → `FUN_00424210` |
| **Skill shot** none (the 5 skill shots award collectibles/modes instead) | — |

Each quest controller's `0x4f` (just-started) branch does the same four things **[C]**:
`light_sweeps` message `0x1b` with a per-quest id, `FUN_004230f0(0, 100000)`
(**+100,000 for starting a quest** — the manual's [GAP]; *Slay Dragon* gives **500,000**),
clear the text box, display its hint string, start its music/voice wave.
The **timer is not started until the light sweep finishes**: the controller's `0x46` branch with
caller type `0x40e` (1038 `light_sweeps`) calls `FUN_00428580` (common start: `FUN_00423b80` +
`dragon_letter_light_group` `0x19`), sets `mode_timer`, and sends `0x50`.

### 2.3 Time limits **[C]** (raw float constants)

| State | Quest | Limit (s) |
|---|---|---|
| 4 | Dragon Hoard | 27.73 |
| 5 | Fire Lizards | 31.69 |
| 6 | Rescue Maiden | 30.45 |
| 7 | Dragon Pass | 30.65 |
| 8 | Wizard's Fury | 30.69 |
| 9 | Slay Dragon | `30 + 5·(armor + defensive spells)`, see §5 |
| 3 | restart window | **10.0** |
| any | re-arm after a restart (`0x51`) | **15.0** |

The odd values are probably the lengths of the corresponding voice-over waves
(`tabc46/48/50/53/55wave`). **[S]** for the *reason*, **[C]** for the values.

### 2.4 Ending

* **Timer expiry** (`0x46`, caller `mode_timer`): the controller copies
  `mode_timer->MessageField = info_text_box->MessageField`, turns its own lights off, sets state 3
  and sends `0x4f`. State 3 (`FUN_00428870`) flashes the two fire-lizard lights `v_lite63/64` and
  arms `mode_timer` for **10.0 s**. If that 10 s elapses, state 3 sends **`0x52` (teardown) to the
  controller named by `mode_timer->MessageField`**, sets `mode_timer->MessageField = -1`, and
  returns to state 2. **[C]**
* **Restart**: during state 3, a hit on `a_sink1`/`a_sink2` restores
  `info_text_box->MessageField = mode_timer->MessageField` and sends **`0x51`** — the controller's
  `0x51` branch re-arms the lights and gives the quest **15.0 s**. **[C]** Matches the manual's
  ten-second restart rule exactly.
* **Objective met** (e.g. *Rescue Maiden* 4th prison light): the controller cancels `mode_timer`,
  runs its own `0x52` teardown inline, sets state 2 and sends `0x4f` — **no** restart window. **[C]**
* **Ball drained**: `Dragon_drain_Control` sends **`0x53`** → every controller's `0x53` branch sets
  state 2, cancels `mode_timer`, falls through into `0x52`. **[C]**

### 2.5 "Completion" bookkeeping — `FUN_00424000`

```
if (a_kout2->MessageField != 0) {
    FUN_004238a0(500000);                        // +500,000 to the jackpot
    v_lite28->MessageField += 1;                 // quests completed
    questLight[a_rockb->MessageField] -> 0x13;   // Quest Light solid ON
    a_kout2->MessageField = 0;
}
FUN_00423e90();                                  // pick the next quest
```
+500,000 to the jackpot per quest completion matches the manual **[C]**.

**Important caveat [P]:** `FUN_00424000` is called from the *shared* `0x52` teardown of every
quest, which is reached on **timer expiry as well as on success**. So as far as this code shows, a
quest's purple light is lit — and the quest counted as "completed" for both progression and the
100,000/quest drain bonus — whenever it is played to its end, success or not. I could not find any
success/failure discriminator. **[U]** on whether that is the real intent.

### 2.6 FT-only message codes in this subsystem **[C] unless noted**

| Code | Meaning | Where observed |
|---|---|---|
| `0x51` | quest **restart / re-arm** | sent by `a_sink1`, `a_sink2` |
| `0x52` | quest **teardown** (lights off, `FUN_004285a0`, `FUN_00424000`) | sent by state 3 and inline |
| `0x53` | **abort** (ball drained) | sent by `Dragon_drain_Control` |
| `0x54` / `0x55` | ramp **entered / exited** (`a_ramp107`, `s_rexit18/19`) | Slay Dragon uses it to defer timeout |
| `0x56` | ramp **traversal complete** | `a_ramp103` (dragon ramp), `a_ramp6` (tower ramp) |
| `1` | flipper-button notification (compared against `a_flip1`) | `FUN_0042ac80` Yes/No prompt **[P]** |

---

## 3. What each quest does

### State 4 — Dragon Hoard (`FUN_00428a80`) **[C]**

Hint 3020 "Steal Gems From Dragon". `gem_light_group` flashes. On collision, the gem is chosen by
*which component was hit*; each awards an unmultiplied score (`FUN_004230f0`), shows its name string,
lights its own gem light, and ORs a bit via `FUN_00428a20`:

| Component(s) | Type | Gem (string) | Score | Light | Bit |
|---|---|---|---|---|---|
| any bumper | 1005 | Gold (3021) | 10,000 | `v_lite88` | 0x001 |
| `a_spot4/5/6/7/22` (spell bank) | 1019 | Diamond (3022) | 50,000 | `v_lite89` | 0x002 |
| `a_spot1/2/3` (tower bank) | 1019 | Pearl (3023) | 60,000 | `v_lite90` | 0x004 |
| `a_ramp6` (tower ramp, `0x56`) | — | Emerald (3025) | 200,000 | `v_lite92` | 0x008 |
| `a_spot8/10/12/14` (gargoyle bank) | 1019 | Sapphire (3026) | 70,000 | `v_lite93` | 0x010 |
| `a_sink1/a_sink2` (fire lizards) | 1017 | Ruby (3024) | 100,000 | `v_lite91` | 0x020 |
| `s_oneway5` (dragon chute) | 1016 | — **awards the jackpot** | — | `v_lite95` | 0x040 |
| `a_ramp103` (dragon ramp, `0x56`) | — | Amethyst (3027) | 250,000 | `v_lite94` | 0x080 |
| `a_accl1` (moat chute) | 1036 | Opal (3028) | 100,000 | `v_lite117` | 0x100 |

Scores match the manual exactly; the manual's two gaps are closed: **Opal = moat chute (100,000)**
and the "9th gem light" in the DAT is the **dragon-chute/jackpot** light. `FUN_00428a20` also shows
that collecting **all nine** (`== 0x1ff`) awards an **extra ball** (`FUN_004231e0`) — not in the
manual. **[C]**

### State 5 — Fire Lizards (`FUN_004291c0` + `FUN_00425c30`) **[C]**

Hint 3031. Per fire-lizard-sink hit: `v_lite91->MessageField` increments, **capped at 10**, and the
award is `count × 100,000` (so 100,000 → 1,000,000 cap) with string 3029 "Fire Lizard Slain". A
separate `a_sink2->MessageField` cycles 0→1→2→3; on the 4th it awards the **jackpot**
(`FUN_00424490`) and resets. Exactly the manual.

### State 6 — Rescue Maiden (`FUN_00429510`) **[C] / [U]**

Hint 3033 "Put ball in Prison Tower". `prison_window_light_group` (4 lights) is driven by a 0.2 s
notify timer (`0x47`) that walks a single lit light around the four windows. Objective: entries to
`a_sink18` (prison tower) until the group's on-count reaches 4 → cancel timer, teardown, state 2.
`DAT_00434d14` saves/restores `v_lite48`'s prior state across the quest.
**[U]:** the per-hit awards (manual: 250,000 / 500,000 / jackpot / extra-ball light) live in
`Dragon_a_sink18_Control` @0x427100, which Ghidra decompiles into stack-byte garbage (lost float
arguments). The quest controller itself only detects the 4-light completion. Needs a cleaner
decompile or hand disassembly.

### State 7 — Dragon Pass (`FUN_00429b10`) **[C]**

Hint 3036. `gargoyle_spot_light_group` desaturation animation; `a_rocka`/`a_rockb` (the two sliding
rocks) are **forced open** (`0x37`) and `a_spot10`/`a_spot14` disabled, exposing the kickouts.
Awards live in the component handlers, keyed on `state == 7`:
* `Dragon_a_kout1_Control` (**armor kickout**) → `FUN_00424490` = **the jackpot** ✔ manual;
* `Dragon_a_kout2_Control` (**gargoyle kickout**) → `FUN_004232b0(1)` = **1 dragon egg** ✔ manual.
The manual's "gargoyle target: 100,000 + 250,000 to jackpot" was **not located**. **[U]**

### State 8 — Wizard's Fury (`FUN_00429ec0` + `Dragon_a_sink4_Control`) **[C]**

Hint 3038. `well_light_group` and `spell_spot_light_group` flash. Each **well** (`a_sink4`) entry
pays out the award currently armed in `a_sink4->MessageField` and then **re-randomises it**,
announcing the new one:

| `a_sink4->MessageField` | Award | Announce |
|---|---|---|
| 0 | Extra ball (`FUN_004231e0`) | 3039 |
| 1 | Max weapons: all 4 weapon lights on, multiplier = 5 (3045) | 3040 |
| 2 | Max armor: shield + center post + `a_bloce` (3044) | 3041 |
| 3 | Jackpot (`FUN_00424490`) | 3042 |
| 4 | 1,000,000 (3046) | 3043 |

**Disagreement with the manual [C]:** the mystery award is **re-rolled randomly by the well
itself**, uniformly over the other four; the spell-target handler `FUN_00424d20` has **no**
`state == 8` case, so "hitting a spell target sets the mystery award" is not implemented. The
manual's fixed per-target mapping does not exist.

### State 9 — Slay Dragon (`FUN_0042a150`, `Dragon_a_sink19_Control`) **[C]**

Entered from (a) `Dragon_a_spot28_Control` — the third/innermost sword target, or (b)
`FUN_00423e90` when all five Quest Lights are lit. Both also flash `v_lite76`. ✔ manual.

* `FUN_004230f0(0, 500000)` on start; hint 3048.
* **Time limit** `_DAT_00434cf8`, in seconds: base **30**; `v_lite37` lit → set to **35** (and
  re-apply the shield effect); `+5` for each of `v_lite38`, `v_lite39`, `v_lite31`, `v_lite32`.
  → `30 + 5 × (armor + defensive spells)`, max 50. ✔ manual exactly.
* **Hits required** `v_lite114->MessageField`: starts at **8**, `-1` for each of `v_lite33`,
  `v_lite34`, `v_lite35`, `v_lite36` (the 4 weapons) and `v_lite28`, `v_lite29` (the 2 *offensive*
  spells). → `8 − (weapons + offensive spells)`. ✔ manual exactly.
* **Super-jackpot accrual** while in state 9 (`0x4a`): bumper (1005) **+50,000**; solo target
  (1019) **+100,000**; `a_sink1`/`a_sink2` (1017) **+500,000**; kickout (1012) **+750,000**.
  ✔ manual §4.3 exactly.
* Each `a_sink19` (dragon's mouth) hit decrements `v_lite114->MessageField`, **adds 15 s to
  `mode_timer`** (not in the manual, **[P]**), and pays according to `dragon_fire_light_group`'s
  on-count:

  | Fire lights on | Award (binary) | Manual |
  |---|---|---|
  | 0 or 1 | 100,000 | 100,000 |
  | 2 | 1,000,000 | 1,000,000 |
  | 3 | **1-in-5** chance of the jackpot, else 100,000 | jackpot |
  | 4 | **1-in-10** chance of the extra-ball light, else 100,000 | extra-ball light |
  | ≥5 | **1-in-10** chance of an extra ball, else 100,000 | extra ball |

  **Disagreement [C]:** the top three awards are probabilistic, not deterministic.
* `dragon_fire_light_group`'s notify timer runs a ping-pong sweep over
  `PTR_PTR_0043bb78 = {v_lite113, v_lite114, v_lite115, v_lite58, v_lite59, v_lite60, v_lite61,
  v_lite62}` **[C]** using `v_lite113->MessageField` as cursor and `DAT_00439b0c` as direction;
  step period 0.123 s, 0.012 s for cursor 6–8.
* **Dragon slain** (`v_lite114->MessageField` reaches 0 → `dragon_mouth_light_group` 0…9 animation,
  frame 9): award the whole **super jackpot** (`table+0x17a`, string 3050) + **1,500,000** +
  the **jackpot**; light all 4 weapon lights and force multiplier = 5; apply all 3 armor effects;
  light spells `v_lite28/29/30/31`; light the whole `dragon_letter_light_group` and call
  `FUN_004244e0` → **state 0xf = Dragon Flurry**. ✔ manual ("super jackpot + all weapons + all
  armor + all spells + Dragon Flurry"); the manual's "+ all gems" was **not** found. **[U]**
* `0x54`/`0x55` on `a_ramp107` set `DAT_00434d18`; while set, a timer expiry is postponed by
  0.05 s instead of ending the quest (don't kill the mode mid-ramp). **[C]**

---

## 4. Collectible tracks — storage and awards

Every track is stored **as lit lights**, with no separate counters. All are read back with the
`(+0x42 || +0x4e || +0x46)` "is lit" idiom.

| Track | Lights | Count | Conf |
|---|---|---|---|
| Weapons | `v_lite33`, `v_lite34`, `v_lite35`, `v_lite36` | 4 | [C] |
| Armor | `v_lite37` (Shield), `v_lite38` (Helm of Terror), `v_lite39` (Chain mail) | 3 | [C] |
| Spells | `v_lite28`, `v_lite29` (+`v_lite30` as its partner display), `v_lite31`, `v_lite32` | 4 (+1) | [C]/[P] |
| Dragon eggs | `dragon_egg_light_group` on-count (8 members) | 8 | [C] |
| Gems | `v_lite88->MessageField` bitmask + `gem_light_group` (9) | 9 | [C] |
| Quests completed | `v_lite28->MessageField` (int) and the 5 Quest Lights | 5 | [C] |
| Extra-ball light | `v_lite57` | — | [C] |
| Multi-mode lights | `v_lite77` ("2"), `v_lite78` ("3"), `v_lite79` ("5") | 3 | [C] |
| D-R-A-G-O-N letters | `v_lite22`…`v_lite27` | 6 | [C] |
| Sword target lights | `v_lite16`, `v_lite17`, `v_lite18` | 3 | [C] |

### 4.1 Weapons — `FUN_00423330` (AwardWeapon) **[C]**

Awards the **first** un-lit weapon light in order (not random), lighting it (`0xf`), setting the
table multiplier `table+0x15e` and adding an unmultiplied score:

| Slot | Light | Multiplier | Score | Glyph |
|---|---|---|---|---|
| 1 | `v_lite33` | 2 | 25,000 | `DAT_00439ab8` |
| 2 | `v_lite34` | 3 | 35,000 | `DAT_00439aba` |
| 3 | `v_lite35` | 4 | 45,000 | `DAT_00439abc` |
| 4 | `v_lite36` | 5 | 55,000 | `DAT_00439abe` |

Matches the manual's Ax/Firebow/Sword of Virtue/Dragon Lance values exactly. Sources: **forest
bumpers every 10 hits** (`Dragon_a_bump1_Control`, counter `a_bump1->MessageField`, `== 10` in both
code paths — **the manual's "every 8 hits" is wrong [C]**), skill shot on `a_spot6`, egg exchange
at 5 eggs, *Wizard's Fury* "Max Weapons".

### 4.2 Armor — `FUN_004234b0` (AwardArmor) **[C]**

Builds a list of un-lit armor lights and picks one **at random** (the manual's "the next piece of
armor" is wrong, **[C]**):

| Slot | Light | Effect | Score |
|---|---|---|---|
| 0 | `v_lite37` | `FUN_00423240`: enable `a_kick1` kickback + flash `outlane_kicker_light_group` (**Shield**) | 25,000 |
| 1 | `v_lite38` | `Dragon_a_center_post_Control(0x38)` — raise the center post (**Helm of Terror**) | 50,000 |
| 2 | `v_lite39` | `Dragon_a_bloce_Control(0x38)` — close the right outlane (**Chain mail**) | 100,000 |

Scores and effects match the manual exactly; this is what pins the light↔armor mapping. The 60 s
durations come from the blocker components' own `+0x86`/`+0x8a` fields, not from the rules code —
numeric value **[U]**.

### 4.3 Spells — `FUN_00423600` (pick) + `FUN_004236b0` (cast) **[C] mechanics, [P] names**

`FUN_00423600` returns a random un-owned slot (0…3) or `-1`. `FUN_004236b0(slot)` increments
`DAT_00434d24` (spells cast **this ball**), adds **120,000**, plays `tabc35wave`, and:

| Slot | Light | Effect | Off/Def | Manual name |
|---|---|---|---|---|
| 0 | `v_lite28` | light on, timed **30.0 s**; while lit, every solo target adds **+100,000 to the super jackpot** (`FUN_0042c020`) | offensive | **Dragon Breath** [C] |
| 1 | `v_lite29` + `v_lite30` | flash both, **enable `a_kout3`** (the slow kickout) | offensive | Dragon Blood [P] |
| 2 | `v_lite31` | flash-then-stay-on; while lit the **drain bonus is held** (`FUN_0042b7c0` step 7 copies the bonus into `drain->MessageField`) | defensive | **Resistance to Fire** [C] |
| 3 | `v_lite32` | `Dragon_a_blocf_Control(0x37)` — **disable the `a_blocf` blocker** for its configured time; `a_blocf`'s own handler drives `v_lite32` | defensive | Stealth [P] |

`FUN_00423780` clears all four (+ `v_lite30`, + re-enable `a_blocf`) on drain and at the end of
*Slay Dragon*.

Two results worth flagging:

* **The spec's offensive/defensive guess is wrong.** The binary's split (from §3's *Slay Dragon*
  arithmetic) is **offensive = slots 0 and 1**, **defensive = slots 2 and 3**. Slot 2 is certainly
  Resistance to Fire (bonus hold) and slot 0 certainly Dragon Breath (super-jackpot-per-target),
  so the manual's "Dragon Breath + Stealth offensive" cannot hold: whichever of Dragon Blood /
  Stealth is slot 3 is **defensive**. **[C]** for the split, **[P]** for which name is slot 1 vs 3.
* **The manual's "5 spell lights for 4 spells" [GAP] is explained:** `v_lite30` is toggled in
  lock-step with `v_lite29` and is never tested by `FUN_00423600`, i.e. it is a second display
  light for slot 1. **[C]**
* Durations: slot 0 is **30 s**, not the manual's 60 s. **[C]**

Sources: `a_sink4` (well) ¾ of the time; the full spell-target bank (`FUN_00424d20`, when all five
`v_lite3`…`v_lite7` are lit); skill shot on `a_spot4`; egg exchange at 4 eggs.

### 4.4 Dragon eggs — `FUN_004232b0(n)` **[C]**

Lights `n` more eggs in `dragon_egg_light_group`, then scores **on-count × 100,000** and displays
`&DAT_00439a60 + 9·count` — a string of `count` `#` characters (a symbol-font glyph run). Matches
the manual's "+100,000 per dragon egg already lit".

Sources: gargoyle ramp (2), forest ramp (1), skill shot on `a_spot5` (1), gargoyle kickout during
*Dragon Pass* (1).

Spending — state 0xc (`FUN_0042ac80`), entered from `s_hole118` (the egg kickout) when state == 2
and a single ball is in play. The offer is keyed on the egg on-count and **downgrades** if the award
is already maxed (8→7→6→5→4→1):

| Eggs | Offer | Prompt |
|---|---|---|
| 0 | automatic 25,000 + "No Dragon Eggs" (3068) | — |
| 1–3 | begin the selected quest | 3057–3061 |
| 4 | cast a spell | 3056 |
| 5 | weapon | 3055 |
| 6 | armor | 3054 |
| 7 | extra-ball light (`v_lite57`) | 3053 |
| 8 | extra ball | 3052 |

Left flipper (`a_flip1`) = Yes and consumes the eggs; anything else = No and pays 25,000. Matches
the manual's exchange table exactly. **[C]**

### 4.5 Gems

See §3 state 4. Stored only as the 9-bit mask in `v_lite88->MessageField` plus `gem_light_group`.
Gems carry **no drain-bonus term**. **[C]**

---

## 5. Jackpots, bonus, multiplier

* `table+0x172` = **jackpot**, clamped to 10,000,000 (`FUN_004238a0`); initialised to **1,000,000**
  at new game (`FUN_00424260` case `0x3f6`). ✔ manual's "minimum 1,000,000". **[C]**
* `table+0x17a` = **super jackpot**, clamped to 20,000,000 (`FUN_00423870`); initialised to
  **2,500,000**. **[C]** (not in the manual)
* `FUN_00424490(_, showGlyph)` = award the jackpot (adds `table+0x172` to the score; never resets
  it — ✔ manual "never reduced"). **[C]**
* `table+0x15e` = score multiplier, set only by the weapon track (2/3/4/5). **[C]**
* `FUN_004230f0(cmp, n)` / `FUN_004230a0(cmp, idx)` = add score (multiplied);
  `FUN_00423130(cmp, n)` = add score with multiplier and the two other multiplier fields
  (`+0x16e`, `+0x176`) temporarily forced to 1 — i.e. **unmultiplied**. **[C]**

### 5.1 Drain bonus — state 0xb, `FUN_0042b7c0` **[C]**

A stepped sequence (`DAT_00434d10` = step, `DAT_00434d00` = accumulator), one text line per step:

| Step | Term | Value | String |
|---|---|---|---|
| 0 | `DAT_00434d08` (targets hit this ball) | × 2,000 | 3012 |
| 1 | `v_lite28->MessageField` (quests completed) | × 100,000 | 3013 |
| 2 | lit armor lights | × 50,000 | 3014 |
| 3 | `DAT_00434d24` (spells cast this ball) | × 75,000 | 3015 |
| 4 | `dragon_egg_light_group` on-count | × 150,000 | 3069 |
| 5 | `table+0x15e` (multiplier) if > 1 | × 75,000 | 3070 |
| 6 | `drain->MessageField` (held bonus) | as-is | 3016 |
| 7 | total → score; if `v_lite31` lit, store total into `drain->MessageField` for next ball | | 3017 |
| 8+ | extra ball / next ball / **state 0 if last ball** | | |

This closes the manual's `3070 "Weapon Bonus"` discrepancy: it is **multiplier × 75,000**, i.e.
`(weapons + 1) × 75,000`, and exists. **[C]** It also confirms *Resistance to Fire* = bonus hold.
Note the manual's "2,000 per target dropped" really counts **collisions with components of type
1006/1019 this ball**, incremented in `FUN_0042c020` — Dragon has no drop targets at all. **[C]**

---

## 6. Skill shot — state 0xa (`FUN_004285f0`) **[C]**

Set by `Dragon_s_plunger1_Control` on each new ball (which also zeroes `a_bump1->MessageField`,
`DAT_00434d08`, `DAT_00434d24`, resets the sword to `FUN_004238d0(3)` and calls `FUN_00423e90`).
The first component the ball touches decides:

| First hit | Award |
|---|---|
| `a_spot4` | `FUN_00423600`/`FUN_004236b0` — cast a random spell |
| `a_spot5` | `FUN_004232b0(1)` — 1 dragon egg |
| `a_spot6` | `FUN_00423330` — a weapon |
| `a_spot7` | `FUN_004234b0` — armor |
| `a_spot22` | **state 0xe** — Gargoyle Gaggle-Ball |
| anything else (listed types) | 15,000 if `table+0x1b2 == 0` |

Matches the manual's skill-shot table exactly, and confirms the spell-bank order
`a_spot4` (top) … `a_spot22` (bottom).

---

## 7. Multiball modes

| State | Mode | Entered by | Ball delta |
|---|---|---|---|
| 0xd | **Tower Barrage** (3051) | `FUN_00423be0`: all three tower-lock lights `v_lite48/49/50` lit | `table+0x192 += 3`, then releases `a_sink18/17/16` at 1/2/3 s |
| 0xe | **Gargoyle Gaggle-Ball** (3062) | `Dragon_a_kout2_Control` with state == 2; or skill shot `a_spot22` | `+= 1` |
| 0xf | **Dragon Flurry** (3063) | `FUN_004244e0`: all six letters `v_lite22`…`v_lite27` lit; or slaying the dragon | `+= 4` (`+= 3` if a ball is still being fed) |

Each ends via `0x53` when the ball count is back to 1 → state 2. The manual's "Tower Ball" is the
engine's **"Tower Barrage"** — question resolved. **[C]** Ball counts: Gaggle = 2 and Flurry = 5
match the manual; Tower Barrage's `+3` on top of the ball in play suggests 4, which the manual calls
3 — the three locked balls make the accounting ambiguous. **[S]**

The D-R-A-G-O-N letters are lit by the "bank completed" events while state == 2: `v_lite22` by two
fire-lizard-sink hits (`FUN_00425c30`), `v_lite23` by the full spell bank (`FUN_00424d20`), and
similar for the tower/gargoyle banks (handlers `FUN_004250e0`, `FUN_004252b0`) — each calls
`FUN_004244e0` afterwards. **[P]** for the exact letter↔bank assignment beyond those two.

---

## 8. Open items

1. **`Dragon_a_sink18_Control` @0x427100 is unusable as decompiled** (lost float arguments →
   stack-byte noise). *Rescue Maiden*'s per-hit awards, the tower-lock/"Ball Trapped" scoring and
   the locked-ball values all live there. **[U]**
2. Same noise class in `Dragon_a_sink19_Control`, `Dragon_a_sink1/2_Control` — recoverable by
   reading, but any single constant from them should be re-checked.
3. Success-vs-timeout: no discriminator found in the quest-completion path (§2.5). **[U]**
4. Spell slot 1 vs slot 3 ↔ *Dragon Blood* vs *Stealth*: inferred from `a_kout3` (a slow kickout,
   plausibly the "magnet" the manual describes as control-code-only) vs `a_blocf` (a blocker whose
   removal redirects a path, plausibly the moat chute). **[P]** — settle it by locating `a_blocf`
   and `a_kout3` geometrically in `DRAGON.DAT`.
5. *Dragon Pass*' "gargoyle target → 100,000 + 250,000 to jackpot" and *Slay Dragon*'s
   "+ all gems" reward were not found. **[U]**
6. `Dragon_a_kout2_Control` increments `a_kout2->MessageField` (capped at 3) as a score index while
   four other functions use the same field as the selected-quest index (4…8). Either a genuine
   dead/buggy path (it can only run when no quest is selected) or a mis-decompile. **[P]**
7. The exact `MessageCode` renumbering between 3DPB and FT is only partly pinned (§0). `0x49`,
   `0x4b`–`0x4e` are unaccounted for; `0x4d` is sent to the previously scored component by
   `FUN_004230a0`/`FUN_004230f0`/`FUN_00423130` (probably `ControlDisableMultiplier`). **[P]**
8. Weapon/armor/spell/egg messages are **single glyphs in a symbol font**, not names:
   `DAT_00439ab4` = `"$"` (spell), `DAT_00439ab8/aba/abc/abe` = `"%&'("` (weapons),
   `DAT_00439ac0/ac2/ac4` = `"+*)"` (Shield/Helm/Chain), `DAT_00439ac8…` = `",-./0123"`
   (dragon-death frames), `DAT_00439a58` = `"!"`, `DAT_00439a5c` = `"\""` (jackpot),
   `DAT_00439a60 + 9i` = `i` × `"#"` (eggs). `FUN_0041de40(box, s, time, 1, 0)` — the `1` selects
   that font. **[C]** for the bytes, **[P]** for the font interpretation.

## 9. Reproducing the global→group-name map

```python
import sys, struct, re; sys.path.insert(0, 'analysis')
from dump_components import load, mapper
d, imgbase, secs = load('binaries/win95/DRAGON.EXE')
off, sec_of, cstr = mapper(d, imgbase, secs)
for n, sva, sz, ro, rsz in secs:
    if n not in ('.data', '.rdata'): continue
    for o in range(ro, ro + rsz - 8, 4):
        p = struct.unpack('<I', d[o:o+4])[0]
        s = cstr(p) if p else None
        if s and re.fullmatch(r'[a-z0-9_]{3,31}', s):
            print(f"DAT_{imgbase + sva + (o - ro) + 4:08x} {s}")
```
