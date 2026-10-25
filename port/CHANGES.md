# Changes

## v0.1.0 (2026-10-25): first release

The whole game, from the Burrow to the Chamber, runs natively on Windows 10 and 11, with no emulator. It needs the files from your own Xbox disc, or a disc image.

### What's in it
- **Graphics:** a Direct3D 12 renderer.
  - Internal resolution up to 4K, render scale, and MSAA and SMAA anti-aliasing.
  - Anisotropic filtering, plus brightness and contrast.
  - Windowed, borderless or fullscreen, with vsync.
- **True 16:9:** the camera shows more at the sides, while menus, the HUD, the inventory and cutscene shots keep their 4:3 shape.
- **Draw everything in view:** objects at the edges of the screen and behind doorways stay drawn, where the Xbox left some out. It's on by default.
- **Sound:** stereo, or the console's 5.1 surround mix (F10 > Audio > Speakers).
- **Controls:**
  - Xbox controllers, with vibration.
  - Keyboard and mouse playing alongside a controller.
  - Remappable keys and buttons, deadzones and sensitivity.
  - A movement pace for the keys (Slow walk, Walk, Run) on the mouse wheel.
  - "Use controller" to ignore a connected pad.
- **Smooth play:**
  - Shaders prepared on another PC come with the port.
  - Every effect the game has drawn on your PC is prepared again at the next start.
  - The game's threads are scheduled so loading in the background doesn't stall the picture on Windows.
- **Accessibility:** colour-blind correction filters.
- **The launcher:**
  - Sets the aspect ratio and settings before the game starts.
  - Unpacks a `.xiso` / `.iso` in the `game` folder the first time.
  - Keeps the newest 10 logs for problem reports.
- **Fixes over the original game:** one Grand Staircase box with the wrong lid animation, and Alohomora's light jumping when the left trigger is pressed during the cast. The original behaviour is kept as an option (`RECOMP_BOX_ANIM=0`, `RECOMP_ALOHOMORA_AIM=1`).

### Known differences
- **Shader stutter on a new graphics card or driver:** the first visit to an area can still pause briefly while shaders are prepared; the loading picture stays up meanwhile.
