/*
 * port/render/test/rd_pace_gpu_test.c
 *
 * The in-between pictures drawn for their GPU work's predicted end
 * (rd_pace_gpu.h), CPU only:
 *   1. the share and the median;
 *   2. the model's edges: before its measurements (0), an empty queue (0 to
 *      the bit), one picture a tick (0 to the bit), the queue's wait less
 *      its mean, the bound from the measured spread, the ratio hold, the
 *      re-anchoring on an exact end and on a bound, the prediction's error,
 *      the restarts (frame back, frame jump, a gap over a second);
 *   3. simulations of the GPU queue per regime (a frame every 33.37 ms,
 *      120 Hz, the window asking when the render thread is free, a further
 *      picture held when its reads would end past the next closing step;
 *      the GPU's start the later of the first submit and the picture
 *      before's end; each picture's end measured where the render thread's
 *      slot wait blocked, two pictures later): the content's every step and
 *      its step against the GPU's ends, with the hand-over's moment (today)
 *      and the model's, through rd_rebase_raw and rd_rebase_alpha as the
 *      renderer takes them; the pictures and their hand-overs the same in
 *      both (the model decides nothing about when a picture is made); never
 *      back, never a repeat; the mean moment within the derived tolerance;
 *      one a tick and an idle GPU exactly today's;
 *   4. the brief's cage pattern (hand-overs at 0.15-0.35 and max(0.506,
 *      that + 0.29-0.45) of a tick, the GPU 14.8-15.6 ms a picture);
 *   5. the rc1 log's own consecutive pictures (L:3846-3851, 4109-4113,
 *      4275-4280: frame, alpha, GPU ms) after a warm-up of the cage;
 *   6. the renderer's side: measurements through the ring, the shift, the
 *      attempt ledger's row, the start line and the evenness line's clause
 *      word for word.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_pace_gpu.h"
#include "rd_pace_rules.h"
#include "rd_rebase.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)
#define CHECK_STR(got, want)                                                                       \
    do {                                                                                           \
        if (strcmp((got), (want)) != 0) {                                                          \
            printf("FAIL %s:%d:\n got  \"%s\"\n want \"%s\"\n", __FILE__, __LINE__, (got),         \
                   (want));                                                                        \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

#define TICK 33.3667
#define REFRESH (1000.0 / 120.0)

/* --------------------------------------------------------------- 1, 2 */

static void sample(RdPaceGpu *m, double hand, double sub, double span, double end, bool exact)
{
    const RdPaceGpuSample s = {hand, sub, span, end, exact};
    rd_pace_gpu_model_note(m, &s, NULL);
}

/* the windows filled: lead 10, span 15 (15 each), measured latencies
   24 .. 33 (10: mean 28.5, 10th 24, 90th 32: the bound 4.5) */
static void warm(RdPaceGpu *m)
{
    for (int i = 0; i < RD_PACE_GPU_MED; i++) {
        sample(m, 10.0 + i, 20.0 + i, 15.0, 0.0, false);
    }
    for (int i = 0; i < RD_PACE_GPU_LAT_MIN; i++) {
        sample(m, 100.0 + i, 0.0, 0.0, 124.0 + 2.0 * i, true);
    }
}

static void testShare(void)
{
    const double v[5] = {5.0, 1.0, 4.0, 2.0, 3.0};
    CHECK(rd_pace_gpu_median(v, 5) == 3.0);
    CHECK(rd_pace_gpu_share(v, 5, 0.1) == 1.0);
    CHECK(rd_pace_gpu_share(v, 5, 0.9) == 5.0);
    CHECK(rd_pace_gpu_share(v, 0, 0.5) == 0.0);
    double w[RD_PACE_GPU_LAT];
    for (int i = 0; i < RD_PACE_GPU_LAT; i++) {
        w[i] = (double)(RD_PACE_GPU_LAT - i);
    }
    /* 32: the 10th the 4th (ceil 3.2), the 90th the 29th */
    CHECK(rd_pace_gpu_share(w, RD_PACE_GPU_LAT, 0.1) == 4.0);
    CHECK(rd_pace_gpu_share(w, RD_PACE_GPU_LAT, 0.9) == 29.0);
}

