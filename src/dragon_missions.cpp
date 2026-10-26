// dragon_missions.cpp -- Dragon's Keep quest/mode state machine, reimplemented.
//
// This is the `control::MissionControl` layer of Full Tilt! Pinball's Dragon's
// Keep table: the mode dispatcher, the quest lifecycle (select / start /
// complete / tear down), the six quest controllers and the collectible award
// paths. It is the structural counterpart of SpaceCadetPinball's
// `control::MissionControl` + `SelectMissionController` + the per-mission
// controllers, with one load-bearing difference: **Dragon's Keep has no ranks.**
// There is no `MissionRcArray`, no `AddRankProgress`, no rank light circles --
// the five ordinary quests are tracked purely as five lit Quest Lights, so
// upstream's entire rank machinery has no counterpart here and is absent.
//
// Source of truth, in order of precedence:
//   1. decomp/dragon/functions/*.c (Ghidra pseudocode of DRAGON.EXE)
//   2. analysis/dragon-quest-system.md (the prior reading of those functions)
//   3. docs/dragon-rules-spec.md (the shipped manual -- loses every conflict)
//
// Every function below carries the address of the routine it reimplements so a
// disagreement can be re-checked against the pseudocode.
//
// MESSAGE CODES. Full Tilt's `MessageCode` enum is NOT upstream's. The offset
// from the 3DPB numbering that SpaceCadetPinball reconstructed is piecewise:
//     TLight                                +0
//     TLightGroup / TSoloTarget / TBlocker   +4
//     gate / kickout / sink                  +9
//     Control* block                        +10
//     ControlCollision                      +11  (0x4a)
// and Full Tilt adds codes with no upstream counterpart at all. So the codes
// this file uses are spelled out in `ft` below rather than taken from the
// upstream enum, which would silently mis-dispatch.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TPinballTable.h"
#include "midi.h"
#include "TPinballComponent.h"

// component_tag<T> instantiates std::is_base_of<TPinballComponent, T>, which needs
// T complete -- a forward declaration is not enough.
#include "TLight.h"
#include "TLightGroup.h"
#include "TSound.h"
#include "TTextBox.h"

// Music track indices into the table's TABC1..TABCN set, which
// midi::load_table_tracks loads 1-based. The engine addresses tracks by index;
// the originals hold Mix_Music-like handles in globals, so these names are the
// handles' roles rather than the handles themselves.
//
// UNVERIFIED: which TABCn file each role maps to. The roles are pinned by the
// call sites; the file numbering needs the track table out of the DAT.
constexpr music_track idle_music = 1;
constexpr music_track dragon_hoard_music = 2;
constexpr music_track fire_lizards_music = 3;
constexpr music_track rescue_maiden_music = 4;
constexpr music_track dragon_pass_music = 5;
constexpr music_track wizards_fury_music = 6;

