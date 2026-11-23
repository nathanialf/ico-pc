/* rd_ab_run.c: the in-play A/B judge in the renderer (rd_ab_run.h; the
 * judge itself rd_ab.c).  Every call but rd__ab_scene on the thread that
 * replays. */
#include "rd_ab_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_ab.h"
#include "rd_internal.h"
#include "rd_nodiscard.h"
#include "rd_perf_clock.h"
#include "rd_vu_zero.h"

static RdAb s_ab;
static bool s_made;
/* the stage switch: (stage + 1) << 32 | its first frame, 0 none yet
   (__atomic: the game thread writes it) */
static uint64_t s_scene;
static int32_t s_stage = -1; /* the replays' stage */
static bool s_open;          /* a replay's record is being made */
static uint8_t s_off;        /* its subjects at their off arm */
static uint8_t s_allowed;    /* the subjects that may be tried now */
/* each subject's ledger row and what was fired into it so far */
static const int k_row[2] = {ICO_ATT_AB_ZERO, ICO_ATT_AB_NODISCARD};
static int s_ledAllowed[2] = {-1, -1}, s_ledStopped[2] = {-1, -1};
static uint32_t s_ledOn[2], s_ledOff[2], s_ledTrials[2];
static long s_fakeLoss;    /* ICO_RD_AB_FAKE_LOSS: the replay N */
static uint32_t s_replays; /* begun this run */
static int s_notice;       /* __atomic: the player's notice (rd_ab_notice, RD_AB_NOTICE_*) */

static void made(void)
{
    if (s_made) {
        return;
    }
    s_made = true;
    rd_ab_init(&s_ab, rd_ab_mode_of(getenv("ICO_RD_AB")));
    (void)rd_ab_subject(&s_ab, "zero-area cull");     /* RD_AB_ZERO */
    (void)rd_ab_subject(&s_ab, "discard-free twins"); /* RD_AB_NODISCARD */
    const char *fl = getenv("ICO_RD_AB_FAKE_LOSS");
    s_fakeLoss = fl != NULL ? strtol(fl, NULL, 10) : 0;
}

/* the player's notice's answer for the UI's thread (rd_ab_notice) */
static void notice(void)
{
    __atomic_store_n(&s_notice, rd_ab_notice_state(&s_ab), __ATOMIC_RELEASE);
}

static void drain(void)
{
    notice();
    char l[RD_AB_LINE];
    while (rd_ab_take_line(&s_ab, l, sizeof(l))) {
        rd__log("%s", l);
    }
}

/* the ledger's rows: each state where it changed, its counts (totals) */
static void ledger(void)
{
    for (int i = 0; i < 2; i++) {
        const int allowed = (s_allowed & (1u << i)) != 0u;
        if (allowed != s_ledAllowed[i] || (int)s_ab.stopped != s_ledStopped[i]) {
            s_ledAllowed[i] = allowed;
            s_ledStopped[i] = s_ab.stopped;
            ico_attempt_decide_timed(k_row[i], s_ab.mode, allowed, s_ab.stopped);
        }
        const RdAbSubject *z = &s_ab.subj[i];
        ico_attempt_fire(k_row[i], z->stagesOn - s_ledOn[i]);
        ico_attempt_fire2(k_row[i], z->stagesOff - s_ledOff[i]);
        ico_attempt_fire3(k_row[i], z->trials - s_ledTrials[i]);
        s_ledOn[i] = z->stagesOn;
        s_ledOff[i] = z->stagesOff;
        s_ledTrials[i] = z->trials;
    }
}

/* the subjects that may be tried now (their own self-tests, marks and
   run-time checks: rd__vu_zero_triable, rd__nd_triable), told to the judge */
static void allow(void)
{
    const bool zero = rd__vu_zero_triable(), nd = rd__nd_triable();
    rd_ab_allow(&s_ab, RD_AB_ZERO, zero);
    rd_ab_allow(&s_ab, RD_AB_NODISCARD, nd);
    /* off for the rest of the run: the player's notice can leave once every
       subject is (rd_ab_notice_state) */
    rd_ab_gone(&s_ab, RD_AB_ZERO, rd__vu_zero_gone());
    rd_ab_gone(&s_ab, RD_AB_NODISCARD, rd__nd_gone());
    s_allowed = (uint8_t)((zero ? 1u << RD_AB_ZERO : 0u) | (nd ? 1u << RD_AB_NODISCARD : 0u));
}