static void testEdges(void)
{
    RdPaceGpu m;
    RdPaceGpuPlace p;
    rd_pace_gpu_model_reset(&m);

    /* before its measurements: 0, not ready, no prediction */
    rd_pace_gpu_model_place(&m, 1000.0, 1, &p);
    CHECK(p.how == RD_PACE_GPU_NOT_READY && p.shiftMs == 0.0 && p.predEndMs == 0.0);
    rd_pace_gpu_model_place(&m, 1016.0, 1, &p);
    CHECK(p.how == RD_PACE_GPU_NOT_READY && p.shiftMs == 0.0);

    /* an empty queue: hand-overs 25 ms apart, G 15 under the gap: q 0 at
       every picture, the shift 0 to the bit, two a tick or not */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    for (int i = 0; i < 40; i++) {
        rd_pace_gpu_model_place(&m, 1000.0 + 25.0 * i, (uint32_t)(10 + i / 2), &p);
        CHECK(p.shiftMs == 0.0 && p.queueMs == 0.0 && p.rawMs == 0.0);
        CHECK(p.predEndMs == 1000.0 + 25.0 * i + 10.0 + 15.0);
    }
    CHECK(p.how == RD_PACE_GPU_PLACED && p.boundMs == 4.5 && p.latP10Ms == 24.0 &&
          p.latP90Ms == 32.0);

    /* one a tick: every picture a new frame, a full queue (hand-overs 10 ms
       apart, G 15): the queue's wait grows, the shift stays 0 */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    for (int i = 0; i < 20; i++) {
        rd_pace_gpu_model_place(&m, 1000.0 + 10.0 * i, (uint32_t)(10 + i), &p);
        CHECK(p.shiftMs == 0.0 && !p.limited);
        CHECK(i == 0 || p.how == RD_PACE_GPU_ONE_A_TICK);
    }
    CHECK(p.queueMs > 0.0);

    /* two a tick, the second queued: picture A at 1000 (E 1025), picture B
       of the same frame at 1012 (start max(1022, 1025): q 3, E 1040); the
       mean q at A 0 (its first), B's raw 3 - 0 = 3, under the bound 4.5 */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    rd_pace_gpu_model_place(&m, 1000.0, 50, &p);
    CHECK(p.how == RD_PACE_GPU_ONE_A_TICK && p.queueMs == 0.0 && p.predEndMs == 1025.0);
    rd_pace_gpu_model_place(&m, 1012.0, 50, &p);
    CHECK(p.how == RD_PACE_GPU_PLACED && p.queueMs == 3.0 && p.rawMs == 3.0 && p.shiftMs == 3.0 &&
          p.predEndMs == 1040.0 && !p.atBound && !p.limited);
    /* the mean moved by 3/16; the next fresh one at 1033 (base 1043 past
       1040: q 0): raw -3/16 */
    rd_pace_gpu_model_place(&m, 1033.0, 51, &p);
    CHECK(p.queueMs == 0.0 && fabs(p.rawMs + 3.0 / 16.0) < 1e-12 && p.shiftMs == p.rawMs);

    /* the bound: a further picture 7 ms after (base 1050, the picture
       before's end 1058: q 8, raw 8 - 0.17578125 over 4.5: held at 4.5;
       inside the ratio: the moment before 1032.8125, the step 11.69 of 7) */
    rd_pace_gpu_model_place(&m, 1040.0, 51, &p);
    CHECK(p.queueMs == 8.0 && p.atBound && p.shiftMs == 4.5 && !p.limited);
    /* the ratio: picture A at 1000 (E 1025), B at 1012 (q 3, the moment
       1015); A measured exact at 1016 (error 9; the latencies 24 .. 33 and
       16: mean 27.36, 10th 24, 90th 32, the bound 4.64) moves B's end to
       max(1022, 1016) + 15 = 1037; C at 1013 (q 14, raw 14 - 3/16 at the
       bound) would step 1 + 4.64 - 3 in 1 ms: held to twice the gap, 1017 */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    rd_pace_gpu_model_place(&m, 1000.0, 50, &p);
    rd_pace_gpu_model_place(&m, 1012.0, 50, &p);
    CHECK(p.shiftMs == 3.0);
    {
        const RdPaceGpuSample s = {1000.0, 1001.0, 15.0, 1016.0, true};
        RdPaceGpuNoted nd;
        rd_pace_gpu_model_note(&m, &s, &nd);
        CHECK(nd.matched && nd.errValid && nd.errMs == 9.0);
    }
    rd_pace_gpu_model_place(&m, 1013.0, 50, &p);
    CHECK(p.queueMs == 14.0 && p.atBound && p.limited && p.shiftMs == 4.0 &&
          fabs(p.boundMs - (32.0 - 301.0 / 11.0)) < 1e-12);
    /* C measured exact at 1020: D at 1014 (base 1024 past 1020: q 0, raw
       minus the mean 3/16 + 13.8125/16) would step 1 - 4 - 1.05 in 1 ms:
       held to half the gap, 1017.5 */
    {
        const RdPaceGpuSample s = {1013.0, 0.0, 0.0, 1020.0, true};
        rd_pace_gpu_model_note(&m, &s, NULL);
    }
    rd_pace_gpu_model_place(&m, 1014.0, 50, &p);
    CHECK(p.queueMs == 0.0 && fabs(p.rawMs + (0.1875 + 13.8125 / 16.0)) < 1e-12 && p.limited &&
          !p.atBound && p.shiftMs == 3.5);

    /* re-anchoring: picture X at 2000 (E 2025, the first of a new chain),
       Y at 2005 (E 2040): X measured exact at 2030 moves Y to 2045 and the
       prediction's error is -5 */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    rd_pace_gpu_model_place(&m, 2000.0, 70, &p);
    rd_pace_gpu_model_place(&m, 2005.0, 70, &p);
    CHECK(p.predEndMs == 2040.0);
    {
        const RdPaceGpuSample s = {2000.0, 2010.0, 15.0, 2030.0, true};
        RdPaceGpuNoted nd;
        rd_pace_gpu_model_note(&m, &s, &nd);
        CHECK(nd.matched && nd.errValid && nd.errMs == -5.0);
    }
    /* Z at 2020: start max(2030, 2045): q 15 */
    rd_pace_gpu_model_place(&m, 2020.0, 71, &p);
    CHECK(p.queueMs == 15.0 && p.predEndMs == 2060.0);
    /* a bound on Y (its submit 2016, span 14: max(2016, 2030) + 14 = 2044,
       the bound 2041 under it): Z's start max(2030, 2041): q 11 */
    {
        const RdPaceGpuSample s = {2005.0, 2016.0, 14.0, 2041.0, false};
        RdPaceGpuNoted nd;
        rd_pace_gpu_model_note(&m, &s, &nd);
        CHECK(nd.matched && !nd.errValid);
    }
    rd_pace_gpu_model_place(&m, 2025.0, 71, &p);
    /* the medians now: span 15 (the 14 one of 15), lead 10 (11 one of 15):
       Z re-predicted at max(2030, 2041) + 15 = 2056, this one's base
       2035: 21 behind it, capped at one picture's G (the frames in
       flight): q 15, the end 2035 + 15 + 15 */
    CHECK(p.queueMs == 15.0 && p.predEndMs == 2065.0);
    /* a measurement of no picture of the ring: samples only */
    {
        const RdPaceGpuSample s = {1.5, 0.0, 0.0, 0.0, false};
        RdPaceGpuNoted nd;
        rd_pace_gpu_model_note(&m, &s, &nd);
        CHECK(!nd.matched);
    }

    /* the restarts: a frame back, a frame on by 9, a gap over a second, a
       gap not positive; each empties the windows (not ready again) */
    static const struct {
        double hand;
        uint32_t frame;
    } kRe[] = {{3010.0, 69}, {3010.0, 79}, {4000.5, 71}, {3000.0, 70}};

    for (size_t i = 0; i < sizeof kRe / sizeof kRe[0]; i++) {
        rd_pace_gpu_model_reset(&m);
        warm(&m);
        rd_pace_gpu_model_place(&m, 3000.0, 70, &p);
        CHECK(!p.restarted && p.how == RD_PACE_GPU_ONE_A_TICK);
        rd_pace_gpu_model_place(&m, kRe[i].hand, kRe[i].frame, &p);
        CHECK(p.restarted && p.how == RD_PACE_GPU_NOT_READY && p.shiftMs == 0.0);
    }
    /* unmeasured pictures (keep frames: no record comes back) handed over
       5 ms apart with G 15: the queue held at one picture's G, not
       growing 10 ms a picture (rc3's 205-426 ms means) */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    for (int i = 0; i < 40; i++) {
        rd_pace_gpu_model_place(&m, 5000.0 + 5.0 * i, (uint32_t)(300 + i / 3), &p);
        CHECK(p.queueMs <= 15.0);
    }
    CHECK(p.queueMs == 15.0);

    /* frame on by 8 within a second: the same chain */
    rd_pace_gpu_model_reset(&m);
    warm(&m);
    rd_pace_gpu_model_place(&m, 3000.0, 70, &p);
    rd_pace_gpu_model_place(&m, 3999.0, 78, &p);
    CHECK(!p.restarted && p.how == RD_PACE_GPU_ONE_A_TICK);
}

