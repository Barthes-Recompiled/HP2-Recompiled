"""Build port/ap/catalog.json, the Archipelago ID source shared by hpcos.exe's bridge and the apworld. The catalog keeps
only IDs, display names and the keys the bridge matches; nothing from the game.

    python make_catalog.py [catalog.json] [--census pickups.tsv]

Location keys (what src/hle_ap.c reports):
  card:<n>           wizard card n became owned (its word at 0x527E0A + 2n)
  cards:<n>          n wizard cards owned
  beans:<n>          n beans collected (rises of 0x527EE0, spending doesn't count)
  spell:<bit>        that bit of the spell set 0x527F0C became set (learned)
  visit:<level>      that level of the title's table entered for the first time
  house:<n>          Gryffindor reached n house points (0x527EE4)
  pickup:<lv>:<hex>:<x>:<z>  a successful grant near that census object
  reward:<hex>       a successful constant scripted reward (census mode)
IDs are given in a fixed order (by key, sorted), so a rebuild gives the same catalog; the version string carries a hash
of the keys and the bridge refuses a slot built from another catalog."""
import argparse
import csv
import hashlib
import json
import math
import os

GAME = "Harry Potter and the Chamber of Secrets Recomp"
LOCATION_BASE = 74200000
ITEM_BASE = 74210000

# the title's level table (trainer.c), the places a player can be; tests, menus and screens left out
LEVELS = {0: "The Burrow", 1: "DADA Expelliarmus", 2: "Library", 3: "Quidditch", 4: "Borgin and Burkes",
          5: "Diagon Alley", 6: "Gringotts", 7: "Mulpeppers", 8: "Leaky Cauldron", 9: "Flourish and Blotts",
          10: "Gambol and Japes", 11: "Magical Menagerie", 12: "Whomping Willow", 13: "Whomping Imp Area",
          14: "Whomping Wiggenweld", 15: "Whomping Gytrash Area", 16: "Great Hall", 17: "DADA", 18: "DADA Fumos",
          19: "Pre Library", 20: "Library Potions", 21: "Dumbledore's Office", 22: "Incendio", 23: "Avifors Challenge",
          24: "Herbology", 25: "Chamber of Secrets", 26: "Hogwarts Grounds", 27: "Entrance Hall",
          29: "Grand Staircase", 30: "Gryffindor Common Room", 33: "Dungeons", 34: "Restricted Section",
          55: "Aragog Arena", 56: "Forbidden Forest", 57: "Aragog's Lair"}
START_LEVEL = 0
# the spell set's bits (HPCOS_ALL_SPELLS 0x00D04017); which spell each is still has to be seen in the game
SPELL_BITS = [0, 1, 2, 4, 14, 20, 22, 23]
CARDS = 101
CARD_MILESTONES = range(5, CARDS + 1, 5)
BEAN_MILESTONES = [10, 25] + list(range(50, 2001, 50))
HOUSE_MILESTONES = range(25, 501, 25)


# Only distinct constant grant sites in give_item_report.md; overlapping generated
# functions are copies of the same site, not additional rewards.
REWARDS = (0x2008, 0x200D, 0x2012, 0x2015)


def item_kind(item, kinds=None):
    if kinds and f"{item:04x}" in kinds:
        return kinds[f"{item:04x}"]
    category, n = item & 0xF000, item & 0xFFF
    if category == 0x2000:
        return f"card {n}"
    return {0: "spell", 0x4000: "equipment", 0x6000: "health",
            0x7000: "quest", 0x8000: "quest", 0x9000: "collection",
            0xA000: "collection"}.get(category, f"item 0x{item:04x}")


def read_census(path):
    """Keep original xyz for matching; whole-unit xz are only the public identity.

    A collision cannot be represented by the requested key. Fail explicitly
    instead of silently dropping an object or assigning duplicate AP IDs.
    """
    with open(str(path) + ".items.json", encoding="utf-8") as f:
        kinds = json.load(f)
    return _read_census(path, kinds)


