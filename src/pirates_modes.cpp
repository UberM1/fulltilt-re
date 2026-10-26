// pirates_modes.cpp -- Skulduggery's mode state machine, reimplemented.
//
// This is the `control::MissionControl` layer of Full Tilt! Pinball's
// Skulduggery table: the message dispatcher, the mode lifecycle
// (offer / start / tick / tear down), the eight mode controllers, the two
// multiball modes and the end-of-game lockdown. It is the structural
// counterpart of SpaceCadetPinball's `control::MissionControl` +
// `SelectMissionController` + the per-mission controllers, and the sibling of
// `dragon_missions.cpp`, which does the same job for Dragon's Keep.
//
// Three load-bearing differences from Dragon's Keep, all structural:
//
//   1. **The dispatcher owns the return to idle.** `FUN_0042d850` loops: after
//      it has handed a `0x52` to the controller it forces the mode register to
//      2 and re-dispatches `0x4f`. That is why no controller in this file sets
//      state 2 itself, and why Skulduggery has no Dragon-style "mode just
//      ended" grace state and no `0x51` restart code at all.
//   2. **Mode completion is never checked.** The shared `0x52` teardown bumps
//      the modes-completed counter and the dispatcher pays +750,000 to the
//      jackpot and turns the wheel light solid -- on timeout and on
//      drain-abort exactly as on success. See `ModeControl` for the full
//      consequence. There is deliberately no win check in this file, because
//      there is none in the binary.
//   3. **The mode order is a permutation.** The selection index 0..7 maps to
//      states {4,8,7,5,6,9,10,11} through the static table `ModeForSlot`, so
//      "mode 5" means two different things depending on whether you are
//      reading a state or a slot. Both spellings exist below, as `Mode` and
//      `ModeSlot`, specifically so a confusion between them cannot compile.
//
// Source of truth, in order of precedence:
//   1. decomp/pirates/asm/*.txt and decomp/pirates_rest/asm/*.txt (objdump).
//      Ghidra drops stack-argument values on these handlers and
//      `FUN_0042dfaf` is `sprintf` with unmodelled varargs, so where the two
//      disagree the listing wins.
//   2. decomp/pirates/functions/*.c, decomp/pirates_rest/functions/*.c
//      (Ghidra pseudocode, `DAT_` pre-annotated with component tag names)
//   3. analysis/pirates-rules.md (the prior reading of those functions)
//   4. docs/dragon-rules-spec.md (the shipped manual -- loses every conflict)
//
// Every function carries the address of the routine it reimplements as the
// first line of its doc block, so any disagreement can be re-checked.
//
// MESSAGE CODES. Full Tilt's `MessageCode` enum is NOT upstream's. The offset
// from the 3DPB numbering that SpaceCadetPinball reconstructed is piecewise:
//     TLight                                    +0
//     TLightGroup/TSoloTarget/TPopupTarget/TBlocker  +4
//     gate / kickout / sink                      +9
//     Control* block                            +10
//     ControlCollision                          +11  (0x4a)
//     mission codes                             +13  (0x4f, 0x50)
// and Skulduggery adds `0x52`/`0x53` (teardown/abort) and `0x4b`/`0x4c`
// (holding-field enable/disable) with no upstream counterpart at all. So the
// codes are spelled out in `ft` below rather than taken from the upstream
// enum, which would silently mis-dispatch.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TPinballTable.h"
#include "TPinballComponent.h"

// component_tag<T> instantiates std::is_base_of<TPinballComponent, T>, which needs
// T complete -- a forward declaration is not enough.
#include "TLight.h"
#include "TLightGroup.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates {

// ---------------------------------------------------------------------------
// Message codes (binary numbering)
// ---------------------------------------------------------------------------

namespace ft {
constexpr MessageCode Msg(int v) { return static_cast<MessageCode>(v); }

// TLight family (delta +0 from upstream).
constexpr MessageCode LightTurnOff = Msg(0x00);
constexpr MessageCode LightTurnOn = Msg(0x01);
constexpr MessageCode LightFlasherStartTimed = Msg(0x07);  // -1.0 == flash forever
constexpr MessageCode LightTurnOnTimed = Msg(0x09);        // value = seconds
constexpr MessageCode LightFlashTimedThenStayOff = Msg(0x10);
constexpr MessageCode LightResetAndTurnOn = Msg(0x13);
constexpr MessageCode LightResetAndTurnOff = Msg(0x14);

// Full-Tilt-only TLight codes. Upstream knows these exist and gave them
// placeholder values -25/-26 (TLightFtTmpOverrideOff / ...ResetOverride);
// 0x19/0x1a is exactly |those|. Skulduggery sends them to light *groups*
// rather than to single lights -- Treasure Hunt and Peg Leg's Loot override
// the two orbit groups dark for the mode's duration and clear the override at
// teardown, which is the same "borrow a light and put it back" idiom
// Dragon's Keep uses on single lights.
constexpr MessageCode LightOverrideOff = Msg(0x19);
constexpr MessageCode LightClearOverride = Msg(0x1a);

// UNVERIFIED: 0x1b, sent only to `light_sweeps` with a float selecting which
// canned sweep to run. 0x1b is not a group code under the +4 rule and
// `light_sweeps` has its own component type (1038), so this is a
// Full-Tilt-only code on a Full-Tilt-only component. Settled by decompiling
// the TComponentGroup Message override that handles 0x1b.
constexpr MessageCode LightSweepStart = Msg(0x1b);

// TLightGroup family (delta +4).
constexpr MessageCode GroupStepBackward = Msg(0x1c);    // the flippers' "rotate"
constexpr MessageCode GroupTurnOnAtIndex = Msg(0x27);
constexpr MessageCode GroupTurnOffAtIndex = Msg(0x28);
constexpr MessageCode GroupGetOnCount = Msg(0x29);
constexpr MessageCode GroupGetLightCount = Msg(0x2a);
constexpr MessageCode GroupTurnOnNext = Msg(0x2d);
constexpr MessageCode GroupRestartNotifyTimer = Msg(0x2f);
constexpr MessageCode GroupToggleSplitIndex = Msg(0x31);

// Sinks (delta +9).
constexpr MessageCode SinkResetTimer = Msg(0x41);

// Timer control. mode_timer is a type-1032 TTimer; 0x45 arms it with a float
// number of seconds (-1.0 cancels) and 0x48 reads the remaining time back in
// **milliseconds**. Neither value lines up with upstream's TTimerResetTimer
// under any of the piecewise deltas, so both are FT-only.
// UNVERIFIED names; the semantics are pinned by ~30 agreeing uses and by the
// `cdq; mov ecx,0x3e8; idiv ecx` that follows every 0x48.
constexpr MessageCode TimerSet = Msg(0x45);
constexpr MessageCode TimerGetRemaining = Msg(0x48);

// Control* block (delta +10, ControlCollision +11, mission codes +13).
constexpr MessageCode TimerExpired = Msg(0x46);
constexpr MessageCode NotifyTimerExpired = Msg(0x47);
constexpr MessageCode Collision = Msg(0x4a);
constexpr MessageCode MissionComplete = Msg(0x4f);  // "mode changed / mode start"
constexpr MessageCode MissionStarted = Msg(0x50);   // "mode tick"

// Full-Tilt-only mode-lifecycle codes, with no upstream counterpart:
// upstream missions only ever see MissionComplete/MissionStarted because
// Space Cadet has no mid-mission drain abort and no explicit teardown step.
constexpr MessageCode ModeTeardown = Msg(0x52);
constexpr MessageCode ModeAbort = Msg(0x53);

// UNVERIFIED: 0x4b / 0x4c, sent to the six `s_magnet*` objects and to the
// `a_kout1` / `a_kout2` kickouts at mode start and at teardown. "Enable /
// disable the holding field" is inferred from behaviour only -- whether a
// disabled device can still capture a ball is decided inside the component
// class, which is engine code and not in this corpus. Settled by decompiling
// the type-1042 Message override.
constexpr MessageCode HoldFieldEnable = Msg(0x4b);
constexpr MessageCode HoldFieldDisable = Msg(0x4c);

// UNVERIFIED: 0x39, sent to the four `a_bloc*` blockers everywhere in this
// corpus with a float that is -1.0 (hold open forever), 0.0 (release) or a
// duration. The analysis reads it as "blocker restart timeout"; under the +4
// blocker rule upstream's TBlockerRestartTimeout would be 0x3f, which is
// already TGateEnable under the +9 rule, so the numbering does not confirm
// the name. The three observed argument values do. Settled by decompiling
// TBlocker::Message.
constexpr MessageCode BlockerRestartTimeout = Msg(0x39);

// Public codes (1000+). 0x400 is sent to the table itself by the game-over
// controller; the rest of the table-level block belongs to the ball/serve
// slice and is not used here.
constexpr MessageCode TableGameOver = Msg(0x400);
}  // namespace ft

// Component *type* ids, from vtable slot 0. A distinct namespace from the
// messages because several values collide numerically.
//
// Vtable +0x00 is a type getter, NOT the destructor: every handler in this
// corpus reaches it as `mov eax,[ecx]; call [eax+0x00]` and then compares the
// result against these ids, with no object-deleting epilogue anywhere
// downstream. `analysis/symbols-cadet.md` still claims slot 0 is the
// destructor; that is wrong, but it is a shared reference and the correction
// belongs to whoever owns it.
namespace type {
const int Light = 0x3ea;         // 1002 TLight
const int PopupTarget = 0x3ee;   // 1006 TPopupTarget -- the 11 drop targets
const int Sink = 0x3f9;          // 1017 TSink
const int SoloTarget = 0x3fb;    // 1019 TSoloTarget -- the a_spot* bank
const int LightRollover = 0x3f7; // 1015, the a_roll* lanes
const int Timer = 0x408;         // 1032 TTimer
const int LightSweeps = 0x40e;   // 1038, the light_sweeps component

// UNVERIFIED: 1020. Three controllers pay a jackpot contribution on a
// collision with this type (Storm +50,000, Treasure Hunt +20,000, Peg Leg's
// Loot +50,000) and nothing else in the corpus names it. The bumpers and
// rebounds are the plausible candidates by elimination, since every other
// scoring component on this table already has a known type. Settled by
// dumping the type id of each object in PIRATES.DAT and intersecting with the
// components those three modes can be hit by.
const int Unknown1020 = 0x3fc;
}  // namespace type

// ---------------------------------------------------------------------------
// Modes
// ---------------------------------------------------------------------------

// The mode register is `info_text_box->MessageField`. Space Cadet parks its
// mission state in a dummy light (`lite198`); both Full Tilt tables reuse the
// single TTextBox they are already going to write the mode's name into.
// `FUN_0042d900` switches on exactly these values and forces anything else to
// Idle.
//
// The gaps are real: 0xe and 0xf have no arm, so they fall to the default and
// bounce straight back to Idle.
enum class Mode
{
	Inert = 0,         // the switch arm is a bare `return` -- set by the new-game branch
	GameOver = 1,      // 0x42bfd0 -- HIGH SCORE
	Idle = 2,          // 0x42ab90 -- "Spell Skulduggery to Start Mode"
	SkillShot = 3,     // 0x42abf0 -- the rolling award list
	AttackShip = 4,
	Bermuda = 5,
	Storm = 6,
	Tavern = 7,
	TreasureHunt = 8,
	Mutiny = 9,
	SwordFight = 10,
	PegLegsLoot = 0xb,  // the finale
	LavaBall = 0xc,     // multiball, +2 balls
	JailBreak = 0xd,    // multiball, releases the brig
	DrainBonus = 0x10,
	Lockdown = 0x11,    // end-of-game table lockdown; pays nothing
};

// The *selection* index, 0..7, which is NOT the state. It lives in
// `mode_timer->MessageField` as index+1 (0 = nothing selected), it is the
// order of the eight wheel lights `v_lite552`..`v_lite559`, and it is the
// order of the manual's mode-name strings 3068..3075. Keeping it a distinct
// type is the cheapest guard against the permutation below.
enum class ModeSlot
{
	AttackShip = 0,
	TreasureHunt = 1,
	Tavern = 2,
	Bermuda = 3,
	Storm = 4,
	Mutiny = 5,
	SwordFight = 6,
	PegLegsLoot = 7,
	None = -1,
};

