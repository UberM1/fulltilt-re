# Dragon's Keep: holes, ramps, ramp exits and the layer chain

Empirical verification of three standing bug reports against DRAGON:

1. "si la pelota entra en los agujeros de los monos no vuelve a salir"
2. "una rampa, la de arriba de todo, no funciona"
3. "el render de la bola en los tuneles funciona extranio"

Nothing under `src/` or `reference/` was changed for this. Everything below was measured
with the already-built binary

    /nix/store/69miyi32xc2nfnfpsw658gjksjx593s4-space-cadet-pinball-fulltilt-dev/bin/SpaceCadetPinball

run headless (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, `HOME`/`XDG_DATA_HOME` in a
staged DRAGON run dir) under `timeout`, driven by the `FtAccelDebug.cpp` harness
(`FT_AUTOPLAY`, `FT_HOLETEST`, `FT_LAYERTEST`, `FT_SINKTEST`, `FT_SINKREPEAT`,
`FT_ACCELTEST`). Static claims are checked against `binaries/win95/DRAGON.EXE` with
`analysis/dump_components.py`'s `load()` plus `objdump -b binary -m i386 -M intel` over
`.text`.

Geometry is read straight out of `DRAGON.DAT` (PARTOUT(4.0)RESOURCE, 668 groups) with a
throwaway parser over `FieldTypes::FloatArray`, keyed the way `loader::query_float_attribute`
keys them (first float of the array).

## 0. Table orientation, so that "topmost" means something

`camera_info` (DAT group 2) for DRAGON:

    row0  1.0000   0.0000   0.0000   0.0000
    row1  0.0000  -0.8808   0.4735   4.3251
    row2  0.0000  -0.4735  -0.8808  25.3437
    d = -353.2735   zMin = 17.6036   zScaler = 3422.5708

`screen_y = y1 * d / z1 + centerY` with `y1 = -0.8808*y0 + ...` and `d < 0`, so screen y
grows with table y: **+Y is down the playfield**, which also agrees with gravity --
`TTableLayer` reads `GravityAnglY = pi/2`, so `GraityDirY = +sin(AngleX)*mult > 0`, and the
measured plunger rest position (-6.957, +12.2) drifts toward +Y. `d < 0` also mirrors X:
table +X draws on the *left* of the screen.

So **the top of the screen is the most negative Y**, and the drain is at about y = +15.5
(measured: last ball sample before `NO ACTIVE BALL` is (0.826, 15.534)).

## 1. The three holes: every one of them releases

Registered (startup, `DBG-hole reg`):

| idx | group name | centre | r | visual group | release mask (1304) | ZSetValue (408[2]) |
|---|---|---|---|---|---|---|
| 0 | `s_hole118` | (5.188, -5.238) | 0.482 | 0x1 | **0x4** (1304 = 2) | 0.482 |
| 1 | `s_hole101` | (4.987, 9.337) | 0.486 | 0x2 | **0x1** (1304 = 0) | 0.486 |
| 2 | `s_hole100` | (-5.077, 9.478) | 0.484 | 0x2 | **0x1** (1304 = 0) | 0.484 |

`s_hole100`/`s_hole101` are the symmetric pair at y = +9.4, x = -5.08 / +4.99 -- i.e. the
two flanking heads in the lower half of the playfield, which is what the bug report means
by "los agujeros de los monos". `s_hole118` is the quest-staging hole on the right.

### Measured: capture -> release

`FT_HOLETEST=<n>` teleports the ball to the hole centre on the hole's own collision group
and then traces it. Every hole captured and every hole released:

| hole | HIT | `DBG-holerel` | hold time | mask | z |
|---|---|---|---|---|---|
| `s_hole118` | t = 3.21 | t = 4.01 | **0.80 s** | 0x1 -> 0x4 | 0.370 -> 0.482 |
| `s_hole101` | t = 3.01 | t = 3.08 | **0.07 s** | 0x2 -> 0x1 | 0.370 -> 0.486 |
| `s_hole100` | t = 3.01 | t = 3.08 | **0.07 s** | 0x2 -> 0x1 | 0.370 -> 0.484 |

and the ball then leaves under its own weight -- no `DBG-STUCK` in any run:

* `s_hole118`: released onto layer 0x4, rode it for **2.29 s** and came back to 0x1 at
  (-7.061, 4.934) -- that is `a_sink15`'s ball position (-7.061, 4.677), i.e. the hole
  feeds the 0x4 lane which ends in that sink.
