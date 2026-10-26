// dragon_part4.cpp -- Dragon's Keep rules, part 4: the drain, the plunger, the kickouts
// and the quest hole.
//
// Seven handlers, in the shape of SpaceCadetPinball's control.cpp and continuing
// dragon_part1.cpp / dragon_part2.cpp. Sources: decomp/dragon/functions/*.c (Ghidra, every
// DAT_ annotated with its component name), decomp/dragon/asm/*.txt (objdump -- the listing
// wins wherever Ghidra's stack arguments disagree with it, and every stack argument below
// was checked against it), analysis/dragon-ball-handlers.md §4-§6 for the component
// identities, and analysis/dragon-quest-system.md for the mode register.
//
//   0x004245b0  s_plunger1  ball feed / per-ball reset      upstream PlungerControl
//   0x00424900  drain       ball drained                    upstream BallDrainControl
//   0x00426d00  a_kick1     outlane kicker (ball save)      upstream LeftKickerControl
//   0x00426d80  a_kout1     Armor Kickout                   upstream GravityWellKickoutControl
//   0x00426e40  a_kout2     Gargoyle Kickout                upstream BlackHoleKickoutControl
//   0x00426ee0  s_hole118   quest-staging hole              upstream LaunchRampHoleControl
//   0x00427ef0  a_kout3     Dragon Blood magnet             upstream JackpotLightControl
//
// Like the other parts this is a fragment: the component table and the score arrays live in
// control_dragon.cpp, and the per-table control interface the forward declarations below
// describe does not exist yet, so the file compiles in shape but does not link. Where part2
// declares one of these objects `extern TLight*` and part1 defines it as `TLight*&` bound to
// a tag, part1's form is the one followed here.
//
// Message codes are Full Tilt's, which are upstream's with a piecewise shift -- TLight +0,
// the group/target/blocker band +4, the gate/kickout/sink band +9, Control* +10,
// ControlCollision +11, the two mission codes +13, public codes >= 1016 +1 and >= 1021 +2.
// Upstream enum names are used for the meaning; the binary's raw value is in the comment
// wherever it is not already pinned in analysis/dragon-ball-handlers.md §1.3.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace dragon {

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

// Defined by part1; reused rather than redeclared.
extern TTextBox*& info_text_box;
extern TPinballTable*& TableG;
extern TLight*& lite11; // gargoyle target bank
extern TLight*& lite12;
extern TLight*& lite13;
extern TLight*& lite14;
extern TLight*& lite45; // rock-A "passage open" light
extern TLight*& lite46; // rock-B "passage open" light
extern TPinballComponent*& rockA;
extern TPinballComponent*& rockB;
extern TPinballComponent*& bump1; // owns the shared forest-bumper hit counter
extern TPinballComponent*& dvtrBA;
extern TPinballComponent*& dvtrBB;
extern TPinballComponent*& dvtrCA;
extern TPinballComponent*& dvtrCB;

// Tags this slice needs and no other part declares. Lights are typed TLight here for
// light_on(); the generated skeleton types every tag TPinballComponent.
extern component_tag<TLight> tag_v_lite33;
extern component_tag<TLight> tag_v_lite34;
extern component_tag<TLight> tag_v_lite35;
extern component_tag<TLight> tag_v_lite36;
extern component_tag<TLight> tag_v_lite37;
extern component_tag<TLight> tag_v_lite40;
extern component_tag<TLight> tag_v_lite47;
extern component_tag<TLight> tag_v_lite48;
extern component_tag<TLight> tag_v_lite49;
extern component_tag<TLight> tag_v_lite50;
extern component_tag<TLight> tag_v_lite67;
extern component_tag<TLight> tag_v_lite68;
extern component_tag<TLight> tag_v_lite69;
extern component_tag<TLight> tag_v_lite70;
extern component_tag<TLight> tag_v_lite125;
extern component_tag<TLight> tag_v_lite126;
extern component_tag<TLightGroup> tag_outlane_kicker_light_group;
extern component_tag<TLightGroup> tag_gargoyle_spot_light_group;
extern component_tag<TLightGroup> tag_spell_spot_light_group;
extern component_tag<TPinballComponent> tag_a_dvtraa;
extern component_tag<TPinballComponent> tag_a_dvtrab;
extern component_tag<TPinballComponent> tag_a_dvtrda;
extern component_tag<TPinballComponent> tag_a_dvtrdb;
extern component_tag<TSound> tag_tabc32wave;
component_tag<TSound> tag_tabc34wave = {"tabc34wave"};

