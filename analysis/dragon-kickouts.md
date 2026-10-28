# `TKickout`, measured against the originals

`reference/SpaceCadetPinball/TKickout.cpp` had never been audited against the 1996
binaries. It has now been read line by line against `DRAGON.EXE`, and the class is
**byte-identical in all three executables** over `0x415e60..0x416590` -- `CADET.EXE` and
`PIRATES.EXE` differ from `DRAGON.EXE` in exactly 11 bytes, every one of them inside a
relocated address (`0x415e84`, `0x415f34`, `0x415f4c`, `0x415f4d`, `0x415f76`, `0x415f8d`,
`0x41603e`, `0x416162`, `0x416163`, `0x41640b`, `0x416579`). So every finding below is a
statement about Space Cadet's own engine as much as about the bonus tables', which is why
none of it needs a `table_rules.h` switch.

## Where the class is

DAT object type 1012 is `0x3f4`, so the component-type getter is `mov eax,0x3f4 / ret` --
`B8 F4 03 00 00 C3`, which occurs exactly once in `DRAGON.EXE`'s `.text`, at **0x41aec0**.
The only `.rdata` word pointing at it is the vtable at **0x437620**:

| slot | address | what |
|---|---|---|
| 0 | 0x41aec0 | component type, returns 0x3f4 = 1012 |
| 1 | 0x4163a0 | `Message` |
| 2 | 0x41aed0 | |
| 3 | 0x4135f0 | |
| 4 | 0x413610 | |
| 5 | 0x41b1a0 | destructor-shaped thunk |
| 6 | 0x4160d0 | `Collision` |
| 7 | 0x4162e0 | `FieldEffect` |

plus **0x415e60** the constructor (the only `.text` reference to 0x437620, at 0x415e83),
**0x416260** `TimerExpired` and **0x416240** `ResetTimerExpired`.

Three `E8` calls reach the constructor: `0x41a6ea` pushes `1` and `0x41a908` pushes `0`,
which are the table's type-1012 and type-1029 arms, and `0x41656f` is a second
constructor at **0x416560**. That third one is *not* a kickout variant -- its vtable
`0x437640` has type getter `0x41aee0` returning `0x412` = **1042**, i.e. `TMagnet`. It
calls `TKickout(table, groupIndex, 1)`, overwrites the vtable, sets `TimerTime2` to 1.0f
and clears `BallCaputeredFlag`, which is exactly what `TMagnet.cpp` already does.

## Field offsets, pinned by the constructor

| offset | field | written at |
|---|---|---|
| +0x05 | `ActiveFlag` | 0x415e90 |
| +0x06 | `MessageField` | 0x415e9b |
| +0x6a | `Threshold` | 0x4160dd (1e9) |
| +0x6e | `SoftHitSoundId` | 0x415f01 |
| +0x72 | `HardHitSoundId` | 0x415f04 |
| +0x76 | `BallCaputeredFlag` | 0x415ea2 |
| +0x7a | `NotSomeFlag` | 0x415e89 |
| +0x7e | `Timer` | 0x415e9e |
| +0x82 | `TimerTime1` = 1.5 (0x3fc00000) | 0x415ea5 |
| +0x86 | `TimerTime2` = 0.05 (0x3d4ccccd) | 0x415eaf |
| +0x8a | `CollisionBallSetZ` | 0x41600a / 0x41601c |
| +0x8e | `Ball` | 0x4160ef |
| +0x92 | `FieldMult` (attribute 305) | 0x415ee7 |
| +0x96/+0x9a | `Circle.Center.X/Y` | 0x415f10 / 0x415f19 |
| +0xa2 | `Circle.RadiusSq` | 0x415f25, then 0x415ff8 |
| +0xa6 | `OriginalBallZ` | 0x41611f |
| +0xaa..+0xb2 | `BallThrowDirection` | 0x416035..0x416048 |
| +0xb6 | `ThrowAngleMult` | 0x41604b |
| +0xba | `ThrowSpeedMult1` (Boost) | 0x41605b |
| +0xbe | `ThrowSpeedMult2` (ThrowBallMult / 100) | 0x41606d |
| +0xc2..+0xca | `Field` {ActiveFlag, CollisionGroup, CollisionComp} | 0x4160a6..0x4160ac |

The `visualStruct` stack offsets the original reads (`Boost` at +24, `ThrowBallMult` +28,
`ThrowBallDirection` +32..40, `ThrowBallAngleMult` +44, `HardHitSoundId` +48,
`CollisionGroup` +52) line up exactly with the port's struct, so every throw parameter was
already being read from the right place. `FieldEffect` (0x4162e0) and `Collision`
(0x4160d0) match the port arm for arm, and `TBall::throw_ball` (0x4029e0) matches too.

## What was wrong

### 1. Attribute 408's default was 0, not `table->CollisionCompOffset`

The leading hypothesis, and it holds:

