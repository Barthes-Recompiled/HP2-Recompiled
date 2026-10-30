"""Offline checks: python -m unittest discover -s port/tools -p test_ap_census.py"""
import json
from pathlib import Path
import struct
import tempfile
import unittest

import ap_census as census


class Memory:
    def __init__(self):
        self.regions = {}

    def put(self, addr, data):
        self.regions[addr] = bytes(data)

    def read(self, addr, n):
        for base, data in self.regions.items():
            if base <= addr < base + len(data):
                return data[addr - base:addr - base + n]
        return b""

    def u32(self, addr):
        data = self.read(addr, 4)
        return struct.unpack("<I", data)[0] if len(data) == 4 else None


def entity(handler, item=0, obj=0x80010000, rec=0):
    ent = bytearray(0xC4)
    ent[0] = 1
    struct.pack_into("<I", ent, 0x70, handler)
    struct.pack_into("<I", ent, 0x88, item)
    struct.pack_into("<I", ent, 0xA8, 0x80050000)  # intentionally wrong record
    struct.pack_into("<I", ent, 0xAC, obj)
    struct.pack_into("<I", ent, 0xC0, rec)
    return ent


class CensusTests(unittest.TestCase):
    def test_all_item_sources(self):
        t = Memory()
        t.put(0x80020014, struct.pack("<I", 0x7004))
        # the level record an initializer reads (L+0x24), with its item in the low half; the fixture's +0xA8 points
        # at it only for the "level" source, the others keep the deliberately wrong one
        t.put(0x80060024, struct.pack("<I", 0x0004_2062))
        for handler, source in census.HANDLERS.items():
            with self.subTest(handler=hex(handler)):
                ent = entity(handler, 0x2001, rec=0x80020000)
                if source == "level":
                    struct.pack_into("<I", ent, 0xA8, 0x80060000)
                if source == "interaction":
                    t.put(0x530408, struct.pack("<I", 0x80030000))
                    t.put(0x800300AC, struct.pack("<I", 0x80040000))
                    t.put(0x8004001C, struct.pack("<I", 1))
                    t.put(0x80040024, struct.pack("<I", 0x9001))
                    self.assertEqual(census.item_id(t, ent, handler), 0x9001)
                    t.put(0x8004001C, struct.pack("<I", 0))
                    expected = 0x6000
                else:
                    expected = {"inline": 0x2001, "record": 0x7004, "equipment": 0x4002, "level": 0x2062}[source]
                self.assertEqual(census.item_id(t, ent, handler), expected)

    def test_heap_after_hole_and_array_dedup(self):
        t = Memory()
        ent = entity(0x75010, 0x2001)
        # First page is unreadable, second contains a valid candidate.
        page = bytearray(4096)
        page[:len(ent)] = ent
        t.put(census.HEAP + 4096, page)
        t.put(0x80010014, struct.pack("<3f", 12.25, 8, -19.75))
        rows = list(census.scan(t, census.HEAP + 4096, "both", 1, 1))
        self.assertEqual(rows, [(0x75010, 0x2001, (12.25, 8, -19.75))])

    def test_dead_invalid_and_zero_pickups(self):
        t = Memory()
        t.put(0x80010014, struct.pack("<3f", 0, 0, float("nan")))
        t.put(census.HEAP, entity(0x75010, 0x2001))
        self.assertEqual(list(census.scan(t, census.HEAP, "array", 1, 1)), [])
        self.assertIsNone(census.item_id(t, entity(0x75010), 0x75010))
        dead = entity(0x75010, 0x2001)
        dead[0] = 0
        t.put(census.HEAP, dead)
        self.assertEqual(list(census.scan(t, census.HEAP, "array", 1, 1)), [])

    def test_live_dispatch_not_destination_order(self):
        t = Memory()
        t.put(0x827E4, bytes([7, 2]))
        t.put(0x827AC + 7 * 4, struct.pack("<I", 0x823B3))
        t.put(0x827AC + 2 * 4, struct.pack("<I", 0x82144))
        self.assertEqual(census.dispatch_kinds(t), {"3001": "bean bag", "3002": "beans"})

    def test_warp_shm_offsets(self):
        # No Windows mapping/process is opened by this test.
        t = object.__new__(census.Trainer)
        t.m = bytearray(4096)
        struct.pack_into("<I", t.m, 136, 41)
        t.warp(34, 7)
        self.assertEqual(struct.unpack_from("<IHHH", t.m, 136), (42, 34, 7, 0))


class CatalogTests(unittest.TestCase):
    def test_committed_catalog_reproducible(self):
        # the committed catalog is the census build from the committed pickup rows (cos_pickups.py's output; numbers
        # only), so the same rows must give the same catalog: the IDs players' slots are made from
        ap = Path(__file__).parents[1] / "ap"
        current = json.loads((ap / "catalog.json").read_text())
        self.assertEqual(census.catalog.build(ap / "pickups.tsv"), current)

    def test_census_identity_metadata_and_rewards(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "census.tsv"
            path.write_text("level\thandler\titem_id\tx\ty\tz\n"
                            "0\t0x75010\t0x3001\t1.49\t7.5\t-2.51\n")
            Path(str(path) + ".items.json").write_text('{"3001":"bean bag"}')
            cat = census.catalog.build(path)
            pickup = next(l for l in cat["locations"] if l["key"].startswith("pickup:"))
            self.assertEqual(pickup["key"], "pickup:0:3001:1:-3")
            self.assertEqual(pickup["name"], "The Burrow: bean bag at (1, -3)")
            self.assertEqual((pickup["x"], pickup["y"], pickup["z"]), (1.49, 7.5, -2.51))
            rewards = {l["key"] for l in cat["locations"] if l["key"].startswith("reward:")}
            self.assertEqual(rewards, {"reward:2008", "reward:200d", "reward:2012", "reward:2015"})
            self.assertEqual(len({l["id"] for l in cat["locations"]}), len(cat["locations"]))
            self.assertEqual(cat, census.catalog.build(path))

    def test_collision_and_invalid_coordinates_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "census.tsv"
            header = "level\thandler\titem_id\tx\ty\tz\n"
            row = "0\t75010\t2001\t1\t7\t-3\n"
            path.write_text(header + row + row)
            with self.assertRaisesRegex(ValueError, "duplicate rounded identity"):
                census.catalog.build(path)
            path.write_text(header + row.replace("\t7\t", "\tnan\t"))
            with self.assertRaisesRegex(ValueError, "invalid pickup"):
                census.catalog.build(path)


if __name__ == "__main__":
    unittest.main()
