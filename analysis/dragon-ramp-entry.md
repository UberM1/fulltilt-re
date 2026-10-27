# Dragon's Keep: how `a_ramp5` is entered, and who writes the ball's z

Closes the two questions `analysis/dragon-holes-ramps.md` left open, and that
`docs/integration.md` still lists as open:

* *"ademas una rampa, la de arriba de todo, no funciona"* — `a_ramp5`.
* *"el render de la bola en los tuneles funciona extranio"* — the frozen z.

**Both have the same root cause, and it is a port bug: `TSink::TimerExpired` drops two of
the four things the original's sink eject does.** A second, independent port bug in
`THole`'s constructor accounts for the rest of the z error, down to the fourth decimal.

Nothing under `src/` or `reference/` was changed. Ground truth is
`binaries/win95/DRAGON.EXE` (objdump over `.text`, via `analysis/dump_components.py`'s
`load()`) and `binaries/win95/DRAGON.DAT` (a throwaway `PARTOUT(4.0)RESOURCE` parser over
`FieldTypes::FloatArray` / `ShortArray`, keyed the way `loader::query_float_attribute` and
`loader::query_visual` key them). The live measurement is the prebuilt
`/nix/store/2v2fsn7qv50qs93z2pghh77gah00d9c1-space-cadet-pinball-fulltilt-dev/bin/SpaceCadetPinball`,
headless, driven by `FtAccelDebug.cpp`.

## 0. Vtables used below, pinned by their type getter

Eight-slot vtables, slot 0 the component-type getter, 1 `Message`, 6 `Collision`,
7 `FieldEffect`:

| vtable | slot 0 | type | class | `Message` | `Collision` |
|---|---|---|---|---|---|
| 0x437620 | 0x41aec0 -> 0x3f4 | 1012 | `TKickout` | 0x4163a0 | 0x4160d0 |
| 0x437640 | 0x41aee0 -> 0x412 | 1042 | `TMagnet` | 0x4166c0 | 0x4165a0 |
| 0x437660 | 0x41aef0 -> 0x3fe | 1022 | `THole` | 0x416c70 | 0x4169c0 |
| 0x437680 | 0x41af10 -> 0x3fd | 1021 | `TRamp` | 0x4135b0 | 0x4177d0 |
| 0x4376c0 | 0x41af50 -> 0x3f9 | 1017 | `TSink` | 0x418650 | 0x4184a0 |

`THole` 0x437660 and `TRamp` 0x437680 agree with the addresses already in
`dragon-holes-ramps.md`, which is the cross-check that the block is being read correctly.

## 1. Question 1: `a_ramp5` is reachable in normal play, and the port breaks the entry

### 1.1 Only three objects on DRAGON carry layer 0x10 at all

Walking `table_objects`' 1025 short array (665 entries, 332 type/group pairs) and
computing `visual.CollisionGroup` the way `query_visual` does (`602` pairs, `|= 1 <<
value`, defaulting to 1):

```
1000 TWall   grp 290  cg=0x10  (unnamed)
1021 TRamp   grp 289  cg=0x10  a_ramp5
1017 TSink   grp 560  cg=0x10  a_sink16
```

So layer 0x10 **is** `a_ramp5`, plus its side wall and one sink. Nothing else on the table
can touch it.

### 1.2 Geometry alone cannot feed it — confirmed, 1303 is absent on all six ramps

Read straight out of `DRAGON.DAT`:

| ramp | cg | 1301 (wall1) | 1302 (wall2) | 1303 | 1305 |
|---|---|---|---|---|---|
| `a_ramp5` | 0x10 | grp **0x10** en=4 off=-0.8 | grp 0x8 en=4 off=-0.8 | **ABSENT** | 0 |
| `a_ramp104` | 0x2 | grp 0x8 en=1 | grp 0x2 en=1 | ABSENT | 0 |
| `a_ramp4` | 0x4 | grp 0x4 en=2 | grp 0x4 en=2 | ABSENT | 0 |
| `a_ramp103` | 0x2 | grp 0x1 en=1 | grp 0x4 en=1 | ABSENT | 0 |
| `a_ramp6` | 0x4 | grp 0x1 en=2 | grp 0x8 en=2 | ABSENT | 0 |
| `a_ramp107` | 0x8 | grp 0x1 en=3 | grp 0x8 en=3 | ABSENT | 0 |

