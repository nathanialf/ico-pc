/* rd_loss_pick.h: which frames a device-loss dump writes (issue 179).
 *
 * The loss dump is for the frame whose submission failed and the one drawn
 * before it.  With the render and submit threads on, the game thread records
 * ahead of the GPU: by the time the loss is seen the renderer's ring holds
 * frames newer than the lost one (v0.11.3's reporter: frame 129 lost, the
 * dump held 132 and 131).  The renderer therefore keeps a memory copy of a
 * frame the GPU may not have finished when the ring reuses its slot
 * (rd_dump.c rd__loss_keep_recycle), and this file decides, from what the
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

/* Whether a frame of replay `replay` may still be on the GPU when the
 * ring reuses its slot: the backend's first unfinished submission is of that
 * replay or a later one (openReplay 0: none is open, the frame is done;
 * replay 0: not in the pass ring's table, older than anything open) */
bool rd_loss_keep_needed(uint32_t replay, uint32_t openReplay);

/* Whether a copy kept for replay `replay` can go: no submission is open, or
 * the first open one is two replays after it (a copy stays while the replay
 * before the open one may still be wanted as the frame before the lost one) */
bool rd_loss_keep_release(uint32_t replay, uint32_t openReplay);

/* The throttle on copies: after the first RD_LOSS_KEEP_FREE frames copies
 * are made while they stay under a quarter of the frames that came to be
 * recycled (a backend whose `finished` count lags would otherwise ask for
 * one every frame); taken and seen count up to now (this one not yet) */
#define RD_LOSS_KEEP_FREE 8u
bool rd_loss_keep_allowed(uint64_t taken, uint64_t seen);

/* The slot to take for a new copy among `n` (frames[i] the number held in
 * slot i, 0 empty): an empty one, else the oldest number */
uint32_t rd_loss_keep_slot(const uint32_t *frames, uint32_t n);

#endif /* PORT_RENDER_RD_LOSS_PICK_H */
