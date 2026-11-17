# Engine changes the port needs

**Done.** All of `src/` compiles and links against the patched engine, the engine still
builds, and all three tables run (`test/run_tables.sh`).

```sh
nix-build build.nix                       # engine, patched, with the rules linked in
test/run_tables.sh                        # Space Cadet, Dragon's Keep, Skulduggery
```

The patch is `git -C reference diff cb9b7b8`: `reference/` is a submodule on the
`fulltilt` branch of our fork of upstream, and every commit after `cb9b7b8` is ours. Space Cadet's registry rows carry
`ModeBeforeComponent = false` and `MultiplierIsValue = false`, which reduce
`control::handler` and `TPinballTable::AddScore` to exactly what upstream did, and leave
`TableControl` and `KeyHandler` null, which keeps its table-level handler and its cheats
upstream's. Two qualifications. Sections 14 and 15 are corrections to upstream code on
every table. And the corrections from section 16 on that are gated on Full Tilt data
(`pb::FullTiltMode`, i.e. `TableRules::FullTiltData`) apply to `CADET.DAT` as well as to
the bonus tables, because `CADET.EXE` is the same engine and does the same thing; only
`PINBALL.DAT` (3D Pinball) is guaranteed upstream's behaviour by those gates. One rendering
change is not gated at all: `zdrv::paint` skips source pixels whose z is the 0xFFFF
"no pixel" marker on every table, which is probably invisible on Space Cadet but has not
been checked pixel for pixel.

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
loader builds it from the DAT's own plunger object. The flippers it also has to protect
are a per-table list since section 21.

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
  patch. `TRampExit` (ctor 0x41b4f0, Collision 0x41b6b0), `TAccelerator` (0x41b7d0,
  0x41b8f0/0x41b9f0) and `TMagnet` (0x416560..0x4166c0) have since been compared against
  their listings and match. `TDiverter` builds its walls from attribute 604 since section
  20; its back-face sprite kick (timer callback 0x41bc50) is still not reproduced.
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

**One thing this first pass missed.** The rules reach the symbol font through the named
token arrays -- `GlyphSpell`, `GlyphWeapon`, `GlyphArmor`, `GlyphJackpot`,
`GlyphDragonDeath`, `EggGlyphs` -- and all of those were rewired. Four call sites in
Dragon predate those arrays and pass the token as a bare literal: the three sword targets
(`src/dragon_part1.cpp`, handlers 0x425750 / 0x4257c0 / 0x425830) and the Slay Dragon tail
(`src/dragon_part3.cpp`). They still went through `Display`, so glyph 1 -- a 220x153
bitmap of the sword -- came out as a literal `!` on the playfield, which is exactly what a
player reported seeing on some hits. The comment at the first of those sites had said the
original "passes a fourth Display argument of 1 here where resource strings pass 0, which
is presumably what marks a string as such a token"; it did, and that argument is this
font selector.

**And a second.** Skulduggery's own `DisplayGlyph` wrapper (`src/pirates_common.cpp`)
still called the text `Display`, and four of its lane handlers (`src/pirates_part1.cpp`:
`a_roll3`, `a_roll5`, `a_roll1`, `a_roll2`) called `info_text_box->Display` directly,
although every one of those sites in `PIRATES.EXE` pushes the fourth argument 1 (a_roll3
at 0x42975e: `push 0x1; push 0x40000000; push 0x43aad0`). They now go through
`TTextBox::DisplayGlyph`. Which artwork font2 holds at the slots for '3'..'9' is still
unverified.

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
those two only ever travel to `control::handler`.

| name | value | original |
|---|---|---|
| `TBlockerSetPosition` | 57 | 0x39 |
| `TBlockerMoveToPosition` | 58 | 0x3a |
| `TBlockerSnapOpen` | **70** | 0x3b |
| `TBlockerSnapClosed` | **71** | 0x3c |
| `ControlBlockerMotionDone` | **87** | 0x3d |

`ControlBlockerMotionDone` first kept the original's 0x3d = 61, and an earlier revision of
this table said so. It is 87 in the code now (`TPinballComponent.h`): Full Tilt's 0x3d and
its 0x47 are different codes, the port already spends 61 on the second
(`ControlNotifyTimerExpired`, raised by `TLightGroup`), and once the rules named both by
symbol rather than raw cast a switch had two identical labels and would not compile.
Nothing outside the port reads these numbers, so the port's one moved. The rules use the
symbol (`src/dragon_part2.cpp`'s `BlockerMotionDone`, `src/dragon_missions.cpp`'s
`JawOpened`).

0x3b and 0x3c are **renumbered** as well. The originals' 0x3b is numerically
59, which is already `TBlockerRestartTimeout` here -- upstream's own value for a different
message, and the one both Space Cadet's `control.cpp` and Dragon's three armour handlers
send. Nothing in either table's rules sends 0x3b or 0x3c (checked across `src/`), so no
call site depends on their original numbers.

### Still open

* ~~`src/dragon_common.cpp`'s `BlockerUnknownP7e` stands in for the position index with
  `blocker->ActiveFlag ? 0 : -1`~~ -- **closed**. It now returns the blocker's
  `CurrentPosition` (-1 when the component is not a `TBlocker`), so the two gargoyle rock
  handlers' `!= -1` guard reads the real field.
* ~~`src/dragon_part2.cpp` claims Full Tilt treats a negative `TBlockerRestartTimeout` as
  *cancel*~~ -- **closed**; the comment now says what the binary does. 0x45's
  `fcom`/`fxch` pair at 0x4152b5..0x4152d9 computes `max(value, 0.0)` and arms the timer
  either way, so `MoatBlockerControl`'s `TBlockerRestartTimeout(-1.0f)` fires
  `ControlTimerExpired` on the next frame rather than cancelling. It is harmless there --
  the handler has already set `MessageField = 0`, so the expiry takes the idle branch and
  does not re-arm. On a `TTimer` the same code with a negative value does cancel
  (0x41b3d9 returns without arming), and the comment now distinguishes the two.
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

## 14. The spliced-bitmap decoder wrote past its buffer

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

## 15. `TKickback` replayed its clip ten times a second

Reported twice by a player on Dragon's Keep, the second time precisely: entering the left
drain channel, the ball is rescued, and the audio becomes deafening.

Upstream collapsed the original's **two** timer callbacks into one `TimerExpired` that
branches on `KickActiveFlag`, and in the armed branch played `HardHitSoundId` and re-armed
*itself* every `TimerTime2` = 0.1 s. That flag is cleared only by a collision that clears
the threshold, so a ball that touches the kickback and then leaves without one -- or is
disabled, as a drained ball is -- leaves it armed for the rest of the game. Measured
before the fix: `10 starts in the last 1000 ms (total 141)`, still climbing at t=17.8 s.
After: the flag clears at t=3.88 and the counter never fires again.

The original is a one-shot, two-stage animation, and the class is one shared engine class
-- the bytes at 0x415ba0 are identical in `CADET.EXE`, `DRAGON.EXE` and `PIRATES.EXE`, at
the same address and the same vtable slot -- so this is a correction to upstream, not a
Full Tilt variant:

