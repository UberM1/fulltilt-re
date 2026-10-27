// dragon_part2.cpp -- Dragon's Keep rules, part 2: lights, lanes, rollovers, trip wires,
// ramps and gates.
//
// Reimplemented from DRAGON.EXE (win95 build) via decomp/dragon/functions/*.c, in the
// shape of SpaceCadetPinball's control.cpp. Bodies here replace the generated stubs in
// src/control_dragon.cpp; the address above each definition is the routine it
// reimplements, and analysis/link_impls.py derives the table rewiring from exactly those
// comments -- so there is one address comment per definition and no other handler
// addresses appear in the comment block above a definition. Cross-references to other
// routines live inside the bodies for that reason.
//
// Everything is in `namespace dragon`, like dragon_part1.cpp and dragon_missions.cpp:
// upstream can qualify these as `control::` because `control` is a class there and these
// are its static members, but a port carrying three tables needs a namespace per table,
// and the generated component table names them `dragon::XControl`.
//
// Message codes: Full Tilt's MessageCode numbering is Space Cadet's with a piecewise
// offset -- TLight codes identical, the TLightGroup/target/blocker block +4, the
// gate/kickout/sink block +9, the Control*/timer block +10, and +11 from ControlCollision
// on. Codes with no upstream counterpart at all are named in FtMessageCode below.
//
// Nothing here links yet: the per-table control interface (a control class per table, its
// own component tag table and string ids) does not exist in the port.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace dragon {

// ---------------------------------------------------------------------------
// Table-local message codes and string ids
// ---------------------------------------------------------------------------

// FT-only codes, continuing dragon_part1.cpp's namespace. dragon_missions.cpp spells the
// same three values ft::RampExited / ft::RampComplete / ft::JawOpened; they need to be
// reconciled into one header, and this file's use of 0x3d is the argument for the generic
// name: the two gargoyle rocks receive it as well as the dragon's jaw, so it is a
// moving-part notification and not a jaw event.
namespace FtMessageCode {
// Raised by the moving-part class when it has reached its target frame -- not a step,
// and not direction-specific. Settled from the engine, which both DRAGON.EXE and
// CADET.EXE carry at the same addresses: 0x414f80 is the class's Message override and
// 0x414f00 its step callback, which reads
//
//     [this+0x9a] = 0;                      ; the step timer
//     if ([this+0x7e] == [this+0x92])       ; current frame == target frame
//         ComponentHandler(0x3d, this);
//
// so it is delivered to the part's own ControlFunc, like 0x4d, and never to a
// component's Message(). Received by a_rocka, a_rockb and (in dragon_missions.cpp)
// a_jaw -- the three multi-frame parts on the table.
constexpr auto BlockerMotionDone = static_cast<MessageCode>(0x3d);
// A ramp component reports the ball leaving it, and completing a full traversal. Dragon
// uses the second as a scoring trigger so a ramp pays once per pass, not per collision.
constexpr auto RampExited = static_cast<MessageCode>(0x55);
constexpr auto RampTraversed = static_cast<MessageCode>(0x56);
}

// Mode register values of info_text_box->MessageField; see analysis/dragon-quest-system.md.
// The first five names are dragon_part1.cpp's; the rest extend the same set.
constexpr int ModeNormal = 2;
constexpr int ModeRestartWindow = 3; // the 10 s window after a quest ends
constexpr int ModeDragonHoard = 4;
constexpr int ModeRescueMaiden = 6;
constexpr int ModeSlayDragon = 9;
constexpr int ModeTowerBarrage = 13; // 0xd, the three-ball tower multiball

// Raw DRAGON.EXE resource ids, passed through as dragon_part1.cpp does.
inline Msg DragonMsg(int rcId) { return static_cast<Msg>(rcId); }

constexpr int Str_JackpotGrows = 3006;  // 0xbbe
constexpr int Str_FireRidge = 3007;     // 0xbbf, "Fire Ridge\n%ld"
constexpr int Str_WindowLane = 3008;    // 0xbc0
constexpr int Str_SuperJackpotUp = 3076; // 0xc04, "Super Jackpot Increased\n%ld"

// ---------------------------------------------------------------------------
// Per-table interface this file depends on but does not define
// ---------------------------------------------------------------------------

int AddScoreFor(TPinballComponent* caller, int index);  // 0x4230a0, indexes Scores[]; returns the credit
int AddScoreAt(TPinballComponent* caller, int score);   // 0x4230f0, literal, multiplied; returns the credit
void AddToJackpot(int amount);                          // 0x4238a0, clamp 10,000,000
void AddToSuperJackpot(int amount);                     // 0x423870, clamp 20,000,000
void AwardDragonEggs(int count);                        // 0x4232b0
void AwardExtraBall(float displayTime);                 // 0x4231e0
void CastRandomSpell();                                 // 0x4237f0
bool CheckDragonFlurry();                               // 0x4244e0, true when it started it

// The mode-6 arm of this file's 0x427880 handler, extracted into dragon_missions.cpp.
// See the CONFLICT note at its call site in PrisonWindowTripControl.
void RescueMaidenAwardWindow(TPinballComponent* caller);

// 0x42c76f is plain variadic sprintf; dragon_common.cpp carries the evidence. Every
// call site in this file formats one `long`, and in all of them it is the score
// AddScoreFor / AddScoreAt just returned -- pushed before the buffer and the format, in
// the ordinary cdecl vararg order, which is what Ghidra was rendering as an unrelated
// earlier expression.
void FormatWithValue(char* out, size_t size, const char* fmt, int value);