* `s_hole100`/`s_hole101`: released onto 0x1 after 0.02 s on the hole's layer and rolled
  down the playfield to the drain.

Three independent 150 s blind-autoplay runs and two 200 s traced runs (850 s of play
in total) add: **3 captures of
`s_hole118` in 200 s, 3 releases** (hold-to-0x1 round trips of 2.29 s / 2.49 s / 1.95 s),
`DBG-STUCK` count 0, `DBG-hole HIT` count == `DBG-holerel` count in every single log.

**Verdict on report 1: not reproducible on this build.** Both "monkey" holes release in
0.07 s and the quest hole in 0.80 s. The release does not depend on any rules handler:
`THole::Collision` always arms `Timer = timer::set(0, this, TimerExpired)` (FT capture time
is 0), `TimerExpired` sets `BallCapturedSecondStage`, and `THole::FieldEffect` then releases
as soon as `Position.Z <= ZSetValue` -- which is already true on entry, because the
playfield z (0.370 = ball radius) is *below* every hole's ZSetValue (0.482/0.484/0.486).

The one path that could still park a ball for good is the rules side, not the engine:
`QuestHoleControl` (src/dragon_part4.cpp, 0x00426ee0) answers `ControlBallCaptured` in
`ModeSkillShot` with `caller->MessageField = 2; return;` and in `ModeNormal` +
`MultiballCount == 1` by starting `ModeEggExchange` and returning -- neither arms a
release. They are safe *only* because the engine's own 0 s capture timer releases anyway.

### Static divergence that is real but is not this bug

DRAGON.EXE does **not** release from `FieldEffect`. At 0x00416b79, when the captured ball
reaches `ZSetValue`, it zeroes `Direction.Z/Y/X` and `Speed` (ball+0xbf/0xbb/0xb7/0xc3) and
then calls its own `Message` through the vtable with **0x3f8** and 0.0:

    416b79  mov  [esi+0xbf],0        ; Direction.Z = 0
    416b83  push 0                   ; value
    416b87  push 0x3f8               ; code  (FT 0x3f8 == upstream PlungerFeedBall = 1015)
    416b8c  mov  ecx,edi             ; this = the hole
    416ba2  mov  [esi+0xc3],0        ; Speed = 0
    416bac  call [eax+4]             ; this->Message(0x3f8, 0.0f)

The 0x3f8 arm of `THole::Message` is at 0x00416cea and it is **gated**: it builds an AABB
around the captured ball's position (+/- `table+0x152` scaled by the two constants at
0x4373b0/0x4373b8), calls 0x004130c0, and only releases when that returns 0.

0x004130c0 is "count active balls on a layer inside a box": it walks the table's ball list
(global 0x0043921c, `+0x13a` -> list, count at `+4`, data at `+8`), skips balls with
`ActiveFlag == 0` (`[ball+5]`), skips balls whose `CollisionMask` (`[ball+0xf7]`) does not
intersect the mask argument, and counts the ones whose `Position.X/Y` (`+0x9f`/`+0xa3`) lie
inside the rect. If the count is non-zero the hole does **not** release and re-arms through
0x004131a0 (`this+0xbe`, code 0x3f8 again).

The release body itself (0x00416da6-0x00416e08) matches the port one-for-one otherwise:
`BallCapturedFlag = 0`, `BallCapturedSecondStage = 0`, `Position.Z = ZSetValue` (hole+0x8e),
`CollisionMask = ` hole's release mask (hole+0x96), `CollisionComp = 0`, the hole's own ball
pointer (hole+0xba) cleared, sound, `control::handler(0x44, this)`.

So the port is missing two things, both verifiable in the binary:

* **no `0x3f8` / `PlungerFeedBall` arm in `THole::Message`.** `THole::Message` in DRAGON.EXE
  dispatches 0x43 (the `THoleReleaseBall` arm this port already restored), 0x3f8, 0x3fe and
  0x402; the port handles only `Reset` and 0x43. Anything in the table that feeds a ball by
  broadcasting 0x3f8 cannot make a Dragon hole give its ball back.
* **no occupancy gate.** Under multiball the port will drop a ball onto the release layer on
  top of a ball that is already standing there; the original waits and retries.

