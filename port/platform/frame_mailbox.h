/*
 * port/platform/frame_mailbox.h
 *
 * What the game thread and the window's render thread hand each other
 * (port/platform/render_thread.c), as plain data and the rules over it, free
 * of SDL and of any lock so a unit test drives both sides under a fake
 * clock (frame_mailbox_test).  The caller holds the one lock around every
 * call (render_thread.c's mutex).
 *
 *   the latest frame   the game thread publishes each closed frame
 *                      {number, slot, closeAt, tickAt, tickPrev}; the newest
 *                      wins, one the render thread did not take yet is
 *                      replaced (counted)
 *   the lock grid      the vsync lock's next presents {dueNs, n, k,
 *                      perFrame}, written by the game thread
 *   the last present   what the render thread's last picture took {frame,
 *                      alpha, cost, display wait, fence wait, start, end,
 *                      shown}
 *   pins               a count per frame slot: the render thread holds the
 *                      slots it reads; the game thread reuses a slot only
 *                      unpinned
 *   jobs               a queue of FM_JOBS calls the render thread runs in
 *                      order, each with its sequence number; done counts
 *                      the last one finished (the hand-over's handshake: a
 *                      replay's submit is job s, and the game thread waits
 *                      for done >= s before it waits that replay's fence or
 *                      reuses its frame slot)
 *   park and quit      park: the render thread finishes what is queued and
 *                      then holds (the surface made again on the game
 *                      thread meanwhile); quit: it runs what is queued,
 *                      then ends
 *   the heartbeat      when the job in progress started (0: none), the
 *                      last one's end, the longest in the block: the
 *                      watchdog takes a job that does not end for the GPU
 *                      hang limit as a GPU hang (diag_host.c)
 *
 * The mode ([dev] render_thread = auto, on, off or sync) is decided here
 * too (fm_mode_choose).
 */
#ifndef ICO_PLATFORM_FRAME_MAILBOX_H
#define ICO_PLATFORM_FRAME_MAILBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the frame slots a pin count is kept for */
#define FM_SLOTS 4u
/* the jobs that can wait at once */
#define FM_JOBS 8u

typedef struct FmFrame {
    uint32_t number, slot;
    uint64_t closeAt, tickAt, tickPrev;
} FmFrame;

typedef struct FmLock {
    uint64_t dueNs;
    uint32_t n, k;
    double perFrame;
} FmLock;

typedef struct FmPresent {
    uint32_t frame;
    float alpha;
    uint64_t costNs, dispWaitNs, fenceNs, t0, t1;
    bool shown;
} FmPresent;

typedef enum FmJobKind {
    FM_JOB_CALL = 0, /* a call to run between pictures */
    FM_JOB_PICTURE   /* a picture's submit and present: noted as the last present */
} FmJobKind;

typedef struct FmJob {
    void (*fn)(void *arg);
    void *arg;
    FmJobKind kind;
    uint64_t seq;
} FmJob;

/* the render thread's next step (fm_next) */
typedef enum FmNext {
    FM_NEXT_JOB = 0, /* run the job given, then fm_finish */
    FM_NEXT_WAIT,    /* nothing to do: wait for the game thread */
    FM_NEXT_PARKED,  /* parked: wait for fm_unpark */
    FM_NEXT_QUIT     /* the queue is empty and quit was asked: end */
} FmNext;

/* The block's numbers for the 10 s line (fm_block_take) */
typedef struct FmBlock {
    double idlePercent; /* the block's time with no job */
    double waitMaxMs;   /* the longest wait for a job (for a frame) */
    double busyMaxMs;   /* the longest job: the heartbeat's longest gap */
    unsigned jobs, pictures;
    unsigned replaced; /* latest frames replaced before they were taken */
} FmBlock;

typedef struct FrameMailbox {
    FmFrame latest;
    uint64_t published, taken, replaced;
    FmLock lock;
    uint64_t lockSerial;
    FmPresent lastPresent;
    uint64_t presents;
    uint32_t pins[FM_SLOTS];
    FmJob jobs[FM_JOBS];
    uint32_t head, count;
    uint64_t posted, started, done;
    bool parkWanted, parked, quit;
    /* the heartbeat: the job in progress since (0 none), the last end */
    uint64_t busySince, lastDoneNs;
    /* the block: its start, the time with no job, the longest wait and
       job, the jobs and pictures run, the latest frames replaced at its
       start */
    uint64_t blockAt, idleNs, idleSince, waitMaxNs, busyMaxNs;
    unsigned blockJobs, blockPictures;
    uint64_t replacedAt;
} FrameMailbox;

