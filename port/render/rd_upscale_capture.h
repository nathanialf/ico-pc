/*
 * port/render/rd_upscale_capture.h
 *
 * The upscaler's pictures for a bug report (issue 203; docs/port/DISPLAY.md
 * "The upscaler's pictures"): with a temporal upscaler in force, F12 and
 * Settings' bug-report row ask for four pictures beside -present.png, taken
 * at the next dispatch of the upscale point (rd_upscale.c
 * rd__upscale_dispatch_at), so a report shows on which side of the upscale
 * a flaw is born:
 *
 *   <stem>-scene.png      SCENE as the upscaler received it (render size,
 *                         the whole post chain drawn, jittered), opaque
 *   <stem>-velocity.png   VELOCITY (render size): x and y in R and G about
 *                         128 (8 levels a texel), the length in B (16 a
 *                         texel), black where no draw wrote a motion vector
 *                         (rd_replay_tool --velocity-dump's mapping)
 *   <stem>-depth.png      SCENE's depth as the upscaler received it, as a
 *                         24-bit step (depth x 16777215): R its top 8 bits
 *                         (a grey picture of the depth), G the next 8, B
 *                         the low 8 (the three give the step exactly);
 *                         where the device cannot read depth back (D24S8 on
 *                         some drivers) not written, the line says so
 *   <stem>-composite.png  COMPOSITE right after the dispatch (display size),
 *                         before the display layer's draws (rd_layer.c)
 *
 * The copies are recorded on the replay's command list after the dispatch,
 * outside any pass, into textures of the capture's own (copy reads only:
 * every picture and the frame's state are unchanged), and read back and
 * written after the submit of the present that followed
 * (rd__upscale_capture_finish, from rd_present.c rd__capture_finish).
 * Pictures wider than the limit given to the arm are halved (a 2 x 2 box)
 * until they fit, so the bug-report zip stays under GitHub's 25 MB.
 *
 * A present that does not dispatch (a keep frame: a fade, a loading
 * picture) leaves the arm waiting for the next one that does, and while it
 * waits the still re-present is refused (rd_reshow.c), so a still camera's
 * between-tick present replays and dispatches.  The arm waits at most
 * RD_UPSCALE_CAPTURE_WAIT_PRESENTS presents and RD_UPSCALE_CAPTURE_WAIT_MS
 * (both must pass), then is dropped with its line.  The window reads the
 * outcome (rd.h rd_upscale_capture_state) and holds the bug-report zip's
 * pictures until it is known (port/platform/upscale_pictures.h).
 */
#ifndef RD_UPSCALE_CAPTURE_H
#define RD_UPSCALE_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rd_internal.h"
#include "rd_upscale.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RD_UPSCALE_CAPTURE_PICTURES 4

/* the four suffixes in the order above ("-scene.png", ...) */
const char *rd__upscale_capture_suffix(int i);

/* The capture's state (the replay tool and the tests read it) */
typedef struct RdUpscaleCaptureStats {
    uint32_t armed;   /* arms since start */
    uint32_t taken;   /* dispatches that copied the pictures */
    uint32_t written; /* captures whose pictures were all written */
    uint32_t failed;  /* captures with a picture not written */
    uint32_t dropped; /* arms dropped: no upscaler in force at the next present */
    uint32_t expired; /* arms dropped: no dispatch within the wait's bound */
    uint32_t w[RD_UPSCALE_CAPTURE_PICTURES], h[RD_UPSCALE_CAPTURE_PICTURES]; /* the last written */
} RdUpscaleCaptureStats;

const RdUpscaleCaptureStats *rd__upscale_capture_stats(void);

/* rd_upscale_dispatch_at's hook: when armed, the copies (cl outside any
 * pass, after the dispatch); depthRec SCENE's record (its depth is copied
 * whole, as the upscaler read it); nothing when not armed */
void rd__upscale_capture_take(RhiCommandList cl, const RdUpscaleIn *in, RdTargetRec *depthRec);
/* After a present's submit: the pictures copied are read back and
 * written (the GPU waited for). Returns 1 when a capture was written, -1
 * when one failed, 0 when none was due. An arm that met a present without
 * an upscaler in force is dropped with its line. */
int rd__upscale_capture_finish(void);
/* the bound of an arm's wait for a dispatch: both must pass */
#define RD_UPSCALE_CAPTURE_WAIT_PRESENTS 60u
#define RD_UPSCALE_CAPTURE_WAIT_MS 2000.0
/* whether an arm waited presents presents over ms milliseconds is over */
bool rd__upscale_capture_wait_over(uint32_t presents, double ms);
/* an arm waits for a dispatch (rd_reshow.c refuses the still re-present
 * meanwhile); read on the thread that builds the presents, so atomic */
bool rd__upscale_capture_armed(void);
/* copies recorded and not yet written (rd_present.c rd__capture_pending:
 * the picture's submit is not handed to the submit thread) */
bool rd__upscale_capture_copied(void);
/* rd__upscale_dispatch_at's note of the frame it dispatched: the post
 * passes, blocks and film noise it held, counted for the 10 s line
 * "upscale point: 10 s: the N frames dispatched held: ..." */
void rd__upscale_note_frame(const RdFrame *f);
/* Drops the capture's textures (rd_shutdown, a new device) */
void rd__upscale_capture_free(void);

#ifdef __cplusplus
}
#endif

#endif