`TRamp`'s per-plane edge loop gives a plane edge the ramp's own `CollisionGroup` unless the
edge coincides with the wall1 or wall2 endpoints, in which case it gets that wall's group.
`a_ramp5`'s **wall1 group equals the ramp's own group (0x10)**, so that edge is unreachable
from any other layer. The only cross-layer edge `a_ramp5` has is the wall2 edge, group 0x8,
at y = -1.712 — the *bottom* of the track, the one the measured traverse leaves by.
Entering there means climbing 12.1 units against gravity.

And `1303` being absent means `Line1` is null, so `TRamp::Collision`'s `edge == Line1` arm
(0x4a at 0x417953) is dead on every Dragon ramp — by data, as the previous pass said.
No ramp attribute can therefore put a ball onto 0x10.

### 1.3 The designed feed is `a_sink16`, and the original's sink eject re-layers the ball

`TSink`'s constructor at **0x418390** reads two fields the port does not have:

```
418405  push 0x515            ; 1301
41840a  ...
418425  call 0x40d8d0         ; query_float_attribute(groupIndex, 0, 1301)
41842a  test eax,eax
41842c  jne  0x418437
41842e  mov  eax,[esp+0x44]   ; <- visual.CollisionGroup
418432  mov  [esi+0x76],eax   ;    sink+0x76 = visual.CollisionGroup   (1301 absent)
418435  jmp  0x418458
418437  fld  [eax]            ;    1301[0]
41844c  call 0x42c7c8         ;    floor -> al
418453  shl  ebx,cl
418455  mov  [esi+0x76],ebx   ;    sink+0x76 = 1 << floor(1301[0])     (1301 present)

418458  push 0x259            ; 601
418460  call 0x40d8d0
418465  mov  ecx,[eax]        -> [esi+0x82]   BallPosition.X   = 601[0]
418474  mov  edx,[eax+0x4]    -> [esi+0x86]   BallPosition.Y   = 601[1]
41847e  mov  eax,[eax+0xc]    -> [esi+0x7a]   BallPosition.Z   = 601[3]   <-- index 3
418467  push 0x197            ; 407
418484  call 0x40d8d0
41848e  mov  [esi+0x7e],ecx   ; TimerTime
```

So `sink+0x76` is a **release layer** and `sink+0x7a` a **release z**. `TSink::TimerExpired`
at **0x418500** then uses both, right after `AddBall`:

```
4185c1  call 0x402c20         ; TPinballTable::AddBall(BallPosition.X, .Y)
4185e9  mov  eax,[esi+0x76]
4185ec  mov  [edi+0x9b],1     ; ball->CollisionDisabledFlag = true
4185f6  mov  [edi+0xf7],eax   ; ball->CollisionMask = sink->ReleaseMask      <-- MISSING
4185fc  fld  [esi+0x7a]
4185ff  fadd [edi+0x15b]      ; + ball->Radius
418605  fstp [edi+0xa7]       ; ball->Position.Z = Radius + sink->BallPos.Z  <-- MISSING
418629  call 0x4029e0         ; throw_ball
418643  call 0x423060         ; control::handler(0x46, this)
```

`AddBall` at **0x402c20** writes `[ball+0xf7] = 1` and `[ball+0xa7] = [ball+0x15b]`, i.e.
mask 1 and z = Radius — identical to the port's `TPinballTable::AddBall`. The two stores at
0x4185f6 / 0x418605 are the whole mechanism by which a Full Tilt sink puts a ball on a
tunnel. **The port's `TSink::TimerExpired` has neither.**

### 1.4 All eleven Dragon sinks, from the DAT