namespace dragon {

// ---------------------------------------------------------------------------
// Message codes (binary numbering)
// ---------------------------------------------------------------------------

namespace ft {
constexpr MessageCode Msg(int v) { return static_cast<MessageCode>(v); }

// TLight family (delta +0 from upstream).
constexpr MessageCode LightTurnOn = Msg(0x01);
constexpr MessageCode LightTurnOnTimed = Msg(0x09);          // value = seconds
constexpr MessageCode LightFlasherStartTimed = Msg(0x07);    // -1.0 == flash forever
constexpr MessageCode LightFlashTimedThenStayOn = Msg(0x0f);
constexpr MessageCode LightResetAndTurnOn = Msg(0x13);
constexpr MessageCode LightResetAndTurnOff = Msg(0x14);

// Full-Tilt-only TLight codes. Upstream knows these three exist and gave them
// placeholder values -24/-25/-26 (TLightFtTmpOverrideOn / ...Off /
// ...ResetOverride); 0x18/0x19/0x1a is exactly |those| and the observed usage
// matches the names. Pinned by RescueMaiden's window sweep (below), which
// overrides a dark light ON and an already-lit light OFF to blink it inverted,
// then clears the override as the cursor leaves.
constexpr MessageCode LightOverrideOn = Msg(0x18);
constexpr MessageCode LightOverrideOff = Msg(0x19);
constexpr MessageCode LightClearOverride = Msg(0x1a);

// UNVERIFIED: 0x1b, sent only to `light_sweeps` with a float selecting which
// canned sweep to run (0..5, one per quest). 27 is not a group code under the
// +4 rule and `light_sweeps` has its own component type (1038), so this is a
// Full-Tilt-only code on a Full-Tilt-only component. Settled by decompiling
// the TLightGroup/TComponentGroup Message override that handles 0x1b.
constexpr MessageCode LightSweepStart = Msg(0x1b);

// TLightGroup family (delta +4).
constexpr MessageCode GroupReset = Msg(0x26);
constexpr MessageCode GroupGetOnCount = Msg(0x29);
constexpr MessageCode GroupTurnOnNext = Msg(0x2d);   // upstream TLightGroupResetAndTurnOn
constexpr MessageCode GroupTurnOffNext = Msg(0x2e);  // upstream TLightGroupResetAndTurnOff
constexpr MessageCode GroupRestartNotifyTimer = Msg(0x2f);
constexpr MessageCode GroupAnimationBackward = Msg(0x1e);

// Targets and blockers (delta +4).
constexpr MessageCode SoloTargetDisable = Msg(0x35);
constexpr MessageCode SoloTargetEnable = Msg(0x36);
constexpr MessageCode BlockerDisable = Msg(0x37);
constexpr MessageCode BlockerEnable = Msg(0x38);

// Gates (delta +9).
constexpr MessageCode GateDisable = Msg(0x3e);
constexpr MessageCode GateEnable = Msg(0x3f);

// Sinks / kickouts / timers (delta +9..+10).
constexpr MessageCode SinkResetTimer = Msg(0x41);
constexpr MessageCode TimerSet = Msg(0x45);       // value = seconds, -1.0 cancels
constexpr MessageCode TimerGetRemaining = Msg(0x48);

// Control* block (delta +10, ControlCollision +11).
constexpr MessageCode BallCaptured = Msg(0x42);
// 0x43 is sent *to* the egg kickout to let the held ball go. The analysis
// reads 0x44 as ControlBallReleased, so this is a private THole command and
// not that code. UNVERIFIED name; settled by decompiling THole::Message.
constexpr MessageCode HoleReleaseBall = Msg(0x43);
constexpr MessageCode TimerExpired = Msg(0x46);
constexpr MessageCode NotifyTimerExpired = Msg(0x47);
constexpr MessageCode Collision = Msg(0x4a);
constexpr MessageCode DisableMultiplier = Msg(0x4d);
constexpr MessageCode MissionComplete = Msg(0x4f);
constexpr MessageCode MissionStarted = Msg(0x50);

// Full-Tilt-only quest-lifecycle codes. These have no upstream counterpart:
// upstream missions only ever see MissionComplete/MissionStarted because
// Space Cadet has no quest restart window and no mid-mission drain abort.
constexpr MessageCode QuestRestart = Msg(0x51);   // re-arm after the 10 s window
constexpr MessageCode QuestTeardown = Msg(0x52);  // lights off, book the result
constexpr MessageCode QuestAbort = Msg(0x53);     // ball drained mid-quest
constexpr MessageCode RampEntered = Msg(0x54);
constexpr MessageCode RampExited = Msg(0x55);
constexpr MessageCode RampComplete = Msg(0x56);

// UNVERIFIED: 0x3d, delivered to SlayDragon with `a_jaw` as the caller to
// re-arm the dragon's ramp after the jaw animation. Named from that one use.
constexpr MessageCode JawOpened = Msg(0x3d);

// UNVERIFIED: 0x3a, sent *to* the sword and the jaw with a float position or
// duration (0.0 / 1.0 for the sword's stages, 7.0 to open the jaw, 0.0 to shut
// it). Under the +4 blocker rule it would be upstream 54 (TGateEnable) and
// under +9 it would be 49 (TPopupTargetDisable); neither fits a moving part,
// so this is an FT-only code on an FT-only component class. Settled by
// decompiling a_sword's and a_jaw's Message overrides.
constexpr MessageCode MovingPartSetPosition = Msg(0x3a);

constexpr MessageCode FlipperPressed = Msg(0x01);  // same value as LightTurnOn; see EggExchange
constexpr MessageCode SetTiltLock = Msg(0x3f3);
constexpr MessageCode Resume = Msg(0x3f1);
constexpr MessageCode Reset = Msg(0x402);
}  // namespace ft

// Component *type* ids, from vtable slot 0. Distinct namespace from messages.
namespace type {
const int Bumper = 0x3ed;        // 1005
const int PopupTarget = 0x3ee;   // 1006
const int Oneway = 0x3f8;        // 1016
const int Sink = 0x3f9;          // 1017
const int Kickout = 0x3f4;       // 1012
const int SoloTarget = 0x3fb;    // 1019
const int Timer = 0x408;         // 1032
const int Accelerator = 0x40c;   // 1036
const int LightSweeps = 0x40e;   // 1038
}  // namespace type

// ---------------------------------------------------------------------------
// Modes and quests
// ---------------------------------------------------------------------------

// The mode register is `info_text_box->MessageField`. Space Cadet parks its
// mission state in a dummy light (`lite198`); Dragon reuses the text box it is
// already going to write the mode's name into. 0x42c120 switches on exactly
// these sixteen values and forces anything else to Idle.
enum class Mode
{
	GameOver = 0,       // 0x42bf60 -- HIGH SCORE, Reset to table
	BetweenBalls = 1,   // no handler at all; every message is dropped
	Idle = 2,           // 0x428800 -- "Hit Fire Lizards To Begin Quest"
	RestartWindow = 3,  // 0x428870 -- 10 s to restart the quest that just ended
	DragonHoard = 4,
	FireLizards = 5,
	RescueMaiden = 6,
	DragonPass = 7,
	WizardsFury = 8,
	SlayDragon = 9,
	SkillShot = 0xa,
	DrainBonus = 0xb,
	EggExchange = 0xc,
	TowerBarrage = 0xd,   // multiball, +3 balls
	GargoyleGaggle = 0xe, // multiball, +1 ball
	DragonFlurry = 0xf,   // multiball, +4 balls
};

// The five ordinary quests are selectable; SlayDragon is not -- it is reached
// by lighting all five Quest Lights or by the innermost sword target. Values
// are the Mode values on purpose: `a_kout2->MessageField` holds the selected
// quest and 0x424210 copies it straight into the mode register.
enum class Quest
{
	None = 0,
	DragonHoard = 4,
	FireLizards = 5,
	RescueMaiden = 6,
	DragonPass = 7,
	WizardsFury = 8,
};

// Quest time limits, in seconds. The odd values are almost certainly the
// lengths of the matching voice-over waves (tabc46/48/50/53/55), which is why
// they are not round numbers. Read as raw floats out of the binary.
const float QuestTimeDragonHoard = 27.73f;
const float QuestTimeFireLizards = 31.69f;
const float QuestTimeRescueMaiden = 30.45f;
const float QuestTimeDragonPass = 30.65f;
const float QuestTimeWizardsFury = 30.69f;
const float RestartWindowTime = 10.0f;   // 0x428870
const float RestartQuestTime = 15.0f;    // the five ordinary quests' 0x51 re-arm

const float Forever = -1.0f;  // the "flash/display indefinitely" sentinel

// ---------------------------------------------------------------------------
// Component tags
//
// Same convention as control_dragon.cpp: the group name in the DAT is the only
// identifier, and `component_tag<T>::Component` is resolved at table load.
// Ghidra's `DAT_<addr>` for a component is always `tag + 4`, so the names here
// are exactly the ones annotated in the pseudocode.
// ---------------------------------------------------------------------------

component_tag<TTextBox> tag_info_text_box = {"info_text_box"};
component_tag<TPinballComponent> tag_mode_timer = {"mode_timer"};
component_tag<TPinballComponent> tag_drain = {"drain"};

// Quest Lights. v_lite71..75 are the five selectable quests, in quest order;
// v_lite76 is Slay Dragon's.
component_tag<TLight> tag_v_lite71 = {"v_lite71"};
component_tag<TLight> tag_v_lite72 = {"v_lite72"};
component_tag<TLight> tag_v_lite73 = {"v_lite73"};
component_tag<TLight> tag_v_lite74 = {"v_lite74"};
component_tag<TLight> tag_v_lite75 = {"v_lite75"};
component_tag<TLight> tag_v_lite76 = {"v_lite76"};

// Quest-selection bookkeeping cells (all kept in MessageFields, as FT does
// throughout its rules code rather than in statics).
component_tag<TPinballComponent> tag_a_kout2 = {"a_kout2"};  // selected Quest
component_tag<TPinballComponent> tag_a_rockb = {"a_rockb"};  // its index 0..4

// Collectible tracks. Dragon stores all of these as lit lights with no
// separate counters, read back through light_on().
component_tag<TLight> tag_v_lite33 = {"v_lite33"};  // weapon 1, multiplier 2
component_tag<TLight> tag_v_lite34 = {"v_lite34"};  // weapon 2, multiplier 3
component_tag<TLight> tag_v_lite35 = {"v_lite35"};  // weapon 3, multiplier 4
component_tag<TLight> tag_v_lite36 = {"v_lite36"};  // weapon 4, multiplier 5
component_tag<TLight> tag_v_lite37 = {"v_lite37"};  // armor: Shield
component_tag<TLight> tag_v_lite38 = {"v_lite38"};  // armor: Helm of Terror
component_tag<TLight> tag_v_lite39 = {"v_lite39"};  // armor: Chain mail
component_tag<TLight> tag_v_lite28 = {"v_lite28"};  // spell 0 (offensive) AND quests-completed counter
component_tag<TLight> tag_v_lite29 = {"v_lite29"};  // spell 1 (offensive)
component_tag<TLight> tag_v_lite30 = {"v_lite30"};  // spell 1's second display light
component_tag<TLight> tag_v_lite31 = {"v_lite31"};  // spell 2 (defensive) -- bonus hold
component_tag<TLight> tag_v_lite32 = {"v_lite32"};  // spell 3 (defensive)
component_tag<TLight> tag_v_lite57 = {"v_lite57"};  // extra-ball light
// The three multi-mode lights, "2"/"3"/"5"; cleared when Slay Dragon ends.
component_tag<TLight> tag_v_lite77 = {"v_lite77"};
component_tag<TLight> tag_v_lite78 = {"v_lite78"};
component_tag<TLight> tag_v_lite79 = {"v_lite79"};
component_tag<TLightGroup> tag_dragon_egg_light_group = {"dragon_egg_light_group"};
component_tag<TLightGroup> tag_gem_light_group = {"gem_light_group"};

// D-R-A-G-O-N letters -- all six lit starts Dragon Flurry.
component_tag<TLight> tag_v_lite22 = {"v_lite22"};
component_tag<TLight> tag_v_lite23 = {"v_lite23"};
component_tag<TLight> tag_v_lite24 = {"v_lite24"};
component_tag<TLight> tag_v_lite25 = {"v_lite25"};
component_tag<TLight> tag_v_lite26 = {"v_lite26"};
component_tag<TLight> tag_v_lite27 = {"v_lite27"};
component_tag<TLightGroup> tag_dragon_letter_light_group = {"dragon_letter_light_group"};

// Gem lights, one per gem, in gem-bit order.
component_tag<TLight> tag_v_lite88 = {"v_lite88"};    // Gold; also holds the gem bitmask
component_tag<TLight> tag_v_lite89 = {"v_lite89"};    // Diamond
component_tag<TLight> tag_v_lite90 = {"v_lite90"};    // Pearl
component_tag<TLight> tag_v_lite91 = {"v_lite91"};    // Ruby; also the Fire Lizards counter
component_tag<TLight> tag_v_lite92 = {"v_lite92"};    // Emerald
component_tag<TLight> tag_v_lite93 = {"v_lite93"};    // Sapphire
component_tag<TLight> tag_v_lite94 = {"v_lite94"};    // Amethyst
component_tag<TLight> tag_v_lite95 = {"v_lite95"};    // dragon chute (pays the jackpot)
component_tag<TLight> tag_v_lite117 = {"v_lite117"};  // Opal

// Fire-lizard sinks and their lights.
component_tag<TPinballComponent> tag_a_sink1 = {"a_sink1"};
component_tag<TPinballComponent> tag_a_sink2 = {"a_sink2"};
component_tag<TLight> tag_v_lite63 = {"v_lite63"};
component_tag<TLight> tag_v_lite64 = {"v_lite64"};
component_tag<TLight> tag_v_lite65 = {"v_lite65"};
component_tag<TLight> tag_v_lite66 = {"v_lite66"};

// Tower locks: three lock lights, two lockable sinks.
component_tag<TLight> tag_v_lite48 = {"v_lite48"};
component_tag<TLight> tag_v_lite49 = {"v_lite49"};
component_tag<TLight> tag_v_lite50 = {"v_lite50"};
component_tag<TPinballComponent> tag_a_sink16 = {"a_sink16"};
component_tag<TPinballComponent> tag_a_sink17 = {"a_sink17"};
component_tag<TPinballComponent> tag_a_sink18 = {"a_sink18"};  // prison tower

// Rescue Maiden.
component_tag<TLightGroup> tag_prison_window_light_group = {"prison_window_light_group"};
component_tag<TLightGroup> tag_watch_window_light_group = {"watch_window_light_group"};
component_tag<TLightGroup> tag_gate_window_light_group = {"gate_window_light_group"};
// The prison-window trip wires. s_trip6 and s_trip5 share one handler and are
// the pair that reads v_lite48 and drives prison_window_light_group; the other
// two pairs (s_trip3/4, s_trip8/9) read v_lite49 and v_lite50 against the
// watch and gate window groups, 1:1.
component_tag<TPinballComponent> tag_s_trip6 = {"s_trip6"};
component_tag<TPinballComponent> tag_s_trip5 = {"s_trip5"};
component_tag<TSound> tag_tabc51wave = {"tabc51wave"};  // window won
component_tag<TLight> tag_v_lite47 = {"v_lite47"};
component_tag<TLight> tag_v_lite125 = {"v_lite125"};
component_tag<TLight> tag_v_lite126 = {"v_lite126"};

// Dragon Pass.
component_tag<TLightGroup> tag_gargoyle_spot_light_group = {"gargoyle_spot_light_group"};
component_tag<TPinballComponent> tag_a_rocka = {"a_rocka"};
component_tag<TLight> tag_v_lite14 = {"v_lite14"};
component_tag<TLight> tag_v_lite45 = {"v_lite45"};
component_tag<TLight> tag_v_lite46 = {"v_lite46"};

// Wizard's Fury.
component_tag<TPinballComponent> tag_a_sink4 = {"a_sink4"};  // the well; holds the armed award
component_tag<TLightGroup> tag_well_light_group = {"well_light_group"};
component_tag<TLightGroup> tag_spell_spot_light_group = {"spell_spot_light_group"};

// Slay Dragon.
component_tag<TPinballComponent> tag_a_sink19 = {"a_sink19"};  // dragon's mouth
component_tag<TPinballComponent> tag_a_jaw = {"a_jaw"};
component_tag<TPinballComponent> tag_a_ramp107 = {"a_ramp107"};
component_tag<TPinballComponent> tag_s_bwal170 = {"s_bwal170"};
component_tag<TPinballComponent> tag_a_sword = {"a_sword"};
component_tag<TLightGroup> tag_dragon_fire_light_group = {"dragon_fire_light_group"};
component_tag<TLightGroup> tag_dragon_mouth_light_group = {"dragon_mouth_light_group"};
component_tag<TLight> tag_v_lite113 = {"v_lite113"};  // fire sweep cursor
component_tag<TLight> tag_v_lite114 = {"v_lite114"};  // hits remaining
component_tag<TLight> tag_v_lite115 = {"v_lite115"};
component_tag<TLight> tag_v_lite119 = {"v_lite119"};
component_tag<TLight> tag_v_lite16 = {"v_lite16"};  // sword target lights
component_tag<TLight> tag_v_lite17 = {"v_lite17"};
component_tag<TLight> tag_v_lite18 = {"v_lite18"};
component_tag<TPinballComponent> tag_a_spot19 = {"a_spot19"};
component_tag<TPinballComponent> tag_a_spot26 = {"a_spot26"};
component_tag<TPinballComponent> tag_a_spot28 = {"a_spot28"};

// Spell-bank / skill-shot targets, top to bottom.
component_tag<TPinballComponent> tag_a_spot4 = {"a_spot4"};
component_tag<TPinballComponent> tag_a_spot5 = {"a_spot5"};
component_tag<TPinballComponent> tag_a_spot6 = {"a_spot6"};
component_tag<TPinballComponent> tag_a_spot7 = {"a_spot7"};
component_tag<TPinballComponent> tag_a_spot22 = {"a_spot22"};
// Tower bank.
component_tag<TPinballComponent> tag_a_spot1 = {"a_spot1"};
component_tag<TPinballComponent> tag_a_spot2 = {"a_spot2"};
component_tag<TPinballComponent> tag_a_spot3 = {"a_spot3"};
// Gargoyle bank.
component_tag<TPinballComponent> tag_a_spot8 = {"a_spot8"};
component_tag<TPinballComponent> tag_a_spot10 = {"a_spot10"};
component_tag<TPinballComponent> tag_a_spot12 = {"a_spot12"};
component_tag<TPinballComponent> tag_a_spot14 = {"a_spot14"};

// Ramps and chutes.
component_tag<TPinballComponent> tag_a_ramp6 = {"a_ramp6"};      // tower ramp
component_tag<TPinballComponent> tag_a_ramp103 = {"a_ramp103"};  // dragon ramp
component_tag<TPinballComponent> tag_s_oneway2 = {"s_oneway2"};
component_tag<TPinballComponent> tag_s_oneway5 = {"s_oneway5"};  // dragon chute
component_tag<TPinballComponent> tag_a_accl1 = {"a_accl1"};      // moat chute
component_tag<TPinballComponent> tag_s_hole118 = {"s_hole118"};  // dragon-egg kickout
component_tag<TPinballComponent> tag_a_flip1 = {"a_flip1"};      // left flipper == "Yes"

// Blockers and kickers driven by the armor/spell effects.
component_tag<TPinballComponent> tag_a_kick1 = {"a_kick1"};
component_tag<TPinballComponent> tag_a_kout3 = {"a_kout3"};
component_tag<TPinballComponent> tag_a_center_post = {"a_center_post"};
component_tag<TPinballComponent> tag_a_bloce = {"a_bloce"};
component_tag<TPinballComponent> tag_a_blocf = {"a_blocf"};
component_tag<TLightGroup> tag_outlane_kicker_light_group = {"outlane_kicker_light_group"};

// Diverters. Each quest points the ball at its own objective by flipping these.
component_tag<TPinballComponent> tag_a_dvtraa = {"a_dvtraa"};
component_tag<TPinballComponent> tag_a_dvtrab = {"a_dvtrab"};
component_tag<TPinballComponent> tag_a_dvtrba = {"a_dvtrba"};
component_tag<TPinballComponent> tag_a_dvtrbb = {"a_dvtrbb"};
component_tag<TPinballComponent> tag_a_dvtrca = {"a_dvtrca"};
component_tag<TPinballComponent> tag_a_dvtrcb = {"a_dvtrcb"};
component_tag<TPinballComponent> tag_a_dvtrda = {"a_dvtrda"};
component_tag<TPinballComponent> tag_a_dvtrdb = {"a_dvtrdb"};

component_tag<TLightGroup> tag_light_sweeps = {"light_sweeps"};

// Voice-overs and effects. Named by their wave resource, as in the DAT.
component_tag<TSound> tag_tabc13wave = {"tabc13wave"};  // sword stage change
component_tag<TSound> tag_tabc21wave = {"tabc21wave"};  // extra ball
component_tag<TSound> tag_tabc24wave = {"tabc24wave"};  // shield
component_tag<TSound> tag_tabc27wave = {"tabc27wave"};  // dragon egg
component_tag<TSound> tag_tabc32wave = {"tabc32wave"};  // "No Dragon Eggs"
component_tag<TSound> tag_tabc33wave = {"tabc33wave"};  // egg-exchange prompt
component_tag<TSound> tag_tabc35wave = {"tabc35wave"};  // spell cast
component_tag<TSound> tag_tabc39wave = {"tabc39wave"};  // weapon
component_tag<TSound> tag_tabc40wave = {"tabc40wave"};  // helm
component_tag<TSound> tag_tabc41wave = {"tabc41wave"};  // shield
component_tag<TSound> tag_tabc42wave = {"tabc42wave"};  // chain mail
component_tag<TSound> tag_tabc43wave = {"tabc43wave"};  // jackpot
component_tag<TSound> tag_tabc46wave = {"tabc46wave"};  // Dragon Hoard
component_tag<TSound> tag_tabc47wave = {"tabc47wave"};  // gem collected
component_tag<TSound> tag_tabc48wave = {"tabc48wave"};  // Fire Lizards
component_tag<TSound> tag_tabc50wave = {"tabc50wave"};  // Rescue Maiden
component_tag<TSound> tag_tabc53wave = {"tabc53wave"};  // Dragon Pass
component_tag<TSound> tag_tabc55wave = {"tabc55wave"};  // Wizard's Fury
component_tag<TSound> tag_tabc57wave = {"tabc57wave"};  // Slay Dragon
component_tag<TSound> tag_tabc58wave = {"tabc58wave"};  // dragon fire sweep

// MIDI tracks, one per quest. Upstream calls the equivalent `midi::play_track`.

// ---------------------------------------------------------------------------
// Per-table interface this file depends on but does not define
//
// These stand in for the Dragon-specific parts of the engine that are not
// written yet. Each maps to one Ghidra routine; the comment is the address.
// ---------------------------------------------------------------------------

int AddScoreAt(TPinballComponent* source, int score);            // 0x4230f0 -- multiplied
int DragonSpecialAddScore(TPinballComponent* source, int score);     // 0x423130 -- unmultiplied
void AddToJackpot(int amount);                                 // 0x4238a0 -- clamp 10,000,000
void AddToSuperJackpot(int amount);                            // 0x423870 -- clamp 20,000,000
int CollectJackpot(float unused, bool showGlyph);                // 0x424490
void AwardExtraBall(float displayTime);                        // 0x4231e0
int RandBelow(int n);                                                // 0x42c4cb, 15-bit LCG -> [0,n)
const char* GetRcString(int id);
void Display(TTextBox* box, const char* text, float time);     // 0x41de40 with font 0
void DisplayGlyph(TTextBox* box, const char* glyph, float time); // 0x41de40 with font 1 (symbol)
void ClearText(TTextBox* box);                                 // 0x41d930
// 0x42c76f -- renders a label (or format string) plus ONE numeric value into
// `out`. The value is a lost argument in every decompile of every caller: the
// 1996 build passes it on the x87 stack, which Ghidra drops.
//
// UNVERIFIED layout. It cannot be a plain sprintf(out, fmt, value): string
// 3008 is a bare "%ld" while 3019 is a bare "Dragon Hoard" with no specifier
// at all, and both are passed in this same slot. So it either concatenates
// label and value itself, or there are two call shapes the decompile has
// flattened into one. Settled by disassembling 0x42c76f.
void FormatWithValue(char* out, const char* labelOrFormat);
void ComponentHandler(MessageCode code, TPinballComponent* caller);  // 0x423060
void Play(TSound* sound);  // vtable +0x18; the original returns the clip length

// Component handlers from control_dragon.cpp that the award paths drive
// directly, bypassing the message bus. The binary really does call these as
// plain functions with a forged caller, which is how a single award can apply
// a blocker effect without the blocker having been hit.
void CenterPostControl(MessageCode code, TPinballComponent* caller);  // 0x425910
void RightOutlaneBlockerControl(MessageCode code, TPinballComponent* caller);        // 0x425a10
void MoatBlockerControl(MessageCode code, TPinballComponent* caller);        // 0x425b10

// `caller->GetComponentType()` stands for vtable slot 0, which in Full Tilt
// returns the component's type id (1005 bumper, 1017 sink, ...). Upstream has
// no such method -- it dispatches on dynamic_cast instead -- so this is the one
// place a port has to add something to TPinballComponent.
//
// `group->LightAt(i)` stands for the inlined bounds-checked index into a
// TLightGroup's member list that the prison sweep does by hand.
//
// `AnnouncementPending(box)` stands for `*(int*)(info_text_box + 0x5e) != 0`:
// the text box's own display timer, non-zero while something is still on
// screen. Every quest's restart branch checks it before overwriting the
// display with its countdown. UNVERIFIED which TTextBox field that is -- the
// offset is confirmed, the name is not.
bool AnnouncementPending(TTextBox* box);

// Glyph runs. Dragon announces collectibles as single characters in a symbol
// font, not as words: '$' = spell, "%&'(" = the four weapons, "+*)" =
// Shield/Helm/Chain, ",-./0123" = the nine dragon-death frames, '"' = jackpot,
// and EggGlyphs[n] is a run of n '#' characters.
extern const char* GlyphSpell;
extern const char* GlyphWeapon[4];
extern const char* GlyphArmor[3];
extern const char* GlyphDragonDeath[9];
extern const char* GlyphJackpot;
extern const char* EggGlyphs[9];

// String ids, as resource numbers. Only the ones this file displays.
enum
{
	STR_BARE_NUMBER = 3008,        // "%ld" -- the award on its own
	STR_IDLE_HINT = 3001,          // "Hit Fire Lizards To Begin Quest"
	STR_HOARD_NAME = 3019,
	STR_HOARD_HINT = 3020,         // "Steal Gems From Dragon"
	STR_GEM_GOLD = 3021,
	STR_GEM_DIAMOND = 3022,
	STR_GEM_PEARL = 3023,
	STR_GEM_RUBY = 3024,
	STR_GEM_EMERALD = 3025,
	STR_GEM_SAPPHIRE = 3026,
	STR_GEM_AMETHYST = 3027,
	STR_GEM_OPAL = 3028,
	STR_FIRE_LIZARD_SLAIN = 3029,
	STR_LIZARDS_NAME = 3030,
	STR_LIZARDS_HINT = 3031,
	STR_MAIDEN_NAME = 3032,
	STR_MAIDEN_HINT = 3033,        // "Put ball in Prison Tower"
	STR_MAIDEN_RESCUED = 3034,
	STR_PASS_NAME = 3035,
	STR_PASS_HINT = 3036,
	STR_FURY_NAME = 3037,
	STR_FURY_HINT = 3038,
	STR_SLAY_NAME = 3047,
	STR_SLAY_HINT = 3048,
	STR_DRAGON_SLAIN = 3050,
	STR_EXCHANGE_EXTRA_BALL = 3052,
	STR_EXCHANGE_EB_LIGHT = 3053,
	STR_EXCHANGE_ARMOR = 3054,
	STR_EXCHANGE_WEAPON = 3055,
	STR_EXCHANGE_SPELL = 3056,
	STR_EXCHANGE_QUEST_FIRST = 3057,  // 3057..3061, one per quest 4..8
	STR_RESTART_PROMPT = 3067,
	STR_NO_DRAGON_EGGS = 3068,
};

// ---------------------------------------------------------------------------
// Module state
//
// Four of these are genuine statics in the binary; the rest of Dragon's quest
// state lives in component MessageFields (see the accessors below), which is
// why this block is so much smaller than it looks like it should be.
// ---------------------------------------------------------------------------

// 0x439b14 -- set by the drain's collision while no multiball is running, and
// it blocks quest *selection* so a quest cannot be handed out to a ball that
// is already gone.
bool BallDraining = false;

// 0x439b10 -- suppresses Idle's text clear for one entry, so the skill-shot
// and egg-exchange announcements survive the transition back to Idle.
bool KeepAnnouncement = false;

// 0x434d14 -- Rescue Maiden borrows v_lite48 (a tower-lock light) for its own
// display and must put it back exactly as it found it: 0 = was off, 1 = was
// solid on, 2 = was flashing.
int SavedTowerLockState = 0;

// 0x434d04 -- Slay Dragon's intro light sweep is still running. While set, the
// "ball passed the one-way, start the clock" path is ignored.
bool SlayDragonIntro = false;

// 0x434d18 -- a ball is on the dragon ramp (a_ramp107). A timer expiry while
// this is set is deferred by 0.05 s rather than ending the quest, so the mode
// never dies with the ball mid-ramp.
bool BallOnDragonRamp = false;

// 0x439b18 -- the dragon is armed (jaw open, mouth sink live).
bool DragonArmed = false;

// 0x439b0c -- direction of the dragon-fire light sweep: +1 forward, -1 back.
int FireSweepDirection = 1;

// 0x434d1c / 0x434d28 -- the egg-exchange offer currently on screen, and the
// spell slot it would cast. Held across the 0x4f prompt and the flipper reply.
int EggOffer = 0;
int EggOfferSpellSlot = -1;

// 0x434d08 / 0x434d24 -- per-ball counters the drain bonus reads back:
// collisions with popup/solo targets, and spells cast. Reset by the plunger.
extern int TargetsHitThisBall;
extern int SpellsCastThisBall;

// 0x434d2c -- how far the ball has got since the plunger. 3 means "still in
// the launch lane"; Slay Dragon waits for it to leave before starting its
// clock. UNVERIFIED name: set to 3 by the plunger and by NewGame, stepped
// 3->2->1->0 by v_lite40Control, and read here and by s_oneway2Control.
// Settled by reading 0x424870 (v_lite40Control) properly.
extern int shoot_again_flag;

// Forward declarations of the modes this file does not implement. They are
// states of the same machine but belong to the bonus/multiball slice.
void GameOverController(MessageCode code);                            // 0x42bf60
void DrainBonusController(MessageCode code, TPinballComponent* caller); // 0x42b7c0
void TowerBarrageController(MessageCode code);                        // 0x42b0e0
void GargoyleGaggleController(MessageCode code);                      // 0x42b3c0
void DragonFlurryController(MessageCode code);                        // 0x42b510

void MissionHandler(MessageCode code, TPinballComponent* caller);
void MissionControl(Mode mode, MessageCode code, TPinballComponent* caller);
int SelectNextQuest();
void SlayDragonController(MessageCode code, TPinballComponent* caller);

// ---------------------------------------------------------------------------
// State accessors
//
// Spelling these out keeps the controllers readable and documents, once, which
// MessageField means what. Every one of these is a `*(int*)(DAT_x + 6)` in the
// pseudocode.
// ---------------------------------------------------------------------------

Mode CurrentMode() { return static_cast<Mode>(tag_info_text_box.Component->MessageField); }
void SetMode(Mode m) { tag_info_text_box.Component->MessageField = static_cast<int>(m); }

Quest SelectedQuest() { return static_cast<Quest>(tag_a_kout2.Component->MessageField); }
void SetSelectedQuest(Quest q) { tag_a_kout2.Component->MessageField = static_cast<int>(q); }

int SelectedQuestLightIndex() { return tag_a_rockb.Component->MessageField; }
void SetSelectedQuestLightIndex(int i) { tag_a_rockb.Component->MessageField = i; }

// The quest the restart window would restart; -1 once the window has closed.
Mode EndedQuest() { return static_cast<Mode>(tag_mode_timer.Component->MessageField); }
void SetEndedQuest(int m) { tag_mode_timer.Component->MessageField = m; }

int QuestsCompleted() { return tag_v_lite28.Component->MessageField; }
int GemMask() { return tag_v_lite88.Component->MessageField; }
void SetGemMask(int m) { tag_v_lite88.Component->MessageField = m; }
int DragonHitsLeft() { return tag_v_lite114.Component->MessageField; }
void SetDragonHitsLeft(int n) { tag_v_lite114.Component->MessageField = n; }

TLight* const QuestLights[5] = {
	tag_v_lite71.Component, tag_v_lite72.Component, tag_v_lite73.Component,
	tag_v_lite74.Component, tag_v_lite75.Component,
};

// PTR_PTR_0043bb78: the eight lights the dragon-fire sweep walks.
TLight* const DragonFireLights[8] = {
	tag_v_lite113.Component, tag_v_lite114.Component, tag_v_lite115.Component,
	nullptr /* v_lite58 */, nullptr /* v_lite59 */, nullptr /* v_lite60 */,
	nullptr /* v_lite61 */, nullptr /* v_lite62 */,
};
// UNVERIFIED: v_lite58..62 are named in the analysis from the static pointer
// array but have no tag declared above because nothing else in this slice
// touches them. Declare the five tags when the sweep is wired up.

// ---------------------------------------------------------------------------
// Small shared predicates
// ---------------------------------------------------------------------------

// Dragon reads every collectible track back out of the lights. The binary
// inlines `LightOnFlag || ToggledOnFlag || FlasherOnFlag` at ~200 sites; that
// is upstream's `TLight::light_on()` and it counts a *flashing* light as owned,
// which matters because several awards flash-then-stay-on.
bool Owned(TLight* light) { return light->light_on(); }

int CountOwned(TLight* const* lights, int n)
{
	int count = 0;
	for (int i = 0; i < n; i++)
		if (Owned(lights[i]))
			count++;
	return count;
}

TLight* const WeaponLights[4] = {
	tag_v_lite33.Component, tag_v_lite34.Component,
	tag_v_lite35.Component, tag_v_lite36.Component,
};
TLight* const ArmorLights[3] = {
	tag_v_lite37.Component, tag_v_lite38.Component, tag_v_lite39.Component,
};
// Spell slots, in the order 0x423600 tests them. Slot 1's partner light
// v_lite30 is deliberately absent: it mirrors slot 1 for display only and is
// never tested, which is what explains the manual's "five lights, four spells".
TLight* const SpellLights[4] = {
	tag_v_lite28.Component, tag_v_lite29.Component,
	tag_v_lite31.Component, tag_v_lite32.Component,
};

// Show "<quest name>  <seconds left>" for half a second, but only if the clock
// is actually running -- the controllers all gate on this so a cancelled timer
// does not print "0".
void DisplayTimeRemaining(int nameStringId)
{
	auto remaining = tag_mode_timer.Component->Message(ft::TimerGetRemaining, 0.0f);
	if (remaining == 0.0f)
		return;

	char buffer[128];
	FormatWithValue(buffer, GetRcString(nameStringId));
	Display(tag_info_text_box.Component, buffer, 0.5f);
}

// ---------------------------------------------------------------------------
// Collectible award paths
// ---------------------------------------------------------------------------

// 0x423240 -- the Shield's effect, applied both when the armor is awarded and
// again at the start of Slay Dragon (the shield is re-armed for the finale).
void ApplyShield()
{
	tag_a_kick1.Component->ActiveFlag = 1;
	tag_v_lite37.Component->MessageField = 0;
	tag_v_lite37.Component->Message(ft::LightFlashTimedThenStayOn, 2.0f);
	tag_outlane_kicker_light_group.Component->Message(ft::LightFlashTimedThenStayOn, 0.0f);
	DisplayGlyph(tag_info_text_box.Component, GlyphArmor[0], 2.0f);
	Play(tag_tabc41wave.Component);
}

// 0x423330 -- AwardWeapon. Lights the first un-lit weapon light *in order*
// (not at random, unlike armor) and sets the table multiplier to match. The
// multiplier is a direct function of the weapon count, which is what makes the
// drain bonus's "Weapon Bonus" term equal (weapons + 1) x 75,000.
void AwardWeapon()
{
	static const int scores[4] = {25000, 35000, 45000, 55000};
	static const int multipliers[4] = {2, 3, 4, 5};

	for (int slot = 0; slot < 4; slot++)
	{
		if (Owned(WeaponLights[slot]))
			continue;

		WeaponLights[slot]->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		pb::MainTable->ScoreMultiplier = multipliers[slot];
		// Unmultiplied: the award must not be scaled by the multiplier it just set.
		DragonSpecialAddScore(nullptr, scores[slot]);
		DisplayGlyph(tag_info_text_box.Component, GlyphWeapon[slot], 2.0f);
		break;
	}

	// Plays even when all four are already lit -- the binary falls through to
	// the wave unconditionally, so a redundant award is still audible.
	Play(tag_tabc39wave.Component);
}

// 0x4234b0 -- AwardArmor. Picks uniformly at random among the un-owned pieces.
// The manual's "the next piece of armor" is wrong.
void AwardArmor()
{
	int candidates[3];
	int count = 0;
	for (int slot = 0; slot < 3; slot++)
		if (!Owned(ArmorLights[slot]))
			candidates[count++] = slot;

	if (count == 0)
		return;

	switch (candidates[RandBelow(count)])
	{
	case 0:  // Shield -- enable the kickback
		ApplyShield();
		AddScoreAt(nullptr, 25000);
		break;
	case 1:  // Helm of Terror -- raise the centre post
		Play(tag_tabc40wave.Component);
		CenterPostControl(ft::BlockerEnable, tag_a_center_post.Component);
		DisplayGlyph(tag_info_text_box.Component, GlyphArmor[1], 2.0f);
		Play(tag_tabc40wave.Component);
		AddScoreAt(nullptr, 50000);
		break;
	default:  // Chain mail -- close the right outlane
		Play(tag_tabc42wave.Component);
		RightOutlaneBlockerControl(ft::BlockerEnable, tag_a_bloce.Component);
		DisplayGlyph(tag_info_text_box.Component, GlyphArmor[2], 2.0f);
		Play(tag_tabc42wave.Component);
		AddScoreAt(nullptr, 100000);
		break;
	}

	// NOTE: the Shield branch does not light v_lite37 itself -- ApplyShield
	// does, via LightFlashTimedThenStayOn. The Helm and Chain-mail branches do
	// not touch v_lite38/v_lite39 at all: their blocker components light their
	// own lights. That asymmetry is in the binary, not a transcription slip.
	// The 60 s durations the manual quotes come from the blockers' own
	// +0x86/+0x8a fields, not from here.
}

// 0x423600 -- a random un-owned spell slot, or -1.
int SelectUnownedSpell()
{
	int candidates[4];
	int count = 0;
	for (int slot = 0; slot < 4; slot++)
		if (!Owned(SpellLights[slot]))
			candidates[count++] = slot;

	return count ? candidates[RandBelow(count)] : -1;
}

// 0x4236b0 -- CastSpell. The offensive/defensive split is slots {0,1} vs
// {2,3}, which is forced by Slay Dragon's arithmetic: it adds 5 s per lit
// v_lite31/v_lite32 (defensive) and removes one required hit per lit
// v_lite28/v_lite29 (offensive). The manual's grouping cannot be reconciled
// with that.
void CastSpell(int slot)
{
	SpellsCastThisBall++;  // 0x434d24, a drain-bonus term

	switch (slot)
	{
	case 0:
		// Dragon Breath. Timed, 30 s -- not the manual's 60. While lit, every
		// solo target adds 100,000 to the super jackpot (see MissionHandler).
		tag_v_lite28.Component->Message(ft::LightTurnOnTimed, 30.0f);
		break;
	case 1:
		// Lights its own light and its partner display light, and enables the
		// slow kickout a_kout3.
		tag_v_lite29.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_v_lite30.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_a_kout3.Component->ActiveFlag = 1;
		break;
	case 2:
		// Resistance to Fire. While lit, the drain bonus is carried to the
		// next ball instead of being cleared.
		tag_v_lite31.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		break;
	default:
		// Disables the a_blocf blocker for its configured time. a_blocf's own
		// handler drives v_lite32, which is why this branch lights nothing.
		MoatBlockerControl(ft::BlockerDisable, tag_a_blocf.Component);
		break;
	}

	AddScoreAt(nullptr, 120000);
	Play(tag_tabc35wave.Component);
	DisplayGlyph(tag_info_text_box.Component, GlyphSpell, 2.0f);

	// UNVERIFIED: slot 1 vs slot 3 <-> "Dragon Blood" vs "Stealth". The
	// mechanics of each slot are certain; only the manual names are guesses,
	// inferred from a_kout3 (a slow kickout, matching the manual's magnet)
	// against a_blocf (a blocker whose removal opens a path). Settled by
	// locating a_blocf and a_kout3 geometrically in DRAGON.DAT.
}

// 0x423780 -- drop every spell. Called on drain and at the end of Slay Dragon.
void ClearSpells()
{
	tag_v_lite28.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_v_lite29.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_v_lite30.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_a_kout3.Component->Message(ft::Reset, 0.0f);
	tag_v_lite31.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	MoatBlockerControl(ft::BlockerEnable, tag_a_blocf.Component);
}

// 0x4232b0 -- light `count` more dragon eggs, then score the *total*. The
// award is on-count x 100,000 after the increment, so eggs get better the more
// you already have.
void AwardDragonEggs(int count)
{
	for (int i = 0; i < count; i++)
		tag_dragon_egg_light_group.Component->Message(ft::GroupTurnOnNext, 1.0f);

	Play(tag_tabc27wave.Component);

	int lit = tag_dragon_egg_light_group.Component->Message(ft::GroupGetOnCount, 0.0f);
	DisplayGlyph(tag_info_text_box.Component, EggGlyphs[lit], 2.0f);
	AddScoreAt(nullptr, lit * 100000);
}

// 0x428a20 -- record one gem in the bitmask. All nine (0x1ff) awards an extra
// ball; the manual does not mention this.
void CollectGem(int bit)
{
	SetGemMask(GemMask() | bit);

	if (GemMask() == 0x1ff)
	{
		tag_gem_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		AwardExtraBall(2.0f);
		return;
	}

	Play(tag_tabc47wave.Component);
}

// 0x4238d0 -- set the sword/sword-target stage. Stage 3 is the fully drawn
// sword (new ball); stage -1 kills the lights (Slay Dragon has started, the
// sword is spent). The innermost target a_spot28 is what enables stage 3 ->
// Slay Dragon.
void SetSwordStage(int stage)
{
	switch (stage)
	{
	case 0:
		Play(tag_tabc13wave.Component);
		tag_a_sword.Component->Message(ft::MovingPartSetPosition, 0.0f);
		tag_a_spot19.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot26.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot28.Component->Message(ft::SoloTargetEnable, 0.0f);
		tag_v_lite16.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite17.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite18.Component->Message(ft::LightFlasherStartTimed, Forever);
		break;
	case 1:
		Play(tag_tabc13wave.Component);
		tag_a_sword.Component->Message(ft::MovingPartSetPosition, 0.0f);
		tag_a_spot19.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot26.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot28.Component->Message(ft::SoloTargetEnable, 0.0f);
		tag_v_lite16.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		tag_v_lite17.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		tag_v_lite18.Component->Message(ft::LightFlasherStartTimed, Forever);
		break;
	case 2:
		Play(tag_tabc13wave.Component);
		tag_a_sword.Component->Message(ft::MovingPartSetPosition, 1.0f);
		tag_a_spot19.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot26.Component->Message(ft::SoloTargetEnable, 0.0f);
		tag_a_spot28.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_v_lite16.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		tag_v_lite17.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite18.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		break;
	case 3:
		tag_a_sword.Component->Message(ft::BlockerEnable, 0.0f);
		tag_a_spot19.Component->Message(ft::SoloTargetEnable, 0.0f);
		tag_a_spot26.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot28.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_v_lite16.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite17.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite18.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		break;
	case -1:
		tag_a_sword.Component->Message(ft::MovingPartSetPosition, 0.0f);
		tag_a_spot19.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot26.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_a_spot28.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_v_lite16.Component->Message(ft::LightResetAndTurnOff, Forever);
		tag_v_lite17.Component->Message(ft::LightResetAndTurnOff, Forever);
		tag_v_lite18.Component->Message(ft::LightResetAndTurnOff, Forever);
		break;
	}
}

// ---------------------------------------------------------------------------
// Tower locks and Dragon Flurry
// ---------------------------------------------------------------------------

// 0x423b80 -- blank the three tower-lock lights and take both lockable sinks
// out of service for the duration of a quest, remembering nothing: the lights
// are *overridden* off, not turned off, so CheckTowerLocks can restore them.
void SuspendTowerLocks()
{
	tag_v_lite48.Component->Message(ft::LightOverrideOff, 0.0f);
	tag_v_lite49.Component->Message(ft::LightOverrideOff, 0.0f);
	tag_v_lite50.Component->Message(ft::LightOverrideOff, 0.0f);
	tag_a_sink18.Component->ActiveFlag = 0;
	tag_a_sink17.Component->ActiveFlag = 0;
	tag_v_lite48.Component->MessageField = 0;
	tag_v_lite49.Component->MessageField = 0;
	tag_v_lite50.Component->MessageField = 0;
}

// 0x423be0 -- drop the overrides and re-point the lockable sinks at whichever
// lock light is *flashing* (= armed). Three solid lock lights starts Tower
// Barrage.
void CheckTowerLocks()
{
	tag_v_lite48.Component->Message(ft::LightClearOverride, 0.0f);
	tag_v_lite49.Component->Message(ft::LightClearOverride, 0.0f);
	tag_v_lite50.Component->Message(ft::LightClearOverride, 0.0f);

	// FlasherOnFlag only: a flashing lock light is the one currently offered.
	struct { TLight* light; bool sink18; bool sink17; } const arm[3] = {
		{tag_v_lite48.Component, true, false},
		{tag_v_lite49.Component, false, true},
		{tag_v_lite50.Component, false, false},
	};

	for (int i = 0; i < 3; i++)
	{
		if (!arm[i].light->FlasherOnFlag)
			continue;
		tag_a_sink18.Component->ActiveFlag = arm[i].sink18;
		tag_a_sink17.Component->ActiveFlag = arm[i].sink17;
		tag_v_lite48.Component->MessageField = (i == 0);
		tag_v_lite49.Component->MessageField = (i == 1);
		tag_v_lite50.Component->MessageField = (i == 2);
		return;
	}

	tag_a_sink18.Component->ActiveFlag = 0;
	tag_a_sink17.Component->ActiveFlag = 0;
	tag_v_lite48.Component->MessageField = 0;
	tag_v_lite49.Component->MessageField = 0;
	tag_v_lite50.Component->MessageField = 0;

	if (Owned(tag_v_lite48.Component) && Owned(tag_v_lite49.Component) &&
	    Owned(tag_v_lite50.Component))
	{
		SetMode(Mode::TowerBarrage);
		MissionHandler(ft::MissionComplete, nullptr);
	}
}

// 0x4244e0 -- all six D-R-A-G-O-N letters lit starts Dragon Flurry. Returns
// whether it fired, because the callers chain it ahead of starting a quest.
bool CheckDragonFlurry()
{
	TLight* const letters[6] = {
		tag_v_lite22.Component, tag_v_lite23.Component, tag_v_lite24.Component,
		tag_v_lite25.Component, tag_v_lite26.Component, tag_v_lite27.Component,
	};
	for (int i = 0; i < 6; i++)
		if (!Owned(letters[i]))
			return false;

	SetMode(Mode::DragonFlurry);
	MissionHandler(ft::MissionComplete, nullptr);
	return true;
}

// ---------------------------------------------------------------------------
// Quest lifecycle
// ---------------------------------------------------------------------------

// 0x423e50 -- drop the flashing selection, if any.
void UnlightSelectedQuest()
{
	if (SelectedQuest() == Quest::None)
		return;
	QuestLights[SelectedQuestLightIndex()]->Message(ft::LightResetAndTurnOff, 0.0f);
	SetSelectedQuest(Quest::None);
}

// 0x423e90 -- SelectNextQuest, the counterpart of upstream's
// SelectMissionController. Upstream advances a rank and picks the mission that
// rank unlocks; Dragon picks *uniformly at random* among the Quest Lights that
// are not yet lit, with no ordering and no progression gate at all.
int SelectNextQuest()
{
	// A quest handed out to a draining ball would be unplayable and would
	// consume one of the five slots, so selection is simply refused.
	if (BallDraining)
		return -1;

	UnlightSelectedQuest();

	int lightIndex[5];
	Quest quest[5];
	int count = 0;
	for (int i = 0; i < 5; i++)
	{
		if (Owned(QuestLights[i]))
			continue;
		lightIndex[count] = i;
		quest[count] = static_cast<Quest>(static_cast<int>(Quest::DragonHoard) + i);
		count++;
	}

	if (count == 0)
	{
		// All five done: the sword is spent and the finale starts immediately,
		// with no selection step and nothing to confirm.
		SetSwordStage(-1);
		tag_v_lite76.Component->Message(ft::LightFlasherStartTimed, Forever);
		SetMode(Mode::SlayDragon);
		MissionHandler(ft::MissionComplete, nullptr);
		return static_cast<int>(Mode::SlayDragon);
	}

	int pick = RandBelow(count);
	SetSelectedQuestLightIndex(lightIndex[pick]);
	SetSelectedQuest(quest[pick]);
	// Flash indefinitely: the light stays flashing until the quest is started,
	// then goes solid when it is booked by FinishQuest.
	QuestLights[lightIndex[pick]]->Message(ft::LightFlasherStartTimed, Forever);
	return static_cast<int>(quest[pick]);
}

// 0x424210 -- StartSelectedQuest. Entered from the fire-lizard sinks, the
// well, and the egg exchange. Returns false if nothing is selected, which is
// what lets the egg exchange fall back to a cash award.
bool StartSelectedQuest()
{
	if (SelectedQuest() == Quest::None)
		return false;

	SetMode(static_cast<Mode>(SelectedQuest()));
	tag_v_lite46.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	// Hold the rock open for 2 s so the ball that triggered the start can get
	// clear before the quest re-points the diverters.
	tag_a_rockb.Component->Message(ft::BlockerEnable, 2.0f);
	MissionHandler(ft::MissionComplete, nullptr);
	return true;
}

// 0x424000 -- FinishQuest. Books the quest that just ran and picks the next.
//
// CAVEAT, unresolved: this is called from the *shared* 0x52 teardown of every
// quest, and 0x52 is reached on timer expiry as much as on success. So as far
// as the binary shows, a quest's Quest Light is lit -- and the quest counted
// for both progression and the 100,000/quest drain bonus -- whenever it is
// played to its end, won or lost. No success/failure discriminator exists
// anywhere in this path. Settled by instrumenting a real game: fail a quest on
// the clock and see whether its purple light lights.
void FinishQuest()
{
	if (SelectedQuest() != Quest::None)
	{
		AddToJackpot(500000);
		tag_v_lite28.Component->MessageField++;  // quests completed this game
		QuestLights[SelectedQuestLightIndex()]->Message(ft::LightResetAndTurnOn, 0.0f);
		SetSelectedQuest(Quest::None);
	}

	SelectNextQuest();
}

// 0x428580 -- the common "quest is now live" step, run from every quest's
// light-sweep-finished branch: hand the tower locks over to the quest and
// animate the letter bank.
void BeginQuestPlay()
{
	SuspendTowerLocks();
	tag_dragon_letter_light_group.Component->Message(ft::LightOverrideOff, 0.0f);
}

// 0x4285a0 -- the common teardown step, run from every quest's 0x52.
void EndQuestPlay()
{
	ClearText(tag_info_text_box.Component);
	tag_v_lite63.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_v_lite64.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_dragon_letter_light_group.Component->Message(ft::LightClearOverride, 0.0f);
	CheckTowerLocks();
}

// 0x4241c0 -- point the quest's objective sink at the ball. If an announcement
// is pending (the egg kickout is mid-prompt) the kickout is released instead,
// because a quest must not arm its target while a ball is held for a yes/no.
void ArmQuestTarget(TPinballComponent* target)
{
	if (KeepAnnouncement)
	{
		tag_s_hole118.Component->MessageField = 2;
		tag_s_hole118.Component->Message(ft::HoleReleaseBall, 0.0f);
		return;
	}
	target->Message(ft::SinkResetTimer, 0.0f);
}

// ---------------------------------------------------------------------------
// Mode 2 -- Idle
// ---------------------------------------------------------------------------

// 0x428800. The analogue of upstream's `UnselectMissionController` body, minus
// the rank display.
void IdleController(MessageCode code)
{
	if (code == ft::MissionComplete)
	{
		// One-shot: an announcement from the skill shot or the egg exchange
		// gets to stay on screen through the transition into Idle.
		if (!KeepAnnouncement)
			ClearText(tag_info_text_box.Component);
		KeepAnnouncement = false;

		if (midi::CurrentMusic() != idle_music)
			midi::PlayMusic(idle_music);
		return;
	}

	if (code == ft::MissionStarted)
		Display(tag_info_text_box.Component, GetRcString(STR_IDLE_HINT), Forever);
}

// 0x42c000 -- the default arm of the dispatcher. Anything the switch does not
// recognise is forced back to Idle, exactly as upstream's
// UnselectMissionController is the default mission.
void UnselectQuestController()
{
	SetMode(Mode::Idle);
	MissionHandler(ft::MissionComplete, nullptr);
}

// ---------------------------------------------------------------------------
// Mode 3 -- the ten-second restart window
//
// This mode has no upstream counterpart: Space Cadet missions end and are gone.
// Dragon keeps the quest that just ended in `mode_timer->MessageField` and
// gives the player ten seconds to put the ball back in a fire-lizard sink,
// which re-enters the quest with fifteen seconds on the clock.
// ---------------------------------------------------------------------------

// 0x428870.
void RestartWindowController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		if (caller != tag_mode_timer.Component)
			break;

