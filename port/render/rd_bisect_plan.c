/* rd_bisect_plan.c: the loss bisect's second level, the planner
 * (rd_bisect_plan.h). */
#include "rd_bisect_plan.h"

#include <stdio.h>
#include <string.h>

void rd_bisect_plan_init(RdBisectPlan *p, uint32_t draws)
{
    memset(p, 0, sizeof(*p));
    p->draws = draws;
}

static bool lostV(uint8_t v)
{
    return v == RD_BISECT_V_LOST || v == RD_BISECT_V_KILLED;
}

/* The search's bounds from the prefix verdicts: lo the longest clean
 * prefix (0: passes 1 .. P - 1 ran clean in the first level), hi the
 * shortest lost one (N: the first level lost the whole pass); *again1 when
 * a step of this level lost the prefix 1 .. N itself. */
static void bounds(const RdBisectPlan *p, uint32_t *lo, uint32_t *hi, bool *againLost)
{
    *lo = 0;
    *hi = p->draws;
    *againLost = false;
    for (uint32_t i = 0; i < p->count && i < RD_BISECT_PLAN_STEPS; i++) {
        const RdBisectStep *s = &p->step[i];
        if ((s->kind != RD_BISECT_STEP_PREFIX && s->kind != RD_BISECT_STEP_AGAIN) ||
            p->verdict[i] == RD_BISECT_V_NONE) {
            continue;
        }
        const uint32_t m = s->drawLast;
        if (lostV(p->verdict[i])) {
            *hi = m < *hi ? m : *hi;
            *againLost = *againLost || m >= p->draws;
        } else {
            *lo = m > *lo ? m : *lo;
        }
    }
}

static int findStep(const RdBisectPlan *p, uint8_t kind, uint8_t filter)
{
    for (uint32_t i = 0; i < p->count && i < RD_BISECT_PLAN_STEPS; i++) {
        if (p->step[i].kind == kind &&
            (kind != RD_BISECT_STEP_CLASS || p->step[i].filter == filter)) {
            return (int)i;
        }
    }
    return -1;
}

/* draw d's note from the steps whose last draw it is */
static const RdBisectKey *keyOf(const RdBisectPlan *p, uint32_t d)
{
    for (uint32_t i = 0; i < p->count && i < RD_BISECT_PLAN_STEPS; i++) {
        if (p->step[i].drawLast == d && p->key[i].valid) {
            return &p->key[i];
        }
    }
    return NULL;
}

void rd_bisect_plan_result(const RdBisectPlan *p, RdBisectResult2 *res)
{
    uint32_t lo, hi;
    bool againLost;
    bounds(p, &lo, &hi, &againLost);
    memset(res, 0, sizeof(*res));
    res->maskClean = -1;
    res->alone = -1;
    if (p->draws == 0) {
        res->end = RD_BISECT_END_NOTHING;
        return;
    }
    if (lo >= hi) {
        res->end = RD_BISECT_END_NOT_MONOTONE;
    } else if (hi - lo <= 1) {
        res->draw = hi;
    }
    const int m = findStep(p, RD_BISECT_STEP_MASK, 0);
    if (m >= 0 && p->verdict[m] != RD_BISECT_V_NONE) {
        res->maskClean = lostV(p->verdict[m]) ? 0 : 1;
    }
    const int a = findStep(p, RD_BISECT_STEP_ALONE, 0);
    if (a >= 0 && p->verdict[a] != RD_BISECT_V_NONE && p->step[a].drawFirst == res->draw) {
        res->alone = lostV(p->verdict[a]) ? 1 : 0;
    }
}

static RdBisectEnd endWith(const RdBisectPlan *p, RdBisectEnd e, RdBisectResult2 *res)
{
    rd_bisect_plan_result(p, res);
    if (res->end == RD_BISECT_END_NO) {
        res->end = e;
    }
    return res->end;
}

