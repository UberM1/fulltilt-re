// pirates_awards.cpp -- Skulduggery's award primitives: the twenty routines every
// other slice calls to actually *hand out* something.
//
// pirates_part1.cpp, pirates_part2.cpp and pirates_modes.cpp all declare these as
// prototypes and none defines them -- the same split that put the scoring wrappers in
// pirates_common.cpp. They belong together because they are one layer: a handler decides
// *that* an award is due, and one of these decides *what* the award is, what it scores,
// what it says on the display and how long the line stays up.
//
// The addresses, in the order the file defines them:
//
//   0x00423720  AwardCoin                  light the next coin, pay the escalating table
//   0x004239a0  CheckGoldComplete          GOLD mask == 0xf -> reset + coin
//   0x004239d0  AwardExtraBall             TableG->ExtraBalls = 1
//   0x00423a30  LightShipwreckMystery      arm v_lite529
//   0x00423a80  (local) extinguish v_lite529
//   0x00423aa0  AwardMapPiece              +1,000,000, and offer the finale when full
//   0x00424290  CheckWindsComplete         W-I-N-D-S -> a map piece
//   0x00423830  (local) "<coin> Buried"
//   0x00424780  (local) AwardSpecialTreasure by index -- the six named treasures
//   0x00424930  AwardSpecialTreasure       the same, by the group's own counter
//   0x00424950  (local) bury the next special treasure
//   0x00424aa0  (local) the fathom bonus formula
//   0x00424b00  BuryTreasureHuntAward      Treasure Hunt's east-wind-lane pouch
//   0x00425ab0  (local) dig the pouch up and pay all of it
//   0x00424350  BumpMultiplier             ScoreMultiplier = fathoms + 1 (flash in place)
//   0x004243d0  AdvanceFathomMultiplier    the same, lighting one more fathom
//   0x00424580  ArmNextTreasureX           advance the "X" cursor, return a sweep id
//   0x00424700  CollectJackpot             adds the jackpot, never resets it
//   0x00424d30  ArmRandomKegMagnet         Tavern's magnet arming
//   0x00424fb0  AwardTavernWindowMystery   the tavern sinks' 1-in-4
//   0x00425000  AwardMaxCrew               a_bump2->MessageField = 6
//   0x00425230  AwardIndicatedWind         Bermuda's progressive
//   0x00425440  (local) the Sword Fight award cycle
//   0x00425550  AwardSpecialTreasureOrNext the Sword Fight group award
//   0x00425c50  AwardShipwreckMystery      a uniform draw from six
//   0x00425d10  CollectShipwreckMystery    right outlane, only if lit
//   0x00424210  BrigCaptureBall            s_hole19's accounting
//
// Sources: analysis/pirates-rules.md (SS4.2-4.6 the collectibles, SS5.1-5.2 the shipwreck
// mystery and the buried-treasure pouch, S6 scoring and the brig) and
// decomp/pirates/functions/*.c -- but every body below was re-read from a fresh objdump of
// PIRATES.EXE, because Ghidra models these routines badly enough that the pseudocode is
// wrong about *return values* in six of them and about an argument in four more. The
// specific disagreements, each noted again at its site:
//
//   * 0x423720 / 0x423aa0 / 0x424d30 decompile as taking no argument. All three
//     `ret 0x4` and all three read and write a float at [esp+4]: they take a display
//     dwell and return the advanced one, exactly like their siblings. The mandated
//     signatures in this port drop that parameter, so each keeps the real routine as a
//     file-local `*At(float)` and the declared entry point calls it with 0.0.
//   * 0x423a30 decompiles as `return 1.0` (it is reading the 0x3f800000 that the
//     v_lite529 message pushes) and 0x424210 as `return 0.96` (likewise the launcher's
//     feed delay). Both actually return [esp+4]. 0x424aa0's "local_18[1]" and
//     0x424950's "return 2.0" are the same error.
//   * 0x424b00's two treasure arms decompile as `FUN_00424950()`; the listing passes
//     `&DAT_00435d18` and `&DAT_00435d1c`, which is the whole point of having two arms.
//   * 0x424780's switch argument comes in as a real parameter, not `unaff_retaddr`.
//
// Glyph tokens and score tables were read out of .data rather than trusted from the
// pseudocode's DAT_ names; see the two blocks below for the raw bytes.
//
// Message codes, piecewise as in the sibling slices -- TLight codes identical to
// upstream, the TLightGroup/TSoloTarget/TPopupTarget/TBlocker block +4, the
// gate/kickout/sink block +9, Control* +10, ControlCollision +11, mission codes +13, and
// the plunger block +1.

#include "pch.h"
#include "control.h"

#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballComponent.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates {

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------
//
// Plain `extern` against whatever the owning slice actually wrote, which is two different
// shapes on this table:
//
//   * where a slice already binds a component, its *alias* is a reference into the tag
//     (`TLight*& lite504 = tag_v_lite504.Component`), so the declaration here has to be
//     `extern TLight*&` too -- an `extern TLight*` would link against the tag's pointer
//     field and silently read the wrong four bytes;
//   * where only control_pirates.cpp or pirates_modes.cpp binds the tag and nobody wrote
//     an alias, the tag itself is declared and reached as `tag_x.Component`, with the
//     tag's own template argument. control_pirates.cpp types every component no slice
//     claimed as `component_tag<TPinballComponent>`, which is why six of the tags below
//     are not spelled with their real class.

extern TTextBox*& info_text_box;  // DAT_0043ad3c -- MessageField is the mode register
extern TPinballTable*& TableG;    // DAT_0043ab9c

// Collectible light groups.
extern TLightGroup*& coin_light_group;              // DAT_0043bc7c
extern TLightGroup*& special_treasure_light_group;  // DAT_0043bd54 -- MessageField is the
                                                    // treasure counter as well
extern TLightGroup*& fathom_light_group;            // DAT_0043bb4c -- the multiplier track

// DAT_0043bc9c. pirates_modes.cpp defines the tag and nobody aliased it.
extern component_tag<TLightGroup> tag_map_piece_light_group;

// The two counters Full Tilt parks in light MessageFields rather than in globals.
extern TLight*& lite607;  // DAT_0043af04 -- map pieces collected
// DAT_0043aeec -- modes completed this game; no alias, pirates_modes.cpp owns the tag.
extern component_tag<TLight> tag_v_lite606;

// The six named special treasures. control_pirates.cpp binds these as plain components.
extern component_tag<TPinballComponent> tag_v_lite571;  // DAT_0043bcb4 Gold ingots
extern component_tag<TPinballComponent> tag_v_lite572;  // DAT_0043bccc Silver chalice
extern component_tag<TPinballComponent> tag_v_lite573;  // DAT_0043bce4 Signet ring
extern component_tag<TPinballComponent> tag_v_lite574;  // DAT_0043bcfc The Frantz Ruby
extern component_tag<TPinballComponent> tag_v_lite575;  // DAT_0043bd14 Shapiro's Sceptre
extern component_tag<TPinballComponent> tag_v_lite576;  // DAT_0043bd2c Crown of Sandige