void fm_init(FrameMailbox *m, uint64_t nowNs);

/* The game thread: frame f closed; the newest wins.  True when it replaced
   one the render thread had not taken */
bool fm_publish(FrameMailbox *m, const FmFrame *f);
/* The render thread: the newest frame when it is newer than the last one
   taken (*out), else false */
bool fm_take(FrameMailbox *m, FmFrame *out);

/* The vsync lock's grid (the game thread), and its read (the render
   thread): false before the first set */
void fm_lock_set(FrameMailbox *m, const FmLock *l);
bool fm_lock_get(const FrameMailbox *m, FmLock *out);

/* Slot pins: the render thread pins the slots it reads and unpins them
   after its list is ended; a slot out of range is ignored */
void fm_pin(FrameMailbox *m, uint32_t slot);
void fm_unpin(FrameMailbox *m, uint32_t slot);
bool fm_pinned(const FrameMailbox *m, uint32_t slot);

/* The game thread: fn(arg) queued; its sequence number, 0 when it is not
   taken (the queue full, parked, a park asked or quitting: the caller
   runs it itself) */
uint64_t fm_post(FrameMailbox *m, void (*fn)(void *arg), void *arg, FmJobKind kind);
/* The render thread's next step at nowNs: a job (*out; the heartbeat
   starts), or wait, parked or quit.  Queued jobs run before a park or a
   quit takes effect */
FmNext fm_next(FrameMailbox *m, FmJob *out, uint64_t nowNs);
/* The render thread: job seq finished at nowNs (the heartbeat stops) */
void fm_finish(FrameMailbox *m, const FmJob *job, uint64_t nowNs);
/* Nothing queued and nothing running */
bool fm_idle(const FrameMailbox *m);
/* Job seq (fm_post's number) has finished: the hand-over's handshake */
bool fm_done_up_to(const FrameMailbox *m, uint64_t seq);

/* Park (the game thread asks; the render thread parks at its next step
   once the queue is empty), parked yet, unpark */
void fm_park(FrameMailbox *m);
bool fm_is_parked(const FrameMailbox *m);
void fm_unpark(FrameMailbox *m);
/* Quit: the queue runs, then FM_NEXT_QUIT */
void fm_quit(FrameMailbox *m);

/* The render thread's last picture, and its read (false before one) */
void fm_present_note(FrameMailbox *m, const FmPresent *p);
bool fm_last_present(const FrameMailbox *m, FmPresent *out);

/* The heartbeat at nowNs: the job in progress (*seq, 0 none), the one
   finished last (*done) and how long the one in progress has run (ns) */
uint64_t fm_heartbeat(const FrameMailbox *m, uint64_t nowNs, uint64_t *seq, uint64_t *done);

/* The block since the last take, and a new one from nowNs */
void fm_block_take(FrameMailbox *m, uint64_t nowNs, FmBlock *out);

/* [dev] render_thread: "auto" (also NULL or ""), "on", "off" or "sync";
   anything else is auto, said in *why */
typedef enum FmMode {
    FM_MODE_OFF = 0, /* submit and present on the game thread, as before */
    FM_MODE_ON,      /* the render thread submits and presents; the game thread goes on */
    FM_MODE_SYNC     /* the render thread does, and the game thread waits for each picture */
} FmMode;

/* The mode the window runs: value the setting, singleThread the backend's
   RhiLimits.singleThreadCommands (OpenGL, Direct3D 11: the same modes,
   every call on the render thread, said in *why), window false without a
   window; *why (never NULL on return) says why it is off, or names the
   setting; a static string */
FmMode fm_mode_choose(const char *value, bool singleThread, bool window, const char **why);
const char *fm_mode_name(FmMode mode);

#endif /* ICO_PLATFORM_FRAME_MAILBOX_H */