// 0x43d0f0 -- the permutation, read as a static array out of the EXE. Slot
// order (= wheel-light order = manual order) to state. Confirmed twice over:
// once by this table and once, controller for controller, by the dispatcher's
// own switch.
const Mode ModeForSlot[8] = {
	Mode::AttackShip,
	Mode::TreasureHunt,
	Mode::Tavern,
	Mode::Bermuda,
	Mode::Storm,
	Mode::Mutiny,
	Mode::SwordFight,
	Mode::PegLegsLoot,
};

// Mode time limits, in seconds, and the start bonus each mode pays. Read from
// the instruction stream (`push <float>; push 0x45`) rather than from the
// pseudocode, which drops four of the eight as lost float arguments.
//
// Only four distinct limits are used across eight modes, which is mild
// support for the theory that these are the lengths of the matching voice-over
// waves -- the same pattern as Dragon's Keep. [S] for the reason, [C] for the
// values.
const float TimeAttackShip = 29.38f;    // 0x41eb0a3d
const float TimeBermuda = 31.14f;       // 0x41f91eb8
const float TimeStorm = 24.14f;         // 0x41c11eb8
const float TimeTavern = 27.40f;        // 0x41db3333
const float TimeTreasureHunt = 30.68f;  // 0x41f570a4
const float TimeMutiny = 48.28f;        // 0x42411eb8 -- by far the longest
const float TimeSwordFight = 29.38f;    // 0x41eb0a3d -- shared with Attack Ship
const float TimePegLegsLoot = 30.68f;   // 0x41f570a4 -- shared with Treasure Hunt

// The light-sweep id each mode hands to `light_sweeps`. Treasure Hunt's is not
// a constant: `ArmNextTreasureX` returns it, so the sweep tracks which "X" is
// armed.
const float SweepAttackShip = 0.0f;
const float SweepPegLegsLoot = 4.0f;  // sent from PrepareTreasureBoard, not from the 0x4f
const float SweepTavern = 7.0f;
const float SweepBermuda = 8.0f;
const float SweepStorm = 9.0f;
const float SweepMutiny = 10.0f;
const float SweepSwordFight = 11.0f;

// The manual says "starting a mode is worth 200,000". The binary pays a
// different amount per mode and only three of the eight actually pay 200,000.
const int BonusAttackShip = 200000;
const int BonusBermuda = 200000;
const int BonusStorm = 300000;
const int BonusTavern = 200000;
const int BonusTreasureHunt = 300000;
const int BonusMutiny = 100000;  // cheapest to start, and the longest to run
const int BonusSwordFight = 250000;
const int BonusPegLegsLoot = 1000000;
const int BonusJailBreak = 100000;

// Paid by the dispatcher, not by any controller, on every teardown of states
// 4..0xb. Matches the manual's "+750,000 per mode completion" exactly.
const int ModeCompletionJackpot = 750000;

const float Forever = -1.0f;  // the "flash / display indefinitely" sentinel

// ---------------------------------------------------------------------------
// String ids
//
// Every mode-name string is TWO lines: "<Name>\n%ld". That is what makes the
// `sprintf(buf, GetRcString(name), secondsRemaining)` call shape work and why
// the names look like they have no format specifier when the string table is
// read one line at a time. The hint strings have no `%ld` and take no operand.
// ---------------------------------------------------------------------------

enum
{
	STR_SCORE_AWARDED = 3003,  // bare "%ld"
	STR_NAME_ATTACK_SHIP = 3016,
	STR_SHIP_SUNK = 3017,
	STR_NAME_TAVERN = 3025,
	STR_HIGH_SCORE = 3027,
	STR_NAME_BERMUDA = 3045,
	STR_STORM_SQUALL = 3046,
	STR_STORM_TROPICAL = 3047,
	STR_STORM_MONSOON = 3048,
	STR_STORM_HURRICANE = 3049,
	STR_SHIPWRECKED = 3050,
	STR_NAME_MUTINY = 3051,
	STR_NAME_SWORD_FIGHT = 3052,
	STR_NAME_TREASURE_HUNT = 3053,
	STR_NAME_PEG_LEGS_LOOT = 3054,
	STR_NAME_LAVA_BALL = 3055,
	STR_NAME_JAIL_BREAK = 3056,
	// The hint block is in manual order EXCEPT that Storm is moved to the end
	// and Mutiny / Sword Fight / Peg Leg's Loot shift up one. It is NOT
	// parallel to the 3068..3075 "<Mode> Mode Selected" block, which is a trap.
	STR_HINT_ATTACK_SHIP = 3060,
	STR_HINT_TREASURE_HUNT = 3061,
	STR_HINT_TAVERN = 3062,
	STR_HINT_BERMUDA = 3063,
	STR_HINT_MUTINY = 3064,
	STR_HINT_SWORD_FIGHT = 3065,
	STR_HINT_PEG_LEGS_LOOT = 3066,
	STR_HINT_STORM = 3067,
	STR_IDLE_HINT = 3076,  // "Spell Skulduggery to Start Mode"
};

// ---------------------------------------------------------------------------
// Component tags
//
// Same convention as control_pirates.cpp: the group name in the DAT is the
// only identifier and `component_tag<T>::Component` is resolved at table
// load. Ghidra's `DAT_<addr>` for a component is always `tag + 4`, so the
// names here are exactly the ones annotated in the pseudocode.
//
// NOTE: about twenty of these (a_spot6, a_sink14/15/17, s_trip3, s_trip8,
// a_kout1, a_kout2, a_kick1, a_kick2, a_roll6, a_sink8, a_sink9, a_sink12,
// v_lite545/546/547, v_lite569, s_magnet1..6) are also defined by
// control_pirates.cpp. They are duplicated here for the same reason
// dragon_missions.cpp duplicates its overlap with control_dragon.cpp: this
// file does not link yet. When the per-table interface lands, each duplicate
// pair has to collapse to one definition plus an extern.
// ---------------------------------------------------------------------------

component_tag<TTextBox> tag_info_text_box = {"info_text_box"};
component_tag<TPinballComponent> tag_mode_timer = {"mode_timer"};
component_tag<TPinballComponent> tag_light_sweeps = {"light_sweeps"};

// The mode wheel, in SELECTION order. v_lite552..558 are the seven lights the
// random picker draws from; v_lite559 (Peg Leg's Loot) is reachable only by
// collecting all six map pieces.
component_tag<TLight> tag_v_lite552 = {"v_lite552"};  // Attack Ship
component_tag<TLight> tag_v_lite553 = {"v_lite553"};  // Treasure Hunt
component_tag<TLight> tag_v_lite554 = {"v_lite554"};  // Tavern
component_tag<TLight> tag_v_lite555 = {"v_lite555"};  // Bermuda Triangle
component_tag<TLight> tag_v_lite556 = {"v_lite556"};  // Storm
component_tag<TLight> tag_v_lite557 = {"v_lite557"};  // Mutiny
component_tag<TLight> tag_v_lite558 = {"v_lite558"};  // Sword Fight
component_tag<TLight> tag_v_lite559 = {"v_lite559"};  // Peg Leg's Loot

// Counters kept in MessageFields, as Full Tilt does throughout its rules code
// rather than in named globals.
component_tag<TLight> tag_v_lite606 = {"v_lite606"};  // modes completed this game
component_tag<TLight> tag_v_lite607 = {"v_lite607"};  // map pieces collected

// SKULDUGGERY letters (11 drop targets, 11 lights, one bitmask in v_lite1).
component_tag<TLight> tag_v_lite1 = {"v_lite1"};    // tavern bank, + the mask
component_tag<TLight> tag_v_lite2 = {"v_lite2"};
component_tag<TLight> tag_v_lite3 = {"v_lite3"};
component_tag<TLight> tag_v_lite4 = {"v_lite4"};    // ship bank
component_tag<TLight> tag_v_lite5 = {"v_lite5"};
component_tag<TLight> tag_v_lite6 = {"v_lite6"};
component_tag<TLight> tag_v_lite7 = {"v_lite7"};
component_tag<TLight> tag_v_lite8 = {"v_lite8"};
component_tag<TLight> tag_v_lite9 = {"v_lite9"};    // parrot bank
component_tag<TLight> tag_v_lite10 = {"v_lite10"};
component_tag<TLight> tag_v_lite11 = {"v_lite11"};

// Volcano display lights, highest index first (that is the order the Sword
// Fight controller flashes them in).
component_tag<TLight> tag_v_lite548 = {"v_lite548"};
component_tag<TLight> tag_v_lite549 = {"v_lite549"};
component_tag<TLight> tag_v_lite550 = {"v_lite550"};
component_tag<TLight> tag_v_lite551 = {"v_lite551"};
component_tag<TPinballComponent> tag_a_spot11 = {"a_spot11"};  // holds the volcano bitmask

// Blockers. Every mode sets all four at start; the shared teardown releases
// a_bloce only, which is why a mode's blocker state leaks into Idle.
component_tag<TPinballComponent> tag_a_blocc = {"a_blocc"};
component_tag<TPinballComponent> tag_a_bloce = {"a_bloce"};
component_tag<TPinballComponent> tag_a_blocf = {"a_blocf"};
component_tag<TPinballComponent> tag_a_blocg = {"a_blocg"};

// The pair of lights the common setup swaps. UNVERIFIED what they show: 504 is
// lit and 505 cleared for the duration of every mode and the swap is reversed
// at teardown, which reads like a "mode in progress / no mode" indicator pair.
// Settled by locating them in PIRATES.DAT.
component_tag<TLight> tag_v_lite504 = {"v_lite504"};
component_tag<TLight> tag_v_lite505 = {"v_lite505"};

// Attack Ship.
component_tag<TLight> tag_v_lite567 = {"v_lite567"};
component_tag<TLightGroup> tag_fathom_light_group = {"fathom_light_group"};

// Treasure Hunt / Peg Leg's Loot share the "X" board and the orbit machinery.
component_tag<TPinballComponent> tag_s_trip3 = {"s_trip3"};  // left orbit; also saves its count
component_tag<TPinballComponent> tag_s_trip8 = {"s_trip8"};  // right orbit; ditto
component_tag<TLightGroup> tag_left_orbit_light_group = {"left_orbit_light_group"};
component_tag<TLightGroup> tag_right_orbit_light_group = {"right_orbit_light_group"};
component_tag<TLightGroup> tag_left_orbit_combo_light_group = {"left_orbit_combo_light_group"};
component_tag<TLightGroup> tag_right_orbit_combo_light_group = {"right_orbit_combo_light_group"};
component_tag<TLight> tag_v_lite519 = {"v_lite519"};
component_tag<TLight> tag_v_lite520 = {"v_lite520"};
component_tag<TLight> tag_v_lite521 = {"v_lite521"};
component_tag<TLight> tag_v_mlite51 = {"v_mlite51"};
component_tag<TPinballComponent> tag_a_sink9 = {"a_sink9"};
component_tag<TPinballComponent> tag_a_sink12 = {"a_sink12"};
component_tag<TLight> tag_v_lite568 = {"v_lite568"};

// Tavern. The three window lights flutter under a 0x46-driven random walk.
component_tag<TLight> tag_v_lite545 = {"v_lite545"};
component_tag<TLight> tag_v_lite546 = {"v_lite546"};
component_tag<TLight> tag_v_lite547 = {"v_lite547"};
component_tag<TPinballComponent> tag_a_spot6 = {"a_spot6"};  // brig target, hit counter 0..2
component_tag<TLight> tag_v_lite530 = {"v_lite530"};         // brig lights
component_tag<TLight> tag_v_lite531 = {"v_lite531"};
component_tag<TLight> tag_v_lite532 = {"v_lite532"};

// Bermuda Triangle.
component_tag<TLightGroup> tag_bermuda_light_group = {"bermuda_light_group"};
component_tag<TPinballComponent> tag_a_kout1 = {"a_kout1"};  // the magnet in the circle

// Storm.
component_tag<TLight> tag_v_lite778 = {"v_lite778"};
component_tag<TPinballComponent> tag_a_kout2 = {"a_kout2"};  // the hurricane magnet
component_tag<TPinballComponent> tag_a_roll6 = {"a_roll6"};  // west wind -> shoot again
component_tag<TLight> tag_v_lite569 = {"v_lite569"};         // shoot again
component_tag<TPinballComponent> tag_a_sink14 = {"a_sink14"};  // the three hideaways
component_tag<TPinballComponent> tag_a_sink15 = {"a_sink15"};
component_tag<TPinballComponent> tag_a_sink17 = {"a_sink17"};

