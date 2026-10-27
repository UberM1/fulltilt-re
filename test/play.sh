#!/usr/bin/env bash
# Build and play one table, for a human.
#
#   test/play.sh cadet | dragon | pirate
#
# test/run_tables.sh is the automated check and runs headless; this is its
# counterpart for actually looking at the thing. It stages the same way: each
# table gets its own directory holding only its own .DAT, because the engine
# probes a fixed list of names with the working directory first and CADET.DAT
# leads that list -- two tables in one directory and Space Cadet always wins.
#
# HOME is redirected into the run directory so each table keeps its own options,
# high scores and imgui layout, and so a stale "Prefer 3D Pinball's data" setting
# cannot reorder the probe.
#
# What you are playing: Dragon's Keep and Skulduggery run rules reconstructed
# from the 1996 executables. They load, serve and score, but "it runs" is not
# "it is right" -- see docs/integration.md for what is still unverified.
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
game_dir=${FULLTILT_GAME_DIR:-$repo_root/game/WIN95/FULLTILT}
work_dir=${FULLTILT_WORK_DIR:-$repo_root/.play}

case ${1:-} in
cadet) src=$game_dir/CADET dat=CADET;;
dragon) src=$game_dir/DRAGON dat=DRAGON;;
pirate | pirates | skulduggery) src=$game_dir/PIRATE dat=PIRATES;;
*)
	echo "usage: ${0##*/} cadet|dragon|pirate" >&2
	exit 64
	;;
esac

[[ -d $src ]] || {
	echo "no game data at $src" >&2
	exit 1
}

echo "building..." >&2
out=$(cd -- "$repo_root" && nix-build build.nix --no-out-link | tail -1)
bin=$out/bin/SpaceCadetPinball
[[ -x $bin ]] || {
	echo "no binary at $bin" >&2
	exit 1
}

rundir=$work_dir/$dat
mkdir -p -- "$rundir/home/.local/share"
# Refresh the data every time: the DATs are read-only inputs, and a stale copy
# would quietly outlive a change to the extraction scripts.
rm -rf -- "${rundir:?}/SOUND"
# Under its own name: table_rules.cpp's registry knows DRAGON.DAT and PIRATES.DAT
# as first-class entries, so no renaming is needed -- isolation is what matters.
cp -- "$src/$dat.DAT" "$rundir/"
[[ -f $src/$dat.BMP ]] && cp -- "$src/$dat.BMP" "$rundir/"
cp -R -- "$src/SOUND" "$rundir/SOUND"

# Keep the previous session: a report usually arrives after the next launch.
log=$rundir/session.log
[[ -f $log ]] && mv -f -- "$log" "$rundir/session.prev.log"

{
	echo "# $dat"
	echo "# $(date -Is)"
	echo "# binary $bin"
	echo "# engine $(cd -- "$repo_root" && git rev-parse --short HEAD)"
} >"$log"

echo "playing $dat from $rundir" >&2
echo "log: $log" >&2
cd -- "$rundir"

# Both streams, unbuffered, to the terminal and the log. The engine reports
# loader failures and missing components on stderr, and they scroll past.
#
# The SDL renderer emits "That operation is not supported" once per frame, which
# buried the only eight meaningful lines of a 130 KB log under 4,000 copies of
# itself -- and would bury a real SDL error just as well. Dropped from the log,
# kept on the terminal, with a count at the end so it cannot vanish silently.
export HOME=$rundir/home XDG_DATA_HOME=$rundir/home/.local/share
set +e
# One awk pass: everything reaches the terminal, the log keeps only the signal,
# and the suppressed count is recorded so a flood cannot hide in the gap.
stdbuf -oL -eL "$bin" "$@" 2>&1 |
	awk -v out="$log" -v noise='SDL Error: That operation is not supported' '
		{ print }
		index($0, noise) { n++; next }
		{ print >> out; fflush(out) }
		END { if (n) printf "# suppressed %d renderer noise lines\n", n >> out }
	'
status=${PIPESTATUS[0]}
set -e

echo "# exit $status" >>"$log"
echo >&2
echo "session log: $log" >&2
[[ -f $rundir/session.prev.log ]] && echo "previous:    $rundir/session.prev.log" >&2
exit "$status"