/* ------------------------------------------------------------ 3, 4, 5 */

static uint32_t s_seed;

static double U(double lo, double hi)
{
    s_seed = s_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((double)(s_seed >> 8) / 16777216.0);
}

typedef struct SimCfg {
    const char *name;
    double gLo, gHi;       /* the GPU's time a picture */
    double rLo, rHi;       /* the render thread's work before its slot wait */
    double dLess;          /* the first submit that much before it (D = R - dLess) */
    double stepLo, stepHi; /* the closing step */
    double reads;          /* a further picture's reads (the hold) */
    int open;              /* 1: the brief's cage hand-overs, not the window's loop */
    uint32_t ticks;
    uint32_t seed;
} SimCfg;

typedef struct SimOut {
    uint32_t pics, further, placed, atBound, limited, oneATick, notReady;
    double fps;
    double meanT, devT, meanM, devM;        /* every step, ticks */
    double glassT, glassM;                  /* (content step - GPU end step) deviation, ticks */
    double twoN, twoDevT, twoDevM;          /* steps over 2-refresh GPU-end gaps */
    double maxAbsShift, meanShift, meanTol; /* ms; the derived tolerance of the mean */
    uint32_t backT, backM, repT, repM;
    double handSum; /* the hand-overs, summed (the same with and without the model) */
} SimOut;

#define SIM_MAX 4096

typedef struct SimPic {
    double hand, sub, g, end, waitAt;
    uint32_t frame;
} SimPic;

static SimPic s_pic[SIM_MAX];

/* the pending measurements: picture i measured when its slot is recycled */
typedef struct Pend {
    RdPaceGpuSample s;
    double avail;
} Pend;

static Pend s_pend[SIM_MAX];

/* one simulation: the hand-overs, the true GPU ends; with useModel the
   model's shifts.  The record of contents into cT/cM (ticks) */
static double s_cT[SIM_MAX], s_cM[SIM_MAX], s_bM[SIM_MAX];

