# CADET.EXE symbol dictionary (control.cpp correspondence)

Target: `binaries/win95/CADET.EXE` (Full Tilt! Pinball 1996, Space Cadet table).
Decompilation: `decomp/cadet/functions/*.c` (165 functions, 0x423000-0x42d000; 56 named
from `analysis/cadet-seeds.tsv`).
Reference source: SpaceCadetPinball `control.cpp` / `control.h` / `T*.h` — the upstream
decompilation of this exact translation unit.

## Method

Three independent evidence channels, cross-checked against each other:

1. **Pairwise body diff.** Each named handler's Ghidra body was read against the
   same-named `control::<Name>` in `control.cpp`. Short, branch-free handlers
   (`RebounderControl`, `BumperControl`, `LeftKickerControl`, `SpaceWarpRolloverControl`,
   `JackpotLightControl`, `ExtraBallLightControl`) pin the first symbols; those then
   decode the long ones (`BallDrainControl`, `WormHoleControl`, `BoosterTargetControl`).
2. **Constraint propagation.** For all 56 handlers, the set of `control.cpp` tag
   identifiers referenced was intersected, per `DAT_` address, across every handler that
   mentions that address; unit candidate sets were committed and propagated for
   injectivity. This resolved 44 globals with no manual input.
3. **Static table read-out.** `analysis/dump_components.py` locates
   `score_components[88]` in `.data` (16-byte records `{tag*, ControlFunc*, ScoreCount,
   Scores*}`); the adjacent 145-pointer run is `simple_components[145]`. Each record's
   `tag*` points at `component_tag {const char* Name; TPinballComponent* Component;}`, so
   `tag+4` is the exact address of the global Ghidra calls `DAT_<x>`, and `*(tag+0)` is the
   group-name string. This yields all 233 tag globals directly from the binary.

Channels 2 and 3 agree on **44/44** addresses with zero disagreements, which is the main
reason the global table below is marked CONFIRMED rather than PROBABLE.

The 1996 MSVC build is **byte-packed** (`/Zp1`): every field offset below is unaligned
(`+6`, `+0x42`, `+0x176`), which is what makes the offset chains fall out so cleanly.

Confidence key:
- **CONFIRMED** — binary and source agree unambiguously (structure, argument shapes,
  call order, and/or a static table read directly out of the file).
- **PROBABLE** — derived by interpolation from confirmed anchors, or from a single
  unambiguous call site in code that is not itself `control.cpp`.
- **GUESS** — plausible but not pinned; flagged as such, do not build on it.

---

## 1. Functions

### 1.1 Core helpers called from the component handlers

| address | name | signature / meaning | conf. | evidence |
|---|---|---|---|---|
| 0x00419f10 | `TPinballTable::AddScore` | `int (TPinballTable* this, int score)` | CONFIRMED | `RebounderControl`: `FUN_00419f10(DAT_00437a58, get_scoring(0))` == `TableG->AddScore(caller->get_scoring(0))`. Appears in 30/56 handlers, always with `DAT_00437a58` first. |
| 0x00419fd0 | `TPinballTable::ChangeBallCount` | `void (TPinballTable* this, int count)` | CONFIRMED | `BallDrainControl`: `FUN_00419fd0(TableG, *(TableG+0x186) - 1)` == `TableG->ChangeBallCount(TableG->BallCount - 1)`. |
| 0x00409730 | `pb::get_rc_string` | `char* (int msgId)` | CONFIRMED | Return value is always the 2nd arg of 0x0041de40; `FuelRollover1Control` `FUN_00409730(0xbcf)` == `pb::get_rc_string(Msg::STRING145)`. 21/56 handlers. |
| 0x0041de40 | `TTextBox::Display` | `void (TTextBox* this, char* text, float time, ...)` | CONFIRMED | `FUN_0041de40(DAT_00438b24, str, 2.0, 0, 0)` == `info_text_box->Display(pb::get_rc_string(...), 2.0)`; `DAT_00438b24` is the `info_text_box` tag Component, `DAT_00438b44` the `mission_text_box` one. 21/56 handlers. |
| 0x0042af7f | `sprintf` (CRT, variadic) | `int (char* buf, const char* fmt, ...)` | CONFIRMED | `GravityWellKickoutControl`: `FUN_0042af7f(stackbuf, FUN_00409730(0xbf4))` then `Display(info_text_box, stackbuf, 2.0)` == `snprintf(Buffer, …, pb::get_rc_string(Msg::STRING182), addedScore); info_text_box->Display(Buffer, 2.0)`. Upstream's `snprintf` is the original's `sprintf`. |
| 0x00423020 | `control::SpecialAddScore` | `int (int score)` | CONFIRMED | Body saves/zeroes/restores `TableG+0x16e`, `+0x176`, `+0x15e` around `AddScore` — exactly `BonusScoreFlag`/`JackpotScoreFlag`/`ScoreMultiplier` in `control::SpecialAddScore`. Original takes no `mission` arg (that is an upstream FT addition). |
| 0x004230a0 | `control::AdvanceWormHoleDestination` | `void (int flag)` | CONFIRMED | Full structural match: `lite198->MessageField` tested against 16/22/23 (`0x10/0x16/0x17`), `lite4->MessageField+1`, wrap at 4, `bsink_arrow_lights` `TLightSetMessageField`/`TLightSetOnStateBmpIndex`, `!lite4->light_on()` guard. |
| 0x00422fe0 | `control::table_add_extra_ball` | `void (float count)` | CONFIRMED | `OutLaneRolloverControl`: `FUN_00422fe0(2.0)` == `table_add_extra_ball(2.0)`; also called from `MedalTargetControl`. |
| 0x00422df0 | `control::table_set_jackpot` | `void ()` | CONFIRMED | `BoosterTargetControl` branch chain: `!lite60->light_on()` -> `table_set_jackpot(); sound = soundwave45` matches `FUN_00422df0(); piVar1 = DAT_004389ec` (= `soundwave45`). |
| 0x00422e40 | `control::table_set_bonus` | `void ()` | CONFIRMED | Same chain: `!lite59->light_on()` -> `table_set_bonus(); sound = soundwave46` == `FUN_00422e40(); DAT_00438a04` (= `soundwave46`). |
| 0x00422e90 | `control::table_set_flag_lights` | `void ()` | CONFIRMED | Same chain: `!lite61->light_on()` + `lite198->MessageField != 15,29` -> `table_set_flag_lights(); sound = soundwave47` == `FUN_00422e90(); DAT_00438a1c` (= `soundwave47`). |
| 0x00422ef0 | `control::table_set_bonus_hold` | `void ()` | CONFIRMED | Same chain: `!lite58->light_on()` -> `table_set_bonus_hold(); sound = soundwave48` == `FUN_00422ef0(); DAT_00438a34` (= `soundwave48`). |
| 0x00422ce0 | `control::table_set_replay` | `void (float value)` | CONFIRMED | `WormHoleControl`: `FUN_00422ce0(); AddScore(50000)` == `table_set_replay(4.0); TableG->AddScore(50000)`. |
| 0x00422f30 | `control::table_bump_ball_sink_lock` | `void ()` | CONFIRMED | `WormHoleControl`, `MultiballCount == 1` path: `FUN_00422f30(); AddScore(10000)` == `table_bump_ball_sink_lock(); TableG->AddScore(10000)`. |
| 0x00422d20 | `control::table_set_multiball` | `void (float time)` | PROBABLE | Only remaining member of the `table_set_*` family in `HyperspaceKickOutControl`'s candidate set after the four above and `table_set_replay`/`table_bump_ball_sink_lock` were pinned; body not in the decompiled range. |
| 0x00426560 | `control::AddRankProgress` | `int (int rank)` | CONFIRMED | Opens with `lite16->Message(TLightResetAndTurnOn)` (`DAT_00437e9c`), then a `do` loop over `outer_circle` (`DAT_004387d4`), returns a value. Matches `control::AddRankProgress` shape exactly; 18 callers (every mission controller). |
| 0x00426250 | `control::cheat_bump_rank` | `void ()` | CONFIRMED | `middle_circle` (`DAT_004387bc`) `TLightGroupGetOnCount` (0x29) `< 9`, `TLightGroupResetAndTurnOn` (0x2d), `RankRcArray[rank]` from `DAT_00439520`, `sprintf` with STRING184 (`0xbf6`), `mission_text_box->Display(buf, 8.0)`, `soundwave10->Play` (`DAT_00438824`). Line-for-line with the source. |
| 0x0042aab0 | `control::CheckBallInControlBounds` | `int (TBall* ball, TCollisionComponent* cmp)` | CONFIRMED | `TableG+0x152 / 2.0` == `TableG->CollisionCompOffset / 2.0f`; four comparisons map 1:1 onto `ball.ActiveFlag && X >= XMin-off && Y >= YMin-off && X <= XMax+off && Y <= YMax+off` with `RectF {XMax,YMax,XMin,YMin}`. |
| 0x0042ab40 | `control::UnstuckBall` | `void (TBall* ball, int dt)` | CONFIRMED | Three `CheckBallInControlBounds` calls against `DAT_004381fc`/`DAT_0043820c`/`DAT_0043821c` (= `a_flip1`,`a_flip2`,`plunger`); `StuckCounter` (`+0x157`) `> 20`; `TableG->MultiballCount--`; `plunger->Message(PlungerRelaunchBall)`. |
| 0x004029e0 | `TBall::throw_ball` | `void (TBall*, vector3* dir, float angleMult, float speedMult1, float speedMult2)` | CONFIRMED | In `UnstuckBall`: `FUN_004029e0(ball, &DAT_00439a58, 90.0, 1.0, 0.0)` == `ball.throw_ball(&throwDir, 90.0f, 1.0f, 0.0f)`; `DAT_00439a58` is the static `vector3{0,-1,0}`. |
| 0x00402c00 | `TBall::Disable` | `void (TBall*)` | CONFIRMED | Same site, the `StuckCounter > 20` branch == `ball.Disable()`. |
| 0x00410ff0 | `pb::chk_highscore` | `int ()` | CONFIRMED | `BallDrainControl`: `if (FUN_00410ff0()) { soundwave3->Play(); TableG->LightGroup->Message(...); mission_text_box->Display(STRING277, -1.0) }` == `if (pb::chk_highscore()) {...}`. |
| 0x0040eb50 | `midi::play_track` | `void (void* track)` | PROBABLE | `if (FUN_0040e9b0() != DAT_00432d08) FUN_0040eb50(DAT_00432d08);` sits exactly where the source has `midi::play_track(MidiTracks::Track1, false)`. Four call sites, always the same pattern. Not in `control.cpp`, so signature is inferred. |
| 0x0040e9b0 | `midi::<currently playing track>` getter | `void* ()` | PROBABLE | Same site: its result is compared with the track object before playing — this is the `restart == false` short-circuit of upstream `midi::play_track`. |
| 0x004262e0 | `control::pbctrl_bdoor_controller` | `void (char key)` | CONFIRMED | Gated on `lite198->MessageField == 0`; a per-character state machine over `DAT_00439698` (progress) / `DAT_0043969c` (matched cheat id); sets `DAT_00437a5c` (= `table_unlimited_balls`) and walks the credits string `DAT_00432cf8` ("Full Tilt! was created by Cinema…"). |
| 0x0041f6c0 | `timer::set` | `int (float time, void* caller, void (*callback)(int,void*))` | PROBABLE | `FUN_0041f6c0(0x40000000 /*2.0f*/, flag, 0x42a660)` — third arg is the address of the calling function itself, i.e. a self-rescheduling timer callback. |
| 0x0041f900 | `timer::kill` | `void (int timerId)` | PROBABLE | Always `if (DAT_00439a50) FUN_0041f900(DAT_00439a50); DAT_00439a50 = 0;` immediately before setting a new timer. |
| 0x0041e070 | `TTextBox::Display` (alt/scrolling entry) | `void (TTextBox*, char*, float, ...)` | GUESS | Same argument shape as 0x0041de40 but a different entry point, used by the credits scroller. |
| 0x0041d930 / 0x0041dc50 / 0x0041e0e0 | `TTextBox` clear / restore / reset helpers | `void (TTextBox*)` | GUESS | Single-argument text-box calls around message display; no source counterpart identified. |

