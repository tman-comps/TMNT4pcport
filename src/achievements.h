#ifndef ACHIEVEMENTS_H
#define ACHIEVEMENTS_H
#include "snes.h"
#include "game.h"
void ach_init(Snes* snes, const char* base_dir);
void ach_update(const GameEvents* ev, int assisted);   // once per emulated frame
int ach_total(void);
int ach_unlocked_count(void);
int ach_get(int i, const char** title, const char** desc);   // returns 1 if unlocked
int ach_pop_toast(void);                // next newly unlocked id, or -1
int ach_game_beaten(void);              // the game has been beaten at least once (unlocks the extras)
#endif