| | |
|---|---|
| `Collision`, first touch | `KickActiveFlag = 1`, arm `TimerTime` -> **0x415c60** |
| **0x415c60** arm | `Threshold = 0`, sprite 1, arm `TimerTime2` -> 0x415ce0. No sound, no branch, no self re-arm. |
| `Collision`, now over the zero threshold | play the clip, clear the flag, kill the pending timer, arm `TimerTime2` -> 0x415ce0 |
| **0x415ce0** retract | sprite 0, `Threshold` back to 1e9, notify the rules **only if the flag is already clear**, then clear it |

The conditional notify is the point of the second stage: arming and timing out quietly is
not an event, only an actual kick is.

Two things came out of fixing it.

**The original plays two clips on a kick, not one.** `TCollisionComponent::DefaultCollision`
is 0x413ab0; the branch that returns 1 plays `[this+0x72]` at 0x413b2b and the softer
branch plays `[this+0x6e]` at 0x413b58, which pins those two fields as `HardHitSoundId` and
`SoftHitSoundId`. `TKickback::Collision` then plays `[this+0x72]` *again* at 0x415c1f. Two
overlapping voices, once, which is bounded and is presumably why a kicker sounds louder
than a wall.

**`TKickback` has a suppressor the port did not model.** `[this+0x86]`, tested at 0x415bd7
*before* `KickActiveFlag`: set by message 0x4c (0x415da7) and cleared by 0x4b, a tilt lock
or a reset (0x415dd7). A suppressed kickback still stops the ball -- the collision below
the guard still runs -- it simply never arms and never kicks. Skulduggery's rules send 0x4c
(`src/pirates_part3.cpp`), where it was a no-op. `Message` also retracted with
`SpriteSet(-1)` where the original uses sprite 0 (0x415e0f reads the bitmap list's entry
0, not a hide).

## 16. `TableRules::TableControl` -- each table's own table-level handler

`TPinballTable` calls `control::table_control_handler` at the tail of its `Message`
(`TPinballTable.cpp:731`) and from `tilt()` (`:413`). Upstream's body is Space Cadet's:
on `SetTiltLock` it clears `table_unlimited_balls` and sends a message to `lite77`. On
either bonus table `lite77` is a Space Cadet tag that `make_links` never binds (it walks
only `CurrentTable`'s arrays), so it was null, and **the first tilt dereferenced it**.

The bonus tables have their own handler at the same call sites. `DRAGON.EXE`'s
TPinballTable calls 0x424260 at 0x419e87 (`push esi; call 0x424260`, the tail of
`Message`) and once from its constructor with 0x3f7 (0x41ab2f); `PIRATES.EXE` calls
0x4230c0 from the same places. Neither is bound in `score_components[]` or
`simple_components[]`, which is how both went unported while every component handler was
done.

Dragon's 0x424260 is a jump table on `code - 0x3f3` (targets at 0x42446c, index bytes at
0x424480):

| code | arm | what it does |
|---|---|---|
| 0x3f3 `SetTiltLock` | 0x424282 | zero 0x439b04; `ClearSpells` (0x423780); `a_kick1->ActiveFlag = 0`, so a tilted ball cannot be kicked back |
| 0x3f6 `NewGame` | 0x42429b | `game_over_phase = 0`; v_lite113..115 `ActiveFlag = 0`; `SetSwordStage(3)`; idle music (tabc7) unless already playing; ball-save phase 0x434d2c = 3; `v_lite40` 0x13; `[TableG+0x172]` (`JackpotScore`) = 1,000,000; `[TableG+0x17a]` (`SuperJackpotScore`) = 2,500,000; mode register = 1 |
| 0x3f7 table load | 0x424326 | loads the music handles by name (see section 23); clears v_lite113..115 `ActiveFlag` |
| 0x400 `GameOver` | 0x424433 | ball-save phase = 0; `v_lite40` 0x14; v_lite113..115 `ActiveFlag = 0` |

Skulduggery's 0x4230c0 is a compare chain with three arms and no game-over arm. 0x3f3
(0x4230e2) sets the mode register to 0x11 (Lockdown) and broadcasts 0x4f through the
dispatcher 0x42d850, which disarms the magnets and kickouts and drops the running mode
without a teardown, so a tilted mode pays no completion bonus. 0x3f6 (0x4230fb..0x4231d4)
is the per-game reset: `JackpotScore` = 1,000,000, the shoot-again phase, `s_oneway12`
off, `game_over_phase`, the collection-cycle flag 0x43abb0, the whole buried-treasure pouch
(cursor 0, slots -1, coin count 1, snapshots 0), `ClearTreasureXLights` (0x424500) and
`RestoreTreasureBoard` (0x424560), idle music (tabb1), the brig count and last scorer,
`a_kick2` and `s_bwal610` off, 0x4c to `a_kick1`/`a_kout1`/`a_kout2`, then
`EnterSkillShotAward` (0x423440: state 3 and its 0x4f) and the register zeroed by hand.
0x3f7 (0x4231d7) loads tabb1..tabb6.

What upstream did: nothing per table. What the port does now: `TableRules` gained
`void (*TableControl)(MessageCode)`. `control::table_control_handler` calls it when it is
set and returns; it runs Space Cadet's body only when the current table's
`ScoreComponents` are Space Cadet's, so a bonus table built with its rules switched off
cannot reach `lite77` either. `dragon::TableControl` (`src/dragon_part4.cpp`) and
`pirates::TableControl` (`src/pirates_modes.cpp`) port the arms above. FT 0x3f3/0x3f6
arrive as the engine's `SetTiltLock`/`NewGame` and Dragon's 0x400 as `GameOver` (the
public-code bands of section 5.1 of `docs/how-this-was-done.md`). The 0x3f7 load arms have
nothing to do in the port: the engine never sends that code to the table, the music
handles are fixed track constants in `src/`, and the `ActiveFlag` clears are repeated by
the new-game arm.

The order matters and is the original's: the call is at the end of `TPinballTable::Message`,
after the engine's own `NewGame` body has set `JackpotScore = 500000`, so the table's
1,000,000 wins. Before this, every bonus-table game started with a 500,000 jackpot and
Dragon's super jackpot started at 0 and carried over between games.

Gated by construction: the field is null on all three Space Cadet rows.

## 17. `TableRules::KeyHandler` -- table cheat codes and the bonus-count skip

The Full Tilt keydown calls the table's key handler first (0x412154, `push esi; call`),
after its game-mode gate and before the engine's own "hidden test" matcher (0x411f50)
and the flipper dispatch. Dragon's is 0x4282c0, Skulduggery's 0x42a830. Both have two
jobs.

* **The bonus fast-forward.** 0x4282c1..0x4282f2: if the mode register is 0xb (the
  end-of-ball count-up), `game_over_phase` is non-zero, and the key is one of the two
  flipper keys (compared against 0x434f74/0x434f78, the cells keydown itself tests at
  0x412183/0x4121a5), send the dispatcher message 1, which the drain-bonus controller
  answers by going quiet and running the remaining steps. Skulduggery's is the same with
  state 0x10. The port's controllers already handled code 1; nothing sent it.