// Scoring components: tags live in control_dragon.cpp.
extern component_tag<TPinballComponent> tag_drain;
extern component_tag<TPinballComponent> tag_s_plunger1;
extern component_tag<TPinballComponent> tag_a_kick1;
extern component_tag<TPinballComponent> tag_a_kout2;
extern component_tag<TPinballComponent> tag_a_kout3;
extern component_tag<TPinballComponent> tag_a_sink16;
extern component_tag<TPinballComponent> tag_a_sink17;
extern component_tag<TPinballComponent> tag_a_sink18;
extern component_tag<TPinballComponent> tag_a_flip1;
extern component_tag<TPinballComponent> tag_a_flip2;
extern component_tag<TPinballComponent> tag_a_flip3;
extern component_tag<TPinballComponent> tag_a_center_post;
extern component_tag<TPinballComponent> tag_a_bloce;

TLight*& lite33 = tag_v_lite33.Component; // the four well lights, cleared per ball
TLight*& lite34 = tag_v_lite34.Component;
TLight*& lite35 = tag_v_lite35.Component;
TLight*& lite36 = tag_v_lite36.Component;
TLight*& lite37 = tag_v_lite37.Component;  // Shield armor; freezes the kicker charge
TLight*& lite40 = tag_v_lite40.Component;  // "Shoot Again" / ball-save lamp
TLight*& lite47 = tag_v_lite47.Component;  // diverter-A routing indicator
TLight*& lite48 = tag_v_lite48.Component;  // tower lock 1 (a_sink18) holds a ball
TLight*& lite49 = tag_v_lite49.Component;  // tower lock 2 (a_sink17)
TLight*& lite50 = tag_v_lite50.Component;  // tower lock 3 (a_sink16)
TLight*& lite67 = tag_v_lite67.Component;  // dragon-ramp combo light
TLight*& lite68 = tag_v_lite68.Component;  // dragon-chute combo light, MessageField = ladder
TLight*& lite69 = tag_v_lite69.Component;  // fire-ridge combo light, MessageField = ladder
TLight*& lite70 = tag_v_lite70.Component;  // tower-ramp combo light
TLight*& lite125 = tag_v_lite125.Component; // dragon-chute route indicator
TLight*& lite126 = tag_v_lite126.Component; // dragon-ramp route indicator
TLightGroup*& outlane_kicker_lights = tag_outlane_kicker_light_group.Component;
TLightGroup*& gargoyle_spot_lights = tag_gargoyle_spot_light_group.Component;
TLightGroup*& spell_spot_lights = tag_spell_spot_light_group.Component;
TPinballComponent*& dvtrAA = tag_a_dvtraa.Component;
TPinballComponent*& dvtrAB = tag_a_dvtrab.Component;
TPinballComponent*& dvtrDA = tag_a_dvtrda.Component;
TPinballComponent*& dvtrDB = tag_a_dvtrdb.Component;
TPinballComponent*& drain = tag_drain.Component;
TPinballComponent*& plunger = tag_s_plunger1.Component;
TPinballComponent*& a_kick1 = tag_a_kick1.Component;
TPinballComponent*& a_kout2 = tag_a_kout2.Component;
TPinballComponent*& a_kout3 = tag_a_kout3.Component;
TPinballComponent*& a_sink16 = tag_a_sink16.Component;
TPinballComponent*& a_sink17 = tag_a_sink17.Component;
TPinballComponent*& a_sink18 = tag_a_sink18.Component;
TPinballComponent*& a_flip1 = tag_a_flip1.Component;
TPinballComponent*& a_flip2 = tag_a_flip2.Component;
TPinballComponent*& a_flip3 = tag_a_flip3.Component;
TPinballComponent*& a_center_post = tag_a_center_post.Component;
TPinballComponent*& a_bloce = tag_a_bloce.Component;
TSound*& tabc32wave = tag_tabc32wave.Component;
TSound*& tabc34wave = tag_tabc34wave.Component;

// ---------------------------------------------------------------------------
// Table-local message codes, modes and string ids
// ---------------------------------------------------------------------------