Neither can trap a ball on *this* build, because the port's inline `FieldEffect` release is
unconditional. Both are the kind of gap that produces exactly the reported symptom once
multiball or a feed is involved, so they are worth closing.

Also faithful-but-relocated: the port zeroes `Direction` and `Speed` in the release branch;
the original zeroes `Direction.X/Y` every frame of the descent (0x00416b24/0x00416b2e) and
`Direction.Z` + `Speed` at the moment it posts 0x3f8. Same end state.

## 2. Ramps and ramp exits

DRAGON.DAT has **six** `TRamp`s and **three** `TRampExit`s. Ranked by their topmost point
(most negative Y = highest on screen):

| group name | DAT group | plane x range | plane y range | ramp group | wall1 (1301) | wall2 (1302) | planes | 1303 | 1305 |
|---|---|---|---|---|---|---|---|---|---|
| `a_ramp5` | 289 | -7.54 .. -6.04 | **-13.84** .. -1.74 | 0x10 | 0x10 en=4 seg (-7.131,-12.727)->(-6.094,-12.754) | 0x8 en=4 seg (-6.627,-1.711)->(-7.664,-1.712) | 21 | absent | 0 |
| `a_ramp107` | 295 | -0.21 .. 2.55 | -11.43 .. -8.56 | 0x8 | 0x1 en=3 seg (0.929,-8.451)->(-0.316,-9.257) | 0x8 en=3 seg (0.695,-11.427)->(2.509,-10.241) | 10 | absent | 0 |
| `a_ramp6` | 293 | -7.02 .. -4.22 | -10.47 .. -5.16 | 0x4 | 0x1 en=2 seg (-4.072,-7.061)->(-5.449,-5.224) | 0x8 en=2 seg (-6.961,-10.338)->(-5.841,-10.488) | 12 | absent | 0 |
| `a_ramp103` | 291 | 2.71 .. 6.23 | -10.28 .. -5.35 | 0x2 | 0x1 en=1 seg (2.821,-5.331)->(2.733,-8.056) | 0x4 en=1 seg (4.952,-10.273)->(6.313,-10.233) | 11 | absent | 0 |
| `a_ramp4` | 287 | -4.17 .. 4.92 | -5.12 .. 2.66 | 0x4 | 0x4 en=2 seg (4.238,-5.144)->(4.905,-4.350) | 0x4 en=2 seg (-3.590,2.718)->(-4.197,1.877) | 14 | absent | 0 |
| `a_ramp104` | 285 | -7.46 .. -6.61 | 5.48 .. 7.36 | 0x2 | 0x8 en=1 seg (-7.589,5.523)->(-6.551,5.519) | 0x2 en=1 seg (-6.580,7.334)->(-7.617,7.323) | 8 | absent | 0 |

| ramp exit | DAT group | 1301 | group1 | group2 | segment | built |
|---|---|---|---|---|---|---|
| `s_rexit21` | 482 | 2 3 2 ... | 0x4 | 0x8 | (-0.466,**-14.703**)->(-0.238,-13.746) | yes |
| `s_rexit18` | 480 | 1 3 2 ... | 0x2 | 0x8 | (-0.478,-13.543)->(-1.996,-13.094) | yes |
| `s_rexit19` | 481 | 1 3 2 ... | 0x2 | 0x8 | (-4.030,-11.451)->(-4.962,-11.765) | yes |

Notes that fall straight out of the table:

* **Not one Dragon ramp has attribute 1303.** `Line1` is therefore null on all six, so
  `TRamp::Collision`'s `ControlCollision` (0x4a in the binary) arm is dead on this table --
  by data, not by port error.
* **Not one Dragon ramp has a non-zero 1305.** `BallZOffsetFlag` is false everywhere, so a
  wall crossing never sets the ball's z. See section 3.
* `a_ramp4`'s two walls are both the ramp's own group 0x4, so it has no edge on any other
  layer: it is only reachable from a ball already on 0x4, which is `a_ramp6`'s ramp group.
  The two are one continuous path.
* `a_ramp104` wall2 group == ramp group == 0x2, and `a_ramp107` wall2 group == ramp group
  == 0x8. On those segments `Line3` (built with `CollisionGroup`) and the per-plane wall
  edge (built with `Wall2CollisionGroup`) are **coincident and carry the same group**, so
  which one catches the ball is down to edge-manager order. Same in the original -- the
  ambiguity is in the data.

