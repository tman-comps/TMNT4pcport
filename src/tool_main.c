// Headless scripted driver for testing and analysis (no window, no sound).
//   tmnt_tool <rom> <interp|recomp> "<script>"
// Script words, run in order:
//   wN            wait N frames, no buttons
//   h<btns>:N     hold buttons for N frames. Buttons: B Y s(elect) S(tart) U D L R A X l r,
//                 player 2 after a '2' (e.g. hS:4 or h2S:4)
//   p<btns>       press for 4 frames then release for 8
//   rN            N frames of pseudo-random play
//   fN            N frames of the fighting bot (walks right, attacks)
//   until:ADDR:VAL[:S]  run until a WRAM byte equals VAL (max 20000 frames), tapping Start with :S
//   seed:N        seed for r
//   god:on|off    keep lives / health topped up every frame (game.h addresses)
//   weak:on|off   enemies die in one hit (coverage runs)
//   poke:ADDR:VAL write a WRAM byte now      freeze:ADDR:VAL  write it every frame
//   peek:ADDR:N   print N WRAM bytes
//   shot:FILE     screenshot (PPM, 256x224)
//   wide:N        render N extra columns on each side (shots are then 256+2N wide)
//   save:FILE     save state                 load:FILE   load state
//   hash          print a hash of WRAM+VRAM+CGRAM+OAM after every frame from now on
//   watch:ADDR:N  print N WRAM bytes whenever they change (from now on)
//   cdl:FILE      write the coverage log (interp mode) at the end
//   dump:FILE     write the 128 KB of WRAM
//   ach:DIR/      run achievements, timer and stats (files in DIR/), print events and unlocks
//   wav:FILE      record the sound from now on (48 kHz stereo)
//   msu:DIR/      use DIR/msu.txt and its PCM files (MSU-1 test)
//   songlog       print each song the game asks for (see msu.c)
//   apulog/apuoff print writes to the sound CPU ports $2140-$2143
//   info          print frame number, game state bytes and misses
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snes.h"
#include "ppu.h"
#include "runner.h"
#include "game.h"
#include "msu.h"
#include "widescreen.h"
#include "achievements.h"
#include "extras.h"

extern uint8_t* g_cdl;
static Snes* S;
static uint32_t rng = 1;
static int wide;   // extra columns on each side (wide:N), shots then come from the PPU's wide buffer
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static int frame_no, god, weak, do_hash, song_log, ach_on;
static struct { uint32_t a; uint8_t v; } fz[64]; static int nfz;
static struct { uint32_t a; int n; uint8_t last[32]; } wt[16]; static int nwt;

static FILE* wav; static uint32_t wav_bytes; static int msu_on;
static void wav_frame(void) {
  static int16_t buf[800 * 2];
  snes_setSamples(S, buf, 800);
  if (msu_on) msu_mix(buf, 800, 48000, 100);
  fwrite(buf, 4, 800, wav); wav_bytes += 3200;
}
static void wav_close(void) {
  if (!wav) return;
  uint8_t h[44] = "RIFF....WAVEfmt \x10\0\0\0\x01\0\x02\0\x80\xbb\0\0\0\xee\x02\0\x04\0\x10\0data....";
  uint32_t a = wav_bytes + 36, b = wav_bytes;
  memcpy(h + 4, &a, 4); memcpy(h + 40, &b, 4);
  fseek(wav, 0, SEEK_SET); fwrite(h, 1, 44, wav); fclose(wav); wav = NULL;
}
static int apu_log;
extern uint8_t (*g_snes_write_filter)(Snes*, uint32_t, uint8_t);
static uint8_t apu_filter(Snes* s, uint32_t adr, uint8_t v) {
  uint32_t a = adr & 0xffff;
  if (((adr >> 16) & 0x7f) < 0x40 && a >= 0x2140 && a <= 0x2143)
    printf("f%d apu %04X=%02X pc=%02X%04X\n", frame_no, a, v, s->cpu->k, s->cpu->pc);
  return v;
}

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const uint8_t* b = p;
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}

