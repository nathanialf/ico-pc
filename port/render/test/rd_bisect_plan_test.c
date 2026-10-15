/* rd_bisect_plan_test.c: the loss bisect's second-level planner
 * (port/render/rd_bisect_plan.h), against oracles:
 *   - "lost iff the prefix reaches draw D" for D in {1, 731, 1312} of
 *     1,312 draws: D is found within 12 steps (the masked step and 11
 *     halvings), then D alone and its classes run, all within 16;
 *   - the masked step clean is reported as such;
 *   - a killed step counts as lost; a step left without a verdict is
 *     planned again;
 *   - prefix verdicts that contradict each other end the plan and say so;
 *   - 1000 seeded random oracles end within 16 steps;
 *   - the state file's names parse back. */
#include <stdio.h>
#include <string.h>

#include "rd_bisect_plan.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL: ");                                                                      \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* the oracle: a draw D that faults with every draw before it; the marks
 * masked: maskClean */
typedef struct Oracle {
    uint32_t d;
    int maskClean;
    int aloneFaults;
    uint32_t seed; /* nonzero: random verdicts */
} Oracle;

static uint32_t rnd(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 16;
}

static RdBisectVerdict verdictOf(Oracle *o, const RdBisectStep *s, RdBisectKey *key)
{
    memset(key, 0, sizeof(*key));
    key->valid = 1;
    key->vu = 1;
    key->vs = 5;
    key->clip = 1;
    key->skinned = s->drawLast % 2;
    key->tex = 65537;
    if (o->seed) {
        return (rnd(&o->seed) & 1) ? RD_BISECT_V_LOST : RD_BISECT_V_CLEAN;
    }
    switch (s->kind) {
    case RD_BISECT_STEP_MASK:
        return o->maskClean ? RD_BISECT_V_CLEAN : RD_BISECT_V_LOST;
    case RD_BISECT_STEP_ALONE:
        return o->aloneFaults ? RD_BISECT_V_LOST : RD_BISECT_V_CLEAN;
    default:
        return s->drawLast >= o->d ? RD_BISECT_V_LOST : RD_BISECT_V_CLEAN;
    }
}

/* runs the plan to its end; returns the steps taken, *foundAt the step
 * (1-based) after which the search had D */
static uint32_t run(RdBisectPlan *p, Oracle *o, RdBisectResult2 *res, uint32_t *foundAt)
{
    RdBisectStep st;
    uint32_t steps = 0;
    *foundAt = 0;
    while (rd_bisect_plan_next(p, &st, res) == RD_BISECT_END_NO) {
        RdBisectKey key;
        const RdBisectVerdict v = verdictOf(o, &st, &key);
        rd_bisect_plan_set(p, p->count - 1, v, &key);
        steps++;
        RdBisectResult2 r;
        rd_bisect_plan_result(p, &r);
        if (!*foundAt && r.draw) {
            *foundAt = steps;
        }
        if (steps > 40) {
            break;
        }
    }
    return steps;
}

static void oracleCases(void)
{
    static const uint32_t ds[3] = {1, 731, 1312};
    for (int i = 0; i < 3; i++) {
        RdBisectPlan p;
        rd_bisect_plan_init(&p, 1312);
        Oracle o = {ds[i], 0, 1, 0};
        RdBisectResult2 res;
        uint32_t found;
        const uint32_t steps = run(&p, &o, &res, &found);
        CHECK(res.draw == ds[i], "D %u: found %u", ds[i], res.draw);
        CHECK(found && found <= 12, "D %u: found after %u steps", ds[i], found);
        CHECK(steps <= RD_BISECT_PLAN_STEPS, "D %u: %u steps", ds[i], steps);
        CHECK(res.end == RD_BISECT_END_DONE || res.end == RD_BISECT_END_BOUND, "D %u: end %d",
              ds[i], (int)res.end);
        CHECK(res.alone == 1 && strcmp(rd_bisect_alone_text(&res), "faults alone") == 0,
              "D %u: alone %d", ds[i], res.alone);
        CHECK(res.maskClean == 0, "D %u: masked %d", ds[i], res.maskClean);
        CHECK(p.step[0].kind == RD_BISECT_STEP_MASK && p.step[0].drawLast == 1312 &&
                  p.step[0].filter == RD_BISECT_MASK_MARKS,
              "D %u: the first step is not the masked pass", ds[i]);
        printf("rd_bisect_plan: D %u found after step %u, %u steps\n", ds[i], found, steps);
    }
    /* the marks masked: clean; the draw does not fault alone */
    {
        RdBisectPlan p;
        rd_bisect_plan_init(&p, 1312);
        Oracle o = {656, 1, 0, 0};
        RdBisectResult2 res;
        uint32_t found;
        run(&p, &o, &res, &found);
        CHECK(res.draw == 656 && res.maskClean == 1 && res.alone == 0, "masked clean: %u %d %d",
              res.draw, res.maskClean, res.alone);
        CHECK(strcmp(rd_bisect_mask_text(&res), "clean with the marks masked") == 0 &&
                  strcmp(rd_bisect_alone_text(&res), "does not fault alone") == 0,
              "masked clean texts");
    }
    /* one draw */
    {
        RdBisectPlan p;
        rd_bisect_plan_init(&p, 1);
        Oracle o = {1, 0, 1, 0};
        RdBisectResult2 res;
        uint32_t found;
        const uint32_t steps = run(&p, &o, &res, &found);
        CHECK(res.draw == 1 && steps <= RD_BISECT_PLAN_STEPS, "one draw: %u in %u", res.draw,
              steps);
    }
    /* no draw */
    {
        RdBisectPlan p;
        RdBisectStep st;
        RdBisectResult2 res;
        rd_bisect_plan_init(&p, 0);
        CHECK(rd_bisect_plan_next(&p, &st, &res) == RD_BISECT_END_NOTHING, "no draw");
    }
}

