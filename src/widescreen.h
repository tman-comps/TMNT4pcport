#ifndef WIDESCREEN_H
#define WIDESCREEN_H
#include "snes.h"
void ws_init(Snes* snes);
void ws_update(int want_extra);   // before each frame: how many extra columns per side the player wants
int ws_active_extra(void);
void ws_state_loaded(void);     // call after loading a state (save state, rewind, restart)        // columns per side that hold real picture this frame (0 = pillarbox)
#endif