### Which ones ever fire, and to whom

Bindings, from `analysis/dragon-components.txt` (score_components, 75 entries) and
`analysis/dump_simple.py` (simple_components, 150 entries) -- i.e. from DRAGON.EXE itself:

| component | DRAGON.EXE binding | port binding | handler tests |
|---|---|---|---|
| `a_ramp103` | score_components[32] -> 0x004265e0 | `dragon::DragonRampControl` | `cmp [esp+4],0x56` at 0x004265e0 |
| `a_ramp6` | score_components[33] -> 0x00426740 | `dragon::TowerRampControl` | `cmp [esp+4],0x56` at 0x00426740 |
| `a_ramp107` | simple_components[58] @ 0x0043b578 | simple component | no ControlFunc at all |
| `a_ramp5` | **absent from both tables** | unbound | nothing |
| `a_ramp4` | **absent from both tables** | unbound | nothing |
| `a_ramp104` | **absent from both tables** | unbound | nothing |
| `s_rexit18/19/21` | absent from both tables | unbound | nothing |

`control::make_links` reports no `DBG-link UNLINKED` line on DRAGON, so every tag that is
bound did find its component.

`TRamp::Collision` in DRAGON.EXE at 0x004177d0 sends, confirmed instruction by instruction:

    417916  push esi / push 0x54 / call 0x423060     ; plane branch, only when the ball's
                                                     ; CollisionMask actually changed
    417953  push esi / push 0x4a / call 0x423060     ; Line1 (entry wall) -- dead on DRAGON
    4179a4  push esi / push 0x55 / call 0x423060     ; Line2 == wall1
    4179e3  push esi / push 0x56 / call 0x423060     ; Line3 == wall2

The port sends 0x4a, 0x55 and 0x56 and drops 0x54. Both bound handlers test **only 0x56**,
so: **a Dragon ramp notifies its rules handler on the wall2 crossing and on nothing else**,
and that is exactly what the port does. 0x55 is reachable but no Dragon handler wants it.

`TRampExit::Collision` at 0x0041b720 sends 0x56 on `Edge1` and 0x55 on `Edge2`
(0x41b780 / 0x41b7a0) and then sets `CollisionMask` from `this+0x7a` / `this+0x82` -- the
port's arms and order match.

### "la rampa de arriba de todo" is `a_ramp5`, and it does work

`a_ramp5` is the topmost ramp by a wide margin: its planes reach y = -13.84 against
-11.43 for the next one. (The single highest piece of collision geometry on the table is
`s_rexit21` at y = -14.703, but that is a ramp *exit*, not a ramp.)

`a_ramp5` is the left-edge tower track. Driving a ball into `a_sink16` (group 0x10, the
cup at the top, ball position (-6.563,-13.524)) puts it on layer 0x10, and the full
traverse is **measured end to end**:

    t=3.08  (-6.592,-13.546) z=?      mask=0x10   ejected onto the ramp
    t=3.28  (-6.591,-12.581) z=2.350  mask=0x10
    t=3.61  (-6.715,-10.050) z=3.146  mask=0x10   ramp crest
    t=4.01  (-6.936, -4.735) z=1.880  mask=0x10
    t=4.14  (-7.014, -1.778) z=2.552  mask=0x10
    t=4.21  (-7.083, -1.368) z=2.552  mask=0x8    <- a_ramp5 wall2 (seg y=-1.712, group 0x8)
    t=4.81  (-7.029,  4.972) z=2.552  mask=0x8
    t=4.88  (-7.014,  6.389) z=2.092  mask=0x2    <- a_ramp104 wall1 (seg y=5.52, group 0x8)
    t=5.14  (-5.077,  9.478) z=1.306  mask=0x2    <- captured by s_hole100
    t=5.21  (-5.077,  9.498) z=0.484  mask=0x1    <- released to the playfield
    t=6.48  ( 0.826, 15.534) z=0.484  mask=0x1    <- drain

1.06 s of ramp, 12.1 table units of descent, a clean wall2 hand-off to 0x8, a clean
hand-off to `a_ramp104` and then out through the gargoyle hole. The whole left tunnel chain
**0x10 -> 0x8 -> 0x2 -> hole -> 0x1 works**.

What `a_ramp5` does *not* do, and cannot:

