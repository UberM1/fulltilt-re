// dragon_part2.cpp -- Dragon's Keep rules: lights, lanes, rollovers, trip wires,
// ramps and gates.
//
// Reimplementation of 25 of DRAGON.EXE's component handlers, in the shape of
// SpaceCadetPinball's control.cpp. Sources: decomp/dragon/functions/*.c (Ghidra, with
// every DAT_ annotated with its group name), analysis/symbols-cadet.md (offsets, vtable
// slots, the CADET<->upstream message-code anchors), analysis/dragon-quest-system.md
// (mode register and collectibles) and analysis/dragon-matches.txt.
//
// This is a fragment: the tags, the score arrays and the component table live in
// control_dragon.cpp, and the per-table interface the forward declarations below describe
// does not exist yet, so the file compiles in shape but does not link.
//
// ---------------------------------------------------------------------------
// Message codes
//
// Full Tilt's MessageCode enum is NOT upstream's. The renumbering is piecewise, because
// FT inserted codes into the middle of several bands; `binary = upstream + shift` with
//
//   TLight                      +0     (0x00..0x17 are upstream 0..23 verbatim)
//   TLightGroup / TComponentGroup
//   / TSoloTarget / TPopupTarget
//   / TBlocker                  +4
//   TGate / TKickout / TSink    +9
//   Control* / timer family     +10
//   ControlCollision            +11  (= 0x4a)
//   ControlMission*             +13  (0x4f / 0x50)
//
// Every value used below is either anchored in symbols-cadet.md §4 or derived from a band
// with >= 2 anchors in it; the two derivations this file relies on and that were NOT
// already in that table are called out at their use sites (0x24/0x25 and 0x35/0x36).
//
// FT also has codes with no upstream counterpart at all. Those are named here rather than
// left as magic numbers; see FtMessage below.
// ---------------------------------------------------------------------------

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"
#include "translations.h"

using MC = MessageCode;

// ---------------------------------------------------------------------------
// Declarations that belong to the shared Dragon header, repeated here so the file is
// self-contained. Names follow analysis/dragon-ball-handlers.md and
// analysis/dragon-scoring-handlers.md so the slices agree.
// ---------------------------------------------------------------------------

// FT-only message codes. The gaps 0x39..0x3d (blocker band) and 0x51..0x56 (mission band)
// have no upstream equivalent, so these are named from observed use, not from a mapping.
namespace FtMessage
{
	// Sent to a TBlocker when it has finished moving. Only the two gargoyle rocks and the
	// dragon's jaw ever receive it. UNVERIFIED: the name. Disassembling FT's TBlocker /
	// TAnimBlocker Message() + timer callback would settle whether 0x3d means "finished
	// extending", "finished retracting" or "animation step done".
	constexpr auto BlockerMotionDone = static_cast<MC>(0x3d);

	// Quest lifecycle, from analysis/dragon-quest-system.md §2.6.
	constexpr auto QuestRestart = static_cast<MC>(0x51);
	constexpr auto QuestTeardown = static_cast<MC>(0x52);
	constexpr auto QuestAbort = static_cast<MC>(0x53);
	constexpr auto RampEntered = static_cast<MC>(0x54);
	constexpr auto RampExited = static_cast<MC>(0x55);
	constexpr auto RampTraversed = static_cast<MC>(0x56);
}

// info_text_box->MessageField is Dragon's mode register -- the exact analogue of Space
// Cadet's lite198->MessageField. Values from dragon-quest-system.md §1.2.
namespace Quest
{
	constexpr int GameOver = 0;
	constexpr int BetweenBalls = 1;
	constexpr int NormalPlay = 2;
	constexpr int RestartWindow = 3;
	constexpr int DragonHoard = 4;
	constexpr int FireLizards = 5;
	constexpr int RescueMaiden = 6;
	constexpr int DragonPass = 7;
	constexpr int WizardsFury = 8;
	constexpr int SlayDragon = 9;
	constexpr int SkillShot = 10;
	constexpr int EndOfBall = 11;
	constexpr int EggExchange = 12;
	constexpr int TowerBarrage = 13;
	constexpr int GargoyleGaggleBall = 14;
	constexpr int DragonFlurry = 15;
}

// Shared helpers, addresses from analysis/dragon-ball-handlers.md §1.4.
int AddScoreFrom(TPinballComponent* caller, int scoreIndex); // 0x4230a0, indexes Scores[]
int AddScoreAmount(TPinballComponent* caller, int score);    // 0x4230f0, literal amount
void AddJackpot(int amount);                                 // 0x4238a0, clamp 10,000,000
void AddSuperJackpot(int amount);                            // 0x423870, clamp 20,000,000
int CollectJackpot(float unused, bool showText);             // 0x424490
void AwardDragonEggs(int count);                             // 0x4232b0
void CastRandomSpell();                                      // 0x4237f0
bool DragonSpelled();                                        // 0x4244e0, starts Dragon Flurry

// Tags. Variable names drop the group-name prefix (v_lite37 -> lite37) as upstream does.
extern TTextBox* info_text_box;
extern TPinballTable* TableG;