namespace FtMessageCode {
// Broadcast whenever the mode register changes; part1's name is kept. Arithmetically this
// is upstream's ControlMissionComplete (binary 0x4f = 66 + 13), but every Dragon mode
// controller treats its 0x4f branch as "my mode just started" -- the sender has already
// written the new mode into info_text_box->MessageField before broadcasting -- so the
// behavioural name is the useful one. analysis/dragon-ball-handlers.md §1.3 lists the
// arithmetic name instead; the two documents describe the same code.
constexpr auto ModeStarted = static_cast<MessageCode>(0x4f);

// Upstream's ControlMissionStarted (0x50 = 67 + 13). Sent by the plunger when a ball is
// physically fed, which is exactly where upstream's PlungerControl sends it, so the
// mapping is structural and not just arithmetic.
constexpr auto MissionStarted = static_cast<MessageCode>(0x50);

// FT-only, no upstream counterpart. Emitted only by the drain handler; every mode
// controller's branch for it forces mode 2, cancels mode_timer and falls into the quest
// teardown, which is why part2 names the same code QuestAbort. Named here from the
// sender's side.
constexpr auto BallDrained = static_cast<MessageCode>(0x53);

// FT-only. Sent to a THole with a float; the only two senders (this file's s_hole118 and
// the sink-side quest staging at 0x004241c0) both mean "keep the ball, release it after
// this long". UNVERIFIED: disassembling FT's THole::Message would settle whether the float
// is a hold time or a release delay, and whether 0.0 means "hold forever".
constexpr auto THoleHoldBall = static_cast<MessageCode>(0x43);
}

// info_text_box->MessageField is Dragon's mode register -- the exact analogue of Space
// Cadet's lite198->MessageField. Only the values this slice reads or writes are named;
// part2's Quest namespace has the full table.
constexpr int ModeNone = 1;             // locked balls being released, no mode
constexpr int ModeNormal = 2;
constexpr int ModeRescueMaiden = 6;
constexpr int ModeDragonPass = 7;
constexpr int ModeSlayDragon = 9;
constexpr int ModeSkillShot = 10;       // the per-ball quest offer; s_hole118 parks here
constexpr int ModeEndOfBall = 11;       // 0xb, the drain bonus count-up
constexpr int ModeEggExchange = 12;     // 0xc
constexpr int ModeTowerBarrage = 13;    // 0xd
constexpr int ModeGargoyleGaggle = 14;  // 0xe

// Raw DRAGON.EXE resource ids; Dragon has its own string table.
inline Msg DragonMsg(int rcId) { return static_cast<Msg>(rcId); }

constexpr int Str_BallSaved = 3009; // 0xbc1

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------

// part1 (0x004230a0 / 0x0042c020 / 0x004238d0 / 0x00423e90).
void AddScoreFor(TPinballComponent* caller, int index);
void MissionHandler(MessageCode code, TPinballComponent* cmp);
void SetSwordStage(int stage);
int SelectNextQuest();

// part2 (0x00425910 / 0x00425a10). The plunger calls these two handlers directly rather
// than going through control::handler, exactly as s_oneway2 calls the Shoot Again lamp's.
void CenterPostControl(MessageCode code, TPinballComponent* caller);
void RightOutlaneBlockerControl(MessageCode code, TPinballComponent* caller);

void ClearSpells();        // 0x00423780, spell lights off + a_kout3 reset + moat blocker back
void AwardArmor();        // 0x004234b0, random among the un-owned pieces, not in order
void CheckTowerLocks();      // 0x00423be0
void AwardDragonEggs(int n);  // 0x004232b0
int CollectJackpot(float unused, bool showText); // 0x00424490
void DisarmDragon(bool slain);                // 0x00424050, the dragon's head and jaw

// part1 keeps LastScoringComp (DAT_00434d20) file-static, so the plunger's reset of it
// needs a setter from that slice.
void ClearLastScoringComponent(); // the DAT_00434d20 = 0 inside 0x004245b0

// ---------------------------------------------------------------------------
// Cross-slice state
// ---------------------------------------------------------------------------

// DAT_00434d2c -- the ball-save countdown, 3 (untimed hold) -> 2 (10 s solid) -> 1 (5 s
// flashing) -> 0, stepped by v_lite40's handler at 0x00424870. part2 declares this extern;
// this slice owns it because the plunger arms it and the drain clears it.
int shoot_again_flag = 0;