static RdBisectEnd plan(RdBisectPlan *p, RdBisectStep *step, RdBisectResult2 *res, uint8_t kind,
                        uint32_t first, uint32_t last, uint8_t filter, uint32_t arg)
{
    if (p->count >= RD_BISECT_PLAN_STEPS) {
        return endWith(p, RD_BISECT_END_BOUND, res);
    }
    RdBisectStep *s = &p->step[p->count];
    memset(s, 0, sizeof(*s));
    s->kind = kind;
    s->filter = filter;
    s->drawFirst = first;
    s->drawLast = last;
    s->arg = arg;
    p->verdict[p->count] = RD_BISECT_V_NONE;
    memset(&p->key[p->count], 0, sizeof(p->key[0]));
    p->count++;
    *step = *s;
    memset(res, 0, sizeof(*res));
    return RD_BISECT_END_NO;
}

RdBisectEnd rd_bisect_plan_next(RdBisectPlan *p, RdBisectStep *step, RdBisectResult2 *res)
{
    if (p->draws == 0) {
        return endWith(p, RD_BISECT_END_NOTHING, res);
    }
    if (p->count > RD_BISECT_PLAN_STEPS) {
        p->count = RD_BISECT_PLAN_STEPS;
    }
    /* a step without a verdict (its renderer did not start): again */
    if (p->count > 0 && p->verdict[p->count - 1] == RD_BISECT_V_NONE) {
        *step = p->step[p->count - 1];
        memset(res, 0, sizeof(*res));
        return RD_BISECT_END_NO;
    }
    uint32_t lo, hi;
    bool againLost;
    bounds(p, &lo, &hi, &againLost);
    if (lo >= hi) {
        return endWith(p, RD_BISECT_END_NOT_MONOTONE, res);
    }
    /* (a step past the bound ends the plan there: plan) */
    /* 1: the hypothesis test */
    if (findStep(p, RD_BISECT_STEP_MASK, 0) < 0) {
        return plan(p, step, res, RD_BISECT_STEP_MASK, 1, p->draws, RD_BISECT_MASK_MARKS, 0);
    }
    /* 2: the prefix search */
    if (hi - lo > 1) {
        return plan(p, step, res, RD_BISECT_STEP_PREFIX, 1, lo + (hi - lo) / 2, RD_BISECT_ALL, 0);
    }
    const uint32_t d = hi;
    /* 3: the draw alone */
    if (findStep(p, RD_BISECT_STEP_ALONE, 0) < 0) {
        return plan(p, step, res, RD_BISECT_STEP_ALONE, d, d, RD_BISECT_ALL, 0);
    }
    /* 4: the classes of draw d */
    const RdBisectKey *k = keyOf(p, d);
    if (k) {
        if (findStep(p, RD_BISECT_STEP_CLASS, RD_BISECT_VS_ONLY) < 0) {
            return plan(p, step, res, RD_BISECT_STEP_CLASS, 1, d, RD_BISECT_VS_ONLY, k->vs);
        }
        if (k->vu && findStep(p, RD_BISECT_STEP_CLASS, RD_BISECT_CLIP_ONLY) < 0) {
            return plan(p, step, res, RD_BISECT_STEP_CLASS, 1, d, RD_BISECT_CLIP_ONLY, k->clip);
        }
        if (k->skinned && findStep(p, RD_BISECT_STEP_CLASS, RD_BISECT_SKINNED_ONLY) < 0) {
            return plan(p, step, res, RD_BISECT_STEP_CLASS, 1, d, RD_BISECT_SKINNED_ONLY, 0);
        }
        if (k->tex && findStep(p, RD_BISECT_STEP_CLASS, RD_BISECT_TEX_ONLY) < 0) {
            return plan(p, step, res, RD_BISECT_STEP_CLASS, 1, d, RD_BISECT_TEX_ONLY, k->tex);
        }
    }
    /* the first level's verdict on the whole pass, when nothing here lost it */
    if (d == p->draws && !againLost && findStep(p, RD_BISECT_STEP_AGAIN, 0) < 0) {
        return plan(p, step, res, RD_BISECT_STEP_AGAIN, 1, p->draws, RD_BISECT_ALL, 0);
    }
    return endWith(p, RD_BISECT_END_DONE, res);
}

