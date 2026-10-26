// pirates_part2.cpp -- Skulduggery rules: the sinks, kickouts, magnets, launcher, drain,
// ramps, trip wires and the brig hole.
//
// Twenty-seven of PIRATES.EXE's handlers (twenty-eight components -- a_kout1 and a_kout2
// share one), in the shape of SpaceCadetPinball's control.cpp and following
// dragon_part3.cpp / dragon_part4.cpp exactly: free functions in namespace pirates,
// MessageCode spelled out, one address comment per handler.
//
// Sources: analysis/pirates-rules.md (which already reconciles the mode register, the
// dispatcher, the jackpot table and the collectible storage -- this file builds on it and
// does not redo it), decomp/pirates/functions/*.c with every DAT_ annotated with its
// component tag, decomp/pirates/asm/*.txt, analysis/pirates-components.txt for the score
// arrays and analysis/pirates-strings.txt for the resource ids.
//
// Ghidra's control flow and call targets were taken as reliable; its stack-argument values
// were not, and every float, score and message code below was re-read from the listing.
// That matters more here than in Dragon: FUN_0042dfaf is sprintf and Ghidra drops its
// varargs entirely, so each "%ld" operand had to be recovered from the `push` that
// precedes `push 0; push <id>; call 0x409730`. Three handlers decompiled into stack-byte
// noise and are transcribed from the listing alone -- a_sink4 (0x4283f0), a_sink5/6/7's
// shared award routine, and the four lane-ladder handlers.
//
//   0x00425e70  s_launcher1  ball serve / skill shot / shoot again   ~ PlungerControl
//   0x00426110  drain        ball drained                            (table-specific)
//   0x00427b90  a_sink17     hideaway, GOLD bit 1                    (table-specific)
//   0x00427e50  a_sink15     hideaway, GOLD bit 2                    (table-specific)
//   0x00428120  a_sink14     hideaway, GOLD bit 4                    (table-specific)
//   0x004283f0  a_sink4      ship sink                               (table-specific)
//   0x00428700  a_sink9      Treasure Hunt sink, left orbit          ~ ReturnLaneRollover
//   0x00428800  a_sink12     Treasure Hunt sink, right orbit         ~ ReturnLaneRollover
//   0x00428a20  a_sink5      tavern sink, window light v_lite547     ~ ExtraBallLight
//   0x00428a90  a_sink6      tavern sink, window light v_lite546     ~ ExtraBallLight
//   0x00428b00  a_sink7      tavern sink, window light v_lite545     ~ ExtraBallLight
//   0x00428b70  a_ramp8      left island ramp (Island Breeze)        (table-specific)
//   0x00428d80  a_ramp5      right island ramp (Nor'Easter)          (table-specific)
//   0x00428f70  a_ramp13     cannon wire ramp (Port O' Call)         ~ LeftKickerGate
//   0x00429000  s_trip1      launch-lane exit                        ~ SpaceWarpRollover
//   0x00429040  s_hole19     the brig                                ~ BumperGroup
//   0x004290c0  s_trip2      drain-side return wire                  ~ ExtraBallLight
//   0x00429140  s_rexit8     ship wire ramp                          ~ WormHoleDestination
//   0x00429210  s_trip3      left orbit (Tradewind Loop)             (table-specific)
//   0x00429400  s_trip8      right orbit (Tradewind Loop, GOLD "D")  (table-specific)
//   0x00429630  s_trip11     WINDS "W" lane                          ~ FuelRollover1
//   0x0042a3d0  a_sink8      lava sink / shared ball release         ~ SpaceWarpRollover
//   0x0042a420  a_kout1+2    Bermuda and Storm "magnets"             ~ BlackHoleKickout
//   0x0042a470  s_magnet1    keg magnet, keg light v_lite560         (table-specific)
//   0x0042a500  s_magnet2    keg magnet, keg light v_lite561         (table-specific)
//   0x0042a590  s_magnet3    keg magnet, keg light v_lite562         (table-specific)
//   0x0042a620  s_magnet4    keg magnet, keg light v_lite563         (table-specific)
//   0x0042a6b0  s_magnet5    keg magnet, keg light v_lite564         (table-specific)
//   0x0042a740  s_magnet6    keg magnet, keg light v_lite565         (table-specific)
//
// Left to other slices: bumpers (a_bump*), drop targets (a_drop*), spot targets (a_spot*),
// rollovers (a_roll*), the flippers, the rebounders, a_kick1/a_kick2, the one-ways and the
// three v_lite* handlers. s_rexit8 is included because it is the ship *wire ramp* and
// belongs with a_ramp13's traversal codes, not with any of those families.
//
// Like the Dragon slices this is a fragment: the component table, the score arrays and the
// per-table control interface live elsewhere, so it compiles in shape but does not link.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TLight.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates {

// ---------------------------------------------------------------------------
// Components -- tags are defined in control_pirates.cpp
// ---------------------------------------------------------------------------
//
// Declared as plain extern pointers, dragon_part3.cpp's form, because the tags already
// exist in the generated control_pirates.cpp and pirates_part1.cpp binds several of these
// same components; a second definition here would collide.
//
// Names follow dragon_part3.cpp: lights drop the binary's v_ prefix (v_lite560 -> lite560),
// everything else keeps its tag name. pirates_part1.cpp agrees on the lights and on the
// sounds but shortens four of the components (a_blocc -> blocC, a_bloce -> blocE,
// a_blocf -> blocF, a_blocg -> blocG, a_kick2 -> kick2, s_bwal610 -> bwal610) and types the
// blockers TBlocker* and the groups TLightGroup*. Whichever file ends up owning the single
// definition should settle that; the tag names used below are the ones part3 would use.

extern TTextBox* info_text_box; // DAT_0043ad3c -- and its MessageField is the mode register
extern TPinballTable* TableG;   // DAT_0043ab9c

// Ship letters S-H-I-P-! -- cleared when Peg Leg's Loot takes the ball.
extern TLight* lite4;
extern TLight* lite5;
extern TLight* lite6;
extern TLight* lite7;
extern TLight* lite8;

// GOLD. v_lite524 doubles as the 4-bit mask (bit 0 a_sink17, bit 1 a_sink15,
// bit 2 a_sink14, bit 3 the right orbit s_trip8); see pirates-rules.md §4.2.
extern TLight* lite524;
extern TLight* lite525;
extern TLight* lite526;
extern TLight* lite527;

// The six Treasure Hunt "X" lights. A flashing X is the armed one.
extern TLight* mlite44;   // on a_sink17
extern TLight* mlite51;   // on the ship sink; lit solid by Peg Leg's Loot
extern TLight* mlite104;  // on a_sink12
extern TLight* mlite106;  // on a_sink14
extern TLight* mlite107;  // on a_sink15
extern TLight* mlite110; // on a_sink9

// Hideaway "ball is coming back out of here" lights, flashed for 2 s on every entry.
extern TLight* lite519; // a_sink17
extern TLight* lite520; // a_sink15
extern TLight* lite521; // a_sink14

extern TLight* lite504; // launch-lane / playfield routing pair, swapped by s_trip1 and
extern TLight* lite505; // s_trip2 together with the a_bloce blocker

extern TLight* lite533; // the "W" of WINDS, lit by s_trip11
extern TLight* lite544; // flashed at ball serve; its own handler is at 0x0042a260
extern TLight* lite545; // tavern window light hit by a_sink7
extern TLight* lite546; // tavern window light hit by a_sink6
extern TLight* lite547; // tavern window light hit by a_sink5; also the 3-bit tavern mask
extern TLight* lite567; // ship-sink indicator
extern TLight* lite568; // ship-sink extra ball, collected in normal play only
extern TLight* lite569; // Shoot Again; its own handler (0x00426080) steps the phase
extern TLight* lite570; // Ship's Hold -- lit by the ship sink during Mutiny

// Wind-lane indicator lights. Each lane has two singles plus its whole group; the ladder
// below lights one, two or all of them by the group's on-count.
extern TLight* lite600; // left orbit
extern TLight* lite601;
extern TLight* lite603; // left island ramp
extern TLight* lite604;
extern TLight* lite608; // right orbit
extern TLight* lite609;
extern TLight* lite611; // right island ramp
extern TLight* lite612;

// Combo lights, two singles plus the group per lane. The group's on-count is the combo
// tier, and it is cleared by ScoringComponentReplaced -- see WindLaneTraversal.
extern TLight* lite626; // right island ramp
extern TLight* lite627;
extern TLight* lite629; // left island ramp
extern TLight* lite630;
extern TLight* lite631; // left orbit
extern TLight* lite632;
extern TLight* lite634; // right orbit
extern TLight* lite635;

extern TPinballComponent* left_island_light_group;
extern TPinballComponent* left_island_combo_light_group;
extern TPinballComponent* right_wind_light_group;
extern TPinballComponent* right_island_combo_light_group;
extern TPinballComponent* left_orbit_light_group;
extern TPinballComponent* left_orbit_combo_light_group;
extern TPinballComponent* right_orbit_light_group;
extern TPinballComponent* right_orbit_combo_light_group;
extern TPinballComponent* fathom_light_group;             // the multiplier track
extern TPinballComponent* coin_light_group;
extern TPinballComponent* special_treasure_light_group;

