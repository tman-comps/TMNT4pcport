#ifndef MSU_H
#define MSU_H
#include <stdint.h>
#include "snes.h"
int msu_init(Snes* snes, const char* base_dir, const char* folder);   // loads <folder>/msu.txt; returns songs that have a PCM
void msu_enable(int on);
int msu_playing(void);
int msu_current_song(void);       // last song the game asked for (-1 = none)
void msu_mix(int16_t* out, int n, int out_rate, int volume_pct);
#endif
