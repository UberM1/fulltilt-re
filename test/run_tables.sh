#!/usr/bin/env bash
#
# Regression harness for the three tables: Space Cadet (known-good), Dragon's Keep
# and Skulduggery.
#
# WHAT A PASS ACTUALLY PROVES
# ---------------------------
# A green row means exactly this: the engine built, the loader parsed that table's
# .DAT without raising an error, every component the table names got constructed and
# bound, and the process entered winmain::MainLoop and kept pumping events until it
# was asked to quit.
#
# That is "the table constructs and runs". It is NOT "the rules are correct".
# Nothing here launches a ball, presses a flipper, enters a quest or reads a score.
# A table whose every handler scores the wrong amount, whose quest machine never
# advances, or whose dispatch order double-scores (see docs/integration.md on
# Dragon's Keep calling its mode machine *before* the component handler) passes this
# harness with a clean sheet. Every `// UNVERIFIED:` in src/ stays unverified after a
# green run. The open questions in docs/integration.md -- success versus timeout,
# which physical component several abstract ones are, the seven stand-in object
# types, the glyph tokens -- are all invisible to this script by construction.
#
# So: this catches *construction* regressions, which is the class of bug that has
# actually bitten this project (loader errors, missing attributes, sound table
# overflow, null dereferences in component constructors, modal error dialogs that
# block forever). Read a green run as "still boots", not "still right".
#
# USAGE
#   test/run_tables.sh
#
# Environment overrides (all optional):
#   FULLTILT_RUN_SECONDS    how long to let each table run    (default 12)
#   FULLTILT_GRACE_SECONDS  how long to wait for a clean quit (default 6)
#   FULLTILT_GAME_DIR       the Full Tilt install             (default game/WIN95/FULLTILT)
#   FULLTILT_WORK_DIR       where to stage run directories    (default a mktemp dir)
#   FULLTILT_KEEP_WORK      set to 1 to keep the run dirs and their logs
#   FULLTILT_SCP_BIN        skip the build, test this binary instead (debugging only)
#
# Exit status: 0 if all three tables pass, 1 if any table regressed, 2 if the engine
# did not build or the harness could not set up (never a silent pass).

set -euo pipefail

readonly SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
readonly REPO_ROOT=$(cd -- "$SCRIPT_DIR/.." && pwd)

RUN_SECONDS=${FULLTILT_RUN_SECONDS:-12}
GRACE_SECONDS=${FULLTILT_GRACE_SECONDS:-6}
GAME_DIR=${FULLTILT_GAME_DIR:-$REPO_ROOT/game/WIN95/FULLTILT}

# dir | DAT/BMP stem | the table name pb::SelectDatFile should report | known-benign
# BL error message lines, comma separated.
#
# The allowlist is a record of warnings that are understood and documented, not a
# wishlist. `mission_text_box` is upstream's TPinballTable looking for Space Cadet's
# mission display by hardcoded group name; neither bonus table has one, and pb.cpp's
# own comment on mode_change says every use of it must tolerate a null. Anything not
# listed here fails the table, so a new warning is a regression by default.
readonly TABLES=(
	"CADET|CADET|Full Tilt! Space Cadet|"
	"DRAGON|DRAGON|Dragon's Keep|mission_text_box"
	"PIRATE|PIRATES|Skulduggery|mission_text_box"
)

# Failure modes this project has actually hit, so a regression is named rather than
# reported as a shrug. Format: regex | what it means.
readonly SIGNATURES=(
	"sound reference is not A_SOUND record|loader::sound_list overflow -- PIRATES.DAT has 67 sound groups"
	"float Attribute not found|query_float_attribute miss -- attribute absent from the state record"
	"Unrecognized float Attribute|query_float_attribute miss -- attribute code the loader does not know"
	"No float Attributes Field|query_float_attribute miss -- no FloatArray on the record at all"
	"loader_kicker|loader::kicker rejected an attribute (410 is Full Tilt's)"
	"loader_query_visual|query_visual returned early -- a null FloatArr follows"
	"loader_material|loader::material rejected a record"
	"loader_state_id|state_id lookup failed"
	"loader_get_sound_id|sound id lookup failed"
	"state_index out of range|state_index out of range"
	"Unrecognized Attribute|unknown attribute code -- an object type the loader skips"
	"walls doesn't match data size|wall count disagrees with the data size"
	"Table cant find|a component the table names was never constructed"
)