static void simRun(const SimCfg *c, SimOut *o, const double *logFrames, const double *logAlpha,
                   const double *logGpu, int logN)
{
    memset(o, 0, sizeof(*o));
    s_seed = c->seed;
    RdPaceGpu m;
    rd_pace_gpu_model_reset(&m);
    RdRebase rbT, rbM;
    memset(&rbT, 0, sizeof rbT);
    memset(&rbM, 0, sizeof rbM);
    uint32_t n = 0, pendN = 0, pendAt = 0;
    double J = 0.0, Eprev = 0.0;
    double sumShift = 0.0, qMin = 1e30, qMax = -1e30;
    const double P = TICK / 2.0;
    const double handCost = 0.3;
    const uint32_t ticks =
        c->ticks + (logN > 0 ? (uint32_t)(logFrames[logN - 1] - logFrames[0]) + 1u : 0u);
    for (uint32_t k = 0; k < ticks && n < SIM_MAX - 8; k++) {
        const double t0 = 1000.0 + (double)k * TICK;
        const double step = U(c->stepLo, c->stepHi);
        /* the asks of this tick */
        double asks[8];
        int nAsk = 0;
        const bool logPart = logN > 0 && k >= c->ticks;
        if (logPart) {
            for (int i = 0; i < logN; i++) {
                if ((uint32_t)(logFrames[i] - logFrames[0]) + c->ticks == k) {
                    asks[nAsk++] = t0 + logAlpha[i] * TICK;
                }
            }
        } else if (c->open) {
            const double a1 = U(0.15, 0.35);
            const double a2b = a1 + U(0.29, 0.45);
            const double a2 = a2b > 0.506 ? a2b : 0.506;
            asks[nAsk++] = t0 + a1 * TICK;
            asks[nAsk++] = t0 + a2 * TICK;
        }
        int ai = 0;
        bool first = true;
        for (int j = 0; j < 2; j++) {
            const double ps = t0 + j * P, pe = ps + P;
            double x = J > ps + (j == 0 ? step : 0.2) ? J : ps + (j == 0 ? step : 0.2);
            for (;;) {
                if (c->open || logPart) {
                    if (ai >= nAsk) {
                        break;
                    }
                    x = asks[ai];
                    if (x >= pe) {
                        break;
                    }
                    ai++;
                } else {
                    if (x + handCost > pe) {
                        break;
                    }
                    if (!first && x + c->reads > t0 + TICK) {
                        j = 2; /* held: the next ask is the next tick's first */
                        break;
                    }
                }
                /* the measurements collected by now */
                for (uint32_t pi = pendAt; pi < pendN; pi++) {
                    if (s_pend[pi].avail <= x) {
                        rd_pace_gpu_model_note(&m, &s_pend[pi].s, NULL);
                        pendAt = pi + 1u;
                    } else {
                        break;
                    }
                }
                const double r = U(c->rLo, c->rHi);
                double gpu = U(c->gLo, c->gHi);
                if (logPart) {
                    for (int i = 0; i < logN; i++) {
                        if (fabs(t0 + logAlpha[i] * TICK - x) < 1e-9) {
                            gpu = logGpu[i];
                        }
                    }
                }
                SimPic *sp = &s_pic[n];
                sp->hand = x;
                sp->sub = x + r - c->dLess;
                sp->g = gpu;
                sp->frame = k;
                sp->end = (sp->sub > Eprev ? sp->sub : Eprev) + gpu;
                sp->waitAt = x + r;
                /* the slot wait at this job's end is for the picture before's
                   fence (two frames in flight): measured then */
                double jobEnd = x + r;
                if (n > 0) {
                    SimPic *pp = &s_pic[n - 1u];
                    const bool blocked = pp->end > jobEnd;
                    const bool exact = blocked && pp->end - jobEnd >= 0.05;
                    const double end = blocked ? (exact ? pp->end : jobEnd) : 0.0;
                    s_pend[pendN].s = (RdPaceGpuSample){pp->hand, pp->sub, pp->g, end, exact};
                    s_pend[pendN].avail = blocked ? pp->end : jobEnd;
                    pendN++;
                    if (blocked) {
                        jobEnd = pp->end;
                    }
                }
                /* the model at the hand-over */
                RdPaceGpuPlace pl;
                rd_pace_gpu_model_place(&m, x, k, &pl);
                if (!first) {
                    o->further++;
                }
                if (pl.how == RD_PACE_GPU_PLACED) {
                    o->placed++;
                    if (pl.queueMs < qMin) {
                        qMin = pl.queueMs;
                    }
                    if (pl.queueMs > qMax) {
                        qMax = pl.queueMs;
                    }
                } else if (pl.how == RD_PACE_GPU_ONE_A_TICK) {
                    o->oneATick++;
                } else {
                    o->notReady++;
                }
                o->atBound += pl.atBound ? 1u : 0u;
                o->limited += pl.limited ? 1u : 0u;
                if (fabs(pl.shiftMs) > o->maxAbsShift) {
                    o->maxAbsShift = fabs(pl.shiftMs);
                }
                sumShift += pl.shiftMs;
                o->handSum += x;
                /* the content as the renderer takes it (no drain here) */
                double sh;
                bool held;
                const double rT = rd_rebase_raw(&rbT, (x - t0) / TICK, TICK, 0.0, k, &sh, &held);
                const double rM =
                    rd_rebase_raw(&rbM, (x + pl.shiftMs - t0) / TICK, TICK, 0.0, k, &sh, &held);
                s_bM[n] = pl.limited ? fabs(pl.shiftMs) : pl.boundMs;
                s_cT[n] = (double)k + rd_rebase_alpha(rT);
                s_cM[n] = (double)k + rd_rebase_alpha(rM);
                Eprev = sp->end;
                J = jobEnd;
                n++;
                first = false;
                if (!(c->open || logPart)) {
                    x = J;
                }
            }
        }
    }
    o->pics = n;
    o->fps = (double)n / ((double)ticks * TICK / 1000.0);
    /* the steps, from the 64th picture (the model's windows full) */
    const uint32_t from = 64u < n ? 64u : 0u;
    double sT = 0, sT2 = 0, sM = 0, sM2 = 0, gT = 0, gT2 = 0, gM = 0, gM2 = 0;
    double wT = 0, wT2 = 0, wM = 0, wM2 = 0, wN = 0;
    uint32_t cnt = 0;
    for (uint32_t i = from + 1u; i < n; i++) {
        const double dT = s_cT[i] - s_cT[i - 1u], dM = s_cM[i] - s_cM[i - 1u];
        const double dE = (s_pic[i].end - s_pic[i - 1u].end) / TICK;
        sT += dT;
        sT2 += dT * dT;
        sM += dM;
        sM2 += dM * dM;
        gT += dT - dE;
        gT2 += (dT - dE) * (dT - dE);
        gM += dM - dE;
        gM2 += (dM - dE) * (dM - dE);
        if (floor((s_pic[i].end - s_pic[i - 1u].end) / REFRESH + 0.5) == 2.0) {
            wN += 1.0;
            wT += dT;
            wT2 += dT * dT;
            wM += dM;
            wM2 += dM * dM;
        }
        o->backT += dT < 0.0 ? 1u : 0u;
        o->backM += dM < 0.0 ? 1u : 0u;
        o->repT += dT == 0.0 ? 1u : 0u;
        o->repM += dM == 0.0 ? 1u : 0u;
        cnt++;
    }
#define DEV(s, s2, k) sqrt(fmax(0.0, (s2) / (k) - ((s) / (k)) * ((s) / (k))))
    o->meanT = sT / cnt;
    o->devT = DEV(sT, sT2, cnt);
    o->meanM = sM / cnt;
    o->devM = DEV(sM, sM2, cnt);
    o->glassT = DEV(gT, gT2, cnt);
    o->glassM = DEV(gM, gM2, cnt);
    o->twoN = wN;
    o->twoDevT = wN > 0 ? DEV(wT, wT2, wN) : 0.0;
    o->twoDevM = wN > 0 ? DEV(wM, wM2, wN) : 0.0;
#undef DEV
    o->meanShift = sumShift / (double)n;
    /* the derived tolerance (rd_pace_gpu.c): 16 * (q's range) over the
       placed pictures, plus every picture held at the bound or the ratio
       at its whole bound (their shifts left the identity) */
    o->meanTol = o->placed ? (RD_PACE_GPU_MEAN_DIV * (qMax - qMin) +
                              (double)(o->atBound + o->limited) * (qMax - qMin + o->maxAbsShift)) /
                                 (double)n
                           : 0.0;
}