		// The window closed unused. Tear the stored quest down for real.
		tag_v_lite63.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite64.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		MissionControl(EndedQuest(), ft::QuestTeardown, nullptr);
		SetEndedQuest(-1);

		// Re-read the mode: that teardown ran FinishQuest, which selects the
		// next quest and can itself jump the machine to Slay Dragon or a
		// multiball. Only fall back to Idle if nothing claimed the machine.
		if (CurrentMode() == Mode::RestartWindow)
		{
			SetMode(Mode::Idle);
			MissionHandler(ft::MissionComplete, nullptr);
		}
		break;

	case ft::MissionComplete:
		// Flash the two fire-lizard lights: those sinks are the restart shot.
		tag_v_lite63.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite64.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_mode_timer.Component->Message(ft::TimerSet, RestartWindowTime);
		// fall through -- show the countdown immediately on entry
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_RESTART_PROMPT);
		break;

	case ft::QuestAbort:
		// The ball drained during the window: no restart is possible, so the
		// stored quest is torn down now and the window is abandoned.
		MissionControl(EndedQuest(), ft::QuestTeardown, nullptr);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		SetEndedQuest(-1);
		SetMode(Mode::Idle);
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Mode 0xa -- skill shot
//
// The first component the plunged ball touches decides the award. Not a quest,
// but it lives in the same switch and it is one of the award paths.
// ---------------------------------------------------------------------------

