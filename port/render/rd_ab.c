/* rd_ab.c: the in-play A/B judge (rd_ab.h).  Pure: no GPU, no clock, no
 * renderer state. */
#include "rd_ab.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the trial's arms by its replay's place: on, off, off, on */
static bool armOff(uint32_t seq)
{
    const uint32_t k = seq % 4u;
    return k == 1u || k == 2u;
}

static void line(RdAb *a, const char *fmt, ...)
{
    if (a->lineN == RD_AB_LINES) {
        /* the oldest dropped (the renderer drains every replay) */
        a->lineHead = (a->lineHead + 1) % RD_AB_LINES;
        a->lineN--;
    }
    char *out = a->lines[(a->lineHead + a->lineN) % RD_AB_LINES];
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(out, RD_AB_LINE, fmt, ap);
    va_end(ap);
    a->lineN++;
}

bool rd_ab_take_line(RdAb *a, char *out, size_t cap)
{
    if (a->lineN == 0 || cap == 0) {
        return false;
    }
    snprintf(out, cap, "%s", a->lines[a->lineHead]);
    a->lineHead = (a->lineHead + 1) % RD_AB_LINES;
    a->lineN--;
    return true;
}

void rd_ab_init(RdAb *a, int mode)
{
    memset(a, 0, sizeof(*a));
    a->mode = mode >= RD_AB_AUTO && mode <= RD_AB_ALT ? mode : RD_AB_AUTO;
    a->stage = -1;
    a->scenery = true; /* the renderer says otherwise (rd_ab_set_scenery) */
}

int rd_ab_mode_of(const char *env)
{
    if (env == NULL) {
        return RD_AB_AUTO;
    }
    if (strcmp(env, "on") == 0) {
        return RD_AB_FIXED_ON;
    }
    if (strcmp(env, "off") == 0) {
        return RD_AB_FIXED_OFF;
    }
    if (strcmp(env, "alt") == 0) {
        return RD_AB_ALT;
    }
    return RD_AB_AUTO;
}

const char *rd_ab_mode_name(int mode)
{
    switch (mode) {
    case RD_AB_FIXED_ON:
        return "ICO_RD_AB=on: every replay with each attempt";
    case RD_AB_FIXED_OFF:
        return "ICO_RD_AB=off: every replay without each attempt";
    case RD_AB_ALT:
        return "ICO_RD_AB=alt: every replay alternating with, without, without, with";
    default:
        return "the in-play judge";
    }
}

int rd_ab_subject(RdAb *a, const char *name)
{
    if (a->nSubj >= RD_AB_SUBJECTS) {
        return -1;
    }
    RdAbSubject *s = &a->subj[a->nSubj];
    memset(s, 0, sizeof(*s));
    snprintf(s->name, sizeof(s->name), "%s", name != NULL ? name : "?");
    return a->nSubj++;
}

void rd_ab_allow(RdAb *a, int id, bool allowed)
{
    if (id >= 0 && id < a->nSubj) {
        a->subj[id].allowed = allowed;
    }
}

static int memFind(const RdAb *a, int id, int32_t stage)
{
    for (int i = 0; i < a->nMem; i++) {
        if (a->mem[i].subject == id && a->mem[i].stage == stage) {
            return i;
        }
    }
    return -1;
}

int rd_ab_verdict(const RdAb *a, int id, int32_t stage)
{
    /* the run's verdict, whatever the stage; else the stage's mark of a
       trial that gave none */
    if (id >= 0 && id < a->nSubj &&
        (a->subj[id].carry == RD_AB_V_ON || a->subj[id].carry == RD_AB_V_OFF)) {
        return a->subj[id].carry;
    }
    const int i = memFind(a, id, stage);
    return i < 0 ? RD_AB_V_NONE : a->mem[i].verdict;
}

static void memPut(RdAb *a, int id, int32_t stage, int verdict)
{
    int i = memFind(a, id, stage);
    if (i < 0) {
        if (a->nMem == RD_AB_MEMORY) {
            return; /* full (RD_AB_TRIALS keeps it under) */
        }
        i = a->nMem++;
    }
    a->mem[i].subject = (int8_t)id;
    a->mem[i].stage = stage;
    a->mem[i].verdict = (uint8_t)verdict;
}

static int cmpD(const void *x, const void *y)
{
    const double a = *(const double *)x, b = *(const double *)y;
    return a < b ? -1 : a > b ? 1 : 0;
}