// The moving part's current frame index, with -1 for "retracted, nothing drawn". From
// the engine at 0x414f80: BlockerDisable (0x37) aims +0x92 at -1, BlockerEnable (0x38)
// aims it at the last frame of the sprite list at +0x3a, MovingPartSetPosition (0x3a)
// aims it at floor(value), and the step callback raises MotionDone once +0x7e has
// arrived. The listing really is an integer compare -- `cmp DWORD PTR [eax+0x7e],
// 0xffffffff` at 0x42381b -- and there is no conflict with dragon_part3.cpp's float
// read, which is of a_sink9 and a different class entirely.
//
// Note the port's TBlocker is upstream's two-state one and never raises MotionDone, so
// the two rock handlers below are inert until the engine grows the frame machinery.
int BlockerUnknownP7e(TPinballComponent* blocker);

// ---------------------------------------------------------------------------
// Components
//
// Tags declared `extern` are defined by dragon_part1.cpp or dragon_missions.cpp (which
// overlap each other already); the rest are defined here because no slice declares them.
// The short aliases are `static` so they cannot collide with part1's identically named
// externally linked references before the shared header exists.
// ---------------------------------------------------------------------------

extern component_tag<TTextBox> tag_info_text_box;
extern component_tag<TLight> tag_v_lite24;  // D-R-A-G-O-N letter, dragon ramp
extern component_tag<TLight> tag_v_lite29;  // Dragon Blood spell
extern component_tag<TLight> tag_v_lite30;  // Dragon Blood's second display light
extern component_tag<TLight> tag_v_lite32;  // Stealth spell
extern component_tag<TLight> tag_v_lite38;  // armor: Helm of Terror (center post)
extern component_tag<TLight> tag_v_lite39;  // armor: Chain mail (right outlane blocker)
extern component_tag<TLight> tag_v_lite47;  // diverter-A routing indicator
extern component_tag<TLight> tag_v_lite48;  // prison tower lock bookkeeping
extern component_tag<TLight> tag_v_lite49;  // watch tower lock bookkeeping
extern component_tag<TLight> tag_v_lite50;  // gate tower lock bookkeeping
extern component_tag<TLight> tag_v_lite55;  // right return-lane ball-save light
extern component_tag<TLight> tag_v_lite56;  // left return-lane ball-save light
extern component_tag<TLight> tag_v_lite119; // dragon-ramp flasher
extern component_tag<TLight> tag_v_lite125; // dragon-chute route indicator
extern component_tag<TLight> tag_v_lite126; // dragon-ramp route indicator
extern component_tag<TLightGroup> tag_outlane_kicker_light_group;
extern component_tag<TLightGroup> tag_prison_window_light_group;
extern component_tag<TLightGroup> tag_watch_window_light_group;
extern component_tag<TLightGroup> tag_gate_window_light_group;
extern component_tag<TPinballComponent> tag_mode_timer;
extern component_tag<TPinballComponent> tag_a_kick1;  // left-outlane kicker (ball save)
extern component_tag<TPinballComponent> tag_a_kout3;  // Dragon Blood holding kickout
extern component_tag<TPinballComponent> tag_a_spot10; // gargoyle target behind rock A
extern component_tag<TPinballComponent> tag_a_spot14; // gargoyle target behind rock B
extern component_tag<TPinballComponent> tag_a_dvtraa;
extern component_tag<TPinballComponent> tag_a_dvtrab;
extern component_tag<TPinballComponent> tag_a_dvtrba;
extern component_tag<TPinballComponent> tag_a_dvtrbb;
extern component_tag<TPinballComponent> tag_a_dvtrca;
extern component_tag<TPinballComponent> tag_a_dvtrcb;
extern component_tag<TPinballComponent> tag_a_dvtrda;
extern component_tag<TPinballComponent> tag_a_dvtrdb;
extern component_tag<TSound> tag_tabc24wave;
extern component_tag<TSound> tag_tabc51wave;

// Declared nowhere else: the four combo lights, the Shoot Again lamp and five waves.
extern component_tag<TLight> tag_v_lite26;  // D-R-A-G-O-N letter, tower ramp
component_tag<TLight> tag_v_lite40 = {"v_lite40"};  // "Shoot Again" / ball-save lamp
component_tag<TLight> tag_v_lite67 = {"v_lite67"};  // dragon-ramp combo ladder
component_tag<TLight> tag_v_lite68 = {"v_lite68"};  // dragon-chute combo ladder
component_tag<TLight> tag_v_lite69 = {"v_lite69"};  // fire-ridge orbit combo ladder
component_tag<TLight> tag_v_lite70 = {"v_lite70"};  // tower-ramp combo light
component_tag<TSound> tag_tabc04wave = {"tabc04wave"};
component_tag<TSound> tag_tabc09wave = {"tabc09wave"};
component_tag<TSound> tag_tabc18wave = {"tabc18wave"};
component_tag<TSound> tag_tabc20wave = {"tabc20wave"};
component_tag<TSound> tag_tabc22wave = {"tabc22wave"};

