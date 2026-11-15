/* rd_replay_fine.c: rd_replay_tool --fine-check: the shadow's levels at the
 * scene's scale as the GPU drew them (rd_shadowres.c, the cuts' form) against
 * the CPU reference (rd_shadowres_math.c) on the same input, level by level
 * (rd_shadow_fine_ab.cmake reads the lines; docs/port/TESTING.md).
 *
 * After the replay the count and each level's two textures are read back.
 * For level k:
 *   the step   every texel of the step's output against rd_sr_step_value on
 *              the step's own source as the GPU held it (the count for level
 *              0, the step of the level before for the others), with the
 *              values rd_shadowres.c drew it with;
 *   the tent   every texel of the level after its tent against
 *              rd_sr_tent2_texel on the GPU's step output.
 *
 * The rule, from the two shaders (port/shaders/shadowres.hlsl):
 *   the tent   the same bytes.  shadowres_tent2_ps works in whole numbers
 *              on the bytes it loads (floor(load * 255 + 0.5) gives the byte
 *              back; the weights and the rounding are integers), so nothing
 *              is left to a device.
 *   the step   the byte the reference gives, or the neighbouring byte where
 *              the reference's value before its rounding lies within E of
 *              the boundary between the two (value - floor(value) within E
 *              of 0.5), each then through MODULATE ((t col) >> 7).  The
 *              shader's sum is float arithmetic a device may fuse or reorder
 *              (SPIR-V does not forbid a fused multiply-add in
 *              m * g_param.xy + g_scale.xy, and the loads' c / 255 is a
 *              rounded float), so its value can differ from the reference's
 *              float32 value by a bounded amount:
 *                a later level (the point mode): its taps fall on whole
 *                  texels (a whole first tap, 2 texels a destination texel)
 *                  with weights of sixteenths, all exact in float32; what is
 *                  left is the rounding of 16 products of c / 255, each
 *                  within 2^-24 of its value: under 255 * 16 * 2^-23 = 0.0005
 *                  of a level.  E = 1 / 1024.
 *                the first level (the box mode): the tap's origin a is a
 *                  float of magnitude below the count's size, so one unit in
 *                  its last place is u = 2^(e - 23) with 2^e the largest power
 *                  of two not above that size (2^-12 from 2048 to 4095 texels).  A box
 *                  weight follows a with slope at most 1 / s (s the count's
 *                  texels a GS pixel) and on an axis at most 4 texels'
 *                  weights depend on a (those holding a, a + s, a + s and
 *                  a + 2 s), so an origin one unit off moves the weights by
 *                  at most 4 u / s in sum on that axis, the value by at most
 *                  255 (4 u / s.x + 4 u / s.y) levels; the products' own
 *                  rounding adds under 0.001.  E = 255 * 4 u (1 / s.x +
 *                  1 / s.y) + 1 / 1024 (0.19 of a level at 2.8 x 2.4 below
 *                  4096 texels).
 *              A texel outside the rule fails; the texels one apart inside it
 *              are counted and printed (0 on a device that evaluates the
 *              shader's float32 operations as written).
 *
 * The lines (stdout), one a level and a last one:
 *   fine check: level K: step W x H: N texels, D not the reference's bytes (all
 *     one byte's rounding, value within E.EEEE of the boundary: largest 1),
 *     O outside the rule (largest L); tent: T not the reference's (largest L);
 *     alpha sums step S, tent S
 *   fine check: 3 levels, O step texels outside the rule, T tent texels not
 *     the reference's: ok|FAILED
 *   fine check: no chain at the scene's scale in this replay (...)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_shadowres.h"

#include "rd_replay_fine.h"

/* E of a step (the head comment) */
static float stepBound(const RdSrProbe *p)
{
    if (!p->step.first) {
        return 1.0f / 1024.0f;
    }
    const uint32_t size = p->sw > p->sh ? p->sw : p->sh;
    float u = 1.0f / 8388608.0f; /* 2^-23: one unit in the last place below 1 */
    for (uint32_t pow2 = 2; pow2 <= size; pow2 *= 2u) {
        u *= 2.0f;
    }
    return 255.0f * 4.0f * u * (1.0f / p->step.sx + 1.0f / p->step.sy) + 1.0f / 1024.0f;
}

