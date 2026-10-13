// MSU-1 style music replacement (orchestral / remastered packs).
//
// How the game plays music: it asks for a song with A = song number at $80:9CC6, which stores a
// pointer into the song table at $82:B95D (8 bytes per song) to $1E2C. The NMI then streams the
// song's data to the sound CPU through $2140-$2143 and finally sends command $80 (start) through
// the one-byte command queue that goes out on $2140 (sender at $80:835C). Command $FE stops the
// music before a new song and $FC fades it out.
// So: a 16-bit write to $1E2C tells us the song; when a pack has a file for it we play that and turn
// the $80 that would start the SNES song into $FE, so only the sound effects come from the SPC.
//
// Files: <exe dir>/msu/msu.txt maps song numbers to track numbers ("SS N  # comment", hex song),
// and the PCM for track N is any file in msu/ whose name ends in "-N.pcm" (standard MSU-1 PCM:
// "MSU1", u32 loop point in samples, then 44.1 kHz 16-bit stereo little-endian). Songs the game
// plays that have no mapping are written to msu/unmapped.txt to help complete a mapping.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "msu.h"
#include "game.h"

extern uint8_t (*g_snes_write_filter)(Snes*, uint32_t, uint8_t);

static Snes* s;
static char g_dir[1024];
static int g_map[256];
static uint8_t g_logged[256];
static int g_enabled, g_song = -1, g_pending = -1;
static uint8_t g_ptr_lo;
static FILE* g_pcm;
static uint32_t g_loop, g_data_len, g_pos;
static double g_frac;
static int g_vol = 255, g_vol_target = 255, g_vol_step;
static int g_current;
static int g_sn, g_si;

static void stop(void) { if (g_pcm) fclose(g_pcm); g_pcm = NULL; g_current = 0; }

static FILE* open_track(int n, uint32_t* loop, uint32_t* len) {
  DIR* d = opendir(g_dir);
  if (!d) return NULL;
  char suffix[32]; snprintf(suffix, sizeof suffix, "-%d.pcm", n);
  struct dirent* e; FILE* f = NULL;
  while ((e = readdir(d))) {
    size_t L = strlen(e->d_name), S = strlen(suffix);
    if (L > S && !strcmp(e->d_name + L - S, suffix)) {
      char p[2048]; snprintf(p, sizeof p, "%s/%s", g_dir, e->d_name);
      if ((f = fopen(p, "rb"))) break;
    }
  }
  closedir(d);
  if (!f) return NULL;
  char hdr[4]; uint8_t lp[4];
  if (fread(hdr, 1, 4, f) != 4 || memcmp(hdr, "MSU1", 4) || fread(lp, 1, 4, f) != 4) { fclose(f); return NULL; }
  *loop = lp[0] | lp[1] << 8 | lp[2] << 16 | (uint32_t)lp[3] << 24;
  fseek(f, 0, SEEK_END); long sz = ftell(f);
  *len = (uint32_t)((sz - 8) / 4);
  fseek(f, 8, SEEK_SET);
  return f;
}

static int play(int n) {
  if (n == g_current && g_pcm) return 1;
  stop();
  g_pcm = open_track(n, &g_loop, &g_data_len);
  if (!g_pcm) return 0;
  g_current = n; g_pos = 0; g_frac = 0; g_sn = g_si = 0;
  g_vol = g_vol_target = 255;
  return 1;
}

