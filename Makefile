# Teenage Mutant Ninja Turtles IV - Turtles in Time: native PC port
#
# The repository holds no game code or data. The recompiled game code (gen/) is generated from YOUR
# OWN ROM by `make gen`; everything else here is the port's own code.
#
#   make gen          generate the game code from roms/tmnt4.sfc (needs Python 3)
#   make tmnt         the game, Linux (needs the SDL2 dev package)
#   make sdl2         download the SDL2 mingw package for Windows builds (third_party/SDL2-mingw)
#   make win          tmnt.exe + SDL2.dll, cross-compiled with mingw-w64
#   make launcher     tmnt_launcher(.exe): no generated code at all, runs the game on the built-in
#                     SNES core from the player's ROM (what GitHub Actions builds and releases)
#   make tool         tmnt_tool: headless scripted driver for tests (see src/tool_main.c)
#   make romhash      prints a ROM's SHA-1 (see src/rom_check.c)

CC      ?= gcc
CFLAGS  ?= -O2 -g
CFLAGS  += -Ithird_party/lakesnes -Ithird_party -Isrc -Igen -w
LAKE    := $(addprefix third_party/lakesnes/,spc.c dsp.c apu.c cpu.c dma.c ppu.c cart.c input.c statehandler.c snes.c snes_other.c)
GEN     := $(wildcard gen/recomp_*.c)
COMMON  := src/game.c src/runner.c src/rom_check.c src/ui.c src/achievements.c src/extras.c src/msu.c src/steam.c src/crashlog.c src/widescreen.c

BUILD   := build
LAKE_O  := $(patsubst %.c,$(BUILD)/%.o,$(LAKE))
GEN_O   := $(patsubst %.c,$(BUILD)/%.o,$(GEN))
COM_O   := $(patsubst %.c,$(BUILD)/%.o,$(COMMON))

all: tmnt tmnt_tool

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# the big generated files build faster at -O1 with little runtime cost
$(BUILD)/gen/%.o: gen/%.c gen/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -O2,$(CFLAGS)) -O1 -c $< -o $@

tmnt_tool: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/tool_main.o
	$(CC) -o $@ $^

tmnt: $(LAKE_O) $(GEN_O) $(COM_O) $(BUILD)/src/main_sdl.o
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl

$(BUILD)/src/main_sdl.o: src/main_sdl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) `sdl2-config --cflags` -c $< -o $@

tool: tmnt_tool

clean:
	rm -rf $(BUILD) build-* tmnt tmnt_tool tmnt.exe tmnt_launcher tmnt_launcher.exe tmnt_tool_interp romhash

.PHONY: all clean win launcher gen sdl2 tool

# ---- Game code generated from the player's ROM (never committed)
PY ?= python3
gen: gen/recomp_dispatch.c
gen/recomp_dispatch.c: roms/tmnt4.sfc analysis/tmnt4/map.json analysis/tmnt4/hooks.txt tools/recomp.py
	@mkdir -p gen
	$(PY) -I tools/recomp.py roms/tmnt4.sfc analysis/tmnt4/map.json third_party/lakesnes/cpu.c gen "" analysis/tmnt4/hooks.txt
roms/%.sfc:
	@echo "Missing $@ -- copy your own dump of the cartridge there (see README.md)"; exit 1

romhash: tools/romhash.c src/rom_check.c src/rom_check.h
	$(CC) -O2 -Isrc tools/romhash.c src/rom_check.c -o $@

# ---- SDL2 for Windows builds (zlib license), downloaded rather than stored in the repo
SDL2_VER := 2.30.9
WINCC   := x86_64-w64-mingw32-gcc
SDLWIN  := third_party/SDL2-mingw/x86_64-w64-mingw32
sdl2: $(SDLWIN)/lib/libSDL2.a
$(SDLWIN)/lib/libSDL2.a:
	mkdir -p third_party
	curl -fsSL -o third_party/sdl2.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-$(SDL2_VER)/SDL2-devel-$(SDL2_VER)-mingw.tar.gz
	cd third_party && tar xzf sdl2.tar.gz && rm -rf SDL2-mingw && mv SDL2-$(SDL2_VER) SDL2-mingw && rm sdl2.tar.gz

# ---- Windows (cross-compiled with mingw-w64)
WBUILD  := build-win
WCFLAGS := -O2 -Ithird_party/lakesnes -Ithird_party -Isrc -Igen -w -I$(SDLWIN)/include/SDL2 -Dmain=SDL_main
W_LAKE  := $(patsubst %.c,$(WBUILD)/%.o,$(LAKE))
W_GEN   := $(patsubst %.c,$(WBUILD)/%.o,$(GEN))
W_COM   := $(patsubst %.c,$(WBUILD)/%.o,$(COMMON) src/main_sdl.c)

$(WBUILD)/gen/%.o: gen/%.c gen/cpu_helpers.inc src/recomp_rt.h
	@mkdir -p $(dir $@)
	$(WINCC) $(filter-out -O2,$(WCFLAGS)) -O1 -c $< -o $@

$(WBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(WINCC) $(WCFLAGS) -c $< -o $@

tmnt.exe: $(W_LAKE) $(W_GEN) $(W_COM)
	$(WINCC) -o $@ $^ -L$(SDLWIN)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc

win: tmnt.exe
	cp $(SDLWIN)/bin/SDL2.dll .

# ---- Launcher build: no code generated from the game (gen/ is not linked). The game runs on the
# SNES core from the player's ROM. Safe to distribute; slower CPU emulation but the same features.
LBUILD  := build-launcher
$(LBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DTMNT_LAUNCHER `sdl2-config --cflags` -c $< -o $@
tmnt_launcher: $(LAKE_O) $(patsubst %.c,$(LBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(CC) -o $@ $^ `sdl2-config --libs` -lm -ldl

WLBUILD := build-win-launcher
$(WLBUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(WINCC) $(WCFLAGS) -DTMNT_LAUNCHER -c $< -o $@
tmnt_launcher.exe: $(W_LAKE) $(patsubst %.c,$(WLBUILD)/%.o,$(COMMON) src/main_sdl.c)
	$(WINCC) -o $@ $^ -L$(SDLWIN)/lib -lmingw32 -lSDL2main -lSDL2 -mwindows -static-libgcc

launcher: tmnt_launcher tmnt_launcher.exe
	cp $(SDLWIN)/bin/SDL2.dll .

# interpreter-only test driver (no gen/ needed): coverage runs before the code map exists
tmnt_tool_interp: $(LAKE_O) $(patsubst %.c,$(LBUILD)/%.o,$(COMMON) src/tool_main.c)
	$(CC) -o $@ $^

# header dependencies (coarse: any header change rebuilds the hand-written sources, not gen/)
HDRS := $(wildcard src/*.h third_party/lakesnes/*.h)
$(LAKE_O) $(COM_O) $(patsubst %.c,$(BUILD)/%.o,$(wildcard src/*.c)): $(HDRS)
$(W_LAKE) $(W_COM): $(HDRS)
