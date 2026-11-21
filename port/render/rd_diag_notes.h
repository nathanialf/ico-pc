/* rd_diag_notes.h: two of the renderer's log lines made whole (issue 179's
 * v0.11.13 log, docs/port/RENDER.md's diagnostics):
 *
 * - The display line once more when the frame's width first changes.  The
 *   reporter's run logged "display: scene 1x1 (512x512) ... frame 512 GS
 *   wide" at its start and never again, while every pass of its blocks was
 *   688x512: the game set the frame's width after the line (rd_core.c
 *   rd_reset_scene runs rd__apply_display, whose line is written only when
 *   a scale, the height or an option changed).  rd_present.c asks
 *   rd_width_note at each rd__apply_display: the line is written when it
 *   would be anyway and once, the first time the width differs from the one
 *   it last named, with " (written again: the frame's width changed from
 *   512)" after the frame's words.
 *
 * - The upload ring's grow line names the ring it replaces: when it was
 *   made and when the slot last used it, for how much ("the upload ring of
 *   frame slot 1 grows to 64 MB (this frame needs 32 MB; the 32 MB ring it
 *   replaces was made 41.2 s before, last used 0.08 s before for 31 MB)").
 *   The estimate is not changed (issue 87).
 *
 * Pure (port/render/test/rd_diag_notes_test.c): the callers keep the
 * notes and pass the clock (rd__now_ms) and sizes. */
#ifndef RD_DIAG_NOTES_H
#define RD_DIAG_NOTES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RdWidthNote {
    uint32_t shown; /* the width the display line last named (0: none yet) */
    uint32_t from;  /* the width it changed from, when written again */
    bool again;     /* the change's line written */
} RdWidthNote;

/* Whether the display line is written now for the frame's width gw:
   logging (it is written anyway), or the first time gw differs from the
   width the line last named.  The width is kept when written. */
bool rd_width_note(RdWidthNote *n, uint32_t gw, bool logging);
/* " (written again: the frame's width changed from 512)" when the line
   is written for the width alone (logging false at the rd_width_note that
   answered true), "" else; the length written */
size_t rd_width_note_text(const RdWidthNote *n, bool logging, char *out, size_t cap);

typedef struct RdRingNote {
    bool made;         /* a ring was made on the slot */
    double madeMs;     /* when (rd__now_ms) */
    bool used;         /* the slot asked for its ring since */
    double usedMs;     /* the last time */
    uint64_t usedNeed; /* and the bytes it needed then */
} RdRingNote;

void rd_ring_note_made(RdRingNote *n, double nowMs);
void rd_ring_note_use(RdRingNote *n, double nowMs, uint64_t need);
/* the grow line's words for the ring old (its note) of oldCap bytes at
   nowMs: "; the 32 MB ring it replaces was made 41.2 s before, last used
   0.08 s before for 31 MB" ("; never used before" without a use, "; no
   ring before" without one); the length written */
size_t rd_ring_note_text(const RdRingNote *old, uint64_t oldCap, double nowMs, char *out,
                         size_t cap);

#endif
