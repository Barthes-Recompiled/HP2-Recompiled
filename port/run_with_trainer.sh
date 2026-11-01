#!/usr/bin/env bash
# The game and the debug trainer together, in one Wine prefix (one wineserver, so the trainer finds the game's
# shared block). The game runs as run_win.sh starts it (`direct`, the old way to skip the launcher, still works).
# The trainer closes when the game does.
here="$(cd "$(dirname "$0")" && pwd)"
export WINEPREFIX=${WINEPREFIX:-$HOME/.wine}
export WINEDEBUG=${WINEDEBUG:--all}
before=$(pgrep -x hpcos_trainer.e | sort)
(cd "$here/run" && exec wine ./hpcos_trainer.exe) &
sleep 2
# Wine re-parents its processes (names cut to 15 characters): note the trainer's own PIDs (not one started elsewhere) to close it later
mine=$(comm -13 <(echo "$before") <(pgrep -x hpcos_trainer.e | sort))
[ "$1" = direct ] && shift
"$here/run_win.sh" "$@"
[ -n "$mine" ] && kill $mine 2>/dev/null
exit 0
