#!/usr/bin/env python3
"""
Map guest addresses between two regional builds of the same XBE.

    py -3 tools/region/xbe_align.py map   --from eur.xbe --to usa.xbe -o map.json
    py -3 tools/region/xbe_align.py addrs --map map.json --from eur.xbe --to usa.xbe FILE...
    py -3 tools/region/xbe_align.py profile --name NAME (--map map.json | --canonical eur.xbe) -o port/region/X.json

Regional builds of one title are usually the same program with a few functions
or data items grown, shrunk, inserted or removed. Everything after such a change
shifts by a constant, so the address map is piecewise constant: a short list of
(from range -> delta) segments.

`map` finds those segments. Each section is compared with its namesake in the
other image after masking every 32-bit value that points into the image (so a
relocated pointer compares equal to its counterpart). Unique 32-byte windows
common to both images give anchors; the longest increasing chain of anchors is
cut wherever the offset changes, and each cut is refined byte by byte. The part
of a section past its raw data (bss) has no bytes to compare, so its segments
are inferred from the pointers into it found in already aligned code and data.

Every segment is then checked: each absolute pointer inside it is moved to the
other image, and the value there must equal the pointer's target mapped through
the segment table. The share that passes is reported per segment.

`addrs` scans source files for guest addresses (sub_XXXXXXXX names and hex
literals inside the image) and says which ones map cleanly.

Only addresses, counts and hashes are written; no image bytes.
"""

import argparse
import bisect
import hashlib
import json
import re
import struct
import sys
from collections import Counter, defaultdict

try:
    import capstone
except ImportError:  # the byte scan below is the fallback
    capstone = None

# (entry, kernel thunk) XOR keys.
KEYS = {"retail": (0xA8FC57AB, 0x5B6D40B6), "debug": (0x94859D4B, 0xEFB1F152)}
SEC_EXEC = 0x4
SEC_WRITE = 0x1

W = 32          # anchor window, bytes
MIN_RUN = 64    # an anchor run shorter than this is treated as noise
EDGE = 16       # bytes compared when checking a single mapped address


class Section:
    def __init__(self, name, flags, va, vsize, raw, rsize, data):
        self.name, self.flags, self.va, self.vsize = name, flags, va, vsize
        self.rsize = rsize
        self.bytes = data[raw:raw + rsize]
        self.masked = None

    @property
    def end(self):
        return self.va + self.vsize


class Xbe:
    def __init__(self, path):
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != b"XBEH":
            raise ValueError("%s: not an XBE" % path)
        self.path = path
        self.sha1 = hashlib.sha1(data).hexdigest()
        self.base, = struct.unpack_from("<I", data, 0x104)
        count, hdrs = struct.unpack_from("<II", data, 0x11C)
        self.sections = []
        for i in range(count):
            fl, va, vs, raw, rs, na = struct.unpack_from(
                "<6I", data, hdrs - self.base + i * 56)
            o = na - self.base
            name = data[o:data.index(b"\0", o)].decode("latin-1")
            self.sections.append(Section(name, fl, va, vs, raw, rs, data))
        self.lo = self.base
        self.hi = max(s.end for s in self.sections)
        ep, th = struct.unpack_from("<I", data, 0x128)[0], struct.unpack_from("<I", data, 0x158)[0]
        for kind, (ke, kt) in KEYS.items():
            if self.lo <= ep ^ ke < self.hi:
                self.kind, self.entry, self.thunk = kind, ep ^ ke, th ^ kt
                break
        else:
            self.kind, self.entry, self.thunk = "unknown", None, None
        self._starts = [s.va for s in self.sections]
        for s in self.sections:
            s.masked = mask_pointers(s.bytes, self.lo, self.hi)

    def section_at(self, va):
        i = bisect.bisect_right(self._starts, va) - 1
        if i >= 0 and va < self.sections[i].end:
            return self.sections[i]
        return None

    def u32(self, va):
        s = self.section_at(va)
        if s is None:
            return None
        o = va - s.va
        if o + 4 <= s.rsize:
            return struct.unpack_from("<I", s.bytes, o)[0]
        return 0 if o >= s.rsize else None  # bss reads as zero

    def masked_at(self, va, n, raw=False):
        s = self.section_at(va)
        if s is None:
            return None
        o = va - s.va
        return (s.bytes if raw else s.masked)[o:o + n] if o < s.rsize else None


