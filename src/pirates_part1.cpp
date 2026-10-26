// pirates_part1.cpp -- Skulduggery scoring rules, part 1: the bumpers and rebounders,
// all eleven drop targets with their three bank routines, all seven solo targets
// (a_spot*) and all ten rollovers (a_roll*).
//
// Reimplemented from PIRATES.EXE (win95 build) in the shape dragon_part1.cpp established:
// free functions in a per-table namespace, MessageCode spelled out, the mode register read
// as info_text_box->MessageField against named Mode* constants, and Pirates' own resource
// ids passed through PiratesMsg(). Bodies here replace the generated stubs in
// src/control_pirates.cpp; each handler's doc block opens with its original address.
//
// Sources: analysis/pirates-rules.md (which already establishes the mode register, the
// 16 states, the hybrid bitmask/light-count collectible storage, the one-to-one mapping of
// the 11 SKULDUGGERY letters onto the 11 TPopupTargets, and the dispatcher's unconditional
// jackpot accrual) plus decomp/pirates/functions/*.c for control flow and
// decomp/pirates/asm/*.txt for everything the decompiler mis-models.
//
// Every stack-argument value below was re-read from the listings, because Ghidra models
// these __stdcall handlers with one parameter too few and then renders the argument pushes
// as `unaff_retaddr`, `(int *)0x0`, `piVar3 = (int *)0x39` and similar. The listings
// disagree with the pseudocode at fourteen call sites in this slice, every one of them a
// scoring call whose caller Ghidra dropped; each is noted where it matters. Likewise
// FUN_0042dfaf is sprintf and its varargs are invisible to the decompiler, so the one
// formatted value recovered in this slice (the bumper line) came out of the instruction
// stream.
//
// Message codes: Full Tilt's numbering is Space Cadet's with a piecewise offset -- TLight
// codes identical, the TLightGroup/TSoloTarget/TPopupTarget/TBlocker block +4, the
// gate/kickout/sink block +9, Control* +10 and ControlCollision (0x4a) +11, mission codes
// +13. Only TLight and the +4 block appear in this slice, plus the two FT-only timer codes
// the captive ball uses.
//
// Nothing here links yet: the per-table control interface (a control class per table, its
// own component tag table and string ids) does not exist in the port.

#include "pch.h"
#include "control.h"

#include <cstdio>