static void simPrint(const SimCfg *c, const SimOut *o)
{
    printf("  %-22s %4u pictures %.1f fps (%u further, %u placed, %u one a tick, %u before "
           "measurements, %u at the bound, %u held to the ratio)\n"
           "      every step: today %.3f (%.3f), model %.3f (%.3f); against the GPU's ends: "
           "today %.3f, model %.3f; over 2-refresh gaps (%u): today %.3f, model %.3f; the "
           "largest shift %.2f ms, the mean %.3f ms (tolerance %.3f)\n",
           c->name, o->pics, o->fps, o->further, o->placed, o->oneATick, o->notReady, o->atBound,
           o->limited, o->meanT, o->devT, o->meanM, o->devM, o->glassT, o->glassM,
           (unsigned)o->twoN, o->twoDevT, o->twoDevM, o->maxAbsShift, o->meanShift, o->meanTol);
}

/* the guarantees every regime keeps */
static void simCommon(const SimCfg *c, const SimOut *o)
{
    if (o->backM != 0 || o->repM != 0 || o->backT != 0) {
        printf("FAIL %s: back %u, repeats %u (today back %u)\n", c->name, o->backM, o->repM,
               o->backT);
        failures++;
    }
    if (fabs(o->meanShift) > o->meanTol + 1e-9) {
        printf("FAIL %s: the mean shift %.4f ms over the tolerance %.4f\n", c->name, o->meanShift,
               o->meanTol);
        failures++;
    }
}

static void testSims(void)
{
    /* the render thread's work before its slot wait 9.0-10.4 ms ("holds a
       picture for 9.7 ms typically", L:3727), the first submit 0.2 ms
       before it; the closing step 4.3-5.8 ms in the cage (L:3727-3728),
       2.4-4.0 in stages 3 and 5 (L:3129-3497); a further picture's reads
       8.4 ms (L:3728) */
    static const SimCfg kCage = {
        "cage (2 a tick)", 14.8, 15.8, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 0, 300, 11u};
    static const SimCfg kOpen = {
        "cage, the brief's", 14.8, 15.6, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 1, 300, 12u};
    static const SimCfg kStage3 = {
        "stage 3/5 (2-3 a tick)", 12.3, 14.5, 7.0, 9.0, 0.2, 2.4, 4.0, 6.0, 0, 300, 13u};
    static const SimCfg kThree = {
        "three a tick", 9.5, 10.5, 6.0, 8.0, 0.2, 2.4, 4.0, 5.0, 0, 300, 14u};
    static const SimCfg kMixed = {
        "mixed (1-2 a tick)", 18.0, 21.0, 9.0, 10.4, 0.2, 3.4, 4.0, 8.4, 0, 300, 15u};
    /* one a tick: every further ask held (its reads a whole tick) */
    static const SimCfg kOne = {"one a tick", 30.0, 32.5, 9.0, 10.4, 0.2,
                                4.3,          5.8,  TICK, 0,   300,  16u};
    static const SimCfg kIdle = {"GPU idle", 4.0, 5.0, 9.0, 10.4, 0.2, 2.4, 4.0, 8.4, 0, 300, 17u};
    SimOut o, o2;

    printf(" simulations (10 s each; steps in ticks, mean (deviation)):\n");
    simRun(&kCage, &o, NULL, NULL, NULL, 0);
    simPrint(&kCage, &o);
    simCommon(&kCage, &o);
    /* the model run twice: the same pictures, the same hand-overs (it
       decides nothing about when a picture is made) */
    simRun(&kCage, &o2, NULL, NULL, NULL, 0);
    CHECK(o2.pics == o.pics && o2.handSum == o.handSum && o2.devM == o.devM);
    CHECK(o.placed > o.pics / 2u);
    /* today's .21 every step (the log's .164-.187, L:3758-4269), the
       model's under half of it; the simulated values rounded up */
    CHECK(o.devM < o.devT / 2.0 && o.glassM < o.glassT / 2.0 && o.twoDevM < o.twoDevT / 2.0);
    CHECK(o.devT > 0.15 && o.devM <= 0.05 && o.glassM <= 0.02 && o.twoDevM <= 0.05);

    simRun(&kOpen, &o, NULL, NULL, NULL, 0);
    simPrint(&kOpen, &o);
    simCommon(&kOpen, &o);
    CHECK(o.devT >= 0.14 && o.devM < o.devT / 2.0 && o.glassM < o.glassT / 2.0 &&
          o.twoDevM < o.twoDevT / 2.0);
    CHECK(o.devM <= 0.07 && o.glassM <= 0.03 && o.twoDevM <= 0.06);

    simRun(&kStage3, &o, NULL, NULL, NULL, 0);
    simPrint(&kStage3, &o);
    simCommon(&kStage3, &o);
    CHECK(o.devM < o.devT / 2.0 && o.glassM < o.glassT / 2.0);

    simRun(&kThree, &o, NULL, NULL, NULL, 0);
    simPrint(&kThree, &o);
    simCommon(&kThree, &o);
    CHECK(o.devM < o.devT / 2.0 && o.glassM < o.glassT / 2.0);

    simRun(&kMixed, &o, NULL, NULL, NULL, 0);
    simPrint(&kMixed, &o);
    simCommon(&kMixed, &o);
    CHECK(o.devM < o.devT / 2.0 && o.glassM < o.glassT / 2.0);

    /* one a tick and an idle GPU: today's moments to the bit */
    simRun(&kOne, &o, NULL, NULL, NULL, 0);
    simPrint(&kOne, &o);
    simCommon(&kOne, &o);
    CHECK(o.further == 0 && o.placed == 0 && o.maxAbsShift == 0.0);
    CHECK(memcmp(s_cT, s_cM, sizeof(double) * o.pics) == 0);

    simRun(&kIdle, &o, NULL, NULL, NULL, 0);
    simPrint(&kIdle, &o);
    simCommon(&kIdle, &o);
    CHECK(o.further > 0 && o.placed > 0 && o.maxAbsShift == 0.0);
    CHECK(memcmp(s_cT, s_cM, sizeof(double) * o.pics) == 0);
}