# Lines that are expected noise under the dummy drivers and the in-flight debug
# output, excluded only from the crash-text scan and the message digest.
readonly NOISE_RE='^(SDL Error: |DBG-|Failed to load font)'

# Text that means the process died badly even without a fatal signal reaching us.
readonly CRASH_RE='Segmentation fault|SIGSEGV|Assertion .* failed|terminate called|std::bad_alloc|double free or corruption|stack smashing detected|free\(\): invalid|munmap_chunk'

die() {
	printf 'run_tables.sh: %s\n' "$*" >&2
	exit 2
}

if [[ -n ${FULLTILT_WORK_DIR:-} ]]; then
	WORK_DIR=$FULLTILT_WORK_DIR
	mkdir -p -- "$WORK_DIR"
else
	WORK_DIR=$(mktemp -d -t fulltilt-tables.XXXXXXXX) || die "could not create a work directory"
fi
readonly WORK_DIR

cleanup() {
	if [[ ${FULLTILT_KEEP_WORK:-0} == 1 ]]; then
		printf '\nrun directories and logs kept in %s\n' "$WORK_DIR"
	else
		rm -rf -- "$WORK_DIR"
	fi
}
trap cleanup EXIT

# --- 1. build ---------------------------------------------------------------

build_engine() {
	if [[ -n ${FULLTILT_SCP_BIN:-} ]]; then
		[[ -x ${FULLTILT_SCP_BIN} ]] || die "FULLTILT_SCP_BIN is not an executable: ${FULLTILT_SCP_BIN}"
		BIN=$FULLTILT_SCP_BIN
		printf 'using FULLTILT_SCP_BIN, build skipped: %s\n\n' "$BIN"
		return
	fi

	[[ -f $REPO_ROOT/build.nix ]] || die "no build.nix at $REPO_ROOT"

	printf 'building the engine (nix-build build.nix)...\n'
	local out=$WORK_DIR/build.out err=$WORK_DIR/build.err
	if ! (cd -- "$REPO_ROOT" && nix-build build.nix --no-out-link --out-link /tmp/scp-out) \
		>"$out" 2>"$err"; then
		printf '\nENGINE DID NOT BUILD. Last 40 lines of the build log:\n\n' >&2
		tail -n 40 -- "$err" >&2
		die "nix-build failed -- no table was run"
	fi

	# The store path is nix-build's stdout; never hardcode it.
	local store
	store=$(grep -m1 '^/nix/store/' -- "$out" || true)
	[[ -n $store ]] || { cat -- "$out" >&2; die "nix-build succeeded but printed no store path"; }

	BIN=$store/bin/SpaceCadetPinball
	[[ -x $BIN ]] || die "built, but $BIN is missing or not executable"
	printf 'built: %s\n\n' "$BIN"
}

# --- 2. stage a clean run directory ----------------------------------------

# The engine finds its data by fixed names: pb::SelectDatFile probes the table
# registry's DAT names in registry order (CADET.DAT, PINBALL.DAT, DEMO.DAT,
# DRAGON.DAT, PIRATES.DAT -- see table_rules.cpp's TableDatSearchOrder and
# TableRules::DatFileName), each also lowercased, across the search paths winmain
# passes: working directory first, then the executable's directory, then SDL's pref
# path.
#
# Because the registry already carries DRAGON.DAT and PIRATES.DAT as first-class
# entries, staging needs no renaming at all: each table's own .DAT under its own
# name in an otherwise empty directory is unambiguous, and cd'ing there makes the
# working directory -- the first search path -- win. The one thing that must hold is
# that no *other* registered DAT is reachable, or CADET.DAT would be picked first;
# hence one directory per table and an isolated pref path.
#
# SOUND/ has to come along: loader.cpp prefixes FT sound file names with "sound/"
# and retries uppercased, and midi.cpp prefixes "SOUND/" for TAB{A,B,C}n.{MID,MDS},
# so the uppercase SOUND directory from the install is what both paths resolve to.
# The .BMP is staged for completeness; the SDL engine never opens it.
stage_table() {
	local dir=$1 stem=$2 rundir=$3
	local src=$GAME_DIR/$dir

	[[ -d $src ]] || die "no table data at $src (set FULLTILT_GAME_DIR?)"

	rm -rf -- "$rundir"
	mkdir -p -- "$rundir/home/.local/share"

	local f
	for f in "$stem.DAT" "$stem.BMP"; do
		[[ -f $src/$f ]] || die "$src/$f is missing"
		cp -- "$src/$f" "$rundir/$f"
	done

	[[ -d $src/SOUND ]] || die "$src/SOUND is missing"
	cp -R -- "$src/SOUND" "$rundir/SOUND"
}

