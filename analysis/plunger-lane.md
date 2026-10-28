# The ball jitters in Dragon's Keep's plunger lane

Scope: the ball's behaviour *while sitting in the plunger lane only* — resting position,
collision layer, z, holding edge, and what nudges it. The launch path itself
(`PlungerInputPressed` / `PlungerLaunchBall` / `PlungerFeedBall` / `SomeCounter`) is
deliberately untouched here and is being worked separately.

Nothing in this file was changed in the tree. It is an investigation only.

## Verdict

`control::UnstuckBall` (`reference/SpaceCadetPinball/control.cpp:1182`) throws the ball
every ~0.5 s while it rests in the lane, because the guard that is supposed to stop it —
"is the ball inside the plunger's bounds?" — is tested against
`control_plunger_tag` (`control.cpp:206`, `= {"plunger"}`), a group name Dragon's Keep
does not have. The tag's `Component` is null, the guard is dead, and the resting ball
reads as stuck.

`DRAGON.EXE`'s own `UnstuckBall` at **0x42c320** tests the plunger by the group Dragon
actually has, `s_plunger1`, and so returns immediately.

This is the unfinished half of `docs/engine-changes.md` section 8. That section moved the
*relaunch* (`control.cpp:1206`) off the dead `plunger` tag and onto
`TPinballTable::Plunger`. The *guard* at `control.cpp:1190` was left on the dead tag.

## Measurement

