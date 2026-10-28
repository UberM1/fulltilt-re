# The Dragon's Keep audio blowup

Player report, twice: *"estoy con el bug de audio de nuevo ... cuando entro en el canal
izquierdo de drenaje y la bola es rescatada ... encima salto esto del audio que te deja
sordo"*. An earlier report of the same thing called it *"efecto en loop"*. Not a crash:
the session exited 0 (`.play/DRAGON/session.log`, engine 3254929).

## The driver: `TKickback` re-arms itself forever

`a_kick1` is Dragon's left-outlane kicker and the only `TKickback` on the table
(`DBG-kick drive into kickback[0] a_kick1`, one entry in the registry). Upstream's
`TKickback::TimerExpired` is a single callback that branches on `KickActiveFlag`, and in
the armed branch it calls `loader::play_sound(HardHitSoundId, ...)` **and re-arms itself**
with `TimerTime2` = 0.1 s. `KickActiveFlag` is cleared only by a collision for which
`DefaultCollision` returns true. A ball that touches the kicker once and then leaves --
which is exactly what the left drain channel does, the ball brushing the kicker on its way
into `drain` -- leaves the flag set forever, so the clip restarts 10 times a second with no
termination condition. `TKickback::Message` kills it only on `SetTiltLock` / `Reset`, and
Dragon's drain handler sends `Reset` to `drain`, never to `a_kick1`
(`src/dragon_part4.cpp`, `BallDrainControl`).

Sound id 21 on Dragon's Keep, clip duration longer than 0.1 s, so the voices overlap.
`Sound::PlaySound` only halts the oldest channel once all `num_channels` are busy, so the
steady state is every mixer channel playing the same clip at once. That is the "deafening".

### Measured, not inferred

