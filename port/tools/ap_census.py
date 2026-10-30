"""Census a running CoS through Local\\hpcos_trainer; never launches the game.

    python port/tools/ap_census.py pickups.tsv --mode both
    python port/tools/ap_census.py extra.tsv --current --mode heap

The default sweeps make_catalog.LEVELS using the same warp request as costrainer.
This changes the running game's level/story state: use a disposable save.
TSV rows are flushed immediately. Diagnostics go to stderr. A .items.json sidecar
resolves item kinds from the running game's dispatch tables (no game data copied).
"""
import argparse
import csv
import ctypes
import importlib.util
import json
import math
import mmap
from pathlib import Path
import struct
import sys
import time

spec = importlib.util.spec_from_file_location("make_catalog", Path(__file__).parents[1] / "ap/make_catalog.py")
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

HEAP, LIMIT, STRIDE = 0x80000000, 0x84000000, 0x370
# The entity array keeps each object's INITIALIZER at +0x70 (seen live 2026-10-30: a Borgin and Burkes chest, GEO
# type 0x50000009, is an entity with handler 0x85800, its initializer; the update 0x85990 is never there). Every
# initializer reads the GEO record's item from the level record at [E+0xA8]+0x24 (L+0x24 in
# research/cos_objects_report.md); most copy it on to [E+0xC0]+0x14, but 0x75F50 (type 0x50000022) hands it straight
# to the pickup spawner sub_00075340 (recomp_0001.c:144867-144874), so the level record is the one source that holds
# for all of them: the "level" source. The grant/update functions stay in case a level stores those instead.
HANDLERS = {0x85800: "level", 0x881C0: "level", 0x86E90: "level", 0x75F50: "level", 0x874D0: "level",
            0x828B0: "equipment", 0xEC100: "level", 0x85C20: "level", 0x85E50: "level", 0x80070: "level",
            0x86080: "level", 0x86360: "level",
            0x75010: "inline", 0x75A20: "inline", 0xD1C10: "inline",
            0x80A00: "record", 0x82A90: "record", 0x828F0: "equipment",
            0x87CE0: "record", 0x92010: "interaction", 0xEC200: "record"}


def heap_ptr(p, size=1):
    return HEAP <= p <= LIMIT - size


class Trainer:
    def __init__(self):
        if sys.platform != "win32":
            raise OSError("the census requires Windows and a running hpcos trainer block")
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        self.k.OpenProcess.restype = ctypes.c_void_p
        self.k.ReadProcessMemory.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                             ctypes.c_void_p, ctypes.c_size_t,
                                             ctypes.POINTER(ctypes.c_size_t)]
        self.k.CloseHandle.argtypes = [ctypes.c_void_p]
        self.m = mmap.mmap(-1, 4096, tagname=r"Local\hpcos_trainer")
        magic, version, pid = struct.unpack_from("<III", self.m)
        if magic != 0x52544348 or version != 4:
            self.m.close()
            raise OSError("no compatible trainer block (expected HCTR version 4)")
        self.offset = struct.unpack_from("<Q", self.m, 16)[0]
        self.handle = self.k.OpenProcess(0x0010 | 0x0400, False, pid)
        if not self.handle:
            self.m.close()
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        self.k.CloseHandle(self.handle)
        self.m.close()

    def read(self, addr, n):
        buf, got = ctypes.create_string_buffer(n), ctypes.c_size_t()
        self.k.ReadProcessMemory(self.handle, addr + self.offset, buf, n, ctypes.byref(got))
        return buf.raw[:got.value]

    def u32(self, addr):
        data = self.read(addr, 4)
        return struct.unpack("<I", data)[0] if len(data) == 4 else None

    def state(self):
        frame, player = struct.unpack_from("<II", self.m, 24)
        level = struct.unpack_from("<H", self.m, 64)[0]
        return frame, player, level

    def warp(self, level, start):
        # trainer_shm.h v4: warp_seq +136, level/start/replay +140/+142/+144.
        # Publish parameters before the sequence, exactly as costrainer.py does.
        struct.pack_into("<HHH", self.m, 140, level, start, 0)
        seq = struct.unpack_from("<I", self.m, 136)[0]
        struct.pack_into("<I", self.m, 136, (seq + 1) & 0xFFFFFFFF)


