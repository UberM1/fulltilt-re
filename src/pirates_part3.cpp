// pirates_part3.cpp -- Skulduggery rules, part 3: the two flipper buttons, the kickback,
// the brig release one-way, the launch-lane one-way, the tavern window light, the Shoot
// Again lamp's phase stepper and the kickback's armed lamp.
//
// The eight handler addresses nothing else covered -- seven routines, because a_flip1 and
// a_flip2 are byte-identical -- in the shape pirates_part1.cpp and
// pirates_part2.cpp established: free functions in namespace pirates, MessageCode spelled
// out, the mode register read as info_text_box->MessageField against named Mode* constants,
// and one address comment opening each handler's doc block.
//
//   0x00422a80  v_lite547    tavern window light (and v_lite546/545)  ~ ExtraBallLight
//   0x00426080  v_lite569    the Shoot Again lamp's phase stepper      ~ ShootAgainLight
//   0x0042a1e0  a_flip1+2    both flipper buttons                     ~ LeftFlipperControl
//   0x0042a260  v_lite544    the kickback's armed lamp, and its disarm (table-specific)
//   0x0042a2a0  a_kick2      the kickback fires                       ~ LeftKickerControl
//   0x0042a340  s_oneway7    brig release                             (table-specific)
//   0x0042a3b0  s_oneway3    launch-lane exit                         ~ oneway2 in Dragon
//
// Sources: analysis/pirates-rules.md (§0 the dispatcher, §1.3 the state table, §5.5 the
// launcher, §6 the brig accounting, §7 the flipper note), decomp/pirates/functions/*.c for
// control flow and decomp/pirates/asm/*.txt for every argument value.
//
// All five listings are short enough to transcribe whole, and all five were, because
// Ghidra's stack-argument modelling fails here in the usual way: a_kick2's scoring call
// decompiles as FUN_004232c0((int *)0x39, 0) where the listing plainly loads the caller
// pointer from [esp+0x8]. No handler in this slice formats a string, so none of
// FUN_0042dfaf's invisible varargs matter.
//
// Message codes, piecewise as in the sibling slices -- TLight +0, the
// TLightGroup/TSoloTarget/TPopupTarget/TBlocker block +4, gate/kickout/sink +9, Control*
// +10, ControlCollision (0x4a) +11, mission codes +13. The ones that appear below:
//
//   binary  upstream                                 where
//   1       TLightTurnOn (1, +0)                     the flipper-button notification
//   0x10    TLightFlasherStartTimedThenStayOff (16)  v_lite544 at the kickback's fire
//   0x13    TLightResetAndTurnOn (19)                v_lite504
//   0x14    TLightResetAndTurnOff (20)               v_lite505, v_lite569 phase 1 -> 0
//   7       TLightFlasherStartTimed (7, +0)          v_lite569 phase 2 -> 1
//   9       TLightTurnOnTimed (9, +0)                v_lite569 phase 3 -> 2
//   0x1c    TLightGroupStepBackward (24, +4)         the three rotated lane groups
//   0x39    FT-only, see FtMessageCode::TBlockerHold the four a_bloc* blockers
//   0x46    ControlTimerExpired (60, +10)            a_kick2's and v_lite569's trigger
//   0x4a    ControlCollision (63, +11)               both one-ways
//   0x4c    FT-only, see FtMessageCode::TMagnetDisarm a_kick1
//
// Like the other slices this is a fragment: the component table, the score arrays and the
// per-table control interface live elsewhere, so it compiles in shape but does not link.

#include "pch.h"
#include "control.h"

#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballComponent.h"
#include "TPinballTable.h"
#include "TTextBox.h"

