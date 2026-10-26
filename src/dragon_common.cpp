// dragon_common.cpp -- Dragon's Keep's shared state, glyph tables and the thin
// wrappers its slices call into the engine through.
//
// The counterpart of pirates_common.cpp, and it exists for the same reason: each
// slice of the rules was written against the others' interfaces and declared what
// it needed as a prototype. These are the ones no slice defined. Everything here
// is either a .data cell that must have exactly one instance, a table of glyph
// tokens read out of DRAGON.EXE, or a one-line forward to an engine call.
//
// It is deliberately not where any *rule* lives. A routine with a behaviour of its
// own belongs with the mechanic it serves.

#include "pch.h"
#include "control.h"

#include <cstdio>
#include <cstring>

#include "pb.h"
#include "table_rules.h"
#include "TBlocker.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace dragon
{
extern TPinballTable*& TableG;

// ---------------------------------------------------------------------------
// Shared .data cells
// ---------------------------------------------------------------------------

// 0x434d08 / 0x434d24 -- the two per-ball counters the drain bonus reads back:
// collisions with popup/solo targets, and spells cast. dragon_part4.cpp's plunger
// resets both; dragon_missions.cpp increments them. They were declared twice under
// two name pairs (TargetsHitThisBall/dropped_target_count and
// SpellsCastThisBall/spell_cast_count) and defined nowhere, which would have been
// two cells for one original had either been defined.
int TargetsHitThisBall = 0;
int SpellsCastThisBall = 0;

// _DAT_00439b04 -- written by the plunger and by the table's tilt message, read
// nowhere in the decompiled set. Kept so the write has a target; see the note in
// dragon_part4.cpp.
int unknown_439b04 = 0;

// DAT_0043a99c -- the unidentified component whose +0x7e float both fire lizard
// sinks read as their flash duration. Which component it is has not been settled
// (see dragon_part3.cpp), so there is no tag to alias and it stays null; the one
// reader is written to tolerate that and fall back to the field's own -1 default.
TBlocker* unknown_a99c = nullptr;

// ---------------------------------------------------------------------------
// Glyph tokens
// ---------------------------------------------------------------------------
//
// Read out of DRAGON.EXE's .rdata at the addresses below. They are one-character
// tokens in the table's symbol font, stored as a run of 2-byte strings, which is
// why consecutive entries are two bytes apart.
//
// The port has no symbol font -- see docs/engine-changes.md -- so DisplayGlyph
// renders them as ASCII and these characters appear literally on the playfield.

const char* GlyphSpell = "$";                                      // 0x439ab4
const char* GlyphWeapon[4] = {"%", "&", "'", "("};                 // 0x439ab8
const char* GlyphArmor[3] = {"+", "*", ")"};                       // 0x439ac0, Shield/Helm/Chain
const char* GlyphJackpot = "\"";                                   // 0x439a5c

// 0x439ac8 stepped by two, so frame 8 lands on the run's terminator and is blank.
// The animation is nine frames and the run is eight tokens long; that is the
// binary's own arithmetic, not a transcription slip.
const char* GlyphDragonDeath[9] = {",", "-", ".", "/", "0", "1", "2", "3", ""};

// 0x439a60 stepped by nine: EggGlyphs[n] is a run of n '#'.
const char* EggGlyphs[9] = {
	"", "#", "##", "###", "####", "#####", "######", "#######", "########",
};

// ---------------------------------------------------------------------------
// Engine wrappers
// ---------------------------------------------------------------------------

// 0x409730 -- the resource string loader. The original takes two arguments; the
// second is always 0 in the rules, which is what pb::get_rc_string already is.
const char* GetRcString(int id)
{
	return pb::get_rc_string(static_cast<Msg>(id));
}

// 0x41de40 with font 0 and font 1. Upstream's TTextBox has one font, so the glyph
// form differs from the text form only in what it is handed.
void Display(TTextBox* box, const char* text, float time)
{
	if (box && text)
		box->Display(text, time);
}

void DisplayGlyph(TTextBox* box, const char* glyph, float time)
{
	// UNVERIFIED rendering, not an unverified call: the token is right, the symbol
	// font it should be drawn in does not exist in the port yet.
	if (box && glyph)
		box->Display(glyph, time);
}

// 0x41d930
void ClearText(TTextBox* box)
{
	if (box)
		box->Clear();
}

// `*(int*)(info_text_box + 0x5e) != 0` -- the text box's own display timer, which
// every quest's restart branch tests before overwriting the display.
bool AnnouncementPending(TTextBox* box)
{
	return box && box->Timer != 0;
}

// vtable +0x18. The original returns the clip length; no caller in the rules uses it.
void Play(TSound* sound)
{
	if (sound)
		sound->Play(nullptr, "dragon");
}

// 0x42c76f -- a label (or format string) plus one numeric value, where the value is
// the argument every decompile of every caller loses on the x87 stack.
//
// UNVERIFIED layout, and written to be safe rather than exact: the strings passed
// here are a mixture of bare "%ld" (3008) and bare labels with no specifier at all
// (3019), so a plain sprintf would read a missing argument for half of them. This
// substitutes the last credited score where there is a specifier and copies the
// label unchanged where there is not. Settled by disassembling 0x42c76f.
int LastCreditedScore = 0;

void FormatWithValue(char* out, const char* labelOrFormat)
{
	if (!out)
		return;
	if (!labelOrFormat)
	{
		*out = '\0';
		return;
	}
	if (std::strchr(labelOrFormat, '%'))
		std::snprintf(out, 64, labelOrFormat, static_cast<long>(LastCreditedScore));
	else
		std::snprintf(out, 64, "%s", labelOrFormat);
}

// 0x42c4cb -- the 15-bit LCG the table's "random in [0,n)" is built on. The 1996
// build links MSVC's rand(), whose constants these are; reproduced rather than
// calling the host rand() so a seed replays the same game.
int DragonRand15()
{
	static unsigned int seed = 1;
	seed = seed * 214013u + 2531011u;
	return static_cast<int>((seed >> 16) & 0x7fff);
}

// 0x423870 -- the super jackpot's accrual, with the clamp its sibling AddToJackpot
// (0x4238a0, in dragon_part1.cpp) applies at half the ceiling.
void AddToSuperJackpot(int score)
{
	auto total = TableG->SuperJackpotScore + score;
	TableG->SuperJackpotScore = total > 20000000 ? 20000000 : total;
}

// 0x423060 -- Dragon's control::handler: the mode machine first, then the
// component's own handler. Several award paths send a message through it by hand
// rather than through the message bus, which is what this name is for. The engine
// now runs the same order for this table, so it is that one call.
void SendToComponentAndHandler(MessageCode code, TPinballComponent* cmp)
{
	control::handler(code, cmp);
}

// A TBlocker's field at +0x7e read as an int, which is how the gargoyle rocks test
// it against -1. Kept to one line because the field itself is unsettled and another
// reader disagrees about its type; see dragon_part2.cpp and docs/engine-changes.md.
int BlockerUnknownP7e(TPinballComponent* blocker)
{
	auto* b = dynamic_cast<TBlocker*>(blocker);
	return b ? static_cast<int>(b->UnknownP7e) : -1;
}

} // namespace dragon
