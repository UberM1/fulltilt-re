// dragon_part1.cpp -- Dragon's Keep scoring rules, part 1: bumpers, rebounders and
// the eighteen spot targets.
//
// Reimplemented from DRAGON.EXE (win95 build) via decomp/dragon/functions/*.c, in the
// shape of SpaceCadetPinball's control.cpp. Bodies here replace the generated stubs in
// src/control_dragon.cpp; the mapping from this file's descriptive names back to that
// skeleton's tag-derived names is in the comment above each handler, next to the
// original address.
//
// Message codes: Full Tilt's MessageCode numbering is Space Cadet's with a piecewise
// offset -- TLight codes identical, the TLightGroup/target/blocker block +4, the
// gate/kickout/sink block +9, and only the Control* block +10 (+11 from ControlCollision
// on, since FT inserted a code between ControlTimerExpired and ControlCollision). Codes
// 0x4b-0x4f therefore sit above upstream's enum and are named Ft* below.
//
// Nothing here links yet: the per-table control interface (a control class per table,
// its own component tag table and string ids) does not exist in the port.

#include "pch.h"
#include "control.h"

#include <algorithm>

#include "pb.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace dragon {

// The port keeps one table pointer; a per-table control class would own its own.
TPinballTable*& TableG = control::TableG;

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

// Tags the generated skeleton does not declare (it lists only scoring components).
extern component_tag<TTextBox> tag_info_text_box;
component_tag<TLight> tag_v_lite3 = {"v_lite3"};
component_tag<TLight> tag_v_lite4 = {"v_lite4"};
component_tag<TLight> tag_v_lite5 = {"v_lite5"};
component_tag<TLight> tag_v_lite6 = {"v_lite6"};
component_tag<TLight> tag_v_lite7 = {"v_lite7"};
component_tag<TLight> tag_v_lite8 = {"v_lite8"};
component_tag<TLight> tag_v_lite9 = {"v_lite9"};
component_tag<TLight> tag_v_lite10 = {"v_lite10"};
component_tag<TLight> tag_v_lite11 = {"v_lite11"};
component_tag<TLight> tag_v_lite12 = {"v_lite12"};
component_tag<TLight> tag_v_lite13 = {"v_lite13"};
extern component_tag<TLight> tag_v_lite14;
extern component_tag<TLight> tag_v_lite16;
extern component_tag<TLight> tag_v_lite17;
extern component_tag<TLight> tag_v_lite18;
component_tag<TLight> tag_v_lite19 = {"v_lite19"};
component_tag<TLight> tag_v_lite20 = {"v_lite20"};
component_tag<TLight> tag_v_lite21 = {"v_lite21"};
extern component_tag<TLight> tag_v_lite23;
extern component_tag<TLight> tag_v_lite25;
extern component_tag<TLight> tag_v_lite27;
extern component_tag<TLight> tag_v_lite45;
extern component_tag<TLight> tag_v_lite46;
component_tag<TLight> tag_v_lite55 = {"v_lite55"};
component_tag<TLight> tag_v_lite56 = {"v_lite56"};
extern component_tag<TLight> tag_v_lite57;
extern component_tag<TLight> tag_v_lite65;
extern component_tag<TLight> tag_v_lite66;
extern component_tag<TLight> tag_v_lite76;
component_tag<TLight> tag_v_lite120 = {"v_lite120"};
component_tag<TLight> tag_v_lite121 = {"v_lite121"};
extern component_tag<TLightGroup> tag_dragon_fire_light_group;
extern component_tag<TPinballComponent> tag_a_dvtrba;
extern component_tag<TPinballComponent> tag_a_dvtrbb;
extern component_tag<TPinballComponent> tag_a_dvtrca;
extern component_tag<TPinballComponent> tag_a_dvtrcb;
component_tag<TSound> tag_tabc03wave = {"tabc03wave"};
component_tag<TSound> tag_tabc05wave = {"tabc05wave"};
component_tag<TSound> tag_tabc06wave = {"tabc06wave"};
component_tag<TSound> tag_tabc07wave = {"tabc07wave"};
component_tag<TSound> tag_tabc08wave = {"tabc08wave"};
extern component_tag<TSound> tag_tabc21wave;
component_tag<TSound> tag_tabc96wave = {"tabc96wave"};