extern TPinballComponent* a_blocc; // the three sink-area blockers, held open for -1.0 s
extern TPinballComponent* a_blocf; // (indefinitely) after every sink ejection
extern TPinballComponent* a_blocg;
extern TPinballComponent* a_bloce; // launch-lane blocker, paired with v_lite504/v_lite505
extern TPinballComponent* a_cannon;
extern TPinballComponent* a_kick1;    // left-outlane kickback, armed by the two multiballs
extern TPinballComponent* a_kick2;
extern TPinballComponent* s_bwal610;
extern TPinballComponent* s_launcher1;
extern TPinballComponent* drain;
extern TPinballComponent* a_sink8;  // the lava sink, and the table's shared ball release
extern TPinballComponent* a_sink9;
extern TPinballComponent* a_sink12;
extern TPinballComponent* s_magnet1;
extern TPinballComponent* s_magnet2;
extern TPinballComponent* s_magnet3;
extern TPinballComponent* s_magnet4;
extern TPinballComponent* s_magnet5;
extern TPinballComponent* s_magnet6;
extern TPinballComponent* a_flip1;
extern TPinballComponent* a_flip2;
extern TPinballComponent* a_flip3;
extern TPinballComponent* a_flip4;

// The keg lights, one per magnet. Lit by the arming routine, extinguished with a 2.0 s
// fade when the magnet fires.
extern TLight* lite560;
extern TLight* lite561;
extern TLight* lite562;
extern TLight* lite563;
extern TLight* lite564;
extern TLight* lite565;

extern TSound* tabb08wave; // tavern sink
extern TSound* tabb11wave; // hideaway swallow
extern TSound* tabb12wave; // wind lane / orbit pass
extern TSound* tabb27wave; // ship sink
extern TSound* tabb39wave; // ball into the brig
extern TSound* tabb40wave; // ship wire ramp, outside Attack Ship
extern TSound* tabb41wave; // cannon wire ramp
extern TSound* tabb43wave; // WINDS lane
extern TSound* tabb55wave; // lava sink timer, outside Lava-Ball
extern TSound* tabb57wave; // "Ship Captured"
extern TSound* tabb70wave; // hideaway during Bermuda Triangle
extern TSound* tabb87wave; // keg magnet grab

// DAT_0043aaa0 -- the announcement tokens, addressed as `lea edx,[ecx*4+0x43aaa0]`, so
// this is a table of 4-byte strings stored in place rather than an array of pointers.
// Entries 0..7 are indexed by the tavern window mask, which is why the display shows which
// of the three windows are collected; the Tavern mode setter (FUN_00423520) uses the base
// address as its own start glyph, i.e. entry 0.
//
// pirates_part1.cpp resolved the four slots starting at DAT_0043aac4 to the single
// characters "6".."9", and 0x43aac4 - 0x43aaa0 is nine entries, so the table is one
// contiguous run of tokens and WindsGlyph below is the entry just past the tavern eight.
// UNVERIFIED: what the eight tavern tokens and the WINDS token expand to. Dumping
// PIRATES.DAT's message groups would settle it; nothing in the rules depends on the text.
extern const char TavernWindowGlyphs[8][4];

// DAT_0043aac0 -- the W-I-N-D-S token shown by s_trip11; entry 8 of the same table.
extern const char WindsGlyph[4];

// ---------------------------------------------------------------------------
// Table-local message codes
// ---------------------------------------------------------------------------
//
// Skulduggery is the same engine and the same build as Dragon's Keep, so the piecewise
// shift is identical: TLight +0, the group/target/blocker band +4, gate/kickout/sink +9,
// Control* +10, ControlCollision +11, the mission pair +13. Everything in this file that
// has an upstream name is spelled with it; the codes below are the ones that do not.

namespace FtMessageCode {
// 0x39, sent only to blockers (a_blocc/e/f/g) and to a_cannon, 82 times across the corpus.
// The float is a hold time: 0.0 closes it now, a positive value holds it that long, -1.0
// holds it indefinitely -- which is what every post-ejection site passes.
//
// Arithmetically 0x39 is 57, which in the +4 band would be upstream's TGateDisable (53),
// and that cannot be what a blocker does with it; upstream TBlocker handles only
// Disable/Enable/RestartTimeout (51/52/59 -> 0x37/0x38/0x3f). So this is a code FT added to
// TBlocker, and the name follows pirates-rules.md §2.5.
// UNVERIFIED: the name and the float's exact meaning. Disassembling FT's TBlocker::Message
// is the only thing that would settle it; nothing in the rules depends on the answer, only
// on reproducing the argument.
constexpr auto TBlockerHold = static_cast<MessageCode>(0x39);

// 0x3a, the second half of the cannon fire. Dragon sends the same code to a_jaw and
// a_sword -- animated mechanisms, never blockers -- so it is a mechanism trigger rather
// than anything in the blocker band.
// UNVERIFIED: the name. Dragon's two senders and this one agree on "start the animation",
// which is as far as the rules code sees.
constexpr auto TMechanismTrigger = static_cast<MessageCode>(0x3a);

// 0x4b / 0x4c -- "arm" and "disarm" the holding field of a magnet or a captive kickout.
// Sent to the six s_magnet*, to a_kout1/a_kout2 and to a_kick1. These are *component*
// Message calls, so they live in TMagnet's and TKickout's private code space, not in the
// control band -- which is why it is only a coincidence that 0x4b and 0x4c fall exactly on
// upstream's ControlEnableMultiplier (64) and ControlDisableMultiplier (65) at +11.
// UNVERIFIED: whether a disarmed device can still capture a ball. That is decided inside
// the component class, which is engine code and not in this corpus at all
// (pirates-rules.md §9 item 3).
constexpr auto TMagnetArm = static_cast<MessageCode>(0x4b);
constexpr auto TMagnetDisarm = static_cast<MessageCode>(0x4c);

// 0x4d -- "you are no longer the active scoring component", sent by all three scoring
// wrappers to whichever component scored previously (the DAT_00435d44 bookkeeping).
// pirates-rules.md §2.5 records it as a no-op; it is not. The four lane-ladder handlers
// answer it by clearing their combo light group, which is what makes the combo a *combo*:
// score anywhere else and the ladder resets. See the note on WindLaneTraversal.
constexpr auto ScoringComponentReplaced = static_cast<MessageCode>(0x4d);

// Mode-register codes, named as dragon_part4.cpp names them. 0x4f is arithmetically
// upstream's ControlMissionComplete and 0x50 its ControlMissionStarted, but every
// Skulduggery controller treats its 0x4f branch as "my mode just started", so the
// behavioural names are the useful ones.
constexpr auto ModeStarted = static_cast<MessageCode>(0x4f);
constexpr auto ModeTick = static_cast<MessageCode>(0x50);

// FT-only. Emitted by this file's drain handler; every mode controller's branch for it
// cancels mode_timer and falls into the 0x52 teardown.
constexpr auto BallDrained = static_cast<MessageCode>(0x53);

// 0x54 / 0x55 / 0x56 -- the ramp traversal codes, FT-only (the mission pair stops at 0x50
// and 0x52/0x53 are teardown and abort). The evidence for the split is behavioural:
// a_ramp13 scores on 0x55 and opens a_blocc on 0x56, while a_ramp8, a_ramp5 and s_rexit8
// score on 0x56; a_ramp8 alone also answers 0x54, by releasing a_blocg indefinitely.
// Reading that as entered / exited, with 0x54 as "the ball left the ramp without
// finishing", is the only assignment that makes all four handlers sensible.
// UNVERIFIED: all three names. Disassembling FT's TRamp would settle it; only the
// dispatch shape matters to the rules, and that is confirmed.
constexpr auto TRampAbandoned = static_cast<MessageCode>(0x54);
constexpr auto TRampEntered = static_cast<MessageCode>(0x55);
constexpr auto TRampExited = static_cast<MessageCode>(0x56);

// 0x402 = 1026. With Dragon's public-code shift (+1 at >= 1016, +2 at >= 1021) that is
// upstream 1024 = Reset, and TDrain::Message handles exactly Reset: it kills the pending
// timer and clears BallInDrainFlag. dragon_part4.cpp had to guess SetTiltLock here and
// flagged it UNVERIFIED; Skulduggery's drain sends the real thing, so the guess can be
// retired on the Dragon side too.
constexpr auto DrainReset = static_cast<MessageCode>(0x402);
}

// ---------------------------------------------------------------------------
// The mode register
// ---------------------------------------------------------------------------
//
// info_text_box->MessageField, exactly as in Dragon. Only the states this slice reads are
// named; pirates-rules.md §1.3 has the full table. They are constexpr, so re-declaring the
// ones other slices also need is harmless.

