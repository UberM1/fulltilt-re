# Dragon's Keep — ball-capturing and path components

Scope: the 22 `DRAGON.EXE` handlers that capture, hold, divert or lose the ball —
sinks, kickouts/kickers, the hole, ramps, ramp exits, the one-way, the accelerator
and the drain.

Sources: `decomp/dragon/functions/*.c` (Ghidra), `decomp/cadet/functions/*.c` with
upstream `control.cpp` as a named oracle, `analysis/dragon-components.txt`,
`docs/dragon-rules-spec.md`.

Everything below is derived from the binary. Where the binary and the manual-derived
spec disagree, the binary wins and the disagreement is called out in §7.

---

## 1. Prerequisites established while doing this

### 1.1 The decomp already carries group-name annotations

`decomp/dragon/functions/*.c` annotate most globals inline, e.g.
`DAT_0043a3c4/*a_sink1*/`. 200+ globals are named this way. That removes most of the
guesswork about *which* component a handler touches. The annotations are consistent
with the component table and with the DAT group names in the spec, so I treat them as
reliable. Globals **not** annotated (`DAT_0043bb6c`, `DAT_00439b1c`, `DAT_00434d20`,
`DAT_00434d24`, `DAT_00434d08`, `DAT_00439b10`, `DAT_00434d2c`) are plain `.data`
variables, not components, and are named by behaviour below.

### 1.2 Object layout (verified against CADET ↔ upstream)

| Offset | Field | How verified |
|---|---|---|
| `+5` (byte) | `TPinballComponent::ActiveFlag` | CADET `GravityWellKickoutControl`: `*(byte*)(param_2+5)=0` ↔ upstream `caller->ActiveFlag = 0` |
| `+6` (int) | `TPinballComponent::MessageField` | CADET `AdvanceWormHoleDestination`: `*(int*)(DAT_0043816c+6)` ↔ upstream `lite4->MessageField` |
| `+0x42` | `TLight::LightOnFlag` | the triple test `+0x42 \|\| +0x4e \|\| +0x46` ↔ upstream `light_on()` |
| `+0x46` | `TLight::FlasherOnFlag` (likely) | used alone in `FUN_00423be0` to mean "this one is flashing" |
| `+0x4e` | third light-on flag | part of the same triple |
| vtbl `+0` | `GetType()` — returns the DAT type code (1006/1017/1019 seen) | `FUN_0042c020` |
| vtbl `+4` | `Message(code, float)` | ubiquitous |
| vtbl `+0xc` | `get_scoring(index)` | CADET `BlackHoleKickoutControl` ↔ upstream `caller->get_scoring(0)` |
| vtbl `+0x18` | `TSound::Play()` — zero args, **returns a float duration** (callers do `FSTP ST0` to discard it) | annotation names + `0x427100` asm |

Calling conventions, from the `0x427100` listing:

* handlers are `__stdcall (int code, TPinballComponent* caller)` — `RET 0x8`
* `Message` is `__thiscall (int code, float value)` — `this` in `ECX`, two pushed args
* `FUN_004230a0` is `__cdecl AddScoreFrom(cmp, index)` and **returns the score actually
  added in EAX** (`PUSH EAX` straight into the following `sprintf`), i.e. it is the
  `TPinballTable::AddScore` return value after multiplier/bonus flags
* `FUN_0041de40` is `__thiscall TTextBox::Display(text, float time, int, int)` — one more
  int argument than upstream's `Display(text, time, lowPriority)`

`TPinballTable*` is `DAT_00439b00`. Fields recovered (all cross-checked against CADET's
`BallDrainControl`/`LaunchRampControl` against the named upstream source, except the two
marked *Dragon-only*):

