/*
 * port/platform/window_loss.h
 *
 * The window's loss dump (the frame of the submission that failed and the
 * one before it, as a zip in the logs folder) and the loss bisect's overlap
 * step.  Their own file because window_host.c is at its file size gate.
 */
#ifndef ICO_PLATFORM_WINDOW_LOSS_H
#define ICO_PLATFORM_WINDOW_LOSS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rd.h"

/* The loss dump (docs/port/RENDER.md, "The loss dump"): the frame the lost
   submission drew and the one replayed before it, with a copy of the log, as
   logs/loss-<date>-<time>.zip; the log names which replay and frame each
   file is and, when the lost frame is no longer held, that it is not and
   which was written instead.  The file name alone into name, "" when none
   was written; the activity file notes the loss either way.  Main thread
   (the render thread drained), or the watchdog's while it is blocked. */
void window_loss_dump(char *name, size_t name_size);

/* The loss bisect's frame number `nth` (0 the lost frame, 1 the one before)
   taken out of the zip to the file zip + suffix with its size and aspect;
   false with line saying why: the zip cannot be opened (with the errno), is
   not a zip, holds no frame, or the frame cannot be written or read by this
   version (dump_zip.h ico_dump_zip_extract_why). */
bool window_bisect_dump_nth(const char *zip, int nth, const char *suffix, char *dump,
                            size_t dumpSize, uint32_t *gw, uint32_t *gh, float *aspect, char *line,
                            size_t size);

/* The bisect's overlap step (rd.h rd_loss_overlap) after the pass-by-pass
   replay b of the lost frame (the file dump, size gw x gh) clean, with the
   bisect's renderer still up: the frame before the lost one is taken out of
   the zip and both are replayed back to back with no wait between them.
   Logs the step and its outcome (or why it did not run); true when it ran
   (ov->result LOST or CLEAN). */
bool window_bisect_overlap(const char *zip, const char *dump, const RdLossBisect *b, uint32_t gw,
                           uint32_t gh, RdLossOverlap *ov);

#endif
