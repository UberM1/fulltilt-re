# Full Tilt! Pinball — *Dragon's Keep* rules specification

Reference spec assembled 2026-10-26 for cross-checking binary reverse engineering of
`DRAGON.EXE` / `DRAGON.DAT` (Cinematronics / Maxis, 1995–96).

Secondary, briefer section for **Skulduggery** (internal name `PIRATES`, internal DAT
description *"Pirate's Cove Table"*) at the end.

---

## 0. Source inventory and confidence legend

| Tag | Meaning |
|---|---|
| **[MANUAL]** | Verbatim from the official Maxis help file `FULLTILT.HLP` (1995). Authoritative primary documentation. |
| **[STRINGS]** | Extracted from the Win16 `STRINGTABLE` resources of `DRAGON.EXE` / `PIRATES.EXE`. Authoritative engine text, with exact resource IDs. |
| **[DAT]** | Extracted from `DRAGON.DAT` / `PIRATES.DAT` by parsing the `PARTOUT(4.0)RESOURCE` ("partman") container. Authoritative component inventory. |
| **[INFERRED]** | My reading/synthesis. Treat as a hypothesis to verify in the binary. |
| **[GAP]** | Not found in any source. |

### 0.1 Sources actually used

1. **`FULLTILT.HLP`** — the official in-game help, which contains a complete
   *"Table Rules"* chapter for all three tables, including eight Dragon's Keep topics.
   This is by far the best source and it answers most of the brief.
   - Obtained from: `https://archive.org/download/full-tilt-pinball-win-3x-en-win-3x-o-release-1/Full-Tilt-Pinball_Win-3x_EN_Win3xO-release%20(1).zip/FTiltP%2FMAXIS%2FFULLTILT%2FFULLTILT.HLP`
   - Item page: <https://archive.org/details/full-tilt-pinball-win-3x-en-win-3x-o-release-1>
   - The file is an *uncompressed* Win 3.x HLP, so topic text is recoverable with plain
     byte scanning (no `helpdeco` needed). Table cells containing short numbers are lost
     by a naive `strings`-style scan with a minimum length > 3; use min-length 1 and
     collapse non-printables.
   - Copyright line inside: `Full Tilt! Pinball for Windows / Copyright © 1995, Maxis. All rights reserved.`
2. **`DRAGON.EXE`** (646,656 bytes) and **`DRAGON.DAT`** (4,452,841 bytes) —
   <https://archive.org/details/full-tilt_pinball> (`DRAGON.zip`).
3. **`PIRATES.EXE`** / **`PIRATES.DAT`** — <https://archive.org/details/full-tilt-pinball_> (`PIRATES.zip`).
4. **`partman` container format** — `partman.cpp` / `GroupData.h` from
   <https://github.com/k4zmu2a/SpaceCadetPinball> (used only as a format reference, not
   as a rules source).

### 0.2 Sources that did NOT pan out — important negative results

- **`https://fulltilt.miraheze.org` is empty and now deleted.** The live host returns
  Miraheze's "Wiki not found" 404 for `/`, `/wiki/Main_Page` and `/w/api.php`. The
  Internet Archive dumps confirm there was never any content:
  - `miraheze-wiki-fulltiltwiki-20260111` → `fulltiltwiki-20260111.xml.gz` (1,744 bytes
    compressed) contains **exactly one page, `Main Page`**, holding the stock Miraheze
    "This Main Page was created automatically" boilerplate plus the line
    `This is a wiki about the game ''Full Tilt!''`.
  - `wiki-fulltilt.miraheze.org_w-20240910` → `…-history.xml.zst` — same, one page.
  - Wayback CDX for `fulltilt.miraheze.org*` shows only `/wiki/Main_Page` plus skin
    assets; no article URLs were ever crawled.
  - There are ~8 further monthly Miraheze dumps (`miraheze-wiki-fulltiltwiki-2025{0321,0406,0512,0703,0805,0905,1011,1211}`); all are the same ~1.7 KB size, i.e. all empty.
  **Conclusion: that wiki can be disregarded entirely.** [DAT]/[STRINGS]/[MANUAL] above
  replace it.
- The *printed/PDF* manual ("PINBALL.DOC") was not located; for Full Tilt! the rules
  shipped as `FULLTILT.HLP`, not as a DOC. [GAP]
- Note the manual itself says of the labelled table picture: *"There are still a few
  surprises, although a complete set of table descriptions are contained in [other]
  topics of this section."* — i.e. Maxis admits the help is not exhaustive.

---

## 1. Overview and goal

> **Overview of Dragon's Keep** — "The theme of this game is a life and death battle with
> a dragon. Your overall goal is to slay the dragon by selecting and completing a series
> of quests **or** by pushing the sword target into the dragon." **[MANUAL]**

Two independent paths to the finale, which is the main structural difference from
Space Cadet:

1. complete all five ordinary quests, **or**
2. push the **sword target** in all the way (3 hits).

Either one makes the **Slay Dragon** quest startable.

### 1.1 There is NO rank ladder

Space Cadet has 9 ranks / 18 progress lights / 17 missions. **Dragon's Keep has no rank
or progress system at all.** **[MANUAL]** — the Overview topic for DK deliberately drops
the "progress in rank" sentence that Space Cadet's Overview has, there is no
"Dragon's Keep Ranks and Quests" topic (Space Cadet has "Space Cadet Ranks and
Missions"), and there are no rank/progress light entries in the DK light list.
Progression is tracked instead by:

- **6 Quest Lights** — "the six square purple lights in the lower-right part of the
  table" **[MANUAL]**; one per quest (5 ordinary + Slay Dragon). [INFERRED: the mapping
  of light→quest; the count matches exactly the six quest names in [STRINGS].]
- Accumulated collectibles: weapons (4), armor (3), spells (4), dragon eggs (8), gems (8).

---

## 2. Table components

### 2.1 Lanes, ramps, chutes and orbits **[MANUAL]**

| Component | Location | Behaviour / score |
|---|---|---|
| **Dragon Ramp** | behind the dragon, left side | Each pass: **+50,000 to score and +50,000 to the jackpot**. Increased by a further 50,000 per additional pass made before the **dragon ramp combination light** goes out (i.e. 50k, 100k, 150k…). |
| **Tower Ramp** | right side, up to the gate tower | (manual gives location only; no score stated) **[GAP on value]** |
| **Dragon Chute** | path behind the dragon's neck, on the main playfield | Each pass: **+200,000 to the SUPER jackpot**, **+25,000 instantly to score**. During *Dragon Hoard* the dragon chute awards **the jackpot**. |
| **Moat Chute** | right side, under the bridge | Ball is **accelerated**; **75,000 points**. Under the *Stealth* spell the exit direction changes to hit the sword target and each pass awards **500,000**. |
| **Fire Ridge Orbit** | around the back of the table | **20,000** per pass. Repeat before the **fire ridge combination light** goes out to escalate: **50,000 → 100,000 → 1,000,000**. |
| **Outlanes** | outermost bottom lanes | **30,000** per pass. |
| **Return Lanes** | innermost bottom lanes | **10,000**; if the return lane light is lit, turns on another **kicker light**. |
| **Gargoyle Ramp** (wire ramp) | rocks → watch tower | **2 dragon eggs** per passage, **+100,000 per dragon egg already lit**. |
| **Forest Ramp** (wire ramp) | rocks → left return lane | **1 dragon egg** per passage, **+100,000 per dragon egg already lit**. |

### 2.2 Bumpers, rebounds, kickers **[MANUAL]**