def _read_census(path, kinds=None):
    locs, seen = [], set()
    with open(path, newline="", encoding="utf-8-sig") as f:
        rows = csv.DictReader(f, delimiter="\t")
        if not {"level", "handler", "item_id", "x", "y", "z"}.issubset(rows.fieldnames or []):
            raise ValueError(f"{path}: expected level, handler, item_id, x, y, z TSV header")
        for line, row in enumerate(rows, 2):
            level, item = int(row["level"]), int(row["item_id"], 16)
            handler = int(row["handler"], 16)
            x, y, z = (float(row[k]) for k in ("x", "y", "z"))
            if level not in LEVELS or not 0 <= item <= 0xFFFF or not all(
                    math.isfinite(v) and abs(v) < 100000 for v in (x, y, z)):
                raise ValueError(f"{path}:{line}: invalid pickup")
            rx, rz = round(x), round(z)
            key = f"pickup:{level}:{item:04x}:{rx}:{rz}"
            if key in seen:
                raise ValueError(f"{path}:{line}: duplicate rounded identity {key}; inspect census")
            seen.add(key)
            place = LEVELS[level]
            # "geo": a placed object read from the level files (cos_pickups.py); "played": a grant the file census
            # can't see (beans from breakables and timed spots), found in a RECOMP_AP_RECORD log and merged in by
            # merge_grants.py: its own group, so the apworld can leave them out
            source = (row.get("source") or "geo").strip()
            group = "Loose Pickups" if source == "played" else "Pickups"
            locs.append(dict(key=key, name=f"{place}: {item_kind(item, kinds)} at ({rx}, {rz})",
                             region=place, group=group, level=level, item=item,
                             handler=handler, x=x, y=y, z=z))
    return locs


def build(census=None):
    locs = []
    add = lambda key, name, region, group: locs.append({"key": key, "name": name, "region": region, "group": group})
    for c in range(1, CARDS + 1):
        add(f"card:{c}", f"Wizard Card {c}", "Anywhere", "Wizard Cards")
    for n in CARD_MILESTONES:
        add(f"cards:{n}", f"{n} wizard cards owned", "Anywhere", "Card Milestones")
    for n in BEAN_MILESTONES:
        add(f"beans:{n}", f"{n} beans collected", "Anywhere", "Bean Milestones")
    for b in SPELL_BITS:
        add(f"spell:{b}", f"Spell {b} learned", "Anywhere", "Spells")
    for lv, name in LEVELS.items():
        add(f"visit:{lv}", f"Visit {name}", name, "Places")
    for n in HOUSE_MILESTONES:
        add(f"house:{n}", f"Gryffindor {n} house points", "Anywhere", "House Points")
    if census is not None:
        # Sidecar is optional: hand-written TSVs remain supported. The live
        # census resolves the otherwise unknown 3xxx dispatch tables.
        if os.path.exists(str(census) + ".items.json"):
            locs.extend(read_census(census))
        else:
            locs.extend(_read_census(census))
        for item in REWARDS:
            add(f"reward:{item:04x}", f"Scripted reward: {item_kind(item)}", "Anywhere", "Scripted Rewards")
    locs.sort(key=lambda l: l["key"])
    for i, l in enumerate(locs):
        l["id"] = LOCATION_BASE + i

    items = {}
    def item(name, **kw):
        items[name] = dict(id=ITEM_BASE + len(items), **kw)
    for lv, name in LEVELS.items():
        if lv != START_LEVEL:
            item(f"{name} Pass", kind="pass", level=lv, **{"class": "progression"})
    for b in SPELL_BITS:
        item(f"Spell {b}", kind="spell", spell=b, **{"class": "progression"})
    for c in range(1, CARDS + 1):
        item(f"Wizard Card {c}", kind="card", card=c, **{"class": "useful"})
    item("Bean Bag", kind="bag", **{"class": "useful"})
    item("Health Upgrade", kind="health", **{"class": "useful"})
    item("Wiggenweld Potion", kind="heal", **{"class": "filler"})
    item("25 Beans", kind="beans", amount=25, **{"class": "filler"})
    item("10 House Points", kind="house", amount=10, **{"class": "filler"})

    keys = "\n".join(l["key"] for l in locs) + "\n" + "\n".join(sorted(items))
    version = "hpcos-1-" + hashlib.sha1(keys.encode()).hexdigest()[:12]
    regions = {"places": [LEVELS[k] for k in LEVELS], "start": LEVELS[START_LEVEL]}
    return {"version": version, "game": GAME, "location_base": LOCATION_BASE, "item_base": ITEM_BASE,
            "regions": regions, "items": items, "locations": locs}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out", nargs="?", default=os.path.join(os.path.dirname(__file__), "catalog.json"))
    parser.add_argument("--census", help="six-column TSV from tools/ap_census.py")
    args = parser.parse_args()
    out, cat = args.out, build(args.census)
    with open(out, "w", newline="\n") as f:
        json.dump(cat, f, indent=1)
        f.write("\n")
    print(f"{out}: {cat['version']}, {len(cat['locations'])} locations, {len(cat['items'])} items")