static void frame(uint16_t p1, uint16_t p2) {
  for (int i = 0; i < 12; i++) { snes_setButtonState(S, 1, i, (p1 >> i) & 1); snes_setButtonState(S, 2, i, (p2 >> i) & 1); }
  for (int i = 0; i < nfz; i++) S->ram[fz[i].a] = fz[i].v;
  if (god) game_god_mode(S->ram);
  if (weak && game_in_play(S->ram))   // object health (see analysis/tmnt4/ram.txt): everything dies in one hit
    for (int a = 0x0900 + 0x1A; a < 0x1A80; a += 0x70) if (S->ram[a] > 1 && S->ram[a] <= 0xC0 && !S->ram[a + 1]) S->ram[a] = 1;
  if (wide) ws_update(wide);
  runner_frame(S);
  frame_no++;
  if (wav) wav_frame();
  if (ach_on) {
    const GameEvents* ev = game_events(S->ram);
    if (ev->new_game) printf("f%d EVENT new game\n", frame_no);
    if (ev->scene_start) printf("f%d EVENT scene %d start\n", frame_no, ev->scene_start);
    if (ev->scene_clear) printf("f%d EVENT scene %d clear\n", frame_no, ev->scene_clear);
    if (ev->beaten) printf("f%d EVENT game beaten\n", frame_no);
    if (ev->continued) printf("f%d EVENT continue\n", frame_no);
    for (int p = 0; p < 2; p++) if (ev->death[p]) printf("f%d EVENT player %d lost a life\n", frame_no, p + 1);
    ach_update(ev, 0);
    extras_frame(ev, 0);
    for (int id; (id = ach_pop_toast()) >= 0;) { const char *t, *d; ach_get(id, &t, &d); printf("f%d ACHIEVEMENT %s\n", frame_no, t); }
  }
  if (song_log) {
    static uint32_t last = 0;
    uint32_t sg = msu_current_song();
    if (sg != last) { printf("f%d song %08X  # %s\n", frame_no, sg, game_describe(S->ram)); last = sg; }
  }
  if (do_hash) {
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, S->ram, sizeof S->ram);
    h = fnv(h, S->ppu->vram, sizeof S->ppu->vram);
    h = fnv(h, S->ppu->cgram, sizeof S->ppu->cgram);
    h = fnv(h, S->ppu->oam, sizeof S->ppu->oam);
    printf("%d %016llx\n", frame_no, (unsigned long long)h);
  }
  for (int i = 0; i < nwt; i++) {
    if (memcmp(wt[i].last, S->ram + wt[i].a, wt[i].n)) {
      memcpy(wt[i].last, S->ram + wt[i].a, wt[i].n);
      printf("f%d %05X:", frame_no, wt[i].a);
      for (int k = 0; k < wt[i].n; k++) printf(" %02X", wt[i].last[k]);
      printf("\n");
    }
  }
}

static uint16_t parse_btns(const char* s, uint16_t* p2) {
  static const char k[] = "BYsSUDLRAXlr";
  uint16_t m = 0, *cur = &m; *p2 = 0;
  for (; *s && *s != ':'; s++) {
    if (*s == '2') { cur = p2; continue; }
    const char* f = strchr(k, *s);
    if (f) *cur |= 1 << (f - k);
  }
  return m;
}