static TTextBox*& info_text_box = tag_info_text_box.Component;
static TLight*& lite24 = tag_v_lite24.Component;
static TLight*& lite26 = tag_v_lite26.Component;
static TLight*& lite29 = tag_v_lite29.Component;
static TLight*& lite30 = tag_v_lite30.Component;
static TLight*& lite32 = tag_v_lite32.Component;
static TLight*& lite38 = tag_v_lite38.Component;
static TLight*& lite39 = tag_v_lite39.Component;
static TLight*& lite40 = tag_v_lite40.Component;
static TLight*& lite47 = tag_v_lite47.Component;
static TLight*& lite48 = tag_v_lite48.Component;
static TLight*& lite49 = tag_v_lite49.Component;
static TLight*& lite50 = tag_v_lite50.Component;
static TLight*& lite55 = tag_v_lite55.Component;
static TLight*& lite56 = tag_v_lite56.Component;
static TLight*& lite67 = tag_v_lite67.Component;
static TLight*& lite68 = tag_v_lite68.Component;
static TLight*& lite69 = tag_v_lite69.Component;
static TLight*& lite70 = tag_v_lite70.Component;
static TLight*& lite119 = tag_v_lite119.Component;
static TLight*& lite125 = tag_v_lite125.Component;
static TLight*& lite126 = tag_v_lite126.Component;
static TLightGroup*& kicker_lights = tag_outlane_kicker_light_group.Component;
static TLightGroup*& prison_windows = tag_prison_window_light_group.Component;
static TLightGroup*& watch_windows = tag_watch_window_light_group.Component;
static TLightGroup*& gate_windows = tag_gate_window_light_group.Component;
static TPinballComponent*& mode_timer = tag_mode_timer.Component;
static TPinballComponent*& kicker = tag_a_kick1.Component;
static TPinballComponent*& kout3 = tag_a_kout3.Component;
static TPinballComponent*& spot10 = tag_a_spot10.Component;
static TPinballComponent*& spot14 = tag_a_spot14.Component;
static TPinballComponent*& dvtrAA = tag_a_dvtraa.Component;
static TPinballComponent*& dvtrAB = tag_a_dvtrab.Component;
static TPinballComponent*& dvtrBA = tag_a_dvtrba.Component;
static TPinballComponent*& dvtrBB = tag_a_dvtrbb.Component;
static TPinballComponent*& dvtrCA = tag_a_dvtrca.Component;
static TPinballComponent*& dvtrCB = tag_a_dvtrcb.Component;
static TPinballComponent*& dvtrDA = tag_a_dvtrda.Component;
static TPinballComponent*& dvtrDB = tag_a_dvtrdb.Component;
static TSound*& tabc04wave = tag_tabc04wave.Component;
static TSound*& tabc09wave = tag_tabc09wave.Component;
static TSound*& tabc18wave = tag_tabc18wave.Component;
static TSound*& tabc20wave = tag_tabc20wave.Component;
static TSound*& tabc22wave = tag_tabc22wave.Component;
static TSound*& tabc24wave = tag_tabc24wave.Component;
static TSound*& tabc51wave = tag_tabc51wave.Component;

// The decompiler dropped every TSound::Play argument (vtable slot +0x18 is called with no
// visible arguments), so the caller pointer and debug name passed below are reconstructed
// from upstream's convention -- they affect stereo panning only, never rules.

// DAT_00434d2c -- the ball-save countdown phase, the structural twin of upstream's
// control::extraball_light_flag. Set to 3 by s_plunger1 on every new ball and by NewGame,
// cleared by the drain handler on a tilt. Owned by dragon_part4.cpp, which holds both
// the plunger that arms it and the drain that clears it.
extern int shoot_again_flag;

// ===========================================================================
// Lights
// ===========================================================================

// 0x00424870 -- v_lite40, the "Shoot Again" lamp (skeleton: v_lite40Control)
//
// Counts the ball-save window down in three timed phases, each ended by the light's own
// TimeoutTimer calling back as ControlTimerExpired. 10 s solid + 5 s flashing is the 15 s
// ball save the manual documents, and the drain handler relaunches the ball iff lite40 is
// still lit, so this state machine *is* the window.
//
// Phase 3 is a hold: the lamp is lit solid and untimed from the moment the ball is fed,
// and only the ball leaving the launch lane starts the clock. The light is addressed
// through its tag rather than through `caller` because the one-way handler below drives
// this function with a null caller.
void ShootAgainLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired)
		return;

	switch (shoot_again_flag)
	{
	case 3:
		shoot_again_flag = 2;
		lite40->Message(MessageCode::TLightTurnOnTimed, 10.0f);
		break;
	case 2:
		shoot_again_flag = 1;
		lite40->Message(MessageCode::TLightFlasherStartTimed, 5.0f);
		break;
	case 1:
		shoot_again_flag = 0;
		lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		break;
	default:
		break;
	}
}

// 0x00424590 -- s_oneway2, the launch-lane one-way (skeleton: s_oneway2Control)
//
// The ball passing out of the launch lane is what takes the Shoot Again lamp off its
// untimed hold. The original really does call the lamp's own handler directly rather than
// routing a message through control::handler, so the lamp is stepped even though no timer
// fired.
void LaunchLaneOneWayControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision && shoot_again_flag == 3)
		ShootAgainLightControl(MessageCode::ControlTimerExpired, nullptr);
}

// 0x00428240 -- v_lite37, the Shield armor light (skeleton: v_lite37Control)
//
// Self-rescheduling three-phase lamp, the same idiom as upstream's
// ShootAgainLightControl but with one more phase. The Shield award primes it with
// MessageField = 0 and a 2 s flash-then-stay-on; from there 53 s solid plus 5 s
// flash-then-off takes the Shield to exactly 60 s, which is where the manual's "60
// second" armor duration comes from -- the light owns the clock, not the kicker. (Helm of
// Terror and Chain mail get theirs from their blockers' duration fields instead, so
// analysis/dragon-quest-system.md §4.2's [UNRESOLVED] on the durations only still stands
// for those two.)
//
// MessageField is post-incremented and the OLD value selects the phase, so phase 2 wraps
// the counter instead of arming a fourth timer.
void ShieldLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired)
		return;

	const int phase = caller->MessageField++;
	if (phase == 0)
		caller->Message(MessageCode::TLightTurnOnTimed, 53.0f);
	else if (phase == 1)
		caller->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 5.0f);
	else
		caller->MessageField = 0;
}

