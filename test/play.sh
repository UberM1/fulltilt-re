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

echo "playing $dat from $rundir" >&2
cd -- "$rundir"
HOME=$rundir/home XDG_DATA_HOME=$rundir/home/.local/share exec "$bin" "$@"