// DAT_0043b09c -- the Shipwreck Mystery light. Bound as a plain component, so the
// "is it lit" test has to go through TLight.
extern component_tag<TPinballComponent> tag_v_lite529;

// W-I-N-D-S. v_lite533 is s_trip11's ("W"); the other four are the rollovers'.
extern TLight*& lite533;  // DAT_0043b734
extern TLight*& lite534;  // DAT_0043b74c -- I
extern TLight*& lite535;  // DAT_0043b764 -- N
extern TLight*& lite536;  // DAT_0043b77c -- D
extern TLight*& lite537;  // DAT_0043b794 -- S
extern TLightGroup*& lane_winds_lights;  // DAT_0043b7b4 -- the group the flippers rotate

// GOLD. v_lite524 carries the 4-bit mask in its MessageField as well as being the "G".
extern TLight*& lite524;  // DAT_0043b134

// The Treasure Hunt "X" board: six marker lights over five cursor phases, plus the
// partner lights each phase flashes with its X, plus the two sinks the mode enables.
extern TLight*& mlite44;   // DAT_0043b1ac
extern TLight*& mlite104;  // DAT_0043b224
extern TLight*& mlite106;  // DAT_0043b1dc
extern TLight*& mlite107;  // DAT_0043b1c4
extern TLight*& mlite110;  // DAT_0043b20c
extern TLight*& lite519;   // DAT_0043c39c
extern TLight*& lite520;   // DAT_0043c3b4
extern TLight*& lite521;   // DAT_0043c3cc
extern TLight*& lite525;   // DAT_0043b14c
extern TLight*& lite526;   // DAT_0043b164
extern TLightGroup*& left_orbit_light_group;   // DAT_0043b5c4
extern TLightGroup*& right_orbit_light_group;  // DAT_0043b6ac
extern TPinballComponent*& a_sink9;            // DAT_0043b104
extern TPinballComponent*& a_sink12;           // DAT_0043b11c

// The six keg lights and their paired magnets.
extern TLight*& lite560;  // DAT_0043bd6c <-> s_magnet1
extern TLight*& lite561;  // DAT_0043bd84 <-> s_magnet2
extern TLight*& lite562;  // DAT_0043bd9c <-> s_magnet3
extern TLight*& lite563;  // DAT_0043bdb4 <-> s_magnet4
extern TLight*& lite564;  // DAT_0043bdcc <-> s_magnet5
extern TLight*& lite565;  // DAT_0043bde4 <-> s_magnet6
extern TPinballComponent*& s_magnet1;
extern TPinballComponent*& s_magnet2;
extern TPinballComponent*& s_magnet3;
extern TPinballComponent*& s_magnet4;
extern TPinballComponent*& s_magnet5;
extern TPinballComponent*& s_magnet6;
// DAT_0043bdfc -- the same six as a group. control_pirates.cpp binds it untyped.
extern component_tag<TPinballComponent> tag_keg_light_group;

// The bumpers. a_bump1 owns the hit counter and the scoring table; a_bump2 owns the crew
// level 0..6, which is what "Max Crew" sets.
extern TPinballComponent*& bump1;  // DAT_0043abec
extern TPinballComponent*& bump2;  // DAT_0043abfc

// Brig plumbing. s_oneway12 is the release one-way, enabled at the second captured ball.
extern component_tag<TPinballComponent> tag_s_oneway12;  // DAT_0043bc04
extern TPinballComponent*& s_launcher1;                  // DAT_0043abdc
extern TSound*& tabb39wave;                              // DAT_0043c1ec -- into the brig

// Voice-overs with no alias anywhere; control_pirates.cpp binds all four untyped, so each
// use has to cast back to TSound for Play().
extern component_tag<TPinballComponent> tag_tabb00wave;  // DAT_0043c444 extra ball
extern component_tag<TPinballComponent> tag_tabb83wave;  // DAT_0043c234 map piece
extern component_tag<TPinballComponent> tag_tabb84wave;  // DAT_0043bf7c coin / treasure
extern component_tag<TPinballComponent> tag_tabb85wave;  // DAT_0043c24c jackpot
extern TSound*& tabb12wave;                              // DAT_0043c0e4 wind lane

// ---------------------------------------------------------------------------
// Cross-slice interface
// ---------------------------------------------------------------------------

// pirates_common.cpp. AddScoreAt is MULTIPLIED; AddScoreUnmultiplied forces the
// multiplier to 1 and clears the bonus/jackpot flags around the call. Both return the
// amount actually credited, which is what every "%ld" line below formats.
int AddScoreAt(TPinballComponent* caller, int score);           // 0x00423310
int AddScoreUnmultiplied(TPinballComponent* caller, int score);  // 0x00423350

int Rnd(int n);                   // 0x0042dd0b
const char* GetRcString(int id);  // 0x00409730(id, 0)
void Display(TTextBox* box, const char* text, float time);        // 0x0041de40, font 0
void DisplayGlyph(TTextBox* box, const char* glyph, float time);  // 0x0041de40, font 1
void FormatWithValue(char* out, const char* format, int value);   // 0x0042dfaf, sprintf
void Play(TSound* sound);                                         // vtable +0x18

// pirates_modes.cpp. Returns the chosen slot; AwardMapPiece fires it when the sixth map
// piece lands during Idle, which is the only way the wheel ever offers Peg Leg's Loot.
int SelectNextMode();  // 0x00425850

// 0x00423870 -- re-raise or blank the four GOLD lights and clear v_lite524's mask.
// The argument is the same "mode" selector the other Reset*/Restore* helpers take: 1
// clears the mask and flashes the lights off, 0 restores them. Owned by the
// transition/board slice; declared here because CheckGoldComplete is the caller.
void ResetGoldLights(int mode);

// 0x00425110 -- flash (or stop flashing) the wind group the shared cursor currently
// indicates. Bermuda Triangle's mode start primes it; AwardIndicatedWind steps it.
void FlashIndicatedWind(bool on);

// 0x00423630 -- state 0xd. Resolved, and the collapse already happened:
// pirates_modes_entry.cpp defines exactly one function for this address and it is spelled
// StartJailBreak, which is what this file and pirates_part1.cpp both declare and call.
// pirates_modes.cpp's `EnterJailBreak` declaration was the second name; it had no
// definition and no call site, and it has been removed, so one address now has one name.
void StartJailBreak();

// ---------------------------------------------------------------------------
// Module state
//
// Eight cells no other slice defines. Non-static so that whichever Reset*/Restore* helper
// ends up owning the per-game teardown can declare them `extern` rather than copy them.
// ---------------------------------------------------------------------------

// 0x00435d10 -- the burial cursor, 0..7 then saturating on the coin arm. Advanced by
// BuryTreasureHuntAward and zeroed when the pouch is dug up.
int BuryCursor = 0;