/* the rc1 log's runs (frame, alpha, GPU ms of five consecutive replays) */
static void testLogRuns(void)
{
    static const struct {
        const char *name;
        double frame[5], alpha[5], gpu[5];
    } kRun[] = {
        {"L:3846-3851",
         {10143, 10143, 10144, 10144, 10145},
         {0.23, 0.58, 0.28, 0.56, 0.22},
         {14.6, 14.3, 15.7, 14.3, 14.9}},
        {"L:4109-4113",
         {11043, 11043, 11044, 11044, 11045},
         {0.35, 0.79, 0.22, 0.64, 0.15},
         {14.5, 14.2, 14.7, 14.2, 14.6}},
        {"L:4275-4280",
         {11643, 11643, 11644, 11644, 11645},
         {0.22, 0.53, 0.33, 0.70, 0.37},
         {16.5, 16.2, 19.8, 16.6, 16.4}},
    };

    /* after 300 ticks of the brief's cage pattern (the model warm), the
       five pictures at their own hand-overs and GPU times */
    static const SimCfg kWarm = {"", 14.8, 15.6, 9.0, 10.4, 0.2, 4.3, 5.8, 8.4, 1, 300, 21u};
    printf(" the rc1 log's runs (steps in ticks):\n");
    for (size_t r = 0; r < sizeof kRun / sizeof kRun[0]; r++) {
        SimOut o;
        simRun(&kWarm, &o, kRun[r].frame, kRun[r].alpha, kRun[r].gpu, 5);
        const uint32_t at = o.pics - 5u;
        double dT = 0, dM = 0, dE = 0;
        printf("  %s: today", kRun[r].name);
        for (uint32_t i = at + 1u; i < o.pics; i++) {
            printf(" %.3f", s_cT[i] - s_cT[i - 1u]);
        }
        printf(", model");
        for (uint32_t i = at + 1u; i < o.pics; i++) {
            printf(" %.3f", s_cM[i] - s_cM[i - 1u]);
        }
        printf(", the GPU's ends");
        for (uint32_t i = at + 1u; i < o.pics; i++) {
            printf(" %.3f", (s_pic[i].end - s_pic[i - 1u].end) / TICK);
        }
        for (uint32_t i = at + 1u; i < o.pics; i++) {
            const double e = (s_pic[i].end - s_pic[i - 1u].end) / TICK;
            dT += (s_cT[i] - s_cT[i - 1u] - e) * (s_cT[i] - s_cT[i - 1u] - e);
            dM += (s_cM[i] - s_cM[i - 1u] - e) * (s_cM[i] - s_cM[i - 1u] - e);
            dE += (s_cM[i] - s_cM[i - 1u] - 0.5) * (s_cM[i] - s_cM[i - 1u] - 0.5);
            CHECK(s_cM[i] > s_cM[i - 1u]);
        }
        printf("; against the GPU's ends rms today %.3f, model %.3f\n", sqrt(dT / 4.0),
               sqrt(dM / 4.0));
        /* the hand-overs are the log's: today's steps are its alphas' */
        CHECK(fabs((s_cT[at + 1u] - s_cT[at]) - (kRun[r].alpha[1] - kRun[r].alpha[0])) < 1e-6);
        /* the uneven runs (today over 0.1 tick from the GPU's ends) at under
           half of it; every step within the two shifts' bounds of today's */
        if (sqrt(dT / 4.0) > 0.1) {
            CHECK(sqrt(dM / 4.0) < sqrt(dT / 4.0) / 2.0);
        }
        for (uint32_t i = at + 1u; i < o.pics; i++) {
            const double d = (s_cM[i] - s_cM[i - 1u]) - (s_cT[i] - s_cT[i - 1u]);
            CHECK(fabs(d) <= (s_bM[i] + s_bM[i - 1u]) / TICK + 1e-9);
        }
        (void)dE;
    }
}

