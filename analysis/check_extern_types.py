#!/usr/bin/env python3
"""Find shared variables that two translation units declare with different types.

C++ puts no type in a variable's mangled name, so

    // a.cpp
    TPinballComponent* last_scoring_component = nullptr;
    // b.cpp
    extern int last_scoring_component;

links without a word of complaint, and every access from b.cpp is then the wrong width
and the wrong interpretation. `last_scoring_component = 0` in b.cpp became a 32-bit store
into an 8-byte pointer: it zeroed the low half, left the high half standing, and handed
the next caller a non-null pointer to nothing. That crashed a real game on Skulduggery --
one ball serve, one score -- and the core dump's `rdi = 0x5b9d00000000`, low word gone and
top word intact, is the signature.

The project had already been bitten by the same class twice: 160 `extern T* x;`
declarations against `T*& x` definitions, which yielded the address of a tag's field in
place of the component. Those were found by hand. This finds them mechanically.

The rules/ sources share state through `extern` rather than a header -- that is how the
slices were written, against each other's interfaces -- so the compiler never sees two
spellings of the same name at once. This script does: it reads every definition and every
`extern` declaration out of src/*.cpp, normalises the type, and reports every name whose
spellings disagree.

    python3 analysis/check_extern_types.py          # audit src/
    python3 analysis/check_extern_types.py --quiet  # exit 1 on any mismatch, no output

Array extents are part of the type and are compared. They have to be: `extern T x[8];`
against a `T x[5];` definition links just as silently as a wrong base type, and every
access past index 4 then walks off the end of the real object into whatever the allocator
or the linker put next -- which is how a heap block's metadata gets overrun and the abort
lands in a `free` far away from the write. An extent left unspecified (`extern T x[];`)
is legal C++ and agrees with any definition, so it is not reported; two *specified*
extents that differ are.

Limits, stated rather than hidden: it is a regex over declarations, not a parser. It skips
functions, anything inside a function body (by brace depth), and extents spelled as
constant expressions rather than literals (`[GemCount]` against `[9]`) are compared as
text, so an agreeing pair spelled two ways reads as a mismatch and a disagreeing pair
spelled one way reads as clean. A clean run means "no two spellings disagree in a way this
can see", which is weaker than "the types agree".
"""

import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "src"

# `extern TPinballComponent*& a_sink1;` / `TLight* lite63 = ...;` / `int phase = 0;`
#
# Anchored at column 0 on purpose. Namespace-scope declarations in these files are
# unindented and function-local ones are indented, which separates the two without
# needing to parse: counting braces does not work here, because every one of these
# files wraps its contents in `namespace dragon {` / `namespace pirates {`.
#
# The extents are captured whole -- `[8]`, `[8][4]`, `[]` -- because they are part of the
# type and a disagreement in them is as silent at link time as a disagreement in the base
# type, and more dangerous: it overruns the object rather than misreading it.
DECL = re.compile(
    r"^(?P<extern>extern\s+)?"
    r"(?P<type>(?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^>]*>)?(?:\s*[*&]+)?)"
    r"\s+(?P<name>[A-Za-z_]\w*)\s*"
    r"(?P<extents>(?:\[[^\]]*\]\s*)*)"
    r"(?P<tail>[\[=;])"
)

# Not variables.
SKIP_TYPES = {"return", "else", "case", "break", "if", "while", "for", "switch", "using",
              "namespace", "struct", "class", "enum", "typedef", "template", "constexpr",
              "static", "inline", "void", "do", "goto", "delete", "new", "sizeof"}


def normalise(type_text):
    """The base type, without any array extents."""
    t = re.sub(r"\s+", " ", type_text.strip())
    t = t.replace("const ", "")
    t = t.replace(" *", "*").replace(" &", "&")
    return t


def normalise_extents(extents_text, tail):
    """`[8][4]` -> ('8', '4'); `[]` -> ('',); no array at all -> ()."""
    text = re.sub(r"\s+", "", extents_text or "")
    if not text and tail == "[":
        # Backtracked out of the extents group: an array whose extent the regex did
        # not reach. Treat it as unspecified rather than as a non-array.
        return ("",)
    return tuple(re.findall(r"\[([^\]]*)\]", text))