### 1.2 Mission controllers — `control::MissionControl` dispatch table

`FUN_0042a260` is `control::MissionControl(MessageCode code, TPinballComponent* caller)`
— **CONFIRMED**: `BallDrainControl` ends with `FUN_0042a260(0x4f, NULL)` exactly where the
source has `MissionControl(MessageCode::ControlMissionComplete, nullptr)`, and its prologue
reproduces the source's pre-switch fixups (`fuel_bargraph` + `lite198->MessageField > 1`
-> reset both trek light groups, `mission_text_box` -> recode to `ControlMissionStarted`).

Its body then switches on `lite198->MessageField` (`*(DAT_004384e4 + 6)`) over cases
0..32 + default, in the **same index order** as the `switch (lite198Msg)` in
`control::MissionControl`. All 34 below are CONFIRMED by that positional 1:1
correspondence (index -> name is fixed by the source; no freedom in the alignment).

| case | address | name |
|---|---|---|
| 0 | 0x00426680 | `control::WaitingDeploymentController` |
| 1 | 0x004267e0 | `control::SelectMissionController` |
| 2 | 0x00426e90 | `control::PracticeMissionController` |
| 3 | 0x00427060 | `control::LaunchTrainingController` |
| 4 | 0x004271f0 | `control::ReentryTrainingController` |
| 5 | 0x004273e0 | `control::ScienceMissionController` |
| 6 | 0x004276c0 | `control::StrayCometController` |
| 7 | 0x00427ae0 | `control::BlackHoleThreatController` |
| 8 | 0x004278d0 | `control::SpaceRadiationController` |
| 9 | 0x00427dd0 | `control::BugHuntController` |
| 10 | 0x004284b0 | `control::AlienMenaceController` |
| 11 | 0x004281a0 | `control::RescueMissionController` |
| 12 | 0x004287b0 | `control::SatelliteController` |
| 13 | 0x00428940 | `control::ReconnaissanceController` |
| 14 | 0x00428bb0 | `control::DoomsdayMachineController` |
| 15 | 0x00428d60 | `control::CosmicPlagueController` |
| 16 | 0x00429000 | `control::SecretMissionYellowController` |
| 17 | 0x00429300 | `control::TimeWarpController` |
| 18 | 0x004296a0 | `control::MaelstromController` |
| 19 | 0x00426720 | `control::QuoteController` (upstream dropped it; `/*case 19: QuoteController*/`) |
| 20 | 0x004285b0 | `control::AlienMenacePartTwoController` |
| 21 | 0x00428eb0 | `control::CosmicPlaguePartTwoController` |
| 22 | 0x004290f0 | `control::SecretMissionRedController` |
| 23 | 0x004291a0 | `control::SecretMissionGreenController` |
| 24 | 0x00429420 | `control::TimeWarpPartTwoController` |
| 25 | 0x00429800 | `control::MaelstromPartTwoController` |
| 26 | 0x004299c0 | `control::MaelstromPartThreeController` |
| 27 | 0x00429b90 | `control::MaelstromPartFourController` |
| 28 | 0x00429c20 | `control::MaelstromPartFiveController` |
| 29 | 0x00429cb0 | `control::MaelstromPartSixController` |
| 30 | 0x00429db0 | `control::MaelstromPartSevenController` |
| 31 | 0x00429e60 | `control::MaelstromPartEightController` |
| 32 | 0x00429fc0 | `control::GameoverController` |
| default | 0x0042a240 | `control::UnselectMissionController` |

### 1.3 Component handlers (seeded, for completeness)

The 56 addresses in `analysis/cadet-seeds.tsv` are confirmed a second way by the static
table: every `score_components[k].ControlFunc` value is one of them, and the group names
in the same record match the components the corresponding `control::<Name>` touches.
`LeftFlipperControl` (0x00424890) serves both `a_flip1` and `a_flip2`, i.e. the original
had one function where upstream split out `RightFlipperControl`.

### 1.4 CRT / not part of control.cpp

`FUN_0042abd0`-`FUN_0042ad33`, `FUN_0042ae8e`-`FUN_0042c05c`, `FUN_0042c8a7`-`FUN_0042d653`,
`FUN_0042e1f4`-`FUN_0042fc2e` are MSVC C runtime (`printf` family, `_fpmath`/`_ctrlfp`
helpers, stack probes). They are entered at non-function-start addresses
(0x42abd6, 0x42abdc, 0x42abe7, …), a sign Ghidra split them at jump-table targets.
Within that block only `0x0042af7f` (`sprintf`), `0x0042aab0`
(`CheckBallInControlBounds`), `0x0042ab40` (`UnstuckBall`) and `0x0042a240`/`0x0042a260`
are `control.cpp` code. — CONFIRMED by callee shape; individual CRT names not resolved.

---

## 2. Globals

### 2.1 Non-tag globals