#include "pb.h"
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates {

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
//
// This is the first Skulduggery slice, so -- following dragon_part1.cpp -- it *defines*
// the tags for every component the generated skeleton does not list (the skeleton lists
// only the 74 scoring components). Later slices should declare these as plain
// `extern TLight* lite1;`-style pointers rather than defining a second tag, exactly as
// dragon_part3.cpp does.

component_tag<TTextBox> tag_info_text_box = {"info_text_box"};

// SKULDUGGERY letter lights. v_lite1 doubles as the 11-bit letter mask
// (v_lite1->MessageField), and v_lite1..v_lite11 are its eleven display lights, in the
// bank order tavern(0..2) / ship(3..7) / parrot(8..10).
component_tag<TLight> tag_v_lite1 = {"v_lite1"};
component_tag<TLight> tag_v_lite2 = {"v_lite2"};
component_tag<TLight> tag_v_lite3 = {"v_lite3"};
component_tag<TLight> tag_v_lite4 = {"v_lite4"};
component_tag<TLight> tag_v_lite5 = {"v_lite5"};
component_tag<TLight> tag_v_lite6 = {"v_lite6"};
component_tag<TLight> tag_v_lite7 = {"v_lite7"};
component_tag<TLight> tag_v_lite8 = {"v_lite8"};
component_tag<TLight> tag_v_lite9 = {"v_lite9"};
component_tag<TLight> tag_v_lite10 = {"v_lite10"};
component_tag<TLight> tag_v_lite11 = {"v_lite11"};

component_tag<TLight> tag_v_lite84 = {"v_lite84"};  // left rebounder
component_tag<TLight> tag_v_lite85 = {"v_lite85"};  // right rebounder

// The pair of route indicators every drop-bank completion swaps.
component_tag<TLight> tag_v_lite504 = {"v_lite504"};
component_tag<TLight> tag_v_lite505 = {"v_lite505"};

// a_spot4/a_spot5's two-target bank. v_lite522 holds the 2-bit mask as well as being
// a_spot4's own light; v_lite779 is a_spot5's.
component_tag<TLight> tag_v_lite522 = {"v_lite522"};
component_tag<TLight> tag_v_lite779 = {"v_lite779"};

// Brig lights, stepped by the brig target (a_spot6).
component_tag<TLight> tag_v_lite530 = {"v_lite530"};
component_tag<TLight> tag_v_lite531 = {"v_lite531"};
component_tag<TLight> tag_v_lite532 = {"v_lite532"};

// W-I-N-D-S letters. Only I/N/D/S are lit from this slice's rollovers; the W
// (v_lite533) belongs to s_trip11, which is another slice's handler.
component_tag<TLight> tag_v_lite534 = {"v_lite534"}; // I -- a_roll5
component_tag<TLight> tag_v_lite535 = {"v_lite535"}; // N -- a_roll1
component_tag<TLight> tag_v_lite536 = {"v_lite536"}; // D -- a_roll2
component_tag<TLight> tag_v_lite537 = {"v_lite537"}; // S -- a_roll3

// Island lanes: three lanes, each with a "top" and a "bottom" light.
component_tag<TLight> tag_v_lite538 = {"v_lite538"};
component_tag<TLight> tag_v_lite539 = {"v_lite539"};
component_tag<TLight> tag_v_lite540 = {"v_lite540"};
component_tag<TLight> tag_v_lite541 = {"v_lite541"};
component_tag<TLight> tag_v_lite542 = {"v_lite542"};
component_tag<TLight> tag_v_lite543 = {"v_lite543"};

component_tag<TLight> tag_v_lite544 = {"v_lite544"}; // lit with a_kick2's re-arm

// Volcano bank display lights. The mask lives in a_spot11->MessageField, not here.
component_tag<TLight> tag_v_lite548 = {"v_lite548"};
component_tag<TLight> tag_v_lite549 = {"v_lite549"};
component_tag<TLight> tag_v_lite550 = {"v_lite550"};
component_tag<TLight> tag_v_lite551 = {"v_lite551"};

component_tag<TLight> tag_v_lite607 = {"v_lite607"}; // Mode Change light

// Wind-lane indicator pairs. The *first* light of each pair is the one Bermuda Triangle
// flashes to mark the indicated lane, so its FlasherOnFlag is the lane's "indicated" test.
component_tag<TLight> tag_v_lite615 = {"v_lite615"};
component_tag<TLight> tag_v_lite616 = {"v_lite616"}; // east wind 1 indicator
component_tag<TLight> tag_v_lite618 = {"v_lite618"};
component_tag<TLight> tag_v_lite619 = {"v_lite619"}; // east wind 2 indicator
component_tag<TLight> tag_v_lite621 = {"v_lite621"};
component_tag<TLight> tag_v_lite622 = {"v_lite622"}; // south wind indicator
component_tag<TLight> tag_v_lite623 = {"v_lite623"}; // west wind indicator
component_tag<TLight> tag_v_lite624 = {"v_lite624"};

component_tag<TLightGroup> tag_south_wind_light_group = {"south_wind_light_group"};
component_tag<TLightGroup> tag_east_wind_1_light_group = {"east_wind_1_light_group"};
component_tag<TLightGroup> tag_east_wind_2_light_group = {"east_wind_2_light_group"};
component_tag<TLightGroup> tag_west_wind_light_group = {"west_wind_light_group"};
component_tag<TLightGroup> tag_island_lane_top_light_group = {"island_lane_top_light_group"};
component_tag<TLightGroup> tag_island_lane_bottom_light_group = {"island_lane_bottom_light_group"};
component_tag<TLightGroup> tag_fathom_light_group = {"fathom_light_group"};

// The four blockers every drop-bank completion drives. They route the ball out of the
// bank; each bank passes a different set of values (see DropBankCompleted*).
component_tag<TBlocker> tag_a_blocc = {"a_blocc"};
component_tag<TBlocker> tag_a_bloce = {"a_bloce"};
component_tag<TBlocker> tag_a_blocf = {"a_blocf"};
component_tag<TBlocker> tag_a_blocg = {"a_blocg"};

component_tag<TPinballComponent> tag_mode_timer = {"mode_timer"};
component_tag<TPinballComponent> tag_s_bwal610 = {"s_bwal610"};

component_tag<TSound> tag_tabb02wave = {"tabb02wave"};
component_tag<TSound> tag_tabb07wave = {"tabb07wave"};
component_tag<TSound> tag_tabb12wave = {"tabb12wave"};
component_tag<TSound> tag_tabb16wave = {"tabb16wave"};
component_tag<TSound> tag_tabb17wave = {"tabb17wave"};
component_tag<TSound> tag_tabb19wave = {"tabb19wave"};
component_tag<TSound> tag_tabb22wave = {"tabb22wave"};
component_tag<TSound> tag_tabb23wave = {"tabb23wave"};
component_tag<TSound> tag_tabb24wave = {"tabb24wave"};
component_tag<TSound> tag_tabb29wave = {"tabb29wave"};
component_tag<TSound> tag_tabb43wave = {"tabb43wave"};
component_tag<TSound> tag_tabb44wave = {"tabb44wave"};
component_tag<TSound> tag_tabb61wave = {"tabb61wave"};
component_tag<TSound> tag_tabb74wave = {"tabb74wave"};
component_tag<TSound> tag_tabb87wave = {"tabb87wave"};
component_tag<TSound> tag_tabb88wave = {"tabb88wave"};
component_tag<TSound> tag_tabb94wave = {"tabb94wave"};

// Scoring components: tags live in control_pirates.cpp.
extern component_tag<TPinballComponent> tag_a_bump1;
extern component_tag<TPinballComponent> tag_a_bump2;
extern component_tag<TPinballComponent> tag_a_drop5;
extern component_tag<TPinballComponent> tag_a_drop13;
extern component_tag<TPinballComponent> tag_a_drop10;
extern component_tag<TPinballComponent> tag_a_spot8;
extern component_tag<TPinballComponent> tag_a_spot11;
extern component_tag<TPinballComponent> tag_a_spot6;
extern component_tag<TPinballComponent> tag_a_kick2;

TTextBox*& info_text_box = tag_info_text_box.Component;
TLight*& lite1 = tag_v_lite1.Component;
TLight*& lite2 = tag_v_lite2.Component;
TLight*& lite3 = tag_v_lite3.Component;
TLight*& lite4 = tag_v_lite4.Component;
TLight*& lite5 = tag_v_lite5.Component;
TLight*& lite6 = tag_v_lite6.Component;
TLight*& lite7 = tag_v_lite7.Component;
TLight*& lite8 = tag_v_lite8.Component;
TLight*& lite9 = tag_v_lite9.Component;
TLight*& lite10 = tag_v_lite10.Component;
TLight*& lite11 = tag_v_lite11.Component;
TLight*& lite84 = tag_v_lite84.Component;
TLight*& lite85 = tag_v_lite85.Component;
TLight*& lite504 = tag_v_lite504.Component;
TLight*& lite505 = tag_v_lite505.Component;
TLight*& lite522 = tag_v_lite522.Component;
TLight*& lite779 = tag_v_lite779.Component;
TLight*& lite530 = tag_v_lite530.Component;
TLight*& lite531 = tag_v_lite531.Component;
TLight*& lite532 = tag_v_lite532.Component;
TLight*& lite534 = tag_v_lite534.Component;
TLight*& lite535 = tag_v_lite535.Component;
TLight*& lite536 = tag_v_lite536.Component;
TLight*& lite537 = tag_v_lite537.Component;
TLight*& lite538 = tag_v_lite538.Component;
TLight*& lite539 = tag_v_lite539.Component;
TLight*& lite540 = tag_v_lite540.Component;
TLight*& lite541 = tag_v_lite541.Component;
TLight*& lite542 = tag_v_lite542.Component;
TLight*& lite543 = tag_v_lite543.Component;
TLight*& lite544 = tag_v_lite544.Component;
TLight*& lite548 = tag_v_lite548.Component;
TLight*& lite549 = tag_v_lite549.Component;
TLight*& lite550 = tag_v_lite550.Component;
TLight*& lite551 = tag_v_lite551.Component;
TLight*& lite607 = tag_v_lite607.Component;
TLight*& lite615 = tag_v_lite615.Component;
TLight*& lite616 = tag_v_lite616.Component;
TLight*& lite618 = tag_v_lite618.Component;
TLight*& lite619 = tag_v_lite619.Component;
TLight*& lite621 = tag_v_lite621.Component;
TLight*& lite622 = tag_v_lite622.Component;
TLight*& lite623 = tag_v_lite623.Component;
TLight*& lite624 = tag_v_lite624.Component;
TLightGroup*& south_wind_lights = tag_south_wind_light_group.Component;
TLightGroup*& east_wind_1_lights = tag_east_wind_1_light_group.Component;
TLightGroup*& east_wind_2_lights = tag_east_wind_2_light_group.Component;
TLightGroup*& west_wind_lights = tag_west_wind_light_group.Component;
TLightGroup*& island_lane_top_lights = tag_island_lane_top_light_group.Component;
TLightGroup*& island_lane_bottom_lights = tag_island_lane_bottom_light_group.Component;
TLightGroup*& fathom_lights = tag_fathom_light_group.Component;
TBlocker*& blocC = tag_a_blocc.Component;
TBlocker*& blocE = tag_a_bloce.Component;
TBlocker*& blocF = tag_a_blocf.Component;
TBlocker*& blocG = tag_a_blocg.Component;
TPinballComponent*& mode_timer = tag_mode_timer.Component;
TPinballComponent*& bwal610 = tag_s_bwal610.Component;
TPinballComponent*& bump1 = tag_a_bump1.Component;
TPinballComponent*& bump2 = tag_a_bump2.Component;
TPinballComponent*& drop5 = tag_a_drop5.Component;
TPinballComponent*& drop13 = tag_a_drop13.Component;
TPinballComponent*& drop10 = tag_a_drop10.Component;
TPinballComponent*& spot8 = tag_a_spot8.Component;
TPinballComponent*& spot11 = tag_a_spot11.Component;
TPinballComponent*& spot6 = tag_a_spot6.Component;
TPinballComponent*& kick2 = tag_a_kick2.Component;
TSound*& tabb02wave = tag_tabb02wave.Component;
TSound*& tabb07wave = tag_tabb07wave.Component;
TSound*& tabb12wave = tag_tabb12wave.Component;
TSound*& tabb16wave = tag_tabb16wave.Component;
TSound*& tabb17wave = tag_tabb17wave.Component;
TSound*& tabb19wave = tag_tabb19wave.Component;
TSound*& tabb22wave = tag_tabb22wave.Component;
TSound*& tabb23wave = tag_tabb23wave.Component;
TSound*& tabb24wave = tag_tabb24wave.Component;
TSound*& tabb29wave = tag_tabb29wave.Component;
TSound*& tabb43wave = tag_tabb43wave.Component;
TSound*& tabb44wave = tag_tabb44wave.Component;
TSound*& tabb61wave = tag_tabb61wave.Component;
TSound*& tabb74wave = tag_tabb74wave.Component;
TSound*& tabb87wave = tag_tabb87wave.Component;
TSound*& tabb88wave = tag_tabb88wave.Component;
TSound*& tabb94wave = tag_tabb94wave.Component;

// UNVERIFIED: tabb02wave is reached through *two* distinct .data slots, DAT_0043c0b4
// (the tavern drop bank) and DAT_0043c15c (a_spot4/5 and a_spot6), both of which the
// annotation pass resolved to the same wave name. One tag is used for both here, which is
// right if PIRATES.DAT really does bind the same wave twice and wrong if the two slots are
// different waves whose names collided. Dumping PIRATES.DAT's sound table in .data order
// would settle it; nothing in the rules depends on which it is.

// ---------------------------------------------------------------------------
// Table-local message codes, mode register, string ids
// ---------------------------------------------------------------------------

namespace FtMessageCode {
// Sent to mode_timer. 0x45 sets the remaining time in seconds (negative cancels); 0x48
// queries it and *returns* milliseconds. Both sit above upstream's enum, which has only
// TTimerResetTimer, so they are FT-only. Named from the captive ball's use, which divides
// 0x48's answer by 1000.0 before feeding it back to 0x45; dragon_missions.cpp reached the
// same two names independently from Dragon's mode timer.
constexpr auto TimerSet = static_cast<MessageCode>(0x45);
constexpr auto TimerGetRemaining = static_cast<MessageCode>(0x48);
} // namespace FtMessageCode

// Mode register values of info_text_box->MessageField; see analysis/pirates-rules.md §1.3.
// Only the states this slice actually tests are named.
constexpr int ModeIdle = 2;          // no mode running: the normal collecting state
constexpr int ModeAttackShip = 4;
constexpr int ModeStorm = 6;
constexpr int ModeTavern = 7;
constexpr int ModeTreasureHunt = 8;
constexpr int ModeMutiny = 9;
constexpr int ModeSwordFight = 10;
constexpr int ModePegLegsLoot = 0xb;  // the highest state the captive ball extends
constexpr int ModeLavaBall = 0xc;
constexpr int ModeBermuda = 5;

// Raw PIRATES.EXE resource ids: Skulduggery has its own string table, so the ids are
// passed through rather than named in the port's Msg enum.
inline Msg PiratesMsg(int rcId) { return static_cast<Msg>(rcId); }

constexpr int Str_CrewAdded = 3001;      // 0xbb9, "Crew Added\nBumpers Now\n%ld"
constexpr int Str_SouEastern = 3010;     // 0xbc2, "Sou'Eastern\n%ld"
constexpr int Str_Easterly = 3011;       // 0xbc3, "Easterly\n%ld" -- both east lanes
constexpr int Str_Westerly = 3012;       // 0xbc4, "Westerly\n%ld"
constexpr int Str_ModeSelected0 = 3068;  // 0xbfc, +0..7 in *selection* order

// Single-character announcement tokens in .data that the text box expands itself, the same
// family as Dragon's "!" (dragon_part1.cpp). The original passes a fourth Display argument
// of 1 for these where resource strings pass 0; the port's TTextBox::Display has no such
// parameter, so they are passed as plain strings here.
//
// UNVERIFIED: what each token expands to. Dumping PIRATES.DAT's message groups would
// settle it; the rules do not depend on the text.
constexpr const char* Glyph_IslandLane = "6";  // DAT_0043aac4, a_roll5
constexpr const char* Glyph_ReturnLane = "7";  // DAT_0043aac8, a_roll1
constexpr const char* Glyph_SouthWind = "8";   // DAT_0043aacc, a_roll2
constexpr const char* Glyph_RightOutlane = "9"; // DAT_0043aad0, a_roll3

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------
//
// The three scoring wrappers return the amount actually credited -- the wrappers end with
// a tail call to TPinballTable::AddScore and no `mov eax`, and the 23 sprintf sites that
// format "%ld" push that eax. Callers in this slice use the return only for the bumper
// line; the rest discard it.

int AddScoreFor(TPinballComponent* caller, int index); // 0x004232c0, scoring[index]
int AddScoreAt(TPinballComponent* caller, int score);  // 0x00423310, a literal score
int AddScoreUnmultiplied(TPinballComponent* caller, int score); // 0x00423350

// 0x00424700 -- award the jackpot. A negative time is used as its magnitude and suppresses
// the sound; the return value is the time the announcement will occupy (t + 2.0), which
// callers chain into their own next Display. See the note on DropBankCompletedTavern.
float AwardJackpot(float time);

// Re-raise and re-enable a whole drop bank, clearing its bitmask. Shared with the mode
// controllers (Attack Ship, Storm, Tavern and Sword Fight all re-raise their bank at mode
// start), so they are declared, not defined, here.
void RaiseShipDropBank();   // 0x00424440, a_drop5..a_drop9
void RaiseTavernDropBank(); // 0x00424f70, a_drop13..a_drop15
void RaiseParrotDropBank(); // 0x004252d0, a_drop10..a_drop12

void ResetSkulduggeryLights(int mode);  // 0x00423b70
void StartSelectedMode();               // 0x00425a50
void StartLavaBall();                   // 0x00423610, sets state 0xc
void StartJailBreak();                  // 0x00423630, sets state 0xd
int SelectNextMode();                   // 0x00425850, returns the selection index 0..7
float AwardMapPiece();                  // 0x00423aa0
float AwardSpecialTreasureOrNext(float time); // 0x00425550, the Sword Fight group award
float ArmRandomKegMagnet();             // 0x00424d30, returns the keg group's on-count
float AwardMaxCrew(float time);         // 0x00425000, a_bump2->MessageField = 6
void ResetBrigLights(int mode);         // 0x00425d50
float CheckWindsComplete(float time);   // 0x00424290, awards a map piece on W-I-N-D-S
float CollectShipwreckMystery(float time); // 0x00425d10, right outlane only if lit
float BuryTreasureHuntAward(float time);   // 0x00424b00
void AwardIndicatedWind();              // 0x00425230, Bermuda's progressive
float AdvanceFathomMultiplier(float time); // 0x004243d0, island lanes' award

// Skulduggery's single per-mode scratch counter, DAT_00435cf8. It is a hit count in Mutiny
// and a bitfield in Sword Fight (bits 0..3 volcano, bits 4..6 parrot); FUN_0042aad0 zeroes
// it at every mode start, which is what keeps the two uses from meeting.
extern int ModeScratch; // DAT_00435cf8

// ---------------------------------------------------------------------------
// Formatted announcements
// ---------------------------------------------------------------------------

// The original's one fixed shape for a formatted line:
//   sprintf(buf, get_rc_string(id, 0), value); info_text_box->Display(buf, time);
// with get_rc_string at 0x00409730 (two arguments, not the one Ghidra models) and sprintf
// at 0x0042dfaf. Static so the other slices can keep their own copy.
static void DisplayWithValue(int rcId, int value, float time)
{
	char buffer[128];
	std::snprintf(buffer, sizeof buffer, pb::get_rc_string(PiratesMsg(rcId)), value);
	info_text_box->Display(buffer, time);
}

// ---------------------------------------------------------------------------
// Bumpers -- a_bump1/a_bump2/a_bump3, one handler, two counters
// ---------------------------------------------------------------------------

// 0x00426390 -- all three of a_bump1..a_bump3 (skeleton: a_bump1Control)
//
// Two cells, both parked in bumper MessageFields: a_bump1 counts hits 0..11 and a_bump2
// holds the crew level 0..6, which is also the index into the shared seven-entry scoring
// table {1500, 3000, 4500, 6000, 7500, 9000, 10500}. So every bumper hit pays the current
// crew level and every twelfth hit buys the next one.
void BumperControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	// Mutiny replaces bumper scoring wholesale: a flat unmultiplied 25,000 and a jackpot
	// on every tenth hit. The tenth hit pays *only* the jackpot -- the 25,000 is in the
	// else arm -- and the crew level is neither read nor advanced.
	if (mode == ModeMutiny)
	{
		if (++ModeScratch == 10)
		{
			ModeScratch = 0;
			AwardJackpot(0.0f);
			return;
		}
		AddScoreAt(caller, 25000);
		tabb74wave->Play(caller, "BumperControl");
		return;
	}

	// Outside idle play the crew counter is frozen: the hit still pays the current level
	// but never advances it. Only state 2 runs the counter.
	if (mode != ModeIdle)
	{
		tabb16wave->Play(caller, "BumperControl");
		AddScoreFor(caller, bump2->MessageField);
		return;
	}

	const int hits = bump1->MessageField + 1;
	if (hits == 12)
	{
		bump1->MessageField = 0;
		if (bump2->MessageField < 6)
		{
			const int level = bump2->MessageField + 1;
			bump2->MessageField = level;
			// The "%ld" is the *new* per-bumper value, read off the push immediately
			// before the get_rc_string call at 0x426415 -- Ghidra drops it.
			DisplayWithValue(Str_CrewAdded, caller->get_scoring(level), 2.0f);
		}
		// The twelfth-hit sound plays whether or not the crew actually advanced, so a
		// maxed-out crew still gets the fanfare.
		tabb17wave->Play(caller, "BumperControl");
	}
	else
	{
		tabb16wave->Play(caller, "BumperControl");
		bump1->MessageField = hits;
	}

	AddScoreFor(caller, bump2->MessageField);
}

