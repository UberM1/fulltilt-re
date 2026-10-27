# Success versus timeout: does either binary discriminate?

Scope: the project's biggest open design question, as stated in `docs/integration.md`
and in `src/dragon_missions.cpp`'s `FinishQuest` comment (lines 1153-1161):

> CAVEAT, unresolved: FinishQuest is called from the *shared* 0x52 teardown of every
> quest, and 0x52 is reached on timer expiry as much as on success. [...] No
> success/failure discriminator exists anywhere in this path.

Ground truth: `binaries/win95/DRAGON.EXE` (SHA-1 `7BBC2FEF948613644773B7689FB0CCA28C7F1B5C`)
and `binaries/win95/PIRATES.EXE` (`5EB97B0CF190F628C7F1C02A9D148F27A508E580`).

Method: every claim below is an objdump listing over the raw `.text` of those two files,
produced with `analysis/dump_components.py`'s `load()` + `objdump -D -b binary -m i386
-M intel`. The Ghidra corpora under `decomp/` were used only to find the addresses;
where an argument value or a stack slot mattered, the listing was read. Confidence
legend as in `analysis/dragon-quest-system.md`: **[C]** confirmed, **[P]** probable,
**[U]** unresolved.

**Headline verdict.** The *behaviour* the current reconstruction implements is correct
and is confirmed by disassembly. The *reason given for it* is wrong, in a way that
matters: a success/failure discriminator does exist, is present in the register the
teardown is already reading, and is **used** — but only to choose the follow-on mode,
never to gate the booking. Both binaries additionally contain a dedicated *failure*
announcement path (Skulduggery's "Shipwrecked") which also books the mode as completed.
So outcome-blind booking is a deliberate design choice, not a missing discriminator.
That is a stronger, not weaker, basis for the port's behaviour, and the caveat text
should be rewritten rather than the code changed.

---

## 1. Is the 0x52 teardown reached identically from timer expiry and from objective completion?

**Verdict: no, the two paths differ — and the difference is visible inside the teardown.
It is used for mode selection and ignored by `FinishQuest`. [C]**

### 1.1 Dragon's Keep — four of the five quests have no objective at all

The five quest controllers and their `0x52` arms:

| Mode | Controller | `call 0x424000` (FinishQuest) at | objective-completion exit? |
|---|---|---|---|
| 4 Dragon Hoard | `0x428a80` | `0x429052` | **none** |
| 5 Fire Lizards | `0x4291c0` | `0x4294cb` | **none** |
| 6 Rescue Maiden | `0x429510` | `0x429a32` | yes — `0x4a`, four prison windows |
| 7 Dragon Pass | `0x429b10` | `0x429e7d` | **none** |
| 8 Wizard's Fury | `0x429ec0` | `0x42a111` | **none** |
| 9 Slay Dragon | `0x42a150` | `0x42ac0a` | (not a Quest-Light quest, see §3.3) |

A scan of the whole rules region for `call 0x424000` returns exactly those six addresses
and no others. [C]

Four of the five Quest-Light quests — Dragon Hoard, Fire Lizards, Dragon Pass, Wizard's
Fury — **cannot be won**. They have no completion test anywhere; they score per-hit and
run until the clock stops them. Dragon Hoard's "collect all nine gems" (`0x428a20`,
`== 0x1ff`) awards an extra ball and lets the quest keep running; it does not tear down.
So if `FinishQuest` were gated on success, four of the five Quest Lights could never be
lit by any sequence of play, and `0x423e90`'s "all five lit → Slay Dragon" branch at
`0x423ec0`-ish would be dead code. That alone settles the design intent. [C]

Every quest's timer expiry goes first to the **restart window** (mode 3), not to `0x52`:
`mov DWORD PTR [ecx+0x6],0x3` into `info_text_box` at `0x428af9`, `0x429257`, `0x42959d`,
`0x429ba8`, `0x429f57`, `0x42a2eb` — one per controller. The `0x52` then arrives later,
from `RestartWindowController` `0x428870`, as `FUN_0042c120(mode_timer->MessageField,
0x52, nullptr)`. [C]

### 1.2 The discriminator that does exist: the message code itself

`0x429510` (Rescue Maiden) is the one controller with an objective. Its completion arm
(`case 0x4a`, caller `a_sink18`, `prison_window_light_group` on-count `== 4`) cancels the
mode timer and then **jumps into the same `0x52` body** — but the body reads the incoming
message code back off `esi`:

```
  429a18:  83 fe 4a           cmp    esi,0x4a          ; param_1 == objective-hit?
  429a1b:  75 15              jne    0x429a32
  429a1d:  a1 1c 9d 43 00     mov    eax,ds:0x439d1c   ; info_text_box
  429a22:  6a 00              push   0x0
  429a24:  6a 4f              push   0x4f
  429a26:  c7 40 06 02 00 00 00  mov DWORD PTR [eax+0x6],0x2   ; mode = Idle
  429a2d:  e8 ee 25 00 00     call   0x42c020          ; re-dispatch MissionComplete
  429a32:  e8 c9 a5 ff ff     call   0x424000          ; FinishQuest -- BOTH branches
