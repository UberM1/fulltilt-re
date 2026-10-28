# Engine changes the port needs

**Done.** All of `src/` compiles and links against the patched engine, the engine still
builds, and all three tables run (`test/run_tables.sh`).

```sh
nix-build build.nix                       # engine, patched, with the rules linked in
test/run_tables.sh                        # Space Cadet, Dragon's Keep, Skulduggery
```

The patch is `engine/fulltilt-engine.patch`. Nothing in it changes Space Cadet's
behaviour: its registry rows carry `ModeBeforeComponent = false` and
`MultiplierIsValue = false`, which reduce `control::handler` and
`TPinballTable::AddScore` to exactly what upstream did.

How the list was arrived at, rather than guessed: the rules were written first, against
the originals, and then compiled against unmodified upstream headers. Every error was a
thing the engine did not have, so the error list *was* the change list. It went 88
errors -> 0 as each item below landed. The last four rows came out of linking rather
than compiling, which is why they are not in the original count.

| change | uses | files |
|---|---|---|
| `TPinballComponent::GetComponentType()` | 29 | `TPinballComponent.h`, `TPinballTable.cpp` |
| indexed per-table music | 21 | `midi.h`, `midi.cpp` |
| `TPinballTable::SuperJackpotScore` | 1 | `TPinballTable.h` |
| `TLightGroup::LightAt(index)` | 2 | `TLightGroup.h` |
| Full Tilt's multi-position `TBlocker` | 12 components | `TBlocker.h`, `TBlocker.cpp`, `TPinballComponent.h`, `table_rules.h` |
| `TableRules::ModeBeforeComponent` | 1 | `table_rules.h`, `control.cpp` |
| `TableRules::MultiplierIsValue` | 1 | `table_rules.h`, `TPinballTable.cpp` |
| per-table `make_links` / `ClearLinks` | 1 | `control.cpp` |
| `UnstuckBall` through `TPinballTable::Plunger` | 1 | `control.cpp` |
| per-table `RT_STRING` tables | all bonus-table text | `table_rules.h`, `table_rules.cpp`, `table_strings.cpp`, `pb.cpp` |

## 1. `TPinballComponent::GetComponentType()` — 17 uses

The single biggest gap, and it is a correction to upstream rather than an addition.

Upstream models vtable slot 0 as the destructor. It is not: the dispatcher reads it and
switches the result over `0x3ee`, `0x3f8`, `0x3f9`, `0x3fb`, `0x408`, `0x40e` — the
component *type ids*. Confirmed twice independently, in `CADET.EXE`'s `0x42a150` and in
`PIRATES.EXE`'s dispatcher `0x42d850`.

Dragon's rules need it because several handlers are bound to more than one component class
and discriminate at runtime — every quest's `TimerExpired` arm has to tell `mode_timer`
(type 1032) from `light_sweeps` (type 1038), and those arrive through the same message.

Space Cadet never needs it, which is why the decompilation could get away with calling the
slot a destructor.

## 2. Per-table music — 11 uses

`midi.cpp` loads `TABA1`..`TABA3` and `MidiTracks` holds three. Skulduggery ships
`TABB1`..`TABB6` and Dragon's Keep `TABC1`..`TABC7`, and Dragon's quest controllers select
a track per quest, so the rules need a `midi_track` handle and a `PlayMusic(track)` the
current API does not expose.

This is the music half of the per-table identity that `reference/SpaceCadetPinball/table_rules.h` describes;
`MusicPrefix` and `MusicTrackCount` are there for it.

## 3. Super jackpot — 1 use

Full Tilt has a second, larger jackpot at `+0x17a` where `JackpotScore` is `+0x172`.
Dragon's Keep's moat chute pays into it and its mouth sink pays it out. Added as
`SuperJackpotScore`; Space Cadet leaves it zero.

### Not a gap after all: balls in play

Skulduggery's Lava-Ball and Jail Break change the number of balls on the playfield, and
the rules first named a `BallsInPlay` field that upstream does not have. It does not need
one: `+0x192` is `MultiballCount`, which is how Space Cadet's own multiball already
counts. The rules were rewritten against it rather than the engine extended.

## 3b. Balls in play — resolved

Skulduggery's Lava-Ball adds two balls and Jail Break releases whatever the brig holds, so
its controllers need to read and change the number of balls on the playfield. Upstream has
`TPinballTable::BallCount` and `ChangeBallCount()`, but `BallCount` is the balls left in
the *game*, not the balls in *play* — multiball tracks its own `MultiballCount` instead.

