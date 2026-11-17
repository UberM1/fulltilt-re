// pirates_modes_entry.cpp -- Skulduggery's mode entry points and playfield setup.
//
// The slice between `pirates_modes.cpp` (the state machine) and the rest of the
// rules: the eleven routines that *enter* a state, the playfield
// setup/teardown helpers the controllers share, and the drain-bonus count-up
// that owns the end of every ball.
//
// Three groups, and the split is the original's own:
//
//   1. **The mode setters, 0x423440..0x423630.** Eleven near-identical
//      five-line routines. Each one flashes (or clears) the mode-in-progress
//      light `v_lite606`, optionally shows a one-character glyph, writes the
//      new state into `info_text_box->MessageField`, and broadcasts `0x4f`.
//      None of them touches the playfield: *the controller's own `0x4f` arm
//      does that*, through `StartOfEveryMode` (0x42aad0) and the helpers
//      below. That is why `EnterStorm` is three statements and Storm still
//      ends up with eight wind groups blanked -- see `StormController` in
//      pirates_modes.cpp. The one exception is `EnterSwordFight`, which
//      zeroes `a_spot8->MessageField` *before* the broadcast.
//
//   2. **The playfield helpers, 0x423680..0x42ce80.** Light-bank restores,
//      drop-bank raises, magnet clears and sink ejects. Almost all of them are
//      built out of one of two arithmetic idioms the compiler emitted inline
//      and which are reproduced verbatim here so the argument values can be
//      checked against the listing:
//
//        `code  = (-(flag == 0) & 4) + 0x10`   -> 0x10 when set, 0x14 when clear
//        `code  = (-(flag == 0) & 0xc) + 7`    -> 7    when set, 0x13 when clear
//        `value = (float)(flag != 0)`          -> 1.0  when set, 0.0  when clear
//
//      The second is the same trick `SetModeWheelLight` (0x4255a0) uses, and
//      the comment there spells it out.
//
//   3. **The drain bonus, 0x42b820.** State 0x10: a six-step count-up driven
//      by its own re-entry, then the extra-ball payout and the next-ball
//      serve. It is a *state of the same machine* as the eight modes, so
//      `ModeControl` reaches it, but it is not a mode: it pays no start bonus,
//      arms no playfield and runs no sweep.
//
// MESSAGE CODES. Full Tilt's numbering is not the port's. Following
// pirates_part1/2/3.cpp (rather than pirates_modes.cpp, which keeps the raw
// binary values in its own `ft` block), every code whose upstream equivalent
// is determinate under the piecewise delta is spelled with the port's
// `MessageCode` name, because that is what the port's component classes
// answer:
//
//     binary 0x01/0x07/0x10/0x13/0x14 -> TLight family, delta +0, same value
//     binary 0x19/0x1a                -> TLightFtTmpOverrideOff / ...ResetOverride
//     binary 0x1b                     -> TLightSweepStart (27), same value
//     binary 0x35/0x36                -> TPopupTargetDisable/Enable (49/50), delta +4
//     binary 0x41                     -> TSinkResetTimer (56), delta +9
//     binary 0x3f3/0x3f4/0x3f9/0x3ff  -> SetTiltLock / ClearTiltLock /
//                                        PlungerStartFeedTimer / SwitchToNextPlayer
//
// Only the genuinely FT-only codes (0x45 the timer set, 0x4c the magnet
// disarm, 0x4f the mode broadcast) are kept as raw casts, in `ft` below.
//
// NOTE for whoever reconciles the slices: pirates_modes.cpp sends raw 0x19 /
// 0x1a to the two orbit light groups (`ft::LightOverrideOff` /
// `LightClearOverride`). The port's TLight answers
// `MessageCode::TLightFtTmpOverrideOff` = -25 and
// `TLightFtResetOverride` = -26, so those two sends are inert as written.
// This file uses the port names; pirates_modes.cpp's `ft` block needs the same
// correction, which is not mine to make.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TPinballTable.h"
#include "TPinballComponent.h"