| sink | cg | 1301 | release mask | 601 `(x, y, ?, z)` | eject z = 0.370 + 601[3] | 407 |
|---|---|---|---|---|---|---|
| `a_sink1` | 0x1 | absent | 0x1 | (2.460, 1.530, 0.075, 0.000) | 0.370 | 2.0 |
| `a_sink2` | 0x1 | absent | 0x1 | (4.847, 3.101, 0.075, 0.000) | 0.370 | 2.0 |
| `a_sink4` | 0x1 | absent | 0x1 | (2.944, -0.951, 0.075, 0.000) | 0.370 | 2.0 |
| `a_sink9` | 0x1 | 0 | 0x1 | (-6.563, -13.524, 0.071, 0.000) | 0.370 | 2.0 |
| `a_sink10` | 0x8 | 3 | **0x8** | (-6.563, -13.524, 0.071, 2.165) | **2.535** | 2.0 |
| `a_sink13` | 0x2 | 1 | **0x2** | (-7.146, -0.970, 0.071, 1.268) | **1.638** | 2.0 |
| `a_sink15` | 0x4 | 2 | **0x4** | (-7.061, 4.677, 0.071, 0.837) | **1.207** | 2.0 |
| `a_sink16` | 0x10 | 4 | **0x10** | (-6.563, -13.524, 0.071, 1.983) | **2.353** | 0.5 |
| `a_sink17` | 0x8 | 3 | **0x8** | (-7.146, -0.970, 0.071, 2.199) | **2.569** | 0.5 |
| `a_sink18` | 0x8 | 3 | **0x8** | (-7.061, 4.677, 0.071, 2.196) | **2.566** | 0.5 |
| `a_sink19` | 0x8 | 3 | **0x8** | (1.538, -10.750, 0.075, -0.800) | **-0.430** | 2.0 |

Ball radius 0.370 (attribute 500 of `ball0`), which is also
`TPinballTable::CollisionCompOffset` (`TBall` ctor sets it; original at 0x402740,
`*(table+0x152) = ball+0x15b`).

Three sinks share **one** ball-return point, (-6.563, -13.524), at the top of the left
tower: `a_sink9` releases it onto the playfield 0x1, `a_sink10` onto the 0x8 tunnel, and
`a_sink16` onto **0x10, which is `a_ramp5`**. That is the diverter the previous pass was
looking for: not a `TGate`, not a `TDiverter` — one swallow point and three ejectors on
three layers, and the rules pick which one fires.

### 1.5 Which handler picks `a_sink16`: `a_sink9`'s and `a_sink10`'s, on every path

`a_sink16` is `simple_components[64]` at 0x0043b590 — a simple component, so it has no
`ControlFunc` of its own and nothing re-arms it but another handler. (`a_ramp5` really is
in neither table; that part of the previous pass stands.) Its tag is 0x0043a9cc, resolved
by `analysis/dump_tags.py`.

`a_sink9` -> `score_components[55]` -> **0x004274d0** (`dragon::TowerLock3EntranceAControl`)
contains three copies of:

```
42754b  push 0xbf800000       ; -1.0f  -> TSink::Message uses TimerTime (0.5)
427550  mov  eax,ds:0x43a9cc  ; a_sink16
427555  mov  eax,[eax]        ; vtable
427557  push 0x41             ; TSinkResetTimer
427559  mov  ecx,ds:0x43a9cc
42755f  call [eax+0x4]        ; a_sink16->Message(TSinkResetTimer, -1.0f)
```

`a_sink10` -> `score_components[56]` -> **0x00427680**
(`dragon::TowerLock3EntranceBControl`) repeats it at 0x427644 and 0x427661.
`src/dragon_part3.cpp:764` and `:826` already reconstruct exactly this — every branch of
both handlers ends in `a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f)`. The rules
side is correct; it is the engine that loses the layer.

So `a_ramp5` is entered on **every tower-lock-3 reject**, which is the common case: it is
the only way the ball leaves that trap when tower 3 is not armed. Not an uphill shot at
all — the previous pass was looking for the wrong mechanism.

### 1.6 Measured, live, on the current build

`FT_SINKTEST=3` (= `a_sink9`, whose collision group is 0x1, so the harness' own
`bb->CollisionMask = k.Group` at `FtAccelDebug.cpp:179` sets nothing artificial):