namespace pirates {

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
//
// Plain extern pointers, pirates_part2.cpp's form: pirates_part1.cpp is the slice that
// defines the tags, and a second definition here would collide.
//
// Where part1 already binds a component this file uses part1's variable name rather than
// the tag name, so the declaration matches a real definition: blocC/blocE/blocF/blocG
// (typed TBlocker*), lite504, lite505, lite544, island_lane_top_lights,
// island_lane_bottom_lights. pirates_part2.cpp spells four of those as a_blocc/a_bloce/
// a_blocf/a_blocg typed TPinballComponent*, so the two slices still have to be reconciled
// by whichever file ends up owning the single definition; that is part2's open note, not a
// new one.

extern TTextBox*& info_text_box; // DAT_0043ad3c -- MessageField is the mode register
extern TPinballTable*& TableG;   // DAT_0043ab9c

// The four blockers that route the ball out of the drop-target/sink area. The kickback
// drives all four with exactly the floats pirates_part1.cpp's SwapBankRoute passes, bar
// a_blocg's; see the handler.
extern TBlocker*& blocC; // DAT_0043ae0c
extern TBlocker*& blocE; // DAT_0043ae1c
extern TBlocker*& blocF; // DAT_0043ae2c
extern TBlocker*& blocG; // DAT_0043ae3c

// The route-indicator pair. v_lite504 lit means the playfield path is the live one; the
// launch-lane trip wire, every drop-bank completion and the kickback all assert it, and
// s_trip2 plus every mode start assert the other direction.
extern TLight*& lite504; // DAT_0043ae54
extern TLight*& lite505; // DAT_0043ae6c

extern TLight*& lite544; // DAT_0043bbbc -- the kickback's "armed" lamp

// The Shoot Again lamp. Declared the way pirates_part2.cpp declares it, because no slice
// binds the tag yet; part2 reads it from the launcher and the stepper at the end of this
// file is its own handler.
extern TLight*& lite569; // DAT_0043bc44

// The outlane kickback pair, part1's variable names for a_kick2 and s_bwal610. Both carry
// a plain ActiveFlag rather than a message interface: pirates_part1.cpp's two return lanes
// set it, the launcher sets it at every serve, and the lamp handler below clears it.
extern TPinballComponent*& kick2;   // DAT_0043bba4 -- a_kick2
extern TPinballComponent*& bwal610; // DAT_0043c42c -- s_bwal610

extern TPinballComponent*& a_kick1; // DAT_0043bbd4 -- the kickback armed by the multiballs

// The three light groups both flipper buttons rotate.
//
// UNVERIFIED: which lights lane_winds_light_group holds. pirates-rules.md §7 reads the
// manual's "the flippers also rotate the wind letter lights and the island lane lights"
// onto these three groups, and the two island groups are pinned by name, but the first is
// either the five W-I-N-D-S letters (v_lite533..537, which part1 drives individually) or
// the four wind-lane indicator pairs. Dumping the group's member list out of PIRATES.DAT
// would settle it; nothing in the rules branches on the answer.
extern TLightGroup*& lane_winds_lights;          // DAT_0043b7b4
extern TLightGroup*& island_lane_top_lights;     // DAT_0043babc
extern TLightGroup*& island_lane_bottom_lights;  // DAT_0043bb2c

// ---------------------------------------------------------------------------
// Cross-slice interface
// ---------------------------------------------------------------------------

// pirates_common.cpp. Returns the amount actually credited, and notifies the previously
// scoring component with 0x4d on its way -- which is the combo break, so calling it is
// never free even when the component scores nothing.
int AddScoreFor(TPinballComponent* caller, int index); // 0x004232c0

// v_lite569's own handler, the Shoot Again lamp's phase stepper. The body is at the end
// of this file now; this is only the forward declaration s_oneway3 needs, because
// s_oneway3 drives the stepper directly rather than through control::handler -- exactly as
// dragon_part2.cpp's LaunchLaneOneWayControl drives Dragon's.
void ShootAgainLightControl(MessageCode code, TPinballComponent* caller);

// DAT_00435d90 -- the Shoot Again phase, 3 = lamp solid and untimed, 2 = 10 s timed on,
// 1 = 5 s flashing, 0 = off. Defined in pirates_part2.cpp as shoot_again_phase.
//
// pirates_modes.cpp defines a second cell for the same address under the name
// ShootAgainPhase. That is a real duplicate, not two globals: Storm's west wind lane (in
// modes) and the launcher plus the drain (in part2) write the same DAT. One of the two
// definitions has to go; this file reads part2's, because part2 owns the launcher that
// sets phase 3 and so owns the only value s_oneway3 ever tests.
extern int shoot_again_phase;

// DAT_00435d3c -- balls the brig is holding, 0..3. Defined in pirates_modes.cpp, which
// owns the three paths that empty it in bulk (Lava-Ball, Jail Break, the end-of-game
// lockdown). s_oneway7 below is the only path that gives one back at a time.
extern int BallsInBrig;

// ---------------------------------------------------------------------------
// Table-local message codes
// ---------------------------------------------------------------------------
//
// Both are pirates_part2.cpp's, with its reasoning; repeated rather than shared because
// constexpr at namespace scope has internal linkage and there is no header to put them in
// yet.

namespace FtMessageCode {
// 0x39, sent only to the a_bloc* blockers and to a_cannon. The float is a hold time: 0.0
// closes the blocker now, a positive value holds it open that long, -1.0 indefinitely.
// Arithmetically 0x39 would be upstream's TGateDisable under the +4 rule, which cannot be
// what a blocker does with it, so part2 reads it as a code FT added to TBlocker.
//
// Note that pirates_part1.cpp spells the same code MessageCode::TBlockerRestartTimeout
// instead -- a behavioural name for an upstream constant whose value (59) is not 0x39.
// This file follows part2: the name is equally unverified either way, but part2's constant
// carries the value the binary actually sends.
// UNVERIFIED: the name and the float's exact meaning. FT's TBlocker::Message is engine
// code and outside this corpus.
constexpr auto TBlockerHold = static_cast<MessageCode>(0x39);

// 0x4c -- "disarm the holding field". Sent to the six s_magnet*, to a_kout1/a_kout2 and,
// here, to a_kick1.
// UNVERIFIED: whether a disarmed kickback can still catch a ball; that is decided inside
// the component class.
constexpr auto TMagnetDisarm = static_cast<MessageCode>(0x4c);
}

// ---------------------------------------------------------------------------
// The mode register
// ---------------------------------------------------------------------------
//
// info_text_box->MessageField; only the three states this slice tests are named.

constexpr int ModeLavaBall = 0xc;
constexpr int ModeJailBreak = 0xd;
constexpr int ModeTableLockdown = 0x11;

// ===========================================================================
// Lights
// ===========================================================================

// 0x00422a80 -- v_lite547, and v_lite546 and v_lite545 with it (skeleton: v_lite547Control)
//
// Three bytes: `ret 0x8`, followed by alignment padding up to the next function. A real
// stub in the shipped binary, not a decompiler failure, and deliberately so rather than
// vestigially: the three tavern window lights *do* have behaviour, and all of it is in the
// Tavern controller (FUN_0042b420). That controller answers a window light's expired flash
// by re-flashing a randomly chosen one of the three for 0.1-0.25 s, a self-sustaining
// chain it primes itself at mode start. Putting that in the lights' own handler would mean
// three copies of it plus a mode test in each; leaving the handler empty puts the whole
// flutter in the one place that owns the mode.
//
// v_lite547 is also the 3-bit tavern-window mask (v_lite547->MessageField), read by the
// three tavern sinks in pirates_part2.cpp. Nothing reads it from here.
//
// The generated skeleton binds this one function to all three lights, which is the
// binary's own arrangement -- three component table entries, one handler address.
void TavernWindowLightControl(MessageCode code, TPinballComponent* caller)
{
}

// ===========================================================================
// The flipper buttons
// ===========================================================================

// 0x0042a1e0 -- a_flip1, and FUN_0042a220 (a_flip2) byte for byte (skeleton: a_flip1Control)
//
// Both buttons rotate the three lane light groups one step. The two listings are
// identical -- same three globals, same code, same float -- so this is one routine bound
// twice, which also settles the manual's claim that the upper-playfield and ship flippers
// are mirrored onto the two buttons: there is nothing per-button to mirror. a_flip3 and
// a_flip4 have no rules handler at all.
//
// Upstream splits this: LeftFlipperControl steps its two groups backward and
// RightFlipperControl steps them forward. Skulduggery sends 0x1c from both, so both
// buttons rotate the same way and the player cannot walk a light back.
//
// The notification code is 1, which upstream's two flipper handlers also test as
// MessageCode::TLightTurnOn -- the value a TFlipper sends its control function when the
// button goes down. Spelled with upstream's name here for the same reason upstream does.
//
// UNVERIFIED: that TLightGroupStepBackward is the direction the manual calls "to the
// right". 0x1c is 28, which is 24 at +4, and 24 is StepBackward; whether that reads as
// rightward on the playfield depends on the groups' member order in PIRATES.DAT. The code
// sent is what matters and it is pinned.
void FlipperLaneRotateControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::TLightTurnOn) // binary 1 -- flipper button pressed
		return;

	lane_winds_lights->Message(MessageCode::TLightGroupStepBackward, 0.0f);
	island_lane_top_lights->Message(MessageCode::TLightGroupStepBackward, 0.0f);
	island_lane_bottom_lights->Message(MessageCode::TLightGroupStepBackward, 0.0f);
}