// DAT_00439b1c -- 0 while a ball is in play, 1 when the last ball drained untilted and 2
// when it drained tilted. Set here and cleared by mode 0xb's controller (0x0042b7c0) and by
// the table's new-game message (0x00424260 case 0x3f6).
// The 1-vs-2 distinction is consumed by 0x0042b7c0's mode-start branch, which skips the
// whole bonus count-up (`step = 8`) when the table is tilt-locked and the phase is not 1 --
// so phase 1 still pays the bonus for a clean last ball and phase 2 does not.
int game_over_phase = 0;

// DAT_0043bb6c -- which plunger action the drain's own timer should take: 1 = none,
// 3 = relaunch the saved ball, 0 = other balls are still live. See the note on value 2 in
// BallDrainControl.
static int drain_phase = 0;

// Owned by the quest machinery and the scoring prologue; cleared on every ball feed.
extern bool BallDraining;   // DAT_00439b14, makes SelectNextQuest return -1
extern int TargetsHitThisBall;   // DAT_00434d08, the bonus's "Targets dropped" line
extern int SpellsCastThisBall;       // DAT_00434d24

// _DAT_00439b04 -- written only here and by the table's tilt message (0x00424260 case
// 0x3f3), and read nowhere in the decompiled set, so it is dead as far as the rules go.
// Ghidra flags the symbol as overlapping a smaller one at the same address, so it may be a
// field of some larger .data object rather than a variable of its own.
// UNVERIFIED: a full cross-reference over the non-rules code (UI, demo, high-score) would
// say whether anything outside control reads it.
extern int unknown_439b04;

// ---------------------------------------------------------------------------
// The plunger -- the per-ball reset
// ---------------------------------------------------------------------------

// 0x004245b0 -- s_plunger1 (skeleton: s_plunger1Control)
//
// Structurally upstream's PlungerControl: PlungerFeedBall just tells the current mode
// controller to deploy, and PlungerStartFeedTimer carries the whole per-ball reset. Where
// upstream resets the table's lights in BallDrainControl and only the skill-shot lights
// here, Dragon has moved everything per-ball into this handler -- which is why its drain is
// a quarter the size of upstream's.
//
// The float the drain and mode 0xb pass with PlungerStartFeedTimer (0.96 s) is upstream's
// hardcoded timer::set(0.95999998f) in TPlunger; FT made it a message argument.
void PlungerControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::PlungerFeedBall) // binary 0x3f8
	{
		MissionHandler(FtMessageCode::MissionStarted, nullptr);
		return;
	}
	if (code != MessageCode::PlungerStartFeedTimer) // binary 0x3f9
		return;

	BallDraining = false;
	unknown_439b04 = 0;

	// Spells, the dragon's head and the tower locks are all per-ball.
	ClearSpells();
	CheckTowerLocks();
	DisarmDragon(true);

	// Both armor blockers retract. The two handlers are called directly, so the mode
	// controller does not see the TBlockerDisable -- the same shortcut part2 documents on
	// s_oneway2. 0x37/0x38 are TBlockerDisable/TBlockerEnable (upstream 51/52, +4 band).
	CenterPostControl(MessageCode::TBlockerDisable, a_center_post);
	RightOutlaneBlockerControl(MessageCode::TBlockerDisable, a_bloce);

	// The outlane kicker starts every ball live with one charge: the group is cleared and
	// then advanced once. Shield is cancelled (its 60 s clock lives in v_lite37's own
	// MessageField phase counter, so that has to be rewound too).
	a_kick1->ActiveFlag = 1;
	lite37->MessageField = 0;
	lite37->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	outlane_kicker_lights->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	outlane_kicker_lights->Message(MessageCode::TLightGroupOffsetAnimationForward, 0.0); // 0x24

	lite33->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite34->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite35->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite36->Message(MessageCode::TLightResetAndTurnOff, 0.0);

	// Dragon's neutral multiplier is 1, not upstream's 0; see part1's DragonSpecialAddScore.
	TableG->ScoreMultiplier = 1;

	// Rescue Maiden and Tower Barrage own the A and D diverters for the duration of the
	// mode, so a ball fed while either is running must not re-route them. The B and C pairs
	// (the gargoyle pass) are reset unconditionally.
	const int mode = info_text_box->MessageField;
	const bool modeOwnsDiverters = mode == ModeRescueMaiden || mode == ModeTowerBarrage;

	if (!modeOwnsDiverters)
	{
		lite47->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		dvtrAA->Message(MessageCode::TGateEnable, 0.0);
		dvtrAB->Message(MessageCode::TGateDisable, 0.0);
	}

	dvtrBA->Message(MessageCode::TGateDisable, 0.0);
	dvtrBB->Message(MessageCode::TGateEnable, 0.0);
	dvtrCA->Message(MessageCode::TGateDisable, 0.0);
	dvtrCB->Message(MessageCode::TGateEnable, 0.0);

	if (!modeOwnsDiverters)
	{
		// Default routing is the dragon ramp, not the chute: v_lite126 is flashed on and
		// v_lite125 killed, with the D pair set to match.
		lite125->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite126->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		dvtrDA->Message(MessageCode::TGateEnable, 0.0);
		dvtrDB->Message(MessageCode::TGateDisable, 0.0);
	}

	// Stage 3 is the first sword target; the rocks go back out and close the pass.
	SetSwordStage(3);
	lite46->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	rockB->Message(MessageCode::TBlockerEnable, 0.0);
	lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	rockA->Message(MessageCode::TBlockerEnable, 0.0);
	gargoyle_spot_lights->Message(MessageCode::TLightResetAndTurnOff, 0.0);

	// The four combination lights, and with them the chute and fire-ridge score ladders
	// that are indexed by their MessageFields.
	lite67->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite70->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite68->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite68->MessageField = 0;
	lite69->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite69->MessageField = 0;
	spell_spot_lights->Message(MessageCode::TLightResetAndTurnOff, 0.0);

	bump1->MessageField = 0; // the shared forest-bumper weapon counter
	TargetsHitThisBall = 0;
	SpellsCastThisBall = 0;
	ClearLastScoringComponent();

	// Every ball opens with a quest pre-selected and the table in mode 10, which is what
	// makes s_hole118 park the ball instead of capturing it. SelectNextQuest returns 9 when
	// all five quests are done, and in that case it has already switched the table to Slay
	// Dragon and broadcast ModeStarted itself -- hence the guard, which exists to avoid
	// stamping mode 10 over mode 9.
	if (SelectNextQuest() != ModeSlayDragon)
	{
		info_text_box->MessageField = ModeSkillShot;
		MissionHandler(FtMessageCode::ModeStarted, nullptr);
	}

	// The ball save: lamp on solid and untimed, countdown parked at phase 3 until the ball
	// leaves the launch lane (part2's LaunchLaneOneWayControl steps it).
	lite40->Message(MessageCode::TLightResetAndTurnOn, 0.0);
	shoot_again_flag = 3;
}

