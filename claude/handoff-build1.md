# Handoff: build 1 (2026-10-13)

First complete build of the Turtles in Time port, made in one overnight session from T-Man's ROM
(USA, SHA-1 4f8fc0f4...). Same architecture as tman-comps/super-star-wars-trilogy-pc.

## What's done
- Pipeline: LakeSnes core (copied from the Star Wars repo, with its port additions: wide rendering,
  hi-res Mode 7, sprite limit off, overclock, write filter), tools/recomp.py (unchanged), new
  tools/cdlmap.py (code map from coverage logs + static tracing), tools/cover/run_all.sh.
- Code map: 48.6k instructions from 40 coverage logs (every scene via the scene warp, both players,
  menus, attract, the ending, game over). Recompiled build checked identical to the interpreter
  (hash of WRAM+VRAM+CGRAM+OAM every frame) in several scenes, no misses in ROM code. The game runs
  some code from WRAM ($7E4200...), that stays on the interpreter.
- Front-end (src/main_sdl.c): 2 players (keys + pads, rebinding, pad swap), pause menu, settings
  pages, save/load state, restart scene, continue from last scene, rewind, fast forward, CRT,
  colour, sprite limit off, sound interpolation, remove slowdown, Mode 7 detail, widescreen,
  achievements (16), timer with splits/PBs, stats, MSU-1, rumble, crash reports, title credit.
  Menus use the game's own HUD font, captured from VRAM (tmnt_font.bin cache).
- tmnt_tool (src/tool_main.c): scripted headless driver used for everything above.

## RAM (analysis/tmnt4/ram.txt)
$32 mode, $68 transition (0 = playing), $82 scene (0-9; 10 = Splinter's lair: ending and Versus),
$3A frame counter, players at $1A80/$1AC0 (+$1A turtle, +$20 lives, +$22 health), boss bar $1BC8,
object health $091A + n*$70. All game logic runs in the NMI; the main loop idles at $80:8154.

## Music (src/msu.c)
Songs are streamed to the SPC; a song = FNV checksum of the bytes sent on $2141-$2143 since the
last $FE (stop), taken when $80 (start) goes out through the command queue (sender $80:835C).
Rewriting that $80 to $FE silences the SNES music and keeps the effects (checked with a test PCM).
msu/msu.txt lists the 24 songs found; labels are by where they first played, some are guesses.

## Widescreen (src/widescreen.c)
Extra columns straight from VRAM (measured: the main layer is loaded >= 88 px beyond both edges
while scrolling). After a camera jump (scene start, room switch) only [camera, camera+344] is
trusted, growing with the camera; outside is black. State loads trust the whole tilemap ring.

## Not done / ideas
1. Time Trial isn't detected (no RAM flag found yet); the timer/achievements treat it as a game.
2. The game's own cheat codes weren't found; extras unlock by beating the game.
3. The Technodrome Shredder (thrown-soldier fight) stops the coverage bot; scenes after it were
   covered through the scene warp.
4. Possible polish: draw the menus into BG3 like the Star Wars port (here they're an overlay in the
   game's font); boss-room-specific widescreen limits.
5. The Beta 2 prototype T-Man uploaded (different intro with text credits) wasn't explored further.

## Setup in a new cloud session
Upload the ROM to roms/tmnt4.sfc, then:
`apt-get install -y libsdl2-dev gcc-mingw-w64-x86-64`, `make gen`, `make tmnt tmnt_tool`,
`make sdl2 && make win` for tmnt.exe + SDL2.dll. Never publish tmnt.exe (it contains code
generated from the ROM); the launcher is what CI builds.
