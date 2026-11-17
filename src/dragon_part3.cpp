// dragon_part3.cpp -- Dragon's Keep rules: the sinks and the three tower locks.
//
// Reimplementation of ten of DRAGON.EXE's sink handlers, in the shape of
// SpaceCadetPinball's control.cpp. Sources: analysis/dragon-ball-handlers.md (which
// already reconciles this exact scope against the raw listings),
// decomp/dragon/functions/*.c, decomp/dragon/asm/*.txt and analysis/symbols-cadet.md.
// a_sink18 (0x427100) is transcribed from its listing alone -- Ghidra inferred one
// parameter for a RET 0x8 __stdcall function and mangled the whole frame. Everywhere
// else the decompiled control flow and call targets were taken as reliable and only the
// stack-argument values were re-read from the listings; each such re-read is noted.
//
// Style: this file takes dragon_part1.cpp's shape (free functions in namespace dragon,
// MessageCode spelled out, DragonMsg/Str_* for Dragon's own resource ids, Mode* for the
// mode register) and dragon_part2.cpp's way of naming components -- plain extern
// pointers, because part1 already *defines* tags for several of the components used here
// (a_sink4, dragon_fire_light_group, v_lite57) and a second definition would collide.
// The two existing slices also disagree on the helper names; the brief settles it in
// part1's favour, so AddScoreFor / AddScoreAt / AwardExtraBall are used below and
// part2's AddScoreFrom / AddScoreAmount are not.
//
// Message codes follow the piecewise mapping documented at the top of part2: TLight +0,
// the group/target/blocker band +4, the gate/kickout/sink band +9 (which is where almost
// everything here sits), Control* +10, ControlCollision +11, the mission pair +13.
//
// Like the other slices this is a fragment: the component table, the score arrays and
// the per-table control interface live elsewhere, so it compiles in shape but does not
// link.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TPlunger.h"
#include "TSound.h"
#include "TTextBox.h"
#include "FtAccelDebug.h" // FTDBG

