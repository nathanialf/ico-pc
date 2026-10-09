/*
 * port/platform/trace_host.h
 *
 * The Main tick count, the heartbeat's state line and the --trace file.
 *
 * Main tick: one pass of the game's Main loop (common/src/main.c, Main,
 * from iosThreadSleep to frameReady = 1). The game reports each pass by
 * calling ico_host_main_tick() at the end of it. Ticks count from 0: while
 * Main runs tick t, ico_host_main_ticks() is t; after it, t + 1. The pad
 * script (port/input/pad_script.h) and the trace use this numbering.
 *
 * Trace file: a header line starting with '#' naming the columns, then one
 * line per Main tick, written after the host step (vsync) in which the tick
 * finished, when every game thread waits again:
 *
 *   tick vsync stage sys0 sys1 gameover gf0 .. gf12 save
 *
 *   tick      the Main tick, from 0 (decimal)
 *   vsync     ico_host_vsync_count() after that step (decimal)
 *   stage     stage_no (common/src/main.c:428, int)
 *   sys0 sys1 systemStatus[0] (1 PAL 50 Hz, 0 NTSC) and [1] (the frame
 *             step) (main.c:37, int[12])
 *   gameover  gameover_flag (main.c:493, int)
 *   gf0..gf12 the story flags, 32 per word, gfN bit b = gflagChk(32N + b)
 *             (script/src/gflag.c:58-61; the bitmap is the static
 *             gflags[50], gflag.c:14, 400 flags, so gf12 holds flags
 *             384..399 in its low 16 bits); 8-digit hex. gf0..gf7 are the
 *             first 8 words of gflags (bytes 0..31, little-endian); gf8..gf12
 *             add the rest, so flag 382 (new game, layout_action.c:671) is
 *             gf11 bit 30 (0x40000000)
 *   save      FNV-1a 32-bit of gameSysMainSaveBuff (common/src/gamesys.c:79,
 *             char[25596], the CheckPoint save image), 8-digit hex
 */
#ifndef ICO_PLATFORM_TRACE_HOST_H
#define ICO_PLATFORM_TRACE_HOST_H

/* The game's end-of-Main-tick hook (one call per Main pass). Advances the
   tick count and the pad script's tick. */
void ico_host_main_tick(void);
/* Main ticks completed so far. */
unsigned int ico_host_main_ticks(void);
/* Opens the trace file and writes its header. 0, or -1 (message on
   stderr). */
int ico_trace_open(const char *path);
/* After each ico_host_step: writes a line for each Main tick finished since
   the last call (normally zero or one). */
void ico_trace_poll(void);
/* Flushes and closes the trace (idempotent). */
void ico_trace_close(void);
/* The game's stage_no, for the host's exit summary. */
int ico_host_stage_no(void);
/* The heartbeat's line of game state (diag_host.h, IcoDiagStatusFn). */
#include <stddef.h>
void ico_host_status(char *out, size_t size);

#endif /* ICO_PLATFORM_TRACE_HOST_H */
