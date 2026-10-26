#pragma once

// A per-table rules interface, which upstream does not have.
//
// SpaceCadetPinball hardcodes Space Cadet throughout: `control` is a class of
// statics bound to one table's components, `pb::FullTiltMode` / `FullTiltDemoMode`
// are two booleans standing in for a table identity, and `TPinballTable` reaches
// for Space Cadet's own components by name. None of that extends to a third and
// fourth table.
//
// The engine itself does extend. `TPinballTable` already builds the playfield from
// `table_objects` in the DAT, so geometry, collision, sprites and physics are
// table-agnostic today. What has to become table-dependent is the rules: the
// component bindings, the score multipliers, the mode machine and the texts.
//
// This header is the seam. Each table supplies one `TableRules` and the engine
// stops knowing which table it is running.
//
// It lives in the engine tree rather than src/ because the engine includes it --
// pb.cpp, midi.cpp and table_rules.cpp all do -- and src/ already compiles with
// -I on this directory, so one copy serves both sides.

#include "control.h"

class TPinballTable;
class TPinballComponent;
enum class MessageCode;

enum class TableId
{
	SpaceCadet3DPB,  // PINBALL.DAT, the Windows release
	SpaceCadetFT,    // CADET.DAT, the Full Tilt release of the same table
	SpaceCadetDemo,  // DEMO.DAT
	DragonsKeep,     // DRAGON.DAT
	Skulduggery,     // PIRATES.DAT
};

struct TableRules
{
	TableId Id;
	const char* Name;        // window title and About text
	const char* DatFileName; // selects the data file, replacing pb::SelectDatFile's fixed list

	// Which era the DAT is from, and whether it is a demo cut of it. These are not
	// derivable from `Id` by anything but a switch, and the engine asks about them
	// constantly -- resolution count, SOUND/ subfolder, kicker Z attribute, plunger
	// and hole timings. `pb::FullTiltMode` / `pb::FullTiltDemoMode` are now views of
	// these two fields, which is what keeps ~25 existing call sites honest.
	bool FullTiltData;
	bool DemoData;

	// The bindings `control::make_links` walks. Space Cadet's are
	// score_components[88] / simple_components[145]; the bonus tables have their
	// own, recovered from the originals' .data.
	component_info* ScoreComponents;
	size_t ScoreComponentCount;
	component_tag_base** SimpleComponents;
	size_t SimpleComponentCount;

	// No per-table multiplier array is needed, and adding one would be wrong.
	//
	// Upstream models the multiplier as an index: `score_multipliers[5] =
	// {1,2,3,5,10}` with `ScoreMultiplier` holding 0..4, neutral at 0. That is a
	// 3D Pinball-ism. The Full Tilt builds hold the multiplier VALUE directly --
	// CADET.EXE's AddScore is `*(this+0x15e) * score + ScoreAdded` with no indexed
	// load, and its callers write 1, 2, 3, 5 and 10 into that field, neutral at 1.
	// Dragon's Keep writes 2, 3, 4 and 5 there, which is simply 2x..5x.
	//
	// So the fix is to make `TPinballTable::ScoreMultiplier` the value everywhere
	// and have the 3D Pinball path write values instead of indices. A per-table
	// array would reintroduce the indexing this table never had.

	// Where the table keeps its mode. Space Cadet uses the dummy light `lite198`,
	// Dragon's Keep the MessageField of its single TTextBox `info_text_box`. The
	// engine only needs to read and write it, not know which object it is.
	component_tag_base* ModeRegister;

	// The mode machine, as `control::handler` calls it: after the component's own
	// handler, with no mode argument. Space Cadet's is `control::MissionControl`
	// over 17 missions and 9 ranks; Dragon's Keep has 6 quests and no ranks at all,
	// so the rank hooks below are allowed to be null.
	//
	// The bonus tables split this in two -- a `(Mode, MessageCode, caller)` switch
	// plus a dispatcher that reads the mode register and calls it -- so the entry
	// point bound here is the dispatcher (`dragon::MissionHandler`,
	// `pirates::Handler`), not the switch.
	void (*ModeControl)(MessageCode code, TPinballComponent* caller);
	void (*ResetRules)(TPinballTable* table);
	void (*BallDrained)(TPinballTable* table);

	// Space Cadet 2026's HUD reads a rank; tables without one leave this null and
	// the HUD hides the section rather than showing an empty one.
	int (*CurrentRank)();
	int (*CurrentMode)();

	// midi.cpp loads TABA1..TABA3 for Space Cadet. Skulduggery ships TABB1..TABB6
	// and Dragon's Keep TABC1..TABC7, so the count is per table.
	//
	// A count of 0 means the prefix is the whole file name and there is no index:
	// 3D Pinball ships one track called PINBALL.MID, not PINBALL1.MID, so it cannot
	// be expressed as a one-entry indexed set.
	const char* MusicPrefix;
	int MusicTrackCount;

	// `options::TableKeyPrefix` already knows three tables; it needs five, and the
	// high score file has to be namespaced per table or they overwrite each other.
	const char* SettingsPrefix;
};

// The table in play. Replaces pb::FullTiltMode / pb::FullTiltDemoMode, which
// cannot express more than two tables between them.
extern const TableRules* CurrentTable;

const TableRules* FindTableRules(TableId id);
const TableRules* FindTableRulesForDat(const char* datFileName);

// The DAT names pb::SelectDatFile probes, in registry order. Space Cadet's three
// come first, so a directory holding several DATs still picks Space Cadet.
const char* const* TableDatSearchOrder(size_t& count);

// Message codes the Full Tilt binaries use and 3D Pinball does not, so upstream's
// enum has no name for them. Numbering here follows upstream's own scheme; these
// are new entries, not the originals' values. (The originals renumber everything
// piecewise relative to upstream -- TLight +0, TLightGroup/TSoloTarget/TBlocker +4,
// gate/kickout/sink +9, Control* +10, ControlCollision +11 -- but that only matters
// when reading the 1996 binaries, never at runtime here.)
enum class FtMessageCode
{
	QuestTeardown,   // original 0x51
	QuestAbort,      // original 0x52
	QuestRestart,    // original 0x53
	LastScorerChanged, // original 0x4d; no component consumes it, kept for fidelity
};
