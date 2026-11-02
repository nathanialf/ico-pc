/*
 * port/platform/window_present.h
 *
 * The window's presentation loop (window_host.c is at its file size gate):
 * ico_window_pace's pace, the vsync lock's window side (the lock_io_*
 * callbacks of pace_policy.h PaceLockIo), the timer path's present loop and
 * the one present it makes (present_once), with the state they share with
 * window_host.c (WindowPres, the pace deadline).  The window's own pieces
 * the loop calls (the display's counts, the 10 s line, the start-up log
 * line, a host pause) stay in window_host.c and are declared here.
 */
#ifndef ICO_PLATFORM_WINDOW_PRESENT_H
#define ICO_PLATFORM_WINDOW_PRESENT_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include "host_config.h" /* ICO_PATH_MAX */
#include "pace_policy.h"
#include "rd.h"

/* A host this far behind the vsync deadlines stops trying to catch up. */
#define RESYNC_NS 100000000ull

/* the presentation loop (ico_window_pace) */
typedef struct WindowPres {
    int framerate;           /* ico_video_framerate() as last applied */
    unsigned cutSerial;      /* ico_video_cut_serial() last passed on */
    uint32_t frame;          /* rd_frame_number() last seen */
    uint32_t presentedFrame; /* the frame of the last present */
    Uint64 tickAt, tickPrev; /* the pace deadlines (simulated time) the last two frames closed at */
    Uint64 lastPresent;      /* the last present's start (the alpha clock's) */
    Uint64 nextPresent;      /* when the next present is due (0: now); pace_next_present keeps */
    Uint64 cost;             /* how long the last rd_present took */
    PaceHist paceHist;       /* the last presents' costs (pace_policy.h) */
    bool paceSlow;           /* pace_slow_present after the last present */
    /* a frame pace left unpresented since the last present (pace_late_skip)
       and its number */
    bool skipOpen;
    uint32_t skipFrame;
    /* the rate log */
    Uint64 statAt;
    unsigned statPresents, statFrames;
    unsigned statSkipLate, statSkipSlow; /* the block's skipped presents by reason */
    /* simulated vsyncs paced, and the lag the pacer dropped (each time
       it was more than RESYNC_NS behind: the simulation ran slower than
       real time by that much) */
    unsigned statVsyncs, statResyncs;
    Uint64 statDropped;
    uint32_t statFrameNo; /* rd_frame_number() at the block's start */
    /* rd_video_presents at the block's start; a movie presents on
       its own, outside the replays the line counts */
    uint32_t statMovie, statMovieFail;
    /* the simulation step's real time (from the end of one pace to the
       start of the next), for the 10 s line */
    Uint64 paceEnd;
    double stepSumMs, stepMaxMs;
    unsigned stepCount;
    /* rd_vu_card_counts at the block's start: the card verdicts the
       steps decided (rd_mesh.c), for the 10 s line */
    uint64_t cardStrips, cardTris;
    double cardMs;
    /* the same at the last step's end, for the slow step's line */
    uint64_t stepCardStrips, stepCardTris;
    double stepCardMs;
    int mailbox; /* rhi_prefer_mailbox as last applied: vsync on and presenting between ticks */
    int vsync;   /* vsync on and presenting between ticks */
    /* vsync on and the cap equals the display's refresh but no vsync lock
       (144 on 59.94: no whole multiple of the game's rate): FIFO, a
       present a vblank, the game's own timer */
    int fifoLock;
    /* the vsync lock (pace_policy.h PaceLock): vsync on, the cap equal to
       the refresh and a whole multiple n of the simulation's rate, n
       refreshes within 1 % of its period: the simulation on a grid of the
       wall clock, n presents a step (mailbox with the display's timing of
       presents, rhi_present_timing; else FIFO) */
    int gridLock;
    int paceHz;           /* the simulation's rate the last pace had (50 or 60; 0: none yet) */
    int clockGone;        /* the display's timing stopped or was not believed: FIFO */
    int vblankGone;       /* the vblank clock stopped or was not believed (the grid on the
                             report until the display or the settings change) */
    Uint64 lockWaitNs;    /* the last locked present's wait (lock_io_present) */
    int presentModeApply; /* the lock wants another present mode: ico_window_pace applies it */
    int timingUse;        /* rhi_present_timing_use as last set (presents carry ids) */
    PaceLock lock;
    unsigned lockN;       /* n the lock was started with (0: not locked at the last vsync) */
    Uint64 lockRefresh;   /* the refresh it was started with */
    int lockMailbox;      /* the swapchain was in mailbox mode when it started */
    int lockClock;        /* PaceLock.clock last logged */
    int offScreen;        /* the window minimised, hidden, occluded or 0x0 (window_off_screen) */
    uint32_t heldFrame;   /* a new frame pace_fifo_hold held back (+1; 0: none) */
    unsigned lockResyncs; /* lock.resyncs already counted in statResyncs */
    Uint64 lockDroppedNs; /* lock.droppedNs already counted in statDropped */
    /* a host pause to take out of the pacing (pace_paused): the time a
       swapchain rebuild took (video_apply, a size event, fullscreen),
       applied before the next pace, and under the lock the first present
       on the new chain's excess over a refresh (present_once), applied
       after it */
    Uint64 pausePendingNs;
    /* RhiStats.swapchains at the last present accounted: a present on a
       newer swapchain is the first on it (pace_first_present_pause) */
    Uint64 swapchains;
    /* the lock's counts at the 10 s block's start */
    double lockNudgeAt;
    unsigned lockSeenAt, lockLateAt, lockEarlyAt, lockWaitedAt, lockSkippedAt;
    unsigned lockSkippedLateAt; /* the lock's late skips counted into statSkipLate */
    double audioRatio;          /* the audio's rate ratio last set (ico_audio_set_rate_ratio) */
    Uint64 lastReturn;          /* when the last present returned (0: none yet) */
    Uint64 lateMaxNs;           /* the block's latest step against when it was due */
    /* the slow-step lines ([dev] slow_step_ms, 0 off): the threshold,
       the block's count and lines, ico_window_pump's time, the renderer's
       texture decodes and pipeline creations at the last pace's end */
    double slowMs, pumpMs;
    /* the pump's parts: the event queue (and its events), video_apply,
       ico_input_sdl_update */
    double pumpEventsMs, pumpSettingsMs, pumpInputMs;
    unsigned pumpEvents;
    /* the block's longest pump, for the 10 s line (issue 105), and the
       block's pumps summed and counted (the game thread's line) */
    double pumpMaxMs, pumpSumMs;
    unsigned pumpCount;
    unsigned slowSteps, slowLogged;
    uint32_t decodes, pipeCreates;
    /* the present clock alpha is taken from (rd.h RdPresentClock) */
    RdPresentClock clock;
    int legacyAlpha; /* ICO_RD_S2_LEGACY=1 (read in video_settings) */
    /* F11, the stats line every second until this time (0: off) */
    Uint64 fastUntil;
    /* F12's presented-picture PNG while its capture is pending ("": none) */
    char presentPng[ICO_PATH_MAX];
    /* the bug-report capture's zip, which the presented picture is added to
       once it is written (empty: F12's, or the zip was not written) */
    char reportZip[ICO_PATH_MAX];
    /* the present that took it: the record of the first present_once after
       the capture was armed (presentInfoPending until then; presentInfoHave
       once noted), the frame the dump holds and whether the zip holds the
       tick before (the -prev.rddump --interp rebuilds the present from) */
    int presentInfoPending, presentInfoHave;
    RdPresentInfo presentInfo;
    uint32_t presentDumpFrame;
    int presentDumpPrev;
    /* the present allowance on the timer path (pace_policy.h PaceBudget;
       under the lock lock.budget): the GPU time a timed replay (the
       median of the last 8, perf_drain; 0: no timestamps yet), the last
       present's waits for the display (the acquire and the present) and for
       the GPU (the fence waits), the simulation step's time (ico_window_pace) */
    PaceBudget budget;
    PaceHist gpuHist;
    Uint64 gpuNs, displayWaitNs, fenceWaitNs, stepNs;
    bool fenceHoldsDisplay; /* RhiStats.fenceWaitsHoldDisplay (OpenGL), vsync on */
    /* a numeric cap above the refresh in mailbox mode held to one present
       a refresh (pace_cap_above_refresh): the refresh at this pace (0: not
       held), and its line written since the last settings change */
    Uint64 capAboveNs;
    int capAboveLogged, mailboxOffLogged;
    /* the 10 s line's allowance counts at the block's start, and the
       allowance they are of (the timer path's or the lock's) */
    const PaceBudget *budgetAtOf;
    unsigned budgetRefusedAt, budgetDropsAt, budgetClimbsAt, budgetClimbRefusalsAt;
} WindowPres;

