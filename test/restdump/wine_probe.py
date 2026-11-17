#!/usr/bin/env python3
"""Rest-state dump of the ORIGINAL table under Wine: step 2 of test/restdump/README.md.

    wine_probe.py dragon|pirate OUT.jsonl --work DIR [--display 96] [--samples 2]

Runs DRAGON.EXE or PIRATES.EXE under Wine on a private Xvfb display, waits for the
game the original starts by itself to feed its first ball, lets the ball sit in the
plunger lane, and reads every component's state out of /proc/<pid>/mem. The records
have the same shape as the native hook's (reference/SpaceCadetPinball/FtRestDump.cpp)
so test/restdump/compare.py can diff them.

It must be run from a nix-shell that provides wine, Xvfb (see the README); the
environment it gives Wine is built here: a private WINEPREFIX under --work, no audio
driver, HOME/XDG_* inside --work, WAYLAND_DISPLAY removed, DISPLAY set explicitly.

Reading another process's memory needs ptrace rights over it. Under Yama
ptrace_scope=1 only an ancestor has them, and Wine re-parents the game process to
init, so this script makes itself a child subreaper (prctl PR_SET_CHILD_SUBREAPER)
before starting Wine: orphaned descendants then re-parent to it and it stays their
ancestor.

Nothing from the executables is copied anywhere; only addresses and offsets are
written here. Where each offset comes from is cited next to it.

Stdlib only.
"""

import argparse
import ctypes
import json
import os
import shutil
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# ---------------------------------------------------------------------------
# Per-table addresses (both EXEs share the engine code at the same addresses; only
# .data differs).
#
#   table_ptr   control's TableG. Written by make_links: DRAGON.EXE 0x422ff0
#               `mov ds:0x439b00,eax`, PIRATES.EXE 0x423010 `mov ds:0x43ab9c,eax`.
#   score       score_components[]: 16-byte {tag*, func, count, scores*} records.
#               make_links loads `mov edi,0x43afe0` / `mov esi,0x4b` (DRAGON 0x422fe6)
#               and `mov esi,0x43c490` / `mov edi,0x4a` (PIRATES 0x423006).
#   simple      simple_components[]: tag pointers, walked up to an end address:
#               DRAGON 0x43b490..0x43b6e8 (0x42301c/0x42302c),
#               PIRATES 0x43c930..0x43cc30 (0x42303c/0x42304c).
# A tag is {const char* Name; TPinballComponent* Component;} (analysis/dump_tags.py).
TABLES = {
    "dragon": dict(dir="DRAGON", exe="DRAGON.EXE", dat="DRAGON.DAT", name="Dragon's Keep",
                   table_ptr=0x439B00, score=(0x43AFE0, 0x4B), simple=(0x43B490, 0x43B6E8)),
    "pirate": dict(dir="PIRATE", exe="PIRATES.EXE", dat="PIRATES.DAT", name="Skulduggery",
                   table_ptr=0x43AB9C, score=(0x43C490, 0x4A), simple=(0x43C930, 0x43CC30)),
}

# TPinballTable (byte-packed; analysis/symbols-cadet.md section 3.5 plus AddScore at
# 0x419f10, identical in both EXEs: CurScore +0x6e, CurScoreE9 +0x72, JackpotScore
# +0x172 under flag +0x176, SuperJackpotScore +0x17a under flag +0x17e, BonusScore
# +0x16a under flag +0x16e, ScoreMultiplier +0x15e, ScoreAdded +0x162).
TABLE_FIELDS = [
    ("score", 0x6E), ("score_e9", 0x72), ("player_count", 0x102), ("current_player", 0x106),
    ("score_multiplier", 0x15E), ("score_added", 0x162), ("bonus_score", 0x16A),
    ("bonus_score_flag", 0x16E), ("jackpot_score", 0x172), ("jackpot_score_flag", 0x176),
    ("super_jackpot_score", 0x17A), ("super_jackpot_score_flag", 0x17E), ("ball_count", 0x186),
    ("max_ball_count", 0x18A), ("extra_balls", 0x18E), ("multiball_count", 0x192),
    ("ball_locked_counter", 0x196), ("multiball_flag", 0x19A), ("replay_active_flag", 0x1A2),
    ("tilt_lock_flag", 0x1B2),
]
FLAG_FIELDS = {"bonus_score_flag", "jackpot_score_flag", "super_jackpot_score_flag",
               "multiball_flag", "replay_active_flag", "tilt_lock_flag"}

