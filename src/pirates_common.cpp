// pirates_common.cpp -- Skulduggery's shared scoring wrappers and module state.
//
// pirates_part1.cpp, pirates_part2.cpp and pirates_modes.cpp all declare these as
// prototypes and none defines them, because each slice was written against the
// others' interfaces. They belong here: all three call them, and the scoring
// tracker below is genuinely shared state that must have exactly one instance.
//
// Structure follows Dragon's equivalents in dragon_part1.cpp, with the addresses
// and one behavioural difference noted below.

#include "pch.h"
#include "control.h"

#include <cstdio>
#include <cstring>

#include "high_score.h"
#include "pb.h"
#include "TPinballTable.h"
#include "TPinballComponent.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates
{
extern TPinballTable*& TableG;

// 0x00435d44 -- the component that last scored (AddScore 0x423310 reads it at
// 0x423311 and stores the caller at 0x42332a; 0x435d2c is Storm's target score).
//
// Skulduggery uses this for more than bookkeeping. When a different component
// scores, the previous one is sent 0x4d, and the four lane handlers answer that by
// clearing their combo light group over 2.0 s. So this pointer is what makes combos
// breakable: score anywhere else and the island and orbit ladders reset. Dragon
// sends the same message and nothing consumes it, which is why it reads as a no-op
// there and must not be dropped here.
TPinballComponent* last_scoring_component = nullptr;

// 0x00435cf8 -- the per-mode scratch cell, shared by the mode controllers and the
// target banks. Every mode means something different by it (ships sunk, targets
// bailed, Peg Leg's payout step, Bermuda's progressive step, Sword Fight's
// completed-group bitfield), which is safe only because the mode start zeroes it.
// pirates_part1.cpp documents which bits each mechanic owns.
//
// The address was recorded here as 0x435d30 and that was wrong: 0x435d30 is a
// separate one-bit flag. The `& 0x70` / `& 0xf` / `== 10` / glyph-index uses in the
// decompile all sit on 0x435cf8, which is also what pirates_part1.cpp's own
// declaration says. pirates_modes.cpp used to define a second cell for the same
// address under the name ModeScratch; they are one object and this is it.
int mode_scratch_counter = 0;

namespace
{
// The 0x4d notify, factored out of all three wrappers.
//
// It goes through the rules dispatcher, not the component's own Message: all three
// wrappers `push 0x4d; call 0x423080`, and 0x423080 is the mode machine followed by
// the component's ControlFunc. That is the only thing that reaches the combo arms of
// a_ramp8, a_ramp5, s_trip3 and s_trip8, which is how a combo breaks.
void NoteScoringComponent(TPinballComponent* caller)
{
	if (last_scoring_component == caller)
		return;
	if (last_scoring_component)
		control::handler(static_cast<MessageCode>(0x4d), last_scoring_component);
	last_scoring_component = caller;
}
} // namespace

// 0x004232c0 -- score this component's scoring[index].
//
// Returns the amount actually credited, after the multiplier and the bonus/jackpot
// flags, because callers chain it: every "...\n%ld" display formats this return
// rather than the nominal score, and several announcements use it as their dwell.
int AddScoreFor(TPinballComponent* caller, int index)
{
	NoteScoringComponent(caller);
	return TableG->AddScore(caller->get_scoring(index));
}

// 0x00423310 -- the same with a literal score. Multiplied: its body is only the
// tracker update plus AddScore, so mode start bonuses, Mutiny's per-bumper award
// and the keg magnets all scale with the multiplier.
int AddScoreAt(TPinballComponent* caller, int score)
{
	NoteScoringComponent(caller);
	return TableG->AddScore(score);
}

// 0x00423350 -- the unmultiplied one, for scores that must land at face value.
//
// Full Tilt has no score_multipliers array: AddScore is `field * score + ScoreAdded`,
// so ScoreMultiplier holds the multiplier itself and 1 is its neutral value. Upstream
// forces 0 because in 3D Pinball the field is an index.
int AddScoreUnmultiplied(TPinballComponent* caller, int score)
{
	const auto bonus = TableG->BonusScoreFlag;
	const auto jackpot = TableG->JackpotScoreFlag;
	const auto mult = TableG->ScoreMultiplier;

	TableG->BonusScoreFlag = false;
	TableG->JackpotScoreFlag = false;
	TableG->ScoreMultiplier = 1;

	NoteScoringComponent(caller);
	const int credited = TableG->AddScore(score);

	TableG->BonusScoreFlag = bonus;
	TableG->JackpotScoreFlag = jackpot;
	TableG->ScoreMultiplier = mult;
	return credited;
}

// ---------------------------------------------------------------------------
// The rest of the shared .data cells
//
// Each is declared `extern` by pirates_part2.cpp and defined nowhere; the comments
// there carry the analysis, so these are the definitions only.
// ---------------------------------------------------------------------------

// DAT_0043aba8 -- non-zero once the game-over sequence has begun.
int game_over_phase = 0;

// DAT_0043aba0 -- cleared at every ball serve and at the end-of-game lockdown, and
// read by nothing in either corpus.
int unknown_43aba0 = 0;

// DAT_0043abb0 -- Peg Leg's Loot's teardown sets it; the next drain blanks the coin
// and special-treasure groups and clears it.
int collection_cycle_reset = 0;

// ---------------------------------------------------------------------------
// Glyph tokens
//
// Read out of PIRATES.EXE's .rdata. Single characters in the table's symbol font,
// stored as fixed-stride runs, which is where the odd strides in the comments at
// the declaration sites come from. They draw through TTextBox::DisplayGlyph, in
// the DAT's own symbol font -- see docs/engine-changes.md section 10.
// ---------------------------------------------------------------------------

// 0x43ab28, stride 9: the run for the n-th bailed Storm target.
const char* StormGlyphs[9] = {
	"", "K", "KK", "KKK", "KKKK", "KKKKK", "KKKKKK", "KKKKKKK", "KKKKKKKK",
};

// 0x43ab16, stride 2: Peg Leg's Loot's payout steps.
const char* PegLegGlyphs[9] = {"", "L", "M", "N", "O", "P", "Q", "R", "S"};

// 0x43aa58, stride 8: the skill-shot ladder, one token per lane.
const char* SkillShotGlyphs[9] = {
	"$&(),.2", "\"$&'*,.", "0\"$%(*,", "20\"#&(*", ".20!$&(",
	",.2/\"$&", "*,.10\"$", "(*,-20\"", "&(*+.20",
};

// 0x43aaa0, stride 4, indexed by the three-bit tavern window mask -- which is why
// the display shows which windows are collected. The 1996 tokens are three symbol
// characters each; the ASCII they land on here is "333".."444".
extern const char TavernWindowGlyphs[8][4];
const char TavernWindowGlyphs[8][4] = {
	"333", "334", "343", "344", "433", "434", "443", "444",
};

// 0x43aac0 -- the W-I-N-D-S token, entry 8 of the same run.
extern const char WindsGlyph[4];
const char WindsGlyph[4] = "5";

// ---------------------------------------------------------------------------
// Engine wrappers
// ---------------------------------------------------------------------------

// 0x409730 with its second argument 0, which is what pb::get_rc_string already is.
const char* GetRcString(int id)
{
	return pb::get_rc_string(static_cast<Msg>(id));
}

// 0x41de40 with font 0 and font 1. Upstream's TTextBox has one font.
void Display(TTextBox* box, const char* text, float time)
{
	if (box && text)
		box->Display(text, time);
}

void DisplayGlyph(TTextBox* box, const char* glyph, float time)
{
	if (box && glyph)
		box->DisplayGlyph(glyph, time);
}

// 0x41d930
void ClearText(TTextBox* box)
{
	if (box)
		box->Clear();
}

// `*(int*)(info_text_box + 0x5e) != 0`. The dispatcher re-spells the text box's timer
// expiry as a mode tick, so this gates that.
//
// The field is CurrentMessage, not Timer -- settled against Full Tilt's own TTextBox.
// TTextBox::Display is CADET.EXE 0x0041de40 (type 1033, vtable 0x00435858, Message at
// 0x0041e110) and its first test is `cmp DWORD PTR [ebx+0x5e],0` at 0x0041de55, exactly
// where upstream's TTextBox::DisplayInternal tests `CurrentMessage`; it then dereferences
// the neighbouring [ebx+0x62] as PreviousMessage->Text ([[ebx+0x62]+4]) and
// PreviousMessage->Glyph ([[ebx+0x62]+0x14]). So +0x5e = CurrentMessage and
// +0x62 = PreviousMessage, and this predicate is "the queue is not empty", which is a
// different thing from "a timer is pending" whenever a message is drawn with Timer == 0.
bool AnnouncementPending(TTextBox* box)
{
	return box && box->CurrentMessage != nullptr;
}

// vtable +0x18. The original returns the clip length; no caller here uses it.
void Play(TSound* sound)
{
	if (sound)
		sound->Play(nullptr, "pirates");
}

// Skulduggery always passes the value explicitly, unlike Dragon's lost x87
// argument, so this one is the plain sprintf it looks like.
void FormatWithValue(char* out, const char* format, int value)
{
	if (!out)
		return;
	if (!format)
	{
		*out = '\0';
		return;
	}
	if (std::strchr(format, '%'))
		std::snprintf(out, 64, format, static_cast<long>(value));
	else
		std::snprintf(out, 64, "%s", format);
}

// 0x410ff0 -- did any player's score make the high-score table? It walks the
// players and asks get_score_position of each, which is upstream's
// pb::chk_highscore; the game-over sequence checks it before announcing one.
bool AnyHighScore()
{
	return pb::chk_highscore();
}

// 0x42dd0b -- the C runtime's rand() and the open-coded `(rand() * n) >> 15`. The
// draw comes from the process-wide EngineRand() (pch.h) the engine's random calls also
// use, as in PIRATES.EXE where both call 0x42dd0b on one seed.
int Rnd(int n)
{
	const int r = EngineRand();
	return n > 0 ? (r * n) >> 15 : 0;
}

// 0x423650 -- the jackpot accrual every broadcast feeds, with its clamp.
void AddToJackpot(int amount)
{
	auto total = TableG->JackpotScore + amount;
	TableG->JackpotScore = total > 10000000 ? 10000000 : total;
}

} // namespace pirates