// 0x00428290 -- v_lite29, the Dragon Blood spell light (skeleton: v_lite29Control)
//
// Dragon Blood's effect is the a_kout3 holding kickout, which deactivates itself every
// time it releases a ball. The light's flash timer is the heartbeat that re-arms it, so
// the kickout stays live for exactly as long as the spell light is lit.
void DragonBloodLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlTimerExpired && lite29->light_on())
		kout3->ActiveFlag = 1;
}

// 0x00427860 -- v_wlite96 / 97 / 98 / 99, the four wall lights (skeleton: v_wlite96Control)
//
// One scoring line, 1,500 each; all four components share this handler. The nearest Space
// Cadet shape match is EscapeChuteSinkControl, but behaviourally this is a RebounderControl.
void WallLightControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		AddScoreFor(caller, 0);
}

// ===========================================================================
// Rollovers and lanes
// ===========================================================================

// 0x00426a50 -- a_roll7, the kicker-arming rollover, 15,000 (skeleton: a_roll7Control)
//
// Hands the player a ball save outright: one more light in the outlane kicker group, the
// kicker switched live, and diverter A flipped so the lane feeds it. a_sink9's handler
// flips the same pair the other way and drops lite47, which is what pins lite47 as the
// diverter-A routing indicator rather than a scoring light.
void KickerRearmRolloverControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	AddScoreFor(caller, 0);
	tabc24wave->Play(caller, "KickerRearmRollover");

	// TLightGroupOffsetAnimationForward is 0x24 in the binary (upstream 32, group band
	// +4). Derived rather than anchored, but cross-checked against a_kick1's own handler
	// at 0x426d00, which spends a light with 0x25 (OffsetAnimationBackward, upstream 33)
	// and goes dead when GetOnCount reaches zero: the two codes are the producer and
	// consumer of the kicker's charge.
	kicker_lights->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0f);
	kicker->ActiveFlag = 1;
	lite47->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	dvtrAA->Message(MessageCode::TGateDisable, 0.0f);
	dvtrAB->Message(MessageCode::TGateEnable, 0.0f);
}

// 0x00426ac0 -- a_roll5, 30,000 (skeleton: a_roll5Control)
//
// Byte-identical to a_roll2 below. Two functions in the original, like upstream's
// LeftKickerControl / RightKickerControl split, so both are kept and the component table
// stays a faithful copy.
void Rollover5Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		AddScoreFor(caller, 0);
}

// 0x00426ae0 -- a_roll2, 30,000 (skeleton: a_roll2Control)
void Rollover2Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		AddScoreFor(caller, 0);
}

// 0x00426b00 -- a_roll6, left return lane, 10,000 (skeleton: a_roll6Control)
//
// Each return lane has a ball-save light -- lite56 here, lite55 on a_roll1 -- lit by the
// tower-lock diverter flip. Rolling through while it is lit cashes it in for a kicker
// charge and consumes the light; the base score is paid either way.
void ReturnLaneRollover6Control(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (lite56->light_on())
	{
		tabc24wave->Play(caller, "ReturnLaneRollover6");
		kicker_lights->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0f);
		kicker->ActiveFlag = 1;
		lite56->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	AddScoreFor(caller, 0);
}

// 0x00426b70 -- a_roll1, right return lane, 10,000 (skeleton: a_roll1Control)
void ReturnLaneRollover1Control(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (lite55->light_on())
	{
		tabc24wave->Play(caller, "ReturnLaneRollover1");
		kicker_lights->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0f);
		kicker->ActiveFlag = 1;
		lite55->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	AddScoreFor(caller, 0);
}

// 0x004269a0 -- s_trip1 / s_trip2 and v_lite69, the fire-ridge orbit
//                                                   (skeleton: s_trip1Control)
//
// A combo ladder: 20,000 / 50,000 / 100,000 / 1,000,000. lite69 holds the rung in its
// MessageField and its own flash timeout resets it, so the ladder only climbs while the
// player keeps the orbit going. v_lite69 is in the component table alongside the two trip
// wires precisely so that timeout is routed back here.
void FireRidgeOrbitControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code == MessageCode::ControlTimerExpired)
	{
		lite69->MessageField = 0; // combo window elapsed
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;

	const int addedScore = AddScoreFor(caller, lite69->MessageField);
	if (lite69->MessageField < 3)
		lite69->MessageField++;

	// Read off the listing: `push 0; push 1; call vtable+4` at 0x426a0b, then
	// `push 0x41a00000; push 0x10; call vtable+4` at 0x426a1f. 0x41a00000 is 20.0f, not
	// the 10.0f borrowed from DragonChuteControl.
	lite69->Message(MessageCode::TLightTurnOn, 0.0f);
	lite69->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 20.0f);

	FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_FireRidge)), addedScore);
	info_text_box->Display(Buffer, 2.0f);
}

// ===========================================================================
// Ramps, chutes and gates
// ===========================================================================

