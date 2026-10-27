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
| `TBlocker::UnknownP7e` | 1 | `TBlocker.h` |
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

## 5. `TBlocker`'s field at `+0x7e` — 1 use

Both fire lizard sink handlers read it as a **float** (`fld`, truncated through `__ftol`),
while `GargoyleRockAControl` compares the same offset against `-1` as an **int**. Both
readings reconcile if it is a float whose `-1.0` means "no duration", which would make the
integer compare the one to fix. Disassembling Full Tilt's `TBlocker::Message` settles it;
until then it is `UnknownP7e` and the port does not depend on the answer.


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
  `value > 0`. `TBlockerRestartTimeout` still clamps with `std::max(value, 0.0f)`; no
  caller in either table passes it a negative value, so the clamp is unreachable rather
  than wrong.
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