// ---------------------------------------------------------------------------
// The drain
// ---------------------------------------------------------------------------

// 0x00424900 -- drain (skeleton: drainControl)
//
// A quarter the size of upstream's BallDrainControl, because Dragon split the job three
// ways. This handler decides only: was the ball saved, is this the end of the game, and are
// there still balls in play. The bonus count-up, the next-ball serve and the extra-ball
// payout all live in mode 0xb's controller (0x0042b7c0), and the GameOver message plus the
// high-score check live in mode 0's (0x0042bf60, which is a line-for-line match for the
// game-over half of upstream's BallDrainControl). The chain is
// drain -> mode 0xb -> mode 0, each step handing over by writing the mode register and
// broadcasting ModeStarted.
//
// Ordering within the collision path, which matters: tilt first (it cancels the save),
// then the save test (which returns without touching the mode, the ball count or the
// quest), then the ball-drained broadcast, then the end-of-game detection, then the
// multiball test, and only last the handover to mode 0xb.
void BallDrainControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlTimerExpired)
	{
		// Once the game is over the drain's timer must not feed another ball; mode 0xb
		// clears game_over_phase when it is done with the end-of-game sequence.
		if (game_over_phase != 0)
			return;

		// 0.96 s is upstream's TPlunger feed delay, passed as an argument in FT.
		if (drain_phase == 2)
			plunger->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
		else if (drain_phase == 3)
			plunger->Message(MessageCode::PlungerRelaunchBall, 0.96f);

		// drain_phase == 2 is the plain "serve the next ball" case and is unreachable:
		// DAT_0043bb6c is written only by this function, and only with 0, 1 and 3. Mode
		// 0xb does not set it either -- it messages s_plunger1 with PlungerStartFeedTimer
		// directly, on both its extra-ball and its next-ball paths. So the branch is dead
		// code, not a hole in the analysis; analysis/dragon-ball-handlers.md §6 guesses
		// that 0x0042b7c0 sets it, and reading that controller shows it does not.
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;

	drain_phase = 1;

	// Tilting kills the ball save outright, lamp and countdown together, so the light test
	// below then fails and the ball is lost even inside the 15 s window.
	if (TableG->TiltLockFlag)
	{
		lite40->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		shoot_again_flag = 0;
	}

	if (lite40->light_on())
	{
		drain_phase = 3;

		// Reset, not SetTiltLock: Skulduggery's drain sends 0x402 to its own drain at
		// the equivalent point, and 0x402 is upstream's Reset under the public-code
		// shift. That kills the drain's pending timer and clears BallInDrainFlag, which
		// is exactly what this path needs -- the drained ball must not count.
		drain->Message(MessageCode::Reset, 0.0);

		// TPinballTable+0x6a, set only here. UNVERIFIED, but almost certainly upstream's
		// BallInDrainFlag: upstream sets it in TDrain::Collision and clears it in
		// TPlunger's PlungerFeedBall, which is exactly the lifetime of this assignment.
		// Anchoring any field between +0x6a and PlayerCount at +0x102 would confirm it.
		TableG->BallInDrainFlag = 1;

		// The original jumps back to the dispatch head with code = ControlTimerExpired
		// rather than calling the plunger itself, so the relaunch goes through the single
		// drain_phase switch above. No ball-drained broadcast, no ball-count change and no
		// mode change: a saved ball leaves the quest, the bonus and the player untouched.
		BallDrainControl(MessageCode::ControlTimerExpired, caller);
		return;
	}

	// Every mode controller's handler for this forces mode 2 and tears the quest down.
	MissionHandler(FtMessageCode::BallDrained, caller);

	// Last ball of the last player with nothing held back: start the game-over sequence.
	// BallCount == 1 because mode 0xb is what decrements it.
	if (TableG->MultiballCount == 0 && TableG->ExtraBalls == 0 && TableG->BallCount == 1
		&& TableG->CurrentPlayer - TableG->PlayerCount == -1 && game_over_phase == 0)
	{
		// 1 = ended cleanly, 2 = ended on a tilt. Computed before TiltLockFlag is forced.
		game_over_phase = TableG->TiltLockFlag ? 2 : 1;
		TableG->TiltLockFlag = 1;
		a_flip1->Message(MessageCode::SetTiltLock, 0.0);
		a_flip3->Message(MessageCode::SetTiltLock, 0.0);
		a_flip2->Message(MessageCode::SetTiltLock, 0.0);

		// Any balls still locked in the towers are released so the player sees them drain,
		// highest tower (300,000) first. Mode 1 is "no mode" -- it exists so the released
		// balls do not restart anything on their way down.
		struct TowerLock
		{
			TLight* light;
			TPinballComponent* sink;
		};
		const TowerLock locks[] = {{lite50, a_sink16}, {lite49, a_sink17}, {lite48, a_sink18}};
		for (const auto& lock : locks)
		{
			// The original tests LightOnFlag alone here, not the full light_on() triple it
			// uses on v_lite40 and v_lite37 -- a lock light is only ever set solid-on, so
			// the flasher flags cannot be the ones carrying the state.
			if (!lock.light->LightOnFlag)
				continue;
			info_text_box->MessageField = ModeNone;
			TableG->MultiballCount++;
			lock.sink->Message(MessageCode::TSinkResetTimer, 0.0); // 0.0 = eject now
			lock.light->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		}
	}

	// Other balls are still live, so this drain is just a lost multiball and nothing else
	// happens. The game-over test above has already pushed MultiballCount back above zero
	// if it released locked balls, which is why game_over_phase has to be consulted too --
	// without it the released balls would suppress the end-of-game handover.
	if (TableG->MultiballCount != 0 && game_over_phase == 0)
	{
		drain_phase = 0;
		return;
	}

	// Hand over to the bonus count-up. Mode 0xb is idempotent-guarded, and mode 0 (game
	// already over, waiting for a new game) must not be disturbed.
	if (info_text_box->MessageField == ModeEndOfBall || info_text_box->MessageField == 0)
		return;
	info_text_box->MessageField = ModeEndOfBall;
	MissionHandler(FtMessageCode::ModeStarted, nullptr);
}

