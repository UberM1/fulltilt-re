// dragon_helpers.cpp -- Dragon's Keep: the three award helpers the slices share and
// the five mode controllers dragon_missions.cpp dispatches to but does not own.
//
// Every routine here was declared by one or more of dragon_part1..4.cpp /
// dragon_missions.cpp and defined by none of them, so the rules compiled and did not
// link. They are collected in one file because they are what is left of the mode
// machine once the six quests are out: the three multiball states, the end-of-ball
// bonus count-up and the end-of-game sequence, plus the two small award paths
// (CastRandomSpell / CollectJackpot) and the tower-lock arm (EnableTowerLock) that
// several slices call and none defines.
//
// Source of truth, in order of precedence:
//   1. decomp/dragon/functions/*.c (Ghidra pseudocode of DRAGON.EXE)
//   2. analysis/dragon-quest-system.md (§5.1 the drain bonus, §7 the multiballs)
//   3. docs/dragon-rules-spec.md (the shipped manual -- loses every conflict)
//
// Conventions follow dragon_missions.cpp, whose controllers these are siblings of:
// component tags rather than aliases, message codes spelled out in `ft` with the
// binary's own numbering, and the mode register read and written through
// CurrentMode() / SetMode().

#include "pch.h"
#include "control.h"

#include "high_score.h"
#include "midi.h"
#include "pb.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

// UNVERIFIED track numbering, exactly as in dragon_missions.cpp: the roles are
// pinned by the call sites (DAT_00439ae8 is started by Gargoyle Gaggle-Ball,
// DAT_00439afc by the game-over sequence), the TABCn file each maps to is not. The
// six quest/idle tracks are 1..6 there, so these continue the run.
constexpr music_track gargoyle_gaggle_music = 7;
constexpr music_track game_over_music = 8;