A port needs one of: a `BallsInPlay` accessor, or the rules rewritten against
`ChangeBallCount` plus `MultiballCount`. The second is probably right, since that is how
Space Cadet's own multiball works; it was left as the literal field name so the gap is
visible rather than papered over.

## 4. `TLightGroup::LightAt(index)` — 2 uses

The prison-window sweep walks a group by index to override one light at a time. Upstream's
`TLightGroup` exposes group-wide operations and `List` but no indexed accessor.

## 5. `TBlocker`'s field at `+0x7e` — resolved

There was never a contradiction: the two readings are of **two different classes** that
happen to put a field at the same offset.

On `TBlocker` it is an **int position index** — the sprite frame the moving part is
currently showing, with `-1` for "retracted, nothing drawn". `TBlocker::Message`'s 0x39
arm stores the truncated index straight into it (`mov DWORD PTR [ecx],edi` at
DRAGON.EXE 0x41516b) and its step callback compares it as an integer against the target
at `+0x92` (`cmp DWORD PTR [esi+0x7e],eax` at 0x414f18). `GargoyleRockAControl`'s
integer compare against `-1` at 0x42381b was right all along.

On `TSink` the same offset is a **float `TimerTime`**, which is what the fire lizard sink
handlers read (verified at 0x418682, inside `TSink::Message`'s 0x41 arm). `src/dragon_part3.cpp`
says so at its own call site.

So `TBlocker::UnknownP7e` is gone, replaced by `TBlocker::CurrentPosition`, and the whole
moving-part class behind it is section 13.


## 6. Per-table dispatch, and the order it runs in

`control::make_links` and `ClearLinks` walked `control::score_components[88]` and
`simple_components[145]` unconditionally; they now walk `CurrentTable`'s arrays.
`control::handler` called `MissionControl` unconditionally; it now calls
`CurrentTable->ModeControl`.

The order is a new field rather than a constant because it differs per table.
`DRAGON.EXE`'s 0x423060 and `PIRATES.EXE`'s 0x423080 both call the mode machine and
*then* the component's `ControlFunc`; Space Cadet's `control::handler` does the
opposite. Many bonus-table handlers suppress their own scoring while a mode is running,
which only works in that order -- reversed, they double-score silently.

## 7. `ScoreMultiplier` is a value, not an index

Already predicted below under "Not surfaced by the compiler", and linking made it
urgent rather than theoretical: Dragon's weapon award and Skulduggery's fathom track
write 2..5 into `TPinballTable::ScoreMultiplier`, and upstream's `AddScore` uses it to
index `score_multipliers[5]`. `TableRules::MultiplierIsValue` selects the reading;
the index path also gained a clamp that cannot trigger for a table staying in 0..4.

## 8. `control::UnstuckBall`'s plunger

It dereferenced the `plunger` tag, which is Space Cadet's group name and null on both
bonus tables -- Skulduggery died there as soon as a ball sat still for twenty frames.
It goes through `TPinballTable::Plunger` instead, which every table has because the
loader builds it from the DAT's own plunger object.

## 9. Per-table string tables

`pb::get_rc_string` forwarded straight to `translations::GetTranslation`, whose `Msg`
enum is Space Cadet's string table embedded from `pinball.exe` and nothing else.
Dragon's Keep and Skulduggery name their messages by the resource ids of their *own*
executables -- 1001..3076 in both -- so every one of them missed, and
`translations.cpp` answered its `"!Missing MsgId!"` placeholder. That string is what
the playfield drew for every message either bonus table displayed; it is the whole of
the "the mission text does not render as it should" report.

`TableRules` now carries the table's own `RcString` table (`{id, text}`, sorted) and
`pb::get_rc_string` consults it before falling through to `translations.cpp`. Space
Cadet's three registry rows carry none, so its ids resolve exactly as before and stay
localisable; the bonus tables have no translations to go through, their text only ever
existed in English.

The data is **embedded**, generated into `table_strings.cpp` by
`analysis/gen_table_strings.py` from the dumps `analysis/dump_strings.py` already
produced. Reading it at runtime was considered and is not actually available: the
strings live in `DRAGON.EXE` and `PIRATES.EXE`, and the port never opens those -- it
stages only the `.DAT`, which has no `RT_STRING` resource. Embedding is also what the
port already does for Space Cadet.

The embedded strings keep their embedded newlines, which is what makes `"Fire
Ridge\n%ld"` render as a label above its value, and they still carry the `%ld` that
`FormatWithValue` fills.

## Not surfaced by the compiler

These produced no errors because no rule named them, but the table will not load or play
correctly without them. From `simonsteiner/SpaceCadetPinball` PR #67, which solved the
loader side independently, plus this project's own component analysis. The first three
and the multiplier are now done; the rest stand.

- **Seven object types the loader skipped**: 1034 launcher, 1035 ramp exits, 1036
  accelerators, 1037, 1038 light sweeps, 1042 magnets, 1043 diverters. Dragon's Keep uses
  1035/1036/1038/1043; Skulduggery adds 1034/1037/1042. *Done* -- the classes are in the
  patch, the magnets and diverters as approximations.
- **`loader::sound_list` holds 64**; `PIRATES.DAT` has 67 sound groups. *Done.*
- **Kicker attribute 410** was unknown to `loader::kicker`, so `query_visual` returned
  early and `TKickout` dereferenced a null `FloatArr`. 18 kickers in `DRAGON.DAT` use it.
  *Done.*
- **`ScoreMultiplier` is an index upstream and a value in Full Tilt.** *Done* -- see
  section 7.
- **A negative timeout means *cancel*** in Full Tilt. Settled on `TBlockerEnable`: the
  original's jump table at 0x415398 sends 0x38 to 0x415038, which compares the value
  against the 0.0 at 0x437330 and only arms the motion timer for a strictly positive one,
  dispatching 0x3a to itself otherwise. The port's `TBlockerEnable` already guards on
  `value > 0`. `TBlockerRestartTimeout` still clamps with `std::max(value, 0.0f)`, which
  section 13 confirms against 0x45's own `fcom`/`fxch` pair -- and the clamp *is*
  reachable: `MoatBlockerControl` sends -1.0f meaning cancel, which the original fires
  immediately instead. Harmless there; see section 13.
- **Text-box tokens.** *Done* -- see section 10.

## 10. The symbol font

Several displays on both bonus tables pass one-character strings (`"!"`, `"#"`, `"##"`,
`"K"`, `"KKKK"`, `"333"`) with a fourth `Display` argument of 1. They are glyph tokens,
not text, and the port drew them as ASCII: literal hashes and K's on the playfield, which
is what "the mission text looks wrong" was partly about.

The fourth argument selects a font, and the font it selects is **in the table's own DAT,
under the group name `font2`**. The evidence is not the name:

| DAT | `pbmsg_ft` | `font1` | `font2` |
|---|---|---|---|
| `CADET.DAT` | 96 | 10 | **absent** |
| `DRAGON.DAT` | 96 | 10 | **20** |
| `PIRATES.DAT` | 96 | 10 | **52** |

`font2` exists on exactly the two tables that use glyph tokens and on neither Space Cadet
table. The counts are the first confirmation: loaded the way `load_msg_font` already loads
a font -- a run of bitmaps starting at the space -- 20 glyphs covers `' '`..`'3'` and 52
covers `' '`..`'S'`, and every token character the rules were reconstructed with falls
inside its table's range, `'S'` (83) being the highest Skulduggery uses.

The geometry is the second, and it is the decisive one. The glyphs are not letters; they
are box-sized artwork, and every reconstructed run fits its box exactly:

| run | glyph | widest use | box |
|---|---|---|---|
| Dragon `EggGlyphs` | `'#'` 33x153 | 8 x 33 = 264 | `info_text_box` 288x155 |
| Dragon `GlyphDragonDeath` | 213x153 | 213 | 288x155 |
| Skulduggery `StormGlyphs` | `'K'` 35x133 | 8 x 35 = 280 | `info_text_box` 320x140 |
| Skulduggery `SkillShotGlyphs` | 42x133 | 7 x 42 = 294 | 320x140 |
| Skulduggery `TavernWindowGlyphs` | 97x133 | 3 x 97 = 291 | 320x140 |
| Skulduggery `PegLegGlyphs` | 260x133 | 260 | 320x140 |

A counter bar of eight `K`s is 280 wide in a 320-wide box. That is not a coincidence that
survives a wrong hypothesis, and it cross-checks the token tables themselves: they were
read out of `.rdata` independently, months before this font was found, and they land on
exactly the glyphs that fit.

What changed: `score::load_msg_font` was split so the same loader can serve a second font
(`score::load_font`), `score::glyph_fontp` holds it, `pb::init` loads `"font2"` next to
`"pbmsg_ft"`, `TTextBoxMessage` carries a `Glyph` flag, and `TTextBox::DisplayGlyph`
queues a message with it set. `TTextBox::Draw` and `LayoutTextLine` take the font per
*message* rather than per box, because the same box alternates between announcements and
glyph runs.

Space Cadet is untouched by construction: its DAT has no `font2`, `glyph_fontp` stays
null, and `FontFor` falls back to the text font -- which is also the behaviour if a
future DAT has the group but no bitmaps in it, guarded on `Height > 0` so the layout
loop cannot spin.

## 11. TRamp and THole, measured against the originals

Two divergences found by reading `DRAGON.EXE`'s own `TRamp::Collision` (0x4177d0) and
`THole::FieldEffect` / `THole::Message` (0x416ab0 / 0x416c70) next to the port's.

### `TRamp::Collision` left the ball colliding

The original sets `CollisionDisabledFlag` on the ball in all three of its layer-changing
branches -- 0x417917 (plane), 0x417979 (wall 1), 0x4179b8 (wall 2) -- and the port set it
in none of them. The offset is pinned, not guessed: `TBall::Disable` at 0x402c00 writes
`[ball+0x5] = 0` and `[ball+0x9b] = 1`, which is `ActiveFlag` and `CollisionDisabledFlag`
in that order. `pb.cpp` clears the flag at the top of each physics pass, so setting it
ends that ball's edge scanning for the frame; `THole`, `TRampExit`, `TKickout`, `TSink`
and `TAccelerator` all already did it and `TRamp` was the one that did not. Without it a
ball crossing onto a ramp can still hit an edge of the layer it just left, in the same
frame.

### The ramp's plane branch announced nothing

The original's plane branch guards on the ball not already being on the ramp's layer
(0x41790a) and, on the layer change, raises **0x54**. The port wrote the mask
unconditionally and raised nothing, so `MessageCode::ControlRampPlaneCrossed = 84` is new
and the write moved inside the guard -- the same value either way, since the guard only
passes when they differ.

That code has consumers, so this is not only fidelity: Dragon's `ft::RampEntered` arm and
Skulduggery's `a_ramp8`, which releases `a_blocg` indefinitely on it, were both dead.

### `THole` released onto an occupied mouth

The original does not release in `FieldEffect`. It stops the ball there (0x416b79 zeroes
the ball's `Direction`) and sends **itself** 0x3f8, whose arm at 0x416cea tests whether
the mouth is clear and *re-arms its own timer* (0x416d8d) instead of releasing when it is
not. The box is the ball's position plus or minus `2 x CollisionCompOffset` -- the same
extent and the same `[table+0x152]` that `TSink::TimerExpired` uses at 0x418509 -- and the
count at 0x4130c0 filters on `CollisionMask` as well as `ActiveFlag`, which is what stops
the held ball counting itself: it still carries the hole's capture mask until the release
assigns the new one.

Nothing outside the hole sends 0x3f8 -- checked across the decompiled corpus, where every
apparent hit is the float constant `0x3f800000` -- so the port keeps the release inside
`FieldEffect` and simply retests on the next frame. That is the same loop without the
extra timer: `Direction.Z` has just been zeroed, gravity takes z back below `ZSetValue`,
and the branch runs again. `TPinballTable::BallCountInRect` gained the mask-filtered
overload for it.

The gate only ever matters during multiball, and both bonus tables have multiball. Space
Cadet reaches the same code, and its three holes still release: regression-tested with
`FT_HOLETEST=0..2`, `s_hole118` 0x1 -> 0x4, `s_hole101` and `s_hole100` 0x2 -> 0x1.

### Not a gap: `timer::kill(TimerExpired)` kills *every* sink's timer

`TSink::Message`'s `PlayerChanged` and `Reset` arms kill by *callback*, and
`timer::kill(void(*)(int,void*))` walks the whole active list and frees every timer with
that function -- so one sink's reset cancels the pending eject of all of them. That looks
like a bug and is not: `DRAGON.EXE`'s own `TSink::Message` does the same, pushing the
callback 0x418500 into 0x41f960 at 0x41869f. The 0x41 arm, by contrast, kills nothing
before arming, so repeated `TSinkResetTimer` messages stack timers in the original too,
and the port matches. Both were checked because two separate passes flagged them.

## 12. The two things that made the ball come back wrong

Both found by enumerating every writer of the ball's z (`ball+0xa7`) and every assignment
of its `CollisionMask` (`ball+0xf7`) in `DRAGON.EXE`, and comparing that list against the
port's. Both are in the engine, not the rules, and both are user-visible.

### `TSink` never put the ejected ball on the sink's layer

A sink's eject is `TPinballTable::AddBall` -- it builds a *new* ball -- and the original
then tells that ball which layer it belongs on and how high it sits:

```
4185c1:  call 0x402c20                      ; AddBall(BallPosition.X, BallPosition.Y)
4185ec:  mov  DWORD PTR [edi+0x9b],0x1      ; CollisionDisabledFlag
4185f6:  mov  DWORD PTR [edi+0xf7],eax      ; CollisionMask  = sink+0x76
4185fc:  fld  DWORD PTR [esi+0x7a]          ; Position.Z     = sink+0x7a + Radius
4185ff:  fadd DWORD PTR [edi+0x15b]
418605:  fstp DWORD PTR [edi+0xa7]
```

and the constructor fills those two fields from the DAT:

```
418405:  push 0x515                         ; attribute 1301
418425:  call query_float_attribute
41842e:  mov  eax,[esp+0x44]                ;   absent -> visual.CollisionGroup
418432:  mov  [esi+0x76],eax
418455:  mov  [esi+0x76],ebx                ;   present -> 1 << (int)floor(arr[0])
418458:  push 0x259                         ; attribute 601
41846e:  mov  [esi+0x82],ecx                ;   [0] -> BallPosition.X
418478:  mov  [esi+0x86],edx                ;   [1] -> BallPosition.Y
41847e:  mov  eax,[eax+0xc]                 ;   [3] -> the return height
418481:  mov  [esi+0x7a],eax
```

The port read 601's first two elements as the position and nothing else, and assigned
neither field on the new ball. So every sink on every table handed the ball back on the
base playfield layer at playfield height, whatever layer it was supposed to serve.

On Dragon's Keep that is what made `a_ramp5`, the topmost ramp, look broken. Only three
objects in the whole table carry collision group 0x10 -- the ramp, `a_sink16` and one
wall -- and the ramp's own crossable edge goes *down*, so the sink is its only entrance.
Measured before: the ball leaves `a_sink16`, appears on mask 0x1 at z 0.370 instead of
0x10 at 2.353, falls free and drains 1.6 s later, never touching the ramp. Measured after:

```
t=3.48  EJECT a_sink16
t=3.61  pos=(-6.594,-13.269) mask=0x10
t=4.28  pos=(-6.974, -2.832) mask=0x10     <- riding the ramp, 10.4 units of it
t=4.41  pos=(-7.115,  0.252) mask=0x8      <- and off the far end
```

Seven of Dragon's eleven sinks are the only way onto the layer they serve, so this was
not one ramp.

### `THole` read the wrong element of attribute 408

```
4168f6:  push 0x198                         ; attribute 408
416918:  call query_float_attribute
416929:  fld  DWORD PTR [edx+0x152]         ; table->CollisionCompOffset
41692f:  fadd DWORD PTR [eax+0xc]           ;   + arr[3]
416937:  fstp DWORD PTR [esi+0x8e]          ; -> ZSetValue
```

The port had `ZSetValue = zArr[2]` and added nothing. The error is not uniform, which is
why it showed up as "the ball renders oddly in the tunnels" rather than as a fixed offset.
Measured, before and after, from the harness's own `DBG-holerel` line:

| hole | released onto | port, before | after |
|---|---|---|---|
| `s_hole100` | 0x1 | 0.484 | **0.370** |
| `s_hole101` | 0x1 | 0.486 | **0.370** |
| `s_hole118` | 0x4 | 0.482 | **2.876** |

The two that return the ball to the base playfield now put it at 0.370, which is exactly
where a freshly served ball sits -- they were holding it 0.114 high, and 0.114 is the
amount by which a ball was measured riding above the playfield all the way from the hole
to the drain. `s_hole118` is the other direction and much worse: it feeds layer 0x4, a
tunnel, and the port was dropping the ball 2.394 *below* where it belongs, so for 2.29 s
it was drawn at playfield height instead of inside the tunnel.

`TBall` and `TKickout` already used element 3; `THole` was the only one that did not.

## 13. `TBlocker` is two different classes

The port's `TBlocker` was 3D Pinball's, and both bonus tables needed Full Tilt's. That is
the whole of "the moving parts on the lower playfield do not move".

Upstream's `TBlocker` -- decompiled from `pinball.exe` -- is a two-state prop:
`TBlockerEnable` sets `ActiveFlag`, shows sprite 0, arms one timer and raises
`ControlTimerExpired`; `TBlockerDisable` clears all of that. The Full Tilt engine's
`TBlocker` is a **multi-position moving part**: a position index, one collision-edge set
per position with its own active flag, a step timer that walks one position per tick, and
a *second* timer that is the real `TBlockerRestartTimeout`.

The two are not a build difference in the table data. The class is byte-identical at the
same addresses in all three Full Tilt executables -- `CADET.EXE`, `DRAGON.EXE` and
`PIRATES.EXE` all carry the constructor at 0x414d60, the timeout callback at 0x414ee0,
the step callback at 0x414f00 and `Message` at 0x414f80, differing only in relocations
(checked byte for byte over 0x414ee0..0x4153b0). So the divergence is between `pinball.exe`
and the 1996 Full Tilt build, and the port inherited the former.

### The state machine, arm by arm

`Message` is 0x414f80. It jump-tables 0x37..0x3c at 0x415398 and then tests 0x45, 0x3f3,
0x3fe and 0x402 by hand at 0x414f8d..0x414fce.

| original | arm | what it does |
|---|---|---|
| 0x37 | 0x414fd3 | retract: aim `+0x92` at -1 and arm the step timer after `value` s; for `value <= 0` dispatch 0x3a(-1.0) to itself instead. Plays the sound at `+0xb2`. |
| 0x38 | 0x415038 | the mirror, aiming at the last position (`+0x3a`'s count - 1). Plays `+0xae`. |
| 0x39 | 0x4150ce | **set position now**: truncate the float, clear the old position's flag byte, store the new index, set its flag, show its bitmap/zmap. |
| 0x3a | 0x4151fb | **aim**: `+0x92 = (int)value`, then arm the step timer at `+0x96` seconds. |
| 0x3b | 0x415245 | snap fully open: 0x39(last) plus the `+0xae` sound. |
| 0x3c | 0x415284 | snap fully closed: 0x39(-1.0) plus the `+0xb2` sound. |
| 0x45 | 0x4152a5 | the real `TBlockerRestartTimeout`: kill `+0x8e`, re-arm it with `max(value, 0.0)` and callback 0x414ee0, which raises 0x46 = `ControlTimerExpired`. |
| 0x3f3 | 0x415340 | `SetTiltLock` -- see below. Drop both timers, go to the home position. |
| 0x3fe | 0x4152e9 | `PlayerChanged`: the base's player-field swap (0x413570), then the same tail. |
| 0x402 | 0x415339 | `Reset`: `TPinballComponent::Reset` (0x413550), then the same tail. |

0x3f3 is `SetTiltLock` and not, as the offsets invite, `TBlocker`'s own object type 1011.
The sender settles it: `TPinballTable::Message` at 0x419765 broadcasts 0x3f3 to every
component in the list and then sends 8 (`TLightTurnOffTimed`) to the table's light group,
which is upstream's `TPinballTable::tilt()` line for line.

Two timers, not one, and this is the part that matters for the rules:

* `+0x8e` is armed only by 0x45 and raises `ControlTimerExpired` (0x46).
* `+0x9a` is the step timer, armed by 0x37/0x38's delay and by every 0x3a, with callback
  0x414f00. That callback zeroes itself, and then: if `+0x7e == +0x92` it raises **0x3d**
  at the rules and stops; otherwise it steps `+0x7e` one towards `+0x92` through a virtual
  `Message(0x39, ...)` and re-arms unconditionally.

So `TBlockerEnable` on a Full Tilt blocker never raises `ControlTimerExpired`. That is why
this is gated rather than replaced -- see below.

### Fields and where they come from

The constructor is 0x414d60, found by the only store of its vtable 0x437560 at 0x414d80.
It calls the base with `createWall = false` (0x414d75) and builds the walls itself.

| offset | field | source |
|---|---|---|
| `+0x76`..`+0x7d` | `PositionFlags[8]` | one per position, each handed to `install_wall` as that position's `ActiveFlagPtr` (0x414dcb..0x414df7, attribute **600** of position *i*'s state record) |
| `+0x7e` | `CurrentPosition` | int, -1 parked |
| `+0x82` | `HomePosition` | attribute **409**, default -1 (0x414e7c) |
| `+0x86` / `+0x8a` | `InitialDuration` / `ExtendedDuration` | attribute **407**'s first two floats cast to int, default **55 / 5** (0x414e07) |
| `+0x8e` | `Timer` | the timeout timer |
| `+0x92` | `TargetPosition` | int |
| `+0x96` | `StepTime` | attribute **903**, default **0.1** (0x414e57) |
| `+0x9a` | `MotionTimer` | the step timer |
| `+0x9e`..`+0xad` | the retry record | `{timer, code, value, component}`; 0x4131a0 arms it, 0x4131e0 re-sends |
| `+0xae` / `+0xb2` | `SoundIndex4` / `SoundIndex3` | `visualStruct`, as before |

The eight-position cap is the original's own: 0x414da1 asserts when
`loader::query_visual_states(groupIndex) > 8` (assert line 0x6b3), and the flag array runs
out exactly at `+0x7e`. The port clamps instead of asserting, because an assert there would
take a table down over data the rules need not reach.

Attributes 407 and 903 are **absent from both bonus tables' DATs** -- measured, not
assumed: a `DBG-block ctor` line per blocker over `DRAGON.DAT` and `PIRATES.DAT` reports
`dur=55/5 step=0.100` for all twelve. So upstream's hardcoded 55 and 5 were right and stay
as the fallback. Attribute 409 is real and differs per part:

```
DRAGON.DAT   a_sword states=3 home=2   a_bloce 1 home=-1  a_blocf 1 home=0
             a_jaw   states=8 home=0   a_center_post 1 home=-1
             a_rocka states=5 home=4   a_rockb states=5 home=4
PIRATES.DAT  a_cannon states=4 home=0  a_blocc 1 home=-1  a_bloce 2 home=1
             a_blocf 1 home=0          a_blocg 1 home=-1
```

`a_jaw` (8 positions) and `a_cannon` (4) have no attribute 600 on any position: they are
pure animated props. The rest install one wall set per position -- 55, 57 and 60 edges on
`a_rocka`, `a_sword` and `a_rockb`.

### A part never moves into a ball

Both the 0x39 arm (0x41510e) and the step callback (0x414f35) count the balls inside the
part's own `AABB` -- `TPinballTable::BallCountInRect`, the engine's 0x413010, reached
through the thunk 0x413990 which simply does `ecx += 0x4a` -- and refuse to extend while
one is there. The step callback re-arms anyway, so the part resumes by itself; a direct
0x39 defers the *same code and value* by 0.5 s through the retry record and tries again.
Retracting (`index < 0`) is always allowed. `+0x4a` is `AABB`, which on this path is the
union of every position's edges because `install_wall` merges each of them into it.

### What the port does now, and what Space Cadet still does

`TableRules::MovingBlockers` selects the class; it is true for `DRAGON.DAT` and
`PIRATES.DAT` and false for all three Space Cadet rows. The Space Cadet path is the old
body **verbatim**, including the `value > 0` guard and the `std::max(value, 0.0f)` clamp,
and the base constructor still gets `createWall = true` for it, so one wall keyed to
`ActiveFlag` and nothing else. `control::DrainBallBlockerControl` depends on
`TBlockerEnable`'s own timer raising `ControlTimerExpired`, which the moving class does not
do, so this is a gate and not a replacement.

A consequence worth naming: the moving class **never writes `ActiveFlag` or
`MessageField`**. Nothing in 0x414f80 touches `+0x5`, and only the 0x402 arm's call to
`TPinballComponent::Reset` touches `+0x6`. That is load-bearing, not an omission: Dragon's
`CenterPostControl`, `RightOutlaneBlockerControl` and `MoatBlockerControl` park their
armour stage (1, 2, 0) in the blocker's `MessageField` and then send `TBlockerDisable`,
which under upstream's class zeroed it out from under them.

### Message codes

`MessageCode` gained five entries. 57 and 58 keep the originals' 0x39 and 0x3a because
`src/pirates_*.cpp` and `src/dragon_missions.cpp` already cast those raw numbers; they
collide with `ControlBallCaptured` / `ControlBallReleased`, which is harmless because
those two only ever travel to `control::handler`. 61 likewise keeps 0x3d, colliding with
`ControlNotifyTimerExpired` -- a collision `src/dragon_part2.cpp` documents and switches
on deliberately.

| name | value | original |
|---|---|---|
| `TBlockerSetPosition` | 57 | 0x39 |
| `TBlockerMoveToPosition` | 58 | 0x3a |
| `TBlockerSnapOpen` | **70** | 0x3b |
| `TBlockerSnapClosed` | **71** | 0x3c |
| `ControlBlockerMotionDone` | 61 | 0x3d |

0x3b and 0x3c are **renumbered**, the only two that are. The originals' 0x3b is numerically
59, which is already `TBlockerRestartTimeout` here -- upstream's own value for a different
message, and the one both Space Cadet's `control.cpp` and Dragon's three armour handlers
send. Nothing in either table's rules sends 0x3b or 0x3c (checked across `src/`), so no
call site depends on their original numbers.

### Still open

* `src/dragon_common.cpp`'s `BlockerUnknownP7e` stands in for the position index with
  `blocker->ActiveFlag ? 0 : -1`. Under the moving class `ActiveFlag` is always 1, so the
  stand-in now always answers "extended" and the two gargoyle rock handlers' `!= -1` guard
  is dead in the other direction. It wants to be
  `static_cast<TBlocker*>(blocker)->CurrentPosition`, which is the real field. Owned by
  whoever owns `src/`.
* `src/dragon_part2.cpp` claims Full Tilt treats a negative `TBlockerRestartTimeout` as
  *cancel*. The binary disagrees: 0x45's `fcom`/`fxch` pair at 0x4152b5..0x4152d9 computes
  `max(value, 0.0)` and arms the timer either way, so `MoatBlockerControl`'s
  `TBlockerRestartTimeout(-1.0f)` fires `ControlTimerExpired` on the next frame rather
  than cancelling. It is harmless there -- the handler has already set `MessageField = 0`,
  so the expiry takes the idle branch and does not re-arm -- but the comment is wrong and
  the engine now matches the binary rather than the comment.
* The port's `SpriteSet(index)` returns without clearing when `index` is past the end of
  `ListBitmap`; the original blanks the sprite (0x4151db). `TBlocker` compensates by
  calling `SpriteSet(-1)` itself in that case, so the difference is contained here.
* `// UNVERIFIED:` nothing in `TBlocker` is guessed, but two things are untested rather
  than unverified: no run has yet driven a ball into a multi-position part while it was
  moving, so the ball-in-the-box deferral and the 0.5 s retry are read off the listing and
  not observed; and `TBlockerSnapOpen` / `TBlockerSnapClosed` have no caller at all, so
  their arms are dead code until a rule sends them. Driving `FT_HOLETEST`/`FT_SINKTEST`
  into `a_rocka`'s box while `a_rocka` steps would settle the first.

Measured after the change: `test/run_tables.sh` green on all three tables, `FT_HOLETEST=0..2`
on Dragon still releasing `s_hole118`, `s_hole101` and `s_hole100`, and a 90 s `FT_AUTOPLAY`
soak on each bonus table showing the parts actually stepping -- `a_cannon` walking 0 -> 1 ->
2 -> 3 and back, `a_bloce` 0 <-> 1, `a_rocka`/`a_rockb`/`a_sword`/`a_jaw` reaching their home
positions -- with no stuck ball and no crash.

## 13. The spliced-bitmap decoder wrote past its buffer

This one is upstream's, not the port's, and it aborted a real game of Dragon's Keep:

```
DBG-hoard sweeps=... rockb=... lite46=...
munmap_chunk(): invalid pointer
# exit 134
```

`exit 134` is SIGABRT from glibc's own heap checker. The core dump (SIGABRT, PID 803105)
puts the abort in `pb::uninit` -> `DatFile::~DatFile` -> `EntryData::~EntryData` ->
`gdrv_bitmap8::~gdrv_bitmap8`, on the `delete[] BmpBufPtr1` of a 109x37 bitmap: the chunk
header four bytes below the buffer had been overwritten with `0x00000000b426b22b`, whose
`IS_MMAPPED` bit then sent `free` down the `munmap_chunk` path. The overwriting bytes read
as consecutive 16-bit z depths, which names the culprit before any disassembly: something
was writing a zmap through an index that had run off the end.

**Where.** `GroupData::SplitSplicedBitmap`. A spliced bitmap encodes runs of pixels
separated by gaps, and a gap is an **absolute advance through a framebuffer whose stride is
the table width** -- 600, 752 or 960 by resolution -- while the destination is a small
sprite of `bmp.Stride`. Upstream compensates for the difference once per gap:

```cpp
if (stride > bmp.Width)
    stride += bmp.Width - tableWidth;
dstInd += stride;
```

which is right only when the gap crosses exactly **one** row boundary. A sprite with blank
rows in the middle emits a gap that crosses several, and the single compensation then
leaves the index short by `(rows - 1) * (tableWidth - bmp.Width)`. Dragon's `v_lite119`
(group 362, a 21x12 sprite) emits a gap of 2414 = 4 x 600 + 14: four rows, compensated as
one, so the index landed at 1922 in a 252-byte bitmap and wrote `zMap.ZPtr1[1922]` 3,844
bytes past a 504-byte buffer.

The fix tracks the table-space position and recomputes the destination index per run:

```cpp
tablePos += stride;
auto dstInd = tablePos / tableWidth * bmp.Stride + tablePos % tableWidth;
```

**Why this is safe to change in a shared decoder.** Every spliced bitmap in all three DATs
was decoded both ways and hashed, pixels and z separately: `DRAGON.DAT` 684 spliced
bitmaps, **3 differ** -- exactly the three resolutions of `v_lite119`, the one that
overflowed; `PIRATES.DAT` 771, **0 differ**; `CADET.DAT` 897, **0 differ**. So Space Cadet
and Skulduggery are bit-for-bit what they were, and only the broken sprite changes. An
AddressSanitizer build reports the overflow at `GroupData.cpp:134` before the fix and is
clean after, across autoplay soaks on all three tables.

Worth knowing for anyone chasing this again: **it does not reproduce headlessly**. The
dummy renderer lays the heap out differently and the stray write lands in padding, so
`MALLOC_CHECK_=3` sees nothing. ASAN is the deterministic test; the abort in a real window
was luck.
