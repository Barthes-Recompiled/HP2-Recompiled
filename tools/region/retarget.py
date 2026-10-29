#!/usr/bin/env python3
"""
Retarget the hand-written sources to another regional build of the game.

    py -3 tools/region/retarget.py --port port --xbe game/default.xbe --out port/src/recomp/region
    py -3 tools/region/retarget.py --port port --profile port/region/<name>.json --out <dir>

The overrides, icall seeds and patch_gen.py labels are written in the guest addresses of one XBE, the
canonical one. For another supported XBE (a profile in port/region/ whose sha1 matches), this writes copies of
every file that holds such addresses into --out, keeping their paths relative to --port. In the copies,
`sub_XXXXXXXX` / `loc_XXXXXXXX` names and hex literals inside the image are rewritten through the profile's
segment map, keeping the width and case of each literal. Files that include a rewritten header are copied as
well, so their quote includes find the copy. A stamp, region.cmake, lists the copied sources for CMake.

If the XBE is the canonical one, an earlier --out folder is removed and nothing is written. If no profile
matches, it stops and lists the supported SHA-1s.

A literal in code that cannot be mapped is an error, reported with file:line, and nothing is written. Three
things resolve one: the profile's `explicit` entries (bounds such as the end of .text, functions whose code
differs between the regions), port/region/retarget.json's `numbers` (values that look like addresses but are
plain numbers, by file), or a mapping segment with high confidence. In comments, an unmappable literal is
left as it is.
"""

import argparse
import fnmatch
import hashlib
import json
import os
import pathlib
import re
import shutil
import sys

STAMP = "region.cmake"

NAME_RE = re.compile(r"(?:(?<=sub_)|(?<=loc_))([0-9A-Fa-f]{8})(?![0-9A-Za-z])")
HEX_RE = re.compile(r"(?<![\w.])0([xX])([0-9A-Fa-f]{5,8})(?=[uUlL]*(?![\w.]))")
C_TOK = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
PY_TOK = re.compile(r'#[^\n]*|""".*?"""|\'\'\'.*?\'\'\'|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.S)
# a macro that pastes a parameter into a name, e.g. #define FALLBACK(addr) ... sub_##addr##_gen(): the bare
# 8 hex digit arguments it is called with are addresses too
DEFINE_RE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(\w+)\(([^)]*)\)((?:[^\n]*\\\r?\n)*[^\n]*)", re.M)
BARE_HEX = re.compile(r"(?<!\w)[0-9A-Fa-f]{8}(?!\w)")
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*"([^"]+)"', re.M)
IMAGE_START = 0x11000  # below this (header, small numbers) nothing is retargeted


def h(v):
    return int(v, 16) if isinstance(v, str) else v


class Profile:
    def __init__(self, path):
        with open(path) as f:
            p = json.load(f)
        self.path, self.name, self.sha1 = path, p["name"], p["sha1"]
        self.canonical = p.get("canonical", False)
        self.image_end = h(p.get("image_end", 0))
        self.segs = sorted(({"start": h(s["from_start"]), "end": h(s["from_end"]), "delta": s["delta"],
                             "confidence": s["confidence"]} for s in p.get("segments", [])),
                           key=lambda s: s["start"])
        self.starts = [s["start"] for s in self.segs]
        self.explicit = {h(e["from"]): h(e["to"]) for e in p.get("explicit", [])}
        # functions the canonical XBE's disassembly finds by a coincidence (a constant that looks like a
        # pointer) that this XBE's would not: seeded so both get the same function boundaries
        self.extra_seeds = [h(e["start"]) for e in p.get("extra_seeds", [])]

    def map(self, v):
        """(new value, how) or (None, why)."""
        if v in self.explicit:
            return self.explicit[v], "explicit"
        import bisect
        i = bisect.bisect_right(self.starts, v) - 1
        if i < 0 or v >= self.segs[i]["end"]:
            return None, "unmapped"
        s = self.segs[i]
        if s["confidence"] != "high":
            return None, "%s-confidence segment" % s["confidence"]
        return v + s["delta"], "segment"