constexpr int ModeInert = 0;
constexpr int ModeGameOver = 1;
constexpr int ModeIdle = 2;
constexpr int ModeSkillShot = 3;
constexpr int ModeAttackShip = 4;        // 4
constexpr int ModeBermudaTriangle = 5;   // 5
constexpr int ModeStorm = 6;             // 6
constexpr int ModeTavern = 7;            // 7
constexpr int ModeTreasureHunt = 8;      // 8
constexpr int ModeMutiny = 9;            // 9
constexpr int ModePegLegsLoot = 11;      // 0xb
constexpr int ModeLavaBall = 12;         // 0xc
constexpr int ModeJailBreak = 13;        // 0xd
constexpr int ModeDrainBonus = 16;       // 0x10
constexpr int ModeTableLockdown = 17;    // 0x11

// Skulduggery has its own string table, so resource ids are passed through rather than
// named in the port's Msg enum (dragon_part3.cpp's convention).
inline Msg PiratesMsg(int rcId) { return static_cast<Msg>(rcId); }

constexpr int Str_SecretHideaway = 3002; // 0xbba, "Secret Hideaway\n%ld"
constexpr int Str_PlainScore = 3003;     // 0xbbb, bare "%ld" -- the generic award line
constexpr int Str_IslandBreeze = 3004;   // 0xbbc, "Island Breeze\n%ld"
constexpr int Str_NorEaster = 3005;      // 0xbbd, "Nor'Easter\n%ld"
constexpr int Str_PortOCall = 3006;      // 0xbbe, "Port O' Call\n%ld" -- the cannon ramp
constexpr int Str_InTheBrig = 3007;      // 0xbbf, "In The Brig", no %ld
constexpr int Str_TradewindLoop = 3008;  // 0xbc0, "Tradewind Loop\n%ld", both orbits
constexpr int Str_OneMillion = 3015;     // 0xbc7, "One Million", no %ld
constexpr int Str_ShipCaptured = 3018;   // 0xbca, "Ship Captured", no %ld
constexpr int Str_ShootAgain = 3026;     // 0xbd2, "Shoot Again", no %ld

// ---------------------------------------------------------------------------
// Helpers owned by other slices
// ---------------------------------------------------------------------------
//
// The three scoring wrappers return the amount credited -- the bare `push eax` straight
// after each call, feeding sprintf's "%ld", is visible at 23 sites in this corpus
// (pirates-rules.md §0.3). Assumed to be defined by pirates_part1.cpp.
int AddScoreFor(TPinballComponent* caller, int index);         // 0x004232c0
int AddScoreAt(TPinballComponent* caller, int score);          // 0x00423310
int AddScoreUnmultiplied(TPinballComponent* caller, int score); // 0x00423350

// Skulduggery's control::handler. Unlike Dragon there is no separate broadcast helper --
// the dispatcher is called directly, and it is also what re-dispatches 0x52 as 0x4f.
void BroadcastMessage(MessageCode code, TPinballComponent* cmp); // 0x0042d850

void SetModeJailBreak();            // 0x00423630, one of the twelve one-line mode setters
void EjectLavaSink(float delay);    // 0x00423680, a_sink8->Message(TSinkResetTimer, delay)
void EjectHideawayAtRandom();       // 0x004236a0
float CheckGoldComplete(float t);   // 0x004239a0, fires the coin award at mask == 0xf
void AwardExtraBall(float t);       // 0x004239d0
void LightShipwreckMystery(float t); // 0x00423a30, lights v_lite529
float BrigCaptureBall(float t);     // 0x00424210
float SpellWinds(float t);          // 0x00424290, non-zero once W-I-N-D-S completed
float BumpMultiplier(float t);      // 0x00424350, ScoreMultiplier = fathoms + 1
void RaiseShipTargets();            // 0x00424440
void DropAndDisableShipTargets();   // 0x004244a0
int ArmNextTreasureX();             // 0x00424580
float CollectJackpot(float t);      // 0x00424700
float AwardSpecialTreasure(float t); // 0x00424930
void ClearKegMagnets();             // 0x00424c70, disarms all six and kills the keg lights
void AwardTavernWindowMystery();    // 0x00424fb0
void AwardBermudaIndicatedWind();   // 0x00425230

// 0x0041de40 with the symbol font selected (the `1` in the fourth argument). The plain
// font-0 form is info_text_box->Display(); dragon_missions.cpp already names this pair.
void DisplayGlyph(TTextBox* box, const char* glyph, float time); // 0x0041de40

// ---------------------------------------------------------------------------
// Cross-slice state
// ---------------------------------------------------------------------------

// DAT_00435d90 -- the Shoot Again phase: 3 = lamp solid and untimed, 2 = 10 s timed on,
// 1 = 5 s flashing, 0 = off. This slice owns it because s_launcher1 arms it at every ball
// serve and the drain clears it; v_lite569's own handler (0x00426080) is what steps it
// down, and s_launcher1's PlungerFeedBall arm only re-asserts the lamp for the phase it
// already holds. Note the handler never decrements it -- pirates-rules.md §5.5 reads the
// 3 -> 0 walk as happening here, and it does not.
int shoot_again_phase = 0;

// DAT_00435d08 -- what the drain's own timer should do next: 0 = nothing (other balls are
// still live), 1 = the ball is gone and the bonus owns the sequence, 2 = serve the next
// ball, 3 = relaunch the saved ball. 2 is written by the drain-bonus controller
// (FUN_0042b820) rather than here; this slice defines the cell because the drain is the
// only other writer and the only reader.
int drain_phase = 0;

// DAT_0043aba8 -- non-zero once the game-over sequence has begun, which locks the drain's
// timer out of serving anything else. Set by the drain, cleared by the bonus controller.
extern int game_over_phase;

// DAT_0043aba0 -- cleared at every ball serve and by the end-of-game lockdown state.
// UNVERIFIED: what reads it. No reader appears in either corpus, so as far as the rules go
// it is write-only, the same shape as Dragon's _DAT_00439b04.
extern int unknown_43aba0;

// DAT_0043abb0 -- set to 1 by Peg Leg's Loot's teardown and consumed by exactly one site,
// the drain below: the next drain blanks the coin and special-treasure groups, i.e. a
// fresh collection cycle starts after the finale (pirates-rules.md §3, state 0xb).
extern int collection_cycle_reset;

// DAT_00435cf8 -- the per-mode scratch counter. Attack Ship counts ships sunk in it and
// Peg Leg's Loot uses it as its payout step, which is why two handlers here write it.
extern int mode_scratch_counter;

// DAT_00435d44 -- the last component to score, the target of ScoringComponentReplaced.
// Owned by the scoring wrappers in pirates_part1.cpp; cleared at ball serve.
extern int last_scoring_component;

// ---------------------------------------------------------------------------
// The launcher -- ball serve, skill shot and Shoot Again
// ---------------------------------------------------------------------------

// 0x00425e70 -- s_launcher1 (skeleton: s_launcher1Control)
//
// The type-1034 launcher stands in for a plunger: PIRATES.DAT has no TPlunger at all. It is
// nearest upstream's PlungerControl, but it does noticeably less -- it does not pick a
// mode, does not reset the table's light state, and does not start the skill shot by
// itself. The per-ball resets Dragon packs into Dragon_s_plunger1_Control are split three
// ways here: this handler, the idle controller (FUN_0042ab90) and the skill-shot
// controller (FUN_0042abf0, which zeroes the "not in skill shot" flag and the
// targets-dropped-this-ball counter in its own 0x4f).
//
// Three codes, and the third is the interesting one:
//
//   PlungerInputReleased  fire the cannon, unless the table is tilt-locked
//   PlungerFeedBall       tick the mode, then re-assert the Shoot Again lamp
//   PlungerStartFeedBall  the actual ball serve
//
// The manual's "15 s re-launch" is not here. The serve delay the drain and the bonus
// controller pass is 0.96 s -- the same constant upstream hardcodes in TPlunger -- and no
// 15-second value appears anywhere in the rules code.
void LauncherControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::PlungerInputReleased) // binary 0x3ed
	{
		// Suppressed during the end-of-ball lockdown, which is what sets TiltLockFlag at
		// game over (see BallDrainControl) rather than an actual tilt.
		if (TableG->TiltLockFlag == 0)
		{
			a_cannon->Message(FtMessageCode::TBlockerHold, 3.0f);
			a_cannon->Message(FtMessageCode::TMechanismTrigger, 0.0);
		}
		return;
	}

	if (code == MessageCode::PlungerFeedBall) // binary 0x3f8
	{
		BroadcastMessage(FtMessageCode::ModeTick, nullptr);

		// Each arm re-applies the lamp state its phase calls for, and only if the lamp is
		// not already in it -- so repeated feeds do not restart the timers. Anything above
		// 3 is clamped to 3 by falling into the solid-on arm.
		if (shoot_again_phase > 3)
			shoot_again_phase = 3;

		switch (shoot_again_phase)
		{
		case 0:
			lite569->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			return;
		case 1:
			if (!lite569->FlasherOnFlag)
				lite569->Message(MessageCode::TLightFlasherStartTimed, 5.0f);
			return;
		case 2:
			if (!lite569->ToggledOnFlag)
				lite569->Message(MessageCode::TLightTurnOnTimed, 10.0f);
			return;
		default: // 3
			if (!lite569->LightOnFlag)
				lite569->Message(MessageCode::TLightResetAndTurnOn, 0.0);
			return;
		}
	}

	if (code != MessageCode::PlungerStartFeedTimer) // binary 0x3f9
		return;

	unknown_43aba0 = 0;

	// Phase 0 means no Shoot Again is pending, so this is a fresh ball and it opens with
	// the skill shot. A re-served saved ball skips all of this: no new skill shot, no keg
	// reset, and the player keeps whatever the previous ball set up.
	if (shoot_again_phase == 0)
	{
		last_scoring_component = 0;
		info_text_box->MessageField = ModeSkillShot;
		BroadcastMessage(FtMessageCode::ModeStarted, nullptr);

		// Keg magnets do not survive the ball (pirates-rules.md §5.4).
		ClearKegMagnets();

		a_bloce->Message(FtMessageCode::TBlockerHold, 0.0);
		lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0);
	}

	// A new ball always starts with the save armed and untimed.
	lite569->Message(MessageCode::TLightResetAndTurnOn, 0.0);
	shoot_again_phase = 3;

	lite544->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_kick2->ActiveFlag = 1;
	s_bwal610->ActiveFlag = 1;

	// The multiplier is a fathom count, so blanking the track is what resets it; the
	// explicit write to ScoreMultiplier is belt and braces, and FUN_00424350 is the only
	// other writer. Skulduggery's neutral multiplier is 1, not upstream's 0.
	fathom_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	TableG->ScoreMultiplier = 1;

	// The outlane kickback is a multiball-only device here: armed by Lava-Ball, Jail Break
	// and the lockdown state, disarmed on every serve.
	a_kick1->Message(FtMessageCode::TMagnetDisarm, 0.0);

	// Repeated verbatim from the phase-0 block above. The original really does send all
	// four twice on a fresh ball; kept as written rather than hoisted, because the
	// duplication is the only thing that makes a *re-served* ball reset the routing too.
	a_bloce->Message(FtMessageCode::TBlockerHold, 0.0);
	lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0);
}