// The pouch. 0x00435d14 is a count; the two treasure slots hold a treasure index or -1;
// the rest hold a score or a flag. The dig-up pays each cell and clears it.
int BuriedCoinCount = 0;       // 0x00435d14
int BuriedTreasureSlot1 = -1;  // 0x00435d18
int BuriedTreasureSlot2 = -1;  // 0x00435d1c
int BuriedFathomBonus = 0;     // 0x00435d20
int BuriedJackpot = 0;         // 0x00435d24
int BuriedExtraBall = 0;       // 0x00435d28

// 0x0043abac -- non-zero only while the buried-treasure pouch is being paid out in one
// burst, and read by exactly two places: the coin award and the special-treasure award,
// which then advance the display by 0.25 s per line instead of 2.0 s so a full pouch does
// not take half a minute.
//
// CORRECTION to analysis/pirates-rules.md S5.2, which calls this "halves every display
// dwell from 2.0 s to 1.625 s". The two constants are doubles assembled from a high dword
// of 0x40000000 and 0x3fd00000 with a zero low dword -- 2.0 and 0.25 exactly. Nothing is
// halved and 1.625 appears nowhere.
int payout_burst = 0;

// ---------------------------------------------------------------------------
// Table-local message codes
// ---------------------------------------------------------------------------
//
// Binary values, as in the sibling slices. constexpr at namespace scope has internal
// linkage, so repeating the block rather than sharing it costs nothing.

namespace ft {
// TLight family (delta +0 from upstream).
constexpr auto LightTurnOff = static_cast<MessageCode>(0x00);
constexpr auto LightFlasherStartTimed = static_cast<MessageCode>(0x07);  // -1.0 = forever
constexpr auto LightFlashTimedThenStayOn = static_cast<MessageCode>(0x0f);
constexpr auto LightFlashTimedThenStayOff = static_cast<MessageCode>(0x10);

// TLightGroup family (delta +4): the port's enum, not the raw byte. Sent raw, 0x29
// lands on TLightGroupResetAndTurnOn and 0x2a on ResetAndTurnOff.
constexpr auto GroupGetOnCount = MessageCode::TLightGroupGetOnCount;        // binary 0x29
constexpr auto GroupGetLightCount = MessageCode::TLightGroupGetLightCount;  // binary 0x2a
constexpr auto GroupTurnOnNext = MessageCode::TLightGroupResetAndTurnOn;    // binary 0x2d

// 0x4b, "arm the holding field", sent to the six s_magnet* and to the two kickouts.
//
// Settled: an unarmed magnet cannot catch a ball at all. Type 1042's vtable is CADET.EXE
// 0x00435640 (slot 0 at 0x0041aee0, `mov eax,0x412; ret`) and TMagnet::Message is
// 0x004166c0: 0x4b sets ActiveFlag ([this+0x5]) to 1 and 0x4c clears it. The magnet's
// capture circle is installed with &ActiveFlag -- the constructor at 0x00416873 passes
// `lea ecx,[esi+0x5]` as the edge's ActiveFlagPtr -- and TEdgeManager skips every edge
// whose ActiveFlag is clear, so an unarmed magnet is simply not in the world. Type 1012's
// TKickout::Message (0x004163a0) does the same and additionally releases a ball it is
// already holding when disarmed (0x00416443..0x00416473).
constexpr auto HoldFieldEnable = static_cast<MessageCode>(0x4b);
}  // namespace ft

// ---------------------------------------------------------------------------
// The mode register
//
// info_text_box->MessageField, as in pirates_part3.cpp. Only the one state this file
// tests is named: pirates_modes.cpp's `Mode` enum is file-local to that slice, so there is
// nothing to share yet.
// ---------------------------------------------------------------------------

constexpr int ModeIdle = 2;

// ---------------------------------------------------------------------------
// Glyph tokens
//
// Single characters in the table's symbol font, held in .data as a run of 4-byte slots.
// Read out of PIRATES.EXE rather than taken from the pseudocode's DAT_ names, which is
// how the multiplier run below turned out to be seven entries of up to three characters
// and not the one-per-slot the stride suggests:
//
//   0x43aad8  3a 00 00 00   ":"       fathom count 0
//   0x43aadc  3a 3d 3c 00   ":=<"     fathom count 1
//   0x43aae0  3a 3e 3c 00   ":><"     ... up to
//   0x43aaf0  3a 42 3c 00   ":B<"     fathom count 6, all six fathoms lit
//   0x43aaf4  43 00 00 00   "C"       map piece
//   0x43aaf8  44 00 00 00   "D"       coin
//   0x43aafc  45 00 00 00   "E"       keg magnet armed
//   0x43ab04  47 00 00 00   "G"       jackpot
//
// The port has no symbol font, so these render as the ASCII they literally contain --
// the same convention as pirates_common.cpp's glyph runs and pirates_part1.cpp's
// Glyph_IslandLane; see docs/engine-changes.md.
// ---------------------------------------------------------------------------

// 0x43aad8, stride 4, indexed by fathom_light_group's on-count 0..6.
const char* const MultiplierGlyphs[7] = {":", ":=<", ":><", ":?<", ":@<", ":A<", ":B<"};

constexpr const char* Glyph_MapPiece = "C";  // DAT_0043aaf4
constexpr const char* Glyph_Coin = "D";      // DAT_0043aaf8
constexpr const char* Glyph_KegMagnet = "E";  // DAT_0043aafc
constexpr const char* Glyph_Jackpot = "G";   // DAT_0043ab04

// ---------------------------------------------------------------------------
// String ids
//
// Pirates' own resource numbers. The pseudocode shows them in hex (0xbbb = 3003).
// ---------------------------------------------------------------------------

enum
{
	AwardStr_Score = 3003,           // bare "%ld"
	AwardStr_MapPiece = 3015,        // 0xbc7
	AwardStr_ExtraBall = 3014,       // 0xbc6
	AwardStr_Treasure0 = 3019,       // 0xbcb..0xbd0, the six named treasures
	AwardStr_TreasureBuried0 = 3034, // 0xbda..0xbdf, "<treasure name> Buried"
	AwardStr_CoinBuried = 3040,      // 0xbe0
	AwardStr_FathomBuried = 3041,    // 0xbe1
	AwardStr_JackpotBuried = 3042,   // 0xbe2
	AwardStr_ExtraBallBuried = 3043, // 0xbe3
	AwardStr_MaxCrew = 3044,         // 0xbe4
	AwardStr_FathomPaid = 3057,      // 0xbf1
	AwardStr_JackpotPaid = 3058,     // 0xbf2
	AwardStr_ShipwreckLit = 3059,    // 0xbf3
};

// ---------------------------------------------------------------------------
// Score tables, read verbatim out of .data
// ---------------------------------------------------------------------------

// 0x43d0c0, indexed by coin_light_group's on-count AFTER the next light comes on. Index 0
// and index 7 are genuinely zero, so a 7th coin pays nothing -- the manual's six
// escalating coin awards and no more.
const int CoinScores[8] = {0, 50000, 100000, 200000, 400000, 800000, 1600000, 0};