namespace dragon {

// ---------------------------------------------------------------------------
// Components -- tags live in control_dragon.cpp / dragon_part1.cpp
// ---------------------------------------------------------------------------

extern TTextBox*& info_text_box;
extern TPinballTable*& TableG;

extern TLight*& lite22;   // the 'D' of D-R-A-G-O-N, lit by the fire lizard pair
extern TLight*& lite33;   // weapon 1 \.
extern TLight*& lite34;   // weapon 2  | Max Weapons lights all four
extern TLight*& lite35;   // weapon 3  |
extern TLight*& lite36;   // weapon 4 /
extern TLight*& lite47;   // diverter-A routing indicator
extern TLight*& lite48;   // tower lock 1 (a_sink18, 100,000)
extern TLight*& lite49;   // tower lock 2 (a_sink17, 200,000)
extern TLight*& lite50;   // tower lock 3 (a_sink16, 300,000)
extern TLight*& lite57;   // extra-ball light
extern TLight*& lite63;   // fire lizard sink 1's own light; also the shared hit counter
extern TLight*& lite64;   // fire lizard sink 2's own light
extern TLight*& lite65;   // fire lizard secondary light flashed for a_sink1
extern TLight*& lite66;   // fire lizard secondary light flashed for a_sink2
extern TLight*& lite91;   // Fire Lizards multiplier rung (0..10, x100,000)
extern TLight*& lite114;  // Slay Dragon "hits left" counter
extern TLight*& lite125;  // dragon-chute route indicator
extern TLight*& lite126;  // dragon-ramp route indicator

extern TLightGroup*& well_light_group;
extern TLightGroup*& spell_spot_light_group;
extern TLightGroup*& dragon_fire_light_group;

extern TPinballComponent*& a_sink1;   // fire lizard sink, left
extern TPinballComponent*& a_sink2;   // fire lizard sink, right
extern TPinballComponent*& a_sink4;   // the well
extern TPinballComponent*& a_sink16;  // tower lock sink 3 (no handler of its own)
extern TPinballComponent*& a_sink17;  // tower lock sink 2
extern TPinballComponent*& a_sink18;  // tower lock sink 1
extern TPinballComponent*& a_center_post;
extern TPinballComponent*& a_bloce;
extern TPinballComponent*& mode_timer;

extern TPinballComponent*& a_dvtraa;
extern TPinballComponent*& a_dvtrab;
extern TPinballComponent*& a_dvtrda;
extern TPinballComponent*& a_dvtrdb;

extern TSound*& tabc04wave;
extern TSound*& tabc10wave;
extern TSound*& tabc13wave;
extern TSound*& tabc16wave;
extern TSound*& tabc24wave;
extern TSound*& tabc35wave;
extern TSound*& tabc51wave;
extern TSound*& tabc56wave;

// DAT_0043a99c is a_sink9's Component, read straight out of .data: the tags are
// `{const char* Name; TPinballComponent* Component;}` pairs laid end to end, the one at
// 0x43a998 points at the string "a_sink9" at 0x43a990, and "a_sink10" begins at
// 0x43a9a0. a_sink9 is the lock-3 tower entrance; the fire lizard sinks simply borrow
// its 407 attribute as a shared duration. dragon_common.cpp reads it.
float ASink9TimerTime();

// ---------------------------------------------------------------------------
// Table-local message codes, mode register, string ids
// ---------------------------------------------------------------------------

namespace FtMessageCode {
// Sent to MissionControl when a fire lizard sink restarts the quest that just ended.
// Dragon-only; the Space Cadet build has no code here, so the name comes from the single
// call site (analysis/dragon-ball-handlers.md §1.3).
constexpr auto QuestRestarted = static_cast<MessageCode>(0x51);

// Sent to mode_timer by a_sink19, which uses the *return value* as the time left in the
// current mode, in milliseconds. By the Control* band's +10 this would be upstream's
// ControlSpinnerLoopReset, which is plainly not what a timer returns, so it is treated as
// FT-only here.
// The unit is settled: at 0x428940 the same call's result is spilled, `fild`ed and
// compared against the double 0.0 at 0x437930, then divided by a literal `esi = 0x3e8`
// before being printed. So Message(0x48) returns an *integer* count of milliseconds, and
// every quest countdown divides by 1000 to print whole seconds. The getter's own name is
// still inferred from that use.
constexpr auto TimerGetRemaining = static_cast<MessageCode>(0x48);
}

// info_text_box->MessageField is Dragon's mode register; values from
// analysis/dragon-quest-system.md §1.2. Only the modes this slice actually tests are
// named; they are constexpr, so re-declaring the three part1 also needs is harmless.
constexpr int ModeRestartWindow = 3;
constexpr int ModeNormal = 2;
constexpr int ModeFireLizards = 5;
constexpr int ModeRescueMaiden = 6;
constexpr int ModeWizardsFury = 8;
constexpr int ModeSlayDragon = 9;
constexpr int ModeTowerBarrage = 13; // 0xd

// Dragon has its own string table, so resource ids are passed through rather than named
// in the port's Msg enum (same convention as part1).
inline Msg DragonMsg(int rcId) { return static_cast<Msg>(rcId); }

constexpr int Str_ExtraBallLight = 3002;   // 0xbba
constexpr int Str_PlainScore = 3008;       // 0xbc0, "%ld"
constexpr int Str_BallTrapped = 3010;      // 0xbc2, "Ball Trapped\n%ld"
constexpr int Str_FireLizardSlain = 3029;  // 0xbd5, "Fire Lizard Slain\n%ld"
constexpr int Str_MysteryAward0 = 3039;    // 0xbdf, +0..4 in a_sink4 MessageField order
constexpr int Str_MaxArmor = 3044;         // 0xbe4
constexpr int Str_MaxWeapons = 3045;       // 0xbe5
constexpr int Str_OneMillion = 3046;       // 0xbe6
constexpr int Str_HitsLeft = 3049;         // 0xbe9, "Slay Dragon\nHits Left: %ld"

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------

// part1 declares these two as void. They are not: 0x4230a0 and 0x4230f0 both return
// TPinballTable::AddScore's result in EAX, and every "%ld" site in this slice formats it
// -- visible as the bare `push eax` right after the call at 0x4271a5 (a_sink18) and
// 0x428164 (a_sink19). Declared with the return type here; part1's prototypes should
// gain it rather than this file inventing a second pair of helpers.
int AddScoreFor(TPinballComponent* caller, int index); // 0x004230a0
int AddScoreAt(TPinballComponent* caller, int score);  // 0x004230f0
void AwardExtraBall(float time);                       // 0x004231e0

void ApplyShield(bool announce);                                // 0x00423240
void CastRandomSpell();                                 // 0x004237f0
void SuspendTowerLocks();                               // 0x00423b80
void CheckTowerLocks();                                // 0x00423be0
bool StartSelectedQuest();                           // 0x00424210
int CollectJackpot(float unused, bool showText);         // 0x00424490
bool CheckDragonFlurry();                               // 0x004244e0
void MissionHandler(MessageCode code, TPinballComponent* cmp); // 0x0042c020
int DragonRand15();                                    // 0x0042c4cb, the raw 15-bit LCG

// Handlers in other slices that this one calls directly rather than through
// control::handler -- a_sink4's Max Armor award enables both armor blockers by invoking
// their handlers, exactly as the binary does (call 0x425910 / 0x425a10 with code 0x38).
void CenterPostControl(MessageCode code, TPinballComponent* caller);          // 0x00425910
void RightOutlaneBlockerControl(MessageCode code, TPinballComponent* caller); // 0x00425a10

// DAT_00439b10 -- set while a quest-start offer is on screen, which makes the next sink
// park the ball in s_hole118 instead of kicking it out (see dragon-ball-handlers.md §5.1).
extern bool KeepAnnouncement;

// The LCG at 0x42c4cb returns 15 bits, and Dragon's "random in [0,n)" is always the
// open-coded `(rand() * n) >> 15` with the round-toward-zero fixup. Collapsed into one
// helper; every call site below matched the same instruction pattern.
int RandBelow(int n)
{
	return (DragonRand15() * n) >> 15;
}

// ===========================================================================
// Fire Lizard Sinks -- a_sink1 (0x425ea0), a_sink2 (0x426000)
// ===========================================================================
//
// 5,000 each, and the table's quest-starting mechanism: two hits on the pair (or one sink
// twice) starts whichever quest a_spot27 selected. During Fire Lizards they *are* the
// quest.
//
// The pair swaps the ball: outside the two special paths each handler ejects the ball
// from the sink the player did *not* shoot into, and flashes that sink's secondary light.
// dragon-ball-handlers.md §3.1 verified this instruction by instruction at 0x425c64,
// 0x425d48 and 0x425d88, so it is the shipped behaviour, not decompiler loss, and is
// ported as-is. Presumably "the lizards pass the ball between themselves": the two sinks
// sit side by side.

// The flash duration both handlers use is `(float)(int)(*(float*)(0x43a99c + 0x7e))` --
// loaded with fld, truncated through __ftol at 0x42c7c8, then widened back with
// fild/fstp. Reproduced literally because the truncation is observable: a DAT value of
// 1.75 flashes for 1 second.
//
// The field is TSink::TimerTime, a float: CADET.EXE's WormHoleControl loads
// `*(float*)(sink + 0x7e)` at all four places upstream's control::WormHoleControl passes
// `sink->TimerTime`. There is no disagreement with part2 after all -- that +0x7e is on a
// moving part, a different class, where it is an integer frame index.
static float FireLizardFlashTime()
{
	return static_cast<float>(static_cast<int>(ASink9TimerTime()));
}

// 0x00425c30 -- the shared tail of both fire lizard handlers (__stdcall, RET 0x4).
void FireLizardSinkCommon(TPinballComponent* caller)
{
	char Buffer[64];

	// The "other sink" rule. Note the polarity: a hit on a_sink1 ejects from a_sink2 and
	// flashes v_lite65, and vice versa.
	TPinballComponent* otherSink = (caller == a_sink1) ? a_sink2 : a_sink1;
	TLight* otherLight = (caller == a_sink1) ? lite65 : lite66;

	const int quest = info_text_box->MessageField;
	if (ft_accel_enabled() || ft_autoplay_enabled())
		fprintf(stderr, "DBG-lizard common caller=%p a_sink1=%p a_sink2=%p other=%p quest=%d lite63=%d\n",
		        static_cast<void*>(caller), static_cast<void*>(a_sink1), static_cast<void*>(a_sink2),
		        static_cast<void*>(otherSink), quest, lite63 ? lite63->MessageField : -999); // FTDBG

	if (quest == ModeNormal)
	{
		// One counter for both sinks, parked on v_lite63's MessageField.
		if (++lite63->MessageField < 2)
		{
			if (ft_accel_enabled() || ft_autoplay_enabled())
				fprintf(stderr, "DBG-lizard branch=first-hit eject-from=%p\n", static_cast<void*>(otherSink)); // FTDBG
			tabc16wave->Play(nullptr, "FireLizardSink");
			otherSink->Message(MessageCode::TSinkResetTimer, -1.0f);
			return;
		}

		lite63->MessageField = 0;
		lite22->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f); // the 'D'

		// The pending flag makes whichever sink next swallows the ball park it in
		// s_hole118 while the quest-start offer is up. It is cleared again before
		// TryStartSelectedQuest because that call itself takes the ball.
		if (ft_accel_enabled() || ft_autoplay_enabled())
			fprintf(stderr, "DBG-lizard branch=second-hit, trying quest start\n"); // FTDBG
		KeepAnnouncement = true;
		if (CheckDragonFlurry())
			return; // D-R-A-G-O-N completed: mode 0xf owns the ball now
		KeepAnnouncement = false;
		if (StartSelectedQuest())
			return;

		// No quest was selected, so nothing took the ball: hand it back. a_sink1
		// unconditionally -- this is one of the two paths that does *not* swap sinks.
		if (ft_accel_enabled() || ft_autoplay_enabled())
			fprintf(stderr, "DBG-lizard branch=no-quest, eject from a_sink1=%p\n", static_cast<void*>(a_sink1)); // FTDBG
		a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	if (quest != ModeFireLizards)
	{
		tabc16wave->Play(nullptr, "FireLizardSink");
		otherSink->Message(MessageCode::TSinkResetTimer, -1.0f);
		otherLight->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}

	// Fire Lizards: the ladder is 100,000 x v_lite91's rung, capped at ten rungs, with
	// the jackpot instead of a score on every fourth hit. a_sink2's MessageField is the
	// 0..2-then-jackpot cycle -- another piece of unrelated state parked on a component.
	tabc16wave->Play(nullptr, "FireLizardSink");
	otherSink->Message(MessageCode::TSinkResetTimer, -1.0f);
	otherLight->Message(MessageCode::TLightFlasherStartTimed, 1.0f);

	if (lite91->MessageField < 10)
		lite91->MessageField++;

	int cycle = a_sink2->MessageField;
	if (cycle < 0 || cycle > 2)
	{
		CollectJackpot(0.0f, true);
		cycle = 0;
		lite91->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	else
	{
		// The rung about to pay is the last one before the jackpot, so the multiplier
		// light starts flashing indefinitely as the warning.
		if (cycle == 2)
			lite91->Message(MessageCode::TLightFlasherStartTimed, -1.0f);
		cycle++;
		const int addedScore = AddScoreAt(caller, lite91->MessageField * 100000);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_FireLizardSlain)), addedScore);
		info_text_box->Display(Buffer, 2.0);
	}
	a_sink2->MessageField = cycle;
}

