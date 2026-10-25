#!/usr/bin/env python3
"""Fixes regen.sh applies to the generated code after a regeneration (python3 tools/patch_gen.py src/recomp/gen).

Four functions start with a conditional jump on the flags of their CALLER's compare (the caller does `cmp` or
`test`, then jumps into them). The lifter gives each function a fresh `_flags = 0`, so the branch was never taken:
the Whomping Willow's right hand ended its solo strike on its first frame, and three other checks were skipped.
Until the toolkit's lifter learns this, each entry test is rewritten here to the caller's compare. Every fix finds
its line by exact text inside its own function and fails loudly if the line is gone (a lifter change), so a regen
never drops one silently. Running it twice changes nothing.
"""
import glob
import re
import sys

# function, the lifted line, the fixed line
FIXES = [
    ("sub_00105D57",  # willow hand state 6 (the right hand's solo strike): waits for event 0x67
     "    if (_flags /* jne: not equal / not zero */) { g_seh_ebp = ebp; sub_00106547(); return; }",
     "    if (eax != 0x67u /* jne: flags of the caller's cmp eax, 0x67 (0x105F3A) - hand-fixed, the lifted _flags was always 0 */) { g_seh_ebp = ebp; sub_00106547(); return; }"),
    ("sub_00105F06",  # willow slam impact spawn: null check
     "    if (_flags /* je: equal / zero */) { g_seh_ebp = ebp; sub_00106547(); return; }",
     "    if (eax == 0 /* je: flags of the caller's cmp eax, 0 (0x1060DD) - hand-fixed, the lifted _flags was always 0 */) { g_seh_ebp = ebp; sub_00106547(); return; }"),
    ("sub_0009D928",
     "    if (_flags /* jne: not equal / not zero */) goto loc_0009D931;",
     "    if (eax != 2u /* jne: flags of the caller's cmp eax, 2 (0x9D79E) - hand-fixed, the lifted _flags was always 0 */) goto loc_0009D931;"),
    ("sub_000C0001",  # null check on [esi+0xB0]
     "    if (_flags /* je: equal / zero */) goto loc_000C000E;",
     "    if (eax == 0 /* je: flags of the caller's test eax, eax (0xBFFFF) - hand-fixed, the lifted _flags was always 0 */) goto loc_000C000E;"),
]


def main(gen):
    files = sorted(glob.glob(gen + "/recomp_0*.c"))
    texts = {f: open(f, encoding="utf-8", errors="surrogateescape").read() for f in files}
    failed = 0
    for func, old, new in FIXES:
        done = False
        for f, s in texts.items():
            m = re.search(r"^void %s(?:_gen)?\(void\)\n\{\n.*?^\}" % func, s, re.S | re.M)
            if not m:
                continue
            body = m.group(0)
            if new in body:
                print("%s: already fixed" % func)
            elif body.count(old) == 1:
                texts[f] = s[:m.start()] + body.replace(old, new) + s[m.end():]
                print("%s: fixed (%s)" % (func, f.rsplit("/", 1)[-1]))
            else:
                print("%s: the lifted line is not there (%d found): fix it by hand" % (func, body.count(old)))
                failed = 1
            done = True
            break
        if not done:
            print("%s: function not found" % func)
            failed = 1
    for f, s in texts.items():
        open(f, "w", encoding="utf-8", errors="surrogateescape").write(s)
    return failed


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "src/recomp/gen"))
