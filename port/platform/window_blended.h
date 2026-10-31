/*
 * port/platform/window_blended.h
 *
 * F12's blended present's frame (rd.h rd_dump_blended_arm): the window arms
 * it after F12's zip and moves the written frame into that zip.  Its own
 * file because window_host.c is at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_BLENDED_H
#define ICO_PLATFORM_WINDOW_BLENDED_H

#include <stddef.h>

/* frame_dump after F12's zip: a result of the last F12 still to collect
   is collected first (window_blended_pump: a written frame goes into the
   last F12's zip), then, with zip not NULL (the zip was written and holds
   the tick before), <dump without ".rddump">-blended.rddump is armed for
   zip; NULL disarms.  1: armed */
int window_blended_arm(const char *dump, const char *zip);
/* photo_pump: while armed, the result once the next present took it: the
   written frame moved into its zip (kept loose, with the reason, when the
   zip refuses it), a failed write's partial file removed, a present at
   alpha 1 named as the tick's own picture; an arm no between-tick present
   took (rd_dump_blended_result -3) ends with no file */
void window_blended_pump(void);

#endif
