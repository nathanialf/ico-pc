/*
 * port/render/rd_icon_hold.h
 *
 * The title's last picture held on screen while the icon capture's frames
 * are drawn (rd.h rd_icon_hold; the capture: port/game/title_icon.c).
 *
 * The icon pass (rd.h rd_set_icon_pass) draws the title's letter alone over
 * a black, then a white clear; those frames are rendered and presented as
 * any other (the capture reads the output after the frame, rd_capture_shot),
 * but the player must not see them.  While a hold is asked for:
 *
 *   armed     every present whose picture holds none of the pass copies the
 *             output (before the overlay, where the capture copies it) into
 *             the hold texture: the last such copy is the picture the
 *             player saw last before the pass's first frame
 *   showing   a present whose picture holds the pass (rd__icon_hold_tainted)
 *             is drawn as usual (the capture's copy taken from it), then the
 *             held copy is drawn over the whole output, one texel a pixel,
 *             before the overlay (the popups and the touch controls stay
 *             live); with no copy held (no present between the hold's
 *             start and the pass: the window minimised) the output is
 *             cleared to black instead, the curtain the capture used before
 *   done      the first present past the pass's frames shows the live
 *             picture again and the copy is freed; the release
 *             (rd_icon_hold(false)) frees it too
 *
 * Only the output after the capture's copy changes: the frames, their
 * replay and what rd_capture_shot reads are the same with and without a
 * hold.  One output-sized RGBA8 or BGRA8 texture while a hold is armed or
 * showing (3840 x 2160: 33 MB).
 *
 * Threads: rd_icon_hold and rd__icon_hold_pass run on the game thread,
 * rd__icon_hold_record on the presenting thread (the render thread with
 * one); the words between them are atomics.
 */
#ifndef PORT_RENDER_RD_ICON_HOLD_H
#define PORT_RENDER_RD_ICON_HOLD_H

#include <stdbool.h>
#include <stdint.h>

#include "rhi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* no frame of the pass yet (rd__icon_hold_tainted's first) */
#define RD_ICON_HOLD_NONE 0xFFFFFFFFu

/* rd_frame.c rd_set_icon_pass: the pass on (frame: the newest frame number
   given out, the frame open now or the last begun) or off (frame: the same,
   the last frame that may hold it) */
void rd__icon_hold_pass(bool on, uint32_t frame);

/* Whether a present of frame (alpha under 1: blended with frame - 1) shows
   any of the pass, whose frames are first .. last (last RD_ICON_HOLD_NONE
   while the pass is on; first RD_ICON_HOLD_NONE before it came on): frame
   at or past first, and not past the pass (title_icon.h
   ico_title_icon_present_clean: above last, and a blend's older frame above
   it too).  Pure; the test's */
bool rd__icon_hold_tainted(uint32_t frame, float alpha, uint32_t first, uint32_t last);

/* rd_present.c rd__present_record, after the capture's copy and before the
   overlay: out (outState RENDER_TARGET here and on return), its format and
   size; open: the output's pass is open (ended before a copy or a draw and
   opened again, loading, after it) */
void rd__icon_hold_record(RhiCommandList cl, RhiTexture out, RhiState *outState, RhiFormat fmt,
                          uint32_t w, uint32_t h, bool open);

/* rd__present_shutdown: the hold's texture freed, the hold forgotten */
void rd__icon_hold_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_RENDER_RD_ICON_HOLD_H */