// ---------------------------------------------------------------------------
// Flipper rebounders -- a_rebo1/a_rebo2
// ---------------------------------------------------------------------------

// 0x00427350 -- a_rebo1 (skeleton: a_rebo1Control)
void FlipperRebounderControl1(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite84->Message(MessageCode::TLightTurnOnTimed, 0.1f);
		AddScoreFor(caller, 0); // 350
	}
}

// 0x00427380 -- a_rebo2 (skeleton: a_rebo2Control)
void FlipperRebounderControl2(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		lite85->Message(MessageCode::TLightTurnOnTimed, 0.1f);
		AddScoreFor(caller, 0); // 350
	}
}

// ---------------------------------------------------------------------------
// The three drop-target banks
// ---------------------------------------------------------------------------
//
// Eleven TPopupTargets in three banks -- ship (a_drop5..a_drop9, 5), tavern
// (a_drop13..a_drop15, 3) and parrot (a_drop10..a_drop12, 3) -- and exactly eleven
// SKULDUGGERY letters. Each target's own handler does only three things: set its bit in
// its bank's mask, light its letter, and call its bank's shared routine. Everything that
// depends on the mode is in the bank routine.
//
// The bank mask lives in the first target's MessageField (a_drop5/a_drop13/a_drop10) and
// the letter mask in v_lite1's, so a bank is "complete" at mask == 0x1f / 0x7 / 0x7.
//
// Note what the dispatcher does before any of this is reached: every collision with a
// type-1006 TPopupTarget adds 20,000 to the jackpot and bumps the per-ball dropped-target
// count, unconditionally and regardless of mode (pirates-rules.md §6.0). So a target pays
// into the jackpot even in the Storm and Treasure Hunt arms below, which award it nothing.