| Component | Count | Behaviour |
|---|---|---|
| **Forest Bumpers** | 4, left side | **1,500 each**. **Every 8 hits awards another weapon.** |
| **Rebounds** (slingshots) | triangle pieces near the flippers | **1,000 each**. |
| **Kicker** | left outlane | Active for as long as any **kicker light** is on. (Kicker lights are turned on by return-lane passes; the *Shield* armor maxes them and freezes them on for 60 s.) |

### 2.3 Kickouts and sinks **[MANUAL]**

| Component | Location | Behaviour |
|---|---|---|
| **Fire Lizard Sinks** (×2) | in front of the lower bumpers | **5,000 each**. When both have been hit, the **dragon letter light** in front of them lights. **Two fire-lizard-sink hits (or one sink twice) starts the next available quest.** Within **10 s of a quest ending**, any fire lizard sink **restarts** that quest. |
| **Well Sink** | between the bumpers, left side | Ball entering **casts a random spell or starts a quest**. |
| **Armor Kickout** | behind the leftmost gargoyle target | Awards **the next piece of armor**. |
| **Gargoyle Kickout** | behind the third gargoyle target from the left | Starts the **two-ball Gargoyle Gaggle-Ball** mode. |
| **Dragon Egg Kickout** | just below the dragon ramp, left side | Offers to **exchange accumulated dragon eggs** (see table below). |

**Dragon Egg Kickout exchange table** **[MANUAL]** (recovered from the HLP table cells):

| Eggs | Award |
|---|---|
| 0 | 25,000 points (automatic) |
| 1–3 | Begin quest |
| 4 | Cast spell |
| 5 | Weapon |
| 6 | Armor |
| 7 | Extra ball light |
| 8 | Extra ball |

Corroborating [STRINGS]: `3068 "No Dragon Eggs"`, `3069 "Dragon Egg Bonus\n%ld"`.
The purchase prompts `3052`–`3061` (`Yes [Left] / No [Right]`) are the UI for this and the
quest-selection offers.

### 2.4 Targets **[MANUAL]**

| Target group | Count / location | Behaviour |
|---|---|---|
| **Spell Target Bank** | 5, middle of table, facing right | **2,500 each**. All 5 hit → **random spell cast** + the dragon letter light in front of the bank lights. During *Wizard's Fury* these set the mystery award. |
| **Tower Target Bank** | 3, right side | **2,000 each**. All 3 → **10,000**, **tower lock enabled**, dragon letter light lights. (Engine string `3003 "Tower Lock Enabled"`.) |
| **Gargoyle Target Bank** | 4, top of table | **3,000 each; 15,000 for all four**; dragon letter light lights. **Two of the four are mounted on sliding rocks** — hitting those slides the rock into the table, opening a passage to a kickout behind it. |
| **Quest Selection Target** | circular yellow target, lower right | **Changes the next available quest**, **5,000 points**. **If the extra ball light is lit, also awards an extra ball.** |
| **Sword Target** | end of the sword jutting from the dragon's neck | 1st hit **20,000**; 2nd hit **50,000**; **3rd hit begins the Slay Dragon quest**. |
| **Ricochet Targets** | 2 — one at the edge of the tower ramp, one behind the dragon | Kick the ball away when hit; **2,500 each**. |

### 2.5 Lights (complete list) **[MANUAL]**