* **The table's own codes**, matched one key at a time through a jump table on
  `key - 0x31` (bounds 0x27; Dragon's index bytes at 0x42854c, targets at 0x428514;
  Skulduggery's at 0x42aaa4 / 0x42aa70). Dragon: `1MAX` extra ball and `CheatsUsed`;
  `NMAX` in Idle starts the selected quest (0x424210); `PB` toggles the alternate ball
  skin (0x438140); `QUOTE` starts the quote scroll; `FMAX` is accepted all the way and does
  nothing. Skulduggery: `1MAX` through its own cheat-only extra ball 0x42a7d0; `NMAX`
  starts the selected mode (0x425a50); `GMAX` toggles the skin (0x439140); `QUOTE`; `FMAX`
  as Dragon.

What upstream did: every key went to `control::pbctrl_bdoor_controller`, Space Cadet's
backdoor, whose `1max`, `gmax`, `rmax`, `bmax` and `easy mode` go through Space Cadet tags
(`soundwave28`, `GravityWellKickoutControl`'s lights, `middle_circle`, `block1`, `gate1`)
that the bonus tables never bind. So the Cheats menu, or typing a code, **crashed either
bonus table**, as did `hidden test`'s `r` key (`cheat_bump_rank`).

What the port does now:

* `TableRules` gained `void (*KeyHandler)(int key, bool flipper)`. `pb::InputDown` calls
  it ahead of the backdoor, with SDL's lower-case letter keycodes upper-cased to the
  virtual-key codes the originals saw, and `flipper` set from the engine's bindings so a
  remapped or gamepad flipper counts; a non-keyboard flipper arrives as key 0.
* `pb::PushCheat` (the Cheats menu) types into the table's handler first, never as a
  flipper.
* On a table with a key handler the backdoor keeps only `hidden test` and the idle
  credits, which the Full Tilt engine has for every table; Space Cadet's `gmax`, `1max`,
  `bmax`, `rmax`, `quote` and `easy mode` arms, and the `r` key, are skipped. The Cheats
  menu hides the items with no counterpart and adds `nmax`, and `pb` on Dragon's Keep.
* The 60 s idle credits now feed the backdoor directly instead of going through
  `PushCheat`, so the engine's own scroll does not reach a table's matcher and reset a
  half-typed code.
* `pb::AlternateBallSkin` is the toggled cell.

Not reproduced, and stated in the code: **the skin toggle has no visible effect.** The
original's only reader is the `TBall` constructor (0x4028bb), which selects each ball
state's second bitmap; the port's `GroupData` keeps only the first (see the "bitmap
override" note in `GroupData.cpp`) and its `TBall` does not read the flag. And **the quote
scroll runs once**: the original loops in the text box's scroll slot (0x41e070) until a
key stops it; the port queues the table's own quote list as one pass of low-priority
messages, which a key clears with `TTextBox::Clear(true)` without touching anything the
rules put up.

Gated by construction: the field is null on all three Space Cadet rows, whose backdoor and
menu are unchanged.

## 18. `TTimer` answers 0x48, "time remaining"

`DRAGON.EXE`'s `TTimer::Message` (0x41b380) tests `cmp eax,0x48 / je 0x41b3fb`. That arm
returns 0 when no timer is armed; otherwise it calls 0x41f880 (walk the active list for the
id, return `(TargetTime - now) / 1000.0`), clamps a negative result to 0.0, multiplies by
the 1000.0 at 0x4377a0, floors and returns an int. The same bytes are in `PIRATES.EXE`.

Upstream's `TTimer` has no such arm -- Space Cadet never asks -- and the engine had no
"remaining" query at all, so every 0x48 answered 0. That blanked every quest's and every
mode's countdown display on both tables, never opened the 0.5 s display tick chain, cut a
Slay Dragon hit's re-arm to 15 s instead of remaining + 15 s, and killed Skulduggery's
captive-ball +10 s, whose `> 0` guard could never pass.

What the port does now: `MessageCode::TTimerGetRemaining = 72` (the original's own 0x48;
72 was free), `timer::remaining(id)` mirroring 0x41f880, and the arm in `TTimer::Message`.
While there, `TTimer` also gained Full Tilt's `PlayerChanged` arm (jump-table slot 0x3fe ->
0x41b453): the per-player swap of section 24, then the same timer kill as tilt, game over
and reset, so a clock started on one player's ball cannot expire during the next player's.

Gating: no Space Cadet code sends 72; the `PlayerChanged` arm returns early unless
`pb::FullTiltMode`.

## 19. `TBall` does not reset a ball the table placed

`CADET.EXE`'s `TBall::Message` (0x402a90) resets only when `+0x153` is 0:
`402aa0 cmp DWORD PTR [esi+0x153],ebx / 402aa6 jne 0x402afe`. `+0x153` is `HasGroupFlag`,
set on a ball built from its own DAT group -- a type-1037 object.

Upstream has no such ball and no such test. `PIRATES.DAT` has exactly one: the captive ball
at (4.65, -9.11) that `a_spot102` is hit through. `NewGame` broadcasts `Reset` to every
component, so the captive ball was deactivated, moved to the origin and given mask 1 at the
start of every game, and never came back -- which killed captive-ball mode selection, the
+10 s extension and its award.

What the port does now: `if (code == MessageCode::Reset && !HasGroupFlag)`. Space Cadet's
DATs have no type-1037 object, so the test cannot change them.

## 20. `TDiverter` reads attribute 604 when 600 is not a single line

The constructor (0x41bb70, the same bytes in all three executables) feeds its two-face
builder 0x41ba30 from one of two places:

```
41bbc1:  cmp  [esp+0x14],2        ; does 600 describe one line?
41bbc6:  jne  0x41bbeb
41bbeb:  push 0x25c               ; attribute 604
41bbfe:  fld  [edi] / fcomp 2.0   ; record tag == 2.0 (0x4377c0)?
41bc23:  call 0x41ba30            ; build the doubled face from edi+4
41bc1d:  add  edi,0x18            ; next 6-float record
```

So 604 is a list of `{2.0, x0, y0, x1, y1, z}` records ended by anything other than 2.0,
each handed to the same builder. **All eight `a_dvtr*` objects in `DRAGON.DAT` carry only
604**, with one to four segments each.

The port had read 600 only, and returned when it was not a single line, under a comment
that said "an earlier note here guessed attribute 604 and a segment list. It is 600, a
single line". So every Dragon diverter built zero edges, both chutes of every pair were
always open, and the rules' gate messages steered nothing. The note was right the first
time; the gate on `FloatArrCount == 2` it cited is the first branch, not the only one.

What the port does now: the 600 path as before, else the 604 walk, each segment through
`InstallSegment` with the original's 0.8 back-face offset. Skulduggery has no diverters;
Space Cadet's DATs have no type 1043, so nothing else reaches the class. The back-face
sprite kick (0x41bc50) is still not reproduced.

## 21. `UnstuckBall`'s bounds are a per-table list

`control::UnstuckBall` skips a ball resting inside a control's bounds -- a cradled ball on a
raised flipper, a ball on the plunger -- and otherwise jiggles it and, past 20 frames,
destroys and re-serves it. Space Cadet checks `flip1`, `flip2` and the plunger. Those
`flip1`/`flip2` tags are Space Cadet's and null on the bonus tables, and section 8's
`TableRules::UnstuckBoundsExtra` named one extra component. So on Dragon's Keep `a_flip1`
and `a_flip2`, and on Skulduggery every flipper, were unprotected: hold the ball on one for
half a second and it was shoved off, and past 20 frames re-served from the plunger.

The originals' lists, read off the calls to `CheckBallInControlBounds`:

* `DRAGON.EXE` 0x42c320: four calls, on `a_flip1`, `a_flip3`, `a_flip2` and `s_plunger1`
  (tags 0x43a714, 0x43a724, 0x43a734, 0x439d44). Three of its four flippers.
* `PIRATES.EXE` 0x42db30: seven, on `a_flip1`, `a_flip3`, `a_flip2`, `a_flip4`,
  `s_launcher1`, `s_oneway12` and `a_kick1`.

What the port does now: `UnstuckBoundsExtra` is a comma-separated list
(`"a_flip1,a_flip3,a_flip2"` and `"a_flip1,a_flip3,a_flip2,a_flip4,s_oneway12,a_kick1"`;
the plunger/launcher is `TPinballTable::Plunger` as before), resolved once in the table
constructor into `TPinballTable::UnstuckBounds` and tested after the Space Cadet clauses.
Empty on Space Cadet.

## 22. `TSink`'s eject height applies to Full Tilt data only

Section 12 made the ejected ball take `601[3] + Radius` as its height, as `DRAGON.EXE` does
at 0x4185fc. That was not gated, and `PINBALL.DAT`'s `v_sink1/2/3/7` carry **-1.0** in that
slot where `CADET.DAT` has 0.0. pinball.exe never reads it, so on 3D Pinball every wormhole
and hyperspace eject started the ball below the table (z = 0.3 - 1.0) until something
rewrote z.

What the port does now: the height is applied only under `pb::FullTiltMode`. The release
layer (`ReleaseMask`) needs no gate: `PINBALL.DAT` has neither 1301 nor 602, so it stays 1.

## 23. Table music: the Music option, pause, and each table's own first track

Two defects, both upstream integration rather than a missing original routine.

**The bonus tables' music ignored the Music option, pause and focus loss.**
`midi::PlayMusic` called `Mix_PlayMusic` unconditionally, set `IsPlaying`, and never set
`active_track`; `StopPlayback` halted only when `active_track` was set, so `music_stop()`
-- which pause, focus loss and the menu toggle all call -- never stopped a table track, and
with music off every mode change started music anyway.

Now `PlayMusic` records `ActiveTableTrack` whether or not music is audible and starts it
only when `IsPlaying`; `StopPlayback` also halts when `TableTrackAudible`; `music_play`
resumes `ActiveTableTrack`; and `play_track` clears it.

**The new-game track.** `TPinballTable::Message(NewGame)` played `Track1` on every table.
Dragon's 0x424260 new-game arm plays its idle handle, which its 0x3f7 arm loads from
`"tabc7"` (listing below), and Skulduggery's plays tabb1. With `TableControl` set, the
engine now leaves the new-game track to the table (section 16).

The same 0x3f7 arm settled Dragon's track map, which the rules had left open:

```
push 0x43bc00 ("tabc7"); call 0x40e9d0; mov ds:0x439af8,eax    ; idle
push 0x43bc08 ("tabc1")                 -> 0x439ae0            ; Fire Lizards
push 0x43bc10 ("tabc2")                 -> 0x439ae4            ; Dragon Hoard
mov  eax,ds:0x439ae0; mov ds:0x439ae8,eax                      ; Gargoyle Gaggle = tabc1
push 0x43bc18 ("tabc4")                 -> 0x439aec            ; Rescue Maiden
push 0x43bc20 ("tabc5")                 -> 0x439af0            ; Dragon Pass
push 0x43bc28 ("tabc6")                 -> 0x439af4            ; Wizard's Fury
mov  eax,ds:0x439ae0; mov ds:0x439afc,eax                      ; game over = tabc1
```

TABC3 is never loaded. The port had idle on track 1, Fire Lizards on 3, Gargoyle Gaggle on
7 and game over on 8 -- out of range for `MusicTrackCount = 7`, so game over was silent.
Skulduggery's map checked out unchanged.

Gating: Space Cadet never calls `PlayMusic`, and `TableControl` is null on its rows, so
it still gets `Track1`.

## 24. Per-player `MessageField`, as the Full Tilt base class keeps it

The Full Tilt engine keeps `MessageField` per player on **every** component. The base class
has two helpers, the same bytes at the same addresses in `CADET.EXE`, `DRAGON.EXE` and
`PIRATES.EXE`:

```
0x413550   MessageField = 0; copies[0..3] (+0x1a..+0x26) = 0
0x413570   copies[CurrentPlayer] = MessageField          ; 413589
           MessageField = copies[floor(value)]           ; 41358d..41359e
```

and the base `Message` (0x4135b0) is those two tests and nothing else: `cmp esi,0x402`
-> 0x413550, `cmp esi,0x3fe` -> 0x413570. It never writes the message code into
`MessageField` the way upstream's base does. The overriding `Message()`s call the same
helpers from their own `Reset` / `PlayerChanged` arms.

Upstream keeps no such copy on the base; it gives one only to `TLight`, `TBumper`,
`TLightGroup`, `TSink` and `TPopupTarget`, each its own. The port's own engine comments had
recorded the gap (`TKickout`, `TMagnet`, `TLightSweep`). It matters because the rules park
per-player state in `MessageField`s: Dragon's selected quest in `a_kout2` and its quest
number in `info_text_box`, Skulduggery's mode selection in `mode_timer`, blocker armour
stages. With two to four players one player's selection carried into the next player's
ball.

What the port does now: `TPinballComponent` gained `FtPlayerMessageField[4]`,
`FtPlayerFieldsReset()` (0x413550) and `FtPlayerFieldsSwap(value)` (0x413570), both no-ops
unless `pb::FullTiltMode`. The base `Message` under Full Tilt data is the two tests and
returns, which is what `TOneway`, `TRamp`, `TSound`, `TRampExit` and `TAccelerator`
inherit (vtables 0x4374c0, 0x437680, 0x437758, 0x4377e0, 0x437800). The overrides call
the helpers where the original does, each cited at its call site:

| class | Reset (0x413550 call) | PlayerChanged (0x413570 call), and what else the arm does |
|---|---|---|
| `TBlocker` | 0x415339 arm (section 13), then the shared tail | 0x4152e9 arm, then the shared tail |
| `TComponentGroup` | -- | 0x413838; not forwarded to members |
| `TDemo` | 0x41835d arm | 0x41830c, nothing else |
| `TDiverter` | 0x41bf4c | 0x41befa, before the per-player flag swap |
| `TDrain` | 0x4187d4 | 0x4187b3, nothing else |
| `TFlagSpinner` | 0x41481f | 0x4148b1, after killing the spin timer |
| `TGate` | 0x4154ca | 0x415493, then the disable arm (Full Tilt only) |
| `THole` | 0x416e49 | 0x416e12; see section 26 |
| `TKickout` | base reset | 0x416517, after the release |
| `TLightBargraph` | 0x40c051 | 0x40bfc2, before the bargraph's own arm |
| `TLightGroup` | 0x40bc4a | already per player upstream (0x40bbea) |
| `TLightRollover` | 0x4159c0 | 0x415a06, nothing else |
| `TLightSweep` | 0x40c9e7 | 0x40c9de; see section 27 |
| `TMagnet` | 0x41671d | 0x416705, then the disarm |
| `TPlunger` | 0x418c8c | 0x418c48, before clearing the pullback |
| `TRollover` / `TTripwire` | 0x415770 | 0x4157b3, nothing else |
| `TSoloTarget` | 0x414cc6 | 0x414cb8, nothing else |
| `TTextBox` | 0x41e136 | 0x41e12c; the whole of 0x41e110 |
| `TTimer` | 0x41b47c | 0x41b45a, then the timer kill (section 18) |
| `TWall` | 0x413c67 | 0x413c97, after expiring a running timer |

Gating: every helper call is a no-op unless `pb::FullTiltMode`, and the arms that add
behaviour beyond the swap (`TGate`, `TWall`, `TTimer`, `TFlagSpinner`, `THole`) are
additionally gated on it. `PINBALL.DAT` is unchanged. `CADET.DAT` gets the same per-player
fields `CADET.EXE` has.

Not ported: `TPinballTable`'s own `PlayerChanged` arm (0x419abb). (`TDrain`'s tilt arm,
0x418792, is section 45.)

## 25. Per-table high-score keys

`TableRules::SettingsPrefix` was set ("DRAGON", "PIRATES") and read nowhere.
`high_score.cpp` keyed its five entries and checksum as `"0.Name"`, `"0.Score"` ...
`"Verification"` with no prefix, so in one install Space Cadet, Dragon's Keep and
Skulduggery overwrote each other's table and checksum.

What the port does now: every table except the three Space Cadet rows prefixes the keys
with `"<SettingsPrefix>."` (e.g. `DRAGON.0.Score`). The Space Cadet rows keep the bare keys,
because upstream wrote them for whichever of `PINBALL.DAT`/`CADET.DAT`/`DEMO.DAT` it found,
and a player's existing scores should stay where they are. There is no original address:
the 1996 tables were separate executables with separate settings.

The rules' high-score check changed with it, on the rules side: both tables' `AnyHighScore`
(the call to 0x410ff0 in their game-over sequences) is upstream's `pb::chk_highscore` line
for line -- demo and cheats tests, then `get_score_position` for each player -- where the
port had "is any entry in the table above zero".

## 26. `THole`: tilt and player-change release, and attribute 1306's lift

`DRAGON.EXE`'s `THole::Message` (0x416c70):

* 0x3f3 (0x416cba) stores -1.0 over the value and falls into the 0x43 arm (0x416cc2),
  whose `value <= 0` branch releases at once (0x416cdb call 0x416a90).
* `Reset` (0x416e47) and `PlayerChanged` (0x416e0b) share a tail: clear or swap the
  per-player fields (0x416e49 / 0x416e12), drop the release retry, and if a ball is held
  kill the capture timer and release (0x416e30, 0x416e38).

The port answered `Reset` alone. Now `SetTiltLock` becomes `THoleReleaseBall(-1)` and
`PlayerChanged` takes the `Reset` path, both under `pb::FullTiltMode`; 3D Pinball's
`THole` still answers `Reset` alone.

**1306.** The constructor reads attribute 1306 (0x51a) at 0x4167c3 into `+0x92`, 0.0 when
absent. On capture the ball's z speed starts at it (0x416a0e..0x416a14), and in
`FieldEffect` (0x416acb) a positive value makes the ball **rise** at that constant speed
until `z >= ZSetValue` (0x416ae3, 0x416b5b); otherwise gravity pulls it down until
`z <= ZSetValue`. The port had only the second branch. Only Dragon's `s_hole118` carries
1306 (1.0), and its `ZSetValue` is above the ball, so `z <= ZSetValue` held on the first
frame and the ball snapped up instead of rising. Now `THole::LiftSpeed` carries 1306 and
`FieldEffect` branches as the listing does. No 3D Pinball or Space Cadet hole has 1306, so
they keep the fall.

## 27. `TLightSweep` stops on a player change

0x40c660's `PlayerChanged` arm (0x40c9d0) calls the virtual `Reset` (0x40c9d4, which stops
a sweep in flight) and then swaps the per-player field (0x40c9de). The port's arm list was
`GameOver`, `Reset`, `SetTiltLock`, so a sweep started on one player's ball stepped on into
the next player's. Now `PlayerChanged` stops the sweep and swaps; `Reset` clears the fields
then stops (0x40c9e7, 0x40c9f0). `GameOver` stopping the sweep is the port's own addition,
kept because it is harmless. The class exists only on Full Tilt data.

