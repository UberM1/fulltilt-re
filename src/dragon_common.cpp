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
#include "TSink.h"
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

// _DAT_00439b04 -- written by the plunger and by the table's tilt message and read
// nowhere at all: the only two instructions in the whole of DRAGON.EXE's .text that
// name the address are those two stores, and the four address bytes occur nowhere else
// in the file. Kept so the writes have a target; see the note in dragon_part4.cpp.
int unknown_439b04 = 0;

// 0x43a99c is a_sink9's Component. The tags are `{const char* Name; TPinballComponent*
// Component;}` pairs laid out consecutively in .data, so the tag at 0x43a998 holds a
// pointer to the string "a_sink9" at 0x43a990 and its Component at 0x43a99c -- read
// straight out of the file, with "a_sink10" starting at 0x43a9a0.
//
// It is therefore a TSink, and its +0x7e is TSink::TimerTime: CADET.EXE's
// WormHoleControl passes `*(float*)(sink + 0x7e)` wherever upstream's
// control::WormHoleControl passes `sink->TimerTime`, four times over. Dragon's fire
// lizard sinks reuse that one sink's 407 attribute as a shared flash duration.
extern component_tag<TPinballComponent> tag_a_sink9;

// ---------------------------------------------------------------------------
// Glyph tokens
// ---------------------------------------------------------------------------
//
// Read out of DRAGON.EXE's .rdata at the addresses below. They are one-character
// tokens in the table's symbol font, stored as a run of 2-byte strings, which is
// why consecutive entries are two bytes apart.
//
// They are drawn in the table's own symbol font, which the port now loads: it is
// the "font2" group of DRAGON.DAT, 20 glyph bitmaps starting at the space, so the
// characters below index it as 'char - 32'. See score::glyph_fontp.

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

// 0x41de40 with font 0 and font 1 -- the original's fourth argument selects which.
// The port's TTextBox now carries the same distinction, so the two forms differ in
// the font as well as in what they are handed.
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

// `*(int*)(info_text_box + 0x5e) != 0` -- CurrentMessage, not the timer. TTextBox's
// Display at 0x41de40 opens with `if ([this+0x5e]) { strcmp(text, [[this+0x62]+4]) ...
// fld [[this+0x5e]+8] }`, which is upstream's `if (CurrentMessage && !strcmp(text,
// PreviousMessage->Text))` followed by PreviousMessage->Refresh(time) reading the
// message's own Time at +8. So +0x5e is CurrentMessage and +0x62 PreviousMessage, and
// the predicate is "the box still has a message queued", which is what every quest's
// restart branch tests before overwriting the display.
bool AnnouncementPending(TTextBox* box)
{
	return box && box->CurrentMessage != nullptr;
}

// vtable +0x18. The original returns the clip length; no caller in the rules uses it.
void Play(TSound* sound)
{
	if (sound)
		sound->Play(nullptr, "dragon");
}

// 0x42c76f is plain variadic `sprintf`, not a bespoke formatter: its prologue builds
// MSVC's fake in-memory FILE (_flag = _IOWRT|_IOSTRG = 0x42, _ptr = _base = the
// buffer, _cnt = INT_MAX), calls _output(&str, fmt, &varargs) at 0x42d84c and then
// writes the terminating NUL. Call sites across the binary pass between one and six
// varargs, and 0x422280 pushes an fstp'd QWORD for a float conversion, which only a
// variadic signature admits.
//
// Every caller in the rules range passes exactly one `long`, pushed before the buffer
// and format -- which is the ordinary cdecl vararg layout, and is why Ghidra, having
// mis-modelled these __stdcall frames, renders it as an unrelated earlier expression
// rather than losing it on the x87 stack. The value is the score AddScoreFor /
// AddScoreAt just credited (both return it in eax) or, in the quest countdowns, the
// remaining milliseconds divided by 1000.
//
// The earlier reading that half these strings carry no "%ld" came from
// analysis/dragon-strings.txt, whose one-line-per-id format silently truncates every
// string at its embedded newline. Read out of the resource directly, 3007 is
// "Fire Ridge\n%ld" and 3019 "Dragon Hoard\n%ld": label, newline, specifier, in all
// of them.
void FormatWithValue(char* out, size_t size, const char* labelOrFormat, int value)
{
	if (!out)
		return;
	if (!labelOrFormat)
	{
		*out = '\0';
		return;
	}
	std::snprintf(out, size, labelOrFormat, static_cast<long>(value));
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

// a_sink9's TimerTime -- its 407 attribute, which both fire lizard sinks borrow as a
// flash duration. Null-tolerant because a handler can run before every tag resolves.
float ASink9TimerTime()
{
	auto* sink = dynamic_cast<TSink*>(tag_a_sink9.Component);
	return sink ? sink->TimerTime : -1.0f;
}

// A Full Tilt moving part's +0x7e: the sprite frame it is currently showing, with -1
// meaning "retracted, no sprite". Its sibling +0x92 is the frame it is travelling
// towards, +0x96 the seconds per step and +0x9a the step timer; the step callback at
// 0x414f00 raises MotionDone (0x3d) only once +0x7e has reached +0x92. So the gargoyle
// rocks' `!= -1` guard reads "it has arrived at an extended frame, not back at the
// parked one" -- BlockerEnable aims at the last frame, BlockerDisable at -1.
//
// The port's TBlocker is upstream's two-state one, which spells the same distinction
// as SpriteSet(0) vs SpriteSet(-1) alongside ActiveFlag, so ActiveFlag is the faithful
// stand-in for "the frame index is not the parked sentinel".
int BlockerUnknownP7e(TPinballComponent* blocker)
{
	return blocker && blocker->ActiveFlag ? 0 : -1;
}

} // namespace dragon
