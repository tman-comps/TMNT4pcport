#ifndef CRASHLOG_H
#define CRASHLOG_H
#include "snes.h"
#define CRASHLOG_BUILD "build 1"
void crashlog_init(Snes* snes, const char* dir, const char* (*settings)(void));
void crashlog_event(const char* fmt, ...);
void crashlog_frame(void);     // once per emulated frame
void crashlog_report(void);    // "Report a problem" from the menu
const char* crashlog_last_report(void);   // file name (without .txt) of the last report written
#endif