namespace dragon
{

// ---------------------------------------------------------------------------
// Message codes (binary numbering)
//
// The subset this file sends, same values and same reasoning as
// dragon_missions.cpp's `ft`: TLight +0, TLightGroup/target/blocker +4,
// gate/kickout/sink +9, Control* +10, ControlCollision +11, and the public
// 1000+ band +1 from 1016 and +2 from 1021.
// ---------------------------------------------------------------------------

namespace ft
{
constexpr MessageCode Msg(int v) { return static_cast<MessageCode>(v); }

// TLight family.
constexpr MessageCode LightTurnOn = Msg(0x01);
constexpr MessageCode LightFlasherStartTimed = Msg(0x07);     // -1.0 == flash forever
constexpr MessageCode LightFlashTimedThenStayOn = Msg(0x0f);
constexpr MessageCode LightFlashTimedThenStayOff = Msg(0x10);
constexpr MessageCode LightResetAndTurnOn = Msg(0x13);
constexpr MessageCode LightResetAndTurnOff = Msg(0x14);
// The originals' light-override codes are 0x18/0x19/0x1a, but the port does not use
// the originals' numbers for these three: upstream already had negative placeholders
// in MessageCode, so they are TLightFtTmpOverrideOn/-Off/TLightFtResetOverride at
// -24/-25/-26 (see TPinballComponent.h). Written as 0x19 they were inert on a TLight
// and, worse, hit TLightGroupStepForward on a light group, which is a different
// mechanic entirely. A light group has no case of its own for them and forwards them
// to each member light, which is what the originals do too.
constexpr MessageCode LightOverrideOff = MessageCode::TLightFtTmpOverrideOff;
constexpr MessageCode LightClearOverride = MessageCode::TLightFtResetOverride;

// TLightGroup family (+4).
constexpr MessageCode GroupGetOnCount = Msg(0x29);

// Gates (+9).
constexpr MessageCode GateDisable = Msg(0x3e);
constexpr MessageCode GateEnable = Msg(0x3f);

// Sinks, kickouts and timers (+9/+10).
constexpr MessageCode KickoutRestartTimer = Msg(0x40);
constexpr MessageCode SinkResetTimer = Msg(0x41);
constexpr MessageCode TimerSet = Msg(0x45);   // seconds; -1.0 cancels

// Control* block (+10).
constexpr MessageCode TimerExpired = Msg(0x46);
constexpr MessageCode MissionComplete = Msg(0x4f);
constexpr MessageCode MissionStarted = Msg(0x50);
constexpr MessageCode QuestAbort = Msg(0x53);

// The flipper notification the egg prompt uses, value 1 -- numerically the same
// as LightTurnOn, which is harmless because no component answers both. The drain
// bonus reads it as "fast-forward the count-up".
constexpr MessageCode FlipperPressed = Msg(0x01);

// Public codes. 0x3f3/0x3f4 are below the +1 step so they are upstream's 1011 and
// 1012 unshifted; 0x3f9 is 1016+1 = PlungerStartFeedTimer, 0x3fb is 1018+1 =
// PlungerRelaunchBall, 0x3ff is 1021+2 = SwitchToNextPlayer and 0x400 is 1022+2 =
// GameOver.
constexpr MessageCode SetTiltLock = Msg(0x3f3);
constexpr MessageCode ClearTiltLock = Msg(0x3f4);
constexpr MessageCode PlungerStartFeedTimer = Msg(0x3f9);
constexpr MessageCode PlungerRelaunchBall = Msg(0x3fb);
constexpr MessageCode SwitchToNextPlayer = Msg(0x3ff);
constexpr MessageCode TableGameOver = Msg(0x400);
}  // namespace ft

const float Forever = -1.0f;

// Upstream's TPlunger hardcodes timer::set(0.95999998f); Full Tilt passes it as a
// message argument, which is where the 0x3f75c28f in every feed call comes from.
const float PlungerFeedDelay = 0.96f;

// ---------------------------------------------------------------------------
// Modes
//
// Redeclared rather than shared: dragon_missions.cpp owns this enum and the two
// accessors below, and the enumerator list here is a verbatim copy so the two
// declarations agree. 0x42c120 switches on exactly these sixteen values.
// ---------------------------------------------------------------------------

enum class Mode
{
	GameOver = 0,
	BetweenBalls = 1,
	Idle = 2,
	RestartWindow = 3,
	DragonHoard = 4,
	FireLizards = 5,
	RescueMaiden = 6,
	DragonPass = 7,
	WizardsFury = 8,
	SlayDragon = 9,
	SkillShot = 0xa,
	DrainBonus = 0xb,
	EggExchange = 0xc,
	TowerBarrage = 0xd,
	GargoyleGaggle = 0xe,
	DragonFlurry = 0xf,
};

// ---------------------------------------------------------------------------
// Component tags owned by other slices
//
// `component_tag<T>` definitions live with whichever slice first needed them; the
// declarations below name the owning file so the types can be checked against it.
// Note tag_tabc44wave / tag_tabc61wave are typed TPinballComponent by
// control_dragon.cpp, not TSound -- see PlayWave() below.
// ---------------------------------------------------------------------------

// dragon_missions.cpp.
extern component_tag<TTextBox> tag_info_text_box;
extern component_tag<TPinballComponent> tag_mode_timer;
extern component_tag<TPinballComponent> tag_drain;
extern component_tag<TLight> tag_v_lite22;
extern component_tag<TLight> tag_v_lite23;
extern component_tag<TLight> tag_v_lite24;
extern component_tag<TLight> tag_v_lite25;
extern component_tag<TLight> tag_v_lite26;
extern component_tag<TLight> tag_v_lite27;
extern component_tag<TLight> tag_v_lite28;
extern component_tag<TLight> tag_v_lite31;
extern component_tag<TLight> tag_v_lite37;
extern component_tag<TLight> tag_v_lite38;
extern component_tag<TLight> tag_v_lite39;
extern component_tag<TLight> tag_v_lite47;
extern component_tag<TLight> tag_v_lite48;
extern component_tag<TLight> tag_v_lite49;
extern component_tag<TLight> tag_v_lite50;
extern component_tag<TLight> tag_v_lite63;
extern component_tag<TLight> tag_v_lite64;
extern component_tag<TLight> tag_v_lite65;
extern component_tag<TLight> tag_v_lite66;
extern component_tag<TLight> tag_v_lite77;
extern component_tag<TLight> tag_v_lite78;
extern component_tag<TLight> tag_v_lite79;
extern component_tag<TLight> tag_v_lite125;
extern component_tag<TLight> tag_v_lite126;
extern component_tag<TLightGroup> tag_dragon_egg_light_group;
extern component_tag<TLightGroup> tag_dragon_letter_light_group;
extern component_tag<TLightGroup> tag_prison_window_light_group;
extern component_tag<TLightGroup> tag_watch_window_light_group;
extern component_tag<TLightGroup> tag_gate_window_light_group;
extern component_tag<TLightGroup> tag_well_light_group;
extern component_tag<TPinballComponent> tag_a_kout2;
extern component_tag<TPinballComponent> tag_a_sink1;
extern component_tag<TPinballComponent> tag_a_sink2;
extern component_tag<TPinballComponent> tag_a_sink4;
extern component_tag<TPinballComponent> tag_a_sink16;
extern component_tag<TPinballComponent> tag_a_sink17;
extern component_tag<TPinballComponent> tag_a_sink18;
extern component_tag<TPinballComponent> tag_a_dvtraa;
extern component_tag<TPinballComponent> tag_a_dvtrab;
extern component_tag<TPinballComponent> tag_a_dvtrba;
extern component_tag<TPinballComponent> tag_a_dvtrbb;
extern component_tag<TPinballComponent> tag_a_dvtrca;
extern component_tag<TPinballComponent> tag_a_dvtrcb;
extern component_tag<TPinballComponent> tag_a_dvtrda;
extern component_tag<TPinballComponent> tag_a_dvtrdb;
extern component_tag<TSound> tag_tabc13wave;  // the "total bonus" / sword chime
extern component_tag<TSound> tag_tabc43wave;  // jackpot
extern component_tag<TSound> tag_tabc48wave;  // Fire Lizards / Gargoyle Gaggle-Ball

// dragon_part1.cpp.
extern component_tag<TSound> tag_tabc06wave;  // one bonus line scored

// dragon_components.cpp.
extern component_tag<TSound> tag_tabc56wave;  // HIGH SCORE

// control_dragon.cpp.
extern component_tag<TPinballComponent> tag_s_plunger1;
extern component_tag<TPinballComponent> tag_tabc44wave;  // bonus count-up begins
extern component_tag<TPinballComponent> tag_tabc61wave;  // Dragon Flurry

// ---------------------------------------------------------------------------
// Cross-slice state
// ---------------------------------------------------------------------------

// dragon_common.cpp -- 0x434d08 / 0x434d24, the two per-ball counters the drain
// bonus reads back. Reset by the plunger, incremented by MissionHandler/CastSpell.
extern int TargetsHitThisBall;
extern int SpellsCastThisBall;

// dragon_part4.cpp -- DAT_00439b1c. 0 while a ball is in play, 1 when the last
// ball drained untilted, 2 when it drained tilted. The 1-vs-2 split is consumed by
// this file's MissionComplete arm, which skips the whole count-up when the table is
// tilt-locked and the phase is not 1.
extern int game_over_phase;

// dragon_missions.cpp -- DAT_00439b10. Set by the skill shot and the egg exchange
// to carry an announcement across a mode change; the two multiballs that can be
// entered from either read it to decide whether they still owe the player a feed.
extern bool KeepAnnouncement;

// dragon_part1.cpp.
extern TPinballTable*& TableG;

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------

// dragon_part1.cpp.
int AddScoreAt(TPinballComponent* caller, int score);             // 0x4230f0 -- multiplied
int DragonSpecialAddScore(TPinballComponent* caller, int score);  // 0x423130 -- unmultiplied

// dragon_common.cpp.
const char* GetRcString(int id);                                  // 0x409730
void Display(TTextBox* box, const char* text, float time);        // 0x41de40, font 0
void DisplayGlyph(TTextBox* box, const char* glyph, float time);  // 0x41de40, font 1
void ClearText(TTextBox* box);                                    // 0x41d930
void FormatWithValue(char* out, const char* labelOrFormat);       // 0x42c76f
void Play(TSound* sound);                                         // vtable +0x18
extern const char* GlyphJackpot;                                  // 0x439a5c, '"'

// dragon_missions.cpp.
Mode CurrentMode();
void SetMode(Mode m);
int QuestsCompleted();
bool Owned(TLight* light);
int SelectUnownedSpell();                                         // 0x423600
void CastSpell(int slot);                                         // 0x4236b0
void UnlightSelectedQuest();                                      // 0x423e50
void CheckTowerLocks();                                           // 0x423be0
void BeginQuestPlay();                                            // 0x428580
void EndQuestPlay();                                              // 0x4285a0
void MissionHandler(MessageCode code, TPinballComponent* caller);  // 0x42c020

// ---------------------------------------------------------------------------
// String ids, as DRAGON.EXE resource numbers
// ---------------------------------------------------------------------------

enum
{
	STR_SHOOT_AGAIN_P1 = 2011,   // 2011..2014, one per player
	STR_TARGET_BONUS = 3012,
	STR_QUEST_BONUS = 3013,
	STR_ARMOR_BONUS = 3014,
	STR_SPELL_BONUS = 3015,
	STR_BONUS_HOLD = 3016,
	STR_TOTAL_BONUS = 3017,
	STR_TOWER_BARRAGE = 3051,
	STR_GARGOYLE_GAGGLE = 3062,
	STR_DRAGON_FLURRY = 3063,
	STR_HIGH_SCORE = 3065,
	STR_DRAGON_EGG_BONUS = 3069,
	STR_WEAPON_BONUS = 3070,
};

// ---------------------------------------------------------------------------
// Module state -- the drain bonus's three cells
// ---------------------------------------------------------------------------

// 0x434d00 / 0x434d10 / 0x434d0c. The accumulator, the step index and the "do not
// narrate" flag a flipper press latches to fast-forward the count-up. All three are
// private to DrainBonusController, which is why they are file-static here where the
// originals are plain .data.
static int BonusTotal = 0;
static int BonusStep = 0;
static int BonusQuiet = 0;

// ---------------------------------------------------------------------------
// Two local shims
// ---------------------------------------------------------------------------

// control_dragon.cpp types its wave tags `component_tag<TPinballComponent>` while
// dragon_common.cpp's Play takes a TSound*. The components really are TSounds -- the
// tag names are the DAT's wave groups and the original reaches vtable +0x18 on them
// -- so this is the downcast the generated tag table forces, not a type question.
void PlayWave(TPinballComponent* wave)
{
	Play(static_cast<TSound*>(wave));
}

// 0x410ff0 -- is there a high score at all? The game-over sequence checks it before
// announcing one. dragon_common.cpp has no counterpart; pirates_common.cpp's is the
// same routine behind the Skulduggery namespace.
bool AnyHighScore()
{
	for (auto& entry : high_score::highscore_table)
		if (entry.Score > 0)
			return true;
	return false;
}

// ---------------------------------------------------------------------------
// Award paths
// ---------------------------------------------------------------------------

// 0x004237f0 -- cast a random spell the player does not already own, or do nothing
// at all when all four are owned. Two lines in the original and the single most
// called award path in the table: the well pays it three times in four, the top
// spell target pays it, and the skill shot on a_spot4 pays it.
void CastRandomSpell()
{
	const int slot = SelectUnownedSpell();
	if (slot != -1)
		CastSpell(slot);
}

// 0x00424490 -- award the jackpot. It adds the jackpot to the score and never
// resets it, which is what the manual's "the jackpot is never reduced" describes.
// The float argument is unread: the original returns it unchanged on the x87 stack,
// so the callers chain it through (`group->Message(code, CollectJackpot(0.0))` is
// the shape in the sibling table) rather than using it here. The port's declared
// return is int and no Dragon caller reads it.
int CollectJackpot(float unused, bool showGlyph)
{
	PlayWave(tag_tabc43wave.Component);
	AddScoreAt(nullptr, TableG->JackpotScore);  // table+0x172, multiplied
	if (showGlyph)
		DisplayGlyph(tag_info_text_box.Component, GlyphJackpot, 2.0f);
	return static_cast<int>(unused);
}

// 0x00423d50 -- arm the next tower lock.
//
// Called by the tower ramp once the ball has run it: point diverter A and diverter D
// at the towers (lighting v_lite125 and dousing v_lite126, the two route
// indicators), then offer the first lock light that is NOT already lit by flashing
// it indefinitely. CheckTowerLocks then reads that flasher back and binds the
// matching sink, and when all three are solid it is what starts Tower Barrage.
//
// Only one light is ever offered per call and the three are tested in order, so the
// towers fill 48 -> 49 -> 50. The trailing CheckTowerLocks() runs on every path --
// the original duplicates the call into each arm as a tail jump, which is what
// Ghidra reports as three terminal calls to 0x423be0.
void EnableTowerLock()
{
	tag_v_lite47.Component->Message(ft::LightResetAndTurnOn, 0.0f);
	tag_a_dvtraa.Component->Message(ft::GateDisable, 0.0f);
	tag_a_dvtrab.Component->Message(ft::GateEnable, 0.0f);
	tag_v_lite125.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	tag_v_lite126.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_a_dvtrda.Component->Message(ft::GateDisable, 0.0f);
	tag_a_dvtrdb.Component->Message(ft::GateEnable, 0.0f);

	TLight* const locks[3] = {
		tag_v_lite48.Component, tag_v_lite49.Component, tag_v_lite50.Component,
	};
	for (int i = 0; i < 3; i++)
	{
		if (Owned(locks[i]))
			continue;
		locks[i]->Message(ft::LightFlasherStartTimed, Forever);
		break;
	}

	CheckTowerLocks();
}

// ---------------------------------------------------------------------------
// The three multiball states
//
// All three have the same three-armed shape, which is also the shape of the five
// quest controllers in dragon_missions.cpp:
//
//   0x4f  the mode has just started -- bump MultiballCount, arm the feeds, light
//         the mode's own "2"/"3"/"5" lamp, then fall through to the announcement
//   0x50  re-announce (an unpause, or the text box's timer running out)
//   0x53  a ball drained -- and ONLY when it was the last one (MultiballCount == 1)
//         does the mode end, which is what makes these modes last as long as the
//         player keeps a ball alive
//
// None of them has a clock: mode_timer is never set, so there is no 0x46 arm and no
// restart window. A multiball ends when the balls do.
// ---------------------------------------------------------------------------

// 0x0042b0e0 -- Tower Barrage (string 3051), three extra balls.
//
// Reached from CheckTowerLocks with all three tower-lock lights solid. The three
// locked balls are what the mode serves: a_sink18/17/16 are told to eject at 1, 2
// and 3 seconds and their lock lights flash for the matching interval and go out
// (LightFlashTimedThenStayOff), so the player sees each tower empty in turn.
//
// The diverter block at the head is the same one EnableTowerLock installs, plus the
// B and C pairs: everything is pointed at the towers for the duration.
void TowerBarrageController(MessageCode code)
{
	if (code == ft::MissionComplete)
	{
		BeginQuestPlay();

		tag_v_lite47.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		tag_a_dvtraa.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrab.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrba.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrbb.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrca.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrcb.Component->Message(ft::GateDisable, 0.0f);
		tag_v_lite125.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_v_lite126.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_dvtrda.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrdb.Component->Message(ft::GateEnable, 0.0f);

		// Rescue Maiden's three window groups flash for the duration -- the mode
		// borrows them as its own "all three towers are live" display.
		tag_prison_window_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_watch_window_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_gate_window_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);

		TableG->MultiballCount += 3;  // table+0x192

		tag_a_sink18.Component->Message(ft::SinkResetTimer, 1.0f);
		tag_v_lite48.Component->Message(ft::LightFlashTimedThenStayOff, 1.0f);
		tag_a_sink17.Component->Message(ft::SinkResetTimer, 2.0f);
		tag_v_lite49.Component->Message(ft::LightFlashTimedThenStayOff, 2.0f);
		tag_a_sink16.Component->Message(ft::SinkResetTimer, 3.0f);
		tag_v_lite50.Component->Message(ft::LightFlashTimedThenStayOff, 3.0f);

		tag_v_lite78.Component->Message(ft::LightFlasherStartTimed, Forever);  // the "3" lamp
		ClearText(tag_info_text_box.Component);
	}
	else if (code == ft::QuestAbort)
	{
		// Only the last ball ends it.
		if (TableG->MultiballCount != 1)
			return;

		EndQuestPlay();
		tag_prison_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_watch_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_gate_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite78.Component->Message(ft::LightResetAndTurnOn, 0.0f);

		SetMode(Mode::Idle);
		MissionHandler(ft::MissionComplete, nullptr);

		// Idle (or whatever the broadcast above selected) may have claimed the
		// diverters for itself. Rescue Maiden and a fresh Tower Barrage are the two
		// modes that drive this same block, so for those two the teardown is skipped
		// entirely rather than undone and re-done.
		const Mode now = CurrentMode();
		if (now != Mode::RescueMaiden && now != Mode::TowerBarrage)
		{
			tag_v_lite47.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_a_dvtraa.Component->Message(ft::GateEnable, 0.0f);
			tag_a_dvtrab.Component->Message(ft::GateDisable, 0.0f);

			tag_v_lite125.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite126.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
			tag_a_dvtrda.Component->Message(ft::GateEnable, 0.0f);
			tag_a_dvtrdb.Component->Message(ft::GateDisable, 0.0f);
		}
		return;
	}
	else if (code != ft::MissionStarted)
	{
		return;
	}