Harness: the prebuilt
`/nix/store/f94wgb4sjshnwhszj79g23w8kc05930n-space-cadet-pinball-fulltilt-dev/bin/SpaceCadetPinball`,
headless, `DRAGON.DAT` + `SOUND` staged in a scratch directory:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
HOME=$PWD/home XDG_DATA_HOME=$PWD/home/.local/share \
FT_AUTOPLAY=1 FT_HOLETEST=9999 timeout 40 .../SpaceCadetPinball
```

`FT_HOLETEST=9999` is out of range for `gHoles`, so `FtAccelDebug.cpp:137`'s drive block
never fires but its per-8-frame `DBG-chain` trace at `FtAccelDebug.cpp:150` still runs.
That gives a per-frame ball trace *without* perturbing the ball, which is why no
instrumentation had to be added.

One lane-rest phase, run 1, t = 2.74 s .. 9.68 s (6.94 s, 105 sampled frames):

| quantity | value |
|---|---|
| resting position | X ∈ [-6.9860, -6.8260], Y ∈ [12.8080, 12.8360] |
| X jitter amplitude | **0.1600** world units (0.43 ball diameters) |
| Y jitter amplitude | **0.0280** world units |
| z | **0.370**, constant, every frame |
| `CollisionMask` | **0x1**, constant — base playfield layer, correct |
| `CollisionComp` | **nullptr**, constant, every frame |
| `Speed` | min **0.0330**, max **0.9280**; **never reaches 0** |
| `StuckCounter` | climbs 0 → 19 → (>20) |
| nudge period | **0.6 s** nominal, 0.46–0.67 s observed, mean 0.488 s including doubled frames |

`StuckCounter` increments at t = 3.28, 3.88, 4.41, 5.08, 5.54, 5.61, 6.21, 6.68, 6.74,
7.34, 7.94, 8.54, 9.14. Every increment coincides with a `Speed` spike to 0.4–0.93 and an
X displacement of up to 0.16 — that *is* the jitter.

At t = 9.74 the counter passed 20, so `control.cpp:1199-1207` ran: the ball was
`Disable()`d, `MultiballCount` decremented, and `PlungerRelaunchBall` served a fresh ball
at the feed point (-6.958, 12.222), which then auto-fired at t = 10.01 at Speed 55.5.
So the same bug also silently destroys and re-serves the ball roughly every 7 s, and
auto-launches it. (The `MultiballCount--` at `control.cpp:1200` is netted back out by
`TPlunger.cpp:124`'s `MultiballCount++`, so the count does not drift.)

Reproducible. Independent second run, same 40 s: 95 lane-band samples, z always 0.370,
mask always 0x1, `CollisionComp` always null, Speed min 0.0160 never 0, X amplitude
0.1590, max `StuckCounter` 18, and respawns at the feed point at t = 2.01 (the legitimate
serve), **8.41 and 17.43** (two spurious `UnstuckBall` relaunches).

## Which edge holds the ball, and why the resting position is right

`s_plunger1` is **group index 321** in `DRAGON.DAT`. Its attributes, parsed straight out of
the DAT (`PARTOUT(4.0)RESOURCE`, per `reference/SpaceCadetPinball/partman.cpp`):

```
ShortValue = 200
ShortArray[6] = (100, 8, 1100, 55, 1101, 6)
FloatArray attr=600 n=7: [600.0, 2.0, -7.5028, 13.2176, -6.3954, 13.1946, -0.8]
FloatArray attr=601 n=5: [601.0, -6.9575, 12.2083, 0.0707, -0.2]
```

Attribute 600 is one `wall_type::Line` (count 2.0) from (-7.5028, 13.2176) to
(-6.3954, 13.1946) — the plunger face across the bottom of the lane.
`TEdgeSegment::install_wall` (`TEdgeSegment.cpp:41`) builds it, calls
`TLine::Offset(table->CollisionCompOffset)` and then `place_in_grid`, which merges the
*offset* endpoints into `TCollisionComponent::AABB`.

`CollisionCompOffset` is the ball radius (`TBall.cpp:78`), and `ball0`/`ball1`/`ball2` in
`DRAGON.DAT` all carry `attr=500 n=2: [500.0, 0.37]`, so **CollisionCompOffset = 0.37**.
That is also why z is 0.370: `TBall.cpp:79` sets `Position.Z = Radius`. The z in the lane
is correct and is not a layer bug.

With `maths::line_init`'s clockwise perpendicular (`maths.cpp:149`,
`PerpendicularC = {Direction.Y, -Direction.X}`, unit length), the offset line is

```
(-7.5105, 12.8477) -> (-6.4031, 12.8247)
AABB  X [-7.5105, -6.4031]   Y [12.8247, 12.8477]
```

The measured resting Y, 12.808–12.836, sits **on that line**. So the edge holding the ball
is the plunger's own face, offset inward by exactly one ball radius — which is where a
resting ball's centre belongs. The resting position is correct; only the nudging is wrong.

`control::CheckBallInControlBounds` (`control.cpp:1212`) widens the AABB by
`CollisionCompOffset / 2 = 0.185`:

```
X [-7.6955, -6.2181]   Y [12.6397, 13.0327]
```

Measured rest is inside on both axes. **The original's plunger guard would be TRUE and
`UnstuckBall` would return without touching the ball.**

Caveat on rigour: that AABB is *computed* from attribute 600 plus `TLine::Offset`, not
printed by the running engine. It is corroborated by the measured resting Y landing on the
computed offset line to within 0.017, but the direct settlement would be one
`fprintf` of `PinballTable->Plunger->AABB` in `TPlunger`'s constructor (not added — the
tree is not mine to edit).

Note the feed point (-6.9575, 12.2083) is *outside* that Y window (12.2083 < 12.6397).
That is consistent, not a problem: a freshly served ball is above Speed 0.8 as it rolls
down the lane, so `pb::timed_frame`'s inactivity gate never opens until it settles at
12.83, which is inside.

## The port's stuck machinery is faithful; only `UnstuckBall` deviates

`pb::timed_frame` (`pb.cpp:419`) matches `DRAGON.EXE` instruction for instruction:

| port | DRAGON.EXE | check |
|---|---|---|
| `!ball->ActiveFlag` | 0x4112d1 `cmp BYTE PTR [ecx+0x5],bl` | ActiveFlag +0x5 |
| `ball->HasGroupFlag` | 0x4112da `cmp DWORD PTR [ecx+0x153],ebx` | |
| `ball->CollisionComp` | 0x4112e6 `cmp DWORD PTR [ecx+0xf3],ebx` | CollisionComp +0xf3 |
| `ball->Speed >= 0.8f` | 0x4112f2 `fld [ecx+0xc3]` / 0x4112f8 `fcomp QWORD PTR ds:0x4372b0` | 0x4372b0 = **0.8** |
| `time_ticks - LastActiveTime > 500` | 0x41130f `sub eax,[ecx+0xd7]` / 0x411315 `cmp eax,0x1f4` | 0x1f4 = **500** |
| `Radius / 2.0f` | 0x41134a `fld [ecx+0x15b]` / 0x411350 `fdiv ds:0x4372b8` | 0x4372b8 = **2.0** |
| `StuckCounter++` | 0x4113a3 `inc DWORD PTR [ecx+0x157]` | StuckCounter +0x157 |
| `StuckCounter = 0` | 0x4113ab / 0x41143f `mov DWORD PTR [ecx+0x157],ebx` | |
| `UnstuckBall(*ball, …)` | 0x4113b1..0x4113be `call 0x42c320` | **unconditional, every frame** |
| `Radius * 2.0f` | 0x4113f6 / 0x4113fc `fmul ds:0x4372b8` | |

So the 500 ms gate and the per-frame `UnstuckBall` call are both correct. The whole
deviation is inside `UnstuckBall`.

## `DRAGON.EXE`'s `UnstuckBall` — 0x42c320

`CheckBallInControlBounds` is **0x42c290** (its body at 0x42c2c0..0x42c31c is the four
`fcomp`s against ball `Position` +0x9f / +0xa3, `ret 0x8`).

```
42c320:  push esi
42c321:  mov  eax,ds:0x43a714          ; a_flip1
42c32c:  call 0x42c290                 ; CheckBallInControlBounds
42c333:  jne  0x42c3ae                 ; -> return
42c335:  mov  eax,ds:0x43a724          ; a_flip3   <-- NOT CHECKED BY THE PORT
42c33c:  call 0x42c290
42c343:  jne  0x42c3ae
42c345:  mov  eax,ds:0x43a734          ; a_flip2
42c34c:  call 0x42c290
42c353:  jne  0x42c3ae
42c355:  mov  eax,ds:0x439d44          ; s_plunger1  <-- THE DEAD GUARD
42c35c:  call 0x42c290
42c363:  jne  0x42c3ae
42c365:  cmp  DWORD PTR [esi+0x157],0x14   ; StuckCounter > 20 ?
42c36c:  jle  0x42c396
42c370:  call 0x402c00                 ; TBall::Disable
42c381:  dec  DWORD PTR [eax+0x192]    ; MultiballCount--
42c387:  mov  ecx,DWORD PTR ds:0x439d44    ; s_plunger1 again
42c38f:  call DWORD PTR [esi+0x4]      ; vtable slot 1 = Message(0x3fb, 0.0)
42c396:  push 0x0 / 0x3f800000 / 0x42b40000 / 0x43bb98
42c3a9:  call 0x4029e0                 ; TBall::throw_ball(&{0,-1,0}, 90.0f, 1.0f, 0.0f)
```

Tag names come from `analysis/dump_tags.py DRAGON.EXE`:

```
0x00439d44  s_plunger1
0x0043a714  a_flip1
0x0043a724  a_flip3
0x0043a734  a_flip2
```

`0x43bb98` reads as the static `vector3 (0.0, -1.0, 0.0)`; `0x42b40000` = 90.0f,
`0x3f800000` = 1.0f. Both match the port's `throw_ball` call.

Two differences from the port, both in the same `if`:

1. **The plunger guard is tested by the wrong name.** 0x42c355 uses the same
   `s_plunger1` pointer the relaunch at 0x42c387 uses. The port's relaunch was already
   fixed to `TableG->Plunger`; the guard was not.
2. **`a_flip3` is not checked at all.** `DRAGON.EXE` makes **four**
   `CheckBallInControlBounds` calls; the port makes three.
   `CADET.EXE`'s `UnstuckBall` at 0x42ab40 makes three (`analysis/symbols-cadet.md:68`),
   which is where the port's three came from. Dragon's Keep has a real third flipper —
   `src/control_dragon.cpp:260` declares `tag_a_flip3 = {"a_flip3"}` and
   `src/dragon_part4.cpp:487` messages it — so a ball resting on `a_flip3` is also
   nudged when it should not be. Not the reported symptom, but the same omission.

`DRAGON.DAT` has **no group named `plunger`** (checked by scanning every `GroupName`
entry in the DAT), which is the direct proof that `control_plunger_tag.Component` is
null on this table.

## The exact change (not applied)

`reference/SpaceCadetPinball/control.cpp`, in `control::UnstuckBall`, lines **1188-1190**:

```cpp
	if (!(flip1 && CheckBallInControlBounds(ball, *flip1)) &&
		!(flip2 && CheckBallInControlBounds(ball, *flip2)) &&
		!(plunger && CheckBallInControlBounds(ball, *plunger)))