// ---------------------------------------------------------------------------
// The outlane kicker and the three kickouts
// ---------------------------------------------------------------------------

// 0x00426d00 -- a_kick1 (skeleton: a_kick1Control)
//
// Upstream's LeftKickerControl shape -- one branch, ControlTimerExpired -- with the ball
// save on top. The kicker's charge is the on-count of outlane_kicker_light_group: firing
// consumes one light (0x25, OffsetAnimationBackward, upstream 33) and the kicker
// deactivates itself when the last one goes out, so a_roll7 and the two return-lane
// rollovers in part2 are the producers for this consumer.
//
// Shield freezes that: while v_lite37 is lit the kicker fires without spending a light,
// which is the manual's "the Shield makes the kicker permanent" for as long as the armor
// lasts. The announcement is outside the branch, so a Shield-powered save still says it.
void OutlaneKickerControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlTimerExpired)
		return;

	if (!lite37->light_on())
	{
		outlane_kicker_lights->Message(MessageCode::TLightGroupOffsetAnimationBackward, 0.0);
		if (!outlane_kicker_lights->Message(MessageCode::TLightGroupGetOnCount, 0.0)) // 0x29
			caller->ActiveFlag = 0;
	}

	info_text_box->Display(pb::get_rc_string(DragonMsg(Str_BallSaved)), 2.0f);
}

