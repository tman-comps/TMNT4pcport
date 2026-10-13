// Speedrun timer (whole run + per-scene splits with personal bests) and play statistics.
// Detected from the per-frame events in game.c, like the achievements.
//   tmnt_times.txt   best time per scene and for the whole game (frames)
//   tmnt_stats.txt   play time, scenes cleared, games started / beaten, lives lost, continues
// Times are in frames of the original console (60.0988 per second), so they don't depend on the PC.
#include <stdio.h>
#include <string.h>
#include "extras.h"
#include "ui.h"

static Snes* s;
static char g_times_path[1100], g_stats_path[1100];
static uint32_t g_best[10], g_best_run;
static uint64_t g_play_frames;
static uint32_t g_cleared, g_started, g_beaten, g_deaths, g_continues, g_two_player_scenes;
static int g_run_on, g_scene_on, g_run_valid;
static uint32_t g_run_frames, g_scene_frames;
static int g_split_show; static int32_t g_split_diff; static uint32_t g_split_time;
static int g_dirty;

static void save(void) {
  FILE* f = fopen(g_times_path, "w");
  if (f) {
    fprintf(f, "run %u\n", g_best_run);
    for (int i = 0; i < 10; i++) if (g_best[i]) fprintf(f, "scene %d %u\n", i + 1, g_best[i]);
    fclose(f);
  }
  f = fopen(g_stats_path, "w");
  if (f) {
    fprintf(f, "play_frames %llu\nscenes_cleared %u\ngames_started %u\ngames_beaten %u\nlives_lost %u\ncontinues %u\ntwo_player_scenes %u\n",
            (unsigned long long)g_play_frames, g_cleared, g_started, g_beaten, g_deaths, g_continues, g_two_player_scenes);
    fclose(f);
  }
  g_dirty = 0;
}

void extras_init(Snes* snes, const char* base_dir) {
  s = snes;
  snprintf(g_times_path, sizeof g_times_path, "%stmnt_times.txt", base_dir);
  snprintf(g_stats_path, sizeof g_stats_path, "%stmnt_stats.txt", base_dir);
  FILE* f = fopen(g_times_path, "r");
  char line[128];
  if (f) {
    while (fgets(line, sizeof line, f)) {
      unsigned a, b;
      if (sscanf(line, "run %u", &a) == 1) g_best_run = a;
      else if (sscanf(line, "scene %u %u", &a, &b) == 2 && a >= 1 && a <= 10) g_best[a - 1] = b;
    }
    fclose(f);
  }
  f = fopen(g_stats_path, "r");
  if (f) {
    while (fgets(line, sizeof line, f)) {
      unsigned long long v; unsigned u;
      if (sscanf(line, "play_frames %llu", &v) == 1) g_play_frames = v;
      else if (sscanf(line, "scenes_cleared %u", &u) == 1) g_cleared = u;
      else if (sscanf(line, "games_started %u", &u) == 1) g_started = u;
      else if (sscanf(line, "games_beaten %u", &u) == 1) g_beaten = u;
      else if (sscanf(line, "lives_lost %u", &u) == 1) g_deaths = u;
      else if (sscanf(line, "continues %u", &u) == 1) g_continues = u;
      else if (sscanf(line, "two_player_scenes %u", &u) == 1) g_two_player_scenes = u;
    }
    fclose(f);
  }
}