```
415fdc:  push 0x198                         ; attribute 408
415ffe:  call query_float_attribute_opt
416003:  test eax,eax
416007:  mov  eax,[eax+0xc]                 ;   present -> arr[3]
41600a:  mov  [esi+0x8a],eax
416012:  mov  eax,[esp+0x70]                ;   absent  -> table
416016:  mov  ecx,[eax+0x152]               ;             CollisionCompOffset
41601c:  mov  [esi+0x8a],ecx
```

Element 3 was already right (and unlike `THole` at 0x416929, the kickout adds nothing to
it). The default was not. Fixed.

### 2. Attribute 306's default was 0, not 0.1

```
415e96:  push 0x132                         ; attribute 306
415eba:  call query_float_attribute_opt
415ebf:  test eax,eax
415ec3:  mov  eax,[eax]                     ;   present -> arr[0]
415ecb:  mov  [esp+0x14],0x3dcccccd          ;   absent  -> 0.1f
415f1f:  fld  [eax]                         ; visual.FloatArr[2]
415f21:  fmul [esp+0x14]
415f25:  fstp [esi+0xa2]                    ; -> the capture radius
415f31:  fcomp QWORD PTR ds:0x437338        ; 0.0
415f3f:  mov  [esi+0xa2],0x3a83126f         ;   exactly zero -> 0.001f
```

With a 0.0f default the port *always* took the 0.001f clamp. Fixed.

### 3. `Message` was missing four of its seven arms

`Message` is 0x4163a0 and tests 0x40, 0x4b, 0x4c, 0x4e, 0x3f3, 0x3fe, 0x402. The port had
three. `0x413570` is the shared per-player `MessageField` save/restore (it writes
`[this+0x1a + 4*CurrentPlayer]`), which fixes `0x3fe` as `PlayerChanged`; `0x413550`
zeroes `MessageField` and the four backups, which fixes `0x402` as `Reset`.

| original | arm | what it does | port before |
|---|---|---|---|
| 0x40 | 0x4163fa | `TKickoutRestartTimer`; `value < 0` -> `TimerTime1` | present |
| 0x4b | 0x416439 | `ActiveFlag = 1` | **missing** |
| 0x4c | 0x416443 | `ActiveFlag = 0`, release a held ball, then kill the re-arm timer *as well* and zero the handle, so `ResetTimerExpired` never runs | **missing** |
| 0x4e | 0x416480 | destroy a held ball: `TBall::Disable` (0x4164bd) and `--MultiballCount` (0x4164c5) | **missing** |
| 0x3f3 | 0x4164db | `SetTiltLock` | present |
| 0x3fe | 0x4164eb | `PlayerChanged`: release, then the base save/restore | **missing** |
| 0x402 | 0x416522 | `Reset`: release, then the base reset | present |

0x4b/0x4c are 75/76, the pair `MessageCode` already names `TMagnetFieldEnable` /
`TMagnetFieldDisable` -- the enum's own comment says they go "to the type-1042 magnets and
to captive kickouts", and the kickout half was simply unimplemented. `TMagnet::Message`
overrides completely and never chains to the base, so the new arms cannot disturb it.

0x4e has no port symbol. It is implemented behind a file-local
`static constexpr MessageCode TKickoutDestroyBall = static_cast<MessageCode>(78)` and is
dead until `TPinballComponent.h` gains a name for it; 78 is free in the enum.

`TimerExpired` plays its sound *after* the throw in the original (0x4162cc) and the port
plays it before. Left alone: same frame, same ball, no observable difference.

## Blast radius, measured

`FtAccelDebug.cpp` now registers every kickout at construction, so the three tables'
attribute coverage is just readable:

| table | kickouts | 408 | 306 |
|---|---|---|---|
| Space Cadet | `a_kout1`, `a_kout2`, `a_kout3` | all present (0.300 / 0.000 / 0.000) | all present (0.05 / 0.9 / 0.2) |
| Dragon's Keep | `a_kout1`, `a_kout2`, `a_kout3` | all present (0.000 / 0.000 / 0.370) | all present (0.9 / 0.9 / 0.01) |
| Skulduggery | `a_kout1`, `a_kout2` | present (0.370 / 0.370) | present (0.05 / 0.05) |
| Skulduggery | `s_magnet1`..`s_magnet6` | **absent** | **absent** |

So the two default fixes change **nothing** on Space Cadet or Dragon's Keep, and
everything on Skulduggery's six magnets, which were the only objects in any of the three
tables relying on the defaults:

```
             before            after
CollisionBallSetZ   0.000      0.370   (= CollisionCompOffset)
capture circle rSq  0.0000     0.0045  (0.001^2 -> (0.1 x 0.670)^2)
```

Measured from the harness's own edge dump:

```
DBG-koutedge d=0.000 group=0x1 comp=s_magnet1 type=1042 circle (6.566,-3.426) rSq=0.0000   before
DBG-koutedge d=0.000 group=0x1 comp=s_magnet1 type=1042 circle (6.566,-3.426) rSq=0.0045   after
```

A 0.001-unit collision circle is 67x too small: all six `s_magnet*` were effectively not
in the world, so none of them could ever raise `ControlCollision` and none of
`src/pirates_*`'s magnet rules could fire. That is a Skulduggery bug the kickout audit
found by accident.