`FT_SOUNDDBG=1` (this document's harness, `loader::play_sound`) counts starts per sound id
per 1000 ms of `pb::time_ticks`; `FT_KICKTEST=<n>` (`FtAccelDebug.cpp`) drives the ball at
`TKickback` *n*'s own wall segment and then calls `TBall::Disable()` `FT_KICKGONE` ms later,
which is what `TDrain::Collision` does to a draining ball.

Before (upstream `TKickback`, store path
`/nix/store/1xfrvh908ifwbn6lpwkfdq73233av39x-...`), `FT_AUTOPLAY=1 FT_KICKTEST=0
FT_KICKSIDE=-1 FT_SOUNDRATE=5`:

```
DBG-kick  drive into kickback[0] a_kick1 seg=(5.401,12.344)->(6.509,12.344) group=0x1
          hardHit=21 softHit=0 t1=0.700 t2=0.100 active=1
DBG-kick  t=   3.31 ball removed (mimics TDrain::Collision) kickActive=1 timer=228
DBG-kick  t=   3.81 kickActive=1 timer=243 threshold=0.0 ball=gone
DBG-kick  t=  17.81 kickActive=1 timer=663 threshold=0.0 ball=gone      <-- still going
DBG-snd   LOOP sound=21  TKickback  10 starts in the last 1000 ms (total 141)
```

14 s of trace, a new timer id every 0.1 s, 141 starts of sound 21, no termination.
`FT_KICKSIDE=1` (the other side of the one-sided segment) never collides and is the control.

After (two separate callbacks, `/nix/store/1s0apy6pxll49al46rqwc7bwzc1m6vcb-...`), same
command line:

```
DBG-kick  t=   3.81 kickActive=1 timer=243 threshold=0.0 ball=gone
DBG-kick  t=   3.88 kickActive=0 timer=0   threshold=1000000000.0 ball=gone
```

Armed at 3.81, retracted at 3.88, state back at rest, **zero** `DBG-snd` lines.

## No other path starts a clip at a high rate

240 s `FT_AUTOPLAY=1 FT_SOUNDDBG=1 FT_SOUNDRATE=6` soak on all three tables with the fixed
engine. The only ids ever flagged are the flipper pair -- 44/45 on Space Cadet, 19/20 on
Dragon's Keep, 23/24 on Skulduggery -- and those are the harness itself: `autoplay()` toggles
both flippers every 0.2 s. All three logs carry byte-identical counts (772 flagged lines,
the same running totals), which is what a metronome looks like and what gameplay does not.
No rules handler, timer or component reached 6 starts/s.

## `TCollisionComponent`'s byte layout, and the `[this+0x72]` question

Pinned from `TCollisionComponent::DefaultCollision` at DRAGON.EXE **0x413ab0**:

| offset | field | evidence |
|---|---|---|
| +0x5e | `Elasticity` | `mov eax,[esi+0x5e]` at 0x413afb, 5th arg to `basic_collision` (0x41ac00) |
| +0x62 | `Smoothness` | `mov edx,[esi+0x62]` at 0x413af7 |
| +0x66 | `Boost` | `mov eax,[esi+0x66]` at 0x413af0 |
| +0x6a | `Threshold` | `fcom DWORD PTR [esi+0x6a]` at 0x413b14, and the 1e9 (`0x4e6e6b28`) the `TKickback` ctor writes there at 0x415b80 |
| +0x6e | `SoftHitSoundId` | played at 0x413b58, on the `projSpeed > 0.2` arm (`fcomp ds:0x437358` at 0x413b42) |
| +0x72 | `HardHitSoundId` | played at 0x413b2b, on the `projSpeed > Threshold` arm, the one that returns 1 |

So the `// UNVERIFIED:` marker in `TKickback::Collision` resolves to the first reading:
**+0x72 is `HardHitSoundId`, and the original really does start the same clip twice on a
kick.** `TKickback::Collision` at 0x415c16 calls `DefaultCollision`, tests the result at
0x415c1b, and on true plays `[esi+0x72]` again at 0x415c1f-0x415c23 -- the same field
`DefaultCollision` has just played at 0x413b2b, through the same `play_sound` at 0x40da00.
Two overlapping voices of one clip, once per kick: an emphasis, and bounded, nothing like
the 10 Hz loop. Faithful port: add

```cpp
loader::play_sound(HardHitSoundId, ball, "TKickback kick"); // 0x415c1f
```

as the first statement of the `if (DefaultCollision(...))` body in
`reference/SpaceCadetPinball/TKickback.cpp`.

## A second guard the port still lacks: `[this+0x86]`

`TKickback::Collision` at 0x415bd7 tests `[esi+0x86]` *before* `[esi+0x82]`
(`KickActiveFlag`) and, when it is non-zero, skips the whole arming block and falls straight
through to `DefaultCollision` at 0x415c05. `TKickback::Message` (0x415d70) sets it to 1 on
code **0x4c** (0x415daa) and clears it in the 0x4b / 0x3f3 arm (0x415dd7), which also clears
`KickActiveFlag` and kills the timer. 0x4c is the code `src/pirates_part3.cpp:402` sends as
`FtMessageCode::TMagnetDisarm` to `a_kick1`; the port's `TKickback::Message` handles only
`SetTiltLock` and `Reset`, so that disarm is currently a no-op. Rules correctness rather than
audio -- but note a disarmed kicker could never have armed the loop in the first place.

## The harness

Keep both; they are cheap and env-gated.

- `FT_SOUNDDBG=1` -- per-sound-id starts per 1000 ms in `loader::play_sound`, printed when an
  id crosses `FT_SOUNDRATE` (default 8 starts/s) and once per window thereafter.
- `FT_KICKTEST=<n>` with `FT_AUTOPLAY=1` -- drive the ball at `TKickback` *n*.
  `FT_KICKSIDE=-1` picks the other side of the segment (a_kick1 only collides from -1),
  `FT_KICKGONE=<ms>` is how long the ball stays before `Disable()` (default 300).

Run headless: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, with `HOME` / `XDG_DATA_HOME`
into a staged run dir. The dummy audio driver still runs the mixer calls, so the counter
works with nothing audible.