```
DBG-sink  reg a_sink9   aabb[x -7.21..-5.91 y -13.64..-13.62] group=0x1  timerTime=2.0000 ballPos=(-6.563,-13.524)
DBG-sink  reg a_sink10  aabb[x -7.25..-5.85 y -13.54..-13.51] group=0x8  timerTime=2.0000 ballPos=(-6.563,-13.524)
DBG-sink  reg a_sink16  aabb[x -7.24..-5.94 y -13.60..-13.58] group=0x10 timerTime=0.5000 ballPos=(-6.563,-13.524)

t= 3.08  ball0 pos=(-6.561,-13.565) spd=6.452 mask=0x1
         DBG-sink  HIT   a_sink9   swallow
         DBG-sink  MSG   a_sink16  TSinkResetTimer value=0.5000 timerTime=0.5000 -> id=220
t= 3.48  DBG-sink  EXP   a_sink16  ballPos=(-6.563,-13.524) occupied=0
         DBG-sink  EJECT a_sink16  dir=(-0.124,0.992,0.000) s1=10.000
t= 3.61  ball0 pos=(-6.594,-13.268) spd=10.424  mask=0x1      <-- should be 0x10
t= 3.74  ball0 pos=(-6.756,-11.727) spd=12.661  mask=0x1
t= 4.01  ball0 pos=(-7.000, -7.757) spd=16.866  mask=0x1
t= 4.41  ball0 pos=(-4.646, -1.259) spd=19.196  mask=0x1
t= 5.08  ball0 pos=( 0.003, 13.895) spd=28.204  mask=0x1
t= 5.21  NO ACTIVE BALL
```

The rules fire exactly as reconstructed, `a_sink16` ejects on cue 0.40 s later — and the
ball comes back **on mask 0x1 at z = 0.370** instead of mask 0x10 at z = 2.353. It then
free-falls the open playfield from the top-left corner to the drain in 1.60 s, never
touching `a_ramp5`. In the original it rides the tower track down: the 1.06 s / 12.1 unit
traverse `dragon-holes-ramps.md` already measured end to end with the mask forced by hand.

### 1.7 Verdict on question 1

**The complaint is "it cannot be shot", and it is a real port defect.** The ramp scores
nothing, and that part is faithful — `a_ramp5` is in neither `score_components[75]` nor
`simple_components[150]`. But the ramp is *reachable*, on the commonest exit from tower
lock 3, and the port never puts a ball on it.

Name of the defect: **`TSink::TimerExpired` does not re-layer or re-height the ejected
ball.** It affects 7 of Dragon's 11 sinks (`a_sink10`, `13`, `15`, `16`, `17`, `18`, `19`),
i.e. every sink that feeds a tunnel. Layers 0x2, 0x4, 0x8 and 0x10 lose their only
rules-driven entry.

### 1.8 The change that follows — do not make it here

`reference/SpaceCadetPinball/TSink.h`: two new fields, e.g.

```cpp
int ReleaseMask;        // sink+0x76
float BallPositionZ;    // sink+0x7a
```

`reference/SpaceCadetPinball/TSink.cpp`, `TSink::TSink` (around line 30, where 601 is
already read): read them, matching 0x418405-0x418481 —

```cpp
auto layerArr = loader::query_float_attribute_opt(groupIndex, 0, 1301);
ReleaseMask = layerArr ? 1 << static_cast<int>(floor(layerArr[0])) : visual.CollisionGroup;
BallPositionZ = floatArr ? floatArr[3] : 0.0f;   // same 601 array, index 3
```

`reference/SpaceCadetPinball/TSink.cpp`, `TSink::TimerExpired`, in the `else` branch
immediately after `AddBall` and before `throw_ball`, matching 0x4185e9-0x418605 —

```cpp
ball->CollisionDisabledFlag = true;          // already there
ball->CollisionMask = sink->ReleaseMask;
ball->Position.Z = ball->Radius + sink->BallPositionZ;
```