def mask_pointers(buf, lo, hi):
    """Zero the low three bytes of every unaligned 32-bit value in [lo, hi)."""
    out = bytearray(buf)
    a, b = lo >> 16, (hi - 1) >> 16
    pat = re.compile(rb"(?=..[%s-%s]\x00)" % (re.escape(bytes([a])), re.escape(bytes([b]))), re.S)
    for m in pat.finditer(buf):
        i = m.start()
        if i + 4 <= len(buf) and lo <= struct.unpack_from("<I", buf, i)[0] < hi:
            out[i:i + 3] = b"\0\0\0"
    return bytes(out)


# --- alignment -------------------------------------------------------------

def unique_windows(buf):
    seen = {}
    for i in range(len(buf) - W + 1):
        k = buf[i:i + W]
        if k.count(0) > W * 3 // 4:  # padding and pointer-dense windows say little
            continue
        h = hash(k)
        seen[h] = -1 if h in seen else i
    return seen


def chain(anchors):
    """Longest chain of anchors increasing in both offsets (anchors sorted by a)."""
    tails, tail_idx, prev = [], [], [-1] * len(anchors)
    for n, (_, b) in enumerate(anchors):
        k = bisect.bisect_left(tails, b)
        prev[n] = tail_idx[k - 1] if k else -1
        if k == len(tails):
            tails.append(b)
            tail_idx.append(n)
        else:
            tails[k], tail_idx[k] = b, n
    out, n = [], tail_idx[-1] if tail_idx else -1
    while n >= 0:
        out.append(anchors[n])
        n = prev[n]
    return out[::-1]


def ext_fwd(A, B, i, d, limit):
    while i < limit and 0 <= i + d < len(B) and A[i] == B[i + d]:
        i += 1
    return i


def ext_back(A, B, i, d, limit):
    while i > limit and 0 <= i - 1 + d < len(B) and A[i - 1] == B[i - 1 + d]:
        i -= 1
    return i


def align_section(A, B):
    """Return [(a_start, a_end, d)] with A[a] matching B[a + d] (masked bytes)."""
    ia, ib = unique_windows(A), unique_windows(B)
    anchors = sorted((i, ib[h]) for h, i in ia.items() if i >= 0 and ib.get(h, -1) >= 0)
    runs = []
    for a, b in chain(anchors):
        d = b - a
        if runs and runs[-1][2] == d:
            runs[-1][1] = a + W
        else:
            runs.append([a, a + W, d])
    runs = [r for r in runs if r[1] - r[0] >= MIN_RUN]
    merged = []
    for r in runs:
        if merged and merged[-1][2] == r[2]:
            merged[-1][1] = r[1]
        else:
            merged.append(r)
    if not merged:
        return []
    merged[0][0] = ext_back(A, B, merged[0][0], merged[0][2], 0)
    for r1, r2 in zip(merged, merged[1:]):
        e1 = ext_fwd(A, B, r1[1], r1[2], r2[0])
        s2 = ext_back(A, B, r2[0], r2[2], r1[1])
        s2 = max(s2, e1, e1 + r1[2] - r2[2])  # no overlap on either side
        r1[1], r2[0] = e1, min(s2, r2[1])
    merged[-1][1] = ext_fwd(A, B, merged[-1][1], merged[-1][2], len(A))
    # an edge gap of the same size on both sides is changed bytes, not inserted ones
    if merged[0][2] == 0:
        merged[0][0] = 0
    if len(A) - merged[-1][1] == len(B) - (merged[-1][1] + merged[-1][2]):
        merged[-1][1] = len(A)
    return [tuple(r) for r in merged if r[1] > r[0]]


# --- pointers --------------------------------------------------------------

_HEX = re.compile(r"0x([0-9a-f]+)")