	Display(tag_info_text_box.Component, GetRcString(STR_TOWER_BARRAGE), Forever);
}

// 0x0042b3c0 -- Gargoyle Gaggle-Ball (string 3062), one extra ball.
//
// The cheap multiball: entered from the gargoyle kickout while idle, or from the
// skill shot on a_spot22. It pays 100,000 for starting, serves one ball out of fire
// lizard sink 1 and flashes both lizard lights, and lights the "2" lamp.
//
// The a_kout2 feed is conditional on KeepAnnouncement: when the mode was entered
// from the skill shot the ball is still in the gargoyle kickout and has to be
// released, and when it was entered by the kickout's own collision it is not.
void GargoyleGaggleController(MessageCode code)
{
	if (code == ft::MissionComplete)
	{
		BeginQuestPlay();
		TableG->MultiballCount += 1;

		tag_a_sink1.Component->Message(ft::SinkResetTimer, Forever);
		if (!KeepAnnouncement)
			tag_a_kout2.Component->Message(ft::KickoutRestartTimer, Forever);

		tag_v_lite66.Component->Message(ft::LightFlasherStartTimed, 1.0f);
		tag_v_lite65.Component->Message(ft::LightFlasherStartTimed, 1.0f);
		AddScoreAt(nullptr, 100000);
		tag_v_lite77.Component->Message(ft::LightFlashTimedThenStayOn, Forever);  // the "2" lamp
		ClearText(tag_info_text_box.Component);
		Play(tag_tabc48wave.Component);
		midi::PlayMusic(gargoyle_gaggle_music);
	}
	else if (code == ft::QuestAbort)
	{
		if (TableG->MultiballCount != 1)
			return;

		EndQuestPlay();
		tag_v_lite77.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		SetMode(Mode::Idle);
		MissionHandler(ft::MissionComplete, nullptr);
		// The two lizard-sink lights go out after the handover, not before: Idle's
		// own arm flashes them and this is what leaves them dark.
		tag_v_lite63.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite64.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		return;
	}
	else if (code != ft::MissionStarted)
	{
		return;
	}

	Display(tag_info_text_box.Component, GetRcString(STR_GARGOYLE_GAGGLE), Forever);
}