extern TLight* lite24;   // D-R-A-G-O-N letter lit by the dragon ramp
extern TLight* lite26;   // D-R-A-G-O-N letter lit by the tower ramp
extern TLight* lite29;   // Dragon Blood spell
extern TLight* lite30;   // Dragon Blood's second display light
extern TLight* lite32;   // Stealth spell
extern TLight* lite37;   // Shield armor
extern TLight* lite38;   // Helm of Terror armor (center post)
extern TLight* lite39;   // Chain mail armor (right outlane blocker)
extern TLight* lite47;   // diverter-A routing indicator
extern TLight* lite48;   // prison tower lock bookkeeping
extern TLight* lite49;   // watch tower lock bookkeeping
extern TLight* lite50;   // gate tower lock bookkeeping
extern TLight* lite55;   // right return-lane ball-save light
extern TLight* lite56;   // left return-lane ball-save light
extern TLight* lite57;   // extra-ball light
extern TLight* lite67;   // dragon-ramp combo light
extern TLight* lite68;   // dragon-chute combo light
extern TLight* lite69;   // fire-ridge orbit combo light
extern TLight* lite70;   // tower-ramp combo light
extern TLight* lite119;  // dragon-ramp flasher
extern TLight* lite125;  // dragon-chute route indicator
extern TLight* lite126;  // dragon-ramp route indicator
extern TLight* lite40;   // "Shoot Again" / ball-save lamp

extern TLightGroup* outlane_kicker_light_group;
extern TLightGroup* prison_window_light_group;
extern TLightGroup* watch_window_light_group;
extern TLightGroup* gate_window_light_group;

extern TPinballComponent* a_kick1;   // left-outlane kicker (ball save)
extern TPinballComponent* a_kout3;   // Dragon Blood holding kickout
extern TPinballComponent* a_spot10;  // gargoyle target behind rock A
extern TPinballComponent* a_spot14;  // gargoyle target behind rock B
extern TPinballComponent* mode_timer;

// Diverters: four pairs of TGate, flipped in opposition to route a lane.
extern TPinballComponent* a_dvtraa;
extern TPinballComponent* a_dvtrab;
extern TPinballComponent* a_dvtrba;
extern TPinballComponent* a_dvtrbb;
extern TPinballComponent* a_dvtrca;
extern TPinballComponent* a_dvtrcb;
extern TPinballComponent* a_dvtrda;
extern TPinballComponent* a_dvtrdb;

extern TSound* soundwave_tabc04;
extern TSound* soundwave_tabc09;
extern TSound* soundwave_tabc18;
extern TSound* soundwave_tabc20;
extern TSound* soundwave_tabc22;
extern TSound* soundwave_tabc24;
extern TSound* soundwave_tabc51;

// The decompiler dropped every TSound::Play argument (slot +0x18 is called with no visible
// args), so the caller pointer passed below is reconstructed from upstream's convention --
// it only affects stereo panning, never rules.

// control::shoot_again_flag -- DAT_00434d2c. Three-phase countdown for the ball-save
// window, the structural twin of upstream's control::extraball_light_flag. Set to 3 by the
// plunger on every new ball and by NewGame, cleared by the drain handler on a tilt.
extern int shoot_again_flag;

// ===========================================================================
// Lights
// ===========================================================================

// 0x00424870 -- v_lite40, the "Shoot Again" lamp.
//
// Counts the ball-save window down in three timed phases, each ended by the light's own
// TimeoutTimer calling back as ControlTimerExpired. 10 s solid + 5 s flashing = the 15 s
// ball save the manual documents; the drain handler relaunches the ball iff lite40 is
// still lit, so this state machine *is* the window.
//
// Phase 3 is the hold: the lamp is lit solid and untimed from the moment the ball is fed,
// and only the ball leaving the launch lane (LaunchLaneOneWayControl) starts the clock.
void control::ShootAgainLightControl(MC code, TPinballComponent* caller)
{
	if (code != MC::ControlTimerExpired)
		return;

	switch (shoot_again_flag)
	{
	case 3:
		shoot_again_flag = 2;
		lite40->Message(MC::TLightTurnOnTimed, 10.0f);
		break;
	case 2:
		shoot_again_flag = 1;
		lite40->Message(MC::TLightFlasherStartTimed, 5.0f);
		break;
	case 1:
		shoot_again_flag = 0;
		lite40->Message(MC::TLightResetAndTurnOff, 0.0);
		break;
	default:
		break;
	}
}

// 0x00424590 -- s_oneway2, the launch-lane one-way.
//
// The ball passing out of the launch lane is what takes the Shoot Again lamp off its
// untimed hold. The original really does call the lamp's own handler directly rather than
// going through control::handler, so the lamp is stepped even though no timer fired.
void control::LaunchLaneOneWayControl(MC code, TPinballComponent* caller)
{
	if (code == MC::ControlCollision && shoot_again_flag == 3)
		ShootAgainLightControl(MC::ControlTimerExpired, nullptr);
}