```

(`esi` is loaded from `[esp+0x50]`, i.e. the first stdcall argument, at `0x429515`; the
function is `ret 0x8` at `0x429ace`.)

So the original author **had** the outcome in hand at the exact instruction before the
booking call, tested it, and deliberately branched on it for the mode only. On success
the quest skips the ten-second restart window and returns straight to Idle; on timeout or
drain it does not. `call 0x424000` is outside the `if`. [C]

The three entries into that shared body are therefore distinguishable, and distinguished:

| entry | `esi` | reached from |
|---|---|---|
| objective met | `0x4a` | `0x429510` own `case 0x4a`, `a_sink18`, 4 windows lit |
| teardown | `0x52` | `0x428870` restart window closing unused |
| abort (drain) | `0x53` | `Dragon_drain_Control` `0x424900`, `FUN_0042c020(0x53, …)` |

### 1.3 Skulduggery — the two paths are the *same instruction sequence*

Worse (for the "they differ" hypothesis): in `PIRATES.EXE` every teardown is raised by the
identical call `FUN_0042d850(0x52, nullptr)`. A grep over the whole decompiled corpus for
any other use of `0x52` returns only the two dispatcher tests (`0x42d900` line 19,
`0x42d850` line 44) and one false positive (a float offset in `0x42daa0`). The 18 raise
sites, all with the same two arguments:

* timer expiry, one per controller: `0x42ad70`, `0x42b090`, `0x42b420`, `0x42c070`,
  `0x42c430`, `0x42ca00`, `0x42cc10`, `0x42cfc0`
* drain-abort `case 0x53`, one per controller: same eight
* Peg Leg's Loot payout sequence finished (`0x42cfc0`, the `default:` arm after step 9)
* — and nothing else.

Skulduggery's only *objective-completion* exit (Peg Leg's Loot running its nine-step
payout to the end) and its *timeout* exit are literally the same call with the same
arguments. There is no register, no flag, no mode-scratch cell that differs. [C]

**No state is set before `0x52` that `FinishQuest` or its callees read.** The only cell
that could serve — `a_kout2->MessageField` — is the *selection*, not the outcome; see §2.

---

## 2. Does the Quest Light get lit on a failed quest?

**Verdict: yes, unconditionally, on timeout and on drain alike. [C]**

`FinishQuest` is `0x424000`, 76 bytes, `void(void)` — it takes no outcome argument
because it takes no argument at all:

```
  424000:  a1 74 a8 43 00     mov    eax,ds:0x43a874        ; a_kout2
  424005:  83 78 06 00        cmp    DWORD PTR [eax+0x6],0x0
  424009:  74 3b              je     0x424046
  42400b:  68 20 a1 07 00     push   0x7a120                ; 500000
  424010:  e8 8b f8 ff ff     call   0x4238a0               ; AddToJackpot
  424015:  a1 f4 ab 43 00     mov    eax,ds:0x43abf4        ; v_lite28
  42401a:  6a 00              push   0x0
  42401c:  6a 13              push   0x13                   ; LightResetAndTurnOn
  42401e:  ff 40 06           inc    DWORD PTR [eax+0x6]     ; quests completed++
  424021:  8b 0d 7c a0 43 00  mov    ecx,DWORD PTR ds:0x43a07c ; a_rockb
  424027:  8b 51 06           mov    edx,DWORD PTR [ecx+0x6]   ; selected light index
  42402a:  8b 04 95 58 bb 43 00  mov eax,DWORD PTR [edx*4+0x43bb58] ; QuestLights[i]
  424031:  8b 48 04           mov    ecx,DWORD PTR [eax+0x4]
  424034:  8b 01              mov    eax,DWORD PTR [ecx]
  424036:  ff 50 04           call   DWORD PTR [eax+0x4]      ; ->Message(0x13, 0.0)
  424039:  8b 0d 74 a8 43 00  mov    ecx,DWORD PTR ds:0x43a874
  42403f:  c7 41 06 00 00 00 00  mov DWORD PTR [ecx+0x6],0x0  ; clear selection
  424046:  e9 45 fe ff ff     jmp    0x423e90                 ; tail: SelectNextQuest