/* the loop's state (window_present.c), and the deadline: when the
   simulation step after this pace is due (simulated time on the wall
   clock) */
extern WindowPres g_windowPres;
extern Uint64 g_windowDeadline;

/* One pace (ico_window_pace, after the step): the vsync lock's vsync or
   the timer path's presents until the deadline, then the sleep to it.
   hz: the simulation's rate (50 PAL, else NTSC's 59.94) */
void window_present_pace(int hz);

/* --- window_host.c's, for the loop ------------------------------------ */
/* The display's refresh period in ns; fallback when it reports none */
Uint64 window_refresh_ns(Uint64 fallback);
/* A pause of the host (a swapchain rebuild): the deadline, the frame's
   times and the present grid move by it */
void window_pace_paused(Uint64 ns);
/* The start-up log line of the present just made at alpha */
void window_boot_log(float alpha);
/* The 10 s lines, when a block ends at now */
void window_pace_log(Uint64 now);
/* The timer path's present allowance starts again */
void window_timer_budget_reset(void);
/* The display under the vsync lock (the 10 s display line): a new lock's
   counts and clock, the clock's vblanks at each locked vsync, a present
   the lock made, the clock's next vblank (1 taken, 0 none yet, -1 not
   running), the clock stopped, and whether it runs (1), stopped itself
   (-1) or is off (0) */
void window_disp_lock_start(void);
void window_disp_vsync(Uint64 refresh);
void window_disp_present(Uint64 refresh);
int window_disp_take(Uint64 refresh, uint64_t *ns);
void window_disp_clock_stop(void);
int window_disp_clock(void);
/* The window (NULL before it opens) */
SDL_Window *window_sdl(void);

#endif /* ICO_PLATFORM_WINDOW_PRESENT_H */
