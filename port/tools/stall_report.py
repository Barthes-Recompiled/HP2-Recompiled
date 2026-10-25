#!/usr/bin/env python3
"""Name the hitch diagnostics of a run log: [STALL] (main.c's stall watcher), [SLOWCALL] (toolkit kernel_bridge.c)
and [SLOWGPU] (renderer/gpu), in log order.

    tools/stall_report.py <hpcos_run_*.log> [hpcos.exe]

Guest addresses are named by the lifted function that contains them (src/recomp/gen), host frames (hpcos+<offset>)
by nm on the exe that made the log -- the same build, or the offsets point elsewhere. The default exe is
build-win/hpcos.exe; logs/builds/ keeps copies of the builds handed out for testing.
"""
import bisect
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
IMAGE_BASE = 0x140000000
TAGS = ("[STALL]", "[SLOWCALL]", "[SLOWGPU]")


def guest_functions():
    starts = set()
    gen = os.path.join(ROOT, "src", "recomp", "gen")
    for name in os.listdir(gen):
        if not name.endswith(".c"):
            continue
        with open(os.path.join(gen, name), errors="replace") as f:
            for line in f:
                m = re.match(r"void sub_([0-9A-F]{8})(?:_gen)?\(void\)", line)
                if m:
                    starts.add(int(m.group(1), 16))
    return sorted(starts)


def host_symbols(exe):
    out = subprocess.run(["x86_64-w64-mingw32-nm", "-n", exe], capture_output=True, text=True).stdout
    syms = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tT":
            syms.append((int(parts[0], 16), parts[2]))
    return syms


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    exe = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build-win", "hpcos.exe")
    funcs = guest_functions()
    syms = host_symbols(exe)
    addrs = [a for a, _ in syms]

    def guest(v):
        i = bisect.bisect_right(funcs, v) - 1
        return "sub_%08X+%X" % (funcs[i], v - funcs[i]) if i >= 0 else "%08X" % v

    def host(m):
        a = IMAGE_BASE + int(m.group(1), 16)
        i = bisect.bisect_right(addrs, a) - 1
        return "%s+%X" % (syms[i][1], a - syms[i][0]) if i >= 0 else m.group(0)

    def guest_list(text):
        return " < ".join(guest(int(t, 16)) for t in text.split())

    with open(sys.argv[1], errors="replace") as f:
        for line in f:
            if not line.startswith(TAGS):
                continue
            line = re.sub(r"\bhpcos\+([0-9A-F]+)\b", host, line.rstrip("\n"))
            if line.startswith("[SLOWCALL]"):
                line = re.sub(r"from 0x([0-9A-F]{8})", lambda m: "from " + guest(int(m.group(1), 16)), line)
            elif " guest:" in line and "| " in line and " ended:" not in line:
                head, _, rest = line.partition(" guest:")
                g, _, tail = rest.partition(" | ")
                line = head + " guest: " + guest_list(g) + " | " + tail
            if "innermost guest:" in line:
                head, _, g = line.partition("innermost guest:")
                g = re.sub(r"\b([0-9A-F]{8})\b", lambda m: guest(int(m.group(1), 16)), g)
                line = head + "innermost guest:" + g
            print(line.replace(" | host:", "\n    host:").replace(" | regs", "\n    regs"))


if __name__ == "__main__":
    main()
