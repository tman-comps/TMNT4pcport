// Teenage Mutant Ninja Turtles IV - Turtles in Time: PC port, SDL2 front-end.
//
// Usage: tmnt [path/to/Teenage Mutant Ninja Turtles IV - Turtles in Time (USA).sfc]
// With no argument it looks for tmnt4.sfc (or any .sfc that is the right dump) next to the program.
//
// Keyboard (player 1): arrows = d-pad, A = Y (attack), Z = B (jump), X = A, S = X, Q/W = L/R,
//           Enter = Start, Right Shift = Select.  Player 2 has its own (rebindable) keys.
//           Esc = menu, F5/F7 = save/load state, F8 = restart scene, F11/Alt+Enter = fullscreen,
//           Tab (hold) = fast forward, Backspace (hold) = rewind, F12 = recompiled/interpreted CPU
// Gamepads: the first controller is player 1, the second player 2 (swappable in Controls).
//           Guide or Back+Start = menu, left trigger (hold) = rewind.
// Settings are saved to tmnt.ini next to the program.
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <strings.h>
#endif
#include "snes.h"
#include "ppu.h"
#include "runner.h"
#include "recomp_rt.h"
#include "rom_check.h"
#include "game.h"
#include "ui.h"
#include "achievements.h"
#include "extras.h"
#include "msu.h"
#include "steam.h"
#include "crashlog.h"
#include "widescreen.h"

#define SAMPLE_RATE 48000

static Snes* g_snes;
static uint8_t* g_rom;
static int g_rom_len;
static uint8_t g_pixels[512 * 480 * 4];
static int16_t g_audio[SAMPLE_RATE / 50 * 2];
static SDL_Window* g_win;
static char g_base[1024], g_ini_path[1100], g_state_path[1100], g_cont_path[1100];
static bool g_quit;
static char g_font_path[1100];

// ---------------------------------------------------------------- settings
enum { ASPECT_4_3, ASPECT_8_7, ASPECT_STRETCH, ASPECT_COUNT };
enum { WIDE_OFF, WIDE_16_10, WIDE_16_9, WIDE_21_9, WIDE_COUNT };
static struct {
  int aspect, integer_scale, fullscreen, volume, smooth;
  int wide, crt, color, nolimit, interp;
  int no_slowdown, timer, rewind, rumble, pause_menu;
  int msu, music_vol;
  int unlocked;                       // extras unlocked (game beaten, or the game's own code entered)
  int x_stage, x_lives, x_health;     // extras: start scene (0 = off, 1-10), infinite lives, infinite health
  int pad_swap;                       // second controller is player 1
  int m7;                             // Mode 7 detail: 0 original, 1 = 2x, 2 = 4x (intro, title, time warps)
} cfg = {ASPECT_4_3, 0, 0, 80, 0,  WIDE_OFF, 0, 0, 0, 0,  0, 0, 1, 1, 1,  0, 100,  0, 0, 0, 0, 0, 0};

// Remappable bindings, per player: one key and one gamepad button for each SNES button (LakeSnes
// order: B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R).
static const char* k_btn_names[12] = {"B  JUMP", "Y  ATTACK", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT",
                                      "A  SPECIAL", "X", "L", "R"};
static const SDL_Keycode k_key_default[2][12] = {
  {SDLK_z, SDLK_a, SDLK_RSHIFT, SDLK_RETURN, SDLK_UP, SDLK_DOWN, SDLK_LEFT, SDLK_RIGHT, SDLK_x, SDLK_s, SDLK_q, SDLK_w},
  {SDLK_n, SDLK_b, SDLK_v, SDLK_m, SDLK_i, SDLK_k, SDLK_j, SDLK_l, SDLK_h, SDLK_g, SDLK_t, SDLK_y}};
static const int k_pad_default[12] = {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_BACK,
  SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
  SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, SDL_CONTROLLER_BUTTON_B,
  SDL_CONTROLLER_BUTTON_Y, SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER};
static SDL_Keycode g_keymap[2][12];
static int g_padmap[2][12];
static void bindings_default(int p) {
  memcpy(g_keymap[p], k_key_default[p], sizeof g_keymap[p]);
  memcpy(g_padmap[p], k_pad_default, sizeof g_padmap[p]);
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void cfg_load(void) {
  bindings_default(0); bindings_default(1);
  FILE* f = fopen(g_ini_path, "r");
  if (!f) return;
  char k[64]; int v;
  while (fscanf(f, " %63[^=]=%d", k, &v) == 2) {
    if (!strcmp(k, "aspect")) cfg.aspect = clampi(v, 0, ASPECT_COUNT - 1);
    else if (!strcmp(k, "integer_scale")) cfg.integer_scale = !!v;
    else if (!strcmp(k, "fullscreen")) cfg.fullscreen = !!v;
    else if (!strcmp(k, "volume")) cfg.volume = clampi(v, 0, 100);
    else if (!strcmp(k, "smooth")) cfg.smooth = !!v;
    else if (!strcmp(k, "widescreen")) cfg.wide = clampi(v, 0, WIDE_COUNT - 1);
    else if (!strcmp(k, "crt")) cfg.crt = clampi(v, 0, 2);
    else if (!strcmp(k, "colour")) cfg.color = clampi(v, 0, 2);
    else if (!strcmp(k, "no_sprite_flicker")) cfg.nolimit = !!v;
    else if (!strcmp(k, "sound_interpolation")) cfg.interp = !!v;
    else if (!strcmp(k, "remove_slowdown")) cfg.no_slowdown = !!v;
    else if (!strcmp(k, "speedrun_timer")) cfg.timer = !!v;
    else if (!strcmp(k, "rewind")) cfg.rewind = !!v;
    else if (!strcmp(k, "rumble")) cfg.rumble = !!v;
    else if (!strcmp(k, "pause_menu")) cfg.pause_menu = !!v;
    else if (!strcmp(k, "orchestral_music")) cfg.msu = !!v;
    else if (!strcmp(k, "music_volume")) cfg.music_vol = clampi(v, 0, 100);
    else if (!strcmp(k, "extras_unlocked")) cfg.unlocked = !!v;
    else if (!strcmp(k, "extra_start_scene")) cfg.x_stage = clampi(v, 0, 10);
    else if (!strcmp(k, "extra_infinite_lives")) cfg.x_lives = !!v;
    else if (!strcmp(k, "extra_infinite_health")) cfg.x_health = !!v;
    else if (!strcmp(k, "pad_swap")) cfg.pad_swap = !!v;
    else if (!strcmp(k, "mode7_detail")) cfg.m7 = clampi(v, 0, 2);
    else if (!strncmp(k, "key_", 4) && atoi(k + 4) >= 0 && atoi(k + 4) < 12) g_keymap[0][atoi(k + 4)] = v;
    else if (!strncmp(k, "pad_", 4) && k[4] >= '0' && k[4] <= '9' && atoi(k + 4) < 12) g_padmap[0][atoi(k + 4)] = v;
    else if (!strncmp(k, "p2key_", 6) && atoi(k + 6) >= 0 && atoi(k + 6) < 12) g_keymap[1][atoi(k + 6)] = v;
    else if (!strncmp(k, "p2pad_", 6) && atoi(k + 6) >= 0 && atoi(k + 6) < 12) g_padmap[1][atoi(k + 6)] = v;
  }
  fclose(f);
}
static void cfg_save(void) {
  FILE* f = fopen(g_ini_path, "w");
  if (!f) return;
  fprintf(f, "aspect=%d\ninteger_scale=%d\nfullscreen=%d\nvolume=%d\nsmooth=%d\nwidescreen=%d\ncrt=%d\ncolour=%d\n",
          cfg.aspect, cfg.integer_scale, cfg.fullscreen, cfg.volume, cfg.smooth, cfg.wide, cfg.crt, cfg.color);
  fprintf(f, "no_sprite_flicker=%d\nsound_interpolation=%d\nremove_slowdown=%d\nspeedrun_timer=%d\nrewind=%d\nrumble=%d\npause_menu=%d\n",
          cfg.nolimit, cfg.interp, cfg.no_slowdown, cfg.timer, cfg.rewind, cfg.rumble, cfg.pause_menu);
  fprintf(f, "orchestral_music=%d\nmusic_volume=%d\nextras_unlocked=%d\nextra_start_scene=%d\nextra_infinite_lives=%d\nextra_infinite_health=%d\npad_swap=%d\n",
          cfg.msu, cfg.music_vol, cfg.unlocked, cfg.x_stage, cfg.x_lives, cfg.x_health, cfg.pad_swap);
  fprintf(f, "mode7_detail=%d\n", cfg.m7);
  for (int i = 0; i < 12; i++) fprintf(f, "key_%d=%d\npad_%d=%d\n", i, (int)g_keymap[0][i], i, g_padmap[0][i]);
  for (int i = 0; i < 12; i++) fprintf(f, "p2key_%d=%d\np2pad_%d=%d\n", i, (int)g_keymap[1][i], i, g_padmap[1][i]);
  fclose(f);
}

// ---------------------------------------------------------------- input
// Button state per player with press latching: a press and release inside the same frame (a very
// quick tap) still reaches the game as a one-frame press.
static uint16_t g_btn_down[2], g_btn_seen[2], g_btn_release[2];
static uint16_t g_btn_inject;   // pressed for the game by the port (player 1)
static void set_btn(int p, int b, bool down) {
  uint16_t m = 1 << b;
  if (down) { g_btn_down[p] |= m; g_btn_seen[p] &= ~m; g_btn_release[p] &= ~m; }
  else if (g_btn_down[p] & m) {
    if (g_btn_seen[p] & m) g_btn_down[p] &= ~m;
    else g_btn_release[p] |= m;            // not seen by the game yet: release after next frame
  }
}
static void push_buttons(void) {
  for (int p = 0; p < 2; p++) {
    uint16_t b = g_btn_down[p] | (p == 0 ? g_btn_inject : 0);
    for (int i = 0; i < 12; i++) snes_setButtonState(g_snes, p + 1, i, (b >> i) & 1);
    g_btn_seen[p] = g_btn_down[p];
  }
}
static void after_frame_buttons(void) {
  for (int p = 0; p < 2; p++) { g_btn_down[p] &= ~g_btn_release[p]; g_btn_release[p] = 0; }
}
static void release_all(void) { memset(g_btn_down, 0, sizeof g_btn_down); memset(g_btn_release, 0, sizeof g_btn_release); }
static int key_to_btn(SDL_Keycode k, int* player) {
  for (int p = 0; p < 2; p++) for (int i = 0; i < 12; i++) if (g_keymap[p][i] == k) { *player = p; return i; }
  return -1;
}
// controllers in the order they were connected; the first one is player 1 (or 2 with pad_swap)
static SDL_JoystickID g_pads[8];
static int g_npads;
static void pad_added(int device) {
  SDL_GameController* gc = SDL_GameControllerOpen(device);
  if (!gc) return;
  SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gc));
  for (int i = 0; i < g_npads; i++) if (g_pads[i] == id) return;
  if (g_npads < 8) g_pads[g_npads++] = id;
}
static void pad_removed(SDL_JoystickID id) {
  for (int i = 0; i < g_npads; i++) if (g_pads[i] == id) { memmove(g_pads + i, g_pads + i + 1, (g_npads - i - 1) * sizeof g_pads[0]); g_npads--; break; }
}
static int pad_player(SDL_JoystickID id) {
  for (int i = 0; i < g_npads; i++) if (g_pads[i] == id) return i > 1 ? 1 : (i ^ cfg.pad_swap);
  return 0;
}
static SDL_GameController* player_pad(int p) {
  for (int i = 0; i < g_npads && i < 2; i++) if ((i ^ cfg.pad_swap) == p) return SDL_GameControllerFromInstanceID(g_pads[i]);
  return NULL;
}
static int pad_to_btn(int p, int b) {
  for (int i = 0; i < 12; i++) if (g_padmap[p][i] == b) return i;
  return -1;
}