// 0x00425ea0 -- a_sink1 (skeleton: a_sink1Control)
void FireLizardSink1Control(MessageCode code, TPinballComponent* caller)
{
	if ((ft_accel_enabled() || ft_autoplay_enabled()) && code == MessageCode::ControlCollision)
		fprintf(stderr, "DBG-lizard sink1 handler entered caller=%p mode=%d\n",
		        static_cast<void*>(caller), info_text_box ? info_text_box->MessageField : -999); // FTDBG
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeRestartWindow)
	{
		// The restart window: the finished quest's id was stashed on mode_timer, so
		// putting the ball back in a lizard sink within the window resumes it.
		//
		// a_sink1 really does send TLightResetAndTurnOff to each light twice (four calls,
		// 0x425f37-0x425f71) where a_sink2 sends one each. Idempotent, and transcribed
		// rather than tidied so the two handlers stay byte-comparable to the original.
		lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0);

		info_text_box->MessageField = mode_timer->MessageField;
		MissionHandler(FtMessageCode::QuestRestarted, nullptr);
		mode_timer->MessageField = -1;

		// The other special path: this one ejects from the sink that was hit.
		caller->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	const float flash = FireLizardFlashTime();

	if (info_text_box->MessageField != ModeFireLizards)
	{
		// Outside the quest the sink lights its own light solid first; during the quest
		// the quest controller owns those lights, so only the partner flash is left.
		lite63->Message(MessageCode::TLightTurnOn, 0.0);
		lite63->Message(MessageCode::TLightFlasherStartTimed, flash);
	}
	lite66->Message(MessageCode::TLightFlasherStartTimed, flash);

	AddScoreFor(caller, 0); // 5,000, paid in the quest too
	FireLizardSinkCommon(caller);
}

