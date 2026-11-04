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
   held_s: the time the memory rule held this creation
   (ico_pipe_mem_hold), back to 0 when the creation changes (no creation,
   the limit off, or seq, done or bg_done moving); last: the time of the
   last hold asked.  Zero-initialise. */
typedef struct IcoPipeStallWatch {
    unsigned seq, done, bg_done;
    double since;
    double held_s, last;
} IcoPipeStallWatch;

double ico_pipe_stall_step(IcoPipeStallWatch *w, double now, int creating, int counted, int on,
                           unsigned seq, unsigned done, unsigned bg_done);

/* The pipeline limit's length (diag_host.c, issues 161 and 188): before the
   game starts (in_play 0: the self-tests and the start-up set) 10 s, or 60 s
   on a driver whose compiles run one at a time (serial_compiler: rhi.h
   RhiLimits.serialPipelines, a PowerVR B-Series driver: Imagination's own
   thread on that driver has a set taking 84 s without its cache, and the
   issue 188 phone's did not return within 10 s from a 70k-word shader,
   slow or hung, the log cannot tell); in play 30 s on every driver. */
#define ICO_PIPE_STALL_START_S 10.0
#define ICO_PIPE_STALL_START_SERIAL_S 60.0
#define ICO_PIPE_STALL_PLAY_S 30.0
double ico_pipe_stall_limit_s(int in_play, int serial_compiler);

/* The heartbeat's memory rule (diag_host.c, issue 188): the process's
   resident memory at this heartbeat against the last one's (KB, -1
   unknown); 1 when it grew by ICO_PIPE_MEM_MOVING_KB or more, the driver's
   compiler at work (the issue 188 phone: 147, 251, 275, 296 MB at the
   heartbeats while its compile ran), so the pipeline limit does not count
   that heartbeat's time; an unknown reading never moves. */
#define ICO_PIPE_MEM_MOVING_KB 2048L
int ico_pipe_mem_moving(long prev_kb, long now_kb);

/* Whether the memory rule holds the limit at this poll (the watchdog's
   thread, before ico_pipe_stall_step, whose counted it then clears): a
   creation in progress whose memory moves (ico_pipe_mem_moving), the
   limit on, and neither memory_low (the free memory under the floor or a
   low-memory level from the system: diag_host.c ico_diag_background_memory,
   read here at every poll, since the set's own checks between batches
   cannot interrupt a creation inside the driver) nor the hold used up:
   the time held in this creation (w->held_s, the polls' time added while
   held) under ICO_PIPE_MEM_HOLD_LIMITS times limit_s.  Without the cap a
   driver call that never returns and grows 2 MB a heartbeat would hold
   the limit until Android's low-memory killer ended the process (no box,
   no stall mark).  A creation that grows all along thus ends at three
   times the limit held plus the limit itself at the latest (40 s before
   the game starts, 240 s on a serial compiler, 120 s in play).  1 held. */
#define ICO_PIPE_MEM_HOLD_LIMITS 3.0
int ico_pipe_mem_hold(IcoPipeStallWatch *w, double now, int creating, int moving, int on,
                      int memory_low, double limit_s);

/* Whether the next start prepares the graphics another way after the
   pipeline limit ended this one (the box's and the line's words, issue
   188): a note naming only a family the renderer can draw another way
   (skip_family: pipeline_skip.h pipeline_skip_family_of_note), or a VU
   shader (vu_note) while the plain VU shaders are not in force yet
   (vu_plain_already: the stall mark of an earlier end), changes what it
   makes; else the next start's set policy does (careful, then on first
   use), except where nothing of it runs before the creation that stalled:
   the self-tests run first at every start whatever the set (in_selftest),
   and a run already on first use has no later step (lazy_run).  1: another
   way. */
int ico_pipe_stall_next_differs(int in_selftest, int lazy_run, int skip_family, int vu_note,
                                int vu_plain_already);

#endif /* ICO_PLATFORM_GPU_HANG_H */
