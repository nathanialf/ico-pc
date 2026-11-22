/* rd_loss_pick.h: which frames a device-loss dump writes (issue 179).
 *
 * The loss dump is for the frame whose submission failed and the one drawn
 * before it.  With the render and submit threads on, the game thread records
 * ahead of the GPU: by the time the loss is seen the renderer's ring holds
 * frames newer than the lost one (v0.11.3's reporter: frame 129 lost, the
 * dump held 132 and 131).  The renderer therefore keeps a memory copy of a
 * frame the GPU may not have finished when the ring reuses its slot, while
 * a slot wait has stalled (rd_dump.c rd__loss_keep_recycle; in steady play
 * none is made, and a loss with no stall before it finds no copy: the pick
 * falls back to the frames held), and this file decides, from what the
 * backend says about its last submissions and which frames are held, which
 * frame was the lost one and which two are written.
 *
 * Pure: no device, no renderer state (rd_loss_pick_test). */
#ifndef PORT_RENDER_RD_LOSS_PICK_H
#define PORT_RENDER_RD_LOSS_PICK_H

#include <stdbool.h>
#include <stdint.h>

/* How the lost frame was found, best first */
typedef enum RdLossHow {
    RD_LOSS_HOW_NONE = 0,
    RD_LOSS_HOW_WAIT,   /* the submission whose wait failed (the loss report's line) */
    RD_LOSS_HOW_OPEN,   /* the first submission not seen finished */
    RD_LOSS_HOW_NEXT,   /* a picture recorded after the newest numbered submission: its
                         * submit failed (it has no number yet) */
    RD_LOSS_HOW_SIDE,   /* nothing was recorded after the newest numbered submission: the
                         * submit that failed was none of a picture's (a capture, a
                         * readback, a check), and the newest numbered picture is the frame */
    RD_LOSS_HOW_NEWEST, /* nothing is known of the submissions: the newest frame held */
} RdLossHow;

typedef struct RdLossInput {
    /* the backend's tags (rhi.h RhiGpuProgress), 0 when it names none */
    uint32_t waitReplay, waitFrame;
    uint32_t openReplay, openFrame;
    uint32_t newestReplay, newestFrame;
    /* the newest replay the pass ring lists (recorded, submitted or not)
     * and its frame, 0 when it lists none */
    uint32_t ringReplay, ringFrame;
    /* the frames of the replays the pass ring still lists, oldest first
     * (replay order); may be NULL with a count of 0 */
    const uint32_t *replayed;
    uint32_t replayedCount;
    /* the frame numbers held, ascending and without repeats: the ring's
     * closed frames and the memory copies */
    const uint32_t *held;
    uint32_t heldCount;
} RdLossInput;

typedef struct RdLossChoice {
    RdLossHow how;
    uint32_t replay;      /* the lost submission's replay (0: not known) */
    uint32_t frame;       /* the lost frame (0: none known) */
    uint32_t before;      /* the frame replayed before it (0: none known) */
    bool frameHeld;       /* `frame` is held */
    bool beforeHeld;      /* `before` is held */
    uint32_t writeFrame;  /* what to write as the lost frame: `frame` when held, else the
                           * held frame nearest it (0: nothing is held) */
    uint32_t writeBefore; /* ... as the frame before: `before` when held and writeFrame is
                           * `frame`, else the newest held frame below writeFrame (0: none) */
} RdLossChoice;

void rd_loss_choose(const RdLossInput *in, RdLossChoice *out);

/* Whether frame `frame` may still be on the GPU when the ring reuses its
 * slot: a picture of it was handed to a replay (sent: rd__frame_sent, set at
 * the hand-over, rd_thread.c) and the backend's first unfinished
 * submission is of that frame or a later one (openFrame 0: none is open,
 * the frame is done; frame 0: none).  Frames, not replays: frame numbers
 * grow with the replays that draw them, and the backend's tags carry both
 * (RhiGpuProgress.openFrame), so the game thread needs no read of the pass
 * ring's table, which the render thread writes.  A frame never sent (with
 * Frame rate Original, a picture the gate left out while the render thread
 * was busy) was never on the GPU and is never a submission's frame: issue
 * 179's v0.11.13 log copied 8 such frames, each newer than the newest
 * submission (its line 782: frame 616, the newest frame 613) */
bool rd_loss_keep_needed(uint32_t frame, bool sent, uint32_t openFrame);

/* Whether a copy kept for frame `frame` can go: no submission is open, or
 * the first open one is two frames after it (a copy stays while the frame
 * before the open one may still be wanted as the frame before the lost one) */
bool rd_loss_keep_release(uint32_t frame, uint32_t openFrame);

/* Whether the slot wait has stalled: a wait in progress (waitingFor, as
 * RhiGpuProgress names it) blocked at least RD_LOSS_KEEP_STALL_NS
 * (waitingNs).  A copy is made only then: the losses of issues 179 and 211
 * came after such waits.  100 ms, not 50: one copy is not cheap
 * (rd_dump.c dumpFrame: the game thread waits for the reads of the
 * pictures handed over (until v0.11.13 it drained the render and submit
 * threads, the stalled picture's GPU work included), then serialises the
 * frame, about 7 MB at 2x, into a block the dump worker made or one
 * allocation of its measured size (rd_loss_keep_block.h; until v0.11.14 a
 * buffer grown by realloc from 1 MiB, four allocations and copies on the
 * way), plus three 16 KB sets), and the
 * Mali-G52's ordinary slot waits average 35-55 ms and
 * reach 60-250 ms in most 10 s blocks (issue 179's v0.11.4 log), so at
 * 50 ms copies would be made in steady play there (4 in every 64 frames,
 * each a hitch of a whole picture or more).  Issue 179's loss had its
 * wait fail after about 0.1 s: a copy may be missed there, a hitch-free
 * steady play is kept */
