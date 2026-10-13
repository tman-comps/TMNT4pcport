// Teenage Mutant Ninja Turtles IV - Turtles in Time (USA): what the port knows about the game's RAM.
// Everything here was found by observing the game (see analysis/tmnt4/ram.txt).
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include <stdbool.h>

// WRAM addresses
enum {
  RAM_FRAMES   = 0x003A,   // 16-bit frame counter
  RAM_MODE     = 0x0032,   // main mode: 1 intro, 2 title, 4 title menu, 5/6 game (select, scenes, ending)
  RAM_SELECT   = 0x0072,   // turtle select screen step (5 = waiting for the choice)
  RAM_TRANSIT  = 0x0068,   // 1 between scenes (select, cutscene, scene card, boss defeated -> next scene)
  RAM_STAGE    = 0x0082,   // scene 0-9: Big Apple 3 AM ... Technodrome (the final scene); 10 = Splinter's lair (ending, Versus)
  RAM_BOSS_HP  = 0x1BC8,   // the boss bar's value (0 = no boss on screen)
  RAM_OBJ_HP   = 0x091A,   // object health, every 0x70 bytes
  RAM_PLAYER   = 0x1A80,   // player blocks, 0x40 bytes each (player 1, player 2)
  P_TURTLE     = 0x1A,     //   0 Leo, 1 Mike, 2 Don, 3 Raph
  P_LIVES      = 0x20,
  P_HP         = 0x22,     //   0x10 = full
};
#define HP_MAX 0x10
#define RAM_P1_LIVES (RAM_PLAYER + P_LIVES)
#define RAM_P1_HP (RAM_PLAYER + P_HP)

static inline int game_mode(const uint8_t* ram) { return ram[RAM_MODE]; }
static inline int game_stage(const uint8_t* ram) { return ram[RAM_STAGE]; }
static inline const uint8_t* game_player(const uint8_t* ram, int p) { return ram + RAM_PLAYER + 0x40 * p; }
static inline int game_lives(const uint8_t* ram, int p) { return game_player(ram, p)[P_LIVES]; }
// health; above HP_MAX while a turtle drops into the scene (treated as full)
static inline int game_hp(const uint8_t* ram, int p) { int h = game_player(ram, p)[P_HP]; return h > HP_MAX ? HP_MAX : h; }
static inline int game_boss_hp(const uint8_t* ram) { return ram[RAM_BOSS_HP]; }
static inline int game_turtle(const uint8_t* ram, int p) { return game_player(ram, p)[P_TURTLE] & 3; }

bool game_in_play(const uint8_t* ram);            // a scene is being played (not select, cards, cutscenes, menus)
bool game_player_active(const uint8_t* ram, int p);
bool game_scene_started(const uint8_t* ram);      // true on the first frame of play of a scene (call every frame)
bool game_at_turtle_select(const uint8_t* ram);
void game_set_start_stage(uint8_t* ram, int stage);
void game_refill_lives(uint8_t* ram, int p);
void game_refill_hp(uint8_t* ram, int p);
bool game_cheat_entered(const uint8_t* ram);     // one of the game's own codes was entered
bool game_time_trial(const uint8_t* ram);        // the game being played is Time Trial

// Fills in lives and health (test drivers and the coverage runs).
void game_god_mode(uint8_t* ram);
// One line describing the game state for logs and the test drivers.
const char* game_describe(const uint8_t* ram);
extern const char* const k_stage_names[10];
// Per-frame events, worked out from RAM by game_events() (call once per emulated frame).
typedef struct {
  bool new_game;        // a new game's first scene started (not a continue)
  int scene_start;      // scene index + 1 whose play just started (0 = none)
  int scene_clear;      // scene index + 1 just cleared (0 = none)
  bool death[2];        // a player lost a life
  bool hurt[2];         // a player lost health
  bool continued;       // a continue was used
  bool beaten;          // the final scene was cleared: the ending starts
  bool two_players;     // both players are in the game
  bool time_trial;      // playing Time Trial
} GameEvents;
const GameEvents* game_events(const uint8_t* ram);
#endif
