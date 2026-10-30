#!/usr/bin/env python3
"""Read CoS Xbox GEO placements in place; write derived TSV to stdout.

Only handlers whose item field has been traced are emitted. Zero, sentinel,
and unsupported item IDs are retained: a row is a placement, not proof that
it is an obtainable AP check. See cos_objects_report.md for evidence/limits.
No source files, caches, or extracted game data are written.
"""

import argparse
import csv
import math
import mmap
from pathlib import Path
import struct
import sys
from typing import NamedTuple


# handler: (serialized item offset, field width, positive-dword gate)
# Type numbers are serialized record types, not native function addresses.
ITEM_FIELDS = {
    0x50000009: (0x5C, 2, False),
    0x50000010: (0x5C, 2, False),
    0x50000020: (0x5C, 2, True),
    0x50000022: (0x5C, 4, False),
    0x5000006E: (0x5C, 4, False),
    0x50000074: (None, 0, False),  # update 0x828F0 grants constant 0x4002
    0x50000102: (0x5C, 2, False),
    0x50000125: (0x48, 4, False),
    0x5000016F: (0x5C, 2, False),
    0x500001E8: (0x5C, 2, False),
    0x500001E9: (0x5C, 2, False),
    0x500001FD: (0x48, 4, False),
    0x50000203: (0x5C, 2, False),
    0x50000204: (0x5C, 2, False),
}


class Placement(NamedTuple):
    block: int
    index: int
    offset: int
    handler: int
    item_id: int
    x: float
    y: float
    z: float


def placements(data):
    """Yield traced placements from a bytes-like GEO, validating array bounds."""
    def require(offset, size):
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError(f"range 0x{offset:X}+0x{size:X} exceeds file")

    def u32(offset):
        require(offset, 4)
        return struct.unpack_from("<I", data, offset)[0]

    def relative(offset):
        require(offset, 4)
        return offset + struct.unpack_from("<i", data, offset)[0]

    require(0, 0xD4)
    if u32(0) != 0x011A181E:
        raise ValueError("unsupported GEO magic (expected Xbox CoS 0x011A181E)")
    count = u32(0x2C)
    if not count:
        return
    if not u32(0x30):
        raise ValueError("nonempty level-block table has null pointer")
    blocks = relative(0x30)
    require(blocks, count * 0xA4)
    for block in range(count):
        base = blocks + block * 0xA4
        n = u32(base + 0x20)
        if not n:
            continue
        if not u32(base + 0x24):
            raise ValueError(f"block {block}: nonempty object table has null pointer")
        start = relative(base + 0x24)
        require(start, n * 0x88)
        for index in range(n):
            offset = start + index * 0x88
            handler = u32(offset + 0x3C)
            field = ITEM_FIELDS.get(handler)
            if field is None:
                continue
            item_offset, width, positive = field
            raw = u32(offset + item_offset) if item_offset is not None else 0x4002
            item_id = raw & 0xFFFF if width == 2 else raw
            if positive and (raw == 0 or raw & 0x80000000):
                item_id = 0
            xyz = struct.unpack_from("<3f", data, offset + 4)
            if not all(math.isfinite(v) for v in xyz):
                raise ValueError(f"nonfinite position at record 0x{offset:X}")
            yield Placement(block, index, offset, handler, item_id, *xyz)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path, help="user-owned game directory or one .geo file")
    parser.add_argument("--provenance", action="store_true",
                        help="print each row's level-block/index/file offset to stderr")
    args = parser.parse_args()
    root = args.game
    if root.is_dir():
        files = sorted((p for p in root.rglob("*") if p.is_file() and
                        p.suffix.lower() == ".geo"), key=lambda p: str(p).lower())
    elif root.is_file() and root.suffix.lower() == ".geo":
        files = [root]
        root = root.parent
    else:
        parser.error("expected an existing game directory or .geo file")
    writer = csv.writer(sys.stdout, delimiter="\t", lineterminator="\n")
    writer.writerow(("level", "handler", "item_id", "x", "y", "z"))
    rows = 0
    skipped = 0
    try:
        for path in files:
            level = path.relative_to(root).with_suffix("").as_posix()
            with path.open("rb") as source:
                if not path.stat().st_size:
                    raise ValueError(f"{path.name}: empty GEO")
                with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as data:
                    if len(data) >= 4 and struct.unpack_from("<I", data)[0] != 0x011A181E:
                        print(f"skipped unsupported GEO version: {path.relative_to(root)}", file=sys.stderr)
                        skipped += 1
                        continue
                    multi = struct.unpack_from("<I", data, 0x2C)[0] > 1 if len(data) >= 0x30 else False
                    for row in placements(data):
                        name = f"{level}#{row.block}" if multi else level
                        writer.writerow((name, f"0x{row.handler:08X}",
                                         f"0x{row.item_id:04X}",
                                         *(format(v, ".9g") for v in row[5:])))
                        rows += 1
                        if args.provenance:
                            print(f"row {rows}: {path.relative_to(root)} block={row.block} "
                                  f"index={row.index} offset=0x{row.offset:X}", file=sys.stderr)
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}; discard partial TSV", file=sys.stderr)
        return 1
    print(f"{len(files)} GEO files; {skipped} unsupported; {rows} traced placements. "
          "Includes empty/sentinel records; coverage is partial (see report).", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