// The eight wind light groups. Six names over eight lanes: Tradewind Loop
// covers both orbits and Easterly both east wind lanes. Storm reads the total
// on-count of all eight to pick its severity.
component_tag<TLightGroup> tag_left_island_light_group = {"left_island_light_group"};
component_tag<TLightGroup> tag_right_wind_light_group = {"right_wind_light_group"};
component_tag<TLightGroup> tag_south_wind_light_group = {"south_wind_light_group"};
component_tag<TLightGroup> tag_east_wind_1_light_group = {"east_wind_1_light_group"};
component_tag<TLightGroup> tag_east_wind_2_light_group = {"east_wind_2_light_group"};
component_tag<TLightGroup> tag_west_wind_light_group = {"west_wind_light_group"};

// Peg Leg's Loot.
component_tag<TLightGroup> tag_coin_light_group = {"coin_light_group"};
component_tag<TLightGroup> tag_special_treasure_light_group = {"special_treasure_light_group"};
component_tag<TLightGroup> tag_map_piece_light_group = {"map_piece_light_group"};

// Multiball / lockdown plumbing.
component_tag<TPinballComponent> tag_a_kick1 = {"a_kick1"};      // left-outlane kickback
component_tag<TPinballComponent> tag_a_kick2 = {"a_kick2"};
component_tag<TPinballComponent> tag_s_oneway12 = {"s_oneway12"};  // brig release
component_tag<TPinballComponent> tag_s_bwal610 = {"s_bwal610"};
component_tag<TPinballComponent> tag_s_magnet1 = {"s_magnet1"};
component_tag<TPinballComponent> tag_s_magnet2 = {"s_magnet2"};
component_tag<TPinballComponent> tag_s_magnet3 = {"s_magnet3"};
component_tag<TPinballComponent> tag_s_magnet4 = {"s_magnet4"};
component_tag<TPinballComponent> tag_s_magnet5 = {"s_magnet5"};
component_tag<TPinballComponent> tag_s_magnet6 = {"s_magnet6"};

// Skill shot.
component_tag<TPinballComponent> tag_s_launcher1 = {"s_launcher1"};  // rolling cursor 0..8
component_tag<TPinballComponent> tag_s_oneway3 = {"s_oneway3"};      // locks the slot in

// Voice-overs and effects, named by their wave resource as in the DAT.
component_tag<TSound> tag_tabb26wave = {"tabb26wave"};  // "Ship Sunk"
component_tag<TSound> tag_tabb28wave = {"tabb28wave"};  // Peg Leg's payout fanfare
component_tag<TSound> tag_tabb32wave = {"tabb32wave"};  // Lava-Ball
component_tag<TSound> tag_tabb58wave = {"tabb58wave"};  // Attack Ship
component_tag<TSound> tag_tabb59wave = {"tabb59wave"};  // Treasure Hunt
component_tag<TSound> tag_tabb65wave = {"tabb65wave"};  // Tavern
component_tag<TSound> tag_tabb66wave = {"tabb66wave"};  // Bermuda Triangle
component_tag<TSound> tag_tabb67wave = {"tabb67wave"};  // Storm
component_tag<TSound> tag_tabb68wave = {"tabb68wave"};  // Storm, per bailed target
component_tag<TSound> tag_tabb73wave = {"tabb73wave"};  // Mutiny; reused at game over
component_tag<TSound> tag_tabb75wave = {"tabb75wave"};  // Sword Fight
component_tag<TSound> tag_tabb95wave = {"tabb95wave"};  // Jail Break
component_tag<TSound> tag_tabb99wave = {"tabb99wave"};  // Peg Leg's Loot

// MIDI tracks. Note the sharing: three pointers cover six modes, which is why
// Attack Ship and Sword Fight, Treasure Hunt and Peg Leg's Loot, and Storm and
// Mutiny sound alike.
extern midi_track* idle_music;              // 0x43ab80
extern midi_track* attack_sword_music;      // 0x43ab84
extern midi_track* treasure_pegleg_music;   // 0x43ab88
extern midi_track* tavern_music;            // 0x43ab8c
extern midi_track* bermuda_music;           // 0x43ab90
extern midi_track* storm_mutiny_music;      // 0x43ab94
extern midi_track* game_over_music;         // 0x43ab98

// ---------------------------------------------------------------------------
// Per-table interface this file depends on but does not define
//
// These stand in for the Skulduggery-specific parts of the rules that are not
// written yet. Each maps to one routine in the original; the comment is its
// address.
// ---------------------------------------------------------------------------

// 0x423310 -- AddScore(amount), MULTIPLIED by TPinballTable::ScoreMultiplier,
// with the "previous scoring component gets 0x4d" bookkeeping.
//
// CORRECTION to analysis/pirates-rules.md, which calls this the *unmultiplied*
// wrapper in three places (§2.2, §3 Mutiny, §5.4) while its own §6 table has
// it right. 0x423310's body is just the bookkeeping plus
// `TPinballTable::AddScore`; it is 0x423350 that zeroes the bonus and jackpot
// flags and forces ScoreMultiplier to 1 around the call. So every mode's start
// bonus, Mutiny's 25,000 per bumper and the magnets' 100,000 ARE multiplied.
int AddScoreAt(TPinballComponent* source, int score);          // 0x423310
int SpecialAddScoreAt(TPinballComponent* source, int score);   // 0x423350 -- unmultiplied
int AddScoreFromTable(TPinballComponent* source, int index);   // 0x4232c0 -- source->scoring[i]

void AddToJackpot(int amount);        // 0x423650 -- clamps at 10,000,000
void AwardJackpot(float displayTime); // 0x424700 -- adds the jackpot; never resets it
int Rnd(int n);                       // 0x42dd0b, the MSVC LCG -> (rand()*n) >> 15

const char* GetRcString(int id);      // 0x409730(id, 0) -- takes TWO arguments
void Display(TTextBox* box, const char* text, float time);        // 0x41de40, font 0
void DisplayGlyph(TTextBox* box, const char* glyph, float time);  // 0x41de40, font 1
void ClearText(TTextBox* box);                                    // 0x41d930

// 0x42dfaf -- `sprintf`. Spelled as a one-operand helper because that is the
// only shape this corpus uses: all 50 call sites push exactly one dword before
// `push 0; push <id>; call GetRcString`. The operand is a vararg Ghidra does
// not model, so it is invisible in the pseudocode and had to be read from the
// listing at every site.
void FormatWithValue(char* out, const char* format, int value);

void PlayMusic(midi_track* track);  // 0x40eb50
midi_track* CurrentMusic();         // 0x40e9b0
void Play(TSound* sound);           // vtable +0x18; the original returns the clip length

// Mode-register setters, one per state, from the transition slice. Each does
// "set the wheel light, set the register, broadcast 0x4f"; the twelve that
// enter a mode also flash the mode-in-progress light v_lite606 and show a
// per-mode glyph, and the two that leave one turn v_lite606 off.
//
// UNVERIFIED in presentation only: the glyph arguments (0x43ab08, 0x43ab10,
// 0x43aaa0, 0x43ab0c) are shared between modes in a pattern I have not read
// out, and four of the twelve setters show none. Settled by disassembling
// 0x423480..0x4235f0.
void EnterIdle();              // 0x423460 -> state 2
void EnterSkillShotAward();    // 0x423440 -> state 3
void EnterAttackShip();        // 0x423480 -> state 4
void EnterBermuda();           // 0x4234c0 -> state 5
void EnterStorm();             // 0x423500 -> state 6
void EnterTavern();            // 0x423520 -> state 7
void EnterTreasureHunt();      // 0x423560 -> state 8
void EnterMutiny();            // 0x4235a0 -> state 9
void EnterSwordFight();        // 0x4235c0 -> state 10; also resets the volcano bank
void EnterPegLegsLoot();       // 0x4235f0 -> state 0xb
void EnterLavaBall();          // 0x423610 -> state 0xc
void EnterJailBreak();         // 0x423630 -> state 0xd

// Board-level helpers the controllers drive. All live in the 0x423000-0x42d000
// range and belong to the collectible / scoring slices of the port.
void RestoreBanksAndLetters();     // 0x42ab40 -- rebuilds the three drop banks and
                                   // the GOLD lights from their bitmasks
void EjectLavaSink(float delay);   // 0x423680 -- a_sink8->Message(SinkResetTimer, delay)
void ResetVolcanoLights(int mode);     // 0x425310
void RestoreVolcanoLights();           // 0x4253a0 -- re-derives all four from a_spot11's mask
void RaiseShipBank();                  // 0x424440
void RaiseTavernBank();                // 0x424f70
void RaiseParrotBank();                // 0x4252d0
void ResetBrigLights(bool lit);        // 0x425d50
void ClearKegMagnets();                // 0x424c70 -- 0x4c to all six, lights off
void ClearTreasureXLights();           // 0x424500
int ArmNextTreasureX();                // 0x424580 -- advances the X cursor, returns a sweep id
void RestoreTreasureBoard();           // 0x424560
void BlankWindGroups();                // 0x425080
void FlashIndicatedWind(bool on);      // 0x425110
void LightShipwreckMystery(float t);   // 0x423a30
void AdvanceFathom(float t);           // 0x424350 -- ScoreMultiplier = fathoms + 1
void MaxCrew(float t);                 // 0x425000 -- a_bump2->MessageField = 6
void AwardCoin();                      // 0x423720
void AwardShipwreckMystery(float t);   // 0x425c50

// Two original-source bugs live behind these declarations rather than in this
// file. They are recorded here because this file is what reaches them, and a
// port has to decide about them deliberately rather than by accident:
//
//   * 0x424780 (AwardSpecialTreasure) switches on the treasure index 0..5 and
//     has a `default` arm, so a 7th and every later treasure re-lights
//     v_lite576 and pays the Crown of Sandige's 1,500,000 again while the
//     counter keeps growing. That inflates the drain bonus without bound (it
//     reads the counter); it does NOT inflate Peg Leg's Loot, which reads the
//     group's on-count -- see PegLegsLootController.
//   * 0x425ab0 (the Buried Treasure payout) pays the *second* buried special
//     treasure as `AwardSpecialTreasure(DAT_00435d18)` -- the first pouch
//     slot, which has already been cleared to -1 -- instead of
//     `DAT_00435d1c`. So the second buried treasure always falls to that same
//     `default` arm and always pays the Crown of Sandige.
//
// Neither is reproduced or suppressed here: both are in the collectible
// slice, and whichever way it goes the decision belongs at those two sites.
bool AnyHighScore();                   // 0x410ff0
void PrepareTreasureBoard();           // 0x42ce80 -- Peg Leg's Loot's 0x4f helper

// `*(int*)(info_text_box + 0x5e) != 0`: the text box's own display timer,
// non-zero while something is still on screen. The dispatcher checks it before
// re-spelling a text-box timer expiry as a mode tick.
//
// UNVERIFIED which TTextBox field that is -- the offset is confirmed by three
// independent uses, the name is not.
bool AnnouncementPending(TTextBox* box);

// Glyph runs. Skulduggery announces progress as single characters in a symbol
// font. `StormGlyphs[n]` is the run for the n-th bailed target (9 bytes per
// entry at 0x43ab28); `PegLegGlyphs[i]` is 'L','M',...,'S' (2 bytes per entry
// at 0x43ab16); `SkillShotGlyphs[i]` is 8 bytes per entry at 0x43aa58.
extern const char* StormGlyphs[];
extern const char* PegLegGlyphs[];
extern const char* SkillShotGlyphs[];

// `caller->GetComponentType()` stands for vtable slot 0 (see namespace `type`).
// Upstream has no such method -- it dispatches on dynamic_cast -- so this is
// the one thing a port has to add to TPinballComponent.
//
// `component->ActiveFlag` stands for the `+5` enable byte and
// `component->MessageField` for the `+6` int, exactly as in Dragon.

// Controllers owned by other slices of the port. They are states of this same
// machine, so the dispatcher below must reach them, but their bodies are the
// bonus slice's business.
void DrainBonusController(MessageCode code, TPinballComponent* caller);  // 0x42b820

// ---------------------------------------------------------------------------
// Module state
//
// Skulduggery is a hybrid: letters and drop banks are bitmasks in component
// MessageFields, but the per-mode scratch is in genuine file statics. These
// are those statics.
// ---------------------------------------------------------------------------

// 0x435cf8 -- the per-mode scratch counter. Every mode means something
// different by it (ships sunk, targets bailed, the Peg Leg's payout step) and
// StartOfEveryMode zeroes it at every mode start.
int ModeScratch = 0;

