// pirates_components.cpp -- the cross-slice component bindings for Skulduggery.
//
// Every slice of this table's rules was written against the others' interfaces: each
// declared the tags and component aliases it touched as `extern` and assumed a sibling
// defined them. For the components listed here no sibling ever did, so the rules
// compiled and did not link. This file is that sibling -- one definition per tag and
// one alias per component, and nothing else.
//
// The aliases are references into the tags (`TLight*& lite22 = tag_v_lite22.Component`)
// rather than copies, which is what lets a slice read a component that
// `control::make_links` resolves later. The matching declarations in the slices are
// `extern T*&` for the same reason: an `extern T*` against a reference definition links
// silently and yields the address of the tag field instead of the component.
//
// Written by analysis/gen_glue.py from the slices' own `extern` declarations; the tag
// names come from PIRATES.EXE's component table, so the group names are exact.

#include "pch.h"
#include "control.h"

#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace pirates
{

// Tags no slice declares with its concrete type; control_pirates.cpp declares these
// `extern` against the types below rather than the generic one it would default to.
component_tag<TPinballComponent> tag_a_cannon = {"a_cannon"};
component_tag<TPinballComponent> tag_a_flip1 = {"a_flip1"};
component_tag<TPinballComponent> tag_a_flip2 = {"a_flip2"};
component_tag<TPinballComponent> tag_a_flip3 = {"a_flip3"};
component_tag<TPinballComponent> tag_a_flip4 = {"a_flip4"};
component_tag<TPinballComponent> tag_a_sink8 = {"a_sink8"};
component_tag<TPinballComponent> tag_drain = {"drain"};
component_tag<TLightGroup> tag_lane_winds_light_group = {"lane_winds_light_group"};
component_tag<TPinballComponent> tag_left_island_combo_light_group = {"left_island_combo_light_group"};
component_tag<TLight> tag_v_lite524 = {"v_lite524"};
component_tag<TLight> tag_v_lite525 = {"v_lite525"};
component_tag<TLight> tag_v_lite526 = {"v_lite526"};
component_tag<TLight> tag_v_lite527 = {"v_lite527"};
component_tag<TLight> tag_v_lite533 = {"v_lite533"};
component_tag<TLight> tag_v_lite560 = {"v_lite560"};
component_tag<TLight> tag_v_lite561 = {"v_lite561"};
component_tag<TLight> tag_v_lite562 = {"v_lite562"};
component_tag<TLight> tag_v_lite563 = {"v_lite563"};
component_tag<TLight> tag_v_lite564 = {"v_lite564"};
component_tag<TLight> tag_v_lite565 = {"v_lite565"};
component_tag<TLight> tag_v_lite570 = {"v_lite570"};
component_tag<TLight> tag_v_lite600 = {"v_lite600"};
component_tag<TLight> tag_v_lite601 = {"v_lite601"};
component_tag<TLight> tag_v_lite603 = {"v_lite603"};
component_tag<TLight> tag_v_lite604 = {"v_lite604"};
component_tag<TLight> tag_v_lite608 = {"v_lite608"};
component_tag<TLight> tag_v_lite609 = {"v_lite609"};
component_tag<TLight> tag_v_lite611 = {"v_lite611"};
component_tag<TLight> tag_v_lite612 = {"v_lite612"};
component_tag<TLight> tag_v_lite626 = {"v_lite626"};
component_tag<TLight> tag_v_lite627 = {"v_lite627"};
component_tag<TLight> tag_v_lite629 = {"v_lite629"};
component_tag<TLight> tag_v_lite630 = {"v_lite630"};
component_tag<TLight> tag_v_lite631 = {"v_lite631"};
component_tag<TLight> tag_v_lite632 = {"v_lite632"};
component_tag<TLight> tag_v_lite634 = {"v_lite634"};
component_tag<TLight> tag_v_lite635 = {"v_lite635"};
component_tag<TLight> tag_v_mlite104 = {"v_mlite104"};
component_tag<TLight> tag_v_mlite106 = {"v_mlite106"};
component_tag<TLight> tag_v_mlite107 = {"v_mlite107"};
component_tag<TLight> tag_v_mlite110 = {"v_mlite110"};
component_tag<TLight> tag_v_mlite44 = {"v_mlite44"};
component_tag<TPinballComponent> tag_right_island_combo_light_group = {"right_island_combo_light_group"};
component_tag<TSound> tag_tabb08wave = {"tabb08wave"};
component_tag<TSound> tag_tabb11wave = {"tabb11wave"};
component_tag<TSound> tag_tabb27wave = {"tabb27wave"};
component_tag<TSound> tag_tabb39wave = {"tabb39wave"};
component_tag<TSound> tag_tabb40wave = {"tabb40wave"};
component_tag<TSound> tag_tabb41wave = {"tabb41wave"};
component_tag<TSound> tag_tabb55wave = {"tabb55wave"};
component_tag<TSound> tag_tabb57wave = {"tabb57wave"};
component_tag<TSound> tag_tabb70wave = {"tabb70wave"};

// Tags another slice owns.
extern component_tag<TBlocker> tag_a_blocc;
extern component_tag<TBlocker> tag_a_bloce;
extern component_tag<TBlocker> tag_a_blocf;
extern component_tag<TBlocker> tag_a_blocg;
extern component_tag<TPinballComponent> tag_a_kick1;
extern component_tag<TPinballComponent> tag_a_kick2;
extern component_tag<TPinballComponent> tag_a_sink12;
extern component_tag<TPinballComponent> tag_a_sink9;
extern component_tag<TLightGroup> tag_coin_light_group;
extern component_tag<TLightGroup> tag_fathom_light_group;
extern component_tag<TLightGroup> tag_left_island_light_group;
extern component_tag<TLightGroup> tag_left_orbit_combo_light_group;
extern component_tag<TLightGroup> tag_left_orbit_light_group;
extern component_tag<TLight> tag_v_lite519;
extern component_tag<TLight> tag_v_lite520;
extern component_tag<TLight> tag_v_lite521;
extern component_tag<TLight> tag_v_lite545;
extern component_tag<TLight> tag_v_lite546;
extern component_tag<TLight> tag_v_lite547;
extern component_tag<TLight> tag_v_lite567;
extern component_tag<TLight> tag_v_lite568;
extern component_tag<TLight> tag_v_lite569;
extern component_tag<TLight> tag_v_mlite51;
extern component_tag<TLightGroup> tag_right_orbit_combo_light_group;
extern component_tag<TLightGroup> tag_right_orbit_light_group;
extern component_tag<TLightGroup> tag_right_wind_light_group;
extern component_tag<TPinballComponent> tag_s_bwal610;
extern component_tag<TPinballComponent> tag_s_launcher1;
extern component_tag<TPinballComponent> tag_s_magnet1;
extern component_tag<TPinballComponent> tag_s_magnet2;
extern component_tag<TPinballComponent> tag_s_magnet3;
extern component_tag<TPinballComponent> tag_s_magnet4;
extern component_tag<TPinballComponent> tag_s_magnet5;
extern component_tag<TPinballComponent> tag_s_magnet6;
extern component_tag<TLightGroup> tag_special_treasure_light_group;

// The aliases themselves.
TPinballTable*& TableG = control::TableG;
TBlocker*& a_blocc = tag_a_blocc.Component;  // the three sink-area blockers, held open for -1.0 s
TBlocker*& a_bloce = tag_a_bloce.Component;  // launch-lane blocker, paired with v_lite504/v_lite505
TBlocker*& a_blocf = tag_a_blocf.Component;  // (indefinitely) after every sink ejection
TBlocker*& a_blocg = tag_a_blocg.Component;
TPinballComponent*& a_cannon = tag_a_cannon.Component;
TPinballComponent*& a_flip1 = tag_a_flip1.Component;
TPinballComponent*& a_flip2 = tag_a_flip2.Component;
TPinballComponent*& a_flip3 = tag_a_flip3.Component;
TPinballComponent*& a_flip4 = tag_a_flip4.Component;
TPinballComponent*& a_kick1 = tag_a_kick1.Component;  // left-outlane kickback, armed by the two multiballs
TPinballComponent*& a_kick2 = tag_a_kick2.Component;
TPinballComponent*& a_sink12 = tag_a_sink12.Component;  // DAT_0043b11c
TPinballComponent*& a_sink8 = tag_a_sink8.Component;  // the lava sink, and the table's shared ball release
TPinballComponent*& a_sink9 = tag_a_sink9.Component;  // DAT_0043b104
TLightGroup*& coin_light_group = tag_coin_light_group.Component;  // DAT_0043bc7c
TPinballComponent*& drain = tag_drain.Component;
TLightGroup*& fathom_light_group = tag_fathom_light_group.Component;  // DAT_0043bb4c -- the multiplier track
TLightGroup*& lane_winds_lights = tag_lane_winds_light_group.Component;  // DAT_0043b7b4 -- the group the flippers rotate
TPinballComponent*& left_island_combo_light_group = tag_left_island_combo_light_group.Component;
TLightGroup*& left_island_light_group = tag_left_island_light_group.Component;
TLightGroup*& left_orbit_combo_light_group = tag_left_orbit_combo_light_group.Component;
TLightGroup*& left_orbit_light_group = tag_left_orbit_light_group.Component;  // DAT_0043b5c4
TLight*& lite519 = tag_v_lite519.Component;  // DAT_0043c39c
TLight*& lite520 = tag_v_lite520.Component;  // DAT_0043c3b4
TLight*& lite521 = tag_v_lite521.Component;  // DAT_0043c3cc
TLight*& lite524 = tag_v_lite524.Component;  // DAT_0043b134
TLight*& lite525 = tag_v_lite525.Component;  // DAT_0043b14c
TLight*& lite526 = tag_v_lite526.Component;  // DAT_0043b164
TLight*& lite527 = tag_v_lite527.Component;
TLight*& lite533 = tag_v_lite533.Component;  // DAT_0043b734
TLight*& lite545 = tag_v_lite545.Component;  // tavern window light hit by a_sink7
TLight*& lite546 = tag_v_lite546.Component;  // tavern window light hit by a_sink6
TLight*& lite547 = tag_v_lite547.Component;  // tavern window light hit by a_sink5; also the 3-bit tavern mask
TLight*& lite560 = tag_v_lite560.Component;  // DAT_0043bd6c <-> s_magnet1
TLight*& lite561 = tag_v_lite561.Component;  // DAT_0043bd84 <-> s_magnet2
TLight*& lite562 = tag_v_lite562.Component;  // DAT_0043bd9c <-> s_magnet3
TLight*& lite563 = tag_v_lite563.Component;  // DAT_0043bdb4 <-> s_magnet4
TLight*& lite564 = tag_v_lite564.Component;  // DAT_0043bdcc <-> s_magnet5
TLight*& lite565 = tag_v_lite565.Component;  // DAT_0043bde4 <-> s_magnet6
TLight*& lite567 = tag_v_lite567.Component;  // ship-sink indicator
TLight*& lite568 = tag_v_lite568.Component;  // ship-sink extra ball, collected in normal play only
TLight*& lite569 = tag_v_lite569.Component;  // Shoot Again; its own handler (0x00426080) steps the phase
TLight*& lite570 = tag_v_lite570.Component;  // Ship's Hold -- lit by the ship sink during Mutiny
TLight*& lite600 = tag_v_lite600.Component;  // left orbit
TLight*& lite601 = tag_v_lite601.Component;
TLight*& lite603 = tag_v_lite603.Component;  // left island ramp
TLight*& lite604 = tag_v_lite604.Component;
TLight*& lite608 = tag_v_lite608.Component;  // right orbit
TLight*& lite609 = tag_v_lite609.Component;
TLight*& lite611 = tag_v_lite611.Component;  // right island ramp
TLight*& lite612 = tag_v_lite612.Component;
TLight*& lite626 = tag_v_lite626.Component;  // right island ramp
TLight*& lite627 = tag_v_lite627.Component;
TLight*& lite629 = tag_v_lite629.Component;  // left island ramp
TLight*& lite630 = tag_v_lite630.Component;
TLight*& lite631 = tag_v_lite631.Component;  // left orbit
TLight*& lite632 = tag_v_lite632.Component;
TLight*& lite634 = tag_v_lite634.Component;  // right orbit
TLight*& lite635 = tag_v_lite635.Component;
TLight*& mlite104 = tag_v_mlite104.Component;  // DAT_0043b224
TLight*& mlite106 = tag_v_mlite106.Component;  // DAT_0043b1dc
TLight*& mlite107 = tag_v_mlite107.Component;  // DAT_0043b1c4
TLight*& mlite110 = tag_v_mlite110.Component;  // DAT_0043b20c
TLight*& mlite44 = tag_v_mlite44.Component;  // DAT_0043b1ac
TLight*& mlite51 = tag_v_mlite51.Component;  // on the ship sink; lit solid by Peg Leg's Loot
TPinballComponent*& right_island_combo_light_group = tag_right_island_combo_light_group.Component;
TLightGroup*& right_orbit_combo_light_group = tag_right_orbit_combo_light_group.Component;
TLightGroup*& right_orbit_light_group = tag_right_orbit_light_group.Component;  // DAT_0043b6ac
TLightGroup*& right_wind_light_group = tag_right_wind_light_group.Component;
TPinballComponent*& s_bwal610 = tag_s_bwal610.Component;
TPinballComponent*& s_launcher1 = tag_s_launcher1.Component;  // DAT_0043abdc
TPinballComponent*& s_magnet1 = tag_s_magnet1.Component;
TPinballComponent*& s_magnet2 = tag_s_magnet2.Component;
TPinballComponent*& s_magnet3 = tag_s_magnet3.Component;
TPinballComponent*& s_magnet4 = tag_s_magnet4.Component;
TPinballComponent*& s_magnet5 = tag_s_magnet5.Component;
TPinballComponent*& s_magnet6 = tag_s_magnet6.Component;
TLightGroup*& special_treasure_light_group = tag_special_treasure_light_group.Component;  // DAT_0043bd54 -- MessageField is the
TSound*& tabb08wave = tag_tabb08wave.Component;  // tavern sink
TSound*& tabb11wave = tag_tabb11wave.Component;  // hideaway swallow
TSound*& tabb27wave = tag_tabb27wave.Component;  // ship sink
TSound*& tabb39wave = tag_tabb39wave.Component;  // DAT_0043c1ec -- into the brig
TSound*& tabb40wave = tag_tabb40wave.Component;  // ship wire ramp, outside Attack Ship
TSound*& tabb41wave = tag_tabb41wave.Component;  // cannon wire ramp
TSound*& tabb55wave = tag_tabb55wave.Component;  // lava sink timer, outside Lava-Ball
TSound*& tabb57wave = tag_tabb57wave.Component;  // "Ship Captured"
TSound*& tabb70wave = tag_tabb70wave.Component;  // hideaway during Bermuda Triangle

} // namespace pirates