// ===========================================================================
// The kickback
// ===========================================================================

// 0x0042a2a0 -- a_kick2 (skeleton: a_kick2Control)
//
// a_kick2 fires. The trigger is ControlTimerExpired rather than a collision because the
// kicker holds the ball for its timer and the kick *is* the expiry; the catch itself is
// handled inside the component.
//
// The six blocker and indicator messages are pirates_part1.cpp's SwapBankRoute with one
// difference: a_blocg is held open indefinitely (-1.0) where a bank completion closes it
// (0.0). So the kick leaves the ball path in the same configuration a completed drop bank
// does -- a_blocc and a_blocf shut, a_bloce held a second, v_lite504 lit as the live path
// -- but with a_blocg left open for the ball the kicker has just thrown.
//
// v_lite544 is a_kick2's armed lamp, and the direction of its message is the tell:
// pirates_part1.cpp's two return lanes arm the kicker with
// TLightFlasherStartTimedThenStayOn, and firing it flashes the same lamp
// ...ThenStayOff. One flat 5,000 per kick, from the shared one-entry score array.
//
// The scoring call is the one place Ghidra is wrong about this handler: it renders
// FUN_004232c0((int *)0x39, 0), having lost the caller pointer the listing loads from
// [esp+0x8]. It is AddScoreFor(caller, 0), and as everywhere else that also makes a_kick2
// the active scoring component, so a kickback breaks whatever combo ladder was running.
//
// UNVERIFIED: which outlane this is. pirates_part1.cpp calls the a_kick2 + s_bwal610 pair
// the *left* outlane kickback, armed by the return lanes; pirates_part2.cpp gives that
// description to a_kick1, armed by the two multiballs. Both cannot be the left outlane.
// The arming evidence is solid on both sides, so the likeliest reading is two kickbacks on
// opposite outlanes -- but the handler bodies do not say which is which, and only the
// table geometry in PIRATES.DAT would.
void KickbackControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired) // binary 0x46
		return;

	blocC->Message(FtMessageCode::TBlockerHold, 0.0f);
	blocE->Message(FtMessageCode::TBlockerHold, 1.0f);
	lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	blocF->Message(FtMessageCode::TBlockerHold, 0.0f);
	blocG->Message(FtMessageCode::TBlockerHold, -1.0f);

	lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	AddScoreFor(caller, 0); // 5000
}

