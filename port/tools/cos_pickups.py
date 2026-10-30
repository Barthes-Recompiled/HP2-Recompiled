"""Every placed pickup of the user's own CoS game files as census rows for make_catalog.py --census: cos_objects.py's
GEO object table (records 0x88 bytes, position +0x04, type +0x3C, item +0x5C/+0x48; see its notes), the level file
names mapped to the title's level numbers, test levels and the Pink Room left out, empty (0x0000) and unhandled
(0x5xxx) items left out, day/night copies of the same pickup kept once.

    python cos_pickups.py <game dir> <out.tsv>"""
import csv
import io
import subprocess
import sys
from pathlib import Path

LEVEL = {"Burrow": 0, "Expelmus": 1, "Library": 2, "B_Burkes": 4, "DiagonAlly": 5, "Gringott": 6, "Mulpepp": 7,
         "LeakyC": 8, "G_Japes": 10, "MagMenag": 11, "Whomping": 12, "WImpArea": 13, "WWiggenweld": 14,
         "WGytrash": 15, "Dada": 17, "PreLibry": 19, "LibrPotn": 20, "Incendio": 22, "Avivors": 23, "Herbology": 24,
         "ChambOfS": 25, "Hogwarts": 26, "EntHall": 27, "Staircas": 29, "GriffRoom": 30, "StealthDun": 33,
         "StealthLib": 34, "AraForest": 56, "AraLair": 57}


def main(game, out):
    tsv = subprocess.run([sys.executable, "-B", str(Path(__file__).with_name("cos_objects.py")), game],
                         check=True, capture_output=True, text=True).stdout
    seen, rows, skipped = set(), [], set()
    for r in csv.DictReader(io.StringIO(tsv), delimiter="\t"):
        name = r["level"].split("#")[0].split("/")[-1].split("\\")[-1]
        base = name[:-6] if name.endswith("_Night") else name
        item = int(r["item_id"], 16)
        if base not in LEVEL:
            skipped.add(name)
            continue
        if item == 0 or item >> 12 == 5:
            continue
        lv, x, z = LEVEL[base], float(r["x"]), float(r["z"])
        key = (lv, item, round(x), round(z))
        if key in seen:
            continue
        seen.add(key)
        rows.append((lv, r["handler"], f"0x{item:04X}", r["x"], r["y"], r["z"], "geo"))
    with open(out, "w", newline="") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(["level", "handler", "item_id", "x", "y", "z", "source"])
        w.writerows(sorted(rows))
    print(f"{out}: {len(rows)} pickups; levels left out: {', '.join(sorted(skipped))}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
