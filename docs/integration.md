# Linking the rules into the engine

Done. The three tables build from one binary, each runs its own rules, and
`test/run_tables.sh` passes all three. What that does and does not prove is below.

## What landed

### 1. The sources are in the build

`reference/CMakeLists.txt`'s `SOURCE_FILES` names all of `../src/*.cpp`, and the target
gained `target_include_directories(... SpaceCadetPinball)` so the rule files resolve
`"pch.h"` and `"control.h"` the way the engine's own translation units do. `build.nix`
copies the repo root (filtered to `reference/` and `src/`) with
`sourceRoot = "fulltilt-src/reference"`, because `../src` has to be inside the copied
tree for the path to resolve.

Three files in `src/` are generated and must be regenerated rather than edited:

```sh
python3 analysis/gen_glue.py          # src/<table>_components.cpp
python3 analysis/gen_control.py binaries/win95/DRAGON.EXE  analysis/dragon-matches.txt  src/control_dragon.cpp
python3 analysis/gen_control.py binaries/win95/PIRATES.EXE analysis/pirates-matches.txt src/control_pirates.cpp
```

Run `gen_glue.py` first: the control skeletons declare a tag `extern` with whatever
type a `src/` file gives it, and the component files are where the concrete types
(`TLight`, `TSound`, `TLightGroup`) for the shared components come from. Both control
runs must report `N/N handlers wired to a reimplementation, 0 still stubs`.

### 2. The rules are bound

`FULLTILT_HAVE_BONUS_RULES` is 1. Each bonus table's registry row carries its
`ScoreComponents` / `SimpleComponents`, its mode dispatcher (`dragon::MissionHandler`,
`pirates::Handler`) and its mode register, which is the table's own
`tag_info_text_box` -- unambiguous now that each table's tags live in its own
namespace. Built with the switch off, the identity and the music survive and the
bindings go null, which makes the playfield load and nothing score rather than crash.

### 3. Dispatch goes through the current table

`control::make_links` and `ClearLinks` walk `CurrentTable`'s arrays. `control::handler`
calls the current table's `ModeControl`, in the order `TableRules::ModeBeforeComponent`
gives.

**Both** bonus tables are mode-first, not just Dragon's Keep: `DRAGON.EXE`'s 0x423060
and `PIRATES.EXE`'s 0x423080 are the same four lines -- mode machine, then the
component's `ControlFunc`. Space Cadet is the one that runs the component first. The
order is load-bearing because so many bonus-table handlers suppress their own scoring
while a mode is running, which only works if the mode controller has already seen the
message; reversed, it double-scores in silence. Skulduggery's extra force-state-2 and
re-dispatch after a teardown is inside `pirates::Handler`, where the original has it,
and is not repeated in the engine.

## What the linking turned up

The rules did not merely need wiring. Each slice had been written against its siblings'
interfaces, declaring what it needed as a prototype and assuming someone defined it:
**227 undefined symbols and 79 duplicate definitions.** Resolving them was most of the
work, and several were latent bugs rather than missing plumbing:

- **`extern T* x;` against a reference definition.** 160 declarations across the slices
  named a component alias as a pointer where the owning file defines
  `T*& x = tag_x.Component;`. A variable's mangled name carries no type, so these linked
  silently and would have yielded the address of the tag's field in place of the
  component. All retyped to `extern T*&`.
- **Static tables of components.** `TLight* const QuestLights[5] = {tag_v_lite71.Component, ...}`
  at namespace scope is initialised before `main`, so it captured five nulls and kept
  them. Dragon's Keep died on its first plunger in exactly this way. They are now
  `ComponentTable<T, N>` -- the tags, indexed through at the point of use.
- **One `.data` cell under two names.** Five pairs in Dragon (0x434d08, 0x434d24,
  0x434d2c, 0x439b10, 0x439b14) and one in Skulduggery (0x435cf8, which was both
  `ModeScratch` and `mode_scratch_counter`) would have become two cells for one
  original. Collapsed to one name each.
- **The multiplier.** The bonus rules write multiplier *values* (2..5) into
  `TPinballTable::ScoreMultiplier`, which upstream reads as an index into
  `score_multipliers[5]`. Now `TableRules::MultiplierIsValue`.
- **`control::UnstuckBall` dereferenced a null `plunger` tag** -- Space Cadet's group
  name, absent on both bonus tables. It goes through `TPinballTable::Plunger`, which is
  built from the DAT.
