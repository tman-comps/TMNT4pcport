// See ui.h. Text uses the public-domain font8x8 (third_party/font8x8_basic.h) with a dark outline,
// so it reads on any background.
#include <string.h>
#include <ctype.h>
#include "ui.h"
#include "font8x8_basic.h"

static uint32_t g_px[UI_W * UI_H];

void ui_clear(void) { memset(g_px, 0, sizeof g_px); }
const uint32_t* ui_pixels(void) { return g_px; }

// blend an ARGB colour (alpha 0..255) over the canvas pixel (straight alpha)
static void put(int x, int y, uint32_t rgb, int a) {
  if (x < 0 || y < 0 || x >= UI_W || y >= UI_H || a <= 0) return;
  uint32_t d = g_px[y * UI_W + x];
  int da = d >> 24;
  if (a >= 255 || da == 0) { g_px[y * UI_W + x] = (uint32_t)(a > 255 ? 255 : a) << 24 | (rgb & 0xFFFFFF); return; }
  int oa = a + da * (255 - a) / 255;
  int r = (((rgb >> 16) & 255) * a + ((d >> 16) & 255) * da * (255 - a) / 255) / oa;
  int g = (((rgb >> 8) & 255) * a + ((d >> 8) & 255) * da * (255 - a) / 255) / oa;
  int b = ((rgb & 255) * a + (d & 255) * da * (255 - a) / 255) / oa;
  g_px[y * UI_W + x] = (uint32_t)oa << 24 | r << 16 | g << 8 | b;
}
static void fill(int x, int y, int w, int h, uint32_t rgb, int a) {
  for (int j = y; j < y + h; j++) for (int i = x; i < x + w; i++) put(i, j, rgb, a);
}

void ui_dim(void) { fill(0, 0, UI_W, UI_H, 0x000008, 140); }

static void frame(int x, int y, int w, int h, int a) {
  fill(x, y, w, h, 0x080818, a * 225 / 256);
  // light outer frame, darker inner line, like the game's HUD panels
  fill(x, y, w, 1, 0xE8E8F8, a); fill(x, y + h - 1, w, 1, 0xE8E8F8, a);
  fill(x, y, 1, h, 0xE8E8F8, a); fill(x + w - 1, y, 1, h, 0xE8E8F8, a);
  fill(x + 1, y + 1, w - 2, 1, 0xF8C820, a); fill(x + 1, y + h - 2, w - 2, 1, 0xF8C820, a);
  fill(x + 1, y + 1, 1, h - 2, 0xF8C820, a); fill(x + w - 2, y + 1, 1, h - 2, 0xF8C820, a);
}
void ui_box(int x, int y, int w, int h) { frame(x, y, w, h, 255); }
void ui_toast_box(int x, int y, int w, int h, int alpha) { frame(x, y, w, h, alpha > 255 ? 255 : alpha); }

static int glyph(int c, int row) {
  if (c < 0 || c > 127) c = '?';
  return (unsigned char)font8x8_basic[c][row];
}
void ui_text_a(int x, int y, const char* s, uint32_t rgb, int alpha) {
  int a = alpha > 255 ? 255 : alpha;
  for (int pass = 0; pass < 2; pass++) {
    int cx = x;
    for (const char* p = s; *p; p++, cx += 8) {
      int c = toupper((unsigned char)*p);
      for (int r = 0; r < 8; r++) {
        int bits = glyph(c, r);
        for (int k = 0; k < 8; k++) {
          if (!(bits & (1 << k))) continue;
          if (pass == 0) {   // outline
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) put(cx + k + dx, y + r + dy, 0x000000, a * 3 / 4);
          } else {
            // slightly darker lower half, like the game's two-tone font
            uint32_t col = r < 4 ? rgb : ((((rgb >> 16) & 255) * 7 / 8) << 16) | ((((rgb >> 8) & 255) * 7 / 8) << 8) | ((rgb & 255) * 7 / 8);
            put(cx + k, y + r, col, a);
          }
        }
      }
    }
  }
}
void ui_text(int x, int y, const char* s, uint32_t rgb) { ui_text_a(x, y, s, rgb, 255); }
int ui_text_w(const char* s) { return (int)strlen(s) * 8; }
void ui_text_r(int xr, int y, const char* s, uint32_t rgb) { ui_text(xr - ui_text_w(s), y, s, rgb); }
void ui_heading(int y, const char* s) {
  int w = ui_text_w(s), x = (UI_W - w) / 2;
  ui_text(x, y, s, UI_GOLD);
  fill(x, y + 10, w, 1, UI_GOLD, 255);
}
void ui_wrap(int x, int y, int w, const char* s, uint32_t rgb) {
  char line[64]; int n = 0, maxc = w / 8;
  const char* p = s;
  while (*p) {
    const char* word = p; int wl = 0;
    while (p[wl] && p[wl] != ' ') wl++;
    if (n && n + 1 + wl > maxc) { line[n] = 0; ui_text(x, y, line, rgb); y += 10; n = 0; }
    if (n) line[n++] = ' ';
    for (int i = 0; i < wl && n < 63; i++) line[n++] = word[i];
    p += wl; while (*p == ' ') p++;
  }
  if (n) { line[n] = 0; ui_text(x, y, line, rgb); }
}
void ui_cursor(int x, int y, int tick) {
  // a small green shell, bobbing
  int o = (tick / 8) & 1;
  static const char* shell[7] = {"..###..", ".#####.", "##.#.##", "#######", "##.#.##", ".#####.", "..#.#.."};
  for (int r = 0; r < 7; r++) for (int c = 0; c < 7; c++)
    if (shell[r][c] == '#') put(x + c + o, y + r, r == 0 || r == 6 ? 0x309020 : UI_GREEN, 255);
    else if (shell[r][c] == '.' && r > 0 && r < 6 && c > 0 && c < 6) put(x + c + o, y + r, 0x186010, 255);
}
