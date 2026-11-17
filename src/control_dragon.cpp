// control_dragon.cpp -- rules for Full Tilt! Pinball's Dragon table.
//
// GENERATED SKELETON (gen_control.py). The score arrays, tags and the component
// table below are recovered from DRAGON.EXE and are believed exact.
// The handler bodies are NOT: each is a stub marked TODO, annotated with its
// address in the original binary and the Space Cadet handler it most resembles.
//
// 75 components, 64 distinct handlers.

#include "pch.h"
#include "control.h"

#include "pb.h"
#include "TPinballTable.h"

// The rules declare some tags with their concrete type, and component_tag<T>
// instantiates std::is_base_of<TPinballComponent, T>, which needs T complete.
#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TSound.h"
#include "TTextBox.h"

namespace dragon {

// Score tables, read out of .data.
int control_a_bump1_scores[1] = {1500};
int control_a_rebo1_scores[1] = {1000};
int control_a_spot4_scores[1] = {2500};
int control_a_spot3_scores[2] = {2000, 10000};
int control_a_spot10_scores[2] = {3000, 15000};
int control_a_spot27_scores[1] = {5000};
int control_a_spot19_scores[1] = {20000};
int control_a_spot26_scores[1] = {50000};
int control_s_oneway5_scores[5] = {25000, 75000, 100000, 125000, 150000};
int control_a_accl1_scores[2] = {75000, 500000};
int control_s_trip1_scores[4] = {20000, 50000, 100000, 1000000};
int control_a_roll7_scores[1] = {15000};
int control_a_roll5_scores[1] = {30000};
int control_a_roll6_scores[1] = {10000};
int control_a_sink15_scores[1] = {100000};
int control_a_sink13_scores[1] = {200000};
int control_a_sink9_scores[1] = {300000};
int control_s_trip6_scores[4] = {25000, 50000, 75000, 100000};
int control_s_trip3_scores[3] = {40000, 80000, 120000};
int control_s_trip8_scores[2] = {50000, 100000};
// Component tags. Names are the group names stored in the DAT. Both the
// scoring table and simple_components below bind through these.
extern component_tag<TPinballComponent> tag_drain;
component_tag<TPinballComponent> tag_s_plunger1 = {"s_plunger1"};
extern component_tag<TPinballComponent> tag_s_oneway2;
extern component_tag<TLight> tag_v_lite40;
component_tag<TPinballComponent> tag_a_bump1 = {"a_bump1"};
component_tag<TPinballComponent> tag_a_bump2 = {"a_bump2"};
component_tag<TPinballComponent> tag_a_bump3 = {"a_bump3"};
component_tag<TPinballComponent> tag_a_bump4 = {"a_bump4"};
component_tag<TPinballComponent> tag_a_rebo1 = {"a_rebo1"};
component_tag<TPinballComponent> tag_a_rebo2 = {"a_rebo2"};
extern component_tag<TPinballComponent> tag_a_spot4;
extern component_tag<TPinballComponent> tag_a_spot5;
extern component_tag<TPinballComponent> tag_a_spot6;
extern component_tag<TPinballComponent> tag_a_spot7;
extern component_tag<TPinballComponent> tag_a_spot22;
extern component_tag<TPinballComponent> tag_a_spot3;
extern component_tag<TPinballComponent> tag_a_spot2;
extern component_tag<TPinballComponent> tag_a_spot1;
extern component_tag<TPinballComponent> tag_a_spot10;
extern component_tag<TPinballComponent> tag_a_spot8;
extern component_tag<TPinballComponent> tag_a_spot14;
extern component_tag<TPinballComponent> tag_a_spot12;
component_tag<TPinballComponent> tag_a_spot27 = {"a_spot27"};
extern component_tag<TPinballComponent> tag_a_spot19;
extern component_tag<TPinballComponent> tag_a_spot26;
extern component_tag<TPinballComponent> tag_a_spot28;
component_tag<TPinballComponent> tag_a_spot30 = {"a_spot30"};
component_tag<TPinballComponent> tag_a_spot31 = {"a_spot31"};
extern component_tag<TPinballComponent> tag_a_center_post;
extern component_tag<TPinballComponent> tag_a_sink1;
extern component_tag<TPinballComponent> tag_a_sink2;
extern component_tag<TPinballComponent> tag_a_sink4;
extern component_tag<TPinballComponent> tag_a_ramp103;
extern component_tag<TPinballComponent> tag_a_ramp6;
extern component_tag<TPinballComponent> tag_s_oneway5;
extern component_tag<TPinballComponent> tag_a_accl1;
component_tag<TPinballComponent> tag_s_trip1 = {"s_trip1"};
component_tag<TPinballComponent> tag_s_trip2 = {"s_trip2"};
extern component_tag<TLight> tag_v_lite69;
component_tag<TPinballComponent> tag_a_roll7 = {"a_roll7"};
component_tag<TPinballComponent> tag_a_roll5 = {"a_roll5"};
component_tag<TPinballComponent> tag_a_roll2 = {"a_roll2"};
component_tag<TPinballComponent> tag_a_roll6 = {"a_roll6"};
component_tag<TPinballComponent> tag_a_roll1 = {"a_roll1"};
component_tag<TPinballComponent> tag_s_rexit19 = {"s_rexit19"};
component_tag<TPinballComponent> tag_s_rexit18 = {"s_rexit18"};
component_tag<TPinballComponent> tag_a_flip2 = {"a_flip2"};
extern component_tag<TPinballComponent> tag_a_kick1;
component_tag<TPinballComponent> tag_a_kout1 = {"a_kout1"};
extern component_tag<TPinballComponent> tag_a_kout2;
extern component_tag<TPinballComponent> tag_s_hole118;
component_tag<TPinballComponent> tag_a_sink15 = {"a_sink15"};
extern component_tag<TPinballComponent> tag_a_sink18;
component_tag<TPinballComponent> tag_a_sink13 = {"a_sink13"};
extern component_tag<TPinballComponent> tag_a_sink17;
component_tag<TPinballComponent> tag_a_sink9 = {"a_sink9"};
component_tag<TPinballComponent> tag_a_sink10 = {"a_sink10"};
component_tag<TPinballComponent> tag_v_wlite96 = {"v_wlite96"};
component_tag<TPinballComponent> tag_v_wlite97 = {"v_wlite97"};
component_tag<TPinballComponent> tag_v_wlite98 = {"v_wlite98"};
component_tag<TPinballComponent> tag_v_wlite99 = {"v_wlite99"};
extern component_tag<TPinballComponent> tag_s_trip6;
extern component_tag<TPinballComponent> tag_s_trip5;
component_tag<TPinballComponent> tag_s_trip3 = {"s_trip3"};
component_tag<TPinballComponent> tag_s_trip4 = {"s_trip4"};
component_tag<TPinballComponent> tag_s_trip8 = {"s_trip8"};
component_tag<TPinballComponent> tag_s_trip9 = {"s_trip9"};
extern component_tag<TPinballComponent> tag_a_bloce;
extern component_tag<TPinballComponent> tag_a_blocf;
extern component_tag<TPinballComponent> tag_a_kout3;
extern component_tag<TPinballComponent> tag_a_sink19;
extern component_tag<TPinballComponent> tag_a_rocka;
extern component_tag<TPinballComponent> tag_a_rockb;
extern component_tag<TLight> tag_v_lite37;
extern component_tag<TLight> tag_v_lite29;
extern component_tag<TLight> tag_v_lite38;
extern component_tag<TLight> tag_v_lite39;
extern component_tag<TPinballComponent> tag_a_dvtraa;
extern component_tag<TPinballComponent> tag_a_dvtrab;
extern component_tag<TPinballComponent> tag_a_dvtrba;
extern component_tag<TPinballComponent> tag_a_dvtrbb;
extern component_tag<TPinballComponent> tag_a_dvtrca;
extern component_tag<TPinballComponent> tag_a_dvtrcb;
extern component_tag<TPinballComponent> tag_a_dvtrda;
extern component_tag<TPinballComponent> tag_a_dvtrdb;
extern component_tag<TLight> tag_v_lite47;
extern component_tag<TTextBox> tag_info_text_box;
extern component_tag<TLight> tag_v_lite22;
extern component_tag<TLight> tag_v_lite23;
extern component_tag<TLight> tag_v_lite24;
extern component_tag<TLight> tag_v_lite25;
extern component_tag<TLight> tag_v_lite26;
extern component_tag<TLight> tag_v_lite27;
extern component_tag<TLight> tag_v_lite3;
extern component_tag<TLight> tag_v_lite4;
extern component_tag<TLight> tag_v_lite5;
extern component_tag<TLight> tag_v_lite6;
extern component_tag<TLight> tag_v_lite7;
extern component_tag<TLightGroup> tag_spell_spot_light_group;
extern component_tag<TLight> tag_v_lite8;
extern component_tag<TLight> tag_v_lite9;
extern component_tag<TLight> tag_v_lite10;
extern component_tag<TLight> tag_v_lite14;
extern component_tag<TLight> tag_v_lite13;
extern component_tag<TLight> tag_v_lite12;
extern component_tag<TLight> tag_v_lite11;
extern component_tag<TLightGroup> tag_gargoyle_spot_light_group;
extern component_tag<TLight> tag_v_lite21;
extern component_tag<TLight> tag_v_lite57;
extern component_tag<TLight> tag_v_lite56;
extern component_tag<TLight> tag_v_lite55;
extern component_tag<TLight> tag_v_lite16;
extern component_tag<TLight> tag_v_lite17;
extern component_tag<TLight> tag_v_lite18;
extern component_tag<TPinballComponent> tag_a_sword;
extern component_tag<TLightGroup> tag_dragon_fire_light_group;
extern component_tag<TLight> tag_v_lite119;
extern component_tag<TLight> tag_v_lite20;
extern component_tag<TLight> tag_v_lite19;
extern component_tag<TLight> tag_v_lite63;
extern component_tag<TLight> tag_v_lite64;
extern component_tag<TLight> tag_v_lite66;
extern component_tag<TLight> tag_v_lite65;
extern component_tag<TSound> tag_tabc16wave;
extern component_tag<TLightGroup> tag_well_light_group;
extern component_tag<TLight> tag_v_lite67;
extern component_tag<TLight> tag_v_lite70;
extern component_tag<TLight> tag_v_lite68;
extern component_tag<TLightGroup> tag_outlane_kicker_light_group;
extern component_tag<TLightGroup> tag_dragon_egg_light_group;
extern component_tag<TLightGroup> tag_dragon_letter_light_group;
extern component_tag<TLight> tag_v_lite45;
extern component_tag<TLight> tag_v_lite46;
extern component_tag<TPinballComponent> tag_a_ramp107;
extern component_tag<TPinballComponent> tag_s_bwal170;
extern component_tag<TLight> tag_v_lite33;
extern component_tag<TLight> tag_v_lite34;
extern component_tag<TLight> tag_v_lite35;
extern component_tag<TLight> tag_v_lite36;
extern component_tag<TPinballComponent> tag_a_sink16;
extern component_tag<TLight> tag_v_lite48;
extern component_tag<TLight> tag_v_lite49;
extern component_tag<TLight> tag_v_lite50;
extern component_tag<TLightGroup> tag_prison_window_light_group;
extern component_tag<TLightGroup> tag_watch_window_light_group;
extern component_tag<TLightGroup> tag_gate_window_light_group;
extern component_tag<TLight> tag_v_lite28;
extern component_tag<TLight> tag_v_lite30;
extern component_tag<TLight> tag_v_lite31;
extern component_tag<TLight> tag_v_lite32;
extern component_tag<TPinballComponent> tag_light_sweeps;
extern component_tag<TLight> tag_v_lite88;
extern component_tag<TLight> tag_v_lite89;
extern component_tag<TLight> tag_v_lite90;
extern component_tag<TLight> tag_v_lite91;
extern component_tag<TLight> tag_v_lite92;
extern component_tag<TLight> tag_v_lite93;
extern component_tag<TLight> tag_v_lite94;
extern component_tag<TLight> tag_v_lite95;
extern component_tag<TLight> tag_v_lite117;
extern component_tag<TPinballComponent> tag_mode_timer;
extern component_tag<TLightGroup> tag_gem_light_group;
extern component_tag<TLight> tag_v_lite120;
extern component_tag<TLight> tag_v_lite121;
extern component_tag<TLight> tag_v_lite113;
extern component_tag<TLight> tag_v_lite114;
extern component_tag<TLight> tag_v_lite115;
extern component_tag<TLightGroup> tag_dragon_mouth_light_group;
extern component_tag<TPinballComponent> tag_a_jaw;
extern component_tag<TLight> tag_v_lite71;
extern component_tag<TLight> tag_v_lite72;
extern component_tag<TLight> tag_v_lite73;
extern component_tag<TLight> tag_v_lite74;
extern component_tag<TLight> tag_v_lite75;
extern component_tag<TLight> tag_v_lite76;
component_tag<TPinballComponent> tag_v_lite58 = {"v_lite58"};
component_tag<TPinballComponent> tag_v_lite59 = {"v_lite59"};
component_tag<TPinballComponent> tag_v_lite60 = {"v_lite60"};
component_tag<TPinballComponent> tag_v_lite61 = {"v_lite61"};
component_tag<TPinballComponent> tag_v_lite62 = {"v_lite62"};
extern component_tag<TSound> tag_tabc04wave;
extern component_tag<TSound> tag_tabc10wave;
extern component_tag<TSound> tag_tabc07wave;
extern component_tag<TSound> tag_tabc51wave;
extern component_tag<TSound> tag_tabc13wave;
extern component_tag<TSound> tag_tabc18wave;
extern component_tag<TSound> tag_tabc21wave;
extern component_tag<TSound> tag_tabc24wave;
extern component_tag<TSound> tag_tabc27wave;
extern component_tag<TSound> tag_tabc32wave;
extern component_tag<TSound> tag_tabc33wave;
extern component_tag<TSound> tag_tabc34wave;
extern component_tag<TSound> tag_tabc35wave;
extern component_tag<TSound> tag_tabc39wave;
extern component_tag<TSound> tag_tabc40wave;
extern component_tag<TSound> tag_tabc41wave;
extern component_tag<TSound> tag_tabc42wave;
extern component_tag<TSound> tag_tabc43wave;
component_tag<TPinballComponent> tag_tabc44wave = {"tabc44wave"};
extern component_tag<TSound> tag_tabc46wave;
extern component_tag<TSound> tag_tabc47wave;
extern component_tag<TSound> tag_tabc48wave;
extern component_tag<TSound> tag_tabc50wave;
extern component_tag<TSound> tag_tabc53wave;
extern component_tag<TSound> tag_tabc55wave;
extern component_tag<TSound> tag_tabc56wave;
extern component_tag<TSound> tag_tabc57wave;
extern component_tag<TSound> tag_tabc58wave;
component_tag<TPinballComponent> tag_tabc61wave = {"tabc61wave"};
extern component_tag<TSound> tag_tabc03wave;
extern component_tag<TPinballComponent> tag_a_flip1;
component_tag<TPinballComponent> tag_a_flip3 = {"a_flip3"};
extern component_tag<TLight> tag_v_lite77;
extern component_tag<TLight> tag_v_lite78;
extern component_tag<TLight> tag_v_lite79;
extern component_tag<TSound> tag_tabc96wave;
extern component_tag<TLight> tag_v_lite125;
extern component_tag<TLight> tag_v_lite126;
extern component_tag<TSound> tag_tabc05wave;
extern component_tag<TSound> tag_tabc06wave;
extern component_tag<TSound> tag_tabc08wave;
extern component_tag<TSound> tag_tabc20wave;
extern component_tag<TSound> tag_tabc22wave;
extern component_tag<TSound> tag_tabc09wave;
extern component_tag<TSound> tag_tabc35wave;

// Handlers implemented in the hand-written rule files.
void BallDrainControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void PlungerControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void LaunchLaneOneWayControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void ShootAgainLightControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void ForestBumperControl(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void FlipperRebounderControl1(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void FlipperRebounderControl2(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SpellTargetControl4(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SpellTargetControl5(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SpellTargetControl6(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SpellTargetControl7(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SpellTargetControl22(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void TowerTargetControl3(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void TowerTargetControl2(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void TowerTargetControl1(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void GargoyleRockTargetAControl(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void GargoyleTargetControl8(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void GargoyleRockTargetBControl(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void GargoyleTargetControl12(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void QuestSelectTargetControl(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SwordTargetControl1(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SwordTargetControl2(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void SwordTargetControl3(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void RicochetTargetControl1(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void RicochetTargetControl2(MessageCode code, TPinballComponent* caller); // dragon_part1.cpp
void CenterPostControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void FireLizardSink1Control(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void FireLizardSink2Control(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void WellSinkControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void DragonRampControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void TowerRampControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void DragonChuteControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void MoatChuteAcceleratorControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void FireRidgeOrbitControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void KickerRearmRolloverControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void Rollover5Control(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void Rollover2Control(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void ReturnLaneRollover6Control(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void ReturnLaneRollover1Control(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void GargoyleRampExitControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void ForestRampExitControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void RightFlipperControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void OutlaneKickerControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void ArmorKickoutControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void GargoyleKickoutControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void QuestHoleControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void TowerLock1EntranceControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void TowerLock1SinkControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void TowerLock2EntranceControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void TowerLock2SinkControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void TowerLock3EntranceAControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void TowerLock3EntranceBControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void WallLightControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void PrisonWindowTripControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void WatchWindowTripControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void GateWindowTripControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void RightOutlaneBlockerControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void MoatBlockerControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void DragonBloodKickoutControl(MessageCode code, TPinballComponent* caller); // dragon_part4.cpp
void DragonMouthSinkControl(MessageCode code, TPinballComponent* caller); // dragon_part3.cpp
void GargoyleRockAControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void GargoyleRockBControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void ShieldLightControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp
void DragonBloodLightControl(MessageCode code, TPinballComponent* caller); // dragon_part2.cpp

// Handlers still to reverse engineer.

} // namespace dragon


component_info dragon_score_components[75]
{
	component_info{dragon::tag_drain, {dragon::BallDrainControl, 0, nullptr}},
	component_info{dragon::tag_s_plunger1, {dragon::PlungerControl, 0, nullptr}},
	component_info{dragon::tag_s_oneway2, {dragon::LaunchLaneOneWayControl, 0, nullptr}},
	component_info{dragon::tag_v_lite40, {dragon::ShootAgainLightControl, 0, nullptr}},
	component_info{dragon::tag_a_bump1, {dragon::ForestBumperControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_a_bump2, {dragon::ForestBumperControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_a_bump3, {dragon::ForestBumperControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_a_bump4, {dragon::ForestBumperControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_a_rebo1, {dragon::FlipperRebounderControl1, 1, dragon::control_a_rebo1_scores}},
	component_info{dragon::tag_a_rebo2, {dragon::FlipperRebounderControl2, 1, dragon::control_a_rebo1_scores}},
	component_info{dragon::tag_a_spot4, {dragon::SpellTargetControl4, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_spot5, {dragon::SpellTargetControl5, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_spot6, {dragon::SpellTargetControl6, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_spot7, {dragon::SpellTargetControl7, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_spot22, {dragon::SpellTargetControl22, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_spot3, {dragon::TowerTargetControl3, 2, dragon::control_a_spot3_scores}},
	component_info{dragon::tag_a_spot2, {dragon::TowerTargetControl2, 2, dragon::control_a_spot3_scores}},
	component_info{dragon::tag_a_spot1, {dragon::TowerTargetControl1, 2, dragon::control_a_spot3_scores}},
	component_info{dragon::tag_a_spot10, {dragon::GargoyleRockTargetAControl, 2, dragon::control_a_spot10_scores}},
	component_info{dragon::tag_a_spot8, {dragon::GargoyleTargetControl8, 2, dragon::control_a_spot10_scores}},
	component_info{dragon::tag_a_spot14, {dragon::GargoyleRockTargetBControl, 2, dragon::control_a_spot10_scores}},
	component_info{dragon::tag_a_spot12, {dragon::GargoyleTargetControl12, 2, dragon::control_a_spot10_scores}},
	component_info{dragon::tag_a_spot27, {dragon::QuestSelectTargetControl, 1, dragon::control_a_spot27_scores}},
	component_info{dragon::tag_a_spot19, {dragon::SwordTargetControl1, 1, dragon::control_a_spot19_scores}},
	component_info{dragon::tag_a_spot26, {dragon::SwordTargetControl2, 1, dragon::control_a_spot26_scores}},
	component_info{dragon::tag_a_spot28, {dragon::SwordTargetControl3, 0, nullptr}},
	component_info{dragon::tag_a_spot30, {dragon::RicochetTargetControl1, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_spot31, {dragon::RicochetTargetControl2, 1, dragon::control_a_spot4_scores}},
	component_info{dragon::tag_a_center_post, {dragon::CenterPostControl, 0, nullptr}},
	component_info{dragon::tag_a_sink1, {dragon::FireLizardSink1Control, 1, dragon::control_a_spot27_scores}},
	component_info{dragon::tag_a_sink2, {dragon::FireLizardSink2Control, 1, dragon::control_a_spot27_scores}},
	component_info{dragon::tag_a_sink4, {dragon::WellSinkControl, 0, nullptr}},
	component_info{dragon::tag_a_ramp103, {dragon::DragonRampControl, 0, nullptr}},
	component_info{dragon::tag_a_ramp6, {dragon::TowerRampControl, 0, nullptr}},
	component_info{dragon::tag_s_oneway5, {dragon::DragonChuteControl, 5, dragon::control_s_oneway5_scores}},
	component_info{dragon::tag_a_accl1, {dragon::MoatChuteAcceleratorControl, 2, dragon::control_a_accl1_scores}},
	component_info{dragon::tag_s_trip1, {dragon::FireRidgeOrbitControl, 4, dragon::control_s_trip1_scores}},
	component_info{dragon::tag_s_trip2, {dragon::FireRidgeOrbitControl, 4, dragon::control_s_trip1_scores}},
	component_info{dragon::tag_v_lite69, {dragon::FireRidgeOrbitControl, 0, nullptr}},
	component_info{dragon::tag_a_roll7, {dragon::KickerRearmRolloverControl, 1, dragon::control_a_roll7_scores}},
	component_info{dragon::tag_a_roll5, {dragon::Rollover5Control, 1, dragon::control_a_roll5_scores}},
	component_info{dragon::tag_a_roll2, {dragon::Rollover2Control, 1, dragon::control_a_roll5_scores}},
	component_info{dragon::tag_a_roll6, {dragon::ReturnLaneRollover6Control, 1, dragon::control_a_roll6_scores}},
	component_info{dragon::tag_a_roll1, {dragon::ReturnLaneRollover1Control, 1, dragon::control_a_roll6_scores}},
	component_info{dragon::tag_s_rexit19, {dragon::GargoyleRampExitControl, 0, nullptr}},
	component_info{dragon::tag_s_rexit18, {dragon::ForestRampExitControl, 0, nullptr}},
	component_info{dragon::tag_a_flip2, {dragon::RightFlipperControl, 0, nullptr}},
	component_info{dragon::tag_a_kick1, {dragon::OutlaneKickerControl, 0, nullptr}},
	component_info{dragon::tag_a_kout1, {dragon::ArmorKickoutControl, 0, nullptr}},
	component_info{dragon::tag_a_kout2, {dragon::GargoyleKickoutControl, 0, nullptr}},
	component_info{dragon::tag_s_hole118, {dragon::QuestHoleControl, 0, nullptr}},
	component_info{dragon::tag_a_sink15, {dragon::TowerLock1EntranceControl, 1, dragon::control_a_sink15_scores}},
	component_info{dragon::tag_a_sink18, {dragon::TowerLock1SinkControl, 1, dragon::control_a_sink15_scores}},
	component_info{dragon::tag_a_sink13, {dragon::TowerLock2EntranceControl, 1, dragon::control_a_sink13_scores}},
	component_info{dragon::tag_a_sink17, {dragon::TowerLock2SinkControl, 1, dragon::control_a_sink13_scores}},
	component_info{dragon::tag_a_sink9, {dragon::TowerLock3EntranceAControl, 1, dragon::control_a_sink9_scores}},
	component_info{dragon::tag_a_sink10, {dragon::TowerLock3EntranceBControl, 1, dragon::control_a_sink9_scores}},
	component_info{dragon::tag_v_wlite96, {dragon::WallLightControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_v_wlite97, {dragon::WallLightControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_v_wlite98, {dragon::WallLightControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_v_wlite99, {dragon::WallLightControl, 1, dragon::control_a_bump1_scores}},
	component_info{dragon::tag_s_trip6, {dragon::PrisonWindowTripControl, 4, dragon::control_s_trip6_scores}},
	component_info{dragon::tag_s_trip5, {dragon::PrisonWindowTripControl, 4, dragon::control_s_trip6_scores}},
	component_info{dragon::tag_s_trip3, {dragon::WatchWindowTripControl, 3, dragon::control_s_trip3_scores}},
	component_info{dragon::tag_s_trip4, {dragon::WatchWindowTripControl, 3, dragon::control_s_trip3_scores}},
	component_info{dragon::tag_s_trip8, {dragon::GateWindowTripControl, 2, dragon::control_s_trip8_scores}},
	component_info{dragon::tag_s_trip9, {dragon::GateWindowTripControl, 2, dragon::control_s_trip8_scores}},
	component_info{dragon::tag_a_bloce, {dragon::RightOutlaneBlockerControl, 0, nullptr}},
	component_info{dragon::tag_a_blocf, {dragon::MoatBlockerControl, 0, nullptr}},
	component_info{dragon::tag_a_kout3, {dragon::DragonBloodKickoutControl, 0, nullptr}},
	component_info{dragon::tag_a_sink19, {dragon::DragonMouthSinkControl, 0, nullptr}},
	component_info{dragon::tag_a_rocka, {dragon::GargoyleRockAControl, 0, nullptr}},
	component_info{dragon::tag_a_rockb, {dragon::GargoyleRockBControl, 0, nullptr}},
	component_info{dragon::tag_v_lite37, {dragon::ShieldLightControl, 0, nullptr}},
	component_info{dragon::tag_v_lite29, {dragon::DragonBloodLightControl, 0, nullptr}},
};

component_tag_base* dragon_simple_components[150]
{
	&dragon::tag_v_lite38,
	&dragon::tag_v_lite39,
	&dragon::tag_a_dvtraa,
	&dragon::tag_a_dvtrab,
	&dragon::tag_a_dvtrba,
	&dragon::tag_a_dvtrbb,
	&dragon::tag_a_dvtrca,
	&dragon::tag_a_dvtrcb,
	&dragon::tag_a_dvtrda,
	&dragon::tag_a_dvtrdb,
	&dragon::tag_v_lite47,
	&dragon::tag_info_text_box,
	&dragon::tag_v_lite22,
	&dragon::tag_v_lite23,
	&dragon::tag_v_lite24,
	&dragon::tag_v_lite25,
	&dragon::tag_v_lite26,
	&dragon::tag_v_lite27,
	&dragon::tag_v_lite3,
	&dragon::tag_v_lite4,
	&dragon::tag_v_lite5,
	&dragon::tag_v_lite6,
	&dragon::tag_v_lite7,
	&dragon::tag_spell_spot_light_group,
	&dragon::tag_v_lite8,
	&dragon::tag_v_lite9,
	&dragon::tag_v_lite10,
	&dragon::tag_v_lite14,
	&dragon::tag_v_lite13,
	&dragon::tag_v_lite12,
	&dragon::tag_v_lite11,
	&dragon::tag_gargoyle_spot_light_group,
	&dragon::tag_v_lite21,
	&dragon::tag_v_lite57,
	&dragon::tag_v_lite56,
	&dragon::tag_v_lite55,
	&dragon::tag_v_lite16,
	&dragon::tag_v_lite17,
	&dragon::tag_v_lite18,
	&dragon::tag_a_sword,
	&dragon::tag_dragon_fire_light_group,
	&dragon::tag_v_lite119,
	&dragon::tag_v_lite20,
	&dragon::tag_v_lite19,
	&dragon::tag_v_lite63,
	&dragon::tag_v_lite64,
	&dragon::tag_v_lite66,
	&dragon::tag_v_lite65,
	&dragon::tag_tabc16wave,
	&dragon::tag_well_light_group,
	&dragon::tag_v_lite67,
	&dragon::tag_v_lite70,
	&dragon::tag_v_lite68,
	&dragon::tag_outlane_kicker_light_group,
	&dragon::tag_dragon_egg_light_group,
	&dragon::tag_dragon_letter_light_group,
	&dragon::tag_v_lite45,
	&dragon::tag_v_lite46,
	&dragon::tag_a_ramp107,
	&dragon::tag_s_bwal170,
	&dragon::tag_v_lite33,
	&dragon::tag_v_lite34,
	&dragon::tag_v_lite35,
	&dragon::tag_v_lite36,
	&dragon::tag_a_sink16,
	&dragon::tag_v_lite48,
	&dragon::tag_v_lite49,
	&dragon::tag_v_lite50,
	&dragon::tag_prison_window_light_group,
	&dragon::tag_watch_window_light_group,
	&dragon::tag_gate_window_light_group,
	&dragon::tag_v_lite28,
	&dragon::tag_v_lite30,
	&dragon::tag_v_lite31,
	&dragon::tag_v_lite32,
	&dragon::tag_light_sweeps,
	&dragon::tag_v_lite88,
	&dragon::tag_v_lite89,
	&dragon::tag_v_lite90,
	&dragon::tag_v_lite91,
	&dragon::tag_v_lite92,
	&dragon::tag_v_lite93,
	&dragon::tag_v_lite94,
	&dragon::tag_v_lite95,
	&dragon::tag_v_lite117,
	&dragon::tag_mode_timer,
	&dragon::tag_gem_light_group,
	&dragon::tag_v_lite120,
	&dragon::tag_v_lite121,
	&dragon::tag_v_lite113,
	&dragon::tag_v_lite114,
	&dragon::tag_v_lite115,
	&dragon::tag_dragon_mouth_light_group,
	&dragon::tag_a_jaw,
	&dragon::tag_v_lite71,
	&dragon::tag_v_lite72,
	&dragon::tag_v_lite73,
	&dragon::tag_v_lite74,
	&dragon::tag_v_lite75,
	&dragon::tag_v_lite76,
	&dragon::tag_v_lite58,
	&dragon::tag_v_lite59,
	&dragon::tag_v_lite60,
	&dragon::tag_v_lite61,
	&dragon::tag_v_lite62,
	&dragon::tag_tabc04wave,
	&dragon::tag_tabc10wave,
	&dragon::tag_tabc07wave,
	&dragon::tag_tabc51wave,
	&dragon::tag_tabc13wave,
	&dragon::tag_tabc18wave,
	&dragon::tag_tabc21wave,
	&dragon::tag_tabc24wave,
	&dragon::tag_tabc27wave,
	&dragon::tag_tabc32wave,
	&dragon::tag_tabc33wave,
	&dragon::tag_tabc34wave,
	&dragon::tag_tabc35wave,
	&dragon::tag_tabc39wave,
	&dragon::tag_tabc40wave,
	&dragon::tag_tabc41wave,
	&dragon::tag_tabc42wave,
	&dragon::tag_tabc43wave,
	&dragon::tag_tabc44wave,
	&dragon::tag_tabc46wave,
	&dragon::tag_tabc47wave,
	&dragon::tag_tabc48wave,
	&dragon::tag_tabc50wave,
	&dragon::tag_tabc53wave,
	&dragon::tag_tabc55wave,
	&dragon::tag_tabc56wave,
	&dragon::tag_tabc57wave,
	&dragon::tag_tabc58wave,
	&dragon::tag_tabc61wave,
	&dragon::tag_tabc03wave,
	&dragon::tag_a_flip1,
	&dragon::tag_a_flip3,
	&dragon::tag_v_lite77,
	&dragon::tag_v_lite78,
	&dragon::tag_v_lite79,
	&dragon::tag_tabc96wave,
	&dragon::tag_v_lite125,
	&dragon::tag_v_lite126,
	&dragon::tag_tabc05wave,
	&dragon::tag_tabc06wave,
	&dragon::tag_tabc08wave,
	&dragon::tag_tabc20wave,
	&dragon::tag_tabc22wave,
	&dragon::tag_tabc09wave,
	&dragon::tag_tabc35wave,
};