- **Inert light overrides.** Both tables aliased the originals' 0x18/0x19/0x1a for the
  FT light-override codes. The port numbers those -24/-25/-26, so as written they did
  nothing to a `TLight` and hit `TLightGroupStepForward` on a light group.
- **`analysis/link_impls.py` mis-attributed two addresses**, because a section header
  naming an address is paired with the next definition under it. 0x427880 pointed at
  `RescueMaidenAwardWindow` instead of `PrisonWindowTripControl` and 0x426140 at
  `RearmMysteryAward` instead of `WellSinkControl`, each silently shadowing the real
  handler. The two headers no longer spell the address in a form the pairing matches.

What was genuinely unimplemented -- Dragon's five bonus/multiball mode controllers and
three award helpers, Skulduggery's eleven mode setters, its playfield setup/teardown
helpers and twenty award helpers -- was reverse engineered from the decompiled corpora
into `src/dragon_helpers.cpp`, `src/pirates_modes_entry.cpp` and
`src/pirates_awards.cpp`.

## What "it runs" does not mean

`test/run_tables.sh` passes all three tables, and its own header is the honest account
of what that proves: the table constructs, binds and pumps the event loop. Nothing
launches a ball, presses a flipper, enters a quest or reads a score.

What *has* been exercised since is the engine's debug harness
(`reference/SpaceCadetPinball/FtAccelDebug.cpp`, env-gated), which drives a ball into a
named sink or hole and logs the swallow and the eject. On Dragon's Keep every sink the
harness can reach gives the ball back -- 68 swallows, 68 ejects across seven sinks,
including the two-hit fire-lizard path that starts a quest -- and all three holes release.
See the appendix of `analysis/dragon-ball-handlers.md` and `analysis/dragon-holes-ramps.md`.
`FT_AUTOPLAY` starts a game and plays it blind; after the review pass below, a 40 s run
on each bonus table finished with no crash. That is still not "the rules are right"; it is
"the ball comes back and nothing dereferences null".

**Nobody has played either table in a real window and compared it with the original.**
Every claim about behaviour in these documents comes from listings, the decompiled corpus,
the DATs, and headless runs that render nothing.

### The review pass

A later pass compared every handler, mode controller and helper of both tables against
its listing, swept `simple_components[]`, censused every function in both rules regions
(not just the ones bound in `score_components[]`) and reviewed the engine patch. It found
crashes that the construction harness could not see, because each needs a particular
action in play:

- **Tilting either bonus table** ran Space Cadet's table-level handler and dereferenced
  its unbound `lite77`. Each table now has its own (`DRAGON.EXE` 0x424260,
  `PIRATES.EXE` 0x4230c0), which also carries the new-game, tilt and game-over resets that
  had been missing: opening jackpots, Dragon's super jackpot, Skulduggery's
  buried-treasure pouch and brig, the tilt lockdown. `docs/engine-changes.md` section 16.
- **The Cheats menu or a typed code** on either bonus table ran Space Cadet's backdoor
  into unbound tags. Each table now has its own key handler (0x4282c0 / 0x42a830), with its
  own codes and the flipper skip of the bonus count. Section 17.
- **Slay Dragon** crashed on the fire sweep's fourth light: five of the eight entries of
  `DragonFireLights` were left null although `.data` at 0x43bb78 names them
  (v_lite58..62). Filled in.

and behaviour that was silently wrong: every countdown blank (no `TTimer` 0x48,
section 18), Skulduggery's captive ball reset away at game start (section 19), all eight
of Dragon's diverters without walls (section 20), cradled balls shoved off unprotected
flippers (section 21), Dragon's music map and its silent game over (section 23),
multiplayer state leaking between players (section 24), high scores shared between tables
(section 25), Skulduggery's combos never breaking because the 0x4d notify went to the
component's `Message` instead of through the dispatcher, Shoot Again serving a second ball
because the drain reset was sent as the raw 0x402, raw light-group codes that landed on
the wrong `TLightGroup` messages, and the `TableG+0x13e` sends -- `TPinballTable::LightGroup`
-- that Skulduggery had left out. `docs/how-this-was-done.md` section 5.7 has the lessons.

## Open questions

- **Play.** See above; it is the largest open item and the only one that would catch a
  rule that is faithfully transcribed and wrongly understood.
