/* rd_made_outside.h: what is made in play outside the renderer's key cache
 * (rd_made_outside.c; docs/port/RENDER_POST.md "Start-up").
 *
 * The slow-replay line counts the pipelines and buffers a replay created
 * (`3 pipelines created, 3 allocations, 1 buffers ...`), and a pipeline of
 * the key cache made on first use has its own line (rd_pipeline.c
 * firstUseLine), but the rest had no name: issue 179's Mali made 5
 * pipelines at replay 1 and issue 211's Adreno 3 at replay 47 with no
 * first-use line, and one buffer of the first 3D scene (frame 126) was not
 * the upload ring by its size (the v0.11.4 start-up hand-back's inventory,
 * items 3 and 11).  Once the window says play begins
 * (rd_made_outside_watch, after the start-up set and its allocations), every
 * pipeline made by rhi_create_pipeline outside the key cache and every
 * buffer made by rhi_create_buffer is named in a line (the RHI's create
 * hook, rhi.h rhi_set_create_hook):
 *   rd: made in play outside the key cache in replay R (WHERE): pipeline
 *   "NAME" (its 1st; pipelines so far 3, of 2 names)
 *   rd: made in play outside the key cache in replay R (WHERE): buffer
 *   "rd ring" of 8192 KB (its 2nd; buffers so far 4, of 3 names)
 * the first of each name always, then that name's 8 first and every 100th
 * (names past RD_MADE_NAMES share one count under the same rule); the
 * run's end line lists each name with its count.  Textures are not named
 * here: the stage's own textures (the texture cache) and the work targets
 * (their own `named targets made` line) are what play makes of them.
 * Pipelines made in a batch (the start-up set, the background finisher, the
 * skin check's background compiles) have their own lines and are not here. */
#ifndef PORT_RENDER_RD_MADE_OUTSIDE_H
#define PORT_RENDER_RD_MADE_OUTSIDE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RD_MADE_NAMES 32u

/* the window, once the start-up's work is done (window_pipe_finish.c
   window_pipe_skin_check): from here every creation is play's */
void rd_made_outside_watch(void);

/* rd_init after the device: the RHI's create hook installed (no line
   until rd_made_outside_watch) */
void rd__made_outside_init(void);
/* rd_shutdown before the device goes: the run's line (when anything was
   made in play), the hook removed, the watch off */
void rd__made_outside_shutdown(void);
/* rd_pipeline.c around a key cache pipeline's creation (its own line,
   firstUseLine): not named here */
void rd__made_outside_key_begin(void);
void rd__made_outside_key_end(void);

/* pure (rd_made_outside_test): whether the nth creation of a name gets a
   line (the first 8, then every 100th) */
bool rd_made_outside_due(uint64_t n);
/* the line's text after "rd: ": kind RHI_CREATE_PIPELINE or _BUFFER, the
   name (NULL: "unnamed"), a buffer's bytes, the replay and where in it, the
   name's count (nth) and the kind's count and names */
size_t rd_made_outside_text(char *out, size_t size, int kind, const char *name, uint64_t bytes,
                            uint32_t replay, const char *where, uint64_t nth, uint64_t kindCount,
                            uint32_t kindNames);

#endif
