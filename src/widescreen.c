// Widescreen. Turtles in Time keeps its playfield in 64-tile-wide tilemaps and streams new columns
// in well ahead of the screen edge (measured: at least 88 px ahead of the right edge on the main
// layer, and everything already scrolled past stays until it is overwritten 512 px later). So the
// columns either side of the 256-pixel view are usually already in VRAM and the PPU just renders
// more of each line (ppu->wideExtra, LakeSnes port addition).
//
// What is NOT in VRAM: anything left of where the camera arrived (scene start, a room switch like
// the boss rooms) that was never on screen. The camera history tells us which world columns hold
// real data: from a jump on, [camera, camera + 256 + AHEAD], growing as the camera moves, never
// wider than the 512-pixel tilemap. Outside it the extra columns are drawn black (ppu->wideClipL/R),
// like the space past the end of a level.
// Only used while a scene is being played; title, cutscenes and cards stay 4:3 (pillarboxed).
#include <stdlib.h>
#include "widescreen.h"
#include "game.h"

#define AHEAD 88

static Snes* s;
static int g_active;
static int g_lo, g_hi, g_prev_cam = -100000, g_prev_play;

void ws_init(Snes* snes) { s = snes; }

void ws_update(int want) {
  Ppu* ppu = s->ppu;
  int play = game_in_play(s->ram);
  int cam = ppu->bgLayer[0].hScroll;
  if (!play || !g_prev_play || abs(cam - g_prev_cam) > 32) { g_lo = cam; g_hi = cam + 256 + AHEAD; }
  if (cam < g_lo) g_lo = cam;
  if (cam + 256 + AHEAD > g_hi) g_hi = cam + 256 + AHEAD;
  if (g_hi - g_lo > 512) { if (cam - g_lo > g_hi - cam - 256) g_lo = g_hi - 512; else g_hi = g_lo + 512; }
  g_prev_cam = cam; g_prev_play = play;
  ppu->wideExtra = want;   // always render the extra columns; the front-end blacks them out when not active
  ppu->wideClipL = g_lo - cam;
  ppu->wideClipR = g_hi - cam;
  if (ppu->wideClipL > 0) ppu->wideClipL = 0;
  if (ppu->wideClipR < 256) ppu->wideClipR = 256;
  g_active = want > 0 && play ? want : 0;
}
int ws_active_extra(void) { return g_active; }

// after a save state, rewind step or restart: the history is unknown, trust the whole tilemap ring
// around the camera until the next jump
void ws_state_loaded(void) {
  int cam = s->ppu->bgLayer[0].hScroll;
  g_lo = cam - 128; g_hi = g_lo + 512;
  g_prev_cam = cam; g_prev_play = game_in_play(s->ram);
}
