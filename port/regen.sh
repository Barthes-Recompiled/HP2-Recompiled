#!/usr/bin/env bash
# Disassemble + recompile the game's default.xbe into src/recomp/gen. Every hand-written override file is listed
# with --exclude-manual, so the function it replaces keeps the generated body as sub_X_gen.
#   ./regen.sh                     the XBE from game/default.xbe beside the toolkit (the repository's top)
#   XBE=/path/to/default.xbe ./regen.sh
# Works with the toolkit around this folder (the GitHub repository: the toolkit at its top, the port in port/) or
# beside it (../xboxrecomp, a working copy). Needs Python 3.
set -euo pipefail
port="$(cd "$(dirname "$0")" && pwd)"
if [ -d "$port/../xboxrecomp/tools/recomp" ]; then toolkit="$(cd "$port/../xboxrecomp" && pwd)"
else toolkit="$(cd "$port/.." && pwd)"; fi
xbe="${XBE:-$toolkit/game/default.xbe}"
[ -f "$xbe" ] || { echo "no default.xbe at $xbe (copy the game's files to $toolkit/game/ or set XBE=)" >&2; exit 1; }
xbe="$(cd "$(dirname "$xbe")" && pwd)/$(basename "$xbe")"
work="$(dirname "$xbe")"
cd "$toolkit"
# the XBE's layout (sections, imports, entry), which disasm reads beside the XBE as default_analysis.json
python3 -m tools.xbe_parser "$xbe" --json "$work/default_analysis.json"
# icall_seeds.json: functions only reached through pointers (vtables, callbacks) the scan would not find
python3 -m tools.disasm "$xbe" --text-only --extra-sections D3D,D3DX,XGRPH,DSOUND,XMV,XPP \
    --seed-functions "$port/icall_seeds.json"
python3 -m tools.func_id "$xbe"
python3 -m tools.abi_analysis "$xbe"
python3 -m tools.recomp "$xbe" --all --split 1000 --gen-dir "$port/src/recomp/gen" --game-name hpcos \
    --exclude-manual "$port/src/hle_crc.c" \
    --exclude-manual "$port/src/hle_debug.c" \
    --exclude-manual "$port/src/hle_dsound.c" \
    --exclude-manual "$port/src/hle_fade.c" \
    --exclude-manual "$port/src/hle_frontend.c" \
    --exclude-manual "$port/src/hle_gringotts.c" \
    --exclude-manual "$port/src/hle_hud.c" \
    --exclude-manual "$port/src/hle_input.c" \
    --exclude-manual "$port/src/hle_rain.c" \
    --exclude-manual "$port/src/hle_shots.c" \
    --exclude-manual "$port/src/hle_switch.c" \
    --exclude-manual "$port/src/hle_trainer.c" \
    --exclude-manual "$port/src/hle_video.c" \
    --exclude-manual "$port/src/renderer/xdk/hle_d3d8.cpp"