// component_tag<T> instantiates std::is_base_of<TPinballComponent, T>, so every
// T below has to be complete.
#include "TLight.h"
#include "TLightGroup.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates
{

// ---------------------------------------------------------------------------
// Table-local message codes
//
// Only the three that have no determinate upstream equivalent; everything else
// goes through MessageCode:: directly. Values are the binary's own.
// ---------------------------------------------------------------------------

namespace ft
{
// Sent to mode_timer (a type-1032 TTimer). 0x45 arms it with a float number of
// seconds, -1.0 cancels. Same two names pirates_part1.cpp and
// pirates_modes.cpp reached independently.
constexpr auto TimerSet = MessageCode::TTimerResetTimer; // binary 0x45

// 0x4c -- disarm a magnet's holding field. Numerically equal to the port's
// TMagnetFieldDisable (76), but spelled as pirates_part2/part3.cpp spell it,
// because the originals send it to the type-1042 magnets' own private code
// space rather than through the control band.
constexpr auto TMagnetDisarm = static_cast<MessageCode>(0x4c);

// The mode-register broadcast. This one must stay raw: it is consumed by this
// table's own dispatcher (0x42d850), which switches on the binary values, not
// by any engine component.
constexpr auto ModeStarted = static_cast<MessageCode>(0x4f);
} // namespace ft

// ---------------------------------------------------------------------------
// Mode register values of info_text_box->MessageField
//
// Spelled as ints rather than reusing pirates_modes.cpp's `enum class Mode`:
// that enum is defined inside that translation unit, and the setters here
// write the register the way the original does, with a bare store.
// See analysis/pirates-rules.md §1.3 for the full table.
// ---------------------------------------------------------------------------

constexpr int ModeGameOver = 1;
constexpr int ModeIdle = 2;
constexpr int ModeSkillShot = 3;
constexpr int ModeAttackShip = 4;
constexpr int ModeBermuda = 5;
constexpr int ModeStorm = 6;
constexpr int ModeTavern = 7;
constexpr int ModeTreasureHunt = 8;
constexpr int ModeMutiny = 9;
constexpr int ModeSwordFight = 10;
constexpr int ModePegLegsLoot = 0xb;
constexpr int ModeLavaBall = 0xc;
constexpr int ModeJailBreak = 0xd;

// The "flash / hold indefinitely" sentinel every light and timer argument uses.
constexpr float Forever = -1.0f;

// ---------------------------------------------------------------------------
// Drain-bonus string ids
// ---------------------------------------------------------------------------

enum
{
	STR_TARGET_BONUS = 3028,       // 0xbd4
	STR_MODE_BONUS = 3029,         // 0xbd5
	STR_MAP_PIECE_BONUS = 3030,    // 0xbd6
	STR_SPECIAL_TREASURES = 3031,  // 0xbd7
	STR_SHIPS_HOLD = 3032,         // 0xbd8
	STR_TOTAL_BONUS = 3033,        // 0xbd9
	// 2011..2014, "Player <n>, Extra Ball". Picked by CurrentPlayer, with the
	// fourth arm serving every player index above 2.
	STR_EXTRA_BALL_P1 = 2011,      // 0x7db
};

// ---------------------------------------------------------------------------
// Glyph tokens
//
// Read straight out of PIRATES.EXE's .data, from the stride-4 run that starts
// at 0x43aaa0 -- the same run whose first nine entries pirates_common.cpp
// publishes as `TavernWindowGlyphs` and `WindsGlyph`. Entry n sits at
// 0x43aaa0 + 4n:
//
//     0..7  "333".."444"   the three tavern windows, indexed by the 3-bit mask
//     8     "5"            the W-I-N-D-S token
//     9..12 "6".."9"       the four lane tokens (pirates_part1.cpp's Glyph_*)
//     26    "H"            0x43ab08 -- Attack Ship
//     27    "I"            0x43ab0c -- Treasure Hunt
//     28    "J"            0x43ab10 -- Bermuda Triangle
//
// These are single characters in the table's symbol font; the port has no
// symbol font and renders them as the ASCII they happen to be, exactly as
// pirates_common.cpp does for the other runs. Verified by dumping the bytes at
// those three addresses, so the four glyph arguments
// pirates_modes.cpp had open at 0x423480..0x4235f0 are now settled:
// three of the eleven setters show one of these, Tavern shows entry 0, and the
// remaining seven show none.
// ---------------------------------------------------------------------------

constexpr const char* Glyph_AttackShip = "H";    // 0x43ab08
constexpr const char* Glyph_TreasureHunt = "I";  // 0x43ab0c
constexpr const char* Glyph_Bermuda = "J";       // 0x43ab10
constexpr const char* Glyph_TavernWindowsNone = "333";  // 0x43aaa0, entry 0

// ---------------------------------------------------------------------------
// Components
//
// Declared as tags rather than as the `T*&` aliases, because several of these
// (the eight drop targets of the ship and parrot banks, the three hideaways,
// tabb42wave, tabb84wave) have no alias in any slice. The template argument of
// each declaration matches its definition exactly -- control_pirates.cpp types
// tabb42wave/tabb84wave as the generic TPinballComponent even though they are
// waves, which is why they are played through a dynamic_cast below.
// ---------------------------------------------------------------------------

// pirates_modes.cpp
extern component_tag<TTextBox> tag_info_text_box;
extern component_tag<TPinballComponent> tag_mode_timer;
extern component_tag<TPinballComponent> tag_light_sweeps;
extern component_tag<TLight> tag_v_lite606;  // mode-in-progress light; its
                                             // MessageField is the modes-completed counter
extern component_tag<TLight> tag_v_lite607;  // map pieces collected

// SKULDUGGERY letters. v_lite1's MessageField is the 11-bit mask.
extern component_tag<TLight> tag_v_lite1;
extern component_tag<TLight> tag_v_lite2;
extern component_tag<TLight> tag_v_lite3;
extern component_tag<TLight> tag_v_lite4;
extern component_tag<TLight> tag_v_lite5;
extern component_tag<TLight> tag_v_lite6;
extern component_tag<TLight> tag_v_lite7;
extern component_tag<TLight> tag_v_lite8;
extern component_tag<TLight> tag_v_lite9;
extern component_tag<TLight> tag_v_lite10;
extern component_tag<TLight> tag_v_lite11;

// Volcano display lights; the 4-bit mask lives in a_spot11->MessageField.
extern component_tag<TLight> tag_v_lite548;
extern component_tag<TLight> tag_v_lite549;
extern component_tag<TLight> tag_v_lite550;
extern component_tag<TLight> tag_v_lite551;
extern component_tag<TPinballComponent> tag_a_spot11;

// Brig lights and the brig target, whose MessageField is its 0..2 hit counter.
extern component_tag<TLight> tag_v_lite530;
extern component_tag<TLight> tag_v_lite531;
extern component_tag<TLight> tag_v_lite532;
extern component_tag<TPinballComponent> tag_a_spot6;

// Treasure Hunt / Peg Leg's Loot board.
extern component_tag<TPinballComponent> tag_a_sink9;
extern component_tag<TPinballComponent> tag_a_sink12;
extern component_tag<TPinballComponent> tag_s_trip3;
extern component_tag<TPinballComponent> tag_s_trip8;
extern component_tag<TLight> tag_v_lite567;
extern component_tag<TLight> tag_v_lite568;
extern component_tag<TLight> tag_v_lite520;
extern component_tag<TLight> tag_v_lite521;
extern component_tag<TLight> tag_v_mlite51;
extern component_tag<TLightGroup> tag_left_orbit_light_group;
extern component_tag<TLightGroup> tag_right_orbit_light_group;

// The three hideaways.
extern component_tag<TPinballComponent> tag_a_sink14;
extern component_tag<TPinballComponent> tag_a_sink15;
extern component_tag<TPinballComponent> tag_a_sink17;

// The eight wind light groups, in the order FlashIndicatedWind selects them.
extern component_tag<TLightGroup> tag_left_island_light_group;
extern component_tag<TLightGroup> tag_right_wind_light_group;
extern component_tag<TLightGroup> tag_south_wind_light_group;
extern component_tag<TLightGroup> tag_east_wind_1_light_group;
extern component_tag<TLightGroup> tag_east_wind_2_light_group;
extern component_tag<TLightGroup> tag_west_wind_light_group;

extern component_tag<TLightGroup> tag_special_treasure_light_group;

// The six keg magnets and their lights.
extern component_tag<TPinballComponent> tag_s_magnet1;
extern component_tag<TPinballComponent> tag_s_magnet2;
extern component_tag<TPinballComponent> tag_s_magnet3;
extern component_tag<TPinballComponent> tag_s_magnet4;
extern component_tag<TPinballComponent> tag_s_magnet5;
extern component_tag<TPinballComponent> tag_s_magnet6;

extern component_tag<TPinballComponent> tag_s_launcher1;

// pirates_components.cpp
extern component_tag<TPinballComponent> tag_a_sink8;  // the lava sink
extern component_tag<TPinballComponent> tag_drain;    // MessageField = the held Ship's Hold bonus
extern component_tag<TLight> tag_v_lite524;           // GOLD lights; 524 holds the 4-bit mask
extern component_tag<TLight> tag_v_lite525;
extern component_tag<TLight> tag_v_lite526;
extern component_tag<TLight> tag_v_lite527;
extern component_tag<TLight> tag_v_lite560;           // keg lights
extern component_tag<TLight> tag_v_lite561;
extern component_tag<TLight> tag_v_lite562;
extern component_tag<TLight> tag_v_lite563;
extern component_tag<TLight> tag_v_lite564;
extern component_tag<TLight> tag_v_lite565;
extern component_tag<TLight> tag_v_lite570;           // Ship's Hold
extern component_tag<TLight> tag_v_mlite44;           // the six "X" lights
extern component_tag<TLight> tag_v_mlite104;
extern component_tag<TLight> tag_v_mlite106;
extern component_tag<TLight> tag_v_mlite107;
extern component_tag<TLight> tag_v_mlite110;

// control_pirates.cpp. The eleven drop targets, in bank order; the *first*
// target of each bank carries that bank's bitmask in its MessageField.
extern component_tag<TPinballComponent> tag_a_drop5;   // ship bank, 5 targets
extern component_tag<TPinballComponent> tag_a_drop6;
extern component_tag<TPinballComponent> tag_a_drop7;
extern component_tag<TPinballComponent> tag_a_drop8;
extern component_tag<TPinballComponent> tag_a_drop9;
extern component_tag<TPinballComponent> tag_a_drop10;  // parrot bank, 3
extern component_tag<TPinballComponent> tag_a_drop11;
extern component_tag<TPinballComponent> tag_a_drop12;
extern component_tag<TPinballComponent> tag_a_drop13;  // tavern bank, 3
extern component_tag<TPinballComponent> tag_a_drop14;
extern component_tag<TPinballComponent> tag_a_drop15;
extern component_tag<TPinballComponent> tag_a_spot8;   // volcano solo target
extern component_tag<TPinballComponent> tag_tabb42wave;  // bonus count-up start
extern component_tag<TPinballComponent> tag_tabb84wave;  // one bonus term

// pirates_part1.cpp
extern component_tag<TSound> tag_tabb74wave;  // the total-bonus fanfare

extern TPinballTable*& TableG;

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------

// pirates_modes.cpp -- the dispatcher. Takes the *binary* message code.
void Handler(MessageCode code, TPinballComponent* caller);  // 0x42d850

// pirates_modes.cpp statics.
extern int WheelCursor;               // DAT_00435d04, the shared "X"/wind cursor
extern int TargetsDroppedThisBall;    // DAT_00435d48

// pirates_common.cpp.
extern int game_over_phase;           // DAT_0043aba8

int AddScoreAt(TPinballComponent* caller, int score);           // 0x423310
int AddScoreUnmultiplied(TPinballComponent* caller, int score); // 0x423350
int Rnd(int n);                                                 // 0x42dd0b
const char* GetRcString(int id);                                // 0x409730(id, 0)
void Display(TTextBox* box, const char* text, float time);      // 0x41de40, font 0
void DisplayGlyph(TTextBox* box, const char* glyph, float time);// 0x41de40, font 1
void ClearText(TTextBox* box);                                  // 0x41d930
void FormatWithValue(char* out, const char* format, int value);  // 0x42dfaf (sprintf)
void Play(TSound* sound);                                       // vtable +0x18

// ---------------------------------------------------------------------------
// Module state
// ---------------------------------------------------------------------------

// 0x43abac -- "compress every announcement". Set for the duration of the
// buried-treasure payout (0x425ab0, the awards slice) and read only here and
// by 0x423720 / 0x424780: with it set, every bonus line shows for 0.25 s
// instead of 2.0 s, so the payout does not stall behind six two-second
// announcements.
//
// DEFINED here because no slice owns it: the awards slice is its only writer
// and this is its only reader outside that slice. If pirates_awards.cpp also
// defines it, this definition is the one to drop and replace with an `extern`.
int compressed_announcements = 0;

// 0x435d38 -- the drain bonus's step cursor. 0..5 are the six terms, >= 6 is
// the serve/extra-ball arm. The 0x4f jumps it straight to 6 when the table is
// locked down, and the timer expiry jumps it to 8.
int bonus_step = 0;

// 0x435d00 -- the running total. Reset by the 0x4f, paid at step 5.
int bonus_accumulator = 0;

// 0x435d34 -- "skip the display". Set when message `1` (the flipper
// fast-forward, reachable from the cheat handler at 0x42a830) arrives, which
// makes every step accumulate and fall through instead of pausing to announce.
int bonus_skip_display = 0;

// ---------------------------------------------------------------------------
// Forward declarations within this file
// ---------------------------------------------------------------------------

void EjectLavaSink(float delay);
void RaiseShipDropBank();
void RaiseTavernDropBank();
void RaiseParrotDropBank();
void RestoreVolcanoLights();

namespace
{
// The two shared idioms, written once. `(-(flag == 0) & 4) + 0x10` and
// `(-(flag == 0) & 0xc) + 7` appear inline at every call site in the listing.

// 0x10 TLightFlasherStartTimedThenStayOff when set, 0x14 TLightResetAndTurnOff
// when clear.
MessageCode FlashOrClearCode(bool set)
{
	return set
		? MessageCode::TLightFlasherStartTimedThenStayOff
		: MessageCode::TLightResetAndTurnOff;
}

// 7 TLightFlasherStartTimed when set, 0x13 TLightResetAndTurnOn when clear.
MessageCode FlashOrSolidCode(bool set)
{
	return set
		? MessageCode::TLightFlasherStartTimed
		: MessageCode::TLightResetAndTurnOn;
}

// The "restore this light from bit n of a mask" shape, which 0x423900 and
// 0x4253a0 repeat four times each.
void RestoreFromMask(TLight* light, int mask, int bit)
{
	light->Message(
		(mask & bit) != 0 ? MessageCode::TLightResetAndTurnOn : MessageCode::TLightResetAndTurnOff,
		0.0f);
}

// control_pirates.cpp types these two waves as the generic component, so the
// TSound they really are has to be recovered before it can be played.
void PlayGenericWave(TPinballComponent* wave)
{
	Play(dynamic_cast<TSound*>(wave));
}

// 0x423400 -- flash the mode-in-progress light forever. The preamble of the
// nine mode setters and of the two multiball starts.
void ArmModeInProgressLight()
{
	tag_v_lite606.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
}

// 0x423420 -- and put it out again. The preamble of EnterIdle and of
// EnterSkillShotAward (0x423440, below).
void ClearModeInProgressLight()
{
	tag_v_lite606.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x423900 -- RestoreGoldLights. Re-derive v_lite524..527 from the 4-bit mask
// in v_lite524->MessageField.
//
// Static because it is not in this file's interface: its only caller is
// RestoreBanksAndLetters below. pirates_modes.cpp's StartOfEveryMode comment
// names a "ResetGoldLights (0x423870)", which is the *clearing* counterpart
// and a different routine.
void RestoreGoldLights()
{
	const int mask = tag_v_lite524.Component->MessageField;
	RestoreFromMask(tag_v_lite524.Component, mask, 1);
	RestoreFromMask(tag_v_lite525.Component, mask, 2);
	RestoreFromMask(tag_v_lite526.Component, mask, 4);
	RestoreFromMask(tag_v_lite527.Component, mask, 8);
}

// 0x423c90 -- RestoreLettersAndBanks. Re-derive all eleven SKULDUGGERY letter
// lights AND the three drop banks' raised/dropped state from the 11-bit mask
// in v_lite1->MessageField, and rebuild each bank's own bitmask while doing
// it. Bank by bank, tavern (bits 0..2) / ship (bits 3..7) / parrot (bits
// 8..10), with the same three-part shape each time:
//
//   * mask bits for this bank all set  -> light every letter solid and RAISE
//     every target, leaving the bank's bitmask at 0. That is the "bank just
//     completed, start it over" case.
//   * otherwise, per target: bit set -> letter solid, target dropped, bit
//     recorded in the bank's bitmask; bit clear -> letter off, target raised.
//
// Static for the same reason as RestoreGoldLights.
void RestoreLettersAndBanks()
{
	const unsigned mask = static_cast<unsigned>(tag_v_lite1.Component->MessageField);

	// --- tavern bank: a_drop13..a_drop15, letters v_lite1..v_lite3, bits 0..2
	tag_a_drop13.Component->MessageField = 0;
	if ((mask & 7) == 7)
	{
		tag_v_lite1.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop13.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite2.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop14.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite3.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop15.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}
	else
	{
		TLight* const letters[3] = {
			tag_v_lite1.Component, tag_v_lite2.Component, tag_v_lite3.Component};
		TPinballComponent* const targets[3] = {
			tag_a_drop13.Component, tag_a_drop14.Component, tag_a_drop15.Component};
		for (int i = 0; i < 3; i++)
		{
			if ((mask & (1u << i)) == 0)
			{
				letters[i]->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				targets[i]->Message(MessageCode::TPopupTargetEnable, 0.0f);
			}
			else
			{
				letters[i]->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
				targets[i]->Message(MessageCode::TPopupTargetDisable, 0.0f);
				tag_a_drop13.Component->MessageField |= (1 << i);
			}
		}
	}

	// --- ship bank: a_drop5..a_drop9, letters v_lite4..v_lite8, bits 3..7
	tag_a_drop5.Component->MessageField = 0;
	if ((mask & 0xf8) == 0xf8)
	{
		tag_v_lite4.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop5.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite5.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop6.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite6.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop7.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite7.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop8.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite8.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop9.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	}
	else
	{
		TLight* const letters[5] = {
			tag_v_lite4.Component, tag_v_lite5.Component, tag_v_lite6.Component,
			tag_v_lite7.Component, tag_v_lite8.Component};
		TPinballComponent* const targets[5] = {
			tag_a_drop5.Component, tag_a_drop6.Component, tag_a_drop7.Component,
			tag_a_drop8.Component, tag_a_drop9.Component};
		for (int i = 0; i < 5; i++)
		{
			if ((mask & (8u << i)) == 0)
			{
				letters[i]->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				targets[i]->Message(MessageCode::TPopupTargetEnable, 0.0f);
			}
			else
			{
				letters[i]->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
				targets[i]->Message(MessageCode::TPopupTargetDisable, 0.0f);
				tag_a_drop5.Component->MessageField |= (1 << i);
			}
		}
	}

	// --- parrot bank: a_drop10..a_drop12, letters v_lite9..v_lite11, bits 8..10
	tag_a_drop10.Component->MessageField = 0;
	if (((mask >> 8) & 7) == 7)
	{
		tag_v_lite9.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop10.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite10.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop11.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		tag_v_lite11.Component->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
		tag_a_drop12.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
		return;
	}

	TLight* const letters[3] = {
		tag_v_lite9.Component, tag_v_lite10.Component, tag_v_lite11.Component};
	TPinballComponent* const targets[3] = {
		tag_a_drop10.Component, tag_a_drop11.Component, tag_a_drop12.Component};
	for (int i = 0; i < 3; i++)
	{
		if ((mask & (0x100u << i)) == 0)
		{
			letters[i]->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
			targets[i]->Message(MessageCode::TPopupTargetEnable, 0.0f);
		}
		else
		{
			letters[i]->Message(MessageCode::TLightResetAndTurnOn, 0.0f);
			targets[i]->Message(MessageCode::TPopupTargetDisable, 0.0f);
			tag_a_drop10.Component->MessageField |= (1 << i);
		}
	}
}
} // namespace

// ===========================================================================
// The mode setters, 0x423460 -- 0x423630
//
// Eleven routines, one per reachable state, and the only writers of the mode
// register outside the dispatcher's own forced return to Idle and the cheat
// handler. Shape: touch v_lite606, maybe show a glyph, store the state,
// broadcast 0x4f. The broadcast is what runs the new state's controller.
// ===========================================================================

// 0x00423440 -- enter the skill-shot award (state 3). Same shape as EnterIdle:
// `call 0x423420; [info_text_box]+6 = 3; push 0; push 0x4f; call 0x42d850`. Its
// only caller in the image is the table's new-game reset (0x4231c5); the serve path
// writes state 3 by hand instead.
void EnterSkillShotAward()
{
	ClearModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeSkillShot;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423460 -- enter Idle (state 2). Puts the mode-in-progress light out.
void EnterIdle()
{
	ClearModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeIdle;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423480 -- enter Attack Ship (state 4). The only setter whose glyph shows
// for 1.0 s rather than 2.0 s.
void EnterAttackShip()
{
	ArmModeInProgressLight();
	DisplayGlyph(tag_info_text_box.Component, Glyph_AttackShip, 1.0f);
	tag_info_text_box.Component->MessageField = ModeAttackShip;
	Handler(ft::ModeStarted, nullptr);
}

// 0x004234c0 -- enter Bermuda Triangle (state 5).
void EnterBermuda()
{
	ArmModeInProgressLight();
	DisplayGlyph(tag_info_text_box.Component, Glyph_Bermuda, 2.0f);
	tag_info_text_box.Component->MessageField = ModeBermuda;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423500 -- enter Storm (state 6). No glyph: Storm has no fixed name, and
// its own 0x4f picks the severity string.
void EnterStorm()
{
	ArmModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeStorm;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423520 -- enter Tavern (state 7). Shows entry 0 of the tavern-window
// glyph run, i.e. all three windows uncollected, which is what the mode starts
// from.
void EnterTavern()
{
	ArmModeInProgressLight();
	DisplayGlyph(tag_info_text_box.Component, Glyph_TavernWindowsNone, 2.0f);
	tag_info_text_box.Component->MessageField = ModeTavern;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423560 -- enter Treasure Hunt (state 8).
void EnterTreasureHunt()
{
	ArmModeInProgressLight();
	DisplayGlyph(tag_info_text_box.Component, Glyph_TreasureHunt, 2.0f);
	tag_info_text_box.Component->MessageField = ModeTreasureHunt;
	Handler(ft::ModeStarted, nullptr);
}

// 0x004235a0 -- enter Mutiny (state 9). No glyph.
void EnterMutiny()
{
	ArmModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeMutiny;
	Handler(ft::ModeStarted, nullptr);
}

// 0x004235c0 -- enter Sword Fight (state 10).
//
// The one setter that is not pure: it zeroes `a_spot8->MessageField` before
// anything else. pirates_modes.cpp's declaration comment calls that "resets
// the volcano bank", which is not quite right -- the volcano bank's mask is in
// `a_spot11->MessageField` and `ResetVolcanoLights(1)` is what clears it.
// a_spot8's MessageField is the write-only cell pirates_part1.cpp documents at
// its a_spot8 handler: this setter clears it, that handler ORs 8 into it, and
// nothing ever reads it. Reproduced because it is in the listing, not because
// it does anything.
void EnterSwordFight()
{
	tag_a_spot8.Component->MessageField = 0;
	ArmModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeSwordFight;
	Handler(ft::ModeStarted, nullptr);
}

// 0x004235f0 -- enter Peg Leg's Loot (state 0xb), the finale. No glyph; its
// own 0x4f runs PrepareTreasureBoard and the sweep.
void EnterPegLegsLoot()
{
	ArmModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModePegLegsLoot;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423610 -- start Lava-Ball (state 0xc), the +2-ball multiball. Note it
// *flashes* v_lite606 like a mode rather than clearing it, even though the
// dispatcher never books it as a completed mode.
void StartLavaBall()
{
	ArmModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeLavaBall;
	Handler(ft::ModeStarted, nullptr);
}

// 0x00423630 -- start Jail Break (state 0xd), the brig-release multiball.
//
// One address, one name, as of now. pirates_part1.cpp and pirates_awards.cpp
// both declare and call it as `StartJailBreak` (from the brig target's third
// hit and from the award ladder), which is the spelling defined here;
// pirates_modes.cpp's rival `EnterJailBreak` declaration had no definition and
// no call site and has been removed.
void StartJailBreak()
{
	ArmModeInProgressLight();
	tag_info_text_box.Component->MessageField = ModeJailBreak;
	Handler(ft::ModeStarted, nullptr);
}

// ===========================================================================
// Playfield setup and teardown
// ===========================================================================

// 0x0042ab40 -- RestoreBanksAndLetters. The teardown half of every mode: put
// the collectible playfield back the way normal play expects to find it.
//
// Six calls and two lights, and the ORDER MATTERS in a way that is almost
// certainly an original-source bug and is reproduced rather than tidied:
// `RestoreLettersAndBanks` (0x423c90) carefully re-derives each drop bank's
// raised/dropped state and bitmask from the letter mask, and then the three
// `Raise*DropBank` calls immediately raise every target and zero every bitmask
// again. So the net effect of the pair is:
//
//   * the eleven letter LIGHTS are restored from v_lite1's mask  (survives)
//   * all eleven drop targets are raised and all three bank masks cleared
//     (overwrites what 0x423c90 just computed)
//
// i.e. a mode hands back a board whose letters are still lit but whose targets
// are all standing, so the letters already collected have to be re-shot. That
// is observable behaviour, so it stays.
void RestoreBanksAndLetters()
{
	RestoreGoldLights();        // 0x423900
	RestoreLettersAndBanks();   // 0x423c90
	RestoreVolcanoLights();     // 0x4253a0
	RaiseShipDropBank();        // 0x424440
	RaiseTavernDropBank();      // 0x424f70
	RaiseParrotDropBank();      // 0x4252d0

	// The two hideaway "X" indicators Treasure Hunt and Peg Leg's Loot borrow.
	tag_v_lite520.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_lite521.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x00424440 -- RaiseShipDropBank. Clear the bank's 5-bit mask (it lives in the
// first target's MessageField) and re-raise/re-enable all five.
void RaiseShipDropBank()
{
	tag_a_drop5.Component->MessageField = 0;
	tag_a_drop5.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop6.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop7.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop8.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop9.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
}

// 0x00424f70 -- RaiseTavernDropBank, a_drop13..a_drop15.
void RaiseTavernDropBank()
{
	tag_a_drop13.Component->MessageField = 0;
	tag_a_drop13.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop14.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop15.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
}

// 0x004252d0 -- RaiseParrotDropBank, a_drop10..a_drop12.
void RaiseParrotDropBank()
{
	tag_a_drop10.Component->MessageField = 0;
	tag_a_drop10.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop11.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
	tag_a_drop12.Component->Message(MessageCode::TPopupTargetEnable, 0.0f);
}

// 0x004244a0 -- DropAndDisableShipTargets. The exact inverse of
// RaiseShipDropBank: mark all five bits set (0x1f) and send the *disable* code,
// which drops them and takes them out of play.
//
// Sent by the ship sink: swallowing a ball into the ship flattens the bank so
// the ship ramp is open. The mask is set to "all dropped" so the bank-completion
// routine does not fire a second time when the targets come back up.
void DropAndDisableShipTargets()
{
	tag_a_drop5.Component->MessageField = 0x1f;
	tag_a_drop5.Component->Message(MessageCode::TPopupTargetDisable, 0.0f);
	tag_a_drop6.Component->Message(MessageCode::TPopupTargetDisable, 0.0f);
	tag_a_drop7.Component->Message(MessageCode::TPopupTargetDisable, 0.0f);
	tag_a_drop8.Component->Message(MessageCode::TPopupTargetDisable, 0.0f);
	tag_a_drop9.Component->Message(MessageCode::TPopupTargetDisable, 0.0f);
}

// 0x00425310 -- ResetVolcanoLights(mode). Blank or re-arm the four volcano
// display lights, highest index first.
//
//   mode == 0 -> 0x14 TLightResetAndTurnOff with 0.0: blank them. This is what
//                StartOfEveryMode sends at every mode start except Lava-Ball's.
//   mode != 0 -> 0x10 TLightFlasherStartTimedThenStayOff with 1.0: flash each
//                for one second and leave it dark, i.e. the "bank cleared"
//                acknowledgement.
//
// And `mode == 1` ALSO clears the 4-bit mask in a_spot11->MessageField. Note
// the asymmetry with ResetSkulduggeryLights below, which clears its mask for
// *any* non-zero mode: here mode 1 clears the mask and any other non-zero mode
// flashes the lights but leaves the mask standing. Both are in the listing.
void ResetVolcanoLights(int mode)
{
	const MessageCode code = FlashOrClearCode(mode != 0);
	const float value = mode != 0 ? 1.0f : 0.0f;

	if (mode == 1)
		tag_a_spot11.Component->MessageField = 0;

	tag_v_lite551.Component->Message(code, value);
	tag_v_lite550.Component->Message(code, value);
	tag_v_lite549.Component->Message(code, value);
	tag_v_lite548.Component->Message(code, value);
}

// 0x004253a0 -- RestoreVolcanoLights. Re-derive all four from a_spot11's mask,
// solid on for a set bit and off for a clear one. The counterpart of
// ResetVolcanoLights(0) for the paths that must not lose progress -- Jail
// Break's teardown is the one that needs it.
void RestoreVolcanoLights()
{
	const int mask = tag_a_spot11.Component->MessageField;
	RestoreFromMask(tag_v_lite551.Component, mask, 1);
	RestoreFromMask(tag_v_lite550.Component, mask, 2);
	RestoreFromMask(tag_v_lite549.Component, mask, 4);
	RestoreFromMask(tag_v_lite548.Component, mask, 8);
}

// 0x00423b70 -- ResetSkulduggeryLights(mode). The same shape as
// ResetVolcanoLights over the eleven letter lights, with the mask-clearing
// condition widened to any non-zero mode.
//
//   mode == 0 -> blank all eleven, mask untouched
//   mode != 0 -> clear v_lite1's 11-bit mask, then flash all eleven for 1.0 s
//                and leave them dark
void ResetSkulduggeryLights(int mode)
{
	const MessageCode code = FlashOrClearCode(mode != 0);
	const float value = mode != 0 ? 1.0f : 0.0f;

	if (mode != 0)
		tag_v_lite1.Component->MessageField = 0;

	tag_v_lite1.Component->Message(code, value);
	tag_v_lite2.Component->Message(code, value);
	tag_v_lite3.Component->Message(code, value);
	tag_v_lite4.Component->Message(code, value);
	tag_v_lite5.Component->Message(code, value);
	tag_v_lite6.Component->Message(code, value);
	tag_v_lite7.Component->Message(code, value);
	tag_v_lite8.Component->Message(code, value);
	tag_v_lite9.Component->Message(code, value);
	tag_v_lite10.Component->Message(code, value);
	tag_v_lite11.Component->Message(code, value);
}

// 0x00423870 -- ResetGoldLights(mode). The four gold lights v_lite524..527, in
// exactly the idiom of its two siblings below: a non-zero mode flashes them out
// over 1 s and clears the 4-bit mask in v_lite524's MessageField, a zero mode
// turns them off outright and leaves the mask alone.
//
// The clearing counterpart of RestoreGoldLights (0x423900) above, and the only one
// of the three Reset*Lights routines that neither rule slice had defined.
void ResetGoldLights(int mode)
{
	const MessageCode code = FlashOrClearCode(mode != 0);
	const float value = mode != 0 ? 1.0f : 0.0f;

	if (mode != 0)
		tag_v_lite524.Component->MessageField = 0;

	tag_v_lite524.Component->Message(code, value);
	tag_v_lite525.Component->Message(code, value);
	tag_v_lite526.Component->Message(code, value);
	tag_v_lite527.Component->Message(code, value);
}

// 0x00425d50 -- ResetBrigLights(mode). The three brig lights plus the brig
// target's hit counter.
//
// Two differences from the two routines above, both deliberate:
//   * the float is ALWAYS 1.0, not `mode != 0`, so the clearing path sends
//     `0x14` with 1.0 rather than with 0.0;
//   * `mode != 0` first turns all three lights plainly ON (0x01) and only then
//     flashes them, which is what makes the "brig emptied" acknowledgement
//     visible: they light, then flash out.
// Either way a_spot6's counter goes back to 0.
void ResetBrigLights(int mode)
{
	const MessageCode code = FlashOrClearCode(mode != 0);

	if (mode != 0)
	{
		tag_v_lite530.Component->Message(MessageCode::TLightTurnOn, 0.0f);
		tag_v_lite531.Component->Message(MessageCode::TLightTurnOn, 0.0f);
		tag_v_lite532.Component->Message(MessageCode::TLightTurnOn, 0.0f);
	}

	tag_v_lite530.Component->Message(code, 1.0f);
	tag_v_lite531.Component->Message(code, 1.0f);
	tag_v_lite532.Component->Message(code, 1.0f);

	tag_a_spot6.Component->MessageField = 0;
}

// 0x00425080 -- BlankWindGroups. Reset all eight wind light groups to off.
// Storm's start and teardown both run it, which is why the wind lanes are
// cleared by starting a Storm.
//
// The first statement is `AddScoreAt(nullptr, 0)` in the original, and it is
// not dead: with a null caller that wrapper sends `0x4d` to whatever component
// scored last and then forgets it, which breaks every running combo. Scoring
// zero is the side channel; see the note on `last_scoring_component` in
// pirates_common.cpp.
void BlankWindGroups()
{
	AddScoreAt(nullptr, 0);

	tag_left_island_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_right_wind_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_left_orbit_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_right_orbit_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_south_wind_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_east_wind_1_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_east_wind_2_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_west_wind_light_group.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x00425110 -- FlashIndicatedWind(on). Bermuda Triangle's progressive: mark
// whichever wind lane the shared cursor `WheelCursor` is pointing at.
//
//   on  -> 0x07 TLightFlasherStartTimed with -1.0, flash that group forever
//   !on -> 0x13 TLightResetAndTurnOn, light it solid
//
// The sense is worth stating plainly because the argument name invites the
// opposite reading: this routine never turns a group OFF. It flashes the
// indicated lane while Bermuda is running and leaves it lit solid when
// Bermuda stops indicating it. The flashing state is also the "is this lane
// the indicated one" test pirates_part1.cpp's rollovers make.
//
// The cursor-to-group map is NOT the order of the WindGroups table in
// pirates_modes.cpp -- it is 0 and the default arm both meaning left orbit,
// then right orbit / left island / right wind / south / west / east 2 / east 1.
// An out-of-range cursor is reset to 0 and falls into the left-orbit arm, so
// this cannot wedge.
void FlashIndicatedWind(bool on)
{
	const MessageCode code = FlashOrSolidCode(on);

	switch (WheelCursor)
	{
	case 1: tag_right_orbit_light_group.Component->Message(code, Forever); return;
	case 2: tag_left_island_light_group.Component->Message(code, Forever); return;
	case 3: tag_right_wind_light_group.Component->Message(code, Forever); return;
	case 4: tag_south_wind_light_group.Component->Message(code, Forever); return;
	case 5: tag_west_wind_light_group.Component->Message(code, Forever); return;
	case 6: tag_east_wind_2_light_group.Component->Message(code, Forever); return;
	case 7: tag_east_wind_1_light_group.Component->Message(code, Forever); return;
	case 0:
		break;
	default:
		WheelCursor = 0;
		break;
	}
	tag_left_orbit_light_group.Component->Message(code, Forever);
}

// 0x00424500 -- ClearTreasureXLights. Put out the six "X" lights, one per
// collectible sink, in the original's order (mlite44, 107, 106, 51, 110, 104 --
// a_sink17, a_sink15, a_sink14, the ship sink, a_sink9, a_sink12).
void ClearTreasureXLights()
{
	tag_v_mlite44.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_mlite107.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_mlite106.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_mlite51.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_mlite110.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_mlite104.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x00424560 -- RestoreTreasureBoard. Take the two extra treasure sinks back
// out of play by clearing their ActiveFlag (the `+5` enable byte). The inverse
// of the two stores PrepareTreasureBoard makes.
void RestoreTreasureBoard()
{
	tag_a_sink9.Component->ActiveFlag = 0;
	tag_a_sink12.Component->ActiveFlag = 0;
}

// 0x0042ce80 -- PrepareTreasureBoard. Peg Leg's Loot's `0x4f` helper: open the
// whole collection board at once, since the finale pays for everything on it.
//
// Five things:
//   1. blank the six "X" lights, then ENABLE a_sink9 and a_sink12 (normally
//      inert) and DISABLE the two orbit trip switches, so the orbits stop
//      scoring and the two extra sinks start;
//   2. stash whether v_lite568 (the ship-sink extra ball) was lit into its own
//      MessageField, 1 or 0 -- the mode is about to flash it, and the teardown
//      restores it from this cell. `light_on()` is the binary's inlined
//      `LightOnFlag || ToggledOnFlag || FlasherOnFlag` at +0x42/+0x4e/+0x46;
//   3. override both orbit light groups dark for the mode's duration;
//   4. flash, forever, every light the finale will pay for: the ship-sink X,
//      the five ship letters, the extra ball and the ship indicator;
//   5. run sweep 4 and clear the two hideaway indicators.
//
// Sweep 4 is sent from HERE and not from the controller's 0x4f, which is why
// pirates_modes.cpp's SweepPegLegsLoot constant carries that note.
void PrepareTreasureBoard()
{
	ClearTreasureXLights();

	tag_a_sink9.Component->ActiveFlag = 1;
	tag_a_sink12.Component->ActiveFlag = 1;
	tag_s_trip3.Component->ActiveFlag = 0;
	tag_s_trip8.Component->ActiveFlag = 0;

	tag_v_lite568.Component->MessageField = tag_v_lite568.Component->light_on() ? 1 : 0;

	tag_left_orbit_light_group.Component->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);
	tag_right_orbit_light_group.Component->Message(MessageCode::TLightFtTmpOverrideOff, 0.0f);

	tag_v_mlite51.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite4.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite5.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite6.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite7.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite8.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite568.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);
	tag_v_lite567.Component->Message(MessageCode::TLightFlasherStartTimed, Forever);

	tag_light_sweeps.Component->Message(MessageCode::TLightSweepStart, 4.0f);

	tag_v_lite520.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_v_lite521.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x00424c70 -- ClearKegMagnets. Disarm all six keg magnets and put their six
// lights out, interleaved magnet-then-light as in the original. Tavern's
// teardown runs it: a keg magnet armed during Tavern does not survive the mode.
void ClearKegMagnets()
{
	tag_s_magnet1.Component->Message(ft::TMagnetDisarm, 0.0f);
	tag_v_lite560.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_s_magnet2.Component->Message(ft::TMagnetDisarm, 0.0f);
	tag_v_lite561.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_s_magnet3.Component->Message(ft::TMagnetDisarm, 0.0f);
	tag_v_lite562.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_s_magnet4.Component->Message(ft::TMagnetDisarm, 0.0f);
	tag_v_lite563.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_s_magnet5.Component->Message(ft::TMagnetDisarm, 0.0f);
	tag_v_lite564.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
	tag_s_magnet6.Component->Message(ft::TMagnetDisarm, 0.0f);
	tag_v_lite565.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
}

// 0x00423680 -- EjectLavaSink(delay). One statement: re-arm the lava sink's
// timer. 0.0 ejects now, -1.0 holds the ball indefinitely, anything else is a
// delay in seconds. The table's shared "give the player a ball back" primitive.
void EjectLavaSink(float delay)
{
	tag_a_sink8.Component->Message(MessageCode::TSinkResetTimer, delay);
}

// 0x004236a0 -- EjectHideawayAtRandom. The sinks' shared "something swallowed a
// ball, give one back" routine, called from a dozen places in
// pirates_part2.cpp.
//
// In Idle it is not random at all: it holds the lava sink's ball indefinitely
// and returns. Only inside a mode (or any other state) does it draw one of the
// three hideaways and tell that one to hold indefinitely instead. So the
// "random" in the name only applies while a mode is running.
//
// The draw is `Rnd(3)`, and the arms are 0 -> a_sink17, 1 -> a_sink15,
// 2 -> a_sink14.
//
// Settled from the listing, and the arguments really are identical -- the
// compiler simply hoisted the two pushes above the second compare, which is
// what Ghidra lost:
//
//   004236b6  call 0x0042dd0b              ; rand()
//   004236bb  lea eax,[eax+eax*2]          ; *3
//   004236be  cdq / and edx,0x7fff / add eax,edx / sar eax,0xf   ; >>15, i.e. Rnd(3)
//   004236ca  test eax,eax / je 0x004236eb ; pick == 0
//   004236ce  push 0xbf800000              ; -1.0f   <-- shared by BOTH remaining arms
//   004236d3  cmp eax,1
//   004236d6  push 0x41                    ; 0x41    <-- likewise
//   004236d8  je 0x00423703                ; pick == 1 -> a_sink15 (0x0043b0cc)
//   004236da  ...                          ; pick == 2 -> a_sink14 (0x0043b0e4)
//   004236eb  ...                          ; pick == 0 -> a_sink17 (0x0043b0b4), its own
//                                            push pair at 0x004236eb/0x004236f7
//
// The 0x004236ce/0x004236d6 pair dominates both the pick==1 and pick==2 arms,
// so a_sink14 is called with exactly (0x41, -1.0f). dump_tags confirms the
// three globals: 0x0043b0b4 a_sink17, 0x0043b0cc a_sink15, 0x0043b0e4
// a_sink14, which is the 0/1/2 order below.
void EjectHideawayAtRandom()
{
	if (tag_info_text_box.Component->MessageField == ModeIdle)
	{
		EjectLavaSink(Forever);
		return;
	}

	const int pick = Rnd(3);
	if (pick == 0)
		tag_a_sink17.Component->Message(MessageCode::TSinkResetTimer, Forever);
	else if (pick == 1)
		tag_a_sink15.Component->Message(MessageCode::TSinkResetTimer, Forever);
	else
		tag_a_sink14.Component->Message(MessageCode::TSinkResetTimer, Forever);
}

// ===========================================================================
// State 0x10 -- the drain bonus
// ===========================================================================

// 0x0042b820 -- DrainBonusController. The end of every ball: count up the four
// bonus terms plus the held bonus, pay the total, then either hand out an extra
// ball or move to the next ball / next player / game over.
//
// It is a *self-driven* state machine, and that is the thing to understand
// about it. Each of the six terms shows for two seconds and RETURNS; the text
// box's display timer expiring comes back as a `0x50` (the dispatcher re-spells
// a text-box timer expiry as a mode tick -- see `Handler`), which re-enters
// here and runs the next step. So the count-up paces itself off the display
// timer with no timer of its own. The only thing `mode_timer` is used for is a
// 30-second deadlock breaker: if the announcements somehow stall, its `0x46`
// jumps the cursor to 8 and the serve happens anyway.
//
// Steps and multipliers, all four confirmed from the instruction stream (the
// compiler emitted them as lea/shl chains, not imul -- see
// analysis/pirates-rules.md §6.1):
//
//   0  targets dropped this ball        x 10,000   "Target Bonus"      3028
//   1  v_lite606->MessageField (modes)  x 200,000  "Mode Bonus"        3029
//   2  v_lite607->MessageField (maps)   x 250,000  "Map Piece Bonus"   3030
//   3  special treasures                x 100,000  "Special Treasures" 3031
//   4  drain->MessageField (held)       as-is      "Ship's Hold"       3032
//   5  pay the total unmultiplied                  "Total Bonus"       3033
//  >=6 extra ball, else next ball / next player / game over
//
// A zero term is skipped silently -- no line, no pause -- which is why a short
// ball's bonus flashes past. The `sprintf` operand at each step is the term
// value ALREADY multiplied, so the line reads "<term>: <points>".
//
// Step 5 is where "Ship's Hold" closes: if v_lite570 is lit, the total is
// stored into `drain->MessageField` and carried into the NEXT ball's step 4,
// and the light goes out; if it is not lit, any previously held bonus is
// discarded. So the hold is spend-once and is set by the ship sink during
// Mutiny.
//
// The final arm sends `(0x14, 0)` and `(0x1a, 0)` to `[TableG+0x13e]`. That field is
// TPinballTable::LightGroup: the table constructor stores `new TLightGroup(this, 0)`
// there (0x41a3dc call 0x40aca0; 0x41a3e1 mov [esi+0x13e],eax). So the game-over
// path blacks out every lamp (0x42bd38, 0x14 = TLightResetAndTurnOff), and every
// exit clears the lamps' temporary overrides after the 0x4f broadcast (0x42bdb8,
// 0x1a = TLightFtResetOverride, which a group forwards to each member light).
void DrainBonusController(MessageCode code, TPinballComponent* caller)
{
	// The original is a `goto`-driven loop over the dispatch head: codes 1 and
	// 0x46 rewrite `code` to 0x50 and re-enter. Spelled as a rewrite of the
	// local rather than as recursion, which is what the listing does.
	int c = static_cast<int>(code);

	if (c == 0x46)  // ControlTimerExpired -- only mode_timer's own
	{
		if (tag_mode_timer.Component != caller)
			return;
		caller = nullptr;
		game_over_phase = 0;
		bonus_step = 8;  // straight to the serve arm
		c = 0x50;
	}
	else if (c == 1)  // the flipper fast-forward (reached from 0x42a830)
	{
		bonus_skip_display = 1;
		ClearText(tag_info_text_box.Component);
		c = 0x50;
	}

	if (c == 0x4f)  // mode start
	{
		bonus_skip_display = 0;
		bonus_step = 0;
		bonus_accumulator = 0;
		tag_mode_timer.Component->Message(ft::TimerSet, 30.0f);

		// Locked down (tilt, or the end-of-ball lockdown that state 0x11 sets)
		// and not already in the game-over sequence: pay nothing, go straight
		// to the serve arm.
		if (TableG->TiltLockFlag != 0 && game_over_phase != 1)
			bonus_step = 6;

		ClearText(tag_info_text_box.Component);
		PlayGenericWave(tag_tabb42wave.Component);
	}
	else if (c == 0x53)  // a ball drained while the count-up was already done
	{
		if (bonus_step < 6)
			return;
	}
	else if (c != 0x50)
	{
		return;
	}

	char buffer[128];

	for (;;)
	{
		const int step = bonus_step++;

		// Two seconds per line normally, a quarter of a second while the
		// buried-treasure payout is compressing everything.
		const float lineTime = compressed_announcements != 0 ? 0.25f : 2.0f;

		// One shape for steps 0..4: if the term is non-zero, add it; and then,
		// only if we are not fast-forwarding, announce it and yield. The
		// accumulation happens either way -- it is inside the `&&` in the
		// original, before the skip test.
		int term = 0;
		int stringId = 0;
		switch (step)
		{
		case 0:
			term = TargetsDroppedThisBall * 10000;
			stringId = STR_TARGET_BONUS;
			break;
		case 1:
			term = tag_v_lite606.Component->MessageField * 200000;
			stringId = STR_MODE_BONUS;
			break;
		case 2:
			term = tag_v_lite607.Component->MessageField * 250000;
			stringId = STR_MAP_PIECE_BONUS;
			break;
		case 3:
			term = tag_special_treasure_light_group.Component->MessageField * 100000;
			stringId = STR_SPECIAL_TREASURES;
			break;
		case 4:
			term = tag_drain.Component->MessageField;  // already in points
			stringId = STR_SHIPS_HOLD;
			break;

		case 5:
			// Ship's Hold: carry the total to the next ball, or discard it.
			if (!tag_v_lite570.Component->light_on())
			{
				tag_drain.Component->MessageField = 0;
			}
			else
			{
				tag_v_lite570.Component->Message(MessageCode::TLightResetAndTurnOff, 0.0f);
				tag_drain.Component->MessageField = bonus_accumulator;
			}

			if (bonus_accumulator != 0)
			{
				// Unmultiplied: the bonus lands at face value, which is the
				// whole reason 0x423350 exists.
				// 0x42bbcf call 0x423350; 0x42bbd4 push eax: the line shows what was
				// credited, which differs from the sum by the table's ScoreAdded.
				const int credited = AddScoreUnmultiplied(caller, bonus_accumulator);
				FormatWithValue(buffer, GetRcString(STR_TOTAL_BONUS), credited);

				if (bonus_skip_display == 0)
				{
					Play(tag_tabb74wave.Component);
					// 1.5x the usual line time: `t/2.0 + t` in the listing.
					Display(tag_info_text_box.Component, buffer, lineTime / 2.0f + lineTime);
					return;
				}
			}
			continue;

		default:
			// ---- the serve arm -------------------------------------------
			// Another ball is still live and the game is over: nothing to do.
			if (game_over_phase != 0 && TableG->MultiballCount != 0)
				return;

			game_over_phase = 0;
			tag_mode_timer.Component->Message(ft::TimerSet, Forever);

			if (TableG->ExtraBalls == 0)
			{
				TableG->ChangeBallCount(TableG->BallCount - 1);

				// Last player, no balls left: game over, and the state goes to
				// 1 rather than to the skill shot.
				if (TableG->CurrentPlayer - TableG->PlayerCount == -1 && TableG->BallCount == 0)
				{
					TableG->LightGroup->Message(MessageCode::TLightResetAndTurnOff, 0.0f); // 0x42bd38
					tag_info_text_box.Component->MessageField = ModeGameOver;
					Handler(ft::ModeStarted, nullptr);
					TableG->LightGroup->Message(MessageCode::TLightFtResetOverride, 0.0f); // 0x42bdbf
					return;
				}

				TableG->Message(MessageCode::ClearTiltLock, 0.0f);
				tag_drain.Component->Message(MessageCode::SetTiltLock, 0.0f);
				TableG->Message(MessageCode::SwitchToNextPlayer, 0.0f);
				tag_s_launcher1.Component->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
			}
			else
			{
				// An extra ball: same player, same ball number, and the
				// announcement stays up until something replaces it.
				TableG->ExtraBalls--;

				const int player = TableG->CurrentPlayer;
				const int id = player >= 0 && player <= 2
					? STR_EXTRA_BALL_P1 + player
					: STR_EXTRA_BALL_P1 + 3;
				Display(tag_info_text_box.Component, GetRcString(id), Forever);

				TableG->Message(MessageCode::ClearTiltLock, 0.0f);
				tag_s_launcher1.Component->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
			}

			tag_info_text_box.Component->MessageField = ModeSkillShot;
			Handler(ft::ModeStarted, nullptr);
			TableG->LightGroup->Message(MessageCode::TLightFtResetOverride, 0.0f); // 0x42bdbf
			return;
		}

		// Steps 0..4 land here.
		if (term == 0)
			continue;

		bonus_accumulator += term;
		if (bonus_skip_display != 0)
			continue;

		PlayGenericWave(tag_tabb84wave.Component);
		FormatWithValue(buffer, GetRcString(stringId), term);
		Display(tag_info_text_box.Component, buffer, lineTime);
		return;
	}
}

} // namespace pirates