// ---------------------------------------------------------------------------
// The drain
// ---------------------------------------------------------------------------

// 0x00426110 -- drain (skeleton: drainControl)
//
// Decides four things and hands everything else off: was the ball saved, is this the end of
// the game, are other balls still live, and -- if not -- start the bonus. The count-up, the
// extra-ball payout and the next-ball serve live in the bonus controller (FUN_0042b820),
// and the game-over sequence in state 1's (FUN_0042bfd0). drain->MessageField is the held
// Ship's Hold bonus and is read by the bonus controller, not here.
//
// Ordering matters and is kept: Shoot Again first (so a saved ball never reaches the
// ball-drained broadcast), then the abort, then the end-of-game test, then the multiball
// test, then the handover.
void BallDrainControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlTimerExpired) // binary 0x46
	{
		// Once the game-over sequence owns the table the drain's timer must not serve.
		if (game_over_phase != 0)
			return;

		// 0.96 s is upstream's hardcoded TPlunger feed delay, which FT made an argument.
		// The two serves differ: a fresh ball goes through PlungerStartFeedTimer and gets
		// a skill shot, a saved one through PlungerRelaunchBall and does not.
		if (drain_phase == 2)
			s_launcher1->Message(MessageCode::PlungerStartFeedTimer, 0.96f);
		else if (drain_phase == 3)
			s_launcher1->Message(MessageCode::PlungerRelaunchBall, 0.96f);
		return;
	}
	if (code != MessageCode::ControlCollision) // binary 0x4a
		return;

	drain_phase = 1;

	// The end-of-ball lockdown cancels the save outright, lamp and phase together, so the
	// test below then fails and the ball is lost even with Shoot Again showing.
	if (TableG->TiltLockFlag != 0)
	{
		lite569->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		shoot_again_phase = 0;
	}

	if (lite569->light_on())
	{
		info_text_box->Display(pb::get_rc_string(PiratesMsg(Str_ShootAgain)), 2.0f);
		drain_phase = 3;

		// Upstream's TDrain::Message handles only Reset, and that is exactly the effect
		// this path needs: kill the pending timer and clear BallInDrainFlag so the drained
		// ball does not count.
		drain->Message(FtMessageCode::DrainReset, 0.0);
		TableG->BallInDrainFlag = 1;

		// The original jumps back to the dispatch head with the timer code rather than
		// calling the launcher itself, so the relaunch goes through the one drain_phase
		// switch above. No ball-drained broadcast, no ball-count change, no mode change:
		// a saved ball leaves the mode, the bonus and the player untouched.
		BallDrainControl(MessageCode::ControlTimerExpired, caller);
		return;
	}

	// Every mode controller's branch for this cancels mode_timer and falls into teardown --
	// which, per pirates-rules.md §2.4, still books the mode as completed and still pays
	// the +750,000 jackpot bonus. Draining out of a mode is indistinguishable from
	// finishing it.
	BroadcastMessage(FtMessageCode::BallDrained, caller);

	// Last ball of the last player with nothing held back. BallCount is still 1 because
	// the bonus controller is what decrements it.
	if (TableG->MultiballCount == 0 && TableG->ExtraBalls == 0 && TableG->BallCount == 1
		&& TableG->CurrentPlayer - TableG->PlayerCount == -1 && game_over_phase == 0)
	{
		// Dragon computes 1-vs-2 here from TiltLockFlag; Skulduggery only asks whether the
		// flag is not the -1 sentinel, so this is a plain bool in practice.
		// UNVERIFIED: what ever writes TiltLockFlag = -1. Nothing in either corpus does,
		// so the test is always true and game_over_phase is always 1; the sentinel is
		// presumably the engine's "table shutting down" value.
		game_over_phase = TableG->TiltLockFlag != -1 ? 1 : 0;
		TableG->TiltLockFlag = 1;

		// All four flippers, not Dragon's three: Skulduggery mirrors the upper-playfield
		// and ship flippers onto the two buttons, so a_flip3 and a_flip4 exist as
		// components even though they have no rules handler.
		a_flip1->Message(MessageCode::SetTiltLock, 0.0);
		a_flip3->Message(MessageCode::SetTiltLock, 0.0);
		a_flip2->Message(MessageCode::SetTiltLock, 0.0);
		a_flip4->Message(MessageCode::SetTiltLock, 0.0);

		// State 0x11 pays nothing and shows nothing: it exists only to lock the table
		// down, disarm every magnet and kickout and hand the brig's balls back. The bonus
		// state is set a few lines below, so the lockdown is a transient.
		info_text_box->MessageField = ModeTableLockdown;
		BroadcastMessage(FtMessageCode::ModeStarted, nullptr);
	}

	// Other balls are still live, so this is a lost multiball and nothing else. The
	// game-over branch above has to be excluded explicitly, because releasing the brig
	// pushes the live-ball count back above zero.
	if (TableG->MultiballCount != 0 && game_over_phase == 0)
	{
		drain_phase = 0;
		drain->Message(FtMessageCode::DrainReset, 0.0);
		return;
	}

	// Peg Leg's Loot armed this on its way out: the finale consumes the whole collection,
	// so the ball after it starts a fresh cycle.
	if (collection_cycle_reset != 0)
	{
		collection_cycle_reset = 0;
		coin_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		special_treasure_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0);
	}

	// Hand over to the count-up. The bonus state is idempotent-guarded and state 1 (game
	// already over, waiting for a new game) must not be disturbed.
	if (info_text_box->MessageField == ModeDrainBonus || info_text_box->MessageField == ModeGameOver)
		return;
	info_text_box->MessageField = ModeDrainBonus;
	BroadcastMessage(FtMessageCode::ModeStarted, nullptr);
}

// ---------------------------------------------------------------------------
// The three hideaways
// ---------------------------------------------------------------------------
//
// a_sink17, a_sink15 and a_sink14 are the same handler written out three times, differing
// only in their GOLD bit, their GOLD letter light, their "ball returning" light, their
// Treasure Hunt "X" light and -- the one real difference -- what the armed X pays:
// a_sink17 pays the jackpot, the other two a special treasure. That, and not a count, is
// where Treasure Hunt's jackpot comes from; pirates-rules.md §3 already records the
// disagreement with the manual's "5th X = jackpot" and this slice confirms it.
//
// Every arm ends by flashing the hideaway's own light for 2 s and leaving through
// EjectHideawayAtRandom, which in normal play ejects the lava sink and otherwise picks one
// of the three hideaways at random -- the manual's "Secret Hideaway" teleport.
//
// Note what the hideaways do *not* do: the +40,000 the manual credits to the jackpot for
// every sink is not here. It is in the dispatcher, which accrues it on type alone before
// dispatching anything (pirates-rules.md §6.0), so a hideaway pays it even on the arms
// below that score nothing at all.

