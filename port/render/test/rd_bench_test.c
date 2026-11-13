/* rd_bench_test: the rc bench's classes and masks (rd_bench.h) over a frame
 * built by hand, no device.
 *
 * The frame holds, list by list, a command of every class the bench knocks
 * out, and the table kWant gives each command's classes as the rules in
 * rd_bench.h say them (an action has RD_BENCH_C_ACTION; a state command,
 * a NOP and the deferred text none).  Checked:
 *   - rd_bench_classify gives exactly kWant (its texture sources and list
 *     0's screen boxes from the test's own functions);
 *   - each class's mask makes NOPs of exactly the commands kWant puts in
 *     it, the others left byte for byte;
 *   - a list's mask takes that list's actions and none of its state;
 *   - the baseline's mask makes the lists whole again;
 *   - rd_bench_variants without a device: the baseline, each list with an
 *     action, each class with a command, in the table's order, no switch
 *     (the shadow chain's draw-at-a-time rows after the levels': the count's
 *     draw, the level the frame has, SHADOW0, and its composite's source,
 *     SHADOW2).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_bench.h"

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

#define B(c) RD_BENCH_BIT(RD_BENCH_C_##c)
#define ACT B(ACTION)

enum { TEX_SHADOW2 = 100, TEX_WORK3 = 101, TEX_WORK2 = 102 };

#define TEMP_COUNT 0x10014u

typedef struct Want {
    int l;
    uint8_t type;
    uint32_t u0, u1;
    uint8_t b0;
    uint32_t cls;
} Want;

static uint32_t T(RdTargetId t)
{
    return rd_target(t).id;
}

static uint32_t srcOf(uint32_t tex, void *user)
{
    (void)user;
    switch (tex) {
    case TEX_SHADOW2:
        return T(RD_TARGET_SHADOW2);
    case TEX_WORK3:
        return T(RD_TARGET_WORK3);
    case TEX_WORK2:
        return T(RD_TARGET_WORK2);
    default:
        return 0;
    }
}

/* mesh 7 covers half its target, every other draw a tenth */
static double areaOf(const RdFrame *f, const RdCmd *c, const RdStateBlock *st, void *user)
{
    (void)f;
    (void)st;
    (void)user;
    return c->u[0] == 7 ? 0.5 : 0.1;
}

