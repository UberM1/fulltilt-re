# Engine changes the port needs

Derived, not guessed. The rules for Dragon's Keep in `src/` are syntax-checked against
upstream's own headers:

```sh
nix-build build.nix             # upstream builds unmodified from reference/
g++ -std=c++17 -fsyntax-only -I reference/SpaceCadetPinball -I <SDL2> src/*.cpp
```

Five of the eight files compile clean against unmodified upstream headers. Every remaining error is a thing the engine does
not yet have, so the error list *is* the change list.

| file | errors |
|---|---|
| `dragon_part1.cpp` | 0 |
| `dragon_part2.cpp` | 0 |
| `dragon_part4.cpp` | 0 |
| `pirates_part1.cpp` | 0 |
| `pirates_part2.cpp` | 0 |
| `dragon_part3.cpp` | 1 |
| `dragon_missions.cpp` | 43 |
| `pirates_modes.cpp` | 44 |

The clean files are the ordinary scoring components; the three that do not compile are the
two mode machines and one sink file. That split is itself informative -- per-component
rules need nothing from the engine that Space Cadet did not already need, and everything
below is a consequence of running *more than one table*.

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

This is the music half of the per-table identity that `src/table_rules.h` describes;
`MusicPrefix` and `MusicTrackCount` are there for it.

## 3. Balls in play — 6 uses (Skulduggery only)

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

## Not surfaced by the compiler

These do not produce errors because no Dragon rule names them, but the table will not load
or play correctly without them. From `simonsteiner/SpaceCadetPinball` PR #67, which solved
the loader side independently, plus this project's own component analysis:

- **Seven object types the loader skips**: 1034 launcher, 1035 ramp exits, 1036
  accelerators, 1037, 1038 light sweeps, 1042 magnets, 1043 diverters. Dragon's Keep uses
  1035/1036/1038/1043; Skulduggery adds 1034/1037/1042.
- **`loader::sound_list` holds 64**; `PIRATES.DAT` has 67 sound groups.
- **Kicker attribute 410** is unknown to `loader::kicker`, so `query_visual` returns early
  and `TKickout` dereferences a null `FloatArr`. 18 kickers in `DRAGON.DAT` use it.
- **`ScoreMultiplier` is an index upstream and a value in Full Tilt.** `AddScore` in these
  binaries is `field * score + ScoreAdded` with no indexed load, and the callers write
  1/2/3/5/10 directly. See `src/table_rules.h`.
- **A negative timeout means *cancel*** in Full Tilt, where `TBlocker::Message` clamps with
  `std::max(value, 0.0f)` and would trip the timer instead.
- **Text-box tokens.** Several displays pass one-character strings (`"!"`, `"#"`, `"##"`,
  `"###"`, `"####"`) with a fourth `Display` argument of 1. They are glyph tokens, not
  text; rendering them as ASCII shows hashes on the playfield.