#define RD_LOSS_KEEP_STALL_NS 100000000ull
bool rd_loss_keep_stalled(uint64_t waitingFor, uint64_t waitingNs);

/* The stall with the head's time (RhiGpuProgress.headKnown / headNs): the
 * time counted against RD_LOSS_KEEP_STALL_NS is the wait's own, at most
 * the time since the GPU was last seen finishing a submission, so a wait
 * that spent its first part queued behind the picture before (two pictures
 * in flight: issue 179's v0.11.14 title and menus, slot waits of 100-110
 * ms, every one copied at 27-49 ms on the game thread) counts only the time
 * the GPU's current picture has run.  A picture that is itself stuck (a
 * hang: nothing seen finished for 100 ms or more) still counts in full.
 * headKnown false: rd_loss_keep_stalled's rule, unchanged */
uint64_t rd_loss_keep_counted_ns(uint64_t waitingNs, bool headKnown, uint64_t headNs);
bool rd_loss_keep_stalled_head(uint64_t waitingFor, uint64_t waitingNs, bool headKnown,
                               uint64_t headNs);

/* Whether a wait (RhiGpuProgress.waitingFor, the number it blocks on) is
 * another than the one counted last (*last, updated): the 10 s line counts
 * each wait once however many recycles see it (review K2); 0 (no wait)
 * never */
bool rd_loss_keep_wait_new(uint64_t *last, uint64_t waitingFor);

/* The throttle on copies, windowed: at most RD_LOSS_KEEP_PER_WINDOW
 * attempts in any RD_LOSS_KEEP_WINDOW recycled frames, every attempt
 * counted (a copy kept, one too big, one that failed); after a copy too big
 * (over RD_LOSS_KEEP_BYTES the build stops there) none is tried for
 * RD_LOSS_KEEP_BIG_PAUSE recycled frames, so an oversized scene is not built
 * again frame after frame.  seen: the recycled frames so far, this one
 * counted */
#define RD_LOSS_KEEP_WINDOW 64u
#define RD_LOSS_KEEP_PER_WINDOW 4u
#define RD_LOSS_KEEP_BIG_PAUSE 600u

typedef struct RdLossThrottle {
    uint64_t at[RD_LOSS_KEEP_PER_WINDOW]; /* the last attempts' `seen` */
    uint32_t next;                        /* the oldest of them */
    uint64_t attempts;
    uint64_t pauseUntil; /* no attempt while seen < it */
} RdLossThrottle;

/* What the recycle of a frame does (rd_loss_keep_decide), in the order
 * decided */
typedef enum RdLossKeep {
    RD_LOSS_KEEP_NOT_OPEN = 0, /* the GPU is past the frame: nothing to keep */
    RD_LOSS_KEEP_NO_STALL,     /* open, but no wait has stalled: not copied */
    RD_LOSS_KEEP_NOT_HEAD,     /* open behind an older open frame, the wait blocked
                                * RD_LOSS_KEEP_STALL_NS or more, but the GPU's current
                                * picture has run less than that (the wait was queued
                                * behind the picture before): not copied
                                * (rd_loss_keep_decide_head only) */
    RD_LOSS_KEEP_NOT_SENT,     /* open and stalled, but no picture of the frame was handed
                                * to a replay: nothing of it is on the GPU, not copied (no
                                * attempt counted) */
    RD_LOSS_KEEP_PAUSED,       /* a copy was too big lately: not tried */
    RD_LOSS_KEEP_THROTTLED,    /* the window's attempts are used */
    RD_LOSS_KEEP_TAKE,         /* copy it (the attempt is counted now) */
} RdLossKeep;

RdLossKeep rd_loss_keep_decide(RdLossThrottle *t, uint64_t seen, uint32_t frame, bool sent,
                               uint32_t openFrame, uint64_t waitingFor, uint64_t waitingNs);
/* The same with the head's time (rd_loss_keep_stalled_head) for a frame
 * queued behind an older unfinished one (frame above openFrame: issue 179's
 * v0.11.14 title copies, each of a frame one to three after the first open
 * one, its log lines 555-765): a wait that stalled by rd_loss_keep_stalled's
 * rule but not by the head's is RD_LOSS_KEEP_NOT_HEAD (no attempt counted).
 * The frame that is the first open one (frame == openFrame, the GPU's
 * current picture, the one a hang holds) decides by the whole wait as
 * rd_loss_keep_decide does, so a hang that begins once the picture before
 * has finished, with the wait already past 100 ms, still leaves its copy.
 * headKnown false decides as rd_loss_keep_decide */
RdLossKeep rd_loss_keep_decide_head(RdLossThrottle *t, uint64_t seen, uint32_t frame, bool sent,
                                    uint32_t openFrame, uint64_t waitingFor, uint64_t waitingNs,
                                    bool headKnown, uint64_t headNs);
/* The attempt's outcome: big (over the bound) pauses the attempts */
void rd_loss_keep_big(RdLossThrottle *t, uint64_t seen);

/* The slot to take for a new copy among `n` (frames[i] the number held in
 * slot i, 0 empty): an empty one, else the oldest number */
uint32_t rd_loss_keep_slot(const uint32_t *frames, uint32_t n);

#endif /* PORT_RENDER_RD_LOSS_PICK_H */