// Scoring components: tags live in control_dragon.cpp.
extern component_tag<TPinballComponent> tag_a_bump1;
extern component_tag<TPinballComponent> tag_a_bump3;
extern component_tag<TPinballComponent> tag_a_bump4;
extern component_tag<TPinballComponent> tag_a_spot10;
extern component_tag<TPinballComponent> tag_a_spot14;
extern component_tag<TPinballComponent> tag_a_sink4;
extern component_tag<TPinballComponent> tag_a_rocka;
extern component_tag<TPinballComponent> tag_a_rockb;

TTextBox*& info_text_box = tag_info_text_box.Component;
TLight*& lite3 = tag_v_lite3.Component;
TLight*& lite4 = tag_v_lite4.Component;
TLight*& lite5 = tag_v_lite5.Component;
TLight*& lite6 = tag_v_lite6.Component;
TLight*& lite7 = tag_v_lite7.Component;
TLight*& lite8 = tag_v_lite8.Component;
TLight*& lite9 = tag_v_lite9.Component;
TLight*& lite10 = tag_v_lite10.Component;
TLight*& lite11 = tag_v_lite11.Component;
TLight*& lite12 = tag_v_lite12.Component;
TLight*& lite13 = tag_v_lite13.Component;
TLight*& lite14 = tag_v_lite14.Component;
TLight*& lite16 = tag_v_lite16.Component;
TLight*& lite17 = tag_v_lite17.Component;
TLight*& lite18 = tag_v_lite18.Component;
TLight*& lite19 = tag_v_lite19.Component;
TLight*& lite20 = tag_v_lite20.Component;
TLight*& lite21 = tag_v_lite21.Component;
TLight*& lite23 = tag_v_lite23.Component;
TLight*& lite25 = tag_v_lite25.Component;
TLight*& lite27 = tag_v_lite27.Component;
TLight*& lite45 = tag_v_lite45.Component;
TLight*& lite46 = tag_v_lite46.Component;
TLight*& lite55 = tag_v_lite55.Component;
TLight*& lite56 = tag_v_lite56.Component;
TLight*& lite57 = tag_v_lite57.Component;
TLight*& lite65 = tag_v_lite65.Component;
TLight*& lite66 = tag_v_lite66.Component;
TLight*& lite76 = tag_v_lite76.Component;
TLight*& lite120 = tag_v_lite120.Component;
TLight*& lite121 = tag_v_lite121.Component;
TLightGroup*& dragon_fire_lights = tag_dragon_fire_light_group.Component;
TPinballComponent*& dvtrBA = tag_a_dvtrba.Component;
TPinballComponent*& dvtrBB = tag_a_dvtrbb.Component;
TPinballComponent*& dvtrCA = tag_a_dvtrca.Component;
TPinballComponent*& dvtrCB = tag_a_dvtrcb.Component;
TPinballComponent*& bump1 = tag_a_bump1.Component;
TPinballComponent*& bump3 = tag_a_bump3.Component;
TPinballComponent*& bump4 = tag_a_bump4.Component;
TPinballComponent*& spot10 = tag_a_spot10.Component;
TPinballComponent*& spot14 = tag_a_spot14.Component;
TPinballComponent*& well_sink = tag_a_sink4.Component;
TPinballComponent*& rockA = tag_a_rocka.Component;
TPinballComponent*& rockB = tag_a_rockb.Component;
TSound*& tabc03wave = tag_tabc03wave.Component;
TSound*& tabc05wave = tag_tabc05wave.Component;
TSound*& tabc06wave = tag_tabc06wave.Component;
TSound*& tabc07wave = tag_tabc07wave.Component;
TSound*& tabc08wave = tag_tabc08wave.Component;
TSound*& tabc21wave = tag_tabc21wave.Component;
TSound*& tabc96wave = tag_tabc96wave.Component;