// 0x004265e0 -- a_ramp103, the dragon ramp (skeleton: a_ramp103Control)
//
// Pays an escalating 50,000 x (rung + 1) and puts the same amount into the jackpot, so a
// held combo compounds twice. lite67 is the combo light: if it has gone out the rung
// resets to 0, otherwise it climbs, capped at 9 -- 500,000 a pass at the top.
//
// Dragon Hoard suppresses both the sound and the score because the quest controller pays
// the Amethyst gem for this ramp instead. That works because Dragon's control::handler
// runs MissionControl *before* the component handler, the reverse of upstream, which is
// why so many Dragon handlers can simply opt out by mode.
void DragonRampControl(MessageCode code, TPinballComponent* caller)
{
	if (code != FtMessageCode::RampTraversed)
		return;

	const int mode = info_text_box->MessageField;

	if (mode != ModeDragonHoard)
		tabc18wave->Play(caller, "DragonRamp");

	if (!lite67->light_on())
		caller->MessageField = 0;
	else if (caller->MessageField < 9)
		caller->MessageField++;

	lite67->Message(MessageCode::TLightTurnOn, 0.0f);
	lite67->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 20.0f);

	// The shared "route the lane to the ramp" idiom; DragonChuteControl runs it inverted,
	// and a_sink10's handler repeats it verbatim. Rescue Maiden and Tower Barrage skip it
	// because those modes own the diverter.
	if (mode != ModeRescueMaiden && mode != ModeTowerBarrage)
	{
		lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		dvtrDA->Message(MessageCode::TGateEnable, 0.0f);
		dvtrDB->Message(MessageCode::TGateDisable, 0.0f);
	}

	lite119->Message(MessageCode::TLightFlasherStartTimed, 2.0f);

	if (mode != ModeDragonHoard)
	{
		const int amount = (caller->MessageField + 1) * 50000;
		AddScoreAt(caller, amount);
		AddToJackpot(amount);
	}

	if (mode == ModeNormal)
	{
		lite24->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		// CheckDragonFlurry starts Dragon Flurry when all six letters are lit; the jackpot
		// line would be stepping on that mode's announcement, so it is suppressed.
		if (!CheckDragonFlurry())
			info_text_box->Display(pb::get_rc_string(DragonMsg(Str_JackpotGrows)), 2.0f);
	}
}

// 0x00426740 -- a_ramp6, the tower ramp (skeleton: a_ramp6Control)
//
// Carries no score of its own. Its combo light lite70 is the arming condition for a free
// spell: a pass with the light already lit casts one, a pass with it dark only
// acknowledges. Either way the light is relit for another 10 s window.
//
// In normal play it also advances a D-R-A-G-O-N letter first, and bails out of the spell
// entirely if that completed the word -- Dragon Flurry has just taken the table over.
void TowerRampControl(MessageCode code, TPinballComponent* caller)
{
	if (code != FtMessageCode::RampTraversed)
		return;

	if (info_text_box->MessageField == ModeNormal)
	{
		lite26->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (CheckDragonFlurry())
		{
			lite70->Message(MessageCode::TLightTurnOn, 0.0f);
			lite70->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);
			return;
		}
	}

	if (lite70->light_on())
		CastRandomSpell();
	else
		tabc09wave->Play(caller, "TowerRamp");

	lite70->Message(MessageCode::TLightTurnOn, 0.0f);
	lite70->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);
}

// 0x004267f0 -- s_oneway5, the dragon chute (skeleton: s_oneway5Control)
//
// A five-rung ladder -- 25,000 / 75,000 / 100,000 / 125,000 / 150,000 -- on lite68, plus a
// flat 200,000 into the *super* jackpot every pass. Dragon Hoard returns before any of it:
// the quest controller awards this chute's gem and the jackpot instead. Normal play flips
// diverter D the opposite way to the dragon ramp, so the two shots fight over the lane.
void DragonChuteControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code == MessageCode::ControlTimerExpired)
	{
		lite68->MessageField = 0; // combo window elapsed
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;
	if (mode == ModeNormal)
	{
		lite125->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite126->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		dvtrDA->Message(MessageCode::TGateDisable, 0.0f);
		dvtrDB->Message(MessageCode::TGateEnable, 0.0f);
	}
	else if (mode == ModeDragonHoard)
		return;

	tabc20wave->Play(caller, "DragonChute");
	AddScoreFor(caller, lite68->MessageField);
	// 0x4268ed formats the increment, not the credit: `push 0x30d40` (200,000) is the
	// vararg, pushed before the format string.
	if (lite68->MessageField < 4)
		lite68->MessageField++;

	// The original inlines the add and the 20,000,000 clamp here rather than calling the
	// shared routine; same arithmetic either way.
	AddToSuperJackpot(200000);

	FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_SuperJackpotUp)), 200000);
	info_text_box->Display(Buffer, 2.0f);

	lite68->Message(MessageCode::TLightTurnOn, 0.0f);
	lite68->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 10.0f);
}

// 0x00426940 -- a_accl1, the moat chute accelerator (skeleton: a_accl1Control)
//
// Two scores, selected by whether the Stealth spell light is lit: 75,000 normally,
// 500,000 with Stealth. Structurally identical to upstream's FlagControl, which indexes
// get_scoring with lite20->light_on() the same way. Dragon Hoard pays the Opal gem instead.
void MoatChuteAcceleratorControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision || info_text_box->MessageField == ModeDragonHoard)
		return;

	tabc22wave->Play(caller, "MoatChuteAccelerator");
	AddScoreFor(caller, lite32->light_on() ? 1 : 0);
}

// 0x00426be0 -- s_rexit19, the gargoyle ramp exit, 2 dragon eggs
//                                                   (skeleton: s_rexit19Control)
//
// Fires on RampExited rather than on collision, so the eggs are paid once per completed
// traversal. It also restores its own diverter pair, but only in normal play -- during a
// quest the controller owns the routing.
void GargoyleRampExitControl(MessageCode code, TPinballComponent* caller)
{
	if (code != FtMessageCode::RampExited)
		return;

	AwardDragonEggs(2);
	if (info_text_box->MessageField == ModeNormal)
	{
		dvtrCA->Message(MessageCode::TGateEnable, 0.0f);
		dvtrCB->Message(MessageCode::TGateDisable, 0.0f);
	}
}

// 0x00426c20 -- s_rexit18, the forest ramp exit, 1 dragon egg
//                                                   (skeleton: s_rexit18Control)
void ForestRampExitControl(MessageCode code, TPinballComponent* caller)
{
	if (code != FtMessageCode::RampExited)
		return;

	AwardDragonEggs(1);
	if (info_text_box->MessageField == ModeNormal)
	{
		dvtrBA->Message(MessageCode::TGateEnable, 0.0f);
		dvtrBB->Message(MessageCode::TGateDisable, 0.0f);
	}
}