// 0x00428240 -- v_lite37, the Shield armor light.
//
// Self-rescheduling three-phase lamp, the same idiom as upstream's ShootAgainLightControl
// but with one more phase. AwardShieldArmor (0x423240) primes it with MessageField = 0 and
// a 2 s flash-then-stay-on; from there 53 s solid + 5 s flash-then-off takes the Shield to
// exactly 60 s, which is where the manual's "60 second" armor duration comes from -- the
// light owns the clock, not the kicker.
//
// Note MessageField is post-incremented and the OLD value selects the phase, so phase 2
// wraps the counter instead of arming a fourth timer.
void control::ShieldLightControl(MC code, TPinballComponent* caller)
{
	if (code != MC::ControlTimerExpired)
		return;

	const int phase = caller->MessageField++;
	if (phase == 0)
		caller->Message(MC::TLightTurnOnTimed, 53.0f);
	else if (phase == 1)
		caller->Message(MC::TLightFlasherStartTimedThenStayOff, 5.0f);
	else
		caller->MessageField = 0;
}

// 0x00428290 -- v_lite29, the Dragon Blood spell light.
//
// Dragon Blood's effect is the a_kout3 holding kickout, which deactivates itself every
// time it releases a ball. The light's flash timer is the heartbeat that re-arms it, so
// the kickout stays live for exactly as long as the spell light is lit.
void control::DragonBloodLightControl(MC code, TPinballComponent* caller)
{
	if (code == MC::ControlTimerExpired && lite29->light_on())
		a_kout3->ActiveFlag = 1;
}

// 0x00427860 -- v_wlite96 / 97 / 98 / 99, the four wall lights.
//
// One scoring line, 1,500 each; the four components share this handler. Upstream's
// EscapeChuteSinkControl is the nearest shape match but not the nearest behaviour: this is
// really a RebounderControl.
void control::WallLightControl(MC code, TPinballComponent* caller)
{
	if (code == MC::ControlCollision)
		AddScoreFrom(caller, 0);
}

// ===========================================================================
// Rollovers and lanes
// ===========================================================================

// 0x00426a50 -- a_roll7, the kicker-arming rollover (15,000).
//
// Hands the player a ball save outright: one more light in the outlane kicker group, the
// kicker switched live, and diverter A flipped so the lane feeds it. a_sink9 flips the
// same pair the other way, which is what pins lite47 as the routing indicator.
//
// 0x24 is TLightGroupOffsetAnimationForward (upstream 32, group band +4). Derived, but
// cross-checked: a_kick1's own handler at 0x426d00 consumes a light with 0x25
// (OffsetAnimationBackward, upstream 33) and goes dead when GetOnCount hits zero, so these
// two codes are the producer/consumer pair for the kicker's charge.
void control::KickerRearmRolloverControl(MC code, TPinballComponent* caller)
{
	if (code != MC::ControlCollision)
		return;

	AddScoreFrom(caller, 0);
	soundwave_tabc24->Play(caller, "KickerRearmRollover");
	outlane_kicker_light_group->Message(MC::TLightGroupOffsetAnimationForward, 0.0);
	a_kick1->ActiveFlag = 1;
	lite47->Message(MC::TLightResetAndTurnOn, 0.0);
	a_dvtraa->Message(MC::TGateDisable, 0.0);
	a_dvtrab->Message(MC::TGateEnable, 0.0);
}

// 0x00426ac0 -- a_roll5 (30,000).
// 0x00426ae0 -- a_roll2 (30,000).
//
// Two byte-identical functions in the original, like upstream's LeftKickerControl /
// RightKickerControl split. Kept separate so the component table stays a faithful copy.
void control::Rollover5Control(MC code, TPinballComponent* caller)
{
	if (code == MC::ControlCollision)
		AddScoreFrom(caller, 0);
}

void control::Rollover2Control(MC code, TPinballComponent* caller)
{
	if (code == MC::ControlCollision)
		AddScoreFrom(caller, 0);
}