def pointer_sites(x):
    """[(site_va, value)] for absolute 32-bit pointers into the image."""
    sites = []
    for s in x.sections:
        buf = s.bytes
        if s.flags & SEC_EXEC and capstone is not None:
            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            off = 0
            while off < len(buf):
                last = off
                for addr, size, _, ops in md.disasm_lite(buf[off:off + 0x10000], s.va + off):
                    last = addr - s.va + size
                    for h in _HEX.findall(ops):
                        v = int(h, 16)
                        if x.lo <= v < x.hi:
                            ins = buf[addr - s.va:last]
                            j = ins.find(struct.pack("<I", v))
                            if j >= 0:
                                sites.append((addr + j, v))
                off = last if last > off else off + 1
        elif s.flags & SEC_EXEC:
            for i in range(len(buf) - 3):
                v = struct.unpack_from("<I", buf, i)[0]
                if x.lo <= v < x.hi:
                    sites.append((s.va + i, v))
        # .data and the XDK libraries' sections are flagged writable and executable; scan those as data too
        if not s.flags & SEC_EXEC or s.flags & SEC_WRITE:
            start = (-s.va) % 4
            for i in range(start, len(buf) - 3, 4):
                v = struct.unpack_from("<I", buf, i)[0]
                if x.lo <= v < x.hi:
                    sites.append((s.va + i, v))
    return sorted(set(sites))


class SegTable:
    def __init__(self, segs):
        self.segs = sorted(segs, key=lambda s: s["from_start"])
        self.starts = [s["from_start"] for s in self.segs]

    def find(self, va):
        i = bisect.bisect_right(self.starts, va) - 1
        if i >= 0 and va < self.segs[i]["from_end"]:
            return self.segs[i]
        return None

    def map(self, va):
        s = self.find(va)
        return None if s is None else va + s["delta"]


def infer_bss(sec, holes, pairs, byte_segs):
    """Segments for the virtual-only tail of a section, from pointer pairs."""
    votes = defaultdict(Counter)
    for v, v2 in pairs:
        if sec.va + sec.rsize <= v < sec.end and any(h0 <= v < h1 for h0, h1 in holes):
            votes[v][v2 - v] += 1
    targets = sorted(votes)
    best = [(t, votes[t].most_common(1)[0][0], sum(votes[t].values())) for t in targets]
    runs = []
    for t, d, n in best:
        if runs and runs[-1]["delta"] == d:
            runs[-1]["last"] = t
            runs[-1]["n"] += 1
        else:
            runs.append({"first": t, "last": t, "delta": d, "n": 1})
    # a lone target that disagrees with both neighbours is noise (an integer, not a pointer)
    keep = []
    for i, r in enumerate(runs):
        nb = [runs[j]["delta"] for j in (i - 1, i + 1) if 0 <= j < len(runs)]
        if r["n"] < 5 and len(nb) == 2 and nb[0] == nb[1]:
            continue
        if keep and keep[-1]["delta"] == r["delta"]:
            keep[-1]["last"], keep[-1]["n"] = r["last"], keep[-1]["n"] + r["n"]
        else:
            keep.append(dict(r))
    segs, unmapped = [], []
    for h0, h1 in holes:
        h0 = max(h0, sec.va + sec.rsize)
        if h0 >= h1:
            continue
        rs = [r for r in keep if h0 <= r["first"] < h1]
        prev_seg = next((s for s in byte_segs if s["from_end"] == h0), None)
        cur = h0
        for i, r in enumerate(rs):
            core_end = r["last"] + 4
            lead_conf = None
            if i == 0 and prev_seg is not None and prev_seg["delta"] == r["delta"]:
                lead_conf = "high"
            elif i == 0:
                lead_conf = "low"
            if lead_conf and cur < r["first"]:
                segs.append(_seg(cur, r["first"], r["delta"], sec.name, lead_conf, "bss-extend"))
            elif cur < r["first"]:
                unmapped.append(_um("from", cur, r["first"], sec.name, "bss boundary between deltas"))
            conf = "high" if r["n"] >= 5 else "medium"
            segs.append(_seg(r["first"], core_end, r["delta"], sec.name, conf, "bss-pointers", r["n"]))
            cur = core_end
        if not rs:
            if prev_seg is not None:
                segs.append(_seg(h0, h1, prev_seg["delta"], sec.name, "low", "bss-extend"))
            else:
                unmapped.append(_um("from", h0, h1, sec.name, "bss without pointer evidence"))
        elif cur < h1:
            segs.append(_seg(cur, h1, rs[-1]["delta"], sec.name, "low", "bss-extend"))
    return segs, unmapped


def _seg(a, b, d, sec, conf, method, evidence=None):
    s = {"from_start": a, "from_end": b, "delta": d, "section": sec,
         "confidence": conf, "method": method}
    if evidence is not None:
        s["targets"] = evidence
    return s