static double medianOf(const double *v, uint32_t n)
{
    double s[RD_AB_HIST > RD_AB_PAIRS ? RD_AB_HIST : RD_AB_PAIRS];
    if (n == 0u) {
        return 0.0;
    }
    memcpy(s, v, (size_t)n * sizeof(double));
    qsort(s, n, sizeof(double), cmpD);
    return n % 2u ? s[n / 2u] : 0.5 * (s[n / 2u - 1u] + s[n / 2u]);
}

static void histPush(double *h, uint32_t *n, uint32_t *at, double v)
{
    h[*at] = v;
    *at = (*at + 1u) % RD_AB_HIST;
    if (*n < RD_AB_HIST) {
        (*n)++;
    }
}

static const char *stageWord(char *buf, size_t n, int32_t stage)
{
    snprintf(buf, n, "stage %ld", (long)stage);
    return buf;
}

/* the trial's verdict on its pairs (n of them, the favour scaled) */
static int judge(const RdAbTrial *t, uint32_t n, bool early, double *med, uint32_t *favOn,
                 uint32_t *favOff)
{
    *favOn = *favOff = 0u;
    for (uint32_t i = 0; i < n; i++) {
        *favOn += t->r[i] < 0.0;
        *favOff += t->r[i] > 0.0;
    }
    *med = medianOf(t->r, n);
    if (early) {
        if (*favOn == n && *med <= -RD_AB_GAIN) {
            return RD_AB_V_ON;
        }
        if (*favOff == n && *med >= RD_AB_GAIN) {
            return RD_AB_V_OFF;
        }
        return RD_AB_V_NONE;
    }
    /* RD_AB_FAVOUR of RD_AB_PAIRS, rounded up for fewer pairs */
    const uint32_t need = (RD_AB_FAVOUR * n + RD_AB_PAIRS - 1u) / RD_AB_PAIRS;
    return *med <= -RD_AB_GAIN && *favOn >= need ? RD_AB_V_ON : RD_AB_V_OFF;
}

static void capCheck(RdAb *a)
{
    if (a->trials >= RD_AB_TRIALS && !a->capSaid) {
        a->capSaid = true;
        line(a, "A/B: %u trials run, the most a run: an attempt not timed yet stays with it",
             (unsigned)RD_AB_TRIALS);
    }
}

/* the trial ended: judged (verdict, early or on its n pairs) or not (why) */
static void trialEnd(RdAb *a, const char *why, bool keptOn, bool judgeable)
{
    RdAbTrial *t = &a->t;
    if (!t->active) {
        return;
    }
    RdAbSubject *s = &a->subj[t->subject];
    char st[32];
    (void)stageWord(st, sizeof(st), t->stage);
    const uint32_t n = t->pairs;
    int v = RD_AB_V_NONE;
    double med = 0.0;
    uint32_t favOn = 0, favOff = 0;
    const bool early = why == NULL && n == RD_AB_EARLY;
    if (judgeable && (why == NULL || n >= RD_AB_PAIRS_MIN)) {
        v = judge(t, n, early, &med, &favOn, &favOff);
    }
    if (v == RD_AB_V_ON || v == RD_AB_V_OFF) {
        if (v == RD_AB_V_ON) {
            s->stagesOn++;
        } else {
            s->stagesOff++;
        }
        const double onK = medianOf(t->mOn, n) * 1000.0, offK = medianOf(t->mOff, n) * 1000.0;
        const double pct = fabs(exp(med) - 1.0) * 100.0;
        char ended[96] = "";
        if (why != NULL) {
            snprintf(ended, sizeof(ended), " (%s)", why);
        } else if (early) {
            snprintf(ended, sizeof(ended), " (all one way: the early verdict)");
        }
        line(
            a,
            "A/B: %s, %s: %u pairs%s, with it %.2f ms, without %.2f ms of the scene's GPU work (the "
            "present and its display wait left out) a thousand vertices and triangles (the median pair %.1f %% %s with it, %u of %u cheaper %s), %u dropped by "
            "a long picture, %u by unequal work: %s for the rest of the run, every stage",
            s->name, st, (unsigned)n, ended, onK, offK, pct, med <= 0.0 ? "cheaper" : "dearer",
            (unsigned)(v == RD_AB_V_ON ? favOn : favOff), (unsigned)n,
            v == RD_AB_V_ON ? "with it" : "without", (unsigned)t->dropLong, (unsigned)t->dropWork,
            v == RD_AB_V_ON ? "on" : "off");
        /* the run's one answer: every stage takes it, no trial again */
        s->carry = (int8_t)v;
        s->carryStage = t->stage;
    } else {
        if (keptOn) {
            memPut(a, t->subject, t->stage, RD_AB_V_KEPT);
        }
        line(a,
             "A/B: %s, %s: no verdict (%s, %u pairs; %u dropped by a long picture, %u by unequal "
             "work, %u replays not measured): with it until it is timed again in %s",
             s->name, st, why != NULL ? why : "?", (unsigned)n, (unsigned)t->dropLong,
             (unsigned)t->dropWork, (unsigned)t->notMeasured,
             keptOn ? "another stage" : "the next stage of play");
    }
    t->active = false;
    capCheck(a);
}

