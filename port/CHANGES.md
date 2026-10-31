# Changes

## 1.0.0 (2026-10-31): Barthes Recompiled, built on MiddleDucky's v0.2.0

This is the first Barthes Recompiled release of the port. Everything that makes the game run on the PC is
**MiddleDucky's** work ([HP-COS-Xbox-Recomp](https://github.com/MiddleDucky/HP-COS-Xbox-Recomp)), and this release
carries all of their v0.2.0 below, open Hogwarts grounds and broom landing included. Thank you, MiddleDucky, so very
much, for bringing this game to the PC and for sharing it so generously.

### New
- **Settings inside the game.** "Settings" in the title menu and the pause menu opens Gameplay, Graphics and Controls
  pages drawn in the game's own font, with the game's own A and B button gems in the hint. They change the same
  settings as F10 and save to `hpcos.toml`.
- **Button Mapping** (Settings > Controls): press the action, then the controller button for it.
- **Both discs.** The European (PAL) disc now works as well as the North American one. Each disc has its own
  download; the Barthes Launcher picks the right one for your disc. (The note under v0.2.0 that only the NTSC-U
  release works is about that version.)
- **Cutscene fast-forward:** hold Start or Back to run a cutscene to its end quickly (on by default).
- **Start-up logos skipped** (on by default; F10 > Gameplay or Settings > Gameplay to turn it off).
- **Controller in background** (Settings > Controls, F10, launcher): turn it off and the game ignores the controller
  while another window has the focus.

### Fixes
- The settings pages fit a 4:3 screen: label and value columns, and the "press a button" prompt in the hint's place.

### Known issues
- Button prompts are always Xbox's; PlayStation and Switch pads work through Steam Input or DS4Windows.
- Tom Riddle's eyes and mouth in the final cutscene, as in v0.2.0.

## v0.2.0 (2026-10-30), compared with v0.1.0

### New: Enhancements (F10)
- **Explore Hogwarts Grounds (experimental).** Walk the whole grounds freely: the paths no longer fade out to their other end, every area stays drawn and solid, and the destination names and warp prompts are hidden. The low walls at the entrance can be crossed; the grounds' outer limits stay. The story's cutscenes and lessons on the grounds still play (the first flying lesson starts in its arch between the torches). Flying practice can only be repeated with the option off. Harry can't walk onto the lake.
  - **At night** the whole night castle is drawn, the flying practice arch takes Harry to the field and the field's exit brings him back, and the broom button shows as unavailable.
- **Land the broom on any flat surface (experimental).** Over a flat roof or a wall's top, the land button lands as in one of the game's landing zones (never on the lake).

### Graphics
- **Post-processing** (F10 > Graphics): bloom, sharpening, saturation, vibrance, warmth, vignette and film grain, all off or neutral by default.
- **Field of view slider (experimental)**, 80 to 130 % of the game's.
- **The castle's rooms no longer come and go** with where the camera looks when "Draw everything in view" is on, and the grounds drawn whole no longer make the frame wait for vertex uploads.

### The launcher
- **Checks your game files.** On the first start, after unpacking a disc image, and from "Verify game files", every file is compared with the disc's; damaged or missing ones are named, and "Repair" writes them again from a disc image in the game folder. Later starts only check the sizes. Each log's first line says whether the files were whole.
- **A new look:** the title in the Magic School One font over a dark green backdrop, with no game artwork; the exes have a wand icon in the same colours.
- **No more crash under Wine** when a DXVK `dxgi.dll` sits beside the exes; under Wine's own slow Direct3D 12 the page suggests running through Proton.
- **`show_launcher` is the first setting in `hpcos.toml`**, to bring the page back (or hold Shift, or F10).

### Other
- **Another edition's game files are turned away with a clear message** instead of the game quitting at once (only the NTSC-U release works).

## v0.1.0 (2026-10-25): first release

The whole game, from the Burrow to the Chamber, runs natively on Windows 10 and 11, with no emulator. It needs the files from your own Xbox disc, or a disc image. Everything below is what the port adds to, or changes from, the original Xbox game.

### Graphics
- **Direct3D 12 renderer:** Direct3D 12 replaces the Xbox's graphics chip. You can choose:
  - an internal resolution from the Xbox's own 480 lines up to 4K, or one that follows the window, with render scale and a height cap;
  - MSAA (2x, 4x, 8x) and SMAA anti-aliasing, alone or together;
  - anisotropic filtering up to 16x;
  - brightness and contrast;
  - windowed, borderless or fullscreen, with vsync and an FPS counter.
- **True 16:9:**
  - The camera shows more at the sides, while the vertical view stays as on the console.
  - The HUD goes out to the screen's edges: the life meters, the button cluster, the counters and Quidditch's scores. The broom race countdown stays in the middle.
  - Menus, the inventory, the books and the loading screens keep their 4:3 layout, with their backdrops filling the screen.
  - Cutscene shots that would show more than the console did are framed to the 4:3 width.
  - Leaving the inventory or a wizard card's screen brings 16:9 straight back.
  - Centred text in menu messages stays centred.