## 28. `TDemo` reads attributes 1405 and 1406

Full Tilt's `TDemo` constructor (0x417a50; the code at 0x417bbc..0x417c2f) reads two
optional attributes, each `{layer, wall...}`: the layer is truncated into a mask
`1 << layer` (0x417bdc / 0x417c15), and the wall is installed on that layer with offset 0
and the wall value of the trigger it duplicates -- 1405 as 1401 (flip left, 0x417be3
`push 0x579`), 1406 as 1403 (flip right, 0x417c1c `push 0x57b`). That is what lets the
attract-mode player work the upper flippers.

In the data: `DRAGON.DAT` has 1406 on layer 0; `PIRATES.DAT` 1405 on layer 0 and 1406 on
layer 2; `CADET.DAT` and `PINBALL.DAT` carry neither. The port read 1400..1404 only. Now
both are installed when present, so no gate is needed. `TDemo` also gained Full Tilt's
`Reset` and `PlayerChanged` field helpers (section 24).

## 29. Kicker attribute 404 is normalised on Full Tilt data

`DRAGON.EXE`'s loader handles kicker attribute 404 at 0x40d39b (jump table 0x40d4a0): it
copies X/Y/Z and jumps to 0x40d471, `call 0x40cb40` -- the same 2D normalise the 410 arm
ends with. The port's comment said Full Tilt's 404 had no normalise, which was inherited
from upstream and is wrong for this engine.

