/* gs_blend_test.c: how far a float blender with round-to-nearest drifts from
 * the GS integer blender under feedback.
 *
 * The GS blends per channel as  out = ((A - B) * C >> 7) + D  with A, B, D
 * 8-bit colours and C an 8-bit factor where 0x80 means 1.0; the result is
 * clamped to 0..255 when COLCLAMP is 1 and wrapped when it is 0.  A GPU
 * blender computes D + (A - B) * (C / 128) in floating point and rounds to
 * the nearest 8-bit value on write.  For one pass the difference is at most
 * one LSB.  The game feeds several passes back on themselves every frame
 * (motion blur reads the previous displayed frame, the aura effect reads
 * its own 128x128 buffer, the shadow count adds without clamping), so the
 * question is whether that one LSB accumulates.
 *
 * This is a pure CPU program:  cc -O2 -o gs_blend_test gs_blend_test.c
 * It prints the maximum and final drift per case; the renderer decision
 * follows from the numbers.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ITER 600
#define LANES 256 /* independent channel histories, one per starting value */

static int gs_blend_int(int a, int b, int c, int d, int colclamp)
{
    int v = (((a - b) * c) >> 7) + d; /* arithmetic shift: rounds toward -inf */
    if (colclamp) {
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
    } else {
        v &= 0xFF;
    }
    return v;
}

static int gs_blend_float(int a, int b, int c, int d, int colclamp)
{
    /* unorm8 in, float math, unorm8 out with round-to-nearest-even as a GPU
       does on write; the factor c/128 is exact in float */
    float fa = a / 255.0f, fb = b / 255.0f, fd = d / 255.0f;
    float fc = c / 128.0f;
    float v = (fa - fb) * fc + fd;
    if (colclamp) {
        if (v < 0.0f)
            v = 0.0f;
        if (v > 1.0f)
            v = 1.0f;
        return (int)lrintf(v * 255.0f);
    }
    /* wrap: emulate an 8-bit wraparound of the float sum */
    long r = lrintf(v * 255.0f);
    return (int)(r & 0xFF);
}

/* integer texture function: (tex * col) >> 7, clamped, as the GS modulates */
static int gs_tfx_int(int tex, int col)
{
    int v = (tex * col) >> 7;
    return v > 255 ? 255 : v;
}

static int gs_tfx_float(int tex, int col)
{
    float v = (tex / 255.0f) * (col / 128.0f);
    if (v > 1.0f)
        v = 1.0f;
    return (int)lrintf(v * 255.0f);
}

static uint32_t rng = 0x12345678u;

static int rnd255(void)
{
    rng = rng * 1664525u + 1013904223u;
    return (int)(rng >> 24);
}

typedef struct Drift {
    int maxAbs;
    int finalAbs;
    int framesDiffering;
} Drift;

static void report(const char *name, Drift d)
{
    printf("%-44s max |int-float| = %3d  final = %3d  frames differing = %d/%d\n", name, d.maxAbs,
           d.finalAbs, d.framesDiffering, ITER);
}

static void accumulate(Drift *d, int frame, int vi, int vf, int *anyDiffThisFrame)
{
    int diff = abs(vi - vf);
    if (diff > d->maxAbs)
        d->maxAbs = diff;
    if (frame == ITER - 1 && diff > d->finalAbs)
        d->finalAbs = diff;
    if (diff)
        *anyDiffThisFrame = 1;
}

/* Case 1: motion blur over the reduced, tinted frame.
 *   reduced = tfx(scene, tint)                   (reduction pass, no feedback)
 *   display = ((prev - reduced) * FIX >> 7) + reduced   (mode 2, FIX = motionBlurAlpha)
 * prev is last frame's display: feedback through the LERP. */
static Drift case_motion_blur(int fix, int sceneMode)
{
    Drift d = {0, 0, 0};
    int di[LANES], df[LANES];
    for (int l = 0; l < LANES; l++)
        di[l] = df[l] = l;
    for (int f = 0; f < ITER; f++) {
        int any = 0;
        for (int l = 0; l < LANES; l++) {
            int scene;
            if (sceneMode == 0)
                scene = 0x60; /* static scene */
            else if (sceneMode == 1)
                scene = (f / 50) & 1 ? 0xE0 : 0x20; /* hard cuts every 50 frames */
            else
                scene = rnd255(); /* noise */
            int tint = 0x70;
            int ri = gs_tfx_int(scene, tint), rf = gs_tfx_float(scene, tint);
            di[l] = gs_blend_int(di[l], ri, fix, ri, 1);
            df[l] = gs_blend_float(df[l], rf, fix, rf, 1);
            accumulate(&d, f, di[l], df[l], &any);
        }
        d.framesDiffering += any;
    }
    return d;
}

