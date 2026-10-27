# Skulduggery: can a ball be permanently swallowed?

Answer: **no sink or hole on PIRATES strands a ball.** 59 swallows were driven across
26 headless runs and every one that was not cut off by `SIGTERM` produced an eject;
all 28 hole captures released. The one handler arm on the table that deliberately
holds a ball and sends no `TSinkResetTimer` — `ShipSinkControl`'s Peg Leg's Loot arm —
is byte-for-byte what `PIRATES.EXE` does at `0x428649`, and the ball leaves through
`PegLegsLootController`'s payout chain instead (`call 0x423680` at `0x42d0fe`, verified).

Method: the `FtAccelDebug.cpp` harness under `FT_AUTOPLAY=1`, driving
`FT_SINKTEST=0..9` / `FT_HOLETEST=0..2` (with and without `FT_LAYERTEST=1`) plus
18 unguided free-play sessions of 300–540 s, all headless
(`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`) against the prebuilt
`/nix/store/69miyi32xc2nfnfpsw658gjksjx593s4-space-cadet-pinball-fulltilt-dev`.
Nothing under `src/` or `reference/` was touched and nothing was rebuilt.

## 1. The components

`PIRATES.DAT` names exactly ten type-1017 sinks and three type-1022 holes, and the
harness registers all thirteen. `strings` over the DAT turns up no other capture
component: `a_kick1`/`a_kick2` are `TKickback`s (no capture), `s_magnet1..6` are
type-1042 `TMagnet`s (`TMagnet::Collision` does not capture), and the only two
`TKickout`s, `a_kout1`/`a_kout2`, both go through `CaptiveKickoutControl`, which ends
on `caller->Message(TKickoutRestartTimer, 1.0f)` — the one message that can release a
`TKickout`. So sinks and holes are the whole surface.

All ten sinks read `timerTime=2.0000` out of the DAT (attribute 407), so every
`TSinkResetTimer` sent with `Forever` (`= -1.0f`, `pirates_modes_entry.cpp:133`)
arms a 2.0 s eject — see §5.

## 2. Measured

Aggregate over every run. "armed" counts `DBG-sink MSG … TSinkResetTimer`, which is
attributed to the sink the message was *sent to*, not the one that swallowed — on this
table those are usually different components (§3), so only the column totals balance.

| idx | component | handler | HIT (swallowed) | armed | EXP | EJECT |
|---|---|---|---|---|---|---|
| 0 | `a_sink4`  | `ShipSinkControl`            | 0 (unreachable by harness) | 0 | 0 | 0 |
| 1 | `a_sink5`  | `TavernSinkLeftControl`      | 11 | 0 | 0 | 0 |
| 2 | `a_sink6`  | `TavernSinkCentreControl`    | 10 | 0 | 0 | 0 |
| 3 | `a_sink7`  | `TavernSinkRightControl`     | 10 | 0 | 0 | 0 |
| 4 | `a_sink8`  | `LavaSinkControl`            | 12 | 43 | 43 | 43 |
| 5 | `a_sink9`  | `TreasureSinkLeftControl`    | 2  | 6 | 6 | 6 |
| 6 | `a_sink12` | `TreasureSinkRightControl`   | 6  | 2 | 2 | 2 |
| 7 | `a_sink14` | `HideawayRightControl`       | 2  | 1 | 1 | 1 |
| 8 | `a_sink15` | `HideawayCentreControl`      | 6  | 2 | 2 | 2 |
| 9 | `a_sink17` | `HideawayLeftControl`        | 0 (unreachable by harness) | 4 | 3 | 3 |
| | **sinks, total** | | **59** | **58** | **57** | **57** |

| idx | component | handler | HIT (captured) | released |
|---|---|---|---|---|
| 0 | `s_hole9`  | *(none — not in `control_pirates.cpp`)* | 1  | 1  |
| 1 | `s_hole11` | *(none)*                                | 26 | 26 |
| 2 | `s_hole19` | `BrigHoleControl`                       | 1  | 1  |
| | **holes, total** | | **28** | **28** |