- **The alternate ball skin.** Dragon's `PB` and Skulduggery's `GMAX` toggle
  `pb::AlternateBallSkin` as the originals do, but `GroupData` keeps only the first bitmap
  of each ball state and `TBall` does not read the flag, so nothing changes on screen.
- **The quote scroll** is queued as one pass; the original loops until a key stops it.
- **`TPinballTable`'s own `PlayerChanged` arm** (0x419abb) is not ported.
- **Which picture each Skulduggery glyph token draws.** The token bytes are confirmed
  against `.data` and now draw through the `font2` symbol font, but which artwork sits at
  font2's slots for '3'..'9' has not been rendered and checked
  (`src/pirates_part1.cpp`, `src/pirates_part2.cpp`).
- **Which physical component** several abstractly-identified ones are -- which outlane
  `a_kick2` guards (Skulduggery's `src/pirates_part3.cpp` reads `a_kick1` as the outer
  lane from two coordinates, which is not evidence), which tower each lock belongs to.
  (Dragon's `unknown_a99c` is no longer one of them: 0x43a99c is `a_sink9`'s `Component`
  field. The tags are `{const char* Name; TPinballComponent* Component;}` with the name
  string inline just before, so 0x43a990 holds `"a_sink9"`, 0x43a998 the pointer to it,
  and 0x43a99c the component -- which makes every unannotated `DAT_0043xxxx` the
  decompiler left bare identifiable by reading the four bytes before it.)
- **`FormatWithValue`** (Dragon's 0x42c76f) takes a value the decompile loses on the
  x87 stack at every call site. It substitutes the last credited score where the string
  has a specifier and copies the label where it does not. Skulduggery's Peg Leg payout and
  drain-bonus total now format what `AddScore` returned, as the listings show
  (0x42d27d, 0x42bbd4); Dragon's call sites have not been rechecked the same way.
- **A few tags are typed generically** by the generated control tables (Dragon's
  v_lite58..62 among them), so playing a sound or testing `light_on()` through them needs
  a `static_cast` down. Safe, because the objects really are `TSound` / `TLight`, but
  unchecked.
- **Announcement nits.** The Full Tilt engine's announcement dispatcher (0x422b80 /
  0x422b90) is stood in for by the engine's Space Cadet strings, which are identical text;
  its Music/Sound On/Off pause texts are never shown, and its case 0x5f clears the box only
  when no game-over sequence is running, which is not modelled.

Settled, kept for the record:

- ~~**Success versus timeout is undiscriminated**~~ -- **settled**, see
  `analysis/quest-outcome.md`. A discriminator does exist (`cmp esi,0x4a` at Dragon's
  0x429a18), but it only chooses which mode the teardown returns to; `call 0x424000`
  sits outside the branch, so `FinishQuest` books the quest either way. The reconstruction
  was already correct and now has the addresses behind it. The design is deliberate: four
  of Dragon's five quests have no objective to meet at all, and Skulduggery counts and
  pays even its explicitly lost mode (string 3050 "Shipwrecked").
- ~~**The glyph tokens render as ASCII.**~~ -- **done** on both tables. The symbol font is
  the `font2` group of each bonus table's own DAT, 20 glyphs in `DRAGON.DAT` and 52 in
  `PIRATES.DAT`, absent from `CADET.DAT`; see `docs/engine-changes.md` section 10.
  Skulduggery's `DisplayGlyph` wrapper and four lane handlers still called the text
  `Display` until the review pass.
- ~~**Dragon's `a_ramp5`**~~ -- **fixed**, and it was the port's fault. The ramp has no
  handler, which is faithful (it is in neither of `DRAGON.EXE`'s component tables), but it
  was also unreachable: `a_sink16` is its only feeder and the port's `TSink` never put the
  ejected ball on the sink's release layer. See `docs/engine-changes.md` section 12.
- ~~**The ball's z freezes while crossing a ramp wall**~~ -- the freeze itself is
  faithful (attribute 1305 is 0 on all six of Dragon's ramps, and `TBall::Repaint`'s own
  guard at 0x402b1f is the same), but the *frozen value* was wrong, because `THole` was
  reading the wrong element of attribute 408. Fixed; see `docs/engine-changes.md`
  section 12. Whether anything still looks wrong needs eyes on a real window -- the
  headless harness renders nothing.
- ~~**`s_hole118` pops the ball up**~~ -- **fixed**: `THole` ignored attribute 1306, the
  constant-rate lift; `docs/engine-changes.md` section 26.