- **Draw everything in view** (on by default): objects at the screen's edges and in rooms seen through doorways stay drawn, where the Xbox left some out.
- **Sharper at high resolutions:**
  - 2D pieces meet without seams.
  - Lines are drawn the way the console draws them.
  - Glows keep their shape when upscaled.
  - Depth precision is improved.
- **Smaller visual fixes:**
  - Fading objects (sludge, cut curtains) are drawn in the right order.
  - When Hermione, Ron and Tom Riddle talk, their teeth and gums no longer show through the chin as the jaw closes, as they do on the Xbox.
  - There's no blue flash at the start of a level.
  - The game window is black, not white, while the game starts.
- **The original games' movies** play through the same window.

### Smoother play
- **Shaders come with the port,** prepared on another PC. Every effect the game draws on your PC is also recorded and prepared again, in the background, at the next start ("Preparing shaders" shows in the corner). A level's first visit doesn't pause for shaders it has seen before.
- **Rain is drawn in one go** instead of one drop at a time (it ran at 30 fps in the rain).
- **Background loading doesn't stall the picture on Windows:** the game's file-cache worker shares the processor fairly with the game, instead of taking it for up to 0.4 s at a time.
- **Inventory loads stay quick** while that worker is busy.
- **The game's clock ticks every millisecond,** as the console's does. Windows' own timer moves in steps of about 15.6 ms, which made music and timing slightly uneven.
- **Freed memory is given back,** as the console does, instead of the game retrying the same memory request thousands of times.
- **The game's checksum routine runs natively,** much faster.

### Sound
- **DirectSound** is replaced by a native mixer. Sounds fade in and out over a few milliseconds instead of clicking.
- **No more crackle:** the music and ambience keep their timing on Windows.
- **5.1 surround:** the console's own speaker mix with each channel on its own speaker (F10 > Audio > Speakers). Stereo is still the default.
- **Music fixes:**
  - In the Basilisk fight, the sword's theme keeps playing once it has started, instead of stopping and restarting every time the sword is dropped and picked up.
  - A spell book's jingle no longer plays twice in Defence Against the Dark Arts (Expelliarmus).
  - A chest's lid sound no longer plays when a level load reopens it.

### Controls
- **Xbox controllers,** with vibration and a vibration strength.
- **Keyboard and mouse** play alongside a controller. Every key can be rebound, and the mouse sensitivity and Y inversion can be set.
- **Movement pace for the keys:** Slow walk, Walk or Run, on the mouse wheel or in F10 > Controls.
- **Controller settings:** button remapping, deadzones and sensitivity for each stick, and camera inversion. The stick's full range reaches a run on PC pads.
- **Several controllers** all drive player 1, so a pad that Windows shows twice no longer splits the input.
- **Use controller** (launcher and F10): turn it off to ignore a connected pad.
- **Alohomora:** pressing the left trigger during the cast no longer moves the spell's light. Aiming first and then casting still works.

### Game fixes
- **The Whomping Willow's right hand** finishes its solo strike, instead of ending it on its first frame.
- **A Grand Staircase box** whose lid never opened now opens.
- **Vases on the Grand Staircase** no longer vanish after going through the library annex.
- **The tower bounce** and other moves reached only through pointers in the game's code now work.
- **Saves and the clock:** saves are stamped with your PC's local time and time zone (it was a fixed UTC+5). The game's region is the one your disc is made for, so the US release plays in US English.
- **The cache partition** is kept between starts instead of being formatted at every start.
- **Quit To Desktop** in the title and pause menus.
- **The Gringotts portrait door,** a GameCube Game Boy Advance bonus, is enabled.
- **House points keep mattering:** on the Xbox the nightly board stops Slytherin at 300 points, so past that Gryffindor leads every night. Here Slytherin has no cap, and Ravenclaw and Hufflepuff can reach 600. Being caught by Goyle always costs 5, 10 or 20 points, where the Xbox could also cost nothing. `RECOMP_HOUSE_CAPS=0` keeps the Xbox's caps.

### Options and the launcher
- **The launcher** sets the aspect ratio, display, graphics and controls before the game starts, and can be skipped (hold Shift to see it again). It unpacks a `.xiso` or `.iso` in the `game` folder the first time.
- **F10 options** over the game. Most changes apply at once. "Defaults (this page)" resets only the page you're on.
- **Colour-blind correction filters,** with a strength setting.
- **Only one copy of the game runs at a time.** Starting it again brings the running game to the front.
- **Logs for problem reports:** a log of each start is written to `logs\runs` (the newest 10 are kept). F10's Debug tab opens that folder, marks a moment in the log, and captures a frame (F12).

### Known differences
- **On a new graphics card or driver,** the first visit to an area can still pause briefly while shaders are prepared; the loading picture stays up meanwhile.
- **In the Chamber's last cutscene,** Tom Riddle's eyes and mouth can show through his face when he looks down.
