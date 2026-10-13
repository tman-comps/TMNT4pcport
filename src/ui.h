// Port UI drawn over the game picture: a 256x224 ARGB canvas (straight alpha) the front-end scales
// together with the picture. Menu boxes, the pop-ups and the timer use the look of the game's own
// HUD: dark boxes with a light frame, yellow headings, green highlight.
#ifndef UI_H
#define UI_H
#include <stdint.h>

#define UI_W 256
#define UI_H 224
enum {
  UI_WHITE = 0xF8F8F8, UI_GOLD = 0xF8D830, UI_GREEN = 0x60F048, UI_GREY = 0x8890A0, UI_RED = 0xF85040,
  UI_BLUE = 0x58A8F8,
};
void ui_clear(void);
const uint32_t* ui_pixels(void);
void ui_dim(void);                                  // darken the whole picture (menus)
void ui_box(int x, int y, int w, int h);            // menu box
void ui_toast_box(int x, int y, int w, int h, int alpha);
void ui_heading(int y, const char* s);              // centred, gold, underlined
void ui_text(int x, int y, const char* s, uint32_t rgb);
void ui_text_a(int x, int y, const char* s, uint32_t rgb, int alpha);
void ui_text_r(int xr, int y, const char* s, uint32_t rgb);   // right-aligned
int ui_text_w(const char* s);
void ui_wrap(int x, int y, int w, const char* s, uint32_t rgb);   // word-wrapped, 10 px lines
void ui_cursor(int x, int y, int tick);             // the selection marker
// The game's own HUD font, captured from VRAM (BG3 tiles $100-$12B: space 0-9 (c) , . " ! A-Z ' -)
// the first time it is there, and cached in tmnt_font.bin next to the program. Until then (and for
// characters the font doesn't have) the built-in 8x8 font is used.
void ui_font_load(const char* path);
void ui_font_capture(const uint16_t* vram, int bg3_tile_word_adr, const char* save_path);   // cheap; call now and then
int ui_font_ready(void);
#endif