// 0x00426000 -- a_sink2 (skeleton: a_sink2Control)
//
// a_sink1's twin, down to the register allocation; two functions in the original.
void FireLizardSink2Control(MessageCode code, TPinballComponent* caller)
{
	if ((ft_accel_enabled() || ft_autoplay_enabled()) && code == MessageCode::ControlCollision)
		fprintf(stderr, "DBG-lizard sink2 handler entered caller=%p mode=%d\n",
		        static_cast<void*>(caller), info_text_box ? info_text_box->MessageField : -999); // FTDBG
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeRestartWindow)
	{
		lite63->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite64->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		info_text_box->MessageField = mode_timer->MessageField;
		MissionHandler(FtMessageCode::QuestRestarted, nullptr);
		mode_timer->MessageField = -1;
		caller->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	const float flash = FireLizardFlashTime();

	if (info_text_box->MessageField != ModeFireLizards)
	{
		lite64->Message(MessageCode::TLightTurnOn, 0.0);
		lite64->Message(MessageCode::TLightFlasherStartTimed, flash);
	}
	lite65->Message(MessageCode::TLightFlasherStartTimed, flash);

	AddScoreFor(caller, 0); // 5,000
	FireLizardSinkCommon(caller);
}

// ===========================================================================
// Well Sink -- a_sink4 (VA 426140; the handler below carries the address)
// ===========================================================================
//
// Two machines in one handler: the quest/spell lottery in ordinary play, and the whole of
// Wizard's Fury's mystery award.
//
// The mystery state lives in a_sink4's own MessageField and is set from the spell target
// bank (part1's SpellTargetControl4..22 write 0..4 there). Paying an award immediately
// re-arms the sink at a *different* state, chosen uniformly among the other four, and
// announces the new one with string 3039 + state.