// 0x435d04 -- the shared cursor. It is the Treasure Hunt "X" selector, the
// Bermuda indicated-wind selector AND the Sword Fight award selector, all
// three aliased onto one int, and StartOfEveryMode zeroes it at every mode
// start.
//
// UNVERIFIED whether that aliasing is a deliberate single "wheel position"
// concept or a second bug of the v_lite551 kind. It cannot be decided from the
// code: nothing resets it per-mode except the shared setup, and the dispatcher
// never touches it. Settled only by instrumenting a real game -- clear a Sword
// Fight group, then start a Treasure Hunt, and see which X arms first.
int WheelCursor = 0;

// 0x435d2c / 0x435d40 -- Storm's severity, computed once at mode start: the
// score per bailed target and the number of targets per jackpot.
int StormTargetScore = 0;
int StormJackpotPeriod = 0;

// 0x435d50 -- Storm has no fixed name, so its 0x4f strcpy's the chosen
// severity string (3046..3049, each of which is "<Severity>!\n%ld") into this
// buffer and the 0x50 tick then uses the buffer as its own format string. That
// is the whole reason the copy exists.
char StormNameFormat[64] = {0};

// 0x435d30 -- the skill shot is in progress. Set by the skill-shot
// controller's 0x4f and by the N-M-A-X cheat, cleared by Idle's 0x4f. Every
// mode's "sweep finished" branch ejects the lava sink only when this is clear,
// i.e. a mode started FROM the skill shot does not feed a ball, because the
// launcher already served one.
//
// UNVERIFIED name: analysis/pirates-rules.md calls this cell "not in skill
// shot", which is the inverse of what the two writers do. Settled by reading
// the launcher handler (0x425e70), the only other reader.
bool SkillShotActive = false;

// 0x435d48 -- collisions with type-1006 TPopupTargets this ball, incremented
// by the dispatcher and zeroed by the skill shot's 0x4f. The drain bonus's
// "Target Bonus" term.
int TargetsDroppedThisBall = 0;

// 0x435d3c -- balls the brig is holding, 0..3. Jail Break and Lava-Ball both
// empty it into the ball count; so does the end-of-game lockdown, which closes
// the accounting loop.
int BallsInBrig = 0;

// 0x435d90 -- the shoot-again light's phase, 3 -> 0. Storm's west wind lane
// sets it to 2; the launcher steps it down.
// Owned by pirates_part2.cpp, where v_lite569's handler is the stepper.
extern int shoot_again_phase;

// 0x43d158 -- which of the four interleaved spokes of bermuda_light_group is
// lit. A 13-member group animated as a 4x4 table at 0.1 s per step.
int BermudaSpoke = 0;

// 0x43d118 -- those four spokes. -1 is "no member", which is how a 13-light
// group fits a 4x4 table.
const int BermudaSpokes[4][4] = {
	{0, 4, 8, 12},
	{1, 5, 9, -1},
	{2, 6, 10, -1},
	{3, 7, 11, -1},
};

// 0x43abb0 -- set by Peg Leg's Loot's teardown; makes the next drain blank the
// coin and special-treasure groups, i.e. a fresh collection cycle starts after
// the finale.
bool RestartCollectionCycle = false;

// 0x43aba0 -- cleared by the lockdown controller and read nowhere in either
// corpus. UNVERIFIED: a flag owned by the ball/serve slice. Settled by
// grepping the remaining ~118 handler-less components for its writer.
int LockdownFlagUnknown = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void Handler(MessageCode code, TPinballComponent* caller);
void ModeControl(Mode mode, MessageCode code, TPinballComponent* caller);
int SelectNextMode();

// ---------------------------------------------------------------------------
// State accessors
//
// Spelling these out documents, once, which MessageField means what. Every one
// is a `*(int*)(DAT_x + 6)` in the pseudocode.
// ---------------------------------------------------------------------------

Mode CurrentMode() { return static_cast<Mode>(tag_info_text_box.Component->MessageField); }
void SetMode(Mode m) { tag_info_text_box.Component->MessageField = static_cast<int>(m); }

// mode_timer's MessageField holds the selected slot as index+1, 0 = none.
// Re-using the timer component as the selection cell is pure Full Tilt.
ModeSlot SelectedSlot()
{
	int raw = tag_mode_timer.Component->MessageField;
	return raw ? static_cast<ModeSlot>(raw - 1) : ModeSlot::None;
}
void SetSelectedSlot(ModeSlot s)
{
	tag_mode_timer.Component->MessageField =
		s == ModeSlot::None ? 0 : static_cast<int>(s) + 1;
}

int ModesCompleted() { return tag_v_lite606.Component->MessageField; }
int MapPieces() { return tag_v_lite607.Component->MessageField; }

// The wheel lights in slot order. Indexed by ModeSlot, never by Mode.
TLight* const WheelLights[8] = {
	tag_v_lite552.Component, tag_v_lite553.Component,
	tag_v_lite554.Component, tag_v_lite555.Component,
	tag_v_lite556.Component, tag_v_lite557.Component,
	tag_v_lite558.Component, tag_v_lite559.Component,
};

TLightGroup* const WindGroups[8] = {
	tag_left_island_light_group.Component,
	tag_right_wind_light_group.Component,
	tag_left_orbit_light_group.Component,
	tag_right_orbit_light_group.Component,
	tag_south_wind_light_group.Component,
	tag_east_wind_1_light_group.Component,
	tag_east_wind_2_light_group.Component,
	tag_west_wind_light_group.Component,
};

// ---------------------------------------------------------------------------
// Small shared predicates
// ---------------------------------------------------------------------------

// The binary inlines `LightOnFlag || ToggledOnFlag || FlasherOnFlag` (+0x42,
// +0x4e, +0x46) wherever it asks "is this light owned". That is upstream's
// `TLight::light_on()`, and it counts a *flashing* light as owned -- which is
// why the mode picker has to fall back to a separate flashing-only scan when
// every wheel light is lit.
bool Owned(TLight* light) { return light->light_on(); }

// +0x46 alone: lit by a flasher, i.e. "selected but not yet completed".
bool Flashing(TLight* light) { return light->FlasherOnFlag != 0; }

// Show "<mode name>\n<seconds left>" for half a second, but only while the
// clock is actually running -- every controller gates on this so a cancelled
// timer does not print "0". This is the `0x50` arm of all eight modes.
void DisplayTimeRemaining(int nameStringId)
{
	int ms = static_cast<int>(tag_mode_timer.Component->Message(ft::TimerGetRemaining, 0.0f));
	if (ms == 0)
		return;

	char buffer[128];
	FormatWithValue(buffer, GetRcString(nameStringId), ms / 1000);
	Display(tag_info_text_box.Component, buffer, 0.5f);
}

// Storm's variant: the format string is the buffer its own 0x4f built.
void DisplayStormTimeRemaining()
{
	int ms = static_cast<int>(tag_mode_timer.Component->Message(ft::TimerGetRemaining, 0.0f));
	if (ms == 0)
		return;

	char buffer[128];
	FormatWithValue(buffer, StormNameFormat, ms / 1000);
	Display(tag_info_text_box.Component, buffer, 0.5f);
}

// ---------------------------------------------------------------------------
// Mode lifecycle
// ---------------------------------------------------------------------------

// 0x4255a0 -- SetModeWheelLight. Maps a *state* to its wheel light and lights
// it either flashing (selected or running) or solid (completed).
//
// The two codes come out of one arithmetic trick in the original:
// `code = (-(solid != 0) & 0xc) + 7`, i.e. 7 (FlasherStartTimed, with -1.0 =
// forever) when flashing and 0x13 (ResetAndTurnOn) when solid. Flashing ->
// solid is exactly what makes SelectNextMode stop offering that mode.
//
// The state-2 arm is the odd one: asked to set "idle"'s wheel light, this
// re-runs the whole selection instead. That is what makes the dispatcher's
// `0x4f`-in-idle path re-draw a mode (see ModeControl).
void SetModeWheelLight(Mode state, bool flashing);  // defined after SelectNextMode

// 0x425700 -- UnlightSelectedMode. Extinguish whichever wheel light the
// current selection owns and clear the selection cell. The default arm clears
// the cell even when the slot was out of range, so this cannot wedge.
void UnlightSelectedMode()
{
	ModeSlot slot = SelectedSlot();
	if (slot != ModeSlot::None && static_cast<int>(slot) < 8)
		WheelLights[static_cast<int>(slot)]->Message(ft::LightResetAndTurnOff, 0.0f);
	SetSelectedSlot(ModeSlot::None);
}

// 0x4256d0 -- OfferMode. Flash the chosen slot's wheel light forever and
// record the slot. Note that it goes through SetModeWheelLight, which wants a
// *state*, hence the ModeForSlot lookup -- the one place the permutation is
// applied on the way in.
void OfferMode(ModeSlot slot)
{
	if (static_cast<int>(slot) >= 8)
		return;
	SetModeWheelLight(ModeForSlot[static_cast<int>(slot)], /*flashing=*/true);
	SetSelectedSlot(slot);
}

// 0x425850 -- SelectNextMode, the counterpart of upstream's
// SelectMissionController. Upstream advances a rank and hands out the mission
// that rank unlocks; Skulduggery picks *uniformly at random* among the wheel
// lights that are not yet lit, with no ordering and no progression gate.
//
// Returns the chosen slot as an int, because three of its four exits are early
// returns that do NOT re-offer anything -- the caller gets the answer without
// the lights changing. That asymmetry is in the original.
int SelectNextMode()
{
	auto* mapPieces = tag_map_piece_light_group.Component;
	int lit = static_cast<int>(mapPieces->Message(ft::GroupGetOnCount, 0.0f));
	int total = static_cast<int>(mapPieces->Message(ft::GroupGetLightCount, 0.0f));

	int chosen;
	if (lit == total)
	{
		// All six map pieces: the finale, unconditionally and with no draw.
		chosen = static_cast<int>(ModeSlot::PegLegsLoot);
	}
	else
	{
		// The pool is the first SEVEN lights only. v_lite559 (Peg Leg's Loot)
		// is deliberately not drawable; it is reachable only by the branch
		// above and by the skill shot.
		int candidates[7];
		int count = 0;
		for (int slot = 0; slot < 7; slot++)
			if (!Owned(WheelLights[slot]))
				candidates[count++] = slot;

		if (count == 0)
		{
			// Every mode has been played. Fall back to whichever light is
			// still *flashing* -- that is the one already selected, so the
			// offer stands. These eight exits return WITHOUT re-offering,
			// which is how a standing selection survives.
			for (int slot = 0; slot < 8; slot++)
				if (Flashing(WheelLights[slot]))
					return slot;
			chosen = static_cast<int>(ModeSlot::PegLegsLoot);
		}
		else
		{
			chosen = candidates[Rnd(count)];
		}
	}

	UnlightSelectedMode();
	OfferMode(static_cast<ModeSlot>(chosen));
	return chosen;
}

// 0x4255a0, continued -- see the declaration above for the shape.
void SetModeWheelLight(Mode state, bool flashing)
{
	UnlightSelectedMode();
	MessageCode code = flashing ? ft::LightFlasherStartTimed : ft::LightResetAndTurnOn;

	switch (state)
	{
	case Mode::Idle:
		// Not a light at all: "the wheel for idle" means "pick a new mode".
		SelectNextMode();
		return;
	case Mode::AttackShip:   WheelLights[0]->Message(code, Forever); return;
	case Mode::TreasureHunt: WheelLights[1]->Message(code, Forever); return;
	case Mode::Tavern:       WheelLights[2]->Message(code, Forever); return;
	case Mode::Bermuda:      WheelLights[3]->Message(code, Forever); return;
	case Mode::Storm:        WheelLights[4]->Message(code, Forever); return;
	case Mode::Mutiny:       WheelLights[5]->Message(code, Forever); return;
	case Mode::SwordFight:   WheelLights[6]->Message(code, Forever); return;
	case Mode::PegLegsLoot:  WheelLights[7]->Message(code, Forever); return;
	default:
		// Every other state -- skill shot, the multiballs, the bonus -- has no
		// wheel light, and the selection has already been dropped above.
		return;
	}
}