/* ------------------------------------------------------------------- 6 */

static void testLive(void)
{
    ico_attempt_reset();
    RdPaceGpuWindow w;
    rd_pace_gpu_take(&w); /* empty */
    char out[2048];
    CHECK(!rd_pace_gpu_format(&w, out, sizeof out) && out[0] == '\0');

    /* paced: the row not applicable */
    rd_pace_gpu_paced();
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_NA);

    /* the default off: the drawn shift 0 at every picture (the hand-over
       rule's value to the bit in presentRebase), the model's computed */
    CHECK(!rd_pace_gpu_on() || getenv("ICO_RD_PACE_GPU") != NULL);
    const bool drawn = rd_pace_gpu_on();
    double would = 0.0;

    /* as made with measurements back without a slot clock: the row as it
       was until 600 of them, then not applicable (no clock) */
    for (int i = 0; i < RD_PACE_GPU_NO_CLOCK; i++) {
        const RdPaceGpuSample s = {5000.0 + 20.0 * i, 0.0, 15.0, 0.0, false};
        rd_pace_gpu_measure(&s);
        CHECK(rd_pace_gpu_shift(5000.0 + 20.0 * i, (uint32_t)(200 + i / 2), &would) == 0.0);
        if (i == RD_PACE_GPU_NO_CLOCK - 2) {
            CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_NA &&
                  strstr(ico_attempt_why_of(ICO_ATT_PACE_GPU), "paced") != NULL);
        }
    }
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == ICO_ATT_NA &&
          strstr(ico_attempt_why_of(ICO_ATT_PACE_GPU), "slot clock") != NULL);
    rd_pace_gpu_take(&w);
    CHECK(w.notReady == RD_PACE_GPU_NO_CLOCK && w.placed == 0);
    CHECK(rd_pace_gpu_format(&w, out, sizeof out));
    CHECK_STR(out, "; the GPU's predicted end: 0 pictures placed by it (0 with one a tick, 600 "
                   "before its measurements, 0 restarts), 0 held to 0.5-2.0 times the "
                   "hand-overs' gap; predicted against the measured end: no exact end");
    rd_pace_gpu_paced();

    /* measurements through the ring: the windows filled (lead 10, span
       15, latencies 24 .. 33), the row on at the next picture */
    for (int i = 0; i < RD_PACE_GPU_MED; i++) {
        const RdPaceGpuSample s = {10.0 + i, 20.0 + i, 15.0, 0.0, false};
        rd_pace_gpu_measure(&s);
    }
    for (int i = 0; i < RD_PACE_GPU_LAT_MIN; i++) {
        const RdPaceGpuSample s = {100.0 + i, 0.0, 0.0, 124.0 + 2.0 * i, true};
        rd_pace_gpu_measure(&s);
    }
    /* a measurement of no hand-over: not taken */
    {
        const RdPaceGpuSample s = {0.0, 20.0, 15.0, 30.0, true};
        rd_pace_gpu_measure(&s);
    }
    CHECK(rd_pace_gpu_shift(1000.0, 50, &would) == 0.0 && would == 0.0);
    CHECK(ico_attempt_state_of(ICO_ATT_PACE_GPU) == (drawn ? ICO_ATT_ON : ICO_ATT_OFF_DEFAULT));
    CHECK(rd_pace_gpu_shift(1012.0, 50, &would) == (drawn ? 3.0 : 0.0) && would == 3.0);
    /* picture 1000 measured exact at 1027: error -2 */
    {
        const RdPaceGpuSample s = {1000.0, 1010.0, 15.0, 1027.0, true};
        rd_pace_gpu_measure(&s);
    }
    CHECK(rd_pace_gpu_shift(1014.0, 50, &would) == (drawn ? would : 0.0));
    rd_pace_gpu_take(&w);
    /* the third: the first anchored at 1027 (predicted 1025: error -2;
       its latency 27 into the window: 11 of them, mean 312 / 11, 10th the
       2nd 25, 90th the 10th 32: the bound 312 / 11 - 25 = 3.64), the
       second's end max(1022, 1027) + 15 = 1042, base 1024: 18, capped at
       one picture's G (the frames in flight): q 15, raw 15 - 3/16 over the
       bound; the ratio's range 1 to 4 ms of shift (the moment before 1015,
       the gap 2) holds it; the queue's waits 3 and 15; the shifts 3 and
       3.64 in the half-ms bins 38 and 39 */
    CHECK(w.placed == 2 && w.oneATick == 1 && w.errN == 1 && w.errSumUs == -2000 &&
          w.boundUs == 3636 && w.latP10Us == 25000 && w.latP90Us == 32000 && w.atBound == 1 &&
          w.shift[38] == 1 && w.shift[39] == 1 && w.queueUs == 18000);
    CHECK(rd_pace_gpu_format(&w, out, sizeof out));
    CHECK_STR(out, "; the GPU's predicted end: 2 pictures placed by it (1 with one a tick, 0 "
                   "before its measurements, 0 restarts), the shift 10th 3.25, median 3.25, 90th "
                   "3.75 ms (the queue's wait 9.00 ms on average), 1 at the bound (the last "
                   "3.64 ms, from the hand-over to the measured end 10th 25.00, 90th 32.00 ms), "
                   "0 held to 0.5-2.0 times the hand-overs' gap; predicted against the measured "
                   "end over 1 pictures: mean -2.00 ms (deviation 0.00)");
    {
        char b[2048];
        (void)ico_attempt_block_text(b, sizeof b, -1);
        CHECK(strstr(b, drawn ? "in-between pictures for the GPU's predicted end on 2 pictures "
                                "placed (1 at the bound)"
                              : "in-between pictures for the GPU's predicted end off by default 2 "
                                "pictures placed (1 at the bound)") != NULL);
    }
    /* the ring full: the 33rd measurement before a take dropped */
    for (int i = 0; i < 33; i++) {
        const RdPaceGpuSample s = {1.0 + i, 0.0, 0.0, 0.0, false};
        rd_pace_gpu_measure(&s);
    }
    rd_pace_gpu_paced();
    rd_pace_gpu_take(&w);
    CHECK(w.dropped == 1);

    /* the start line */
    char line[1024];
    (void)rd_pace_gpu_start_text(true, false, "kept for measurement", line, sizeof line);
    CHECK_STR(line,
              "rd: in-between pictures for the GPU's predicted end: off by default (kept for "
              "measurement); the GPU per picture the median of the last 15 measured spans, the "
              "hand-over to the first submit the median of the last 15, the queue's wait from "
              "the hand-overs (at most 1 picture's GPU time: the frames in flight); the shift "
              "the wait less its running mean, held to the measured spread of the hand-over to "
              "the GPU's end over the last 32 measured ends and to 0.5-2.0 times the hand-overs' "
              "gap; one picture a tick or an empty queue keep the hand-over's moment; computed "
              "and reported, not drawn (ICO_RD_PACE_GPU=1 draws it)");
    (void)rd_pace_gpu_start_text(true, true, "x", line, sizeof line);
    CHECK(strncmp(line, "rd: in-between pictures for the GPU's predicted end: on (x); ", 61) == 0 &&
          strstr(line, "not drawn") == NULL);
    (void)rd_pace_gpu_start_text(false, false, "x", line, sizeof line);
    CHECK_STR(line, "rd: in-between pictures for the GPU's predicted end: not applicable here (x)");
    char small[8];
    CHECK(rd_pace_gpu_start_text(false, false, "x", small, sizeof small) == 7 &&
          strlen(small) == 7);
    ico_attempt_reset();
}