`CADET.DAT` has non-unit 404 vectors (|v| = 1.414 on several kickers), so on Full Tilt
Space Cadet those kickers threw about 41% too hard. Now 404 is normalised over X/Y when
`pb::FullTiltMode`. 3D Pinball's loader does not normalise, so `PINBALL.DAT` keeps the raw
vector.

## 30. Debug scaffolding: cached flags, emptied registries

Not a fidelity change. `TPlunger::Message`, `TPlunger::BallFeedTimer` and
`TBlocker::SetPosition` called `getenv` on every message or step; they now read
`ft_autodrain_enabled()` / `ft_blockdbg_enabled()`, which cache the lookup as
`FT_ACCELTEST` and `FT_AUTOPLAY` already did. The `FtAccelDebug.cpp` registries of holes,
sinks, kickouts, accelerators and ramp exits held component pointers and names that died
with the table and grew on every table load; `TPinballTable`'s constructor now empties
them first (`ft_debug_clear_registries`). Normal play prints nothing new.

## 31. The per-frame "That operation is not supported"

Not a fidelity change, and not Full Tilt's: it is upstream's vendored ImGui backend.
`ImGui_ImplSDL2_NewFrame` (`imgui_impl_sdl.cpp`) called `SDL_CaptureMouse` on every frame.
Wayland and the dummy video driver have no mouse capture, so every call left "That
operation is not supported" in SDL's error slot, and `winmain`'s main loop -- which prints
whatever is in that slot -- printed it once per rendered frame. Its duplicate limiter did
not help, because the loop iterates more often than it renders and the empty iterations in
between reset it. A 40 s headless run logged 2,387 copies; `test/play.sh` grew an awk
filter just to keep them out of the session log.

