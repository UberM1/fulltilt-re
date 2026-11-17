# Rest-state comparison against the originals

Phase A of a differential harness. It compares the port with `DRAGON.EXE` / `PIRATES.EXE`
running under Wine at one point: a new game started, the first ball fed into the plunger
lane and left there, nothing launched yet. Both sides write the same JSONL records: one
`meta` line, one `table` line (score, ball count, multiplier, jackpots, mode register,
...), one `tag` line per entry of the table's `score_components[]` /
`simple_components[]` (name and the component it is bound to), and one `component` line
per entry of the table's component list (name, class code, `ActiveFlag`, `MessageField`,
plus per-class state for lights, light groups, moving blockers, kickbacks, bumpers and
timers).

Both originals start a game by themselves after loading, and so does the port, so no
input is sent on either side.

## 1. Native dump

```sh
env -u LD_LIBRARY_PATH test/restdump/native_dump.sh dragon OUT/native-dragon.jsonl OUT/work
env -u LD_LIBRARY_PATH test/restdump/native_dump.sh pirate OUT/native-pirate.jsonl OUT/work
```

The script builds with Nix (or takes `FULLTILT_SCP_BIN`), stages the table in its own
directory and runs the engine headless with `FT_RESTDUMP=<out>`. The hook
(`reference/SpaceCadetPinball/FtRestDump.cpp`, called from `pb::frame`, inert without the
variable) waits until exactly one ball is in play, still, in the plunger-lane strip, for
`FT_RESTDUMP_DELAY` seconds of game time (default 2), writes the file and exits. Two runs
give byte-identical files.

## 2. Original under Wine

```sh
nix-shell -p wineWow64Packages.stagingFull xorg-server --run \
  'env -u WAYLAND_DISPLAY -u XDG_RUNTIME_DIR -u PULSE_SERVER -u PIPEWIRE_REMOTE \
     PULSE_SERVER=unix:/nonexistent SDL_AUDIODRIVER=dummy \
     python3 test/restdump/wine_probe.py dragon OUT/wine-dragon.jsonl --work OUT/wine --display 96'
```

(`pirate` likewise.) The probe starts its own Xvfb on the given display (91-139; it
refuses one whose lock file exists), creates a private `WINEPREFIX` under `--work` on first
use, stages the EXE, DAT and `SOUND/`, starts the game, and reads `/proc/<pid>/mem`. It
makes itself a child subreaper first: Wine re-parents the game to init, and under Yama
`ptrace_scope=1` only an ancestor may read its memory. It takes `--samples` snapshots (default
2) `--interval` seconds apart once the ball has been still for `--settle` seconds.

**Sound.** The probe cuts every route to a sound device itself: no `XDG_RUNTIME_DIR`,
`PULSE_SERVER=unix:/nonexistent`, no `PIPEWIRE_REMOTE`, an `ALSA_CONFIG_PATH` that defines
no card, Wine's audio drivers disabled by DLL override, and
`HKCU\Software\Wine\Drivers "Audio"=""` written and checked before every launch (it
refuses to start the game if the key is not there). While waiting it also checks the
game process for an open `/dev/snd` device or a mapped PulseAudio/PipeWire library and
kills the run if it finds one. The environment on the command line above is belt and
braces; keep it.

The addresses and offsets it reads, and where each was read from, are commented at the
top of `wine_probe.py`. Nothing from the executables is copied into the repository.

## 3. Compare

```sh
python3 test/restdump/compare.py OUT/native-dragon.jsonl OUT/wine-dragon.jsonl [more wine dumps...]
```

Components are matched by group name, tags by array position. Any field that differs
between Wine samples (or Wine runs) is listed as VOLATILE and not compared. Fields only one
side records are not compared. Unnamed components are only counted. Exit 1 when anything
differs.

## Limits

- One point in time. The originals step on wall-clock time, so anything that moves on a
  timer (Skulduggery's skill-shot slot in `s_launcher1`'s `MessageField`, flasher blink
  phase) is volatile or left out.
- The class codes differ by design: the native dump records the DAT object type, the
  original's `GetComponentType` answers per class; `compare.py` folds the known aliases
  (1010->1000, 1029->1012, 1034->1001, 1003/1004->1041).
- `TKickback`'s `kick_active`/`suppressed` offsets (+0x82/+0x86) are not fully verified;
  see the comment in `wine_probe.py`.