| address | name | meaning | conf. | evidence |
|---|---|---|---|---|
| 0x00437a58 | `control::TableG` | `TPinballTable*` | CONFIRMED | First argument of every `AddScore`/`ChangeBallCount` call and the base of every `TableG->field` access. |
| 0x00437a5c | `control::table_unlimited_balls` | `int`/`bool` cheat flag | CONFIRMED | Three agreeing sites: set to 0 in `PlungerControl` (source: `table_unlimited_balls = false`), tested at the top of `BallDrainControl`'s collision path (source: `if (table_unlimited_balls)`), set to 1 by `pbctrl_bdoor_controller`. |
| 0x004394d4 | `control::extraball_light_flag` | `int` | CONFIRMED | Only user is `ExtraBallLightControl`, and its set/test/clear pattern is identical to the source's. |
| 0x00439520 | `control::RankRcArray` | `Msg[9]` | CONFIRMED | `cheat_bump_rank`: `FUN_00409730(*(&DAT_00439520 + rank*4))` == `pb::get_rc_string(RankRcArray[rank])`. |
| 0x004381b0 | `control::WormholeSinkArray` | `component_tag* [3]` = `v_sink1, v_sink2, v_sink3` | CONFIRMED | Read straight out of `.data`; `WormHoleControl` indexes it as `(&DAT_004381ac)[i]` with a 1-based `i`. |
| 0x004381c0 | `control::WormholeLightArray1` | `component_tag* [3]` = `lite5, lite6, lite7` | CONFIRMED | Same, Ghidra anchors it as `&DAT_004381bc + i`. |
| 0x004381d0 | `control::WormholeLightArray2` | `component_tag* [3]` = `lite4, lite2, lite3` | CONFIRMED | Same, anchored as `&DAT_004381cc + i`. Note these arrays hold **tag** pointers, so the call shape is `(*(tagptr + 4))->Message(...)`. |
| 0x00439a58 | static `vector3 {0, -1, 0}` | throw direction for `UnstuckBall` | CONFIRMED | Only use is the `throw_ball` call in `UnstuckBall`. |
| 0x00432cf8 | `pbctrl_bdoor_controller` credits/quote cursor | `char*` | CONFIRMED | Walked character-by-character until `'~'`; reset to the "Full Tilt! was created by Cinematronics" literal (`s_Full_Tilt__was_created_by_Cinema_004396a0`). |
| 0x00439698 / 0x0043969c | cheat-code progress / matched-cheat id | `int` | PROBABLE | The two state variables of `pbctrl_bdoor_controller`'s key state machine (upstream keeps a `cheatBuffer` string instead). |
| 0x00439a50 | credits-scroller timer id | `int` | PROBABLE | Killed with `FUN_0041f900` and re-armed with `FUN_0041f6c0` in `FUN_0042a660`/`FUN_0042a720`. |
| 0x00432d08 | `midi` track object (Track1) | pointer | PROBABLE | Argument of `FUN_0040eb50` at the four sites where the source has `midi::play_track(MidiTracks::Track1, false)`. |
| 0x0043721c | a second `TPinballTable*` global | pointer | PROBABLE | `BallDrainControl` reads `*(DAT_0043721c + 0x192)` where the source reads `TableG->MultiballCount` — same offset, different base, so it is another global holding the table (most likely `pb::MainTable`). |
| 0x00432d04 | drain "shoot again / relaunch pending" flag | `int` | GUESS | Cleared at the start of `BallDrainControl`'s collision path, set to 1 on the extra-ball/shoot-again branch, and on timer-expiry selects `plunger->Message(0x3fb)` over `plunger->Message(0x3f9)`. Possibly `control::waiting_deployment_flag`, but upstream's `BallDrainControl` has no such flag, so unproven. |

### 2.2 Component tags

Each row is the address of the `Component` field of one `component_tag` (i.e. `tag + 4`),
which is what the decompiler shows as `DAT_<addr>`. All addresses and group names are read
directly out of `score_components[88]` / `simple_components[145]` in `.data`, and the 44
of them that constraint propagation independently resolved agree exactly. The
`refs` column counts how many of the 56 named handlers, and how many of all 165
decompiled functions, reference the address.

A handful of group names occur twice in `control.cpp` under two tag identifiers
(`soundwave14`, `soundwave35`, `soundwave36`, `soundwave50`); for those the address and
group name are certain but which of the two idents it is, is not.