Order matters only in that both must land after `AddBall`, which resets mask to 1 and z to
`Radius`. The `1 << value` form is Full Tilt's; if a 3DPB guard is wanted, `THole.cpp`'s
`pb::FullTiltMode` precedent is the shape to copy — but note CADET's own sinks all take
the `1301`-absent default, so nothing in Space Cadet changes either way.

## 2. Question 2: every writer of the ball's z in DRAGON.EXE, exhaustively

### 2.1 The enumeration

Every store to `ball+0xa7` (`Position.Z`) in `DRAGON.EXE`'s `.text` — fourteen, all of
them:

| address | function | what it writes |
|---|---|---|
| 0x4027e5, 0x402839 | `TBall` ctor 0x402740 | 408[3], then `+= Radius` |
| 0x40299f | `TBall::throw_ball` region 0x402920.. | throw setup |
| 0x402ae2 | `TBall::Message(0x402)` 0x402a90 | `Radius` |
| **0x402b47** | **`TBall::Repaint` 0x402b10** | **plane recompute, guarded** |
| 0x402d21 | `TPinballTable::AddBall` 0x402c20 | `Radius` |
| 0x41612f | `TKickout::Collision` 0x4160d0 | `CollisionBallSetZ` (old z saved to comp+0xa6) |
| 0x4162a1 | `TKickout::TimerExpired` 0x416260 | restores the saved z |
| 0x416b1e | `THole::FieldEffect` 0x416ab0 | the descent |
| 0x416dba | `THole::Message(0x3f8)` 0x416c70 | `ZSetValue` (hole+0x8e) |
| 0x4178fe | `TRamp::Collision` plane branch | plane `BallCollisionOffset` |
| 0x41799e | `TRamp::Collision` Line2 | guarded, `cmp [esi+0x86],0` at 0x417989 |
| 0x4179dd | `TRamp::Collision` Line3 | guarded, `cmp [esi+0x86],0` at 0x4179c8 |
| **0x418605** | **`TSink::TimerExpired` 0x418500** | **`Radius + sink+0x7a` — absent in the port** |

That is the complete set. Two of the task's candidates are therefore **ruled out by the
binary**:

* **`TRampExit` (type 1035), `Collision` at 0x41b720: never writes z.** No store to
  `ball+0xa7` exists anywhere in 0x41b7xx. The port is right to leave z alone there.
* **`TTableLayer`: never writes z.** It has no entry in the list at all. Its only ball
  interaction is `FieldEffect`, which writes the gravity vector.

And the freeze itself is **faithful**:

```
402b10  TBall::Repaint
402b18  cmp  [ecx+0x14f],0          ; CollisionFlag
402b1f  je   0x402b4d               ; <- skips the whole z recompute
402b21  fld  [esi+0x147]            ; CollisionOffset.Y
402b27  fmul [esi+0xa3]             ;   * Position.Y
402b2d  fld  [esi+0x143]            ; CollisionOffset.X
402b33  fmul [esi+0x9f]             ;   * Position.X
402b39  faddp
402b3b  fadd [esi+0x15b]            ; + Radius
402b41  fadd [esi+0x14b]            ; + CollisionOffset.Z
402b47  fstp [esi+0xa7]             ; Position.Z
```

byte-for-byte the port's `TBall::Repaint` (TBall.cpp:83-91), and `TRamp::Collision` clears
`CollisionFlag` at every wall crossing (0x417961). With `1305 = 0` on all six ramps the two
guarded wall stores never run. So **z freezing across a tunnel is the original's own
behaviour**, and the port reproduces it correctly. That much of the previous pass stands.

What was wrong was not the freeze. It was the *value* frozen, and that comes from two
places the port gets wrong.

### 2.2 Unaccounted writer #1: the sink eject (section 1.3)

Everything in section 1 applies here too. In the original, a ball entering a tunnel from a
sink arrives at the tunnel's own height; in the port it arrives at 0.370. The DAT's design
heights for layer 0x8 are 2.535 / 2.569 / 2.566 (`a_sink10` / `17` / `18`), which is also
why 2.552 — the frozen value `dragon-holes-ramps.md` measured on 0x8 after `a_ramp5`'s
wall2 hand-off — is right to within 0.02: **the ramp's last plane z and the tunnel's own
sink height agree.** The freeze is not the artefact. Arriving at 0.370 is.