void extras_frame(const GameEvents* ev, int assisted) {
  if (!s) return;
  const uint8_t* ram = s->ram;
  int mode = game_mode(ram);
  if (mode >= 5) g_play_frames++;
  if (ev->new_game && !ev->time_trial) { g_run_on = 1; g_run_valid = !assisted; g_run_frames = 0; g_started++; g_dirty = 1; }
  if (mode < 5 && mode > 0) { g_run_on = 0; g_scene_on = 0; }   // back at the title
  if (assisted) g_run_valid = 0;
  if (g_run_on) g_run_frames++;
  if (ev->scene_start) { g_scene_on = 1; g_scene_frames = 0; }
  else if (g_scene_on) g_scene_frames++;
  for (int p = 0; p < 2; p++) if (ev->death[p]) { g_deaths++; g_dirty = 1; }
  if (ev->continued) { g_continues++; g_dirty = 1; }
  if (ev->scene_clear && g_scene_on) {
    int i = ev->scene_clear - 1;
    g_cleared++;
    if (ev->two_players) g_two_player_scenes++;
    g_split_time = g_scene_frames;
    g_split_diff = g_best[i] ? (int32_t)g_scene_frames - (int32_t)g_best[i] : 0x7fffffff;
    if (!assisted && !ev->time_trial && (!g_best[i] || g_scene_frames < g_best[i])) g_best[i] = g_scene_frames;
    g_split_show = 300;
    g_scene_on = 0; g_dirty = 1;
  }
  if (ev->beaten && g_run_on) {
    g_beaten++;
    if (g_run_valid && (!g_best_run || g_run_frames < g_best_run)) g_best_run = g_run_frames;
    g_run_on = 0; g_dirty = 1;
  }
  if (g_split_show > 0) g_split_show--;
  if (g_dirty && (g_play_frames % 600 == 0 || ev->scene_clear || ev->beaten)) save();
}

void extras_shutdown(void) { if (s) save(); }

void extras_format_time(char* out, int n, uint32_t f) {
  uint32_t cs = (uint32_t)((uint64_t)f * 100 * 1000 / 60099);   // console frames -> centiseconds
  uint32_t h = cs / 360000, m = cs / 6000 % 60, sec = cs / 100 % 60, c = cs % 100;
  if (h) snprintf(out, n, "%u:%02u:%02u.%02u", h, m, sec, c);
  else snprintf(out, n, "%02u:%02u.%02u", m, sec, c);
}

int extras_timer_text(char* run, int rn, char* st, int sn, uint32_t* rc, uint32_t* sc) {
  if (!g_run_on && !g_split_show) return 0;
  extras_format_time(run, rn, g_run_frames);
  *rc = g_run_valid ? UI_WHITE : UI_GREY;
  st[0] = 0; *sc = UI_WHITE;
  if (g_split_show) {
    char t[24]; extras_format_time(t, sizeof t, g_split_time);
    if (g_split_diff == 0x7fffffff) { snprintf(st, sn, "%s NEW", t); *sc = UI_GOLD; }
    else {
      char d[24]; uint32_t ad = g_split_diff < 0 ? -g_split_diff : g_split_diff;
      extras_format_time(d, sizeof d, ad);
      snprintf(st, sn, "%c%s", g_split_diff <= 0 ? '-' : '+', d);
      *sc = g_split_diff <= 0 ? UI_GREEN : UI_RED;
    }
  } else if (g_scene_on) {
    extras_format_time(st, sn, g_scene_frames);
    *sc = UI_BLUE;
  }
  return 1;
}

int extras_stat_lines(char l[][48], int max) {
  int n = 0;
  char t[32];
  uint64_t secs = g_play_frames * 1000 / 60099;
  if (n < max) snprintf(l[n++], 48, "PLAY TIME|%lluH %02lluM", (unsigned long long)(secs / 3600), (unsigned long long)(secs / 60 % 60));
  if (n < max) snprintf(l[n++], 48, "GAMES STARTED|%u", g_started);
  if (n < max) snprintf(l[n++], 48, "GAMES BEATEN|%u", g_beaten);
  if (n < max) snprintf(l[n++], 48, "SCENES CLEARED|%u", g_cleared);
  if (n < max) snprintf(l[n++], 48, "LIVES LOST|%u", g_deaths);
  if (n < max) snprintf(l[n++], 48, "CONTINUES USED|%u", g_continues);
  if (g_best_run) { extras_format_time(t, sizeof t, g_best_run); if (n < max) snprintf(l[n++], 48, "BEST GAME|%s", t); }
  if (n < max) snprintf(l[n++], 48, "BEST SCENE TIMES");
  for (int i = 0; i < 10 && n < max; i++) {
    if (!g_best[i]) continue;
    extras_format_time(t, sizeof t, g_best[i]);
    snprintf(l[n++], 48, " SCENE %d|%s", i + 1, t);
  }
  return n;
}