| Offset | Field |
|---|---|
| `+0x102` | `PlayerCount` |
| `+0x106` | `CurrentPlayer` |
| `+0x15e` | `ScoreMultiplier` (**Dragon's neutral value is 1, not 0** — see `FUN_00423130`) |
| `+0x166` | `ReflexShotScore` (unused by Dragon as far as I can see) |
| `+0x16a` | `BonusScore` |
| `+0x16e` | `BonusScoreFlag` |
| `+0x172` | `JackpotScore`, clamped to 10,000,000 |
| `+0x176` | `JackpotScoreFlag` |
| `+0x17a` | **super jackpot**, clamped to 20,000,000 — *Dragon-only* |
| `+0x186` | `BallCount` |
| `+0x18e` | `ExtraBalls` |
| `+0x192` | `MultiballCount` (balls in play) |
| `+0x19e` | `UnknownP78` |
| `+0x1b2` | `TiltLockFlag` |
| `+0x6a` | unknown flag, set to 1 when a ball is saved — *Dragon-only*, **UNRESOLVED** |

### 1.3 Message codes

The brief's "+10" rule is wrong in detail. The real 1996 enum is upstream's enum with
**extra codes interleaved**, so the delta grows with the value. Codes pinned by
CADET↔upstream equality (not arithmetic):

| Binary | Upstream name | Pinned by |
|---|---|---|
| `1`,`7`,`0xb`,`0xf`,`0x10`,`0x13`,`0x14`,`0x17` | `TLightTurnOn`, `TLightFlasherStartTimed`, `TLightSetOnStateBmpIndex`, `TLightFlasherStartTimedThenStayOn`, `…ThenStayOff`, `TLightResetAndTurnOn`, `TLightResetAndTurnOff`, `TLightSetMessageField` | identical values; `0x13`/`0x17`/`0xb` via `AdvanceWormHoleDestination` |
| `0x19`, `0x1a` | `TLightFtTmpOverrideOff`, `TLightFtResetOverride` (upstream `-25`, `-26`) | position; FT-only light override block occupies real 24–26 |
| `0x25`,`0x26`,`0x29`,`0x2d`,`0x30`,`0x33` | `TLightGroupOffsetAnimationBackward`(33), `TLightGroupReset`(34), `…GetOnCount`(37), `…ResetAndTurnOn`(41), `…FlashWhenOn`(44), `…CountdownEnded`(47) | all exactly **upstream+4**, each confirmed by a named CADET handler |
| `0x37`, `0x38` | `TBlockerDisable`(51), `TBlockerEnable`(52) | `DrainBallBlockerControl` |
| `0x3e`, `0x3f` | `TGateDisable`(53), `TGateEnable`(54) | `LeftKickerControl` + `LeftKickerGateControl` |
| `0x40` | `TKickoutRestartTimer`(55) | `BlackHoleKickoutControl` |
| `0x41` | `TSinkResetTimer`(56) | `EscapeChuteSinkControl` (one-liner, unambiguous) |
| `0x42` | `ControlBallCaptured`(57) | arithmetic continuation + `s_hole118` semantics; **medium confidence** |
| `0x43` | **FT-only `THole` timed-release/hold code** (no upstream equivalent) | `s_hole118` and `FUN_004241c0` both *send* it to a hole with a float; **inferred** |
| `0x44` | `ControlBallReleased`(58) | `LaunchRampHoleControl` |
| `0x45` | `TBlockerRestartTimeout`(59) | `DrainBallBlockerControl` |
| `0x46` | `ControlTimerExpired`(60) | `MissionControl` prologue, many |
| `0x4a` | `ControlCollision`(63) | everywhere |
| `0x4b`, `0x4c` | `ControlEnableMultiplier`(64), `ControlDisableMultiplier`(65) | `GravityWellKickoutControl`, `PlungerControl` |
| `0x4d` | **FT-only "you are no longer the active scorer"** | only emitted by Dragon's `AddScore` wrappers; **inferred**, see §1.4 |
| `0x4f`, `0x50` | `ControlMissionComplete`(66), `ControlMissionStarted`(67) | CADET `MissionControl` prologue ↔ upstream lines 2318/2330 |
| `0x51` | **FT-only "mission restarted"** | emitted only by the fire-lizard sinks when restoring a saved quest; **inferred** |
| `0x53` | **FT-only "ball drained"** | emitted only by `Dragon_drain_Control`; **UNRESOLVED** |
| `0x55` | **FT-only ramp-exit traversal** | the trigger for `s_rexit18`/`s_rexit19`; **UNRESOLVED** |
| `0x56` | **FT-only ramp traversal** | the trigger for `a_ramp103`/`a_ramp6`; **UNRESOLVED** |
| `0x3f3` | `SetTiltLock`(1011) | sent to flippers to kill them |
| `0x3f9` | `PlungerStartFeedTimer`(1016) | CADET `BallDrainControl` ↔ upstream |
| `0x3fb` | `PlungerRelaunchBall`(1018) | same |
| `0x402` | `Reset`(1024) | `GravityWellKickoutControl` |

Public codes ≥1016 are **+1** and ≥1021 are **+2** relative to upstream's reconstruction;
codes ≤1012 are unshifted. `0x42`, `0x49`, `0x43`, `0x4d`, `0x4e`, `0x51`–`0x56` are real
codes with no upstream counterpart.

Summarised as bands (independently derived here, and matching the coordinator's
piecewise correction): `TLight` **+0**; `TLightGroup` / `TPopupTarget` / `TSoloTarget` /
`TComponentGroup` / `TBlocker`(51,52) **+4**; gate / kickout / sink (53–56) **+9**;
`Control*` **+10**, except `ControlCollision` **+11** and the two mission codes **+13**
(an extra FT-only code sits at `0x49`, and two more at `0x4d`/`0x4e`). Every sink and
kickout code in this document — `0x40` `TKickoutRestartTimer`, `0x41` `TSinkResetTimer`,
`0x3e`/`0x3f` the gate pair — is in the **+9** band and was pinned by CADET↔upstream
equality, not by the flat +10 rule, so nothing here needed revising.

### 1.4 Dragon's shared helpers (all shared with the mission controllers)

| Address | Name I give it | Body |
|---|---|---|
| `0x423060` | `control::handler` | **calls `MissionControl` FIRST**, then `cmp->Control->ControlFunc`. Upstream does the reverse. This matters: a Dragon mission controller sees `ControlCollision` *before* the component handler, which is why so many component handlers suppress their own default scoring during a quest. |
| `0x4230a0` | `AddScoreFrom(cmp, scoreIndex)` | `if (lastScorer != cmp) { if (lastScorer) handler(0x4d, lastScorer); lastScorer = cmp; } table->AddScore(cmp->get_scoring(i))`. `lastScorer` = `DAT_00434d20`. |
| `0x4230f0` | `AddScoreFrom(cmp, literalScore)` | same bookkeeping, explicit amount |
| `0x423130` | `SpecialAddScore(cmp, score)` | zeroes `BonusScoreFlag`/`JackpotScoreFlag`, sets `ScoreMultiplier = 1`, adds, restores — structurally identical to upstream `SpecialAddScore` |
| `0x4231e0` | `table_add_extra_ball(time)` | `ExtraBalls = 1` (**assignment, not `++`**), play `tabc21wave`, show string 3011 |
| `0x423240` | `AwardShieldArmor()` | `a_kick1->ActiveFlag = 1`, `v_lite37->MessageField = 0`, `v_lite37->Message(…ThenStayOn, 2.0)`, `outlane_kicker_light_group->Message(…ThenStayOn)`, text, `tabc41wave` |
| `0x4232b0` | `AwardDragonEggs(n)` | `n ×  dragon_egg_light_group->Message(TLightGroupResetAndTurnOn, 1.0)`, `tabc27wave`, then `AddScoreFrom(nullptr, GetOnCount × 100000)` and a text line indexed by the on-count |
| `0x4234b0` | `AwardNextArmor()` | random pick among the un-lit of `v_lite37/38/39` → Shield (25,000) / Helm of Terror (`a_center_post` blocker enabled, 50,000) / Chain mail (`a_bloce` blocker enabled, 100,000) |
| `0x4237f0` | `CastRandomSpell()` | `FUN_00423600` picks a random un-lit spell among `v_lite28/29/31/32`, `FUN_004236b0` applies it: Dragon Breath (`v_lite28` on for 30 s) / Dragon Blood (`v_lite29`+`v_lite30`, `a_kout3->ActiveFlag=1`) / Resistance to Fire (`v_lite31`) / Stealth (`a_blocf` blocker *disabled*). Awards 120,000, bumps a spell counter `DAT_00434d24` |
| `0x423870` | `AddSuperJackpot(n)` | `+0x17a += n`, clamp 20,000,000 |
| `0x4238a0` | `AddJackpot(n)` | `+0x172 += n`, clamp 10,000,000 |
| `0x423b80` | `DisableTowerLocks()` | all three lock lights `TLightFtTmpOverrideOff`, `a_sink18/17->ActiveFlag = 0`, all three `MessageField = 0` |
| `0x423be0` | `ArmNextTowerLock()` | `TLightFtResetOverride` on all three; arm exactly the first *flashing* one (`MessageField = 1`, matching sink `ActiveFlag = 1`); if none flashing and **all three are solid-on**, set quest = `0xd` and `MissionControl(ControlMissionComplete)` |
| `0x424210` | `TryStartSelectedQuest()` | `if (a_kout2->MessageField) { quest = a_kout2->MessageField; v_lite46->Message(TLightResetAndTurnOff); a_rockb->Message(TBlockerEnable, 2.0); MissionControl(ControlMissionComplete, nullptr); return 1 } return 0` |
| `0x424490` | `CollectJackpot(ret, showText)` | `tabc43wave`, `AddScoreFrom(nullptr, table->JackpotScore)`, optional text |
| `0x4244e0` | `DragonSpelled()` | if **all six** of `v_lite22..v_lite27` are on → quest = `0xf`, `MissionControl(ControlMissionComplete)`, return 1 |
| `0x425c30` | `FireLizardSinkCommon(caller)` | the shared tail of `a_sink1`/`a_sink2`, see §3.1 |
| `0x42c020` | `MissionControl(code, cmp)` | prologue + dispatch on `info_text_box->MessageField` via `0x42c120` |
| `0x42c4cb` | `rand()` | LCG `x = x*0x343fd + 0x269ec3`, returns bits 16–30. `(rand()*N)>>15` is the idiom for "random in [0,N)" |
| `0x42c76f` | `sprintf` | |
| `0x409730` | `pb::get_rc_string(id)` | the ids **are** the spec's string ids in decimal (`0xbc2` = 3010, etc.) |
| `0x41de40` | `TTextBox::Display(text, time, lowPriority, ?)` | all 89 call sites pass `info_text_box` |
| `0x419f10` | `TPinballTable::AddScore` | |

### 1.5 The quest-state variable, and the quest table

`FUN_0042c020` is byte-for-byte the shape of upstream's `MissionControl` prologue, and
dispatches on `*(int*)(DAT_00439d1c + 6)`. `DAT_00439d1c` is annotated
**`info_text_box`**.

So: **Dragon's Keep has no `lite198` equivalent. The current quest/mode number is stored
in the `MessageField` of the one and only text box.** All 89 text displays go through the
same object. This is not a Ghidra artefact — the same pointer variable is dereferenced
both ways in the same functions.

Mode table recovered from `FUN_0042c120` plus the evidence in each handler:

| Mode | Controller | Identification | Confidence |
|---|---|---|---|
| 0 | `0x42bf60` | pre-launch / waiting (upstream `WaitingDeploymentController`) | high |
| 1 | — (empty case) | "none"; the drain sets it when releasing locked balls | high |
| 2 | `0x428800` | **normal play, no quest active** | high |
| 3 | `0x428870` | **post-quest 10 s restart window** — `mode_timer->MessageField` holds the quest to restart, `-1` when spent | high |
| 4 | `0x428a80` | **Dragon Hoard** | high |
| 5 | `0x4291c0` | **Fire Lizards** (string 3029 "Fire Lizard Slain") | high |
| 6 | `0x429510` | **Rescue Maiden** | medium |
| 7 | `0x429b10` | **Dragon Pass** (armor kickout → jackpot, gargoyle kickout → egg) | high |
| 8 | `0x429ec0` | **Wizard's Fury** (the well's 5-way mystery award) | high |
| 9 | `0x42a150` | **Slay Dragon** (`dragon_fire_light_group`, `a_sink19`, string 3049) | high |
| 10 | `0x4285f0` | unknown; `s_hole118` parks itself (`MessageField = 2`) instead of capturing | low |
| 0xb | `0x42b7c0` | end-of-ball / bonus count-up | high |
| 0xc | `0x42ac80` | entered from `s_hole118` in single-ball normal play; its body switches on `a_kout2->MessageField` → the **Dragon Egg Kickout exchange offer** | medium |
| 0xd | `0x42b0e0` | entered when all three tower locks are full → **Tower Barrage** (3-ball, string 3051) | medium-high |
| 0xe | `0x42b3c0` | entered from `a_kout2` in normal play → **Gargoyle Gaggle-Ball** (2-ball) | high |
| 0xf | `0x42b510` | entered when D-R-A-G-O-N is spelled → **Dragon Flurry** (5-ball, string 3063)? | low-medium |