# --- 3. run it, headless, with a timeout -----------------------------------

# Returns through the globals RUN_STATUS, RUN_REACHED_LOOP and RUN_HUNG.
#
# `timeout` is not enough on its own: it reports 124 whether the child died of the
# signal or handled it, which is exactly the distinction that matters here. So the
# run is supervised by hand.
#
# SDL installs a SIGTERM handler that turns the signal into an SDL_QUIT event, and
# winmain::MainLoop exits cleanly on SDL_QUIT with return_value 0. So a child that
# exits 0 after SIGTERM has demonstrably been round the event loop. A child that
# ignores SIGTERM is stuck somewhere that never pumps events -- which is precisely
# what a modal error dialog does: pb::ShowMessageBox calls SDL_ShowSimpleMessageBox
# after printing, and on a real display that blocks forever. That is reported as a
# hang, not a pass.
run_table() {
	local rundir=$1
	RUN_STATUS=0 RUN_REACHED_LOOP=0 RUN_HUNG=0

	(
		cd -- "$rundir" || exit 127
		HOME=$rundir/home \
		XDG_DATA_HOME=$rundir/home/.local/share \
		XDG_CONFIG_HOME=$rundir/home/.config \
		SDL_VIDEODRIVER=dummy \
		SDL_AUDIODRIVER=dummy \
		exec "$BIN"
	) >"$rundir/stdout.log" 2>"$rundir/stderr.log" &
	local pid=$!

	local waited=0
	while ((waited < RUN_SECONDS)); do
		kill -0 "$pid" 2>/dev/null || break
		sleep 1
		waited=$((waited + 1))
	done

	local alive_at_timeout=0
	if kill -0 "$pid" 2>/dev/null; then
		alive_at_timeout=1
		kill -TERM "$pid" 2>/dev/null || true
		local grace=0
		while ((grace < GRACE_SECONDS)) && kill -0 "$pid" 2>/dev/null; do
			sleep 1
			grace=$((grace + 1))
		done
		if kill -0 "$pid" 2>/dev/null; then
			RUN_HUNG=1
			kill -KILL "$pid" 2>/dev/null || true
		fi
	fi

	wait "$pid" || RUN_STATUS=$?

	# Survived the whole run and then quit cleanly when asked: the loop was live.
	# Status 143 means the default SIGTERM disposition killed it, i.e. SDL's handler
	# never converted the signal -- it was not pumping events.
	if ((alive_at_timeout == 1 && RUN_HUNG == 0 && RUN_STATUS == 0)); then
		RUN_REACHED_LOOP=1
	fi
}

# --- 4. classify ------------------------------------------------------------

# Every pb::ShowMessageBox prints two lines: "BL error: <title>" then the message.
# SDL_MESSAGEBOX_ERROR goes to stderr and anything else to stdout, which is how the
# loader's hard errors are told from TPinballTable's binding warnings.
extract_bl() {
	[[ -s $1 ]] || return 0
	awk '
		/^BL error: /{
			title = substr($0, 11)
			if ((getline msg) > 0)
				printf "%s / %s\n", title, msg
			next
		}
	' "$1"
}

in_allowlist() {
	local msg=$1 allow=$2 entry
	[[ -n $allow ]] || return 1
	local IFS=,
	for entry in $allow; do
		[[ -n $entry && $msg == *"$entry"* ]] && return 0
	done
	return 1
}

# --- main -------------------------------------------------------------------

build_engine

declared_failures=0
declare -a ROWS=() DETAILS=()