Found by checking `SDL_GetError` after each stage of the frame: only
`ImGui_ImplSDL2_NewFrame` set it. The backend now calls `SDL_CaptureMouse` only when the
wanted state changes, and clears the error when the call is refused. The same 40 s run logs
one line, raised once during start-up by a call that has not been identified.

## 32. `TLight`: the `ActiveFlag` gate, and `Reset` re-arms it

`TLight::Message` (0x40a490, the same bytes at the same address in `CADET.EXE`,
`DRAGON.EXE` and `PIRATES.EXE`) opens with
`cmp byte [ecx+5],0 / jne dispatch; cmp eax,0x402 / je; cmp eax,0x3fe / je; xor eax,eax; ret 8`:
a light whose `ActiveFlag` is clear ignores every message, and answers 0, except Reset
(0x402) and PlayerChanged (0x3fe). The virtual `Reset` (0x40a310, vtable +0x18), which the
constructor (0x40a21f) and both of those arms call, starts with `mov byte [esi+5],1`
(0x40a31c). Upstream had neither: every light dumped `ActiveFlag` 0, and the lights Dragon's
Keep switches off between dragon fights (v_lite113..115, cleared in the table's NewGame and
GameOver arms and by the disarm at 0x424050, set again by the arm at 0x424130) still answered the rules and the
light-group animations. Both are now in `TLight.cpp`, ungated: Space Cadet has the same code,
and nothing on 3D Pinball clears a light's `ActiveFlag`, so it never takes the early return
there.

## 33. `MultiballFlag` and the 500,000 jackpot are Space Cadet's rules, not the engine's

Upstream's `TPinballTable::Message` set `MultiballFlag = true; JackpotScore = 500000` in its
StartGamePlayer1 arm for every Full Tilt table. The writers of table +0x19a in the
originals are: the table constructor (0x41a316, 0) and the table's Reset arm (0x419e5e, 0)
in all three executables, plus `CADET.EXE`'s table-level rule handler 0x422c10, whose 0x3f6
(NewGame) arm at 0x422c4d stores 500,000 into +0x172 and 1 into +0x19a. `DRAGON.EXE` and
`PIRATES.EXE` have no third writer and no reader, so on the bonus tables the flag stays 0
(confirmed against both under Wine by `test/restdump`). The readers are Space Cadet's
rules only (`CADET.EXE` 0x42433d and 0x424637, `control.cpp`'s hyperspace and sink
handlers). The two stores moved to `control::table_control_handler`, on NewGame, behind the
same Space Cadet check as the rest of that body and `pb::FullTiltMode` (3D Pinball has no
multiball). A rest dump of Full Tilt Space Cadet still shows `multiball_flag` 1 and
`jackpot_score` 500,000.

## 34. The table's Reset arm sets `SuperJackpotScore` to 20,000

The table's Reset arm (0x419d82, identical in all three executables) loads
`mov ecx,0x4e20` at 0x419e14 and stores it into both +0x172 (`JackpotScore`) and +0x17a
(`SuperJackpotScore`, 0x419e3a), with 0 into their flags +0x176/+0x17e. Upstream set only
`JackpotScore`. Dragon's Keep overwrites both in its own NewGame arm; Skulduggery left the
port's super jackpot at 0 where the original has 20,000. Nothing on Space Cadet reads it,
so the store is ungated.

### Not changed: two component groups the originals have and the port does not

The originals' component list starts with two unnamed `TComponentGroup`s more than the
port's. They are made by the table constructor (`new(0x4e)` + 0x413670 with group 0, at
0x41a366 and 0x41a38c, in all three executables) and stored at table +0x42 and +0x46. The
object loader appends every type-1003 flipper to +0x42's list (0x41a5a1) and every 1004 to
+0x46's (0x41a5f8), and the table's flipper-input arms (0x419638..0x4196a7) send extend /
retract to the group instead of to a flipper. The port reaches the same flippers through
`TPinballTable::MessageFlippers`, which walks `FlipperList` by type code under the same
`TiltLockFlag` test, so the two groups would add list entries and nothing else.

### Not changed: v_lite114's `ToggledOffFlag`

