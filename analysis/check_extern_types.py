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

Limits, stated rather than hidden: it is a regex over declarations, not a parser. It skips
functions, anything inside a function body (by brace depth), and arrays whose extents are
spelled differently but mean the same. A clean run means "no two spellings disagree in a
way this can see", which is weaker than "the types agree".
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
DECL = re.compile(
    r"^(?P<extern>extern\s+)?"
    r"(?P<type>(?:const\s+)?[A-Za-z_][\w:]*(?:\s*<[^>]*>)?(?:\s*[*&]+)?)"
    r"\s+(?P<name>[A-Za-z_]\w*)\s*(?P<tail>[\[=;])"
)

# Not variables.
SKIP_TYPES = {"return", "else", "case", "break", "if", "while", "for", "switch", "using",
              "namespace", "struct", "class", "enum", "typedef", "template", "constexpr",
              "static", "inline", "void", "do", "goto", "delete", "new", "sizeof"}


def normalise(type_text, tail):
    t = re.sub(r"\s+", " ", type_text.strip())
    t = t.replace("const ", "")
    t = t.replace(" *", "*").replace(" &", "&")
    if tail == "[":
        t += "[]"
    return t


def declarations():
    """{qualified name: {normalised type: [(file, line, is_extern)]}}"""
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
                    t = normalise(m.group("type"), m.group("tail"))
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
        bad += 1
        if quiet:
            continue
        print(f"{name}:")
        for t, where in sorted(spellings.items()):
            for file, lineno, is_extern in where:
                kind = "extern" if is_extern else "DEFN  "
                print(f"    {kind} {t:<28} {file}:{lineno}")
    if not quiet:
        print(f"\n{len(decls)} shared names, {bad} with disagreeing spellings")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