// ---------------------------------------------------------------- video helpers
static void apply_fullscreen(void) { SDL_SetWindowFullscreen(g_win, cfg.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
static double pixel_aspect(void) { return cfg.aspect == ASPECT_8_7 ? 1.0 : (4.0 / 3.0) / (256.0 / 224.0); }
static int wide_extra(void) {   // extra columns each side for the chosen widescreen ratio
  static const double r[WIDE_COUNT] = {0, 16.0 / 10, 16.0 / 9, 21.0 / 9};
  if (!cfg.wide) return 0;
  int e = (int)((r[cfg.wide] * 224 / pixel_aspect() - 256) / 2);
  return clampi(e, 0, PPU_WIDE_MAX);
}

// Colour styles: a lookup on the top 5 bits of each channel (the SNES only has 5 bits per channel).
static uint32_t g_color_lut[32768];
static int g_color_lut_mode = -1;
static void color_apply(uint32_t* buf, int stride, int w, int h) {
  if (!cfg.color) return;
  if (g_color_lut_mode != cfg.color) {
    double sat = cfg.color == 1 ? 1.30 : 0.85, gam = cfg.color == 1 ? 0.95 : 1.0, lift = cfg.color == 2 ? 0.04 : 0.0;
    for (int i = 0; i < 32768; i++) {
      double r = ((i >> 10) & 31) / 31.0, g = ((i >> 5) & 31) / 31.0, b = (i & 31) / 31.0;
      double y = 0.299 * r + 0.587 * g + 0.114 * b;
      r = y + (r - y) * sat; g = y + (g - y) * sat; b = y + (b - y) * sat;
      if (cfg.color == 2) { r = r * 1.02 + lift; g = g + lift; b = b * 0.96 + lift; }
      r = r < 0 ? 0 : r > 1 ? 1 : pow(r, gam); g = g < 0 ? 0 : g > 1 ? 1 : pow(g, gam); b = b < 0 ? 0 : b > 1 ? 1 : pow(b, gam);
      g_color_lut[i] = ((int)(r * 255 + 0.5) << 16) | ((int)(g * 255 + 0.5) << 8) | (int)(b * 255 + 0.5);
    }
    g_color_lut_mode = cfg.color;
  }
  for (int y = 0; y < h; y++) {
    uint32_t* p = buf + (size_t)y * stride;
    for (int x = 0; x < w; x++) { uint32_t c = p[x]; p[x] = g_color_lut[((c >> 9) & 0x7c00) | ((c >> 6) & 0x3e0) | ((c >> 3) & 0x1f)]; }
  }
}

// CRT filter: a multiply-blended overlay over the picture: scanlines on the 224 game lines, and for
// "Full CRT" also an aperture-grille mask and a soft vignette. Rebuilt when the size or mode changes.
static SDL_Texture* g_crt_tex;
static int g_crt_w, g_crt_h, g_crt_mode;
static void crt_overlay(SDL_Renderer* ren, SDL_Rect dst) {
  if (!cfg.crt || dst.w <= 0 || dst.h <= 0) return;
  if (!g_crt_tex || g_crt_w != dst.w || g_crt_h != dst.h || g_crt_mode != cfg.crt) {
    if (g_crt_tex) SDL_DestroyTexture(g_crt_tex);
    g_crt_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, dst.w, dst.h);
    if (!g_crt_tex) return;
    uint32_t* px = malloc((size_t)dst.w * dst.h * 4);
    double strength = cfg.crt == 2 ? 0.45 : 0.35;
    for (int y = 0; y < dst.h; y++) {
      double ph = (y + 0.5) * 224.0 / dst.h; ph -= (int)ph;
      double d = ph - 0.5, scan = 1.0 - strength * (4.0 * d * d);
      double vy = (y + 0.5) / dst.h - 0.5;
      for (int x = 0; x < dst.w; x++) {
        double r = scan, g = scan, b = scan;
        if (cfg.crt == 2) {
          int k = x % 3;
          r *= k == 0 ? 1.0 : 0.82; g *= k == 1 ? 1.0 : 0.82; b *= k == 2 ? 1.0 : 0.82;
          double vx = (x + 0.5) / dst.w - 0.5, v = 1.0 - 0.55 * (vx * vx + vy * vy) * 1.6;
          r *= v; g *= v; b *= v;
        }
        px[y * dst.w + x] = 0xff000000u | ((int)(r * 255) << 16) | ((int)(g * 255) << 8) | (int)(b * 255);
      }
    }
    SDL_UpdateTexture(g_crt_tex, NULL, px, dst.w * 4);
    free(px);
    SDL_SetTextureBlendMode(g_crt_tex, SDL_BLENDMODE_MOD);
    g_crt_w = dst.w; g_crt_h = dst.h; g_crt_mode = cfg.crt;
  }
  SDL_RenderCopy(ren, g_crt_tex, NULL, &dst);
}

// ---------------------------------------------------------------- save states, restart, rewind
static uint8_t* g_state;   // scratch, 4 MB
static void state_save_to(const char* path) {
  int n = snes_saveState(g_snes, g_state);
  FILE* f = fopen(path, "wb");
  if (f) { fwrite(g_state, 1, n, f); fclose(f); }
}
static bool state_load_from(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  int n = (int)fread(g_state, 1, 4 * 1024 * 1024, f);
  fclose(f);
  bool ok = n > 0 && snes_loadState(g_snes, g_state, n);
  if (ok) ws_state_loaded();
  return ok;
}
static void ui_toast_text(const char* t, const char* d);
static bool g_assisted;    // save states / rewind / extras used: no achievements or records this session
static void save_state_file(void) { state_save_to(g_state_path); ui_toast_text("STATE SAVED", NULL); crashlog_event("state saved"); }
static void load_state_file(void) {
  if (state_load_from(g_state_path)) { g_assisted = true; release_all(); ui_toast_text("STATE LOADED", NULL); crashlog_event("state loaded"); }
  else ui_toast_text("NO SAVED STATE", NULL);
}
// restart scene: a snapshot taken when each scene's play starts (also written to tmnt_continue.state)
static uint8_t* g_cont;
static int g_cont_len, g_cont_stage = -1;
static void restart_tick(void) {
  int st = game_stage(g_snes->ram);
  if (game_scene_started(g_snes->ram)) {
    g_cont_len = snes_saveState(g_snes, g_cont);
    g_cont_stage = st;
    FILE* f = fopen(g_cont_path, "wb");
    if (f) { fwrite(g_cont, 1, g_cont_len, f); fclose(f); }
    crashlog_event("scene %d start snapshot", st + 1);
  }
}
static bool restart_available(void) { return g_cont_len > 0 && game_in_play(g_snes->ram); }
static void restart_scene(void) {
  if (!g_cont_len) return;
  snes_loadState(g_snes, g_cont, g_cont_len);
  ws_state_loaded();
  release_all();
  crashlog_event("restart scene");
}
static bool continue_available(void) { FILE* f = fopen(g_cont_path, "rb"); if (f) fclose(f); return f != NULL; }
static void continue_last(void) {
  if (state_load_from(g_cont_path)) { g_assisted = true; release_all(); crashlog_event("continue from last scene"); }
}

// Rewind: a ring of save states, one every 4 frames (about 10 seconds). While held, each shown frame
// steps back one snapshot and runs one frame to redraw.
#define RW_SLOTS 150
#define RW_EVERY 4
static uint8_t* g_rw_buf[RW_SLOTS];
static int g_rw_len[RW_SLOTS], g_rw_head, g_rw_count, g_rw_tick;
static bool g_rewinding;
static void rewind_record(void) {
  if (++g_rw_tick % RW_EVERY) return;
  int n = snes_saveState(g_snes, g_state);
  if (!g_rw_buf[g_rw_head] || g_rw_len[g_rw_head] < n) { free(g_rw_buf[g_rw_head]); g_rw_buf[g_rw_head] = malloc(n); }
  memcpy(g_rw_buf[g_rw_head], g_state, n); g_rw_len[g_rw_head] = n;
  g_rw_head = (g_rw_head + 1) % RW_SLOTS;
  if (g_rw_count < RW_SLOTS) g_rw_count++;
}
static bool rewind_step(void) {
  if (g_rw_count <= 1) return false;
  g_rw_head = (g_rw_head + RW_SLOTS - 1) % RW_SLOTS; g_rw_count--;
  int i = (g_rw_head + RW_SLOTS - 1) % RW_SLOTS;
  snes_loadState(g_snes, g_rw_buf[i], g_rw_len[i]);
  ws_state_loaded();
  if (!g_assisted) crashlog_event("rewind used");
  g_assisted = true;
  return true;
}

// ---------------------------------------------------------------- rumble
static void rumble(int p, uint16_t lo, uint16_t hi, uint32_t ms) {
  SDL_GameController* gc = player_pad(p);
  if (cfg.rumble && gc) SDL_GameControllerRumble(gc, lo, hi, ms);
}
static void rumble_tick(void) {
  static int last_hp[2] = {-1, -1}, last_lives[2] = {-1, -1};
  for (int p = 0; p < 2; p++) {
    bool play = game_in_play(g_snes->ram) && game_player_active(g_snes->ram, p);
    int hp = game_hp(g_snes->ram, p), lives = game_lives(g_snes->ram, p);
    if (play && last_lives[p] >= 0) {
      if (lives < last_lives[p]) rumble(p, 0xC000, 0xFFFF, 450);
      else if (last_hp[p] > 0 && hp < last_hp[p]) rumble(p, 0x5000, 0x9000, 150);
    }
    last_hp[p] = play ? hp : -1; last_lives[p] = play ? lives : -1;
  }
}

// ---------------------------------------------------------------- extras applied to the game
static void extras_apply(void) {
  uint8_t* ram = g_snes->ram;
  if (!cfg.unlocked) return;
  bool any = false;
  // start scene: only on the select screen of a new game (scene 1 chosen by the game; Versus uses scene index 10)
  if (cfg.x_stage > 1 && game_at_turtle_select(ram) && (game_stage(ram) == 0 || game_stage(ram) == cfg.x_stage - 1)) {
    game_set_start_stage(ram, cfg.x_stage - 1); any = true;
  }
  if (cfg.x_lives && game_in_play(ram)) for (int p = 0; p < 2; p++) if (game_player_active(ram, p)) { game_refill_lives(ram, p); any = true; }
  if (cfg.x_health && game_in_play(ram)) for (int p = 0; p < 2; p++) if (game_player_active(ram, p)) { game_refill_hp(ram, p); any = true; }
  if (any && !g_assisted) { g_assisted = true; crashlog_event("extras in use"); }
}

// ---------------------------------------------------------------- menu
enum {
  M_RESUME, M_RESTART, M_CONTINUE, M_OPTIONS, M_ACH, M_STATS, M_SAVE, M_LOAD, M_RESET, M_QUIT,
  M_PG_VIDEO, M_PG_AUDIO, M_PG_GAME, M_PG_P1, M_PG_P2, M_PG_EXTRAS, M_REPORT,
  M_WIDE, M_M7, M_ASPECT, M_INTEGER, M_SMOOTH, M_CRT, M_COLOR, M_NOLIMIT, M_FULL,
  M_VOLUME, M_MUSIC, M_MUSICVOL, M_INTERP,
  M_SLOW, M_TIMER, M_REWIND, M_RUMBLE, M_PAUSEBTN,
  M_X_STAGE, M_X_LIVES, M_X_HEALTH,
  M_SWAP, M_DEFAULTS, M_BIND0 /* .. M_BIND0 + 11 */, M_COUNT = M_BIND0 + 12
};
enum { PG_MAIN, PG_OPTIONS, PG_VIDEO, PG_AUDIO, PG_GAME, PG_EXTRAS, PG_P1, PG_P2, PG_ACH, PG_STATS, NPAGES };
static const char* k_page_titles[NPAGES] = {"PAUSE", "PORT SETTINGS", "VIDEO", "AUDIO", "GAMEPLAY", "EXTRAS",
                                            "PLAYER 1 CONTROLS", "PLAYER 2 CONTROLS", "ACHIEVEMENTS", "STATISTICS"};
static const int k_pg_main[] = {M_RESUME, M_RESTART, M_CONTINUE, M_OPTIONS, M_ACH, M_STATS, M_SAVE, M_LOAD, M_RESET, M_QUIT, -1};
static const int k_pg_options[] = {M_PG_VIDEO, M_PG_AUDIO, M_PG_GAME, M_PG_P1, M_PG_P2, M_PG_EXTRAS, M_REPORT, -1};
static const int k_pg_video[] = {M_WIDE, M_M7, M_ASPECT, M_INTEGER, M_SMOOTH, M_CRT, M_COLOR, M_NOLIMIT, M_FULL, -1};
static const int k_pg_audio[] = {M_VOLUME, M_MUSIC, M_MUSICVOL, M_INTERP, -1};
static const int k_pg_game[] = {M_SLOW, M_TIMER, M_REWIND, M_RUMBLE, M_PAUSEBTN, -1};
static const int k_pg_extras[] = {M_X_STAGE, M_X_LIVES, M_X_HEALTH, -1};
static int k_pg_ctrl[16];
static const int* k_pages[NPAGES] = {k_pg_main, k_pg_options, k_pg_video, k_pg_audio, k_pg_game, k_pg_extras, k_pg_ctrl, k_pg_ctrl, NULL, NULL};
static bool g_menu;
static int g_page, g_sel, g_scroll;
static int g_bind_wait;       // rebinding: 1 = waiting for a key or pad button for row g_sel
static int g_ui_tick;
static int g_msu_songs;
static int g_report_timer;
static const char* k_on_off[] = {"OFF", "ON"};

static bool item_visible(int id) {
  switch (id) {
    case M_RESTART: return restart_available();
    case M_CONTINUE: return !game_in_play(g_snes->ram) && continue_available();
    case M_MUSIC: case M_MUSICVOL: return g_msu_songs > 0;
    default: return true;
  }
}
static int page_items(int pg, int* out) {
  int n = 0;
  if (pg == PG_P1 || pg == PG_P2) {
    for (int i = 0; i < 12; i++) out[n++] = M_BIND0 + i;
    out[n++] = M_SWAP; out[n++] = M_DEFAULTS;
    return n;
  }
  for (const int* p = k_pages[pg]; p && *p >= 0; p++) if (item_visible(*p)) out[n++] = *p;
  return n;
}
static const char* key_label(SDL_Keycode k) {
  static char b[24];
  const char* n = SDL_GetKeyName(k);
  snprintf(b, sizeof b, "%s", n && *n ? n : "-");
  for (char* c = b; *c; c++) if (*c >= 'a' && *c <= 'z') *c -= 32;
  return b;
}
static const char* pad_label(int b) {
  static const char* n[] = {"A", "B", "X", "Y", "BACK", "GUIDE", "START", "LSTICK", "RSTICK", "LB", "RB", "UP", "DOWN", "LEFT", "RIGHT"};
  return b >= 0 && b < (int)(sizeof n / sizeof n[0]) ? n[b] : "-";
}
// label and value of a menu row
static void item_text(int id, char* label, char* value, int n) {
  static const char* aspect[] = {"4:3", "8:7", "STRETCH"};
  static const char* wide[] = {"OFF", "16:10", "16:9", "21:9"};
  static const char* crt[] = {"OFF", "SCANLINES", "FULL CRT"};
  static const char* color[] = {"ORIGINAL", "VIVID", "SOFT"};
  value[0] = 0;
  int p = g_page == PG_P2;
  switch (id) {
    case M_RESUME: snprintf(label, n, "RESUME"); break;
    case M_RESTART: snprintf(label, n, "RESTART SCENE"); break;
    case M_CONTINUE: snprintf(label, n, "CONTINUE FROM LAST SCENE"); break;
    case M_OPTIONS: snprintf(label, n, "PORT SETTINGS"); break;
    case M_ACH: snprintf(label, n, "ACHIEVEMENTS"); snprintf(value, n, "%d/%d", ach_unlocked_count(), ach_total()); break;
    case M_STATS: snprintf(label, n, "STATISTICS"); break;
    case M_SAVE: snprintf(label, n, "SAVE STATE"); snprintf(value, n, "F5"); break;
    case M_LOAD: snprintf(label, n, "LOAD STATE"); snprintf(value, n, "F7"); break;
    case M_RESET: snprintf(label, n, "RESET GAME"); break;
    case M_QUIT: snprintf(label, n, "QUIT"); break;
    case M_PG_VIDEO: snprintf(label, n, "VIDEO"); break;
    case M_PG_AUDIO: snprintf(label, n, "AUDIO"); break;
    case M_PG_GAME: snprintf(label, n, "GAMEPLAY"); break;
    case M_PG_P1: snprintf(label, n, "PLAYER 1 CONTROLS"); break;
    case M_PG_P2: snprintf(label, n, "PLAYER 2 CONTROLS"); break;
    case M_PG_EXTRAS: snprintf(label, n, "EXTRAS"); snprintf(value, n, "%s", cfg.unlocked ? "" : "LOCKED"); break;
    case M_REPORT: snprintf(label, n, "REPORT A PROBLEM"); if (g_report_timer) snprintf(value, n, "SAVED"); break;
    case M_WIDE: snprintf(label, n, "WIDESCREEN"); snprintf(value, n, "%s", wide[cfg.wide]); break;
    case M_M7: { static const char* m7[] = {"ORIGINAL", "HIGH 2X", "ULTRA 4X"}; snprintf(label, n, "MODE 7 DETAIL"); snprintf(value, n, "%s", m7[cfg.m7]); break; }
    case M_ASPECT: snprintf(label, n, "ASPECT"); snprintf(value, n, "%s", aspect[cfg.aspect]); break;
    case M_INTEGER: snprintf(label, n, "INTEGER SCALE"); snprintf(value, n, "%s", k_on_off[cfg.integer_scale]); break;
    case M_SMOOTH: snprintf(label, n, "SMOOTH PIXELS"); snprintf(value, n, "%s", k_on_off[cfg.smooth]); break;
    case M_CRT: snprintf(label, n, "CRT FILTER"); snprintf(value, n, "%s", crt[cfg.crt]); break;
    case M_COLOR: snprintf(label, n, "COLOUR"); snprintf(value, n, "%s", color[cfg.color]); break;
    case M_NOLIMIT: snprintf(label, n, "SPRITE FLICKER"); snprintf(value, n, "%s", cfg.nolimit ? "REMOVED" : "ORIGINAL"); break;
    case M_FULL: snprintf(label, n, "FULLSCREEN"); snprintf(value, n, "%s", k_on_off[cfg.fullscreen]); break;
    case M_VOLUME: snprintf(label, n, "VOLUME"); snprintf(value, n, "%d%%", cfg.volume); break;
    case M_MUSIC: snprintf(label, n, "MUSIC"); snprintf(value, n, "%s", cfg.msu ? "ORCHESTRAL" : "ORIGINAL"); break;
    case M_MUSICVOL: snprintf(label, n, "MUSIC VOLUME"); snprintf(value, n, "%d%%", cfg.music_vol); break;
    case M_INTERP: snprintf(label, n, "SOUND"); snprintf(value, n, "%s", cfg.interp ? "CRISP" : "ORIGINAL"); break;
    case M_SLOW: snprintf(label, n, "REMOVE SLOWDOWN"); snprintf(value, n, "%s", k_on_off[cfg.no_slowdown]); break;
    case M_TIMER: snprintf(label, n, "SPEEDRUN TIMER"); snprintf(value, n, "%s", k_on_off[cfg.timer]); break;
    case M_REWIND: snprintf(label, n, "REWIND"); snprintf(value, n, "%s", k_on_off[cfg.rewind]); break;
    case M_RUMBLE: snprintf(label, n, "RUMBLE"); snprintf(value, n, "%s", k_on_off[cfg.rumble]); break;
    case M_PAUSEBTN: snprintf(label, n, "START BUTTON"); snprintf(value, n, "%s", cfg.pause_menu ? "PAUSE MENU" : "GAME PAUSE"); break;
    case M_X_STAGE: snprintf(label, n, "START AT SCENE");
      if (cfg.x_stage) snprintf(value, n, "%d", cfg.x_stage); else snprintf(value, n, "OFF"); break;
    case M_X_LIVES: snprintf(label, n, "INFINITE LIVES"); snprintf(value, n, "%s", k_on_off[cfg.x_lives]); break;
    case M_X_HEALTH: snprintf(label, n, "PIZZA POWER"); snprintf(value, n, "%s", k_on_off[cfg.x_health]); break;
    case M_SWAP: snprintf(label, n, "SWAP CONTROLLERS"); snprintf(value, n, "%s", k_on_off[cfg.pad_swap]); break;
    case M_DEFAULTS: snprintf(label, n, "RESET TO DEFAULTS"); break;
    default:
      if (id >= M_BIND0 && id < M_BIND0 + 12) {
        int b = id - M_BIND0;
        snprintf(label, n, "%s", k_btn_names[b]);
        if (g_bind_wait && g_sel < 12 && b == g_sel) snprintf(value, n, "PRESS A KEY");
        else snprintf(value, n, "%s / %s", key_label(g_keymap[p][b]), pad_label(g_padmap[p][b]));
      }
  }
}

static int g_ach_sel;
static void open_menu(bool open) {
  if (open == g_menu) return;
  g_menu = open;
  g_page = PG_MAIN; g_sel = 0; g_scroll = 0; g_bind_wait = 0;
  release_all();
  if (!open) cfg_save();
}
static void go_page(int pg) { g_page = pg; g_sel = 0; g_scroll = 0; g_ach_sel = 0; }
static int parent_page(int pg) {
  switch (pg) {
    case PG_VIDEO: case PG_AUDIO: case PG_GAME: case PG_EXTRAS: case PG_P1: case PG_P2: return PG_OPTIONS;
    default: return PG_MAIN;
  }
}
static void menu_back(void) {
  if (g_bind_wait) { g_bind_wait = 0; return; }
  if (g_page == PG_MAIN) { open_menu(false); return; }
  int from = g_page;
  go_page(parent_page(g_page));
  int items[32], n = page_items(g_page, items);
  int want = from == PG_OPTIONS ? M_OPTIONS : from == PG_ACH ? M_ACH : from == PG_STATS ? M_STATS :
             from == PG_VIDEO ? M_PG_VIDEO : from == PG_AUDIO ? M_PG_AUDIO : from == PG_GAME ? M_PG_GAME :
             from == PG_P1 ? M_PG_P1 : from == PG_P2 ? M_PG_P2 : M_PG_EXTRAS;
  for (int i = 0; i < n; i++) if (items[i] == want) g_sel = i;
}
static void menu_change(int id, int dir, bool activate) {
  int d = dir ? dir : 1;
  switch (id) {
    case M_RESUME: if (activate) open_menu(false); break;
    case M_RESTART: if (activate) { restart_scene(); open_menu(false); } break;
    case M_CONTINUE: if (activate) { continue_last(); open_menu(false); } break;
    case M_OPTIONS: if (activate) go_page(PG_OPTIONS); break;
    case M_ACH: if (activate) go_page(PG_ACH); break;
    case M_STATS: if (activate) go_page(PG_STATS); break;
    case M_SAVE: if (activate) { save_state_file(); open_menu(false); } break;
    case M_LOAD: if (activate) { load_state_file(); open_menu(false); } break;
    case M_RESET: if (activate) { snes_reset(g_snes, false); crashlog_event("reset"); open_menu(false); } break;
    case M_QUIT: if (activate) g_quit = true; break;
    case M_PG_VIDEO: if (activate) go_page(PG_VIDEO); break;
    case M_PG_AUDIO: if (activate) go_page(PG_AUDIO); break;
    case M_PG_GAME: if (activate) go_page(PG_GAME); break;
    case M_PG_P1: if (activate) go_page(PG_P1); break;
    case M_PG_P2: if (activate) go_page(PG_P2); break;
    case M_PG_EXTRAS: if (activate && cfg.unlocked) go_page(PG_EXTRAS); break;   // the row shows LOCKED until then
    case M_REPORT: if (activate) { crashlog_report(); g_report_timer = 180; } break;
    case M_WIDE: cfg.wide = (cfg.wide + d + WIDE_COUNT) % WIDE_COUNT; break;
    case M_M7: cfg.m7 = (cfg.m7 + d + 3) % 3; break;
    case M_ASPECT: cfg.aspect = (cfg.aspect + d + ASPECT_COUNT) % ASPECT_COUNT; break;
    case M_INTEGER: cfg.integer_scale ^= 1; break;
    case M_SMOOTH: cfg.smooth ^= 1; break;
    case M_CRT: cfg.crt = (cfg.crt + d + 3) % 3; break;
    case M_COLOR: cfg.color = (cfg.color + d + 3) % 3; break;
    case M_NOLIMIT: cfg.nolimit ^= 1; break;
    case M_FULL: cfg.fullscreen ^= 1; apply_fullscreen(); break;
    case M_VOLUME: cfg.volume = clampi(cfg.volume + (dir ? dir * 10 : 10), 0, 100); if (!dir && cfg.volume == 100 && activate) cfg.volume = 0; break;
    case M_MUSIC: cfg.msu ^= 1; msu_enable(cfg.msu); break;
    case M_MUSICVOL: cfg.music_vol = clampi(cfg.music_vol + (dir ? dir * 10 : 10), 0, 100); break;
    case M_INTERP: cfg.interp ^= 1; break;
    case M_SLOW: cfg.no_slowdown ^= 1; break;
    case M_TIMER: cfg.timer ^= 1; break;
    case M_REWIND: cfg.rewind ^= 1; break;
    case M_RUMBLE: cfg.rumble ^= 1; if (cfg.rumble) rumble(0, 0x8000, 0x8000, 200); break;
    case M_PAUSEBTN: cfg.pause_menu ^= 1; break;
    case M_X_STAGE: cfg.x_stage = (cfg.x_stage + d + 11) % 11; break;
    case M_X_LIVES: cfg.x_lives ^= 1; break;
    case M_X_HEALTH: cfg.x_health ^= 1; break;
    case M_SWAP: cfg.pad_swap ^= 1; break;
    case M_DEFAULTS: if (activate) bindings_default(g_page == PG_P2); break;
    default:
      if (id >= M_BIND0 && id < M_BIND0 + 12 && activate) g_bind_wait = 1;
  }
  cfg_save();
}
static void menu_key(int dy, int dx, bool enter) {
  if (g_page == PG_ACH) { g_ach_sel = clampi(g_ach_sel + dy, 0, ach_total() - 1); return; }
  if (g_page == PG_STATS) return;
  int items[32], n = page_items(g_page, items);
  if (!n) return;
  if (g_sel >= n) g_sel = n - 1;
  if (dy) g_sel = (g_sel + dy + n) % n;
  if (dx) menu_change(items[g_sel], dx, false);
  if (enter) menu_change(items[g_sel], 0, true);
}
static void bind_key(SDL_Keycode k) {
  int p = g_page == PG_P2, b = g_sel;
  for (int q = 0; q < 2; q++) for (int i = 0; i < 12; i++) if (g_keymap[q][i] == k) g_keymap[q][i] = g_keymap[p][b];   // swap a duplicate
  g_keymap[p][b] = k;
  g_bind_wait = 0; cfg_save();
}
static void bind_pad(int bt) {
  int p = g_page == PG_P2, b = g_sel;
  for (int i = 0; i < 12; i++) if (g_padmap[p][i] == bt) g_padmap[p][i] = g_padmap[p][b];
  g_padmap[p][b] = bt;
  g_bind_wait = 0; cfg_save();
}

// ---------------------------------------------------------------- menu drawing (into the UI canvas)
static void draw_menu(void) {
  ui_dim();
  int bx = 16, bw = 224;
  if (g_page == PG_ACH) {
    ui_box(bx, 12, bw, 200);
    ui_heading(20, k_page_titles[g_page]);
    int n = ach_total(), rows = 9, top = clampi(g_ach_sel - rows / 2, 0, n > rows ? n - rows : 0);
    for (int r = 0; r < rows && top + r < n; r++) {
      const char *t, *d; int i = top + r;
      bool got = ach_get(i, &t, &d);
      int y = 40 + r * 14;
      ui_text(bx + 14, y, got ? "*" : "-", got ? UI_GOLD : UI_GREY);
      ui_text(bx + 26, y, t, i == g_ach_sel ? UI_GREEN : got ? UI_WHITE : UI_GREY);
    }
    const char *t, *d; ach_get(g_ach_sel, &t, &d);
    ui_wrap(bx + 12, 174, bw - 24, d, UI_WHITE);
    char c[32]; snprintf(c, sizeof c, "%d / %d", ach_unlocked_count(), ach_total());
    ui_text_r(bx + bw - 12, 160, c, UI_GOLD);
    return;
  }
  if (g_page == PG_STATS) {
    ui_box(bx, 12, bw, 200);
    ui_heading(20, k_page_titles[g_page]);
    char lines[24][48]; int n = extras_stat_lines(lines, 24);
    int y = 40, sc = 0;
    for (int i = 0; i < n; i++) {
      char* sep = strchr(lines[i], '|');
      if (!strncmp(lines[i], " SCENE ", 7) && sep) {   // best scene times: two columns
        *sep = 0;
        int col = sc % 2, x = bx + 12 + col * 108;
        char num[8]; snprintf(num, sizeof num, "%s", lines[i] + 7);
        ui_text(x, y, num, UI_WHITE); ui_text_r(x + 96, y, sep + 1, UI_GOLD);
        if (col) y += 12;
        sc++;
        continue;
      }
      if (sc % 2) { y += 12; sc = 0; }
      if (sep) { *sep = 0; ui_text(bx + 12, y, lines[i], UI_WHITE); ui_text_r(bx + bw - 12, y, sep + 1, UI_GOLD); }
      else ui_text(bx + 12, y, lines[i], UI_GREEN);
      y += 12;
    }
    return;
  }
  int items[32], n = page_items(g_page, items);
  if (g_sel >= n) g_sel = n - 1;
  int rows = 11;
  if (g_sel < g_scroll) g_scroll = g_sel;
  if (g_sel >= g_scroll + rows) g_scroll = g_sel - rows + 1;
  int shown = n < rows ? n : rows;
  int h = 44 + shown * 14;
  int y0 = (224 - h) / 2;
  ui_box(bx, y0, bw, h);
  ui_heading(y0 + 8, k_page_titles[g_page]);
  for (int r = 0; r < shown; r++) {
    int i = g_scroll + r;
    char label[48], value[48];
    item_text(items[i], label, value, sizeof label);
    int y = y0 + 30 + r * 14;
    bool sel = i == g_sel;
    if (sel) ui_cursor(bx + 8, y, g_ui_tick);
    ui_text(bx + 20, y, label, sel ? UI_GREEN : UI_WHITE);
    if (value[0]) ui_text_r(bx + bw - 10, y, value, sel && (g_ui_tick / 16) & 1 ? UI_WHITE : UI_GOLD);
  }
  if (n > rows) {
    if (g_scroll > 0) ui_text(bx + bw / 2 - 4, y0 + 22, "^", UI_GOLD);
    if (g_scroll + rows < n) ui_text(bx + bw / 2 - 4, y0 + h - 10, "v", UI_GOLD);
  }
}

// ---------------------------------------------------------------- pop-ups and timer
static char g_toast_t[48], g_toast_d[96];
static int g_toast_timer;
static void ui_toast_text(const char* t, const char* d) {
  snprintf(g_toast_t, sizeof g_toast_t, "%s", t);
  snprintf(g_toast_d, sizeof g_toast_d, "%s", d ? d : "");
  g_toast_timer = d ? 300 : 90;
}
static void toast_tick(void) {
  if (g_menu) return;   // pop-ups wait while the menu is open
  if (g_toast_timer > 0) { g_toast_timer--; return; }
  int id = ach_pop_toast();
  if (id >= 0) {
    const char *t, *d; ach_get(id, &t, &d);
    char head[64]; snprintf(head, sizeof head, "ACHIEVEMENT: %s", t);
    ui_toast_text(head, d);
    steam_unlock(id);
  }
}
static bool draw_overlays(void) {   // pop-up and timer; true if anything was drawn
  bool any = false;
  if (cfg.timer && !g_menu) {
    char run[32], st[32]; uint32_t rc, sc;
    if (extras_timer_text(run, sizeof run, st, sizeof st, &rc, &sc)) {
      ui_text_r(250, 204, run, rc);
      if (st[0]) ui_text_r(250, 214, st, sc);
      any = true;
    }
  }
  if (g_toast_timer > 0 && !g_menu) {
    int h = g_toast_d[0] ? 34 : 20, y = 224 - h - 26;
    int a = g_toast_timer < 16 ? g_toast_timer * 16 : 256;
    ui_toast_box(16, y, 224, h, a);
    ui_text_a(24, y + 6, g_toast_t, UI_GOLD, a);
    if (g_toast_d[0]) ui_text_a(24, y + 20, g_toast_d, UI_WHITE, a);
    any = true;
  }
  return any;
}

// ---------------------------------------------------------------- crash log settings line
static const char* settings_summary(void) {
  static char b[300];
  snprintf(b, sizeof b, "wide=%d aspect=%d integer=%d smooth=%d crt=%d colour=%d nolimit=%d interp=%d noslow=%d timer=%d rewind=%d msu=%d full=%d extras=%d/%d/%d/%d recomp=%d",
           cfg.wide, cfg.aspect, cfg.integer_scale, cfg.smooth, cfg.crt, cfg.color, cfg.nolimit, cfg.interp, cfg.no_slowdown,
           cfg.timer, cfg.rewind, cfg.msu, cfg.fullscreen, cfg.unlocked, cfg.x_stage, cfg.x_lives, cfg.x_health, g_use_recomp);
  return b;
}

// ---------------------------------------------------------------- automated test driver
// TMNT_AUTOTEST="w600 k:Return w60 shot:a.ppm q": wN wait frames, k:KEY tap a key, d:/u: key down/up, shot:FILE, q quit.
static const char* g_at;
static int g_at_wait;
static char g_at_shot[256];
static void at_key(SDL_Keycode k, bool down) {
  SDL_Event e = {0};
  e.type = down ? SDL_KEYDOWN : SDL_KEYUP; e.key.keysym.sym = k; e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  SDL_PushEvent(&e);
}
static void autotest_tick(void) {
  if (!g_at) return;
  if (g_at_wait > 0) { g_at_wait--; return; }
  while (*g_at == ' ') g_at++;
  if (!*g_at) { g_at = NULL; return; }
  char w[256]; int n = 0;
  while (g_at[n] && g_at[n] != ' ' && n < 255) { w[n] = g_at[n]; n++; }
  w[n] = 0; g_at += n;
  if (w[0] == 'w') g_at_wait = atoi(w + 1);
  else if (w[0] == 'q') g_quit = true;
  else if (!strncmp(w, "shot:", 5)) snprintf(g_at_shot, sizeof g_at_shot, "%s", w + 5);
  else if (w[1] == ':' && (w[0] == 'k' || w[0] == 'd' || w[0] == 'u')) {
    SDL_Keycode k = SDL_GetKeyFromName(w + 2);
    if (w[0] != 'u') at_key(k, true);
    if (w[0] != 'd') at_key(k, false);
    g_at_wait = 2;
  }
}
static void autotest_shot(const uint32_t* buf, int stride, int w, int h) {
  if (!g_at_shot[0]) return;
  FILE* f = fopen(g_at_shot, "wb");
  if (f) {
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { uint32_t c = buf[y * stride + x]; fputc(c >> 16, f); fputc(c >> 8, f); fputc(c, f); }
    fclose(f);
  }
  g_at_shot[0] = 0;
}

// ---------------------------------------------------------------- ROM loading
static bool find_rom(int argc, char** argv) {
  const char* tries[4]; int nt = 0;
  char p1[1200], p2[1200];
  if (argc > 1) tries[nt++] = argv[1];
  snprintf(p1, sizeof p1, "%stmnt4.sfc", g_base); tries[nt++] = p1;
  tries[nt++] = "tmnt4.sfc";
  snprintf(p2, sizeof p2, "%sroms/tmnt4.sfc", g_base); tries[nt++] = p2;
  RomCheckResult last = ROM_ERR_NOT_FOUND;
  for (int i = 0; i < nt; i++) {
    RomImage img;
    RomCheckResult r = rom_load_and_verify(tries[i], GAME_TMNT4, &img);
    if (r == ROM_OK) { g_rom = img.data; g_rom_len = (int)img.size; return true; }
    if (r != ROM_ERR_NOT_FOUND) last = r;
  }
  // any .sfc / .smc next to the program that is the right dump, whatever its name
  char* base = g_base[0] ? g_base : ".";
  SDL_RWops* dummy = NULL; (void)dummy;
#ifdef _WIN32
  char pat[1200]; snprintf(pat, sizeof pat, "%s*.*", g_base);
  WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(pat, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const char* e = strrchr(fd.cFileName, '.');
      if (!e || (_stricmp(e, ".sfc") && _stricmp(e, ".smc"))) continue;
      char p[1400]; snprintf(p, sizeof p, "%s%s", g_base, fd.cFileName);
      RomImage img;
      RomCheckResult r = rom_load_and_verify(p, GAME_TMNT4, &img);
      if (r == ROM_OK) { g_rom = img.data; g_rom_len = (int)img.size; FindClose(h); return true; }
      if (r != ROM_ERR_NOT_FOUND) last = r;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
#else
  DIR* d = opendir(base);
  if (d) {
    struct dirent* e;
    while ((e = readdir(d))) {
      const char* x = strrchr(e->d_name, '.');
      if (!x || (strcasecmp(x, ".sfc") && strcasecmp(x, ".smc"))) continue;
      char p[1400]; snprintf(p, sizeof p, "%s%s%s", g_base, g_base[0] ? "" : "./", e->d_name);
      RomImage img;
      RomCheckResult r = rom_load_and_verify(p, GAME_TMNT4, &img);
      if (r == ROM_OK) { g_rom = img.data; g_rom_len = (int)img.size; closedir(d); return true; }
      if (r != ROM_ERR_NOT_FOUND) last = r;
    }
    closedir(d);
  }
#endif
  char msg[1024];
  if (last == ROM_ERR_NOT_FOUND)
    snprintf(msg, sizeof msg, "Couldn't find your game ROM.\n\nPut your own dump of\n  %s\nnext to the program as tmnt4.sfc,\n"
             "or drag the ROM file onto the program.\n\nSHA-1 (without a copier header): 4f8fc0f45023e0339e501054b253c33f479a001a",
             "Teenage Mutant Ninja Turtles IV - Turtles in Time (USA)");
  else snprintf(msg, sizeof msg, "%s", rom_check_message(last, GAME_TMNT4));
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Turtles in Time", msg, NULL);
  return false;
}

// "PC PORT BY T-MAN" on the title screen, in the title's own font and palette: written into the free
// row between OPTION and the copyright lines (BG3 tilemap row 24), after the game's vblank uploads.
static void title_credit(void) {
  Ppu* ppu = g_snes->ppu;
  int mode = game_mode(g_snes->ram);
  if (mode < 2 || mode > 4) return;
  uint16_t* map = &ppu->vram[ppu->bgLayer[2].tilemapAdr & 0x7fff];
  // the title's "KONAMI" (row 25) must be there: K O N A M I = font tiles $11A $11E $11D $110 $11C $118
  static const uint16_t konami[6] = {0x11A, 0x11E, 0x11D, 0x110, 0x11C, 0x118};
  int col = -1;
  for (int c = 0; c + 6 <= 32 && col < 0; c++) {
    int ok = 1;
    for (int i = 0; i < 6 && ok; i++) ok = (map[25 * 32 + c + i] & 0x3ff) == konami[i];
    if (ok) col = c;
  }
  if (col < 0) return;
  uint16_t attr = map[25 * 32 + col] & 0xfc00;
  const char* t = "PC PORT BY T-MAN";
  int x0 = (32 - (int)strlen(t)) / 2;
  for (int i = 0; t[i]; i++) {
    char c = t[i];
    int g = c == ' ' ? 0 : c == '-' ? 0x2B : (c >= 'A' && c <= 'Z') ? 0x10 + c - 'A' : 0;
    map[24 * 32 + x0 + i] = attr | (0x100 + g);
  }
}
static void menus_after_vblank(void) { title_credit(); }

int main(int argc, char** argv) {
  SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  {
    char* base = SDL_GetBasePath();
    snprintf(g_base, sizeof g_base, "%s", base ? base : "");
    if (base) SDL_free(base);
  }
  snprintf(g_ini_path, sizeof g_ini_path, "%stmnt.ini", g_base);
  snprintf(g_state_path, sizeof g_state_path, "%stmnt.state", g_base);
  snprintf(g_cont_path, sizeof g_cont_path, "%stmnt_continue.state", g_base);
  cfg_load();
  if (!find_rom(argc, argv)) return 1;

  g_snes = snes_init();
  snes_loadRom(g_snes, g_rom, g_rom_len);
  g_snes->ppu->wideBuffer = calloc((256 + 2 * PPU_WIDE_MAX) * 240, 4);
  g_snes->ppu->m7hiresBuf = calloc((256 + 2 * PPU_WIDE_MAX) * 4 * 896, 4);
  g_state = malloc(4 * 1024 * 1024);
  g_cont = malloc(4 * 1024 * 1024);
  crashlog_init(g_snes, g_base, settings_summary);
  ach_init(g_snes, g_base);
  extras_init(g_snes, g_base);
  ws_init(g_snes);
  steam_init(g_base);
  g_msu_songs = msu_init(g_snes, g_base, "msu");
  msu_enable(cfg.msu && g_msu_songs);
  g_runner_after_vblank = menus_after_vblank;
  g_at = getenv("TMNT_AUTOTEST");
  snprintf(g_font_path, sizeof g_font_path, "%stmnt_font.bin", g_base);
  ui_font_load(g_font_path);

  g_win = SDL_CreateWindow("Teenage Mutant Ninja Turtles IV - Turtles in Time", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           256 * 4, 224 * 4, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  SDL_Renderer* ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!ren) ren = SDL_CreateRenderer(g_win, -1, 0);
  const int WW = 256 + 2 * PPU_WIDE_MAX;
  SDL_Texture *tex[2], *uitex;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  tex[0] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WW, 224);
  uitex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, UI_W, UI_H);
  SDL_SetTextureBlendMode(uitex, SDL_BLENDMODE_BLEND);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  tex[1] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, WW, 224);
  SDL_Texture* htex[2];   // high-resolution Mode 7 frames (up to 4x)
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  htex[0] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256 * 4, 224 * 4);
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  htex[1] = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 256 * 4, 224 * 4);
  apply_fullscreen();

  SDL_AudioSpec want = {0}, have;
  want.freq = SAMPLE_RATE; want.format = AUDIO_S16; want.channels = 2; want.samples = 1024;
  SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (dev) SDL_PauseAudioDevice(dev, 0);
  for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) pad_added(i);

  static uint32_t frame[224][256 + 2 * PPU_WIDE_MAX];
  bool fast = false;
  uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
  const double frame_time = 1.0 / 60.0988;
  while (!g_quit) {
    autotest_tick();
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      switch (ev.type) {
        case SDL_QUIT: g_quit = true; break;
        case SDL_CONTROLLERDEVICEADDED: pad_added(ev.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: pad_removed(ev.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
          bool down = ev.type == SDL_CONTROLLERBUTTONDOWN;
          int bt = ev.cbutton.button, p = pad_player(ev.cbutton.which);
          if (g_menu && g_bind_wait) {
            if (down && bt != SDL_CONTROLLER_BUTTON_GUIDE) bind_pad(bt);
            break;
          }
          if (bt == SDL_CONTROLLER_BUTTON_GUIDE && down) { open_menu(!g_menu); break; }
          if (g_menu) {
            if (!down) break;
            if (bt == SDL_CONTROLLER_BUTTON_DPAD_UP) menu_key(-1, 0, false);
            else if (bt == SDL_CONTROLLER_BUTTON_DPAD_DOWN) menu_key(1, 0, false);
            else if (bt == SDL_CONTROLLER_BUTTON_DPAD_LEFT) menu_key(0, -1, false);
            else if (bt == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) menu_key(0, 1, false);
            else if (bt == SDL_CONTROLLER_BUTTON_START && g_page == PG_MAIN) open_menu(false);
            else if (bt == SDL_CONTROLLER_BUTTON_A || bt == SDL_CONTROLLER_BUTTON_START) menu_key(0, 0, true);
            else if (bt == SDL_CONTROLLER_BUTTON_B || bt == SDL_CONTROLLER_BUTTON_BACK) menu_back();
            break;
          }
          int b = pad_to_btn(p, bt);
          if (b == BTN_START && down && p == 0 && cfg.pause_menu && game_in_play(g_snes->ram)) { open_menu(true); break; }
          if (b >= 0) set_btn(p, b, down);
          SDL_GameController* gc = SDL_GameControllerFromInstanceID(ev.cbutton.which);
          if (down && gc && SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK) &&
              SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) open_menu(true);
          break;
        }
        case SDL_KEYDOWN: case SDL_KEYUP: {
          bool down = ev.type == SDL_KEYDOWN;
          SDL_Keycode k = ev.key.keysym.sym;
          bool alt = ev.key.keysym.mod & KMOD_ALT;
          if (down && (k == SDLK_F11 || (k == SDLK_RETURN && alt))) { cfg.fullscreen ^= 1; apply_fullscreen(); cfg_save(); break; }
          if (g_menu && g_bind_wait) {
            if (down && !ev.key.repeat) { if (k == SDLK_ESCAPE) g_bind_wait = 0; else bind_key(k); }
            break;
          }
          if (down && k == SDLK_ESCAPE && !ev.key.repeat) { if (g_menu) menu_back(); else open_menu(true); break; }
          if (g_menu) {
            if (!down) break;
            if (k == SDLK_UP) menu_key(-1, 0, false);
            else if (k == SDLK_DOWN) menu_key(1, 0, false);
            else if (k == SDLK_LEFT) menu_key(0, -1, false);
            else if (k == SDLK_RIGHT) menu_key(0, 1, false);
            else if (k == SDLK_RETURN || k == SDLK_SPACE || k == g_keymap[0][BTN_B] || k == g_keymap[0][BTN_A]) menu_key(0, 0, true);
            else if (k == SDLK_BACKSPACE || k == g_keymap[0][BTN_Y]) menu_back();
            break;
          }
          int p = 0, b = key_to_btn(k, &p);
          if (b == BTN_START && down && !ev.key.repeat && p == 0 && cfg.pause_menu && game_in_play(g_snes->ram)) { open_menu(true); break; }
          if (b >= 0 && !ev.key.repeat) set_btn(p, b, down);
          if (k == SDLK_TAB) fast = down;
          if (k == SDLK_BACKSPACE) g_rewinding = down;
          if (down && !ev.key.repeat) {
            if (k == SDLK_F5) save_state_file();
            if (k == SDLK_F7) load_state_file();
            if (k == SDLK_F8 && restart_available()) restart_scene();
#ifndef TMNT_LAUNCHER
            if (k == SDLK_F12) { g_use_recomp ^= 1; ui_toast_text(g_use_recomp ? "CPU: RECOMPILED" : "CPU: INTERPRETER", NULL); }
#endif
          }
          break;
        }
      }
    }
    if (!g_menu) {
      for (int i = 0; i < g_npads && i < 2; i++) {   // left stick as d-pad
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(g_pads[i]);
        if (!gc) continue;
        int p = i ^ cfg.pad_swap;
        int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
        static int lx[2], ly[2];
        int nx = x < -16000 ? -1 : x > 16000 ? 1 : 0, ny = y < -16000 ? -1 : y > 16000 ? 1 : 0;
        if (nx != lx[p]) { set_btn(p, BTN_LEFT, nx < 0); set_btn(p, BTN_RIGHT, nx > 0); lx[p] = nx; }
        if (ny != ly[p]) { set_btn(p, BTN_UP, ny < 0); set_btn(p, BTN_DOWN, ny > 0); ly[p] = ny; }
      }
      bool rw_held = g_rewinding;
      for (int i = 0; i < g_npads; i++) {
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(g_pads[i]);
        if (gc && SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) rw_held = true;
      }
      int frames = fast ? 4 : 1;
      g_snes->ppu->m7hiresK = cfg.m7 == 2 ? 4 : cfg.m7 == 1 ? 2 : 0;
      g_snes->ppu->m7frameUsed = false;
      if (cfg.rewind && rw_held) {
        frames = 0;
        if (rewind_step()) {   // stepped back: run one frame (no input) to redraw
          uint16_t keep[2] = {g_btn_down[0], g_btn_down[1]};
          g_btn_down[0] = g_btn_down[1] = 0; push_buttons(); g_btn_down[0] = keep[0]; g_btn_down[1] = keep[1];
          runner_frame(g_snes);
        }
      }
      for (int i = 0; i < frames; i++) {
        extern int g_snes_overclock, g_dsp_interp;
        if (cfg.rewind) rewind_record();
        g_snes->ppu->noSpriteLimit = cfg.nolimit;
        g_dsp_interp = cfg.interp;
        g_snes_overclock = cfg.no_slowdown && game_in_play(g_snes->ram);
        ws_update(wide_extra());
        extras_apply();
        push_buttons();
        runner_frame(g_snes);
        after_frame_buttons();
        restart_tick();
        rumble_tick();
        const GameEvents* gev = game_events(g_snes->ram);
        ach_update(gev, g_assisted);
        extras_frame(gev, g_assisted);
        if (!cfg.unlocked && (ach_game_beaten() || game_cheat_entered(g_snes->ram))) {
          cfg.unlocked = 1; cfg_save();
          ui_toast_text("EXTRAS UNLOCKED!", "SEE PORT SETTINGS - EXTRAS.");
        }
        steam_tick();
        crashlog_frame();
      }
      if (dev && !fast) {
        int n = SAMPLE_RATE / 60;
        snes_setSamples(g_snes, g_audio, n);
        for (int i = 0; i < n * 2; i++) g_audio[i] = (int16_t)(g_audio[i] * cfg.volume / 100);
        msu_mix(g_audio, n, SAMPLE_RATE, cfg.volume * cfg.music_vol / 100);
        if (SDL_GetQueuedAudioSize(dev) < (Uint32)(have.freq / 5 * 4)) SDL_QueueAudio(dev, g_audio, n * 4);
      }
    }
    g_ui_tick++;
    if (!ui_font_ready() && (g_ui_tick & 31) == 0) ui_font_capture(g_snes->ppu->vram, g_snes->ppu->bgLayer[2].tileAdr, g_font_path);
    toast_tick();
    if (g_report_timer > 0) g_report_timer--;

    // the picture: the normal 256-wide frame, or the wide one (extra columns valid in this scene)
    int want = wide_extra(), act = ws_active_extra();
    if (want > 0) {
      uint32_t* wb = g_snes->ppu->wideBuffer;
      for (int y = 0; y < 224; y++) {
        uint32_t* src = wb + (y + 1) * WW + PPU_WIDE_MAX - want;
        for (int x = 0; x < 256 + 2 * want; x++)
          frame[y][x] = (x < want - act || x >= want + 256 + act) ? 0 : src[x];
      }
    } else {
      snes_setPixels(g_snes, g_pixels);
      for (int y = 0; y < 224; y++) {
        uint8_t* p = g_pixels + ((y * 2 + 16) * 512) * 4;
        for (int x = 0; x < 256; x++, p += 8) frame[y][x] = (p[3] << 16) | (p[2] << 8) | p[1];
      }
    }
    int fw = 256 + 2 * want;
    color_apply(&frame[0][0], WW, fw, 224);
    // menu, pop-ups and timer: drawn on a 256x224 canvas over the 4:3 part of the picture, crisp
    ui_clear();
    bool ui = false;
    if (g_menu) { draw_menu(); ui = true; }
    if (draw_overlays()) ui = true;
    if (g_at_shot[0]) {   // test screenshots include the UI
      static uint32_t comp[224][256 + 2 * PPU_WIDE_MAX];
      memcpy(comp, frame, sizeof comp);
      const uint32_t* u = ui_pixels();
      for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) {
        uint32_t c = u[y * UI_W + x], a = c >> 24, *d = &comp[y][x + want];
        if (!a) continue;
        int r = (((c >> 16) & 255) * a + ((*d >> 16) & 255) * (255 - a)) / 255, g = (((c >> 8) & 255) * a + ((*d >> 8) & 255) * (255 - a)) / 255, b = ((c & 255) * a + (*d & 255) * (255 - a)) / 255;
        *d = r << 16 | g << 8 | b;
      }
      autotest_shot(&comp[0][0], WW, fw, 224);
    }
    SDL_Texture* t = tex[cfg.smooth ? 1 : 0];
    SDL_Rect up = {0, 0, fw, 224};
    SDL_UpdateTexture(t, &up, frame, WW * 4);
    // Mode 7 frames (intro, title, time warps; never widescreen) can be redrawn at 2x / 4x
    if (cfg.m7 && g_snes->ppu->m7hiresK && g_snes->ppu->m7frameUsed && !g_menu && act == 0) {
      int K = g_snes->ppu->m7hiresK;
      g_snes->ppu->wideExtra = 0;
      ppu_renderMode7Hires(g_snes->ppu);
      color_apply(g_snes->ppu->m7hiresBuf, 256 * K, 256 * K, 224 * K);
      t = htex[cfg.smooth ? 1 : 0];
      up = (SDL_Rect){0, 0, 256 * K, 224 * K};
      SDL_UpdateTexture(t, &up, g_snes->ppu->m7hiresBuf, 256 * K * 4);
      want = 0; fw = 256;
    }
    int ww, wh;
    SDL_GetRendererOutputSize(ren, &ww, &wh);
    double ar = want > 0 ? fw * pixel_aspect() / 224.0
              : cfg.aspect == ASPECT_4_3 ? 4.0 / 3.0 : cfg.aspect == ASPECT_8_7 ? 256.0 / 224.0 : (double)ww / wh;
    int dw = ww, dh = (int)(ww / ar);
    if (dh > wh) { dh = wh; dw = (int)(wh * ar); }
    if (cfg.integer_scale && cfg.aspect != ASPECT_STRETCH) {
      int sc = dh / 224; if (sc < 1) sc = 1;
      dh = 224 * sc; dw = (int)(dh * ar);
    }
    SDL_Rect dst = {(ww - dw) / 2, (wh - dh) / 2, dw, dh};
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, t, &up, &dst);
    crt_overlay(ren, dst);
    if (ui) {
      SDL_UpdateTexture(uitex, NULL, ui_pixels(), UI_W * 4);
      SDL_Rect ud = dst;
      if (want > 0) { ud.w = (int)((double)dst.w * 256 / fw); ud.x = dst.x + (dst.w - ud.w) / 2; }
      SDL_RenderCopy(ren, uitex, NULL, &ud);
    }
    SDL_RenderPresent(ren);
    next += (uint64_t)(frame_time * freq);
    uint64_t now = SDL_GetPerformanceCounter();
    if (now < next) SDL_Delay((Uint32)((next - now) * 1000 / freq));
    else if (now - next > freq / 4) next = now;
  }
  cfg_save();
  extras_shutdown();
  steam_shutdown();
  SDL_Quit();
  return 0;
}