// 0x4285f0.
void SkillShotController(MessageCode code, TPinballComponent* caller)
{
	// The five lettered awards and the consolation all end the same way, so
	// the binary shares one exit; keeping that shape makes the diff readable.
	bool consolation = false;

	switch (code)
	{
	case ft::BallCaptured:
		// The egg kickout swallowed the plunged ball: start the quest instead.
		if (caller == tag_s_hole118.Component)
			StartSelectedQuest();
		return;

	case ft::MissionComplete:
		KeepAnnouncement = true;
		// fall through
	case ft::MissionStarted:
		Display(tag_info_text_box.Component, GetRcString(3066 /* "Launch Ball" */), Forever);
		return;

	case ft::Collision:
		switch (caller->GetComponentType())
		{
		case type::SoloTarget:
			if (caller == tag_a_spot4.Component)
			{
				int slot = SelectUnownedSpell();
				if (slot != -1)
					CastSpell(slot);
			}
			else if (caller == tag_a_spot5.Component)
				AwardDragonEggs(1);
			else if (caller == tag_a_spot6.Component)
				AwardWeapon();
			else if (caller == tag_a_spot7.Component)
				AwardArmor();
			else if (caller == tag_a_spot22.Component)
			{
				SetMode(Mode::GargoyleGaggle);
				MissionHandler(ft::MissionComplete, nullptr);
				return;
			}
			else
			{
				consolation = true;
				break;
			}
			SetMode(Mode::Idle);
			MissionHandler(ft::MissionComplete, nullptr);
			return;

		// 1007 ends the skill shot with no award and no consolation.
		case 0x3ef:
			SetMode(Mode::Idle);
			MissionHandler(ft::MissionComplete, nullptr);
			MissionHandler(ft::MissionStarted, nullptr);
			return;

		case 1000: case type::PopupTarget: case 0x3f4: case 0x3f6: case 0x3f7:
		case type::Sink: case 0x3fa: case 0x3fc: case 0x3fd: case 0x400: case 0x406:
			consolation = true;
			break;

		default:
			return;
		}
		break;

	default:
		return;
	}

	// Anything else the ball can legally touch first is worth 15,000, but only
	// if the table is not tilt-locked.
	if (consolation && !pb::MainTable->TiltLockFlag)
		AddScoreAt(nullptr, 15000);

	SetMode(Mode::Idle);
	MissionHandler(ft::MissionComplete, nullptr);
	MissionHandler(ft::MissionStarted, nullptr);
}