## The stuck ball is *not* a `TKickout` bug

`FT_KOUTTEST=<n>` drives the ball into kickout n; `FT_KOUTTRACE=1` traces every frame,
`FT_KOUTREPEAT=1` re-drives every 8 s, `FT_KOUTCENTER=1` drops the ball straight on the
mouth (for a magnet's tiny circle), `FT_KOUTARM=1` forces `ActiveFlag` first,
`FT_KOUTEDGES=1` dumps the nearby collision geometry, `FT_KOUTBLOCK=1` dumps every moving
blocker's position and `FT_KOUTBLOCKPOS=<n>` parks them all at position n.

### `a_kout1`, the Armor Kickout -- fine

Captures, holds 1.5 s (`TimerTime1`, `-1.0` from `ArmorKickoutControl`), throws at
`(-0.467,-0.884,-0.092)` x 35.0, and is gone. One capture per drive, repeatable.

### `a_kout2`, the Gargoyle Kickout -- an infinite capture loop

```
t=3.08  CAPTURE a_kout2   ballZ 0.370 -> setZ 0.000 mask=0x1
t=3.08  MSG a_kout2 code=55 value=-1.0000          (GargoyleGaggleController's feed)
t=4.58  DBG-koutrel a_kout2 / throw dir=(0.326,0.945,-0.092) spd=35.000
t=4.58  pos=(-3.155,-11.072) spd=35.098            up the channel
t=4.59  pos=(-3.059,-10.793) spd=35.195
t=4.60  pos=(-3.209,-10.928) spd=26.224            rebounded
t=4.63  CAPTURE a_kout2                            re-captured, 0.05 s after re-arm
```

and then forever: 11 captures in 20 s, one every ~1.55 s = `TimerTime1` plus flight.

Every number in that trace is the original's. The eject fires on schedule, the direction
`(0.326,0.945)` is the DAT's own and is parallel to the channel the kickout sits in (the
wall `(-3.613,-11.806)->(-3.214,-10.556)` normalises to `(0.304,0.953)`), and the speed is
the DAT's `Boost`. The ball simply has nowhere to go: the pocket is capped above by the
wall `(-2.720,-11.919)->(-3.796,-11.364)`, walled either side, and the channel's mouth
0.6 units downhill is crossed by `a_rockb`'s position-4 line
`(-3.787,-10.645)->(-2.643,-11.310)` and then by three `a_spot14` solo-target lines at
y -10.46, -10.59 and -10.83.

Retracting the rocks is not enough, and it is not the rock's position either:

| `FT_KOUTBLOCKPOS` | -1 | 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|---|
| `a_kout2` captures in 14 s | 7 | 7 | 7 | 7 | 7 | 7 |

With every blocker retracted the ball gets 0.28 units further (to y -10.793) and rebounds
off `a_spot14` instead. So the obstruction that matters is a **type-1019 `TSoloTarget`**,
not a blocker and not the kickout.

What the harness cannot settle is whether the original ejects into the same dead end. It
reads like one of two things, both outside `TKickout`:

* `a_spot14`'s edges should not be in the channel at all -- a geometry or
  collision-group question for `TSoloTarget` / the loader, not for the kickout; or
* the original's `a_kout2` is not supposed to be re-armed here, in which case the
  missing message is the 0x4c (`TMagnetFieldDisable`) arm this audit just added and
  something in `src/dragon_*.cpp` should be sending it.

`// UNVERIFIED:` which. Settling it means disassembling `DRAGON.EXE`'s `TSoloTarget`
constructor and `a_spot14`'s DAT record, or finding a sender of 0x4b/0x4c in Dragon's
rules; neither is in this file's scope.

### `a_kout3`, the Dragon Blood magnet -- fine, once armed

`notSomeFlag=1`, so it is the type-1029 arm: it starts with `ActiveFlag = 0` and
`ResetTimerExpired` never re-arms it. Its 306 is 0.01 against a 3.938 visual radius, so
the capture circle is 0.039 across while the *field* radius is 3.938 with `FieldMult` 300
-- a magnet that reaches four units and holds in a pinpoint. With `FT_KOUTARM=1
FT_KOUTCENTER=1` it captures, holds until a flipper press, and releases on
`MSG code=55 value=0.0000`. Its throw is `dir=(0.992,0.124,-3.844)` with
`m1 = m2 = 0`, i.e. `Speed = 0`: the magnet drops the ball rather than kicking it, which
is why the direction is neither normalised nor used.

## The text the player saw

There is no "Yellow Belly" string in any of the three binaries or in
`table_strings.cpp`. The line is Dragon's **3062, "Gargoyle Gaggle-Ball"**, and the
mechanism explains why it stays on screen: `GargoyleKickoutControl`'s normal-play branch
sets mode 0xe and `GargoyleGaggleController` displays 3062 with a `Forever` duration, so
the text sits there for as long as the mode runs -- which, with the ball looping in
`a_kout2`, is for the rest of the ball.