```

The guard at `0x424005` is **"is a Quest-Light quest currently selected"**, not "did the
player win". Proof: the only writer of `a_kout2->MessageField` to a non-zero value is
`SelectNextQuest` `0x423e90` at `0x423e90+…` (`*(a_kout2 + 6) = uVar1`, the chosen state
4..8), and the only two clearers are `0x423e50` (cancel a selection, also turning that
light off with `0x14`) and `0x42403f` itself. `StartSelectedQuest` `0x424210` does **not**
clear it — it only copies it into `info_text_box->MessageField` — so the cell is still set
for the whole run of the quest, and the guard is true at every `0x52`. Exhaustive: a grep
for `DAT_0043a874` across the Dragon corpus gives exactly nine sites, and only
`0x423e90`, `0x423e50`, `0x424000` touch `+6`. [C]

The Quest Lights array `PTR_PTR_0043bb58` = `{v_lite71, v_lite72, v_lite73, v_lite74,
v_lite75}` is touched from exactly three places in the entire binary:

| site | message | meaning |
|---|---|---|
| `0x423e90` (`+0x73`) | `7`, `-1.0` | **flash forever** — quest selected |
| `0x423e50` (`+0x17`) | `0x14` | **off** — selection cancelled |
| `0x424000` @ `0x42401c` | `0x13` | **solid on** — quest booked |

plus `0x42a150`'s `0x52` arm, which turns all five off after Slay Dragon. [C]

So: light solid ⇔ `FinishQuest` ran with a selection live ⇔ the quest was *played to an
end*, by any route. Draining mid-quest counts: `Dragon_drain_Control` `0x424900` sends
`FUN_0042c020(0x53, param_2)`, every controller's `case 0x53` falls through into `0x52`,
and `0x52` calls `0x424000`. [C]

---

## 3. Does the 100,000-per-quest drain bonus count failed quests?

**Verdict: yes. Same single `inc`, same guard. [C]**

### 3.1 The only writer

`v_lite28->MessageField` (`DAT_0043abf4 + 6`) is incremented at exactly one address in
`DRAGON.EXE`: `0x42401e`, inside `FinishQuest`, inside the selection guard. A grep for
`DAT_0043abf4` over the Dragon corpus returns twelve sites; eleven read it (as a *light*,
for the Dragon Breath spell) and one writes `+6`. [C]

### 3.2 The only reader that pays

The end-of-ball bonus tally `0x42b7c0`, case 1:

```
  42b940:  a1 f4 ab 43 00     mov    eax,ds:0x43abf4
  42b945:  8b 58 06           mov    ebx,DWORD PTR [eax+0x6]   ; quests completed
  42b948:  e8 03 85 ff ff     call   0x423e50                  ; cancel pending selection
  42b94d:  85 db              test   ebx,ebx
  42b94f:  0f 84 5d ff ff ff  je     0x42b8b2
  42b955:  8b c3 / shl 5 / sub / lea / lea / shl 5              ; ebx * 100000
  42b968:  01 05 00 4d 43 00  add    DWORD PTR ds:0x434d00,eax  ; bonus accumulator
  42b97d:  68 c5 0b 00 00     push   0xbc5                      ; string 3013 "Quest Bonus\n%ld"