// 0x00427b90 -- a_sink17 (skeleton: a_sink17Control)
void HideawayLeftControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeIdle:
	{
		const int addedScore = AddScoreFor(caller, 0); // 40,000
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		lite524->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite524->MessageField |= 1; // GOLD "G"
		CheckGoldComplete(2.0f);
		tabb11wave->Play(nullptr, "Hideaway");
		EjectHideawayAtRandom();
		break;
	}

	case ModeBermudaTriangle:
		// Scored but not announced: the shipwreck mystery light is the message.
		AddScoreFor(caller, 0);
		tabb70wave->Play(nullptr, "HideawayBermuda");
		LightShipwreckMystery(0.0);
		EjectHideawayAtRandom();
		break;

	case ModeStorm:
		// Storm's controller ends the mode on any hideaway (string 3050, self-sent abort),
		// so this handler deliberately pays nothing -- the mode-ending award is the point.
		EjectHideawayAtRandom();
		tabb11wave->Play(nullptr, "HideawayStorm");
		break;

	case ModeTreasureHunt:
		if (!mlite44->FlasherOnFlag)
		{
			// The X on this hideaway is not the armed one, so it is an ordinary 40,000.
			const int addedScore = AddScoreFor(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
			info_text_box->Display(Buffer, 2.0f);
			tabb11wave->Play(nullptr, "Hideaway");
			EjectHideawayAtRandom();
		}
		else
		{
			// The armed X goes solid and this hideaway -- alone among the five Treasure
			// Hunt sinks -- pays the jackpot rather than a special treasure.
			mlite44->Message(MessageCode::TLightResetAndTurnOn, 0.0);
			lite524->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			lite519->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			CollectJackpot(0.0);
			ArmNextTreasureX();
			// Out through the lava sink, not the random hideaway: the next X has just been
			// armed and a teleport could land the ball on it for free.
			EjectLavaSink(-1.0f);
		}
		break;

	case ModePegLegsLoot:
		AwardSpecialTreasure(0.0);
		EjectHideawayAtRandom();
		break;

	default:
	{
		const int addedScore = AddScoreFor(caller, 0);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		tabb11wave->Play(nullptr, "Hideaway");
		EjectHideawayAtRandom();
		break;
	}
	}

	lite519->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
}

// 0x00427e50 -- a_sink15 (skeleton: a_sink15Control)
//
// a_sink17's twin on GOLD bit 2 and X light mlite107, with two asymmetries that are in
// the listing and are reproduced rather than tidied: the idle arm flashes this hideaway's
// own light v_lite520 once *before* scoring as well as at the end, and the armed-X arm pays
// a special treasure where a_sink17 pays the jackpot.
void HideawayCentreControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeIdle:
	{
		lite520->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		const int addedScore = AddScoreFor(caller, 0);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		lite525->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite524->MessageField |= 2; // GOLD "O"
		CheckGoldComplete(2.0f);
		tabb11wave->Play(nullptr, "Hideaway");
		EjectHideawayAtRandom();
		break;
	}

	case ModeBermudaTriangle:
		AddScoreFor(caller, 0);
		tabb70wave->Play(nullptr, "HideawayBermuda");
		LightShipwreckMystery(0.0);
		EjectHideawayAtRandom();
		break;

	case ModeStorm:
		EjectHideawayAtRandom();
		tabb11wave->Play(nullptr, "HideawayStorm");
		break;

	case ModeTreasureHunt:
		if (!mlite107->FlasherOnFlag)
		{
			const int addedScore = AddScoreFor(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
			info_text_box->Display(Buffer, 2.0f);
			tabb11wave->Play(nullptr, "Hideaway");
			EjectHideawayAtRandom();
		}
		else
		{
			mlite107->Message(MessageCode::TLightResetAndTurnOn, 0.0);
			lite525->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			lite520->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			AwardSpecialTreasure(0.0);
			ArmNextTreasureX();
			EjectLavaSink(-1.0f);
		}
		break;

	case ModePegLegsLoot:
		AwardSpecialTreasure(0.0);
		EjectHideawayAtRandom();
		break;

	default:
	{
		const int addedScore = AddScoreFor(caller, 0);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		tabb11wave->Play(nullptr, "Hideaway");
		EjectHideawayAtRandom();
		break;
	}
	}

	lite520->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
}

// 0x00428120 -- a_sink14 (skeleton: a_sink14Control)
//
// The third hideaway: GOLD bit 4, letter light v_lite526, X light v_mlite106. Byte-for-byte
// a_sink15's shape, including the leading flash of its own light in the idle arm.
void HideawayRightControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeIdle:
	{
		lite521->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		const int addedScore = AddScoreFor(caller, 0);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		lite526->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite524->MessageField |= 4; // GOLD "L"
		CheckGoldComplete(2.0f);
		tabb11wave->Play(nullptr, "Hideaway");
		EjectHideawayAtRandom();
		break;
	}

	case ModeBermudaTriangle:
		AddScoreFor(caller, 0);
		tabb70wave->Play(nullptr, "HideawayBermuda");
		LightShipwreckMystery(0.0);
		EjectHideawayAtRandom();
		break;

	case ModeStorm:
		EjectHideawayAtRandom();
		tabb11wave->Play(nullptr, "HideawayStorm");
		break;

	case ModeTreasureHunt:
		if (!mlite106->FlasherOnFlag)
		{
			const int addedScore = AddScoreFor(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
			info_text_box->Display(Buffer, 2.0f);
			tabb11wave->Play(nullptr, "Hideaway");
			EjectHideawayAtRandom();
		}
		else
		{
			mlite106->Message(MessageCode::TLightResetAndTurnOn, 0.0);
			lite526->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			lite521->Message(MessageCode::TLightResetAndTurnOff, 0.0);
			AwardSpecialTreasure(0.0);
			ArmNextTreasureX();
			EjectLavaSink(-1.0f);
		}
		break;

	case ModePegLegsLoot:
		AwardSpecialTreasure(0.0);
		EjectHideawayAtRandom();
		break;

	default:
	{
		const int addedScore = AddScoreFor(caller, 0);
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		tabb11wave->Play(nullptr, "Hideaway");
		EjectHideawayAtRandom();
		break;
	}
	}

	lite521->Message(MessageCode::TLightFlasherStartTimed, 2.0f);
}

// ---------------------------------------------------------------------------
// The ship sink
// ---------------------------------------------------------------------------

// 0x004283f0 -- a_sink4 (skeleton: a_sink4Control)
//
// The table's busiest sink: 100,000 in normal play, the ship-sinking shot in Attack Ship,
// the Ship's Hold arm in Mutiny, and the trigger for Peg Leg's Loot's payout.
//
// Transcribed from the listing, which was necessary for more than the arguments.
// **Ghidra's state-2 and default arms are swapped.** The jump table at 0x004286d8 is
// {0x4284d6, 0x4285cb, 0x428606, 0x428649, 0x42841d} and the index table at 0x004286ec is
// {0, 4, 1, 4, 4, 4, 4, 2, 4, 3} over states 2..0xb, so state 2 reaches 0x4284d6 -- the arm
// with the v_lite568 extra-ball test -- and every unlisted state reaches 0x42841d, the
// plain one. Ghidra assigned "case 2" to whichever block sits immediately after the switch,
// which here is the default. The behaviour confirms the table: v_lite568 is a normal-play
// collectible, so the only arm that should look at it is state 2's.
//
// Because the two arms differ only in that test, they are written as one block below with
// the test guarded on the mode.
void ShipSinkControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeAttackShip:
		// The ship is sunk outright: the five ship drop targets go down and stay down,
		// their bank mask pre-set to complete, and the jackpot pays.
		DropAndDisableShipTargets();
		mode_scratch_counter++;
		// The jackpot helper's float return becomes the notify delay here. The three other
		// call sites in this slice discard it, and the compiler emitted `fstp [esp+4]` over
		// the argument slot, so this is what the source wrote.
		// UNVERIFIED: whether that was deliberate. What matters is the effect -- the notify
		// timer is what later sends 0x47 to fathom_light_group, which prints "Ship Sunk".
		fathom_light_group->Message(MessageCode::TLightGroupRestartNotifyTimer, CollectJackpot(0.0));
		EjectLavaSink(-1.0f);
		return;

	case ModeMutiny:
		// Lights Ship's Hold: whatever the drain bonus totals this ball is carried into the
		// next one. No score, no ejection delay change -- the light *is* the award.
		tabb27wave->Play(nullptr, "ShipSink");
		lite570->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		lite567->Message(MessageCode::TLightFlasherStartTimed, 1.0f);
		EjectLavaSink(-1.0f);
		return;

	case ModePegLegsLoot:
		// The finale's arming shot. Note there is no ejection: the ball stays in the sink
		// while the payout steps run, which is the manual's "put the ball in the ship
		// sink". Step 1 is what the controller looks for.
		mlite51->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		lite4->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite5->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite6->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite7->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite8->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite568->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite567->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		mode_scratch_counter = 1;
		return;

	default:
		break;
	}

	// Normal play and every other mode. a_blocf is closed, then all three sink-area
	// blockers are released indefinitely so the ejected ball has somewhere to go. The
	// doubled a_blocf send is in the original.
	a_blocf->Message(FtMessageCode::TBlockerHold, 0.0);
	lite567->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	a_blocc->Message(FtMessageCode::TBlockerHold, -1.0f);
	a_blocf->Message(FtMessageCode::TBlockerHold, -1.0f);
	a_blocg->Message(FtMessageCode::TBlockerHold, -1.0f);
	EjectLavaSink(-1.0f);

	if (info_text_box->MessageField == ModeIdle && lite568->light_on())
	{
		// The ship sink's extra ball, collectable in normal play only.
		lite568->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		AwardExtraBall(0.0);
	}
	else
	{
		const int addedScore = AddScoreFor(caller, 0); // 100,000
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_PlainScore)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
	}

	tabb27wave->Play(nullptr, "ShipSink");
}