// Both completion gates re-raise all three banks and then fork; see §2.2 and §4.5.
// Declared here because the bank routines call them before deciding anything else.
static bool SkulduggeryCompleteIdle();     // 0x00425de0
static bool SkulduggeryCompleteLavaBall(); // 0x00425e30

// 0x00425de0 -- all eleven letters lit, in idle play: clear the letters, re-raise all
// three banks and start the selected mode. Returns true when it fired, in which case the
// calling bank routine must not score the hit.
static bool SkulduggeryCompleteIdle()
{
	if ((lite1->MessageField & 0x7ff) != 0x7ff)
		return false;

	// DAT_00435d30 ("not in the skill shot") is latched here so the skill-shot controller
	// cannot claim the ball that just spelled the word.
	// UNVERIFIED: the flag's exact meaning -- it is set in one other place
	// (FUN_0042abf0's 0x4f) and read by the cheat handler, and no reader in this slice
	// depends on it, so it is left to whichever slice owns the launcher.
	ResetSkulduggeryLights(1);
	RaiseTavernDropBank();
	RaiseShipDropBank();
	RaiseParrotDropBank();
	StartSelectedMode();
	return true;
}

// 0x00425e30 -- the same completion during Lava-Ball, which pays a map piece instead of
// starting a mode (a mode is already running). Lava-Ball is the only non-idle state in
// which the letter lights are touched at all.
static bool SkulduggeryCompleteLavaBall()
{
	if ((lite1->MessageField & 0x7ff) != 0x7ff)
		return false;

	ResetSkulduggeryLights(1);
	RaiseTavernDropBank();
	RaiseShipDropBank();
	RaiseParrotDropBank();
	AwardMapPiece();
	return true;
}

// The blocker/indicator swap every bank completion performs outside a scoring mode. All
// four blockers get code 0x39 and the two route indicators swap; the three banks pass
// *different* floats to a_blocc and a_blocf, which is the ball-path difference between
// them. Those floats are from the listings -- Ghidra rendered several of them as a stray
// `piVar3 = (int *)0x39`.
//
// 0x39 is treated as TBlockerRestartTimeout, following dragon_part2.cpp, which reached the
// same reading for Dragon's blockers; a negative value cancels the timeout there too.
// UNVERIFIED: by the +4 rule 0x39 would be upstream's TGateDisable (53), and
// TBlockerRestartTimeout (59) would be 0x3f. The components are named a_bloc*, every FT
// table drives them with this code, and 0x3f is already ControlCollision's neighbour, so
// the blocker reading is taken; it would be settled by the TBlocker class's own code,
// which is engine and outside this corpus.
static void SwapBankRoute(float blocCTime, float blocFTime)
{
	blocC->Message(MessageCode::TBlockerRestartTimeout, blocCTime);
	blocE->Message(MessageCode::TBlockerRestartTimeout, 1.0f);
	lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
	lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	blocF->Message(MessageCode::TBlockerRestartTimeout, blocFTime);
	blocG->Message(MessageCode::TBlockerRestartTimeout, 0.0f);
}

// --- Ship bank: a_drop5..a_drop9, scoring {3000, 25000} --------------------

// 0x004264f0 -- the shared routine of all five ship drop targets.
static void ShipDropBankHit(TPinballComponent* caller)
{
	const int mode = info_text_box->MessageField;

	// The two gated states check for a completed SKULDUGGERY first; if the word just
	// fired, the hit scores nothing at all.
	if (mode == ModeIdle && SkulduggeryCompleteIdle())
		return;
	if (mode == ModeLavaBall && SkulduggeryCompleteLavaBall())
		return;

	const bool complete = (drop5->MessageField & 0x1f) == 0x1f;

	if (mode == ModeAttackShip)
	{
		if (!complete)
		{
			tabb22wave->Play(caller, "ShipDropBankHit");
			AddScoreAt(caller, 10000);
			return;
		}
		// Ship sunk: re-raise the bank, pay the jackpot, light one more fathom and add a
		// flat 25,000. The jackpot's own announcement time is handed to the fathom group
		// as its message argument, so the group animation starts when the jackpot line
		// clears -- the same return-value chaining the tavern bank uses.
		RaiseShipDropBank();
		const float jackpotTime = AwardJackpot(0.0f);
		fathom_lights->Message(MessageCode::TLightGroupRestartNotifyTimer, jackpotTime);
		AddScoreAt(caller, 25000);
		return;
	}

	if (mode == ModeStorm)
	{
		// Storm scores every TPopupTarget from its own controller, so the bank routine
		// only has to put the targets back up. An incomplete bank does nothing here.
		if (complete)
			RaiseShipDropBank();
		return;
	}

	// Idle play, Tavern, Lava-Ball and every other state share one path. (The jump table
	// gives state 7 its own arm, but it is byte-identical to the default.)
	if (!complete)
	{
		AddScoreFor(caller, 0); // 3000
		tabb22wave->Play(caller, "ShipDropBankHit");
		return;
	}
	SwapBankRoute(0.0f, -1.0f);
	RaiseShipDropBank();
	AddScoreFor(caller, 1); // 25000 -- caller, not null: Ghidra's `(int *)0x0` is wrong
	tabb24wave->Play(caller, "ShipDropBankHit");
}

// 0x004268b0 -- a_drop5 (skeleton: a_drop5Control)
void ShipDropTargetControl5(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop5->MessageField |= 0x01;
	// The letter is only lit in idle play and during Lava-Ball; in a scoring mode the
	// target still drops and still feeds the jackpot, but spells nothing.
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x08;
		lite4->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ShipDropBankHit(caller);
}