def extents_disagree(a, b):
    """True only when both spellings say something and say different things.

    `extern T x[];` leaves the extent unspecified, which is legal C++ and agrees with
    whatever the definition says, so an empty dimension matches any dimension. The
    *rank* still has to match: `T x[8]` against `T x[8][4]` is a real disagreement.
    """
    if len(a) != len(b):
        return True
    return any(da and db and da != db for da, db in zip(a, b))


def declarations():
    """{qualified name: {(base type, extents tuple): [(file, line, is_extern)]}}"""
    # The brace may be on the same line as the keyword or on the next one -- both
    # spellings occur in these files, and treating the next-line form as an unnamed
    # frame suppressed every declaration in pirates_common.cpp.
    ns_open = re.compile(r"^\s*namespace\s*(\w*)\s*(\{)?\s*$|^\s*namespace\s*(\w*)\s*\{")
    out = {}
    for path in sorted(SRC.glob("*.cpp")):
        # A brace stack, so that closing an inner `namespace ft {` restores `pirates`
        # rather than dropping to global or -- worse -- leaving `ft` on the stack for
        # the rest of the file. Matching on the `} // namespace` comment did the latter,
        # and the qualified names it produced (`pirates::FtMessageCode::...`) silently
        # stopped colliding with the real ones, so the audit reported a clean run while
        # the very mismatch it was written for sat in the tree. Only brace counting is
        # reliable here. Frames are named when they are namespaces and None otherwise.
        stack = []
        pending_ns = None
        for lineno, line in enumerate(path.read_text().splitlines(), 1):
            code = line.split("//")[0]

            if not any(f is None for f in stack):
                m = DECL.match(code)
                if m and m.group("type").split()[0] not in SKIP_TYPES and "(" not in code:
                    t = (
                        normalise(m.group("type")),
                        normalise_extents(m.group("extents"), m.group("tail")),
                    )
                    namespace = "::".join(f for f in stack if f)
                    name = m.group("name")
                    key = f"{namespace}::{name}" if namespace else name
                    out.setdefault(key, {}).setdefault(t, []).append(
                        (path.name, lineno, bool(m.group("extern")))
                    )

            m_ns = ns_open.match(code)
            if m_ns:
                name = m_ns.group(1) or m_ns.group(3) or ""
                pending_ns = name
            for ch in code:
                if ch == "{":
                    stack.append(pending_ns if pending_ns is not None else None)
                    pending_ns = None
                elif ch == "}" and stack:
                    stack.pop()
            if code.strip() and not m_ns:
                pending_ns = None
    return out


def main():
    quiet = "--quiet" in sys.argv
    decls = declarations()
    bad = 0
    for name, spellings in sorted(decls.items()):
        if len(spellings) < 2:
            continue
        # A reference binding to the tag's field is the project's own idiom: `T*& x`
        # declared `extern T*&` elsewhere agrees. Only genuine disagreement counts, and
        # `T*` versus `T*&` is genuine -- it yields the address of the field, not the
        # component.
        #
        # Extents are the other half: two spellings that share a base type still
        # disagree if one says `[5]` and the other `[8]`, and an unspecified `[]`
        # agrees with both. So a name is only reported when some pair of its spellings
        # genuinely conflicts.
        keys = sorted(spellings)
        conflict = any(
            a[0] != b[0] or extents_disagree(a[1], b[1])
            for i, a in enumerate(keys)
            for b in keys[i + 1:]
        )
        if not conflict:
            continue
        bad += 1
        if quiet:
            continue
        print(f"{name}:")
        for t, where in sorted(spellings.items()):
            spelling = t[0] + "".join(f"[{d}]" for d in t[1])
            for file, lineno, is_extern in where:
                kind = "extern" if is_extern else "DEFN  "
                print(f"    {kind} {spelling:<28} {file}:{lineno}")
    if not quiet:
        print(f"\n{len(decls)} shared names, {bad} with disagreeing spellings")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