// 0x00426c60 -- a_flip2, the right flipper (skeleton: a_flip2Control)
//
// Not a scoring handler at all: it is the release button for the a_kout3 holding kickout
// that Dragon Blood arms. Code 1 is TFlipperExtend -- the private TFlipper numbering is
// unshifted, like TLight's -- so this runs the moment the flipper is raised, not on the
// public input message. (dragon_missions.cpp names the same value ft::FlipperPressed for
// the egg-exchange prompt.)
//
// During Slay Dragon the two spell lights are restarted with TLightFlasherStartTimed,
// which leaves them lit and therefore leaves the kickout armed for the rest of the finale;
// outside it they flash once and go out, consuming the spell.
void RightFlipperControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::TFlipperExtend || kout3->MessageField != 1)
		return;

	const int mode = info_text_box->MessageField;
	kout3->MessageField = 0;

	if (mode == ModeSlayDragon)
	{
		// 1.0f confirmed: the compiler hoists the single `push 0x3f800000` to 0x426c82,
		// above the mode branch, so the same operand serves lite29 in both arms -- which
		// is also why Ghidra could not attribute it to either call.
		lite29->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		lite30->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	}
	else
	{
		lite29->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		lite30->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	kout3->Message(MessageCode::TKickoutRestartTimer, 0.0f);
}

// ===========================================================================
// Trip wires -- the three tower window lanes
// ===========================================================================
//
// s_trip6/5, s_trip3/4 and s_trip8/9 are three identically shaped handlers, one per tower,
// each owning its own window light group. The pairing
//
//   prison tower -> lite48 / prison_window_light_group / s_trip6, s_trip5
//   watch tower  -> lite49 / watch_window_light_group  / s_trip3, s_trip4
//   gate tower   -> lite50 / gate_window_light_group   / s_trip8, s_trip9
//
// upgrades analysis/dragon-quest-system.md §1.1's [PROBABLE] on "v_lite48/49/50 tower-lock
// bookkeeping for prison/watch/gate" to certain: each handler reads exactly one of them.
//
// The common tail is a bank: score by the group's on-count, light the next window, and when
// the hit would fill the bank clear it and flash it off instead. The lock lights'
// MessageField is the "this tower is armed to swallow a ball" flag, and a trip wire stands
// down while it is set so the sink can lock instead. The number of scores each component
// carries equals its group's light count -- four, three and two respectively -- which is
// what makes the on-count a safe scoring index.

// 0x00427880 -- s_trip6 / s_trip5, the prison tower windows, 25k/50k/75k/100k
//                                                   (skeleton: s_trip6Control)
//
// Also where Rescue Maiden's per-hit awards live. analysis/dragon-quest-system.md §3 state
// 6 lists them as [UNRESOLVED] and blames the unusable decompile of a_sink18; they are in
// fact right here and perfectly legible -- 250,000, 500,000, the jackpot, then the
// extra-ball light with string 3034 "Maiden Rescued / Extra Ball Available", exactly the
// manual's ladder. a_sink18 is the lock sink, not the award site.
void PrisonWindowTripControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeNormal:
		if (lite48->MessageField)
			return; // armed for a ball lock -- let the prison sink have it
		tabc04wave->Play(caller, "PrisonWindowTrip");
		break;

	case ModeRestartWindow:
		// Only the tower whose quest just ended keeps paying during the 10 s restart
		// window; mode_timer->MessageField holds that quest's index.
		if (mode_timer->MessageField == ModeRescueMaiden)
		{
			const int credited = AddScoreFor(caller, 0);
			FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
			info_text_box->Display(Buffer, 2.0f);
			return;
		}
		break;

	case ModeRescueMaiden:
		// RESOLVED. The ladder (250,000 / 500,000 / jackpot / extra-ball light, then
		// light the window) lives at this address in the binary, so this is its one
		// call site. dragon_missions.cpp owns the body as RescueMaidenAwardWindow but
		// no longer calls it from RescueMaidenController's Collision arm, which only
		// detects the four-window completion -- the binary's FUN_00429510 does the
		// same. Both calls would have fired the award twice, because Dragon's
		// control::handler runs the mode machine before the component handler.
		RescueMaidenAwardWindow(caller);
		return;

	case ModeTowerBarrage:
		AddScoreAt(caller, 100000); // flat rate while the tower is spitting balls
		return;

	default:
		break;
	}

	const int lit = prison_windows->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	const int credited = AddScoreFor(caller, lit);
	FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
	info_text_box->Display(Buffer, 2.0f);

	if (prison_windows->Message(MessageCode::TLightGroupGetLightCount, 0.0f) - lit == 1)
	{
		// This hit fills the bank. Code 0 on a group is TLightGroupNull, which TLightGroup
		// forwards to TLight::TLightTurnOff, so the whole group goes dark and then flashes
		// out -- the bank resets rather than sitting full.
		prison_windows->Message(MessageCode::TLightTurnOff, 0.0f);
		prison_windows->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	prison_windows->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
}

