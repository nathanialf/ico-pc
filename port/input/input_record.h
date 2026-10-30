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
 * The recording carries the play word only (the buttons the boy and the
 * camera read). The menus' word, which keeps the gamepad's face buttons and
 * d-pad by position, is the same as the play word in a script and is not
 * recorded.
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
/* The header's two frame lines for a native frame width and display aspect:
   "# effective gs_width = W" and "# effective aspect = A" (A with four
   decimals), each with its newline; the length, as snprintf returns it. */
int ico_input_record_frame_lines(char *out, size_t size, int gs_width, float aspect);
/* A recording header line against this run's frame: 1 and a message
   ("gs_width = 688; this run has 512") in msg when line is a gs_width or
   aspect line that differs, else 0. */
int ico_input_record_frame_diff(const char *line, int gs_width, float aspect, char *msg,
                                size_t size);
/* The native frame width in force at `tick` (once per vsync, main_host.c):
   the first call only remembers it (the header carries it); a change writes
   the comment "# tick N gs_width W". Does nothing without a recording. */
void ico_input_record_note_width(unsigned int tick, int gs_width);
/* Lines written so far, not counting comments (tests). */
unsigned int ico_input_record_lines(void);
/* A save state's load (port/game/state_host.c) put the Main tick back to
   `tick`: the read held is written, then the comment "# state loaded slot
   N (Main tick T)"; the ticks after it count from T again, so a recording
   with a load in it replays only up to that line.  Nothing without a
   recording. */
void ico_input_record_state_loaded(int slot, unsigned int tick);

#endif /* ICO_PORT_INPUT_INPUT_RECORD_H */
