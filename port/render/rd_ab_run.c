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
static int s_ledAllowed = -1, s_ledStopped = -1;
static uint32_t s_ledOn, s_ledOff, s_ledTrials; /* fired into the ledger so far */
static long s_fakeLoss;                         /* ICO_RD_AB_FAKE_LOSS: the replay N */
static uint32_t s_replays;                      /* begun this run */

static void made(void)
{
    if (s_made) {
        return;
    }
    s_made = true;
    rd_ab_init(&s_ab, rd_ab_mode_of(getenv("ICO_RD_AB")));
    (void)rd_ab_subject(&s_ab, "zero-area cull"); /* RD_AB_ZERO */
    const char *fl = getenv("ICO_RD_AB_FAKE_LOSS");
    s_fakeLoss = fl != NULL ? strtol(fl, NULL, 10) : 0;
}

static void drain(void)
{
    char l[RD_AB_LINE];
    while (rd_ab_take_line(&s_ab, l, sizeof(l))) {
        rd__log("%s", l);
    }
}

/* the ledger's row: its state where it changed, its counts (totals) */
static void ledger(void)
{
    const int allowed = (s_allowed & (1u << RD_AB_ZERO)) != 0u;
    if (allowed != s_ledAllowed || (int)s_ab.stopped != s_ledStopped) {
        s_ledAllowed = allowed;
        s_ledStopped = s_ab.stopped;
        ico_attempt_decide_timed(ICO_ATT_AB_ZERO, s_ab.mode, allowed, s_ab.stopped);
    }
    const RdAbSubject *z = &s_ab.subj[RD_AB_ZERO];
    ico_attempt_fire(ICO_ATT_AB_ZERO, z->stagesOn - s_ledOn);
    ico_attempt_fire2(ICO_ATT_AB_ZERO, z->stagesOff - s_ledOff);
    ico_attempt_fire3(ICO_ATT_AB_ZERO, z->trials - s_ledTrials);
    s_ledOn = z->stagesOn;
    s_ledOff = z->stagesOff;
    s_ledTrials = z->trials;
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
    const bool zero = rd__vu_zero_triable();
    rd_ab_allow(&s_ab, RD_AB_ZERO, zero);
    s_allowed = zero ? (uint8_t)(1u << RD_AB_ZERO) : 0u;
    const RdAbTag tag = rd_ab_begin(&s_ab, s_stage, rd__now_ms(), r->keep != 0);
    s_off = tag.off & s_allowed;
    r->abTrial = tag.trial;
    r->abSeq = tag.seq;
    r->abStage = tag.stage;
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
    const RdAbIn in = {
        {r->abTrial, r->abSeq, r->abStage, r->abOff}, r->abUsed, r->keep != 0, r->gpuValid != 0,
        (double)r->vuPreVertices + (double)r->vuTris, r->gpuMs};
    rd_ab_collect(&s_ab, &in);
    drain();
    ledger();
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

void rd__ab_start_line(void)
{
    made();
    const bool zero = rd__vu_zero_triable();
    rd_ab_allow(&s_ab, RD_AB_ZERO, zero);
    s_allowed = zero ? (uint8_t)(1u << RD_AB_ZERO) : 0u;
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