### 2.3 Unaccounted writer #2: `THole::ZSetValue` is read from the wrong index

`THole`'s constructor at **0x416740**:

```
4168f6  push 0x198            ; 408
4168fb  push 0
4168fd  push edi
416918  call 0x40d8d0         ; query_float_attribute(groupIndex, 0, 408)
41691d  mov  edx,[esp+0x70]   ; the table
416929  fld  [edx+0x152]      ; table->CollisionCompOffset  (= ball Radius, 0.370)
41692f  fadd [eax+0xc]        ; + 408[3]                    <-- index 3
416937  fstp [esi+0x8e]       ; hole->ZSetValue
...
416971  mov  [esi+0x96],edi   ; hole->CollisionMask = 1 << floor(1304[0])
```

The port, `reference/SpaceCadetPinball/THole.cpp:61-62`:

```cpp
auto zArr = loader::query_float_attribute_opt(groupIndex, 0, 408);
ZSetValue = zArr ? zArr[2] : 0.0f;
```

Wrong index **and** no `CollisionCompOffset` base. Index 2 of these `(x, y, ?, z)` arrays is
the object's own radius, not a height — which is why the harness prints `r` and `z` as the
same number. `TBall`'s ctor (TBall.cpp:46) and `TKickout`'s (TKickout.cpp:71) both already
use `[3]`; `THole` is the only one that does not.

Read out of `DRAGON.DAT`:

| hole | 1304 | release mask | 408 `(x, y, r, z)` | original `ZSetValue` | port `ZSetValue` | error |
|---|---|---|---|---|---|---|
| `s_hole118` | 2 | 0x4 | (5.1885, -5.2377, 0.4823, **2.5061**) | **2.8761** | 0.4823 | **-2.394** |
| `s_hole101` | 0 | 0x1 | (4.9869, 9.3369, 0.4863, **0.0000**) | **0.3700** | 0.4863 | **+0.1163** |
| `s_hole100` | 0 | 0x1 | (-5.0772, 9.4776, 0.4841, **0.0000**) | **0.3700** | 0.4841 | **+0.1141** |

Measured live on the current build, unchanged from the previous pass:

```
DBG-hole  reg s_hole118  center=(5.188,-5.238) r=0.482 group=0x1 release=0x4 z=0.482
DBG-hole  reg s_hole101  center=(4.987,9.337)  r=0.486 group=0x2 release=0x1 z=0.486
DBG-hole  reg s_hole100  center=(-5.077,9.478) r=0.484 group=0x2 release=0x1 z=0.484
```

**This is the "0.114 too high" number exactly.** `dragon-holes-ramps.md` measured the ball
holding z = 0.484 from the `s_hole100` release all the way to the drain against a fresh
ball's 0.370, i.e. 0.114 too high; 0.4841 - 0.3700 = **0.1141**. The original sets 0.370
there — the hole hands the ball back at *exactly* playfield height, because its 408[3] is
0.0 and the base is the ball radius. The port adds the hole's own aperture radius instead.

And `s_hole118`, which releases onto the 0x4 tunnel and then rides it for 2.29 s (measured),
should sit at **2.876** and sits at **0.482** — 2.39 units low, so the ball is drawn at
playfield depth for the whole of a tunnel it is supposed to be high above. That is "el
render de la bola en los tuneles funciona extranio" in its strongest form.

### 2.4 Why a wrong z shows

`TBall::Repaint` picks the sprite by `index` = the first `VisualZArray[index] <= zDepth`
and passes `zDepth` to `SpriteSetBall` for the depth buffer. A frozen-but-correct z is a
correctly sized ball correctly occluded; a frozen-and-wrong z is a ball drawn at the wrong
size and tested against the table's zMap at the wrong depth for the whole stretch — a
per-stretch constant error, which matches the complaint's "funciona extranio" rather than
"flickers". `reference/SpaceCadetPinball/zdrv.cpp` and the render path need no change:
they are being handed bad input, not mishandling good input.