```

`(ebx<<5 - ebx)` = 31·ebx; `[ebx + eax*4]` = 125·ebx; ×5, ×5, `<<5` → **×100,000**. [C]

Nothing between the `inc` and this read filters by outcome. A quest that timed out is
worth the same 100,000 as one that was won.

### 3.3 Slay Dragon does not double-count

`0x42a150`'s `0x52` also calls `0x424000` (at `0x42ac0a`), but Slay Dragon is entered via
`0x423e90`'s "no unlit Quest Light remains" branch, which returns 9 *without* writing
`a_kout2->MessageField` — and `0x423e50`, called at the top of `0x423e90`, has just
cleared it. So the guard at `0x424005` is false and the body is skipped; only the tail
`jmp 0x423e90` runs. Same for the restart loop: `Dragon_a_sink1_Control` `0x425ea0`'s
mode-3 arm restores the stored quest and sets `mode_timer->MessageField = -1`, so the
restart window's own teardown is bypassed and `FinishQuest` runs exactly once per quest.
[C]

---

## 4. The same question for Skulduggery's modes

**Verdict: identical, and the dispatcher makes it worse. No discriminator exists at all —
not even an unused one. [C]**

### 4.1 The counter

`v_lite606->MessageField` ("modes completed this game") is incremented at exactly eight
addresses, one in each mode controller's `case 0x52`, each immediately after
`call 0x42ab40`:

| mode state | controller | `inc` at |
|---|---|---|
| 4 Attack | `0x42ad70` | `0x42b004` |
| 5 Treasure Hunt | `0x42c070` | `0x42c39f` |
| 6 Storm | `0x42c430` | `0x42c953` |
| 7 Tavern | `0x42b420` | `0x42b798` |
| 8 Bermuda | `0x42b090` | `0x42b315` |
| 9 Mutiny | `0x42ca00` | `0x42cb8d` |
| 10 Sword Fight | `0x42cc10` | `0x42cdf3` |
| 11 Peg Leg's Loot | `0x42cfc0` | `0x42d2d8` |

[C]

### 4.2 The dispatcher pays and lights on the same path

`FUN_0042d900`, verified byte for byte:

```
  42d900:  56 57              push esi / push edi
  42d902:  8b 7c 24 10        mov    edi,DWORD PTR [esp+0x10]   ; code
  42d906:  83 ff 4f           cmp    edi,0x4f
  42d909:  75 0e              jne    0x42d919
  ...                                                           ; 0x4f -> light flashing
  42d919:  8b 74 24 0c        mov    esi,DWORD PTR [esp+0xc]    ; state
  42d91d:  83 ff 52           cmp    edi,0x52
  42d920:  75 21              jne    0x42d943
  42d922:  83 fe 04           cmp    esi,0x4
  42d925:  7c 0f              jl     0x42d936
  42d927:  83 fe 0b           cmp    esi,0xb
  42d92a:  7f 0a              jg     0x42d936
  42d92c:  68 b0 71 0b 00     push   0xb71b0                    ; 750000
  42d931:  e8 1a 5d ff ff     call   0x423650                   ; AddToJackpot
  42d936:  6a 00 / 56         push 0 / push esi
  42d939:  e8 62 7c ff ff     call   0x4255a0                   ; SetModeWheelLight(state, solid)
  42d93e:  e8 0d 7f ff ff     call   0x425850                   ; SelectNextMode
```

Only two values reach this code: `esi` = the mode state, `edi` = the message code, which
is always exactly `0x52` here. `FUN_004255a0`'s second argument selects the light message
arithmetically — `cVar1 = (-(param_2 == 0) & 0xc) + 7`, i.e. `7` (flash) when flashing,
`0x13` (solid on) when not — so the wheel light goes solid, which is what makes
`FUN_00425850` stop offering that mode. [C]

So a Skulduggery mode earns, merely by having been *started* and then ended any way at
all: `+750,000` to the jackpot, a solid wheel light (wheel progression),
`v_lite606->MessageField += 1` worth **200,000** each at the drain bonus
(`0x42b990` reads it, `0x42b9b3` adds `×200,000` — 125·5·5·`<<6`), and **1,000,000** each
in the Peg Leg's Loot finale payout (`0x42d234` reads it). [C]

### 4.3 The one place a mode "fails" explicitly — and is still booked

Storm (`0x42c430`) is the only mode in either table with a player-triggered early end,
and it is a **loss**, not a win. On any hideaway sink (`a_sink17`, `a_sink15`, `a_sink14`):

```
  42c5cb:  6a 00
  42c5cd:  68 ea 0b 00 00     push   0xbea                 ; string 3050 "Shipwrecked"
  42c5d2:  e8 59 d1 fd ff     call   0x409730              ; GetRcString
  ...      display for 2.0 s
  42c5ec:  56                 push   esi                   ; the sink
  42c5ed:  6a 53              push   0x53                  ; ModeAbort
  42c5ef:  e8 3c fe ff ff     call   0x42c430              ; direct self-call, not a broadcast
  42c5f4:  68 00 00 00 40     push   0x40000000            ; 2.0
  42c5f9:  e8 32 74 ff ff     call   0x423a30              ; LightShipwreckMystery