static void trialStart(RdAb *a, int id)
{
    RdAbTrial *t = &a->t;
    memset(t, 0, sizeof(*t));
    t->active = true;
    t->subject = id;
    t->stage = a->stage;
    t->serial = ++a->serial;
    /* the stage's spans so far seed the running median */
    memcpy(t->hist, a->hist, sizeof(t->hist));
    t->histN = a->histN;
    t->histAt = a->histAt;
    a->trials++;
    a->subj[id].trials++;
}

static uint8_t verdictOff(const RdAb *a, int32_t stage)
{
    uint8_t off = 0;
    for (int i = 0; i < a->nSubj; i++) {
        (void)stage;
        /* the run's one verdict; before it, with it */
        if (a->subj[i].carry == RD_AB_V_OFF) {
            off |= (uint8_t)(1u << i);
        }
    }
    return off;
}

static uint8_t allMask(const RdAb *a)
{
    return (uint8_t)((1u << a->nSubj) - 1u);
}

static void countArms(RdAb *a, uint8_t off)
{
    for (int i = 0; i < a->nSubj; i++) {
        if (a->subj[i].allowed) {
            if (off & (1u << i)) {
                a->subj[i].armOff++;
            } else {
                a->subj[i].armOn++;
            }
        }
    }
}

RdAbTag rd_ab_begin(RdAb *a, int32_t stage, double nowMs, bool keep)
{
    RdAbTag tag = {0, 0, stage, 0, a->epoch};
    if (a->stopped) {
        tag.off = allMask(a);
        countArms(a, tag.off);
        return tag;
    }
    if (a->mode == RD_AB_FIXED_ON) {
        countArms(a, 0);
        return tag;
    }
    if (a->mode == RD_AB_FIXED_OFF) {
        tag.off = allMask(a);
        countArms(a, tag.off);
        return tag;
    }
    if (a->mode == RD_AB_ALT) {
        tag.off = armOff(a->altSeq++) ? allMask(a) : 0u;
        countArms(a, tag.off);
        return tag;
    }
    if (stage != a->stage) {
        trialEnd(a, "the stage ended", false, true);
        a->stage = stage;
        a->stageMs = nowMs;
        a->usedNow = 0;
        a->histN = a->histAt = 0;
    }
    RdAbTrial *t = &a->t;
    if (t->active && !a->subj[t->subject].allowed) {
        trialEnd(a, "the attempt was turned off", false, true);
    }
    if (t->active && t->seq >= RD_AB_GIVE_UP) {
        char why[64];
        snprintf(why, sizeof(why), "%u replays", (unsigned)RD_AB_GIVE_UP);
        trialEnd(a, why, true, true);
    }
    if (!a->scenery) {
        a->stageMs = nowMs; /* the 2 s count from the stage's scenery on */
    }
    /* a trial: game scenery (play or a cutscene, not the title, the logo,
       a menu or a movie: rd_ab_set_scenery), 2 s of it into the stage, for a
       subject with no verdict this run that had work in this stage and
       gave no answer here before */
    if (!t->active && stage >= 0 && a->scenery && a->trials < RD_AB_TRIALS &&
        nowMs - a->stageMs >= RD_AB_START_MS) {
        for (int i = 0; i < a->nSubj; i++) {
            if (a->subj[i].allowed && (a->usedNow & (1u << i)) &&
                a->subj[i].carry == RD_AB_V_NONE && memFind(a, i, stage) < 0) {
                trialStart(a, i);
                break;
            }
        }
    }
    tag.off = verdictOff(a, stage);
    if (t->active && !keep) {
        const uint8_t bit = (uint8_t)(1u << t->subject);
        tag.trial = t->serial;
        tag.seq = t->seq++;
        if (armOff(tag.seq)) {
            tag.off |= bit;
            t->offReplays++;
            a->offTrial++;
        } else {
            tag.off &= (uint8_t)~bit;
        }
    }
    countArms(a, tag.off);
    return tag;
}