// 0x00426b00 -- a_roll6, left return lane (10,000).
// 0x00426b70 -- a_roll1, right return lane (10,000).
//
// Each lane has a ball-save light (lite56 / lite55, lit by the 0x423d50 diverter flip).
// Rolling through while it is lit cashes it in for a kicker charge and consumes the light;
// the base score is paid either way.
void control::ReturnLaneRollover6Control(MC code, TPinballComponent* caller)
{
	if (code != MC::ControlCollision)
		return;

	if (lite56->light_on())
	{
		soundwave_tabc24->Play(caller, "ReturnLaneRollover6");
		outlane_kicker_light_group->Message(MC::TLightGroupOffsetAnimationForward, 0.0);
		a_kick1->ActiveFlag = 1;
		lite56->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	AddScoreFrom(caller, 0);
}

void control::ReturnLaneRollover1Control(MC code, TPinballComponent* caller)
{
	if (code != MC::ControlCollision)
		return;

	if (lite55->light_on())
	{
		soundwave_tabc24->Play(caller, "ReturnLaneRollover1");
		outlane_kicker_light_group->Message(MC::TLightGroupOffsetAnimationForward, 0.0);
		a_kick1->ActiveFlag = 1;
		lite55->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	AddScoreFrom(caller, 0);
}

// 0x004269a0 -- s_trip1 / s_trip2 and v_lite69, the fire-ridge orbit.
//
// A combo ladder: 20,000 / 50,000 / 100,000 / 1,000,000. lite69 holds the rung in its
// MessageField and its own flash timeout resets it, so the ladder only climbs while the
// player keeps the orbit going. v_lite69 is in the component table alongside the two trip
// wires precisely so that timeout is routed back here.
//
// UNVERIFIED: both Message values in the tail. Ghidra lost the float arguments (the
// abStack_50 writes in the listing are spilled return addresses, not data), and only the
// 0x10 opcode survived. 10.0f is taken from DragonChuteControl below, which runs the
// byte-identical turn-on/flash-then-off pair on its own ladder light. Reading the two
// push instructions at 0x4269e9 / 0x4269f4 would settle it.
//
// UNVERIFIED: the sprintf argument. Every other "%ld" site in Dragon formats the amount
// AddScoreFrom just credited, which is why the return value is captured here.
void control::FireRidgeOrbitControl(MC code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code == MC::ControlTimerExpired)
	{
		lite69->MessageField = 0; // combo window elapsed
		return;
	}
	if (code != MC::ControlCollision)
		return;

	const int addedScore = AddScoreFrom(caller, lite69->MessageField);
	if (lite69->MessageField < 3)
		lite69->MessageField++;

	lite69->Message(MC::TLightTurnOn, 0.0);
	lite69->Message(MC::TLightFlasherStartTimedThenStayOff, 10.0f);

	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3007), addedScore);
	info_text_box->Display(Buffer, 2.0); // "Fire Ridge\n%ld"
}

// ===========================================================================
// Ramps, chutes and gates
// ===========================================================================

// 0x004265e0 -- a_ramp103, the dragon ramp.
//
// Pays an escalating 50,000 x (rung + 1) and puts the same amount into the jackpot, so a
// held combo compounds twice. lite67 is the combo light: if it has gone out the rung
// resets to 0, otherwise it climbs, capped at 9 (i.e. 500,000 a pass at the top).
//
// Dragon Hoard (4) suppresses both the sound and the score because the quest controller
// pays the Amethyst gem for this ramp instead -- control::handler runs MissionControl
// *before* the component handler on this table, which is why so many Dragon handlers can
// simply opt out by mode.
//
// The lite125/lite126 + diverter-D block is the shared "route the lane to the ramp" idiom;
// DragonChuteControl below runs it inverted, and a_sink10's handler repeats it verbatim.
// Rescue Maiden (6) and Tower Barrage (0xd) skip it because they own the diverter.
void control::DragonRampControl(MC code, TPinballComponent* caller)
{
	if (code != FtMessage::RampTraversed)
		return;

	const int quest = info_text_box->MessageField;

	if (quest != Quest::DragonHoard)
		soundwave_tabc18->Play(caller, "DragonRamp");

	if (!lite67->light_on())
		caller->MessageField = 0;
	else if (caller->MessageField < 9)
		caller->MessageField++;

	lite67->Message(MC::TLightTurnOn, 0.0);
	lite67->Message(MC::TLightFlasherStartTimedThenStayOff, 20.0f);

	if (quest != Quest::RescueMaiden && quest != Quest::TowerBarrage)
	{
		lite125->Message(MC::TLightResetAndTurnOff, 0.0);
		lite126->Message(MC::TLightFlasherStartTimedThenStayOn, 1.0f);
		a_dvtrda->Message(MC::TGateEnable, 0.0);
		a_dvtrdb->Message(MC::TGateDisable, 0.0);
	}

	lite119->Message(MC::TLightFlasherStartTimed, 2.0f);

	if (quest != Quest::DragonHoard)
	{
		const int amount = (caller->MessageField + 1) * 50000;
		AddScoreAmount(caller, amount);
		AddJackpot(amount);
	}

	if (quest == Quest::NormalPlay)
	{
		lite24->Message(MC::TLightFlasherStartTimedThenStayOn, 1.0f);
		// DragonSpelled() starts Dragon Flurry when all six letters are lit; the jackpot
		// line would be stepping on the mode announcement, so it is suppressed.
		if (!DragonSpelled())
			info_text_box->Display(pb::get_rc_string(Msg::STRING3006), 2.0); // "Jackpot Grows"
	}
}

// 0x00426740 -- a_ramp6, the tower ramp.
//
// Carries no score of its own. Its combo light lite70 is the arming condition for a free
// spell: a pass with the light already lit casts one, a pass with it dark only acks. Either
// way the light is relit for another 10 s window.
//
// In normal play it also advances a D-R-A-G-O-N letter first, and bails out entirely if
// that completed the word -- Dragon Flurry has just taken the table over.
void control::TowerRampControl(MC code, TPinballComponent* caller)
{
	if (code != FtMessage::RampTraversed)
		return;

	if (info_text_box->MessageField == Quest::NormalPlay)
	{
		lite26->Message(MC::TLightFlasherStartTimedThenStayOn, 1.0f);
		if (DragonSpelled())
		{
			lite70->Message(MC::TLightTurnOn, 0.0);
			lite70->Message(MC::TLightFlasherStartTimedThenStayOff, 10.0f);
			return;
		}
	}

	if (lite70->light_on())
		CastRandomSpell();
	else
		soundwave_tabc09->Play(caller, "TowerRamp");

	lite70->Message(MC::TLightTurnOn, 0.0);
	lite70->Message(MC::TLightFlasherStartTimedThenStayOff, 10.0f);
}