// 0x43d0e0, Bermuda Triangle's progressive for hitting the indicated lane. The fifth hit
// is not in this table: it pays the jackpot and resets the step.
const int IndicatedWindScores[4] = {125000, 250000, 500000, 1000000};

// ---------------------------------------------------------------------------
// Shared scratch
// ---------------------------------------------------------------------------

// DAT_00435cf8 -- the per-mode scratch counter, defined by pirates_common.cpp. Two of this
// file's routines read it, and they mean different things by it: AwardIndicatedWind uses
// it as Bermuda's progressive step 0..4, AwardSpecialTreasureOrNext as Sword Fight's
// completed-group bitfield (bits 0..3 volcano, bits 4..6 parrot). That aliasing is in the
// original and is safe only because the mode start zeroes the cell.
//
// Resolved, and the hazard is gone: there is one global for 0x435cf8, not two. The cell is
// defined once, by pirates_common.cpp (`int mode_scratch_counter = 0;`), and every other
// slice declares it extern -- pirates_modes.cpp at its own global block, pirates_part1.cpp,
// pirates_part2.cpp and this file. The rival `ModeScratch` definition the note warned about
// no longer exists anywhere in src/, so pirates_modes.cpp's mode start does clear the cell
// this file reads, and Bermuda's step is zeroed with everything else.
//
// The aliasing *within* the cell is still real and still intentional in the original: the
// same int is Bermuda's progressive step and Sword Fight's completed-group bitfield,
// which is safe only because no mode start can overlap another.
extern int mode_scratch_counter;

// DAT_00435d04 -- the shared cursor, defined by pirates_modes.cpp. The Treasure Hunt "X"
// selector, the Bermuda indicated-wind selector and the Sword Fight award selector are all
// this one int.
extern int WheelCursor;

// DAT_00435d3c -- balls the brig is holding, 0..3. Defined by pirates_modes.cpp.
extern int BallsInBrig;

// DAT_00435d48 -- type-1006 collisions this ball. Defined by pirates_modes.cpp; the
// fathom bonus formula is the only reader outside the drain bonus.
extern int TargetsDroppedThisBall;

// ---------------------------------------------------------------------------
// Local helpers
// ---------------------------------------------------------------------------

namespace {

// The display dwell every award advances by. 2.0 s normally, 0.25 s while the
// buried-treasure pouch is being paid out in one burst. Both constants are doubles in the
// binary; the sum is stored back as a float.
float Dwell()
{
	return payout_burst != 0 ? 0.25f : 2.0f;
}

// `Play` takes a TSound*, but control_pirates.cpp binds these four waves as plain
// components because no slice claimed them with a concrete type. The objects really are
// TSounds, so the downcast is safe -- it just cannot be checked here.
void PlayComponent(TPinballComponent* comp)
{
	Play(static_cast<TSound*>(comp));
}

// v_lite529 has the same problem: it is a TLight bound as a plain component, and the
// "is it lit" test is TLight::light_on().
TLight* ShipwreckLight()
{
	return static_cast<TLight*>(tag_v_lite529.Component);
}

}  // namespace

// Forward declarations for the award cycle, which is mutually recursive through the
// shipwreck mystery: a mystery can pay a buried-treasure pouch, and a pouch can hold a
// coin or a treasure.
static float AwardCoinAt(float time);
static float AwardMapPieceAt(float time);
static float AwardExtraBallAt(float time);
static float AwardSpecialTreasureIndex(int index, float time);
static float AwardShipwreckMysteryAt(float time);
static float PayBuriedTreasure(float time);

float AwardSpecialTreasure(float t);
float BumpMultiplier(float t);
float AwardMaxCrew(float t);
float CollectJackpot(float displayTime);

// ===========================================================================
// Coins and GOLD
// ===========================================================================

// 0x00423720 -- the coin award.
//
// Light the next coin light, read the group's on-count back, pay CoinScores[count], and
// print the glyph and then the amount. Six sources reach it: GOLD complete, a Sword Fight
// group clear, the tavern sinks' 1-in-4, skill-shot slots 1/2/8, a buried coin, and the
// shipwreck mystery by way of AwardTavernWindowMystery.
//
// The real routine takes a display dwell and returns the advanced one (`ret 0x4`, with
// [esp+4] read three times and written twice). Ghidra models it as taking nothing, which
// is why the declared entry point in the other slices is `void AwardCoin()`; this is the
// routine, and AwardCoin below is the mandated spelling of it.
static float AwardCoinAt(float time)
{
	char line[64];

	coin_light_group->Message(ft::GroupTurnOnNext, 1.0f);
	const int count = coin_light_group->Message(ft::GroupGetOnCount, 1.0f);

	// Multiplied. The 1.0 on the on-count query is a leftover of the previous push and
	// is reproduced because the group ignores it either way.
	const int credited = AddScoreAt(nullptr, CoinScores[count & 7]);
	FormatWithValue(line, GetRcString(AwardStr_Score), credited);

	time += Dwell();
	DisplayGlyph(info_text_box, Glyph_Coin, time);
	time += Dwell();
	Display(info_text_box, line, time);

	PlayComponent(tag_tabb84wave.Component);
	return time;
}

// 0x00423720, as the other slices declare it.
//
// Resolved, and nothing is lost. The original does return the dwell in st(0), and the
// split across its five call sites is exact:
//
//   0x004239ba  chains it (CheckGoldComplete's tail: fstp [esp+4]; fld [esp+4]; ret)
//   0x00425470  chains it, then `inc ds:0x435d04` (the wheel cursor)
//   0x00425ad3  stores it into a local and keeps using it (the pouch payout loop)
//   0x00424ff0  discards it (`fstp st(0)` immediately after the call)
//   0x0042ac57  discards it, then tail-calls 0x00423460 (EnterIdle)
//
// so it is two discards and three chains, not three dropped chains. `AwardCoinAt` below
// carries the float, and every chaining site in this file uses it; the `void AwardCoin()`
// spelling the other slices declare is reached from exactly one place,
// pirates_modes.cpp's skill-shot coin slot, which is the original's 0x0042ac57 -- a site
// that discards the value anyway. The void wrapper is therefore faithful where it is used.
void AwardCoin()
{
	AwardCoinAt(0.0f);
}

// 0x004239a0 -- the GOLD completion check.
//
// GOLD is three hideaways plus one right-orbit pass, and the 4-bit mask lives in
// v_lite524->MessageField (bit 8, the "D", is the orbit). At 0xf: blank the four lights,
// clear the mask and award a coin.
float CheckGoldComplete(float t)
{
	if ((lite524->MessageField & 0xf) == 0xf)
	{
		ResetGoldLights(1);
		t = AwardCoinAt(t);
	}
	return t;
}

// ===========================================================================
// Extra ball
// ===========================================================================

// 0x004239d0 -- the extra ball.
//
// An assignment, not an increment: however many ways a player stacks extra balls in one
// ball, the drain bonus only ever serves one. That is in the original.
static float AwardExtraBallAt(float time)
{
	PlayComponent(tag_tabb00wave.Component);
	time += 2.0f;
	TableG->ExtraBalls = 1;
	Display(info_text_box, GetRcString(AwardStr_ExtraBall), time);
	return time;
}