```

`case 0x53` cancels the mode timer and calls `FUN_0042d850(0x52, nullptr)` — the ordinary
teardown. The player is told, in words, that they have been shipwrecked, and the game then
counts the mode as completed, pays the 750,000 and lights the wheel. [C]

`analysis/pirates-rules.md` §2.4 lists this under "**Objective met** — the mode's own code
broadcasts `0x53` (Storm's hideaway bail-out)". That is a misreading: string 3050 is
"Shipwrecked" and the consolation `LightShipwreckMystery` confirms it. It belongs under
failure. `src/pirates_modes.cpp:1428-1441` implements the behaviour correctly but its
comment calls it a "bail-out ... consolation", which understates it.

---

## 5. Is there a distinct "quest failed" string, sound or animation that the reconstruction never reaches?

**Verdict: no in Dragon's Keep; in Skulduggery there is exactly one failure string, 3050
"Shipwrecked", and the reconstruction already reaches it. [C]**

### 5.1 Method

`analysis/dragon-strings.txt` and `analysis/pirates-strings.txt` hold the complete
`RT_STRING` tables, 133 lines each, ids 1001-1020, 2001-2039, 3001-3076. Every id in the
table-rules block 3001-3076 was looked for as a `push imm32` in `.text` (the calling
convention for `FUN_00409730`, `GetRcString`), by scanning the raw section bytes rather
than the decompiled corpus — the corpus does not cover every function, so a grep over
`decomp/` alone produces false "unreferenced" hits.

### 5.2 Result

Every string 3001-3076 in both binaries has at least one `push` site, with five exceptions
that are **completely dead** — the 4-byte little-endian id does not occur anywhere in the
whole file, let alone in `.text`:

| binary | dead id | text |
|---|---|---|
| DRAGON.EXE | 3005 | `Shoot Again` |
| DRAGON.EXE | 3018 | `Jackpot\n%ld` |
| DRAGON.EXE | 3064 | `Dragon Loop\n%ld Added to Jackpot` |
| PIRATES.EXE | 3009 | `Shipwreck Mystery\n%ld` |
| PIRATES.EXE | 3013 | `WINDS\n%ld` |

(3005 and 3018 are live in `PIRATES.EXE` at `0x4291be` and `0x42617a` — the two `.rc`
files share a numbering.) None is failure-flavoured; none is a lost "quest failed"
announcement. [C]

### 5.3 Dragon's Keep has no failure vocabulary at all

Reading the whole 3001-3076 block: the only outcome-flavoured strings are successes —
3034 `Maiden Rescued\nExtra Ball Available` (pushed once, at `0x427a02`, inside the
prison-window trip handler `0x427880`, i.e. the award ladder of the one quest that has an
objective), 3029 `Fire Lizard Slain`, 3050 `Super Jackpot`, 3004 `Pass Clear` — plus
3067 `Restart Quest\n%ld`, which is the restart window's own prompt and the closest the
table comes to acknowledging a timeout. There is no "Quest Failed", "Time Up", "Dragon
Escaped" or equivalent, reachable or dead. [C]

A second check on sounds: every quest's `0x52` arm was read in full
(`0x429052`, `0x4294cb`, `0x429a32`, `0x429e7d`, `0x42a111` and their surrounding
instructions). None plays a wave; the only wave calls in the teardown region are the
`0x18` *stop* calls on the per-quest voice (`tabc46wave`, `tabc48wave`, `tabc50wave`,
`tabc53wave`, `tabc55wave`), which are issued on the `0x51` restart arm, not on `0x52`.
`0x429a18`'s success branch adds no sound either. [C] for "no distinct teardown sound",
**[P]** for "no distinct animation" — the `.DAT` was not examined and a failure animation
could in principle live in a light-group override this reading did not recognise.

---

## Verdict table

| # | question | verdict |
|---|---|---|
| 1 | identical paths into `0x52`? | **No.** Timeout goes via the restart window (mode 3) in Dragon; the objective path (`0x4a`) jumps straight in. The teardown *reads* the difference (`cmp esi,0x4a` at `0x429a18`) and uses it to pick the follow-on mode, never to gate the booking. In Skulduggery the two paths are the identical call `FUN_0042d850(0x52, nullptr)`. [C] |
| 2 | Quest Light lit on a failed quest? | **Yes**, at `0x42401c`, guarded only by "a quest is selected" (`0x424005`). Timeout and drain both reach it. [C] |
| 3 | 100,000/quest bonus counts failures? | **Yes.** Single `inc` at `0x42401e`; single reader at `0x42b940`/`0x42b968`, `×100,000`. [C] |
| 4 | Skulduggery the same? | **Yes, more so.** Eight `inc` sites (table in §4.1), plus `+750,000` and the solid wheel light in `0x42d900` at `0x42d92c`/`0x42d939`, all on one outcome-blind path. Its one explicit *failure* ("Shipwrecked", `0x42c5cd`) books the mode as completed. [C] |
| 5 | unreached failure string/sound? | **No.** Five dead strings exist (§5.2), none failure-flavoured. Dragon has no failure vocabulary. Skulduggery's 3050 "Shipwrecked" is live and already implemented. [C] (animations **[P]**) |

**The current reconstruction's behaviour is confirmed. Its stated reason is not.** The
sentence "No success/failure discriminator exists anywhere in this path" is false for
Dragon's Keep: one exists, at `0x429a18`, and is used. What is true is the stronger claim
that the discriminator is deliberately not applied to booking — and that four of Dragon's
five quests have no success condition to discriminate, so there was never anything to
apply it to.

---

## Concrete source changes that follow

None of these changes behaviour. The code is right; the comments assert something the
binary contradicts, and this project has been bitten before by a confident wrong claim
surviving in a comment.

### `src/dragon_missions.cpp`

1. **Lines 1153-1161, the `FinishQuest` header comment.** Replace "No success/failure
   discriminator exists anywhere in this path. Settled by instrumenting a real game" with
   the confirmed finding: `0x424000` takes no argument and is called from all six `0x52`
   sites (`0x429052`, `0x4294cb`, `0x429a32`, `0x429e7d`, `0x42a111`, `0x42ac0a`); the
   guard at `0x424005` tests the *selection* cell `a_kout2->MessageField`, not an outcome;
   the one discriminator that exists, `cmp esi,0x4a` at `0x429a18`, is used for the
   follow-on mode only and sits immediately before the unconditional `call 0x424000` at
   `0x429a32`. Drop "unresolved" — it is resolved.

2. **Same comment: add the structural argument.** Dragon Hoard, Fire Lizards, Dragon Pass
   and Wizard's Fury have no completion test at all (`0x428a80`, `0x4291c0`, `0x429b10`,
   `0x429ec0` — no `0x4a` exit), so gating the booking on success would make four of the
   five Quest Lights unlightable and `0x423e90`'s all-five-lit branch unreachable.

3. **Lines ~1875-1895, the Rescue Maiden shared teardown.** The `if (objectiveMet)` block
   is a faithful reconstruction of `0x429a18`-`0x429a2d`; say so by address, and say
   explicitly that `FinishQuest()` sits outside it on purpose, matching `0x429a32`.

4. **No code change.** `objectiveMet` is already implemented, `FinishQuest()` is already
   unconditional, and the drain path already books.

### `src/pirates_modes.cpp`

5. **Lines 18-23 and 2299-2308, the two "Mode completion is never checked" blocks.**
   Keep the conclusion; replace "No success/failure discriminator exists anywhere in the
   dispatcher or in any of the eight controllers" with the verified mechanism: all 18
   raise sites are the identical `FUN_0042d850(0x52, nullptr)`, so there is genuinely no
   value to discriminate on (unlike Dragon, where one exists and is ignored); cite
   `0x42d900` `0x42d91d`/`0x42d92c`/`0x42d939` and the eight `inc` addresses of §4.1.

6. **Lines 1428-1441, the Storm hideaway arm.** Retitle it: this is a **failure**, not a
   "bail-out ... consolation". Cite `0x42c5cd` (string 3050 "Shipwrecked") and the direct
   self-call `push 0x53; call 0x42c430` at `0x42c5ed`-`0x42c5ef`. Note as the decisive
   evidence that even an explicitly announced failure is booked as a completed mode.

7. **Line 2309, "Whether that is the intended design is not answerable from the binary."**
   It now is, as far as a binary can answer it: the author tested the outcome at
   `0x429a18` and chose not to gate on it, and wrote a failure announcement at `0x42c5cd`
   that still books. Soften to "intended" rather than "unanswerable", or drop the line.

### Elsewhere (owned by other agents, listed for them, not to be edited from here)

8. `docs/integration.md`, "What 'it runs' does not mean" → the first bullet ("Success
   versus timeout is undiscriminated on both tables ... watching a real game settles it")
   can be promoted from open question to settled finding, pointing here.
9. `analysis/pirates-rules.md` §2.4 → move "Storm's hideaway bail-out" out of
   "**Objective met**" and into a failure row; string 3050 is "Shipwrecked".
10. `analysis/dragon-quest-system.md` line 223, the "**Important caveat [P]**" → upgrade
    to **[C]** with the `0x429a18` evidence.
