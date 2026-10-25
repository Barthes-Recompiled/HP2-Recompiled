# Developing the HP: Chamber of Secrets port

Everything needed to go from a fresh clone to a built, running and packaged game. Players want
[README.txt](README.txt) instead.

The port is a static recompilation of the Xbox title (`default.xbe`, XDK 5028) built on the
**xboxrecomp** toolkit (repository root: `src/`, `tools/`, `templates/`, see
[README.xboxrecomp.md](../README.xboxrecomp.md)), with the game project in `port/`. Rendering is a D3D8 HLE →
Direct3D 12 renderer by TGP482 and Joshhhuaaa (MIT, `port/src/renderer/LICENSE`), adapted to this title.

**Nothing from the game is in this repository**: no game files, no disassembly, no recompiled code, no
artwork. You need your own copy of the Xbox disc; the pipeline regenerates everything else from it.

How the port works inside (the lifter, the HLE layers, the renderer, threads and timing) is in
[HOW_IT_WORKS.txt](HOW_IT_WORKS.txt); every change from the original game's behaviour, with the switch that turns it
off, is in [PORT_CHANGES.md](PORT_CHANGES.md).


## 1. Layout

```
<repo>/
  game/                   YOUR disc files (default.xbe, ...) - ignored by git
  src/ tools/ templates/  xboxrecomp toolkit: runtime libraries (kernel, D3D, audio, input, video, USB...)
                          and the Python pipeline (xbe_parser, disasm, func_id, abi_analysis, recomp)
  cmake/ include/ docs/ tests/ LICENSES/   toolkit
  port/
    CMakeLists.txt        the game: hpcos.exe, hpcos_launcher.exe, hpcos_trainer.exe
    regen.sh              XBE -> port/src/recomp/gen/*.c (generated, ignored by git)
    icall_seeds.json      indirect-call targets the disassembler can't find by itself (regen.sh input)
    src/                  hand-written code
      main.c              startup: single instance / launcher check, debug.env, settings, XBE load, kernel
      hle_*.c             overrides of recompiled game/XDK functions
      renderer/           renderer (gpu/d3d12.cpp, shaders.cpp, pipeline_cache.cpp), XDK D3D8 HLE
                          (xdk/hle_d3d8.cpp), settings + F10 panel + input (app/)
      launcher/           hpcos_launcher.exe (Dear ImGui over D3D11), with the disc image unpacker (xiso.c)
      trainer/            hpcos_trainer.exe, the debug trainer
      recomp/             recomp_types.h etc.; gen/ is generated
    res/                  app.ico; base_shaders.bin and base_pipelines.bin shipped with releases (see 5)
    tools/                package_release.sh, check_tls_refs.py, make_icon.py, stall_report.py
    third_party/imgui/    Dear ImGui (MIT)
    README.txt            the players' readme (shipped with releases)
```


## 2. Setup

The exes are Windows programs built with MinGW-w64 GCC.

**On Windows**: MSYS2, UCRT64 environment.

1. Install MSYS2 (`winget install --id MSYS2.MSYS2 -e`, or msys2.org).
2. In the **MSYS2 UCRT64** terminal:
   ```
   pacman -Syu
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
                      mingw-w64-ucrt-x86_64-python mingw-w64-ucrt-x86_64-python-capstone
   ```

**On Linux**: cross-compile with the `x86_64-w64-mingw32` toolchain (`mingw-w64-gcc`), CMake and Python 3 with
`capstone`, using the toolchain file `cmake/mingw-w64-x86_64.cmake`. The game runs under Wine for testing.

Then copy the disc's files into `<repo>/game/`, so that `<repo>/game/default.xbe` exists.


## 3. Regenerate the recompiled code

```
bash port/regen.sh          # from anywhere; a few minutes
```

It disassembles `game/default.xbe` (seeded with `port/icall_seeds.json`), identifies library functions, analyses
calling conventions and writes `port/src/recomp/gen/recomp_000N.c` and its headers.

Every hand-written override file is passed with `--exclude-manual`: a `sub_XXXXXXXX` defined in one of them keeps
the generated body as `sub_XXXXXXXX_gen`, which the override calls when it wants the original behaviour (160
functions today). **A new override file must be added to regen.sh** and to CMakeLists.txt. Never hand-edit
`gen/`: regenerate.

If the game jumps through a pointer to code the disassembler never saw (an `ICALL` failure in the log), add the
target to `icall_seeds.json` and regenerate.


## 4. Build

```
cd port
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

On Linux add `-DCMAKE_TOOLCHAIN_FILE=../cmake/mingw-w64-x86_64.cmake` (and leave out `-G Ninja` if Ninja is not
installed). Outputs in `port/build/`: `hpcos.exe`, `hpcos_launcher.exe`, `hpcos_trainer.exe`.

* The exes' icon is the port's own `res/app.ico` (a wand and sparkle drawn from shapes by `tools/make_icon.py`):
  no game artwork is built in.
* Generated code is compiled at **-O2** on purpose: GCC 16 at -O3 hoisted the address of a thread-local array
  out of a loop, reading the TLS template instead of the thread's copy. The post-build step
  `tools/check_tls_refs.py` fails the build on any such access.
* CMake options: `HPCOS_RELEASE` (players' build: stack canaries, SSE4.1), `HPCOS_VERSION` (the version in the
  exes' Properties > Details), `HPCOS_DIFFTEST` (differential test against the original x86 code, Unicorn),
  `HPCOS_CALL_PROFILE` (per-target call counts).


## 5. Run, test, release

Start `hpcos_launcher.exe` from the build folder with the game files beside it (a `game\` folder), or copy the exes
next to your `game\` folder. `hpcos.exe` refuses to start without the launcher.

`debug.env` beside the exe sets environment switches at start (one `KEY=VALUE` a line, `#` comments): the
`RECOMP_*` switches in PORT_CHANGES.md turn individual changes off, and the log tracers on.

A release is made by

```
VERSION=0.1.0 bash port/tools/package_release.sh
```

which builds with `HPCOS_RELEASE=ON`, checks the security flags (ASLR, DEP, stack protector) and that every DLL the
exes import comes with Windows, and writes `port/dist/hpcos-<date>/` and `port/dist/hpcos_v<VERSION>_windows.zip`.
`res/base_shaders.bin` and `res/base_pipelines.bin` (a Windows play-through's `shadercache\shaders.bin` and
`pipelines_list.bin`) ship as `shadercache\base_*.bin`: a player's first visit to an area then needs no shader
compiler, and the pipelines are made ahead at the first start. Refresh them when shader generation changes.