// 0x004239d0, as the other slices declare it. See AwardCoin for the dropped return.
void AwardExtraBall(float t)
{
	AwardExtraBallAt(t);
}

// ===========================================================================
// The Shipwreck Mystery light
// ===========================================================================

// 0x00423a30 -- arm the Shipwreck Mystery.
//
// Lit by any hideaway during Bermuda Triangle or Storm, and by Storm's mode-ending
// hideaway. The flash is timed-then-stay-ON for 1.0 s, i.e. it settles solid and waits for
// the right outlane.
//
// Ghidra returns 1.0 here; that is the 0x3f800000 the v_lite529 message pushes. The
// listing's `flds 0x4(%esp)` after the call reads the parameter slot, which the
// announcement already advanced by 2.0.
static float LightShipwreckMysteryAt(float time)
{
	time += 2.0f;
	Display(info_text_box, GetRcString(AwardStr_ShipwreckLit), time);
	ShipwreckLight()->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	return time;
}

// 0x00423a30, as the other slices declare it.
void LightShipwreckMystery(float t)
{
	LightShipwreckMysteryAt(t);
}

// 0x00423a80 -- disarm it. Flash for 1.0 s and then stay off, which is the collect
// animation; the only caller is CollectShipwreckMystery.
static void ExtinguishShipwreckMystery()
{
	ShipwreckLight()->Message(ft::LightFlashTimedThenStayOff, 1.0f);
}

// ===========================================================================
// Map pieces
// ===========================================================================

// 0x00423aa0 -- the map piece.
//
// +1,000,000 and one more light in map_piece_light_group, with the count mirrored into
// v_lite607->MessageField because the drain bonus reads it from there. If that completes
// the group *and* the table is idle, SelectNextMode runs immediately and returns slot 7 --
// which is the only path that ever offers Peg Leg's Loot on the wheel.
//
// Note the asymmetry: the offer is gated on Idle, but the map piece itself is not, so
// collecting the sixth piece inside a mode banks it and silently skips the offer.
//
// Like the coin award this really takes a dwell and returns it advanced (two 2.0 s steps);
// Ghidra's `return 0.0` is an artefact.
static float AwardMapPieceAt(float time)
{
	auto* group = tag_map_piece_light_group.Component;

	group->Message(ft::GroupTurnOnNext, 1.0f);
	time += 2.0f;
	lite607->MessageField++;
	DisplayGlyph(info_text_box, Glyph_MapPiece, time);

	time += 2.0f;
	Display(info_text_box, GetRcString(AwardStr_MapPiece), time);

	AddScoreAt(nullptr, 1000000);
	PlayComponent(tag_tabb83wave.Component);

	const int lit = group->Message(ft::GroupGetOnCount, 0.0f);
	const int total = group->Message(ft::GroupGetLightCount, 0.0f);
	if (lit == total && info_text_box->MessageField == ModeIdle)
		SelectNextMode();

	return time;
}

// 0x00423aa0, as the other slices declare it. The declared return exists but the original
// hands back the advanced dwell, so this reproduces that rather than the pseudocode's 0.0.
float AwardMapPiece()
{
	return AwardMapPieceAt(0.0f);
}

// 0x00424290 -- W-I-N-D-S complete.
//
// All five letter lights owned (lit, toggled on, or flashing -- TLight::light_on()), and
// the whole group is blanked and flashed off before the map piece is paid. The letters are
// rotated right by either flipper, so the player assembles them by lane *and* by flipper.
float CheckWindsComplete(float time)
{
	if (lite533->light_on() && lite534->light_on() && lite535->light_on()
		&& lite536->light_on() && lite537->light_on())
	{
		lane_winds_lights->Message(ft::LightTurnOff, 0.0f);
		lane_winds_lights->Message(ft::LightFlashTimedThenStayOff, 1.0f);
		time = AwardMapPieceAt(time);
	}
	return time;
}

// ===========================================================================
// Special treasures
// ===========================================================================

// 0x00424780 -- award the index-th special treasure.
//
// The counter is bumped FIRST and the switch is on the caller's index, so the six
// treasures always come in the same order. Not reachable directly from any other slice:
// AwardSpecialTreasure below supplies the index from the group's own counter, and the
// buried-treasure payout supplies a saved one.
//
// Original-source bug, reproduced: the `default` arm means a 7th and every later treasure
// re-lights v_lite576 and pays the Crown of Sandige's 1,500,000 again, while the counter
// keeps growing. That inflates the drain bonus's "Special Treasures" term without bound.
// It does NOT inflate Peg Leg's Loot, which reads the group's on-count instead.
static float AwardSpecialTreasureIndex(int index, float time)
{
	special_treasure_light_group->MessageField++;

	int stringId;
	TPinballComponent* light;
	int score;
	switch (index)
	{
	case 0:
		stringId = AwardStr_Treasure0 + 0;  // Gold ingots
		light = tag_v_lite571.Component;
		score = 100000;
		break;
	case 1:
		stringId = AwardStr_Treasure0 + 1;  // Silver chalice
		light = tag_v_lite572.Component;
		score = 250000;
		break;
	case 2:
		stringId = AwardStr_Treasure0 + 2;  // Signet ring
		light = tag_v_lite573.Component;
		score = 500000;
		break;
	case 3:
		stringId = AwardStr_Treasure0 + 3;  // The Frantz Ruby
		light = tag_v_lite574.Component;
		score = 750000;
		break;
	case 4:
		stringId = AwardStr_Treasure0 + 4;  // Shapiro's Sceptre
		light = tag_v_lite575.Component;
		score = 1000000;
		break;
	default:
		stringId = AwardStr_Treasure0 + 5;  // Crown of Sandige
		light = tag_v_lite576.Component;
		score = 1500000;
		break;
	}

	const char* text = GetRcString(stringId);
	light->Message(ft::LightFlashTimedThenStayOn, 1.0f);

	// Multiplied, and the announcement is a plain name with no operand.
	AddScoreAt(nullptr, score);

	time += Dwell();
	Display(info_text_box, text, time);
	PlayComponent(tag_tabb84wave.Component);
	return time;
}

// 0x00424930 -- the next special treasure.
//
// Three lines in the original: read the group's counter, hand it to 0x424780, return what
// that returns. Every "X" in Treasure Hunt, the shipwreck mystery's roll 0 and the Sword
// Fight cycle's step 0 all arrive here.
float AwardSpecialTreasure(float t)
{
	return AwardSpecialTreasureIndex(special_treasure_light_group->MessageField, t);
}

// ===========================================================================
// Treasure Hunt's buried-treasure pouch
// ===========================================================================

// 0x00423830 -- "<coin> Buried". No score and no light: the burial only writes the pouch.
static float DisplayBuriedCoin(float time)
{
	time += 2.0f;
	Display(info_text_box, GetRcString(AwardStr_CoinBuried), time);
	return time;
}

