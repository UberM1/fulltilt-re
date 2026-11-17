// control_pirates.cpp -- rules for Full Tilt! Pinball's Pirates table.
//
// GENERATED SKELETON (gen_control.py). The score arrays, tags and the component
// table below are recovered from PIRATES.EXE and are believed exact.
// The handler bodies are NOT: each is a stub marked TODO, annotated with its
// address in the original binary and the Space Cadet handler it most resembles.
//
// 74 components, 69 distinct handlers.

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

namespace pirates {

// Score tables, read out of .data.
int control_a_bump1_scores[7] = {1500, 3000, 4500, 6000, 7500, 9000, 10500};
int control_a_rebo1_scores[1] = {350};
int control_a_drop5_scores[2] = {3000, 25000};
int control_a_drop13_scores[2] = {4000, 30000};
int control_a_drop10_scores[2] = {5000, 35000};
int control_a_spot102_scores[1] = {50000};
int control_a_spot11_scores[2] = {1000, 15000};
int control_a_spot4_scores[2] = {5000, 20000};
int control_a_spot6_scores[1] = {5000};
int control_a_sink17_scores[1] = {40000};
int control_a_sink4_scores[1] = {100000};
int control_a_sink5_scores[2] = {75000, 200000};
int control_a_ramp8_scores[12] = {25000, 50000, 75000, 150000, 175000, 200000, 275000, 300000, 325000, 425000, 450000, 475000};
int control_s_trip11_scores[1] = {25000};
int control_a_roll5_scores[1] = {10000};
int control_a_roll2_scores[3] = {25000, 50000, 75000};
int control_a_roll12_scores[2] = {1200, 20000};
// Component tags. Names are the group names stored in the DAT. Both the
// scoring table and simple_components below bind through these.
extern component_tag<TPinballComponent> tag_s_oneway3;
component_tag<TPinballComponent> tag_a_bump1 = {"a_bump1"};
component_tag<TPinballComponent> tag_a_bump2 = {"a_bump2"};
component_tag<TPinballComponent> tag_a_bump3 = {"a_bump3"};
extern component_tag<TPinballComponent> tag_drain;
extern component_tag<TPinballComponent> tag_s_launcher1;
extern component_tag<TLight> tag_v_lite569;
component_tag<TPinballComponent> tag_a_rebo1 = {"a_rebo1"};
component_tag<TPinballComponent> tag_a_rebo2 = {"a_rebo2"};
component_tag<TPinballComponent> tag_a_drop5 = {"a_drop5"};
component_tag<TPinballComponent> tag_a_drop6 = {"a_drop6"};
component_tag<TPinballComponent> tag_a_drop7 = {"a_drop7"};
component_tag<TPinballComponent> tag_a_drop8 = {"a_drop8"};
component_tag<TPinballComponent> tag_a_drop9 = {"a_drop9"};
component_tag<TPinballComponent> tag_a_drop13 = {"a_drop13"};
component_tag<TPinballComponent> tag_a_drop14 = {"a_drop14"};
component_tag<TPinballComponent> tag_a_drop15 = {"a_drop15"};
component_tag<TPinballComponent> tag_a_drop10 = {"a_drop10"};
component_tag<TPinballComponent> tag_a_drop11 = {"a_drop11"};
component_tag<TPinballComponent> tag_a_drop12 = {"a_drop12"};
component_tag<TPinballComponent> tag_a_spot102 = {"a_spot102"};
extern component_tag<TPinballComponent> tag_a_spot11;
component_tag<TPinballComponent> tag_a_spot8 = {"a_spot8"};
component_tag<TPinballComponent> tag_a_spot9 = {"a_spot9"};
component_tag<TPinballComponent> tag_a_spot10 = {"a_spot10"};
component_tag<TPinballComponent> tag_a_spot4 = {"a_spot4"};
component_tag<TPinballComponent> tag_a_spot5 = {"a_spot5"};
extern component_tag<TPinballComponent> tag_a_spot6;
extern component_tag<TPinballComponent> tag_a_sink17;
extern component_tag<TPinballComponent> tag_a_sink15;
extern component_tag<TPinballComponent> tag_a_sink14;
component_tag<TPinballComponent> tag_a_sink4 = {"a_sink4"};
extern component_tag<TPinballComponent> tag_a_sink9;
extern component_tag<TPinballComponent> tag_a_sink12;
component_tag<TPinballComponent> tag_a_sink5 = {"a_sink5"};
component_tag<TPinballComponent> tag_a_sink6 = {"a_sink6"};
component_tag<TPinballComponent> tag_a_sink7 = {"a_sink7"};
component_tag<TPinballComponent> tag_a_ramp8 = {"a_ramp8"};
component_tag<TPinballComponent> tag_a_ramp5 = {"a_ramp5"};
component_tag<TPinballComponent> tag_a_ramp13 = {"a_ramp13"};
component_tag<TPinballComponent> tag_s_trip1 = {"s_trip1"};
component_tag<TPinballComponent> tag_s_hole19 = {"s_hole19"};
component_tag<TPinballComponent> tag_s_trip2 = {"s_trip2"};
component_tag<TPinballComponent> tag_s_rexit8 = {"s_rexit8"};
extern component_tag<TPinballComponent> tag_s_trip3;
extern component_tag<TPinballComponent> tag_s_trip8;
component_tag<TPinballComponent> tag_s_trip11 = {"s_trip11"};
component_tag<TPinballComponent> tag_a_roll3 = {"a_roll3"};
component_tag<TPinballComponent> tag_a_roll5 = {"a_roll5"};
component_tag<TPinballComponent> tag_a_roll1 = {"a_roll1"};
component_tag<TPinballComponent> tag_a_roll2 = {"a_roll2"};
component_tag<TPinballComponent> tag_a_roll9 = {"a_roll9"};
component_tag<TPinballComponent> tag_a_roll8 = {"a_roll8"};
extern component_tag<TPinballComponent> tag_a_roll6;
component_tag<TPinballComponent> tag_a_roll12 = {"a_roll12"};
component_tag<TPinballComponent> tag_a_roll11 = {"a_roll11"};
component_tag<TPinballComponent> tag_a_roll10 = {"a_roll10"};
extern component_tag<TPinballComponent> tag_a_flip1;
extern component_tag<TPinballComponent> tag_a_flip2;
extern component_tag<TPinballComponent> tag_a_kick2;
extern component_tag<TLight> tag_v_lite544;
component_tag<TPinballComponent> tag_s_oneway7 = {"s_oneway7"};
extern component_tag<TLight> tag_v_lite547;
extern component_tag<TLight> tag_v_lite546;
extern component_tag<TLight> tag_v_lite545;
extern component_tag<TPinballComponent> tag_a_sink8;
extern component_tag<TPinballComponent> tag_a_kout2;
extern component_tag<TPinballComponent> tag_a_kout1;
extern component_tag<TPinballComponent> tag_s_magnet1;
extern component_tag<TPinballComponent> tag_s_magnet2;
extern component_tag<TPinballComponent> tag_s_magnet3;
extern component_tag<TPinballComponent> tag_s_magnet4;
extern component_tag<TPinballComponent> tag_s_magnet5;
extern component_tag<TPinballComponent> tag_s_magnet6;
extern component_tag<TPinballComponent> tag_a_flip3;
extern component_tag<TPinballComponent> tag_a_flip4;
extern component_tag<TTextBox> tag_info_text_box;
extern component_tag<TBlocker> tag_a_blocc;
extern component_tag<TBlocker> tag_a_bloce;
extern component_tag<TBlocker> tag_a_blocf;
extern component_tag<TBlocker> tag_a_blocg;
extern component_tag<TLight> tag_v_lite504;
extern component_tag<TLight> tag_v_lite505;
extern component_tag<TLight> tag_v_lite84;
extern component_tag<TLight> tag_v_lite85;
extern component_tag<TLight> tag_v_lite1;
extern component_tag<TLight> tag_v_lite2;
extern component_tag<TLight> tag_v_lite3;
extern component_tag<TLight> tag_v_lite4;
extern component_tag<TLight> tag_v_lite5;
extern component_tag<TLight> tag_v_lite6;
extern component_tag<TLight> tag_v_lite7;
extern component_tag<TLight> tag_v_lite8;
extern component_tag<TLight> tag_v_lite9;
extern component_tag<TLight> tag_v_lite10;
extern component_tag<TLight> tag_v_lite11;
extern component_tag<TLight> tag_v_lite606;
extern component_tag<TLight> tag_v_lite607;
extern component_tag<TLight> tag_v_lite551;
extern component_tag<TLight> tag_v_lite550;
extern component_tag<TLight> tag_v_lite549;
extern component_tag<TLight> tag_v_lite548;
extern component_tag<TLight> tag_v_lite522;
extern component_tag<TLight> tag_v_lite779;
extern component_tag<TLight> tag_v_lite530;
extern component_tag<TLight> tag_v_lite531;
extern component_tag<TLight> tag_v_lite532;
component_tag<TPinballComponent> tag_v_lite528 = {"v_lite528"};
component_tag<TPinballComponent> tag_v_lite529 = {"v_lite529"};
extern component_tag<TLight> tag_v_lite524;
extern component_tag<TLight> tag_v_lite525;
extern component_tag<TLight> tag_v_lite526;
extern component_tag<TLight> tag_v_lite527;
extern component_tag<TLight> tag_v_lite567;
extern component_tag<TLight> tag_v_lite603;
extern component_tag<TLight> tag_v_lite604;
component_tag<TPinballComponent> tag_v_lite605 = {"v_lite605"};
extern component_tag<TLightGroup> tag_left_island_light_group;
extern component_tag<TLight> tag_v_lite629;
extern component_tag<TLight> tag_v_lite630;
component_tag<TPinballComponent> tag_v_lite777 = {"v_lite777"};
extern component_tag<TPinballComponent> tag_left_island_combo_light_group;
extern component_tag<TLight> tag_v_lite611;
extern component_tag<TLight> tag_v_lite612;
component_tag<TPinballComponent> tag_v_lite613 = {"v_lite613"};
extern component_tag<TLightGroup> tag_right_wind_light_group;
extern component_tag<TLight> tag_v_lite626;
extern component_tag<TLight> tag_v_lite627;
component_tag<TPinballComponent> tag_v_lite628 = {"v_lite628"};
extern component_tag<TPinballComponent> tag_right_island_combo_light_group;
extern component_tag<TLight> tag_v_lite600;
extern component_tag<TLight> tag_v_lite601;
component_tag<TPinballComponent> tag_v_lite602 = {"v_lite602"};
extern component_tag<TLightGroup> tag_left_orbit_light_group;
extern component_tag<TLight> tag_v_lite631;
extern component_tag<TLight> tag_v_lite632;
component_tag<TPinballComponent> tag_v_lite633 = {"v_lite633"};
extern component_tag<TLightGroup> tag_left_orbit_combo_light_group;
extern component_tag<TLight> tag_v_lite608;
extern component_tag<TLight> tag_v_lite609;
component_tag<TPinballComponent> tag_v_lite610 = {"v_lite610"};
extern component_tag<TLightGroup> tag_right_orbit_light_group;
extern component_tag<TLight> tag_v_lite634;
extern component_tag<TLight> tag_v_lite635;
component_tag<TPinballComponent> tag_v_lite636 = {"v_lite636"};
extern component_tag<TLightGroup> tag_right_orbit_combo_light_group;
extern component_tag<TLight> tag_v_lite533;
extern component_tag<TLight> tag_v_lite534;
extern component_tag<TLight> tag_v_lite535;
extern component_tag<TLight> tag_v_lite536;
extern component_tag<TLight> tag_v_lite537;
extern component_tag<TLight> tag_v_lite622;
extern component_tag<TLight> tag_v_lite621;
component_tag<TPinballComponent> tag_v_lite620 = {"v_lite620"};
extern component_tag<TLightGroup> tag_south_wind_light_group;
extern component_tag<TLight> tag_v_lite616;
extern component_tag<TLight> tag_v_lite615;
component_tag<TPinballComponent> tag_v_lite614 = {"v_lite614"};
extern component_tag<TLightGroup> tag_east_wind_1_light_group;
extern component_tag<TLight> tag_v_lite619;
extern component_tag<TLight> tag_v_lite618;
component_tag<TPinballComponent> tag_v_lite617 = {"v_lite617"};
extern component_tag<TLightGroup> tag_east_wind_2_light_group;
extern component_tag<TLight> tag_v_lite623;
extern component_tag<TLight> tag_v_lite624;
component_tag<TPinballComponent> tag_v_lite625 = {"v_lite625"};
extern component_tag<TLightGroup> tag_west_wind_light_group;
extern component_tag<TLight> tag_v_lite538;
extern component_tag<TLight> tag_v_lite539;
extern component_tag<TLight> tag_v_lite540;
extern component_tag<TLightGroup> tag_island_lane_top_light_group;
extern component_tag<TLight> tag_v_lite541;
extern component_tag<TLight> tag_v_lite542;
extern component_tag<TLight> tag_v_lite543;
extern component_tag<TLightGroup> tag_island_lane_bottom_light_group;
extern component_tag<TLightGroup> tag_fathom_light_group;
extern component_tag<TLightGroup> tag_lane_winds_light_group;
extern component_tag<TPinballComponent> tag_a_kick1;
extern component_tag<TPinballComponent> tag_s_oneway12;
extern component_tag<TLight> tag_v_lite568;
extern component_tag<TLightGroup> tag_coin_light_group;
extern component_tag<TLight> tag_v_mlite44;
extern component_tag<TLight> tag_v_mlite107;
extern component_tag<TLight> tag_v_mlite106;
extern component_tag<TLight> tag_v_mlite51;
extern component_tag<TLight> tag_v_mlite110;
extern component_tag<TLight> tag_v_mlite104;
extern component_tag<TSound> tag_tabb58wave;
extern component_tag<TSound> tag_tabb22wave;
extern component_tag<TSound> tag_tabb26wave;
extern component_tag<TSound> tag_tabb24wave;
extern component_tag<TSound> tag_tabb57wave;
extern component_tag<TSound> tag_tabb59wave;
component_tag<TPinballComponent> tag_tabb84wave = {"tabb84wave"};
extern component_tag<TSound> tag_tabb65wave;
extern component_tag<TLight> tag_v_lite560;
extern component_tag<TLight> tag_v_lite561;
extern component_tag<TLight> tag_v_lite562;
extern component_tag<TLight> tag_v_lite563;
extern component_tag<TLight> tag_v_lite564;
extern component_tag<TLight> tag_v_lite565;
component_tag<TPinballComponent> tag_keg_light_group = {"keg_light_group"};
component_tag<TPinballComponent> tag_v_lite571 = {"v_lite571"};
component_tag<TPinballComponent> tag_v_lite572 = {"v_lite572"};
component_tag<TPinballComponent> tag_v_lite573 = {"v_lite573"};
component_tag<TPinballComponent> tag_v_lite574 = {"v_lite574"};
component_tag<TPinballComponent> tag_v_lite575 = {"v_lite575"};
component_tag<TPinballComponent> tag_v_lite576 = {"v_lite576"};
extern component_tag<TLightGroup> tag_special_treasure_light_group;
extern component_tag<TPinballComponent> tag_light_sweeps;
extern component_tag<TPinballComponent> tag_mode_timer;
component_tag<TPinballComponent> tag_tabb42wave = {"tabb42wave"};
extern component_tag<TSound> tag_tabb66wave;
extern component_tag<TSound> tag_tabb70wave;
extern component_tag<TSound> tag_tabb11wave;
extern component_tag<TLight> tag_v_lite778;
extern component_tag<TSound> tag_tabb67wave;
extern component_tag<TSound> tag_tabb68wave;
extern component_tag<TSound> tag_tabb73wave;
extern component_tag<TSound> tag_tabb74wave;
extern component_tag<TSound> tag_tabb75wave;
extern component_tag<TSound> tag_tabb94wave;
extern component_tag<TSound> tag_tabb32wave;
extern component_tag<TSound> tag_tabb02wave;
extern component_tag<TSound> tag_tabb29wave;
extern component_tag<TSound> tag_tabb12wave;
extern component_tag<TSound> tag_tabb16wave;
extern component_tag<TSound> tag_tabb17wave;
extern component_tag<TSound> tag_tabb19wave;
extern component_tag<TSound> tag_tabb07wave;
extern component_tag<TSound> tag_tabb02wave;
extern component_tag<TSound> tag_tabb23wave;
extern component_tag<TSound> tag_tabb27wave;
extern component_tag<TSound> tag_tabb08wave;
extern component_tag<TSound> tag_tabb40wave;
extern component_tag<TSound> tag_tabb41wave;
extern component_tag<TSound> tag_tabb39wave;
extern component_tag<TSound> tag_tabb43wave;
extern component_tag<TSound> tag_tabb61wave;
component_tag<TPinballComponent> tag_tabb83wave = {"tabb83wave"};
component_tag<TPinballComponent> tag_tabb85wave = {"tabb85wave"};
component_tag<TPinballComponent> tag_tabb86wave = {"tabb86wave"};
extern component_tag<TSound> tag_tabb87wave;
extern component_tag<TSound> tag_tabb88wave;
extern component_tag<TLightGroup> tag_map_piece_light_group;
extern component_tag<TSound> tag_tabb95wave;
extern component_tag<TSound> tag_tabb99wave;
extern component_tag<TLight> tag_v_lite552;
extern component_tag<TLight> tag_v_lite553;
extern component_tag<TLight> tag_v_lite554;
extern component_tag<TLight> tag_v_lite555;
extern component_tag<TLight> tag_v_lite556;
extern component_tag<TLight> tag_v_lite557;
extern component_tag<TLight> tag_v_lite558;
extern component_tag<TLight> tag_v_lite559;
extern component_tag<TLight> tag_v_lite519;
extern component_tag<TLight> tag_v_lite520;
extern component_tag<TLight> tag_v_lite521;
extern component_tag<TLightGroup> tag_bermuda_light_group;
extern component_tag<TPinballComponent> tag_s_bwal610;
extern component_tag<TLight> tag_v_lite570;
component_tag<TPinballComponent> tag_tabb00wave = {"tabb00wave"};
extern component_tag<TPinballComponent> tag_a_cannon;
extern component_tag<TSound> tag_tabb55wave;
extern component_tag<TSound> tag_tabb28wave;
extern component_tag<TSound> tag_tabb44wave;

// Handlers implemented in the hand-written rule files.
void LaunchLaneOneWayControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void BumperControl(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void BallDrainControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void LauncherControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void ShootAgainLightControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void FlipperRebounderControl1(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void FlipperRebounderControl2(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ShipDropTargetControl5(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ShipDropTargetControl6(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ShipDropTargetControl7(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ShipDropTargetControl8(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ShipDropTargetControl9(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void TavernDropTargetControl13(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void TavernDropTargetControl14(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void TavernDropTargetControl15(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ParrotDropTargetControl10(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ParrotDropTargetControl11(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ParrotDropTargetControl12(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void CaptiveBallControl(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void VolcanoTargetControl11(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void VolcanoTargetControl8(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void VolcanoTargetControl9(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void VolcanoTargetControl10(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void HideawayTargetControl4(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void HideawayTargetControl5(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void BrigTargetControl(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void HideawayLeftControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void HideawayCentreControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void HideawayRightControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void ShipSinkControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void TreasureSinkLeftControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void TreasureSinkRightControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void TavernSinkLeftControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void TavernSinkCentreControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void TavernSinkRightControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void LeftIslandRampControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void RightIslandRampControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void CannonRampControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void LaunchLaneExitControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void BrigHoleControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void ReturnWireControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void ShipWireRampControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void LeftOrbitControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void RightOrbitControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void WindsLaneControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void RightOutlaneControl(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ReturnLaneControl5(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void ReturnLaneControl1(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void SouthWindLaneControl(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void EastWindLaneControl1(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void EastWindLaneControl2(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void WestWindLaneControl(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void IslandLaneControl12(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void IslandLaneControl11(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void IslandLaneControl10(MessageCode code, TPinballComponent* caller); // pirates_part1.cpp
void FlipperLaneRotateControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void FlipperLaneRotateControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void KickbackControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void KickbackArmedLightControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void BrigReleaseOneWayControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void TavernWindowLightControl(MessageCode code, TPinballComponent* caller); // pirates_part3.cpp
void LavaSinkControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void CaptiveKickoutControl(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void KegMagnet1Control(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void KegMagnet2Control(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void KegMagnet3Control(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void KegMagnet4Control(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void KegMagnet5Control(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp
void KegMagnet6Control(MessageCode code, TPinballComponent* caller); // pirates_part2.cpp

// Handlers still to reverse engineer.

} // namespace pirates


component_info pirates_score_components[74]
{
	component_info{pirates::tag_s_oneway3, {pirates::LaunchLaneOneWayControl, 0, nullptr}},
	component_info{pirates::tag_a_bump1, {pirates::BumperControl, 7, pirates::control_a_bump1_scores}},
	component_info{pirates::tag_a_bump2, {pirates::BumperControl, 7, pirates::control_a_bump1_scores}},
	component_info{pirates::tag_a_bump3, {pirates::BumperControl, 7, pirates::control_a_bump1_scores}},
	component_info{pirates::tag_drain, {pirates::BallDrainControl, 0, nullptr}},
	component_info{pirates::tag_s_launcher1, {pirates::LauncherControl, 0, nullptr}},
	component_info{pirates::tag_v_lite569, {pirates::ShootAgainLightControl, 0, nullptr}},
	component_info{pirates::tag_a_rebo1, {pirates::FlipperRebounderControl1, 1, pirates::control_a_rebo1_scores}},
	component_info{pirates::tag_a_rebo2, {pirates::FlipperRebounderControl2, 1, pirates::control_a_rebo1_scores}},
	component_info{pirates::tag_a_drop5, {pirates::ShipDropTargetControl5, 2, pirates::control_a_drop5_scores}},
	component_info{pirates::tag_a_drop6, {pirates::ShipDropTargetControl6, 2, pirates::control_a_drop5_scores}},
	component_info{pirates::tag_a_drop7, {pirates::ShipDropTargetControl7, 2, pirates::control_a_drop5_scores}},
	component_info{pirates::tag_a_drop8, {pirates::ShipDropTargetControl8, 2, pirates::control_a_drop5_scores}},
	component_info{pirates::tag_a_drop9, {pirates::ShipDropTargetControl9, 2, pirates::control_a_drop5_scores}},
	component_info{pirates::tag_a_drop13, {pirates::TavernDropTargetControl13, 2, pirates::control_a_drop13_scores}},
	component_info{pirates::tag_a_drop14, {pirates::TavernDropTargetControl14, 2, pirates::control_a_drop13_scores}},
	component_info{pirates::tag_a_drop15, {pirates::TavernDropTargetControl15, 2, pirates::control_a_drop13_scores}},
	component_info{pirates::tag_a_drop10, {pirates::ParrotDropTargetControl10, 2, pirates::control_a_drop10_scores}},
	component_info{pirates::tag_a_drop11, {pirates::ParrotDropTargetControl11, 2, pirates::control_a_drop10_scores}},
	component_info{pirates::tag_a_drop12, {pirates::ParrotDropTargetControl12, 2, pirates::control_a_drop10_scores}},
	component_info{pirates::tag_a_spot102, {pirates::CaptiveBallControl, 1, pirates::control_a_spot102_scores}},
	component_info{pirates::tag_a_spot11, {pirates::VolcanoTargetControl11, 2, pirates::control_a_spot11_scores}},
	component_info{pirates::tag_a_spot8, {pirates::VolcanoTargetControl8, 2, pirates::control_a_spot11_scores}},
	component_info{pirates::tag_a_spot9, {pirates::VolcanoTargetControl9, 2, pirates::control_a_spot11_scores}},
	component_info{pirates::tag_a_spot10, {pirates::VolcanoTargetControl10, 2, pirates::control_a_spot11_scores}},
	component_info{pirates::tag_a_spot4, {pirates::HideawayTargetControl4, 2, pirates::control_a_spot4_scores}},
	component_info{pirates::tag_a_spot5, {pirates::HideawayTargetControl5, 2, pirates::control_a_spot4_scores}},
	component_info{pirates::tag_a_spot6, {pirates::BrigTargetControl, 1, pirates::control_a_spot6_scores}},
	component_info{pirates::tag_a_sink17, {pirates::HideawayLeftControl, 1, pirates::control_a_sink17_scores}},
	component_info{pirates::tag_a_sink15, {pirates::HideawayCentreControl, 1, pirates::control_a_sink17_scores}},
	component_info{pirates::tag_a_sink14, {pirates::HideawayRightControl, 1, pirates::control_a_sink17_scores}},
	component_info{pirates::tag_a_sink4, {pirates::ShipSinkControl, 1, pirates::control_a_sink4_scores}},
	component_info{pirates::tag_a_sink9, {pirates::TreasureSinkLeftControl, 1, pirates::control_a_sink17_scores}},
	component_info{pirates::tag_a_sink12, {pirates::TreasureSinkRightControl, 1, pirates::control_a_sink17_scores}},
	component_info{pirates::tag_a_sink5, {pirates::TavernSinkLeftControl, 2, pirates::control_a_sink5_scores}},
	component_info{pirates::tag_a_sink6, {pirates::TavernSinkCentreControl, 2, pirates::control_a_sink5_scores}},
	component_info{pirates::tag_a_sink7, {pirates::TavernSinkRightControl, 2, pirates::control_a_sink5_scores}},
	component_info{pirates::tag_a_ramp8, {pirates::LeftIslandRampControl, 12, pirates::control_a_ramp8_scores}},
	component_info{pirates::tag_a_ramp5, {pirates::RightIslandRampControl, 12, pirates::control_a_ramp8_scores}},
	component_info{pirates::tag_a_ramp13, {pirates::CannonRampControl, 1, pirates::control_a_spot102_scores}},
	component_info{pirates::tag_s_trip1, {pirates::LaunchLaneExitControl, 0, nullptr}},
	component_info{pirates::tag_s_hole19, {pirates::BrigHoleControl, 0, nullptr}},
	component_info{pirates::tag_s_trip2, {pirates::ReturnWireControl, 0, nullptr}},
	component_info{pirates::tag_s_rexit8, {pirates::ShipWireRampControl, 1, pirates::control_a_sink4_scores}},
	component_info{pirates::tag_s_trip3, {pirates::LeftOrbitControl, 12, pirates::control_a_ramp8_scores}},
	component_info{pirates::tag_s_trip8, {pirates::RightOrbitControl, 12, pirates::control_a_ramp8_scores}},
	component_info{pirates::tag_s_trip11, {pirates::WindsLaneControl, 1, pirates::control_s_trip11_scores}},
	component_info{pirates::tag_a_roll3, {pirates::RightOutlaneControl, 1, pirates::control_s_trip11_scores}},
	component_info{pirates::tag_a_roll5, {pirates::ReturnLaneControl5, 1, pirates::control_a_roll5_scores}},
	component_info{pirates::tag_a_roll1, {pirates::ReturnLaneControl1, 1, pirates::control_a_roll5_scores}},
	component_info{pirates::tag_a_roll2, {pirates::SouthWindLaneControl, 3, pirates::control_a_roll2_scores}},
	component_info{pirates::tag_a_roll9, {pirates::EastWindLaneControl1, 3, pirates::control_a_roll2_scores}},
	component_info{pirates::tag_a_roll8, {pirates::EastWindLaneControl2, 3, pirates::control_a_roll2_scores}},
	component_info{pirates::tag_a_roll6, {pirates::WestWindLaneControl, 3, pirates::control_a_roll2_scores}},
	component_info{pirates::tag_a_roll12, {pirates::IslandLaneControl12, 2, pirates::control_a_roll12_scores}},
	component_info{pirates::tag_a_roll11, {pirates::IslandLaneControl11, 2, pirates::control_a_roll12_scores}},
	component_info{pirates::tag_a_roll10, {pirates::IslandLaneControl10, 2, pirates::control_a_roll12_scores}},
	component_info{pirates::tag_a_flip1, {pirates::FlipperLaneRotateControl, 0, nullptr}},
	component_info{pirates::tag_a_flip2, {pirates::FlipperLaneRotateControl, 0, nullptr}},
	component_info{pirates::tag_a_kick2, {pirates::KickbackControl, 1, pirates::control_a_spot6_scores}},
	component_info{pirates::tag_v_lite544, {pirates::KickbackArmedLightControl, 0, nullptr}},
	component_info{pirates::tag_s_oneway7, {pirates::BrigReleaseOneWayControl, 0, nullptr}},
	component_info{pirates::tag_v_lite547, {pirates::TavernWindowLightControl, 0, nullptr}},
	component_info{pirates::tag_v_lite546, {pirates::TavernWindowLightControl, 0, nullptr}},
	component_info{pirates::tag_v_lite545, {pirates::TavernWindowLightControl, 0, nullptr}},
	component_info{pirates::tag_a_sink8, {pirates::LavaSinkControl, 0, nullptr}},
	component_info{pirates::tag_a_kout2, {pirates::CaptiveKickoutControl, 0, nullptr}},
	component_info{pirates::tag_a_kout1, {pirates::CaptiveKickoutControl, 0, nullptr}},
	component_info{pirates::tag_s_magnet1, {pirates::KegMagnet1Control, 0, nullptr}},
	component_info{pirates::tag_s_magnet2, {pirates::KegMagnet2Control, 0, nullptr}},
	component_info{pirates::tag_s_magnet3, {pirates::KegMagnet3Control, 0, nullptr}},
	component_info{pirates::tag_s_magnet4, {pirates::KegMagnet4Control, 0, nullptr}},
	component_info{pirates::tag_s_magnet5, {pirates::KegMagnet5Control, 0, nullptr}},
	component_info{pirates::tag_s_magnet6, {pirates::KegMagnet6Control, 0, nullptr}},
};

component_tag_base* pirates_simple_components[192]
{
	&pirates::tag_a_flip3,
	&pirates::tag_a_flip4,
	&pirates::tag_info_text_box,
	&pirates::tag_a_blocc,
	&pirates::tag_a_bloce,
	&pirates::tag_a_blocf,
	&pirates::tag_a_blocg,
	&pirates::tag_v_lite504,
	&pirates::tag_v_lite505,
	&pirates::tag_v_lite84,
	&pirates::tag_v_lite85,
	&pirates::tag_v_lite1,
	&pirates::tag_v_lite2,
	&pirates::tag_v_lite3,
	&pirates::tag_v_lite4,
	&pirates::tag_v_lite5,
	&pirates::tag_v_lite6,
	&pirates::tag_v_lite7,
	&pirates::tag_v_lite8,
	&pirates::tag_v_lite9,
	&pirates::tag_v_lite10,
	&pirates::tag_v_lite11,
	&pirates::tag_v_lite606,
	&pirates::tag_v_lite607,
	&pirates::tag_v_lite551,
	&pirates::tag_v_lite550,
	&pirates::tag_v_lite549,
	&pirates::tag_v_lite548,
	&pirates::tag_v_lite522,
	&pirates::tag_v_lite779,
	&pirates::tag_v_lite530,
	&pirates::tag_v_lite531,
	&pirates::tag_v_lite532,
	&pirates::tag_v_lite528,
	&pirates::tag_v_lite529,
	&pirates::tag_v_lite524,
	&pirates::tag_v_lite525,
	&pirates::tag_v_lite526,
	&pirates::tag_v_lite527,
	&pirates::tag_v_lite567,
	&pirates::tag_v_lite603,
	&pirates::tag_v_lite604,
	&pirates::tag_v_lite605,
	&pirates::tag_left_island_light_group,
	&pirates::tag_v_lite629,
	&pirates::tag_v_lite630,
	&pirates::tag_v_lite777,
	&pirates::tag_left_island_combo_light_group,
	&pirates::tag_v_lite611,
	&pirates::tag_v_lite612,
	&pirates::tag_v_lite613,
	&pirates::tag_right_wind_light_group,
	&pirates::tag_v_lite626,
	&pirates::tag_v_lite627,
	&pirates::tag_v_lite628,
	&pirates::tag_right_island_combo_light_group,
	&pirates::tag_v_lite600,
	&pirates::tag_v_lite601,
	&pirates::tag_v_lite602,
	&pirates::tag_left_orbit_light_group,
	&pirates::tag_v_lite631,
	&pirates::tag_v_lite632,
	&pirates::tag_v_lite633,
	&pirates::tag_left_orbit_combo_light_group,
	&pirates::tag_v_lite608,
	&pirates::tag_v_lite609,
	&pirates::tag_v_lite610,
	&pirates::tag_right_orbit_light_group,
	&pirates::tag_v_lite634,
	&pirates::tag_v_lite635,
	&pirates::tag_v_lite636,
	&pirates::tag_right_orbit_combo_light_group,
	&pirates::tag_v_lite533,
	&pirates::tag_v_lite534,
	&pirates::tag_v_lite535,
	&pirates::tag_v_lite536,
	&pirates::tag_v_lite537,
	&pirates::tag_v_lite622,
	&pirates::tag_v_lite621,
	&pirates::tag_v_lite620,
	&pirates::tag_south_wind_light_group,
	&pirates::tag_v_lite616,
	&pirates::tag_v_lite615,
	&pirates::tag_v_lite614,
	&pirates::tag_east_wind_1_light_group,
	&pirates::tag_v_lite619,
	&pirates::tag_v_lite618,
	&pirates::tag_v_lite617,
	&pirates::tag_east_wind_2_light_group,
	&pirates::tag_v_lite623,
	&pirates::tag_v_lite624,
	&pirates::tag_v_lite625,
	&pirates::tag_west_wind_light_group,
	&pirates::tag_v_lite538,
	&pirates::tag_v_lite539,
	&pirates::tag_v_lite540,
	&pirates::tag_island_lane_top_light_group,
	&pirates::tag_v_lite541,
	&pirates::tag_v_lite542,
	&pirates::tag_v_lite543,
	&pirates::tag_island_lane_bottom_light_group,
	&pirates::tag_fathom_light_group,
	&pirates::tag_lane_winds_light_group,
	&pirates::tag_a_kick1,
	&pirates::tag_s_oneway12,
	&pirates::tag_v_lite568,
	&pirates::tag_coin_light_group,
	&pirates::tag_v_mlite44,
	&pirates::tag_v_mlite107,
	&pirates::tag_v_mlite106,
	&pirates::tag_v_mlite51,
	&pirates::tag_v_mlite110,
	&pirates::tag_v_mlite104,
	&pirates::tag_tabb58wave,
	&pirates::tag_tabb22wave,
	&pirates::tag_tabb26wave,
	&pirates::tag_tabb24wave,
	&pirates::tag_tabb57wave,
	&pirates::tag_tabb59wave,
	&pirates::tag_tabb84wave,
	&pirates::tag_tabb65wave,
	&pirates::tag_v_lite560,
	&pirates::tag_v_lite561,
	&pirates::tag_v_lite562,
	&pirates::tag_v_lite563,
	&pirates::tag_v_lite564,
	&pirates::tag_v_lite565,
	&pirates::tag_keg_light_group,
	&pirates::tag_v_lite571,
	&pirates::tag_v_lite572,
	&pirates::tag_v_lite573,
	&pirates::tag_v_lite574,
	&pirates::tag_v_lite575,
	&pirates::tag_v_lite576,
	&pirates::tag_special_treasure_light_group,
	&pirates::tag_light_sweeps,
	&pirates::tag_mode_timer,
	&pirates::tag_tabb42wave,
	&pirates::tag_tabb66wave,
	&pirates::tag_tabb70wave,
	&pirates::tag_tabb11wave,
	&pirates::tag_v_lite778,
	&pirates::tag_tabb67wave,
	&pirates::tag_tabb68wave,
	&pirates::tag_tabb73wave,
	&pirates::tag_tabb74wave,
	&pirates::tag_tabb75wave,
	&pirates::tag_tabb94wave,
	&pirates::tag_tabb32wave,
	&pirates::tag_tabb02wave,
	&pirates::tag_tabb29wave,
	&pirates::tag_tabb12wave,
	&pirates::tag_tabb16wave,
	&pirates::tag_tabb17wave,
	&pirates::tag_tabb19wave,
	&pirates::tag_tabb07wave,
	&pirates::tag_tabb02wave,
	&pirates::tag_tabb23wave,
	&pirates::tag_tabb27wave,
	&pirates::tag_tabb08wave,
	&pirates::tag_tabb40wave,
	&pirates::tag_tabb41wave,
	&pirates::tag_tabb39wave,
	&pirates::tag_tabb43wave,
	&pirates::tag_tabb61wave,
	&pirates::tag_tabb83wave,
	&pirates::tag_tabb85wave,
	&pirates::tag_tabb86wave,
	&pirates::tag_tabb87wave,
	&pirates::tag_tabb88wave,
	&pirates::tag_map_piece_light_group,
	&pirates::tag_tabb95wave,
	&pirates::tag_tabb99wave,
	&pirates::tag_v_lite552,
	&pirates::tag_v_lite553,
	&pirates::tag_v_lite554,
	&pirates::tag_v_lite555,
	&pirates::tag_v_lite556,
	&pirates::tag_v_lite557,
	&pirates::tag_v_lite558,
	&pirates::tag_v_lite559,
	&pirates::tag_v_lite519,
	&pirates::tag_v_lite520,
	&pirates::tag_v_lite521,
	&pirates::tag_bermuda_light_group,
	&pirates::tag_s_bwal610,
	&pirates::tag_v_lite570,
	&pirates::tag_tabb00wave,
	&pirates::tag_a_cannon,
	&pirates::tag_tabb55wave,
	&pirates::tag_tabb28wave,
	&pirates::tag_tabb44wave,
};