// 0x425a50 -- StartSelectedMode. Entered from the SKULDUGGERY bank completion
// and from the N-M-A-X cheat. Maps the selection slot straight to its state
// setter; the identity of that map IS the permutation, which is why this
// function and ModeForSlot have to agree.
void StartSelectedMode()
{
	switch (SelectedSlot())
	{
	case ModeSlot::AttackShip:   EnterAttackShip(); return;
	case ModeSlot::TreasureHunt: EnterTreasureHunt(); return;
	case ModeSlot::Tavern:       EnterTavern(); return;
	case ModeSlot::Bermuda:      EnterBermuda(); return;
	case ModeSlot::Storm:        EnterStorm(); return;
	case ModeSlot::Mutiny:       EnterMutiny(); return;
	case ModeSlot::SwordFight:   EnterSwordFight(); return;
	case ModeSlot::PegLegsLoot:  EnterPegLegsLoot(); return;
	default:
		// Nothing selected: spelling the word with no mode offered does
		// nothing at all, which cannot normally happen because the captive
		// ball and every teardown keep a selection standing.
		return;
	}
}

// 0x42aad0 -- the `0x4f` preamble every one of the eight modes runs, and a
// real shared function in the original rather than an inlined block.
void StartOfEveryMode()
{
	ModeScratch = 0;
	WheelCursor = 0;
	// ResetGoldLights(0) and ResetSkulduggeryLights(0) -- 0x423870 / 0x423b70,
	// the collectible slice's business.
	tag_a_bloce.Component->Message(ft::BlockerRestartTimeout, 1.0f);
	tag_v_lite504.Component->Message(ft::LightResetAndTurnOn, 0.0f);
	tag_v_lite505.Component->Message(ft::LightResetAndTurnOff, 0.0f);

	// Lava-Ball is exempt: it runs this same setup but must not have its
	// volcano bank reset underneath it, since the volcano bank is what started
	// it. The guard reads the register directly rather than taking a parameter.
	if (CurrentMode() != Mode::LavaBall)
		ResetVolcanoLights(0);
}

// No address: the tail every one of the eight modes' `0x52` runs is five
// statements copy-pasted into each controller, not a function -- which is
// worth knowing, because it means a controller could in principle have
// diverged and none did.
//
// This is where "completion" is booked, and it is reached on timer expiry and
// on drain-abort exactly as on success.
void EndOfEveryMode()
{
	RestoreBanksAndLetters();
	tag_v_lite606.Component->MessageField++;  // modes completed this game
	tag_a_bloce.Component->Message(ft::BlockerRestartTimeout, 0.0f);
	tag_v_lite504.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_v_lite505.Component->Message(ft::LightResetAndTurnOn, 0.0f);
}

// No address: the `0x46`/light-sweeps arm shared by all eight modes, factored
// out of each. The sweep has finished, so arm the clock and announce the mode.
//
// As in Dragon's Keep the timer does NOT start at mode start -- it starts when
// the intro sweep ends, so the sweep is free time.
void ModeSweepFinished(float timeLimit)
{
	// Release whatever the lava sink is holding, unless the ball came from the
	// launcher (a mode started by the skill shot needs no extra feed).
	if (!SkillShotActive)
		EjectLavaSink(0.0f);

	tag_mode_timer.Component->Message(ft::TimerSet, timeLimit);
	Handler(ft::MissionStarted, nullptr);
}

// No address: the `0x53` arm shared by all eight modes. Cancel the clock, then fall
// straight into the teardown. There is no separate "failed" path -- this is
// the other half of "completion is never checked".
void AbortMode()
{
	tag_mode_timer.Component->Message(ft::TimerSet, Forever);
	Handler(ft::ModeTeardown, nullptr);
}

// ---------------------------------------------------------------------------
// State 1 -- game over
// ---------------------------------------------------------------------------

// 0x42bfd0. Not a bonus state: the bonus has already been paid by state 0x10.
void GameOverController(MessageCode code)
{
	if (code != ft::MissionComplete)
		return;

	pb::MainTable->Message(ft::TableGameOver, 0.0f);
	if (CurrentMusic() != game_over_music)
		PlayMusic(game_over_music);

	// The announcement is conditional on there being a high score to announce.
	if (!AnyHighScore())
		return;

	Play(tag_tabb73wave.Component);

	// GAP, deliberately left unimplemented rather than guessed: between the
	// wave and the display the original dereferences `TableG+0x13e` as a
	// component pointer and sends it (0x10, 3.0) -- a three-second
	// flash-then-off, i.e. a victory flash on some light or group. 0x13e is
	// not a TPinballTable field this corpus names anywhere else, so which
	// component it is cannot be recovered from the rules code. Settled by
	// reading TPinballTable's field layout out of CADET.EXE, where the same
	// engine struct is used by a table whose handlers name more of it.

	Display(tag_info_text_box.Component, GetRcString(STR_HIGH_SCORE), Forever);
}

// ---------------------------------------------------------------------------
// State 2 -- idle
// ---------------------------------------------------------------------------

// 0x42ab90. The analogue of upstream's `UnselectMissionController` body.
// Shorter than Dragon's because Skulduggery has no announcement-carryover
// flag: the skill shot clears the text box itself before it hands over.
void IdleController(MessageCode code)
{
	if (code == ft::MissionComplete)
	{
		SkillShotActive = false;
		if (CurrentMusic() != idle_music)
			PlayMusic(idle_music);
		return;
	}

	if (code == ft::MissionStarted)
		Display(tag_info_text_box.Component, GetRcString(STR_IDLE_HINT), Forever);
}

// ---------------------------------------------------------------------------
// State 3 -- skill shot
//
// The launcher's rolling cursor, locked in by the first thing the served ball
// touches. Five of the nine slots start a mode *directly*, bypassing the
// selection wheel entirely -- so the skill shot can start a mode the wheel was
// not offering, and can start one whose wheel light is already lit.
// ---------------------------------------------------------------------------

// 0x42abf0.
void SkillShotController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::MissionComplete:
		SkillShotActive = true;
		TargetsDroppedThisBall = 0;
		tag_a_blocc.Component->Message(ft::BlockerRestartTimeout, Forever);
		tag_a_bloce.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		tag_v_lite504.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite505.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		tag_a_blocf.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		tag_a_blocg.Component->Message(ft::BlockerRestartTimeout, Forever);
		return;

	case ft::MissionStarted:
	{
		// Roll 0 -> 8 -> 0 on every tick and show the slot's glyph for 0.2 s.
		int slot = tag_s_launcher1.Component->MessageField + 1;
		if (slot == 9)
			slot = 0;
		tag_s_launcher1.Component->MessageField = slot;
		DisplayGlyph(tag_info_text_box.Component, SkillShotGlyphs[slot], 0.2f);
		return;
	}

	case ft::Collision:
		if (caller != tag_s_oneway3.Component)
			return;
		break;

	default:
		return;
	}

	int slot = tag_s_launcher1.Component->MessageField;
	ClearText(tag_info_text_box.Component);

	switch (slot)
	{
	case 0:
		AwardShipwreckMystery(0.0f);
		EnterIdle();
		return;
	case 1:
	case 2:
	case 8:
		AwardCoin();
		EnterIdle();
		return;
	// The five mode slots. Note they go to the *state* setters, not through
	// StartSelectedMode, so SkillShotActive stays set and the mode's sweep
	// will not feed a ball from the lava sink.
	case 3: SetMode(Mode::Bermuda); Handler(ft::MissionComplete, nullptr); return;
	case 4: SetMode(Mode::AttackShip); Handler(ft::MissionComplete, nullptr); return;
	case 5: SetMode(Mode::TreasureHunt); Handler(ft::MissionComplete, nullptr); return;
	case 6: SetMode(Mode::Tavern); Handler(ft::MissionComplete, nullptr); return;
	case 7: SetMode(Mode::Storm); Handler(ft::MissionComplete, nullptr); return;
	default:
		// Unreachable: the cursor is clamped to 0..8 above.
		return;
	}

	// UNVERIFIED in one respect: the original calls the *setter* functions
	// (0x4234c0 etc.), which also set the wheel light and show a glyph, where
	// the five lines above only set the register and broadcast. The difference
	// is cosmetic for the state machine but not for the display. Settled by
	// disassembling 0x423480..0x423560 and inlining what they do.
}

// ---------------------------------------------------------------------------
// State 4 -- Attack Ship
//
// Sink the ship: drop its five targets, or put the ball straight in the ship
// sink. Hint 3060, "Hit ship to sink."
// ---------------------------------------------------------------------------

// 0x42ad70.
void AttackShipController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			ModeSweepFinished(TimeAttackShip);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
				Handler(ft::ModeTeardown, nullptr);
			return;
		default:
			return;
		}

	case ft::NotifyTimerExpired:
		// The fathom group finished its animation: that is the "Ship Sunk"
		// announcement, which is why it is driven off a light-group notify
		// rather than from the sink handler.
		if (caller == tag_fathom_light_group.Component)
		{
			Display(tag_info_text_box.Component, GetRcString(STR_SHIP_SUNK), 2.0f);
			Play(tag_tabb26wave.Component);
		}
		return;

	case ft::MissionComplete:
		StartOfEveryMode();
		// Pre-set to 1, not 0: the ship sink's own handler increments it, and
		// the mode wants the first sink to read as "the first ship".
		ModeScratch = 1;
		tag_light_sweeps.Component->Message(ft::LightSweepStart, SweepAttackShip);
		RaiseShipBank();
		tag_a_blocc.Component->Message(ft::BlockerRestartTimeout, Forever);
		tag_a_bloce.Component->Message(ft::BlockerRestartTimeout, 1.0f);
		tag_v_lite504.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		tag_v_lite505.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_blocf.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		tag_a_blocg.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		AddScoreAt(nullptr, BonusAttackShip);
		Display(tag_info_text_box.Component, GetRcString(STR_HINT_ATTACK_SHIP), Forever);
		tag_v_lite4.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite5.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite6.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite7.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite8.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite567.Component->Message(ft::LightFlasherStartTimed, Forever);
		PlayMusic(attack_sword_music);
		Play(tag_tabb58wave.Component);
		return;

	case ft::MissionStarted:
		DisplayTimeRemaining(STR_NAME_ATTACK_SHIP);
		return;

	case ft::ModeTeardown:
		tag_v_lite567.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		RaiseShipBank();
		EndOfEveryMode();
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// State 5 -- Bermuda Triangle
//
// Follow the winds: hit the lane the cursor indicates, repeatedly, for an
// escalating progressive. Hint 3063, "Follow the Winds."
// ---------------------------------------------------------------------------

// 0x42c070.
void BermudaController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			// Bermuda is the only mode that arms its playfield in the
			// sweep-finished branch rather than at 0x4f: the rotating circle
			// and the centre magnet would otherwise be live under the sweep.
			tag_bermuda_light_group.Component->Message(ft::GroupRestartNotifyTimer, 0.1f);
			tag_a_kout1.Component->Message(ft::HoldFieldEnable, 0.0f);
			FlashIndicatedWind(true);
			ModeSweepFinished(TimeBermuda);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
				Handler(ft::ModeTeardown, nullptr);
			return;
		default:
			return;
		}

	case ft::NotifyTimerExpired:
		if (caller == tag_bermuda_light_group.Component)
		{
			// One step of the four-phase spoke animation: turn the current
			// spoke's four members off, advance, turn the next spoke's on, and
			// re-arm the 0.1 s notify. Members numbered -1 do not exist; the
			// group is expected to ignore them.
			auto* group = tag_bermuda_light_group.Component;
			group->Message(ft::GroupRestartNotifyTimer, 0.1f);
			for (int i = 0; i < 4; i++)
				group->Message(ft::GroupTurnOffAtIndex,
				               static_cast<float>(BermudaSpokes[BermudaSpoke][i]));
			if (++BermudaSpoke > 3)
				BermudaSpoke = 0;
			for (int i = 0; i < 4; i++)
				group->Message(ft::GroupTurnOnAtIndex,
				               static_cast<float>(BermudaSpokes[BermudaSpoke][i]));
		}
		return;

	case ft::MissionComplete:
		StartOfEveryMode();
		AddScoreAt(nullptr, BonusBermuda);
		tag_light_sweeps.Component->Message(ft::LightSweepStart, SweepBermuda);
		BlankWindGroups();
		PlayMusic(bermuda_music);
		Play(tag_tabb66wave.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_HINT_BERMUDA), Forever);
		return;

	case ft::MissionStarted:
		DisplayTimeRemaining(STR_NAME_BERMUDA);
		return;

	case ft::ModeTeardown:
		BlankWindGroups();
		tag_bermuda_light_group.Component->Message(ft::GroupRestartNotifyTimer, Forever);
		tag_bermuda_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_kout1.Component->Message(ft::HoldFieldDisable, 0.0f);
		EndOfEveryMode();
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}

	// NOT HERE, deliberately: the progressive award table
	// {125,000; 250,000; 500,000; 1,000,000; jackpot} and the flat 50,000 for a
	// non-indicated lane live in the lane handlers (0x425230), not in this
	// controller. The controller only owns the circle, the magnet and the clock.
}