static void resumeCases(void)
{
    /* a step without a verdict is planned again; killed counts as lost */
    RdBisectPlan p;
    RdBisectStep a, b;
    RdBisectResult2 res;
    rd_bisect_plan_init(&p, 100);
    CHECK(rd_bisect_plan_next(&p, &a, &res) == RD_BISECT_END_NO, "first");
    CHECK(rd_bisect_plan_next(&p, &b, &res) == RD_BISECT_END_NO && p.count == 1 &&
              memcmp(&a, &b, sizeof(a)) == 0,
          "a step without a verdict is not planned again");
    rd_bisect_plan_set(&p, 0, RD_BISECT_V_LOST, NULL);
    CHECK(rd_bisect_plan_next(&p, &a, &res) == RD_BISECT_END_NO &&
              a.kind == RD_BISECT_STEP_PREFIX && a.drawLast == 50,
          "prefix 1..50: %u", a.drawLast);
    rd_bisect_plan_set(&p, 1, RD_BISECT_V_KILLED, NULL);
    CHECK(rd_bisect_plan_next(&p, &a, &res) == RD_BISECT_END_NO && a.drawLast == 25,
          "killed counts as lost: next 1..%u", a.drawLast);
    /* a contradiction: 1..25 clean although 1..50 was lost is fine; 1..60
     * clean after 1..50 lost is not (forced here by editing the plan) */
    rd_bisect_plan_set(&p, 2, RD_BISECT_V_CLEAN, NULL);
    p.step[p.count] = (RdBisectStep){RD_BISECT_STEP_PREFIX, RD_BISECT_ALL, 1, 60, 0};
    p.verdict[p.count] = RD_BISECT_V_CLEAN;
    p.count++;
    CHECK(rd_bisect_plan_next(&p, &a, &res) == RD_BISECT_END_NOT_MONOTONE &&
              strcmp(rd_bisect_alone_text(&res), "not finished") == 0,
          "contradicting prefixes end the plan: %d", (int)res.end);
    /* the whole pass clean again (AGAIN): not monotone */
    {
        RdBisectPlan q;
        rd_bisect_plan_init(&q, 8);
        Oracle o = {9, 0, 0, 0}; /* no prefix of this level loses */
        uint32_t found;
        run(&q, &o, &res, &found);
        CHECK(res.end == RD_BISECT_END_NOT_MONOTONE, "the pass clean again: end %d", (int)res.end);
    }
}

static void randomCases(void)
{
    for (uint32_t seed = 1; seed <= 1000; seed++) {
        RdBisectPlan p;
        rd_bisect_plan_init(&p, 1 + seed * 7u % 5000u);
        Oracle o = {0, 0, 0, seed};
        RdBisectResult2 res;
        uint32_t found;
        const uint32_t steps = run(&p, &o, &res, &found);
        CHECK(steps <= RD_BISECT_PLAN_STEPS && res.end != RD_BISECT_END_NO,
              "random seed %u: %u steps, end %d", seed, steps, (int)res.end);
    }
}

static void names(void)
{
    for (int k = RD_BISECT_STEP_MASK; k <= RD_BISECT_STEP_AGAIN; k++) {
        CHECK(rd_bisect_kind_parse(rd_bisect_kind_name((uint8_t)k)) == k, "kind %d", k);
    }
    for (int f = RD_BISECT_ALL; f <= RD_BISECT_TEX_ONLY; f++) {
        CHECK(rd_bisect_filter_parse(rd_bisect_filter_name((uint8_t)f)) == f, "filter %d", f);
    }
    CHECK(rd_bisect_kind_parse("prefixx") == -1 && rd_bisect_filter_parse("") == -1, "unknown");
    char t[96];
    const RdBisectStep s = {RD_BISECT_STEP_PREFIX, RD_BISECT_ALL, 1, 656, 0};
    rd_bisect_step_text(&s, 1312, t, sizeof(t));
    CHECK(strcmp(t, "draws 1..656 of 1312") == 0, "step text %s", t);
}

int main(void)
{
    oracleCases();
    resumeCases();
    randomCases();
    names();
    printf("rd_bisect_plan_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