After 32 the inactive v_lite113..115 freeze whatever state the new-game light show left
them in: the table's NewGame arm starts `TLightGroupLightShowAnimation` on the all-lights
group (`start_animation` puts every light in `TurnOffTimed`, then about 30% get a random
`TurnOnTimed`) and only then does Dragon's NewGame arm clear their `ActiveFlag`, so the
light show's own cleanup later cannot reach them. Which of the three got a `TurnOnTimed`
depends on the `rand()` stream (the originals' TLightGroup animations call the MSVC `rand`
at 0x42c4cb; the port uses the C library's), so v_lite114 differs only through the random
number generator, which is handled separately.

## 35. One rand() for engine and rules; the light show's short loop; no-audio durations

Three things decide what the first random pick of a game gets, and the port had all
three differently from the originals. All are gated on `pb::FullTiltMode`; `CADET.EXE`
has the same code at the same addresses, so Full Tilt Space Cadet changes with the bonus
tables and only `PINBALL.DAT` keeps upstream's behaviour.

**One generator.** Each Full Tilt EXE links the C runtime's `rand()` statically
(`DRAGON.EXE` 0x42c4cb, `PIRATES.EXE` 0x42dd0b, `CADET.EXE` 0x42acdb): MSVC's LCG
(`seed = seed * 0x343fd + 0x269ec3; return (seed >> 16) & 0x7fff`) on one seed
(0x43bc74 / 0x43d23c / 0x439b1c) that is 1 in the file's `.data` and that nothing but
`rand()` writes -- no `srand` call exists. The engine's 17 call sites are at the same
addresses in all three EXEs: `TBall` throw (0x402a0b, 0x402a45), `TLightGroup::Message`
(0x40b412, 0x40b440, 0x40b4f3, 0x40b582, 0x40b646), `TDemo` (0x417d71 ... 0x418237),
`TPlunger` collision (0x418eb2, 0x418f14) and the table layer's field effect (0x4193bb).
The rules call the same function: Dragon 15 more sites (0x42351b ... 0x4280e2), Skulduggery
9 (0x4236b6 ... 0x42b576). Upstream used the host `rand()` and `RandFloat()`, and the rules
each kept a private copy of the LCG, so the rules' first draw was always the sequence's
first value. Now `EngineRand()` / `RandFloat()` (pch.h, defined in pb.cpp) are that one
generator on Full Tilt data, with `RandFloat` dividing by 0x7fff as the EXEs do (they
multiply by 3.051851e-05), and `DragonRand15` / pirates' `Rnd` call it.

**The light show and game-over animations skip the last light.** `TLightGroup::Message`
iterates `for (i = 0; i < count - 1; i++)` in both (0x40b407..0x40b483 and
0x40b4cc..0x40b52c, identical in all three EXEs); upstream iterates every light. The
table's light group is every `TLight` (110 on Dragon, 149 on Skulduggery), so the new-game
light show draws for 109 / 148 lights.

**Durations without a wave are 0.** Full Tilt's sound loader zeroes a sound's duration
(0x40ccd1) and writes it from the wave only after the wave loads (0x40cd4c, after
0x41e710, which returns 0 at once when the WaveMix session at 0x4396dc is not open).
`play_sound` returns that duration, and the new-game arm uses it unchecked as the
light-show timer (0x419a47..0x419a5c). Upstream computes the duration from the WAV header
whether or not the wave loaded. Now on Full Tilt data a sound whose wave did not load has
duration 0. With sound working nothing changes.

**Measured.** Under Wine without a sound device (the rest-dump harness), polling
`DRAGON.EXE`'s seed shows: seed 1 until the new game; 134 draws (the light show over 109
lights) while `LightShowTimer` is set; the timer fires on the next tick; one draw, the
quest pick, `(r * 5) >> 15` with r = 13966 = slot 2 (quest 6); nothing more until the ball
is fed 0.96 s later. `PIRATES.EXE` shows 185 draws (148 lights) before its timer clears.
The port run with `-noaudio` (`test/restdump/native_dump.sh` now passes it, to match) makes
the same pick, and `a_kout2` / `a_rockb` `MessageField` and the flashing quest light
(v_lite73, not v_lite71) agree with the originals. With audio the light show runs for its
sound's length on both sides, so the first pick comes later in the sequence. Once the ball
moves, physics draws diverge and later picks are not expected to match.

## 36. The plunger's release window and pullback step on Full Tilt data

`TPlunger` used 3D Pinball's timings on every table. CADET.EXE's TPlunger has two others:

- On release (0x418aa0) it arms `ReleasedTimer` (0x418930) for 0.2 s (`push 0x3e4ccccd`),
  not for `PullbackDelay`. The boost is live only until that timer fires, and a ball
  resting on the plunger collides with it about every 33-42 ms, so the 25 ms window often
  held no collision at all and the press was lost. Skulduggery's launcher, whose press
  is the release, lost most first presses ("launched only on the second or third pull").
- The pullback steps every 0.05 s (`[+0x82] = 0x3ccccccd` at 0x4188e3, read by
  `PullbackTimer` at 0x418dbe/0x418dd2), not every 0.025 s.

The same instructions are at the same addresses in DRAGON.EXE and PIRATES.EXE. Both values are gated on `pb::FullTiltMode`; 3D Pinball
data keeps upstream's 0.025 s for both.

## 37. A sound that did not load lasts 0 s

`loader::get_sound_id` read a sound's duration from its WAV header whether or not the wave
loaded. CADET.EXE zeroes the duration (0x40ccd1) and writes it only after the wave loads
(0x40cd28 skips the store when 0x41e710 returns null, and 0x41e710 returns null when there
is no DirectSound object, 0x41e768). With no sound device every duration is therefore 0,
and so is the new-game light show, which waits `play_sound`'s return (0x419a47..0x419a61):
the ball is served 0.96 s after F2. Now the duration is kept only when `WavePtr` is
non-null, which is the case whenever the mixer is open, the SDL dummy driver included, so
normal play is unchanged. It is gated on `pb::FullTiltMode` (the same change as the
last part of section 35, found twice independently): 3D Pinball data keeps upstream's
header duration.

This matters for comparisons. The silent Wine setup (`Audio=""`) gives the original no
DirectSound, so the original skipped its light show and took a plunger pull 1.4 s after F2.
Ours, with the dummy driver, waited out the light-show WAV (about 4 s on Space Cadet) and
ignored that pull, because the plunger ignores presses until a ball is in play
(0x41898a: `[table+0x192]`). Run ours with `-noaudio` to compare against silent Wine.

## 38. The tilt timeout is 15 s on Full Tilt data

`TPinballTable::tilt` armed `tilt_timeout` for 30 s, 3D Pinball's value. Full Tilt arms it
for 15 s (0x41a25b `push 0x41a1b0` / 0x41a261 `push 0x41700000` / 0x41a266 call
timer::set). The bytes are the same in CADET.EXE, DRAGON.EXE and PIRATES.EXE, so it is
gated on `pb::FullTiltMode`; PINBALL.DAT keeps 30 s. Run: `FT_TABLETEST=tilt` (see
section 39) on Skulduggery reports the timer gone after 15.000 s of game time (30.000 s
before the change).

## 39. The tilt timeout leaves table-placed balls alone

`tilt_timeout` called `Drain->Collision` for every ball in `BallList`. Full Tilt skips the
balls the table placed itself (0x41a1f4 `mov eax,[esi]` / 0x41a1f6 `cmp [eax+0x153],ebp` /
0x41a1fc `jne 0x41a216`, past the call at 0x41a213); +0x153 is our `TBall::HasGroupFlag`.
Without the skip, every tilt drained Skulduggery's captive ball: `TDrain::Collision`
disabled it and took one more off `MultiballCount`, Reset never re-enables a placed ball,
and `AddBall` then reused it as the next fed ball. Same bytes in all three EXEs; gated on
`pb::FullTiltMode` (3D Pinball places no ball, so the gate changes nothing there).

`FT_TABLETEST=tilt` (FtAccelDebug.cpp, inert without the variable) starts a game, tilts
with the fed ball pinned in the lane so only the timeout can end the tilt, and logs the
placed balls before and after. Skulduggery: before the change the captive ball was
`active=0` after the timeout and 6 s later sat at the plunger as the fed ball; now it stays
`active=1` at (4.573,-8.777) throughout.

## 40. The super jackpot is kept per player