def _um(side, a, b, sec, why):
    return {"side": side, "start": a, "end": b, "section": sec, "reason": why}


def check_pointers(table, sites, tx):
    """Per byte segment: [checked, ok, target unmapped].

    A value that is the same in both images while its target moved is an
    integer that happens to look like an address, not a pointer; skip it."""
    stats = defaultdict(lambda: [0, 0, 0])
    for p, v in sites:
        s = table.find(p)
        if s is None or s["method"] != "bytes":
            continue
        v2, mv = tx.u32(p + s["delta"]), table.map(v)
        if v2 == v and mv != v:
            continue
        st = stats[s["from_start"]]
        st[0] += 1
        if mv is None:
            st[2] += 1
        elif mv == v2:
            st[1] += 1
    return stats


def build_map(fx, tx):
    tsec = {s.name: s for s in tx.sections}
    segs, unmapped = [], []
    for fs in fx.sections:
        ts = tsec.get(fs.name)
        if ts is None:
            unmapped.append(_um("from", fs.va, fs.end, fs.name, "section only in from"))
            continue
        b_cover = []
        for a0, a1, d in align_section(fs.masked, ts.masked):
            segs.append(_seg(fs.va + a0, fs.va + a1, ts.va - fs.va + d, fs.name, None, "bytes"))
            b_cover.append((a0 + d, a1 + d))
        cur = 0
        for a0, a1 in [(s["from_start"] - fs.va, s["from_end"] - fs.va) for s in segs if s["section"] == fs.name]:
            if a0 > cur:
                unmapped.append(_um("from", fs.va + cur, fs.va + a0, fs.name, "only in from"))
            cur = a1
        if cur < fs.rsize:
            unmapped.append(_um("from", fs.va + cur, fs.va + fs.rsize, fs.name, "only in from"))
        cur = 0
        for b0, b1 in sorted(b_cover):
            if b0 > cur:
                unmapped.append(_um("to", ts.va + cur, ts.va + b0, ts.name, "only in to"))
            cur = max(cur, b1)
        if cur < ts.rsize:
            unmapped.append(_um("to", ts.va + cur, ts.va + ts.rsize, ts.name, "only in to"))
    for ts in tx.sections:
        if not any(s.name == ts.name for s in fx.sections):
            unmapped.append(_um("to", ts.va, ts.end, ts.name, "section only in to"))

    # pointers found in aligned bytes give (target, target in other image) pairs
    sites = pointer_sites(fx)
    bt = SegTable(segs)
    pairs = []
    for p, v in sites:
        s = bt.find(p)
        if s is not None and s["from_end"] - s["from_start"] >= 256:
            v2 = tx.u32(p + s["delta"])
            if v2 is not None and tx.lo <= v2 < tx.hi and v2 != v:
                pairs.append((v, v2))
    for fs in fx.sections:
        cov = sorted((s["from_start"], s["from_end"]) for s in segs if s["section"] == fs.name)
        holes, cur = [], fs.va
        for a, b in cov:
            if a > cur:
                holes.append((cur, a))
            cur = max(cur, b)
        if cur < fs.end:
            holes.append((cur, fs.end))
        bss_segs, bss_um = infer_bss(fs, holes, pairs, [s for s in segs if s["section"] == fs.name])
        segs += bss_segs
        unmapped += bss_um

    table = SegTable(segs)
    stats = check_pointers(table, sites, tx)
    # a segment whose pointers mostly disagree matched on look-alike code; drop it
    bad = [s for s in table.segs if s["method"] == "bytes"
           and stats[s["from_start"]][0] >= 4 and stats[s["from_start"]][1] * 2 < stats[s["from_start"]][0]]
    for s in bad:
        unmapped.append(_um("from", s["from_start"], s["from_end"], s["section"], "failed pointer check"))
    if bad:
        table = SegTable([s for s in table.segs if s not in bad])
        stats = check_pointers(table, sites, tx)
    for s in table.segs:
        if s["method"] != "bytes":
            continue
        n, ok, miss = stats[s["from_start"]]
        s["pointers_target_unmapped"] = miss
        span = s["from_end"] - s["from_start"]
        s["pointers_checked"], s["pointers_ok"] = n, ok
        share = ok / n if n else None
        s["verified_share"] = None if share is None else round(share, 4)
        if span >= 256 and (share is None or share >= 0.9):
            s["confidence"] = "high"
        elif span < 256 or share >= 0.6:
            s["confidence"] = "medium"
        else:
            s["confidence"] = "low"
    return {
        "from_sha1": fx.sha1, "to_sha1": tx.sha1,
        "from_entry": fx.entry, "to_entry": tx.entry,
        "from_thunk": fx.thunk, "to_thunk": tx.thunk,
        "image_start": fx.lo, "image_end": fx.hi,
        "pointer_scan": "capstone" if capstone else "bytes",
        "segments": table.segs,
        "unmapped": sorted(unmapped, key=lambda u: (u["side"], u["start"])),
    }