// ---------------------------------------------------------------------------
// Mode 4 -- Dragon Hoard
//
// Collect gems. Unlike every other quest the objective is not one shot repeated
// but nine *distinct* shots, each identified by which component was hit, each
// with its own score, light and bit.
// ---------------------------------------------------------------------------

// 0x428a80.
void DragonHoardController(MessageCode code, TPinballComponent* caller)
{
	// One gem: light, unmultiplied score, name, bit.
	struct Gem { TLight* light; int score; int stringId; int bit; };
	auto collect = [&](const Gem& gem)
	{
		gem.light->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		AddScoreAt(caller, gem.score);
		char buffer[128];
		FormatWithValue(buffer, GetRcString(gem.stringId));
		Display(tag_info_text_box.Component, buffer, 2.0f);
		CollectGem(gem.bit);
	};

	switch (code)
	{
	case ft::TimerExpired:
		if (caller->GetComponentType() == type::LightSweeps)
		{
			// The intro sweep finished: only now does the clock start. Doing
			// it here rather than at MissionComplete is why the player is not
			// charged for the announcement.
			BeginQuestPlay();
			ArmQuestTarget(tag_a_sink1.Component);
			tag_mode_timer.Component->Message(ft::TimerSet, QuestTimeDragonHoard);
			MissionHandler(ft::MissionStarted, nullptr);
			return;
		}
		if (caller->GetComponentType() != type::Timer)
			return;
		if (caller == tag_mode_timer.Component)
		{
			// Out of time: remember which quest ended and open the window.
			SetEndedQuest(static_cast<int>(CurrentMode()));
			tag_gem_light_group.Component->Message(ft::LightOverrideOff, 0.0f);
			SetMode(Mode::RestartWindow);
			MissionHandler(ft::MissionComplete, nullptr);
			return;
		}
		break;

	case ft::Collision:
		switch (caller->GetComponentType())
		{
		case type::Bumper:
			collect({tag_v_lite88.Component, 10000, STR_GEM_GOLD, 0x001});
			return;

		case type::Oneway:
			// The dragon chute pays the running jackpot instead of a fixed score.
			if (caller == tag_s_oneway5.Component)
			{
				tag_v_lite95.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
				CollectJackpot(0.0f, true);
				CollectGem(0x040);
				return;
			}
			break;

		case type::Sink:
			if (caller == tag_a_sink1.Component || caller == tag_a_sink2.Component)
			{
				collect({tag_v_lite91.Component, 100000, STR_GEM_RUBY, 0x020});
				return;
			}
			break;

		case type::SoloTarget:
			if (caller == tag_a_spot4.Component || caller == tag_a_spot5.Component ||
			    caller == tag_a_spot6.Component || caller == tag_a_spot7.Component ||
			    caller == tag_a_spot22.Component)
			{
				collect({tag_v_lite89.Component, 50000, STR_GEM_DIAMOND, 0x002});
				return;
			}
			if (caller == tag_a_spot3.Component || caller == tag_a_spot2.Component ||
			    caller == tag_a_spot1.Component)
			{
				collect({tag_v_lite90.Component, 60000, STR_GEM_PEARL, 0x004});
				return;
			}
			if (caller == tag_a_spot10.Component || caller == tag_a_spot8.Component ||
			    caller == tag_a_spot14.Component || caller == tag_a_spot12.Component)
			{
				collect({tag_v_lite93.Component, 70000, STR_GEM_SAPPHIRE, 0x010});
				return;
			}
			break;

		case type::Accelerator:
			// The moat chute. This closes the manual's "ninth gem" gap: Opal.
			if (caller == tag_a_accl1.Component)
			{
				collect({tag_v_lite117.Component, 100000, STR_GEM_OPAL, 0x100});
				return;
			}
			break;

		default:
			break;
		}
		break;

	case ft::MissionComplete:
		tag_light_sweeps.Component->Message(ft::LightSweepStart, 0.0f);
		tag_gem_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		// Divert the ball toward the gem banks.
		tag_a_dvtrba.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrbb.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrca.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrcb.Component->Message(ft::GateDisable, 0.0f);
		AddScoreAt(nullptr, 100000);  // +100,000 just for starting a quest
		ClearText(tag_info_text_box.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_HOARD_HINT), Forever);
		SetGemMask(0);
		midi::PlayMusic(dragon_hoard_music);
		Play(tag_tabc46wave.Component);
		return;

	case ft::QuestRestart:
		Play(tag_tabc46wave.Component);
		tag_gem_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		tag_mode_timer.Component->Message(ft::TimerSet, RestartQuestTime);
		// If an announcement is still on screen, do not stomp it with the
		// countdown; the player gets to read the quest name first.
		if (AnnouncementPending(tag_info_text_box.Component))
			return;
		// fall through
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_HOARD_NAME);
		return;

	case ft::QuestAbort:
		SetMode(Mode::Idle);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		// fall through -- an abort is a teardown with the mode already reset
	case ft::QuestTeardown:
		EndQuestPlay();
		tag_gem_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_gem_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		FinishQuest();
		return;

	case ft::RampComplete:
		if (caller == tag_a_ramp103.Component)
		{
			collect({tag_v_lite94.Component, 250000, STR_GEM_AMETHYST, 0x080});
			return;
		}
		if (caller == tag_a_ramp6.Component)
		{
			collect({tag_v_lite92.Component, 200000, STR_GEM_EMERALD, 0x008});
			return;
		}
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// Mode 5 -- Fire Lizards
//
// Hit the two fire-lizard sinks repeatedly. The per-hit award escalates with
// the hit count and every fourth hit pays the jackpot. The counting itself
// lives in a_sink1/a_sink2's own handlers (0x425ea0 / 0x426000); this
// controller only owns the mode's lights and clock.
// ---------------------------------------------------------------------------

// 0x4291c0.
void FireLizardsController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		if (caller->GetComponentType() == type::LightSweeps)
		{
			BeginQuestPlay();
			ArmQuestTarget(tag_a_sink1.Component);
			tag_mode_timer.Component->Message(ft::TimerSet, QuestTimeFireLizards);
			MissionHandler(ft::MissionStarted, nullptr);
			return;
		}
		if (caller->GetComponentType() != type::Timer)
			return;
		if (caller == tag_mode_timer.Component)
		{
			SetEndedQuest(static_cast<int>(CurrentMode()));
			tag_v_lite66.Component->Message(ft::LightOverrideOff, 0.0f);
			tag_v_lite65.Component->Message(ft::LightOverrideOff, 0.0f);
			tag_v_lite91.Component->Message(ft::LightOverrideOff, 0.0f);
			SetMode(Mode::RestartWindow);
			MissionHandler(ft::MissionComplete, nullptr);
			return;
		}
		break;

	case ft::MissionComplete:
		tag_light_sweeps.Component->Message(ft::LightSweepStart, 1.0f);
		// v_lite91 doubles as the hit counter (capped at 10 by the sink
		// handler); a_sink2's field is the 0..3 jackpot sub-counter.
		tag_v_lite91.Component->MessageField = 0;
		tag_a_sink2.Component->MessageField = 0;
		AddScoreAt(nullptr, 100000);
		ClearText(tag_info_text_box.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_LIZARDS_HINT), Forever);
		tag_v_lite63.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite64.Component->Message(ft::LightFlasherStartTimed, Forever);
		Play(tag_tabc48wave.Component);
		midi::PlayMusic(fire_lizards_music);
		return;

	case ft::QuestRestart:
		tag_v_lite63.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite64.Component->Message(ft::LightFlasherStartTimed, Forever);
		Play(tag_tabc48wave.Component);
		tag_v_lite66.Component->Message(ft::LightClearOverride, 0.0f);
		tag_v_lite65.Component->Message(ft::LightClearOverride, 0.0f);
		tag_v_lite91.Component->Message(ft::LightClearOverride, 0.0f);
		tag_mode_timer.Component->Message(ft::TimerSet, RestartQuestTime);
		if (AnnouncementPending(tag_info_text_box.Component))
			return;
		// fall through
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_LIZARDS_NAME);
		return;

	case ft::QuestAbort:
		SetMode(Mode::Idle);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		// fall through
	case ft::QuestTeardown:
		EndQuestPlay();
		tag_v_lite63.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite64.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite66.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite66.Component->Message(ft::LightClearOverride, 0.0f);
		tag_v_lite65.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite65.Component->Message(ft::LightClearOverride, 0.0f);
		tag_v_lite91.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite91.Component->Message(ft::LightClearOverride, 0.0f);
		FinishQuest();
		return;

	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// Mode 6 -- Rescue Maiden
