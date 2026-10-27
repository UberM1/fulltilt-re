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
//   0x39    FT-only, see FtMessageCode::TBlockerSetPosition the four a_bloc* blockers
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
// Settled from PIRATES.DAT. `lane_winds_light_group` is group 531, and its member list
// (iattribute 1027, the array TLightGroup's constructor walks) is
// [401, 402, 403, 404, 405] = v_lite533, v_lite534, v_lite535, v_lite536, v_lite537 --
// the five W-I-N-D-S letters, the same five pirates_part1.cpp drives individually. Not
// the wind-lane indicator pairs, which live in their own groups (right_wind,
// south_wind, west_wind, east_wind_1, east_wind_2, three members each).
//
// The five are the five rollover lanes, two left and three right: v_lite533 and
// v_lite534 sit at screen x=216,232 over a_roll5/a_roll4, and v_lite535..537 at
// x=396,412,427 over a_roll1/a_roll2/a_roll3. The two island groups are
// island_lane_top (groups 406-408 = v_lite538..540) and island_lane_bottom
// (409-411 = v_lite541..543), both pinned by name as before.
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
// 0x39 -- SETTLED, and it is not a hold time. Full Tilt's TBlocker::Message is at
// CADET.EXE 0x00414f80 (byte-identical at the same address in PIRATES.EXE, same build),
// and it dispatches codes 0x37..0x3c through a jump table at 0x00415398 plus 0x45,
// 0x3f3 (SetTiltLock), 0x3fe (PlayerChanged) and 0x402 (Reset). The 0x39 arm is
// 0x004150ce and it does this:
//
//     edi = (int)floor(value)                  -- 0x42afff then 0x42afd8, the floor/ftol
//     clamp the *old* index against ListBitmap->size()   (0x00415130..0x0041515e)
//     clear the per-position byte at [this + old + 0x76]
//     [this + 0x7e] = edi                      -- the current position index
//     if (edi >= 0)  set [this + edi + 0x76] = 1 and show sprite edi
//     else           SpriteSet(none)           -- 0x004151eb, via 0x41cfa0
//
// So the float is a POSITION INDEX, not a duration: a Full Tilt TBlocker is a
// multi-position moving wall, one visual state and one collision-flag byte per position,
// and -1 means "no position" -- retracted, nothing active, sprite off. The rest of the
// family confirms the reading: 0x3b (0x00415245) sends itself 0x39 with
// ListBitmap->size()-1 and plays the enable sound [+0xae]; 0x3c (0x00415284) sends itself
// 0x39 with -1.0f and plays the disable sound [+0xb2]; 0x3a animates toward a position.
//
// The numbering that looked wrong is fine once the real pair is known. CADET.EXE's own
// DrainBallBlockerControl (0x00423480) sends 0x38 where upstream's DrainBallBlockerControl
// sends TBlockerEnable (52, i.e. +4) and 0x45 where upstream sends TBlockerRestartTimeout
// (59) -- so 0x37/0x38 are Disable/Enable in the +4 band, RestartTimeout is 0x45, and
// 0x39-0x3c are four codes Full Tilt added below them.
//
// Consequence for the port, flagged because it is not this file's to fix: the engine's
// TBlocker (reference/SpaceCadetPinball/TBlocker.cpp) has no case for 0x39, and its
// `float UnknownP7e` is in fact this int position index -- which is also why Dragon's
// gargoyle rocks compare the same offset against -1 as an int. pirates_part1.cpp used to
// send upstream's TBlockerRestartTimeout (59) here, which is a different code; it now
// sends this one, so all three slices agree on what the binary sends.
constexpr auto TBlockerSetPosition = static_cast<MessageCode>(0x39);