static void random_play(int n) {
  static const uint16_t cho[] = {
    1 << BTN_RIGHT, (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B), 1 << BTN_Y,
    (1 << BTN_LEFT) | (1 << BTN_Y), (1 << BTN_B) | (1 << BTN_Y), 1 << BTN_A, (1 << BTN_UP) | (1 << BTN_Y),
    (1 << BTN_DOWN) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_UP), (1 << BTN_RIGHT) | (1 << BTN_DOWN),
    (1 << BTN_Y) | (1 << BTN_B), 1 << BTN_X, (1 << BTN_RIGHT) | (1 << BTN_Y) | (1 << BTN_B), 1 << BTN_LEFT,
    (1 << BTN_DOWN) | (1 << BTN_B), (1 << BTN_UP) | (1 << BTN_LEFT) | (1 << BTN_Y)};
  int done = 0;
  while (done < n) {
    int len = 3 + rnd() % 30; uint16_t b = cho[rnd() % (sizeof cho / sizeof cho[0])];
    if (rnd() % 40 == 0) b = 1 << BTN_START;
    for (int i = 0; i < len && done < n; i++, done++) frame(b, 0);
  }
}

// beat-'em-up bot: mostly walks right while attacking, with some vertical movement and jump attacks
static void fight_play(int n) {
  static const uint16_t cho[] = {
    (1 << BTN_RIGHT) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_Y), 1 << BTN_RIGHT, 1 << BTN_RIGHT, 1 << BTN_Y,
    (1 << BTN_UP) | (1 << BTN_Y), (1 << BTN_DOWN) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_UP),
    (1 << BTN_RIGHT) | (1 << BTN_DOWN), (1 << BTN_B) | (1 << BTN_Y), (1 << BTN_RIGHT) | (1 << BTN_B),
    (1 << BTN_LEFT) | (1 << BTN_Y), (1 << BTN_Y) | (1 << BTN_B), 1 << BTN_LEFT};
  int done = 0;
  while (done < n) {
    int len = 3 + rnd() % 24; uint16_t b = cho[rnd() % (sizeof cho / sizeof cho[0])];
    for (int i = 0; i < len && done < n; i++, done++) {
      uint16_t bb = b;
      if ((b & (1 << BTN_Y)) && (i & 4)) bb &= ~(1 << BTN_Y);   // mash attack rather than hold it
      if (rnd() % 600 == 0) bb = 1 << BTN_START;               // get past text and "press start" screens
      frame(bb, 0);
    }
  }
}

static void shot(const char* name) {
  static uint8_t px[512 * 480 * 4];
  snes_setPixels(S, px);
  FILE* o = fopen(name, "wb");
  if (!o) { perror(name); return; }
  fprintf(o, "P6 256 224 255\n");
  for (int y = 0; y < 224; y++) for (int x = 0; x < 256; x++) {
    uint8_t* p = px + ((y * 2 + 16) * 512 + x * 2) * 4;
    fputc(p[3], o); fputc(p[2], o); fputc(p[1], o);   // default output format is BGRX with one leading pad byte
  }
  fclose(o);
}

static void shot_wide(const char* name) {
  const int WW = 256 + 2 * PPU_WIDE_MAX;
  FILE* o = fopen(name, "wb");
  if (!o) { perror(name); return; }
  fprintf(o, "P6 %d 224 255\n", 256 + 2 * wide);
  for (int y = 0; y < 224; y++) for (int x = PPU_WIDE_MAX - wide; x < PPU_WIDE_MAX + 256 + wide; x++) {
    uint32_t c = S->ppu->wideBuffer[(y + 1) * WW + x];
    fputc(c >> 16, o); fputc(c >> 8, o); fputc(c, o);
  }
  fclose(o);
}

