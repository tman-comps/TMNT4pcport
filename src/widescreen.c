// Widescreen. Turtles in Time keeps its background layers in 64-tile-wide tilemaps and streams new
// columns in well ahead of the screen edge, so the columns either side of the 256-pixel view are
// already in VRAM: the PPU just renders more of each line (ppu->wideExtra, LakeSnes port addition).
// Only used while a scene is being played; title, cutscenes and cards stay 4:3 (pillarboxed).
#include "widescreen.h"
#include "game.h"

static Snes* s;
static int g_active;

void ws_init(Snes* snes) { s = snes; }

void ws_update(int want) {
  int e = 0;
  if (want > 0 && game_in_play(s->ram)) e = want;
  s->ppu->wideExtra = want;      // always render the extra columns (cheap); black them out when not valid
  g_active = e;
}
int ws_active_extra(void) { return g_active; }