| address | group name | control.cpp symbol | type | refs (named / all) | confidence |
|---|---|---|---|---|---|
| 0x00437a6c | `a_bump1` | `control_bump1_tag.Component` (alias `bump1`) | `TBumper*` | 1 / 4 | CONFIRMED |
| 0x00437a7c | `a_bump2` | `control_bump2_tag.Component` (alias `bump2`) | `TBumper*` | 0 / 2 | CONFIRMED |
| 0x00437a8c | `a_bump3` | `control_bump3_tag.Component` (alias `bump3`) | `TBumper*` | 0 / 2 | CONFIRMED |
| 0x00437a9c | `a_bump4` | `control_bump4_tag.Component` (alias `bump4`) | `TBumper*` | 0 / 3 | CONFIRMED |
| 0x00437ab4 | `attack_bumpers` | `control_attack_bump_tag.Component` (alias `attack_bump`) | `TComponentGroup*` | 2 / 3 | CONFIRMED |
| 0x00437ac4 | `lite8` | `control_lite8_tag.Component` (alias `lite8`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437ad4 | `lite9` | `control_lite9_tag.Component` (alias `lite9`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437ae4 | `lite10` | `control_lite10_tag.Component` (alias `lite10`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437afc | `bmpr_inc_lights` | `control_bmpr_inc_lights_tag.Component` (alias `bmpr_inc_lights`) | `TLightGroup*` | 3 / 3 | CONFIRMED |
| 0x00437b0c | `a_roll3` | `control_roll3_tag.Component` (alias `roll3`) | `TRollover*` | 1 / 4 | CONFIRMED |
| 0x00437b1c | `a_roll2` | `control_roll2_tag.Component` (alias `roll2`) | `TRollover*` | 1 / 4 | CONFIRMED |
| 0x00437b2c | `a_roll1` | `control_roll1_tag.Component` (alias `roll1`) | `TRollover*` | 0 / 3 | CONFIRMED |
| 0x00437b54 | `a_bump5` | `control_bump5_tag.Component` (alias `bump5`) | `TBumper*` | 1 / 2 | CONFIRMED |
| 0x00437b64 | `a_bump6` | `control_bump6_tag.Component` (alias `bump6`) | `TBumper*` | 0 / 0 | CONFIRMED |
| 0x00437b74 | `a_bump7` | `control_bump7_tag.Component` (alias `bump7`) | `TBumper*` | 0 / 0 | CONFIRMED |
| 0x00437b8c | `launch_bumpers` | `control_launch_bump_tag.Component` (alias `launch_bump`) | `TComponentGroup*` | 2 / 3 | CONFIRMED |
| 0x00437b9c | `lite171` | `control_lite171_tag.Component` (alias `lite171`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437bac | `lite170` | `control_lite170_tag.Component` (alias `lite170`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437bbc | `lite169` | `control_lite169_tag.Component` (alias `lite169`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437bdc | `ramp_bmpr_inc_lights` | `control_ramp_bmpr_inc_lights_tag.Component` (alias `ramp_bmpr_inc_lights`) | `TLightGroup*` | 3 / 3 | CONFIRMED |
| 0x00437bf4 | `a_roll112` | `control_roll112_tag.Component` (alias `roll112`) | `TRollover*` | 1 / 3 | CONFIRMED |
| 0x00437c0c | `a_roll111` | `control_roll111_tag.Component` (alias `roll111`) | `TRollover*` | 1 / 3 | CONFIRMED |
| 0x00437c24 | `a_roll110` | `control_roll110_tag.Component` (alias `roll110`) | `TRollover*` | 0 / 2 | CONFIRMED |
| 0x00437c4c | `v_rebo1` | `control_rebo1_tag.Component` (alias `rebo1`) | `TWall*` | 0 / 1 | CONFIRMED |
| 0x00437c5c | `v_rebo2` | `control_rebo2_tag.Component` (alias `rebo2`) | `TWall*` | 0 / 1 | CONFIRMED |
| 0x00437c6c | `lite84` | `control_lite84_tag.Component` (alias `lite84`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437c7c | `lite85` | `control_lite85_tag.Component` (alias `lite85`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437c94 | `v_rebo3` | `control_rebo3_tag.Component` (alias `rebo3`) | `TWall*` | 0 / 1 | CONFIRMED |
| 0x00437ca4 | `v_rebo4` | `control_rebo4_tag.Component` (alias `rebo4`) | `TWall*` | 0 / 1 | CONFIRMED |
| 0x00437cb4 | `a_kick1` | `control_kicker1_tag.Component` (alias `kicker1`) | `TKickback*` | 0 / 0 | CONFIRMED |
| 0x00437cc4 | `a_kick2` | `control_kicker2_tag.Component` (alias `kicker2`) | `TKickback*` | 0 / 0 | CONFIRMED |
| 0x00437cd4 | `v_gate1` | `control_gate1_tag.Component` (alias `gate1`) | `TGate*` | 4 / 4 | CONFIRMED |
| 0x00437ce4 | `v_gate2` | `control_gate2_tag.Component` (alias `gate2`) | `TGate*` | 4 / 4 | CONFIRMED |
| 0x00437cf4 | `lite30` | `control_lite30_tag.Component` (alias `lite30`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00437d04 | `lite29` | `control_lite29_tag.Component` (alias `lite29`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00437d14 | `lite196` | `control_lite196_tag.Component` (alias `lite196`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00437d24 | `lite195` | `control_lite195_tag.Component` (alias `lite195`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00437d3c | `s_onewy4` | `control_oneway4_tag.Component` (alias `oneway4`) | `TOneway*` | 0 / 1 | CONFIRMED |
| 0x00437d54 | `s_onewy10` | `control_oneway10_tag.Component` (alias `oneway10`) | `TOneway*` | 0 / 1 | CONFIRMED |
| 0x00437d7c | `v_bloc1` | `control_block1_tag.Component` (alias `block1`) | `TBlocker*` | 2 / 2 | CONFIRMED |
| 0x00437d8c | `lite1` | `control_lite1_tag.Component` (alias `lite1`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x00437d9c | `ramp` | `control_ramp_tag.Component` (alias `ramp`) | `TRamp*` | 0 / 4 | CONFIRMED |
| 0x00437dac | `lite54` | `control_lite54_tag.Component` (alias `lite54`) | `TLight*` | 4 / 4 | CONFIRMED |
| 0x00437dbc | `lite55` | `control_lite55_tag.Component` (alias `lite55`) | `TLight*` | 2 / 3 | CONFIRMED |
| 0x00437dcc | `lite56` | `control_lite56_tag.Component` (alias `lite56`) | `TLight*` | 4 / 21 | CONFIRMED |
| 0x00437de4 | `ramp_hole` | `control_ramp_hole_tag.Component` (alias `ramp_hole`) | `THole*` | 0 / 0 | CONFIRMED |
| 0x00437dfc | `a_roll4` | `control_roll4_tag.Component` (alias `roll4`) | `TRollover*` | 1 / 4 | CONFIRMED |
| 0x00437e0c | `a_roll8` | `control_roll8_tag.Component` (alias `roll8`) | `TRollover*` | 0 / 3 | CONFIRMED |
| 0x00437e1c | `lite17` | `control_lite17_tag.Component` (alias `lite17`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00437e2c | `lite18` | `control_lite18_tag.Component` (alias `lite18`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00437e44 | `a_roll6` | `control_roll6_tag.Component` (alias `roll6`) | `TRollover*` | 1 / 3 | CONFIRMED |
| 0x00437e54 | `a_roll7` | `control_roll7_tag.Component` (alias `roll7`) | `TRollover*` | 1 / 3 | CONFIRMED |
| 0x00437e64 | `lite27` | `control_lite27_tag.Component` (alias `lite27`) | `TLight*` | 4 / 4 | CONFIRMED |
| 0x00437e74 | `lite28` | `control_lite28_tag.Component` (alias `lite28`) | `TLight*` | 4 / 4 | CONFIRMED |
| 0x00437e8c | `a_roll5` | `control_roll5_tag.Component` (alias `roll5`) | `TRollover*` | 0 / 2 | CONFIRMED |
| 0x00437e9c | `lite16` | `control_lite16_tag.Component` (alias `lite16`) | `TLight*` | 2 / 3 | CONFIRMED |
| 0x00437eb4 | `a_roll179` | `control_roll179_tag.Component` (alias `roll179`) | `TRollover*` | 0 / 0 | CONFIRMED |
| 0x00437ecc | `a_roll180` | `control_roll180_tag.Component` (alias `roll180`) | `TRollover*` | 0 / 0 | CONFIRMED |
| 0x00437ee4 | `a_roll181` | `control_roll181_tag.Component` (alias `roll181`) | `TRollover*` | 0 / 0 | CONFIRMED |
| 0x00437efc | `a_roll182` | `control_roll182_tag.Component` (alias `roll182`) | `TRollover*` | 0 / 0 | CONFIRMED |
| 0x00437f14 | `a_roll183` | `control_roll183_tag.Component` (alias `roll183`) | `TRollover*` | 0 / 0 | CONFIRMED |
| 0x00437f2c | `a_roll184` | `control_roll184_tag.Component` (alias `roll184`) | `TRollover*` | 0 / 1 | CONFIRMED |
| 0x00437f44 | `a_flag1` | `control_flag1_tag.Component` (alias `flag1`) | `TFlagSpinner*` | 0 / 2 | CONFIRMED |
| 0x00437f5c | `lite20` | `control_lite20_tag.Component` (alias `lite20`) | `TLight*` | 2 / 5 | CONFIRMED |
| 0x00437f74 | `literoll179` | `control_literoll179_tag.Component` (alias `literoll179`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437f8c | `literoll180` | `control_literoll180_tag.Component` (alias `literoll180`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437fa4 | `literoll181` | `control_literoll181_tag.Component` (alias `literoll181`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437fbc | `literoll182` | `control_literoll182_tag.Component` (alias `literoll182`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437fd4 | `literoll183` | `control_literoll183_tag.Component` (alias `literoll183`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00437fec | `literoll184` | `control_literoll184_tag.Component` (alias `literoll184`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438004 | `fuel_bargraph` | `control_fuel_bargraph_tag.Component` (alias `fuel_bargraph`) | `TLightBargraph*` | 11 / 13 | CONFIRMED |
| 0x00438014 | `a_kout2` | `control_kickout2_tag.Component` (alias `kickout2`) | `TKickout*` | 0 / 4 | CONFIRMED |
| 0x00438024 | `lite21` | `control_lite21_tag.Component` (alias `lite21`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438034 | `lite22` | `control_lite22_tag.Component` (alias `lite22`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438044 | `lite23` | `control_lite23_tag.Component` (alias `lite23`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438054 | `lite24` | `control_lite24_tag.Component` (alias `lite24`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438074 | `hyperspace_lights` | `control_hyper_lights_tag.Component` (alias `hyper_lights`) | `TLightGroup*` | 2 / 2 | CONFIRMED |
| 0x00438084 | `lite25` | `control_lite25_tag.Component` (alias `lite25`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x00438094 | `lite26` | `control_lite26_tag.Component` (alias `lite26`) | `TLight*` | 2 / 3 | CONFIRMED |
| 0x004380a4 | `lite130` | `control_lite130_tag.Component` (alias `lite130`) | `TLight*` | 2 / 3 | CONFIRMED |
| 0x004380b4 | `a_flag2` | `control_flag2_tag.Component` (alias `flag2`) | `TFlagSpinner*` | 0 / 2 | CONFIRMED |
| 0x004380c4 | `lite19` | `control_lite19_tag.Component` (alias `lite19`) | `TLight*` | 1 / 4 | CONFIRMED |
| 0x004380ec | `v_sink1` | `control_sink1_tag.Component` (alias `sink1`) | `TSink*` | 1 / 4 | CONFIRMED |
| 0x004380fc | `v_sink2` | `control_sink2_tag.Component` (alias `sink2`) | `TSink*` | 1 / 4 | CONFIRMED |
| 0x0043810c | `v_sink3` | `control_sink3_tag.Component` (alias `sink3`) | `TSink*` | 1 / 4 | CONFIRMED |
| 0x0043811c | `lite5` | `control_lite5_tag.Component` (alias `lite5`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x0043812c | `lite6` | `control_lite6_tag.Component` (alias `lite6`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x0043813c | `lite7` | `control_lite7_tag.Component` (alias `lite7`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x0043815c | `worm_hole_lights` | `control_worm_hole_lights_tag.Component` (alias `worm_hole_lights`) | `TLightGroup*` | 2 / 4 | CONFIRMED |
| 0x0043816c | `lite4` | `control_lite4_tag.Component` (alias `lite4`) | `TLight*` | 2 / 4 | CONFIRMED |
| 0x0043817c | `lite2` | `control_lite2_tag.Component` (alias `lite2`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x0043818c | `lite3` | `control_lite3_tag.Component` (alias `lite3`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x004381ac | `bsink_arrow_lights` | `control_bsink_arrow_lights_tag.Component` (alias `bsink_arrow_lights`) | `TLightGroup*` | 2 / 5 | CONFIRMED |
| 0x004381fc | `a_flip1` | `control_flip1_tag.Component` (alias `flip1`) | `TFlipper*` | 0 / 2 | CONFIRMED |
| 0x0043820c | `a_flip2` | `control_flip2_tag.Component` (alias `flip2`) | `TFlipper*` | 0 / 2 | CONFIRMED |
| 0x0043821c | `plunger` | `control_plunger_tag.Component` (alias `plunger`) | `TPlunger*` | 1 / 2 | CONFIRMED |
| 0x00438234 | `l_trek_lights` | `control_l_trek_lights_tag.Component` (alias `l_trek_lights`) | `TLightGroup*` | 4 / 9 | CONFIRMED |
| 0x0043824c | `r_trek_lights` | `control_r_trek_lights_tag.Component` (alias `r_trek_lights`) | `TLightGroup*` | 4 / 9 | CONFIRMED |
| 0x0043825c | `a_targ1` | `control_target1_tag.Component` (alias `target1`) | `TPopupTarget*` | 2 / 6 | CONFIRMED |
| 0x0043826c | `a_targ2` | `control_target2_tag.Component` (alias `target2`) | `TPopupTarget*` | 2 / 6 | CONFIRMED |
| 0x0043827c | `a_targ3` | `control_target3_tag.Component` (alias `target3`) | `TPopupTarget*` | 2 / 6 | CONFIRMED |
| 0x0043828c | `lite61` | `control_lite61_tag.Component` (alias `lite61`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x0043829c | `lite60` | `control_lite60_tag.Component` (alias `lite60`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x004382ac | `lite59` | `control_lite59_tag.Component` (alias `lite59`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x004382bc | `lite58` | `control_lite58_tag.Component` (alias `lite58`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x004382d4 | `a_targ6` | `control_target6_tag.Component` (alias `target6`) | `TPopupTarget*` | 2 / 5 | CONFIRMED |
| 0x004382e4 | `a_targ5` | `control_target5_tag.Component` (alias `target5`) | `TPopupTarget*` | 2 / 5 | CONFIRMED |
| 0x004382f4 | `a_targ4` | `control_target4_tag.Component` (alias `target4`) | `TPopupTarget*` | 2 / 5 | CONFIRMED |
| 0x00438304 | `lite11` | `control_lite11_tag.Component` (alias `lite11`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438314 | `lite12` | `control_lite12_tag.Component` (alias `lite12`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438324 | `lite13` | `control_lite13_tag.Component` (alias `lite13`) | `TLight*` | 0 / 0 | CONFIRMED |
| 0x00438344 | `bumper_target_lights` | `control_bumber_target_lights_tag.Component` (alias `bumber_target_lights`) | `TLightGroup*` | 3 / 3 | CONFIRMED |
| 0x00438364 | `a_targ9` | `control_target9_tag.Component` (alias `target9`) | `TPopupTarget*` | 2 / 5 | CONFIRMED |
| 0x00438374 | `a_targ8` | `control_target8_tag.Component` (alias `target8`) | `TPopupTarget*` | 2 / 5 | CONFIRMED |
| 0x00438384 | `a_targ7` | `control_target7_tag.Component` (alias `target7`) | `TPopupTarget*` | 2 / 5 | CONFIRMED |
| 0x004383a4 | `top_target_lights` | `control_top_target_lights_tag.Component` (alias `top_target_lights`) | `TLightGroup*` | 4 / 4 | CONFIRMED |
| 0x004383c4 | `a_targ10` | `control_target10_tag.Component` (alias `target10`) | `TSoloTarget*` | 1 / 3 | CONFIRMED |
| 0x004383dc | `a_targ11` | `control_target11_tag.Component` (alias `target11`) | `TSoloTarget*` | 1 / 3 | CONFIRMED |
| 0x004383f4 | `a_targ12` | `control_target12_tag.Component` (alias `target12`) | `TSoloTarget*` | 0 / 2 | CONFIRMED |
| 0x00438404 | `lite70` | `control_lite70_tag.Component` (alias `lite70`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438414 | `lite71` | `control_lite71_tag.Component` (alias `lite71`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438424 | `lite72` | `control_lite72_tag.Component` (alias `lite72`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438444 | `top_circle_tgt_lights` | `control_top_circle_tgt_lights_tag.Component` (alias `top_circle_tgt_lights`) | `TLightGroup*` | 2 / 3 | CONFIRMED |
| 0x0043845c | `a_targ13` | `control_target13_tag.Component` (alias `target13`) | `TSoloTarget*` | 1 / 4 | CONFIRMED |
| 0x00438474 | `a_targ14` | `control_target14_tag.Component` (alias `target14`) | `TSoloTarget*` | 1 / 4 | CONFIRMED |
| 0x0043848c | `a_targ15` | `control_target15_tag.Component` (alias `target15`) | `TSoloTarget*` | 0 / 3 | CONFIRMED |
| 0x0043849c | `lite101` | `control_lite101_tag.Component` (alias `lite101`) | `TLight*` | 2 / 3 | CONFIRMED |
| 0x004384ac | `lite102` | `control_lite102_tag.Component` (alias `lite102`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x004384bc | `lite103` | `control_lite103_tag.Component` (alias `lite103`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x004384d4 | `ramp_tgt_lights` | `control_ramp_tgt_lights_tag.Component` (alias `ramp_tgt_lights`) | `TLightGroup*` | 2 / 4 | CONFIRMED |
| 0x004384e4 | `lite198` | `control_lite198_tag.Component` (alias `lite198`) | `TLight*` | 2 / 38 | CONFIRMED |
| 0x004384fc | `a_targ16` | `control_target16_tag.Component` (alias `target16`) | `TSoloTarget*` | 1 / 4 | CONFIRMED |
| 0x00438514 | `a_targ17` | `control_target17_tag.Component` (alias `target17`) | `TSoloTarget*` | 1 / 4 | CONFIRMED |
| 0x0043852c | `a_targ18` | `control_target18_tag.Component` (alias `target18`) | `TSoloTarget*` | 0 / 3 | CONFIRMED |
| 0x00438544 | `a_targ19` | `control_target19_tag.Component` (alias `target19`) | `TSoloTarget*` | 1 / 4 | CONFIRMED |
| 0x0043855c | `a_targ20` | `control_target20_tag.Component` (alias `target20`) | `TSoloTarget*` | 1 / 4 | CONFIRMED |
| 0x00438574 | `a_targ21` | `control_target21_tag.Component` (alias `target21`) | `TSoloTarget*` | 0 / 3 | CONFIRMED |
| 0x00438584 | `lite104` | `control_lite104_tag.Component` (alias `lite104`) | `TLight*` | 1 / 2 | CONFIRMED |
| 0x00438594 | `lite105` | `control_lite105_tag.Component` (alias `lite105`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x004385a4 | `lite106` | `control_lite106_tag.Component` (alias `lite106`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x004385b4 | `lite107` | `control_lite107_tag.Component` (alias `lite107`) | `TLight*` | 1 / 2 | CONFIRMED |
| 0x004385c4 | `lite108` | `control_lite108_tag.Component` (alias `lite108`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x004385d4 | `lite109` | `control_lite109_tag.Component` (alias `lite109`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x004385f4 | `lchute_tgt_lights` | `control_lchute_tgt_lights_tag.Component` (alias `lchute_tgt_lights`) | `TLightGroup*` | 2 / 4 | CONFIRMED |
| 0x00438614 | `bpr_solotgt_lights` | `control_bpr_solotgt_lights_tag.Component` (alias `bpr_solotgt_lights`) | `TLightGroup*` | 2 / 4 | CONFIRMED |
| 0x0043862c | `a_targ22` | `control_target22_tag.Component` (alias `target22`) | `TSoloTarget*` | 0 / 2 | CONFIRMED |
| 0x0043863c | `lite110` | `control_lite110_tag.Component` (alias `lite110`) | `TLight*` | 3 / 4 | CONFIRMED |
| 0x00438654 | `a_roll9` | `control_roll9_tag.Component` (alias `roll9`) | `TLightRollover*` | 0 / 1 | CONFIRMED |
| 0x0043866c | `a_kout3` | `control_kickout3_tag.Component` (alias `kickout3`) | `TKickout*` | 0 / 1 | CONFIRMED |
| 0x00438684 | `a_kout1` | `control_kickout1_tag.Component` (alias `kickout1`) | `TKickout*` | 1 / 1 | CONFIRMED |
| 0x00438694 | `lite62` | `control_lite62_tag.Component` (alias `lite62`) | `TLight*` | 2 / 2 | CONFIRMED |
| 0x004386ac | `drain` | `control_drain_tag.Component` (alias `drain`) | `TDrain*` | 1 / 1 | CONFIRMED |
| 0x004386c4 | `s_onewy1` | `control_oneway1_tag.Component` (alias `oneway1`) | `TOneway*` | 0 / 0 | CONFIRMED |
| 0x004386d4 | `s_trip1` | `control_trip1_tag.Component` (alias `trip1`) | `TTripwire*` | 0 / 0 | CONFIRMED |
| 0x004386e4 | `s_trip2` | `control_trip2_tag.Component` (alias `trip2`) | `TTripwire*` | 0 / 0 | CONFIRMED |
| 0x004386f4 | `s_trip3` | `control_trip3_tag.Component` (alias `trip3`) | `TTripwire*` | 0 / 0 | CONFIRMED |
| 0x00438704 | `s_trip4` | `control_trip4_tag.Component` (alias `trip4`) | `TTripwire*` | 0 / 0 | CONFIRMED |
| 0x00438714 | `s_trip5` | `control_trip5_tag.Component` (alias `trip5`) | `TTripwire*` | 0 / 0 | CONFIRMED |
| 0x00438724 | `lite67` | `control_lite67_tag.Component` (alias `lite67`) | `TLight*` | 7 / 8 | CONFIRMED |
| 0x00438734 | `lite68` | `control_lite68_tag.Component` (alias `lite68`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438744 | `lite69` | `control_lite69_tag.Component` (alias `lite69`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438754 | `lite131` | `control_lite131_tag.Component` (alias `lite131`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438764 | `lite132` | `control_lite132_tag.Component` (alias `lite132`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438774 | `lite133` | `control_lite133_tag.Component` (alias `lite133`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438794 | `skill_shot_lights` | `control_skill_shot_lights_tag.Component` (alias `skill_shot_lights`) | `TLightGroup*` | 5 / 5 | CONFIRMED |
| 0x004387a4 | `lite77` | `control_lite77_tag.Component` (alias `lite77`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x004387bc | `middle_circle` | `control_middle_circle_tag.Component` (alias `middle_circle`) | `TLightGroup*` | 2 / 6 | CONFIRMED |
| 0x004387d4 | `outer_circle` | `control_outer_circle_tag.Component` (alias `outer_circle`) | `TLightGroup*` | 1 / 3 | CONFIRMED |
| 0x004387e4 | `lite200` | `control_lite200_tag.Component` (alias `lite200`) | `TLight*` | 3 / 3 | CONFIRMED |
| 0x004387f4 | `lite199` | `control_lite199_tag.Component` (alias `lite199`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x0043880c | `soundwave9` | `control_soundwave9_tag.Component` (alias `soundwave9`) | `TSound*` | 0 / 16 | CONFIRMED |
| 0x00438824 | `soundwave10` | `control_soundwave10_tag.Component` (alias `soundwave10`) | `TSound*` | 0 / 3 | CONFIRMED |
| 0x0043883c | `soundwave21` | `control_soundwave21_tag.Component` (alias `soundwave21`) | `TSound*` | 2 / 2 | CONFIRMED |
| 0x00438854 | `soundwave23` | `control_soundwave23_tag.Component` (alias `soundwave23`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x0043886c | `soundwave24` | `control_soundwave24_tag.Component` (alias `soundwave24`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438884 | `soundwave30` | `control_soundwave30_tag.Component` (alias `soundwave30`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x0043889c | `soundwave28` | `control_soundwave28_tag.Component` (alias `soundwave28`) | `TSound*` | 0 / 0 | CONFIRMED |
| 0x004388b4 | `soundwave50` | `control_soundwave50_1_tag / control_soundwave50_2_tag` | `TSound*` | 1 / 1 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x004388cc | `soundwave25` | `control_soundwave25_tag.Component` (alias `soundwave25`) | `TSound*` | 2 / 2 | CONFIRMED |
| 0x004388e4 | `soundwave8` | `control_soundwave8_tag.Component` (alias `soundwave8`) | `TSound*` | 0 / 0 | CONFIRMED |
| 0x004388fc | `soundwave40` | `control_soundwave40_tag.Component` (alias `soundwave40`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438914 | `soundwave41` | `control_soundwave41_tag.Component` (alias `soundwave41`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x0043892c | `soundwave36` | `control_soundwave36_1_tag / control_soundwave36_2_tag` | `TSound*` | 1 / 1 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x00438944 | `soundwave50` | `control_soundwave50_1_tag / control_soundwave50_2_tag` | `TSound*` | 1 / 1 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x0043895c | `soundwave35` | `control_soundwave35_1_tag / control_soundwave35_2_tag` | `TSound*` | 1 / 1 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x00438974 | `soundwave36` | `control_soundwave36_1_tag / control_soundwave36_2_tag` | `TSound*` | 1 / 1 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x0043898c | `soundwave35` | `control_soundwave35_1_tag / control_soundwave35_2_tag` | `TSound*` | 1 / 1 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x004389a4 | `soundwave38` | `control_soundwave38_tag.Component` (alias `soundwave38`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x004389bc | `soundwave39` | `control_soundwave39_tag.Component` (alias `soundwave39`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x004389d4 | `soundwave44` | `control_soundwave44_tag.Component` (alias `soundwave44`) | `TSound*` | 0 / 0 | CONFIRMED |
| 0x004389ec | `soundwave45` | `control_soundwave45_tag.Component` (alias `soundwave45`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438a04 | `soundwave46` | `control_soundwave46_tag.Component` (alias `soundwave46`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438a1c | `soundwave47` | `control_soundwave47_tag.Component` (alias `soundwave47`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438a34 | `soundwave48` | `control_soundwave48_tag.Component` (alias `soundwave48`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438a4c | `soundwave52` | `control_soundwave52_tag.Component` (alias `soundwave52`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438a64 | `soundwave14` | `control_soundwave14_1_tag / control_soundwave14_2_tag` | `TSound*` | 2 / 2 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x00438a7c | `soundwave59` | `control_soundwave59_tag.Component` (alias `soundwave59`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438a94 | `soundwave27` | `control_soundwave27_tag.Component` (alias `soundwave27`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438aac | `soundwave14` | `control_soundwave14_1_tag / control_soundwave14_2_tag` | `TSound*` | 6 / 6 | CONFIRMED addr; tag ident ambiguous (duplicate group name) |
| 0x00438ac4 | `soundwave3` | `control_soundwave3_tag.Component` (alias `soundwave3`) | `TSound*` | 2 / 2 | CONFIRMED |
| 0x00438adc | `soundwave26` | `control_soundwave26_tag.Component` (alias `soundwave26`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438af4 | `soundwave49D` | `control_soundwave49D_tag.Component` (alias `soundwave49D`) | `TSound*` | 4 / 4 | CONFIRMED |
| 0x00438b0c | `soundwave7` | `control_soundwave7_tag.Component` (alias `soundwave7`) | `TSound*` | 1 / 1 | CONFIRMED |
| 0x00438b24 | `info_text_box` | `control_info_text_box_tag.Component` (alias `info_text_box`) | `TTextBox*` | 21 / 23 | CONFIRMED |
| 0x00438b44 | `mission_text_box` | `control_mission_text_box_tag.Component` (alias `mission_text_box`) | `TTextBox*` | 1 / 40 | CONFIRMED |
| 0x00438b54 | `v_sink7` | `control_sink7_tag.Component` (alias `sink7`) | `TSink*` | 0 / 0 | CONFIRMED |
| 0x00438b64 | `lite300` | `control_lite300_tag.Component` (alias `lite300`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x00438b74 | `lite301` | `control_lite301_tag.Component` (alias `lite301`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438b84 | `lite302` | `control_lite302_tag.Component` (alias `lite302`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438b94 | `lite303` | `control_lite303_tag.Component` (alias `lite303`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438ba4 | `lite304` | `control_lite304_tag.Component` (alias `lite304`) | `TLight*` | 0 / 4 | CONFIRMED |
| 0x00438bb4 | `lite305` | `control_lite305_tag.Component` (alias `lite305`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438bc4 | `lite306` | `control_lite306_tag.Component` (alias `lite306`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438bd4 | `lite307` | `control_lite307_tag.Component` (alias `lite307`) | `TLight*` | 0 / 4 | CONFIRMED |
| 0x00438be4 | `lite308` | `control_lite308_tag.Component` (alias `lite308`) | `TLight*` | 0 / 5 | CONFIRMED |
| 0x00438bf4 | `lite309` | `control_lite309_tag.Component` (alias `lite309`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438c04 | `lite310` | `control_lite310_tag.Component` (alias `lite310`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438c14 | `lite311` | `control_lite311_tag.Component` (alias `lite311`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438c24 | `lite312` | `control_lite312_tag.Component` (alias `lite312`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438c34 | `lite313` | `control_lite313_tag.Component` (alias `lite313`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438c44 | `lite314` | `control_lite314_tag.Component` (alias `lite314`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x00438c54 | `lite315` | `control_lite315_tag.Component` (alias `lite315`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438c64 | `lite316` | `control_lite316_tag.Component` (alias `lite316`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438c74 | `lite317` | `control_lite317_tag.Component` (alias `lite317`) | `TLight*` | 0 / 4 | CONFIRMED |
| 0x00438c84 | `lite318` | `control_lite318_tag.Component` (alias `lite318`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438c94 | `lite319` | `control_lite319_tag.Component` (alias `lite319`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438ca4 | `lite320` | `control_lite320_tag.Component` (alias `lite320`) | `TLight*` | 0 / 3 | CONFIRMED |
| 0x00438cb4 | `lite321` | `control_lite321_tag.Component` (alias `lite321`) | `TLight*` | 0 / 2 | CONFIRMED |
| 0x00438cc4 | `lite322` | `control_lite322_tag.Component` (alias `lite322`) | `TLight*` | 0 / 1 | CONFIRMED |
| 0x00438cdc | `goal_lights` | `control_goal_lights_tag.Component` (alias `goal_lights`) | `TLightGroup*` | 0 / 2 | CONFIRMED |
| 0x00438cec | `lite38` | `control_lite38_tag.Component` (alias `lite38`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438cfc | `lite39` | `control_lite39_tag.Component` (alias `lite39`) | `TLight*` | 1 / 1 | CONFIRMED |
| 0x00438d0c | `lite40` | `control_lite40_tag.Component` (alias `lite40`) | `TLight*` | 1 / 1 | CONFIRMED |

---

## 3. Struct offsets

The build is byte-packed, so a 4-byte field directly follows a 2-byte pair of `char`s.
Every "derived" row below sits on a 4-byte-spaced chain between two CONFIRMED anchors.

### 3.1 `TPinballComponent` (base of everything)

| offset | field | conf. | evidence |
|---|---|---|---|
| +0x00 | vtable pointer | CONFIRMED | `(**(code **)(*p + n))(...)` everywhere. |
| +0x04 | `char UnusedBaseFlag` | PROBABLE | Only slot left between the vptr and `ActiveFlag`. |
| +0x05 | `char ActiveFlag` | CONFIRMED | `GravityWellKickoutControl`: `*(undefined1 *)((int)param_2 + 5) = 0` == `caller->ActiveFlag = 0`; and `*(DAT_00438684 + 5) = 1` == `kickout1->ActiveFlag = 1`. Also `CheckBallInControlBounds` tests `*(char*)(ball+5)` == `ball.ActiveFlag`. |
| +0x06 | `int MessageField` | CONFIRMED | `BoosterTargetControl`: `*(int*)(param_2+6)` and `*(int*)(DAT_0043825c+6) = 0` == `caller->MessageField` / `target1->MessageField = 0`. `AdvanceWormHoleDestination` reads `lite198`/`lite4` `MessageField` the same way. ~40 sites. |
| +0x0a | `char* GroupName` | PROBABLE | Declaration order after `MessageField`. |
| +0x0e | `component_control* Control` | PROBABLE | Declaration order. |
| +0x12 | `int GroupIndex` | PROBABLE | Declaration order. |
| +0x16 | `render_sprite* RenderSprite` | PROBABLE | Declaration order. |
| +0x1a | `TPinballTable* PinballTable` | PROBABLE | Declaration order. |
| +0x1e | `ListBitmap` | PROBABLE | Declaration order. |
| +0x22, +0x26 | `float VisualPosNormX/Y` | PROBABLE | Needed to make the base end at 0x2a, which is what the TLight chain below requires. |
| — | **base size 0x2a** | CONFIRMED (by implication) | The only base size that puts `TLight::LightOnFlag` at the observed +0x42 with the upstream field order and 4-byte fields. |

### 3.2 vtable slots (byte offsets into the vtable)

| slot offset | method | conf. | evidence |
|---|---|---|---|
| +0x00 | destructor | PROBABLE | Slot 0 convention; never called from control code. |
| +0x04 | `int Message(MessageCode code, float value)` | CONFIRMED | Hundreds of sites, always `(**(code **)(*comp + 4))(code, value)` where the source has `comp->Message(code, value)`. |
| +0x08 | `void port_draw()` | PROBABLE | Declaration order; not called from control code. |
| +0x0c | `int get_scoring(unsigned index)` | CONFIRMED | `BumperControl`: `(**(code **)(*param_2 + 0xc))(*(int*)(param_2+0x76))` == `caller->get_scoring(static_cast<TBumper*>(caller)->BmpIndex)`. Always feeds `AddScore`. |
| +0x10 | `vector2 get_coordinates()` | PROBABLE | Declaration order. |
| +0x18 | `float TSound::Play(TPinballComponent* caller)` | CONFIRMED | `GravityWellKickoutControl`: `fVar4 = (**(code **)(*DAT_00438b0c + 0x18))()` then `caller->Message(TKickoutRestartTimer, fVar4)` == `auto duration = soundwave7->Play(lite62, ...); caller->Message(TKickoutRestartTimer, duration)`. Also `OutLaneRolloverControl`, `BoosterTargetControl`, `BallDrainControl` (soundwave26/47/27/59/3). Return value is the clip length. |

(+0x14 is never taken in this translation unit.)

### 3.3 `TLight`

Anchored by three independent confirmations; the rest of the chain is 4-byte spaced in
upstream declaration order.

| offset | field | conf. | evidence |
|---|---|---|---|
| +0x2a, +0x2e | `int BmpArr[2]` | PROBABLE | Chain. |
| +0x32, +0x36 | `float FlashDelay[2]` | PROBABLE | Chain. |
| +0x3a | `int FlashTimer` | PROBABLE | Chain. |
| +0x3e | `bool FlashLightOnFlag` | PROBABLE | Chain. |
| +0x42 | `LightOnFlag` | CONFIRMED | `light_on()` is inlined everywhere as `*(p+0x42)==0 && *(p+0x4e)==0 && *(p+0x46)==0`, in exactly the source's term order `LightOnFlag \|\| ToggledOnFlag \|\| FlasherOnFlag`. |
| +0x46 | `FlasherOnFlag` | CONFIRMED | `ReentryLanesRolloverControl` tests `*(p+0x46)==0` as the outer guard == `if (!light->FlasherOnFlag)`, with `*(p+0x42)` as the inner `LightOnFlag` test. |
| +0x4a | `ToggledOffFlag` | PROBABLE | The one slot in the chain never read by control code. |
| +0x4e | `ToggledOnFlag` | CONFIRMED | Third term of the inlined `light_on()`. |
| +0x52 | `TurnOffAfterFlashingFg` | PROBABLE | Chain. |
| +0x56 | `int LightOnBmpIndex` | PROBABLE | Chain. |
| +0x5a, +0x5e | `float SourceDelay[2]` | PROBABLE | Chain. |
| +0x62 | `int TimeoutTimer` | PROBABLE | Chain. |

### 3.4 Other component types

| type | offset | field | conf. | evidence |
|---|---|---|---|---|
| `TBumper` | +0x76 | `int BmpIndex` | CONFIRMED | `BumperControl` passes it to `get_scoring`; `ReentryLanesRolloverControl` tests `*(DAT_00437a6c + 0x76) < 3` == `bump1->BmpIndex < 3`. |
| `TSink` | +0x7e | `float TimerTime` | CONFIRMED | `WormHoleControl` passes `*(param_2 + 0x7e)` as the value of every `TLightFlasherStartTimedThenStayOff` / `TSinkResetTimer` message == `sink->TimerTime`. |
| `TCollisionComponent` | +0x4a | `AABB.XMax` | CONFIRMED | `CheckBallInControlBounds`; `RectF` is `{XMax, YMax, XMin, YMin}` upstream and the four comparisons line up only under that order. |
| `TCollisionComponent` | +0x4e | `AABB.YMax` | CONFIRMED | Same. |
| `TCollisionComponent` | +0x52 | `AABB.XMin` | CONFIRMED | Same. |
| `TCollisionComponent` | +0x56 | `AABB.YMin` | CONFIRMED | Same. |
| `TBall` | +0x9f | `Position.X` | CONFIRMED | `CheckBallInControlBounds` compares it against the X bounds. |
| `TBall` | +0xa3 | `Position.Y` | CONFIRMED | Same, Y bounds. |
| `TBall` | +0xa7 | `Position.Z` | PROBABLE | Third component of the same `vector3`. |
| `TBall` | +0x157 | `int StuckCounter` | CONFIRMED | `UnstuckBall`: `if (0x14 < *(int*)(ball + 0x157))` == `if (ball.StuckCounter <= 20) … else …`. |

### 3.5 `TPinballTable` (base `DAT_00437a58`)

| offset | field | conf. | evidence |
|---|---|---|---|
| +0x102 | `int PlayerCount` | CONFIRMED | `BallDrainControl`: `*(T+0x106) - *(T+0x102) == -1` == `CurrentPlayer + 1 == PlayerCount` (negated source branch). |
| +0x106 | `int CurrentPlayer` | CONFIRMED | Same site; also the 0/1/2/default switch picking STRING198..201 == `switch (TableG->CurrentPlayer)`. |
| +0x13e | `TLightGroup* LightGroup` | CONFIRMED | `BallDrainControl`: `(**(code**)(**(int**)(T+0x13e) + 4))(0x10, 3.0)` == `TableG->LightGroup->Message(TLightFlasherStartTimedThenStayOff, 3.0)`. |
| +0x152 | `float CollisionCompOffset` | CONFIRMED | `CheckBallInControlBounds`: `*(float*)(T+0x152) / 2.0` == `TableG->CollisionCompOffset / 2.0f`. |
| +0x15e | `int ScoreMultiplier` | CONFIRMED | Saved/neutralised/restored by `SpecialAddScore`. (Original stores the multiplier value, set to 1; upstream stores an index and sets 0.) |
| +0x162 | `int ScoreAdded` | PROBABLE | Chain between +0x15e and +0x16a. |
| +0x166 | `int ReflexShotScore` | PROBABLE | Chain. |
| +0x16a | `int BonusScore` | CONFIRMED | `BallDrainControl`: `FUN_00423020(*(T+0x16a))` == `SpecialAddScore(TableG->BonusScore)`; and `*(T+0x16a) = 25000` == `TableG->BonusScore = 25000`. |
| +0x16e | `BonusScoreFlag` | CONFIRMED | First of the three fields `SpecialAddScore` saves and clears. |
| +0x172 | `int JackpotScore` | PROBABLE | Chain. |
| +0x176 | `JackpotScoreFlag` | CONFIRMED | `JackpotLightControl` is a one-liner: `*(T+0x176) = 0` == `TableG->JackpotScoreFlag = false`; also the second field `SpecialAddScore` saves. |
| +0x17a..+0x182 | `UnknownP71`, +2 unidentified ints | — | The original has two more fields here than upstream's layout; needed to make the confirmed +0x186 fall where it does. |
| +0x186 | `int BallCount` | CONFIRMED | `ChangeBallCount(*(T+0x186) - 1)` == `ChangeBallCount(TableG->BallCount - 1)`, and `*(T+0x186) == 0` in the same condition as `TableG->BallCount`. |
| +0x18a | `int MaxBallCount` | PROBABLE | Chain. |
| +0x18e | `int ExtraBalls` | CONFIRMED | `if (*(T+0x18e) == 0) { ChangeBallCount… } else { *(T+0x18e) -= 1; … }` == `if (TableG->ExtraBalls) { TableG->ExtraBalls--; … } else { ChangeBallCount… }`. |
| +0x192 | `int MultiballCount` | CONFIRMED | `WormHoleControl`: `*(T+0x192) != 1` == `TableG->MultiballCount == 1`; `BallDrainControl` switches on it over 0/1/2; `UnstuckBall` decrements it. |
| +0x196 | `int BallLockedCounter` | PROBABLE | Chain. |
| +0x19a | `MultiballFlag` | CONFIRMED | `WormHoleControl`: `if (*(T+0x19a) != 0)` == `if (TableG->MultiballFlag)`. |
| +0x19e | `int UnknownP78` | CONFIRMED | `BallDrainControl`: `*(T+0x19e) = *(T+0x19e) - 1` == `--TableG->UnknownP78` (upstream's name for it). |
| +0x1a2 | `int ReplayActiveFlag` | PROBABLE | Chain. |
| +0x1a6 | `int ReplayTimer` | PROBABLE | Chain. |
| +0x1aa, +0x1ae | `UnknownP81`, `UnknownP82` | PROBABLE | Chain. |
| +0x1b2 | `int TiltLockFlag` | CONFIRMED | `BallDrainControl` tests it twice, matching `if (TableG->TiltLockFlag)` and `if (!TableG->TiltLockFlag)`; it is the last field and the chain from +0x186 lands on it exactly. |

The chain +0x186 -> +0x1b2 is twelve consecutive 4-byte fields with four independent
CONFIRMED anchors in it (`BallCount`, `ExtraBalls`, `MultiballCount`, `MultiballFlag`,
`UnknownP78`, `TiltLockFlag`), which is why the interpolated rows are PROBABLE rather
than GUESS.

---

## 4. `MessageCode` values (binary numbering)

The original enum is **not** the upstream one: private codes 0..23 coincide, group codes
are shifted by about +4, component/control codes by +9..+11, and public codes above 1012
by +1..+2. Only directly observed pairs are listed.

| binary | upstream name | upstream value | evidence |
|---|---|---|---|
| 0x00 | `TLightTurnOff` | 0 | `ReentryLanesRolloverControl`, `bmpr_inc_lights->Message(TLightTurnOff, 0)`. |
| 0x04 | `TLightFlasherStart` | 4 | `OutLaneRolloverControl`, `lite30->Message(TLightFlasherStart, 0)`. |
| 0x07 | `TLightFlasherStartTimed` | 7 | `ExtraBallLightControl`, `lite17->Message(TLightFlasherStartTimed, 5.0)` (`0x40a00000`). |
| 0x08 | `TLightTurnOffTimed` | 8 | `FuelRollover1Control`, `literoll179->Message(TLightTurnOffTimed, 0.05f)`. |
| 0x09 | `TLightTurnOnTimed` | 9 | `FlipperRebounderControl1`, `lite84->Message(TLightTurnOnTimed, 0.1f)` (`0x3dcccccd`). |
| 0x0b | `TLightSetOnStateBmpIndex` | 11 | `AdvanceWormHoleDestination`. |
| 0x0c | `TBumperIncBmpIndex` | 12 | `ReentryLanesRolloverControl`, `attack_bump->Message(TBumperIncBmpIndex, 0)`. |
| 0x10 | `TLightFlasherStartTimedThenStayOff` | 16 | `WormHoleControl`, `WormholeLightArray1[i]->Message(..., sink->TimerTime)`. |
| 0x13 | `TLightResetAndTurnOn` | 19 | `SpaceWarpRolloverControl`, both lights. |
| 0x14 | `TLightResetAndTurnOff` | 20 | `BallDrainControl`'s long reset block. |
| 0x17 | `TLightSetMessageField` | 23 | `AdvanceWormHoleDestination`. |
| 0x26 | `TLightGroupReset` | 34 | `ReentryLanesRolloverControl`, `l_trek_lights->Message(TLightGroupReset, 0)`. |
| 0x29 | `TLightGroupGetOnCount` | 37 | `FuelRollover1Control`, `fuel_bargraph->Message(TLightGroupGetOnCount, 0) > 1`. |
| 0x2a | `TLightGroupGetLightCount` | 38 | `ReentryLanesRolloverControl`, compared against the GetOnCount result. |
| 0x2b | `TLightGroupGetMessage2` | 39 | `ReentryLanesRolloverControl`, the `l_trek_lights` guard. |
| 0x2d | `TLightGroupResetAndTurnOn` | 41 | `cheat_bump_rank`, `middle_circle->Message(..., 2.0f)`. |
| 0x31 | `TLightGroupToggleSplitIndex` | 45 | `FuelRollover1Control`, `fuel_bargraph->Message(..., 1.0)`. |
| 0x33 | `TLightGroupCountdownEnded` | 47 | `MissionControl`'s first pre-switch case. |
| 0x34 | `TComponentGroupResetNotifyTimer` | 48 | `ReentryLanesRolloverControl`, `attack_bump->Message(..., 60.0)` (`0x42700000`). |
| 0x36 | `TPopupTargetEnable` | 50 | `BoosterTargetControl`, re-enabling target1..3. |
| 0x3f | `TGateEnable` | 54 | `LeftKickerControl`/`RightKickerControl` one-liners. |
| 0x40 | `TKickoutRestartTimer` | 55 | `GravityWellKickoutControl`, with the sound duration. |
| 0x41 | `TSinkResetTimer` | 56 | `WormHoleControl`, `WormholeSinkArray[i]->Message(..., sink->TimerTime)`. |
| 0x46 | `ControlTimerExpired` | 60 | `LeftKickerControl`, `JackpotLightControl`, `ExtraBallLightControl`, `BallDrainControl`. |
| 0x4a | `ControlCollision` | 63 | The entry guard of ~45 handlers. |
| 0x4b | `ControlEnableMultiplier` | 64 | `GravityWellKickoutControl`'s second switch case. |
| 0x4f | `ControlMissionComplete` | 66 | `BallDrainControl`: `FUN_0042a260(0x4f, NULL)`. |
| 0x50 | `ControlMissionStarted` | 67 | `MissionControl` recodes to it for `mission_text_box`/`Resume`. |
| 0x3f1 | `Resume` | 1009 | `MissionControl`'s third pre-switch case. |
| 0x3f4 | `ClearTiltLock` | 1012 | `BallDrainControl`, `TableG->Message(ClearTiltLock, 0)`. |
| 0x3f9 | `PlungerStartFeedTimer` | 1016 | `BallDrainControl`'s timer-expiry branch. |
| 0x3fb | `PlungerRelaunchBall` | 1018 | `UnstuckBall` and `table_bump_ball_sink_lock`-adjacent code. |
| 0x3ff | `SwitchToNextPlayer` | 1021 | `BallDrainControl`, `TableG->Message(SwitchToNextPlayer, 0)`. |
| 0x400 | `GameOver` | 1022 | `BallDrainControl`, `TableG->Message(GameOver, 0)`. |
| 0x402 | `Reset` | 1024 | `BallDrainControl`'s reset block; `GravityWellKickoutControl`'s third case. |

---

## 5. Still unknown

High-traffic symbols that were **not** resolved:

- `FUN_0042a750` — a large switch over private codes 0x57..0x6x that only displays
  strings in `info_text_box`/`mission_text_box`. It is not `control::table_control_handler`
  (that one is a two-line `SetTiltLock` handler in the source) and has no obvious upstream
  counterpart; likely a Full-Tilt-only announcement dispatcher. 
- `FUN_0042a660` / `FUN_0042a720` — the credits/quote scroller pair (`DAT_00432cf8`
  cursor, `DAT_00439a50` timer). Upstream folds this into
  `pbctrl_bdoor_controller`'s static string tables, so there is no 1:1 source function.
- `FUN_0041d930`, `FUN_0041dc50`, `FUN_0041e070`, `FUN_0041e0e0` — four `TTextBox`
  entry points besides `Display`; the argument shapes are clear but the upstream
  `TTextBox` has no matching methods.
- `FUN_00421bd0`, `FUN_004037a0`, `FUN_00410ca0`, `FUN_0041d930` — single-call-site
  helpers outside the decompiled range; no source anchor.
- `DAT_00432d04` (GUESS only) and `DAT_0043721c` (PROBABLE only) are the two globals
  with real traffic whose identity is still soft.
- `control::make_links` / `ClearLinks` / `make_component_link` / `handler` /
  `table_control_handler` — not located. They live below 0x423000 (the
  `score_components` table at 0x00438d10 and `simple_components` at 0x00439290 are what
  they iterate), and that range was not decompiled.