// ===========================================================================
// The two one-way gates
// ===========================================================================

// 0x0042a340 -- s_oneway7 (skeleton: s_oneway7Control)
//
// The brig release, and the other half of the table's ball accounting: s_hole19 takes a
// ball out of play and counts it here, and a ball passing this gate hands exactly one
// back. pirates_part2.cpp's BrigHoleControl and this handler are the matched pair, and
// they carry the matched guard -- Lava-Ball, Jail Break and the end-of-game lockdown are
// excluded from both, because those three empty the brig in bulk and adjust the ball count
// themselves. Letting the gate decrement during one of them would double-count.
//
// Two asymmetries in the original, both reproduced:
//
// First, the kickback is disarmed whenever the brig ends up empty, including when it was
// already empty before this ball came through. A ball rolling the release gate with
// nothing in the brig therefore still disarms a_kick1 -- which matters, because the two
// multiballs arm it and this gate is inside the path they release through.
//
// Second, the clamp to 0 is unreachable: the decrement is already guarded by `> 0`, so the
// counter cannot go negative on this path. It is there against the bulk writers in
// pirates_modes.cpp, which set the cell directly.
void BrigReleaseOneWayControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision) // binary 0x4a
		return;

	const int mode = info_text_box->MessageField;
	const bool modeOwnsTheBalls = (mode >= ModeLavaBall && mode <= ModeJailBreak)
		|| mode == ModeTableLockdown;
	if (modeOwnsTheBalls)
		return;

	if (BallsInBrig > 0)
	{
		BallsInBrig--;
		TableG->MultiballCount++;
	}

	if (BallsInBrig == 0)
		a_kick1->Message(FtMessageCode::TMagnetDisarm, 0.0f);

	if (BallsInBrig < 0)
		BallsInBrig = 0;
}