// ---------------------------------------------------------------------------
// State 6 -- Storm
//
// Bail water: hit drop targets, with the score per target and the jackpot
// period fixed once at mode start by how many wind lights are already lit.
// Hint 3067, "Hit targets to bail water."
// ---------------------------------------------------------------------------

// 0x42c430.
void StormController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			ModeSweepFinished(TimeStorm);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
			{
				// Storm is the only mode that extinguishes a light in the
				// timer-expiry path rather than leaving it to the teardown;
				// the teardown then does it again.
				tag_v_lite778.Component->Message(ft::LightResetAndTurnOff, Forever);
				Handler(ft::ModeTeardown, nullptr);
			}
			return;
		default:
			return;
		}

	case ft::Collision:
		switch (caller->GetComponentType())
		{
		case type::PopupTarget:
		{
			// One bailed target. The Nth pays the jackpot *instead of* the
			// per-target score and resets the count, so the jackpot hit itself
			// is worth no points.
			ModeScratch++;
			if (StormJackpotPeriod == ModeScratch)
			{
				AwardJackpot(0.0f);
				ModeScratch = 0;
				return;
			}
			Play(tag_tabb68wave.Component);
			AddScoreAt(caller, StormTargetScore);
			DisplayGlyph(tag_info_text_box.Component, StormGlyphs[ModeScratch], 2.0f);
			return;
		}

		case type::LightRollover:
			// The west wind lane lights "shoot again" for ten seconds.
			if (caller == tag_a_roll6.Component)
			{
				shoot_again_phase = 2;
				tag_v_lite569.Component->Message(ft::LightTurnOnTimed, 10.0f);
			}
			return;

		case type::Sink:
			// Any hideaway ENDS the mode early -- the only mode in the table
			// with a player-triggered bail-out -- and arms the shipwreck
			// mystery as the consolation. The self-send is a direct call in
			// the original, not a broadcast, so it skips the dispatcher's
			// jackpot accrual for this collision.
			if (caller == tag_a_sink17.Component || caller == tag_a_sink15.Component ||
			    caller == tag_a_sink14.Component)
			{
				Display(tag_info_text_box.Component, GetRcString(STR_SHIPWRECKED), 2.0f);
				StormController(ft::ModeAbort, caller);
				LightShipwreckMystery(2.0f);
			}
			return;

		default:
			return;
		}

	case ft::MissionComplete:
	{
		StartOfEveryMode();
		AddScoreAt(nullptr, BonusStorm);
		tag_light_sweeps.Component->Message(ft::LightSweepStart, SweepStorm);
		PlayMusic(storm_mutiny_music);
		Play(tag_tabb67wave.Component);
		tag_a_blocc.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		tag_a_blocf.Component->Message(ft::BlockerRestartTimeout, Forever);
		tag_a_blocg.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		// Storm is the only mode that re-raises all three drop banks: every
		// target on the table is a bailing target.
		RaiseTavernBank();
		RaiseShipBank();
		RaiseParrotBank();
		tag_v_lite778.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_a_kout2.Component->Message(ft::HoldFieldEnable, 0.0f);
		// All eleven SKULDUGGERY letters flash, matching the three raised banks.
		tag_v_lite1.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite2.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite3.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite4.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite5.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite6.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite7.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite8.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite9.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite10.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite11.Component->Message(ft::LightFlasherStartTimed, Forever);

		// Severity, computed once from the total on-count of all eight wind
		// light groups. The 5/10/15 banding is the binary's; the four tiers
		// and their jackpot periods match the manual exactly.
		int winds = 0;
		for (int i = 0; i < 8; i++)
			winds += static_cast<int>(WindGroups[i]->Message(ft::GroupGetOnCount, 0.0f));

		int severityString;
		if (winds <= 5)
		{
			StormJackpotPeriod = 8;
			StormTargetScore = 10000;
			severityString = STR_STORM_SQUALL;
		}
		else if (winds <= 10)
		{
			StormJackpotPeriod = 7;
			StormTargetScore = 25000;
			severityString = STR_STORM_TROPICAL;
		}
		else if (winds <= 15)
		{
			StormJackpotPeriod = 6;
			StormTargetScore = 50000;
			severityString = STR_STORM_MONSOON;
		}
		else
		{
			StormJackpotPeriod = 5;
			StormTargetScore = 100000;
			severityString = STR_STORM_HURRICANE;
		}

		// The severity string becomes this mode's *name format* for the rest of
		// the run. The original is an inlined strcpy; the semantics are the
		// point, not the instruction count.
		const char* severity = GetRcString(severityString);
		size_t n = 0;
		while (severity[n] && n + 1 < sizeof StormNameFormat)
		{
			StormNameFormat[n] = severity[n];
			n++;
		}
		StormNameFormat[n] = '\0';

		Display(tag_info_text_box.Component, GetRcString(STR_HINT_STORM), Forever);
		return;
	}

	case ft::MissionStarted:
		DisplayStormTimeRemaining();
		return;

	case ft::ModeTeardown:
		tag_v_lite778.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_kout2.Component->Message(ft::HoldFieldDisable, 0.0f);
		EndOfEveryMode();
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}

	// CORRECTION to analysis/pirates-rules.md §3 and §6.0, which credit Storm
	// with "+50,000 to the jackpot per type-0x3fc collision". There is no
	// jackpot accrual in this controller at all: `call 0x423650` appears zero
	// times in 0x42c430, in the pseudocode and in the listing alike. The two
	// mode-local accruals that do exist are Peg Leg's Loot's +50,000
	// (0x42cfc0) and Treasure Hunt's +20,000 (0x42b090); the analysis
	// appears to have attributed the former to the wrong mode. Storm's
	// jackpot comes only from the dispatcher's per-collision accrual and from
	// its own every-Nth-target AwardJackpot above.
}

// ---------------------------------------------------------------------------
// State 7 -- Tavern
//
// Hit the tavern bank; each target arms a random keg magnet. The controller's
// own job is almost entirely the three window lights' random flutter.
// Hint 3062, "Hit the Tavern."
// ---------------------------------------------------------------------------

// 0x42b420.
void TavernController(MessageCode code, TPinballComponent* caller)
{
	// The original is a loop, not a switch: the sweep-finished branch falls
	// back to the top of the switch with (0x46, v_lite547) substituted, which
	// kicks the window flutter off immediately rather than waiting for the
	// first real timer expiry. Spelling that as a goto-free re-entry keeps the
	// behaviour and loses the label.
	for (;;)
	{
		switch (code)
		{
		case ft::MissionComplete:
			StartOfEveryMode();
			ResetBrigLights(false);
			AddScoreAt(nullptr, BonusTavern);
			tag_light_sweeps.Component->Message(ft::LightSweepStart, SweepTavern);
			PlayMusic(tavern_music);
			Play(tag_tabb65wave.Component);
			RaiseTavernBank();
			tag_v_lite547.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite546.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite545.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			// Reset the brig target's hit counter: during Tavern its third hit
			// means "Max Crew", not "start Jail Break".
			tag_a_spot6.Component->MessageField = 0;
			tag_v_lite530.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite531.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite532.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			Display(tag_info_text_box.Component, GetRcString(STR_HINT_TAVERN), Forever);
			tag_v_lite1.Component->Message(ft::LightFlasherStartTimed, Forever);
			tag_v_lite2.Component->Message(ft::LightFlasherStartTimed, Forever);
			tag_v_lite3.Component->Message(ft::LightFlasherStartTimed, Forever);
			return;

		case ft::MissionStarted:
			DisplayTimeRemaining(STR_NAME_TAVERN);
			return;

		case ft::ModeTeardown:
			ResetBrigLights(false);
			RaiseTavernBank();
			ClearKegMagnets();  // keg magnets do not survive the mode
			EndOfEveryMode();
			return;

		case ft::ModeAbort:
			AbortMode();
			return;

		case ft::TimerExpired:
			break;  // handled below

		default:
			return;
		}

		switch (caller->GetComponentType())
		{
		case type::Light:
		{
			// One window light's flash expired: re-flash a *randomly chosen*
			// one of the three for 0.100-0.250 s. The chain is self-sustaining
			// -- each expiry schedules the next -- which is why the mode start
			// has to prime it once.
			if (caller != tag_v_lite547.Component && caller != tag_v_lite546.Component &&
			    caller != tag_v_lite545.Component)
				return;

			TLight* window;
			switch (Rnd(3))
			{
			case 0: window = tag_v_lite547.Component; break;
			case 1: window = tag_v_lite546.Component; break;
			default: window = tag_v_lite545.Component; break;
			}
			window->Message(ft::LightFlasherStartTimed,
			                static_cast<float>(Rnd(150) + 100) / 1000.0f);
			return;
		}

		case type::Timer:
			if (caller != tag_mode_timer.Component)
				return;
			// Stop the flutter before the teardown, or the chain would keep
			// re-arming itself after the mode is gone.
			tag_v_lite547.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite546.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite545.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			Handler(ft::ModeTeardown, nullptr);
			return;

		case type::LightSweeps:
			ModeSweepFinished(TimeTavern);
			// Prime the window flutter by re-entering with a synthetic
			// "v_lite547's flash expired".
			code = ft::TimerExpired;
			caller = tag_v_lite547.Component;
			continue;

		default:
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// State 8 -- Treasure Hunt
//
// One "X" light is armed at a time; a hideaway or extra sink under the armed X
// pays a special treasure. Hint 3061, "Hit the X to find treasure."
// ---------------------------------------------------------------------------

// 0x42b090.
void TreasureHuntController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			ModeSweepFinished(TimeTreasureHunt);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
				Handler(ft::ModeTeardown, nullptr);
			return;
		default:
			return;
		}

	case ft::Collision:
		if (caller->GetComponentType() == type::Unknown1020)
			AddToJackpot(20000);
		return;

	case ft::MissionComplete:
		StartOfEveryMode();
		tag_a_blocc.Component->Message(ft::BlockerRestartTimeout, Forever);
		tag_a_blocf.Component->Message(ft::BlockerRestartTimeout, Forever);
		tag_a_blocg.Component->Message(ft::BlockerRestartTimeout, Forever);
		AddScoreAt(nullptr, BonusTreasureHunt);
		ClearTreasureXLights();
		// The orbits are taken out of play for the duration: their trip wires
		// are disabled and their light groups blanked, with the on-counts
		// parked in the trip wires' own MessageFields so the teardown can put
		// the player's orbit progress back exactly as it found it.
		tag_s_trip3.Component->ActiveFlag = 0;
		tag_s_trip8.Component->ActiveFlag = 0;
		tag_s_trip3.Component->MessageField =
			static_cast<int>(tag_left_orbit_light_group.Component->Message(ft::GroupGetOnCount, 0.0f));
		tag_s_trip8.Component->MessageField =
			static_cast<int>(tag_right_orbit_light_group.Component->Message(ft::GroupGetOnCount, 0.0f));
		tag_left_orbit_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_right_orbit_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_left_orbit_combo_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_right_orbit_combo_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite520.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite521.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		// The sweep id is not a constant here: arming the first X returns the
		// id of the sweep that points at it, so the intro animation tracks the
		// objective.
		tag_light_sweeps.Component->Message(ft::LightSweepStart,
		                                    static_cast<float>(ArmNextTreasureX()));
		PlayMusic(treasure_pegleg_music);
		Play(tag_tabb59wave.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_HINT_TREASURE_HUNT), Forever);
		return;

	case ft::MissionStarted:
		DisplayTimeRemaining(STR_NAME_TREASURE_HUNT);
		return;

	case ft::ModeTeardown:
		ClearTreasureXLights();
		RestoreTreasureBoard();
		EndOfEveryMode();
		tag_v_lite519.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_s_trip3.Component->ActiveFlag = 1;
		tag_s_trip8.Component->ActiveFlag = 1;
		// Turn both orbit groups off, then restore the saved on-count through
		// ToggleSplitIndex -- which is the group's own "light the first N"
		// operation, not an index-at-a-time replay.
		tag_left_orbit_light_group.Component->Message(ft::LightTurnOff, 0.0f);
		tag_right_orbit_light_group.Component->Message(ft::LightTurnOff, 0.0f);
		tag_left_orbit_light_group.Component->Message(
			ft::GroupToggleSplitIndex, static_cast<float>(tag_s_trip3.Component->MessageField));
		tag_right_orbit_light_group.Component->Message(
			ft::GroupToggleSplitIndex, static_cast<float>(tag_s_trip8.Component->MessageField));
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}

	// NOT HERE: the X-is-flashing test, the special-treasure award and the
	// "bury an award" path at the east wind lanes all live in the sink and
	// lane handlers. Two original-source bugs reachable from this mode are
	// documented at the interface declarations above and in PegLegsLoot below.
}