// 0x0042b510 -- Dragon Flurry (string 3063), four extra balls.
//
// The big one: entered by lighting all six D-R-A-G-O-N letters (CheckDragonFlurry)
// or by slaying the dragon. Every feed on the table is armed at once -- both fire
// lizard sinks, the well and the prison tower -- and the six letters are turned on
// and then set flashing, which is the mode's own display.
//
// The +4 becomes +3 when KeepAnnouncement is set, with a PlungerRelaunchBall in its
// place: one of the four balls is the one still sitting in the launch lane, so the
// plunger serves it instead of the table conjuring a fifth.
void DragonFlurryController(MessageCode code)
{
	TLight* const letters[6] = {
		tag_v_lite22.Component, tag_v_lite23.Component, tag_v_lite24.Component,
		tag_v_lite25.Component, tag_v_lite26.Component, tag_v_lite27.Component,
	};

	if (code == ft::MissionComplete)
	{
		BeginQuestPlay();
		TableG->MultiballCount += 4;
		if (KeepAnnouncement)
		{
			TableG->MultiballCount -= 1;
			tag_s_plunger1.Component->Message(ft::PlungerRelaunchBall, PlungerFeedDelay);
		}

		tag_a_sink1.Component->Message(ft::SinkResetTimer, Forever);
		tag_v_lite66.Component->Message(ft::LightFlasherStartTimed, 1.0f);
		tag_a_sink2.Component->Message(ft::SinkResetTimer, Forever);
		tag_v_lite65.Component->Message(ft::LightFlasherStartTimed, 1.0f);
		tag_a_sink4.Component->Message(ft::SinkResetTimer, 1.0f);
		tag_well_light_group.Component->Message(ft::LightFlasherStartTimed, 1.0f);
		tag_a_sink18.Component->Message(ft::SinkResetTimer, 1.0f);
		tag_v_lite48.Component->Message(ft::LightFlashTimedThenStayOff, 1.0f);

		// Drop the quest machinery's override on the letter bank, then take the six
		// letters solid-on and immediately set them flashing. Turning them on first
		// matters: a letter that was already lit by a bank completion and one lit by
		// slaying the dragon have to look the same for the duration.
		tag_dragon_letter_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		for (int i = 0; i < 6; i++)
			letters[i]->Message(ft::LightTurnOn, 0.0f);
		for (int i = 0; i < 6; i++)
			letters[i]->Message(ft::LightFlasherStartTimed, Forever);

		tag_v_lite79.Component->Message(ft::LightFlashTimedThenStayOn, Forever);  // the "5" lamp
		ClearText(tag_info_text_box.Component);
		PlayWave(tag_tabc61wave.Component);
	}
	else if (code == ft::QuestAbort)
	{
		if (TableG->MultiballCount != 1)
			return;

		EndQuestPlay();
		tag_v_lite79.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		// The letters are consumed: the bank has to be re-collected for the next one.
		for (int i = 0; i < 6; i++)
			letters[i]->Message(ft::LightResetAndTurnOff, 0.0f);

		SetMode(Mode::Idle);
		MissionHandler(ft::MissionComplete, nullptr);
		return;
	}
	else if (code != ft::MissionStarted)
	{
		return;
	}

	Display(tag_info_text_box.Component, GetRcString(STR_DRAGON_FLURRY), Forever);
}