Holes balance exactly. `THole` arms its own capture timer in `Collision` and the
second stage in `FieldEffect` always lowers the ball to `ZSetValue` and restores
`CollisionMask`, so no rule has to send `THoleReleaseBall` and none does — correctly,
because the three PIRATES holes are pass-through, not parking spots. `s_hole19`'s
`BrigHoleControl` only does bookkeeping (`BrigCaptureBall`: `MultiballCount--`,
`BallsInBrig++`, then `s_launcher1->Message(PlungerRelaunchBall, 0.96f)`); the
physical ball is released by the engine like any other hole, confirmed by
`DBG-holerel s_hole19 mask 0x4 -> 0x1 z=0.388`.

### The two unmatched sink HITs are truncation, not stranding

59 HIT vs 57 EJECT, and both gaps are the last line of a run killed by `timeout`:

- `pbr/sink8.log`: `DBG-sink HIT a_sink15` at `t=33.88` is the final line in the file.
- `pbr/long10.log`: `HIT a_sink14` → `MSG a_sink17` with no `EXP`. The timer was armed;
  an armed `TSink` timer has only two non-ejecting outcomes — re-arm at 0.5 s while
  `BallCountInRect` is non-zero (which prints a further `EXP`, and none appears), or
  `timer::kill(TimerExpired)` from `TSink::Message(Reset)` / `(PlayerChanged)`. The
  first is ruled out by the missing `EXP`; the second only happens at a new ball, new
  game or player change, each of which re-serves a ball from the plunger. Neither is a
  permanent swallow.

Per-file, every other run is exactly balanced (`HIT == EJECT`).

### Harness limitations, stated as such

`a_sink4` and `a_sink17` never registered a single `DBG-sink HIT`, in any run.
This is the thin-AABB failure the brief warns about, not a stranding:

- `FT_SINKTEST=9` (`a_sink17`, aabb `x 1.96..2.69 y -8.74..-7.81`) parks the ball at
  `(1.710,-7.673)` with `spd=0.054` and it stays there — resting against the lip,
  outside the sink, for the rest of the run. Twelve drives, zero landings.
- `FT_SINKTEST=0` (`a_sink4`, aabb `x 1.54..2.46 y -6.17..-5.71`) never lands either;
  the only `HIT` in that log is a different component (`a_sink12`).

Indirect evidence covers both. `a_sink17` *ejected* 3 times during other tests, as the
random target of `EjectHideawayAtRandom`, so its `TSink` eject path is live; and its
handler `HideawayLeftControl` is the same shape as `HideawayRightControl`
(`a_sink14`), which was swallowed and ejected. For `a_sink4` there is no empirical
cover at all — §4 settles it statically against the 1996 binary instead.

## 3. Why HIT and EJECT name different sinks

This is the behaviour most likely to read as the reported bug and it is not one. On
Skulduggery a swallowed ball normally comes back out of a *different* hole:

- Every hideaway and tavern arm leaves through `EjectHideawayAtRandom`
  (`pirates_modes_entry.cpp:1057`, original `0x004236a0`), which in `ModeIdle` ejects
  the **lava sink** and otherwise draws `Rnd(3)` over `a_sink17`/`a_sink15`/`a_sink14`
  — the manual's "Secret Hideaway" teleport. Hence log pairs like
  `HIT a_sink15 → MSG a_sink17 → EJECT a_sink17`.
- The two treasure sinks are the two ends of one tube: outside Treasure Hunt each
  disables itself (`caller->ActiveFlag = 0`) and ejects through the other
  (`pirates_part2.cpp:1066` and `:1104`). Hence `HIT a_sink9 → EJECT a_sink12`.

`TSink::TimerExpired` calls `TPinballTable::AddBall`, and `AddBall` reuses the first
`ActiveFlag == 0` ball in `BallList`, which is the ball `TSink::Collision` just
`Disable()`d. The logs show the same `TBall*` on both sides
(`HIT … ball=0x…6050` / `EJECT … ball=0x…6050`), so nothing leaks: the ball is the
same object, moved across the playfield.

## 4. The one arm that holds a ball and arms no timer

`ShipSinkControl` (`src/pirates_part2.cpp:944`, original `0x004283f0`),
`case ModePegLegsLoot`:

