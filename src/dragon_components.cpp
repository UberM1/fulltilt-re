// dragon_components.cpp -- the cross-slice component bindings for Dragon's Keep.
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
// names come from DRAGON.EXE's component table, so the group names are exact.

#include "pch.h"
#include "control.h"

#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace dragon
{

// Tags no slice declares with its concrete type; control_dragon.cpp declares these
// `extern` against the types below rather than the generic one it would default to.
component_tag<TSound> tag_tabc10wave = {"tabc10wave"};
component_tag<TSound> tag_tabc16wave = {"tabc16wave"};
component_tag<TSound> tag_tabc56wave = {"tabc56wave"};

// Tags another slice owns.
extern component_tag<TPinballComponent> tag_a_dvtraa;
extern component_tag<TPinballComponent> tag_a_dvtrab;
extern component_tag<TPinballComponent> tag_a_dvtrda;
extern component_tag<TPinballComponent> tag_a_dvtrdb;
extern component_tag<TPinballComponent> tag_a_sink1;
extern component_tag<TPinballComponent> tag_a_sink2;
extern component_tag<TPinballComponent> tag_a_sink4;
extern component_tag<TLightGroup> tag_dragon_fire_light_group;
extern component_tag<TLight> tag_v_lite114;
extern component_tag<TLight> tag_v_lite22;
extern component_tag<TLight> tag_v_lite63;
extern component_tag<TLight> tag_v_lite64;
extern component_tag<TLight> tag_v_lite91;
extern component_tag<TPinballComponent> tag_mode_timer;
extern component_tag<TLightGroup> tag_spell_spot_light_group;
extern component_tag<TSound> tag_tabc04wave;
extern component_tag<TSound> tag_tabc13wave;
extern component_tag<TSound> tag_tabc24wave;
extern component_tag<TSound> tag_tabc35wave;
extern component_tag<TSound> tag_tabc51wave;
extern component_tag<TLightGroup> tag_well_light_group;

// The aliases themselves.
TPinballComponent*& a_dvtraa = tag_a_dvtraa.Component;
TPinballComponent*& a_dvtrab = tag_a_dvtrab.Component;
TPinballComponent*& a_dvtrda = tag_a_dvtrda.Component;
TPinballComponent*& a_dvtrdb = tag_a_dvtrdb.Component;
TPinballComponent*& a_sink1 = tag_a_sink1.Component;  // fire lizard sink, left
TPinballComponent*& a_sink2 = tag_a_sink2.Component;  // fire lizard sink, right
TPinballComponent*& a_sink4 = tag_a_sink4.Component;  // the well
TLightGroup*& dragon_fire_light_group = tag_dragon_fire_light_group.Component;
TLight*& lite114 = tag_v_lite114.Component;  // Slay Dragon "hits left" counter
TLight*& lite22 = tag_v_lite22.Component;  // the 'D' of D-R-A-G-O-N, lit by the fire lizard pair
TLight*& lite63 = tag_v_lite63.Component;  // fire lizard sink 1's own light; also the shared hit counter
TLight*& lite64 = tag_v_lite64.Component;  // fire lizard sink 2's own light
TLight*& lite91 = tag_v_lite91.Component;  // Fire Lizards multiplier rung (0..10, x100,000)
TPinballComponent*& mode_timer = tag_mode_timer.Component;
TLightGroup*& spell_spot_light_group = tag_spell_spot_light_group.Component;
TSound*& tabc04wave = tag_tabc04wave.Component;
TSound*& tabc10wave = tag_tabc10wave.Component;
TSound*& tabc13wave = tag_tabc13wave.Component;
TSound*& tabc16wave = tag_tabc16wave.Component;
TSound*& tabc24wave = tag_tabc24wave.Component;
TSound*& tabc35wave = tag_tabc35wave.Component;
TSound*& tabc51wave = tag_tabc51wave.Component;
TSound*& tabc56wave = tag_tabc56wave.Component;
TLightGroup*& well_light_group = tag_well_light_group.Component;

} // namespace dragon