```

The plunger clause must come from the table's own plunger, which every table has, exactly
as `control.cpp:1206` already does for the relaunch — `TableG->Plunger` is a
`TPlunger*`, hence a `TCollisionComponent*`, so it is the right type for
`CheckBallInControlBounds` with no cast. And a third flipper clause is needed for
`a_flip3`, which has no tag in `control.cpp` (Dragon's lives in
`src/control_dragon.cpp:260`), so it has to reach `UnstuckBall` through the per-table
registry rather than a new hardcoded group name — a `TableRules` row in the shape of the
existing `ModeBeforeComponent` / `MultiplierIsValue` flags, or an extra entry in the
per-table arrays `docs/engine-changes.md` section 6 already made `make_links` walk.

Fixing only the plunger clause removes the reported symptom. The `a_flip3` clause is a
separate, smaller instance of the same omission and is cosmetically invisible until a ball
parks on that flipper.

## Settled negative: the plunger feed point has no omitted z

`docs/engine-changes.md`'s `TSink` (0x4185f6 / 0x4185fc) and `THole` (0x416929..0x416937)
bugs were both "the port read the wrong element of a float attribute and dropped a height
or a release mask". That shape does **not** recur here.

`TPlunger`'s constructor is **0x418800** (it stores the vtable `0x437700` at 0x418820).
Its attribute-601 read is the tail of the function:

```
4188d5:  push 0x259                     ; 601
4188ed:  call 0x40d8d0                  ; query_float_attribute
4188f2:  mov  ecx,DWORD PTR [eax]       ; arr[0]
4188f5:  mov  DWORD PTR [esi+0x156],ecx ; table->PlungerPosition.X
4188fb:  mov  edx,DWORD PTR [eax+0x4]   ; arr[1]
418901:  mov  DWORD PTR [esi+0x15a],edx ; table->PlungerPosition.Y
418909:  add  esp,0x4c
41890c:  ret  0xc
```

Two dwords, and then the function returns. `[table+0x156]`/`[table+0x15a]` sit directly
after `CollisionCompOffset` at `[table+0x152]`, which confirms the layout.
Attribute 601's remaining elements, **0.0707 and -0.2**, are never read on this path.

`TPlunger.cpp:71`'s `table->PlungerPosition = {floatArr[0], floatArr[1]}` is therefore
**exactly right** and needs no change. Corroborated by measurement: the served ball
appears at (-6.958, 12.222) against the DAT's (-6.9575, 12.2083).

The rest of the constructor also matches the port: 0x41887f's `0x42480000` = 50.0f default
`MaxPullback`, 0x4188b9's `ds:0x4373e8` = **8.0** (the `ListBitmap->size() * 8.0f`
divisor), 0x4188e3's `0x3ccccccd` = 0.025f `PullbackDelay`, 0x4188d0/0x4188dc/0x4188e0's
`0x3f000000` = 0.5f `Elasticity`/`Smoothness`, 0x418866's `0x4e6e6b28` = 1e9 `Threshold`,
and `[edi+0x96]` is `LauncherFlag`.

## What is not settled

- **The plunger's `AABB` as the engine actually computes it.** Computed above and
  corroborated to 0.017 by the measured resting Y, but not read out of a live object. One
  `fprintf` of `PinballTable->Plunger->AABB` in `TPlunger`'s constructor settles it.
- **Whether `Speed` is *supposed* to reach exactly 0 at rest.** It never does, in either
  run (floor 0.016–0.033). That is below every gate that matters (the 0.8 at 0x4112f8,
  and `pb.cpp:457`'s `Speed < 0.8f` time-step clamp), so it does not by itself cause
  anything, and the jitter is fully accounted for by `UnstuckBall`. Settling it would mean
  reading `TBall::Repaint`/the integrator's damping against `DRAGON.EXE`, which is out of
  this file's scope.
- **Whether `a_flip3` also needs a clause in `PIRATES.EXE`'s `UnstuckBall`.**
  `src/pirates_components.cpp:36` declares an `a_flip3` tag for Skulduggery too, but
  `PIRATES.EXE`'s `UnstuckBall` was not disassembled here. The equivalent dump of its
  `CheckBallInControlBounds` call count would settle it.