/* the default off, bit for bit: presentRebase's path (rd_thread.c) over a
   cage-like run of hand-overs with the model and the timer computed: the
   value drawn the hand-over rule's (rd_rebase_raw of the measured ask, as
   v0.11.5) to the bit, the note's drawn rule the hand-over's */
static void testOffBitIdentical(void)
{
    if (getenv("ICO_RD_PACE_GPU") != NULL || getenv("ICO_RD_AS_MADE_TIMER") != NULL) {
        printf(" off-by-default check skipped: a tests' switch is set\n");
        return;
    }
    rd_pace_gpu_paced();
    rd_pace_rules_paced();
    RdRebase today, now;
    memset(&today, 0, sizeof today);
    memset(&now, 0, sizeof now);
    const double T = 33.3667;
    uint32_t seed = 5u;
    uint32_t same = 0, n = 0;
    for (uint32_t k = 0; k < 300; k++) {
        const double t0 = 1000.0 + k * T;
        for (int j = 0; j < 2; j++) {
            seed = seed * 1664525u + 1013904223u;
            const double u = (double)(seed >> 8) / 16777216.0;
            const double ask = t0 + (j == 0 ? 0.15 + 0.2 * u : 0.51 + 0.28 * u) * T;
            const double drain = 0.05 + 0.1 * u, hand = ask + drain;
            /* a measurement two pictures late, as the record's */
            const RdPaceGpuSample sm = {hand - 2.0 * 16.68, hand - 2.0 * 16.68 + 9.5, 15.0,
                                        hand - 2.0 * 16.68 + 27.0, true};
            rd_pace_gpu_measure(&sm);
            double sh0, sh1, would = 0.0;
            bool h0, h1;
            const double atAsk = (ask - t0) / T;
            const double r0 = rd_rebase_raw(&today, atAsk, T, drain, k, &sh0, &h0);
            const double queue = rd_pace_gpu_shift(hand, k, &would);
            double r1 = rd_rebase_raw(&now, queue != 0.0 ? atAsk + queue / T : atAsk, T, drain, k,
                                      &sh1, &h1);
            RdRuleNote note;
            r1 = rd_pace_rules_pick(r1, queue, would, ask - atAsk * T, T, hand, k, &note);
            same += memcmp(&r0, &r1, sizeof r0) == 0 && note.rule == RD_RULE_HAND &&
                            note.alpha[RD_RULE_HAND] == rd_rebase_alpha(r0)
                        ? 1u
                        : 0u;
            n++;
        }
    }
    printf(" off by default: %u of %u values the hand-over rule's to the bit\n", same, n);
    CHECK(same == n && n == 600u);
    rd_pace_gpu_paced();
    rd_pace_rules_paced();
}

int main(void)
{
    printf("rd_pace_gpu_test\n");
    testShare();
    testEdges();
    testSims();
    testLogRuns();
    testLive();
    testOffBitIdentical();
    if (failures) {
        printf("rd_pace_gpu_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_pace_gpu_test: ok\n");
    return 0;
}