// 0x004267f0 -- s_oneway5, the dragon chute.
//
// A five-rung ladder (25,000 / 75,000 / 100,000 / 125,000 / 150,000) on lite68, plus a flat
// 200,000 into the *super* jackpot every pass. The super-jackpot add is inlined here rather
// than calling AddSuperJackpot, but with the identical 20,000,000 clamp.
//
// Dragon Hoard (4) returns before anything: the quest controller awards this chute's gem
// and the jackpot instead. Normal play flips diverter D the opposite way to the dragon
// ramp, so the two shots fight over the same lane.
void control::DragonChuteControl(MC code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code == MC::ControlTimerExpired)
	{
		lite68->MessageField = 0; // combo window elapsed
		return;
	}
	if (code != MC::ControlCollision)
		return;

	const int quest = info_text_box->MessageField;
	if (quest == Quest::NormalPlay)
	{
		lite125->Message(MC::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite126->Message(MC::TLightResetAndTurnOff, 0.0);
		a_dvtrda->Message(MC::TGateDisable, 0.0);
		a_dvtrdb->Message(MC::TGateEnable, 0.0);
	}
	else if (quest == Quest::DragonHoard)
		return;

	soundwave_tabc20->Play(caller, "DragonChute");
	AddScoreFrom(caller, lite68->MessageField);
	if (lite68->MessageField < 4)
		lite68->MessageField++;

	TableG->SuperJackpotScore += 200000;
	if (TableG->SuperJackpotScore > 20000000)
		TableG->SuperJackpotScore = 20000000;

	// UNVERIFIED: the sprintf argument -- lost with the float args. The string is
	// "Super Jackpot Increased\n%ld" and the super jackpot was just changed, so the new
	// total is the only sensible operand; the 200,000 delta is the alternative.
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3076), TableG->SuperJackpotScore);
	info_text_box->Display(Buffer, 2.0);

	lite68->Message(MC::TLightTurnOn, 0.0);
	lite68->Message(MC::TLightFlasherStartTimedThenStayOff, 10.0f);
}

// 0x00426940 -- a_accl1, the moat chute accelerator.
//
// Two scores, selected by whether the Stealth spell light is lit: 75,000 normally,
// 500,000 with Stealth. Structurally identical to upstream's FlagControl, which indexes
// get_scoring with lite20->light_on() the same way. Dragon Hoard pays the Opal gem instead.
void control::MoatChuteAcceleratorControl(MC code, TPinballComponent* caller)
{
	if (code != MC::ControlCollision || info_text_box->MessageField == Quest::DragonHoard)
		return;

	soundwave_tabc22->Play(caller, "MoatChuteAccelerator");
	AddScoreFrom(caller, lite32->light_on() ? 1 : 0);
}

// 0x00426be0 -- s_rexit19, the gargoyle ramp exit (2 dragon eggs).
// 0x00426c20 -- s_rexit18, the forest ramp exit (1 dragon egg).
//
// Fire on RampExited, not on collision, so the eggs are paid once per completed traversal.
// Each also restores its own diverter pair, but only in normal play -- during a quest the
// controller owns the routing.
void control::GargoyleRampExitControl(MC code, TPinballComponent* caller)
{
	if (code != FtMessage::RampExited)
		return;

	AwardDragonEggs(2);
	if (info_text_box->MessageField == Quest::NormalPlay)
	{
		a_dvtrca->Message(MC::TGateEnable, 0.0);
		a_dvtrcb->Message(MC::TGateDisable, 0.0);
	}
}

void control::ForestRampExitControl(MC code, TPinballComponent* caller)
{
	if (code != FtMessage::RampExited)
		return;

	AwardDragonEggs(1);
	if (info_text_box->MessageField == Quest::NormalPlay)
	{
		a_dvtrba->Message(MC::TGateEnable, 0.0);
		a_dvtrbb->Message(MC::TGateDisable, 0.0);
	}
}