// ---------------------------------------------------------------------------
// Mode 0xb -- the drain bonus
// ---------------------------------------------------------------------------

// Narrate one bonus line and return true, or return false when the count-up is
// running silently. Each line is shown for two seconds and the next step arrives
// with the text box's own timer expiry, remapped to MissionStarted by
// MissionHandler -- which is what turns the loop below into one line per step.
//
// UNVERIFIED value: 0x42c76f takes a label plus ONE number and that number is the
// argument every decompile of every caller drops off the x87 stack. It is almost
// certainly this step's own term (a bonus count-up that printed the running total
// against "Target Bonus" would read wrongly), but nothing in the pseudocode says
// so, so this calls FormatWithValue exactly as dragon_missions.cpp's
// DisplayTimeRemaining does and passes no value of its own. Settled by
// disassembling 0x42c76f.
static bool AnnounceBonusLine(int stringId, TPinballComponent* wave)
{
	if (BonusQuiet != 0)
		return false;

	char buffer[128];
	FormatWithValue(buffer, GetRcString(stringId));
	Display(tag_info_text_box.Component, buffer, 2.0f);
	PlayWave(wave);
	return true;
}

// No address -- the `default` arm of 0x0042b7c0's step switch, i.e. everything
// after the last bonus line has been paid. Split out because it is a page of
// end-of-ball bookkeeping that has nothing to do with the count-up.
//
// Three outcomes: an extra ball is owed and is served with a "Player n Shoot Again"
// banner; or the ball is spent and this was the last ball of the last player, which
// hands over to mode 0 (game over); or the next ball/player is served.
static void FinishBall()
{
	// An extra ball or a locked ball released by the drain's game-over path can put
	// balls back in play after the bonus started. game_over_phase distinguishes
	// "balls are live, wait" from "those are the game-over balls, proceed".
	if (game_over_phase != 0 && TableG->MultiballCount != 0)
		return;
	game_over_phase = 0;
	tag_mode_timer.Component->Message(ft::TimerSet, Forever);

	if (TableG->ExtraBalls == 0)  // table+0x18e
	{
		TableG->ChangeBallCount(TableG->BallCount - 1);  // 0x419fd0

		// Last player, no balls left.
		if (TableG->CurrentPlayer - TableG->PlayerCount == -1 && TableG->BallCount == 0)
		{
			TableG->LightGroup->Message(ft::LightResetAndTurnOff, 0.0f);
			SetMode(Mode::GameOver);
			MissionHandler(ft::MissionComplete, nullptr);
		}
		else
		{
			TableG->Message(ft::ClearTiltLock, 0.0f);
			// SetTiltLock to the drain, not the table: it stops the drain's own
			// pending feed timer so the plunger below is the only thing that serves.
			tag_drain.Component->Message(ft::SetTiltLock, 0.0f);
			TableG->Message(ft::SwitchToNextPlayer, 0.0f);
			tag_s_plunger1.Component->Message(ft::PlungerStartFeedTimer, PlungerFeedDelay);
		}
	}
	else
	{
		// An extra ball: same player, same ball number, no SwitchToNextPlayer and no
		// ChangeBallCount.
		TableG->ExtraBalls -= 1;

		int player = TableG->CurrentPlayer;
		if (player < 0 || player > 3)
			player = 3;  // the original's else-arm: anything but 0/1/2 prints player 4
		Display(tag_info_text_box.Component, GetRcString(STR_SHOOT_AGAIN_P1 + player), Forever);

		TableG->Message(ft::ClearTiltLock, 0.0f);
		tag_s_plunger1.Component->Message(ft::PlungerStartFeedTimer, PlungerFeedDelay);
	}

	// Release the master light group's override on every path, undoing the
	// LightOverrideOff the count-up opened with.
	TableG->LightGroup->Message(ft::LightClearOverride, 0.0f);
}