// ---------------------------------------------------------------------------
// Table-local message codes and string ids
// ---------------------------------------------------------------------------

// FT-only codes above upstream's enum; see the header note.
namespace FtMessageCode {
// Sent to the component that was scoring before this one, and the name is right:
// 0x4230a0 is `if (last != caller) { if (last) ComponentHandler(0x4d, last); last =
// caller; } return TableG->AddScore(caller->get_scoring(i));`.
//
// It never reaches a component's Message() -- ComponentHandler (0x423060) routes it to
// the recipient's ControlFunc, which is why no TLight/TBumper/TSoloTarget needing a
// case for it was ever the question. Dragon has no handler that wants it; PIRATES.EXE
// has four (0x428b70, 0x428d80, 0x429210, 0x429400), each dropping its own indicator
// light when it stops being the current scorer. So it is a live rules notification, not
// a dead code, and a port must deliver it.
constexpr auto ScoringComponentReplaced = static_cast<MessageCode>(0x4d);
// Broadcast whenever the mode register changes. Every mode-aware Dragon handler has a
// case for it, so it reads as "mode started" -- upstream's ControlMissionStarted plus
// one FT-only slot.
constexpr auto ModeStarted = static_cast<MessageCode>(0x4f);
}

// Mode register values of info_text_box->MessageField; see analysis/dragon-quest-system.md.
constexpr int ModeNormal = 2;
// No handler writes 4 as a literal: the quest modes are copied wholesale out of
// a_kout2's MessageField (0x424210) or mode_timer's (the fire lizard sinks), where
// SelectNextQuest left the chosen quest. 4 is Dragon Hoard because MissionControl's
// switch at 0x42c120 sends case 4 to 0x428a80, the gem controller.
constexpr int ModeDragonHoard = 4;
constexpr int ModeDragonPass = 7;
constexpr int ModeWizardsFury = 8;
constexpr int ModeSlayDragon = 9;

// Raw DRAGON.EXE resource ids: Dragon has its own string table, so the ids are passed
// through rather than named in the port's Msg enum.
inline Msg DragonMsg(int rcId) { return static_cast<Msg>(rcId); }

constexpr int Str_TowerLockEnabled = 3003; // 0xbbb
constexpr int Str_PassClear = 3004;        // 0xbbc
constexpr int Str_ExtraBall = 3011;        // 0xbc3
constexpr int Str_MysteryAward0 = 3039;    // 0xbdf, +0..4 in a_spot4/5/6/7/22 order
constexpr int Str_QuestName4 = 3071;       // 0xbff, +0..4 for quest ids 4..8

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------

void AwardWeapon();            // 0x00423330
bool CheckDragonFlurry();          // 0x004244e0, true when it started Dragon Flurry
void CastRandomSpell();            // 0x004237f0
void EnableTowerLock();            // 0x00423d50
int SelectNextQuest();             // 0x00423e90, 4..8, or 9 when none are left
void UnlightSelectedQuest();    // 0x00423e50
void SetSwordStage(int stage);     // 0x004238d0
void SendToComponentAndHandler(MessageCode code, TPinballComponent* cmp); // 0x00423060
void MissionHandler(MessageCode code, TPinballComponent* cmp);          // 0x0042c020

// ---------------------------------------------------------------------------
// Scoring plumbing
// ---------------------------------------------------------------------------

// Dragon tracks which component scored last (cleared on ball launch by s_plunger1's
// handler) and notifies the outgoing one. Upstream has no equivalent.
static TPinballComponent* LastScoringComp = nullptr; // DAT_00434d20