// 0x00426900 -- a_drop6 (skeleton: a_drop6Control)
void ShipDropTargetControl6(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop5->MessageField |= 0x02;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x10;
		lite5->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ShipDropBankHit(caller);
}

// 0x00426950 -- a_drop7 (skeleton: a_drop7Control)
void ShipDropTargetControl7(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop5->MessageField |= 0x04;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x20;
		lite6->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ShipDropBankHit(caller);
}

// 0x004269a0 -- a_drop8 (skeleton: a_drop8Control)
void ShipDropTargetControl8(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop5->MessageField |= 0x08;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x40;
		lite7->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ShipDropBankHit(caller);
}

// 0x004269f0 -- a_drop9 (skeleton: a_drop9Control)
void ShipDropTargetControl9(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop5->MessageField |= 0x10;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x80;
		lite8->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ShipDropBankHit(caller);
}

// --- Tavern bank: a_drop13..a_drop15, scoring {4000, 30000} ----------------

// 0x00426a40 -- the shared routine of all three tavern drop targets.
static void TavernDropBankHit(TPinballComponent* caller)
{
	const int mode = info_text_box->MessageField;

	if (mode == ModeIdle && SkulduggeryCompleteIdle())
		return;
	if (mode == ModeLavaBall && SkulduggeryCompleteLavaBall())
		return;

	const bool complete = (drop13->MessageField & 0x7) == 0x7;

	if (mode == ModeStorm)
	{
		if (complete)
			RaiseTavernDropBank();
		return;
	}

	if (mode == ModeTavern)
	{
		if (!complete)
		{
			// Every tavern-target hit arms one more keg magnet; the returned keg on-count
			// is discarded on this path.
			ArmRandomKegMagnet();
			tabb87wave->Play(caller, "TavernDropBankHit");
			AddScoreAt(nullptr, 25000);
			return;
		}
		SwapBankRoute(0.0f, 0.0f);
		RaiseTavernDropBank();
		// AwardJackpot(ArmRandomKegMagnet()) -- the keg group's on-count becomes the
		// jackpot announcement's dwell, so the display lingers one second longer per lit
		// keg. That is what the instruction stream says (the float return is stored and
		// re-pushed at 0x426c80-0x426c89); it looks like the same "chain my announcement
		// after yours" idiom the ship bank and a_roll2 use, with the two helpers' return
		// conventions quietly disagreeing.
		AwardJackpot(ArmRandomKegMagnet());
		AddScoreAt(nullptr, 50000);
		return;
	}

	// Both scoring calls on this path pass a real caller; Ghidra's `piVar3` is noise.
	if (!complete)
	{
		AddScoreFor(caller, 0); // 4000
		tabb02wave->Play(caller, "TavernDropBankHit");
		return;
	}
	SwapBankRoute(0.0f, 0.0f);
	RaiseTavernDropBank();
	AddScoreFor(caller, 1); // 30000
	tabb29wave->Play(caller, "TavernDropBankHit");
}

// 0x00426dc0 -- a_drop13 (skeleton: a_drop13Control)
void TavernDropTargetControl13(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop13->MessageField |= 0x01;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		// Letter bit 0's display light is v_lite1 itself, the same component that holds
		// the mask.
		lite1->MessageField |= 0x01;
		lite1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	TavernDropBankHit(caller);
}

// 0x00426e10 -- a_drop14 (skeleton: a_drop14Control)
void TavernDropTargetControl14(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop13->MessageField |= 0x02;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x02;
		lite2->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	TavernDropBankHit(caller);
}

// 0x00426e60 -- a_drop15 (skeleton: a_drop15Control)
void TavernDropTargetControl15(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop13->MessageField |= 0x04;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x04;
		lite3->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	TavernDropBankHit(caller);
}

// --- Parrot bank: a_drop10..a_drop12, scoring {5000, 35000} ----------------

// 0x00426eb0 -- the shared routine of all three parrot drop targets.
static void ParrotDropBankHit(TPinballComponent* caller)
{
	const int mode = info_text_box->MessageField;

	if (mode == ModeIdle && SkulduggeryCompleteIdle())
		return;
	if (mode == ModeLavaBall && SkulduggeryCompleteLavaBall())
		return;

	const bool complete = (drop10->MessageField & 0x7) == 0x7;

	if (mode == ModeStorm)
	{
		if (complete)
			RaiseParrotDropBank();
		return;
	}

	if (mode == ModeSwordFight)
	{
		if (!complete)
		{
			AddScoreAt(nullptr, 30000);
			tabb94wave->Play(caller, "ParrotDropBankHit");
			return;
		}
		// The parrot group is bits 4..6 of the mode scratch. On the first clear the three
		// letters go solid; clear it a second time in the same Sword Fight and they only
		// flash, because the bits are already set and the award cycler has already been
		// told. Either way the bank is re-raised and the cycler runs.
		if ((ModeScratch & 0x70) == 0)
		{
			ModeScratch |= 0x70;
			lite9->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			lite10->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			lite11->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		}
		else
		{
			lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		}
		RaiseParrotDropBank();
		AwardSpecialTreasureOrNext(0.0f);
		tabb94wave->Play(caller, "ParrotDropBankHit");
		return;
	}

	if (mode == ModeTavern)
	{
		// Tavern is the one state where a bank completion does *not* swap the ball route:
		// the parrot bank just re-raises and pays. The other two banks run SwapBankRoute
		// in Tavern, so this asymmetry is deliberate rather than a missing branch.
		if (!complete)
		{
			tabb07wave->Play(caller, "ParrotDropBankHit");
			AddScoreFor(caller, 0); // 5000
			return;
		}
		RaiseParrotDropBank();
		AddScoreFor(caller, 1); // 35000
		tabb19wave->Play(caller, "ParrotDropBankHit");
		return;
	}

	if (!complete)
	{
		tabb07wave->Play(caller, "ParrotDropBankHit");
		AddScoreFor(caller, 0); // 5000
		return;
	}
	SwapBankRoute(-1.0f, 0.0f);
	RaiseParrotDropBank();
	AddScoreFor(caller, 1); // 35000
	tabb19wave->Play(caller, "ParrotDropBankHit");
}

// 0x00427260 -- a_drop10 (skeleton: a_drop10Control)
void ParrotDropTargetControl10(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop10->MessageField |= 0x01;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x100;
		lite9->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ParrotDropBankHit(caller);
}

// 0x004272b0 -- a_drop11 (skeleton: a_drop11Control)
void ParrotDropTargetControl11(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop10->MessageField |= 0x02;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x200;
		lite10->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ParrotDropBankHit(caller);
}

// 0x00427300 -- a_drop12 (skeleton: a_drop12Control)
void ParrotDropTargetControl12(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	drop10->MessageField |= 0x04;
	if (info_text_box->MessageField == ModeIdle || info_text_box->MessageField == ModeLavaBall)
	{
		lite1->MessageField |= 0x400;
		lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	}
	ParrotDropBankHit(caller);
}

// ---------------------------------------------------------------------------
// Volcano bank -- a_spot11/a_spot8/a_spot9/a_spot10, scoring {1000, 15000}
// ---------------------------------------------------------------------------
//
// Four TSoloTargets, a 4-bit mask in a_spot11->MessageField and the display lights
// v_lite551/550/549/548 in bit order. Clearing the bank in idle play starts Lava-Ball;
// clearing it during Sword Fight is one of the two ways to trigger that mode's award
// cycler. Each target also pays 20,000 into the jackpot from the dispatcher, before any of
// this runs.

// 0x00427540 -- the idle-play tail of all four volcano targets.
static void VolcanoBankHitIdle(TPinballComponent* caller)
{
	if ((spot11->MessageField & 0xf) == 0xf)
	{
		spot11->MessageField = 0;
		AddScoreFor(caller, 1); // 15000
		StartLavaBall();
		return;
	}
	AddScoreFor(caller, 0); // 1000
}

