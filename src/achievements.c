// Port achievements: our own list. Everything is detected from game RAM through the per-frame
// events in game.c. Nothing counts in a session where save states, rewind or the extras were used.
// Saved to tmnt_achievements.txt next to the program; mirrored to Steam when available (steam.c).
#include <stdio.h>
#include <string.h>
#include "achievements.h"
#include "steam.h"

typedef struct { const char* title; const char* desc; } Ach;
enum {
  A_START, A_S1, A_S2, A_S3, A_S4, A_S5, A_S6, A_S7, A_S8, A_S9, A_BEAT,
  A_ALL4, A_DUO, A_UNTOUCHED, A_NOCONT, A_TRIAL, A_COUNT
};
static const Ach k_ach[A_COUNT] = {
  {"COWABUNGA!", "Start a new game."},
  {"BIG APPLE, 3 AM", "Clear scene 1."},
  {"ALLEYCAT BLUES", "Clear scene 2."},
  {"SEWER SURFIN'", "Clear scene 3."},
  {"LET'S KICK SHELL!", "Clear the Technodrome, scene 4."},
  {"TURTLESAURUS", "Clear Prehistoric Turtlesaurus, scene 5."},
  {"SKULL AND CROSSBONES", "Clear the pirate ship, scene 6."},
  {"WOUNDED KNEE", "Clear the train, scene 7."},
  {"NEON NIGHT-RIDERS", "Clear scene 8."},
  {"STARBASE", "Clear the Starbase, scene 9."},
  {"TURTLES IN TIME", "Beat Super Shredder and save New York."},
  {"HEROES IN A HALF SHELL", "Play a scene as each of the four turtles."},
  {"TOTALLY TUBULAR TEAM", "Clear a scene with two players."},
  {"SHELL OF STEEL", "Clear a scene without losing any health."},
  {"ONE QUARTER", "Beat the game without using a continue."},
  {"AGAINST THE CLOCK", "Finish a Time Trial course."},
};

static char g_path[1100];
static int g_unlocked[A_COUNT];
static int g_toast[A_COUNT], g_toast_n;
static Snes* s;
static int g_turtles;          // bit per turtle played (kept across sessions)
static int g_beaten;           // game beaten at least once
static int g_run_on, g_run_cont, g_scene_hurt, g_scene_two;

static void save(void) {
  FILE* f = fopen(g_path, "w");
  if (!f) return;
  for (int i = 0; i < A_COUNT; i++) if (g_unlocked[i]) fprintf(f, "%d\n", i);
  fprintf(f, "turtles %d\nbeaten %d\n", g_turtles, g_beaten);
  fclose(f);
}
static void unlock(int id) {
  if (g_unlocked[id]) return;
  g_unlocked[id] = 1;
  if (g_toast_n < A_COUNT) g_toast[g_toast_n++] = id;
  save();
}

void ach_init(Snes* snes, const char* base_dir) {
  s = snes;
  snprintf(g_path, sizeof g_path, "%stmnt_achievements.txt", base_dir);
  FILE* f = fopen(g_path, "r");
  if (!f) return;
  char line[64];
  while (fgets(line, sizeof line, f)) {
    int v;
    if (sscanf(line, "turtles %d", &v) == 1) g_turtles = v;
    else if (sscanf(line, "beaten %d", &v) == 1) g_beaten = v;
    else if (sscanf(line, "%d", &v) == 1 && v >= 0 && v < A_COUNT) g_unlocked[v] = 1;
  }
  fclose(f);
}

void ach_update(const GameEvents* ev, int assisted) {
  if (!s) return;
  const uint8_t* ram = s->ram;
  if (assisted) { g_run_on = 0; return; }
  if (ev->new_game) { unlock(A_START); g_run_on = 1; g_run_cont = 0; }
  if (ev->continued) g_run_cont = 1;
  if (ev->scene_start) {
    g_scene_hurt = 0; g_scene_two = 0;
    for (int p = 0; p < 2; p++) if (game_player_active(ram, p)) g_turtles |= 1 << game_turtle(ram, p);
    if (g_turtles == 15) unlock(A_ALL4);
    save();
  }
  if (game_in_play(ram)) {
    if (ev->hurt[0] || ev->hurt[1] || ev->death[0] || ev->death[1]) g_scene_hurt = 1;
    if (ev->two_players) g_scene_two = 1;
  }
  if (ev->scene_clear) {
    int sc = ev->scene_clear;
    if (ev->time_trial) unlock(A_TRIAL);
    else if (sc >= 1 && sc <= 9) unlock(A_S1 + sc - 1);
    if (!g_scene_hurt) unlock(A_UNTOUCHED);
    if (g_scene_two) unlock(A_DUO);
  }
  if (ev->beaten && !ev->time_trial) {
    unlock(A_BEAT);
    if (g_run_on && !g_run_cont) unlock(A_NOCONT);
    g_beaten = 1; save();
  }
}

int ach_total(void) { return A_COUNT; }
int ach_unlocked_count(void) { int n = 0; for (int i = 0; i < A_COUNT; i++) n += g_unlocked[i]; return n; }
int ach_get(int i, const char** t, const char** d) {
  if (i < 0 || i >= A_COUNT) { *t = *d = ""; return 0; }
  *t = k_ach[i].title; *d = k_ach[i].desc;
  return g_unlocked[i];
}
int ach_pop_toast(void) {
  if (!g_toast_n) return -1;
  int id = g_toast[0];
  memmove(g_toast, g_toast + 1, --g_toast_n * sizeof g_toast[0]);
  return id;
}
int ach_game_beaten(void) { return g_beaten; }