// The re-arm, as five open-coded chains in the original -- one per current state, each
// mapping the four rand() outcomes onto the four other states in ascending order. That
// is exactly "the other four, ascending", so it collapses without changing the
// distribution or the mapping; the chains were read off 0x42628e, 0x426306, 0x4263ed,
// 0x4264ac and 0x426531 to confirm each permutation before collapsing them.
static int RearmMysteryAward(int current)
{
	const int pick = RandBelow(4);
	int next = 0;
	for (int i = 0, seen = 0; i < 5; i++)
	{
		if (i == current)
			continue;
		if (seen++ == pick)
		{
			next = i;
			break;
		}
	}
	return next;
}

// 0x00426140 -- a_sink4 (skeleton: a_sink4Control)
void WellSinkControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int quest = info_text_box->MessageField;

	if (quest == ModeNormal)
	{
		// One chance in four of offering the selected quest; otherwise a free spell. The
		// multiplier really is 4 (`shl eax,0x2` at 0x4261a9), not the coin flip
		// dragon-ball-handlers.md §3.2 describes.
		if (RandBelow(4) == 1)
		{
			KeepAnnouncement = true;
			if (!StartSelectedQuest())
			{
				// Nothing to start, so the offer never appears and the ball comes
				// straight back. The kickout below then fires a second time on the same
				// sink, which TSink tolerates; transcribed as found.
				KeepAnnouncement = false;
				a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
			}
			// Note the flag is left set when a quest *did* start: s_hole118's handler is
			// what clears it once the ball is parked.
		}
		else
		{
			CastRandomSpell();
		}

		a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}

	if (quest != ModeWizardsFury)
	{
		// During any other quest the well is a plain spell dispenser -- no lottery.
		CastRandomSpell();
		a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
		well_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		spell_spot_light_group->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}

	// Wizard's Fury: pay the armed award. The state numbering is the reverse of the
	// targets' physical top-to-bottom order.
	const int award = a_sink4->MessageField;
	switch (award)
	{
	case 0:
		// Announces itself with string 3011 from inside AwardExtraBall.
		AwardExtraBall(2.0f);
		break;

	case 1:
		lite33->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite34->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite35->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite36->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		// Dragon's score_multipliers are {1,1,2,3,4,5}, so 5 is the top weapon tier
		// rather than a 5x bonus stacked on top of one.
		TableG->ScoreMultiplier = 5;
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MaxWeapons)), 2.0);
		break;

	case 2:
		// Max Armor is "all three armor awards at once", and the two blocker armors are
		// granted by calling their own handlers directly -- the handler *is* the armor's
		// state machine (see part2), so going through it is what starts their timers.
		ApplyShield(false);
		tabc24wave->Play(nullptr, "WellSinkMaxArmor");
		CenterPostControl(MessageCode::TBlockerEnable, a_center_post);
		RightOutlaneBlockerControl(MessageCode::TBlockerEnable, a_bloce);
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MaxArmor)), 2.0);
		break;

	case 3:
		CollectJackpot(0.0f, true);
		break;

	default:
		// State 4 and, defensively, anything else: the flat million. Goes through
		// AddScoreAt, so the weapon multiplier applies to it.
		AddScoreAt(caller, 1000000);
		info_text_box->Display(pb::get_rc_string(DragonMsg(Str_OneMillion)), 2.0);
		break;
	}

	const int next = RearmMysteryAward(award);
	a_sink4->MessageField = next;
	info_text_box->Display(pb::get_rc_string(DragonMsg(Str_MysteryAward0 + next)), 4.0);
	a_sink4->Message(MessageCode::TSinkResetTimer, -1.0f);
}

// ===========================================================================
// The three tower locks
// ===========================================================================
//
// Six score components, two entrances each to three physical lock sinks:
//
//   a_sink15, a_sink18 -> a_sink18, light v_lite48, 100,000
//   a_sink13, a_sink17 -> a_sink17, light v_lite49, 200,000
//   a_sink9,  a_sink10 -> a_sink16, light v_lite50, 300,000
//
// The three lock lights carry all the shared state: MessageField != 0 means "this tower
// is armed", a solid LightOnFlag means "a ball is locked here", and ArmNextTowerLock
// (0x423be0) keeps a_sink18/a_sink17's ActiveFlag in step so only the armed tower
// swallows a ball. Because that function always arms v_lite48 -> v_lite49 -> v_lite50,
// the player sees 100k then 200k then 300k even though the scores are per tower. All
// three locked lit sets mode 0xd, Tower Barrage.
//
// Three asymmetries worth not "fixing":
//
//  * Tower 3 consumes the ball. a_sink9/a_sink10's lock path decrements MultiballCount
//    and never relaunches, while the other four entrances feed a replacement ball. That
//    is coherent: locking the third ball is precisely when ArmNextTowerLock starts Tower
//    Barrage, whose controller puts the balls back. dragon-ball-handlers.md §3.3.2
//    confirmed the absence of any 0x3fb or s_plunger1 reference in both listings.
//  * The TSinkResetTimer argument varies per *handler*, not per sink object: 0.0 ejects
//    immediately, a negative value makes TSink substitute its DAT-configured delay. So
//    a_sink17 the object is kicked with 0.0 by its own handler and -1.0 by a_sink13's.
//  * Which of ArmNextTowerLock / DisableTowerLocks runs afterwards differs per handler
//    and per branch, and three of the six call neither.
//
// ArmNextTowerLock only ever writes a_sink18's and a_sink17's ActiveFlag -- a_sink16's is
// never managed, which is consistent with tower 3 being reached through a_sink9/a_sink10
// rather than by dropping into a_sink16 directly.