The table saved and restored `JackpotScore` on a player change but not
`SuperJackpotScore`. Full Tilt keeps both in the 32-byte player record: the super jackpot
at +0x92, seeded for players 2-4 at new game (0x41998c `mov ecx,[edi+0x17a]` / 0x419992),
saved at 0x419b9b..0x419baa and restored at 0x419bf4..0x419bfa. `score_struct_super` gains
the slot; the three copies are gated on `pb::FullTiltMode` (same bytes in CADET.EXE, and
Space Cadet never reads the value). As in the original, the seeding happens before the
table handler's new-game arm, so on Dragon's Keep player 1 starts at 2,500,000 and the
other players at the Reset arm's 20,000.

`FT_TABLETEST=players` runs a two-player game, stamps each player's value and drains.
Dragon's Keep: player 1 2500000 -> set 1111111; player 2 20000 -> set 2222222; player 1
back with 1111111; player 2 back with 2222222. Skulduggery gives the same round trip.

## 41. StartGamePlayer1 sends the master light group only the reset

On StartGamePlayer1 the table sent its light group `TLightGroupReset` and then
`TLightResetAndTurnOff`. Full Tilt sends one message, 0x4197c9 `push 0` / 0x4197cb
`push 0x26` (Full Tilt's TLightGroupReset, engine 34) / 0x4197cf call, then goes straight
to the plunger (0x4197d8). Same bytes in CADET.EXE; the second message is skipped on
`pb::FullTiltMode`. The rest-state comparison is unchanged (0 differences on both tables).

The "Player N" text that follows (and the one at SwitchToNextPlayer) was untimed on every
table. Each Full Tilt executable shows it through its own text-event function, codes
0x60..0x67 (demo / normal text for players 1-4), and each passes its own time to
TTextBox::Display: CADET.EXE 0x42a750 6.0 s (`push 0x40c00000` in all eight arms,
0x42a895..0x42a991), PIRATES.EXE 0x422b90 1.0 s (`push 0x3f800000`, 0x422ccc..0x422dc8),
DRAGON.EXE 0x422b80 untimed (`push 0xbf800000`, 0x422cbc..0x422db8). The time is now
`TableRules::PlayerTextTime` (0 = untimed, so PINBALL.DAT, DEMO.DAT and Dragon's Keep
keep -1; DEMO.DAT's executable was not available to check). At StartGamePlayer1 Full
Tilt also shows the text only when the table has a demo part (0x4197e7 `mov eax,[edi+0x112]`
/ 0x4197ef `je 0x419802`), gated on `pb::FullTiltMode`. The player-change arms read the
demo part null-safely (0x419ca1..0x419ca9) and now do the same.

## 42. Nudging needs a ball in play; the tilt needs no ball draining and no demo

Full Tilt's keydown (0x41221a..0x412294) returns from each bump key unless
`[table+0x192] >= 1` (`MultiballCount`) as well as `[table+0x1b2] == 0` (`TiltLockFlag`).
Its frame (0x411b03..0x411b6d) shows the nudge warning only with `TiltLockFlag` and
`[table+0x6a]` (`BallInDrainFlag`) both clear, and tilts only if, in addition, the demo part
`[table+0x112]` is absent or its byte +5 (`ActiveFlag`) is 0. Ours tested `TiltLockFlag`
alone in both places. The extra conditions are gated on `pb::FullTiltMode`; the three EXEs
differ in these ranges only in global addresses.

## 43. The 2 s start-up wait

Full Tilt's WinMain waits 2000 ms before the first game or demo (0x4221b0..0x4221f2: a
timeGetTime loop, `cmp eax,0x7d0 / jae`, around `PeekMessage(..., PM_REMOVE)` with no
dispatch, so input typed during it is dropped). winmain now does the same on
`pb::FullTiltMode`: it pumps SDL events for 2 s, flushing input events
(SDL_KEYDOWN..SDL_MULTIGESTURE) and leaving window and quit events queued. No `pb::frame`
runs, so game time does not move and the headless harnesses (test/run_tables.sh,
test/restdump) are unaffected apart from 2 s of wall clock. Only global addresses differ
in this range across the three EXEs.

## 44. Full Tilt's text-box queue: an untimed message clears it

Upstream's `TTextBox::Display` appends an untimed (`-1`) message behind whatever timed
messages are queued, so it waits for them, and its timer callback tells the rules
(`ControlTimerExpired`) on every expiry. The Full Tilt engine does neither. Display 0x41de40:

- same text and font as the last queued message (0x41de55..0x41dea5): both untimed, nothing
  happens; new one timed, the last message takes the new time and the queue is rescheduled
  without a redraw (0x41dee3..0x41df06);
- an untimed head is dropped by any new message (0x41df19..0x41df33), and an untimed new
  message drops the whole queue (0x41df38..0x41df4c, `call 0x41d8e0`), so it shows at once;
- the message is appended, the queue rescheduled (0x41dd30) and the head drawn (0x41db00).

The scheduler 0x41dd30 kills and re-arms the timer each time; an untimed head gets no timer
at all; heads more than 2 s past their end are dropped; the timer is the head's remaining
time but at least 0.25 s; and only an empty queue sends control code 0x46 (ControlTimerExpired)
with the box as caller (0x41dddb), after which the box is erased if the rules queued nothing
(0x41d930). The timer callback 0x41dce0 drops the head and reschedules.

`TTextBox::FtDisplay` / `FtSchedule` / `FtTimerExpired` / `FtClearQueue` port these four, and
upstream's `Draw` is split into its scheduling half and `Render` (the drawing half) so both
paths share the drawing. The rules' `AnnouncementPending` guard on the text box's
`ControlTimerExpired` (dragon_missions.cpp, pirates_modes.cpp) is now always true when it is
tested, as it was in the originals, which test +0x5e the same way. `Clear(true)` (the
port's one-pass quote scroll) reschedules through `FtSchedule` on Full Tilt data.

Gating: CADET.EXE has the same code at the same addresses (0x41d800..0x41e160 differ only in
data and call targets), so this is the Full Tilt engine's behaviour; it is gated on
`pb::FullTiltMode` and 3D Pinball data keeps upstream's queue. The rest-state comparison is
unchanged (0 differences on both tables), and the start-of-game text sequence on both bonus
tables is the same as before (the opening texts are all untimed or a single timed run).

## 45. TDrain's SetTiltLock arm

TDrain 0x418770 has a third arm the port lacked: 0x3f3 (SetTiltLock, 0x418777) jumps to
0x418792..0x4187a9, which kills the end-of-ball timer and returns -- no per-player reset and
BallInDrainFlag untouched, unlike Reset (0x4187be..0x4187dc). Dragon's Keep's ball save sends
exactly this to its drain (0x424973 `push 0x3f3`) before relaunching the saved ball itself;
src/dragon_part4.cpp sent Reset instead, which also ran 0x413550 on the drain and wiped the
bonus the Resistance-to-Fire spell keeps in the drain's MessageField, for every player. With
SetTiltLock and no engine arm the drain's timer would survive and serve a second ball.

Gated on `pb::FullTiltMode`; CADET.EXE has the same arm at the same address. A run of the
`FT_AUTODRAIN` harness on Dragon's Keep with a sentinel in the drain's MessageField shows one
relaunch either way and the sentinel kept (it was zeroed before).
