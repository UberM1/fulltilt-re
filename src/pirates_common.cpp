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

#include "pb.h"
#include "TPinballTable.h"
#include "TPinballComponent.h"

namespace pirates
{
extern TPinballTable* TableG;

// 0x00435d2c -- the component that last scored.
//
// Skulduggery uses this for more than bookkeeping. When a different component
// scores, the previous one is sent 0x4d, and the four lane handlers answer that by
// clearing their combo light group over 2.0 s. So this pointer is what makes combos
// breakable: score anywhere else and the island and orbit ladders reset. Dragon
// sends the same message and nothing consumes it, which is why it reads as a no-op
// there and must not be dropped here.
TPinballComponent* last_scoring_component = nullptr;

// 0x00435d30 -- scratch bits shared by the mode controllers and the target banks.
// Several unrelated mechanics pack their state into it; see the uses in
// pirates_part1.cpp, which document which bits each owns.
int mode_scratch_counter = 0;

namespace
{
// The 0x4d notify, factored out of all three wrappers.
void NoteScoringComponent(TPinballComponent* caller)
{
	if (last_scoring_component == caller)
		return;
	if (last_scoring_component)
		last_scoring_component->Message(static_cast<MessageCode>(0x4d), 0.0);
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

} // namespace pirates
