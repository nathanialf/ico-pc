/*
 * port/input/input_record.h
 *
 * The pad recording: what the game read from the pad, written as a pad script
 * (pad_script.h), so the headless build replays a player's session with
 * pad_script=<the file>.
 *
 * The sample is taken where the game reads the pad (scePadRead,
 * pad_host.c), keyed by the Main tick the read belongs to, which is the
 * script's own key: Main reads the pad once per tick (sugipon/src/
 * keyInput.c ExecKeyInput -> iosPadDevRead), so the script then returns, at
 * every tick, the value that tick read. A vsync sampler would also write
 * values no tick read, which a tick-keyed script cannot place.
 *
 * File: the caller's header lines (each starting with '#'), then
 *
 *     <tick> <buttons-hex> lx ly rx ry
 *
 * one line per tick whose value differs from the one before (the first
 * against the script's default: released, sticks centred), buttons as four
 * hex digits and the sticks in decimal, and a closing comment
 * "# end at tick T: N lines, C ticks read twice with different values"
 * (C is 0 unless something other than Main reads the pad: the debug
 * motion viewer). Lines are written by ico_input_record_poll after the
 * step that read them and reach the disc at most a second later.
 */
#ifndef ICO_PORT_INPUT_INPUT_RECORD_H
#define ICO_PORT_INPUT_INPUT_RECORD_H

#include <stddef.h>
#include "pad_script.h"

/* One script line with its newline; the length, as snprintf returns it. */
int ico_input_record_format(char *out, size_t size, unsigned int tick, const IcoPadFrame *f);

/* Creates path (replacing it) and writes header (NULL or lines each ending
   in a newline). 0, or -1 with a message on stderr. */
int ico_input_record_open(const char *path, const char *header);
/* 1 while a recording is open (tests). */
int ico_input_record_active(void);
/* The game's read at `tick` (scePadRead); does nothing without a
   recording. No I/O. */
void ico_input_record_sample(unsigned int tick, const IcoPadFrame *f);
/* Once per vsync after the step (main_host.c): writes the samples of the
   ticks before current_tick, and flushes the file once a second. */
void ico_input_record_poll(unsigned int current_tick);
/* Puts the lines written so far on the disc (F12, window_host.c). */
void ico_input_record_flush(void);
/* Writes the last sample and the closing comment and closes the file
   (idempotent; atexit-safe). */
void ico_input_record_close(void);
/* Lines written so far, not counting comments (tests). */
unsigned int ico_input_record_lines(void);

#endif /* ICO_PORT_INPUT_INPUT_RECORD_H */