// 0x00424950 -- bury the next special treasure in `slot`.
//
// Reads the group's on-count (NOT its MessageField counter, which is what the award path
// uses), stores it in the slot so the dig-up can replay it, flashes that treasure's light
// for a second and names it. Ghidra drops the slot pointer and returns a bare 2.0; the
// listing passes the slot and returns the advanced dwell.
static float BurySpecialTreasure(int* slot, float time)
{
	const int count = special_treasure_light_group->Message(ft::GroupGetOnCount, 0.0f);
	*slot = count;

	int stringId;
	TPinballComponent* light;
	switch (count)
	{
	case 0:
		stringId = AwardStr_TreasureBuried0 + 0;
		light = tag_v_lite571.Component;
		break;
	case 1:
		stringId = AwardStr_TreasureBuried0 + 1;
		light = tag_v_lite572.Component;
		break;
	case 2:
		stringId = AwardStr_TreasureBuried0 + 2;
		light = tag_v_lite573.Component;
		break;
	case 3:
		stringId = AwardStr_TreasureBuried0 + 3;
		light = tag_v_lite574.Component;
		break;
	case 4:
		stringId = AwardStr_TreasureBuried0 + 4;
		light = tag_v_lite575.Component;
		break;
	default:
		stringId = AwardStr_TreasureBuried0 + 5;
		light = tag_v_lite576.Component;
		break;
	}

	const char* text = GetRcString(stringId);
	light->Message(ft::LightFlasherStartTimed, 1.0f);

	time += 2.0f;
	Display(info_text_box, text, time);
	PlayComponent(tag_tabb84wave.Component);
	return time;
}

// 0x00424aa0 -- the fathom bonus, which is what cursor 5 buries.
//
//   fathoms_lit * ((treasures*2 + map_pieces)*25 + modes_completed*20 + targets) * 10,000
//
// Not in the manual. The compiler emitted every multiply as an lea chain
// (lea(x,x,4) for *5, repeated, then shl 4), which is the only reason the constants are
// legible at all; they were read back out of the listing rather than out of the
// pseudocode's folded arithmetic.
static int FathomBonus()
{
	const int treasures = special_treasure_light_group->MessageField;
	const int mapPieces = lite607->MessageField;
	const int modes = tag_v_lite606.Component->MessageField;

	const int term = (treasures * 2 + mapPieces) * 25 + modes * 20 + TargetsDroppedThisBall;
	const int fathoms = fathom_light_group->Message(ft::GroupGetOnCount, 0.0f);
	return fathoms * term * 10000;
}

// 0x00424b00 -- bury an award.
//
// Either east wind lane during Treasure Hunt. The cursor advances on every burial and
// never resets here -- only the dig-up zeroes it -- so a long Treasure Hunt walks the
// whole pouch and then keeps burying coins.
float BuryTreasureHuntAward(float time)
{
	const int cursor = BuryCursor;
	BuryCursor++;

	switch (cursor)
	{
	case 0:
	case 1:
	case 2:
		time = DisplayBuriedCoin(time);
		BuriedCoinCount++;
		return time;

	case 3:
		return BurySpecialTreasure(&BuriedTreasureSlot1, time);

	case 4:
		return BurySpecialTreasure(&BuriedTreasureSlot2, time);

	case 5:
		time += 2.0f;
		Display(info_text_box, GetRcString(AwardStr_FathomBuried), time);
		// Evaluated at burial, not at payout -- so it is worth whatever the collection
		// state was when it went into the ground.
		BuriedFathomBonus = FathomBonus();
		return time;

	case 6:
		time += 2.0f;
		Display(info_text_box, GetRcString(AwardStr_JackpotBuried), time);
		// Likewise a snapshot: the jackpot's value at burial, not at payout.
		BuriedJackpot = TableG->JackpotScore;
		return time;

	case 7:
		time += 2.0f;
		Display(info_text_box, GetRcString(AwardStr_ExtraBallBuried), time);
		BuriedExtraBall = 1;
		return time;

	default:
		time = DisplayBuriedCoin(time);
		BuriedCoinCount++;
		return time;
	}
}

// 0x00425ab0 -- dig the pouch up and pay all of it.
//
// Reached only as the shipwreck mystery's sixth roll. `payout_burst` is held for the
// duration so the coins and treasures take 0.25 s a line instead of 2.0 s; the four
// hand-written lines here keep the full 2.0 s either way.
//
// Two oddities, both in the original and both reproduced:
//
//   * the coin loop leaves BuriedCoinCount at 1, not 0 -- so one coin is always still
//     buried after a payout, and the next payout pays it;
//   * the second treasure is paid as AwardSpecialTreasureIndex(BuriedTreasureSlot1, ...),
//     re-reading the first slot, which the line above has already cleared to -1. So the
//     second buried treasure always falls to the `default` arm and always pays the Crown
//     of Sandige. That is a genuine bug, and it is left in because suppressing it would
//     change the payout.
static float PayBuriedTreasure(float time)
{
	char line[64];

	payout_burst = 1;
	BuryCursor = 0;

	for (; BuriedCoinCount > 0; BuriedCoinCount--)
		time = AwardCoinAt(time);
	BuriedCoinCount = 1;

	if (BuriedTreasureSlot1 != -1)
	{
		time = AwardSpecialTreasureIndex(BuriedTreasureSlot1, time);
		BuriedTreasureSlot1 = -1;
	}
	if (BuriedTreasureSlot2 != -1)
	{
		// The bug: BuriedTreasureSlot1, not BuriedTreasureSlot2.
		time = AwardSpecialTreasureIndex(BuriedTreasureSlot1, time);
		BuriedTreasureSlot2 = -1;
	}

	if (BuriedFathomBonus != 0)
	{
		const int credited = AddScoreUnmultiplied(nullptr, BuriedFathomBonus);
		FormatWithValue(line, GetRcString(AwardStr_FathomPaid), credited);
		time += 2.0f;
		Display(info_text_box, line, time);
		BuriedFathomBonus = 0;
	}

	if (BuriedJackpot != 0)
	{
		const int credited = AddScoreUnmultiplied(nullptr, BuriedJackpot);
		FormatWithValue(line, GetRcString(AwardStr_JackpotPaid), credited);
		time += 2.0f;
		Display(info_text_box, line, time);
		PlayComponent(tag_tabb85wave.Component);
		BuriedJackpot = 0;
	}

	if (BuriedExtraBall != 0)
	{
		// The only sub-award whose return is discarded, so the extra ball's line
		// overlaps whatever came before it.
		AwardExtraBallAt(time);
		BuriedExtraBall = 0;
	}

	payout_burst = 0;
	return time;
}

// ===========================================================================
// The fathom multiplier
// ===========================================================================