// 0x00425490 -- the Sword Fight tail of all four volcano targets.
//
// This is the "volcano group cleared" test, and as shipped it never passes: it reads
// v_lite550->MessageField, a field nothing in either decompiled corpus ever writes (the
// only two occurrences of the expression in 364 functions are the read and the clear in
// this one function). The four target handlers put their bits in ModeScratch instead.
// Confirmed from the listing at 0x425490: `mov eax,ds:0x43af84` is v_lite550, not
// a_spot11 (0x43af1c) where the real mask lives -- the same one-hex-digit class of bug as
// the v_lite551/a_spot11 mix-up in Jail Break (pirates-rules.md §3).
//
// The consequences are worth being deliberate about in a port:
//   * the live effect of this function in Sword Fight is only the flat 30,000;
//   * the four volcano lights are never swept (the TLightResetTimed/TLightFlasherStartTimed
//     block below is dead);
//   * but the *award* still happens, because AwardSpecialTreasureOrNext (0x00425550) runs
//     its own `ModeScratch & 0xf == 0xf` check on the bits the handlers really set.
// So Sword Fight's volcano objective works, by a different route than the code reads, and
// pays the flat 30,000 on the clearing hit as well.
//
// Also note the return: Ghidra renders the taken branch as `return 1.0`, but the listing
// ends both branches with `fld [esp+0x4]`, i.e. it returns its own argument unchanged.
// Every caller passes 0.0, so the value chained into AwardSpecialTreasureOrNext is always
// 0.0.
static float VolcanoBankHitSwordFight(float time)
{
	if ((lite550->MessageField & 0xf) == 0xf)
	{
		lite550->MessageField = 0;
		lite551->Message(MessageCode::TLightResetTimed, 0.0f);
		lite550->Message(MessageCode::TLightResetTimed, 0.0f);
		lite549->Message(MessageCode::TLightResetTimed, 0.0f);
		lite548->Message(MessageCode::TLightResetTimed, 0.0f);
		lite551->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		lite550->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		lite549->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		lite548->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return time;
	}
	AddScoreAt(nullptr, 30000);
	return time;
}

// 0x00427580 -- a_spot11 (skeleton: a_spot11Control)
void VolcanoTargetControl11(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;
	if (mode == ModeIdle)
	{
		spot11->MessageField |= 0x1;
		lite551->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		VolcanoBankHitIdle(caller);
		tabb23wave->Play(caller, "VolcanoTargetControl");
		return;
	}
	if (mode != ModeSwordFight)
	{
		tabb23wave->Play(caller, "VolcanoTargetControl");
		AddScoreFor(caller, 0); // 1000
		return;
	}
	ModeScratch |= 0x1;
	lite551->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	AwardSpecialTreasureOrNext(VolcanoBankHitSwordFight(0.0f));
	tabb94wave->Play(caller, "VolcanoTargetControl");
}

// 0x00427650 -- a_spot8 (skeleton: a_spot8Control)
void VolcanoTargetControl8(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;
	if (mode == ModeIdle)
	{
		// Only this one of the four plays its sound *before* scoring; cosmetic.
		tabb23wave->Play(caller, "VolcanoTargetControl");
		spot11->MessageField |= 0x2;
		lite550->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		VolcanoBankHitIdle(caller);
		return;
	}
	if (mode != ModeSwordFight)
	{
		tabb23wave->Play(caller, "VolcanoTargetControl");
		AddScoreFor(caller, 0); // 1000
		return;
	}
	ModeScratch |= 0x2;
	lite550->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	AwardSpecialTreasureOrNext(VolcanoBankHitSwordFight(0.0f));
	tabb94wave->Play(caller, "VolcanoTargetControl");
}

// 0x00427720 -- a_spot9 (skeleton: a_spot9Control)
void VolcanoTargetControl9(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;
	if (mode == ModeIdle)
	{
		spot11->MessageField |= 0x4;
		lite549->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		VolcanoBankHitIdle(caller);
		tabb23wave->Play(caller, "VolcanoTargetControl");
		return;
	}
	if (mode != ModeSwordFight)
	{
		tabb23wave->Play(caller, "VolcanoTargetControl");
		AddScoreFor(caller, 0); // 1000
		return;
	}
	ModeScratch |= 0x4;
	lite549->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	AwardSpecialTreasureOrNext(VolcanoBankHitSwordFight(0.0f));
	tabb94wave->Play(caller, "VolcanoTargetControl");
}

// 0x004277f0 -- a_spot10 (skeleton: a_spot10Control)
void VolcanoTargetControl10(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;
	if (mode == ModeIdle)
	{
		spot11->MessageField |= 0x8;
		lite548->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		VolcanoBankHitIdle(caller);
		tabb23wave->Play(caller, "VolcanoTargetControl");
		return;
	}
	if (mode != ModeSwordFight)
	{
		tabb23wave->Play(caller, "VolcanoTargetControl");
		AddScoreFor(caller, 0); // 1000
		return;
	}
	ModeScratch |= 0x8;
	lite548->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	// Unique to this target, and dead: a_spot8->MessageField is zeroed by the Sword Fight
	// setter (FUN_004235c0) and read nowhere in either corpus, so this is the third
	// address in the Sword Fight volcano muddle -- the setter clears a_spot8, one handler
	// writes a_spot8, and the completion test reads v_lite550. Confirmed in the listing
	// (`mov ecx,ds:0x43af2c; or [ecx+0x6],0x8` at 0x42788a), so it is in the original
	// source, not a decompiler artifact. A port can drop it with no behavioural change.
	spot8->MessageField |= 0x8;
	AwardSpecialTreasureOrNext(VolcanoBankHitSwordFight(0.0f));
	tabb94wave->Play(caller, "VolcanoTargetControl");
}

// ---------------------------------------------------------------------------
// Hideaway-entrance pair -- a_spot4/a_spot5, scoring {5000, 20000}
// ---------------------------------------------------------------------------
//
// Two targets guarding one passage, with a 2-bit mask in v_lite522->MessageField. Hitting
// both cancels a_blocf's timeout (which is what opens the passage) and retires the pair.
// Unlike every other bank here this one is mode-blind: there is no mode test anywhere in
// the three functions.

// 0x004278c0 -- the shared tail of a_spot4 and a_spot5.
//
// The completing hit scores 20,000 and the others 5,000, which falls out of the mask test
// being used directly as the scoring index -- the original computes `bVar1` once and passes
// it as both the branch condition and the index.
static void HideawayTargetPairHit(TPinballComponent* caller)
{
	const bool complete = (lite522->MessageField & 0x3) == 0x3;
	if (complete)
	{
		// A negative TBlockerRestartTimeout cancels the timeout, which is what leaves the
		// passage open; the pair then goes dark and the mask resets, so the next two hits
		// start the cycle over.
		blocF->Message(MessageCode::TBlockerRestartTimeout, -1.0f);
		lite522->MessageField = 0;
		lite522->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		lite779->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	}
	AddScoreFor(caller, complete ? 1 : 0); // 20000 / 5000
	tabb02wave->Play(caller, "HideawayTargetPairHit");
}

// 0x00427940 -- a_spot4 (skeleton: a_spot4Control)
void HideawayTargetControl4(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	// v_lite522 is both the mask holder and this target's own display light.
	lite522->MessageField |= 0x1;
	lite522->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	HideawayTargetPairHit(caller);
}

// 0x00427980 -- a_spot5 (skeleton: a_spot5Control)
void HideawayTargetControl5(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;
	lite522->MessageField |= 0x2;
	lite779->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	HideawayTargetPairHit(caller);
}

// ---------------------------------------------------------------------------
// Brig target -- a_spot6, scoring {5000}
// ---------------------------------------------------------------------------