static void NoteScoringComponent(TPinballComponent* caller)
{
	if (LastScoringComp == caller)
		return;
	if (LastScoringComp)
		SendToComponentAndHandler(FtMessageCode::ScoringComponentReplaced, LastScoringComp);
	LastScoringComp = caller;
}

// 0x004230a0 -- the workhorse: score this component's scoring[index].
//
// Returns the amount actually credited, after the multiplier and the bonus/jackpot
// flags. Callers use it: every "...\n%ld" display in Dragon formats this return, not
// the nominal score.
int AddScoreFor(TPinballComponent* caller, int index)
{
	NoteScoringComponent(caller);
	return TableG->AddScore(caller->get_scoring(index));
}

// 0x004230f0 -- same, with a literal score instead of a scoring-table lookup. Goes
// through AddScore, so the weapon multiplier still applies.
int AddScoreAt(TPinballComponent* caller, int score)
{
	NoteScoringComponent(caller);
	return TableG->AddScore(score);
}

// 0x00423130 -- upstream's SpecialAddScore, except that Dragon forces the multiplier to
// 1 where upstream forces 0, and it takes the scoring component (null from the weapon
// award).
//
// It forces 1 rather than 0 because Full Tilt has no score_multipliers array: AddScore
// is `field * score + ScoreAdded`, so ScoreMultiplier holds the multiplier itself and
// its neutral value is 1. Upstream's 0 is a 3D Pinball-ism, where the field is an index.
int DragonSpecialAddScore(TPinballComponent* caller, int score)
{
	const auto bonus = TableG->BonusScoreFlag;
	const auto jackpot = TableG->JackpotScoreFlag;
	const auto scoreMult = TableG->ScoreMultiplier;

	NoteScoringComponent(caller);
	TableG->BonusScoreFlag = false;
	TableG->JackpotScoreFlag = false;
	TableG->ScoreMultiplier = 1;
	const int credited = TableG->AddScore(score);
	TableG->BonusScoreFlag = bonus;
	TableG->JackpotScoreFlag = jackpot;
	TableG->ScoreMultiplier = scoreMult;
	return credited;
}

// The DAT_00434d20 = 0 inside the plunger (VA 4245b0, whose handler is in
// dragon_part4.cpp): a new ball owes nothing to whatever scored last, so no 0x4d
// goes out and the next scorer is the first.
//
// The address is spelled without its 0x here on purpose: analysis/link_impls.py
// pairs an address in a comment with the next definition under it, and this is not
// that function.
void ClearLastScoringComponent()
{
	LastScoringComp = nullptr;
}

// 0x004238a0 -- the jackpot clamp is the one upstream's AddScore applies in FT mode.
void AddToJackpot(int score)
{
	TableG->JackpotScore = std::min(TableG->JackpotScore + score, 10000000);
}

// 0x004231e0 -- a negative time suppresses the announcement.
void AwardExtraBall(float time)
{
	// TPinballTable+0x18e is ExtraBalls, and the *assignment* is the Full Tilt
	// behaviour rather than a transcription slip: CADET.EXE's control::table_add_extra_ball
	// at 0x422fe0 -- pinned in analysis/symbols-cadet.md and matching upstream's body
	// line for line (soundwave18, then STRING142 at 0xbc2 displayed for the float
	// argument) -- opens with `TableG->[0x18e] = 1` where upstream has `++ExtraBalls`.
	TableG->ExtraBalls = 1;
	tabc21wave->Play(nullptr, "AwardExtraBall");
	if (time >= 0.0f)
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_ExtraBall)), time);
}

// ---------------------------------------------------------------------------
// Forest bumpers and rebounders
// ---------------------------------------------------------------------------