* **It can never score or trigger anything.** It is bound to no handler -- and that is
  faithful: it appears in neither `score_components[75]` nor `simple_components[150]` in
  DRAGON.EXE. Its 0x55/0x56 notifications reach `control::handler`, find no `ControlFunc`
  for the component, and die there. Same for `a_ramp4`, `a_ramp104` and all three rexits.
  `a_ramp107` is a simple component, which also has no `ControlFunc`.
* **The uphill entry was never observed.** Entry onto 0x10 requires a ball already on
  layer 0x8 crossing `a_ramp5`'s wall2 segment at y = -1.712 from below, against gravity:
  the per-plane loop does install that segment as a plane edge on group 0x8
  (`wall2Enabled = 4`, non-zero), so the edge exists and the mechanism is the one that was
  measured working in reverse. But in 850 s of autoplay (2 x 200 s traced, 3 x 150 s blind) the
  ball reached mask 0x10 exactly **zero** times, and the explicit attempt
  (ball placed on 0x8 at `a_sink18`, y = +3.92, speed 8) fell back down: climbing 5.6 units
  against the measured ~20 u/s^2 needs about 15 u/s and the harness cannot aim.
  **This is not settled.** The geometry is there; no run has shown a ball using it.

So report 2 is half-confirmed: the topmost ramp is physically live and carries a ball
correctly, but it is inert as a rules object, and no measurement shows a ball entering it
from below.

### What the two bound ramp handlers actually did

Over 200 s of traced autoplay the ball entered both scoring ramps from the playfield and
fell back out of both without reaching the exit wall, so **neither handler ever fired**:

    t= 95.81  0x1 -> 0x4 at (-5.701,-7.492)   a_ramp6  (tower ramp) entered
    t= 96.67  0x4 -> 0x1 at (-4.980,-5.265)   fell back out after 0.86 s
    t=158.09  0x1 -> 0x2 at ( 3.401,-6.520)   a_ramp103 (dragon ramp) entered
    t=158.42  0x2 -> 0x1 at ( 2.418,-7.229)   fell back out after 0.33 s

The entry side of both is therefore verified (`wall1` group 0x1 -> ramp group), and the
exit side is verified on `a_ramp5` (same code path, same `Line3` branch). Blind flipper
flapping simply never completed a ramp. `ControlRampWall2Crossed` reaching
`DragonRampControl`/`TowerRampControl` is **not yet directly measured**.

Ramp exits that did fire, with the mask transition the harness recorded:

    DBG-hit t= 75.97 rexit s_rexit21  mask 0x4 -> 0x8
    DBG-hit t= 76.81 rexit s_rexit18  mask 0x8 -> 0x2

`s_rexit19` was never hit in any run.

## 3. The tunnel question: z and CollisionMask, measured

Reference values: ball radius / playfield z = **0.370**. Layers in use on DRAGON: 0x1
(playfield), 0x2, 0x4, 0x8, 0x10.

The measured z along the left chain is in the trace above. The shape of it is the answer:

* **On a ramp** (`CollisionFlag == 1`) z is recomputed every frame by
  `TBall::Repaint` from the plane's `BallCollisionOffset`:
  z = off.X*x + off.Y*y + radius + off.Z. On `a_ramp5` that sweeps **2.350 -> 3.146 ->
  1.880 -> 2.552** over the traverse, which is the ramp's own profile (its planes'
  `BallCollisionOffset.Z` spans -3.169 .. 7.084).
* **Off the planes** z **freezes**. At the wall2 crossing `TRamp::Collision` takes the
  `else` branch, sets `CollisionFlag = 0`, and -- because `1305` is 0 on every Dragon
  ramp -- skips `Position.Z = Radius + Wall2BallOffset`. Nothing else writes z. Measured:
  **z = 2.552 held for 0.67 s and 6.3 table units of travel on layer 0x8**
  (y = -1.37 -> +4.97), then 1.706 held across layer 0x2, then 0.484 held from the hole
  release all the way to the drain. A freshly launched ball is at 0.370; this one rolls the
  main playfield drawn at 0.484.
* `TRampExit::Collision` changes the layer and touches z not at all.

`TBall::Repaint` picks the ball sprite by `index` = first `VisualZArray[index] <= zDepth`
and hands `zDepth` to `SpriteSetBall` for the depth buffer, so a frozen z means a ball
drawn at the wrong size and tested against the table's zMap at the wrong depth for the
whole stretch between two ramps -- which is the reported "render extranio en los tuneles",
and it is a *per-stretch constant* error, not noise.