// ---------------------------------------------------------------------------
// The two Treasure Hunt sinks
// ---------------------------------------------------------------------------
//
// a_sink9 and a_sink12 only exist for Treasure Hunt: FUN_00424580 enables them when it arms
// the first "X", and outside the mode they disable themselves on contact and push the ball
// out through the *other* one. That pairing is in the listing for both and is not a
// transcription slip -- the two sinks are the ends of one tube, so ejecting through the far
// end is how a ball that fell into a disabled sink gets back to the playfield.

// 0x00428700 -- a_sink9 (skeleton: a_sink9Control)
void TreasureSinkLeftControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeTreasureHunt)
	{
		if (!mlite110->FlasherOnFlag)
		{
			const int addedScore = AddScoreFor(caller, 0); // 40,000
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
			info_text_box->Display(Buffer, 2.0f);
			tabb11wave->Play(nullptr, "TreasureSink");
			EjectHideawayAtRandom();
			return;
		}
		mlite110->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		left_orbit_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		AwardSpecialTreasure(0.0);
		ArmNextTreasureX();
		EjectLavaSink(-1.0f);
		return;
	}

	if (info_text_box->MessageField == ModePegLegsLoot)
	{
		AwardSpecialTreasure(0.0);
		EjectHideawayAtRandom();
		return;
	}

	caller->ActiveFlag = 0;
	a_sink12->Message(MessageCode::TSinkResetTimer, 0.0); // 0.0 = eject now
}

// 0x00428800 -- a_sink12 (skeleton: a_sink12Control)
void TreasureSinkRightControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeTreasureHunt)
	{
		if (!mlite104->FlasherOnFlag)
		{
			const int addedScore = AddScoreFor(caller, 0);
			snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_SecretHideaway)), addedScore);
			info_text_box->Display(Buffer, 2.0f);
			tabb11wave->Play(nullptr, "TreasureSink");
			EjectHideawayAtRandom();
			return;
		}
		mlite104->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		right_orbit_light_group->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		AwardSpecialTreasure(0.0);
		ArmNextTreasureX();
		EjectLavaSink(-1.0f);
		return;
	}

	if (info_text_box->MessageField == ModePegLegsLoot)
	{
		AwardSpecialTreasure(0.0);
		EjectHideawayAtRandom();
		return;
	}

	caller->ActiveFlag = 0;
	a_sink9->Message(MessageCode::TSinkResetTimer, 0.0);
}

// ---------------------------------------------------------------------------
// The three tavern sinks
// ---------------------------------------------------------------------------

// The award shared by a_sink5, a_sink6 and a_sink7 outside Tavern mode. Not a handler of
// its own -- the three call it directly -- so it carries no address comment; it is the
// routine at the end of a_sink12's listing range, immediately after that handler.
//
// lite547->MessageField is the 3-bit window mask for all three sinks (a_sink5 sets bit 0,
// a_sink6 bit 1, a_sink7 bit 2) and also indexes the glyph row that is displayed. Collect
// all three and the mask resets, the three window lights are flashed off together, and the
// score index steps from 0 to 1 -- i.e. 75,000 a sink, 200,000 for the set. The score
// index is literally the "is the mask complete" bool.
static void TavernSinkAward(TPinballComponent* caller)
{
	char Buffer[64];

	DisplayGlyph(info_text_box, TavernWindowGlyphs[lite547->MessageField], 2.0f);

	const bool allThree = (lite547->MessageField & 7) == 7;
	if (allThree)
	{
		lite547->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		lite546->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		lite545->Message(MessageCode::TLightFlasherStartTimedThenStayOff, 1.0f);
		lite547->MessageField = 0;
	}

	const int addedScore = AddScoreFor(caller, allThree ? 1 : 0);
	tabb08wave->Play(nullptr, "TavernSink");

	// 4.0 s, not the 2.0 s everything else in this slice uses: the glyph row above has to
	// be read first and this line replaces it.
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_PlainScore)), addedScore);
	info_text_box->Display(Buffer, 4.0f);

	a_blocc->Message(FtMessageCode::TBlockerHold, -1.0f);
	a_blocf->Message(FtMessageCode::TBlockerHold, -1.0f);
	a_blocg->Message(FtMessageCode::TBlockerHold, -1.0f);
	EjectLavaSink(-1.0f);
}

// 0x00428a20 -- a_sink5 (skeleton: a_sink5Control)
//
// Inside Tavern the three sinks stop scoring and become a target-practice game instead: the
// mode controller keeps a random flutter running on the three window lights, and hitting
// the sink under the one that happens to be flashing pays the 1-in-4 mystery
// (FUN_00424fb0 -- a coin award, a map piece, or nothing). Miss and you get the sound only.
// That is not in the manual at all.
void TavernSinkLeftControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeTavern)
	{
		lite547->MessageField |= 1;
		lite547->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TavernSinkAward(caller);
		return;
	}

	if (lite547->FlasherOnFlag)
		AwardTavernWindowMystery();
	else
		tabb08wave->Play(nullptr, "TavernSinkMiss");
	EjectLavaSink(-1.0f);
}

// 0x00428a90 -- a_sink6 (skeleton: a_sink6Control)
//
// The mask stays on v_lite547 for all three sinks, but the light this one flashes and
// tests is its own, v_lite546.
void TavernSinkCentreControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeTavern)
	{
		lite547->MessageField |= 2;
		lite546->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TavernSinkAward(caller);
		return;
	}

	if (lite546->FlasherOnFlag)
		AwardTavernWindowMystery();
	else
		tabb08wave->Play(nullptr, "TavernSinkMiss");
	EjectLavaSink(-1.0f);
}

// 0x00428b00 -- a_sink7 (skeleton: a_sink7Control)
void TavernSinkRightControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeTavern)
	{
		lite547->MessageField |= 4;
		lite545->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		TavernSinkAward(caller);
		return;
	}

	if (lite545->FlasherOnFlag)
		AwardTavernWindowMystery();
	else
		tabb08wave->Play(nullptr, "TavernSinkMiss");
	EjectLavaSink(-1.0f);
}

// ---------------------------------------------------------------------------
// The four wind lanes with combo ladders
// ---------------------------------------------------------------------------

// The shared body of a_ramp8, a_ramp5, s_trip3 and s_trip8. The four are isomorphic in the
// listing -- same block structure, same constants, same score table -- and differ only in
// their components and their name string, so they are one routine here with four thin
// handlers above it. No address comment: this routine exists four times in the binary, once
// inlined in each handler.
//
// The 12-entry score table the component declares
// ({25k, 50k, 75k, 150k, 175k, 200k, 275k, 300k, 325k, 425k, 450k, 475k}) is a 4x3 grid,
// and this is what indexes it:
//
//   comboTier = the combo light group's on-count, mapped 0 -> 0, 1 -> 3, 2 -> 6, 3+ -> 9
//   windCount = the lane's wind light group's on-count, clamped to 0, 1 or 2
//   index     = comboTier + windCount
//
// So the wind ladder the manual describes -- 1st pass 25,000, 2nd in the same lane 50,000,
// 3rd 75,000 -- is the *bottom row*, and each combo step adds 125,000, then 250,000, then
// 400,000 on top of it. The manual documents the row and not the grid.
//
// Two details that only the listing gives:
//
//   * The wind *indicator* lights are flashed only in normal play, but the score index uses
//     the group's on-count regardless. Inside a mode the lane still pays the ladder value;
//     it just does not show the player where they are on it.
//   * The combo tier is advanced by lighting its own lights, and it is cleared by
//     ScoringComponentReplaced -- i.e. by scoring anywhere else on the table. That is the
//     whole combo mechanic, and it is why pirates-rules.md's "0x4d ... (no-op)" is wrong.
//
// Bermuda Triangle takes the lane over completely: if this lane's first wind light is the
// flashing one it is the indicated lane and pays the progressive table, otherwise a flat
// unmultiplied 50,000. Neither path touches the combo or the ladder.
struct WindLane
{
	TPinballComponent* windGroup;
	TPinballComponent* comboGroup;
	TLight* combo1;
	TLight* combo2;
	TLight* wind1;
	TLight* wind2;
	int nameString;
};