// 0x00424bc0 -- all four of a_bump1..a_bump4 (skeleton: a_bump1Control)
void ForestBumperControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	// In Dragon Hoard the bumpers pay gold out of the quest logic instead, so both the
	// score and the sound are skipped -- but the weapon counter keeps running.
	const bool dragonHoard = info_text_box->MessageField == ModeDragonHoard;

	if (!dragonHoard)
		tabc03wave->Play(caller, "ForestBumperControl");

	// Only the two rear bumpers have a companion light.
	if (caller == bump3)
		lite65->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	else if (caller == bump4)
		lite66->Message(MessageCode::TLightFlasherStartTimed, 0.5f);

	if (!dragonHoard)
		AddScoreFor(caller, 0); // 1500

	// One counter for all four bumpers, kept in a_bump1's MessageField. The manual says
	// a weapon every 8 hits; the binary compares against 10.
	if (++bump1->MessageField == 10)
	{
		bump1->MessageField = 0;
		AwardWeapon();
	}
}

// 0x00424cc0 -- a_rebo1 (skeleton: a_rebo1Control)
void FlipperRebounderControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite120->Message(MessageCode::TLightTurnOnTimed, 0.1f);
		AddScoreFor(caller, 0); // 1000
	}
}

// 0x00424cf0 -- a_rebo2 (skeleton: a_rebo2Control)
void FlipperRebounderControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite121->Message(MessageCode::TLightTurnOnTimed, 0.1f);
		AddScoreFor(caller, 0); // 1000
	}
}

// ---------------------------------------------------------------------------
// Spell Target Bank -- a_spot4/5/6/7/22, lights v_lite3..v_lite7
// ---------------------------------------------------------------------------

// 0x00424d20 -- shared tail of all five spell targets.
void SpellTargetBankHit(TPinballComponent* caller)
{
	const bool complete = lite3->light_on() && lite4->light_on() && lite5->light_on()
		&& lite6->light_on() && lite7->light_on();

	if (!complete)
	{
		if (info_text_box->MessageField != ModeDragonHoard)
		{
			AddScoreFor(caller, 0); // 2500
			tabc05wave->Play(caller, "SpellTargetBankHit");
		}
		return;
	}

	lite3->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite4->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite5->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite6->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite7->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);

	// The completing hit itself scores nothing: the bank has a single scoring entry and
	// the all-lit path never reaches it. The manual lists no completion bonus either.
	if (info_text_box->MessageField == ModeNormal)
	{
		lite23->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f); // dragon letter
		if (!CheckDragonFlurry())
			CastRandomSpell();
	}
	else
	{
		CastRandomSpell();
	}
}

// 0x00424e60 -- a_spot4 (skeleton: a_spot4Control)
void SpellTargetControl4(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeWizardsFury)
	{
		// Wizard's Fury re-arms the Well Sink's mystery award instead of scoring: the
		// ordinal picked here is what a_sink4 pays out later (0 = 1,000,000,
		// 1 = jackpot, 2 = max weapons, 3 = max armor, 4 = extra ball).
		well_sink->MessageField = 0;
		tabc05wave->Play(caller, "SpellTargetControl");
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MysteryAward0 + 0)), 2.0f);
		return;
	}

	lite3->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	SpellTargetBankHit(caller);
}

// 0x00424ee0 -- a_spot5 (skeleton: a_spot5Control)
void SpellTargetControl5(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeWizardsFury)
	{
		tabc05wave->Play(caller, "SpellTargetControl");
		well_sink->MessageField = 1;
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MysteryAward0 + 1)), 2.0f);
		return;
	}

	lite4->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	SpellTargetBankHit(caller);
}

// 0x00424f60 -- a_spot6 (skeleton: a_spot6Control)
void SpellTargetControl6(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeWizardsFury)
	{
		tabc05wave->Play(caller, "SpellTargetControl");
		well_sink->MessageField = 2;
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MysteryAward0 + 2)), 2.0f);
		return;
	}

	lite5->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	SpellTargetBankHit(caller);
}

// 0x00424fe0 -- a_spot7 (skeleton: a_spot7Control)
void SpellTargetControl7(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeWizardsFury)
	{
		tabc05wave->Play(caller, "SpellTargetControl");
		well_sink->MessageField = 3;
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MysteryAward0 + 3)), 2.0f);
		return;
	}

	lite6->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	SpellTargetBankHit(caller);
}