// 0x004279b0 -- a_spot6 (skeleton: a_spot6Control)
//
// A three-hit counter in a_spot6->MessageField with two completely different payoffs: in
// idle play the third hit starts Jail Break, during Tavern it gives Max Crew. Every hit
// pays the same 5,000, and the target is inert in all other states (the handler has no
// default arm at all).
void BrigTargetControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	if (mode == ModeIdle)
	{
		if (spot6->MessageField == 0)
		{
			tabb02wave->Play(caller, "BrigTargetControl");
			spot6->MessageField = 1;
			AddScoreFor(caller, 0); // 5000
			lite530->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			return;
		}
		if (spot6->MessageField == 1)
		{
			tabb02wave->Play(caller, "BrigTargetControl");
			spot6->MessageField = 2;
			AddScoreFor(caller, 0); // 5000
			lite531->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
			return;
		}
		// Third hit. The brig may well be empty -- this handler does not check; Jail
		// Break's own 0x4f arm aborts straight back to idle when it is
		// (pirates-rules.md §3, state 0xd), which is where the manual's "needs a ball in
		// the brig" requirement really lives. Note the third hit lights no brig light.
		spot6->MessageField = 0;
		AddScoreFor(caller, 0); // 5000
		StartJailBreak();
		return;
	}

	if (mode != ModeTavern)
		return;

	if (spot6->MessageField == 0)
	{
		tabb02wave->Play(caller, "BrigTargetControl");
		spot6->MessageField = 1;
		AddScoreFor(caller, 0); // 5000
		lite530->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite531->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		lite532->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		return;
	}
	if (spot6->MessageField == 1)
	{
		tabb02wave->Play(caller, "BrigTargetControl");
		spot6->MessageField = 2;
		AddScoreFor(caller, 0); // 5000
		lite530->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		lite531->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite532->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
		return;
	}

	// Third tavern hit: Max Crew instead of Jail Break, and -- unlike the idle arm -- the
	// counter is *not* reset, so every further hit during the same Tavern re-awards Max
	// Crew. The listing has no write to a_spot6->MessageField on this path.
	// UNVERIFIED: whether that is intended. It costs nothing (Max Crew is idempotent once
	// a_bump2->MessageField is 6) beyond repeating the announcement and the 5,000, so it
	// is reproduced as-is.
	tabb61wave->Play(caller, "BrigTargetControl");
	ResetBrigLights(1);
	AwardMaxCrew(0.0f);
	AddScoreFor(caller, 0); // 5000
}

// ---------------------------------------------------------------------------
// Captive ball -- a_spot102, scoring {50000}
// ---------------------------------------------------------------------------

// 0x004273b0 -- a_spot102 (skeleton: a_spot102Control)
//
// The mode-selection device. In idle play it re-rolls the next mode and announces it;
// while a mode is running it buys the mode exactly ten more seconds. It pays its 50,000
// on every path.
void CaptiveBallControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	if (mode == ModeIdle)
	{
		// The announcement is indexed by the *selection* index 0..7 (Attack / Treasure
		// Hunt / Tavern / Bermuda / Storm / Mutiny / Swordfight / Peg Leg's Loot), which
		// is not the mode-register order -- the two differ by the DAT_0043d0f0
		// permutation. Indices above 6 all fall into the same default arm, so index 7
		// and anything larger print "Pegleg's Loot Mode Selected".
		const int index = SelectNextMode();
		const int stringId = (index >= 0 && index <= 6) ? Str_ModeSelected0 + index
														: Str_ModeSelected0 + 7;
		info_text_box->Display(pb::get_rc_string(PiratesMsg(stringId)), 2.0f);
		AddScoreFor(caller, 0); // 50000
		lite607->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		return;
	}

	if (mode >= ModeAttackShip && mode <= ModePegLegsLoot)
	{
		// +10.0 s, uncapped and repeatable. The timer answers in milliseconds, hence the
		// /1000.0; both constants were read out of the float pool (1000.0 as a double at
		// 0x438920, 10.0 as a float at 0x438914).
		const int remainingMs = static_cast<int>(mode_timer->Message(FtMessageCode::TimerGetRemaining, 0.0f));
		if (remainingMs > 0)
			mode_timer->Message(FtMessageCode::TimerSet, static_cast<float>(remainingMs) / 1000.0f + 10.0f);
	}

	lite607->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
	// The in-mode arm scores too: the listing's tail at 0x42750a passes the real caller,
	// where Ghidra shows a null one. So the captive ball is worth 50,000 whether or not a
	// mode is running, which is slightly more generous than the manual's "+50,000 outside
	// a mode".
	AddScoreFor(caller, 0); // 50000
}

// ---------------------------------------------------------------------------
// Right outlane -- a_roll3, scoring {25000}
// ---------------------------------------------------------------------------

// 0x004296d0 -- a_roll3 (skeleton: a_roll3Control)
//
// Three things in priority order, and only the last of them scores: light the WINDS "S"
// (which may complete the word and pay a map piece), else collect the shipwreck mystery if
// it is armed, else the plain 25,000. Both earlier awards return a non-zero announcement
// time, which is how the handler knows to stay out of the way.
void RightOutlaneControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	float claimed = 0.0f;
	if (info_text_box->MessageField == ModeIdle)
	{
		lite537->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		claimed = CheckWindsComplete(0.0f);
	}
	if (claimed == 0.0f)
		claimed = CollectShipwreckMystery(0.0f);
	if (claimed != 0.0f)
		return;

	tabb43wave->Play(caller, "RightOutlaneControl");
	info_text_box->Display(Glyph_RightOutlane, 2.0f);
	AddScoreFor(caller, 0); // 25000
}

// ---------------------------------------------------------------------------
// Return lanes -- a_roll5 and a_roll1, scoring {10000}
// ---------------------------------------------------------------------------
//
// Both lanes light a WINDS letter in idle play, then unconditionally re-arm the left
// outlane kickback pair (a_kick2 and s_bwal610) and flash v_lite544. The ActiveFlag writes
// are the +5 enable byte, set directly rather than by message.

// 0x00429790 -- a_roll5 (skeleton: a_roll5Control)
void ReturnLaneControl5(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	bool windsAwarded = false;
	if (info_text_box->MessageField == ModeIdle)
	{
		lite534->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		windsAwarded = CheckWindsComplete(0.0f) != 0.0f;
		if (!windsAwarded)
			info_text_box->Display(Glyph_IslandLane, 2.0f);
	}
	// The sound is suppressed only when WINDS just completed, which has its own fanfare.
	if (!windsAwarded)
		tabb44wave->Play(caller, "ReturnLaneControl");

	lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	kick2->ActiveFlag = 1;
	bwal610->ActiveFlag = 1;
	AddScoreFor(caller, 0); // 10000
}

// 0x00429840 -- a_roll1 (skeleton: a_roll1Control)
void ReturnLaneControl1(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	bool windsAwarded = false;
	if (info_text_box->MessageField == ModeIdle)
	{
		lite535->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		windsAwarded = CheckWindsComplete(0.0f) != 0.0f;
		if (!windsAwarded)
			info_text_box->Display(Glyph_ReturnLane, 2.0f);
	}
	if (!windsAwarded)
		tabb44wave->Play(caller, "ReturnLaneControl");

	lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	kick2->ActiveFlag = 1;
	bwal610->ActiveFlag = 1;
	AddScoreFor(caller, 0); // 10000
}

// ---------------------------------------------------------------------------
// Wind lanes -- a_roll2, a_roll9, a_roll8, a_roll6, scoring {25000, 50000, 75000}
// ---------------------------------------------------------------------------
//
// Four of the eight wind lanes (the other four are ramps and tripwires in other slices).
// The scoring index is the lane's own light group's on-count, so a lane pays 25,000 on its
// first pass, 50,000 on its second and 75,000 from the third on -- and the lights that
// produce that count are only advanced in idle play, which is why repeat passes inside a
// mode all pay 25,000.
//
// Each lane also has a Bermuda Triangle arm: when the lane's indicator light is the one
// currently *flashing* it is the indicated lane and the mode's progressive table pays;
// otherwise a flat unmultiplied 50,000. a_roll9 and a_roll8 are the two "Easterly" lanes
// and share both the name string and Treasure Hunt's bury-an-award behaviour.