static void WindLaneTraversal(const WindLane& lane, TPinballComponent* caller)
{
	char Buffer[64];

	const int mode = info_text_box->MessageField;

	if (mode == ModeBermudaTriangle)
	{
		if (lane.wind1->FlasherOnFlag)
		{
			AwardBermudaIndicatedWind();
			return;
		}
		AddScoreUnmultiplied(caller, 50000);
		tabb12wave->Play(nullptr, "WindLane");
		return;
	}

	tabb12wave->Play(nullptr, "WindLane");

	int index;
	switch (lane.comboGroup->Message(MessageCode::TLightGroupGetOnCount, 0.0))
	{
	case 0:
		lane.combo1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 0;
		break;
	case 1:
		// combo1 is already on; it is re-flashed so both lights announce together.
		lane.combo1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		lane.combo2->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 3;
		break;
	case 2:
		lane.comboGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 6;
		break;
	default:
		lane.comboGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index = 9;
		break;
	}

	const int windCount = lane.windGroup->Message(MessageCode::TLightGroupGetOnCount, 0.0);
	if (windCount == 0)
	{
		if (mode == ModeIdle)
			lane.wind1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
	}
	else if (windCount == 1)
	{
		if (mode == ModeIdle)
		{
			lane.wind1->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
			lane.wind2->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		}
		index += 1;
	}
	else
	{
		if (mode == ModeIdle)
			lane.windGroup->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 2.0f);
		index += 2;
	}

	const int addedScore = AddScoreFor(caller, index);
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(lane.nameString)), addedScore);
	info_text_box->Display(Buffer, 2.0f);
}

// 0x00428b70 -- a_ramp8 (skeleton: a_ramp8Control)
//
// The left island ramp, "Island Breeze". The only handler in the corpus that answers
// TRampAbandoned, and all it does there is release a_blocg indefinitely -- a ball that
// starts up the ramp and does not finish has to be let out of whatever the ramp entrance
// closed behind it.
void LeftIslandRampControl(MessageCode code, TPinballComponent* caller)
{
	const WindLane lane = {
		left_island_light_group, left_island_combo_light_group,
		lite629, lite630, lite603, lite604, Str_IslandBreeze
	};

	if (code == FtMessageCode::ScoringComponentReplaced)
	{
		left_island_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
		return;
	}
	if (code == FtMessageCode::TRampAbandoned)
	{
		a_blocg->Message(FtMessageCode::TBlockerHold, -1.0f);
		return;
	}
	if (code != FtMessageCode::TRampExited)
		return;

	WindLaneTraversal(lane, caller);
}

// 0x00428d80 -- a_ramp5 (skeleton: a_ramp5Control)
//
// The right island ramp, "Nor'Easter". a_ramp8 without the TRampAbandoned arm.
void RightIslandRampControl(MessageCode code, TPinballComponent* caller)
{
	const WindLane lane = {
		right_wind_light_group, right_island_combo_light_group,
		lite626, lite627, lite611, lite612, Str_NorEaster
	};

	if (code == FtMessageCode::ScoringComponentReplaced)
	{
		right_island_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
		return;
	}
	if (code != FtMessageCode::TRampExited)
		return;

	WindLaneTraversal(lane, caller);
}

// ---------------------------------------------------------------------------
// The cannon wire ramp and the ship wire ramp
// ---------------------------------------------------------------------------

// 0x00428f70 -- a_ramp13 (skeleton: a_ramp13Control)
//
// "Port O' Call", 50,000, and the component that settles what string 3006 is: the spec had
// it down as a seventh wind name missing from the manual, and it is the name of this ramp.
//
// It scores on entry (TRampEntered) and opens a_blocc on exit (TRampExited) -- the opposite
// assignment to the island ramps, which is the behavioural evidence for reading 0x55 and
// 0x56 as entered and exited rather than as two flavours of "completed".
void CannonRampControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code == FtMessageCode::TRampEntered)
	{
		const int addedScore = AddScoreFor(caller, 0); // 50,000
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_PortOCall)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		tabb41wave->Play(nullptr, "CannonRamp");
		return;
	}

	if (code == FtMessageCode::TRampExited)
		a_blocc->Message(FtMessageCode::TBlockerHold, -1.0f);
}

// 0x00429140 -- s_rexit8 (skeleton: s_rexit8Control)
//
// The ship wire ramp. Outside Attack Ship it is a plain 100,000; inside, it captures the
// ship: the five drop targets go back up, the scratch counter advances, "Ship Captured"
// shows, and the multiplier gains a fathom.
//
// Against the manual: the multiplier is *incremented*, not maxed -- FUN_00424350 sets
// ScoreMultiplier to fathoms + 1 after lighting one more fathom light. The Attack Ship arm
// also pays no score at all, which is why the component's single 100,000 entry is only ever
// reachable outside the mode.
void ShipWireRampControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != FtMessageCode::TRampExited)
		return;

	if (info_text_box->MessageField != ModeAttackShip)
	{
		const int addedScore = AddScoreFor(caller, 0); // 100,000
		snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_PlainScore)), addedScore);
		info_text_box->Display(Buffer, 2.0f);
		tabb40wave->Play(nullptr, "ShipWireRamp");
		return;
	}

	RaiseShipTargets();
	mode_scratch_counter++;
	info_text_box->Display(pb::get_rc_string(PiratesMsg(Str_ShipCaptured)), 2.0f);
	BumpMultiplier(2.0f);
	tabb57wave->Play(nullptr, "ShipCaptured");
}

// ---------------------------------------------------------------------------
// The launch-lane trip wires and the brig
// ---------------------------------------------------------------------------

// 0x00429000 -- s_trip1 (skeleton: s_trip1Control)
//
// The ball has left the launch lane: close a_bloce behind it for a second and swap the
// routing pair so v_lite504 reads as the live path. s_trip2 is the undo, and the mode-start
// setup routine (FUN_0042aad0) does the same swap in the other direction, which is how a
// mode takes the upper playfield away.
void LaunchLaneExitControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	a_bloce->Message(FtMessageCode::TBlockerHold, 1.0f);
	lite504->Message(MessageCode::TLightResetAndTurnOn, 0.0);
	lite505->Message(MessageCode::TLightResetAndTurnOff, 0.0);
}

// 0x00429040 -- s_hole19 (skeleton: s_hole19Control)
//
// The brig. It only swallows a ball when a mode could survive losing one: the guard excludes
// Lava-Ball, Jail Break (whose whole point is emptying it) and the end-of-game lockdown,
// and the two multiballs plus the lockdown are also the three paths that ever give the
// balls back.
//
// The score call credits nothing -- s_hole19 declares no scores, so AddScoreFor returns 0 --
// and string 3007 "In The Brig" has no "%ld" to put it in. The call is there for its side
// effect only: it makes the brig the active scoring component, which breaks any combo
// ladder the ball had running. Formatted through sprintf anyway, as the original does.
void BrigHoleControl(MessageCode code, TPinballComponent* caller)
{
	char Buffer[64];

	if (code != MessageCode::ControlBallCaptured) // binary 0x42
		return;

	const int mode = info_text_box->MessageField;
	const bool modeOwnsTheBalls = (mode >= ModeLavaBall && mode <= ModeJailBreak)
		|| mode == ModeTableLockdown;
	if (modeOwnsTheBalls)
		return;

	const int addedScore = AddScoreFor(caller, 0);
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_InTheBrig)), addedScore);
	info_text_box->Display(Buffer, 2.0f);

	// Takes the ball out of play, counts it in the brig, enables the release one-way at
	// two, and starts Jail Break at three.
	BrigCaptureBall(2.0f);
}