/* Case 2: aura feedback buffer.
 *   feed = ((src - feed) * FIX >> 7) + feed      (mode 2 toward the source)
 *   feed = feed - (decay * FIX2 >> 7)            (mode 1 fade, Cs = decay colour)
 * run with the source pulsing so the buffer never settles. */
static Drift case_aura(int fixIn, int fixDecay)
{
    Drift d = {0, 0, 0};
    int di[LANES], df[LANES];
    for (int l = 0; l < LANES; l++)
        di[l] = df[l] = 0;
    for (int f = 0; f < ITER; f++) {
        int any = 0;
        int src = 0x80 + (int)(0x60 * sin(f * 0.07));
        for (int l = 0; l < LANES; l++) {
            int s = (src + l) & 0xFF;
            di[l] = gs_blend_int(s, di[l], fixIn, di[l], 1);
            df[l] = gs_blend_float(s, df[l], fixIn, df[l], 1);
            di[l] = gs_blend_int(0, 0x40, fixDecay, di[l], 1); /* Cd - Cs*FIX with Cs = 0x40 */
            df[l] = gs_blend_float(0, 0x40, fixDecay, df[l], 1);
            accumulate(&d, f, di[l], df[l], &any);
        }
        d.framesDiffering += any;
    }
    return d;
}

/* Case 3: shadow count: mode 0 (Cs*FIX + Cd) with FIX 0x80 and COLCLAMP 0,
 * many overlapping volumes adding and subtracting per frame; the value must
 * come back to exactly zero where counts cancel.  Not feedback across frames
 * (shadow_Reset clears), but the wrap must be exact within a frame. */
static Drift case_shadow_count(void)
{
    Drift d = {0, 0, 0};
    for (int f = 0; f < ITER; f++) {
        int any = 0;
        int vi = 0, vf = 0;
        int n = 1 + (f % 7);
        for (int k = 0; k < n; k++) {
            vi = gs_blend_int(0x01, 0, 0x80, vi, 0);
            vf = gs_blend_float(0x01, 0, 0x80, vf, 0);
        }
        for (int k = 0; k < n; k++) {
            vi = gs_blend_int(0, 0x01, 0x80, vi, 0);
            vf = gs_blend_float(0, 0x01, 0x80, vf, 0);
        }
        accumulate(&d, f, vi, vf, &any);
        d.framesDiffering += any;
    }
    return d;
}

/* Case 4: single pass, no feedback: how often does one LERP_AS blend differ
 * at all?  Sets the baseline tolerance for non-feedback comparisons. */
static Drift case_single_pass(void)
{
    Drift d = {0, 0, 0};
    for (int f = 0; f < ITER; f++) {
        int any = 0;
        for (int l = 0; l < LANES; l++) {
            int cs = rnd255(), cd = l, as = rnd255() & 0x80 ? 0x80 : rnd255();
            int vi = gs_blend_int(cs, cd, as, cd, 1), vf = gs_blend_float(cs, cd, as, cd, 1);
            accumulate(&d, f, vi, vf, &any);
        }
        d.framesDiffering += any;
    }
    return d;
}

int main(void)
{
    printf("GS integer blend vs float blend with round-to-nearest, %d iterations, %d lanes\n\n",
           ITER, LANES);
    report("single pass LERP_AS (baseline)", case_single_pass());
    report("motion blur FIX 0x40, static scene", case_motion_blur(0x40, 0));
    report("motion blur FIX 0x40, hard cuts", case_motion_blur(0x40, 1));
    report("motion blur FIX 0x40, noise", case_motion_blur(0x40, 2));
    report("motion blur FIX 0x70, static scene", case_motion_blur(0x70, 0));
    report("motion blur FIX 0x70, hard cuts", case_motion_blur(0x70, 1));
    report("aura in 0x20 / decay 0x10", case_aura(0x20, 0x10));
    report("aura in 0x40 / decay 0x08", case_aura(0x40, 0x08));
    report("shadow count wrap (COLCLAMP 0)", case_shadow_count());
    return 0;
}