// 0x00426c60 -- a_flip2, the right flipper.
//
// Not a scoring handler at all: it is the release button for the a_kout3 holding kickout,
// which Dragon Blood arms. Code 1 is TFlipperExtend (the private TFlipper numbering is
// unshifted, like TLight's), so this runs the moment the flipper is raised, not on the
// public input message.
//
// During Slay Dragon the two spell lights are restarted with TLightFlasherStartTimed,
// which leaves them lit and therefore leaves the kickout armed for the rest of the finale;
// outside it they flash once and go out, consuming the spell.
void control::RightFlipperControl(MC code, TPinballComponent* caller)
{
	if (code != MC::TFlipperExtend || a_kout3->MessageField != 1)
		return;

	const int quest = info_text_box->MessageField;
	a_kout3->MessageField = 0;

	if (quest == Quest::SlayDragon)
	{
		// UNVERIFIED: lite29's duration. Ghidra dropped the value on this one call only;
		// 1.0f is taken from lite30 on the next line, which is flashed in lock-step with
		// lite29 everywhere else in the binary.
		lite29->Message(MC::TLightFlasherStartTimed, 1.0f);
		lite30->Message(MC::TLightFlasherStartTimed, 1.0f);
	}
	else
	{
		lite29->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
		lite30->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	a_kout3->Message(MC::TKickoutRestartTimer, 0.0);
}

// ===========================================================================
// Trip wires -- the three tower window lanes
// ===========================================================================
//
// s_trip6/5, s_trip3/4 and s_trip8/9 are three identically-shaped handlers, one per tower,
// each owning its own window light group. The pairing
//
//   prison tower -> lite48 / prison_window_light_group / s_trip6, s_trip5
//   watch tower  -> lite49 / watch_window_light_group  / s_trip3, s_trip4
//   gate tower   -> lite50 / gate_window_light_group   / s_trip8, s_trip9
//
// upgrades dragon-quest-system.md §1.1's [P] on "v_lite48/49/50 tower-lock bookkeeping for
// prison/watch/gate" to certain: each handler reads exactly one of the three.
//
// The common tail is a bank: score by the group's on-count, light the next window, and when
// the hit would fill the bank clear it and flash it off instead. The lock lights'
// MessageField is the "this tower is armed to swallow a ball" flag, and a trip wire stands
// down while it is set so the sink can lock instead.

// 0x00427880 -- s_trip6 / s_trip5, the prison tower windows (25k/50k/75k/100k).
//
// Also where Rescue Maiden's per-hit awards live. dragon-quest-system.md §3 state 6 lists
// them as [UNRESOLVED] and blames the unusable decompile of a_sink18 (0x427100); they are
// in fact right here and perfectly legible -- 250,000, 500,000, the jackpot, then the
// extra-ball light with string 3034 "Maiden Rescued / Extra Ball Available", exactly the
// manual's ladder. a_sink18 is the lock sink, not the award site.
void control::PrisonWindowTripControl(MC code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MC::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case Quest::NormalPlay:
		if (lite48->MessageField)
			return; // armed for a ball lock -- let a_sink18 have it
		soundwave_tabc04->Play(caller, "PrisonWindowTrip");
		break;

	case Quest::RestartWindow:
		// Only the tower whose quest just ended keeps paying during the 10 s restart
		// window; mode_timer->MessageField holds that quest's index.
		if (mode_timer->MessageField == Quest::RescueMaiden)
		{
			const int addedScore = AddScoreFrom(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
			info_text_box->Display(Buffer, 2.0);
			return;
		}
		break;

	case Quest::RescueMaiden:
		{
			soundwave_tabc51->Play(caller, "MaidenRescueHit");
			const int lit = prison_window_light_group->Message(MC::TLightGroupGetOnCount, 0.0);
			if (lit == 0)
			{
				const int addedScore = AddScoreAmount(caller, 250000);
				snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
				info_text_box->Display(Buffer, 2.0);
			}
			else if (lit == 1)
			{
				const int addedScore = AddScoreAmount(caller, 500000);
				snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
				info_text_box->Display(Buffer, 2.0);
			}
			else if (lit == 2)
			{
				CollectJackpot(0.0, true);
			}
			else
			{
				lite57->Message(MC::TLightFlasherStartTimedThenStayOn, 1.0f);
				info_text_box->Display(pb::get_rc_string(Msg::STRING3034), 2.0);
			}
			// Lighting the fourth window is what the quest controller watches for, so it
			// happens on every hit, award or not.
			prison_window_light_group->Message(MC::TLightGroupResetAndTurnOn, 1.0f);
			return;
		}

	case Quest::TowerBarrage:
		AddScoreAmount(caller, 100000); // flat rate while the tower is spitting balls
		return;

	default:
		break;
	}

	const int lit = prison_window_light_group->Message(MC::TLightGroupGetOnCount, 0.0);
	const int addedScore = AddScoreFrom(caller, lit);
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
	info_text_box->Display(Buffer, 2.0);

	if (prison_window_light_group->Message(MC::TLightGroupGetLightCount, 0.0) - lit == 1)
	{
		// This hit fills the bank. Code 0 on a group is TLightGroupNull, which TLightGroup
		// forwards to TLight::TLightTurnOff, so the whole group goes dark and then flashes
		// out -- the bank resets rather than sitting full.
		prison_window_light_group->Message(MC::TLightTurnOff, 0.0);
		prison_window_light_group->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	prison_window_light_group->Message(MC::TLightGroupResetAndTurnOn, 1.0f);
}

// 0x00427b30 -- s_trip3 / s_trip4, the watch tower windows (40k/80k/120k).
//
// Same bank, three lights. Note the sound placement differs from the prison tower: here
// tabc04 plays on every scoring path, there only on the normal-play one. Transcribed as
// found rather than regularised.
void control::WatchWindowTripControl(MC code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MC::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case Quest::NormalPlay:
		if (lite49->MessageField)
			return;
		break;

	case Quest::RestartWindow:
		if (mode_timer->MessageField == Quest::RescueMaiden)
		{
			const int addedScore = AddScoreFrom(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
			info_text_box->Display(Buffer, 2.0);
			return;
		}
		break;

	case Quest::RescueMaiden:
		// Only the prison tower advances the maiden; the other two just pay the base.
		{
			soundwave_tabc51->Play(caller, "WatchWindowTrip");
			const int addedScore = AddScoreFrom(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
			info_text_box->Display(Buffer, 2.0);
			return;
		}

	case Quest::TowerBarrage:
		AddScoreAmount(caller, 100000);
		return;

	default:
		break;
	}

	soundwave_tabc04->Play(caller, "WatchWindowTrip");
	const int lit = watch_window_light_group->Message(MC::TLightGroupGetOnCount, 0.0);
	const int addedScore = AddScoreFrom(caller, lit);
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
	info_text_box->Display(Buffer, 2.0);

	if (watch_window_light_group->Message(MC::TLightGroupGetLightCount, 0.0) - lit == 1)
	{
		watch_window_light_group->Message(MC::TLightTurnOff, 0.0);
		watch_window_light_group->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	watch_window_light_group->Message(MC::TLightGroupResetAndTurnOn, 1.0f);
}

// 0x00427d10 -- s_trip8 / s_trip9, the gate tower windows (50k/100k).
//
// Same bank, two lights, and no sound at all on the normal-play path.
void control::GateWindowTripControl(MC code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MC::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case Quest::NormalPlay:
		if (lite50->MessageField)
			return;
		break;

	case Quest::RestartWindow:
		if (mode_timer->MessageField == Quest::RescueMaiden)
		{
			const int addedScore = AddScoreFrom(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
			info_text_box->Display(Buffer, 2.0);
			return;
		}
		break;

	case Quest::RescueMaiden:
		{
			soundwave_tabc51->Play(caller, "GateWindowTrip");
			const int addedScore = AddScoreFrom(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
			info_text_box->Display(Buffer, 2.0);
			return;
		}

	case Quest::TowerBarrage:
		AddScoreAmount(caller, 100000);
		return;

	default:
		break;
	}

	const int lit = gate_window_light_group->Message(MC::TLightGroupGetOnCount, 0.0);
	const int addedScore = AddScoreFrom(caller, lit);
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(Msg::STRING3008), addedScore);
	info_text_box->Display(Buffer, 2.0);

	if (gate_window_light_group->Message(MC::TLightGroupGetLightCount, 0.0) - lit == 1)
	{
		gate_window_light_group->Message(MC::TLightTurnOff, 0.0);
		gate_window_light_group->Message(MC::TLightFlasherStartTimedThenStayOff, 1.0f);
		return;
	}
	gate_window_light_group->Message(MC::TLightGroupResetAndTurnOn, 1.0f);
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
//    duration to TBlockerEnable, and
//  * FT treats a negative TBlockerRestartTimeout as "cancel". Upstream's TBlocker clamps
//    it with std::max(value, 0.0f), which would fire the timer immediately instead. The
//    reading is forced by the rest of the binary: 0x42a150 cancels mode_timer with
//    Message(0x45, -1.0f) and plainly means to stop it, not to trip it. A port must take
//    the FT semantics or a_blocf's cancel path (and the mode timer) misbehaves.
//
// The two durations are the blocker's own InitialDuration / ExtendedDuration fields (the
// original reads them at +0x86 / +0x8a and casts int to float, exactly as upstream does),
// so the armor timings are table data, not rules code.

// 0x00425910 -- a_center_post, the Helm of Terror center post.
void control::CenterPostControl(MC code, TPinballComponent* caller)
{
	auto block = static_cast<TBlocker*>(caller);

	switch (code)
	{
	case MC::TBlockerDisable:
		// Forced down -- drain, tilt, or the armor being cleared.
		block->MessageField = 0;
		block->Message(MC::TBlockerDisable, 0.0);
		lite38->Message(MC::TLightResetAndTurnOff, 0.0);
		break;

	case MC::TBlockerEnable:
		block->MessageField = 1;
		block->Message(MC::TBlockerEnable, 0.0);
		block->Message(MC::TBlockerRestartTimeout, static_cast<float>(block->InitialDuration));
		lite38->Message(MC::TLightFlasherStartTimedThenStayOn, 2.0f);
		break;

	case MC::ControlTimerExpired:
		if (block->MessageField == 1)
		{
			// Grace period: the post stays up but the light flashes it out, so the player
			// can see the armor expiring.
			const auto extended = static_cast<float>(block->ExtendedDuration);
			block->MessageField = 2;
			block->Message(MC::TBlockerRestartTimeout, extended);
			lite38->Message(MC::TLightFlasherStartTimedThenStayOff, extended);
		}
		else
		{
			block->MessageField = 0;
			block->Message(MC::TBlockerDisable, 0.0);
		}
		break;

	default:
		break;
	}
}

// 0x00425a10 -- a_bloce, the Chain mail right-outlane blocker.
//
// Byte-identical to a_center_post except for the light; two functions in the original.
void control::RightOutlaneBlockerControl(MC code, TPinballComponent* caller)
{
	auto block = static_cast<TBlocker*>(caller);

	switch (code)
	{
	case MC::TBlockerDisable:
		block->MessageField = 0;
		block->Message(MC::TBlockerDisable, 0.0);
		lite39->Message(MC::TLightResetAndTurnOff, 0.0);
		break;

	case MC::TBlockerEnable:
		block->MessageField = 1;
		block->Message(MC::TBlockerEnable, 0.0);
		block->Message(MC::TBlockerRestartTimeout, static_cast<float>(block->InitialDuration));
		lite39->Message(MC::TLightFlasherStartTimedThenStayOn, 2.0f);
		break;

	case MC::ControlTimerExpired:
		if (block->MessageField == 1)
		{
			const auto extended = static_cast<float>(block->ExtendedDuration);
			block->MessageField = 2;
			block->Message(MC::TBlockerRestartTimeout, extended);
			lite39->Message(MC::TLightFlasherStartTimedThenStayOff, extended);
		}
		else
		{
			block->MessageField = 0;
			block->Message(MC::TBlockerDisable, 0.0);
		}
		break;

	default:
		break;
	}
}

// 0x00425b10 -- a_blocf, the moat blocker (the Stealth spell).
//
// Same machine with inverted polarity, because Stealth works by *removing* this blocker:
// TBlockerDisable is the award and TBlockerEnable is the cancel, and the timed stages put
// the blocker back rather than taking it away. lite32 is the spell light, driven from here
// rather than from the spell code -- which is why dragon-quest-system.md §4.3 lists slot 3
// as "a_blocf's own handler drives v_lite32".
void control::MoatBlockerControl(MC code, TPinballComponent* caller)
{
	auto block = static_cast<TBlocker*>(caller);

	switch (code)
	{
	case MC::TBlockerDisable:
		{
			// Stealth cast: open the moat path for InitialDuration and light the spell.
			const auto initial = static_cast<float>(block->InitialDuration);
			block->MessageField = 1;
			block->Message(MC::TBlockerDisable, 0.0);
			block->Message(MC::TBlockerRestartTimeout, initial);
			lite32->Message(MC::TLightTurnOnTimed, initial);
			break;
		}

	case MC::TBlockerEnable:
		// Cancel (drain, or the end of Slay Dragon): kill the timer, drop the light, close.
		block->Message(MC::TBlockerRestartTimeout, -1.0f);
		lite32->Message(MC::TLightResetAndTurnOff, 0.0);
		block->MessageField = 0;
		block->Message(MC::TBlockerEnable, 0.0);
		break;

	case MC::ControlTimerExpired:
		if (block->MessageField == 1)
		{
			const auto extended = static_cast<float>(block->ExtendedDuration);
			block->MessageField = 2;
			block->Message(MC::TBlockerRestartTimeout, extended);
			lite32->Message(MC::TLightFlasherStartTimed, extended);
		}
		else
		{
			block->MessageField = 0;
			block->Message(MC::TBlockerEnable, 0.0);
		}
		break;

	default:
		break;
	}
}

// 0x00423810 -- a_rocka, the rock in front of the a_spot10 gargoyle target.
// 0x00423840 -- a_rockb, the rock in front of a_spot14.
//
// The rocks are TBlockers with a rock sprite. Hitting the target slides its rock open
// (TBlockerDisable) and disables the target; when the rock finishes sliding back the
// target is re-armed, which is all these two handlers do. 0x36 is TSoloTargetEnable
// (upstream 50, target band +4): derived, but confirmed by a_spot10's own handler at
// 0x4254a0, which sends 0x35 -- TSoloTargetDisable, upstream 49 -- to itself on the way in.
//
// UNVERIFIED: both the FtMessage::BlockerMotionDone name and the guard field. The original
// tests an int at +0x7e (a TBlocker field, since a_center_post's durations sit at +0x86 /
// +0x8a) against -1 and does nothing when it matches. The reading taken here is "only
// re-arm the target if this rock closes on its own": Dragon Pass holds both rocks open
// indefinitely and must not get its targets back. Disassembling FT's TBlocker::Message /
// its timer callback, or reading the blocker records in DRAGON.DAT, would settle both.
void control::GargoyleRockAControl(MC code, TPinballComponent* caller)
{
	if (code != FtMessage::BlockerMotionDone)
		return;
	if (static_cast<TBlocker*>(caller)->UnknownP7e == -1)
		return;

	a_spot10->Message(MC::TSoloTargetEnable, 0.0);
}

void control::GargoyleRockBControl(MC code, TPinballComponent* caller)
{
	if (code != FtMessage::BlockerMotionDone)
		return;
	if (static_cast<TBlocker*>(caller)->UnknownP7e == -1)
		return;

	a_spot14->Message(MC::TSoloTargetEnable, 0.0);
}