// 0x4c -- SETTLED. The three classes that answer it all mean "disarm", but they are three
// different classes with three different mechanisms, read out of CADET.EXE:
//
//   TMagnet   (type 1042, Message 0x004166c0): 0x4b sets ActiveFlag ([this+0x5]) to 1,
//             0x4c clears it and kills the hold timer. The magnet's capture circle is
//             installed with &ActiveFlag (0x00416873: `lea ecx,[esi+0x5]`), and
//             TEdgeManager skips edges whose ActiveFlag is clear -- so a disarmed magnet
//             cannot catch a ball at all.
//   TKickout  (type 1012, Message 0x004163a0): same ActiveFlag pair, and 0x4c additionally
//             releases a ball it is already holding (0x00416443..0x00416473).
//   TKickback (type 1014, Message 0x00415d70, which is what a_kick1 and a_kick2 are):
//             0x4c sets a *suppression* dword [this+0x86] to 1 and 0x4b clears it. In
//             TKickback::Collision (0x00415ba0) that dword only short-circuits the arming
//             of the kick timer (0x00415bd7); DefaultCollision still runs at 0x00415c05.
//
// So the answer differs by device: a disarmed KICKBACK still catches and bounces the ball,
// it just does not kick it. A disarmed magnet or kickout is out of the world entirely.
// a_kick1 is type 1014 in PIRATES.DAT, so the brig release below leaves the ball path
// solid and only cancels the kick.
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
// Settled: StepBackward does read as "to the right" on this playfield. 0x1c is 28, which
// is 24 at +4, and 24 is TLightGroupStepBackward, whose body (TLightGroup.cpp) gives each
// light the state of its *predecessor* -- so the lit pattern walks toward higher indices.
// All three groups are listed in PIRATES.DAT in left-to-right screen order:
// lane_winds 401..405 at x=216,232,396,412,427; island_lane_top 406..408 at
// x=312,328,344; island_lane_bottom 409..411 at x=312,328,345. Higher index = further
// right, so one 0x1c step moves every lit lane light one lane rightward, and since both
// buttons send 0x1c the player cannot walk one back.
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
// difference: a_blocg is retracted (-1.0) where a bank completion drives it to position 0
// (0.0). Reading those floats as positions rather than durations (see
// FtMessageCode::TBlockerSetPosition), the kick leaves the ball path in the same
// configuration a completed drop bank does -- a_blocc and a_blocf at position 0, a_bloce
// at position 1, v_lite504 lit as the live path -- but with a_blocg taken out of the world
// entirely, clearing the way for the ball the kicker has just thrown.
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
// Half settled, from PIRATES.DAT's geometry, and it kills the reading the two slices had
// settled on. Both kickbacks are on the SAME side -- the left -- not on opposite outlanes.
// Table X is positive to the left of screen (v_lite533 at screen x=216 has 501 X=+5.72,
// v_lite537 at x=427 has X=-6.62) and Y grows toward the drain, which sits at X=0
// (v_lite570 X=0.0005 Y=13.71, v_lite569 X=1e-6 Y=15.34). Attribute 600 gives:
//
//   a_kick2    X 5.23..6.25   Y 12.98        screen (209,380)
//   s_bwal610  X 5.21..6.54   Y 11.70..13.67 (no sprite; the wall behind a_kick2)
//   v_lite544  X 5.70         Y 12.74        screen (211,362)
//   a_kick1    X 6.92..7.94   Y 14.02        screen (173,392)
//   a_flip1    screen x 266..306             a_flip2 screen x 332..373
//
// So a_kick2 is the inner of the two, with s_bwal610 overlapping it and v_lite544 sitting
// directly on it -- which independently confirms this slice's a_kick2 + s_bwal610 +
// v_lite544 triple -- and a_kick1 is further out and lower. Nothing is on the right (-X)
// side at that height.
//
// UNVERIFIED, and now narrower: which of the two left-side lanes the manual calls the
// outlane. a_kick1 is the outer and lower of the pair, which is where an outlane kickback
// belongs, making a_kick2 the inner return/kickback lane -- but that is a reading of two
// coordinates, not evidence. Locating the left-side wall chain (the type-1000 s_bwal*
// runs, attribute 600) between the two would settle it; nothing in the rules branches on
// the answer. pirates_part1.cpp's and pirates_part2.cpp's "left outlane" wording should
// both be corrected to "left side" by whoever owns those sentences.
void KickbackControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired) // binary 0x46
		return;

	blocC->Message(FtMessageCode::TBlockerSetPosition, 0.0f);
	blocE->Message(FtMessageCode::TBlockerSetPosition, 1.0f);
	lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	blocF->Message(FtMessageCode::TBlockerSetPosition, 0.0f);
	blocG->Message(FtMessageCode::TBlockerSetPosition, -1.0f);

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
// Settled directly out of Full Tilt's TLight. TLight::Message is CADET.EXE 0x0040a490,
// dispatching codes 0..0x1a through a jump table at 0x0040ac30, and the first five arms
// name the fields outright against upstream's TLight::Message:
//
//   code 0  TLightTurnOff          0x0040a4eb  [+0x42] = 0, then tests 0x46/0x4a/0x4e
//   code 1  TLightTurnOn           0x0040a534  [+0x42] = 1, then the same three
//   code 2  TLightGetLightOnFlag   0x0040a57d  returns [+0x42]
//   code 3  TLightGetFlasherOnFlag 0x0040a588  returns [+0x46]
//   code 4  TLightFlasherStart     0x0040a593  [+0x46] = 1, [+0x4e] = 0
//
// Upstream's TurnOff/TurnOn read `LightOnFlag = false/true; if (!FlasherOnFlag &&
// !ToggledOffFlag && !ToggledOnFlag)`, in that order, so the layout is a stride-4 run:
//
//   +0x42 LightOnFlag   +0x46 FlasherOnFlag   +0x4a ToggledOffFlag
//   +0x4e ToggledOnFlag +0x52 TurnOffAfterFlashingFg +0x56 LightOnBmpIndex
//
// i.e. exactly the three this handler reads, and the same three the drain reads on
// v_lite569 at 0x00426166/0x0042616b/0x00426170. The 32-bit-vs-bool note stands: Full
// Tilt stores all of these as dwords where upstream declares them `bool`, so the port's
// field accesses remain a layout approximation -- but the identities are no longer a
// guess.
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