def load_profiles(d):
    out = []
    for p in sorted(pathlib.Path(d).glob("*.json")):
        with open(p) as f:
            if "sha1" in json.load(f):
                out.append(Profile(p))
    return out


def sha1_of(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


def comment_spans(text, path):
    tok = {".c": C_TOK, ".cpp": C_TOK, ".h": C_TOK, ".hpp": C_TOK, ".py": PY_TOK}.get(path.suffix)
    if tok is None:
        return []
    return [(m.start(), m.end()) for m in tok.finditer(text) if m.group()[0] in "/#" or m.group()[:3] in ('"""', "'''")]


def fmt_like(orig, v):
    upper = not any(c in "abcdef" for c in orig)
    s = "%0*X" % (len(orig), v) if upper else "%0*x" % (len(orig), v)
    return s


class Retarget:
    def __init__(self, port, profile, cfg):
        self.port, self.prof, self.cfg = port, profile, cfg
        def entries(key):  # {"values": [...], "files": [globs], "why": ...}
            return [(None if v == "*" else h(v), n.get("files", ["*"])) for n in cfg.get(key, []) for v in n["values"]]
        self.numbers, self.addresses = entries("numbers"), entries("addresses")
        self.failures = []
        self.stats = {"code": 0, "comment": 0, "numbers": 0, "comment_left": 0, "names": 0}

    @staticmethod
    def listed(entries, rel, v):
        return any(n in (None, v) and any(fnmatch.fnmatch(rel, g) for g in globs) for n, globs in entries)

    @staticmethod
    def looks_like_number(v):
        """Round values and masks: as likely a size or flag as an address, so a person decides."""
        return v % 0x100 == 0 or bin(v).count("1") <= 2

    def paste_macros(self, texts):
        """{macro name: index of a parameter it pastes into sub_/loc_} over all scanned files."""
        self.macros = {}
        defs = [(name, [x.strip() for x in params.split(",")], body)
                for t in texts.values() for name, params, body in DEFINE_RE.findall(t)]
        for name, ps, body in defs:
            for i, prm in enumerate(ps):
                if prm and re.search(r"(?:sub|loc)_##\s*%s\b" % re.escape(prm), body):
                    self.macros[name] = i
        # and macros that hand a parameter on to one, e.g. COMPLEX_STATE(addr, rs) -> HLE(addr, 1)
        grew = True
        while grew:
            grew = False
            for name, ps, body in defs:
                if name in self.macros:
                    continue
                for inner, idx in list(self.macros.items()):
                    for m in re.finditer(r"\b%s\s*\(" % re.escape(inner), body):
                        args = body[m.end():].split(")")[0].split(",")
                        if idx < len(args) and args[idx].strip() in ps:
                            self.macros[name] = ps.index(args[idx].strip())
                            grew = True
                            break
                    if name in self.macros:
                        break

    def macro_args(self, text):
        """Match-like (start, digits) for each bare address argument of a paste macro."""
        out = []
        for name, idx in getattr(self, "macros", {}).items():
            for m in re.finditer(r"\b%s\s*\(" % re.escape(name), text):
                pos, depth, arg, start = m.end(), 0, 0, m.end()
                while pos < len(text):
                    c = text[pos]
                    if c in "([{":
                        depth += 1
                    elif c in ")]}" and depth:
                        depth -= 1
                    elif c in ",)" and not depth:
                        if arg == idx:
                            a = text[start:pos]
                            mm = re.fullmatch(r"\s*([0-9A-Fa-f]{8})\s*", a)
                            if mm:
                                out.append((start + mm.start(1), mm.group(1)))
                            break
                        if c == ")":
                            break
                        arg, start = arg + 1, pos + 1
                    pos += 1
        return out

    def rewrite(self, rel, text):
        """Returns (new text, number of literals changed); records failures."""
        path = pathlib.Path(rel)
        spans = comment_spans(text, path)
        starts = [a for a, _ in spans]
        import bisect

        def in_comment(pos):
            i = bisect.bisect_right(starts, pos) - 1
            return i >= 0 and pos < spans[i][1]

        class Hit:  # a macro argument, shaped like a NAME_RE match
            def __init__(self, a, d):
                self.a, self.d = a, d

            def start(self, g=0):
                return self.a

            def group(self, g):
                return self.d
        edits = []
        for rx, is_name in ((NAME_RE, True), (HEX_RE, False), (None, True)):
            hits = rx.finditer(text) if rx else [Hit(a, d) for a, d in self.macro_args(text)]
            for m in hits:
                digits = m.group(1) if is_name else m.group(2)
                v = int(digits, 16)
                if not is_name and not IMAGE_START <= v < self.prof.image_end:
                    continue
                if not is_name and self.listed(self.numbers, rel, v):
                    self.stats["numbers"] += 1
                    continue
                comment = in_comment(m.start())
                if (not is_name and v not in self.prof.explicit and self.looks_like_number(v)
                        and not self.listed(self.addresses, rel, v)):
                    if comment:
                        self.stats["comment_left"] += 1
                    else:
                        line = text.count("\n", 0, m.start()) + 1
                        self.failures.append("%s:%d: 0x%s: looks like a plain number; list it in retarget.json's "
                                             "numbers or addresses" % (rel, line, digits))
                    continue
                new, how = self.prof.map(v)
                if new is None:
                    if comment:
                        self.stats["comment_left"] += 1
                    else:
                        line = text.count("\n", 0, m.start()) + 1
                        self.failures.append("%s:%d: %s%s: %s" % (
                            rel, line, "sub_/loc_ " if is_name else "0x", digits, how))
                    continue
                if new != v:
                    a = m.start(1) if is_name else m.start(2)
                    edits.append((a, a + len(digits), fmt_like(digits, new)))
                    self.stats["comment" if comment else "code"] += 1
                    self.stats["names"] += is_name
        edits.sort()
        out, last = [], 0
        for a, b, s in edits:
            out.append(text[last:a])
            out.append(s)
            last = b
        out.append(text[last:])
        return "".join(out), len(edits)

    def files(self):
        seen = []
        for g in self.cfg["scan"]:
            for p in sorted(self.port.glob(g)):
                rel = p.relative_to(self.port).as_posix()
                if p.is_file() and rel not in seen and not any(
                        fnmatch.fnmatch(rel, x) for x in self.cfg.get("skip", [])):
                    seen.append(rel)
        return seen

    def resolve_include(self, rel, name):
        base = (self.port / rel).parent
        for d in [base] + [self.port / i for i in self.cfg.get("include_dirs", [])]:
            p = (d / name).resolve()
            if p.is_file():
                try:
                    return p.relative_to(self.port.resolve()).as_posix()
                except ValueError:
                    return None
        return None

    def run(self):
        texts, changed = {}, {}
        originals = {rel: read(self.port / rel) for rel in self.files()}
        self.paste_macros(originals)
        for rel, text in originals.items():
            new, n = self.rewrite(rel, text)
            texts[rel] = new
            if n:
                changed[rel] = n
        # a file that includes a copied header must be copied too, or it would include the original
        copy = set(changed)
        while True:
            more = {rel for rel, t in texts.items() if rel not in copy and any(
                self.resolve_include(rel, inc) in copy for inc in INCLUDE_RE.findall(t))}
            if not more:
                break
            copy |= more
        return texts, changed, sorted(copy)


def read(path):
    with open(path, encoding="utf-8", errors="surrogateescape", newline="") as f:
        return f.read()  # line endings as they are, so a copy differs only in its literals


def blank_literals(text):
    return BARE_HEX.sub("#", HEX_RE.sub("0x#", NAME_RE.sub("#", text)))


def append_seeds(text, starts):
    """Add {"start": ...} entries to icall_seeds.json's list, in the file's own layout."""
    nl = "\r\n" if "\r\n" in text else "\n"
    end = text.rstrip().rfind("]")
    body = text[:end].rstrip()
    add = "".join("," + nl + " {" + nl + '  "start": "0x%08X"' % a + nl + " }" for a in starts)
    return body + add + nl + text[end:]


def remove_out(out):
    if out.exists():
        if not (out / STAMP).exists() and any(out.iterdir()):
            sys.exit("retarget: %s exists without %s; not removing it" % (out, STAMP))
        shutil.rmtree(out)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    ap.add_argument("--port", required=True, help="the port folder (holds region/, src/, icall_seeds.json)")
    ap.add_argument("--xbe", help="pick the profile by this XBE's SHA-1")
    ap.add_argument("--profile", help="use this profile instead")
    ap.add_argument("--out", required=True, help="folder for the retargeted copies (removed and rewritten)")
    a = ap.parse_args(argv)

    port = pathlib.Path(a.port)
    rdir = port / "region"
    out = pathlib.Path(a.out)
    profiles = load_profiles(rdir)
    if a.profile:
        prof = Profile(a.profile)
    else:
        sha = sha1_of(a.xbe)
        prof = next((p for p in profiles if p.sha1 == sha), None)
        if prof is None:
            lines = ["retarget: %s (sha1 %s) is not a supported XBE. Supported:" % (a.xbe, sha)]
            lines += ["  %s  %s%s" % (p.sha1, p.name, " (canonical)" if p.canonical else "") for p in profiles]
            sys.exit("\n".join(lines))
    if prof.canonical:
        remove_out(out)
        print("retarget: %s is the canonical XBE (%s); the sources are used as they are" % (prof.name, prof.sha1))
        return 0

    with open(rdir / "retarget.json") as f:
        cfg = json.load(f)
    rt = Retarget(port, prof, cfg)
    texts, changed, copy = rt.run()
    if rt.failures:
        sys.stderr.write("retarget: %d literal(s) in code cannot be mapped to %s:\n" % (len(rt.failures), prof.name))
        sys.stderr.write("".join("  %s\n" % f for f in rt.failures))
        sys.stderr.write("add a profile `explicit` entry, or a retarget.json `numbers` entry for a plain number\n")
        return 1

    # self-check: a copy differs from its original only in address literals
    for rel in copy:
        orig = read(port / rel)
        if blank_literals(orig) != blank_literals(texts[rel]):
            sys.exit("retarget: internal error: %s changed outside address literals" % rel)
    if prof.extra_seeds:
        rel = "icall_seeds.json"
        texts[rel] = append_seeds(texts[rel], prof.extra_seeds)
        if rel not in copy:
            copy.append(rel)
    remove_out(out)
    for rel in copy:
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        with open(dst, "w", encoding="utf-8", errors="surrogateescape", newline="") as f:
            f.write(texts[rel])
    sources = [r for r in copy if pathlib.Path(r).suffix in (".c", ".cpp")]
    with open(out / STAMP, "w", newline="\n") as f:
        f.write("# written by tools/region/retarget.py; regen.sh rewrites it\n")
        f.write('set(REGION_PROFILE "%s")\nset(REGION_SHA1 "%s")\n' % (prof.name, prof.sha1))
        f.write("set(REGION_FILES\n%s)\n" % "".join("    %s\n" % r for r in sources))
    s = rt.stats
    print("retarget: %s (%s): %d files written (%d with rewritten literals, %d for their includes)" % (
        prof.name, prof.sha1, len(copy), len(changed), len(copy) - len(changed)))
    if prof.extra_seeds:
        print("retarget: %d extra icall seed(s): %s" % (len(prof.extra_seeds),
              " ".join("0x%08X" % a for a in prof.extra_seeds)))
    print("retarget: literals rewritten: %d in code, %d in comments (%d of them sub_/loc_ names); "
          "%d listed plain numbers kept; %d left in comments (unmappable or number-like)" % (
              s["code"], s["comment"], s["names"], s["numbers"], s["comment_left"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
