/* rd_ab_run.c: the in-play A/B judge in the renderer (rd_ab_run.h; the
 * judge itself rd_ab.c).  Every call but rd__ab_scene on the thread that
 * replays. */
#include "rd_ab_run.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_ab.h"
#include "rd_fastps.h"
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

/* the subjects in their registration order (rd_ab_run.h RD_AB_*, the row's
   place its id): the judge's name, the ledger row, whether it may be tried
   now (its own self-test, mark and run-time checks); the next attempt is
   one more row (up to RD_AB_SUBJECTS) */
static bool camTriable(void)
{
    /* the twins on, and their clipper check clean on this device */
    return rd__nd_triable() && rd__nd_class_on();
}

static const struct {
    const char *name;
    int row;
    bool (*triable)(void);
} k_subj[] = {
    {"zero-area cull", ICO_ATT_AB_ZERO, rd__vu_zero_triable},              /* RD_AB_ZERO */
    {"discard-free twins", ICO_ATT_AB_NODISCARD, rd__nd_triable},          /* RD_AB_NODISCARD */
    {"fast pixel build", ICO_ATT_AB_FASTPS, rd__fps_triable},              /* RD_AB_FASTPS */
    {"discard-free twins, camera rule", ICO_ATT_AB_ND_CAMERA, camTriable}, /* RD_AB_ND_CAMERA */
};

#define N_SUBJ ((int)(sizeof(k_subj) / sizeof(k_subj[0])))
_Static_assert(sizeof(k_subj) / sizeof(k_subj[0]) <= RD_AB_SUBJECTS, "the judge's registry");
_Static_assert(RD_AB_ND_CAMERA == 3, "the rows' order is the subjects' ids");
/* each subject's ledger row: what was fired into it so far */
static int s_ledAllowed[RD_AB_SUBJECTS], s_ledStopped[RD_AB_SUBJECTS];
static uint32_t s_ledOn[RD_AB_SUBJECTS], s_ledOff[RD_AB_SUBJECTS], s_ledTrials[RD_AB_SUBJECTS];
static long s_fakeLoss;    /* ICO_RD_AB_FAKE_LOSS: the replay N */
static uint32_t s_replays; /* begun this run */
static int s_scenery;      /* __atomic: the game shows scenery now (rd_ab_scenery) */
static bool s_tainted;     /* the replay being made drew a build for an unmade pipeline */

static void made(void)
{
    if (s_made) {
        return;
    }
    s_made = true;
    rd_ab_init(&s_ab, rd_ab_mode_of(getenv("ICO_RD_AB")));
    for (int i = 0; i < N_SUBJ; i++) {
        (void)rd_ab_subject(&s_ab, k_subj[i].name);
        s_ledAllowed[i] = s_ledStopped[i] = -1; /* the row's state not decided yet */
    }
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

/* the ledger's rows: each state where it changed, its counts (totals) */
static void ledger(void)
{
    for (int i = 0; i < N_SUBJ; i++) {
        const int allowed = (s_allowed & (1u << i)) != 0u;
        if (allowed != s_ledAllowed[i] || (int)s_ab.stopped != s_ledStopped[i]) {
            s_ledAllowed[i] = allowed;
            s_ledStopped[i] = s_ab.stopped;
            ico_attempt_decide_timed(k_subj[i].row, s_ab.mode, allowed, s_ab.stopped);
        }
        const RdAbSubject *z = &s_ab.subj[i];
        ico_attempt_fire(k_subj[i].row, z->stagesOn - s_ledOn[i]);
        ico_attempt_fire2(k_subj[i].row, z->stagesOff - s_ledOff[i]);
        ico_attempt_fire3(k_subj[i].row, z->trials - s_ledTrials[i]);
        s_ledOn[i] = z->stagesOn;
        s_ledOff[i] = z->stagesOff;
        s_ledTrials[i] = z->trials;
    }
}

/* the subjects that may be tried now (their own self-tests, marks and
   run-time checks: k_subj's triable), told to the judge */
static void allow(void)
{
    uint8_t allowed = 0;
    for (int i = 0; i < N_SUBJ; i++) {
        const bool on = k_subj[i].triable();
        rd_ab_allow(&s_ab, i, on);
        allowed |= on ? (uint8_t)(1u << i) : 0u;
    }
    rd_ab_set_scenery(&s_ab, __atomic_load_n(&s_scenery, __ATOMIC_ACQUIRE) != 0);
    s_allowed = allowed;
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
    s_tainted = false;
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

void rd_ab_scenery(int scenery)
{
    __atomic_store_n(&s_scenery, scenery != 0, __ATOMIC_RELEASE);
}

bool rd__ab_off(int subject)
{
    return s_open && subject >= 0 && subject < RD_AB_SUBJECTS && (s_off & (1u << subject)) != 0u;
}

void rd__ab_used(int subject)
{
    if (s_open && subject >= 0 && subject < RD_AB_SUBJECTS) {
        g_rdPerf.abUsed = rd_ab_use_note(g_rdPerf.abUsed, &s_tainted, subject, RD_AB_NOTE_USED);
    }
}

void rd__ab_unused(int subject)
{
    if (s_open && subject >= 0 && subject < RD_AB_SUBJECTS) {
        g_rdPerf.abUsed = rd_ab_use_note(g_rdPerf.abUsed, &s_tainted, subject, RD_AB_NOTE_UNUSED);
    }
}

void rd__ab_unmade(void)
{
    if (s_open) {
        g_rdPerf.abUsed = rd_ab_use_note(g_rdPerf.abUsed, &s_tainted, -1, RD_AB_NOTE_UNMADE);
    }
}

void rd__ab_start_line(void)
{
    made();
    allow();
    char l[1536]; /* about 1 KB with four or five subjects named */
    (void)rd_ab_start_text(&s_ab, l, sizeof(l));
    rd__log("%s", l);
    ledger();
}

size_t rd__ab_stats_text(char *out, size_t cap)
{
    made();
    return rd_ab_stats_text(&s_ab, out, cap);
}