int main(void)
{
    const uint32_t scene = T(RD_TARGET_SCENE), gs = 848u | (448u << 16);
    /* u1 of a TARGET: the depth; of a POST_STUB: the RdPostRec's offset (0) */
    const Want kWant[] = {
        {0, RDC_TARGET, scene, scene, 0, 0},
        {0, RDC_CLEAR, scene, 0, 0, ACT},
        {0, RDC_TEST, 0, 0, 0, 0},
        {0, RDC_MESH, 5, 0, 0, ACT},
        {0, RDC_TEST, 0, 0, 1, 0},
        {0, RDC_MESH, 6, 0, 0, ACT | B(L0_ALPHA)},
        {0, RDC_TEST, 0, 0, 0, 0},
        {0, RDC_MESH, 7, 0, 0, ACT | B(L0_LARGE)},
        {0, RDC_ABE, 0, 0, 1, 0},
        {0, RDC_SKINNED, 8, 0, 0, ACT | B(L0_ALPHA)},
        {0, RDC_ABE, 0, 0, 0, 0},
        {3, RDC_TARGET, TEMP_COUNT, scene, 0, 0},
        {3, RDC_SHADOW_RESET, 0, 0, 0, ACT | B(WINDOW)},
        {3, RDC_SHADOW_STRIP, 0, 0, 0, ACT | B(VOLUMES)},
        {3, RDC_SHADOW_RESOLVE, 0, 0, 0, ACT | B(WINDOW) | B(RESOLVE)},
        {3, RDC_TARGET, 0, 0, 0, 0}, /* SHADOW0 below */
        {3, RDC_TEXTURE, TEX_SHADOW2, 0, 0, 0},
        {3, RDC_SCREEN, 0, 0, 0, ACT | B(LEVELS) | B(LEVEL0)},
        {3, RDC_TARGET, scene, scene, 0, 0},
        {3, RDC_TEXTURE, TEX_SHADOW2, 0, 0, 0},
        {3, RDC_SCREEN, 0, 0, 0, ACT | B(COMPOSITES) | B(COMP2)},
        {4, RDC_TEXTURE_OFF, 0, 0, 0, 0},
        {4, RDC_POST_STUB, 0, 0, RD_POST_FOG, ACT | B(FOG)},
        {4, RDC_SCREEN, 0, 0, 0, ACT | B(FOG)},
        {4, RDC_TARGET, scene, scene, 0, 0},
        {4, RDC_SCREEN, 0, 0, 0, ACT},
        {7, RDC_TARGET, scene, scene, 0, 0},
        {7, RDC_TEXTURE_OFF, 0, 0, 0, 0},
        {7, RDC_POST_STUB, 0, 0, RD_POST_FLARE, ACT | B(MASK)},
        {7, RDC_TARGET, 0, scene, 0, 0}, /* WORK2 below */
        {7, RDC_POST_STUB, 0, 0, RD_POST_FLARE, ACT | B(LAMPS_SETUP)},
        {7, RDC_MESH, 9, 0, 0, ACT | B(LAMPS)},
        {7, RDC_TARGET, 0, 0, 0, 0}, /* WORK0 below */
        {7, RDC_TEXTURE, TEX_WORK2, 0, 0, 0},
        {7, RDC_POST_STUB, 0, 0, RD_POST_FLARE, ACT | B(CHAIN)},
        {7, RDC_TARGET, 0, 0, 0, 0}, /* WORK3 below */
        {7, RDC_POST_STUB, 0, 0, RD_POST_EYE_BLUR, ACT | B(CHAIN)},
        {7, RDC_TARGET, scene, scene, 0, 0},
        {7, RDC_TEXTURE, TEX_WORK3, 0, 0, 0},
        {7, RDC_POST_STUB, 0, 0, RD_POST_FLARE, ACT | B(PASTE)},
        {7, RDC_TARGET, 0, 0, 0, 0}, /* AURA_WORK below: the aura by its target */
        {7, RDC_SCREEN, 0, 0, 0, ACT | B(AURA)},
        {11, RDC_TARGET, scene, scene, 0, 0},
        {11, RDC_TEXTURE, TEX_WORK3, 0, 0, 0},
        {11, RDC_SCREEN, 0, 0, 0, ACT | B(PASTE)},
        {11, RDC_OVERLAY_TEXT, 0, 0, 0, 0},
        {11, RDC_NOP, 0, 0, 0, 0},
        {12, RDC_TARGET, 0, 0, 0, 0}, /* DISPLAY below */
        {12, RDC_POST_STUB, 0, 0, RD_POST_REDUCTION, ACT | B(REDUCTION)},
        {12, RDC_UPSCALE_POINT, 0, 0, 0, 0},
    };
    const int nWant = (int)(sizeof(kWant) / sizeof(kWant[0]));

    /* the targets the table leaves 0 */
    static const struct {
        int at;
        RdTargetId t;
    } kTargets[] = {{15, RD_TARGET_SHADOW0}, {29, RD_TARGET_WORK2},     {32, RD_TARGET_WORK0},
                    {35, RD_TARGET_WORK3},   {40, RD_TARGET_AURA_WORK}, {47, RD_TARGET_DISPLAY}};

    static RdFrame f;
    static RdCmd store[RD_LIST_COUNT][32];
    static uint32_t clsStore[RD_LIST_COUNT][32];
    static RdCmd origStore[RD_LIST_COUNT][32];
    uint32_t *cls[RD_LIST_COUNT];
    RdCmd *orig[RD_LIST_COUNT];
    int at[64][2]; /* the table's row -> list, index */
    memset(&f, 0, sizeof(f));
    static uint8_t payload[sizeof(RdPostRec)];
    memset(payload, 0, sizeof(payload)); /* src 0: the state's texture names the source */
    f.payload = payload;
    f.payloadSize = (uint32_t)sizeof(payload);
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        f.lists[l].cmds = store[l];
        f.lists[l].cap = 32;
        cls[l] = clsStore[l];
        orig[l] = origStore[l];
    }
    for (int r = 0; r < nWant; r++) {
        const Want *w = &kWant[r];
        RdCmd *c = &f.lists[w->l].cmds[f.lists[w->l].count];
        memset(c, 0, sizeof(*c));
        c->type = w->type;
        c->u[0] = w->u0;
        c->u[1] = w->u1;
        c->b[0] = w->b0;
        if (w->type == RDC_TARGET) {
            c->u[2] = gs;
        }
        if (w->type == RDC_TEXTURE) {
            c->b[0] = 0; /* RdTexFn */
        }
        at[r][0] = w->l;
        at[r][1] = (int)f.lists[w->l].count;
        f.lists[w->l].count++;
    }
    for (size_t k = 0; k < sizeof(kTargets) / sizeof(kTargets[0]); k++) {
        f.lists[at[kTargets[k].at][0]].cmds[at[kTargets[k].at][1]].u[0] = T(kTargets[k].t);
    }
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        memcpy(orig[l], f.lists[l].cmds, sizeof(RdCmd) * f.lists[l].count);
    }

    rd_bench_classify(&f, srcOf, areaOf, NULL, cls);
    for (int r = 0; r < nWant; r++) {
        const uint32_t got = cls[at[r][0]][at[r][1]];
        CHECK(got == kWant[r].cls, "row %d (list %d command %d, type %u): classes 0x%x, want 0x%x",
              r, at[r][0], at[r][1], kWant[r].type, got, kWant[r].cls);
    }

    /* each class's mask: exactly its commands */
    for (int c = 1; c < RD_BENCH_C_COUNT; c++) {
        const uint32_t n = rd_bench_mask(&f, orig, cls, RD_BENCH_BIT(c), 0);
        uint32_t want = 0;
        for (int r = 0; r < nWant; r++) {
            const RdCmd *got = &f.lists[at[r][0]].cmds[at[r][1]];
            const RdCmd *was = &orig[at[r][0]][at[r][1]];
            if (kWant[r].cls & RD_BENCH_BIT(c)) {
                want++;
                CHECK(got->type == RDC_NOP, "class %d: row %d not a NOP", c, r);
                RdCmd x = *got;
                x.type = was->type;
                CHECK(memcmp(&x, was, sizeof(x)) == 0, "class %d: row %d changed past its type", c,
                      r);
            } else {
                CHECK(memcmp(got, was, sizeof(*got)) == 0, "class %d: row %d changed", c, r);
            }
        }
        CHECK(n == want, "class %d: %u NOPs, want %u", c, n, want);
    }
    /* list 7's mask: its actions, none of its state */
    {
        const uint32_t n = rd_bench_mask(&f, orig, cls, 0, 1u << 7);
        uint32_t want = 0;
        for (int r = 0; r < nWant; r++) {
            const RdCmd *got = &f.lists[at[r][0]].cmds[at[r][1]];
            const bool knocked = kWant[r].l == 7 && (kWant[r].cls & ACT);
            want += knocked;
            CHECK(knocked ? got->type == RDC_NOP : got->type == kWant[r].type,
                  "list 7: row %d type %u", r, got->type);
        }
        CHECK(n == want && want == 7, "list 7: %u NOPs, want %u (7)", n, want);
    }
    /* the baseline: whole again */
    CHECK(rd_bench_mask(&f, orig, cls, 0, 0) == 0, "the baseline makes no NOP");
    for (int l = 0; l < RD_LIST_COUNT; l++) {
        CHECK(memcmp(f.lists[l].cmds, orig[l], sizeof(RdCmd) * f.lists[l].count) == 0,
              "list %d whole again", l);
    }

    /* the variants without a device */
    {
        static RdBenchFrame b;
        memset(&b, 0, sizeof(b));
        b.f = f;
        for (int l = 0; l < RD_LIST_COUNT; l++) {
            b.orig[l] = orig[l];
            b.cls[l] = cls[l];
            for (uint32_t i = 0; i < f.lists[l].count; i++) {
                for (int c = 0; c < RD_BENCH_C_COUNT; c++) {
                    b.classCount[c] += (cls[l][i] >> c) & 1u;
                }
                b.listActions[l] += cls[l][i] & 1u;
            }
        }
        static const char *const kNames[] = {
            "baseline",
            "without list 0",
            "without list 3",
            "without list 4",
            "without list 7",
            "without list 11",
            "without list 12",
            "without the shadow volumes' draws",
            "without the shadow window (reset, volumes, count, levels)",
            "without the shadow levels",
            "without the shadow count's draw (the levels read the count as it was)",
            "without the shadow level SHADOW0",
            "without the shadow composite of SHADOW2 (the first)",
            "without the shadow composites",
            "without the fog (and the crack fill it arms)",
            "without the lamps' set-up (WORK2's clears, the depth fill)",
            "without the lamps' meshes",
            "without the glow mask",
            "without the blur chain",
            "without the glow paste",
            "without the aura",
            "without the reduction (its draws: DISPLAY not drawn)",
            "list 0 without its alpha-tested and blended draws",
            "list 0 without its draws over a quarter of the picture",
        };
        const int want = (int)(sizeof(kNames) / sizeof(kNames[0]));
        RdBenchVariant v[48];
        const int n = rd_bench_variants(&b, v, 48);
        CHECK(n == want, "%d variants, want %d", n, want);
        for (int i = 0; i < n && i < want; i++) {
            CHECK(strcmp(v[i].name, kNames[i]) == 0, "variant %d: \"%s\", want \"%s\"", i,
                  v[i].name, kNames[i]);
            CHECK(v[i].sw == RD_BENCH_S_NONE, "variant %d: a switch without a device", i);
        }
        CHECK(n > 8 &&
                  v[8].classes ==
                      (RD_BENCH_BIT(RD_BENCH_C_WINDOW) | RD_BENCH_BIT(RD_BENCH_C_VOLUMES) |
                       RD_BENCH_BIT(RD_BENCH_C_LEVELS)) &&
                  v[8].sum == 2,
              "the window's knock-out holds the volumes and the levels, in the parts' sum");
        CHECK(n > 4 && v[4].lists == (1u << 7) && v[4].sum == 1 && v[4].knock,
              "list 7's knock-out");
        CHECK(v[0].knock == 0 && v[0].classes == 0 && v[0].lists == 0, "the baseline");
        /* the list counts the variants took: one action each in a list */
        CHECK(b.listActions[11] == 1 && b.listActions[12] == 1, "lists 11 and 12: one action");
    }

    if (s_fail) {
        printf("rd_bench_test: %d failures\n", s_fail);
        return 1;
    }
    printf("rd_bench_test: ok\n");
    return 0;
}