Quest ids **4–8** are confirmed independently: `a_spot27` (the quest selection target,
5,000 pts) calls `FUN_00423e90` (= `SelectNextQuest`) and prints string `0xbff + (id-4)`,
i.e. 3071–3075 = the five "<name> Quest Selected" strings in the spec's order
(Dragon Hoard, Fire Lizards, Rescue Maiden, Dragon Pass, Wizard's Fury). The selected
quest lives in `a_kout2->MessageField`.

---

## 2. Component map for this scope

| Score component | Handler | Scores | What it is | CADET analogue |
|---|---|---|---|---|
| `drain` | `0x424900` | — | the drain | `BallDrainControl` |
| `a_sink1` | `0x425ea0` | 5,000 | Fire Lizard Sink (left) | — (no CADET equivalent) |
| `a_sink2` | `0x426000` | 5,000 | Fire Lizard Sink (right) | — |
| `a_sink4` | `0x426140` | — | **Well Sink** (`well_light_group`) | closest: `HyperspaceKickOutControl` |
| `a_sink15` | `0x426f90` | 100,000 | tower-lock entrance A → `a_sink18` | — |
| `a_sink18` | `0x427100` | 100,000 | tower-lock sink 1 (light `v_lite48`) | — |
| `a_sink13` | `0x427290` | 200,000 | tower-lock entrance B → `a_sink17` | — |
| `a_sink17` | `0x4273d0` | 200,000 | tower-lock sink 2 (light `v_lite49`) | — |
| `a_sink9` | `0x4274d0` | 300,000 | tower-lock entrance C → `a_sink16` | — |
| `a_sink10` | `0x427680` | 300,000 | tower-lock sink 3 entrance (light `v_lite50`) | — |
| `a_sink19` | `0x427f10` | — | **the dragon's mouth** (Slay Dragon) | `HyperspaceKickOutControl` |
| `a_kick1` | `0x426d00` | — | left-outlane **kicker / ball save** | `LeftKickerControl` |
| `a_kout1` | `0x426d80` | — | **Armor Kickout** | `GravityWellKickoutControl` |
| `a_kout2` | `0x426e40` | — | **Gargoyle Kickout** | `BlackHoleKickoutControl` |
| `a_kout3` | `0x427ef0` | — | the Dragon Blood magnet/kickout | — |
| `s_hole118` | `0x426ee0` | — | quest-staging hole | `LaunchRampHoleControl` |
| `a_ramp103` | `0x4265e0` | — | **Dragon Ramp** | `LaunchRampControl` |
| `a_ramp6` | `0x426740` | — | **Tower Ramp** | `LaunchRampControl` |
| `s_rexit19` | `0x426be0` | — | **Gargoyle Ramp** exit (2 eggs) | — |
| `s_rexit18` | `0x426c20` | — | **Forest Ramp** exit (1 egg) | — |
| `s_oneway5` | `0x4267f0` | 25k/75k/100k/125k/150k | **Dragon Chute** | `DeploymentChuteToEscapeChuteOneWayControl` |
| `a_accl1` | `0x426940` | 75,000 / 500,000 | **Moat Chute** accelerator | — |

Eight gates named `a_dvtraa/ab/ba/bb/ca/cb/da/db` are **diverters** in four
enable/disable pairs. Pair assignment: `a` → `a_sink9`, `b` → `s_rexit18`,
`c` → `s_rexit19`, `d` → `a_sink10` / `a_ramp103` / `s_oneway5`.

---

## 3. The sinks

### 3.1 Fire Lizard Sinks — `a_sink1` (0x425ea0), `a_sink2` (0x426000)

**What they do.** 5,000 points each. They are the quest-starting mechanism and, during
mode 5, the quest itself.

Three branches on the quest number:

* **mode 3 (restart window)** — turn both fire-lizard lights off, copy
  `mode_timer->MessageField` back into the quest variable, `MissionControl(0x51, nullptr)`,
  then set `mode_timer->MessageField = -1` and kick the ball out. This is the manual's
  *"a quest can be restarted … within ten seconds"*, implemented by stashing the finished
  quest id on `mode_timer`.
* **mode 5 (Fire Lizards quest)** — flash the partner light and fall into the common tail.
* **anything else** — turn this sink's own light on + flash it, flash the partner light,
  score 5,000, fall into the common tail.

**The shared counter.** `FUN_00425c30` (the common tail) keys everything off
`v_lite63->MessageField` — *one* counter shared by both sinks:

```
v_lite63->MessageField++;
if (MessageField < 2)  -> play tabc16wave, kick the OTHER sink out
else                   -> MessageField = 0;
                          v_lite22->Message(ThenStayOn, 1.0);   // the 'D' of D-R-A-G-O-N
                          if (DragonSpelled()) return;
                          if (TryStartSelectedQuest()) return;
                          a_sink1->Message(TSinkResetTimer, -1)
```

That is exactly the manual's *"two fire-lizard-sink hits (or one sink twice) starts the
next available quest"*, and it also lights one of the six DRAGON letter lights.

In mode 5 the tail instead runs the Fire Lizards scoring ladder, on two more
`MessageField`s used as counters:

```
if (v_lite91->MessageField < 10) v_lite91->MessageField++;       // the 100k multiplier
n = a_sink2->MessageField;                                        // the 0..3 jackpot cycle
if (n < 0 || n > 2) { CollectJackpot(); n = 0; v_lite91->Message(ThenStayOn, 1.0); }
else { if (n == 2) v_lite91->Message(FlasherStartTimed, -1.0);
       n++;
       AddScoreFrom(caller, v_lite91->MessageField * 100000);
       Display(sprintf(get_rc_string(3029)), 2.0); }      // "Fire Lizard Slain\n%ld"
a_sink2->MessageField = n;
```

100,000 × n capped at 1,000,000, jackpot on every 4th hit — the manual's numbers exactly.

**Surprise, flagged.** In every branch the sink that is *kicked out* is the **other** one
(`if (caller != a_sink1) a_sink1->…; else a_sink2->…`), and the light flashed is the other
sink's light. This is consistent across all three branches of `FUN_00425c30` and the two
handlers, so it is not a one-off decompiler slip — but I cannot explain the design, and
the register allocation in these two functions is visibly broken (`piVar3 = (int*)0x14`,
`unaff_retaddr`), so treat the *polarity* of the comparison as only medium confidence.

```cpp
// a_sink1 / a_sink2 — Fire Lizard Sinks. 5,000 each.
void control::FireLizardSinkControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    auto quest = info_text_box->MessageField;
    const bool isSink1 = (caller == a_sink1);
    auto* ownLight     = isSink1 ? lite63 : lite64;
    auto* partnerLight = isSink1 ? lite66 : lite65;

    if (quest == Quest::RestartWindow)      // 3
    {
        lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0);
        lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0);
        info_text_box->MessageField = mode_timer->MessageField;
        MissionControl(MessageCode::ControlMissionRestarted /*0x51*/, nullptr);
        mode_timer->MessageField = -1;
        caller->Message(MessageCode::TSinkResetTimer, -1.0f);
        return;
    }

    if (quest != Quest::FireLizards)        // 5
    {
        ownLight->Message(MessageCode::TLightTurnOn, 0.0);
        ownLight->Message(MessageCode::TLightFlasherStartTimed, kFireLizardFlashTime);
        partnerLight->Message(MessageCode::TLightFlasherStartTimed, kFireLizardFlashTime);
        AddScoreFrom(caller, 0);            // 5,000
    }
    else
    {
        partnerLight->Message(MessageCode::TLightFlasherStartTimed, kFireLizardFlashTime);
        AddScoreFrom(caller, 0);
    }
    FireLizardSinkCommon(caller);
}

void control::FireLizardSinkCommon(TPinballComponent* caller)
{
    // NOTE: the binary always acts on the *other* sink of the pair. Unexplained.
    auto* otherSink  = (caller != a_sink1) ? a_sink1 : a_sink2;
    auto* otherLight = (caller != a_sink1) ? lite66  : lite65;
    auto quest = info_text_box->MessageField;

    if (quest == Quest::NormalPlay)         // 2
    {
        if (++lite63->MessageField < 2)
        {
            soundwave16->Play(nullptr, "FireLizardSink1");
            otherSink->Message(MessageCode::TSinkResetTimer, -1.0f);
            return;
        }
        lite63->MessageField = 0;
        lite22->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);  // D-R-A-G-O-N
        questStartPending = true;
        if (DragonSpelled())    return;     // -> mode 0xf
        questStartPending = false;
        if (TryStartSelectedQuest()) return;
        a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
        return;
    }

    if (quest != Quest::FireLizards)        // 5
    {
        soundwave16->Play(nullptr, "FireLizardSink2");
        otherSink->Message(MessageCode::TSinkResetTimer, -1.0f);
        otherLight->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
        return;
    }

    soundwave16->Play(nullptr, "FireLizardSink3");
    otherSink->Message(MessageCode::TSinkResetTimer, -1.0f);
    otherLight->Message(MessageCode::TLightFlasherStartTimed, 1.0f);

    if (lite91->MessageField < 10)
        lite91->MessageField++;

    char Buffer[64];
    int n = a_sink2->MessageField;
    if (n < 0 || n > 2)
    {
        CollectJackpot(true);               // every 4th hit
        n = 0;
        lite91->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
    }
    else
    {
        if (n == 2)
            lite91->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
        n++;
        AddScoreFrom(caller, lite91->MessageField * 100000);
        snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3029), /*score*/);
        info_text_box->Display(Buffer, 2.0);
    }
    a_sink2->MessageField = n;
}
```

### 3.2 Well Sink — `a_sink4` (0x426140)

**What it does.** The manual's *"ball entering casts a random spell or starts a quest"*,
plus the full **Wizard's Fury** mystery-award machine. Confirmed by the annotation
`well_light_group`.

* **modes 2 and anything but 8** — coin-flip: on a 1, try to start the selected quest
  (and if there is none, kick out); on a 0, `CastRandomSpell()`. Then kick out and flash
  `well_light_group` + `spell_spot_light_group`.
* **mode 8 (Wizard's Fury)** — a 5-state machine on `a_sink4->MessageField`:

| MessageField | Award | Award string | then re-armed to a random other state, announced with |
|---|---|---|---|
| 0 | extra ball (`table_add_extra_ball(2.0)`) | — | 3039 |
| 1 | **max weapons** — all four `v_lite33..36` on, `ScoreMultiplier = 5` | 3045 | 3040 |
| 2 | **max armor** — Shield + `a_center_post` + `a_bloce` all enabled | 3044 | 3041 |
| 3 | **jackpot** (`CollectJackpot`) | — | 3042 |
| 4 | **1,000,000** | 3046 | 3043 |

The spec's physical order (top spell target = 1,000,000 … bottom = extra ball) is the
reverse of the binary's state numbering, and strings 3039–3043 line up exactly with the
spec's "Mystery Bonus set to:" set. The re-arm is `rand()%4` over the *other* four states.

```cpp
// a_sink4 — the Well Sink.
void control::WellSinkControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    auto quest = info_text_box->MessageField;

    if (quest == Quest::NormalPlay || quest != Quest::WizardsFury)
    {
        if (quest == Quest::NormalPlay && (rand() * 2 >> 15) == 1)
        {
            questStartPending = true;
            if (!TryStartSelectedQuest())
            {
                questStartPending = false;
                a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
            }
        }
        else
        {
            CastRandomSpell();
        }
        a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
        well_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
        spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
        return;
    }

    // Wizard's Fury: pay out the armed mystery award, then re-arm at random.
    Msg announce;
    switch (a_sink4->MessageField)
    {
    case 0:  table_add_extra_ball(2.0f); break;
    case 1:  lite33->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
             lite34->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
             lite35->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
             lite36->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
             TableG->ScoreMultiplier = 5;
             info_text_box->Display(pb::get_rc_string(Msg::STRING3045), 2.0);  // Max Weapons
             break;
    case 2:  AwardShieldArmor();
             soundwave24->Play(nullptr, "WellSinkMaxArmor");
             DrainBallBlockerControl(MessageCode::TBlockerEnable, a_center_post);
             BlockerEControl(MessageCode::TBlockerEnable, a_bloce);
             info_text_box->Display(pb::get_rc_string(Msg::STRING3044), 2.0);  // Max Armor
             break;
    case 3:  CollectJackpot(true); break;
    default: AddScoreFrom(caller, 1000000);
             info_text_box->Display(pb::get_rc_string(Msg::STRING3046), 2.0);  // One Million
             break;
    }

    // re-arm: pick uniformly among the four states that are not the one just paid out
    int pick = rand() * 4 >> 15;    // the binary open-codes this as a per-case chain
    a_sink4->MessageField = pick;
    announce = static_cast<Msg>(3039 + pick);
    info_text_box->Display(pb::get_rc_string(announce), 4.0);
    a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
}
```

*Uncertainty:* the re-arm is written as five near-identical `rand()%4` chains, one per
case, each skipping the current state. I believe the collapsed form above is equivalent,
but the per-case chains should be transcribed literally for a bit-exact port.

### 3.3 The three tower locks — `a_sink15`/`a_sink18`, `a_sink13`/`a_sink17`, `a_sink9`/`a_sink10`

**This is the answer to the brief's question about the 100k/200k/300k ladder, and it is
not the egg mechanic.** These six score components are *two entrances each* to **three
physical lock sinks**:

| Entrances | Lock sink | Lock light | Score |
|---|---|---|---|
| `a_sink15`, `a_sink18` | `a_sink18` | `v_lite48` | 100,000 |
| `a_sink13`, `a_sink17` | `a_sink17` | `v_lite49` | 200,000 |
| `a_sink9`, `a_sink10` | `a_sink16` | `v_lite50` | 300,000 |

The ladder is **per tower, not per lock order** — but because `ArmNextTowerLock()`
(`0x423be0`) always arms them in the order `v_lite48 → v_lite49 → v_lite50`, the effective
sequence a player sees is 100k, 200k, 300k. That reconciles the binary with the manual's
*"Locked ball: 100,000 first, then 200,000, then 300,000"*.

**Yes, they share state**, three pieces of it:

1. `v_liteNN->MessageField != 0` means "this tower's lock is **armed**". Set by
   `ArmNextTowerLock()`, cleared by `DisableTowerLocks()`. The manual's
   *"Tower Lock Enabled"* (string 3003) comes from the tower target bank.
2. `v_liteNN->LightOnFlag` (solid on) means "a ball is **locked** here".
3. `a_sink18->ActiveFlag` / `a_sink17->ActiveFlag` are toggled so that only the armed
   tower actually swallows the ball.

The lock sequence in each handler is identical:

```
if (lockLight->MessageField != 0) {                 // armed -> LOCK
    tabc10wave->Play();
    lockLight->Message(TLightResetAndTurnOn, 0);    // solid = locked
    table->MultiballCount--;                        // this ball is gone
    s_plunger1->Message(PlungerRelaunchBall, 0.96f);// feed a replacement
    AddScoreFrom(caller, 0);                        // 100k / 200k / 300k
    Display(sprintf(get_rc_string(3010)), 2.0);     // "Ball Trapped\n%ld"
    ArmNextTowerLock();                             // -> mode 0xd when all three are on
} else {                                            // not armed -> just kick out
    ... route the diverters ...
    lockSink->Message(TSinkResetTimer, -1.0f);
}
```

String 3010 *"Ball Trapped"* is the clincher: these are ball locks, not collectibles.
`ArmNextTowerLock()` starting mode `0xd` when all three are solid matches the spec's
3-ball **Tower Barrage**.

Per-handler differences:

* `a_sink15` (0x426f90) — guards on `quest != 6`; in quest 6 (Rescue Maiden) the ball is
  instead ejected from a **random fire lizard sink** (`rand()%2` over `a_sink1`/`a_sink2`,
  flashing that sink's light). A teleport effect.
* `a_sink18` (0x427100) — **transcribed from the raw listing, see §3.3.1.** Locks only in
  mode 2; outside modes 2 and 6 it kicks out with `-1.0` and calls `DisableTowerLocks()`;
  in mode 2 but unarmed it kicks out with `0.0` and calls `ArmNextTowerLock()`; in mode 6
  it ejects the ball from a random fire lizard sink and never locks.
* `a_sink13` (0x427290) — locks in mode 2; outside 2 and 6 it plays `tabc04wave` and kicks
  out; in mode 6 it kicks out silently (the quest controller does the talking).
* `a_sink17` (0x4273d0) — locks in mode 2; otherwise kicks out and `DisableTowerLocks()`.
* `a_sink9` (0x4274d0) / `a_sink10` (0x427680) — lock in mode 2; outside modes 6 and `0xd`
  they also flip a diverter pair before kicking out: `a_sink9` sets
  `v_lite47` off + `a_dvtraa->TGateEnable` + `a_dvtrab->TGateDisable`; `a_sink10` sets
  `v_lite125` off, `v_lite126` flash-then-on, `a_dvtrda->TGateEnable`,
  `a_dvtrdb->TGateDisable`. `a_sink10`'s block is **character-for-character the same** as
  `a_ramp103`'s, so it is a shared "set the diverter to route X" idiom.
* `a_sink9`/`a_sink10` additionally omit the `s_plunger1` relaunch in the lock path — they
  only decrement `MultiballCount` and score. Possibly a decompiler omission; **flagged**.

```cpp
// a_sink15 / a_sink18 / a_sink13 / a_sink17 / a_sink9 / a_sink10 — tower lock sinks.
// One handler per entrance in the original; this is the common shape.
void control::TowerLockSinkControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    auto quest = info_text_box->MessageField;
    auto* lockLight = TowerLockLightFor(caller);    // lite48 / lite49 / lite50
    auto* lockSink  = TowerLockSinkFor(caller);     // a_sink18 / a_sink17 / a_sink16

    if (quest == Quest::RescueMaiden)               // 6, a_sink15/a_sink18 only
    {
        soundwave51->Play(nullptr, "TowerLockTeleport");
        bool right = (rand() * 2 >> 15) != 0;
        (right ? a_sink1 : a_sink2)->Message(MessageCode::TSinkResetTimer, -1.0f);
        (right ? lite66  : lite65 )->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
        return;
    }

    if (lockLight->MessageField != 0)               // armed: lock the ball
    {
        char Buffer[64];
        soundwave10->Play(nullptr, "TowerLockTrap");
        lockLight->Message(MessageCode::TLightResetAndTurnOn, 0.0);
        TableG->MultiballCount--;
        TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);
        AddScoreFrom(caller, 0);                    // 100k / 200k / 300k
        snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3010), /*score*/);
        info_text_box->Display(Buffer, 2.0);
        ArmNextTowerLock();                         // may start Tower Barrage (mode 0xd)
        return;
    }

    // not armed: route the diverter for this tower, then spit the ball back out
    if (quest != Quest::RescueMaiden && quest != Quest::TowerBarrage)
        SetDiverterRoute(caller);
    lockSink->Message(MessageCode::TSinkResetTimer, -1.0f);
}
```

#### 3.3.1 `a_sink18` (0x427100) from the raw listing — locked-ball scoring settled

The Ghidra output for this one is unusable (it inferred one parameter for a `RET 0x8`
`__stdcall` function, so every `abStack_50[…]` byte store is a misattributed argument
push). The 108-instruction listing in `decomp/dragon/asm/0x427100.txt` resolves it
completely. Transcription:

```cpp
// a_sink18 — tower lock sink 1 (light v_lite48, 100,000). __stdcall.
void __stdcall control::TowerLock1SinkControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)              // [ESP+0x44] vs 0x4a
        return;

    switch (info_text_box->MessageField)                    // [0x439d1c] -> +6
    {
    case Quest::NormalPlay:                                 // 2
        if (lite48->MessageField != 0)                      // armed -> LOCK
        {
            char Buffer[0x40];
            soundwave10->Play();                            // tabc10wave, float retval discarded
            TableG->MultiballCount--;                       // DEC [ecx+0x192]
            lite48->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
            TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);
            int added = AddScoreFrom(caller, 0);            // [ESP+0x48]; returns the score
            snprintf(Buffer, sizeof Buffer,
                     pb::get_rc_string(Msg::STRING3010), added);   // "Ball Trapped\n%ld"
            info_text_box->Display(Buffer, 2.0f, 0, 0);
            ArmNextTowerLock();
        }
        else                                                // not armed -> plain kickout
        {
            a_sink18->Message(MessageCode::TSinkResetTimer, 0.0f);   // note: 0.0, not -1.0
            ArmNextTowerLock();
        }
        return;

    case Quest::RescueMaiden:                               // 6
        soundwave51->Play();                                // tabc51wave
        if ((rand() * 2 >> 15) != 0)
        {
            a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
            lite66->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
        }
        else
        {
            a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
            lite65->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
        }
        return;                                             // the tower never locks here

    default:
        a_sink18->Message(MessageCode::TSinkResetTimer, -1.0f);
        DisableTowerLocks();
        return;
    }
}
```

**The locked-ball scoring is settled.** `AddScoreFrom(caller, 0)` is
`caller->get_scoring(0)` = `a_sink18`'s single declared score, **100,000**, passed through
`TPinballTable::AddScore`; the function's EAX return (the amount actually credited, after
`ScoreMultiplier` and the bonus/jackpot flags) is what gets formatted into string 3010
*"Ball Trapped\n%ld"*. There is no hidden ladder and no shared counter in the scoring path
— the escalation a player sees is purely the arming order imposed by
`ArmNextTowerLock()` (100k sink first, then 200k, then 300k). Nothing further is needed to
reimplement it.

Corrections this forces on §3.3 above:

* the unarmed kickout inside mode 2 uses **`0.0`** (the sink's own `TimerTime`
  is *not* substituted — `TSink::Message` only substitutes on a negative value), while the
  out-of-quest kickout uses **`-1.0`**. I had both as `-1.0`.
* mode 2 ends with `ArmNextTowerLock()` on **both** paths, not only the lock path.
* mode 6 calls `DisableTowerLocks()` on neither path; only the `default` branch does.
* `rand()` is open-coded as `ADD EAX,EAX / CDQ / AND EDX,0x7fff / ADD EAX,EDX / SAR EAX,0xf`
  — i.e. `(rand()*2) >> 15` with the usual round-toward-zero correction, confirming the
  `(rand()*N)>>15` idiom read off the decompilation.

**Confidence transfer.** The listing matches the decompiled `.c` file's *control flow and
call targets* instruction for instruction — only the stack-argument modelling was wrong.
That is good evidence that the control flow in the other `.c` files in this scope is
trustworthy even where the `abStack_NN[...]` noise appears, and that only the *argument
values* in those functions need treating as suspect. The six tower-lock handlers all show
the same noise pattern, so §3.3's shared shape stands.

It does **not** resolve the one genuinely odd thing in this scope: the "kick the *other*
sink" polarity in `a_sink1`/`a_sink2`/`FUN_00425c30` (§3.1). `a_sink18`'s mode-6 branch is
a *different* mechanism (random choice between the two fire lizard sinks, not "the other
one"), so it neither confirms nor refutes it. Raw listings for `0x425ea0`, `0x426000` and
`0x425c30` would settle that.

### 3.4 The dragon's mouth — `a_sink19` (0x427f10)

Only live during quest 9 (**Slay Dragon**); outside it the ball is handed to `a_sink1`.

```
if (quest != 9) { a_sink1->Message(...); return; }
if (--v_lite114->MessageField == 0) { tabc56wave->Play(); return; }   // dragon slain -> controller
t = mode_timer->Message(?) / 1000.0;            // remaining mode time, in seconds
mode_timer->Message(0x45, t + 15.0);            // extend the mode by 15 s
// bounce the ball out of one of six sinks at random, 3.0 s delay:
//   a_sink4, a_sink1, a_sink2, a_sink18, a_sink17, a_sink16
pick = rand()%6; pick->Message(TSinkResetTimer, 3.0f);
switch (dragon_fire_light_group->Message(TLightGroupGetOnCount, 0)) {
  case 0: case 1:                      score = 100000;   break;
  case 2: tabc13wave->Play();          score = 1000000;  break;
  case 3: if (rand()%5  == 0) { CollectJackpot(); goto done; }           break;
  case 4: if (rand()%10 == 0) { tabc35wave->Play();
                                Display(get_rc_string(3002));  // "Extra Ball Light"
                                v_lite57->Message(ThenStayOn, 1.0); goto done; } break;
  default:if (rand()%10 == 0) { table_add_extra_ball(0.0); goto done; }  break;
}
tabc13wave->Play(); score = 100000;
AddScoreFrom(caller, score);
Display(sprintf(get_rc_string(3008)), 2.0);
done:
Display("<literal>", 3.5, lowPriority);
Display(sprintf(get_rc_string(3049)), 5.0);     // "Slay Dragon / Hits Left: %ld"
```

The award ladder matches the spec's *fire light* table (100,000 / 1,000,000 / jackpot /
extra ball light / extra ball) **but the top three rungs are probabilistic** — jackpot
only 1-in-5, extra ball light 1-in-10, extra ball 1-in-10, falling back to 100,000. The
manual presents them as deterministic. See §7.

`v_lite114->MessageField` is the "hits left" counter. The 15 s extension per hit is not in
the manual.

---

## 4. Kickouts and the kicker

### 4.1 `a_kick1` (0x426d00) — the outlane kicker / ball save

Resembles **`LeftKickerControl`** (same one-branch shape, same `ControlTimerExpired`
trigger), but does more.

```cpp
// a_kick1 — left-outlane kicker. "Active for as long as any kicker light is on."
void control::OutlaneKickerControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlTimerExpired)
        return;

    if (!lite37->light_on())        // lite37 = Shield armor: freezes the kicker lights
    {
        outlane_kicker_light_group->Message(MessageCode::TLightGroupOffsetAnimationBackward, 0.0);
        if (!outlane_kicker_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0))
            caller->ActiveFlag = 0;     // last light gone -> kicker goes dead
    }
    info_text_box->Display(pb::get_rc_string(Msg::STRING3009), 2.0);   // "Ball Saved"
}
```

String 3009 *"Ball Saved"* and the Shield interaction confirm the spec exactly.

### 4.2 `a_kout1` (0x426d80) — Armor Kickout

Resembles **`GravityWellKickoutControl`** (collision → award → light work →
`TKickoutRestartTimer`).

```cpp
void control::ArmorKickoutControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    if (info_text_box->MessageField == Quest::DragonPass)    // 7
    {
        CollectJackpot(true);                 // manual: "each armor kickout: the jackpot"
        caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
        return;
    }

    AwardNextArmor();
    lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
    lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0);
    a_rocka->Message(MessageCode::TBlockerEnable, 2.0f);     // slide the rock in
    caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
}
```

The `a_rocka` blocker enable is the spec's *"two of the four gargoyle targets are mounted
on sliding rocks — hitting those slides the rock into the table, opening a passage to a
kickout behind it"*, driven from the kickout side.

*Decompiler noise:* the final `caller->Message` is rendered as
`(**(code **)(*piVar1 + 4))(0x40, -1.0f)` with `piVar1 = (int*)0x14`. The intent is
unambiguous from the mode-7 twin.

### 4.3 `a_kout2` (0x426e40) — Gargoyle Kickout

Resembles **`BlackHoleKickoutControl`**.

```cpp
void control::GargoyleKickoutControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;

    auto quest = info_text_box->MessageField;

    if (quest == Quest::NormalPlay)        // 2 -> start Gargoyle Gaggle-Ball
    {
        info_text_box->MessageField = Quest::GargoyleGaggleBall;   // 0xe
        MissionControl(MessageCode::ControlMissionComplete, nullptr);
        return;
    }

    soundwave32->Play(nullptr, "GargoyleKickout");

    if (quest == Quest::DragonPass)        // 7 -> one dragon egg
    {
        AwardDragonEggs(1);
        caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
        return;
    }

    if (caller->MessageField < 3)
        caller->MessageField++;
    AddScoreFrom(caller, caller->MessageField);   // a_kout2 has NO score array -> 0
    a_kout2->Message(MessageCode::TKickoutRestartTimer, -1.0f);
}
```

Two things to flag here.

* `a_kout2` declares **zero scores** in the component table, and upstream
  `get_scoring` returns 0 for `index >= ScoreCount`. So the "score ladder" in the default
  branch awards **nothing**; its only effect is the `0x4d` notification to the previous
  scorer. That is consistent with the spec's `[GAP]` on the gargoyle kickout's point value
  (its reward is a mode, not points).
* `a_kout2->MessageField` is **also** the "selected quest" slot that
  `TryStartSelectedQuest()` reads and `SelectNextQuest` (`0x423e90`) writes with 4–8.
  The default branch here increments it with a `< 3` guard, so the two uses can only
  coexist because the guard never fires once a quest is selected. This is either a genuine
  (ugly) union in the original, or Ghidra attributing the increment to the wrong object.
  **Unresolved** — worth re-checking by hand on the disassembly.

### 4.4 `a_kout3` (0x427ef0) — Dragon Blood magnet

Two lines. It is enabled (`ActiveFlag = 1`) by the *Dragon Blood* spell in
`FUN_004236b0`, and on collision only records that it fired.

```cpp
void control::DragonBloodKickoutControl(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlCollision)
        a_kout3->MessageField = 1;
}
```

`a_kout3` is also referenced from `a_flip2`'s handler (the dragon flipper), which fits the
manual's *"Dragon Blood turns on the magnet in front of the dragon flipper"*. Note the DK
DAT has no `s_magnet*` object, so the effect is implemented via this kickout.

Side finding: `FUN_004236b0`'s Dragon Blood case lights **both** `v_lite29` and
`v_lite30`. The spec records a `[GAP]`: *"there are only 4 spells — the 5th light's meaning
is unexplained"*. The answer is that Dragon Blood owns two of the five spell lights.

---

## 5. The hole, the ramps, the ramp exits, the one-way, the accelerator

### 5.1 `s_hole118` (0x426ee0) — quest-staging hole

The only handler in this scope that uses `ControlBallCaptured`/`ControlBallReleased`, so
the closest CADET analogue is **`LaunchRampHoleControl`** (which is the only CADET handler
triggering on `ControlBallReleased`).

```cpp
void control::QuestHoleControl(MessageCode code, TPinballComponent* caller)
{
    auto quest = info_text_box->MessageField;

    if (code == MessageCode::ControlBallCaptured)        // 0x42
    {
        if (quest == Quest::NormalPlay)                 // 2
        {
            if (TableG->MultiballCount == 1)
            {
                info_text_box->MessageField = Quest::EggExchange;   // 0xc
                MissionControl(MessageCode::ControlMissionComplete, nullptr);
                return;
            }
        }
        else if (quest == 10)
        {
            caller->MessageField = 2;                   // park, do not capture
            return;
        }
        soundwave32->Play(nullptr, "QuestHole");
        caller->MessageField = 1;
        caller->Message(MessageCode::THoleHold /*0x43*/, 1.0f);
        return;
    }

    if (code == MessageCode::ControlBallReleased)        // 0x44
    {
        if (quest == Quest::NormalPlay && caller->MessageField == 0)
            soundwave34->Play(nullptr, "QuestHoleRelease");
    }
}
```

`FUN_004241c0` shows the other half of the mechanism: when `questStartPending`
(`DAT_00439b10`) is set, the ball is parked in `s_hole118`
(`MessageField = 2; Message(0x43, 0)`) instead of being kicked out of whichever sink it
landed in. So `s_hole118` is where the ball waits while a quest-start offer is on screen.
Mode `0xc` reached from here with a single ball in play, and `0x42ac80`'s body switching on
`a_kout2->MessageField`, is my basis for calling `0xc` the **Dragon Egg Kickout exchange
offer** — medium confidence.

Code `0x43` is the main soft spot: it is the only code in this scope with no upstream
counterpart that is *sent to a component*. Both call sites send it to a hole with a float,
so a timed hold/release is the only reading that makes sense. Flagged.

### 5.2 `a_ramp103` (0x4265e0) — Dragon Ramp

Closest CADET analogue: **`LaunchRampControl`** (combination-light gating, sound selection,
default scoring only when nothing is lit). Triggered by FT-only code `0x56`.

```cpp
void control::DragonRampControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlRampTraversed /*0x56*/)
        return;

    auto quest = info_text_box->MessageField;

    if (quest != Quest::DragonHoard)                 // 4
        soundwave18->Play(nullptr, "DragonRamp");

    // combination-light driven escalation, capped at 10 steps
    if (!lite67->light_on())
        caller->MessageField = 0;
    else if (caller->MessageField < 9)
        caller->MessageField++;

    lite67->Message(MessageCode::TLightTurnOn, 0.0);
    lite67->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 20.0f);  // 20 s window

    if (quest != Quest::RescueMaiden && quest != Quest::TowerBarrage)
        SetDiverterRouteD();     // lite125 off, lite126 flash-on, dvtrda enable, dvtrdb disable

    lite119->Message(MessageCode::TLightFlasherStartTimed, 2.0f);

    if (quest != Quest::DragonHoard)
    {
        int score = (caller->MessageField + 1) * 50000;
        AddScoreFrom(caller, score);
        AddJackpot(score);                            // same amount to the jackpot
    }

    if (quest == Quest::NormalPlay)                   // 2
    {
        lite24->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);   // a DRAGON letter
        if (!DragonSpelled())
            info_text_box->Display(pb::get_rc_string(Msg::STRING3006), 2.0);     // "Jackpot Grows"
    }
}
```

This is the cleanest confirmation in the whole scope: *"Each pass: +50,000 to score and
+50,000 to the jackpot. Increased by a further 50,000 per additional pass made before the
dragon ramp combination light goes out (50k, 100k, 150k…)"* — exact, including the
double award. New facts: the window is **20 seconds** and the escalation **caps at
500,000** (`MessageField < 9`).

### 5.3 `a_ramp6` (0x426740) — Tower Ramp

```cpp
void control::TowerRampControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlRampTraversed /*0x56*/)
        return;

    bool skipAward = false;
    if (info_text_box->MessageField == Quest::NormalPlay)
    {
        lite26->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);   // a DRAGON letter
        if (DragonSpelled())
            skipAward = true;
    }

    if (!skipAward)
    {
        if (!lite70->light_on())
            soundwave9->Play(nullptr, "TowerRamp");   // first pass: nothing but a sound
        else
            CastRandomSpell();                        // repeat pass inside the window
    }

    lite70->Message(MessageCode::TLightTurnOn, 0.0);
    lite70->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);     // 10 s window
}
```

The spec marks the tower ramp's value as `[GAP]`. **Answer: it has no point value at all.**
A first pass only lights the combination light (10 s); a repeat pass inside that window
**casts a random spell** (worth 120,000 via `FUN_004236b0`). During normal play it also
lights one DRAGON letter.

### 5.4 `s_rexit19` (0x426be0) and `s_rexit18` (0x426c20) — Gargoyle / Forest Ramp exits

Trivial and decisive, triggered by FT-only code `0x55`:

```cpp
void control::GargoyleRampExitControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlRampExit /*0x55*/)
        return;
    AwardDragonEggs(2);
    if (info_text_box->MessageField == Quest::NormalPlay)
    {
        a_dvtrca->Message(MessageCode::TGateEnable, 0.0);
        a_dvtrcb->Message(MessageCode::TGateDisable, 0.0);
    }
}

void control::ForestRampExitControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlRampExit /*0x55*/)
        return;
    AwardDragonEggs(1);
    if (info_text_box->MessageField == Quest::NormalPlay)
    {
        a_dvtrba->Message(MessageCode::TGateEnable, 0.0);
        a_dvtrbb->Message(MessageCode::TGateDisable, 0.0);
    }
}
```

`AwardDragonEggs(n)` lights `n` more lights in `dragon_egg_light_group` and then awards
`GetOnCount × 100000`. That is the spec's *"Gargoyle Ramp: 2 dragon eggs per passage,
+100,000 per dragon egg already lit"* / *"Forest Ramp: 1 dragon egg … +100,000 per egg"* —
exact, and it identifies `s_rexit19` = Gargoyle Ramp, `s_rexit18` = Forest Ramp.

Small nuance: the award uses the egg count **after** the new eggs are added, so the
manual's "per egg *already* lit" is off by the eggs just granted.

### 5.5 `s_oneway5` (0x4267f0) — Dragon Chute

Closest CADET analogue: **`DeploymentChuteToEscapeChuteOneWayControl`** (one-way, score
index taken from a counter, formatted text, light bookkeeping).

```cpp
void control::DragonChuteControl(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlTimerExpired)      // combination light timed out
    {
        lite68->MessageField = 0;                      // reset the ladder
        return;
    }
    if (code != MessageCode::ControlCollision)
        return;

    auto quest = info_text_box->MessageField;
    if (quest == Quest::NormalPlay)
    {
        lite125->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
        lite126->Message(MessageCode::TLightResetAndTurnOff, 0.0);
        a_dvtrda->Message(MessageCode::TGateDisable, 0.0);
        a_dvtrdb->Message(MessageCode::TGateEnable, 0.0);
    }
    else if (quest == Quest::DragonHoard)              // 4 -> the quest controller handles it
        return;

    soundwave20->Play(nullptr, "DragonChute");
    AddScoreFrom(caller, lite68->MessageField);        // 25k/75k/100k/125k/150k ladder
    if (lite68->MessageField < 4)
        lite68->MessageField++;

    AddSuperJackpot(200000);                           // table->+0x17a, clamp 20,000,000

    char Buffer[64];
    snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3076), /*super jackpot*/);
    info_text_box->Display(Buffer, 2.0);               // "Super Jackpot Increased\n%ld"

    lite68->Message(MessageCode::TLightTurnOn, 0.0);
    lite68->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);
}
```

String 3076 *"Super Jackpot Increased"* with `+0x17a` is what identifies `TPinballTable`
field `0x17a` as the **super jackpot**, separate from `JackpotScore` at `0x172`. The
+200,000 and the 25,000 first-pass score are both exactly as documented.

### 5.6 `a_accl1` (0x426940) — Moat Chute accelerator

Six lines, and the single most satisfying confirmation in the scope.

```cpp
void control::MoatChuteAcceleratorControl(MessageCode code, TPinballComponent* caller)
{
    if (code != MessageCode::ControlCollision)
        return;
    if (info_text_box->MessageField == Quest::DragonHoard)   // 4: dead during Dragon Hoard
        return;

    soundwave22->Play(nullptr, "MoatChute");
    bool stealth = lite32->light_on();          // lite32 = the Stealth spell light
    AddScoreFrom(caller, stealth ? 1 : 0);      // 75,000 or 500,000
}
```

`a_accl1`'s score pair is exactly `75000, 500000`, and `v_lite32` is the fourth of the four
spell lights picked by `CastRandomSpell` — the one whose effect is "disable the `a_blocf`
blocker", i.e. change the chute's exit route. That triangulates `v_lite32` = **Stealth** and
matches the spec word for word: *"Moat Chute: ball is accelerated; 75,000 points. Under
the Stealth spell the exit direction changes to hit the sword target and each pass awards
500,000."*

By elimination this also assigns the other three spell lights:
`v_lite28` = **Dragon Breath** (30 s timer; `MissionControl`'s prologue adds 100,000 to the
super jackpot for every type-1019 component hit while it is lit), `v_lite29`+`v_lite30` =
**Dragon Blood**, `v_lite31` = **Resistance to Fire** (light only, i.e. bonus hold).

---

## 6. The drain — `Dragon_drain_Control` (0x424900)

Dragon's drain handler is a **fraction** of CADET's `BallDrainControl`: ~45 lines against
CADET's ~200. All the end-of-ball light-resetting, bonus payout and player switching has
moved into mode `0xb`'s controller (`0x42b7c0`), reached via
`info_text_box->MessageField = 0xb; MissionControl(ControlMissionComplete, nullptr)`.

```cpp
void control::BallDrainControl(MessageCode code, TPinballComponent* caller)
{
    if (code == MessageCode::ControlTimerExpired)
    {
        if (gameOverPhase /*DAT_00439b1c*/ != 0)
            return;
        if (drainPhase /*DAT_0043bb6c*/ == 2)
            TableG->Plunger->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
        else if (drainPhase == 3)
            TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);
        return;
    }
    if (code != MessageCode::ControlCollision)
        return;

    drainPhase = 1;

    if (TableG->TiltLockFlag)
    {
        lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0);
        DAT_00434d2c = 0;
    }

    if (lite40->light_on())                     // v_lite40 = the shoot-again / ball-save light
    {
        drainPhase = 3;
        drain->Message(MessageCode::SetTiltLock, 0.0);
        TableG->UnknownP6a = 1;
        BallDrainControl(MessageCode::ControlTimerExpired, caller);   // -> relaunch
        return;
    }

    MissionControl(MessageCode::ControlBallDrained /*0x53*/, caller);

    // last ball of the last player, nothing left: start the game-over sequence
    if (TableG->MultiballCount == 0 && TableG->ExtraBalls == 0 && TableG->BallCount == 1 &&
        TableG->CurrentPlayer - TableG->PlayerCount == -1 && gameOverPhase == 0)
    {
        gameOverPhase = TableG->TiltLockFlag ? 2 : 1;
        TableG->TiltLockFlag = 1;
        a_flip1->Message(MessageCode::SetTiltLock, 0.0);
        a_flip3->Message(MessageCode::SetTiltLock, 0.0);
        a_flip2->Message(MessageCode::SetTiltLock, 0.0);

        // release every locked ball, highest tower first
        struct { TLight* light; TSink* sink; } locks[] =
            {{lite50, a_sink16}, {lite49, a_sink17}, {lite48, a_sink18}};
        for (auto& l : locks)
            if (l.light->LightOnFlag)
            {
                info_text_box->MessageField = 1;
                TableG->MultiballCount++;
                l.sink->Message(MessageCode::TSinkResetTimer, 0.0);
                l.light->Message(MessageCode::TLightResetAndTurnOff, 0.0);
            }
    }

    if (TableG->MultiballCount != 0 && gameOverPhase == 0)
    {
        drainPhase = 0;                         // other balls still live: nothing to do
        return;
    }

    if (info_text_box->MessageField == Quest::EndOfBall || info_text_box->MessageField == 0)
        return;
    info_text_box->MessageField = Quest::EndOfBall;      // 0xb
    MissionControl(MessageCode::ControlMissionComplete, nullptr);
}
```

Notes and loose ends:

* The `drainPhase == 2` branch (normal new-ball feed) is **never reached** in the
  decompiled set — `DAT_0043bb6c` is written only by this function, and only with 0, 1 and
  3. Mode `0xb`'s controller presumably sets it to 2; `0x42b7c0` is in the decomp but I did
  not read it (out of scope). Worth confirming.
* `drain->Message(SetTiltLock, 0.0)` on the ball-save path is odd — upstream's `TDrain` has
  no `Message` override. Either `TDrain` in FT handles 1011, or `0x3f3` means something
  else here. **Unresolved.**
* `TableG + 0x6a` set to 1 on a ball save is **unidentified**.
* `DAT_00434d2c` (cleared when draining while tilted) is **unidentified**.
* `gameOverPhase` taking the value 1 vs 2 depending on `TiltLockFlag` is the
  tilt-vs-normal distinction; the two values are presumably read by `0x42b7c0`.

The ball save is confirmed against the spec's *"Plunger — a bow … If the ball drains in
the first 15 s it is re-launched"*: `v_lite40` is a score component in its own right
(handler `0x424870`) and is what carries that 15 s window.

---

## 7. Manual-vs-binary discrepancies found

Ordered roughly by how much they would change a reimplementation.

1. **Slay Dragon's award ladder is probabilistic.** The manual presents the fire-light
   table as deterministic (1st 100,000 / 2nd 1,000,000 / 3rd jackpot / 4th extra ball
   light / 5th extra ball). `a_sink19` gates the top three rungs on `rand()`: jackpot
   **1-in-5**, extra ball light **1-in-10**, extra ball **1-in-10**, otherwise it falls
   through to a plain 100,000. (Binary authoritative.)
2. **The Tower Ramp has no point value — it casts a spell.** The spec marks it `[GAP]`.
   `a_ramp6` awards no score at all; a first pass only lights a 10 s combination light, a
   repeat pass inside the window calls `CastRandomSpell()` (120,000 + the spell).
3. **The Dragon Chute has an undocumented score ladder.** The manual says "+25,000
   instantly to score". `s_oneway5` has five scores (25k / 75k / 100k / 125k / 150k)
   indexed by `v_lite68->MessageField`, which escalates while the combination light is lit
   (10 s) and resets on `ControlTimerExpired`.
4. **"During Dragon Hoard the dragon chute awards the jackpot" is not in the chute
   handler.** `s_oneway5` returns early during quest 4; if the jackpot is awarded it must
   be done by `0x428a80`. Likewise the manual's per-gem values (50k/60k/70k/100k/200k/250k)
   appear nowhere in these handlers — `a_ramp103` keeps awarding its own escalating
   50,000 ladder and merely lights a progress light.
5. **Dragon Breath lasts 30 s in the binary, 60 s in the manual.**
   `FUN_004236b0` case 0 is `v_lite28->Message(TLightTurnOnTimed, 30.0f)`.
6. **Spelling D-R-A-G-O-N is an entire undocumented mechanic.** `v_lite22`–`v_lite27` are
   six progress lights lit by completing six component groups (the fire-lizard pair, the
   spell target bank, the tower target bank, the gargoyle target bank, the dragon ramp, the
   tower ramp — the first four corroborated by the manual's scattered "the dragon letter
   light in front of them lights" remarks, the last two found only in the binary).
   `FUN_004244e0` fires mode `0xf` when all six are on. The manual never says what
   completing D-R-A-G-O-N does.
7. **The 5th spell light is Dragon Blood's second light** — closes the spec's `[GAP]`.
8. **The three tower-lock scores are per tower, not per lock order.** The manual's
   "100,000 first, then 200,000, then 300,000" is right only because
   `ArmNextTowerLock()` always arms them in that order.
9. **The gargoyle kickout awards no points outside Dragon Pass** and does not start
   Gargoyle Gaggle-Ball unconditionally — only from normal play (mode 2). The manual
   implies it always starts the mode.
10. **The quest-restart window is implemented on `mode_timer`, not as a timer on the
    sinks**, and uses a dedicated message code (`0x51`) the Space Cadet build does not
    have. Mode 3 *is* the window; the duration itself lives in `mode_timer`'s DAT
    attributes, so the manual's "ten seconds" is not visible in code.
11. **Jackpot clamps**: jackpot 10,000,000, super jackpot 20,000,000. The manual gives only
    the 1,000,000 *minimum*.
12. **`table_add_extra_ball` assigns `ExtraBalls = 1`** rather than incrementing, so extra
    balls do not stack. CADET increments.
13. **Dragon's `handler()` calls `MissionControl` before the component's own
    `ControlFunc`** — the opposite of upstream. Several of the suppressions above
    (`if (quest != 4) …`) only make sense with that ordering, so it must be preserved in a
    port.
14. **Jackpot contributions are narrower than documented.** The spec says "Sinks 75,000";
    `MissionControl`'s prologue adds 75,000 only when the type-1017 component is
    `a_sink4` or `a_sink1`. Targets (type 1006) and type-1019 components do add 50,000 each
    and also bump a dropped-target counter (`DAT_00434d08`), consistent with the drain
    bonus's "Target dropped 2,000" line.

---

## 8. What stayed ambiguous

* **Message codes `0x43`, `0x49`, `0x4d`, `0x4e`, `0x51`, `0x52`, `0x53`, `0x54`, `0x55`,
  `0x56`.** All are real codes with no Space Cadet counterpart, so CADET cannot pin them.
  My readings (`THole` hold, "no longer active scorer", "mission restarted", "ball
  drained", "ramp exit", "ramp traversed") come from call-site behaviour only.
* **`a_kout2->MessageField` serving as both a hit counter and the selected-quest slot.**
  Needs a hand check against the disassembly.
* **Which physical tower each lock sink is** (gate / watch / prison). The binary gives
  score and arming order but not geometry; that needs the DAT visual positions.
* **Modes 10 (`0x4285f0`), `0xc` (`0x42ac80`) and `0xf` (`0x42b510`).** Named here only by
  the way the in-scope handlers enter them.
* **`drainPhase == 2`** is unreachable in the decompiled set.
* **`TableG + 0x6a`**, **`DAT_00434d2c`**, and the 1-vs-2 meaning of `gameOverPhase`.
* **The polarity of the "other sink" logic** in `a_sink1`/`a_sink2`/`FUN_00425c30`. The
  register allocation in those three functions is the worst in the scope.
* **`a_sink9`/`a_sink10` omit the plunger relaunch** on the lock path where the other four
  entrances have it. Possibly real (those towers may feed differently), possibly a
  decompiler omission.
* **Sound ids.** I have used `tabcNNwave` → `soundwaveNN` naively; the numbering is
  Dragon's own and does not line up with CADET's.
