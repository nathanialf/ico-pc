/*
 * port/platform/gpu_hang.h
 *
 * The GPU hang limit (issue 123): how long the main thread may wait for one
 * submission the GPU has not finished before the run ends as a GPU hang,
 * free of the watchdog's thread so a unit test checks it (gpu_hang_test).
 *
 * The limit is half the watchdog's (ini watchdog=S, default 30: 15 s), at
 * least ICO_GPU_HANG_MIN_S; watchdog=0 turns it off with the watchdog.  The
 * watchdog's thread (diag_host.c watchdog_loop) polls the backend's
 * progress (rhi.h rhi_gpu_progress: the submission a wait in progress
 * blocks on, the newest seen finished) a few times a second and steps this
 * watch: the time one wait for one unfinished submission was seen, counted
 * only while the watchdog counts (not paused in the background, no
 * suspend gap), ends the run through the watchdog's own GPU hang path
 * (watchdog_fire: the pass ring, the loss dump, the loss count, the box)
 * once it reaches the limit, instead of after the watchdog's 2 x S without
 * a Main tick.  The waits themselves stay as they are (vk_sync.c): the
 * thread blocked in the driver is not the one that ends the run.
 */
#ifndef ICO_PLATFORM_GPU_HANG_H
#define ICO_PLATFORM_GPU_HANG_H

#define ICO_GPU_HANG_MIN_S 2.0

/* the limit in seconds from the watchdog's (ini watchdog=S): 0 (off) for
   0, else S / 2, at least ICO_GPU_HANG_MIN_S */
double ico_gpu_hang_limit_s(unsigned long watchdog_s);

/* Zero-initialise before the first step. */
typedef struct IcoGpuHangWatch {
    unsigned long long waiting_for; /* the submission the counted time is for, 0 none */
    double counted_s;               /* the time it was seen unfinished, counted */
    int fired;                      /* the limit was reached (once a run) */
} IcoGpuHangWatch;

/* One poll: waiting_for and finished as rhi_gpu_progress gives them (0:
   no wait in progress), dt_s the time since the last poll, count 0 when
   that time is not counted (the watchdog paused, the system asleep).  A
   wait for a submission above finished counts its time; another
   submission's wait starts again from 0; no wait, or one already
   finished, clears it.  1 once, when the counted time reaches limit_s
   (limit_s <= 0: never). */
int ico_gpu_hang_step(IcoGpuHangWatch *w, double limit_s, unsigned long long waiting_for,
                      unsigned long long finished, double dt_s, int count);

/* The render thread's watch (diag_host.c, the window's render thread):
   the same count over its picture in progress (seq, 0 none: a call between
   pictures is not counted, render_thread.c) and the last one finished
   (done), where the driver's pipeline creations are progress too: mark is
   their counter (diag_host.c activity_seq: it moves at every creation's
   start and end) and creating says one is in progress now.  A creation
   started or ended since the last poll, or one in progress, starts the
   count again, so a picture that makes its pipelines on first use (about
   250 ms each on a phone) is not a hang however many it makes; one
   creation that does not return is the watchdog's pipeline limit's and
   its own to judge, as without the render thread.  Zero-initialise. */
typedef struct IcoRenderHangWatch {
    IcoGpuHangWatch w;
    unsigned mark; /* the creations' counter at the last poll */
    int seen;      /* mark is set */
} IcoRenderHangWatch;

int ico_render_hang_step(IcoRenderHangWatch *r, double limit_s, unsigned long long seq,
                         unsigned long long done, unsigned mark, int creating, double dt_s,
                         int count);

/* The watchdog's pipeline limit (diag_host.c, issue 161): whether the
   crash report's note says a pipeline creation is in progress now.  Every
   creation's note starts "creating pipeline"; the background finisher's
   (port/render/rd_pipeline.c finBegin: "creating pipelines A-B of M in
   the background: ...") stays up from its batch's start until the next
   presented picture takes the batch, which a movie or a long load does not
   make, so it counts only while some of its compiles have not returned
   (bg_left: the backend's background compiles begun and not finished,
   rhi.h RhiGpuProgress.bgPipesLeft).  1: a creation in progress. */
int ico_pipe_note_creating(const char *note, unsigned bg_left);

/* The pipeline limit's watch: the time a creation was seen in progress
   with nothing moving.  seq the notes' counter (diag_host.c activity_seq:
   every note set or cleared), done the start-up set's count, bg_done the
   background compiles finished (RhiGpuProgress.bgPipesDone: the finisher's
   own heartbeat, each compile that returns); any of them moving, no
   creation, the time not counted (counted 0) or the limit off (on 0)
   starts it again.  Returns the stalled seconds (0 while it moves).
   Zero-initialise. */
typedef struct IcoPipeStallWatch {
    unsigned seq, done, bg_done;
    double since;
} IcoPipeStallWatch;

double ico_pipe_stall_step(IcoPipeStallWatch *w, double now, int creating, int counted, int on,
                           unsigned seq, unsigned done, unsigned bg_done);

#endif /* ICO_PLATFORM_GPU_HANG_H */