// 0x00427b30 -- s_trip3 / s_trip4, the watch tower windows, 40k/80k/120k
//                                                   (skeleton: s_trip3Control)
//
// Same bank, three lights. Only the prison tower advances the maiden, so Rescue Maiden
// here just pays the base score. Note the sound placement differs from the prison tower:
// tabc04 plays on every scoring path here, on the normal-play one only there. Transcribed
// as found rather than regularised.
void WatchWindowTripControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeNormal:
		if (lite49->MessageField)
			return;
		break;

	case ModeRestartWindow:
		if (mode_timer->MessageField == ModeRescueMaiden)
		{
			const int credited = AddScoreFor(caller, 0);
			FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
			info_text_box->Display(Buffer, 2.0f);
			return;
		}
		break;

	case ModeRescueMaiden:
	{
		tabc51wave->Play(caller, "WatchWindowTrip");
		const int credited = AddScoreFor(caller, 0);
		FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
		info_text_box->Display(Buffer, 2.0f);
		return;
	}

	case ModeTowerBarrage:
		AddScoreAt(caller, 100000);
		return;

	default:
		break;
	}

	tabc04wave->Play(caller, "WatchWindowTrip");
	const int lit = watch_windows->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	const int credited = AddScoreFor(caller, lit);
	FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
	info_text_box->Display(Buffer, 2.0f);

	if (watch_windows->Message(MessageCode::TLightGroupGetLightCount, 0.0f) - lit == 1)
	{
		watch_windows->Message(MessageCode::TLightTurnOff, 0.0f);
		watch_windows->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	watch_windows->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
}

// 0x00427d10 -- s_trip8 / s_trip9, the gate tower windows, 50k/100k
//                                                   (skeleton: s_trip8Control)
//
// Same bank, two lights, and no sound at all on the normal-play path.
void GateWindowTripControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeNormal:
		if (lite50->MessageField)
			return;
		break;

	case ModeRestartWindow:
		if (mode_timer->MessageField == ModeRescueMaiden)
		{
			const int credited = AddScoreFor(caller, 0);
			FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
			info_text_box->Display(Buffer, 2.0f);
			return;
		}
		break;

	case ModeRescueMaiden:
	{
		tabc51wave->Play(caller, "GateWindowTrip");
		const int credited = AddScoreFor(caller, 0);
		FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
		info_text_box->Display(Buffer, 2.0f);
		return;
	}

	case ModeTowerBarrage:
		AddScoreAt(caller, 100000);
		return;

	default:
		break;
	}

	const int lit = gate_windows->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	const int credited = AddScoreFor(caller, lit);
	FormatWithValue(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_WindowLane)), credited);
	info_text_box->Display(Buffer, 2.0f);

	if (gate_windows->Message(MessageCode::TLightGroupGetLightCount, 0.0f) - lit == 1)
	{
		gate_windows->Message(MessageCode::TLightTurnOff, 0.0f);
		gate_windows->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	gate_windows->Message(MessageCode::TLightGroupResetAndTurnOn, 1.0f);
}

// ===========================================================================
// Blockers
// ===========================================================================
//
// All four below are upstream's DrainBallBlockerControl two-stage pattern -- MessageField
// walks 1 (initial period) -> 2 (extended period) -> 0 (gone), with a companion light
// mirroring each stage -- but with two FT differences worth keeping in mind:
//
//  * FT arms the timer with a separate TBlockerRestartTimeout rather than passing the
//    duration to TBlockerEnable. The code is 0x45, and `a_center_post`'s own handler at
//    0x425910 is the proof: the constants it pushes are 0x37, 0x38, 0x45 twice, and the
//    light codes 0x14 and 0xf -- nothing else. Upstream numbers that message 59, the same
//    value as TTimerResetTimer, which is why dragon_part3.cpp can send it to `mode_timer`
//    (a TTimer, not a TBlocker) and mean it. The port's mapping is already right here;
//    the +4 arithmetic that covers TBlockerDisable and TBlockerEnable does *not* extend
//    to this one, and 0x39 -- which is in TBlocker's 0x37..0x3c jump table at 0x415398 --
//    is a different mechanic entirely: it sets the part's position index. And
//  * a negative value means different things to the two classes that answer 0x45, and
//    this comment used to get it backwards. On a *TBlocker* it is clamped: 0x4152b5
//    compares the value against the 0.0 at 0x437330 and arms with max(value, 0.0), so
//    MoatBlockerControl's -1.0f fires the timeout immediately rather than cancelling it.
//    Upstream's std::max(value, 0.0f) is therefore faithful, and the -1.0f is harmless
//    only because the handler has already set MessageField = 0, so the expiry it trips
//    falls into the "gone" branch. On a *TTimer* -- which is what `mode_timer` is, and
//    what dragon_part3.cpp sends -1.0f to -- it cancels: 0x41b3d9 returns without
//    arming. Upstream gave both classes the clamp; TTimer.cpp now has the cancel.
//
// The two durations are the blocker's own InitialDuration / ExtendedDuration (the original
// reads them at +0x86 / +0x8a and casts int to float, exactly as upstream does), so the
// armor timings are table data rather than rules code.

// 0x00425910 -- a_center_post, the Helm of Terror center post
//                                                   (skeleton: a_center_postControl)
void CenterPostControl(MessageCode code, TPinballComponent* caller)
{
	auto block = static_cast<TBlocker*>(caller);

	switch (code)
	{
	case MessageCode::TBlockerDisable:
		// Forced down -- drain, tilt, or the armor being cleared.
		block->MessageField = 0;
		block->Message(MessageCode::TBlockerDisable, 0.0f);
		lite38->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		break;

	case MessageCode::TBlockerEnable:
		block->MessageField = 1;
		block->Message(MessageCode::TBlockerEnable, 0.0f);
		block->Message(MessageCode::TBlockerRestartTimeout, static_cast<float>(block->InitialDuration));
		lite38->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		break;

	case MessageCode::ControlTimerExpired:
		if (block->MessageField == 1)
		{
			// Grace period: the post stays up but the light flashes it out, so the player
			// can see the armor expiring.
			const auto extended = static_cast<float>(block->ExtendedDuration);
			block->MessageField = 2;
			block->Message(MessageCode::TBlockerRestartTimeout, extended);
			lite38->Message(MessageCode::TLightFlasherStartTimedThenStayOff, extended);
		}
		else
		{
			block->MessageField = 0;
			block->Message(MessageCode::TBlockerDisable, 0.0f);
		}
		break;

	default:
		break;
	}
}