static uint8_t filter(Snes* sn, uint32_t adr, uint8_t v) {
  uint32_t bank = (adr >> 16) & 0xff, a = adr & 0xffff;
  bool wram = bank == 0x7e ? a == 0x1E2C || a == 0x1E2D : (bank & 0x7f) < 0x40 && (a == 0x1E2C || a == 0x1E2D);
  if (wram) {
    if (a == 0x1E2C) g_ptr_lo = v;
    else {
      int ptr = g_ptr_lo | v << 8;
      if (ptr >= 0xB95D && (ptr - 0xB95D) % 8 == 0 && ptr < 0xB95D + 8 * 256) {
        g_song = g_pending = (ptr - 0xB95D) / 8;
        if (g_enabled && !g_map[g_song] && !g_logged[g_song]) {   // help complete the mapping
          g_logged[g_song] = 1;
          char p[1100]; snprintf(p, sizeof p, "%s/unmapped.txt", g_dir);
          FILE* f = fopen(p, "a");
          if (f) { fprintf(f, "%02X  ?   # %s\n", g_song, game_describe(sn->ram)); fclose(f); }
        }
      }
    }
    return v;
  }
  if ((bank & 0x7f) >= 0x40 || a != 0x2140) return v;
  if (sn->cpu->pc != 0x835F || (sn->cpu->k & 0x7f) != 0) return v;   // only the game's command sender
  switch (v) {
    case 0x80:   // start the song that was just streamed
      if (g_enabled && g_pending >= 0) {
        int n = g_map[g_pending];
        g_pending = -1;
        if (n && play(n)) return 0xFE;   // ours plays: keep the SPC music stopped
        stop();
      }
      break;
    case 0xFE: if (g_pending < 0) stop(); break;
    case 0xFC: g_vol_target = 0; g_vol_step = 2; break;
  }
  return v;
}

int msu_init(Snes* snes, const char* base_dir, const char* folder) {
  s = snes;
  stop();
  memset(g_logged, 0, sizeof g_logged);
  memset(g_map, 0, sizeof g_map);
  snprintf(g_dir, sizeof g_dir, "%s%s", base_dir, folder);
  char p[1100]; snprintf(p, sizeof p, "%s/msu.txt", g_dir);
  FILE* f = fopen(p, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof line, f)) {
      unsigned song; int n;
      if (line[0] == '#') continue;
      if (sscanf(line, "%x %d", &song, &n) == 2 && song < 256) g_map[song] = n;
    }
    fclose(f);
  }
  g_snes_write_filter = filter;
  int have = 0;
  for (int i = 0; i < 256; i++) if (g_map[i]) {
    uint32_t a, b; FILE* t = open_track(g_map[i], &a, &b);
    if (t) { fclose(t); have++; }
  }
  return have;
}

void msu_enable(int on) { g_enabled = on; if (!on) stop(); }
int msu_playing(void) { return g_pcm != NULL; }
int msu_current_song(void) { return g_song; }

static int16_t g_sbuf[8192 * 2];
static int refill(void) {
  g_sn = g_si = 0;
  while (g_sn < 8192) {
    if (g_pos >= g_data_len) {
      if (g_loop >= g_data_len) break;
      g_pos = g_loop;
      fseek(g_pcm, 8 + (long)g_pos * 4, SEEK_SET);
    }
    uint32_t want = 8192 - g_sn;
    if (want > g_data_len - g_pos) want = g_data_len - g_pos;
    size_t got = fread(&g_sbuf[g_sn * 2], 4, want, g_pcm);
    if (!got) break;
    g_sn += (int)got; g_pos += (uint32_t)got;
  }
  return g_sn > 0;
}

void msu_mix(int16_t* out, int n, int out_rate, int volume_pct) {
  if (!g_pcm) return;
  double step = 44100.0 / out_rate;
  for (int i = 0; i < n; i++) {
    if (g_si >= g_sn && !refill()) { stop(); return; }
    if (g_vol != g_vol_target) {
      if (g_vol < g_vol_target) g_vol = g_vol + g_vol_step > g_vol_target ? g_vol_target : g_vol + g_vol_step;
      else g_vol = g_vol - g_vol_step < g_vol_target ? g_vol_target : g_vol - g_vol_step;
      if (!g_vol_step) g_vol = g_vol_target;
    }
    int gain = g_vol * volume_pct;
    int L = out[2 * i] + g_sbuf[g_si * 2] * gain / 25500;
    int R = out[2 * i + 1] + g_sbuf[g_si * 2 + 1] * gain / 25500;
    out[2 * i] = (int16_t)(L > 32767 ? 32767 : L < -32768 ? -32768 : L);
    out[2 * i + 1] = (int16_t)(R > 32767 ? 32767 : R < -32768 ? -32768 : R);
    g_frac += step;
    while (g_frac >= 1.0) { g_frac -= 1.0; g_si++; if (g_si >= g_sn && !refill()) { stop(); return; } }
  }
}