// 0x0042a3b0 -- s_oneway3 (skeleton: s_oneway3Control)
//
// The ball leaving the launch lane is what takes the Shoot Again lamp off its untimed
// hold. The original really does call v_lite569's own control function directly rather
// than routing a message through control::handler, so the lamp steps 3 -> 2 and starts its
// 10 s even though no timer fired; dragon_part2.cpp's LaunchLaneOneWayControl is the same
// idiom at the same point in Dragon, down to the phase number tested.
//
// Guarding on phase 3 makes the step idempotent: only the serve sets 3, so a ball that
// re-enters the launch lane later cannot restart the window.
//
// Note what this handler is *not*. pirates-rules.md §5.3 attributes the skill shot's
// "first s_oneway3 collision locks the award in" to s_oneway3, and that lock-in is not
// here -- it is in the skill-shot controller (FUN_0042abf0), which tests
// `caller == s_oneway3` on its collision arm, as pirates_modes.cpp has it. The gate is the
// trigger for both, but it only owns the Shoot Again half. The analysis is right about the
// mechanic and wrong about which function implements it.
void LaunchLaneOneWayControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision && shoot_again_phase == 3)
		ShootAgainLightControl(MessageCode::ControlTimerExpired, nullptr);
}

// ===========================================================================
// The Shoot Again lamp
// ===========================================================================

// 0x00426080 -- v_lite569 (skeleton: v_lite569Control)
//
// The Shoot Again lamp's phase stepper, and the only writer that walks the phase down.
// pirates-rules.md §5.5 puts the 3 -> 0 walk in the launcher; it is here. The launcher
// only ever *asserts* a phase (3 at serve) or re-asserts the lamp state for the phase it
// already holds, and the drain clears the cell outright.
//
// One step per call, and each step repaints the lamp for the phase it steps *into* --
// which is why the three messages are the same three s_launcher1's PlungerFeedBall arm
// sends, with the same two floats:
//
//   3 -> 2   TLightTurnOnTimed 10 s        solid for ten more seconds
//   2 -> 1   TLightFlasherStartTimed 5 s   flashing for five
//   1 -> 0   TLightResetAndTurnOff         out, Shoot Again spent
//
// Phase 3 itself is the untimed hold the serve installs: nothing in the handler produces
// it, and s_oneway3 leaving the launch lane is what knocks it down to 2 and starts the
// clock (see LaunchLaneOneWayControl above). After that the lamp's own timeout drives the
// rest, because TLight::TimerExpired notifies its control handler -- so 2 -> 1 and 1 -> 0
// happen 10 s and 5 s later with nothing else involved.
//
// Two differences from the structural reference, upstream's ShootAgainLightControl:
//
// First, upstream keeps its state in `caller->MessageField` and has only two phases
// (lit -> flashing-then-off). Skulduggery keeps three phases in a global, which is what
// lets the launcher and the drain read and write it; and because the lamp is reached
// through the v_lite569 pointer rather than through `caller`, `caller` is unused here.
// That is not cosmetic -- it is what makes the direct call in LaunchLaneOneWayControl
// legal with a null caller, and the original does exactly that.
//
// Second, note what the stepper does *not* do: it does not clamp. The launcher clamps
// anything above 3 down to 3 before it reads the cell; the stepper just tests 1, 2 and 3
// and returns having done nothing for any other value, phase 0 included. So a phase the
// west-wind lane in pirates_modes.cpp pushed past 3 sits frozen until the next feed
// clamps it.
//
// The listing is a plain cmp/je chain on 1, 2, 3 -- there is no jump table in this
// function. The table at 0x00426068 that the phase/lamp-state mapping was read from is
// s_launcher1's switch, immediately before this body in .text; it belongs to 0x00425e70.
void ShootAgainLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired) // binary 0x46
		return;

	if (shoot_again_phase == 1)
	{
		shoot_again_phase = 0;
		lite569->Message(MessageCode::TLightResetAndTurnOff, 0.0); // binary 0x14
	}
	else if (shoot_again_phase == 2)
	{
		shoot_again_phase = 1;
		lite569->Message(MessageCode::TLightFlasherStartTimed, 5.0f); // binary 7, 0x40a00000
	}
	else if (shoot_again_phase == 3)
	{
		shoot_again_phase = 2;
		lite569->Message(MessageCode::TLightTurnOnTimed, 10.0f); // binary 9, 0x41200000
	}
}

