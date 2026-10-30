# Pickup census and grant checks

The default catalog is unchanged. A census adds one location per TSV row and
four distinct constant scripted rewards: `2008`, `200d`, `2012`, `2015` (cards
8, 13, 18, 21). The vanilla grant remains in place. Existing card, bean, spell,
visit, and milestone checks remain available alongside object checks.

Run these from the repository root with **your already running game** unpaused:

```powershell
python port/tools/ap_census.py port/ap/pickups.tsv --mode both
python port/ap/make_catalog.py --census port/ap/pickups.tsv
```

The first command warps through every real `make_catalog.LEVELS` entry at start
0, using the same shared-memory request as `research/costrainer.py warp`.
It waits for matching shared/guest level numbers, live Harry and object pointers,
and advancing Harry frames with stable pointers for 1.5 seconds. A level that
does not settle within 20 seconds is skipped and logged on stderr. Each TSV row
is flushed immediately, so partial results survive interruption. The output
must not already exist. Keep its optional `pickups.tsv.items.json` sidecar beside
it: that supplies verified bean/bag/health labels from the live dispatch tables.
An ordinary six-column TSV also works without the sidecar; unresolved item
names then retain their hex IDs rather than guessed names.

Options: `--mode array` (default), `--mode heap`, or `--mode both` (deduplicates
entity addresses); `--entities 2048`; `--heap-mb 64`; `--levels 0 2 26` to sweep a
subset; `--start N`; `--current` to scan the current level without a warp.
The heap scan checks aligned candidates across the full 64 MiB guest heap,
skipping unreadable pages. TSV columns are `level`, `handler`, `item_id`, `x`,
`y`, `z`; handler and item IDs use hexadecimal. Failed reads, dead entities,
invalid pointers, and nonfinite positions are excluded.

Rebuild the executable **and regenerate the AP world/seed from the same catalog**
after importing the TSV. A changed catalog has a different key hash and is
incompatible with existing slots. To preview without replacing the catalog:

```powershell
python port/ap/make_catalog.py port/build-ap/census-catalog.json --census port/ap/pickups.tsv
```

## Item sources and static evidence

`E` is the entity passed to the handler; `R = [E+0xC0]`. This is distinct from
the level record at `E+0xA8`. Position is `[[E+0xAC]+0x14]` (three floats).
Evidence below is in `research/give_item.c`, as cited by
`research/give_item_report.md` section 2, and verified against generated bodies.

| Handler | Item source | Grant-site evidence |
|---|---|---|
| `0x75010` | `[E+0x88]`, nonzero | `0x7519B`; lines 10213–10259; copies to `[R+0x14]` at `0x75034` |
| `0x75A20` | `[E+0x88]` | `0x75CA7`; lines 10751–10805 |
| `0xD1C10` | `[E+0x88]`, nonzero | `0xD1C8A`; lines 13775–13802 |
| `0x80A00` | `[R+0x14]` | `0x80A16`; lines 10903–10923; loads `R` from `E+0xC0` |
| `0x82A90` | `[R+0x14]` | `0x82AAA`; lines 11149–11199 |
| `0x828F0` | constant `0x4002` | `0x82909`; lines 11035–11117 |
| `0x87CE0` | `[R+0x14]` | `0x87DCC`; lines 11380–11426; `R` loaded at `0x87D0C` |
| `0x92010` | `[[Harry+0xAC]+0x24]` when that object's `+0x1C` bit 1 is set; otherwise `0x6000` | `0x92010` loads Harry's object into stack local; grants at `0x9220A` and `0x92253`; lines 11699–11713, 11940–12019 |
| `0xEC200` | `[R+0x14]` | `0xEC22F`; lines 14992–15022 |

Constant reward evidence: `0xD1029` → `2008`; `0xE4E51` → `2012`;
`0xE4E93` → `200d`; `0xE4ECD` → `2015`. Overlapping generated copies
(`D0FD0`/`D100A`) count once. The `0x6000` and `0x4002` constants above
are object grants, not separate global scripted reward locations.

## Runtime matching and limitations

`sub_00081A10` calls its renamed vanilla body first. Only successful `AL != 0`
grants are matched. It chooses the nearest catalog object in the current level
with that exact item ID, within **six world units in 3D**, using original TSV
coordinates. This allows collection/interaction reach and small animation
offsets while avoiding a floor above/below Harry. Six is a provisional choice
to validate with gameplay, not a measured universal pickup radius. If there is
no match, an existing `reward:<hex>` key is reported instead. Bridge-delivered
cards are guarded against reporting; repeated grants select the same nearest
location, including already checked locations, to avoid consuming neighbours.

- This implements the census/matching pipeline, not a completed live census.
  No game was launched or controlled during implementation.
- One loaded start/story state cannot expose every spawn, shop stock, reward,
  or container. Collected or dormant entities may be absent. Sweep relevant
  starts/day states separately. Callback functions may not themselves appear
  as entity `+0x70` handlers; a zero count does not establish absence.
- The shop helper `0xC51F0` reads a descriptor, not one of these entity fields.
  Its card purchases spawn `+0x88` pickups, but non-card stock needs additional
  descriptor census/context to become distinct checks. Generic script operands
  (`0x70456`) and pending reward IDs (`0x209AD8`) also lack stable identities in
  this census. Coverage of every shop/script reward remains unverified.
- `0x92010`'s item is player-state dependent; a later state can grant another ID.
  Moving/dynamically spawned items may be outside the census radius, and nearby
  identical items can be misattributed. Train/debug grants are not AP deliveries
  and can also trigger the hook; avoid them during an AP run.
- Keys use Python's whole-unit rounding (ties to even), lowercase four-digit
  item hex, and x/z only. Two rows with the same rounded identity are rejected
  explicitly; vertical stacks and repeated snapshots need manual review. The
  requested key format cannot represent such objects as distinct locations.
- Unknown consumables/dispatches retain `item 0xNNNN` labels; no table ordering
  or item identity is inferred from generated destination order.
- The census warps alter game/story state. Use a disposable save. These checks
  retain vanilla rewards; AP reward replacement and world logic are separate.

## Verification

```powershell
python -m unittest discover -s port/tools -p test_ap_census.py
```

This checks all field sources, the `+A8`/`+C0` distinction, heap holes, array/heap
deduplication, live dispatch ordering, warp layout, default catalog identity,
location metadata, rewards, collisions, and invalid coordinates.

In MSYS2 UCRT64 (temporary files stay in the worktree):

```sh
export PATH=/c/msys64/ucrt64/bin:$PATH
export TMPDIR="$PWD/port/build-ap" TMP="$PWD/port/build-ap" TEMP="$PWD/port/build-ap"
cc -O2 -ffunction-sections -fdata-sections port/ap/test_give_hook.c -Wl,--gc-sections -o port/build-ap/test_give_hook.exe
port/build-ap/test_give_hook.exe
cmake --build port/build-ap -j4
```

The native harness uses mock grant/bridge functions and reserved synthetic guest
memory. It checks success/failure, reward fallback, guest EAX/stack behavior, and
suppression of bridge-delivered cards. It does not execute generated game code.
`regen.sh` and CMake's regeneration comment include `hle_ap.c` in
`--exclude-manual`; the existing local generated body was renamed by hand for
this build. Generated files and research are deliberately not committed.