void rd__ab_scene(int stage, uint32_t first)
{
    const uint64_t v = ((uint64_t)(uint32_t)(stage + 1) << 32) | first;
    __atomic_store_n(&s_scene, v, __ATOMIC_RELEASE);
}

void rd__ab_begin(RdPerfRecord *r)
{
    made();
    s_replays++;
    if (g_rd.hasDevice && rhi_device_lost()) {
        rd_ab_stop(&s_ab, "the graphics device was lost");
    } else if (s_fakeLoss > 0 && (long)s_replays == s_fakeLoss) {
        rd_ab_stop(&s_ab, "ICO_RD_AB_FAKE_LOSS: a device loss faked at this replay's start");
    }
    const uint64_t sc = __atomic_load_n(&s_scene, __ATOMIC_ACQUIRE);
    if (sc != 0u && r->frame >= (uint32_t)sc) {
        s_stage = (int32_t)(uint32_t)(sc >> 32) - 1;
    }
    allow();
    const RdAbTag tag = rd_ab_begin(&s_ab, s_stage, rd__now_ms(), r->keep != 0);
    s_off = tag.off & s_allowed;
    r->abTrial = tag.trial;
    r->abSeq = tag.seq;
    r->abStage = tag.stage;
    r->abEpoch = tag.epoch;
    r->abOff = s_off;
    s_open = true;
    drain();
    ledger();
}

void rd__ab_end(void)
{
    s_open = false;
}

void rd__ab_collect(const RdPerfRecord *r)
{
    made();
    if (rd_perf_reset_counted(r->gpuResetSign)) {
        /* the GPU's own sign of a reset (rd_perf_clock.h: a hang the driver
           came back from) */
        rd_ab_stop(&s_ab, "the GPU's own sign of a reset: a hang");
    }
    /* the measure: the scene's GPU work alone (rd_ab.h rd_ab_scene_ms): the
       present's pass and the display wait before it are left out on both
       arms alike (the same stamps on either: the arm changes the pre-pass
       and the lists' draws, never which stamps a replay writes) */
    const double sceneMs = rd_ab_scene_ms(r->gpuMs, r->gpuDisplayWaitMs, r->gpuPresentMs);
    const RdAbIn in = {{r->abTrial, r->abSeq, r->abStage, r->abOff, r->abEpoch},
                       r->abUsed,
                       r->keep != 0,
                       r->gpuValid != 0 && sceneMs > 0.0,
                       (double)r->vuPreVertices + (double)r->vuTris,
                       sceneMs};
    rd_ab_collect(&s_ab, &in);
    drain();
    ledger();
}

void rd__ab_renderer(void)
{
    made();
    static uint32_t s_renderers; /* the renderers made this process */
    if (s_renderers++ == 0u) {
        return; /* the first: nothing to forget */
    }
    s_open = false;
    rd_ab_forget(&s_ab, "a Graphics API switch");
    drain();
    ledger();
}

int rd_ab_notice(void)
{
    return __atomic_load_n(&s_notice, __ATOMIC_ACQUIRE);
}

bool rd__ab_off(int subject)
{
    return s_open && subject >= 0 && subject < RD_AB_SUBJECTS && (s_off & (1u << subject)) != 0u;
}

void rd__ab_used(int subject)
{
    if (s_open && subject >= 0 && subject < RD_AB_SUBJECTS) {
        g_rdPerf.abUsed |= (uint8_t)(1u << subject);
    }
}

void rd__ab_unused(int subject)
{
    if (s_open && subject >= 0 && subject < RD_AB_SUBJECTS) {
        g_rdPerf.abUsed &= (uint8_t)~(1u << subject);
    }
}

void rd__ab_start_line(void)
{
    made();
    allow();
    char l[1024];
    (void)rd_ab_start_text(&s_ab, l, sizeof(l));
    rd__log("%s", l);
    ledger();
}

size_t rd__ab_stats_text(char *out, size_t cap)
{
    made();
    return rd_ab_stats_text(&s_ab, out, cap);
}
