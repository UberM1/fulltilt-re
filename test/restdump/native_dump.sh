#!/usr/bin/env bash
# Rest-state dump of the native build: step 1 of test/restdump/README.md.
#
#   test/restdump/native_dump.sh dragon|pirate OUT.jsonl [WORK_DIR]
#
# Stages the table in its own directory (the engine takes the first DAT it finds),
# runs the engine headless with FT_RESTDUMP set, and waits for the hook in
# reference/SpaceCadetPinball/FtRestDump.cpp to write OUT.jsonl and exit.
#
# -noaudio: the originals run under Wine with no sound device, and without one Full
# Tilt loads no wave and every sound duration is 0 -- which times the new-game light
# show and so how many rand() draws precede the first pick. The port does the same
# when no mixer is open (docs/engine-changes.md section 32).
#
# Environment: FULLTILT_SCP_BIN (skip the build), FULLTILT_GAME_DIR,
# FT_RESTDUMP_DELAY (seconds the ball sits in the plunger lane first; default 2),
# FULLTILT_DUMP_TIMEOUT (default 60 s).
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_dir=${FULLTILT_GAME_DIR:-$repo_root/game/WIN95/FULLTILT}

case ${1:-} in
dragon) src=$game_dir/DRAGON dat=DRAGON ;;
pirate | pirates) src=$game_dir/PIRATE dat=PIRATES ;;
*)
	echo "usage: ${0##*/} dragon|pirate OUT.jsonl [WORK_DIR]" >&2
	exit 64
	;;
esac
out=${2:?usage: ${0##*/} dragon|pirate OUT.jsonl [WORK_DIR]}
out=$(realpath -m -- "$out")
work=${3:-$(mktemp -d -t fulltilt-restdump.XXXXXXXX)}

if [[ -n ${FULLTILT_SCP_BIN:-} ]]; then
	bin=$FULLTILT_SCP_BIN
else
	store=$(cd -- "$repo_root" && env -u LD_LIBRARY_PATH nix-build build.nix --no-out-link | tail -1)
	bin=$store/bin/SpaceCadetPinball
fi
[[ -x $bin ]] || { echo "no engine binary at $bin" >&2; exit 1; }

rundir=$work/native-$dat
mkdir -p -- "$rundir/home/.local/share"
cp -- "$src/$dat.DAT" "$rundir/"
[[ -d $rundir/SOUND ]] || cp -R -- "$src/SOUND" "$rundir/SOUND"
rm -f -- "$out"

(
	cd -- "$rundir"
	exec env -u LD_LIBRARY_PATH -u WAYLAND_DISPLAY \
		HOME="$rundir/home" XDG_DATA_HOME="$rundir/home/.local/share" \
		XDG_CONFIG_HOME="$rundir/home/.config" \
		SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
		FT_RESTDUMP="$out" \
		timeout "${FULLTILT_DUMP_TIMEOUT:-60}" "$bin" -noaudio
) >"$rundir/stdout.log" 2>"$rundir/stderr.log" || {
	status=$?
	echo "engine exited with $status before dumping; logs in $rundir" >&2
	exit 1
}
[[ -s $out ]] || { echo "engine exited but wrote no dump; logs in $rundir" >&2; exit 1; }
echo "$out"