// ===========================================================================
// The kickback's armed lamp
// ===========================================================================

// 0x0042a260 -- v_lite544 (skeleton: v_lite544Control)
//
// v_lite544 is the outlane kickback's armed indicator, and this handler is how the
// kickback disarms itself: the lamp owns the arm state, and a_kick2 plus s_bwal610 just
// follow it.
//
// Nothing here looks at the kickback at all. On the lamp's timeout the handler reads the
// lamp's own three on-flags, and if all three are clear -- the lamp ended the timeout dark
// -- it drops the ActiveFlag on a_kick2 and s_bwal610, the same two bytes
// pirates_part1.cpp's return lanes and s_launcher1's serve set to 1. If any flag is still
// set the lamp is still showing something and the pair stays live.
//
// That guard is what separates the two ways the lamp is driven, because
// TLight::TimerExpired clears ToggledOnFlag and FlasherOnFlag before it notifies the
// control handler, leaving LightOnFlag as the carrier:
//
//   armed  TLightFlasherStartTimedThenStayOn 1 s   (both return lanes, and every serve)
//          flashes a second, then stays lit -- LightOnFlag set, so the pair survives.
//   fired  TLightFlasherStartTimedThenStayOff 1 s  (KickbackControl above)
//          flashes a second, then TLight's TurnOffAfterFlashingFg path sends
//          TLightResetAndTurnOff -- all three flags clear, so the pair goes inactive.
//
// So the kickback is a one-shot that rearms from the return lanes, and the single-second
// flash doubles as both the "armed" and the "just fired" animation; only the lamp's
// resting state afterwards differs. It also means a serve always rearms it, which is
// consistent with s_launcher1 setting both ActiveFlags itself rather than relying on this.
//
// UNVERIFIED: the field identities behind the three offsets the listing reads --
// [+0x42], [+0x4e], [+0x46]. They are taken from s_launcher1, whose PlungerFeedBall arm
// guards TLightResetAndTurnOn with [+0x42], TLightTurnOnTimed with [+0x4e] and
// TLightFlasherStartTimed with [+0x46], which pins them to LightOnFlag, ToggledOnFlag and
// FlasherOnFlag respectively -- pirates_part2.cpp already reads them that way. Dumping
// Full Tilt's TLight layout would confirm it directly; note the binary reads all three as
// 32-bit where upstream declares bools, so the port's field accesses are a layout
// approximation either way.
void KickbackArmedLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired) // binary 0x46
		return;

	if (lite544->LightOnFlag)     // [+0x42]
		return;
	if (lite544->ToggledOnFlag)   // [+0x4e]
		return;
	if (lite544->FlasherOnFlag)   // [+0x46]
		return;

	kick2->ActiveFlag = 0;
	bwal610->ActiveFlag = 0;
}

} // namespace pirates