//
// The only quest with a real objective counter: put the ball in the prison
// tower until all four prison windows are lit. It is also the only quest that
// completes *successfully* mid-flight, skipping the restart window entirely.
// ---------------------------------------------------------------------------

// Rescue Maiden's per-hit award ladder.
//
// This is the `mode == 6` arm of Dragon_s_trip6_Control (VA 427880) -- the
// PRISON-WINDOW TRIP WIRE, not the tower sink. s_trip6/s_trip5 share that
// handler and own prison_window_light_group; the other two trip pairs
// (s_trip3/4, s_trip8/9) read v_lite49/v_lite50 against the watch and gate
// window groups instead. It lives here rather than in s_trip6Control because
// the rule belongs to the quest: the handler is a bare mode switch, and its
// other arms (idle, restart window, Tower Barrage, and the generic
// bank-completion flourish in its tail) have nothing to do with this ladder.
//
// The award is keyed on the on-count BEFORE the window is lit, so the ladder
// is per-window-won, and the window is lit last. This is the manual's ladder
// in full, which is worth stating plainly: it was previously written off as
// unrecoverable on the assumption that it lived in a_sink18's handler, which
// Ghidra renders as stack-byte noise. It does not live there and it is legible.
void RescueMaidenAwardWindow(TPinballComponent* caller)
{
	auto* windows = tag_prison_window_light_group.Component;
	Play(tag_tabc51wave.Component);

	// GetOnCount reads the lights' REAL state, which the sweep's overrides do
	// not disturb -- that is precisely why 0x18/0x19/0x1a have to be a
	// separate display layer and not plain turn-on/turn-off. The ladder keying
	// on this count is independent corroboration of those three names.
	char buffer[128];
	switch (static_cast<int>(windows->Message(ft::GroupGetOnCount, 0.0f)))
	{
	case 0:
		AddScoreAt(caller, 250000);
		FormatWithValue(buffer, GetRcString(STR_BARE_NUMBER));
		Display(tag_info_text_box.Component, buffer, 2.0f);
		break;
	case 1:
		AddScoreAt(caller, 500000);
		FormatWithValue(buffer, GetRcString(STR_BARE_NUMBER));
		Display(tag_info_text_box.Component, buffer, 2.0f);
		break;
	case 2:
		CollectJackpot(0.0f, true);
		break;
	default:
		// The fourth window: the extra-ball light, and the only site that says
		// so out loud.
		tag_v_lite57.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		Display(tag_info_text_box.Component, GetRcString(STR_MAIDEN_RESCUED), 2.0f);
		break;
	}

	// Light the window last, so the next hit sees the incremented count.
	windows->Message(ft::GroupTurnOnNext, 1.0f);

	// NOTE: lighting the fourth window here does not itself end the quest.
	// RescueMaidenController's Collision arm does, on the next a_sink18 entry
	// with all four lit -- so the ladder and the completion test are two ends
	// of the same objective, reached through different components.
}

// The 0x47 notify-timer sweep -- the same mechanism as the ladder above, seen
// from the other end. It walks a cursor around the four prison windows every
// 0.2 s and inverts whichever window it is on: a dark window is overridden ON,
// an already-won window is overridden OFF. That inversion is what keeps the
// travelling marker readable against the windows already won, and it is the
// evidence that fixes the three FT light-override codes.
void SweepPrisonWindows()
{
	auto* group = tag_prison_window_light_group.Component;
	group->Message(ft::GroupRestartNotifyTimer, 0.2f);

	int cursor = group->MessageField;
	if (TLight* leaving = group->LightAt(cursor))
		leaving->Message(ft::LightClearOverride, 0.0f);

	cursor = (cursor < 3) ? cursor + 1 : 0;
	group->MessageField = cursor;

	if (TLight* arriving = group->LightAt(cursor))
		arriving->Message(Owned(arriving) ? ft::LightOverrideOff : ft::LightOverrideOn, 0.0f);
}

// Put v_lite48 back the way Rescue Maiden found it. The quest borrows a
// tower-lock light for its own display, and the three states have to be
// restored distinctly or the tower-lock logic reads the wrong thing.
void RestoreBorrowedTowerLight()
{
	switch (SavedTowerLockState)
	{
	case 0:
		tag_v_lite48.Component->Message(ft::LightResetAndTurnOff, Forever);
		break;
	case 1:
		tag_v_lite48.Component->Message(ft::LightResetAndTurnOn, Forever);
		break;
	default:
		tag_v_lite48.Component->Message(ft::LightTurnOn, 0.0f);
		tag_v_lite48.Component->Message(ft::LightFlasherStartTimed, Forever);
		break;
	}
}

// 0x429510.
void RescueMaidenController(MessageCode code, TPinballComponent* caller)
{
	bool objectiveMet = false;

	switch (code)
	{
	case ft::TimerExpired:
		if (caller->GetComponentType() == type::LightSweeps)
		{
			BeginQuestPlay();
			tag_v_lite48.Component->Message(ft::LightFlasherStartTimed, Forever);
			tag_v_lite48.Component->Message(ft::LightClearOverride, 0.0f);
			tag_watch_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_gate_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_prison_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_prison_window_light_group.Component->Message(ft::GroupRestartNotifyTimer, 0.2f);
			ArmQuestTarget(tag_a_sink18.Component);
			tag_mode_timer.Component->Message(ft::TimerSet, QuestTimeRescueMaiden);
			MissionHandler(ft::MissionStarted, nullptr);
			return;
		}
		if (caller->GetComponentType() != type::Timer)
			return;
		if (caller == tag_mode_timer.Component)
		{
			tag_prison_window_light_group.Component->Message(ft::LightOverrideOff, 0.0f);
			tag_prison_window_light_group.Component->Message(ft::GroupRestartNotifyTimer, Forever);
			SetEndedQuest(static_cast<int>(CurrentMode()));
			SetMode(Mode::RestartWindow);
			MissionHandler(ft::MissionComplete, nullptr);
			return;
		}
		return;

	case ft::NotifyTimerExpired:
		if (caller == tag_prison_window_light_group.Component)
			SweepPrisonWindows();
		return;

	case ft::Collision:
		// Windows are won at the trip wire, not here. s_trip6Control owns that
		// call; routing it from this controller too would fire the ladder twice,
		// because Dragon's handler runs MissionControl before the component's own
		// ControlFunc. FUN_00429510 only detects the four-window completion below.
		if (caller != tag_a_sink18.Component)
			return;
		// Four windows lit = the maiden is out. Objective-met ends the quest
		// at once: no restart window, because there is nothing left to do.
		if (tag_prison_window_light_group.Component->Message(ft::GroupGetOnCount, 0.0f) != 4)
			return;
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		tag_prison_window_light_group.Component->Message(ft::GroupRestartNotifyTimer, Forever);
		objectiveMet = true;
		break;

	case ft::MissionComplete:
		// Remember v_lite48's exact state before borrowing it.
		if (tag_v_lite48.Component->FlasherOnFlag)
			SavedTowerLockState = 2;
		else
			SavedTowerLockState = (tag_v_lite48.Component->LightOnFlag ||
			                       tag_v_lite48.Component->ToggledOnFlag) ? 1 : 0;

		tag_light_sweeps.Component->Message(ft::LightSweepStart, 2.0f);
		tag_v_lite47.Component->Message(ft::LightResetAndTurnOn, 0.0f);
		// Route everything to the prison tower.
		tag_a_dvtraa.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrab.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrba.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrbb.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrca.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrcb.Component->Message(ft::GateEnable, 0.0f);
		tag_v_lite125.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_v_lite126.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_dvtrda.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrdb.Component->Message(ft::GateEnable, 0.0f);
		AddScoreAt(nullptr, 100000);
		ClearText(tag_info_text_box.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_MAIDEN_HINT), Forever);
		Play(tag_tabc50wave.Component);
		midi::PlayMusic(rescue_maiden_music);
		tag_v_lite48.Component->Message(ft::LightFlasherStartTimed, Forever);
		return;

	case ft::QuestRestart:
		Play(tag_tabc50wave.Component);
		tag_prison_window_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		tag_mode_timer.Component->Message(ft::TimerSet, RestartQuestTime);
		tag_prison_window_light_group.Component->Message(ft::GroupRestartNotifyTimer, 0.2f);
		if (AnnouncementPending(tag_info_text_box.Component))
			return;
		// fall through
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_MAIDEN_NAME);
		return;

	case ft::QuestAbort:
		SetMode(Mode::Idle);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		break;

	case ft::QuestTeardown:
		break;

	default:
		return;
	}

	// Shared teardown, reached from QuestTeardown, QuestAbort and the
	// objective-met collision.
	RestoreBorrowedTowerLight();
	EndQuestPlay();
	tag_prison_window_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_prison_window_light_group.Component->Message(ft::LightClearOverride, 0.0f);

	if (objectiveMet)
	{
		// Success returns straight to Idle rather than to the restart window.
		SetMode(Mode::Idle);
		MissionHandler(ft::MissionComplete, nullptr);
	}

	FinishQuest();

	// FinishQuest can re-select Rescue Maiden or kick into Tower Barrage; in
	// either case the diverters it just set must stay set, so the restore is
	// conditional on the mode not having been claimed.
	if (CurrentMode() != Mode::RescueMaiden && CurrentMode() != Mode::TowerBarrage)
	{
		tag_v_lite47.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_dvtraa.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrab.Component->Message(ft::GateDisable, 0.0f);

		tag_v_lite125.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_v_lite126.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_a_dvtrda.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrdb.Component->Message(ft::GateDisable, 0.0f);
	}

	// The per-hit ladder (250,000 / 500,000 / jackpot / extra-ball light) is in
	// RescueMaidenAwardWindow above, driven by the prison-window trip wire.
	// This controller owns only the clock, the window sweep and the four-light
	// completion test.
}

// ---------------------------------------------------------------------------
// Mode 7 -- Dragon Pass
//
// Force the two sliding rocks open and disable the targets in front of the
// kickouts, exposing them. The awards are in the kickouts' own handlers, keyed
// on the mode, which is why this controller scores nothing per hit.
// ---------------------------------------------------------------------------

// 0x429b10.
void DragonPassController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		if (caller->GetComponentType() == type::LightSweeps)
		{
			BeginQuestPlay();
			tag_v_lite14.Component->Message(ft::LightResetAndTurnOn, 0.0f);
			tag_gargoyle_spot_light_group.Component->Message(ft::GroupAnimationBackward, 0.1f);
			ArmQuestTarget(tag_a_sink18.Component);
			tag_mode_timer.Component->Message(ft::TimerSet, QuestTimeDragonPass);
			MissionHandler(ft::MissionStarted, nullptr);
			return;
		}
		if (caller->GetComponentType() != type::Timer)
			return;
		if (caller == tag_mode_timer.Component)
		{
			tag_v_lite46.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite45.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_gargoyle_spot_light_group.Component->Message(ft::LightOverrideOff, 0.0f);
			SetEndedQuest(static_cast<int>(CurrentMode()));
			SetMode(Mode::RestartWindow);
			MissionHandler(ft::MissionComplete, nullptr);
			return;
		}
		break;

	case ft::MissionComplete:
		tag_light_sweeps.Component->Message(ft::LightSweepStart, 3.0f);
		tag_gargoyle_spot_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		// Only flash the kickout light if we came from Idle: entering from the
		// egg exchange means StartSelectedQuest already turned it off.
		if (CurrentMode() == Mode::Idle)
			tag_v_lite46.Component->Message(ft::LightFlasherStartTimed, Forever);

		// Hold both rocks open and take the targets in front of the kickouts
		// out of play -- that is the whole shot geometry of this quest.
		tag_a_rockb.Component->Message(ft::BlockerDisable, 0.0f);
		tag_a_spot14.Component->Message(ft::SoloTargetDisable, 0.0f);
		tag_v_lite45.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_a_rocka.Component->Message(ft::BlockerDisable, 0.0f);
		tag_a_spot10.Component->Message(ft::SoloTargetDisable, 0.0f);

		AddScoreAt(nullptr, 100000);
		ClearText(tag_info_text_box.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_PASS_HINT), Forever);
		tag_v_lite46.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite45.Component->Message(ft::LightFlasherStartTimed, Forever);
		Play(tag_tabc53wave.Component);
		midi::PlayMusic(dragon_pass_music);
		return;

	case ft::QuestRestart:
		tag_v_lite46.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_v_lite45.Component->Message(ft::LightFlasherStartTimed, Forever);
		Play(tag_tabc53wave.Component);
		tag_gargoyle_spot_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		tag_mode_timer.Component->Message(ft::TimerSet, RestartQuestTime);
		if (AnnouncementPending(tag_info_text_box.Component))
			return;
		// fall through
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_PASS_NAME);
		return;

	case ft::QuestAbort:
		SetMode(Mode::Idle);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		// fall through
	case ft::QuestTeardown:
		EndQuestPlay();
		tag_gargoyle_spot_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_gargoyle_spot_light_group.Component->Message(ft::GroupReset, 0.0f);
		tag_gargoyle_spot_light_group.Component->Message(ft::LightClearOverride, 0.0f);
		tag_v_lite46.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_rockb.Component->Message(ft::BlockerEnable, 0.0f);
		tag_v_lite45.Component->Message(ft::LightResetAndTurnOff, 0.0f);
		tag_a_rocka.Component->Message(ft::BlockerEnable, 0.0f);
		FinishQuest();
		return;

	default:
		return;
	}

	// NOTE: a_spot10/a_spot14 are disabled on entry but never re-enabled here;
	// their own handlers must do it. UNVERIFIED -- check 0x4254a0 / 0x425570.
	//
	// UNVERIFIED: the manual's "gargoyle target: 100,000 + 250,000 to jackpot"
	// was not located anywhere. What is confirmed is that a_kout1 (armor
	// kickout) pays the jackpot and a_kout2 (gargoyle kickout) pays one dragon
	// egg, both gated on mode == 7 inside their own handlers.
}