def wait_stable(t, level, initial_frame, timeout, settle):
    deadline, since, last, advancing = time.monotonic() + timeout, None, None, 0
    while time.monotonic() < deadline:
        frame, player, reported = t.state()
        ent = t.read(player, 0xB0) if heap_ptr(player, 0xB0) else b""
        obj = struct.unpack_from("<I", ent, 0xAC)[0] if len(ent) == 0xB0 else 0
        identity = (player, obj)
        valid = (frame != initial_frame and reported == level and t.u32(0x527B78) == level
                 and len(ent) == 0xB0 and ent[0] & 1 and heap_ptr(obj, 0x20)
                 and t.u32(0x530408) == player)
        if valid and last and identity == last[:2] and frame != last[2]:
            advancing += 1
            if since is None:
                since = time.monotonic()
            if advancing >= 3 and time.monotonic() - since >= settle:
                return player
        else:
            since, advancing = None, 0
        last = (*identity, frame) if valid else None
        time.sleep(0.1)
    return None


def item_id(t, ent, handler):
    source = HANDLERS[handler]
    if source == "inline":
        item = struct.unpack_from("<I", ent, 0x88)[0]
        return item if item else None
    if source == "equipment":
        return 0x4002
    if source == "level":
        level_rec = struct.unpack_from("<I", ent, 0xA8)[0]
        if not heap_ptr(level_rec, 0x28):
            return None
        item = t.u32(level_rec + 0x24) & 0xFFFF
        return item if item else None
    if source == "interaction":
        player = t.u32(0x530408)
        obj = t.u32(player + 0xAC) if player and heap_ptr(player, 0xB0) else None
        flags = t.u32(obj + 0x1C) if obj and heap_ptr(obj, 0x28) else None
        return t.u32(obj + 0x24) if flags is not None and flags & 1 else 0x6000
    # IMPORTANT: +A8 is a level record, but the grant reads the different
    # associated record at +C0. Verified in 80A00/82A90/87CE0/EC200.
    rec = struct.unpack_from("<I", ent, 0xC0)[0]
    return t.u32(rec + 0x14) if heap_ptr(rec, 0x18) else None


def candidates(t, player, mode, count, heap_mb):
    if mode in ("array", "both"):
        # Harry is the array base used by entarray.py. Read each entity to avoid
        # losing the rest of the array on an unreadable page.
        for i in range(count):
            addr = player + i * STRIDE
            if not heap_ptr(addr, 0xC4):
                break
            yield addr, t.read(addr, 0xC4)
    if mode in ("heap", "both"):
        # Chunked reads survive uncommitted holes; overlap covers boundary entities.
        end = HEAP + (heap_mb << 20)
        for base in range(HEAP, end, 0x1000):
            data = t.read(base, min(0x1000 + 0xC0, end - base))
            for off in range(0, min(0x1000, len(data) - 0xC4 + 1), 16):
                handler = struct.unpack_from("<I", data, off + 0x70)[0]
                if handler in HANDLERS:
                    yield base + off, data[off:off + 0xC4]


def scan(t, player, mode, count, heap_mb):
    seen = set()
    for addr, ent in candidates(t, player, mode, count, heap_mb):
        if addr in seen or len(ent) < 0xC4 or not ent[0] & 1:
            continue
        handler = struct.unpack_from("<I", ent, 0x70)[0]
        if handler not in HANDLERS:
            continue
        obj = struct.unpack_from("<I", ent, 0xAC)[0]
        pos = t.read(obj + 0x14, 12) if heap_ptr(obj, 0x20) else b""
        if len(pos) != 12:
            continue
        xyz = struct.unpack("<3f", pos)
        item = item_id(t, ent, handler)
        if item is None or not 0 <= item <= 0xFFFF or not all(
                math.isfinite(v) and abs(v) < 100000 for v in xyz):
            continue
        seen.add(addr)
        yield handler, item, xyz