### 2.5 Verdict on question 2

**The missing writer exists and there are two of them. The port is wrong; the freeze is
not.**

1. `TSink::TimerExpired` never sets z (nor the mask). Original: 0x418605 /
   0x4185f6. Fix in section 1.8.
2. `THole`'s constructor reads `408[2]` where the original reads
   `CollisionCompOffset + 408[3]`. Original: 0x416929-0x416937.

The change that follows for #2, in `reference/SpaceCadetPinball/THole.cpp`, `THole::THole`,
line 62:

```cpp
ZSetValue = zArr ? PinballTable->CollisionCompOffset + zArr[3] : 0.0f;
```

`PinballTable->CollisionCompOffset` is set to the ball radius by the `TBall` constructor
(TBall.cpp:78), which runs first — `TPinballTable`'s own constructor calls `AddBall` at
line 68, before the `table_objects` loop that builds the holes. So the value is live by the
time any `THole` is constructed, in the port exactly as in the original (`table+0x152`,
written at 0x402740 and read at 0x416929). No other `THole` member changes; `CollisionMask`
from 1304 is already right (0x416971, port line 65-70).

Do not fix the freeze, do not touch `TRamp`, `TRampExit`, `TTableLayer` or `zdrv.cpp`.

## 3. Side findings, stated as such

* **`TKickout`'s absent-408 default differs.** Original 0x416012-0x41601c:
  `CollisionBallSetZ = table->CollisionCompOffset` when 408 is missing. Port
  (TKickout.cpp:71-73): `0.0f`. No Dragon kickout is missing 408, so this is latent, not
  live. Unmeasured.
* **`THole::Circle.RadiusSq` looks like a second index/squaring slip.** The original builds
  the *edge* circle from `(306 * 600[2])` squared (0x416839-0x4168aa, and `TCircle`'s ctor
  squares its argument — the port matches here), but then at 0x4168f6-0x416912 resets the
  hole's own field circle to `600[2] * 600[2]` = 0.2326 for `s_hole118`. The port leaves
  `Circle.RadiusSq = 306 * 600[2]` = 0.0482 (THole.cpp:49). A ~5x too small capture field.
  **Spotted while reading the constructor for `ZSetValue`; not measured and not pursued.**
* **`a_sink19`'s 601[3] is -0.800** on release layer 0x8, giving an eject z of -0.430 —
  below the playfield. That is what the DAT says, so the fix in 1.8 would reproduce it
  faithfully. Why a tunnel sink would release below the table is not explained here.

## 4. What is still not settled, and what would settle it

* **Whether the two fixes make it *look* right.** Everything above was run with
  `SDL_VIDEODRIVER=dummy`; no frame was rendered or compared. Settling it needs a windowed
  run of the patched binary against the real game under Wine (`game/WIN95/`), same table,
  same ball path, frames compared — not another headless trace.
* **Whether `ControlRampWall2Crossed` ever reaches `DragonRampControl` /
  `TowerRampControl`.** Still unmeasured, and unchanged by this pass: 200 s of autoplay
  entered both scoring ramps and fell back out of both before the exit wall. With
  `TSink::TimerExpired` fixed, a tower-lock-3 reject drives a ball down `a_ramp5` to
  `a_ramp104` on command, which is a repeatable harness route — but `a_ramp5` is bound to
  no handler, so it proves the code path and not the binding. Settling it needs the ball
  shot up `a_ramp6` or `a_ramp103` far enough to cross wall2, which blind flipper flapping
  has not done in 850 s.
* **Whether anything broadcasts 0x3f8 (`PlungerFeedBall`) to a hole**, and the identity of
  0x004131a0, the hole's re-arm path for a refused release. Both were left open by the
  previous pass and nothing here touched them. Settling them needs the callers of 0x416cea
  and 0x4131a0 traced, which this pass did not do.
* **Skulduggery.** `PIRATES.EXE` was not examined. Its sinks go through the same `TSink`,
  so the 1.8 fix presumably matters there too, but its 1301/601 values were not read and
  nothing here should be taken as a claim about that table.