// 0x004298f0 -- a_roll2, the Sou'Eastern lane (skeleton: a_roll2Control)
void SouthWindLaneControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	// This local is the announcement-time accumulator; it ends up added to the display
	// dwell at the bottom, so a WINDS completion here pushes the lane's own line back.
	// Ghidra loses it entirely and renders the dwell as `5.74532e-44 + 2.0`.
	float claimed = 0.0f;
	const int mode = info_text_box->MessageField;

	if (mode == ModeIdle)
	{
		lite536->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		claimed = CheckWindsComplete(0.0f);
		if (claimed == 0.0f)
		{
			info_text_box->Display(Glyph_SouthWind, 2.0f);
			tabb12wave->Play(caller, "SouthWindLaneControl");
		}
	}
	else if (mode == ModeBermuda)
	{
		if (lite622->FlasherOnFlag)
		{
			AwardIndicatedWind();
			return;
		}
		AddScoreUnmultiplied(caller, 50000);
		tabb12wave->Play(caller, "SouthWindLaneControl");
		return;
	}

	const int lit = south_wind_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	int index;
	if (lit == 0)
	{
		if (mode == ModeIdle)
			lite622->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 0;
	}
	else if (lit == 1)
	{
		if (mode == ModeIdle)
		{
			lite622->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			lite621->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		index = 1;
	}
	else
	{
		if (mode == ModeIdle)
			south_wind_lights->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 2;
	}

	const int scored = AddScoreFor(caller, index);
	DisplayWithValue(Str_SouEastern, scored, claimed + 2.0f);
}

// 0x00429ac0 -- a_roll9, east wind lane 1 (skeleton: a_roll9Control)
void EastWindLaneControl1(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	if (mode == ModeBermuda)
	{
		if (lite616->FlasherOnFlag)
		{
			AwardIndicatedWind();
			return;
		}
		AddScoreUnmultiplied(caller, 50000);
		tabb12wave->Play(caller, "EastWindLaneControl");
		return;
	}

	// Treasure Hunt takes the lane over completely: bury an award and score nothing.
	if (mode == ModeTreasureHunt)
	{
		BuryTreasureHuntAward(0.0f);
		return;
	}

	tabb12wave->Play(caller, "EastWindLaneControl");

	const int lit = east_wind_1_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	int index;
	if (lit == 0)
	{
		if (mode == ModeIdle)
			lite616->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 0;
	}
	else if (lit == 1)
	{
		if (mode == ModeIdle)
		{
			lite616->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			lite615->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		index = 1;
	}
	else
	{
		if (mode == ModeIdle)
			east_wind_1_lights->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 2;
	}

	const int scored = AddScoreFor(caller, index);
	DisplayWithValue(Str_Easterly, scored, 2.0f);
}

// 0x00429c30 -- a_roll8, east wind lane 2 (skeleton: a_roll8Control)
void EastWindLaneControl2(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	if (mode == ModeBermuda)
	{
		if (lite619->FlasherOnFlag)
		{
			AwardIndicatedWind();
			return;
		}
		AddScoreUnmultiplied(caller, 50000);
		tabb12wave->Play(caller, "EastWindLaneControl");
		return;
	}

	if (mode == ModeTreasureHunt)
	{
		BuryTreasureHuntAward(0.0f);
		return;
	}

	tabb12wave->Play(caller, "EastWindLaneControl");

	const int lit = east_wind_2_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	int index;
	if (lit == 0)
	{
		if (mode == ModeIdle)
			lite619->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 0;
	}
	else if (lit == 1)
	{
		if (mode == ModeIdle)
		{
			lite619->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			lite618->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		index = 1;
	}
	else
	{
		if (mode == ModeIdle)
			east_wind_2_lights->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 2;
	}

	const int scored = AddScoreFor(caller, index);
	DisplayWithValue(Str_Easterly, scored, 2.0f);
}

// 0x00429da0 -- a_roll6, the Westerly lane (skeleton: a_roll6Control)
//
// The only one of the four with no Treasure Hunt arm: Storm uses this lane for the
// shoot-again light, but that is in the Storm controller, not here.
void WestWindLaneControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	if (mode == ModeBermuda)
	{
		if (lite623->FlasherOnFlag)
		{
			AwardIndicatedWind();
			return;
		}
		AddScoreUnmultiplied(caller, 50000);
		tabb12wave->Play(caller, "WestWindLaneControl");
		return;
	}

	tabb12wave->Play(caller, "WestWindLaneControl");

	const int lit = west_wind_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0f);
	int index;
	if (lit == 0)
	{
		if (mode == ModeIdle)
			lite623->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 0;
	}
	else if (lit == 1)
	{
		if (mode == ModeIdle)
		{
			lite623->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			// The second light of this pair ascends (623 -> 624) where the other three
			// lanes descend (616 -> 615, 619 -> 618, 622 -> 621). Confirmed in the
			// listing; presumably just how the lights are laid out on this lane.
			lite624->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		index = 1;
	}
	else
	{
		if (mode == ModeIdle)
			west_wind_lights->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 2;
	}

	const int scored = AddScoreFor(caller, index);
	DisplayWithValue(Str_Westerly, scored, 2.0f);
}

// ---------------------------------------------------------------------------
// Island lanes -- a_roll12, a_roll11, a_roll10, scoring {1200, 20000}
// ---------------------------------------------------------------------------
//
// Three lanes, each with one light in the "top" group and one in the "bottom" group, and a
// strict order: the top light first, the bottom light second, and once both are lit the
// lane is spent. Filling *both* three-light groups pays 20,000 and advances the fathom
// multiplier. These are the lanes the flippers rotate, which is what makes the "spent"
// state temporary.
//
// Note the predicate: the original tests LightOnFlag alone (field +0x42), not the full
// light_on() triple, so a light that is merely flashing still counts as dark here.

// 0x00429ef0 -- the shared tail, reached only on the hit that lights a lane's second
// light. Takes the caller; Ghidra renders it as a no-argument function and loses both
// scoring calls' first argument.
static void IslandLaneSecondLight(TPinballComponent* caller)
{
	if (island_lane_top_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0f) == 3
		&& island_lane_bottom_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0f) == 3)
	{
		island_lane_top_lights->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		island_lane_bottom_lights->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		AdvanceFathomMultiplier(0.0f);
		tabb88wave->Play(caller, "IslandLaneSecondLight");
		AddScoreFor(caller, 1); // 20000
		return;
	}
	tabb44wave->Play(caller, "IslandLaneSecondLight");
	AddScoreFor(caller, 0); // 1200
}

// 0x00429fa0 -- a_roll12 (skeleton: a_roll12Control)
void IslandLaneControl12(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (!lite538->LightOnFlag)
	{
		tabb44wave->Play(caller, "IslandLaneControl");
		AddScoreFor(caller, 0); // 1200
		lite538->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		return;
	}
	if (!lite541->LightOnFlag)
	{
		lite541->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		IslandLaneSecondLight(caller);
		return;
	}
	// Lane already spent: both lights get a 2-second reminder flash and the hit pays the
	// base 1,200 again.
	lite538->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	lite541->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	tabb44wave->Play(caller, "IslandLaneControl");
	AddScoreFor(caller, 0); // 1200
}

// 0x0042a060 -- a_roll11 (skeleton: a_roll11Control)
void IslandLaneControl11(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (!lite539->LightOnFlag)
	{
		tabb44wave->Play(caller, "IslandLaneControl");
		AddScoreFor(caller, 0); // 1200
		lite539->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		return;
	}
	if (!lite542->LightOnFlag)
	{
		lite542->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		IslandLaneSecondLight(caller);
		return;
	}
	lite539->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	lite542->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	tabb44wave->Play(caller, "IslandLaneControl");
	AddScoreFor(caller, 0); // 1200
}

// 0x0042a120 -- a_roll10 (skeleton: a_roll10Control)
void IslandLaneControl10(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (!lite540->LightOnFlag)
	{
		tabb44wave->Play(caller, "IslandLaneControl");
		AddScoreFor(caller, 0); // 1200
		lite540->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		return;
	}
	if (!lite543->LightOnFlag)
	{
		lite543->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		IslandLaneSecondLight(caller);
		return;
	}
	lite540->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	lite543->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
	tabb44wave->Play(caller, "IslandLaneControl");
	AddScoreFor(caller, 0); // 1200
}

} // namespace pirates