static void state_io(const char* name, int save) {
  static uint8_t buf[1 << 20];
  if (save) {
    int n = snes_saveState(S, buf);
    FILE* o = fopen(name, "wb"); if (!o) { perror(name); return; }
    fwrite(buf, 1, n, o); fclose(o);
  } else {
    int n; uint8_t* d = runner_load_file(name, &n);
    if (!d || !snes_loadState(S, d, n)) fprintf(stderr, "can't load state %s\n", name);
    else if (wide) ws_state_loaded();
    free(d);
  }
}

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: %s rom interp|recomp \"script\"\n", argv[0]); return 1; }
  int len; uint8_t* rom = runner_load_file(argv[1], &len);
  if (!rom || (!runner_check_rom(rom, len) && !getenv("TMNT_ANY_ROM"))) { fprintf(stderr, "need %s\n", "Teenage Mutant Ninja Turtles IV - Turtles in Time (USA)"); return 1; }
  g_use_recomp = !strcmp(argv[2], "recomp");
  const char* cdl_out = NULL;
  if (!g_use_recomp) g_cdl = calloc(1, 1 << 24);
  S = snes_init();
  snes_loadRom(S, rom, len);
  char* script = strdup(argv[3]);
  for (char* w = strtok(script, " \n"); w; w = strtok(NULL, " \n")) {
    uint16_t p2;
    if (w[0] == 'w' && w[1] >= '0' && w[1] <= '9') { for (int n = atoi(w + 1); n > 0; n--) frame(0, 0); }
    else if (w[0] == 'h' && strchr(w, ':')) { uint16_t b = parse_btns(w + 1, &p2); for (int n = atoi(strchr(w, ':') + 1); n > 0; n--) frame(b, p2); }
    else if (w[0] == 'p' && strncmp(w, "peek:", 5) && strncmp(w, "poke:", 5)) { uint16_t b = parse_btns(w + 1, &p2); for (int n = 0; n < 4; n++) frame(b, p2); for (int n = 0; n < 8; n++) frame(0, 0); }
    else if (w[0] == 'r' && w[1] >= '0' && w[1] <= '9') random_play(atoi(w + 1));
    else if (w[0] == 'f' && w[1] >= '0' && w[1] <= '9') fight_play(atoi(w + 1));
    else if (!strncmp(w, "until:", 6)) {   // until:ADDR:VAL[:S] wait (tapping Start if :S) until WRAM byte == VAL
      char* e; uint32_t a = strtoul(w + 6, &e, 16) & 0x1ffff; char* e2; uint8_t v = strtoul(e + 1, &e2, 16);
      int tap = *e2 == ':' && e2[1] == 'S';
      for (int n = 0; S->ram[a] != v && n < 20000; n++) frame(tap && n % 40 < 3 ? 1 << BTN_START : 0, 0);
    }
    else if (!strncmp(w, "seed:", 5)) rng = strtoul(w + 5, 0, 0);
    else if (!strncmp(w, "god:", 4)) god = !strcmp(w + 4, "on");
    else if (!strncmp(w, "weak:", 5)) weak = !strcmp(w + 5, "on");
    else if (!strncmp(w, "poke:", 5)) { char* e; uint32_t a = strtoul(w + 5, &e, 16); S->ram[a & 0x1ffff] = strtoul(e + 1, 0, 16); }
    else if (!strncmp(w, "freeze:", 7)) { char* e; fz[nfz].a = strtoul(w + 7, &e, 16) & 0x1ffff; fz[nfz++].v = strtoul(e + 1, 0, 16); }
    else if (!strncmp(w, "peek:", 5)) {
      char* e; uint32_t a = strtoul(w + 5, &e, 16); int n = *e ? atoi(e + 1) : 1;
      printf("f%d %05X:", frame_no, a);
      for (int i = 0; i < n; i++) printf(" %02X", S->ram[(a + i) & 0x1ffff]);
      printf("\n");
    }
    else if (!strncmp(w, "watch:", 6)) { char* e; wt[nwt].a = strtoul(w + 6, &e, 16); wt[nwt].n = *e ? atoi(e + 1) : 1; if (wt[nwt].n > 32) wt[nwt].n = 32; memcpy(wt[nwt].last, S->ram + wt[nwt].a, wt[nwt].n); nwt++; }
    else if (!strncmp(w, "shot:", 5)) { if (wide) shot_wide(w + 5); else shot(w + 5); }
    else if (!strncmp(w, "wide:", 5)) {
      wide = atoi(w + 5); if (wide > PPU_WIDE_MAX) wide = PPU_WIDE_MAX;
      if (!S->ppu->wideBuffer) S->ppu->wideBuffer = calloc((256 + 2 * PPU_WIDE_MAX) * 240, 4);
      S->ppu->wideExtra = wide;
      ws_init(S); ws_state_loaded();
    }
    else if (!strncmp(w, "save:", 5)) state_io(w + 5, 1);
    else if (!strncmp(w, "load:", 5)) state_io(w + 5, 0);
    else if (!strncmp(w, "vram:", 5)) {   // VRAM (64 KB), then CGRAM (512 bytes), then the PPU's BG settings
      FILE* o = fopen(w + 5, "wb"); fwrite(S->ppu->vram, 1, sizeof S->ppu->vram, o); fwrite(S->ppu->cgram, 1, sizeof S->ppu->cgram, o); fclose(o);
      for (int l = 0; l < 4; l++) printf("bg%d tilemap %04X tiles %04X\n", l + 1, S->ppu->bgLayer[l].tilemapAdr, S->ppu->bgLayer[l].tileAdr);
    }
    else if (!strncmp(w, "dump:", 5)) { FILE* o = fopen(w + 5, "wb"); fwrite(S->ram, 1, sizeof S->ram, o); fclose(o); }
    else if (!strcmp(w, "hash")) do_hash = 1;
    else if (!strcmp(w, "apulog")) g_snes_write_filter = apu_filter;
    else if (!strncmp(w, "ach:", 4)) { ach_init(S, w + 4); extras_init(S, w + 4); ach_on = 1; }
    else if (!strncmp(w, "wav:", 4)) { wav = fopen(w + 4, "wb"); uint8_t z[44] = {0}; fwrite(z, 1, 44, wav); wav_bytes = 0; }
    else if (!strncmp(w, "msu:", 4)) { msu_on = msu_init(S, w + 4, "") > 0; msu_enable(1); }
    else if (!strcmp(w, "songlog")) { msu_init(S, "/nonexistent/", "msu"); msu_enable(1); song_log = 1; extern int g_msu_debug; g_msu_debug = 1; }
    else if (!strcmp(w, "apuoff")) g_snes_write_filter = NULL;
    else if (!strncmp(w, "cdl:", 4)) cdl_out = w + 4;
    else if (!strcmp(w, "info")) printf("f%d %s  bg hofs %d %d %d\n", frame_no, game_describe(S->ram), S->ppu->bgLayer[0].hScroll, S->ppu->bgLayer[1].hScroll, S->ppu->bgLayer[2].hScroll);
    else { fprintf(stderr, "unknown script word: %s\n", w); return 1; }
  }
  wav_close();
  if (ach_on) {
    extras_shutdown();
    char l[24][48]; int n = extras_stat_lines(l, 24);
    for (int i = 0; i < n; i++) printf("STAT %s\n", l[i]);
  }
  if (cdl_out && g_cdl) { FILE* o = fopen(cdl_out, "wb"); fwrite(g_cdl, 1, 1 << 24, o); fclose(o); }
  static uint32_t miss[4096]; int n = runner_miss_list(miss, 4096), nrom = 0;
  for (int i = 0; i < n; i++) if ((miss[i] >> 16) != 0x7E && (miss[i] >> 16) != 0x7F && (miss[i] & 0xffff) >= 0x8000) nrom++;
  fprintf(stderr, "frames %d, misses: %llu total, %d unique, %d unique in ROM\n", frame_no, (unsigned long long)runner_misses(), n, nrom);
  for (int i = 0, k = 0; i < n && k < 16; i++)
    if ((miss[i] >> 16) != 0x7E && (miss[i] >> 16) != 0x7F && (miss[i] & 0xffff) >= 0x8000) { fprintf(stderr, "  %06X\n", miss[i]); k++; }
  return 0;
}
