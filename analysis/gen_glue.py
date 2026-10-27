"""Emit src/<table>_components.cpp: the cross-slice component bindings.

The rule slices were each written against the others' interfaces, declaring the tags
and component aliases they touch as `extern`. For many components no slice ever
defined them. This walks the slices' own `extern T* <name>;` declarations, resolves
each name to a group name in the binary's component table, and emits exactly one
definition per tag and one alias per component.

    python3 analysis/gen_glue.py

Run it before analysis/gen_control.py: the generated control_<table>.cpp declares a
tag `extern` with whatever type a src/ file gives it, and these files are where the
concrete types (TLight, TSound, TLightGroup) for the shared components come from.
"""

import sys,re,pathlib
sys.path.insert(0,'analysis')
from dump_simple import simple_components
import gen_control as g

ROOT=pathlib.Path("/home/ubr/fulltilt-re")
src=ROOT/"src"
# Both spellings: a slice that has not been retyped yet says `extern T* x;`, and
# one that has says `extern T*& x;`. Matching only the first made this generator
# non-idempotent -- a second run emitted an empty file and broke the link.
E=re.compile(r"^extern\s+(T\w+)\*&?\s+(\w+);(.*)$")
A=re.compile(r"^(T\w+)\*&\s+(\w+)\s*=\s*(tag_\w+)\.Component;")

HDR = """// {pre}_components.cpp -- the cross-slice component bindings for {title}.
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
// names come from {exe}'s component table, so the group names are exact.

#include "pch.h"
#include "control.h"

#include "TBlocker.h"
#include "TLight.h"
#include "TLightGroup.h"
#include "TPinballTable.h"
#include "TSound.h"
#include "TTextBox.h"

namespace {pre}
{{
"""

def run():
    for exe,pre,title in [('binaries/win95/DRAGON.EXE','dragon',"Dragon's Keep"),
                          ('binaries/win95/PIRATES.EXE','pirates','Skulduggery')]:
        rows=g.table(str(ROOT/exe))
        names=set(n for n,_,_ in rows)|set(nm for _a,_p,nm in simple_components(str(ROOT/exe)))
        files=[f for f in sorted(src.glob(f"{pre}_*.cpp")) if f.name!=f"{pre}_components.cpp"]
        srctags={}; adef=set(); ext={}; srctags_owner=set()
        for f in files:
            txt=f.read_text()
            for m in re.finditer(r"^component_tag<(\w+)>\s+tag_(\w+)\s*=",txt,re.M):
                srctags.setdefault(m.group(2),m.group(1))
            for line in txt.splitlines():
                m=A.match(line)
                if m: adef.add(m.group(2))
                m=re.match(r"^(T\w+)\*&\s+(\w+)\s*=\s*control::", line)
                if m: srctags_owner.add(m.group(2))
                m=E.match(line)
                if m: ext.setdefault(m.group(2),(m.group(1),m.group(3).strip()))
        missing=sorted(k for k in ext if k not in adef)
        tagdefs=[]; externs=[]; aliases=[]
        unresolved=[]
        for name in missing:
            typ,comment=ext[name]
            if name=="TableG":
                # The port keeps one table pointer in control; a per-table control class
                # would own its own. Only emitted for a table whose slices all declare it.
                if "TableG" not in srctags_owner:
                    aliases.append("TPinballTable*& TableG = control::TableG;")
                continue
            cands=[name,"v_"+name]
            if name.endswith("_lights"): cands.append(name[:-7]+"_light_group")
            grp=next((c for c in cands if c in names),None)
            if grp is None:
                unresolved.append(name); continue
            tag=f"tag_{grp}"
            if grp in srctags:
                externs.append(f"extern component_tag<{srctags[grp]}> {tag};")
                atyp=srctags[grp]
            else:
                tagdefs.append(f'component_tag<{typ}> {tag} = {{"{grp}"}};')
                atyp=typ
            cm=f"  {comment}" if comment else ""
            aliases.append(f"{atyp}*& {name} = {tag}.Component;{cm}")
        out=[HDR.format(pre=pre,title=title,exe=pathlib.Path(exe).name)]
        if tagdefs:
            out.append("// Tags no slice declares with its concrete type; control_%s.cpp declares these\n"
                       "// `extern` against the types below rather than the generic one it would default to.\n"
                       % pre + "\n".join(tagdefs) + "\n")
        if externs:
            out.append("// Tags another slice owns.\n"+"\n".join(externs)+"\n")
        out.append("// The aliases themselves.\n"+"\n".join(aliases)+"\n")
        out.append(f"}} // namespace {pre}\n")
        (src/f"{pre}_components.cpp").write_text("\n".join(out))
        print(f"{pre}: {len(tagdefs)} tags defined, {len(externs)} extern, {len(aliases)} aliases"
              + (f", UNRESOLVED {unresolved}" if unresolved else ""))
run()