```cpp
	case ModePegLegsLoot:
		mlite51->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		lite4->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		…
		mode_scratch_counter = 1;
		return;
```

No `TSinkResetTimer`, no `EjectLavaSink`. The ball sits in `a_sink4`, `Disable()`d,
with no timer of its own.

**Confirmed faithful to `PIRATES.EXE`.** The jump table at `0x004286d8` is
`{0x4284d6, 0x4285cb, 0x428606, 0x428649, 0x42841d}` and the index table at
`0x004286ec` is `{0,4,1,4,4,4,4,2,4,3}` over states `2..0xb`, so state `0xb`
(Peg Leg's Loot) reaches `0x428649`. Disassembled with `objdump -D -b binary -m i386`
over `.text` (base `0x401000`, 212992 bytes, extracted with
`analysis/dump_components.py`'s `load()`):

```
  428649:	6a 00                	push   0x0
  42864b:	a1 f4 b1 43 00       	mov    eax,ds:0x43b1f4
  428652:	6a 13                	push   0x13          ; TLightResetAndTurnOn
  42865a:	ff 50 04             	call   DWORD PTR [eax+0x4]
  …                                                      ; 7 x push 0x14 (TurnOff)
  4286c6:	c7 05 f8 5c 43 00 01 	mov    DWORD PTR ds:0x435cf8,0x1   ; mode_scratch_counter = 1
  4286d0:	83 c4 40             	add    esp,0x40
  4286d3:	c2 08 00             	ret    0x8
```

Eight light messages, one store, `ret`. No `push 0x41` (`TSinkResetTimer`), no
`call 0x423680` (`EjectLavaSink`). Contrast the same handler's default arm at
`0x42841d`, which does end on `push 0xbf800000; call 0x423680` — i.e. the omission at
`0x428649` is deliberate in the original, not a decompilation loss.

The exit is `PegLegsLootController` (`src/pirates_modes.cpp:1917`, original
`0x42cfc0`). The payout is self-driving and needs no ball: the mode tick is the text
box's display timer expiring (`pirates_modes.cpp:2400`), steps 1..8 each display a
glyph for 0.25 s and step 9 for 4.0 s, and the default arm tears the mode down and
feeds a ball. Confirmed in the binary:

```
  42d0f3:	6a 00                	push   0x0
  42d0f5:	6a 52                	push   0x52          ; ModeTeardown
  42d0f7:	e8 54 07 00 00       	call   0x42d850      ; Handler
  42d0fc:	6a 00                	push   0x0           ; 0.0f -> eject now
  42d0fe:	e8 7d 65 ff ff       	call   0x423680      ; EjectLavaSink
  42d103:	e8 08 65 ff ff       	call   0x423610      ; StartLavaBall
```

Step 1 also stops the mode clock (`tag_mode_timer->Message(TimerSet, Forever)`), so
the chain cannot be interrupted by a mode timeout while the ball is held. The ball is
back on the playfield about 6 s after it enters the sink.

Residual, binary-faithful, not a port bug: `TSink` has no occupancy guard, so if a
*second* ball enters `a_sink4` during the payout the handler runs again and resets
`mode_scratch_counter` to 1, restarting the animation, while only one lava-sink eject
fires at the end. That needs multiball concurrent with Peg Leg's Loot and is exactly
what `0x004283f0` does in 1996. Not reproduced here.

## 5. Every other arm, statically

Checked arm by arm in `src/pirates_part2.cpp`; each row is the exit each arm takes.

| handler | arms | exit |
|---|---|---|
| `HideawayLeftControl` (`a_sink17`, `0x427b90`) | Idle, Bermuda, Storm, TreasureHunt (X not armed), TreasureHunt (X armed), PegLegsLoot, default | `EjectHideawayAtRandom()` on all but the armed-X arm, which uses `EjectLavaSink(-1.0f)` |
| `HideawayCentreControl` (`a_sink15`, `0x427e50`) | same seven | same |
| `HideawayRightControl` (`a_sink14`, `0x428120`) | same seven | same |
| `ShipSinkControl` (`a_sink4`, `0x4283f0`) | AttackShip, Mutiny, PegLegsLoot, default | `EjectLavaSink(-1.0f)` ×3; **PegLegsLoot: none** (§4) |
| `TreasureSinkLeftControl` (`a_sink9`, `0x428700`) | TreasureHunt ×2, PegLegsLoot, otherwise | `EjectHideawayAtRandom()`, `EjectLavaSink(-1.0f)`, `EjectHideawayAtRandom()`, `a_sink12->Message(TSinkResetTimer, 0.0)` |
| `TreasureSinkRightControl` (`a_sink12`, `0x428800`) | same four | mirror, ending `a_sink9->Message(TSinkResetTimer, 0.0)` |
| `TavernSinkLeftControl` (`a_sink5`, `0x428a20`) | not-Tavern, Tavern hit, Tavern miss | `TavernSinkAward()` → `EjectLavaSink(-1.0f)`; `EjectLavaSink(-1.0f)` |
| `TavernSinkCentreControl` (`a_sink6`, `0x428a90`) | same three | same |
| `TavernSinkRightControl` (`a_sink7`, `0x428b00`) | same three | same |
| `LavaSinkControl` (`a_sink8`, `0x42a3d0`) | ControlTimerExpired, ControlCollision | sound only; `caller->Message(TSinkResetTimer, 0.0)` |

So exactly one arm of one handler on the table holds a ball without arming a timer,
and it is the one the original holds too.

`EjectHideawayAtRandom` was itself verified against `0x004236a0`, which settles the
`UNVERIFIED` note at `src/pirates_modes_entry.cpp:1050` — Ghidra lost the operands of
the `a_sink14` call, but the listing shares the pushes between the `eax == 1` and
`eax == 2` paths:

```
  4236ce:	68 00 00 80 bf       	push   0xbf800000    ; -1.0f
  4236d3:	83 f8 01             	cmp    eax,0x1
  4236d6:	6a 41                	push   0x41          ; TSinkResetTimer
  4236d8:	74 29                	je     0x423703      ; -> a_sink15 (0x43b0cc)
  4236da:	a1 e4 b0 43 00       	mov    eax,ds:0x43b0e4   ; a_sink14
```

The `a_sink14` arm is `(0x41, -1.0f)`, identical to the other two. Reconstruction
confirmed.

### One comment to fix (behaviour is already right)

Three comments in `src/` say `-1.0f` / `Forever` "holds the ball indefinitely":
`src/pirates_modes_entry.cpp:1035` (`EjectLavaSink`'s header) and the
`EjectHideawayAtRandom` note above it. That is wrong for `TSink`. `TSink::Message`
substitutes the DAT's `TimerTime` when `value < 0.0f` and arms the timer
unconditionally, so on PIRATES `-1.0f` means *eject in 2.0 s*, not *hold*. The logs
agree: every such send prints `value=2.0000 timerTime=2.0000`. The code is correct —
only the comments mislead, and they mislead in the direction of expecting a stranded
ball, which may be where the bug report came from. `Forever` does mean "indefinitely"
for `TTimer`, `TLight` and `TBlocker`; `TSink` is the exception.

## 6. What this does not cover

- `a_sink4` and `a_sink17` were never directly swallowed (§2). `a_sink4`'s Peg Leg's
  Loot arm in particular is settled only statically, from `0x428649` and `0x42d0fe`.
  Reaching either needs a real game, or a harness that drives the ball along the
  sink's edge rather than dropping it on the AABB centre.
- No mode was ever entered. Every measured swallow was in `ModeIdle`, so the
  mode-specific arms in §5 are read, not run. Peg Leg's Loot needs all six map pieces
  or the skill shot and autoplay reached neither.
- Multiball was never active, so the `TSink` occupancy case at the end of §4 is
  untested.
- `TSink::Message(Reset)` and `(PlayerChanged)` both call
  `timer::kill(TimerExpired)`, which kills *every* sink's pending eject table-wide,
  not just the receiver's (`timer::kill(void(*)(int,void*))`, `timer.cpp:64`). This is
  upstream engine behaviour, shared with Space Cadet, and harmless because both
  messages coincide with a ball being re-served — but it is the only mechanism found
  that can discard an already-armed eject.