// 0x00425a10 -- a_bloce, the Chain mail right-outlane blocker (skeleton: a_bloceControl)
//
// Byte-identical to a_center_post except for the light; two functions in the original.
void RightOutlaneBlockerControl(MessageCode code, TPinballComponent* caller)
{
	auto block = static_cast<TBlocker*>(caller);

	switch (code)
	{
	case MessageCode::TBlockerDisable:
		block->MessageField = 0;
		block->Message(MessageCode::TBlockerDisable, 0.0f);
		lite39->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		break;

	case MessageCode::TBlockerEnable:
		block->MessageField = 1;
		block->Message(MessageCode::TBlockerEnable, 0.0f);
		block->Message(MessageCode::TBlockerRestartTimeout, static_cast<float>(block->InitialDuration));
		lite39->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		break;

	case MessageCode::ControlTimerExpired:
		if (block->MessageField == 1)
		{
			const auto extended = static_cast<float>(block->ExtendedDuration);
			block->MessageField = 2;
			block->Message(MessageCode::TBlockerRestartTimeout, extended);
			lite39->Message(MessageCode::TLightFlasherStartTimedThenStayOff, extended);
		}
		else
		{
			block->MessageField = 0;
			block->Message(MessageCode::TBlockerDisable, 0.0f);
		}
		break;

	default:
		break;
	}
}

// 0x00425b10 -- a_blocf, the moat blocker and the Stealth spell
//                                                   (skeleton: a_blocfControl)
//
// The same machine with inverted polarity, because Stealth works by *removing* this
// blocker: TBlockerDisable is the award and TBlockerEnable is the cancel, and the timed
// stages put the blocker back rather than taking it away. lite32 is the spell light,
// driven from here rather than from the spell code -- which is why
// analysis/dragon-quest-system.md §4.3 lists slot 3 as "a_blocf's own handler drives
// v_lite32".
void MoatBlockerControl(MessageCode code, TPinballComponent* caller)
{
	auto block = static_cast<TBlocker*>(caller);

	switch (code)
	{
	case MessageCode::TBlockerDisable:
		{
			// Stealth cast: open the moat path for InitialDuration and light the spell.
			const auto initial = static_cast<float>(block->InitialDuration);
			block->MessageField = 1;
			block->Message(MessageCode::TBlockerDisable, 0.0f);
			block->Message(MessageCode::TBlockerRestartTimeout, initial);
			lite32->Message(MessageCode::TLightTurnOnTimed, initial);
			break;
		}

	case MessageCode::TBlockerEnable:
		// Cancel -- drain, or the end of Slay Dragon. Kill the timer, drop the light, close.
		block->Message(MessageCode::TBlockerRestartTimeout, -1.0f);
		lite32->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		block->MessageField = 0;
		block->Message(MessageCode::TBlockerEnable, 0.0f);
		break;

	case MessageCode::ControlTimerExpired:
		if (block->MessageField == 1)
		{
			const auto extended = static_cast<float>(block->ExtendedDuration);
			block->MessageField = 2;
			block->Message(MessageCode::TBlockerRestartTimeout, extended);
			lite32->Message(MessageCode::TLightFlasherStartTimed, extended);
		}
		else
		{
			block->MessageField = 0;
			block->Message(MessageCode::TBlockerEnable, 0.0f);
		}
		break;

	default:
		break;
	}
}

// 0x00423810 -- a_rocka, the rock in front of the a_spot10 gargoyle target
//                                                   (skeleton: a_rockaControl)
//
// The rocks are TBlockers wearing a rock sprite. Hitting the target slides its rock open
// (TBlockerDisable) and disables the target; when the rock has finished sliding back the
// target is re-armed, which is all this handler and its twin do.
//
// TSoloTargetEnable is 0x36 in the binary (upstream 50, target band +4): derived, but
// confirmed by a_spot10's own handler, which sends 0x35 -- TSoloTargetDisable, upstream
// 49 -- to itself on the way in.
//
// The BlockerUnknownP7e guard is now exact rather than a reading: +0x7e is the frame the
// rock has just arrived at and -1 is the retracted sentinel, so this re-arms the target
// only when the rock has finished sliding back *out* (BlockerEnable aims at the last
// frame). The retraction that a_spot10's own handler triggers raises the same
// notification and is filtered out here -- a_spot10 disables itself on the way in and is
// never re-enabled by its own handler (0x4254a0 confirms it: the body is one flash, one
// BlockerDisable, one self-disable and a call to 0x4252b0).
void GargoyleRockAControl(MessageCode code, TPinballComponent* caller)
{
	if (code != FtMessageCode::BlockerMotionDone)
		return;
	if (BlockerUnknownP7e(caller) == -1)
		return;

	spot10->Message(MessageCode::TSoloTargetEnable, 0.0f);
}

// 0x00423840 -- a_rockb, the rock in front of the a_spot14 gargoyle target
//                                                   (skeleton: a_rockbControl)
//
// Identical to a_rocka except for the target it re-arms. Confirmed against the binary:
// 0x423810's body sends to a_spot10 and 0x423840's to a_spot14, so the two addresses and
// the two targets pair as written here.
void GargoyleRockBControl(MessageCode code, TPinballComponent* caller)
{
	if (code != FtMessageCode::BlockerMotionDone)
		return;
	if (BlockerUnknownP7e(caller) == -1)
		return;

	spot14->Message(MessageCode::TSoloTargetEnable, 0.0f);
}

} // namespace dragon