// 0x00426f90 -- a_sink15, the first entrance to tower lock 1 (skeleton: a_sink15Control)
//
// The one entrance with no mode-2 guard on its lock path: a_sink15 locks in *any* mode
// but Rescue Maiden, as long as v_lite48 is armed. In practice only normal play leaves it
// armed, since every quest entry runs DisableTowerLocks, but the asymmetry is real.
void TowerLock1EntranceControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeRescueMaiden)
	{
		// Teleport: during Rescue Maiden the tower spits the ball out of a fire lizard
		// sink picked at random instead of locking it.
		tabc51wave->Play(nullptr, "TowerLockTeleport");
		if (RandBelow(2) != 0)
		{
			a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
			lite66->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		}
		else
		{
			a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
			lite65->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		}
		return;
	}

	if (lite48->MessageField != 0)
	{
		tabc10wave->Play(nullptr, "TowerLockTrap");
		TableG->MultiballCount--; // +0x192, balls in play: this one is gone
		lite48->Message(MessageCode::TLightResetAndTurnOn, 0.0); // solid = occupied
		TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);

		const int addedScore = AddScoreFor(caller, 0); // 100,000
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_BallTrapped)), addedScore);
		info_text_box->Display(Buffer, 2.0);

		CheckTowerLocks(); // all three solid -> Tower Barrage
		return;
	}

	// Unarmed: the ball is handed to the lock sink, not to this entrance, and the
	// acknowledgement sound comes after the eject.
	a_sink18->Message(MessageCode::TSinkResetTimer, -1.0f);
	tabc04wave->Play(nullptr, "TowerLockReject");
}

// 0x00427100 -- a_sink18, tower lock sink 1 itself (skeleton: a_sink18Control)
//
// Transcribed from decomp/dragon/asm/0x427100.txt; the decompilation is unusable.
// Unlike a_sink15 this one is strictly mode-gated, and it is the only entrance that
// re-arms on the unarmed path as well as the lock path.
void TowerLock1SinkControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeNormal:
		if (lite48->MessageField != 0)
		{
			tabc10wave->Play(nullptr, "TowerLockTrap");
			TableG->MultiballCount--;
			lite48->Message(MessageCode::TLightResetAndTurnOn, 0.0);
			TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);

			const int addedScore = AddScoreFor(caller, 0); // 100,000
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_BallTrapped)), addedScore);
			info_text_box->Display(Buffer, 2.0);
		}
		else
		{
			// 0.0, not -1.0: an unarmed tower in normal play spits the ball straight
			// back instead of holding it for the configured delay.
			a_sink18->Message(MessageCode::TSinkResetTimer, 0.0f);
		}
		// Both branches re-arm, so a ball through a dead tower still advances the ladder
		// if the tower target bank has lit a different one in the meantime.
		CheckTowerLocks();
		return;

	case ModeRescueMaiden:
		// Same teleport as a_sink15, and like it never locks. Note neither Rescue Maiden
		// path touches the lock state at all.
		tabc51wave->Play(nullptr, "TowerLockTeleport");
		if (RandBelow(2) != 0)
		{
			a_sink1->Message(MessageCode::TSinkResetTimer, -1.0f);
			lite66->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		}
		else
		{
			a_sink2->Message(MessageCode::TSinkResetTimer, -1.0f);
			lite65->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		}
		return;

	default:
		// Any other mode: give the ball back on the configured delay and tear the locks
		// down, since a quest owns the towers now.
		a_sink18->Message(MessageCode::TSinkResetTimer, -1.0f);
		SuspendTowerLocks();
		return;
	}
}