// ---------------------------------------------------------------------------
// State 9 -- Mutiny
//
// Suppress the rebellion by hitting bumpers. The longest mode (48.28 s) and
// the cheapest to start (100,000). Hint 3064.
//
// This controller is nearly empty on purpose: the mode's whole rule -- 25,000
// per bumper hit with a jackpot on every tenth -- lives in the bumper handler,
// gated on the state. The controller owns only music, blockers and the clock.
// ---------------------------------------------------------------------------

// 0x42ca00.
void MutinyController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			ModeSweepFinished(TimeMutiny);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
				Handler(ft::ModeTeardown, nullptr);
			return;
		default:
			return;
		}

	case ft::MissionComplete:
		StartOfEveryMode();
		AddScoreAt(nullptr, BonusMutiny);
		tag_light_sweeps.Component->Message(ft::LightSweepStart, SweepMutiny);
		PlayMusic(storm_mutiny_music);
		Play(tag_tabb73wave.Component);
		// All three blockers released: Mutiny wants the ball loose in the
		// bumper field, not funnelled at an objective.
		tag_a_blocc.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		tag_a_blocf.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		tag_a_blocg.Component->Message(ft::BlockerRestartTimeout, 0.0f);
		Display(tag_info_text_box.Component, GetRcString(STR_HINT_MUTINY), Forever);
		return;

	case ft::MissionStarted:
		DisplayTimeRemaining(STR_NAME_MUTINY);
		return;

	case ft::ModeTeardown:
		EndOfEveryMode();
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// State 10 -- Sword Fight
//
// Clear the parrot bank or the volcano bank; either clear awards one
// collectible from a cyclic list. Hint 3065.
//
// Like Mutiny, the rules are elsewhere: the bank routines call 0x425550 ->
// 0x425440, which cycles 0 -> special treasure, 1 -> coin, 2 -> map piece,
// >=3 -> extra ball. The choice is CYCLIC in that fixed order, not the
// manual's "one of", and the cursor keeps advancing -- so clearing four groups
// in one Sword Fight pays all four awards once each, then extra balls.
// ---------------------------------------------------------------------------

// 0x42cc10.
void SwordFightController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			ModeSweepFinished(TimeSwordFight);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
				Handler(ft::ModeTeardown, nullptr);
			return;
		default:
			return;
		}

	case ft::MissionComplete:
		StartOfEveryMode();
		AddScoreAt(nullptr, BonusSwordFight);
		tag_light_sweeps.Component->Message(ft::LightSweepStart, SweepSwordFight);
		// The three parrot letters and the four volcano lights, which are the
		// two clearable groups.
		tag_v_lite9.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite10.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite11.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite551.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite550.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite549.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite548.Component->Message(ft::LightFlasherStartTimed, Forever);
		RaiseParrotBank();
		PlayMusic(attack_sword_music);
		Play(tag_tabb75wave.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_HINT_SWORD_FIGHT), Forever);
		return;

	case ft::MissionStarted:
		DisplayTimeRemaining(STR_NAME_SWORD_FIGHT);
		return;

	case ft::ModeTeardown:
		EndOfEveryMode();
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}

	// UNVERIFIED, and it is a real gap: nothing resets the shared WheelCursor
	// for Sword Fight except StartOfEveryMode. So the award a Sword Fight
	// hands out depends on what a *previous* mode left in that cell, since
	// Treasure Hunt and Bermuda Triangle advance the same int. See WheelCursor.
}

// ---------------------------------------------------------------------------
// State 0xb -- Peg Leg's Loot
//
// The finale. Reached only by collecting all six map pieces (SelectNextMode
// returns slot 7 unconditionally) or through the skill shot. Start bonus
// 1,000,000, the largest in the table. Hint 3066.
//
// The payout is a stepped animation driven by ModeScratch, armed when the ball
// enters the ship sink (whose handler sets ModeScratch = 1). Steps 1..8 are
// eight glyphs at 0.25 s; step 9 is the whole payout; step 10 tears the mode
// down and starts Lava-Ball.
// ---------------------------------------------------------------------------

// 0x42cfc0.
void PegLegsLootController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		switch (caller->GetComponentType())
		{
		case type::LightSweeps:
			// CORRECTION to analysis/pirates-rules.md §2.3, which says Peg
			// Leg's Loot is the only mode with no light sweep and therefore
			// arms its timer directly at 0x4f. It does have one: the sweep id
			// 4.0 is sent from PrepareTreasureBoard (0x42ce80), its 0x4f
			// helper, rather than from the 0x4f body -- so the grep for 0x1b
			// in 0x42cfc0 comes up empty while the sweep still happens. The
			// clock is armed here like every other mode's, with 30.68 s read
			// off `push 0x41f570a4` at 0x42d034.
			ModeSweepFinished(TimePegLegsLoot);
			return;
		case type::Timer:
			if (caller == tag_mode_timer.Component)
				Handler(ft::ModeTeardown, nullptr);
			return;
		default:
			return;
		}

	case ft::Collision:
		if (caller->GetComponentType() == type::Unknown1020)
			AddToJackpot(50000);
		return;

	case ft::MissionComplete:
		StartOfEveryMode();
		AddScoreAt(nullptr, BonusPegLegsLoot);
		// 0x42ce80: re-use the Treasure Hunt board (the X lights, the two
		// extra sinks, the orbit override) and fire the 4.0 sweep.
		PrepareTreasureBoard();
		PlayMusic(treasure_pegleg_music);
		Play(tag_tabb99wave.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_HINT_PEG_LEGS_LOOT), Forever);
		return;

	case ft::MissionStarted:
		switch (ModeScratch)
		{
		case 0:
			// Not armed yet: behave like every other mode's tick.
			DisplayTimeRemaining(STR_NAME_PEG_LEGS_LOOT);
			return;

		case 1:
			// The ball is in the ship sink. Stop the clock -- from here the
			// payout animation, not the timer, drives the mode -- and start
			// the fanfare.
			tag_mode_timer.Component->Message(ft::TimerSet, Forever);
			Play(tag_tabb28wave.Component);
			// fall through
		case 2: case 3: case 4: case 5: case 6: case 7: case 8:
			// Eight glyphs, 'L' to 'S', 0.25 s each.
			DisplayGlyph(tag_info_text_box.Component, PegLegGlyphs[ModeScratch], 0.25f);
			ModeScratch++;
			return;

		case 9:
		{
			// The payout, exactly the manual's four terms.
			int coins = static_cast<int>(
				tag_coin_light_group.Component->Message(ft::GroupGetOnCount, 0.0f));
			int treasures = static_cast<int>(
				tag_special_treasure_light_group.Component->Message(ft::GroupGetOnCount, 0.0f));
			int maps = static_cast<int>(
				tag_map_piece_light_group.Component->Message(ft::GroupGetOnCount, 0.0f));
			int modes = ModesCompleted();

			// Order matters: the multiplier is bumped by one fathom BEFORE the
			// total is scored, and AddScoreAt is the MULTIPLIED wrapper, so
			// the finale pays the new multiplier on the whole total. ("Max
			// Crew" and the jackpot in between do not touch it.)
			AdvanceFathom(Forever);
			MaxCrew(Forever);
			AwardJackpot(0.0f);

			int total = coins * 500000 + treasures * 500000 + maps * 1000000 + modes * 1000000;
			AddScoreAt(nullptr, total);

			char buffer[128];
			FormatWithValue(buffer, GetRcString(STR_SCORE_AWARDED), total);
			Display(tag_info_text_box.Component, buffer, 4.0f);
			ModeScratch++;
			return;

			// REPRODUCED-BY-OMISSION, worth stating because the analysis gets
			// it slightly wrong: the "7th+ special treasure keeps paying
			// 1,500,000 on an unbounded counter" bug inflates the DRAIN BONUS,
			// which reads `special_treasure_light_group->MessageField`, but
			// NOT this payout, which reads the group's *on-count* (0x29, read
			// off the listing at 0x42d1fd) and so saturates at six. Same for
			// the map pieces and the coins. The modes-completed term is the
			// unbounded one here, via the no-win-check teardown.
		}

		default:
			// Payout done. Tear down, feed a ball from the lava sink and go
			// straight into Lava-Ball -- the finale is the only mode that
			// chains into another state instead of returning to idle. (The
			// dispatcher still forces state 2 after the 0x52, so the
			// EnterLavaBall that follows is what actually wins.)
			Handler(ft::ModeTeardown, nullptr);
			EjectLavaSink(0.0f);
			EnterLavaBall();
			return;
		}

	case ft::ModeTeardown:
		// The fattest teardown in the table: on top of the shared tail it
		// starts a fresh collection cycle.
		ClearTreasureXLights();
		RestoreTreasureBoard();
		EndOfEveryMode();
		tag_s_trip3.Component->ActiveFlag = 1;
		tag_s_trip8.Component->ActiveFlag = 1;
		tag_left_orbit_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		tag_right_orbit_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		tag_left_orbit_combo_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_right_orbit_combo_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		// Restore v_lite568 to whatever PrepareTreasureBoard recorded in its
		// MessageField: 0 -> turn off (0x14), non-zero -> turn on (0x13). The
		// original computes the code as `(MessageField == 0) + 0x13`, which is
		// the same two cases and is why only one call site appears.
		tag_v_lite568.Component->Message(
			tag_v_lite568.Component->MessageField == 0 ? ft::LightResetAndTurnOff
			                                           : ft::LightResetAndTurnOn,
			0.0f);
		tag_v_lite567.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		// Relight the map-piece group flashing: the set has been spent.
		tag_map_piece_light_group.Component->Message(ft::LightTurnOff, 0.0f);
		tag_map_piece_light_group.Component->Message(ft::LightFlashTimedThenStayOff, 1.0f);
		// Clear the SIX mode wheel lights -- v_lite552..557 only. v_lite558
		// (Sword Fight) and v_lite559 (Peg Leg's Loot itself) are left lit.
		//
		// UNVERIFIED whether that is deliberate. It means that after the
		// finale the random pool is six wide, not seven, so Sword Fight cannot
		// be offered again until the next finale. Settled by playing past one
		// finale and watching the wheel.
		tag_v_lite552.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite553.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite554.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite555.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite556.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite557.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		RestartCollectionCycle = true;
		return;

	case ft::ModeAbort:
		AbortMode();
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// State 0xc -- Lava-Ball
//
// A +2-ball multiball started by clearing the volcano bank outside Sword
// Fight. Not a timed mode: it ends when the ball count falls back to one, like
// Dragon's Keep's multiballs.
// ---------------------------------------------------------------------------

