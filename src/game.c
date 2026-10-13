// Game-specific RAM knowledge (see game.h and analysis/tmnt4/ram.txt).
#include <stdio.h>
#include "game.h"

const char* const k_stage_names[10] = {
  "BIG APPLE, 3 AM", "ALLEYCAT BLUES", "SEWER SURFIN'", "TECHNODROME", "PREHISTORIC TURTLESAURUS",
  "SKULL AND CROSSBONES", "BURY MY SHELL AT WOUNDED KNEE", "NEON NIGHT-RIDERS", "STARBASE", "THE FINAL SHELL SHOCK"};

// The transition flag also reads 0 for a few frames when the turtle select screen starts, so play
// only counts once it has lasted 8 frames (timed with the game's frame counter at $3A).
bool game_in_play(const uint8_t* ram) {
  static uint16_t since; static int was;
  int raw = ram[RAM_MODE] == 6 && ram[RAM_TRANSIT] == 0;
  uint16_t now = ram[RAM_FRAMES] | ram[RAM_FRAMES + 1] << 8;
  if (raw && !was) since = now;
  was = raw;
  return raw && (uint16_t)(now - since) >= 8;
}
bool game_player_active(const uint8_t* ram, int p) {
  const uint8_t* b = game_player(ram, p);
  return b[P_LIVES] || b[P_HP];
}
bool game_scene_started(const uint8_t* ram) {
  static int prev = 1;
  int now = game_in_play(ram);
  bool r = now && !prev;
  prev = now;
  return r;
}
bool game_at_turtle_select(const uint8_t* ram) { return ram[RAM_MODE] == 6 && ram[RAM_SELECT] == 5 && ram[RAM_TRANSIT]; }
void game_set_start_stage(uint8_t* ram, int stage) { ram[RAM_STAGE] = (uint8_t)stage; }
// (call only during play, see game_god_mode)
void game_refill_lives(uint8_t* ram, int p) { uint8_t* b = ram + RAM_PLAYER + 0x40 * p; if (b[P_LIVES] < 3) b[P_LIVES] = 3; }
void game_refill_hp(uint8_t* ram, int p) { uint8_t* b = ram + RAM_PLAYER + 0x40 * p; if (b[P_HP] && b[P_HP] < HP_MAX) b[P_HP] = HP_MAX; }
bool game_cheat_entered(const uint8_t* ram) { (void)ram; return false; }
bool game_time_trial(const uint8_t* ram) { (void)ram; return false; }


// Only during play: the scene-clear tally counts the remaining health down, and refilling it then
// would make the tally run forever.
void game_god_mode(uint8_t* ram) {
  if (!game_in_play(ram)) return;
  for (int p = 0; p < 2; p++) {
    uint8_t* b = ram + RAM_PLAYER + 0x40 * p;
    if (!b[P_LIVES] && !b[P_HP]) continue;
    if (b[P_LIVES] < 2) b[P_LIVES] = 3;
    if (b[P_HP] < 6) b[P_HP] = HP_MAX;
  }
}

const char* game_describe(const uint8_t* ram) {
  static char s[160];
  snprintf(s, sizeof s, "mode %d stage %d play %d p1 turtle %d lives %d hp %d, p2 turtle %d lives %d hp %d",
           ram[RAM_MODE], ram[RAM_STAGE] + 1, game_in_play(ram), game_turtle(ram, 0), game_lives(ram, 0), game_hp(ram, 0),
           game_turtle(ram, 1), game_lives(ram, 1), game_hp(ram, 1));
  return s;
}

// ---- events
static GameEvents ev;
static int e_prev_mode = -1, e_prev_play = 0, e_prev_stage = -1, e_from_title = 0;
static int e_prev_lives[2] = {-1, -1}, e_prev_hp[2] = {-1, -1}, e_played_stage = -1;
const GameEvents* game_events(const uint8_t* ram) {
  bool tt = ev.time_trial;
  ev = (GameEvents){0};
  ev.time_trial = tt;
  int mode = ram[RAM_MODE], play = game_in_play(ram), st = ram[RAM_STAGE];
  if (mode != 6) { e_played_stage = -1; }
  if (mode <= 4 && mode >= 1) e_from_title = 1;
  if (play && !e_prev_play) {
    if (st >= 10) { ev.time_trial = true; e_from_title = 0; }   // Versus (fought in Splinter's lair): not a game
    else {
      ev.scene_start = st + 1;
      if (e_from_title) { ev.new_game = true; e_from_title = 0; ev.time_trial = game_time_trial(ram); }
      e_played_stage = st;
    }
  }
  // a scene is cleared when play stops and the scene number moves on to the next one
  if (mode == 6 && e_prev_stage >= 0 && st == e_prev_stage + 1 && e_played_stage == e_prev_stage) ev.scene_clear = st;
  if (ev.scene_clear == 10) ev.beaten = true;   // scene 10 cleared: the ending (in Splinter's lair, scene index 10)
  for (int p = 0; p < 2; p++) {
    int l = game_lives(ram, p), h = game_hp(ram, p);
    if (play && e_prev_lives[p] >= 0) {
      if (l < e_prev_lives[p]) ev.death[p] = true;
      else if (h < e_prev_hp[p] && e_prev_hp[p] > 0) ev.hurt[p] = true;
      if (e_prev_lives[p] == 0 && l > 0 && !e_from_title) ev.continued = true;
    }
    e_prev_lives[p] = mode == 6 ? l : -1; e_prev_hp[p] = play ? h : -1;
  }
  ev.two_players = play && game_player_active(ram, 0) && game_player_active(ram, 1);
  e_prev_mode = mode; e_prev_play = play; e_prev_stage = mode == 6 ? st : -1;
  return &ev;
}