// 0x00427290 -- a_sink13, the first entrance to tower lock 2 (skeleton: a_sink13Control)
//
// Mode-gated like a_sink18, but calls neither ArmNextTowerLock nor DisableTowerLocks on
// any path, and its Rescue Maiden branch is silent -- the quest controller does the
// talking there -- where the out-of-quest branch plays tabc04.
void TowerLock2EntranceControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	const int quest = info_text_box->MessageField;

	if (quest == ModeNormal)
	{
		if (lite49->MessageField != 0)
		{
			tabc10wave->Play(nullptr, "TowerLockTrap");
			TableG->MultiballCount--;
			lite49->Message(MessageCode::TLightResetAndTurnOn, 0.0);
			TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);

			const int addedScore = AddScoreFor(caller, 0); // 200,000
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_BallTrapped)), addedScore);
			info_text_box->Display(Buffer, 2.0);

			CheckTowerLocks();
			return;
		}

		tabc04wave->Play(nullptr, "TowerLockReject");
		a_sink17->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	if (quest != ModeRescueMaiden)
	{
		tabc04wave->Play(nullptr, "TowerLockReject");
		a_sink17->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	a_sink17->Message(MessageCode::TSinkResetTimer, -1.0f);
}

// 0x004273d0 -- a_sink17, tower lock sink 2 itself (skeleton: a_sink17Control)
//
// The only handler whose *both* kickout paths use 0.0, and the mirror image of a_sink18's
// bookkeeping: arm after an unarmed hit in normal play, tear down outside it.
void TowerLock2SinkControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeNormal)
	{
		a_sink17->Message(MessageCode::TSinkResetTimer, 0.0f);
		SuspendTowerLocks();
		return;
	}

	if (lite49->MessageField != 0)
	{
		tabc10wave->Play(nullptr, "TowerLockTrap");
		TableG->MultiballCount--;
		lite49->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		TableG->Plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);

		const int addedScore = AddScoreFor(caller, 0); // 200,000
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_BallTrapped)), addedScore);
		info_text_box->Display(Buffer, 2.0);

		CheckTowerLocks();
		return;
	}

	a_sink17->Message(MessageCode::TSinkResetTimer, 0.0f);
	CheckTowerLocks();
}

// 0x004274d0 -- a_sink9, the first entrance to tower lock 3 (skeleton: a_sink9Control)
//
// Tower 3's two entrances share one "reject" block -- sound, diverter flip, eject -- which
// the compiler emitted twice, once per mode branch. The inner `quest != 6 && quest != 0xd`
// test is therefore dead inside the mode-2 branch; kept so the shape stays comparable to
// the listing (and to a_sink10, which repeats the same redundancy).
//
// a_sink9 owns diverter pair A, the same pair a_roll7 flips the other way, which is what
// pins v_lite47 as that pair's routing indicator.
void TowerLock3EntranceAControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	const int quest = info_text_box->MessageField;

	if (quest == ModeNormal)
	{
		if (lite50->MessageField != 0)
		{
			// No PlungerRelaunchBall here: locking tower 3 is what starts Tower Barrage,
			// and its controller is responsible for the balls. Adding a relaunch to
			// match towers 1 and 2 would leave a spare ball loose at mode start.
			tabc10wave->Play(nullptr, "TowerLockTrap");
			TableG->MultiballCount--;
			lite50->Message(MessageCode::TLightResetAndTurnOn, 0.0);

			const int addedScore = AddScoreFor(caller, 0); // 300,000
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_BallTrapped)), addedScore);
			info_text_box->Display(Buffer, 2.0);

			CheckTowerLocks();
			return;
		}

		tabc04wave->Play(nullptr, "TowerLockReject");
		if (quest != ModeRescueMaiden && quest != ModeTowerBarrage)
		{
			lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			a_dvtraa->Message(MessageCode::TGateEnable, 0.0);
			a_dvtrab->Message(MessageCode::TGateDisable, 0.0);
		}
		a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	if (quest != ModeRescueMaiden)
	{
		tabc04wave->Play(nullptr, "TowerLockReject");
		// Here the guard bites: Tower Barrage and Rescue Maiden own the routing, so the
		// diverter is left alone and only the eject happens.
		if (quest != ModeRescueMaiden && quest != ModeTowerBarrage)
		{
			lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			a_dvtraa->Message(MessageCode::TGateEnable, 0.0);
			a_dvtrab->Message(MessageCode::TGateDisable, 0.0);
		}
		a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
}

// 0x00427680 -- a_sink10, the second entrance to tower lock 3 (skeleton: a_sink10Control)
//
// a_sink9's twin on diverter pair D, which it fights over with the dragon ramp and the
// dragon chute: the four-message block below is character-for-character part2's
// DragonRampControl block, i.e. a shared "route lane D to the ramp" idiom.
void TowerLock3EntranceBControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	const int quest = info_text_box->MessageField;

	if (quest == ModeNormal)
	{
		if (lite50->MessageField != 0)
		{
			tabc10wave->Play(nullptr, "TowerLockTrap");
			TableG->MultiballCount--;
			lite50->Message(MessageCode::TLightResetAndTurnOn, 0.0);

			const int addedScore = AddScoreFor(caller, 0); // 300,000
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_BallTrapped)), addedScore);
			info_text_box->Display(Buffer, 2.0);

			CheckTowerLocks();
			return;
		}

		tabc04wave->Play(nullptr, "TowerLockReject");
		if (quest != ModeRescueMaiden && quest != ModeTowerBarrage)
		{
			lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			a_dvtrda->Message(MessageCode::TGateEnable, 0.0);
			a_dvtrdb->Message(MessageCode::TGateDisable, 0.0);
		}
		a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	if (quest != ModeRescueMaiden)
	{
		tabc04wave->Play(nullptr, "TowerLockReject");
		if (quest != ModeRescueMaiden && quest != ModeTowerBarrage)
		{
			lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			a_dvtrda->Message(MessageCode::TGateEnable, 0.0);
			a_dvtrdb->Message(MessageCode::TGateDisable, 0.0);
		}
		a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
		return;
	}

	a_sink16->Message(MessageCode::TSinkResetTimer, -1.0f);
}