def summarize(m, out, per_segment=True):
    segs = m["segments"]
    by = Counter((s["method"], s["confidence"]) for s in segs)
    n = sum(s.get("pointers_checked", 0) for s in segs)
    ok = sum(s.get("pointers_ok", 0) for s in segs)
    miss = sum(s.get("pointers_target_unmapped", 0) for s in segs)
    out.write("from %s -> to %s\n" % (m["from_sha1"], m["to_sha1"]))
    ent = SegTable(segs).map(m["from_entry"]) if m["from_entry"] is not None else None
    out.write("entry %08X -> mapped %s, actual %08X\n" % (
        m["from_entry"], "%08X" % ent if ent is not None else "-", m["to_entry"]))
    out.write("segments: %d (%s)\n" % (len(segs), ", ".join(
        "%s/%s %d" % (k[0], k[1], v) for k, v in sorted(by.items()))))
    out.write("pointers verified: %d/%d (%.2f%%); %d of the failures point at unmapped targets\n" % (
        ok, n, 100.0 * ok / n if n else 0, miss))
    um = Counter(u["side"] for u in m["unmapped"])
    sz = Counter()
    for u in m["unmapped"]:
        sz[u["side"]] += u["end"] - u["start"]
    out.write("unmapped: from %d ranges / %d bytes, to %d ranges / %d bytes\n" % (
        um["from"], sz["from"], um["to"], sz["to"]))
    for s in segs if per_segment else []:
        v = s.get("verified_share")
        out.write("  %-9s %08X-%08X %+7d %-6s %-12s %s\n" % (
            s["section"], s["from_start"], s["from_end"], s["delta"], s["confidence"], s["method"],
            "%d/%d" % (s["pointers_ok"], s["pointers_checked"]) if v is not None
            else ("targets %d" % s["targets"] if "targets" in s else "-")))


# --- source address check --------------------------------------------------

def same_code(fx, tx, table, a, to, n):
    """True if n bytes at a match those at to, allowing for relocated absolute
    pointers and for rel32 branches whose targets map onto each other."""
    A, B = fx.masked_at(a, n + 4), tx.masked_at(to, n + 4)
    RA, RB = fx.masked_at(a, n + 4, raw=True), tx.masked_at(to, n + 4, raw=True)
    if A is None or B is None:
        return True  # bss: nothing to compare
    i = 0
    while i < min(n, len(A), len(B)):
        if A[i] == B[i]:
            i += 1
            continue
        for j in range(max(0, i - 4), i):
            op = A[j] in (0xE8, 0xE9) or (j and A[j - 1] == 0x0F and 0x80 <= A[j] <= 0x8F)
            if op and A[j] == B[j] and j + 5 <= min(len(RA), len(RB)):
                ta = a + j + 5 + struct.unpack_from("<i", RA, j + 1)[0]
                tb = to + j + 5 + struct.unpack_from("<i", RB, j + 1)[0]
                if table.map(ta) == tb:
                    i = j + 5
                    break
        else:
            return False
    return True

_ADDR = re.compile(r"\bsub_([0-9A-Fa-f]{6,8})\b|\b0x([0-9A-Fa-f]{5,8})(?=[uUlL]*\b)")