int rd_replay_fine_check(void)
{
    RdSrProbe pr[RD_SR_LEVELS];
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        if (!rd__shadow_fine_probe(k, &pr[k])) {
            printf("fine check: no chain at the scene's scale in this replay (level %d: not "
                   "drawn, or not with the cuts)\n",
                   k);
            return 2;
        }
    }
    /* the count as the first step read it */
    const RdTargetRec *tc = rd__target_rec(pr[0].count);
    if (!tc || tc->tw != pr[0].sw || tc->th != pr[0].sh) {
        printf("fine check: the count target is gone or not the step's size\n");
        return 2;
    }
    uint8_t *src = malloc((size_t)pr[0].sw * pr[0].sh * 4u);
    uint32_t rw = 0, rh = 0;
    if (!src || !rd__read_target((RdTarget){pr[0].count}, src, (size_t)pr[0].sw * pr[0].sh * 4u,
                                 &rw, &rh)) {
        printf("fine check: the count's readback failed\n");
        free(src);
        return 2;
    }
    unsigned long long outsideAll = 0, tentAll = 0;
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        const RdSrProbe *p = &pr[k];
        const size_t bytes = (size_t)p->w * p->h * 4u;
        uint8_t *step = malloc(bytes), *fin = malloc(bytes);
        if (!step || !fin || !rd__shadow_fine_read(k, false, step, bytes) ||
            !rd__shadow_fine_read(k, true, fin, bytes)) {
            printf("fine check: level %d: the readback failed\n", k);
            free(step);
            free(fin);
            free(src);
            return 2;
        }
        const float bound = stepBound(p);
        unsigned long long apart = 0, outside = 0, tent = 0, stepSum = 0, tentSum = 0;
        int outLargest = 0, tentLargest = 0;
        for (uint32_t y = 0; y < p->h; y++) {
            for (uint32_t x = 0; x < p->w; x++) {
                const uint8_t *g = step + ((size_t)y * p->w + x) * 4u;
                const uint8_t *f = fin + ((size_t)y * p->w + x) * 4u;
                float val[4];
                uint8_t ref[4];
                rd_sr_step_value(src, p->sw, p->sh, &p->step, x, y, val);
                bool isApart = false, isOut = false;
                for (int ch = 0; ch < 4; ch++) {
                    const float fl = floorf(val[ch]);
                    const uint32_t t = (uint32_t)floorf(val[ch] + 0.5f);
                    const uint8_t want = rd_sr_step_byte(t, p->step.col[ch]);
                    if (g[ch] == want) {
                        continue;
                    }
                    /* the neighbouring byte, where the value is within the
                     * bound of the boundary between the two */
                    const uint32_t other = t == (uint32_t)fl ? t + 1u : (uint32_t)fl;
                    const bool atEdge = fabsf(val[ch] - fl - 0.5f) <= bound;
                    if (atEdge && g[ch] == rd_sr_step_byte(other, p->step.col[ch])) {
                        isApart = true;
                    } else {
                        isOut = true;
                        const int d = abs((int)g[ch] - (int)want);
                        outLargest = d > outLargest ? d : outLargest;
                    }
                }
                apart += isApart ? 1u : 0u;
                outside += isOut ? 1u : 0u;
                rd_sr_tent2_texel(step, p->w, p->h, p->lx, p->ly, x, y, ref);
                bool tentOff = false;
                for (int ch = 0; ch < 4; ch++) {
                    if (f[ch] != ref[ch]) {
                        tentOff = true;
                        const int d = abs((int)f[ch] - (int)ref[ch]);
                        tentLargest = d > tentLargest ? d : tentLargest;
                    }
                }
                tent += tentOff ? 1u : 0u;
                stepSum += g[3];
                tentSum += f[3];
            }
        }
        printf("fine check: level %d: step %ux%u: %llu texels, %llu not the reference's bytes "
               "(all one byte's rounding, value within %.4f of the boundary: largest %d), %llu "
               "outside the rule (largest %d); tent: %llu not the reference's (largest %d); alpha "
               "sums step %llu, tent %llu\n",
               k, p->w, p->h, (unsigned long long)p->w * p->h, apart + outside, bound,
               apart ? 1 : 0, outside, outLargest, tent, tentLargest, stepSum, tentSum);
        outsideAll += outside;
        tentAll += tent;
        /* the next level's source: this level's step as the GPU drew it */
        free(src);
        free(fin);
        src = step;
    }
    free(src);
    const bool ok = outsideAll == 0 && tentAll == 0;
    printf("fine check: %d levels, %llu step texels outside the rule, %llu tent texels not the "
           "reference's: %s\n",
           RD_SR_LEVELS, outsideAll, tentAll, ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