// ===========================================================================
// The dragon's mouth -- a_sink19 (0x427f10)
// ===========================================================================
//
// Live only during Slay Dragon. v_lite114's MessageField is the hit counter; reaching
// zero plays the death wave and leaves the rest to mode 9's controller.
//
// Every surviving hit buys 15 more seconds of mode time and bounces the ball out of one
// of six sinks at random -- the well, both fire lizards and all three tower locks -- so
// the player never gets it back where it went in.
//
// The award ladder is read off dragon_fire_light_group's on-count, and its top three
// rungs are *probabilistic*, which the manual presents as deterministic: jackpot 1 in 5,
// extra ball light 1 in 10, extra ball 1 in 10, each falling through to a flat 100,000.
void DragonMouthSinkControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeSlayDragon)
	{
		// Outside the finale the mouth is a pass-through to the left fire lizard sink,
		// with 0.0 so the ball reappears at once (confirmed at 0x427f29: push 0, push
		// 0x41). The ball is not ejected from a_sink19 at all.
		a_sink1->Message(MessageCode::TSinkResetTimer, 0.0f);
		return;
	}

	if (--lite114->MessageField == 0)
	{
		// Dragon slain. The handler deliberately stops here: the ball stays swallowed and
		// mode 9's controller runs the finish.
		tabc56wave->Play(nullptr, "DragonSlain");
		return;
	}

	// mode_timer answers in milliseconds and is rearmed in seconds, hence the /1000.
	const int remainingMs = mode_timer->Message(FtMessageCode::TimerGetRemaining, 0.0);
	const float remaining = static_cast<float>(remainingMs) / 1000.0f;
	mode_timer->Message(MessageCode::TBlockerRestartTimeout, remaining + 15.0f);

	TPinballComponent* exitSink;
	switch (RandBelow(6))
	{
	case 0: exitSink = a_sink4; break;
	case 1: exitSink = a_sink1; break;
	case 2: exitSink = a_sink2; break;
	case 3: exitSink = a_sink18; break;
	case 4: exitSink = a_sink17; break;
	default: exitSink = a_sink16; break;
	}
	exitSink->Message(MessageCode::TSinkResetTimer, 3.0f);

	// awarded == the rung paid something other than a score, which in the original is a
	// jump straight to the shared tail; score == 0 means "fall through to the 100,000".
	bool awarded = false;
	int score = 0;

	switch (dragon_fire_light_group->Message(MessageCode::TLightGroupGetOnCount, 0.0))
	{
	case 0:
	case 1:
		break;

	case 2:
		tabc13wave->Play(nullptr, "DragonMouthHit");
		score = 1000000;
		break;

	case 3:
		if (RandBelow(5) == 0)
		{
			CollectJackpot(0.0f, true);
			awarded = true;
		}
		break;

	case 4:
		if (RandBelow(10) == 0)
		{
			tabc35wave->Play(nullptr, "DragonMouthExtraBallLight");
			info_text_box->Display(pb::get_rc_string(DragonMsg(Str_ExtraBallLight)), 2.0);
			lite57->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			awarded = true;
		}
		break;

	default:
		if (RandBelow(10) == 0)
		{
			// Time 0.0 still announces: only a negative time suppresses the string.
			AwardExtraBall(0.0f);
			awarded = true;
		}
		break;
	}

	if (!awarded)
	{
		if (score == 0)
		{
			tabc13wave->Play(nullptr, "DragonMouthHit");
			score = 100000;
		}
		const int addedScore = AddScoreAt(caller, score);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_PlainScore)), addedScore);
		info_text_box->Display(Buffer, 2.0);
	}

	// The shared tail. "!" is the one-character symbol token at .data:0x439a58 -- see
	// part1's note on the sword targets -- and the fourth Display argument the original
	// passes for it is the glyph-font selector, so it goes through DisplayGlyph.
	info_text_box->DisplayGlyph("!", 3.5);

	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(DragonMsg(Str_HitsLeft)),
		lite114->MessageField);
	info_text_box->Display(Buffer, 5.0);
}

} // namespace dragon
