"""Fold RECOMP_AP_RECORD grant logs into the committed pickup census, for the grants the level files can't show:
beans from breakables and timed spots are decided when they drop, so only a play-through's log sees them.

    python merge_grants.py <pickups.tsv> <grants.tsv>...

A grant row (level, caller return address, item id, Harry's x y z) becomes a census row with source "played" unless
the census already has a row of that level and item within MATCH units of it: the radius hle_ap.c's hook matches a
grant to a catalog pickup with (the log holds Harry's position, a step or two from the object). Items the bridge
itself gives, and AP items, never reach the log (hle_ap.c leaves them out). The result is sorted so a rebuild gives
the same rows, and the same catalog IDs."""
import csv
import math
import sys

MATCH = 6.0


def read(path):
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = list(csv.DictReader(f, delimiter="\t"))
    for r in rows:
        r.setdefault("source", "geo")
        r["source"] = r["source"] or "geo"
    return rows


def near(rows, g):
    """a census row of the grant's level and item within MATCH units of it"""
    level, item = int(g["level"]), int(g["item_id"], 16)
    gx, gy, gz = (float(g[k]) for k in ("x", "y", "z"))
    for r in rows:
        if int(r["level"]) != level or int(r["item_id"], 16) != item:
            continue
        if math.dist((gx, gy, gz), tuple(float(r[k]) for k in ("x", "y", "z"))) <= MATCH:
            return True
    return False


def main(pickups, grants):
    rows = read(pickups)
    added = 0
    for path in grants:
        for g in read(path):
            if near(rows, g):
                continue
            handler = g["handler"].lower()
            rows.append({"level": g["level"], "handler": handler if handler.startswith("0x") else "0x" + handler,
                         "item_id": f"0x{int(g['item_id'], 16):04X}", "x": g["x"], "y": g["y"], "z": g["z"],
                         "source": "played"})
            added += 1
    rows.sort(key=lambda r: (int(r["level"]), int(r["item_id"], 16), round(float(r["x"])), round(float(r["z"]))))
    with open(pickups, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, ["level", "handler", "item_id", "x", "y", "z", "source"], delimiter="\t",
                           lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    print(f"{pickups}: {added} grant rows added, {len(rows)} rows")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2:])