// 0x00424350 -- bump the multiplier.
//
// TPinballTable::ScoreMultiplier = fathoms_lit + 1. Full Tilt holds the multiplier VALUE
// here, not an index into a table (see the comment in reference/.../table_rules.h), so 1
// is neutral and the ceiling is 7 with all six fathom lights lit.
//
// This arm *flashes* the already-lit track rather than lighting one more
// (LightFlashTimedThenStayOn, not GroupTurnOnNext) -- which is the only difference from
// AdvanceFathomMultiplier below, and it is why the ship wire ramp and the shipwreck
// mystery can be worth a multiplier step without adding a fathom.
//
// A negative dwell suppresses the announcement entirely; Peg Leg's Loot's payout calls it
// with -1.0 for exactly that reason, since it is already printing its own lines.
//
// Disagreement with the manual [C]: the manual says the ship wire ramp *maxes* the
// multiplier. It increments it.
float BumpMultiplier(float t)
{
	fathom_light_group->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	const int count = fathom_light_group->Message(ft::GroupGetOnCount, 0.0f);

	if (!(t < 0.0f))
	{
		t += 2.0f;
		DisplayGlyph(info_text_box, MultiplierGlyphs[count < 7 ? count : 6], t);
	}

	TableG->ScoreMultiplier = count + 1;
	return t;
}

// 0x004243d0 -- the island lanes' award: light one more fathom, then the same.
//
// No negative-dwell guard here: the island lanes always announce.
float AdvanceFathomMultiplier(float time)
{
	fathom_light_group->Message(ft::GroupTurnOnNext, 1.0f);
	const int count = fathom_light_group->Message(ft::GroupGetOnCount, 0.0f);

	time += 2.0f;
	DisplayGlyph(info_text_box, MultiplierGlyphs[count < 7 ? count : 6], time);

	TableG->ScoreMultiplier = count + 1;
	return time;
}

// ===========================================================================
// Treasure Hunt's "X" board
// ===========================================================================

// 0x00424580 -- advance the "X" cursor and arm the next one.
//
// The heart of Treasure Hunt. Enables the two extra sinks (they are inert outside the
// mode), advances the shared cursor and flashes one "X" marker together with whatever
// hideaway or orbit group it sits on, forever (-1.0). The return is the light-sweep id for
// that phase, which is why Treasure Hunt's sweep is not a constant like every other
// mode's.
//
// The cursor is read before it is incremented and the wrap is in the default arm, so the
// phase order is 0,1,2,3,4->0 and phase 4 is the one that resets it.
int ArmNextTreasureX()
{
	a_sink9->ActiveFlag = 1;
	a_sink12->ActiveFlag = 1;

	const int phase = WheelCursor;
	WheelCursor++;

	const float forever = -1.0f;
	switch (phase)
	{
	case 0:
		mlite110->Message(ft::LightFlasherStartTimed, forever);
		left_orbit_light_group->Message(ft::LightFlasherStartTimed, forever);
		return 5;

	case 1:
		mlite107->Message(ft::LightFlasherStartTimed, forever);
		lite525->Message(ft::LightFlasherStartTimed, forever);
		lite520->Message(ft::LightFlasherStartTimed, forever);
		return 2;

	case 2:
		mlite104->Message(ft::LightFlasherStartTimed, forever);
		right_orbit_light_group->Message(ft::LightFlasherStartTimed, forever);
		return 6;

	case 3:
		mlite106->Message(ft::LightFlasherStartTimed, forever);
		lite526->Message(ft::LightFlasherStartTimed, forever);
		lite521->Message(ft::LightFlasherStartTimed, forever);
		return 3;

	default:
		WheelCursor = 0;
		mlite44->Message(ft::LightFlasherStartTimed, forever);
		lite524->Message(ft::LightFlasherStartTimed, forever);
		lite519->Message(ft::LightFlasherStartTimed, forever);
		return 1;
	}
}

// ===========================================================================
// The jackpot
// ===========================================================================

// 0x00424700 -- award the jackpot.
//
// Adds TPinballTable::JackpotScore and never resets it: the jackpot on this table only
// ever grows, which is what the manual says and what makes the +20,000/+40,000/+750,000
// accruals in the dispatcher meaningful.
//
// Unmultiplied, so the fathom multiplier does not apply to it.
//
// The sign of the argument is a flag as well as a value: a negative dwell is used as its
// own magnitude AND suppresses the voice-over, which is how the callers that are already
// in the middle of a noisy sequence (the brig, the drop banks) ask for a silent jackpot.
float CollectJackpot(float displayTime)
{
	if (!(displayTime < 0.0f))
		PlayComponent(tag_tabb85wave.Component);
	else
		displayTime = -displayTime;

	displayTime += 2.0f;
	DisplayGlyph(info_text_box, Glyph_Jackpot, displayTime);
	AddScoreUnmultiplied(nullptr, TableG->JackpotScore);
	return displayTime;
}

// ===========================================================================
// Tavern: the keg magnets and the window mystery
// ===========================================================================

// 0x00424d30 -- arm a random keg magnet.
//
// Build the list of keg lights that are NOT owned; if every one is already on, use all
// six instead. Draw one uniformly, arm its paired magnet, light that keg, and announce.
// Called only from the tavern drop-bank routine while state == 7 -- once per tavern-target
// hit and again on bank completion.
//
// Two notes. The keg group's on-count is read and then DISCARDED (the listing keeps the
// result in eax and immediately overwrites it with the display time), so the read is dead
// code in the original; it is left in because it is a real message to a real component and
// a group could in principle do something with it.
//
// And this is the third routine Ghidra models as taking nothing: it `ret 0x4` and its
// "local_18[1]" is the parameter slot aliased onto the candidate array. So the comment in
// pirates_part1.cpp -- "returns the keg group's on-count" -- is wrong: the return is the
// advanced display dwell. That matters, because part1 chains it straight into
// CollectJackpot, which would otherwise be handed a light count as a time.
static float ArmRandomKegMagnetAt(float time)
{
	TLight* const kegLights[6] = {lite560, lite561, lite562, lite563, lite564, lite565};
	TPinballComponent* const magnets[6] = {
		s_magnet1, s_magnet2, s_magnet3, s_magnet4, s_magnet5, s_magnet6,
	};

	int candidates[6];
	int count = 0;
	for (int i = 0; i < 6; i++)
		if (!kegLights[i]->light_on())
			candidates[count++] = i;

	if (count == 0)
	{
		for (int i = 0; i < 6; i++)
			candidates[i] = i;
		count = 6;
	}

	const int pick = candidates[Rnd(count)];
	magnets[pick]->Message(ft::HoldFieldEnable, 0.0f);
	kegLights[pick]->Message(ft::LightFlashTimedThenStayOn, 1.0f);

	// Dead in the original; see the note above.
	tag_keg_light_group.Component->Message(ft::GroupGetOnCount, 0.0f);

	time += 2.0f;
	DisplayGlyph(info_text_box, Glyph_KegMagnet, time);
	return time;
}

// 0x00424d30, as pirates_part1.cpp declares it.
float ArmRandomKegMagnet()
{
	return ArmRandomKegMagnetAt(0.0f);
}