// 0x004290c0 -- s_trip2 (skeleton: s_trip2Control)
//
// The return wire on the drain side. Only three states undo s_trip1's routing swap -- idle
// and the two multiballs -- because every mode wants the upper playfield closed off for its
// duration. Attack Ship uses the same wire for something else entirely: it re-raises the
// five ship drop targets, so a ball coming back down re-arms the ship.
void ReturnWireControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	switch (info_text_box->MessageField)
	{
	case ModeIdle:
	case ModeLavaBall:
	case ModeJailBreak:
		a_bloce->Message(FtMessageCode::TBlockerHold, 0.0);
		lite504->Message(MessageCode::TLightResetAndTurnOff, 0.0);
		lite505->Message(MessageCode::TLightResetAndTurnOn, 0.0);
		return;
	case ModeAttackShip:
		RaiseShipTargets();
		return;
	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// The two orbits
// ---------------------------------------------------------------------------

// 0x00429210 -- s_trip3 (skeleton: s_trip3Control)
//
// The left orbit, "Tradewind Loop". The same ladder as the island ramps, reached through
// ControlCollision because an orbit is a trip wire and not a ramp.
//
// s_trip3->MessageField holds this lane's saved light count across Treasure Hunt, which
// disables both orbits and blanks their groups; the mode's teardown restores them with
// TLightGroupToggleSplitIndex. Nothing in this handler touches the field.
void LeftOrbitControl(MessageCode code, TPinballComponent* caller)
{
	const WindLane lane = {
		left_orbit_light_group, left_orbit_combo_light_group,
		lite631, lite632, lite600, lite601, Str_TradewindLoop
	};

	if (code == FtMessageCode::ScoringComponentReplaced)
	{
		left_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;

	WindLaneTraversal(lane, caller);
}

// 0x00429400 -- s_trip8 (skeleton: s_trip8Control)
//
// The right orbit. Same name string as the left -- "Tradewind Loop" is one name over two
// lanes, which is how eight wind light groups cover the manual's six wind names.
//
// It carries one thing the left orbit does not: bit 3 of GOLD, the "D". GOLD is three
// hideaways *plus one orbit pass*, which is why it cannot be spelled from hideaways alone.
// The bit is set in normal play only, and then the lane falls through into the ordinary
// ladder -- so the pass that completes GOLD also pays its combo value.
void RightOrbitControl(MessageCode code, TPinballComponent* caller)
{
	const WindLane lane = {
		right_orbit_light_group, right_orbit_combo_light_group,
		lite634, lite635, lite608, lite609, Str_TradewindLoop
	};

	if (code == FtMessageCode::ScoringComponentReplaced)
	{
		right_orbit_combo_light_group->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
		return;
	}
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField == ModeIdle)
	{
		lite527->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
		lite524->MessageField |= 8; // GOLD "D"
		// Unlike the three hideaways, this site runs the completion check with 0.0 rather
		// than 2.0 -- the coin award announces itself and the ladder line follows.
		CheckGoldComplete(0.0);
	}
	else if (info_text_box->MessageField == ModeBermudaTriangle)
	{
		// Handled here rather than inside WindLaneTraversal because the GOLD block above
		// must not run during Bermuda, and the shared routine is entered after it.
		if (lite608->FlasherOnFlag)
		{
			AwardBermudaIndicatedWind();
			return;
		}
		AddScoreUnmultiplied(caller, 50000);
		tabb12wave->Play(nullptr, "WindLane");
		return;
	}

	WindLaneTraversal(lane, caller);
}

// ---------------------------------------------------------------------------
// The WINDS lane
// ---------------------------------------------------------------------------

// 0x00429630 -- s_trip11 (skeleton: s_trip11Control)
//
// The "W" of W-I-N-D-S, 25,000. The five letter lights are stored as lit lights rather than
// as a mask (the one letter set on this table that is), and the flippers rotate them, so
// which letter this lane lights depends on where the rotation currently sits.
//
// SpellWinds does the completion check and awards the map piece itself, which is why the
// score and the glyph are skipped when it reports a completion: the award is the message.
// Outside normal play the lane scores but shows nothing.
void WindsLaneControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	if (info_text_box->MessageField != ModeIdle)
	{
		AddScoreFor(caller, 0);
		tabb43wave->Play(nullptr, "WindsLane");
		return;
	}

	lite533->Message(MessageCode::TLightFlasherStartTimedThenStayOn, 1.0f);
	if (SpellWinds(0.0) != 0.0f)
		return;

	AddScoreFor(caller, 0); // 25,000
	DisplayGlyph(info_text_box, WindsGlyph, 2.0f);
	tabb43wave->Play(nullptr, "WindsLane");
}

// ---------------------------------------------------------------------------
// The lava sink
// ---------------------------------------------------------------------------

// 0x0042a3d0 -- a_sink8 (skeleton: a_sink8Control)
//
// The table's ball-release device, and the one component in this slice that is mostly used
// by other handlers rather than by its own: FUN_00423680 is a one-liner that ejects it, and
// every sink in this file leaves through that. Lava-Ball sources its two extra balls from
// here, ejecting twice if the brig is empty, once if it holds one, and not at all if it
// holds two or more.
//
// Its own handler is two lines. On a collision it simply ejects immediately. On a timer
// expiry it plays the eject sound, *except* during Lava-Ball -- where the sink is firing
// repeatedly to feed the multiball and the mode has its own audio.
void LavaSinkControl(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlTimerExpired)
	{
		if (info_text_box->MessageField != ModeLavaBall)
			tabb55wave->Play(nullptr, "LavaSink");
		return;
	}

	if (code == MessageCode::ControlCollision)
		caller->Message(MessageCode::TSinkResetTimer, 0.0); // 0.0 = eject now
}

// ---------------------------------------------------------------------------
// The Bermuda and Storm "magnets"
// ---------------------------------------------------------------------------

// 0x0042a420 -- a_kout1 and a_kout2 (skeleton: a_kout2Control)
//
// One handler bound to two components. These are the magnets the manual puts in Bermuda
// Triangle and Storm, and they are not type-1042 magnets at all: they are kickouts driven
// with the same arm/disarm pair as the keg magnets. a_kout1 is the one in the centre of the
// Bermuda circle, armed by that mode's timer branch; a_kout2 is the Hurricane magnet, armed
// by Storm's mode start and disarmed at its teardown. The end-of-game lockdown disarms both.
//
// A flat 1,000,000 -- ten times a keg magnet -- announced as "One Million", with no mode
// test, no multiplier and no progression. Then the kickout restarts its own timer with
// 1.0 s, which is the hold before the ball is thrown back out.
//
// The score goes through AddScoreAt with a *null* caller, not `caller`. That is in the
// listing (`push 0x0` under the amount) and it is not a transcription slip: the effect is
// that catching a ball in either kickout does not make it the active scoring component, so
// it does not break a combo ladder. The keg magnets pass `caller` and do.
void CaptiveKickoutControl(MessageCode code, TPinballComponent* caller)
{
	if (code != MessageCode::ControlCollision)
		return;

	AddScoreAt(nullptr, 1000000);
	info_text_box->Display(pb::get_rc_string(PiratesMsg(Str_OneMillion)), 2.0f);
	caller->Message(MessageCode::TKickoutRestartTimer, 1.0f);
}

// ---------------------------------------------------------------------------
// The six keg magnets
// ---------------------------------------------------------------------------

// The shared body of s_magnet1..s_magnet6. The six handlers are byte-identical in the
// listing apart from the keg light and the magnet itself, so they are one routine with six
// thin handlers; no address comment, because it exists six times in the binary.
//
// A magnet is armed at random by the tavern drop bank (FUN_00424d30, once per tavern-target
// hit and again on bank completion), captures one ball, pays a flat unmultiplied 100,000,
// and disarms itself. Nothing stacks, nothing carries, and the whole set is cleared at the
// Tavern teardown and at every ball serve, so keg magnets do not survive the ball.
//
// The keg light is extinguished with TLightResetAndTurnOff and a 2.0 s argument -- a fade,
// not an immediate off. That one refinement is why pirates-rules.md §0.3 re-read this
// handler against its listing, and it holds.
//
// Each handler sends the disarm through its own global rather than through `caller`, exactly
// as written; the two are the same object because the handler is bound to one component.
static void KegMagnetGrab(TLight* kegLight, TPinballComponent* magnet, TPinballComponent* caller)
{
	char Buffer[64];

	tabb87wave->Play(nullptr, "KegMagnet");
	kegLight->Message(MessageCode::TLightResetAndTurnOff, 2.0f);
	magnet->Message(FtMessageCode::TMagnetDisarm, 0.0);

	const int addedScore = AddScoreAt(caller, 100000);
	snprintf(Buffer, sizeof Buffer, pb::get_rc_string(PiratesMsg(Str_PlainScore)), addedScore);
	info_text_box->Display(Buffer, 2.0f);
}

// 0x0042a470 -- s_magnet1 (skeleton: s_magnet1Control)
void KegMagnet1Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		KegMagnetGrab(lite560, s_magnet1, caller);
}

// 0x0042a500 -- s_magnet2 (skeleton: s_magnet2Control)
void KegMagnet2Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		KegMagnetGrab(lite561, s_magnet2, caller);
}

// 0x0042a590 -- s_magnet3 (skeleton: s_magnet3Control)
void KegMagnet3Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		KegMagnetGrab(lite562, s_magnet3, caller);
}

// 0x0042a620 -- s_magnet4 (skeleton: s_magnet4Control)
void KegMagnet4Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		KegMagnetGrab(lite563, s_magnet4, caller);
}

// 0x0042a6b0 -- s_magnet5 (skeleton: s_magnet5Control)
void KegMagnet5Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		KegMagnetGrab(lite564, s_magnet5, caller);
}

// 0x0042a740 -- s_magnet6 (skeleton: s_magnet6Control)
//
// UNVERIFIED: that the sixth handler pairs with v_lite565 rather than some other keg light.
// The six keg lights are v_lite560..v_lite565 and the six magnets s_magnet1..s_magnet6, and
// the first is pinned by its listing; the remaining five are asserted from the 1:1 pairing
// in pirates-rules.md §5.4 and the .data ordering. Reading the five remaining listings for
// their light globals would close it.
void KegMagnet6Control(MessageCode code, TPinballComponent* caller)
{
	if (code == MessageCode::ControlCollision)
		KegMagnetGrab(lite565, s_magnet6, caller);
}

} // namespace pirates
