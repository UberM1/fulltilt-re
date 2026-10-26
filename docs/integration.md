# Linking the rules into the engine

Where the project stands and what the last step is.

## What is done

- `reference/` builds (`nix-build build.nix`), patched by `engine/fulltilt-engine.patch`.
- All ten rule files in `src/` compile against that patched engine with **zero errors**.
- The loader accepts Full Tilt's kicker attribute 410, its larger sound tables, and the
  seven object types 3D Pinball never had.

## What is not

The rules compile but are not **linked**. `src/*.cpp` is not in `CMakeLists.txt`'s
`SOURCE_FILES`, and nothing calls into them: `control::make_links` still walks Space
Cadet's `score_components[88]` and `simple_components[145]` unconditionally.

## The step

Two halves, in this order.

### 1. Add the sources

```cmake
set(SOURCE_FILES
        ...
        ../src/pirates_common.cpp
        ../src/dragon_part1.cpp
        ../src/dragon_part2.cpp
        ../src/dragon_part3.cpp
        ../src/dragon_part4.cpp
        ../src/dragon_missions.cpp
        ../src/pirates_part1.cpp
        ../src/pirates_part2.cpp
        ../src/pirates_part3.cpp
        ../src/pirates_modes.cpp
        ../src/control_dragon.cpp
        ../src/control_pirates.cpp
)
```

The two `control_<table>.cpp` are generated — regenerate rather than edit:

```sh
python3 analysis/gen_control.py binaries/win95/DRAGON.EXE  analysis/dragon-matches.txt  src/control_dragon.cpp
python3 analysis/gen_control.py binaries/win95/PIRATES.EXE analysis/pirates-matches.txt src/control_pirates.cpp
```

Their generated stub bodies must give way to the hand-written handlers: the generator
emits component-derived names (`a_bump1Control`) while the implementations are named for
what they do (`ForestBumperControl`). `analysis/link_impls.py` prints the mapping, and
cross-checks it — it will report any handler defined outside its table's namespace, which
would compile and then fail to link.

### 2. Dispatch per table

`control::make_links` and `control::handler` are Space Cadet's by construction. They need
to go through `CurrentTable` instead, so that:

- `make_links` walks the current table's `ScoreComponents` / `SimpleComponents`.
- `handler` calls the current table's `ModeControl` rather than `MissionControl`.

One ordering detail is load-bearing: **Dragon's Keep calls its mode machine *before* the
component's own handler**, the opposite of Space Cadet. That is why so many of its
handlers suppress their own scoring during a quest. Getting it backwards silently
double-scores. Skulduggery's dispatcher additionally forces state 2 and re-dispatches
after every teardown.

## After that

The tables will load and run their own rules, which is the point — but "runs" is not
"correct". The analysis documents carry explicit open questions that only a live table can
settle, and they are flagged `// UNVERIFIED:` at their sites in the code. The largest:

- **Success versus timeout is undiscriminated** on both tables. The binaries call the same
  teardown path whether a mode is won or runs out, so a timed-out quest still lights its
  light and still counts toward the drain bonus. Written as the binary behaves, documented
  as unresolved; watching a real game settles it.
- **Which physical component** several abstractly-identified ones are — which outlane
  `a_kick2` guards, which tower each lock belongs to.
- **The seven object types are stand-ins**, mapped onto the nearest existing class so the
  tables load. Magnets as kickouts and diverters as blockers behave approximately, not
  faithfully. Their real constructors are in `DRAGON.EXE` and `PIRATES.EXE`.
- **Glyph tokens.** Several displays pass one-character strings that the original renders
  as symbols. Rendering them as ASCII puts `#` and `!` on the playfield.