// 0x00426d80 -- a_kout1 (skeleton: a_kout1Control)
//
// Upstream's GravityWellKickoutControl shape: collision, award, light work, restart the
// kickout timer. -1.0 as the TKickoutRestartTimer argument means "use the delay configured
// in the DAT"; 0.0 would mean eject immediately.
//
// Outside Dragon Pass this is the armor kickout. The piece is picked at random among the
// ones the player does not already own (0x004234b0), not in Shield -> Helm -> Chain mail
// order, so a port that hands them out in sequence is wrong even though the manual reads
// that way. The four gargoyle bank lights are cleared and rock A slid back out, because
// reaching this kickout at all means the ball went through the passage rock A was covering.
void ArmorKickoutControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeDragonPass)
	{
		// Dragon Pass turns the same kickout into the jackpot collector -- the manual's
		// "each armor kickout pays the jackpot". No armor, no light work.
		CollectJackpot(0.0f, true);
		caller->Message(MessageCode::TKickoutRestartTimer, -1.0f); // 0x40
		return;
	}

	AwardArmor();
	lite14->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite13->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite12->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite11->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
	lite45->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	rockA->Message(MessageCode::TBlockerEnable, 2.0f);
	caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
}

// 0x00426e40 -- a_kout2 (skeleton: a_kout2Control)
//
// Upstream's BlackHoleKickoutControl shape. Three outcomes by mode:
//
//   normal play  -> start Gargoyle Gaggle-Ball (mode 0xe) and hand the ball to it. Note
//                   this path does NOT restart the kickout timer: the mode controller
//                   (0x0042b3c0) owns the ejection, because it has to add balls first.
//   Dragon Pass  -> one dragon egg, then eject.
//   any other    -> eject, and nothing else of consequence; see below.
//
// Against the manual: the gargoyle kickout starts Gargoyle Gaggle-Ball only from normal
// play, not unconditionally.
//
// The default branch's counter is dead. a_kout2 declares zero scores in the component
// table and get_scoring returns 0 past ScoreCount, so AddScoreFor(caller, 1..3) credits
// nothing on every path; the only observable effect is part1's "you are no longer the
// active scorer" notification to the previous scoring component. It looks like a score
// ladder whose score array was dropped before release. Ported faithfully rather than
// repurposed.
//
// The field being incremented is also the selected-quest slot (SelectNextQuest writes a
// quest id 4..8 into it, TryStartSelectedQuest reads it back as the mode to enter). The two
// tenants cannot hold it at once: this branch runs only while some other mode is already
// active, and the quest machinery only reads the slot when no mode is running and zeroes it
// before writing. A port that keeps the selected quest in its own variable sidesteps the
// question at the cost of bug-compatibility.
void GargoyleKickoutControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	const int mode = info_text_box->MessageField;

	if (mode == ModeNormal)
	{
		info_text_box->MessageField = ModeGargoyleGaggle;
		MissionHandler(FtMessageCode::ModeStarted, nullptr);
		return;
	}

	tabc32wave->Play(nullptr, "GargoyleKickoutControl");

	if (mode == ModeDragonPass)
	{
		AwardDragonEggs(1);
		caller->Message(MessageCode::TKickoutRestartTimer, -1.0f);
		return;
	}

	if (caller->MessageField < 3)
		caller->MessageField++;
	AddScoreFor(caller, caller->MessageField); // always 0; see the note above
	// The original ejects through the a_kout2 global here and through `caller` in the
	// Dragon Pass branch above. Same object -- this handler is bound to one component --
	// kept as written so the two paths stay comparable with the listing.
	a_kout2->Message(MessageCode::TKickoutRestartTimer, -1.0f);
}