def check_addrs(m, paths, fx=None, tx=None):
    table = SegTable(m["segments"])
    lo, hi = 0x11000, m["image_end"]
    rows = []
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as f:
            for ln, line in enumerate(f, 1):
                for mo in _ADDR.finditer(line):
                    a = int(mo.group(1) or mo.group(2), 16)
                    if not lo <= a < hi:
                        continue
                    s = table.find(a)
                    if s is None:
                        cls, to = "unmapped", None
                    else:
                        to = a + s["delta"]
                        cls = "high" if s["confidence"] == "high" else "ambiguous"
                        if cls == "high" and fx is not None:
                            n = min(EDGE, s["from_end"] - a)
                            if not same_code(fx, tx, table, a, to, n):
                                cls = "ambiguous"
                    rows.append({"file": path, "line": ln, "addr": a, "to": to,
                                 "kind": "sub" if mo.group(1) else "hex", "class": cls})
    return rows


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[1])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("map", help="build the segment map")
    p.add_argument("--from", dest="src", required=True, help="the repo's canonical XBE")
    p.add_argument("--to", dest="dst", required=True, help="the other region's XBE")
    p.add_argument("-o", "--output", required=True, help="JSON map to write")
    p.add_argument("-q", "--quiet", action="store_true", help="omit the per-segment list")
    p = sub.add_parser("profile", help="reduce a map to a port/region profile (or write the canonical one)")
    p.add_argument("--name", required=True, help="e.g. ps-usa")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("--map", help="map from the canonical XBE to this profile's XBE")
    g.add_argument("--canonical", metavar="XBE", help="the XBE the sources are written for")
    p.add_argument("--explicit", help="JSON list of explicit {from, to, kind, name, confidence, why} entries")
    p.add_argument("--extra-seeds", help="JSON list of {start, why}: icall seeds this XBE needs for the same "
                   "function boundaries as the canonical one")
    p.add_argument("-o", "--output", required=True)
    p = sub.add_parser("addrs", help="map the guest addresses used in source files")
    p.add_argument("--map", required=True)
    p.add_argument("--from", dest="src", help="from XBE, enables the byte check")
    p.add_argument("--to", dest="dst")
    p.add_argument("--json", help="write every address row here")
    p.add_argument("files", nargs="+")
    a = ap.parse_args(argv)

    if a.cmd == "map":
        fx, tx = Xbe(a.src), Xbe(a.dst)
        m = build_map(fx, tx)
        with open(a.output, "w") as f:
            json.dump(m, f, indent=1)
        summarize(m, sys.stdout, not a.quiet)
        return 0

    if a.cmd == "profile":
        if a.canonical:
            x = Xbe(a.canonical)
            prof = {"name": a.name, "sha1": x.sha1, "canonical": True, "image_end": "0x%08X" % x.hi}
        else:
            with open(a.map) as f:
                m = json.load(f)
            prof = {"name": a.name, "sha1": m["to_sha1"], "canonical": False,
                    "canonical_sha1": m["from_sha1"], "image_end": "0x%08X" % m["image_end"],
                    "segments": [{"from_start": "0x%08X" % s["from_start"], "from_end": "0x%08X" % s["from_end"],
                                  "delta": s["delta"], "section": s["section"], "confidence": s["confidence"]}
                                 for s in m["segments"]],
                    "explicit": []}
            if a.explicit:
                with open(a.explicit) as f:
                    prof["explicit"] = json.load(f)
            if a.extra_seeds:
                with open(a.extra_seeds) as f:
                    prof["extra_seeds"] = json.load(f)
        with open(a.output, "w", newline="\n") as f:
            json.dump(prof, f, indent=1)
            f.write("\n")
        return 0

    with open(a.map) as f:
        m = json.load(f)
    fx = Xbe(a.src) if a.src else None
    tx = Xbe(a.dst) if a.dst else None
    rows = check_addrs(m, a.files, fx, tx)
    uniq = {}
    for r in rows:
        uniq.setdefault(r["addr"], r["class"])
        if r["class"] != "high":
            uniq[r["addr"]] = r["class"]
    c, cu = Counter(r["class"] for r in rows), Counter(uniq.values())
    print("occurrences: %d (high %d, ambiguous %d, unmapped %d)" % (
        len(rows), c["high"], c["ambiguous"], c["unmapped"]))
    print("unique addresses: %d (high %d, ambiguous %d, unmapped %d)" % (
        len(uniq), cu["high"], cu["ambiguous"], cu["unmapped"]))
    for r in rows:
        if r["class"] != "high":
            print("  %-9s %08X -> %s  %s:%d" % (r["class"], r["addr"],
                  "%08X" % r["to"] if r["to"] is not None else "-", r["file"], r["line"]))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(rows, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
