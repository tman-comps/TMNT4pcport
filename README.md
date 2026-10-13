# Teenage Mutant Ninja Turtles IV: Turtles in Time — PC Port

A native PC version of the SNES **Teenage Mutant Ninja Turtles IV: Turtles in Time** (Konami, 1992), made by statically recompiling the original game code to C and running it on an accurate SNES graphics and sound core, with PC features added on top. It's built the same way as the [Super Star Wars trilogy port](https://github.com/tman-comps/super-star-wars-trilogy-pc).

> ⚠️ **This repository contains no game code or data.** You need your own dump of the original cartridge. No ROMs, graphics, music, levels or game code are included or will be provided. Please don't ask.

---

## Features

- **Native speed, faithful gameplay.** The recompiled code does exactly what the original does: each instruction performs the same bus accesses as the reference interpreter, and the two are checked frame for frame (identical RAM, VRAM, palette and sprite memory every frame).
- **Two players.** Each player gets their own keys and controller. The first controller is player 1, the second player 2 (swappable). Player 2 joins any time with Start, as on the console.
- **Widescreen** 16:10, 16:9 and 21:9 while a scene is being played. The extra columns come from the level data the game has already loaded either side of the screen. Columns that were never loaded, such as left of where a scene or boss room starts, stay black like the end of a level. Title, cutscenes and scene cards stay 4:3.
- **Remove slowdown.** The SNES version lags when the screen gets busy; this gives the game's CPU enough time every frame. Only active while a scene is being played.
- **Pause menu.** Start (or Esc, or the Guide button) opens the port's menu over the game: Resume, Restart scene, Continue from the last scene, Port settings, Achievements, Statistics, Save/Load state, Reset, Quit. The Start button can be set back to the game's own pause.
- **Achievements:** 16 of them, with pop-ups, a list in the menu, and optional Steam support.
- **Speedrun timer** with per-scene splits and saved personal bests (in console frames, so PC speed doesn't matter), plus play statistics.
- **Rewind:** hold Backspace or the left trigger.
- **CRT filter** (scanlines or full CRT), **colour styles**, **sprite flicker removal** and crisper sound interpolation.
- **Orchestral / remastered music** through your own MSU-1 audio packs (see `msu/`).
- **Extras**, unlocked by beating the game: start at any scene, infinite lives, and Pizza Power (health always full).
- Rumble, full keyboard and controller rebinding for both players, 60 fps at any window size, and crash reports you can send.

---

## What you need

| Game | Region / Version | File name | SHA-1 (headerless) |
|------|------------------|-----------|--------------------|
| Teenage Mutant Ninja Turtles IV: Turtles in Time | USA | `tmnt4.sfc` | `4f8fc0f45023e0339e501054b253c33f479a001a` |

Other versions (Europe, Japan, prototypes) are **not supported** and are rejected at startup with a message saying which version is needed. A 512-byte copier header is stripped automatically. If your file has another name, the program also finds it by its contents when it's next to the program.

**Checking your ROM on Windows:** open PowerShell in the folder with your ROM and run

```powershell
Get-FileHash .\tmnt4.sfc -Algorithm SHA1
```

(or build `romhash`, see below, which also handles copier headers).

---

## How to play

There are two versions:

- **The launcher** (`tmnt_launcher.exe`) contains no game code. It runs the game on the built-in SNES core straight from your ROM. GitHub builds this one automatically, and it's the one attached to Releases.
- **The native build** (`tmnt.exe`) translates the game's code to native C from *your* ROM when you build it. You build it yourself (see below).

Both have the same features. To play:

1. Put the program, `SDL2.dll`, and your ROM (`tmnt4.sfc`) in one folder.
2. Run the program. You can also drag the ROM file onto it.

Settings are saved in `tmnt.ini` next to the program. Save states, best times, stats and achievements are kept in `tmnt*` files next to it too.

---

## Controls

All of these can be changed in *Port settings → Player 1 / Player 2 controls*.

| SNES button | In game | Player 1 keys | Player 2 keys | Controller (Xbox layout) |
|-------------|---------|---------------|---------------|---------------------------|
| D-pad | Move | Arrow keys | I J K L | D-pad / left stick |
| Y | Attack | A | B | X (left) |
| B | Jump | Z | N | A (bottom) |
| A | Special attack | X | H | B (right) |
| X | | S | G | Y (top) |
| L / R | | Q / W | T / Y | LB / RB |
| Select | | Right Shift | V | Back |
| Start | Pause menu / join | Enter | M | Start |

| Shortcut | Key |
|----------|-----|
| Menu | Esc, the Guide button, or Back+Start |
| Save / load state | F5 / F7 |
| Restart scene | F8 |
| Rewind (when enabled) | hold Backspace / left trigger |
| Fast forward | hold Tab |
| Fullscreen | F11 or Alt+Enter |
| Recompiled / interpreted CPU (native build) | F12 |

**Steam Controller:** while Steam is running, it applies its Desktop Configuration to programs it didn't start (A acts as Enter, and so on). Add the program to Steam as a non-Steam game, or set a gamepad template as Steam's Desktop Layout.

---

## Building from source

The build uses a Makefile with GCC: native on Linux, and cross-compiled to Windows with mingw-w64. On Windows itself, use WSL or MSYS2.

**Requirements**

- GCC and GNU make; `mingw-w64` for Windows builds
- SDL2 development files (`libsdl2-dev` on Debian/Ubuntu). For Windows builds, `make sdl2` downloads the official SDL2 mingw package into `third_party/SDL2-mingw`.
- Python 3, only for `make gen`
- `curl`, only for `make sdl2`

**Launcher (no ROM needed to build)**

```sh
make sdl2               # once, for Windows builds
make launcher           # tmnt_launcher (Linux) + tmnt_launcher.exe + SDL2.dll
```

**Native build (needs your ROM)**

```sh
mkdir roms
cp /path/to/your/tmnt4.sfc roms/
make gen                # translates the game code from your ROM into gen/
make tmnt               # Linux
make sdl2 win           # Windows: tmnt.exe + SDL2.dll
```

`roms/`, `gen/` and `build/` are ignored by git, so they are never committed.

**Tools**

- `make romhash`: prints a ROM's SHA-1 (`./romhash tmnt4.sfc`).
- `make tool`: `tmnt_tool`, a headless scripted driver (no window or sound). It plays input scripts, takes screenshots and save states, watches RAM, logs songs and sound commands, and writes coverage logs. See the comment at the top of `src/tool_main.c`. `tmnt_tool roms/tmnt4.sfc interp "..."` against `... recomp "..."` with `hash` compares the two CPUs frame by frame.
- `tools/recomp.py`: the recompiler. It turns each traced instruction into the C body of the same opcode in LakeSnes's interpreter, so behaviour and timing are identical.
- `tools/cdlmap.py` and `tools/cover/run_all.sh`: build the code map (`analysis/tmnt4/map.json`) from coverage logs of the game being played on the interpreter, plus static tracing.
- `tools/trace.py`, `tools/emit.py`: static tracer and disassembly listing (written from your ROM, never committed).
- `analysis/tmnt4/`: what the recompiler needs (code addresses and CPU modes) and notes on the game's RAM. These files contain no bytes from the game.

---

## How it works

Turtles in Time dispatches nearly everything through jump tables and pushed return addresses, so static tracing alone finds only a few percent of its code. The code map is instead built from what the game actually executes: the interpreter plays every scene, both game modes and the menus (with a bot, and test cheats so it gets through), and logs every instruction it runs with its CPU mode. Static tracing then fills in the paths around them. Code the map misses still works: the recompiled code hands it to the interpreter one instruction at a time. So does the code the game copies into RAM and runs there.

---

## Credits

- **Port by T-Man**
- Original game by Konami
- Built with the help of [Claude](https://claude.ai) (Anthropic), an AI assistant, used for the reverse engineering, recompilation, front-end code, testing and documentation
- [LakeSnes](https://github.com/angelo-wf/LakeSnes) SNES core (MIT), modified; [SDL2](https://libsdl.org) (zlib); the public-domain font8x8. See `THIRD_PARTY_LICENSES.txt` and `third_party/`.
- Inspired by other SNES reimplementation projects such as [zelda3](https://github.com/snesrev/zelda3)

---

## Legal

This is an unofficial fan project made for preservation and personal use. It is **not affiliated with, endorsed by, or connected to** Konami, Paramount, Nickelodeon or Mirage Studios. Teenage Mutant Ninja Turtles is a trademark of its respective owners.