// 0x00424fb0 -- the tavern sinks' mystery award.
//
// A uniform draw from four: map piece, special treasure, coin, extra ball. Every arm is
// called with a 0.0 dwell and every return is discarded, which is why this one really is
// `void` in the original as well.
void AwardTavernWindowMystery()
{
	switch (Rnd(4))
	{
	case 0:
		AwardMapPieceAt(0.0f);
		return;
	case 1:
		AwardSpecialTreasure(0.0f);
		return;
	case 2:
		AwardCoinAt(0.0f);
		return;
	default:
		AwardExtraBallAt(0.0f);
		return;
	}
}

// ===========================================================================
// Crew
// ===========================================================================

// 0x00425000 -- Max Crew.
//
// a_bump2->MessageField is the crew level 0..6 and a_bump1's seven-entry scoring table is
// the per-bumper value, so setting the level to 6 jumps the bumpers straight to 10,500 a
// hit. The level is set unconditionally; only the announcement is gated on a non-negative
// dwell, which is how Peg Leg's Loot's payout takes the crew without printing a line.
//
// The "%ld" operand is a_bump1->get_scoring(6) -- the NEW per-bumper value -- so the line
// reads "Max Crew / Bumpers Now 10500".
float AwardMaxCrew(float t)
{
	bump2->MessageField = 6;

	if (!(t < 0.0f))
	{
		char line[64];
		const int perBumper = bump1->get_scoring(6);
		FormatWithValue(line, GetRcString(AwardStr_MaxCrew), perBumper);
		t += 2.0f;
		Display(info_text_box, line, t);
	}

	return t;
}

// ===========================================================================
// Bermuda Triangle's progressive
// ===========================================================================

// 0x00425230 -- hit the indicated wind lane.
//
// Move the flash to the next lane first, then pay the step. The four steps are
// 125,000 / 250,000 / 500,000 / 1,000,000, UNMULTIPLIED, and the fifth hit pays the
// jackpot and resets the step to 0 instead -- so the ladder is repeatable within one
// Bermuda Triangle.
//
// Note that the step counter is the shared mode scratch cell, and that the cursor it
// advances is the shared wheel cursor, so starting Bermuda Triangle also moves Treasure
// Hunt's "X" selector. See the note on mode_scratch_counter above.
void AwardIndicatedWind()
{
	FlashIndicatedWind(false);
	WheelCursor++;
	FlashIndicatedWind(true);

	if (mode_scratch_counter >= 4)
	{
		CollectJackpot(0.0f);
		mode_scratch_counter = 0;
		return;
	}

	char line[64];
	const int credited =
		AddScoreUnmultiplied(nullptr, IndicatedWindScores[mode_scratch_counter]);
	FormatWithValue(line, GetRcString(AwardStr_Score), credited);
	Display(info_text_box, line, 2.0f);
	Play(tabb12wave);
	mode_scratch_counter++;
}

// ===========================================================================
// Sword Fight's group award
// ===========================================================================

// 0x00425440 -- the award cycle.
//
// Cyclic in a fixed order and NOT a random pick, which is where the manual's "one of:
// coin award, special treasure, map piece or extra ball" is loose: 0 pays a special
// treasure, 1 a coin, 2 a map piece and >=3 an extra ball, and the cursor keeps advancing.
// Clearing four groups in one Sword Fight therefore pays all four once each and then extra
// balls forever.
static float CycleSwordFightAward(float time)
{
	float result;
	if (WheelCursor == 0)
		result = AwardSpecialTreasure(time);
	else if (WheelCursor == 1)
		result = AwardCoinAt(time);
	else if (WheelCursor == 2)
		result = AwardMapPieceAt(time);
	else
		result = AwardExtraBallAt(time);

	WheelCursor++;
	return result;
}

// 0x00425550 -- the Sword Fight group award.
//
// The mode scratch cell doubles as a bitfield here: bits 0..3 are the four volcano
// targets and bits 4..6 the three parrot targets. Either group complete clears *that*
// group's bits (keeping the other group's) and pays one cycle step. The parrot test comes
// first, so a hit that completes both in the same frame pays once and leaves the volcano
// bits standing for the next hit.
float AwardSpecialTreasureOrNext(float time)
{
	const int scratch = mode_scratch_counter;

	if ((scratch & 0x70) == 0x70)
	{
		mode_scratch_counter = scratch & 0x0f;
		return CycleSwordFightAward(time);
	}

	if ((scratch & 0x0f) == 0x0f)
	{
		mode_scratch_counter = scratch & 0x70;
		return CycleSwordFightAward(time);
	}

	return time;
}

// ===========================================================================
// The Shipwreck Mystery award
// ===========================================================================

// 0x00425c50 -- fire the Shipwreck Mystery.
//
// A uniform draw from six, exactly the manual's list, with the one correction that roll 3
// increments the multiplier rather than maxing it. Reached from the right outlane when the
// light is lit, and directly from skill-shot slot 0.
static float AwardShipwreckMysteryAt(float time)
{
	switch (Rnd(6))
	{
	case 0:
		return AwardSpecialTreasure(time);
	case 1:
		return AwardExtraBallAt(time);
	case 2:
		return AwardMaxCrew(time);
	case 3:
		return BumpMultiplier(time);
	case 4:
		return AwardMapPieceAt(time);
	default:
		return PayBuriedTreasure(time);
	}
}

// 0x00425c50, as the other slices declare it.
void AwardShipwreckMystery(float t)
{
	AwardShipwreckMysteryAt(t);
}

// 0x00425d10 -- collect it at the right outlane.
//
// Only if the light is owned, and the light is extinguished before the award fires -- so
// an award that re-lights it (nothing currently does) would survive the collect.
float CollectShipwreckMystery(float time)
{
	if (ShipwreckLight()->light_on())
	{
		ExtinguishShipwreckMystery();
		time = AwardShipwreckMysteryAt(time);
	}
	return time;
}

// ===========================================================================
// The brig
// ===========================================================================

// 0x00424210 -- the brig captures a ball.
//
// One ball leaves play and one more is held. The second captured ball enables s_oneway7's
// counterpart s_oneway12 (the release), and the third starts Jail Break instead of serving
// a replacement -- which is why the brig is both a ball-save and a multiball trigger.
//
// The MultiballCount floor at 0 is defensive in the original and is kept: the brig can be
// fed while the count is already 0 during a teardown.
//
// Ghidra returns 0.96 here; that is the launcher's feed delay, which the listing pushes
// and the return then reads past. The real return is the parameter, untouched.
float BrigCaptureBall(float t)
{
	TableG->MultiballCount--;
	BallsInBrig++;

	if (BallsInBrig == 2)
		tag_s_oneway12.Component->ActiveFlag = 1;

	if (TableG->MultiballCount < 0)
		TableG->MultiballCount = 0;

	if (BallsInBrig == 3)
	{
		StartJailBreak();
		return t;
	}

	Play(tabb39wave);

	// Binary 0x3fb. The plunger block is +1 from upstream, so this is
	// PlungerRelaunchBall: a replacement ball with no skill shot, after the 0.96 s feed
	// delay upstream hardcodes and Full Tilt made an argument.
	s_launcher1->Message(MessageCode::PlungerRelaunchBall, 0.96f);
	return t;
}

}  // namespace pirates