// ---------------------------------------------------------------------------
// Mode 8 -- Wizard's Fury
//
// The thinnest controller: all it owns is the clock and two flashing light
// groups. The mystery award itself lives in a_sink4's handler, which pays
// whatever is armed in a_sink4->MessageField and then re-rolls it.
// ---------------------------------------------------------------------------

// 0x429ec0.
void WizardsFuryController(MessageCode code, TPinballComponent* caller)
{
	switch (code)
	{
	case ft::TimerExpired:
		if (caller->GetComponentType() == type::LightSweeps)
		{
			BeginQuestPlay();
			ArmQuestTarget(tag_a_sink4.Component);
			tag_mode_timer.Component->Message(ft::TimerSet, QuestTimeWizardsFury);
			MissionHandler(ft::MissionStarted, nullptr);
			return;
		}
		if (caller->GetComponentType() != type::Timer)
			return;
		if (caller == tag_mode_timer.Component)
		{
			tag_well_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_spell_spot_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			SetEndedQuest(static_cast<int>(CurrentMode()));
			tag_well_light_group.Component->Message(ft::LightOverrideOff, 0.0f);
			SetMode(Mode::RestartWindow);
			MissionHandler(ft::MissionComplete, nullptr);
			return;
		}
		break;

	case ft::MissionComplete:
		tag_light_sweeps.Component->Message(ft::LightSweepStart, 4.0f);
		AddScoreAt(nullptr, 100000);
		ClearText(tag_info_text_box.Component);
		Display(tag_info_text_box.Component, GetRcString(STR_FURY_HINT), Forever);
		tag_well_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_spell_spot_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		Play(tag_tabc55wave.Component);
		midi::PlayMusic(wizards_fury_music);
		return;

	case ft::QuestRestart:
		Play(tag_tabc55wave.Component);
		tag_well_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_spell_spot_light_group.Component->Message(ft::LightFlasherStartTimed, Forever);
		tag_mode_timer.Component->Message(ft::TimerSet, RestartQuestTime);
		if (AnnouncementPending(tag_info_text_box.Component))
			return;
		// fall through
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_FURY_NAME);
		return;

	case ft::QuestAbort:
		SetMode(Mode::Idle);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
		// fall through
	case ft::QuestTeardown:
		EndQuestPlay();
		FinishQuest();
		return;

	default:
		return;
	}

	// The mystery-award table, for the record (a_sink4->MessageField, paid and
	// re-rolled by Dragon_a_sink4_Control):
	//   0 extra ball | 1 max weapons (all 4 lights, multiplier 5) |
	//   2 max armor  | 3 jackpot     | 4 1,000,000
	// CONFIRMED disagreement with the manual: the award is re-rolled uniformly
	// by the well itself. The spell-target handler (0x424d20) has no mode == 8
	// case at all, so "hitting a spell target sets the mystery award" is simply
	// not implemented and the manual's fixed per-target mapping does not exist.
}

// ---------------------------------------------------------------------------
// Mode 9 -- Slay Dragon
//
// The finale, and the only mode whose difficulty depends on the collectibles:
// armor and defensive spells buy time, weapons and offensive spells remove
// required hits.
// ---------------------------------------------------------------------------

// 0x434cf8 -- Slay Dragon's own time limit in whole seconds, computed once at
// mode entry. Kept separate from the quest constants because it is not a
// constant: it is 30 + 5 x (armor + defensive spells), capped at 50 by there
// being only five such lights.
int SlayDragonTimeLimit = 30;

// 0x424130 -- arm the dragon: the mouth sink goes live, the fire sweep starts
// from scratch, and the jaw opens.
void ArmDragon()
{
	DragonArmed = true;
	tag_v_lite113.Component->ActiveFlag = 1;
	tag_v_lite114.Component->ActiveFlag = 1;
	tag_v_lite115.Component->ActiveFlag = 1;
	tag_v_lite119.Component->Message(ft::LightOverrideOn, 0.0f);
	tag_dragon_mouth_light_group.Component->Message(ft::LightClearOverride, 0.0f);
	tag_dragon_fire_light_group.Component->Message(ft::LightClearOverride, 0.0f);
	FireSweepDirection = 1;
	tag_v_lite113.Component->MessageField = -1;  // the sweep pre-increments
	tag_a_jaw.Component->Message(ft::MovingPartSetPosition, 7.0f);
}

// 0x424050 -- disarm. `slain` picks between hiding the dragon lights (the quest
// ran out) and clearing their overrides (the dragon is dead and the lights
// should show their real state).
void DisarmDragon(bool slain)
{
	DragonArmed = false;
	auto code = slain ? ft::LightClearOverride : ft::LightOverrideOff;
	tag_v_lite119.Component->Message(code, 0.0f);
	tag_dragon_mouth_light_group.Component->Message(code, 0.0f);
	tag_dragon_fire_light_group.Component->Message(code, 0.0f);

	tag_dragon_mouth_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_dragon_fire_light_group.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_dragon_fire_light_group.Component->Message(ft::GroupRestartNotifyTimer, Forever);
	tag_v_lite113.Component->ActiveFlag = 0;
	tag_v_lite114.Component->ActiveFlag = 0;
	tag_v_lite115.Component->ActiveFlag = 0;
	tag_s_bwal170.Component->ActiveFlag = 0;
	tag_a_ramp107.Component->ActiveFlag = 0;
	tag_a_jaw.Component->Message(ft::MovingPartSetPosition, 0.0f);
}

// The ping-pong sweep over the eight dragon-fire lights. The number lit when a
// mouth shot lands is what sets that shot's award, so the sweep is the timing
// element of the whole mode. Steps every 0.123 s, except cursor 6..8 which
// step at 0.012 s -- the tail of the sweep snaps shut.
void SweepDragonFire()
{
	auto* group = tag_dragon_fire_light_group.Component;
	int& cursor = tag_v_lite113.Component->MessageField;

	if (FireSweepDirection < 1)
	{
		cursor--;
		if (cursor == -1)
			FireSweepDirection = 1;  // bounce forward
		else
			DragonFireLights[cursor]->Message(ft::LightResetAndTurnOff, 0.0f);
	}
	else
	{
		cursor++;
		if (cursor == 0)
			Play(tag_tabc58wave.Component);
		if (cursor >= 0 && cursor <= 7)
			DragonFireLights[cursor]->Message(ft::LightResetAndTurnOn, 0.0f);
		else if (cursor == 8)
			FireSweepDirection = -1;  // bounce back
	}

	group->Message(ft::GroupRestartNotifyTimer,
	               (cursor > 5 && cursor < 9) ? 0.012f : 0.123f);
}

// The dragon-death animation, frame by frame, driven by the mouth group's
// notify timer. Frames 0..8 are glyphs; frame 9 is the payout.
void AnimateDragonDeath(TPinballComponent* caller)
{
	auto* group = tag_dragon_mouth_light_group.Component;
	int frame = group->MessageField++;

	if (frame < 0)
		return;

	if (frame < 9)
	{
		DisplayGlyph(tag_info_text_box.Component, GlyphDragonDeath[frame], Forever);
		group->Message(ft::GroupRestartNotifyTimer, 0.4f);
		return;
	}
	if (frame != 9)
		return;

	// The dragon is dead. Everything the table has to give, at once.
	pb::MainTable->LightGroup->Message(ft::LightClearOverride, 0.0f);
	DragonSpecialAddScore(caller, pb::MainTable->SuperJackpotScore);  // the whole super jackpot
	char buffer[128];
	FormatWithValue(buffer, GetRcString(STR_DRAGON_SLAIN));
	Display(tag_info_text_box.Component, buffer, Forever);
	AddScoreAt(caller, 1500000);
	CollectJackpot(0.0f, false);

	// All four weapons and the top multiplier.
	for (int i = 0; i < 4; i++)
		WeaponLights[i]->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	pb::MainTable->ScoreMultiplier = 5;

	// All three armor effects.
	ApplyShield();
	CenterPostControl(ft::BlockerEnable, tag_a_center_post.Component);
	RightOutlaneBlockerControl(ft::BlockerEnable, tag_a_bloce.Component);

	// All four spells, applied directly rather than through CastSpell so the
	// 120,000-per-spell score and the spells-cast counter are not touched.
	tag_v_lite28.Component->Message(ft::LightTurnOnTimed, 30.0f);
	tag_v_lite29.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	tag_v_lite30.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	tag_a_kout3.Component->ActiveFlag = 1;
	tag_v_lite31.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	MoatBlockerControl(ft::BlockerDisable, tag_a_blocf.Component);

	// Tear this mode down, release the locked ball, light every letter, and
	// hand over to Dragon Flurry.
	SlayDragonController(ft::QuestTeardown, nullptr);
	tag_a_sink16.Component->Message(ft::SinkResetTimer, Forever);
	tag_dragon_letter_light_group.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
	CheckDragonFlurry();

	// UNVERIFIED: the manual also promises "+ all gems" here. Nothing in this
	// path touches gem_light_group or v_lite88's bitmask. Settled by watching a
	// real dragon kill with a partial gem set.
}

// 0x42a150.
void SlayDragonController(MessageCode code, TPinballComponent* caller)
{
	bool abort = false;

	switch (code)
	{
	case ft::MissionComplete:
		SlayDragonIntro = true;
		tag_light_sweeps.Component->Message(ft::LightSweepStart, 5.0f);
		// The two lockable sinks are off the table for the finale.
		tag_a_sink18.Component->ActiveFlag = 0;
		tag_a_sink17.Component->ActiveFlag = 0;
		BallOnDragonRamp = false;

		// Time limit: 30 s base, 35 if the Shield is up (and the shield is
		// re-applied), +5 per Helm / Chain mail / each defensive spell.
		SlayDragonTimeLimit = 30;
		if (Owned(tag_v_lite37.Component))
		{
			SlayDragonTimeLimit = 35;
			ApplyShield();
			Play(tag_tabc24wave.Component);
		}
		if (Owned(tag_v_lite38.Component))
		{
			SlayDragonTimeLimit += 5;
			CenterPostControl(ft::BlockerEnable, tag_a_center_post.Component);
		}
		if (Owned(tag_v_lite39.Component))
		{
			SlayDragonTimeLimit += 5;
			RightOutlaneBlockerControl(ft::BlockerEnable, tag_a_bloce.Component);
		}
		if (Owned(tag_v_lite31.Component))
			SlayDragonTimeLimit += 5;
		if (Owned(tag_v_lite32.Component))
			SlayDragonTimeLimit += 5;

		// Hits required: 8, less one per weapon and per *offensive* spell.
		// This arithmetic is what proves the offensive/defensive split is
		// {v_lite28, v_lite29} vs {v_lite31, v_lite32}.
		SetDragonHitsLeft(8);
		for (int i = 0; i < 4; i++)
			if (Owned(WeaponLights[i]))
				SetDragonHitsLeft(DragonHitsLeft() - 1);
		if (Owned(tag_v_lite28.Component))
			SetDragonHitsLeft(DragonHitsLeft() - 1);
		if (Owned(tag_v_lite29.Component))
			SetDragonHitsLeft(DragonHitsLeft() - 1);

		// Diverters. Guarded for the same reason as Rescue Maiden's teardown:
		// do not fight a mode that has already claimed the routing.
		if (CurrentMode() != Mode::RescueMaiden && CurrentMode() != Mode::TowerBarrage)
		{
			tag_v_lite47.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_a_dvtraa.Component->Message(ft::GateEnable, 0.0f);
			tag_a_dvtrab.Component->Message(ft::GateDisable, 0.0f);
		}
		tag_a_dvtrba.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrbb.Component->Message(ft::GateEnable, 0.0f);
		tag_a_dvtrca.Component->Message(ft::GateDisable, 0.0f);
		tag_a_dvtrcb.Component->Message(ft::GateEnable, 0.0f);
		if (CurrentMode() != Mode::RescueMaiden && CurrentMode() != Mode::TowerBarrage)
		{
			tag_v_lite125.Component->Message(ft::LightResetAndTurnOff, 0.0f);
			tag_v_lite126.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
			tag_a_dvtrda.Component->Message(ft::GateEnable, 0.0f);
			tag_a_dvtrdb.Component->Message(ft::GateDisable, 0.0f);
		}

		// 500,000 to start, five times an ordinary quest's 100,000.
		AddScoreAt(nullptr, 500000);
		Display(tag_info_text_box.Component, GetRcString(STR_SLAY_HINT), Forever);
		// Spell slot 1 is granted outright for the finale.
		tag_v_lite29.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_v_lite30.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_a_kout3.Component->ActiveFlag = 1;
		Play(tag_tabc57wave.Component);
		return;

	case ft::TimerExpired:
		if (caller->GetComponentType() == type::LightSweeps)
		{
			SlayDragonIntro = false;
			BeginQuestPlay();
			ArmDragon();
			// If the ball is still in the launch lane the clock waits for it
			// to pass the one-way (see the Oneway case below) -- the player is
			// not charged for the plunge.
			if (shoot_again_flag != 3)
			{
				tag_mode_timer.Component->Message(ft::TimerSet,
				                                  static_cast<float>(SlayDragonTimeLimit));
				MissionHandler(ft::MissionStarted, nullptr);
			}
			return;
		}
		if (caller->GetComponentType() != type::Timer)
			return;
		if (caller != tag_mode_timer.Component)
			return;
		if (BallOnDragonRamp)
		{
			// Never kill the mode with the ball mid-ramp; look again in 50 ms.
			tag_mode_timer.Component->Message(ft::TimerSet, 0.05f);
			return;
		}
		SetEndedQuest(static_cast<int>(CurrentMode()));
		DisarmDragon(false);
		SetMode(Mode::RestartWindow);
		MissionHandler(ft::MissionComplete, nullptr);
		return;

	case ft::NotifyTimerExpired:
		if (caller == tag_dragon_fire_light_group.Component)
			SweepDragonFire();
		else if (caller == tag_dragon_mouth_light_group.Component)
			AnimateDragonDeath(caller);
		return;

	case ft::Collision:
		// Super-jackpot accrual, by what was hit. These are on top of the
		// global accrual in MissionHandler and are unique to this mode.
		switch (caller->GetComponentType())
		{
		case type::Bumper:
			AddToSuperJackpot(50000);
			return;
		case type::SoloTarget:
			AddToSuperJackpot(100000);
			return;
		case type::Kickout:
			AddToSuperJackpot(750000);
			return;

		case type::Oneway:
			// The deferred clock start: the ball has left the launch lane.
			if (SlayDragonIntro)
				return;
			if (caller != tag_s_oneway2.Component)
				return;
			if (shoot_again_flag != 3)
				return;
			tag_mode_timer.Component->Message(ft::TimerSet,
			                                  static_cast<float>(SlayDragonTimeLimit));
			MissionHandler(ft::MissionStarted, nullptr);
			return;

		case type::Sink:
			if (caller == tag_a_sink19.Component)
			{
				// The dragon's mouth. Only the *last* hit is handled here:
				// hits 2..8 decrement the counter, add 15 s to the clock and
				// pay out according to how many fire lights are lit, all
				// inside Dragon_a_sink19_Control @0x427f10.
				if (DragonHitsLeft() == 1)
				{
					pb::MainTable->LightGroup->Message(ft::LightOverrideOn, 0.0f);
					DisarmDragon(false);
					tag_mode_timer.Component->Message(ft::TimerSet, Forever);
					tag_dragon_mouth_light_group.Component->MessageField = 0;
					// ~0.1 ms: start the death animation on the very next tick.
					tag_dragon_mouth_light_group.Component->Message(
						ft::GroupRestartNotifyTimer, 0.0001f);
					ClearText(tag_info_text_box.Component);
				}
				BallOnDragonRamp = false;
				return;
			}
			if (caller == tag_a_sink1.Component || caller == tag_a_sink2.Component)
				AddToSuperJackpot(500000);
			return;

		default:
			return;
		}

	case ft::JawOpened:
		// The jaw finished opening: the dragon ramp and its back wall become
		// solid again and the fire sweep resumes.
		if (DragonArmed && caller == tag_a_jaw.Component)
		{
			tag_a_ramp107.Component->ActiveFlag = 1;
			tag_s_bwal170.Component->ActiveFlag = 1;
			tag_dragon_fire_light_group.Component->Message(ft::GroupRestartNotifyTimer, 0.123f);
		}
		return;

	case ft::QuestRestart:
		Play(tag_tabc57wave.Component);
		// Half the original limit, not the ordinary quests' flat 15 s -- this
		// is the one quest whose restart time scales with its difficulty.
		tag_mode_timer.Component->Message(ft::TimerSet, SlayDragonTimeLimit / 2.0f);
		tag_v_lite29.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_v_lite30.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
		tag_a_kout3.Component->ActiveFlag = 1;
		ArmDragon();
		if (AnnouncementPending(tag_info_text_box.Component))
			return;
		// fall through
	case ft::MissionStarted:
		DisplayTimeRemaining(STR_SLAY_NAME);
		return;

	case ft::RampEntered:
		if (caller == tag_a_ramp107.Component)
			BallOnDragonRamp = true;
		return;

	case ft::RampExited:
		if (caller == tag_a_ramp107.Component)
			BallOnDragonRamp = false;
		return;

	case ft::SetTiltLock:
		// A tilt during the finale forfeits the spells as well as the mode.
		ClearSpells();
		abort = true;
		break;

	case ft::QuestAbort:
		abort = true;
		break;

	case ft::QuestTeardown:
		break;

	default:
		return;
	}

	if (abort)
	{
		SetMode(Mode::Idle);
		tag_mode_timer.Component->Message(ft::TimerSet, Forever);
	}

	// Teardown. The sword is reset to its full stage and all five ordinary
	// Quest Lights are *cleared*, which is what makes the five quests
	// available again after the finale -- Slay Dragon is a loop point, not an
	// end state.
	SetSwordStage(3);
	for (int i = 0; i < 5; i++)
		QuestLights[i]->Message(ft::LightResetAndTurnOff, 0.0f);
	EndQuestPlay();
	DisarmDragon(true);
	// The three multi-mode lights ("2"/"3"/"5") go out with the finale.
	tag_v_lite77.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_v_lite78.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	tag_v_lite79.Component->Message(ft::LightResetAndTurnOff, 0.0f);
	// Slay Dragon's own light goes solid: the dragon has been faced.
	tag_v_lite76.Component->Message(ft::LightResetAndTurnOn, 0.0f);
	FinishQuest();
}