void rd_ab_collect(RdAb *a, const RdAbIn *in)
{
    if (a->mode != RD_AB_AUTO || a->stopped || in->tag.stage != a->stage ||
        in->tag.epoch != a->epoch) {
        return;
    }
    const bool measurable = in->valid && !in->keep && in->work > 0.0 && in->gpuMs > 0.0;
    a->usedNow |= in->used;
    if (in->tag.trial == 0u) {
        if (measurable && in->used != 0u) {
            histPush(a->hist, &a->histN, &a->histAt, in->gpuMs);
        }
        return;
    }
    RdAbTrial *t = &a->t;
    if (!t->active || in->tag.trial != t->serial) {
        return; /* an ended trial's */
    }
    const uint8_t bit = (uint8_t)(1u << t->subject);
    if (!measurable || !(in->used & bit)) {
        t->notMeasured++;
        t->havePend = false;
        return;
    }
    histPush(t->hist, &t->histN, &t->histAt, in->gpuMs);
    const bool off = (in->tag.off & bit) != 0u;
    if (in->tag.seq % 2u == 0u) {
        t->havePend = true;
        t->pendSeq = in->tag.seq;
        t->pendOff = off;
        t->pendWork = in->work;
        t->pendMs = in->gpuMs;
        return;
    }
    if (!t->havePend || t->pendSeq + 1u != in->tag.seq || t->pendOff == off) {
        t->havePend = false;
        t->notMeasured++;
        return;
    }
    t->havePend = false;
    const double wMax = in->work > t->pendWork ? in->work : t->pendWork;
    if (fabs(in->work - t->pendWork) > RD_AB_WORK_TOL * wMax) {
        t->dropWork++;
        return;
    }
    if (t->histN < RD_AB_MEDIAN_MIN) {
        t->notMeasured += 2u; /* no running median yet */
        return;
    }
    const double med = medianOf(t->hist, t->histN);
    if (in->gpuMs > RD_AB_LONG * med || t->pendMs > RD_AB_LONG * med) {
        t->dropLong++;
        return;
    }
    const double mThis = in->gpuMs / in->work, mPend = t->pendMs / t->pendWork;
    const double mOn = off ? mPend : mThis, mOff = off ? mThis : mPend;
    t->r[t->pairs] = log(mOn / mOff);
    t->mOn[t->pairs] = mOn;
    t->mOff[t->pairs] = mOff;
    t->pairs++;
    if (t->pairs == RD_AB_EARLY) {
        double m;
        uint32_t fOn, fOff;
        if (judge(t, t->pairs, true, &m, &fOn, &fOff) != RD_AB_V_NONE) {
            trialEnd(a, NULL, false, true);
        }
    } else if (t->pairs == RD_AB_PAIRS) {
        trialEnd(a, NULL, false, true);
    }
}

void rd_ab_set_scenery(RdAb *a, bool scenery)
{
    a->scenery = scenery;
}

void rd_ab_forget(RdAb *a, const char *why)
{
    trialEnd(a, "the renderer was made again", false, false);
    a->nMem = 0;
    for (int i = 0; i < a->nSubj; i++) {
        a->subj[i].carry = RD_AB_V_NONE;
        a->subj[i].carryStage = 0;
    }
    a->stage = -1; /* the next replay's stage begins again */
    a->usedNow = 0;
    a->histN = a->histAt = 0;
    a->epoch++;
    line(a,
         "A/B: the renderer was made again (%s): every verdict forgotten, each attempt with it "
         "until it is timed once again on this renderer",
         why != NULL ? why : "?");
}

double rd_ab_scene_ms(double spanMs, double displayWaitMs, double presentMs)
{
    const double ms =
        spanMs - (displayWaitMs > 0.0 ? displayWaitMs : 0.0) - (presentMs > 0.0 ? presentMs : 0.0);
    return ms > 0.0 ? ms : 0.0;
}