// 0x00427ef0 -- a_kout3 (skeleton: a_kout3Control)
//
// The Dragon Blood magnet, and the closest thing in Dragon to upstream's
// JackpotLightControl: one code test, one field store, nothing else. Modelled on it
// directly.
//
// The kickout is switched live (ActiveFlag = 1) by the Dragon Blood spell and re-armed on
// every flash of v_lite29 by part2's DragonBloodLightControl, so the spell light's timer is
// what bounds the magnet's lifetime. All this handler does is record that the magnet
// caught something; a_flip2's handler (part2) is the reader. The DK table has no
// s_magnet* object at all, which is why the effect is a kickout.
void DragonBloodKickoutControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
	{
		// Through the global, as in the original -- the function does not even load its
		// caller argument.
		a_kout3->MessageField = 1;
	}
}

// ---------------------------------------------------------------------------
// The quest-staging hole
// ---------------------------------------------------------------------------

// 0x00426ee0 -- s_hole118 (skeleton: s_hole118Control)
//
// The only handler in this slice that uses ControlBallCaptured/ControlBallReleased, which
// makes upstream's LaunchRampHoleControl the nearest analogue. It is where the ball waits
// while an offer is on screen: the sink-side quest staging at 0x004241c0 parks a ball here
// the same way (MessageField = 2, then a 0.0 hold) when a quest start is pending.
//
// MessageField says who owns the ball sitting in the hole, and every writer agrees on it:
// 1 = claimed (this handler's own 1 s hold, or the egg exchange after it has paid out),
// 2 = parked indefinitely by somebody else (the sink-side staging at 0x004241c0, and mode
// 10 below), 0 = an unclaimed, plain release. Mode 0xc's controller (0x0042ac80) is the
// other writer: it zeroes the field, drops the table back to normal play and releases the
// ball when the player declines the exchange and takes the flat 25,000 -- which is the one
// case the release sound below is for.
void QuestHoleControl(MessageCode code, TPinballComponent* caller)
{
	const int mode = info_text_box->MessageField;

	if (code == MessageCode::ControlBallCaptured) // 0x42
	{
		// Single-ball normal play: the hole offers the Dragon Egg exchange (mode 0xc).
		// With other balls in play the offer is skipped and the hole just holds the ball,
		// falling through to the capture path below.
		if (mode == ModeNormal)
		{
			if (TableG->MultiballCount == 1)
			{
				info_text_box->MessageField = ModeEggExchange;
				MissionHandler(FtMessageCode::ModeStarted, nullptr);
				return;
			}
		}
		else if (mode == ModeSkillShot)
		{
			// Mode 10 is the per-ball quest offer the plunger set up, and its controller
			// (0x004285f0) answers this same capture by calling TryStartSelectedQuest. It
			// has already run by the time we get here -- Dragon's control::handler
			// dispatches to the mode controller *before* the component's own handler --
			// so all that is left is to mark the ball parked and let the quest release it.
			caller->MessageField = 2;
			return;
		}

		tabc32wave->Play(nullptr, "QuestHoleControl");
		caller->MessageField = 1;
		caller->Message(FtMessageCode::THoleHoldBall, 1.0f); // 0x43
		return;
	}

	if (code != MessageCode::ControlBallReleased) // 0x44
		return;

	// Only an unclaimed release during ordinary play is announced: a ball the hole is
	// holding on its own clock (1) or one parked by a mode (2) is that owner's business.
	if (mode == ModeNormal && caller->MessageField == 0)
		tabc34wave->Play(nullptr, "QuestHoleControl");
}

} // namespace dragon