# TPinballTable+0x12a: the component list, an objlist {?, int Count @+4, ptr[] @+8}.
# make_component_link walks it (PIRATES.EXE 0x422f5d..0x422f8c) and compares each
# entry's +0xa against the tag name, so +0xa is GroupName.
TABLE_COMPONENT_LIST = 0x12A

# TPinballComponent: +0 vtable, +5 char ActiveFlag, +6 int MessageField, +0xa GroupName
# (symbols-cadet.md 3.1). Vtable slot 0 is GetComponentType, `mov eax,imm32; ret`,
# which gives each object's class code (symbols-cadet.md 3.2).
OFF_ACTIVE, OFF_MSGFIELD, OFF_NAME = 0x5, 0x6, 0xA

# TLight, from TLight::Message at 0x40a490 (decomp/dragon_engine/functions/0x40a490.c):
# case 2 returns +0x42 (LightOnFlag), case 3 returns +0x46 (FlasherOnFlag); case 8
# (TurnOffTimed) sets +0x4a (ToggledOffFlag); light_on() tests +0x42/+0x4e/+0x46
# (ToggledOnFlag +0x4e); case 0x10 sets +0x52 (TurnOffAfterFlashingFg); case 0xb
# writes +0x56 (LightOnBmpIndex); cases 0x18/0x19 set +0x5a (TemporaryOverrideFlag)
# and arm +0x5e; PlayerChanged kills +0x86 (TimeoutTimer) and +0x76 (FlashTimer).
LIGHT_FIELDS = [("light_on", 0x42), ("flasher_on", 0x46), ("toggled_off", 0x4A),
                ("toggled_on", 0x4E), ("turn_off_after_flashing", 0x52), ("on_bmp_index", 0x56),
                ("tmp_override", 0x5A)]
LIGHT_TIMEOUT_TIMER = 0x86

# TLightGroup, from TLightGroup::Message at 0x40af80: the light list pointer at +0x46
# (count at [+4], entries at [+8]); animation codes are stored to +0x52 (MessageField2)
# with +0x56 (AnimationFlag) set beside them; PlayerChanged saves +0x52 and +0x4a
# (Timer1Time) per player.
GROUP_LIST, GROUP_TIMER1, GROUP_MSG2, GROUP_ANIM = 0x46, 0x4A, 0x52, 0x56

# TBlocker (Full Tilt's moving part), from TBlocker::Message at 0x414f80: per-position
# flag bytes at +0x76.., CurrentPosition +0x7e, HomePosition +0x82 (what Reset and
# PlayerChanged move to), timeout timer +0x8e (code 0x45), TargetPosition +0x92,
# motion timer +0x9a.
BLK_FLAGS, BLK_CUR, BLK_HOME, BLK_TIMEOUT, BLK_TARGET, BLK_MOTION = 0x76, 0x7E, 0x82, 0x8E, 0x92, 0x9A

# TKickback, TKickback::Message at 0x415d70: code 0x4c sets +0x86 (Suppressed),
# Reset clears +0x86 and +0x82 (read here as KickActiveFlag; the timer callback at
# 0x415d49 tests +0x82 before raising 0x46). UNVERIFIED pairing: a constructor-like
# block at 0x415ea5 stores floats into the same two offsets.
KICK_ACTIVE, KICK_SUPPRESSED = 0x82, 0x86

# TBumper +0x76 BmpIndex (symbols-cadet.md 3.4). TTimer +0x42 its timer handle
# (TTimer::Message 0x41b380, code 0x45 arms it).
BUMPER_BMP, TIMER_HANDLE = 0x76, 0x42

# TBall +0x9f/+0xa3 Position (symbols-cadet.md 3.4).
BALL_X, BALL_Y = 0x9F, 0xA3

PR_SET_CHILD_SUBREAPER = 36


class Mem:
    def __init__(self, pid):
        self.pid = pid
        self.f = open(f"/proc/{pid}/mem", "rb", buffering=0)

    def read(self, addr, n):
        self.f.seek(addr)
        data = self.f.read(n)
        if len(data) != n:
            raise OSError(f"short read at {addr:#x}")
        return data

    def u8(self, a):
        return self.read(a, 1)[0]

    def i32(self, a):
        return struct.unpack("<i", self.read(a, 4))[0]

    def u32(self, a):
        return struct.unpack("<I", self.read(a, 4))[0]

    def f32(self, a):
        return struct.unpack("<f", self.read(a, 4))[0]

    def cstr(self, a, limit=64):
        if not a:
            return None
        try:
            raw = self.read(a, limit)
        except OSError:
            return None
        end = raw.find(b"\0")
        return raw[: end if end >= 0 else limit].decode("latin-1")