// 0x00425060 -- a_spot22 (skeleton: a_spot22Control)
void SpellTargetControl22(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeWizardsFury)
	{
		tabc05wave->Play(caller, "SpellTargetControl");
		well_sink->MessageField = 4;
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MysteryAward0 + 4)), 2.0f);
		return;
	}

	lite7->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	SpellTargetBankHit(caller);
}

// ---------------------------------------------------------------------------
// Tower Target Bank -- a_spot3/2/1, lights v_lite8/v_lite9/v_lite10
// ---------------------------------------------------------------------------

// 0x004250e0 -- shared tail of the three tower targets.
void TowerTargetBankHit(TPinballComponent* caller)
{
	const bool complete = lite8->light_on() && lite9->light_on() && lite10->light_on();

	if (!complete)
	{
		if (info_text_box->MessageField != ModeDragonHoard)
		{
			AddScoreFor(caller, 0); // 2000
			tabc06wave->Play(caller, "TowerTargetBankHit");
		}
		return;
	}

	lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	AddScoreFor(caller, 1); // 10000, awarded even in Dragon Hoard

	if (info_text_box->MessageField == ModeNormal)
	{
		lite27->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f); // dragon letter
		if (!CheckDragonFlurry())
		{
			EnableTowerLock();
			tabc07wave->Play(caller, "TowerTargetBankHit");
			info_text_box->Display(pb::get_rc_string(DragonMsg(Str_TowerLockEnabled)), 2.0f);
		}
	}

	if (info_text_box->MessageField != ModeDragonHoard)
		tabc06wave->Play(caller, "TowerTargetBankHit");
}

// 0x00425220 -- a_spot3 (skeleton: a_spot3Control)
void TowerTargetControl3(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TowerTargetBankHit(caller);
	}
}

// 0x00425250 -- a_spot2 (skeleton: a_spot2Control)
void TowerTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TowerTargetBankHit(caller);
	}
}

// 0x00425280 -- a_spot1 (skeleton: a_spot1Control)
void TowerTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TowerTargetBankHit(caller);
	}
}

// ---------------------------------------------------------------------------
// Gargoyle Target Bank -- a_spot10/8/14/12, lights v_lite14/13/12/11
// ---------------------------------------------------------------------------

// 0x004252b0 -- shared tail of the four gargoyle targets.
void GargoyleTargetBankHit(TPinballComponent* caller)
{
	const bool complete = lite14->light_on() && lite13->light_on()
		&& lite12->light_on() && lite11->light_on();
	const bool dragonHoard = info_text_box->MessageField == ModeDragonHoard;

	if (!complete)
	{
		if (!dragonHoard)
		{
			tabc08wave->Play(caller, "GargoyleTargetBankHit");
			AddScoreFor(caller, 0); // 3000
		}
		return;
	}

	lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);

	// Both diverter pairs flip to open the pass, and both rocks slide back out.
	//
	// Which of the pair is open is settled by the recovered TDiverter (object type 1043,
	// reference/SpaceCadetPinball/TDiverter.h): the edges it installs are gated on
	// &ActiveFlag, so Enable puts the wall into the world and closes that chute while
	// Disable takes it out and opens one. A diverter answers the gate pair as well as the
	// blocker pair and the magnets' 0x4b/0x4c, and Dragon drives it with the gate pair --
	// so 0x3e/0x3f really are TGateDisable/TGateEnable here. Which physical chute each
	// half of a pair covers still needs the geometry out of DRAGON.DAT.
	dvtrBA->Message(MessageCode::TGateDisable, 0.0);
	dvtrBB->Message(MessageCode::TGateEnable, 0.0);
	dvtrCA->Message(MessageCode::TGateDisable, 0.0);
	dvtrCB->Message(MessageCode::TGateEnable, 0.0);
	lite56->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	lite55->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	rockB->Message(MessageCode::TBlockerEnable, 0.0);
	lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	rockA->Message(MessageCode::TBlockerEnable, 0.0);

	bool flurry = false;
	if (info_text_box->MessageField == ModeNormal)
	{
		lite25->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f); // dragon letter
		flurry = CheckDragonFlurry();
	}
	if (!flurry)
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_PassClear)), 2.0f);

	if (!dragonHoard)
	{
		tabc08wave->Play(caller, "GargoyleTargetBankHit");
		AddScoreFor(caller, 1); // 15000
	}
}