// ---------------------------------------------------------------------------
// Mode 0xc -- the dragon-egg exchange
//
// The egg kickout holds the ball and offers one award for the eggs collected.
// Left flipper is yes. No upstream counterpart at all.
// ---------------------------------------------------------------------------

// The offer the egg count buys, highest first. The offer *downgrades* when the
// award is already maxed out, which is why the 0x4f arm below is a cascade of
// fallthroughs rather than a table lookup: 8 eggs with no extra balls left
// becomes the 7-egg offer, and so on down to "begin the selected quest".
// 0x42ac80.
void EggExchangeController(MessageCode code, TPinballComponent* caller)
{
	if (code == ft::FlipperPressed)
	{
		SetMode(Mode::Idle);
		ClearText(tag_info_text_box.Component);

		if (caller == tag_a_flip1.Component)
		{
			// Yes.
			switch (EggOffer)
			{
			case 1: case 2: case 3:
				KeepAnnouncement = true;
				if (StartSelectedQuest())
				{
					// One egg spent, the rest kept -- a quest start is the
					// cheap offer and does not clear the track.
					tag_dragon_egg_light_group.Component->Message(ft::GroupTurnOffNext, 1.0f);
					return;
				}
				KeepAnnouncement = false;
				break;
			case 4:
				CastSpell(EggOfferSpellSlot);
				break;
			case 5:
				AwardWeapon();
				break;
			case 6:
				AwardArmor();
				break;
			case 7:
				Play(tag_tabc35wave.Component);
				tag_v_lite57.Component->Message(ft::LightFlashTimedThenStayOn, 1.0f);
				break;
			default:
				AwardExtraBall(2.0f);
				break;
			}

			// Spend the eggs: one light off per egg the offer cost.
			while (EggOffer-- != 0)
				tag_dragon_egg_light_group.Component->Message(ft::GroupTurnOffNext, 1.0f);

			tag_s_hole118.Component->MessageField = 1;
		}
		else
		{
			// No -- 25,000 consolation, eggs kept.
			AddScoreAt(nullptr, 25000);
			tag_s_hole118.Component->MessageField = 0;
		}

		tag_s_hole118.Component->Message(ft::HoleReleaseBall, 0.0f);
		KeepAnnouncement = true;
		MissionHandler(ft::MissionComplete, nullptr);
		return;
	}

	if (code == ft::MissionComplete)
	{
		tag_s_hole118.Component->MessageField = 0;
		ClearText(tag_info_text_box.Component);
		EggOffer = tag_dragon_egg_light_group.Component->Message(ft::GroupGetOnCount, 0.0f);

		// The downgrade cascade. Each case checks whether its award is already
		// maxed and, if so, drops to the next lower offer.
		switch (EggOffer)
		{
		case 8:
			if (pb::MainTable->ExtraBalls == 0)
				break;  // no extra ball pending -> keep the 8-egg offer
			EggOffer = 7;
			// fall through
		case 7:
			if (!Owned(tag_v_lite57.Component))
				break;
			EggOffer = 6;
			// fall through
		case 6:
			if (CountOwned(ArmorLights, 3) != 3)
				break;
			EggOffer = 5;
			// fall through
		case 5:
			if (CountOwned(WeaponLights, 4) != 4)
				break;
			EggOffer = 4;
			// fall through
		case 4:
			EggOfferSpellSlot = SelectUnownedSpell();
			if (EggOfferSpellSlot != -1)
				break;
			// fall through -- every spell owned
		case 1: case 2: case 3:
			EggOffer = 1;
			break;
		default:
			EggOffer = 0;
			break;
		}
	}
	else if (code != ft::MissionStarted)
	{
		return;
	}

	// Display the prompt for whatever offer survived the cascade.
	int stringId;
	switch (EggOffer)
	{
	case 0:
		// No eggs at all: pay 25,000 and let the ball go, no prompt.
		Play(tag_tabc32wave.Component);
		AddScoreAt(nullptr, 25000);
		tag_s_hole118.Component->MessageField = 0;
		Display(tag_info_text_box.Component, GetRcString(STR_NO_DRAGON_EGGS), 2.0f);
		tag_s_hole118.Component->Message(ft::HoleReleaseBall, 2.0f);
		KeepAnnouncement = true;
		SetMode(Mode::Idle);
		MissionHandler(ft::MissionComplete, nullptr);
		return;

	case 1: case 2: case 3:
	{
		// "<quest name>?" -- which quest depends on what is selected, and if
		// nothing is selected there is nothing to offer, so bail silently.
		Quest q = SelectedQuest();
		if (q == Quest::None)
			goto done;
		stringId = STR_EXCHANGE_QUEST_FIRST + (static_cast<int>(q) - static_cast<int>(Quest::DragonHoard));
		break;
	}
	case 4: stringId = STR_EXCHANGE_SPELL; break;
	case 5: stringId = STR_EXCHANGE_WEAPON; break;
	case 6: stringId = STR_EXCHANGE_ARMOR; break;
	case 7: stringId = STR_EXCHANGE_EB_LIGHT; break;
	default: stringId = STR_EXCHANGE_EXTRA_BALL; break;
	}

	Display(tag_info_text_box.Component, GetRcString(stringId), Forever);

done:
	// The voice-over only plays on entry, not on the re-display.
	if (code == ft::MissionComplete)
		Play(tag_tabc33wave.Component);

	// UNVERIFIED: the "flipper pressed" code is 1 here, compared against
	// a_flip1. 1 collides numerically with TLightTurnOn, so this is a private
	// per-component code reaching MissionControl rather than a public one.
	// Settled by finding who sends it (presumably the flipper's own handler).
}

// ---------------------------------------------------------------------------
// The dispatcher
// ---------------------------------------------------------------------------

// 0x42c120 -- MissionControl. One switch on the mode register, exactly like
// upstream's `control::MissionControl`, with two structural differences:
// Dragon's pre-switch work lives in MissionHandler below rather than inline,
// and there is no rank bookkeeping to do before the switch.
void MissionControl(Mode mode, MessageCode code, TPinballComponent* caller)
{
	switch (mode)
	{
	case Mode::GameOver:      GameOverController(code); break;
	case Mode::BetweenBalls:  break;  // deliberately deaf: balls are in flight
	case Mode::Idle:          IdleController(code); break;
	case Mode::RestartWindow: RestartWindowController(code, caller); break;
	case Mode::DragonHoard:   DragonHoardController(code, caller); break;
	case Mode::FireLizards:   FireLizardsController(code, caller); break;
	case Mode::RescueMaiden:  RescueMaidenController(code, caller); break;
	case Mode::DragonPass:    DragonPassController(code, caller); break;
	case Mode::WizardsFury:   WizardsFuryController(code, caller); break;
	case Mode::SlayDragon:    SlayDragonController(code, caller); break;
	case Mode::SkillShot:     SkillShotController(code, caller); break;
	case Mode::DrainBonus:    DrainBonusController(code, caller); break;
	case Mode::EggExchange:   EggExchangeController(code, caller); break;
	case Mode::TowerBarrage:  TowerBarrageController(code); break;
	case Mode::GargoyleGaggle: GargoyleGaggleController(code); break;
	case Mode::DragonFlurry:  DragonFlurryController(code); break;
	default:                  UnselectQuestController(); break;
	}
}

// 0x42c020 -- the tail of `control::handler`: the mode-independent scoring
// that every collision feeds, then the dispatch. Upstream keeps the equivalent
// pre-switch block inside MissionControl itself; Dragon splits it out because
// the accrual has to run for callers the mode switch will then ignore.
void MissionHandler(MessageCode code, TPinballComponent* caller)
{
	auto* box = tag_info_text_box.Component;
	if (!box)
		return;  // called before the table is built

	// A drain collision with no multiball running latches "the ball is gone",
	// which blocks quest selection until the next ball.
	if (caller && caller == tag_drain.Component && code == ft::Collision &&
	    pb::MainTable->MultiballCount == 0)
		BallDraining = true;

	if (caller)
	{
		// Dragon Breath (spell slot 0): while lit, every solo target pays
		// 100,000 into the super jackpot. The manual claims bumpers and
		// rebounds too; the binary gates strictly on TSoloTarget.
		if (Owned(tag_v_lite28.Component) && caller->GetComponentType() == type::SoloTarget)
			AddToSuperJackpot(100000);

		int toJackpot = 0;
		switch (caller->GetComponentType())
		{
		case type::PopupTarget:
		case type::SoloTarget:
			toJackpot = 50000;
			// The drain bonus's "per target dropped" term. Dragon has no drop
			// targets at all -- this counts collisions with these two types.
			TargetsHitThisBall++;
			break;
		case type::Sink:
			// Restricted to two sinks, not all of them as the manual implies.
			if (caller != tag_a_sink4.Component && caller != tag_a_sink1.Component)
				break;
			toJackpot = 75000;
			break;
		default:
			break;
		}
		if (toJackpot)
			AddToJackpot(toJackpot);
	}

	// Two codes mean "re-announce the current mode": an unpause, and a timer
	// expiry on the text box itself once its display timer has run out.
	if (code == ft::Resume ||
	    (code == ft::TimerExpired && caller == box && !AnnouncementPending(box)))
		code = ft::MissionStarted;

	MissionControl(CurrentMode(), code, caller);
}

}  // namespace dragon