// 0x0042b7c0 -- the drain bonus count-up.
//
// Eight terms, one per step, each announced for two seconds before the next is
// added; a step whose term is zero is skipped silently in the same call, which is
// why the body is a loop and not one step per message. The accumulator is paid out
// at step 7 and held over to the next ball if Resistance to Fire (v_lite31) is lit.
//
// Entered by the drain handler writing mode 0xb and broadcasting 0x4f. Three things
// can advance it: the text box's display timer (arriving as MissionStarted), a
// flipper press (code 1), which latches BonusQuiet and runs the whole sequence
// through in one go, and mode_timer's 30 s backstop, which jumps straight to the
// end-of-ball step so a stuck count-up cannot hang the game.
void DrainBonusController(MessageCode code, TPinballComponent* caller)
{
	if (code == ft::TimerExpired)
	{
		// Only the mode's own backstop; a text-box expiry has already been remapped
		// to MissionStarted by MissionHandler before it gets here.
		if (caller != tag_mode_timer.Component)
			return;
		caller = nullptr;
		game_over_phase = 0;
		BonusStep = 8;
		code = ft::MissionStarted;
	}
	else if (code == ft::FlipperPressed)
	{
		BonusQuiet = 1;
		ClearText(tag_info_text_box.Component);
		code = ft::MissionStarted;
	}

	if (code == ft::MissionComplete)
	{
		BonusQuiet = 0;
		BonusStep = 0;
		BonusTotal = 0;
		tag_mode_timer.Component->Message(ft::TimerSet, 30.0f);

		// Tilted: no bonus at all. Phase 1 is "the last ball drained clean" and still
		// pays, which is the only reason the drain bothers to tell 1 from 2.
		if (TableG->TiltLockFlag != 0 && game_over_phase != 1)
			BonusStep = 8;

		TableG->LightGroup->Message(ft::LightOverrideOff, 0.0f);
		ClearText(tag_info_text_box.Component);
		PlayWave(tag_tabc44wave.Component);
	}
	else if (code == ft::QuestAbort)
	{
		// Another ball drained mid-count-up. Ignored unless the sequence is already
		// at the end-of-ball step, where it is what releases the next ball.
		if (BonusStep < 8)
			return;
	}
	else if (code != ft::MissionStarted)
	{
		return;
	}

	for (;;)
	{
		const int step = BonusStep++;
		switch (step)
		{
		case 0:
			// 2,000 per target hit this ball. The manual calls it "per target
			// dropped"; Dragon has no drop targets and MissionHandler counts
			// collisions with popup and solo targets instead.
			if (TargetsHitThisBall != 0)
			{
				BonusTotal += TargetsHitThisBall * 2000;
				if (AnnounceBonusLine(STR_TARGET_BONUS, tag_tabc06wave.Component))
					return;
			}
			break;

		case 1:
		{
			// 100,000 per quest completed this GAME, not this ball -- v_lite28's
			// MessageField is never reset per ball.
			const int quests = QuestsCompleted();
			// The selection is dropped here, mid-count-up, so the next ball's
			// plunger picks a fresh quest. Reading the counter first matters.
			UnlightSelectedQuest();
			if (quests != 0)
			{
				BonusTotal += quests * 100000;
				if (AnnounceBonusLine(STR_QUEST_BONUS, tag_tabc06wave.Component))
					return;
			}
			break;
		}

		case 2:
		{
			// 50,000 per lit armor light.
			TLight* const armor[3] = {
				tag_v_lite37.Component, tag_v_lite38.Component, tag_v_lite39.Component,
			};
			int lit = 0;
			for (int i = 0; i < 3; i++)
				if (Owned(armor[i]))
					lit++;
			if (lit != 0)
			{
				BonusTotal += lit * 50000;
				if (AnnounceBonusLine(STR_ARMOR_BONUS, tag_tabc06wave.Component))
					return;
			}
			break;
		}

		case 3:
			// 75,000 per spell cast this ball.
			if (SpellsCastThisBall != 0)
			{
				BonusTotal += SpellsCastThisBall * 75000;
				if (AnnounceBonusLine(STR_SPELL_BONUS, tag_tabc06wave.Component))
					return;
			}
			break;

		case 4:
		{
			// 150,000 per dragon egg still held.
			const int eggs = tag_dragon_egg_light_group.Component->Message(ft::GroupGetOnCount, 0.0f);
			if (eggs != 0)
			{
				BonusTotal += eggs * 150000;
				if (AnnounceBonusLine(STR_DRAGON_EGG_BONUS, tag_tabc06wave.Component))
					return;
			}
			break;
		}

		case 5:
			// "Weapon Bonus" = multiplier x 75,000, and the multiplier IS the weapon
			// count plus one (the weapon track writes 2..5 into table+0x15e). Skipped
			// at the neutral value, so no weapons means no line.
			if (TableG->ScoreMultiplier > 1)
			{
				BonusTotal += TableG->ScoreMultiplier * 75000;
				if (AnnounceBonusLine(STR_WEAPON_BONUS, tag_tabc06wave.Component))
					return;
			}
			break;

		case 6:
		{
			// The bonus held over from the previous ball by Resistance to Fire,
			// parked in drain->MessageField. Added as-is, not multiplied by anything.
			const int held = tag_drain.Component->MessageField;
			if (held != 0)
			{
				BonusTotal += held;
				if (AnnounceBonusLine(STR_BONUS_HOLD, tag_tabc06wave.Component))
					return;
			}
			break;
		}

		case 7:
			// Pay out, and decide what the next ball inherits. Resistance to Fire
			// (spell slot 2) is spent in the act: the light goes out and the total is
			// stored for next ball's step 6.
			if (!Owned(tag_v_lite31.Component))
			{
				tag_drain.Component->MessageField = 0;
			}
			else
			{
				tag_v_lite31.Component->Message(ft::LightResetAndTurnOff, 0.0f);
				tag_drain.Component->MessageField = BonusTotal;
			}

			if (BonusTotal != 0)
			{
				// Unmultiplied: the terms above already carry the multiplier through
				// the weapon line, so the total must not be scaled again.
				DragonSpecialAddScore(caller, BonusTotal);
				if (AnnounceBonusLine(STR_TOTAL_BONUS, tag_tabc13wave.Component))
					return;
			}
			break;

		default:
			FinishBall();
			return;
		}
	}
}