void rd_ab_stop(RdAb *a, const char *why)
{
    if (a->stopped) {
        return;
    }
    a->stopped = true;
    snprintf(a->stopWhy, sizeof(a->stopWhy), "%s", why != NULL ? why : "?");
    a->t.active = false;
    char names[RD_AB_SUBJECTS * 52] = "";
    size_t at = 0;
    for (int i = 0; i < a->nSubj && at < sizeof(names); i++) {
        const int k =
            snprintf(names + at, sizeof(names) - at, "%s%s", i ? ", " : "", a->subj[i].name);
        at = k < 0 ? sizeof(names) : at + (size_t)k;
    }
    line(a, "A/B: every trial stopped for the run (%s): each attempt at its off arm from here (%s)",
         a->stopWhy, a->nSubj ? names : "no attempt");
}

/* snprintf into out at *at, *at kept within cap - 1 */
static void put(char *out, size_t cap, size_t *at, const char *fmt, ...)
{
    if (*at + 1u >= cap) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(out + *at, cap - *at, fmt, ap);
    va_end(ap);
    if (w > 0) {
        *at = *at + (size_t)w < cap ? *at + (size_t)w : cap - 1u;
    }
}

size_t rd_ab_start_text(const RdAb *a, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    size_t at = 0;
    out[0] = '\0';
    put(out, cap, &at, "A/B: %s%s%s; may try:", rd_ab_mode_name(a->mode),
        a->stopped ? ", stopped: " : "", a->stopped ? a->stopWhy : "");
    int shown = 0;
    for (int i = 0; i < a->nSubj; i++) {
        if (a->subj[i].allowed) {
            put(out, cap, &at, "%s %s", shown++ ? "," : "", a->subj[i].name);
        }
    }
    if (!shown) {
        put(out, cap, &at, " none");
    }
    int notShown = 0;
    for (int i = 0; i < a->nSubj; i++) {
        if (!a->subj[i].allowed) {
            put(out, cap, &at, "%s %s",
                notShown++ ? "," : "; not tried (off here):", a->subj[i].name);
        }
    }
    if (a->mode == RD_AB_AUTO) {
        put(out, cap, &at,
            "; each attempt timed once a run, in the first stage of play or a cutscene (not the "
            "title, the logo, a menu or a movie) where it has work, %.1f s into it, one at a "
            "time, at most %u trials a run, its "
            "replays with, without, without, with; a pair two replays in a row whose vertices "
            "and triangles are within %.0f %%, measured as the scene's GPU time (the present and its display wait left out) over them, dropped with a "
            "picture over %.1f times the running median (of %u at least); verdict at %u pairs "
            "all one way by %.0f %% or more, else at %u: on with a median %.0f %% or more cheaper "
            "with it and %u of %u cheaper with it, else off; no verdict under %u pairs in %u "
            "replays; the verdict kept for every stage for the rest of the run; with it until "
            "then, and timed again in the next stage after a trial with no verdict",
            RD_AB_START_MS / 1000.0, (unsigned)RD_AB_TRIALS, RD_AB_WORK_TOL * 100.0, RD_AB_LONG,
            (unsigned)RD_AB_MEDIAN_MIN, (unsigned)RD_AB_EARLY, RD_AB_GAIN * 100.0,
            (unsigned)RD_AB_PAIRS, RD_AB_GAIN * 100.0, (unsigned)RD_AB_FAVOUR,
            (unsigned)RD_AB_PAIRS, (unsigned)RD_AB_PAIRS_MIN, (unsigned)RD_AB_GIVE_UP);
    }
    return at;
}

size_t rd_ab_stats_text(const RdAb *a, char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    size_t at = 0;
    out[0] = '\0';
    put(out, cap, &at, "A/B (%s):",
        a->mode == RD_AB_AUTO        ? "the judge"
        : a->mode == RD_AB_FIXED_ON  ? "on"
        : a->mode == RD_AB_FIXED_OFF ? "off"
                                     : "alt");
    if (a->nSubj == 0) {
        put(out, cap, &at, " no attempt");
    }
    for (int i = 0; i < a->nSubj; i++) {
        const RdAbSubject *s = &a->subj[i];
        put(out, cap, &at,
            "%s %s: %llu replays with it, %llu without (%u trials, %u stages on, %u off)",
            i ? ";" : "", s->name, (unsigned long long)s->armOn, (unsigned long long)s->armOff,
            (unsigned)s->trials, (unsigned)s->stagesOn, (unsigned)s->stagesOff);
    }
    return at;
}