def fmt_float(f):
    # Matches the native "%.6g".
    return float("%.6g" % f)


def type_code(mem, comp):
    try:
        fn = mem.u32(mem.u32(comp))
        code = mem.read(fn, 6)
    except OSError:
        return 0
    if code[0] == 0xB8 and code[5] == 0xC3:
        return struct.unpack("<i", code[1:5])[0]
    return 0


def objlist(mem, lst):
    if not lst:
        return []
    n = mem.i32(lst + 4)
    if n < 0 or n > 4096:
        raise OSError(f"implausible list count {n}")
    return [mem.u32(lst + 8 + 4 * i) for i in range(n)]


def component_record(mem, index, comp, tagged):
    t = type_code(mem, comp)
    rec = {"kind": "component", "index": index, "name": mem.cstr(mem.u32(comp + OFF_NAME)),
           "type": t, "tagged": tagged, "active": 1 if mem.u8(comp + OFF_ACTIVE) else 0,
           "message_field": mem.i32(comp + OFF_MSGFIELD)}
    if t == 1002:
        for k, o in LIGHT_FIELDS:
            v = mem.i32(comp + o)
            rec[k] = v if k == "on_bmp_index" else (1 if v else 0)
        rec["timeout_running"] = 1 if mem.u32(comp + LIGHT_TIMEOUT_TIMER) else 0
    elif t in (1026, 1030):
        lights = objlist(mem, mem.u32(comp + GROUP_LIST))
        rec["message_field2"] = mem.i32(comp + GROUP_MSG2)
        rec["animation_flag"] = 1 if mem.i32(comp + GROUP_ANIM) else 0
        rec["timer1_time"] = fmt_float(mem.f32(comp + GROUP_TIMER1))
        rec["light_count"] = len(lights)
        rec["lights_on"] = sum(1 for l in lights if l and mem.i32(l + 0x42))
        rec["members"] = [mem.cstr(mem.u32(l + OFF_NAME)) if l else None for l in lights]
    elif t == 1011:
        motion = mem.u32(comp + BLK_MOTION)
        rec["current_position"] = mem.i32(comp + BLK_CUR)
        rec["home_position"] = mem.i32(comp + BLK_HOME)
        rec["target_position"] = mem.i32(comp + BLK_TARGET) if motion else -1
        rec["motion_running"] = 1 if motion else 0
        rec["timeout_running"] = 1 if mem.u32(comp + BLK_TIMEOUT) else 0
        rec["position_flags"] = [1 if b else 0 for b in mem.read(comp + BLK_FLAGS, 8)]
    elif t == 1014:
        rec["kick_active"] = 1 if mem.i32(comp + KICK_ACTIVE) else 0
        rec["suppressed"] = 1 if mem.i32(comp + KICK_SUPPRESSED) else 0
    elif t == 1005:
        rec["bmp_index"] = mem.i32(comp + BUMPER_BMP)
    elif t == 1032:
        rec["running"] = 1 if mem.u32(comp + TIMER_HANDLE) else 0
    return rec