// ---------------------------------------------------------------------------
// Mode 0 -- game over
// ---------------------------------------------------------------------------

// 0x0042bf60 -- the end-of-game sequence. Not a bonus state: the bonus has already
// been paid by mode 0xb, which is what wrote this mode and broadcast 0x4f.
//
// Only MissionComplete is answered, so the mode is deaf afterwards and simply parks
// the table until a new game. The drain handler relies on that: once the mode
// register reads 0, nothing may disturb it.
void GameOverController(MessageCode code)
{
	if (code != ft::MissionComplete)
		return;

	if (midi::CurrentMusic() != game_over_music)
		midi::PlayMusic(game_over_music);

	TableG->Message(ft::TableGameOver, 0.0f);

	// The announcement is conditional on there being a high score to beat at all --
	// on a fresh install the table says nothing.
	if (!AnyHighScore())
		return;

	Play(tag_tabc56wave.Component);
	// A three-second flash on the master light group, then everything off. Upstream
	// does exactly this in the game-over half of BallDrainControl
	// (TableG->LightGroup->Message(TLightFlasherStartTimedThenStayOff, 3.0)), which
	// is what identifies table+0x13e as TPinballTable::LightGroup.
	TableG->LightGroup->Message(ft::LightFlashTimedThenStayOff, 3.0f);
	Display(tag_info_text_box.Component, GetRcString(STR_HIGH_SCORE), Forever);
}

}  // namespace dragon