| Light | Description |
|---|---|
| **Armor Lights** | 3 large shield-shaped lights: one between the flippers, one above each outlane. |
| **Combination Lights** | blue triangles in front of each ramp, the dragon chute and the fire ridge orbit. |
| **Dragon Egg Lights** | 8 oval purple lights from the dragon egg kickout to the moat chute. |
| **Dragon Letter Lights** | green circular lights spelling **D-R-A-G-O-N**. *Manual says "five"; the DAT group `dragon_letter_light_group` contains **six** lights (groups 383–388) — the DAT is almost certainly right (D,R,A,G,O,N = 6) and the manual has a typo.* **[DAT]** |
| **Extra Ball Light** | diamond-shaped red light in front of the quest selection target. |
| **Fire Lights** | 5 lights in an elongated triangle extending from the dragon (used by *Slay Dragon*). DAT `dragon_fire_light_group` = 5 lights ✔ **[DAT]** |
| **Fire Lizard Lights** | star-shaped orange light in front of each fire lizard sink. |
| **Gargoyle Lights** | 4 diamond-shaped blue lights in front of the gargoyle targets. DAT `gargoyle_spot_light_group` = 4 ✔ **[DAT]** |
| **Gem Lights** | hexagonal yellow lights scattered about the table. *Manual says "eight"; DAT `gem_light_group` has **nine** members (434–442).* **[DAT]** |
| **Moat Lights** | 4 arrow-shaped lights in the moat chute. |
| **Multi-Mode Lights** | 3 square green lights in the centre labelled **"2", "3", "5"** — one per multiball mode (2-ball / 3-ball / 5-ball). |
| **Quest Lights** | **6** square purple lights, lower-right. |
| **Quest Selection Light** | yellow circular light in front of the quest selection target. |
| **Return Lane Lights** | circular orange lights above each return lane. |
| **Ricochet Target Lights** | red arrow-shaped lights in front of each ricochet target. |
| **Spell Lights** | 5 purple circular lights scattered about the table. *(There are only 4 spells — the 5th light's meaning is unexplained.* **[GAP]**) |
| **Spell Target Lights** | 5 diamond-shaped blue lights in front of the spell targets. |
| **Sword Target Lights** | 3 red arrow-shaped lights in front of the sword (one per sword hit). |
| **Tower Lock Lights** | circular blue lights on top of each tower. |
| **Tower Target Lights** | 3 diamond-shaped blue lights in front of the tower targets. |
| **Wall Lights** | 4 red circular lights between the watch and prison towers (**1,500 each**). |
| **Weapon Lights** | 4 orange diamond-shaped lights between the bumpers and the flippers. |
| **Well Lights** | 4 circular blue lights by the well. DAT `well_light_group` = 4 ✔ **[DAT]** |
| **Window Lights** | window-shaped lights in each tower. DAT: `prison_window_light_group` = 4, `watch_window_light_group` = 3, `gate_window_light_group` = 2 **[DAT]** |
| **Tilt Light** | large red X above the left rebound. |

### 2.6 Misc. components **[MANUAL]**

- **Towers** — three: **gate tower** (upper right), **watch tower** (middle right),
  **prison tower** (bottom right). If the tower lock is enabled for that tower the ball
  is locked. Scoring:
  | | |
  |---|---|
  | Each wall light | 1,500 |
  | Gate tower | 50,000 first hit, 100,000 thereafter |
  | Watch tower | 40,000, then 80,000, then 120,000 |
  | Prison tower | 25,000, then 50,000, then 75,000, then 100,000 |
  | Locked ball | 100,000 first, then 200,000, then 300,000 |
- **Plunger** — a **bow**: "Pull the bow back to launch the ball." If the ball drains in
  the first 15 s it is re-launched.
- **Flippers** — **three**: two near the drain, one at the entrance to the fire ridge
  orbit on the right. (DAT confirms 1× `TFlipper(L)` + 2× `TFlipper(R)` **[DAT]** — i.e.
  the right-side upper flipper is driven by the right flipper button.)
- **Accelerators** — **three**: one in the moat chute, two in the fire ridge orbit.
  (DAT: `a_accl1`, `a_accl3`, `a_accl4`, `a_accl5` = **four** accelerator objects, type
  `1036`. Manual says three. **[DAT]** discrepancy.)
- **Magnets** — "Under certain circumstances, such as the *dragon blood* spell, a magnet
  underneath the table will be turned on to affect the ball's path." Note: unlike
  Skulduggery, **the DK DAT has no `s_magnet*` objects** — the DK magnet is presumably
  implemented inside the control code rather than as a component. **[DAT]**
- **Center post** — present as the DAT blocker `a_center_post`; raised for 60 s by the
  *Helm of Terror* armor. **[DAT]** + **[MANUAL]**

### 2.7 Authoritative component inventory from `DRAGON.DAT` **[DAT]**

`DRAGON.DAT` is a `PARTOUT(4.0)RESOURCE` / `FullTilt` container, description
**`Dragon's Keep Table`**, **668 groups**, 278 of them named. The group named
`table_objects` holds a `ShortArray` tagged **1025** containing **332 `(objectType, groupIndex)` pairs** —
the full component list, in construction order.

Type-code histogram for Dragon's Keep:

| Type | Class (Space Cadet naming) | Count |
|---|---|---|
| 1000/1010 | `TWall` | 70 |
| 1001 | `TPlunger` | 1 (`s_plunger1`) |
| 1002 | `TLight` | 110 |
| 1003 | `TFlipper` (left) | 1 (`a_flip1`) |
| 1004 | `TFlipper` (right) | 2 (`a_flip2`, `a_flip3`) |
| 1005 | `TBumper` | 4 (`a_bump1`…`a_bump4`) |
| 1007 | `TDrain` | 1 |
| 1011 | `TBlocker` | 7 (`a_sword`, `a_bloce`, `a_blocf`, `a_jaw`, `a_center_post`, `a_rocka`, `a_rockb`) |
| 1012 | `TKickout` (fast) | 2 (`a_kout1`, `a_kout2`) |
| 1014 | `TKickback` | 1 (`a_kick1`) |
| 1015 | `TRollover` | 5 (`a_roll1`, `a_roll2`, `a_roll5`, `a_roll6`, `a_roll7`) |
| 1016 | `TOneway` | 8 |
| 1017 | `TSink` | 11 (`a_sink1,2,4,9,10,13,15,16,17,18,19`) |
| 1019 | `TSoloTarget` | 18 (`a_spot*`) |
| 1020 | `TLightRollover` | 4 (`v_wlite96`…`v_wlite99`) |
| 1021 | `TRamp` | 6 (`a_ramp4`, `a_ramp5`, `a_ramp6`, `a_ramp103`, `a_ramp104`, `a_ramp107`) |
| 1022 | `THole` | 3 (`s_hole100`, `s_hole101`, `s_hole118`) |
| 1023 | `TDemo` | 1 |
| 1024 | `TTripwire` | 8 |
| 1026 | `TLightGroup` | 12 (names below) |
| 1029 | `TKickout` (slow) | 1 (`a_kout3`) |
| 1031 | `TSound` | 38 |
| 1032 | `TTimer` | 1 (`mode_timer`) |
| 1033 | `TTextBox` | 1 (`info_text_box`) |
| **1035** | *unknown — `s_rexit18/19/21` ("ramp exit")* | 3 |
| **1036** | *unknown — `a_accl1/3/4/5` ("accelerator")* | 4 |
| **1038** | *unknown — `light_sweeps`* | 1 |
| **1043** | *unknown — `a_dvtraa/ab/ba/bb/ca/cb/da/db` ("diverter", 4 pairs)* | 8 |

**Notable for RE:** type codes **1034, 1035, 1036, 1038, 1042, 1043 have no handler in the
Space Cadet decompilation** (`TPinballTable::TPinballTable` only switches 1000–1033).
These are Full-Tilt-only component classes. Dragon's Keep uses 1035, 1036, 1038, 1043;
Skulduggery additionally uses **1034** (`s_launcher1`) and **1042** (`s_magnet1`…`s_magnet6`). **[DAT]**

**Also notable:** Dragon's Keep contains **no `TPopupTarget` (1006) and no
`TFlagSpinner` (1018)** — it has no drop targets and no spinners, only spot targets.
This conflicts with the manual's drain bonus line "2,000 points per **target dropped**",
which is presumably boilerplate carried over from the other tables; verify what the
counter actually counts. **[DAT]** / [INFERRED]

**12 light groups (DK), with member counts** **[DAT]**
(`ShortArray` tagged `1027` lists member light group-indices; `FloatArray` `903.0` = 0.2 s
default sweep period for every group):

| Group | Members |
|---|---|
| `spell_spot_light_group` | 5 |
| `dragon_letter_light_group` | 6 |
| `dragon_fire_light_group` | 5 |
| `well_light_group` | 4 |
| `dragon_egg_light_group` | 8 |
| `outlane_kicker_light_group` | 4 |
| `prison_window_light_group` | 4 |
| `watch_window_light_group` | 3 |
| `gate_window_light_group` | 2 |
| `gem_light_group` | 9 |
| `dragon_mouth_light_group` | 3 |
| `gargoyle_spot_light_group` | 4 |

---

## 3. Collectibles (table-specific mechanic)

Dragon's Keep's signature mechanic: three parallel collectible tracks that change the
*physical* table, plus two score-only tracks.

### 3.1 Weapons — 4, each raises the table multiplier **[MANUAL]**

| # | Weapon | Effect |
|---|---|---|
| 1 | **Frantz's Ax** | **2×** multiplier + 25,000 |
| 2 | **Firebow** | **3×** + 35,000 |
| 3 | **Sword of Virtue** | **4×** + 45,000 |
| 4 | **Dragon Lance** | **5×** + 55,000 |

Awarded by: every **8 forest-bumper hits**; skill shot (3rd spell target); dragon egg
exchange at 5 eggs; the *Max Weapons* mystery award. Engine string `3045 "Max Weapons"`.
(*"Frantz"* is developer John Frantz; also the source of Skulduggery's "Frantz Ruby".)

### 3.2 Armor — 3, each changes table geometry for 60 s **[MANUAL]**

| Armor | Effect | Points |
|---|---|---|
| **Shield** | Maximizes the kicker lights and prevents them turning off for 60 s | 25,000 |
| **Helm of Terror** | **Raises the center post** for 60 s | 50,000 |
| **Chain mail** | **Closes off the right outlane** for 60 s | 100,000 |

Awarded by: **Armor Kickout**; skill shot (4th spell target); dragon egg exchange at 6
eggs; *Max Armor* mystery award (`3044 "Max Armor"`).

### 3.3 Spells — 4 **[MANUAL]**

Cast "usually from the well or the spell target bank, but there are other possibilities".

| Spell | Effect | Duration |
|---|---|---|
| **Dragon Breath** | **+100,000 to the SUPER jackpot for every target, bumper, rebound and kickout hit** | 60 s |
| **Dragon Blood** | Turns on the **magnet in front of the dragon flipper** | (not stated) |
| **Resistance to Fire** | Turns on **bonus hold** (bonus not reset on drain) | (not stated) |
| **Stealth** | Changes the **moat chute exit direction to hit the sword target**, and awards **500,000 per moat-chute pass** | 60 s |

**Offensive vs defensive spells matter for *Slay Dragon***: hits required = 8 − (weapons +
**offensive** spells); time = 30 s + 5 s per (armor + **defensive** spell). The manual does
not label which spells are offensive and which defensive. [INFERRED: *Dragon Breath* and
*Stealth* are offensive; *Dragon Blood* and *Resistance to Fire* are defensive.] **[GAP —
verify in the binary.]**

### 3.4 Dragon eggs — 8 **[MANUAL]**

Earned 2 per **Gargoyle Ramp** pass, 1 per **Forest Ramp** pass (plus 100,000 per egg
already lit), 1 per skill shot on the 2nd spell target, 1 per gargoyle-kickout entry
during *Dragon Pass*. Spent at the **Dragon Egg Kickout** (table in §2.3). Worth
**150,000 each** in the drain bonus.

### 3.5 Gems — 8 named + gold **[MANUAL]** / **[STRINGS]**

Collected during the *Dragon Hoard* quest. The 8 gem names are confirmed by consecutive
string IDs `3021`–`3028`: **Gold, Diamond, Pearl, Ruby, Emerald, Sapphire, Amethyst,
Opal**. Note: the manual's *Dragon Hoard* topic lists only six gem→component mappings
plus gold, and never mentions **Opal** — the Opal source is unknown. **[GAP]**

---

## 4. The quest system

### 4.1 Starting, selecting, restarting **[MANUAL]**

> "A quest is started by putting the ball in the fire lizard sinks twice. The next
> available quest can be changed by hitting the quest selection target. A quest can be
> restarted after it has ended by putting the ball back in on[e] of the fire lizard sinks
> within ten seconds."

Also startable from: the **Well Sink** (random — "a random spell is cast **or a quest is
started**"), and the **Dragon Egg Kickout** at 1–3 eggs ("Begin quest").

Engine confirmation **[STRINGS]**: `3001 "Hit Fire Lizards To Begin Quest"`,
`3067 "Restart Quest\n%ld"`, and the five "<name>\nQuest Selected" strings `3071`–`3075`.

### 4.2 The six quests

There are **six** quests — five ordinary plus the finale — matching the six Quest Lights.
Confirmed by [STRINGS] `3071`–`3075` ("Quest Selected" for exactly five quests, Slay
Dragon being started differently) plus `3047`–`3049` for Slay Dragon.

| # | Quest | Start | Objective | Rewards |
|---|---|---|---|---|
| 1 | **Dragon Hoard** | quest-selectable (`3071`, `3057`) | "Steal the gems from the dragon by hitting the components with the flashing gem lights in front of them." Engine hint string: `3020 "Steal Gems From Dragon"` | Diamonds (**spell targets**) 50,000 · Pearls (**tower targets**) 60,000 · Sapphires (**gargoyle targets**) 70,000 · Rubies (**fire lizards**) 100,000 · Emeralds (**tower ramp**) 200,000 · Amethysts (**dragon ramp**) 250,000. Bumpers award **gold (10,000)**. **Dragon chute awards the jackpot.** |
| 2 | **Fire Lizard Attack** (engine name: **Fire Lizards**, `3030`/`3058`/`3072`) | quest-selectable | "Slay fire lizards by hitting the fire lizard sinks." Hint `3031 "Put ball in Fire Lizard Sinks"` | Each successive hit is worth **+100,000 cumulative**, capped at **1,000,000**. **Every 4th hit awards the jackpot.** Per-hit message `3029 "Fire Lizard Slain\n%ld"`. |
| 3 | **Rescue Maiden** (`3032`/`3059`/`3073`) | quest-selectable | "Rescue the damsel in distress by putting the ball in the **prison tower** repeatedly." Hint `3033 "Put ball in Prison Tower"` | 1st **250,000** · 2nd **500,000** · 3rd **Jackpot** · 4th **extra ball light turned on**. Completion message `3034 "Maiden Rescued / Extra Ball Available"`. |
| 4 | **Dragon Pass** (`3035`/`3060`/`3074`) | quest-selectable | "Find the passage to the dragon's lair by putting the ball in the **gargoyle and armor kickouts** as often as possible." Hint `3036 "Put ball in Armor and Gargoyle Kickouts"` | Each **gargoyle target** hit: **100,000** score **+250,000 to the jackpot**. Each **gargoyle kickout**: **a dragon egg**. Each **armor kickout**: **the jackpot**. Engine string `3004 "Pass Clear"` marks completion. |
| 5 | **Wizard's Fury** (`3037`/`3061`/`3075`) | quest-selectable | "Put the ball in the **well** as many times as possible. Each time the well is hit, **the mystery award is given**." Hint `3038 "Put ball in Well and hit Spell Targets"` | Hitting a **spell target** re-arms the mystery award: top = **1,000,000** · 2nd = **Jackpot** · 3rd = **Max. weapons** · 4th = **Maximum armor** · bottom = **Extra ball**. Announcement strings `3039`–`3043` (`"Mystery Bonus set to:\n<X>"`). |
| 6 | **Slay Dragon** (`3047`) — **finale** | **Only** by (a) pushing the **sword target** in all the way (3 hits) or (b) completing **all the other quests** | "Put the ball in the **dragon's mouth** several times to kill the dragon." Hint `3048 "Put ball in Dragon's Mouth"`; live counter `3049 "Slay Dragon / Hits Left: %ld"` | See §4.3 |

Starting a quest: the manual never states a flat "starting a quest is worth N" for DK
(unlike Space Cadet's 100,000). **[GAP]**
Completing a quest: **+500,000 to the jackpot** (§5.1) and **100,000 per completed quest
in the drain bonus** (§5.2).

### 4.3 Slay Dragon (finale) in detail **[MANUAL]**

- **Hits required = 8 − (number of weapons + number of offensive spells).**
- **Time limit = 30 s + 5 s per (armor item + defensive spell).**
- Each dragon's-mouth entry awards according to which **fire light** is on, counting
  outward from the mouth:

  | Fire light | Award |
  |---|---|
  | 1st | 100,000 points |
  | 2nd | 1,000,000 points |
  | 3rd | Jackpot |
  | 4th | Extra ball light |
  | 5th | Extra ball |

- Components add to the **super jackpot** during this quest:

  | | |
  |---|---|
  | Bumpers | +50,000 |
  | Targets | +100,000 |
  | Ball sinks | +500,000 |
  | Kickouts | +750,000 |

  Engine string `3076 "Super Jackpot Increased\n%ld"`; award `3050 "Super Jackpot\n%ld"`.
- **If the dragon is slain:** super jackpot + **all gems** + **all weapons** + **all
  armor** + **all spells** + **Dragon Flurry** (the 5-ball mode).

---

## 5. Scoring

### 5.1 Jackpot and super jackpot **[MANUAL]**

> "The jackpot is accumulated over the course of the game and **never reduced**. The
> minimum value for the jackpot is **1,000,000 points**."

Always-on jackpot contributions:

| Source | Adds |
|---|---|
| Targets | 50,000 |
| Sinks | 75,000 |
| Quest completion | 500,000 |
| Dragon ramp pass | 50,000 (escalating, §2.1) |
| Dragon chute pass | 200,000 (to **super** jackpot) |
| Gargoyle target during *Dragon Pass* | 250,000 |
| *Dragon Breath* spell: any target/bumper/rebound/kickout | 100,000 (to **super** jackpot) |
| *Slay Dragon*: bumpers / targets / sinks / kickouts | 50,000 / 100,000 / 500,000 / 750,000 (to **super** jackpot) |

Engine strings: `3006 "Jackpot Grows"`, `3018 "Jackpot\n%ld"`,
`3064 "Dragon Loop\n%ld Added to Jackpot"`, `3050 "Super Jackpot\n%ld"`,
`3076 "Super Jackpot Increased\n%ld"`.
The **super jackpot** appears to be a separate, larger accumulator used only by the
finale. [INFERRED]

### 5.2 Drain bonus **[MANUAL]**

| Item | Value each |
|---|---|
| Target dropped | 2,000 |
| Quest completed | 100,000 |
| Armor item | 50,000 |
| Spell | 75,000 |
| Dragon egg | 150,000 |

"In addition, if **bonus hold** is active, the bonus amount will be held over from last
bonus." (Bonus hold is granted by the *Resistance to Fire* spell.)

The engine breaks the bonus out into labelled lines **[STRINGS]**:
`3012 Target Bonus` · `3013 Quest Bonus` · `3014 Armor Bonus` · `3015 Spell Bonus` ·
`3069 Dragon Egg Bonus` · `3070 Weapon Bonus` · `3016 Bonus Hold` · `3017 Total Bonus`.
Note **`3070 "Weapon Bonus"` exists in the engine but no weapon term appears in the
manual's bonus formula** — a documented-vs-implemented discrepancy worth checking.

### 5.3 Skill shots **[MANUAL]**

"A special award is given, depending on the **first thing the ball hits after being
launched**":

| First hit | Award |
|---|---|
| **Top spell target** | Cast random spell |
| **2nd spell target** | 1 dragon egg |
| **3rd spell target** | Award weapon |
| **4th spell target** | Award armor |
| **Bottom spell target** | Begin **Gargoyle Gaggle-Ball** |
| Anything else | 15,000 points |

(Engine string `3062 "Gargoyle Gaggle-Ball"`.) Unlike Space Cadet, DK has **no
"reflex shot"** topic. **[MANUAL omission / likely genuinely absent]**

### 5.4 Component point values, collected

| Component | Points |
|---|---|
| Forest bumper | 1,500 (+weapon every 8 hits) |
| Rebound / slingshot | 1,000 |
| Fire lizard sink | 5,000 |
| Spell target | 2,500 |
| Tower target | 2,000 (10,000 for all 3) |
| Gargoyle target | 3,000 (15,000 for all 4) |
| Quest selection target | 5,000 |
| Sword target | 20,000 / 50,000 / → Slay Dragon |
| Ricochet target | 2,500 |
| Wall light | 1,500 |
| Outlane | 30,000 |
| Return lane | 10,000 |
| Dragon ramp | 50,000 (escalating) |
| Dragon chute | 25,000 score (+200,000 super jackpot) |
| Moat chute | 75,000 (500,000 under *Stealth*) |
| Fire ridge orbit | 20,000 → 50,000 → 100,000 → 1,000,000 |
| Gargoyle / forest ramp | 100,000 per already-lit dragon egg |
| Gate tower | 50,000 / 100,000 |
| Watch tower | 40,000 / 80,000 / 120,000 |
| Prison tower | 25,000 / 50,000 / 75,000 / 100,000 |
| Locked ball | 100,000 / 200,000 / 300,000 |
| Dragon egg kickout with 0 eggs | 25,000 |
| Skill shot, "anything else" | 15,000 |
| Tower ramp | **[GAP]** |
| Well sink | **[GAP]** (award is a spell or quest, no point value given) |
| Armor / gargoyle kickout | **[GAP]** (award is armor / mode) |
| Fire lizard sink during quest | 100,000 cumulative per hit, cap 1,000,000 |

### 5.5 Multiplier

The **table multiplier** is driven purely by weapons: 2× / 3× / 4× / 5× (§3.1).
Dragon's Keep has **no separate field-multiplier target bank** (that is a Space Cadet
feature). `3044 "Max Armor"` / `3045 "Max Weapons"` max the tracks (and hence the
multiplier). The *Multi-Mode Lights* "2/3/5" are **ball counts, not multipliers**.
[INFERRED from the light description.]

---

## 6. Multiball, extra balls, specials, replay

### 6.1 Multiball modes **[MANUAL]** — three, matching the "2/3/5" multi-mode lights

| Mode | Balls | Started by |
|---|---|---|
| **Gargoyle Gaggle-Ball** | **2** | Putting the ball in the **gargoyle kickout** (also available as a skill shot on the bottom spell target) |
| **Tower Ball** | **3** | **Locking three balls in the towers** (the tower lock is enabled by dropping all three tower targets) |
| **Dragon Flurry** | **5** | **Turning on all the dragon letter lights** (spelling D-R-A-G-O-N); also awarded automatically for slaying the dragon |

Engine strings `3062 "Gargoyle Gaggle-Ball"`, `3063 "Dragon Flurry"`. There is **no
"Tower Ball" string**; the engine instead has `3051 "Tower Barrage"` and
`3003 "Tower Lock Enabled"`. [INFERRED: "Tower Barrage" is the in-game name of the
3-ball mode and the manual's "Tower Ball" is either an earlier name or an editorial
slip. **Worth verifying in the binary.**]

### 6.2 Extra balls **[MANUAL]** (from the shared general-rules topic)

> "You can receive an **unlimited** number of extra balls during the game. These are
> played immediately after the current ball has drained. The table is still reset, but
> the ball counter is not incremented."

Two-step mechanic on DK: things light the **Extra Ball Light** (diamond light in front of
the quest selection target); **hitting the quest selection target while it is lit awards
the extra ball**. `3002 "Extra Ball Light"`, `3011 "Extra Ball"`.

Sources of **extra ball light**: *Rescue Maiden* 4th prison-tower hit · *Slay Dragon* 4th
fire light · dragon egg exchange at 7 eggs · `3053 "Purchase Extra Ball Light?"`.
Sources of a **direct extra ball**: *Slay Dragon* 5th fire light · dragon egg exchange at
8 eggs · *Wizard's Fury* mystery award (bottom spell target) · `3052 "Purchase Extra Ball?"`.

### 6.3 Shoot again / ball save **[MANUAL]** + **[STRINGS]**

- "If the ball drains in the **first 15 seconds** of play, you may shoot the ball again."
  DK's plunger topic repeats this: "If the ball drains in the first 15 seconds it is
  re-launched." `3005 "Shoot Again"`, `3009 "Ball Saved"`.
- `3010 "Ball Trapped\n%ld"` — ball locked in a tower. **[STRINGS]**

### 6.4 Replay **[MANUAL]** (shared general rules; stated under Space Cadet)

> "Sometimes you may get to replay the ball. When this happens, nothing on the table
> resets or changes, and you do not receive the bonus. **You get only one replay at a
> time.** The replay light is located under the left flipper."

**No replay-score threshold is documented anywhere**, and no DK "Replay Light" appears in
the DK light list. **[GAP — the replay trigger condition for DK is undocumented.]**

### 6.5 "Special"

There is **no "special"** award on Dragon's Keep in either the manual or the string
table. The equivalents are the extra ball light and the super jackpot. **[GAP/absent]**

### 6.6 Nudging / tilt **[MANUAL]**

> "You can nudge the table to the left, to the right, and upwards. If you nudge the table
> too much, it will tilt. When the table tilts, all components stop working until the
> ball drains and the tilt light, which is a large, red X above the left rebound, goes
> on. The table is reset with the next ball."

`2005 "Careful..."`, `2006 "TILT!"`. Up to **4 players** (`2007`–`2014`), 3 balls default
(`MaxBallCount = 3` in the engine).

### 6.7 Purchase prompts **[STRINGS]** — an undocumented mechanic

String IDs `3052`–`3061` are a set of **Yes [Left] / No [Right]** offers:

```
3052 Purchase Extra Ball?        3057 Dragon Hoard?
3053 Purchase Extra Ball Light?  3058 Fire Lizards?
3054 Purchase Armor?             3059 Rescue Maiden?
3055 Purchase Weapon?            3060 Dragon Pass?
3056 Purchase Spell?             3061 Wizard's Fury?
```

The manual describes the second column (quest offers at the fire lizard sinks / well /
egg kickout) but **never mentions "purchasing"** armor, weapons, spells or extra balls.
[INFERRED: this is the **Dragon Egg Kickout** UI — eggs are the currency, the player is
offered the award their egg count qualifies for and accepts with the left flipper. The
word "Purchase" supports the egg-as-currency reading.] **Worth confirming.**

### 6.8 Other engine strings with no manual counterpart **[STRINGS] [GAP on meaning]**

| ID | String | Note |
|---|---|---|
| `3004` | `Pass Clear` | *Dragon Pass* completion. |
| `3007` | `Fire Ridge\n%ld` | fire ridge orbit award display. |
| `3051` | `Tower Barrage` | likely the 3-ball tower mode (§6.1). |
| `3064` | `Dragon Loop\n%ld Added to Jackpot` | "dragon loop" is not a manual component name — probably the dragon ramp/chute combination shot. |
| `3066` | `Launch Ball` | plunger prompt. |
| `3068` | `No Dragon Eggs` | egg kickout with 0 eggs. |
| `3070` | `Weapon Bonus\n%ld` | drain-bonus line absent from the manual's formula. |
| `3046` | `One Million` | *Wizard's Fury* mystery award label. |

---

## 7. What remains unknown for Dragon's Keep

1. **Raw per-component score tables.** In the Space Cadet decompilation these live in
   `control.cpp` as hardcoded `component_tag` score arrays, i.e. in **code**, not in the
   `.DAT`. DK's are therefore inside `DRAGON.EXE`'s data segment and need disassembly;
   the manual's figures above are the documented values but may be rounded or stale.
2. **Which spells are "offensive" vs "defensive"** for the Slay Dragon hit/time formula.
3. **Replay threshold** for DK.
4. **Tower ramp**, **well sink**, **armor kickout** and **gargoyle kickout** base point
   values.
5. The **9th gem light** / the **Opal** gem's source, and the **5th spell light**.
6. Mapping of the **6 quest lights** and the **3 sword target lights** to specific state.
7. Semantics of component types **1035, 1036, 1038, 1043** (and 1034/1042 for Pirates).
8. Whether the manual's **"Tower Ball"** or the engine's **"Tower Barrage"** is the
   shipped name.
9. Mapping of the 18 `a_spot*` DAT objects to the 16 named targets in the manual (2 extra).
10. Durations other than the stated 60 s / 30 s / 10 s / 15 s windows, and the
    `mode_timer` default. (`light_sweeps` has `FloatArray 1602.0 → 0.05 s`,
    `1601.0 → 1.0`, `1603.0 → 3.0`, `1604.0 → 0.1` — timing constants, meaning unverified.)

---
---

# Skulduggery (internal `PIRATES`, DAT description "Pirate's Cove Table")

All of the following is **[MANUAL]** unless marked. String IDs are from `PIRATES.EXE`.

## S1. Overview

> "The theme of this game is a hunt for pirate treasure. Your overall goal is to find
> **Peg Leg's loot** by selecting and completing a series of **modes** or piecing together
> the **map** to Peg Leg's treasure."

Same two-path structure as DK: complete the modes, **or** collect all six map pieces.
No ranks. Progression tracked by a central **wheel** carrying 7 mode lights, 6 coin
lights, 6 special-treasure lights and Peg Leg's Loot light.

## S2. Modes (the quest equivalent) — 8

Started by **knocking down all the targets and spelling "SKULDUGGERY"**
(`3076 "Spell Skulduggery to Start Mode"`), or from the **skill shot**. The next
available mode is changed by hitting the **captive ball**; hitting it *during* a mode
**extends the mode timer**. **Starting a mode is worth 200,000 points.**

| Mode | Objective | Scoring |
|---|---|---|
| **Attack Ship** (`3016`) | Hit all 5 ship targets to sink the ship | 10,000 each, +25,000 jackpot each. All dropped **or** ball in ship sink → **ship sunk**, jackpot awarded (`3017 "Ship Sunk"`). Ball down the **ship wire ramp** → **ship captured**, table multiplier maxed (`3018 "Ship Captured"`). |
| **Treasure Hunt** (`3053`) | Send the ball into the hideaway with the flashing red **"X"** | Each X = a **special treasure**; **5th X = jackpot**. Ball into either east wind lane → an award is **buried** (retrievable later via "buried treasure"). Strings `3034`–`3043` are the ten "<X>\nBuried" messages. |
| **Tavern** (`3025`) | Hit tavern targets and window sinks | Targets 25,000 each, +50,000 jackpot, and light a **keg light** (turning on the magnet beneath). Ball stuck on a keg light = **100,000**. All targets dropped → **jackpot**. |
| **Bermuda Triangle** (`3045`) | Put the ball up the specific wind lane indicated | 125,000 → 250,000 → 500,000 → 1,000,000 → **jackpot**. Any hideaway hit → shipwreck mystery light on. |
| **Storm** | Hit targets to bail water. Severity = number of wind lights lit | **Squall** (`3046`) 10,000/target, jackpot every 8 · **Tropical Storm** (`3047`) 25,000, jackpot every 7 · **Monsoon** (`3048`) 50,000, every 6 · **Hurricane** (`3049`) 100,000, every 5. West wind lane → shoot again light. Any hideaway ends the mode and lights the shipwreck mystery. |
| **Mutiny** (`3051`) | "Put down the rebellion by hitting bumpers" | 25,000 per bumper; **jackpot every 10 bumper hits**. Ship sink → **ship's hold light** on. |
| **Sword Fight** (`3052`) | Hit parrot and volcano targets | 30,000 each. Clearing either group awards one of: **coin award, special treasure, map piece or extra ball**. |
| **Peg Leg's Loot** (`3054`) — finale, unlocked by **all six map pieces** | Put the ball in the **ship sink** | **500,000 per coin award** + **500,000 per special treasure** + **1,000,000 per map piece** + **1,000,000 per mode completed**. Bumpers and the table multiplier are maxed. |

Mode-selection strings `3068`–`3075`: Attack / Treasure Hunt / Tavern / Bermuda / Storm /
Mutiny / Swordfight / Pegleg's Loot "Mode Selected". Hint strings `3060`–`3067`.

Additional non-mode multiball-ish states: **Lava-Ball** (`3055`) — started by hitting all
four volcano targets; **Jail Break!** (`3056`) — started by hitting the brig target 3×
with ≥1 ball in the brig, **or automatically when three balls are caught in the brig**.

## S3. Collectibles

- **Winds** — six wind lanes, each named: **Nor'Easter** (right island ramp),
  **Island Breeze** (left island ramp), **Tradewinds** (left and right orbit),
  **Sou'Easter** (middle-right return lane), **Westerly** (below the tavern),
  **Easterly** (near the parrot). 1st pass 25,000 · 2nd in the same lane 50,000 · 3rd
  75,000. Spelling **WINDS** with the wind letter lights awards a **map piece**.
  (Engine also has `3004 Island Breeze`, `3005 Nor'Easter`, `3006 Port O' Call`,
  `3008 Tradewind Loop`, `3010 Sou'Eastern`, `3011 Easterly`, `3012 Westerly`,
  `3013 WINDS` — note **`Port O' Call`** is a seventh name not in the manual. **[GAP]**)
- **Gold** — spelling **GOLD** with the gold letter lights awards a **coin treasure**.
- **Map pieces** — six; each worth **1,000,000**; all six unlock Peg Leg's Loot.
- **Coin awards** — six, escalating: **50,000 / 100,000 / 200,000 / 400,000 / 800,000 /
  1,600,000**.
- **Special treasures** — six, named: **Gold ingots 100,000** · **Silver chalice 250,000**
  · **Signet ring 500,000** · **The Frantz Ruby 750,000** · **Shapiro's Sceptre 1,000,000**
  · **Crown of Sandige 1,500,000**. (String IDs `3019`–`3024`.)
- **Crew** — bumper upgrade track; `3001 "Crew Added / Bumpers Now %ld"`,
  `3044 "Max Crew / Bumpers Now %ld"`.

## S4. Components

**Bumpers / rebounds / kicker**: 3 **island bumpers** on the upper playfield, 1,500 each,
**+1,500 per 12 hits up to a maximum of 10,500**. Rebounds **350** each. One **kicker** in
the left outlane, activated by either return lane, **5,000**.

**Targets**: **Ship Target Bank** (5 drop targets, 3,000 each, all = 25,000) ·
**Tavern Target Bank** (3, 4,000 each, all = 30,000) · **Parrot Target Bank** (3 drop,
5,000 each, all = 35,000) · **Volcano Target Bank** (4 stationary, 1,000 each, all =
15,000 **and starts Lava-Ball**) · **Captive Ball** (changes next mode + 50,000 outside a
mode; extends the timer during a mode) · **Board Targets** (1 at each end of the ship,
5,000 each; both = 20,000 **and opens the gate in front of the ship wire ramp**) ·
**Brig Target** (5,000; 3 hits with a ball in the brig starts Jail Break).
Every ship/tavern/parrot target hit lights a **SKULDUGGERY** letter light.

**Sinks**: 3 **hideaways** (40,000 each, light a GOLD letter) · **Ship Sink** (100,000) ·
3 **Tavern Sinks** (75,000 each, all three = 200,000).

**Lanes/ramps**: **Ship Wire Ramp** (ship → innermost right return lane, 100,000; a gate
sometimes blocks it) · **Cannon Wire Ramp** (plunger → upper playfield; 50,000 if the ball
comes back down) · **Left/Right Outlane** (25,000, lights a wind letter; right outlane
also awards the shipwreck mystery if the mystery light is on) · **Return Lanes** (10,000
each, light the wind letter above, and turn on the left-outlane kicker) · **Island Lanes**
(right of the volcano; **second** pass through = 1,200; all island lane lights lit →
**table multiplier increased** + 20,000).

**Misc**: **4 flippers** (two by the drain, one on the upper playfield, one just left of
the ship; flippers also **rotate the wind letter lights and island lane lights to the
right**) · **Plunger** always launches with the same power, path varies slightly; 15 s
re-launch · **The Brig** (lower right; traps balls until Jail Break) · **2 accelerators**
(right and left entrances to the rear orbit) · **magnets** active during Storm, Bermuda
Triangle and Tavern modes.

**Lights** (full list in the manual): Bermuda Triangle lights (magnet active in the
centre of the circle when lit), Board, Brig Target, Coin (6), Combination, Extra Ball,
Fathom (6, in the anchor chain), Gold Letter, Hideaway, Hurricane (magnet beneath), Island
Lane (6), Keg (6, magnet beneath each), Kicker, Mode Change, Mode (7, on the wheel),
Peg Leg's Loot, Skulduggery Letter, Special Treasure (6), Ship's Hold, Shipwreck Mystery,
Tavern Window (3), Volcano (4), Wind Letter, Wind (3 per wind lane), "X" lights (6).

## S5. Scoring summary

- **Jackpot**: accumulated, never reduced, **starts at 1,000,000**. Targets **+20,000**,
  sinks and hideaways **+40,000**, **mode completion +750,000**, plus lump sums from
  specific awards.
- **Drain bonus**: **10,000 per target dropped** · **200,000 per mode completed** ·
  **250,000 per map piece** · **100,000 per special treasure**; bonus hold carries the
  previous bonus over. Engine lines `3028 Target Bonus`, `3029 Mode Bonus`,
  `3030 Map Piece Bonus`, `3031 Special Treasures`, `3032 Ship's Hold`, `3033 Total Bonus`.
- **Skill shot**: a rolling list selects the award — SILVER / GOLD / DIAMONDS → gold
  coins; plus Shipwreck Mystery, TREASURE → Treasure Hunt, ATTACK → Attack Ship,
  BERMUDA → Bermuda Triangle, TAVERN → Tavern, STORM → Storm.
- **Reflex shots**: possible in several wind lanes by returning the ball up the same lane
  quickly — **125,000** once, **250,000** twice, **400,000** three or more times.
- **Shipwreck Mystery** (`3009`, `3059`): from the skill shot or the right-outlane mystery
  light. Awards one of: Special Treasure, **Max Crew** (bumpers maxed), **Max Multiplier**,
  Map Piece, Buried Treasure, **Extra Ball**.

## S6. Skulduggery component inventory **[DAT]**

`PIRATES.DAT`: `PARTOUT(4.0)RESOURCE` / `FullTilt`, description **"Pirate's Cove Table"**,
**797 groups** (355 named), `table_objects` 1025-array holds **438** `(type, group)` pairs.
Differences from DK worth noting:

- **4 flippers** (`a_flip1`..`a_flip4`: L, R, L, R) — the upper-playfield and ship
  flippers are mirrored onto the two buttons.
- **11 `TPopupTarget` (1006)** drop targets `a_drop5`…`a_drop15` — DK has none.
- **6 magnets** as first-class objects, type **1042**: `s_magnet1`…`s_magnet6`.
- **1 object of type 1034**: `s_launcher1` (the cannon/plunger launcher).
- **2 `TKickback`** (`a_kick1`, `a_kick2`), **2 accelerators** (type 1036), **3 ramp exits**
  (type 1035), 1 `light_sweeps` (1038).
- **13 `TLightRollover`** named `v_mlite*` and **6 `v_map1`…`v_map6`** light groups for the
  map pieces.
- **22 light groups**, named: `lane_winds`, `island_lane_top`, `island_lane_bottom`,
  `right_wind`, `right_island_combo`, `south_wind`, `west_wind`, `east_wind_1`,
  `east_wind_2`, `left_island`, `left_island_combo`, `left_orbit`, `left_orbit_combo`,
  `right_orbit`, `right_orbit_combo`, `fathom`, `special_treasure`, `coin`, `mode`,
  `keg`, `bermuda`, `map_piece` (each `…_light_group`).

## S7. Gaps for Skulduggery

- Per-component raw score tables (same reason as DK — they live in `PIRATES.EXE` code).
- The seventh wind name **"Port O' Call"** (`3002 Secret Hideaway`, `3006 Port O' Call`,
  `3007 In The Brig` are all engine strings without manual counterparts).
- Mode time limits and the captive-ball extension amount.
- Replay threshold; "special" award (again, apparently absent).

---
---

# Appendix A — cross-source corroboration and extra binary facts

Two independent research passes were run against the same question. Both reached the same
primary source (`FULLTILT.HLP`) and the same DAT extraction, independently, which raises
confidence in every **[MANUAL]** and **[DAT]** line above to "verified twice".

## A1. A better source for the help file, and a proper decompile

- A cleaner copy of the retail CD is at **<https://archive.org/details/CinematronicsFullTiltPinball>**,
  laid out as `FULLTILT/FULLTILT/{CADET,DRAGON,PIRATE}/` plus `FULLTILT/FULLTILT/FULLTILT.HLP`.
  Direct: `https://archive.org/download/CinematronicsFullTiltPinball/FULLTILT%2FFULLTILT%2FFULLTILT.HLP`
- The HLP can be properly decompiled (not just byte-scanned) with the pure-Python
  **`winhlp`** package (`pip install winhlp`). It yields **59 topics, 14 of them Dragon's
  Keep**, with correct table structure, plus the embedded images — including the
  **annotated Dragon's Keep playfield diagram**, which is the one artifact this spec
  cannot reproduce in text and which is worth pulling when mapping DAT objects to named
  components.

## A2. Two different builds of DRAGON.EXE exist — mind which one you RE

- The build analysed for the §2/§4/§6 **[STRINGS]** IDs in this document is the
  **Win16 (NE / `MZP`) build** from the Win 3.x release
  (`full-tilt-pinball-win-3x-en-win-3x-o-release-1`), 646,656 bytes. Its rules text lives
  in `RT_STRING` resource blocks 188–193 → **string IDs 3001–3076**.
- The retail CD copy on `CinematronicsFullTiltPinball` is a **PE32 i386 GUI** build,
  linked **1996-03-17** (same date as `CADET.EXE`), **no debug info**:
  - `DRAGON.EXE` SHA-1 `7BBC2FEF948613644773B7689FB0CCA28C7F1B5C`
  - `PIRATES.EXE` SHA-1 `5EB97B0CF190F628C7F1C02A9D148F27A508E580`
- Expect the same string IDs in the PE build's string table, but **verify** — the NE and
  PE builds are separate links.

## A3. Where the rules actually live (and do not)

- **There is no script, no message table and no mission text in the `.DAT`.** Confirmed by
  both passes: the only type-9 (`String`) entries in `DRAGON.DAT` are the copyright line,
  `'...'`, and 50 `tabc##.wav` filenames. All rules are compiled code in `DRAGON.EXE`.
- In the Space Cadet decompilation the equivalent is `control.cpp` (**4,603 lines**),
  which binds to components **by group name** through a tag table
  (`component_tag<TBumper> control_bump1_tag = {"a_bump1"};`) and drives them with
  `MessageCode`s. A new table therefore means a whole new rules file — and **DK/Pirates
  use different group-naming conventions**, so every existing `component_tag` misses.
- `DRAGON.DAT` has **no `table_size` group** (3DPB's `PINBALL.DAT` does); group 1 is
  `background`, group 2 `camera_info`.
- Dragon's Keep has **`info_text_box` but no `mission_text_box`** — Space Cadet's
  `pb::MissTextBox` has no DK counterpart, i.e. quest text goes through the single info box.
- Per-table MIDI: Space Cadet `TABA1`–`TABA3`; **Dragon's Keep `TABC1`–`TABC7`** (7 tracks);
  Skulduggery `TABB1`–`TABB6`. Sound prefixes `tabc##.wav` / `tabb##.wav`.
- Sound-table sizes: CADET 48, **DRAGON 51**, PIRATES 67 (the Space Cadet port caps
  `loader::sound_list` at 64, which is why Pirates overflows it).

## A4. Loader attribute magics — useful for reading the DAT further

`loader.cpp` reads per-group attributes as **magic-tagged arrays**: the first element of a
`ShortArray` / `FloatArray` entry is the attribute ID.

- `ShortValue == 200` = base group, `201` = state group (`loader::state_id`).
- `300` = material, with float sub-attributes `301` smoothness, `302` elasticity,
  `304` soft-hit sound.
- `400` = kicker, with `401` threshold, `402` boost, `403` throw-ball multiplier,
  `404` direction (3 floats), `405` angle multiplier, `406` hard-hit sound.
- `1025` = `table_objects` component list; `1027` = light-group membership;
  `903` = light-group sweep period.
- **`410` is a Full-Tilt-only kicker sub-attribute** — 8 floats, apparently two points of
  a throw path. It appears in **18 kicker groups in `DRAGON.DAT`** and 14 in
  `PIRATES.DAT`, and **never in `CADET.DAT`**, where `404`/`406` are used instead.
  The Space Cadet loader does not know `410`, so `query_visual` returns early and
  `TKickout` crashes on a null float array.
- Float attribute magics present in `DRAGON.DAT` but not `CADET.DAT`:
  **409, 604, 704, 705, 706, 1306, 1600–1604**. (`704`/`705`/`706` appear in the `table`
  group as 4-float rectangles — see §2.7's dump of group 269 — and `1600`–`1604` are the
  `light_sweeps` timing constants.)
- `PIRATES.DAT` additionally has **1405, 1406**.

## A5. State of the art in the decompilation community

- **k4zmu2a has not started on either table** and says so plainly in
  [issue #148](https://github.com/k4zmu2a/SpaceCadetPinball/issues/148):
  > "Current state of decompilation: 3DPB - done / FT Cadet – partially done /
  > **FT others – not started**" … "It takes a lot of work to decompile FT tables without
  > any symbols. It would be easier if I had an FT build with debug symbols, but I am
  > pretty sure that there is no such build in public access."
  Also [#20](https://github.com/k4zmu2a/SpaceCadetPinball/issues/20),
  [#160](https://github.com/k4zmu2a/SpaceCadetPinball/issues/160) ("The amount of work it
  takes can be compared to doing the whole process all over again, once per table"),
  [#256](https://github.com/k4zmu2a/SpaceCadetPinball/issues/256).
  The repo has **no wiki** and **Discussions are disabled**.
- The furthest anyone has got: **[simonsteiner/SpaceCadetPinball PR #67, "Spike: bonus
  tables without rules"](https://github.com/simonsteiner/SpaceCadetPinball/pull/67)**
  (branch `spike/skulduggery`, opened 2026-10-26, open/unmerged). With rules stubbed and
  two loader fixes (128 sounds, kicker attribute 410), **both tables load, draw, and all
  flippers move; Dragon's Keep plays a complete ball** (plunger → physics → drain → next
  ball). Write-up:
  [`docs/plan/2026-10-26-bonus-tables-spike.md`](https://raw.githubusercontent.com/simonsteiner/SpaceCadetPinball/spike/skulduggery/docs/plan/2026-10-26-bonus-tables-spike.md).
  It estimates **4–7 months** for both tables and proposes one `control_<table>.cpp` per
  table behind a `TableRules` function-pointer struct.
- Format documentation: **`Doc/.dat file format.txt`** in k4zmu2a's repo (authored by
  AdrienTD) and the older copy plus the generic lister `dat_parser/pbwdlist.c` in
  **[AdrienTD/PinballTools](https://github.com/AdrienTD/PinballTools)** (dev ended 2017).
  `pbwdlist.c` works unmodified on all three Full Tilt DATs.
- `Doc/.dat dump.txt` in the repo covers **`PINBALL.DAT` (Space Cadet) only**.
- No GameFAQs / Neoseeker / IGN / GameSpot guide for Dragon's Keep exists.
  Wikipedia, Codex Gamicus and TV Tropes only paraphrase; the
  [Codex Gamicus summary](https://gamicus.fandom.com/wiki/Full_Tilt!_Pinball) is accurate
  but adds nothing. TCRF covers Space Cadet only.

## A6. Extra component facts not in §2.7

- `PIRATES.DAT` contains **one object of type 1037** (group 791, unnamed, carrying only
  float attribute `408` and short `602`). Dragon's Keep has no 1037.
- `PIRATES.DAT` has **no type-1001 `TPlunger`** at all — the launch device is the
  type-1034 `s_launcher1`.
- Type-code totals for the unimplemented range **1034–1043**: CADET **0**,
  DRAGON **16**, PIRATES **14**. That range is the whole delta in component classes
  between Space Cadet and the two Full Tilt-exclusive tables.
- Naming-convention delta (matters when porting `control.cpp`): FT `CADET.DAT` keeps
  3DPB's `lite###` / `a_targ##` / `s_onewy#` / `v_sink#` / `a_flag#` / `*_lights`
  scheme; Dragon and Pirates use `v_lite###` / `a_spot##` / `s_oneway##` / `a_sink##` /
  `<name>_light_group`, plus `s_rexit##`, `a_accl#`, `a_dvtr??`, `s_magnet#`, `pbmsg_ft`,
  `mode_timer`, `light_sweeps`.
- The assertion string `sweep == NULL` is present in all three EXEs, i.e. the
  `light_sweeps` class (type 1038) is part of the shared engine and may also be
  recoverable from `CADET.EXE`.

## A7. Safety note on one source

`https://tcrf.net/Full_Tilt!_Pinball` served **prompt-injection text aimed at AI agents**
instead of game documentation during this research. Nothing from that URL was used here.
Treat it as hostile; the legitimate TCRF article is `tcrf.net/3D_Pinball:_Space_Cadet`,
which covers Space Cadet only.
