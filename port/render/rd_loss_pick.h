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
 * slot: the backend's first unfinished submission is of that frame or a
 * later one (openFrame 0: none is open, the frame is done; frame 0: none).
 * Frames, not replays: frame numbers grow with the replays that draw them,
 * and the backend's tags carry both (RhiGpuProgress.openFrame), so the
 * game thread needs no read of the pass ring's table, which the render
 * thread writes */
bool rd_loss_keep_needed(uint32_t frame, uint32_t openFrame);

/* Whether a copy kept for frame `frame` can go: no submission is open, or
 * the first open one is two frames after it (a copy stays while the frame
 * before the open one may still be wanted as the frame before the lost one) */
bool rd_loss_keep_release(uint32_t frame, uint32_t openFrame);

/* Whether the slot wait has stalled: a wait in progress (waitingFor, as
 * RhiGpuProgress names it) blocked at least RD_LOSS_KEEP_STALL_NS
 * (waitingNs).  A copy is made only then: the losses of issues 179 and 211
 * came after such waits, and issue 179's had its wait fail after about
 * 0.1 s, so a gate at 100 ms could miss the copy altogether.  50 ms: the
 * Pixel 10 Pro Fold's longest slot waits in GPU-bound play were 26.9 to
 * 43.2 ms a 10 s block (v0.11.4 log), and a steady stretch past 50 ms is
 * bounded by the throttle below (4 attempts in any 64 recycled frames) */
#define RD_LOSS_KEEP_STALL_NS 50000000ull
bool rd_loss_keep_stalled(uint64_t waitingFor, uint64_t waitingNs);

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

/* What the recycle of a frame does (rd_loss_keep_decide) */
typedef enum RdLossKeep {
    RD_LOSS_KEEP_NOT_OPEN = 0, /* the GPU is past the frame: nothing to keep */
    RD_LOSS_KEEP_NO_STALL,     /* open, but no wait has stalled: not copied */
    RD_LOSS_KEEP_PAUSED,       /* a copy was too big lately: not tried */
    RD_LOSS_KEEP_THROTTLED,    /* the window's attempts are used */
    RD_LOSS_KEEP_TAKE,         /* copy it (the attempt is counted now) */
} RdLossKeep;

RdLossKeep rd_loss_keep_decide(RdLossThrottle *t, uint64_t seen, uint32_t frame, uint32_t openFrame,
                               uint64_t waitingFor, uint64_t waitingNs);
/* The attempt's outcome: big (over the bound) pauses the attempts */
void rd_loss_keep_big(RdLossThrottle *t, uint64_t seen);

/* The slot to take for a new copy among `n` (frames[i] the number held in
 * slot i, 0 empty): an empty one, else the oldest number */
uint32_t rd_loss_keep_slot(const uint32_t *frames, uint32_t n);

#endif /* PORT_RENDER_RD_LOSS_PICK_H */