// 0x004254a0 -- a_spot10, mounted on rock A (skeleton: a_spot10Control)
void GargoyleRockTargetAControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	// Negative time = flash indefinitely. Disabling the blocker slides the rock in,
	// opening the passage behind it, and the target itself leaves play until the bank
	// completes and puts both rocks back.
	lite45->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	rockA->Message(MessageCode::TBlockerDisable, 0.0);
	spot10->Message(MessageCode::TSoloTargetDisable, 0.0);
	lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	GargoyleTargetBankHit(caller);
}

// 0x00425570 -- a_spot14, mounted on rock B (skeleton: a_spot14Control)
void GargoyleRockTargetBControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	// Asymmetric with rock A, which lights v_lite45 unconditionally. The mode test is a
	// clean compare in the binary, not a decompiler artifact, but whether it is the
	// shipped intent or a bug is unknown -- it would be settled by watching v_lite46
	// during Dragon Pass in the real game.
	if (info_text_box->MessageField == ModeNormal)
		lite46->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	rockB->Message(MessageCode::TBlockerDisable, 0.0);
	spot14->Message(MessageCode::TSoloTargetDisable, 0.0);
	lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	GargoyleTargetBankHit(caller);
}

// 0x00425500 -- a_spot8 (skeleton: a_spot8Control)
void GargoyleTargetControl8(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeDragonPass)
	{
		lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		GargoyleTargetBankHit(caller);
		return;
	}

	// Dragon Pass: the two fixed gargoyle targets pay flat and feed the jackpot instead
	// of advancing the bank. The rock-mounted pair keeps ordinary bank behaviour, which
	// contradicts the manual's "each gargoyle target".
	lite13->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	AddToJackpot(250000);
	AddScoreAt(caller, 100000);
}

// 0x004255e0 -- a_spot12 (skeleton: a_spot12Control)
void GargoyleTargetControl12(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeDragonPass)
	{
		lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		GargoyleTargetBankHit(caller);
		return;
	}

	lite11->Message(MessageCode::TLightFlasherStartTimed, 0.5f);
	AddToJackpot(250000);
	AddScoreAt(caller, 100000);
}

// ---------------------------------------------------------------------------
// Quest Selection Target -- a_spot27
// ---------------------------------------------------------------------------

// 0x00425650 -- a_spot27 (skeleton: a_spot27Control)
void QuestSelectTargetControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	// Extra ball and quest selection are strictly either/or: with the light lit the hit
	// pays the ball and nothing else, against the manual's "5,000 points *and* an extra
	// ball".
	if (lite57->light_on())
	{
		lite57->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		AwardExtraBall(5.0f);
		return;
	}

	tabc96wave->Play(caller, "QuestSelectTargetControl");
	lite21->Message(MessageCode::TLightFlasherStartTimed, 1.0f);

	// Only idle play re-rolls the quest; during a quest the target is just 5,000.
	if (info_text_box->MessageField == ModeNormal)
	{
		const int quest = SelectNextQuest();
		if (quest >= 4 && quest <= 8)
			info_text_box->Display(pb::get_rc_string(DragonMsg(Str_QuestName4 + quest - 4)), 2.0f);
	}

	AddScoreFor(caller, 0); // 5000
}

