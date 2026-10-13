#ifndef EXTRAS_H
#define EXTRAS_H
#include <stdint.h>
#include "snes.h"
#include "game.h"
void extras_init(Snes* snes, const char* base_dir);
void extras_frame(const GameEvents* ev, int assisted);   // once per emulated frame; assisted = no records
void extras_shutdown(void);
int extras_stat_lines(char lines[][48], int max);         // "label|value", or a plain heading
void extras_format_time(char* out, int n, uint32_t frames);
// speedrun timer: run time and the current scene's time (or the last split), with colours
int extras_timer_text(char* run, int rn, char* st, int sn, uint32_t* run_col, uint32_t* st_col);
#endif