def dispatch_kinds(t):
    kinds = {}
    # give_item_report.md guest block effects; table order is read, never guessed.
    effects = {0x81FA1: "quest", 0x81FF8: "quest", 0x82054: "quest",
               0x820E9: "collection", 0x82144: "beans", 0x821E2: "beans",
               0x82222: "beans", 0x822A1: "beans", 0x823B3: "bean bag",
               0x823F2: "health", 0x82465: "health"}
    for item in range(0x3001, 0x3081):
        selector = t.read(0x827E4 + item - 0x3001, 1)
        if not selector or selector[0] >= 14:
            continue
        target = t.u32(0x827AC + 4 * selector[0])
        if target in effects:
            kinds[f"{item:04x}"] = effects[target]
        elif target in (0x820AB, 0x82377):
            secondary = t.read(0x81734 + item - 0x3001, 1)
            if secondary and secondary[0] < 3:
                effect = t.u32(0x81728 + 4 * secondary[0])
                if effect in (0x816AC, 0x816BD):
                    kinds[f"{item:04x}"] = "beans" if effect == 0x816BD else "collection"
    return kinds


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("output", type=Path)
    p.add_argument("--mode", choices=("array", "heap", "both"), default="array")
    p.add_argument("--current", action="store_true", help="read current level only; do not warp")
    p.add_argument("--levels", type=int, nargs="+", help="sweep a subset of real levels")
    p.add_argument("--start", type=int, default=0)
    p.add_argument("--timeout", type=float, default=20)
    p.add_argument("--settle", type=float, default=1.5)
    p.add_argument("--entities", type=int, default=2048)
    p.add_argument("--heap-mb", type=int, default=64, choices=range(1, 65), metavar="1..64")
    a = p.parse_args()
    if a.timeout <= 0 or a.settle < 0 or a.entities < 1 or not 0 <= a.start <= 65535:
        p.error("invalid timeout, settle, entity count, or start")
    levels = a.levels if a.levels is not None else list(catalog.LEVELS)
    if any(lv not in catalog.LEVELS for lv in levels):
        p.error("--levels must be real levels in make_catalog.LEVELS")
    t = Trainer()
    try:
        if a.current:
            levels = [t.state()[2]]
        # Exclusive creation protects a previous partial census after a timeout/crash.
        with a.output.open("x", newline="", encoding="utf-8") as f:
            w = csv.writer(f, delimiter="\t", lineterminator="\n")
            w.writerow(("level", "handler", "item_id", "x", "y", "z"))
            f.flush()
            Path(str(a.output) + ".items.json").write_text(
                json.dumps(dispatch_kinds(t), indent=2) + "\n", encoding="utf-8")
            for level in levels:
                frame = t.state()[0]
                if not a.current:
                    t.warp(level, a.start)
                player = wait_stable(t, level, frame, a.timeout, a.settle)
                if player is None:
                    print(f"level {level}: skipped (not stable in {a.timeout:g}s)", file=sys.stderr)
                    continue
                n = 0
                for handler, item, xyz in scan(t, player, a.mode, a.entities, a.heap_mb):
                    if t.u32(0x527B78) != level or t.state()[2] != level:
                        print(f"level {level}: changed during scan; stopped", file=sys.stderr)
                        break
                    w.writerow((level, f"0x{handler:05x}", f"0x{item:04x}",
                                *(format(v, ".9g") for v in xyz)))
                    f.flush()
                    n += 1
                print(f"level {level}: {n} objects", file=sys.stderr)
    finally:
        t.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, KeyboardInterrupt) as exc:
        sys.exit(str(exc) or "interrupted; completed TSV rows preserved")