// 0x42d480.
void LavaBallController(MessageCode code)
{
	switch (code)
	{
	case ft::MissionComplete:
		StartOfEveryMode();  // the one caller StartOfEveryMode's LavaBall guard exists for
		tag_a_kick1.Component->Message(ft::HoldFieldEnable, 0.0f);
		tag_s_oneway12.Component->ActiveFlag = 0;  // the brig cannot release during multiball

		// Source the two extra balls from the lava sink and/or the brig. Two
		// ejects if the brig is empty, one if it holds a ball, none if it
		// holds two or more -- and the brig is then zeroed, so whatever it was
		// holding counts toward the same +2.
		if (BallsInBrig == 0)
		{
			EjectLavaSink(2.0f);
			EjectLavaSink(1.0f);
		}
		else if (BallsInBrig == 1)
		{
			EjectLavaSink(1.0f);
		}
		pb::MainTable->MultiballCount += 2;
		BallsInBrig = 0;

		ClearText(tag_info_text_box.Component);
		Play(tag_tabb32wave.Component);
		// fall through -- show the name immediately on entry
	case ft::MissionStarted:
		Display(tag_info_text_box.Component, GetRcString(STR_NAME_LAVA_BALL), Forever);
		return;

	case ft::ModeAbort:
		// A drain. Only the drain that brings the count back to one ends the
		// mode; the other two are ignored, which is the whole multiball rule.
		if (pb::MainTable->MultiballCount != 1)
			return;
		RestoreBanksAndLetters();
		ClearText(tag_info_text_box.Component);
		tag_a_kick1.Component->Message(ft::HoldFieldDisable, 0.0f);
		// Lava-Ball sets state 2 itself and re-broadcasts, instead of going
		// through a 0x52 -- which is exactly why it pays no completion bonus
		// and does not advance the wheel. The dispatcher's +750,000 guard
		// (state < 0xc) says the same thing from the other side.
		SetMode(Mode::Idle);
		ResetVolcanoLights(1);  // relight the four volcano lights, clear the mask
		Handler(ft::MissionComplete, nullptr);
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// State 0xd -- Jail Break!
//
// Releases whatever the brig is holding, 1 to 3 balls. Entered from the brig
// target's third hit outside a mode, or when the brig fills to three.
// ---------------------------------------------------------------------------

// 0x42d590.
void JailBreakController(MessageCode code)
{
	switch (code)
	{
	case ft::MissionComplete:
		if (BallsInBrig == 0)
		{
			// Nothing to break out. The mode starts and immediately cancels
			// itself, paying nothing -- so the manual's "needs at least one
			// ball in the brig" requirement is real, it just lives here rather
			// than in the brig target's handler.
			ResetBrigLights(true);
			SetMode(Mode::Idle);
			Handler(ft::MissionComplete, nullptr);
			return;
		}

		StartOfEveryMode();

		// ORIGINAL-SOURCE BUG, reproduced verbatim. The intent was plainly
		// `a_spot11->MessageField = 0xf` -- "light all four volcano lights for
		// the duration of Jail Break" -- but the write goes to v_lite551, one
		// of the four volcano *display* lights, whose MessageField is read
		// nowhere in either corpus. The two globals differ by one hex digit in
		// the original source (0x43af6c vs 0x43af1c), and the listing
		// confirms the target, so this is not a mis-decompile:
		//
		//   42d5de:  mov   eax,ds:0x43af6c          ; v_lite551
		//   42d5e3:  mov   DWORD PTR [eax+0x6],0xf  ; ->MessageField = 0xf
		//   42d5ea:  call  0x4253a0                 ; RestoreVolcanoLights()
		//
		// RestoreVolcanoLights then re-derives all four lights from a_spot11's
		// untouched mask, so the write is dead and **Jail Break silently fails
		// to light the volcano bank**. Kept because this file reproduces the
		// shipped game; a port that wants the designers' intent should change
		// the next line to `tag_a_spot11.Component->MessageField = 0xf;` and
		// say so in its own notes.
		tag_v_lite551.Component->MessageField = 0xf;
		RestoreVolcanoLights();

		pb::MainTable->MultiballCount += BallsInBrig;
		BallsInBrig = 0;

		// Turn the three brig lights on, then flash them for a second and
		// leave them off -- the brig is empty now.
		tag_v_lite530.Component->Message(ft::LightTurnOn, 0.0f);
		tag_v_lite531.Component->Message(ft::LightTurnOn, 0.0f);
		tag_v_lite532.Component->Message(ft::LightTurnOn, 0.0f);
		tag_v_lite530.Component->Message(ft::LightFlashTimedThenStayOff, 1.0f);
		tag_v_lite531.Component->Message(ft::LightFlashTimedThenStayOff, 1.0f);
		tag_v_lite532.Component->Message(ft::LightFlashTimedThenStayOff, 1.0f);

		tag_a_kick1.Component->Message(ft::HoldFieldEnable, 0.0f);
		tag_s_oneway12.Component->ActiveFlag = 0;
		AddScoreAt(nullptr, BonusJailBreak);
		ClearText(tag_info_text_box.Component);
		Play(tag_tabb95wave.Component);
		// fall through
	case ft::MissionStarted:
		Display(tag_info_text_box.Component, GetRcString(STR_NAME_JAIL_BREAK), Forever);
		return;

	case ft::ModeAbort:
		if (pb::MainTable->MultiballCount != 1)
			return;
		RestoreBanksAndLetters();
		ClearText(tag_info_text_box.Component);
		tag_a_kick1.Component->Message(ft::HoldFieldDisable, 0.0f);
		SetMode(Mode::Idle);
		// Note the order: the broadcast happens BEFORE the two board restores,
		// so the first message the new idle state sees is delivered with the
		// brig lights and volcano bank still in their Jail Break condition.
		// That is what the original does; whether it matters depends on what
		// Idle's 0x4f touches, and it touches neither.
		Handler(ft::MissionComplete, nullptr);
		ResetBrigLights(false);
		ResetVolcanoLights(1);
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// State 0x11 -- end-of-game table lockdown
//
// Not a bonus state: it pays nothing and displays nothing. Set by the drain
// handler when the last ball of the last player drains, which then immediately
// sets state 0x10 so the bonus still runs -- so 0x11 is a transient that
// exists only to make the table safe during the game-over sequence.
//
// It also closes the ball-accounting loop: every ball the brig ever swallowed
// is returned to the count by exactly one of three paths -- the normal
// release, Lava-Ball / Jail Break, or here.
// ---------------------------------------------------------------------------

// 0x42d730.
void LockdownController(MessageCode code)
{
	if (code == ft::MissionComplete)
	{
		LockdownFlagUnknown = 0;
		tag_a_kick2.Component->ActiveFlag = 0;
		tag_s_bwal610.Component->ActiveFlag = 0;
		// The kickback stays ARMED: a ball lost down the outlane during the
		// game-over sequence would hang the sequence waiting for it.
		tag_a_kick1.Component->Message(ft::HoldFieldEnable, 0.0f);
		tag_s_oneway12.Component->ActiveFlag = 0;

		// Nothing may hold a ball from here on.
		tag_s_magnet1.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_s_magnet2.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_s_magnet3.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_s_magnet4.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_s_magnet5.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_s_magnet6.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_a_kout1.Component->Message(ft::HoldFieldDisable, 0.0f);
		tag_a_kout2.Component->Message(ft::HoldFieldDisable, 0.0f);

		if (BallsInBrig > 0)
		{
			pb::MainTable->MultiballCount += BallsInBrig;
			BallsInBrig = 0;
		}
		return;
	}

	if (code == ft::ModeAbort && pb::MainTable->MultiballCount == 1)
		tag_a_kick1.Component->Message(ft::HoldFieldDisable, 0.0f);
}

// ---------------------------------------------------------------------------
// The dispatcher
// ---------------------------------------------------------------------------

// 0x42d900 -- ModeControl. A flat switch on the mode register, preceded by the
// two mode-lifecycle actions that are NOT in any controller.
//
// Those two arms are where the table's progression actually lives, and reading
// them is what settles the open question about mode completion:
//
//   * `0x4f` sets the running mode's wheel light FLASHING -- "selected or in
//     progress".
//   * `0x52` pays +750,000 to the jackpot (for states 4..0xb only, so not for
//     Lava-Ball or Jail Break), sets the wheel light SOLID -- "completed",
//     which is what makes SelectNextMode stop offering it -- and selects the
//     next mode.
//
// **Mode completion is never checked.** The controllers' shared `0x52` already
// increments `v_lite606->MessageField` (modes completed), and `0x52` is reached
// on timer expiry and on drain-abort exactly as on success. Add the two actions
// here and a mode counts as "completed" -- for the 200,000/mode drain bonus,
// the 1,000,000/mode finale payout, the +750,000 jackpot AND the wheel
// progression -- merely by having been *started*. No success/failure
// discriminator exists anywhere in the dispatcher or in any of the eight
// controllers. There is deliberately no win check in this file.
//
// Whether that is the intended design is not answerable from the binary.
void ModeControl(Mode mode, MessageCode code, TPinballComponent* caller)
{
	int state = static_cast<int>(mode);

	if (code == ft::MissionComplete)
	{
		SetModeWheelLight(mode, /*flashing=*/true);
	}
	else if (code == ft::ModeTeardown)
	{
		if (state > 3 && state < 0xc)
			AddToJackpot(ModeCompletionJackpot);
		SetModeWheelLight(mode, /*flashing=*/false);
		SelectNextMode();
	}

	switch (mode)
	{
	case Mode::Inert:
		// A bare return. Set by the new-game branch, and it swallows every
		// message until something writes the register again.
		return;
	case Mode::GameOver:     GameOverController(code); return;
	case Mode::Idle:         IdleController(code); return;
	case Mode::SkillShot:    SkillShotController(code, caller); return;
	case Mode::AttackShip:   AttackShipController(code, caller); return;
	case Mode::Bermuda:      BermudaController(code, caller); return;
	case Mode::Storm:        StormController(code, caller); return;
	case Mode::Tavern:       TavernController(code, caller); return;
	case Mode::TreasureHunt: TreasureHuntController(code, caller); return;
	case Mode::Mutiny:       MutinyController(code, caller); return;
	case Mode::SwordFight:   SwordFightController(code, caller); return;
	case Mode::PegLegsLoot:  PegLegsLootController(code, caller); return;
	case Mode::LavaBall:     LavaBallController(code); return;
	case Mode::JailBreak:    JailBreakController(code); return;
	case Mode::DrainBonus:   DrainBonusController(code, caller); return;
	case Mode::Lockdown:     LockdownController(code); return;
	default:
		// 0x42d830 -- the analogue of upstream's UnselectMissionController and
		// of Dragon's 0x42c000: force the register to 2 and re-broadcast.
		// States 0xe and 0xf land here, as does any corruption.
		SetMode(Mode::Idle);
		Handler(ft::MissionComplete, nullptr);
		return;
	}
}

// 0x42d850 -- control::handler, the entry point every component handler
// broadcasts through. Three things happen before the dispatch and one after.
void Handler(MessageCode code, TPinballComponent* caller)
{
	for (;;)
	{
		auto* box = tag_info_text_box.Component;
		if (!box)
			return;  // called before the table is built

		// 1. The global jackpot accrual. This runs on EVERY broadcast with a
		//    non-null caller, before any dispatch and regardless of mode -- so
		//    a drop target pays its +20,000 even in a mode that suppresses its
		//    score, and the same collision can also pay a mode-local accrual
		//    on top. Matches the manual's "+20,000 per target, +40,000 per
		//    sink" exactly.
		if (caller)
		{
			int toJackpot = 0;
			switch (caller->GetComponentType())
			{
			case type::PopupTarget:
				// The drain bonus's "Target Bonus" term really counts
				// collisions with the eleven drop targets, not drops.
				TargetsDroppedThisBall++;
				toJackpot = 20000;
				break;
			case type::SoloTarget:
				toJackpot = 20000;
				break;
			case type::Sink:
				toJackpot = 40000;
				break;
			default:
				break;
			}
			if (toJackpot)
				AddToJackpot(toJackpot);
		}

		// 2. "The text box's display has run out" is re-spelled as a mode
		//    tick, which is how every mode's countdown keeps refreshing
		//    itself without a second timer. Verbatim Dragon's Keep.
		if (code == ft::TimerExpired && caller == box && !AnnouncementPending(box))
			code = ft::MissionStarted;

		// 3. Dispatch.
		ModeControl(CurrentMode(), code, caller);

		if (code != ft::ModeTeardown)
			return;

		// 4. A teardown always lands back in idle, here rather than in each
		//    controller -- which is why no controller in this file sets state
		//    2 itself and why Skulduggery has no restart window.
		//
		//    Note the consequence, which is visible only when this loop and
		//    ModeControl are read together: the 0x52 arm above already called
		//    SelectNextMode, and the 0x4f this loop re-dispatches lands in
		//    state 2, whose SetModeWheelLight arm calls SelectNextMode AGAIN.
		//    So every teardown draws two random modes and discards the first.
		//    Harmless, but it is why a mode's "next mode" display can be seen
		//    to flicker, and any port that caches the draw must preserve it.
		caller = nullptr;
		code = ft::MissionComplete;
		SetMode(Mode::Idle);
	}
}

}  // namespace pirates