def snapshot(mem, spec, sample, elapsed):
    table = mem.u32(spec["table_ptr"])
    comps = objlist(mem, mem.u32(table + TABLE_COMPONENT_LIST))
    tags = []
    base, count = spec["score"]
    for i in range(count):
        tags.append(("score", i, mem.u32(base + 16 * i)))
    start, end = spec["simple"]
    for i, a in enumerate(range(start, end, 4)):
        tags.append(("simple", i, mem.u32(a)))

    out = [{"kind": "meta", "side": "wine", "table": spec["name"], "sample": sample,
            "seconds_after_feed": round(elapsed, 1), "score_components": count,
            "simple_components": (end - start) // 4, "component_count": len(comps)}]
    rec = {"kind": "table"}
    for k, o in TABLE_FIELDS:
        v = mem.i32(table + o)
        rec[k] = (1 if v else 0) if k in FLAG_FIELDS else v
    mode = 0
    for c in comps:
        if c and mem.cstr(mem.u32(c + OFF_NAME)) == "info_text_box":
            mode = mem.i32(c + OFF_MSGFIELD)
    rec["mode"] = mode
    out.append(rec)

    tagged = set()
    for array, i, tag in tags:
        comp = mem.u32(tag + 4) if tag else 0
        if comp:
            tagged.add(comp)
        out.append({"kind": "tag", "array": array, "index": i,
                    "name": mem.cstr(mem.u32(tag)) if tag else None,
                    "bound": mem.cstr(mem.u32(comp + OFF_NAME)) if comp else None})
    for i, c in enumerate(comps):
        if c:
            out.append(component_record(mem, i, c, c in tagged))
    return out


def find_game_pid(exe):
    """The Wine process running exe whose image base reads 'MZ'."""
    me = os.getpid()
    for d in Path("/proc").iterdir():
        if not d.name.isdigit() or int(d.name) == me:
            continue
        try:
            cmd = (d / "cmdline").read_bytes()
        except OSError:
            continue
        # The game's argv[0] is its Windows path, "Z:\...\DRAGON.EXE".
        argv0 = cmd.split(b"\0")[0].replace(b"\\", b"/").rsplit(b"/", 1)[-1]
        if argv0.lower() != exe.encode().lower():
            continue
        try:
            m = Mem(int(d.name))
            if m.read(0x400000, 2) == b"MZ":
                return m
        except OSError:
            continue
    return None


def audio_leak(pid):
    """A reason string if the game process holds a sound device or audio-server
    library, else None. Checked on every poll; a hit kills the run."""
    try:
        for fd in Path(f"/proc/{pid}/fd").iterdir():
            try:
                target = os.readlink(fd)
            except OSError:
                continue
            if target.startswith("/dev/snd") or target.startswith("/dev/dsp"):
                return f"fd {target}"
        maps = Path(f"/proc/{pid}/maps").read_text(errors="replace")
    except OSError:
        return None
    for lib in ("libpulse", "libpipewire", "libasound_module_pcm_pulse"):
        if lib in maps:
            return f"mapped {lib}"
    return None


def ball_positions(mem, spec):
    table = mem.u32(spec["table_ptr"])
    out = []
    for c in objlist(mem, mem.u32(table + TABLE_COMPONENT_LIST)):
        if c and type_code(mem, c) == 1037 and mem.u8(c + OFF_ACTIVE):
            out.append((round(mem.f32(c + BALL_X), 2), round(mem.f32(c + BALL_Y), 2)))
    return out


def stage(spec, work, game_dir):
    run = work / f"wine-{spec['dir']}"
    run.mkdir(parents=True, exist_ok=True)
    src = game_dir / spec["dir"]
    for f in (spec["exe"], spec["dat"]):
        shutil.copy2(src / f, run / f)
    if not (run / "SOUND").exists():
        shutil.copytree(src / "SOUND", run / "SOUND")
    return run


# Every route to a sound device is cut, not just the one Wine is asked to use: the
# audio-server sockets are hidden (no XDG_RUNTIME_DIR, PULSE_SERVER pointing nowhere,
# no PIPEWIRE_REMOTE), ALSA gets a configuration that defines no card at all (so
# neither PCM nor the MIDI sequencer can open a device), Wine's audio drivers are
# disabled by DLL override, and the prefix's HKCU\Software\Wine\Drivers "Audio" is
# set to "" before every game launch (set_no_audio).
AUDIO_ENV_DROP = ("WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "PULSE_SERVER", "PIPEWIRE_REMOTE",
                  "ALSA_CONFIG_PATH", "AUDIODEV")
ALSA_NULL_CONF = "pcm.!default { type null }\nctl.!default { type null }\n"


def wine_env(work, display):
    env = {k: v for k, v in os.environ.items() if k not in AUDIO_ENV_DROP}
    home = work / "home"
    home.mkdir(parents=True, exist_ok=True)
    alsa = work / "alsa-null.conf"
    alsa.write_text(ALSA_NULL_CONF)
    env.update(WINEPREFIX=str(work / "prefix"), WINEARCH="win64", WINEDEBUG="-all",
               WINEDLLOVERRIDES="mscoree,mshtml=;winealsa.drv,winepulse.drv,wineoss.drv,"
                                "winecoreaudio.drv=",
               PULSE_SERVER="unix:/nonexistent", SDL_AUDIODRIVER="dummy",
               ALSA_CONFIG_PATH=str(alsa),
               HOME=str(home), XDG_DATA_HOME=str(home / ".local/share"),
               XDG_CONFIG_HOME=str(home / ".config"), DISPLAY=f":{display}")
    return env


def set_no_audio(env):
    """HKCU\\Software\\Wine\\Drivers "Audio"="" -- no Wine audio driver at all."""
    r = subprocess.run(["wine", "reg", "add", r"HKCU\Software\Wine\Drivers", "/v", "Audio",
                        "/d", "", "/f"], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=120)
    subprocess.run(["wineserver", "-w"], env=env, timeout=120)
    reg = Path(env["WINEPREFIX"]) / "user.reg"
    text = reg.read_text(errors="replace") if reg.exists() else ""
    block = text.split("[Software\\\\Wine\\\\Drivers]", 1)
    if r.returncode != 0 or len(block) < 2 or '"Audio"=""' not in block[1].split("\n[", 1)[0]:
        sys.exit("could not disable audio in the Wine prefix; refusing to start the game")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("table", choices=sorted(TABLES))
    ap.add_argument("out")
    ap.add_argument("--work", required=True, help="scratch dir: prefix, run dir, logs")
    ap.add_argument("--display", type=int, default=96)
    ap.add_argument("--game-dir", default=str(REPO / "game/WIN95/FULLTILT"))
    ap.add_argument("--settle", type=float, default=2.0,
                    help="seconds the fed ball must sit still before the first sample")
    ap.add_argument("--samples", type=int, default=2)
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--timeout", type=float, default=120.0)
    a = ap.parse_args()

    if not 90 < a.display < 140 or Path(f"/tmp/.X{a.display}-lock").exists():
        sys.exit(f"display :{a.display} is out of range or in use")
    spec = TABLES[a.table]
    work = Path(a.work).resolve()
    work.mkdir(parents=True, exist_ok=True)
    run = stage(spec, work, Path(a.game_dir))
    env = wine_env(work, a.display)

    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
        sys.exit(f"prctl(PR_SET_CHILD_SUBREAPER) failed: errno {ctypes.get_errno()}")

    procs = []
    xlog = open(work / "xvfb.log", "w")
    procs.append(subprocess.Popen(["Xvfb", f":{a.display}", "-screen", "0", "1024x768x24",
                                   "-nolisten", "tcp"], stdout=xlog, stderr=xlog))
    time.sleep(2)
    if not (work / "prefix").exists():
        print("creating the Wine prefix...", file=sys.stderr)
        subprocess.run(["wineboot", "-i"], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=600)
        subprocess.run(["wineserver", "-w"], env=env, timeout=120)
    set_no_audio(env)
    wlog = open(work / f"wine-{spec['dir']}.log", "w")
    procs.append(subprocess.Popen(["wine", spec["exe"]], cwd=run, env=env, stdout=wlog,
                                  stderr=wlog))

    records = []
    try:
        t0 = time.monotonic()
        mem = None
        while mem is None:
            if time.monotonic() - t0 > a.timeout:
                sys.exit("the game process never appeared")
            time.sleep(0.5)
            mem = find_game_pid(spec["exe"])
        print(f"game pid {mem.pid}", file=sys.stderr)

        # Rest: a game running, exactly one ball in play, the ball (whichever ball
        # object is active and not the captive one -- the positions are compared
        # between polls, so a ball that never moves passes trivially) not moving.
        still_since, last = None, None
        while True:
            if time.monotonic() - t0 > a.timeout:
                sys.exit("the table never came to rest")
            time.sleep(0.25)
            leak = audio_leak(mem.pid)
            if leak:
                sys.exit(f"audio path open in the game process ({leak}); killed")
            try:
                table = mem.u32(spec["table_ptr"])
                if not table or mem.i32(table + 0x192) != 1:
                    still_since = None
                    continue
                pos = ball_positions(mem, spec)
            except OSError:
                still_since = None
                continue
            now = time.monotonic()
            if pos != last:
                last, still_since = pos, now
            elif now - still_since >= a.settle:
                break
        print(f"at rest, balls {last}", file=sys.stderr)
        first = time.monotonic()
        for s in range(a.samples):
            if s:
                time.sleep(a.interval)
            records += snapshot(mem, spec, s, a.settle + time.monotonic() - first)
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.send_signal(signal.SIGTERM)
        subprocess.run(["wineserver", "-k"], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=60)
        for p in procs:
            try:
                p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                p.kill()
        # Reap anything re-parented to us.
        try:
            while os.waitpid(-1, os.WNOHANG)[0]:
                pass
        except ChildProcessError:
            pass

    with open(a.out, "w") as f:
        for r in records:
            f.write(json.dumps(r, separators=(",", ":")) + "\n")
    print(a.out)


if __name__ == "__main__":
    main()