Is it a port bug? **No, as far as the binary says.** The three ingredients are all
confirmed in DRAGON.EXE / DRAGON.DAT:

* `1305` really is 0 for all six ramps (read out of the DAT).
* DRAGON.EXE's wall branches really do guard the z store on it:
  `cmp [esi+0x86],0 / je` at 0x00417989 (Line2) and 0x004179c8 (Line3).
* DRAGON.EXE's wall branches really do clear the ramp flag: `mov [ecx+0x14f],0` at
  0x00417961 (= `ball->CollisionFlag = 0`, same offset the plane branch sets to 1).

The port's only writers of `Position.Z` are `TBall` ctor/`Reset` (= radius), `TRamp`
(plane + the two guarded wall stores), `THole` (descent + `ZSetValue`), `TKickout` and
`TPinballTable`. So nothing restores radius when a ball comes back to layer 0x1 from a
hole or a wall crossing, in the port *or* in upstream. If the real game looks right here,
the missing piece is a z writer somewhere else in the FT engine that this port has not
found -- not the ramp code.

### One genuine divergence, found while checking the above

DRAGON.EXE's `TRamp::Collision` sets `[ball+0x9b] = 1` in **all three** branches
(0x00417917 plane, 0x00417979 Line2, 0x004179b8 Line3). The port sets nothing there.

`ball+0x9b` is `CollisionDisabledFlag`: `THole::Collision` at 0x004169d1 sets the same
field in the one place upstream's `THole::Collision` sets `ball->CollisionDisabledFlag =
true`, and `TRampExit::Collision` at 0x0041b78b/0x0041b7ae sets it in the one place the
port's `TRampExit::Collision` sets it -- two independent one-for-one correspondences. (The
arithmetic also works: `Position` is at `ball+0x9f`, and the field is the dword immediately
below it.)

Consequence: in DRAGON.EXE a ramp collision ends the ball's edge scan for that frame; in
this port it does not, so the ball can hit a second edge -- including an edge of the layer
it has just left -- in the same frame as a ramp wall crossing. That is a plausible second
contributor to odd behaviour at layer boundaries, and it is a one-line fix in `TRamp.cpp`.
I did not reproduce a specific visible artefact from it, so it is reported as a divergence,
not as a diagnosis.

## 4. Summary

| report | verdict |
|---|---|
| holes keep the ball | **not reproducible.** 0.07 s release for `s_hole100`/`s_hole101`, 0.80 s for `s_hole118`; HIT count == release count in every log; no `DBG-STUCK`. Two structural gaps remain (no 0x3f8/`PlungerFeedBall` arm, no occupancy gate) that can re-trap a ball under multiball or a ball feed. |
| topmost ramp dead | **partly.** The topmost ramp is `a_ramp5` (planes to y = -13.84). It carries a ball correctly downhill, measured end to end (1.06 s, 0x10 -> 0x8). It is bound to no handler at all -- faithfully, it is in neither component table of DRAGON.EXE -- so it can never score. Its uphill entry edge exists in the data but was never exercised: **unsettled**. |
| tunnel render | **confirmed and explained.** Ball z freezes at the last ramp-plane value for the whole inter-ramp stretch (measured 2.552 for 6.3 units on 0x8, 1.706 on 0x2, 0.484 for the rest of the ball on 0x1). Cause is 1305 = 0 on all six ramps plus `CollisionFlag = 0` at wall crossings, both confirmed in DRAGON.EXE at 0x00417961/0x00417989/0x004179c8. Not a port bug by the binary; if the original looks right, an unfound z writer is missing. |

### Not settled by the binary

* Whether anything in the FT engine broadcasts 0x3f8 to holes (the arm exists at
  0x00416cea; its callers were not traced).
* The identity of 0x004131a0, the hole's re-arm path for a refused release (it is called
  with `this+0xbe`, `(hole, 0x3f8, 0)`; whether that is a timer or a message queue was not
  determined).
* Whether a ball can climb onto `a_ramp5` from layer 0x8 in real play.
* Whether `ControlRampWall2Crossed` reaches `DragonRampControl` / `TowerRampControl`: the
  code path is proven on `a_ramp5`, but no run completed either scoring ramp.