for spec in "${TABLES[@]}"; do
	IFS='|' read -r dir stem expect_name allow <<<"$spec"
	rundir=$WORK_DIR/$dir

	printf 'running %-8s ' "$dir"
	stage_table "$dir" "$stem" "$rundir"
	run_table "$rundir"

	out=$rundir/stdout.log
	err=$rundir/stderr.log

	# Which table did the engine actually pick? A staging mistake shows up here
	# rather than as a mysteriously green Dragon run that was really Space Cadet.
	selected=$(sed -n 's/^Loading \(.*\) from: .*/\1/p' -- "$out" | head -n1)

	mapfile -t bl_err < <(extract_bl "$err")
	mapfile -t bl_warn < <(extract_bl "$out")

	unexpected=()
	for msg in "${bl_err[@]}"; do
		unexpected+=("E: $msg")
	done
	for msg in "${bl_warn[@]}"; do
		in_allowlist "$msg" "$allow" || unexpected+=("W: $msg")
	done

	crash_text=$(grep -hEv "$NOISE_RE" -- "$out" "$err" 2>/dev/null | grep -hE "$CRASH_RE" || true)

	crashed=no
	if ((RUN_STATUS >= 128)) && ((RUN_HUNG == 0)); then
		sig=$((RUN_STATUS - 128))
		case $sig in
			11) crashed="SIGSEGV" ;;
			6) crashed="SIGABRT" ;;
			15) crashed="SIGTERM (not handled)" ;;
			*) crashed="signal $sig" ;;
		esac
	elif [[ -n $crash_text ]]; then
		crashed="yes (in output)"
	fi

	loop=no
	if ((RUN_HUNG == 1)); then
		loop="HANG"
	elif ((RUN_REACHED_LOOP == 1)); then
		loop=yes
	elif ((RUN_STATUS == 0)); then
		loop="exited early (0)"
	else
		loop="exited ($RUN_STATUS)"
	fi

	verdict=PASS
	reasons=()
	if [[ $selected != "$expect_name" ]]; then
		verdict=FAIL
		reasons+=("engine selected '${selected:-<nothing>}', expected '$expect_name' -- staging or registry problem")
	fi
	if ((RUN_HUNG == 1)); then
		verdict=FAIL
		reasons+=("hung: ignored SIGTERM for ${GRACE_SECONDS}s, so it was not pumping events -- a blocking modal dialog looks exactly like this")
	elif ((RUN_REACHED_LOOP == 0)); then
		verdict=FAIL
		reasons+=("never confirmed in the main loop (exit status $RUN_STATUS)")
	fi
	if [[ $crashed != no ]]; then
		verdict=FAIL
		reasons+=("crashed: $crashed")
	fi
	if ((${#unexpected[@]} > 0)); then
		verdict=FAIL
		reasons+=("${#unexpected[@]} unexpected loader/binding message(s)")
	fi

	[[ $verdict == PASS ]] || declared_failures=$((declared_failures + 1))
	printf '%s\n' "$verdict"

	ROWS+=("$dir|yes|$loop|$((${#bl_err[@]} + ${#bl_warn[@]}))|${#unexpected[@]}|$crashed|$verdict")

	# Detail block: the named failure modes, then the first few distinct messages.
	detail="  $dir -- ${selected:-<no table selected>}"
	for sigspec in "${SIGNATURES[@]}"; do
		re=${sigspec%%|*}
		meaning=${sigspec#*|}
		n=$({ grep -hE -- "$re" "$out" "$err" 2>/dev/null || true; } | wc -l)
		((n > 0)) && detail+=$'\n'"      [$n] $meaning"
	done
	if ((${#unexpected[@]} > 0)); then
		detail+=$'\n'"      first distinct unexpected messages:"
		while IFS= read -r line; do
			detail+=$'\n'"        $line"
		done < <(printf '%s\n' "${unexpected[@]}" | sort -u | head -n 5)
	fi
	if ((${#bl_warn[@]} > 0)) && ((${#unexpected[@]} == 0)); then
		detail+=$'\n'"      known-benign only: $(printf '%s\n' "${bl_warn[@]}" | sort -u | head -n 3 | paste -sd'; ' -)"
	fi
	for r in "${reasons[@]}"; do
		detail+=$'\n'"      FAIL: $r"
	done
	DETAILS+=("$detail")
done

printf '\n'
printf '%-8s  %-5s  %-16s  %-7s  %-10s  %-20s  %s\n' \
	TABLE BUILT "MAIN LOOP" "BL MSGS" UNEXPECTED CRASHED VERDICT
printf '%-8s  %-5s  %-16s  %-7s  %-10s  %-20s  %s\n' \
	-------- ----- ---------------- ------- ---------- -------------------- -------
for row in "${ROWS[@]}"; do
	IFS='|' read -r t b l n u c v <<<"$row"
	printf '%-8s  %-5s  %-16s  %-7s  %-10s  %-20s  %s\n' "$t" "$b" "$l" "$n" "$u" "$c" "$v"
done

printf '\ndetail:\n'
for d in "${DETAILS[@]}"; do
	printf '%s\n' "$d"
done

printf '\n'
if ((declared_failures > 0)); then
	printf '%d of %d tables regressed.\n' "$declared_failures" "${#TABLES[@]}"
	exit 1
fi
printf 'all %d tables construct and run. This says nothing about whether the rules are correct.\n' \
	"${#TABLES[@]}"