// ---------------------------------------------------------------------------
// Sword Target -- three separate TSoloTargets, one per push stage
// ---------------------------------------------------------------------------
//
// a_spot19/v_lite16 (20,000) -> a_spot26/v_lite17 (50,000) -> a_spot28/v_lite18 (no
// score, starts Slay Dragon). SetSwordStage enables exactly one of the three and
// disables the others, so only the live stage can be hit at all.
//
// The light predicate, narrowed: the decompile's "reachable only via one flag" is the
// shipped control flow, not decompiler loss. At 0x425762 the listing is
//
//     ecx = [lite16+0x46]; test ecx,ecx; jne body
//     cmp [lite16+0x42],0; jne 0x4257ba   ; 0x4257ba is `ret 8`
//     ret 8                                ; and so is the fallthrough
//
// so both outcomes of the second compare return and the body runs iff +0x46 != 0. The
// Fire Ridge handler (0x426964) inlines light_on() as the OR of +0x42, +0x4e and +0x46,
// which places +0x46 among its three fields but does not name it -- that still needs
// the Full Tilt TLight layout, since the 1996 build's flags are ints four bytes apart
// where upstream's are bools.
//
// light_on() is kept here because it is a superset of the real test and the two agree in
// practice: SetSwordStage flashes the live stage's light indefinitely while completed
// stages' lights are steady-on and their targets disabled, so only the live stage can be
// hit at all.
//
// The announcement is literally the one-character string "!" at .data:0x439a58, one of a
// family ("!", "\"", "#".."####", "%".."(") that the box expands itself; the original
// also passes a fourth Display argument of 1 here where resource strings pass 0, which
// is presumably what marks a string as such a token. The port's TTextBox::Display has no
// such parameter.
//
// Narrowed, not settled: the bytes at 0x439a58 are `21 00 00 00 22 00 00 00`, so these
// really are one-character NUL-terminated strings on a four-byte stride -- "!" at
// 0x439a58, '"' (GlyphJackpot) at 0x439a5c -- and not an array of pointers or a
// misdecoded struct. What "!" renders as still needs DRAGON.DAT's symbol font.

// 0x00425750 -- a_spot19 (skeleton: a_spot19Control)
void SwordTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision || info_text_box->MessageField != ModeNormal)
		return;
	if (!lite16->light_on())
		return;

	info_text_box->Display("!", 2.0f);
	SetSwordStage(2);
	dragon_fire_lights->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	AddScoreFor(caller, 0); // 20000
}

// 0x004257c0 -- a_spot26 (skeleton: a_spot26Control)
void SwordTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision || info_text_box->MessageField != ModeNormal)
		return;
	if (!lite17->light_on())
		return;

	info_text_box->Display("!", 2.0f);
	SetSwordStage(1);
	dragon_fire_lights->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	AddScoreFor(caller, 0); // 50000
}

// 0x00425830 -- a_spot28 (skeleton: a_spot28Control)
void SwordTargetControl3(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision || info_text_box->MessageField != ModeNormal)
		return;
	if (!lite18->light_on())
		return;

	// Stage -1 retires all three targets; the third hit pays nothing but starts Slay
	// Dragon, which is why a_spot28 has no scoring table.
	info_text_box->Display("!", 2.0f);
	SetSwordStage(-1);
	UnlightSelectedQuest();
	lite76->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
	info_text_box->MessageField = ModeSlayDragon;
	MissionHandler(FtMessageCode::ModeStarted, nullptr);
}

// ---------------------------------------------------------------------------
// Ricochet Targets -- a_spot30/a_spot31
// ---------------------------------------------------------------------------

// 0x004258b0 -- a_spot30 (skeleton: a_spot30Control)
// The manual's "kicks the ball away" is the TSoloTarget's own collision response, not
// anything the handler does.
void RicochetTargetControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite20->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
		AddScoreFor(caller, 0); // 2500
	}
}

// 0x004258e0 -- a_spot31 (skeleton: a_spot31Control)
void RicochetTargetControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite19->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
		AddScoreFor(caller, 0); // 2500
	}
}

} // namespace dragon