void rd_bisect_plan_set(RdBisectPlan *p, uint32_t i, RdBisectVerdict v, const RdBisectKey *key)
{
    if (i >= p->count || i >= RD_BISECT_PLAN_STEPS) {
        return;
    }
    p->verdict[i] = (uint8_t)v;
    if (key) {
        p->key[i] = *key;
    }
}

const char *rd_bisect_kind_name(uint8_t kind)
{
    switch (kind) {
    case RD_BISECT_STEP_MASK:
        return "mask";
    case RD_BISECT_STEP_PREFIX:
        return "prefix";
    case RD_BISECT_STEP_ALONE:
        return "alone";
    case RD_BISECT_STEP_CLASS:
        return "class";
    case RD_BISECT_STEP_AGAIN:
        return "again";
    default:
        return "?";
    }
}

int rd_bisect_kind_parse(const char *s)
{
    for (int k = RD_BISECT_STEP_MASK; k <= RD_BISECT_STEP_AGAIN; k++) {
        if (strcmp(s, rd_bisect_kind_name((uint8_t)k)) == 0) {
            return k;
        }
    }
    return -1;
}

const char *rd_bisect_filter_name(uint8_t filter)
{
    switch (filter) {
    case RD_BISECT_ALL:
        return "all";
    case RD_BISECT_MASK_MARKS:
        return "mask_marks";
    case RD_BISECT_VS_ONLY:
        return "vs_only";
    case RD_BISECT_CLIP_ONLY:
        return "clip_only";
    case RD_BISECT_SKINNED_ONLY:
        return "skinned_only";
    case RD_BISECT_TEX_ONLY:
        return "tex_only";
    default:
        return "?";
    }
}

int rd_bisect_filter_parse(const char *s)
{
    for (int f = RD_BISECT_ALL; f <= RD_BISECT_TEX_ONLY; f++) {
        if (strcmp(s, rd_bisect_filter_name((uint8_t)f)) == 0) {
            return f;
        }
    }
    return -1;
}

void rd_bisect_step_text(const RdBisectStep *s, uint32_t draws, char *out, size_t size)
{
    char what[64] = "";
    switch (s->filter) {
    case RD_BISECT_MASK_MARKS:
        snprintf(what, sizeof(what), ", marks masked");
        break;
    case RD_BISECT_VS_ONLY:
        snprintf(what, sizeof(what), ", vertex shader %u only", (unsigned)s->arg);
        break;
    case RD_BISECT_CLIP_ONLY:
        snprintf(what, sizeof(what), ", clip mode %u only", (unsigned)s->arg);
        break;
    case RD_BISECT_SKINNED_ONLY:
        snprintf(what, sizeof(what), ", skinned only");
        break;
    case RD_BISECT_TEX_ONLY:
        snprintf(what, sizeof(what), ", texture %u only", (unsigned)s->arg);
        break;
    default:
        break;
    }
    if (s->drawFirst == s->drawLast) {
        snprintf(out, size, "draw %u of %u alone%s", (unsigned)s->drawFirst, (unsigned)draws, what);
    } else {
        snprintf(out, size, "draws %u..%u of %u%s", (unsigned)s->drawFirst, (unsigned)s->drawLast,
                 (unsigned)draws, what);
    }
}

const char *rd_bisect_alone_text(const RdBisectResult2 *r)
{
    if (r->draw == 0 || r->alone < 0 || r->end == RD_BISECT_END_NOT_MONOTONE) {
        return "not finished";
    }
    return r->alone ? "faults alone" : "does not fault alone";
}

const char *rd_bisect_mask_text(const RdBisectResult2 *r)
{
    return r->maskClean > 0    ? "clean with the marks masked"
           : r->maskClean == 0 ? "lost with the marks masked"
                               : "";
}
