/* vu1_test.c: the VU1 program shaders (port/shaders/vu_*.hlsl) and their CPU
 * references (port/render/vu1_ref/).
 *
 *   part a  hand traces: a few vertices per program followed through the
 *           .vsm instructions by hand (the traces are in the comments next to
 *           the expected values, ico2/vusrc line numbers); the CPU reference
 *           must give exactly these values
 *   part b  CPU reference against the shaders on the Vulkan RHI (lavapipe in
 *           the container): (1) every vertex shader in its probe mode
 *           (VU_F_PROBE) writes each vertex's GS X, Y, Z, RGBA, S, T, Q and
 *           region test as 32-bit values into an RGBA8_UINT target, compared
 *           with the reference's output: X, Y within 1 (1/16 pixel), Z within
 *           2^-20 relative, colours within 1, STQ within 2^-20 relative, the
 *           region test exact; (2) a strip drawn through the shipped entry
 *           and vu_ps (untextured, opaque) into a 64 x 64 RGBA8 target, and
 *           the triangles the reference kicks drawn through sprite_world_vs /
 *           sprite_ps (rd_screen_prims' shaders); every pixel within 1 LSB.
 *           Scissor triangles that cross a Z plane go through GPU clipping
 *           and are measured and reported, not asserted.
 *           (3) the port's departure from the VU: a triangle the region
 *           test drops (the probe's region field still says outside, as
 *           the reference) is drawn and clipped by the GPU, and a particle
 *           outside the x/y window is drawn. The strip in (2) is drawn
 *           without those triangles and must match the reference; they
 *           are drawn on their own into a third target, whose coverage is
 *           checked against the triangles clipped at the near plane on the
 *           CPU. Particles: the reference image gets the sprites the port
 *           adds (field 12 of the probe: the port draws it).
 *   part c  RdVuCommon (rd.h) against the VU parameter block:
 *           the qword map,
 *           and the VuCB mirror.
 *
 * Exit 0 on success, 1 on a mismatch, 77 when part a and c pass and there is
 * no Vulkan device. */
#include "rd.h"
#include "rhi.h"
#include "shader_consts.h"
#include "shaders_gen.h"
#include "vk/rhi_vk.h"
#include "vu1_ref.h"

#include <fenv.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* the largest differences the probe saw, [0] reference rounding to nearest,
 * [1] toward zero: X/Y in 1/16 pixel, Z and STQ relative, colour LSB */
static struct {
    int xy, rgba;
    double z, stq;
} stat[2];

static int statSet;

static void statXY(int d)
{
    stat[statSet].xy = d > stat[statSet].xy ? d : stat[statSet].xy;
}

static void statRgba(int d)
{
    stat[statSet].rgba = d > stat[statSet].rgba ? d : stat[statSet].rgba;
}

static void statRel(double *m, double a, double b)
{
    double d = fabs(a - b) / (fabs(b) > 1e-30 ? fabs(b) : 1.0);
    *m = d > *m ? d : *m;
}

#define FAILF(...)                                                                                 \
    do {                                                                                           \
        if (failures < 60) {                                                                       \
            printf("FAIL " __VA_ARGS__);                                                           \
        }                                                                                          \
        failures++;                                                                                \
    } while (0)

static float fbits(uint32_t u)
{
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t ubits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static void qw(float *d, float x, float y, float z, float w)
{
    d[0] = x;
    d[1] = y;
    d[2] = z;
    d[3] = w;
}

/* ------------------------------------------------------------ the scene
 * Matrices are columns (VU: c0 * x + c1 * y + c2 * z + c3 * w), chosen so a
 * hand trace stays exact:
 *   M   model to GS screen: x' = 16x + 2048z, y' = 16y + 2048z,
 *       z' = 3 * 2^20 * (z - 1), w' = z; at z = 2, X = 2048 + 8x,
 *       GS Z = 16 * 2^20 * 3 (z - 1) / z
 *   M2  model to clip (scissor): x/64, y/64, 2z - 3, w = z: near z = 1,
 *       far z = 3, x and y planes 1024 pixels from the centre
 *   V   viewport, V * M2 = M
 *   L1  light directions: l = (n.z, n.x, n.y, n.w)
 *   L2  light colours (0.5, 0.5, 0.5), (0.25, 0, 0), (0, 0.25, 0), ambient
 *       (0.125, 0.125, 0.25) */
static float sceneCommon[16][4];
static float sceneNormal[12][4];
static float sceneLight[8][4];

static void buildScene(void)
{
    memset(sceneCommon, 0, sizeof(sceneCommon));
    qw(sceneCommon[0], 0, 0, 0, 1);
    qw(sceneCommon[1], 4095, 4095, 0, 16777215);
    qw(sceneCommon[2], 0, 0, 0, 0);
    qw(sceneCommon[3], fbits(0x8000), fbits(0x302EC000), fbits(0x512), 0);
    qw(sceneCommon[4], 16, 0, 0, 0);
    qw(sceneCommon[5], 0, 16, 0, 0);
    qw(sceneCommon[6], 2048, 2048, 3145728, 1);
    qw(sceneCommon[7], 0, 0, -3145728, 0);
    qw(sceneCommon[8], 1024, 0, 0, 0);
    qw(sceneCommon[9], 0, 1024, 0, 0);
    qw(sceneCommon[10], 0, 0, 1048576, 0);
    qw(sceneCommon[11], 2048, 2048, 1048576, 1);
    qw(sceneCommon[12], 1, 0, 0, 0);
    qw(sceneCommon[13], 0, 1, 0, 0);
    qw(sceneCommon[14], 0, 0, 1, 0);
    qw(sceneCommon[15], 0, 0, 0, 1);
    memcpy(sceneNormal[0], sceneCommon[4], 4 * sizeof(float[4])); /* M */
    qw(sceneNormal[4], 0.015625f, 0, 0, 0);                       /* M2 */
    qw(sceneNormal[5], 0, 0.015625f, 0, 0);
    qw(sceneNormal[6], 0, 0, 2, 1);
    qw(sceneNormal[7], 0, 0, -3, 0);
    qw(sceneNormal[8], 1, 0, 0, 0); /* MV = identity */
    qw(sceneNormal[9], 0, 1, 0, 0);
    qw(sceneNormal[10], 0, 0, 1, 0);
    qw(sceneNormal[11], 0, 0, 0, 1);
    qw(sceneLight[0], 0, 1, 0, 0); /* L1 */
    qw(sceneLight[1], 0, 0, 1, 0);
    qw(sceneLight[2], 1, 0, 0, 0);
    qw(sceneLight[3], 0, 0, 0, 1);
    qw(sceneLight[4], 0.5f, 0.5f, 0.5f, 0); /* L2 */
    qw(sceneLight[5], 0.25f, 0, 0, 0);
    qw(sceneLight[6], 0, 0.25f, 0, 0);
    qw(sceneLight[7], 0.125f, 0.125f, 0.25f, 0);
}

/* The VU state after the uploads a draw of each program makes. */
static void stateNormal(Vu1Ref *r)
{
    vu1ref_init(r);
    vu1ref_load_common(r, sceneCommon);
    float uv[4] = {0.5f, 0.25f, 0, 0};
    vu1ref_set_uv_offset(r, uv);
    vu1ref_normal_set_matrix(r, sceneNormal);
    vu1ref_normal_set_light(r, sceneLight);
}

/* Bone 0 (VU address 16) = identity, bone 1 (address 20) = translation by
 * (8, 0, 0); the packet's fade alpha 0.5. */
static void stateCluster(Vu1Ref *r)
{
    float pk[10][4];
    memset(pk, 0, sizeof(pk));
    qw(pk[0], fbits(9), 0, 0, 0.5f); /* n + 1 = 9 quadwords copied from pk[1] */
    qw(pk[1], 1, 0, 0, 0);
    qw(pk[2], 0, 1, 0, 0);
    qw(pk[3], 0, 0, 1, 0);
    qw(pk[4], 0, 0, 0, 1);
    qw(pk[5], 1, 0, 0, 0);
    qw(pk[6], 0, 1, 0, 0);
    qw(pk[7], 0, 0, 1, 0);
    qw(pk[8], 8, 0, 0, 1);
    vu1ref_init(r);
    vu1ref_load_common(r, sceneCommon);
    float uv[4] = {0.5f, 0.25f, 0, 0};
    vu1ref_set_uv_offset(r, uv);
    vu1ref_cluster_set_matrix(r, (const float (*)[4])pk);
    vu1ref_cluster_set_light(r, sceneLight);
}

static void stateMesh(Vu1Ref *r)
{
    vu1ref_init(r);
    vu1ref_load_common(r, sceneCommon);
    float uv[4] = {0.5f, 0.25f, 0, 0};
    vu1ref_set_uv_offset(r, uv);
    float m[4][4];
    memcpy(m, sceneCommon[4], sizeof(m));
    vu1ref_mesh_set_matrix(r, m);
    vu1ref_mesh_set_light(r, sceneLight);
}

static void stateParticle(Vu1Ref *r)
{
    float m[8][4];
    memcpy(m[0], sceneCommon[4], 4 * sizeof(float[4]));
    memcpy(m[4], sceneCommon[4], 4 * sizeof(float[4]));
    vu1ref_init(r);
    vu1ref_load_common(r, sceneCommon);
    vu1ref_particle_set_matrix(r, m);
}

/* A Packet.c-style GIF tag: NLOOP n, EOP, PRE, PRIM 0x1C (strip, IIP, TME),
 * NREG 3 (ST, RGBAQ, XYZ2). */
static void tag(float *d, int n)
{
    qw(d, fbits(0x8000u | (uint32_t)n), fbits(0x300E4000u), fbits(0x512u), 0);
}

/* ------------------------------------------------------- part a: traces */

static void expectV(const char *what, int k, const VuGsVertex *v, int32_t x, int32_t y, int32_t z,
                    int adc, int r, int g, int b, int a, float s, float t, float q)
{
    if (v->xyz[0] != x || v->xyz[1] != y || v->xyz[2] != z || vu_gs_adc(v) != adc) {
        FAILF("%s v%d xyz %d %d %d adc %d, expected %d %d %d adc %d\n", what, k, v->xyz[0],
              v->xyz[1], v->xyz[2], vu_gs_adc(v), x, y, z, adc);
    }
    if (v->rgba[0] != r || v->rgba[1] != g || v->rgba[2] != b || v->rgba[3] != a) {
        FAILF("%s v%d rgba %d %d %d %d, expected %d %d %d %d\n", what, k, v->rgba[0], v->rgba[1],
              v->rgba[2], v->rgba[3], r, g, b, a);
    }
    if (v->stq[0] != s || v->stq[1] != t || v->stq[2] != q) {
        FAILF("%s v%d stq %.9g %.9g %.9g, expected %.9g %.9g %.9g\n", what, k, v->stq[0], v->stq[1],
              v->stq[2], s, t, q);
    }
}

static VuBatchOut outA;

static void traceNormalC(void)
{
    Vu1Ref r;
    float in[1 + 4 * 3][4];
    tag(in[0], 4);
    /* v0: pos (1.5, -2, 2, 1), ST (0.25, 0.75, 1, 0) (strip start), colour
     * (200, 100, 50, 127).
     *   :152-155 vf28 = M * pos: x = ((16*1.5 + 0*-2) + 2048*2) + 0*1 = 4120,
     *            y = ((0 + 16*-2) + 4096) + 0 = 4064, z = 3145728*2 - 3145728
     *            = 3145728, w = 2
     *   :159 q = 1/2; :171 vf28.xyz = (2060, 2032, 1572864)
     *   :177 ftoi4.xyz: 32960, 32512, 25165824
     *   :157, :172 STQ = ((0.25 + 0.5) / 2, (0.75 + 0.25) / 2, (1 + 0) / 2)
     *   :173 ftoi0 colour (200, 100, 50, 127)
     *   :156, :160 ST.w - 1 < 0: vi15 set; :167-169 vi05 = 3; :171 vi05 = 2
     *   :175-181 0 < 2060, 2032 < 4094 and 0 < 2 < 16777214: inside;
     *   :183 vi05 > 0: XYZ2 with vf20 (w word 0x8000, ADC) */
    qw(in[1], 1.5f, -2, 2, 1);
    qw(in[2], 0.25f, 0.75f, 1, 0);
    qw(in[3], 200, 100, 50, 127);
    /* v1: pos (-4, 4, 4, 1): x = -64 + 8192 = 8128, y = 8256, z = 9437184,
     *   w = 4; q = 1/4: (2032, 2064, 2359296) -> 32512, 33024, 37748736;
     *   ST (0.5, 0.5, 1, 1): STQ (1/4, 0.75/4, 1/4); vi05 = 1: ADC */
    qw(in[4], -4, 4, 4, 1);
    qw(in[5], 0.5f, 0.5f, 1, 1);
    qw(in[6], 10, 20, 30, 127);
    /* v2: pos (0, 0, 2, 1): (2048, 2048, 1572864) -> 32768, 32768, 25165824;
     *   ST (0, 0, 1, 1): STQ (0.25, 0.125, 0.5); vi05 = 0: kick (w word 0):
     *   the triangle v0 v1 v2 */
    qw(in[7], 0, 0, 2, 1);
    qw(in[8], 0, 0, 1, 1);
    qw(in[9], 255, 255, 255, 127);
    /* v3: pos (600, 0, 2, 1): x = 9600 + 4096 = 13696, X = 6848 >= 4094:
     *   :181 ibne to CLIP_NORMAL_C, vi05 = 3, ADC; ftoi4 109568 */
    qw(in[10], 600, 0, 2, 1);
    qw(in[11], 0, 0, 1, 1);
    qw(in[12], 1, 2, 3, 127);

    stateNormal(&r);
    vu1ref_normal_c(&r, 32, (const float (*)[4])in, &outA);
    if (outA.count != 4) {
        FAILF("normal_c 32 count %d\n", outA.count);
        return;
    }
    expectV("normal_c 32", 0, &outA.v[0], 32960, 32512, 25165824, 1, 200, 100, 50, 127, 0.375f,
            0.5f, 0.5f);
    expectV("normal_c 32", 1, &outA.v[1], 32512, 33024, 37748736, 1, 10, 20, 30, 127, 0.25f,
            0.1875f, 0.25f);
    expectV("normal_c 32", 2, &outA.v[2], 32768, 32768, 25165824, 0, 255, 255, 255, 127, 0.25f,
            0.125f, 0.5f);
    expectV("normal_c 32", 3, &outA.v[3], 109568, 32768, 25165824, 1, 1, 2, 3, 127, 0.25f, 0.125f,
            0.5f);
    if (r.vi[5] != 3) {
        FAILF("normal_c 32 vi05 left at %d, expected 3\n", r.vi[5]);
    }

    /* code 34 (BEGIN_NORMAL_C_NOCLIP, :212-269): no region test; v3 is
     * kicked with X = 109568, of which the GS keeps 109568 & 0xFFFF = 44032
     * (2752.0): the wrap the shader reproduces in VU_CLIP_NONE */
    stateNormal(&r);
    vu1ref_normal_c(&r, 34, (const float (*)[4])in, &outA);
    expectV("normal_c 34", 3, &outA.v[3], 109568, 32768, 25165824, 0, 1, 2, 3, 127, 0.25f, 0.125f,
            0.5f);
    if (vu_gs_x(&outA.v[3]) != 44032) {
        FAILF("normal_c 34 GS X %u, expected 44032\n", vu_gs_x(&outA.v[3]));
    }
}

static void traceScissor(void)
{
    Vu1Ref r;
    float in[1 + 4 * 3][4];
    tag(in[0], 4);
    /* START_SCISSOR_C (code 36). M2 = (x/64, y/64, 2z - 3, z):
     * v0 (-2, 0, 2) strip start: clip (-1/32, 0, 1, 2), no flags.
     *   :338-339 vi04 = vi05 = -2; :353 vi06 = 0; :354-356 vi04 = vi05 = 1,
     *   vi09 = 2; :358-361 1 + 1 + 0 > 0: SCISSOR_C_XYZ3; :371 vi09 = 1,
     *   :372 ibgtz reads vi09 from before the iaddi (2): skip; :399 ADC
     * v1 (0, 0, 2): vi04 = 1, vi05 = 0, vi06 = 0: XYZ3; vi09 = 0, the
     *   branch reads 1: skip; ADC. (Without the VI delay the branch would
     *   read 0 and clip the triangle (vf22 = (0,0,0,1), v0, v1).)
     * v2 (0, 2, 2): vi04 = vi05 = vi06 = 0: XYZ2, w word ftoi4(1.0) = 16,
     *   kick: X 2048 -> 32768, Y (32 + 4096) / 2 = 2064 -> 33024
     * v3 (256, 0, 2): clip (4, 0, 1, 2): +x (4 > |2|); vi06 = 1: XYZ3;
     *   vi09 = -1, the branch reads 0: the six fcor tests fail (v1 and v2
     *   have no flag): SCISSOR_COMMON on (v1, v2, v3); v3 gets ADC.
     * SCISSOR_COMMON: only XPLUS cuts. Edges v1-v2 (in, in): v1; v2-v3 (in,
     *   out): v2 and the cut I23: dc = 0 - 2, dn = 4 - 2, q = -2 / 4,
     *   t = 0.5, I23 = v2 + (v3 - v2) * 0.5 = (2, 1/64, 1, 2); v3-v1 (out,
     *   in): I31: dc = 2, dn = -2, t = 0.5, (2, 0, 1, 2). Fan v1, v2, I23,
     *   I31 through V: x' = 1024 xc + 2048 w, y' = 1024 yc + 2048 w,
     *   z' = 2^20 (zc + w), w; divided by w = 2:
     *   v1 (2048, 2048), v2 (2048, 2064), I23 (3072, 2056), I31 (3072,
     *   2048), Z 1572864 -> 25165824, w word 16.
     *   Colours (16,32,64), (48,64,96), (80,96,128) -> I23 (64, 80, 112),
     *   I31 = (16 - 80) * 0.5 + 80 ... = (48, 64, 96).
     *   STQ: ST + (0.5, 0.25) on xy: v1 (0.5, 0.25, 1), v2 (0.5, 1.25, 1),
     *   v3 (1.5, 0.25, 1), I23 (1, 0.75, 1), I31 (1, 0.25, 1); times 1/2. */
    qw(in[1], -2, 0, 2, 1);
    qw(in[2], 0, 0, 1, 0);
    qw(in[3], 0, 0, 0, 127);
    qw(in[4], 0, 0, 2, 1);
    qw(in[5], 0, 0, 1, 1);
    qw(in[6], 16, 32, 64, 127);
    qw(in[7], 0, 2, 2, 1);
    qw(in[8], 0, 1, 1, 1);
    qw(in[9], 48, 64, 96, 127);
    qw(in[10], 256, 0, 2, 1);
    qw(in[11], 1, 0, 1, 1);
    qw(in[12], 80, 96, 128, 127);
    stateNormal(&r);
    r.vi[9] = 0;
    vu1ref_normal_c(&r, 36, (const float (*)[4])in, &outA);
    if (outA.count != 4 || outA.fanCount != 1) {
        FAILF("scissor count %d fans %d, expected 4 and 1\n", outA.count, outA.fanCount);
        return;
    }
    if (!vu_gs_adc(&outA.v[0]) || !vu_gs_adc(&outA.v[1]) || vu_gs_adc(&outA.v[2]) ||
        !vu_gs_adc(&outA.v[3])) {
        FAILF("scissor ADC %d %d %d %d, expected 1 1 0 1\n", vu_gs_adc(&outA.v[0]),
              vu_gs_adc(&outA.v[1]), vu_gs_adc(&outA.v[2]), vu_gs_adc(&outA.v[3]));
    }
    if (outA.v[2].xyz[0] != 32768 || outA.v[2].xyz[1] != 33024 || outA.v[2].xyz[3] != 16) {
        FAILF("scissor v2 %d %d w %d\n", outA.v[2].xyz[0], outA.v[2].xyz[1], outA.v[2].xyz[3]);
    }
    const VuGsFan *f = &outA.fans[0];
    if (f->n != 4 || f->beforeVertex != 3 || f->tag[0] != 0x8004u || f->tag[1] != 0x302EC000u) {
        FAILF("scissor fan n %d at %d tag %08x %08x\n", f->n, f->beforeVertex, f->tag[0],
              f->tag[1]);
        return;
    }
    expectV("fan", 0, &f->v[0], 32768, 32768, 25165824, 0, 16, 32, 64, 127, 0.25f, 0.125f, 0.5f);
    expectV("fan", 1, &f->v[1], 32768, 33024, 25165824, 0, 48, 64, 96, 127, 0.25f, 0.625f, 0.5f);
    expectV("fan", 2, &f->v[2], 49152, 32896, 25165824, 0, 64, 80, 112, 127, 0.5f, 0.375f, 0.5f);
    expectV("fan", 3, &f->v[3], 49152, 32768, 25165824, 0, 48, 64, 96, 127, 0.5f, 0.125f, 0.5f);
}

static void traceNormalL(void)
{
    Vu1Ref r;
    float in[1 + 4 * 3][4];
    tag(in[0], 3);
    /* START_NORMAL_L (code 32):
     * v0 pos (0,0,2,1), n (0,0,1,1), ST (0.25,0.5,1,0), colour (128,128,128,127)
     *   :154-157 vf29 = L1 * n (w term vf08 * n.w) = (n.z, n.x, n.y, n.w) =
     *   (1, 0, 0, 1); :161 max 0; :166-171 vf29 = L2 * vf29 (w term vf12 *
     *   vf29.w) = (0.5 + 0.125, 0.5 + 0.125, 0.5 + 0.25, 0); :177 max 0;
     *   :182 rgb = 128 * (0.625, 0.625, 0.75) = (80, 80, 96); :186 min 255;
     *   :190 ftoi0 -> (80, 80, 96, 127). XYZ (32768, 32768, 25165824), ADC
     *   (strip start). STQ ((0.25 + 0.5) / 2, (0.5 + 0.25) / 2, 1 / 2).
     * v1 pos (2,2,4,1), n (0.5, 0.25, -1, 1): l = (-1, 0.5, 0.25, 1) -> max
     *   (0, 0.5, 0.25, 1); c.x = ((0.5*0 + 0.25*0.5) + 0*0.25) + 0.125 = 0.25,
     *   c.y = 0.0625 + 0.125 = 0.1875, c.z = 0.25; colour (255, 64, 200):
     *   (63.75, 12, 50) -> (63, 12, 50, 127). X = (32 + 8192) / 4 = 2056 ->
     *   32896 (x and y), Z 9437184 / 4 * 16 = 37748736. ST (0, 0, 1, 1):
     *   STQ (0.5/4, 0.25/4, 1/4). ADC (vi05 = 1).
     * v2 pos (-2,0,2,1), n (0, 2, 0, 0): l = (0, 0, 2, 0); c = (0, 0.5, 0);
     *   colour (100,100,100) -> (0, 50, 0, 127). X = (4096 - 32) / 2 = 2032
     *   -> 32512. Kick. */
    qw(in[1], 0, 0, 2, 1);
    qw(in[2], 0, 0, 1, 1);
    qw(in[3], 0.25f, 0.5f, 1, 0);
    qw(in[4], 128, 128, 128, 127);
    qw(in[5], 2, 2, 4, 1);
    qw(in[6], 0.5f, 0.25f, -1, 1);
    qw(in[7], 0, 0, 1, 1);
    qw(in[8], 255, 64, 200, 127);
    qw(in[9], -2, 0, 2, 1);
    qw(in[10], 0, 2, 0, 0);
    qw(in[11], 0, 0, 1, 1);
    qw(in[12], 100, 100, 100, 127);
    stateNormal(&r);
    vu1ref_normal_l(&r, 32, (const float (*)[4])in, &outA);
    expectV("normal_l 32", 0, &outA.v[0], 32768, 32768, 25165824, 1, 80, 80, 96, 127, 0.375f,
            0.375f, 0.5f);
    expectV("normal_l 32", 1, &outA.v[1], 32896, 32896, 37748736, 1, 63, 12, 50, 127, 0.125f,
            0.0625f, 0.25f);
    expectV("normal_l 32", 2, &outA.v[2], 32512, 32768, 25165824, 0, 0, 50, 0, 127, 0.25f, 0.125f,
            0.5f);
    /* START_NORMAL_L_SPEC (code 34): :273-276 c = vf09 l.x + vf10 l.y +
     * vf11 l.z + vf00 l.w: v0 (0.5, 0.5, 0.5); :283, :287 squared twice:
     * 0.0625; 128 * 0.0625 = 8. v1: c = (0.125, 0.0625, 0) -> fourth power
     * (2^-12, 2^-16, 0) times (255, 64, 200): (0.06, 0.001, 0) -> 0. v2:
     * (0, 0.5, 0) -> (0, 0.0625 * 100 = 6.25 -> 6, 0). */
    stateNormal(&r);
    vu1ref_normal_l(&r, 34, (const float (*)[4])in, &outA);
    expectV("normal_l 34", 0, &outA.v[0], 32768, 32768, 25165824, 1, 8, 8, 8, 127, 0.375f, 0.375f,
            0.5f);
    expectV("normal_l 34", 1, &outA.v[1], 32896, 32896, 37748736, 1, 0, 0, 0, 127, 0.125f, 0.0625f,
            0.25f);
    expectV("normal_l 34", 2, &outA.v[2], 32512, 32768, 25165824, 0, 0, 6, 0, 127, 0.25f, 0.125f,
            0.5f);
}

static void traceReflect(void)
{
    Vu1Ref r;
    float in[1 + 4 * 2][4];
    tag(in[0], 2);
    /* START_NORMAL_REF (code 38), MV = identity, inverse view = identity:
     * v0 pos (0,0,4,1), n (0,0,1,0), colour (90,80,70,127):
     *   :384-387 v = (0,0,4,1); :388-391 p = (8192, 8192, 9437184, 4);
     *   :392, :399-401 |v|^2 = 16; :404 vf31.x = 0 + 1/4; :405 rsqrt 1/4;
     *   :412 d = (0, 0, 1); :413 p.xyz / 4 = (2048, 2048, 2359296);
     *   :416, :420-422 d.n = 1; :426-428 r = (0,0,1) + (0,0,1) + (0,0,1) =
     *   (0, 0, 3); :433-436 W = (0, 0, 3); :440 z = 6; :444 xy = 6;
     *   :450-452 xy = 6 * 0.125 + 1.0 * 0.25 = 1; :445 z = 1;
     *   :456 STQ = (1, 1, 1) / 4. Colour as sent. ADC (strip start).
     * v1 pos (0,0,2,1), n (0,1,0,0): d = (0,0,1), d.n = 0, r = 0, W = 0,
     *   xy = 0.25: STQ (0.25, 0.25, 1) / 2. X (2048, 2048), Z 25165824. */
    qw(in[1], 0, 0, 4, 1);
    qw(in[2], 0, 0, 1, 0);
    qw(in[3], 0, 0, 1, 0);
    qw(in[4], 90, 80, 70, 127);
    qw(in[5], 0, 0, 2, 1);
    qw(in[6], 0, 1, 0, 0);
    qw(in[7], 0, 0, 1, 1);
    qw(in[8], 300, 20, 10, 127);
    stateNormal(&r);
    vu1ref_normal_l(&r, 38, (const float (*)[4])in, &outA);
    expectV("normal_ref", 0, &outA.v[0], 32768, 32768, 37748736, 1, 90, 80, 70, 127, 0.25f, 0.25f,
            0.25f);
    /* the colour is sent without a clamp: 300 reaches the GS as 300 & 255 */
    expectV("normal_ref", 1, &outA.v[1], 32768, 32768, 25165824, 1, 300, 20, 10, 127, 0.125f,
            0.125f, 0.5f);
}

static void traceCluster(void)
{
    Vu1Ref r;
    float in[1 + 5 * 2][4];
    tag(in[0], 2);
    /* START_CLUSTER_0 (code 20). Bone 0 (address 16) identity, bone 1
     * (address 20) translation (8, 0, 0); weights 0.75, 0.25.
     * v0 pos (0,0,2,1), n (0,0,1,1), ST (0.25, 0.5, 1, 0), colour 128:
     *   :196-199 B0 pos = (0,0,2,1); :202-205 B1 pos = (8,0,2,1);
     *   :220-221 P.xyz = (0,0,2) * 0.75 + (8,0,2) * 0.25 = (2, 0, 2), w 1;
     *   :226-229 S = M P = (32 + 4096, 4096, 3145728, 2); :243 / 2 =
     *   (2064, 2048, 1572864) -> 33024, 32768, 25165824;
     *   :208-223 N = (0, 0, 1), w 1; :232-235 L1 N = (1, 0, 0, 1); :246-250
     *   (0.625, 0.625, 0.75) -> (80, 80, 96, 127);
     *   :255 STQ = ST / 2, no UV offset: (0.125, 0.25, 0.5);
     *   :260-265 0 < 2064 < 4095, 0 < 2048 < 4095, 1 < 2 < 16777215: inside;
     *   strip start: ADC.
     * v1 pos (0,0,1,1): P = (2, 0, 1), w = 1: :260 mem[0].w - w = 0, no
     *   sign: outside (w must exceed 1); X = 32 + 2048 = 2080 -> 33280,
     *   Z 0; ADC. */
    qw(in[1], 0, 0, 2, 1);
    qw(in[2], 0, 0, 1, 1);
    qw(in[3], fbits(16), 0.75f, fbits(20), 0.25f);
    qw(in[4], 0.25f, 0.5f, 1, 0);
    qw(in[5], 128, 128, 128, 127);
    qw(in[6], 0, 0, 1, 1);
    qw(in[7], 0, 0, 1, 1);
    qw(in[8], fbits(16), 0.75f, fbits(20), 0.25f);
    qw(in[9], 0, 0, 1, 1);
    qw(in[10], 128, 128, 128, 127);
    stateCluster(&r);
    vu1ref_cluster(&r, 20, (const float (*)[4])in, &outA);
    expectV("cluster 20", 0, &outA.v[0], 33024, 32768, 25165824, 1, 80, 80, 96, 127, 0.125f, 0.25f,
            0.5f);
    expectV("cluster 20", 1, &outA.v[1], 33280, 32768, 0, 1, 80, 80, 96, 127, 0, 0, 1);
    if (outA.v[1].inside) {
        FAILF("cluster 20 v1: w = 1 must fail the region test (w > mem[0].w)\n");
    }
    /* code 22 (:391 vf00 for vf20, :397, :401): (0.5, 0.5, 0.5)^4 * 128 = 8 */
    stateCluster(&r);
    vu1ref_cluster(&r, 22, (const float (*)[4])in, &outA);
    expectV("cluster 22", 0, &outA.v[0], 33024, 32768, 25165824, 1, 8, 8, 8, 127, 0.125f, 0.25f,
            0.5f);
    /* code 24 (CLUSTER_1_SPEC): (0.625, 0.625, 0.75)^2 = (0.390625,
     * 0.390625, 0.5625) * 128 = (50, 50, 72); :562 alpha 127 * mem[2].w
     * (0.5) = 63.5 -> 63; :505 UV offset: STQ ((0.25 + 0.5) / 2, (0.5 + 0.25)
     * / 2, 0.5) */
    stateCluster(&r);
    vu1ref_cluster(&r, 24, (const float (*)[4])in, &outA);
    expectV("cluster 24", 0, &outA.v[0], 33024, 32768, 25165824, 1, 50, 50, 72, 63, 0.375f, 0.375f,
            0.5f);
}

static void traceMesh(void)
{
    Vu1Ref r;
    float in[2 + 3 * 1][4];
    tag(in[0], 1);
    /* START_MESH_LIGHT (code 22): in[1] = the batch colour (128, 64, 32,
     * 127); v0 pos (0,0,2,1), n (0,0,1,1), ST (0.25, 0.5, 1, 0):
     *   :168-171, :176 L = (1, 0, 0, 1); :184-187 C = (0.625, 0.625, 0.75)
     *   (not clamped); :192 rgb = (80, 40, 24); :190 alpha moved: 127.
     *   STQ (0.375, 0.375, 0.5). The mesh loops read no strip flag and vi05
     *   starts at 0 here: :200 vi05 = -1, kick (the GS has one vertex in its
     *   queue and draws nothing). */
    qw(in[1], 128, 64, 32, 127);
    qw(in[2], 0, 0, 2, 1);
    qw(in[3], 0, 0, 1, 1);
    qw(in[4], 0.25f, 0.5f, 1, 0);
    stateMesh(&r);
    vu1ref_mesh(&r, 22, (const float (*)[4])in, &outA);
    expectV("mesh 22", 0, &outA.v[0], 32768, 32768, 25165824, 0, 80, 40, 24, 127, 0.375f, 0.375f,
            0.5f);
    /* code 24 (:284 C squared once): (0.390625, 0.390625, 0.5625) * (128, 64,
     * 32) = (50, 25, 18) */
    stateMesh(&r);
    vu1ref_mesh(&r, 24, (const float (*)[4])in, &outA);
    expectV("mesh 24", 0, &outA.v[0], 32768, 32768, 25165824, 0, 50, 25, 18, 127, 0.375f, 0.375f,
            0.5f);
    /* code 20 (START_MESH_NOLIGHT): per vertex pos, ST; :368 the colour as
     * sent */
    float in2[2 + 2][4];
    tag(in2[0], 1);
    qw(in2[1], 128, 64, 32, 127);
    qw(in2[2], 0, 0, 2, 1);
    qw(in2[3], 0.25f, 0.5f, 1, 0);
    stateMesh(&r);
    vu1ref_mesh(&r, 20, (const float (*)[4])in2, &outA);
    expectV("mesh 20", 0, &outA.v[0], 32768, 32768, 25165824, 0, 128, 64, 32, 127, 0.375f, 0.375f,
            0.5f);
}

static void traceParticle(void)
{
    Vu1Ref r;
    float in[6 + 2 * 4][4]; /* three particles and a fourth that must not be read */
    /* START_PARTICLE, M = S = the scene's M, k = (1, 0.5, 0.5):
     * p0 (0, 0, 4, size 2), (u 0.25, v 0.5, grey 128, alpha 64):
     *   :97-104 h = M (0,0,4,1) = (8192, 8192, 9437184, 4); :99-112 e =
     *   S (2, 2, 0, 1) = (32, 32, -3145728, 0); :108 q = 1/4; :116-121
     *   corner 0 = ((8192 - 32) / 4, ..) = (2040, 2040, 2359296), corner 1 =
     *   (2056, 2056, 2359296); :125-133 inside (1024, 3071) and 1 < 4:
     *   drawn: XYZ 32640 / 32896, Z 37748736; RGBA (128, 128, 128, 64);
     *   ST0 (0.25, 0.5, 1), ST1 (0.75, 1.0, 1)
     * p1 alpha 0: :100-105 skipped
     * p2 (300, 0, 4): corners at x = (4800 + 8192 -/+ 32) / 4 = 3240, 3256 >=
     *   3071: skipped */
    qw(in[0], fbits(3), 0, 0, 0);
    qw(in[1], fbits(0x0001u), fbits(0x50004000u | (214u << 15)), fbits(0x52521u), 0);
    qw(in[2], fbits(0x8001u), fbits(0x50004000u | (214u << 15)), fbits(0x52521u), 0);
    qw(in[3], 1024, 1024, 0, 1);
    qw(in[4], 3071, 3071, 0, 16777215);
    qw(in[5], 1, 0.5f, 0.5f, 0);
    qw(in[6], 0, 0, 4, 2);
    qw(in[7], 0.25f, 0.5f, 128, 64);
    qw(in[8], 0, 0, 4, 2);
    qw(in[9], 0, 0, 128, 0);
    qw(in[10], 300, 0, 4, 2);
    qw(in[11], 0, 0, 128, 64);
    VuParticleOut po;
    stateParticle(&r);
    vu1ref_particle(&r, (const float (*)[4])in, 12, &po);
    if (po.count != 1 || po.s[0].index != 0) {
        FAILF("particle count %d, expected 1\n", po.count);
        return;
    }
    const VuGsSprite *s = &po.s[0];
    if (s->xyz[0][0] != 32640 || s->xyz[0][1] != 32640 || s->xyz[1][0] != 32896 ||
        s->xyz[1][1] != 32896 || s->xyz[0][2] != 37748736 || s->xyz[1][2] != 37748736) {
        FAILF("particle corners %d %d %d %d z %d %d\n", s->xyz[0][0], s->xyz[0][1], s->xyz[1][0],
              s->xyz[1][1], s->xyz[0][2], s->xyz[1][2]);
    }
    if (s->rgba[0] != 128 || s->rgba[3] != 64 || s->st[0][0] != 0.25f || s->st[0][1] != 0.5f ||
        s->st[1][0] != 0.75f || s->st[1][1] != 1.0f || s->st[0][2] != 1.0f) {
        FAILF("particle colour or ST\n");
    }
    /* :144-145: the EOP tag lands on the last particle's tag (VU 544) */
    if (r.vi[4] != 544 || ubits(r.mem[544][0]) != 0x8001u) {
        FAILF("particle EOP store at %d\n", r.vi[4]);
    }
    /* A batch that draws nothing stores the EOP tag through the vi04 an
     * earlier loop left: after normal_c, 0, the common block's (0,0,0,1),
     * which the cluster region test reads as its lower bound. */
    stateParticle(&r);
    r.vi[4] = 0;
    qw(in[0], fbits(1), 0, 0, 0);
    qw(in[7], 0, 0, 128, 0);
    vu1ref_particle(&r, (const float (*)[4])in, 8, &po);
    if (po.count != 0 || ubits(r.mem[0][0]) != 0x8001u) {
        FAILF("particle empty batch: count %d, mem[0].x %08x\n", po.count, ubits(r.mem[0][0]));
    }
    /* an over-count input: the count word says 1000, the buffer holds 3
     * particles (12 qwords): only those are read (particle 0 draws, 1 has
     * alpha 0, 2 is clipped); a buffer of 8 qwords holds one, of 7 or 5 none
     * (the particle needs both its qwords; the header needs 6) */
    qw(in[0], fbits(1000), 0, 0, 0);
    qw(in[7], 0.25f, 0.5f, 128, 64);
    qw(in[12], 0, 0, 4, 2);
    qw(in[13], 0, 0, 128, 64); /* beyond the 12: must not be read (a visible particle) */

    static const struct {
        uint32_t qwords;
        int want;
    } over[] = {{12, 1}, {8, 1}, {7, 0}, {6, 0}, {5, 0}};

    for (size_t k = 0; k < sizeof(over) / sizeof(over[0]); k++) {
        stateParticle(&r);
        vu1ref_particle(&r, (const float (*)[4])in, over[k].qwords, &po);
        if (po.count != over[k].want) {
            FAILF("particle over-count input of %u qwords: %d particles, expected %d\n",
                  over[k].qwords, po.count, over[k].want);
        }
    }
}

static void traceKicks(void)
{
    /* the static kick list: strips of 4 and 3 vertices in one batch, then a
     * second batch of 3 (vertex 7 starts it) */
    float stw[10] = {0, 1, 1, 1, 0, 1, 1, 0, 1, 1};
    int first[10] = {0, 0, 0, 0, 0, 0, 0, 7, 7, 7};
    int kicks[10];
    int n = vu1ref_static_kicks(stw, first, 10, kicks);
    static const int want[] = {2, 3, 6, 9};
    if (n != 4 || memcmp(kicks, want, sizeof(want)) != 0) {
        FAILF("static kicks: %d\n", n);
    }
}

/* The region test on a wide screen (vu1ref_set_wide_x, vu_common.hlsli
 * vu_region_pos): x is compared where the squeezed picture puts it,
 * (X - 2048) * f + 2048, so at f = 0.375 (32:9) a vertex up to X 7504 or
 * down to X -3413 passes, while the drawn position stays the unsqueezed
 * one; at f = 1 the test is the 4:3 one. */
static void wideVertex(const char *what, int k, const VuGsVertex *v, int32_t x, int inside, int adc)
{
    if (v->xyz[0] != x || v->inside != inside || vu_gs_adc(v) != adc) {
        FAILF("%s v%d X %d inside %d adc %d, expected %d %d %d\n", what, k, v->xyz[0], v->inside,
              vu_gs_adc(v), x, inside, adc);
    }
}

static void traceWide(void)
{
    Vu1Ref r;
    /* normal_c / normal_l code 32 with the scene's M (X = 8x + 2048 at
     * z = 2, Y = 2048): v0 x 0 (strip start), v1 x -2 (X 2032), v2 x 600
     * (X 6848: (4800 * 0.375) + 2048 = 3848), v3 x -600 (X -2752: 248),
     * v4 x 744 (X 8000: 4280, outside at both), v5 x 0. Wide: v2 and v3
     * pass and kick the triangles 0-1-2 and 1-2-3 (vi05 2, 1, 0, -1); v4
     * sets ADC and vi05 = 3, v5 is then ADC. 4:3: v2, v3, v4 outside, every
     * vertex ADC. */
    static const float xs[6] = {0, -2, 600, -600, 744, 0};
    static const int32_t gx[6] = {32768, 32512, 109568, -44032, 128000, 32768};
    static const int inWide[6] = {1, 1, 1, 1, 0, 1}, adcWide[6] = {1, 1, 0, 0, 1, 1};
    static const int in43[6] = {1, 1, 0, 0, 0, 1};
    float c[1 + 6 * 3][4], l[1 + 6 * 4][4];
    tag(c[0], 6);
    tag(l[0], 6);
    for (int k = 0; k < 6; k++) {
        qw(c[1 + 3 * k], xs[k], 0, 2, 1);
        qw(c[2 + 3 * k], 0, 0, 1, k == 0 ? 0.0f : 1.0f);
        qw(c[3 + 3 * k], 64, 64, 64, 127);
        qw(l[1 + 4 * k], xs[k], 0, 2, 1);
        qw(l[2 + 4 * k], 0, 0, 1, 1);
        qw(l[3 + 4 * k], 0, 0, 1, k == 0 ? 0.0f : 1.0f);
        qw(l[4 + 4 * k], 64, 64, 64, 127);
    }
    int kicks[VU_BATCH_MAX];
    for (int wide = 0; wide < 2; wide++) {
        vu1ref_set_wide_x(wide ? 0.375f : 1.0f);
        for (int prog = 0; prog < 2; prog++) {
            const char *what = prog ? (wide ? "normal_l 32 wide" : "normal_l 32 4:3")
                                    : (wide ? "normal_c 32 wide" : "normal_c 32 4:3");
            stateNormal(&r);
            if (prog) {
                vu1ref_normal_l(&r, 32, (const float (*)[4])l, &outA);
            } else {
                vu1ref_normal_c(&r, 32, (const float (*)[4])c, &outA);
            }
            for (int k = 0; k < 6 && outA.count == 6; k++) {
                wideVertex(what, k, &outA.v[k], gx[k], wide ? inWide[k] : in43[k],
                           wide ? adcWide[k] : 1);
            }
            int n = vu1ref_kicks(&outA, kicks);
            if (outA.count != 6 || n != (wide ? 2 : 0) ||
                (wide && (kicks[0] != 2 || kicks[1] != 3))) {
                FAILF("%s: %d vertices, %d kicks\n", what, outA.count, n);
            }
        }
    }

    /* the grid program (mesh code 20) with M = identity: p = pos, w = 1.
     * v0 (2048, 2048), v1 (2100, 2100), v2 (4200, 2060), v3 x 2^-20.
     * vi05 starts at 0: v0 and v1 kick (nothing drawn yet). v2: 4:3 outside
     * (ADC, vi05 = 3); wide (2152 * 0.375) + 2048 = 2855, kicks the
     * triangle 0-1-2. v3 at 4:3 passes the test: the remap is skipped at
     * f = 1 ((2^-20 - 2048) + 2048 rounds to 0, which would fail 0 < x). */
    float m[4][4];
    qw(m[0], 1, 0, 0, 0);
    qw(m[1], 0, 1, 0, 0);
    qw(m[2], 0, 0, 1, 0);
    qw(m[3], 0, 0, 0, 1);
    float g[2 + 4 * 2][4];
    tag(g[0], 4);
    qw(g[1], 128, 64, 32, 127);
    static const float gp[4][2] = {{2048, 2048}, {2100, 2100}, {4200, 2060}, {0x1p-20f, 2048}};
    for (int k = 0; k < 4; k++) {
        qw(g[2 + 2 * k], gp[k][0], gp[k][1], 0, 1);
        qw(g[3 + 2 * k], 0, 0, 1, 0);
    }
    for (int wide = 0; wide < 2; wide++) {
        const char *what = wide ? "mesh 20 wide" : "mesh 20 4:3";
        vu1ref_set_wide_x(wide ? 0.375f : 1.0f);
        stateMesh(&r);
        vu1ref_mesh_set_matrix(&r, m);
        vu1ref_mesh(&r, 20, (const float (*)[4])g, &outA);
        if (outA.count != 4) {
            FAILF("%s count %d\n", what, outA.count);
            continue;
        }
        wideVertex(what, 0, &outA.v[0], 32768, 1, 0);
        wideVertex(what, 1, &outA.v[1], 33600, 1, 0);
        wideVertex(what, 2, &outA.v[2], 67200, wide, !wide);
        wideVertex(what, 3, &outA.v[3], 0, 1, !wide);
    }
    vu1ref_set_wide_x(1.0f);
}

/* -------------------------------------------------------- part c: layout */

static void checkLayout(void)
{
    if (offsetof(RdVuCommon, unitW) != 0 || offsetof(RdVuCommon, clip) != 16 ||
        offsetof(RdVuCommon, zero) != 32 || offsetof(RdVuCommon, giftag) != 48 ||
        offsetof(RdVuCommon, screenView) != 64 || offsetof(RdVuCommon, viewport) != 128 ||
        offsetof(RdVuCommon, invView) != 192 || sizeof(RdVuCommon) != 256) {
        FAILF("RdVuCommon does not match the VU parameter block (qw 0, 1, 2, 3, 4..7, 8..11, "
              "12..15)\n");
    }
    /* the block as gsb_MakeCommonMatrix builds it, copied verbatim into the
     * first 16 qwords of VuCB and VU memory */
    RdVuCommon b;
    memset(&b, 0, sizeof(b));
    b.unitW[3] = 1.0f;
    b.clip[0] = b.clip[1] = 4095.0f;
    b.clip[3] = 16777215.0f;
    b.giftag[0] = 0x8000;
    b.giftag[1] = 0x302EC000;
    b.giftag[2] = 0x512;
    IcoVuCB cb;
    memset(&cb, 0, sizeof(cb));
    memcpy(cb.mem, &b, sizeof(b));
    Vu1Ref r;
    vu1ref_init(&r);
    vu1ref_load_common(&r, (const float (*)[4]) & b);
    if (cb.mem[1][3] != 16777215.0f || ubits(cb.mem[3][1]) != 0x302EC000u ||
        r.mem[1][0] != 4095.0f || ubits(r.mem[3][2]) != 0x512u) {
        FAILF("VuCB / VU memory 0..15 do not take RdVuCommon verbatim\n");
    }
}

/* ------------------------------------------------------ part b: the GPU */

enum { P_NORMALC, P_NORMALL, P_CLUSTER, P_MESH, P_PARTICLE };

#define MAXV 64
#define RT 64

typedef struct Case {
    const char *name;
    const char *vs;
    int prog, code, clip;
    int vq, hdr, nbatch, vpb;
    int measured; /* report, do not assert, the rendered comparison */
    int trail;    /* qwords after each batch's vertices (a Mesh3D buffer's MSCNT) */
    int tagless;  /* the GPU stream is the vertices only, as rd_create_vu_mesh keeps them */
    float wide;   /* the world x scale of the draw (g_space[0].x): 1, or 0.375 for 32:9 */
    int depart;   /* DEPART_*: input made to show the port's departure from the VU */
} Case;

/* DEPART_EYE: vertex 7 of each batch behind the eye (model (0, 0, -1), w =
 * -1): the VU drops its three triangles and so does the port (a triangle
 * with a vertex at w <= 0 stays dropped: vu_triangle_out). DEPART_SPRITE: particle 0 is a 1040-pixel sprite
 * from X 2040 (inside the target) to 3080 (past the window's 3071): the VU
 * skips it, the port draws it. */
enum { DEPART_NONE, DEPART_EYE, DEPART_SPRITE };

/* the CPU side of one case */
typedef struct CaseRef {
    int nv;
    VuGsVertex v[MAXV];
    float stw[MAXV];
    int first[MAXV];
    float pos[MAXV][4]; /* each vertex's model position (the input's first qword) */
    IcoSpriteVertex tri[MAXV * 3 * 3];
    int ntri;
    VuParticleOut po;
    int shown[MAXV]; /* particles: the port draws it (alpha and the w window only) */
} CaseRef;

static uint32_t rngState = 12345u;

static float rnd(float lo, float hi)
{
    rngState = rngState * 1664525u + 1013904223u;
    return lo + (hi - lo) * (float)(rngState >> 8) * (1.0f / 16777216.0f);
}

/* A strip's vertex values: positions inside the 64 x 64 target around
 * (2048, 2048) unless out is set, colours, ST, unit-ish normals. */
/* zmode 0: z in 1.8..2.4; 1: z = 2 (scissor cases: with one w the VU's
 * clip-space cut points interpolate like the GPU's screen-space ones);
 * 2: z alternating 0.5 (behind M2's near plane z = 1) and 2. out: x = 150
 * (beyond the scissor guard band, X = 3248) for the scissor cases, else
 * x = 600 (X = 6848, outside the region test). The wide cases (Case.wide
 * 0.375) use out 2: x = 255.875 (X 4095, outside at 4:3, inside the wide
 * test, and below 4096 so the reference's 16-bit sprite X is the same
 * point), and out 3: x = 744 (X 8000, outside both). out 4 (DEPART_EYE):
 * (0, 0, -1), behind the eye. */
static void makeVertex(int prog, float (*v)[4], int k, int start, int out, int zmode, int scissor)
{
    float z = zmode == 2 ? (k & 1 ? 0.5f : 2.0f) : zmode == 1 ? 2.0f : rnd(1.8f, 2.4f);
    float x = rnd(-3.5f, 3.5f) * z / 2.0f, y = rnd(-3.5f, 3.5f) * z / 2.0f;
    if (out == 4) {
        x = y = 0.0f;
        z = -1.0f;
    } else if (out) {
        x = scissor ? 150.0f : out == 2 ? 255.875f : out == 3 ? 744.0f : 600.0f;
        z = 2.0f;
    }
    float pos[4] = {x, y, z, 1};
    float nx = rnd(-1, 1), ny = rnd(-1, 1), nz = rnd(-1, 1);
    float nrm[4] = {nx, ny, nz, 1};
    float st[4] = {rnd(0, 1), rnd(0, 1), 1, start ? 0.0f : 1.0f};
    float col[4] = {(float)(int)rnd(0, 255.9f), (float)(int)rnd(0, 255.9f),
                    (float)(int)rnd(0, 255.9f), 127};
    int q = 0;
    memcpy(v[q++], pos, 16);
    if (prog == P_NORMALL || prog == P_CLUSTER) {
        memcpy(v[q++], nrm, 16);
    }
    if (prog == P_CLUSTER) {
        float w0 = rnd(0, 1);
        qw(v[q++], fbits(16), w0, fbits(20), 1.0f - w0);
    }
    if (prog == P_MESH) {
        /* mesh: pos, [normal,] ST */
        return;
    }
    memcpy(v[q++], st, 16);
    memcpy(v[q++], col, 16);
}

/* where a batch's GIF tag sits in its header */
static int tagOffset(const Case *c)
{
    return c->hdr - (c->prog == P_MESH ? 2 : 1);
}

/* Fill the case's VU input (batches, each hdr + vpb * vq qwords) into in;
 * returns the qword count. */
static int makeInput(const Case *c, float (*in)[4])
{
    int at = 0;
    if (c->prog == P_PARTICLE) {
        int n = c->vpb;
        qw(in[0], fbits((uint32_t)n), 0, 0, 0);
        qw(in[1], fbits(0x0001u), fbits(0x50004000u | (214u << 15)), fbits(0x52521u), 0);
        qw(in[2], fbits(0x8001u), fbits(0x50004000u | (214u << 15)), fbits(0x52521u), 0);
        qw(in[3], 1024, 1024, 0, 1);
        qw(in[4], 3071, 3071, 0, 16777215);
        qw(in[5], 1.0f, 0.25f, 0.375f, 0);
        for (int i = 0; i < n; i++) {
            float z = rnd(1.8f, 4.0f);
            qw(in[6 + 2 * i], rnd(-3, 3) * z / 2, rnd(-3, 3) * z / 2, z, rnd(0.5f, 3.0f));
            qw(in[7 + 2 * i], rnd(0, 1), rnd(0, 1), (float)(int)rnd(0, 255.9f),
               i == 3 ? 0.0f : (float)(int)rnd(1, 128));
        }
        qw(in[6], 300, 0, 4, 2); /* outside the window: skipped by the VU, drawn by the port */
        if (c->depart == DEPART_SPRITE) {
            /* h = (5120, 4096, 3 * 2^20, 2), half extent 16 * 65 / 2 = 520:
             * corners (2040, 1528) and (3080, 2568) */
            qw(in[6], 64, 0, 2, 65);
            qw(in[7], 0.25f, 0.5f, 128, 64);
        }
        return 6 + 2 * n;
    }
    for (int b = 0; b < c->nbatch; b++) {
        int t = at + tagOffset(c);
        if (t > at) {
            qw(in[at], 0, 0, 0, fbits(0x6C008000u)); /* a VIF qword: NOP NOP NOP UNPACK */
        }
        tag(in[t], c->vpb);
        if (c->prog == P_MESH) {
            qw(in[t + 1], 200, 150, 100, 127);
        }
        int v0 = at + c->hdr;
        for (int k = 0; k < c->vpb; k++) {
            float (*v)[4] = &in[v0 + k * c->vq];
            int start = k == 0 || (c->prog != P_MESH && k == 5);
            int scissor = c->clip == ICO_VU_CLIP_SCISSOR;
            int out = c->clip != ICO_VU_CLIP_NONE && (k == 7) && !c->measured;
            if (c->wide != 1.0f && c->clip == ICO_VU_CLIP_REGION) {
                out = k == 7 ? 2 : k == 9 ? 3 : 0;
            }
            if (c->depart == DEPART_EYE && k == 7) {
                out = 4;
            }
            int zmode = c->measured ? 2 : scissor ? 1 : 0;
            makeVertex(c->prog, v, k, start, out, zmode, scissor);
            if (c->prog == P_MESH) {
                int lit = c->code != 20;
                float nrm[4] = {rnd(-1, 1), rnd(-1, 1), rnd(-1, 1), 1};
                if (lit) {
                    memcpy(v[1], nrm, 16);
                }
                qw(v[lit ? 2 : 1], rnd(0, 1), rnd(0, 1), 1, 0);
            }
        }
        at = v0 + c->vpb * c->vq;
        for (int i = 0; i < c->trail; i++) {
            qw(in[at++], fbits(0x17000000u), 0, 0, 0); /* MSCNT */
        }
    }
    return at;
}

static void baseState(const Case *c, Vu1Ref *r)
{
    switch (c->prog) {
    case P_NORMALC:
    case P_NORMALL:
        stateNormal(r);
        break;
    case P_CLUSTER:
        stateCluster(r);
        break;
    case P_MESH:
        stateMesh(r);
        break;
    default:
        stateParticle(r);
        break;
    }
}

/* VuCB from the VU state the draw starts from (vu_common.hlsli's map). */
static void makeVuCB(const Case *c, const Vu1Ref *r, IcoVuCB *cb, IcoVuBoneCB *bones, uint32_t base)
{
    memset(cb, 0, sizeof(*cb));
    memcpy(cb->mem, r->mem, 16 * sizeof(float[4]));
    if (c->prog == P_NORMALC || c->prog == P_NORMALL) {
        memcpy(cb->mem, r->mem, 36 * sizeof(float[4]));
    } else if (c->prog == P_CLUSTER) {
        memcpy(cb->mem[28], r->vf[13], 8 * sizeof(float[4]));
        memcpy(bones->bone, r->mem[16], sizeof(bones->bone));
    } else if (c->prog == P_MESH) {
        memcpy(cb->mem[16], r->vf[1], 4 * sizeof(float[4]));
        memcpy(cb->mem[28], r->vf[5], 8 * sizeof(float[4]));
    } else {
        memcpy(cb->mem[16], r->vf[1], 8 * sizeof(float[4]));
    }
    cb->draw[0] = base;
    cb->draw[1] = (uint32_t)c->vq;
    cb->draw[2] = (uint32_t)c->clip;
    cb->draw[3] = c->prog == P_PARTICLE || c->tagless ? 0u : (uint32_t)c->vpb;
    cb->batch[0] = c->tagless ? 0u : (uint32_t)c->hdr;
    cb->batch[1] = (uint32_t)c->trail;
}

static IcoSpriteVertex spriteV(const VuGsVertex *v)
{
    IcoSpriteVertex s;
    s.x = (uint16_t)vu_gs_x(v);
    s.y = (uint16_t)vu_gs_y(v);
    s.z = (uint32_t)v->xyz[2];
    for (int i = 0; i < 4; i++) {
        s.rgba[i] = (uint8_t)(v->rgba[i] & 255);
    }
    s.u = s.v = 0.0f;
    return s;
}

/* Particle i as the port draws it (vu_particle.hlsl: the alpha test and the
 * w window of qw 3/4, not the x/y window), with the scene's M as both
 * particle matrices (stateParticle): returns whether it is drawn and fills
 * s. The inputs that use it are exact in float (DEPART_SPRITE, makeInput's
 * particle 0). */
static int portSprite(const float (*in)[4], int i, VuGsSprite *s)
{
    const float *a = in[6 + 2 * i], *b = in[7 + 2 * i];
    float h[4], e[4];
    for (int j = 0; j < 4; j++) {
        h[j] = sceneCommon[4][j] * a[0] + sceneCommon[5][j] * a[1] + sceneCommon[6][j] * a[2] +
               sceneCommon[7][j];
        const float sz = a[3] * in[5][0];
        e[j] = sceneCommon[4][j] * sz + sceneCommon[5][j] * sz + sceneCommon[7][j];
    }
    memset(s, 0, sizeof(*s));
    s->index = i;
    const float q = 1.0f / h[3];
    for (int c = 0; c < 2; c++) {
        const float sg = c ? 1.0f : -1.0f;
        s->xyz[c][0] = (int32_t)((h[0] + sg * e[0]) * q * 16.0f);
        s->xyz[c][1] = (int32_t)((h[1] + sg * e[1]) * q * 16.0f);
        s->xyz[c][2] = (int32_t)(h[2] * q * 16.0f);
    }
    s->rgba[0] = s->rgba[1] = s->rgba[2] = (int32_t)b[2];
    s->rgba[3] = (int32_t)b[3];
    const int alphaZero = (ubits(b[3]) & 0x7F800000u) == 0u;
    return !alphaZero && in[3][3] < h[3] && h[3] < in[4][3];
}

/* Run the reference over the case and collect vertices and the triangles
 * the GS would draw (kicks and fans) as sprite vertices. */
static void runRef(const Case *c, const float (*in)[4], int nq, CaseRef *cr)
{
    Vu1Ref r;
    baseState(c, &r);
    memset(cr, 0, sizeof(*cr));
    if (c->prog == P_PARTICLE) {
        vu1ref_particle(&r, in, (uint32_t)nq, &cr->po);
        /* the sprites in input order: the reference's, and between them the
         * ones only the port draws */
        static VuGsSprite added;
        for (int k = 0, pi = 0; k < c->vpb; k++) {
            const VuGsSprite *s = 0;
            cr->shown[k] = portSprite(in, k, &added);
            if (pi < cr->po.count && cr->po.s[pi].index == k) {
                s = &cr->po.s[pi++];
            } else if (cr->shown[k]) {
                s = &added;
            } else {
                continue;
            }
            VuGsVertex a, b;
            memset(&a, 0, sizeof(a));
            memset(&b, 0, sizeof(b));
            memcpy(a.xyz, s->xyz[0], 16);
            memcpy(b.xyz, s->xyz[1], 16);
            memcpy(a.rgba, s->rgba, 16);
            memcpy(b.rgba, s->rgba, 16);
            VuGsVertex cTR = a, cBL = a, cBR = b;
            cTR.xyz[0] = b.xyz[0];
            cBL.xyz[1] = b.xyz[1];
            const VuGsVertex *q6[6] = {&a, &cTR, &cBL, &cBL, &cTR, &cBR};
            for (int j = 0; j < 6; j++) {
                cr->tri[cr->ntri * 3 + j] = spriteV(q6[j]);
            }
            cr->ntri += 2;
        }
        return;
    }
    int at = 0;
    for (int b = 0; b < c->nbatch; b++) {
        static VuBatchOut big;
        switch (c->prog) {
        case P_NORMALC:
            vu1ref_normal_c(&r, c->code, in + at + tagOffset(c), &big);
            break;
        case P_NORMALL:
            vu1ref_normal_l(&r, c->code, in + at + tagOffset(c), &big);
            break;
        case P_CLUSTER:
            vu1ref_cluster(&r, c->code, in + at + tagOffset(c), &big);
            break;
        default:
            vu1ref_mesh(&r, c->code, in + at + tagOffset(c), &big);
            break;
        }
        int v0 = cr->nv;
        for (int k = 0; k < big.count; k++) {
            cr->v[v0 + k] = big.v[k];
            const float *st = in[at + c->hdr + k * c->vq + (c->vq - 2)];
            cr->stw[v0 + k] = st[3];
            memcpy(cr->pos[v0 + k], in[at + c->hdr + k * c->vq], 16);
            cr->first[v0 + k] = v0;
        }
        /* fans first: they are kicked during the loop, before the batch */
        for (int f = 0; f < big.fanCount; f++) {
            const VuGsFan *fan = &big.fans[f];
            for (int i = 2; i < fan->n; i++) {
                cr->tri[cr->ntri * 3 + 0] = spriteV(&fan->v[0]);
                cr->tri[cr->ntri * 3 + 1] = spriteV(&fan->v[i - 1]);
                cr->tri[cr->ntri * 3 + 2] = spriteV(&fan->v[i]);
                cr->ntri++;
            }
        }
        int kicks[MAXV];
        int nk = vu1ref_kicks(&big, kicks);
        for (int i = 0; i < nk; i++) {
            for (int j = 0; j < 3; j++) {
                cr->tri[cr->ntri * 3 + j] = spriteV(&big.v[kicks[i] - 2 + j]);
            }
            cr->ntri++;
        }
        cr->nv += big.count;
        at += c->hdr + c->vpb * c->vq + c->trail;
    }
}

static int near1(float a, float b)
{
    float d = fabsf(a - b);
    return d <= fabsf(b) * (1.0f / 1048576.0f) + 1e-30f;
}

static void compareProbe(const Case *c, const CaseRef *cr, const uint8_t *img, uint32_t pitch)
{
    const int n = c->prog == P_PARTICLE ? c->vpb : cr->nv;
    int pi = 0;
    for (int k = 0; k < n; k++) {
        uint32_t f[ICO_VU_PROBE_FIELDS];
        for (int i = 0; i < ICO_VU_PROBE_FIELDS; i++) {
            memcpy(&f[i], img + (size_t)k * pitch + (size_t)i * 4, 4);
        }
        if (c->prog == P_PARTICLE) {
            const VuGsSprite *s = pi < cr->po.count && cr->po.s[pi].index == k ? &cr->po.s[pi] : 0;
            if ((f[12] != 0) != (cr->shown[k] != 0)) {
                FAILF("%s particle %d drawn by the port %u, expected %d\n", c->name, k, f[12],
                      cr->shown[k]);
            }
            if ((f[10] != 0) != (s != 0)) {
                FAILF("%s particle %d drawn %u, reference %d\n", c->name, k, f[10], s != 0);
                continue;
            }
            if (!s) {
                continue;
            }
            pi++;
            int dx = abs((int32_t)f[0] - s->xyz[0][0]) | abs((int32_t)f[1] - s->xyz[0][1]) |
                     abs((int32_t)f[2] - s->xyz[1][0]) | abs((int32_t)f[3] - s->xyz[1][1]);
            if (dx > 1 || !near1((float)(int32_t)f[4], (float)s->xyz[0][2])) {
                FAILF("%s particle %d corners %d %d %d %d z %d, reference %d %d %d %d z %d\n",
                      c->name, k, f[0], f[1], f[2], f[3], f[4], s->xyz[0][0], s->xyz[0][1],
                      s->xyz[1][0], s->xyz[1][1], s->xyz[0][2]);
            }
            for (int i = 0; i < 4; i++) {
                if (abs((int)((f[5] >> (8 * i)) & 255) - (s->rgba[i] & 255)) > 1) {
                    FAILF("%s particle %d rgba %08x\n", c->name, k, f[5]);
                    break;
                }
            }
            if (fbits(f[6]) != s->st[0][0] || fbits(f[7]) != s->st[0][1] ||
                !near1(fbits(f[8]), s->st[1][0]) || !near1(fbits(f[9]), s->st[1][1])) {
                FAILF("%s particle %d ST\n", c->name, k);
            }
            continue;
        }
        const VuGsVertex *v = &cr->v[k];
        int dx = abs((int32_t)f[0] - v->xyz[0]), dy = abs((int32_t)f[1] - v->xyz[1]);
        statXY(dx > dy ? dx : dy);
        statRel(&stat[statSet].z, (double)(int32_t)f[2], (double)v->xyz[2]);
        for (int i = 0; i < 3; i++) {
            statRel(&stat[statSet].stq, (double)fbits(f[4 + i]), (double)v->stq[i]);
        }
        for (int i = 0; i < 4; i++) {
            statRgba(abs((int)((f[3] >> (8 * i)) & 255) - (v->rgba[i] & 255)));
        }
        if (dx > 1 || dy > 1 || !near1((float)(int32_t)f[2], (float)v->xyz[2])) {
            FAILF("%s v%d X %d Y %d Z %d, reference %d %d %d\n", c->name, k, (int32_t)f[0],
                  (int32_t)f[1], (int32_t)f[2], v->xyz[0], v->xyz[1], v->xyz[2]);
        }
        for (int i = 0; i < 4; i++) {
            if (abs((int)((f[3] >> (8 * i)) & 255) - (v->rgba[i] & 255)) > 1) {
                FAILF("%s v%d rgba %08x, reference %d %d %d %d\n", c->name, k, f[3], v->rgba[0],
                      v->rgba[1], v->rgba[2], v->rgba[3]);
                break;
            }
        }
        if (!near1(fbits(f[4]), v->stq[0]) || !near1(fbits(f[5]), v->stq[1]) ||
            !near1(fbits(f[6]), v->stq[2])) {
            FAILF("%s v%d STQ %.9g %.9g %.9g, reference %.9g %.9g %.9g\n", c->name, k, fbits(f[4]),
                  fbits(f[5]), fbits(f[6]), v->stq[0], v->stq[1], v->stq[2]);
        }
        if ((int)f[7] != v->inside) {
            FAILF("%s v%d region test %u, reference %d\n", c->name, k, f[7], v->inside);
        }
        if (c->clip == ICO_VU_CLIP_SCISSOR && f[8] != v->clipFlags) {
            FAILF("%s v%d clip flags %02x, reference %02x\n", c->name, k, f[8], v->clipFlags);
        }
    }
}

/* every pixel within 1; returns the pixels that differ by more */
static int compareImages(const uint8_t *a, const uint8_t *b, uint32_t pitch, int *maxd)
{
    int bad = 0;
    *maxd = 0;
    for (int y = 0; y < RT; y++) {
        for (int x = 0; x < RT; x++) {
            const uint8_t *p = a + (size_t)y * pitch + (size_t)x * 4;
            const uint8_t *q = b + (size_t)y * pitch + (size_t)x * 4;
            int d = 0;
            for (int i = 0; i < 4; i++) {
                int e = abs((int)p[i] - (int)q[i]);
                d = e > d ? e : d;
            }
            *maxd = d > *maxd ? d : *maxd;
            bad += d > 1;
        }
    }
    return bad;
}

/* Whether the VU draws triangle k (vertices k-2..k): with the region test,
 * only when all three are inside (the ADC counter); the other modes draw
 * every static kick. */
static int vuDraws(const Case *c, const CaseRef *cr, int k)
{
    return c->clip != ICO_VU_CLIP_REGION ||
           (cr->v[k - 2].inside && cr->v[k - 1].inside && cr->v[k].inside);
}

/* Triangle k as the port draws it when the region test drops it, as a
 * polygon in pixels of the RT x RT target: clipped to model z >= 1, which
 * is GS Z >= 0 under the scene's M (z' = 3 * 2^20 (z - 1), w' = z), where
 * the GPU clips it (0 <= z); then projected. Vertices in front of that use
 * the reference's GS X and Y (any program); a triangle with one behind
 * is projected with M itself, so only for normal_c and normal_l (the
 * cluster's bones move it). A triangle with a vertex behind the eye
 * (model z <= 0, w <= 0) is not drawn, as on the VU. Returns the corner
 * count, 0 when nothing is left, -1 when it cannot be predicted. */
/* Whether triangle k has a vertex behind the eye (w' = model z <= 0 under
 * the scene's M) */
static int behindEye(const CaseRef *cr, int k)
{
    for (int j = 0; j < 3; j++) {
        if (cr->pos[k - 2 + j][2] <= 0.0f) {
            return 1;
        }
    }
    return 0;
}

static int portPolygon(const Case *c, const CaseRef *cr, int k, float poly[4][2])
{
    const float half = RT / 2;
    int front = 0;
    for (int j = 0; j < 3; j++) {
        front += cr->pos[k - 2 + j][2] >= 1.0f;
    }
    if (front == 3) {
        for (int j = 0; j < 3; j++) {
            const VuGsVertex *v = &cr->v[k - 2 + j];
            poly[j][0] = half + ((float)v->xyz[0] / 16.0f - 2048.0f) * c->wide;
            poly[j][1] = half + ((float)v->xyz[1] / 16.0f - 2048.0f);
        }
        return 3;
    }
    if (c->prog != P_NORMALC && c->prog != P_NORMALL) {
        return -1;
    }
    if (behindEye(cr, k)) {
        return 0;
    }
    int n = 0;
    for (int j = 0; j < 3; j++) {
        const float *a = cr->pos[k - 2 + j], *b = cr->pos[k - 2 + (j + 1) % 3];
        float m[3];
        if (a[2] >= 1.0f) {
            memcpy(m, a, sizeof(m));
            poly[n][0] = half + 16.0f * m[0] / m[2] * c->wide;
            poly[n][1] = half + 16.0f * m[1] / m[2];
            n++;
        }
        if ((a[2] >= 1.0f) != (b[2] >= 1.0f)) {
            const float t = (1.0f - a[2]) / (b[2] - a[2]);
            for (int i = 0; i < 3; i++) {
                m[i] = a[i] + (b[i] - a[i]) * t;
            }
            poly[n][0] = half + 16.0f * m[0] * c->wide; /* m[2] = 1 */
            poly[n][1] = half + 16.0f * m[1];
            n++;
        }
    }
    return n;
}

/* How far pixel centre (x, y) is inside the convex polygon (the smallest
 * distance to an edge, negative outside); a very large negative value for
 * a polygon of no area. */
static float insideBy(const float (*poly)[2], int n, float x, float y)
{
    float area = 0.0f;
    for (int i = 0; i < n; i++) {
        const float *a = poly[i], *b = poly[(i + 1) % n];
        area += a[0] * b[1] - b[0] * a[1];
    }
    if (n < 3 || fabsf(area) < 1e-3f) {
        return -1e30f;
    }
    const float sg = area > 0.0f ? 1.0f : -1.0f;
    float d = 1e30f;
    for (int i = 0; i < n; i++) {
        const float *a = poly[i], *b = poly[(i + 1) % n];
        const float ex = b[0] - a[0], ey = b[1] - a[1];
        const float len = sqrtf(ex * ex + ey * ey);
        if (len <= 0.0f) {
            continue;
        }
        const float e = sg * (ex * (y - a[1]) - ey * (x - a[0])) / len;
        d = e < d ? e : d;
    }
    return d;
}

/* The third target (the triangles the port draws and the VU drops) against
 * the polygons: a pixel centre more than one pixel inside one of them is
 * drawn, one more than a pixel outside all of them is not. Returns the
 * pixels clearly inside; *bad counts the mismatches, or is -1 when a
 * triangle cannot be predicted (portPolygon; none of the cases has one, so
 * the caller fails it). */
static int checkPortTriangles(const Case *c, const CaseRef *cr, const uint32_t *idx, int first,
                              int count, const uint8_t *img, uint32_t pitch, int *bad)
{
    static float polys[MAXV][4][2];
    static int pn[MAXV];
    int np = 0;
    *bad = 0;
    for (int i = 0; i < count; i += 3) {
        const int k = (int)((idx[first + i] & ICO_VU_INDEX_MASK) >> 2);
        pn[np] = portPolygon(c, cr, k, polys[np]);
        if (pn[np] < 0) {
            *bad = -1;
            return 0;
        }
        np++;
    }
    int in = 0;
    for (int y = 0; y < RT; y++) {
        for (int x = 0; x < RT; x++) {
            float d = -1e30f;
            for (int p = 0; p < np; p++) {
                const float e =
                    insideBy((const float (*)[2])polys[p], pn[p], (float)x + 0.5f, (float)y + 0.5f);
                d = e > d ? e : d;
            }
            const uint8_t *q = img + (size_t)y * pitch + (size_t)x * 4;
            const int drawn = q[0] != 16 || q[1] != 32 || q[2] != 48;
            in += d > 1.0f;
            *bad += (d > 1.0f && !drawn) || (d < -1.0f && drawn);
        }
    }
    return in;
}

static const Case kCases[] = {
    {"prelit 32", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"prelit 34", "vu_prelit_vs", P_NORMALC, 34, ICO_VU_CLIP_NONE, 3, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"prelit 36", "vu_prelit_vs", P_NORMALC, 36, ICO_VU_CLIP_SCISSOR, 3, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"prelit 36 near", "vu_prelit_vs", P_NORMALC, 36, ICO_VU_CLIP_SCISSOR, 3, 1, 1, 8, 1, 0, 0,
     1.0f, DEPART_NONE},
    {"lit 32 tagless", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 2, 12, 0, 0, 1, 1.0f,
     DEPART_NONE},
    {"lit 36", "vu_lit_vs", P_NORMALL, 36, ICO_VU_CLIP_SCISSOR, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"lit spec 34", "vu_lit_spec_vs", P_NORMALL, 34, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"reflect 38", "vu_reflect_vs", P_NORMALL, 38, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"skin 20 tagless", "vu_skin_vs", P_CLUSTER, 20, ICO_VU_CLIP_REGION, 5, 1, 2, 12, 0, 0, 1, 1.0f,
     DEPART_NONE},
    {"skin 22", "vu_skin_spec_vs", P_CLUSTER, 22, ICO_VU_CLIP_REGION, 5, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"skin 24", "vu_skin_debug_vs", P_CLUSTER, 24, ICO_VU_CLIP_REGION, 5, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE},
    {"grid 20", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 3, 10, 0, 0, 0, 1.0f,
     DEPART_NONE},
    /* as prim_makePacketMesh3D lays the buffer out: VIF qword, tag, colour,
     * vertices, MSCNT */
    {"grid 22 raw", "vu_grid_lit_vs", P_MESH, 22, ICO_VU_CLIP_REGION, 3, 3, 3, 10, 0, 1, 0, 1.0f,
     DEPART_NONE},
    {"grid 24", "vu_grid_spec_vs", P_MESH, 24, ICO_VU_CLIP_REGION, 3, 2, 2, 10, 0, 0, 0, 1.0f,
     DEPART_NONE},
    /* a wide scene draw at 32:9: the region test compares the squeezed x */
    {"prelit 32 wide", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_NONE},
    {"lit 32 wide", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 0.375f,
     DEPART_NONE},
    {"grid 20 wide", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 2, 10, 0, 0, 0, 0.375f,
     DEPART_NONE},
    {"particle", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f, DEPART_NONE},
    /* the port's departures from the VU (DEPART_*): a strip with a vertex
     * behind the eye, a particle past the window */
    {"prelit 32 eye", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_EYE},
    {"lit 32 eye", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_EYE},
    {"particle past", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f,
     DEPART_SPRITE},
};

#define NCASES ((int)(sizeof(kCases) / sizeof(kCases[0])))

static RhiShader makeShader(const char *name)
{
    const IcoShaderBlob *b = ico_find_shader(name);
    if (!b) {
        FAILF("shader %s not in the table\n", name);
        return (RhiShader){0};
    }
    RhiShaderDesc d = {b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT,
                       b->spirv, b->spirv_len, b->entry, b->name};
    RhiShader s = rhi_create_shader(&d);
    if (!s.id) {
        FAILF("shader %s not created\n", name);
    }
    return s;
}

static RhiPipeline makePipeline(RhiShader vs, RhiShader ps, const RhiBindGroupLayout *layouts,
                                int sprite, RhiFormat fmt, const char *name)
{
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    RhiPipelineDesc d = {0};
    d.vertex = vs;
    d.fragment = ps;
    d.layouts = layouts;
    d.layoutCount = 3;
    if (sprite) {
        d.vertexBindings = &vb;
        d.vertexBindingCount = 1;
        d.vertexAttrs = va;
        d.vertexAttrCount = 4;
    }
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0] = (RhiBlendState){.writeMask = 0xF};
    d.colorFormats[0] = fmt;
    d.colorCount = 1;
    d.depthFormat = RHI_FMT_UNKNOWN;
    d.debugName = name;
    RhiPipeline p = rhi_create_pipeline(&d);
    if (!p.id) {
        FAILF("pipeline %s not created\n", name);
    }
    return p;
}

static void frameCB(IcoFrameCB *f, float w, float h, float ox, float oy)
{
    memset(f, 0, sizeof(*f));
    f->target[0] = w;
    f->target[1] = h;
    f->target[2] = 1.0f / w;
    f->target[3] = 1.0f / h;
    f->origin[0] = ox;
    f->origin[1] = oy;
    f->space[0][0] = f->space[0][1] = f->space[1][0] = f->space[1][1] = 1.0f;
    f->z[0] = 1.0f / 4294967296.0f;
}

/* offsets in the ring */
#define OFF_STREAM 0u
#define OFF_INDEX 65536u
#define OFF_SPRITE 131072u
#define OFF_UBO 196608u
#define UBO_SLOT 4096u
#define RING_SIZE (OFF_UBO + 8u * UBO_SLOT)

static int gpuTests(void)
{
    RhiDeviceDesc dd = {NULL, false, true, "vu1_test"};
    if (!rhi_init(&dd)) {
        return 77;
    }
    printf("vu1_test: adapter %s\n", rhi_adapter_name());
    if (UBO_SLOT % rhi_limits()->uniformAlign) {
        FAILF("uniformAlign %u does not divide %u\n", rhi_limits()->uniformAlign, UBO_SLOT);
    }
    const int setupFailures = failures;
    const uint32_t FS = 1u << RHI_STAGE_FRAGMENT, VS = 1u << RHI_STAGE_VERTEX;
    static const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER, 3}};
    const RhiBindSlot s1[4] = {{0, RHI_BIND_STORAGE_BUFFER, VS},
                               {1, RHI_BIND_UNIFORM_BUFFER, VS | FS},
                               {2, RHI_BIND_UNIFORM_BUFFER, VS},
                               {3, RHI_BIND_UNIFORM_BUFFER, VS}};
    const RhiBindSlot s2[3] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                               {1, RHI_BIND_SAMPLER, FS},
                               {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    RhiBindGroupLayout l0 = rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s0, 1, "frame"});
    RhiBindGroupLayout l1 =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s1, 4, "vu draw"});
    RhiBindGroupLayout l2 = rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){s2, 3, "tex"});
    const RhiBindGroupLayout lay[3] = {l0, l1, l2};

    RhiShader probePs = makeShader("vu_probe_ps"), vuPs = makeShader("vu_ps");
    RhiShader spriteVs = makeShader("sprite_world_vs"), spritePs = makeShader("sprite_ps");
    RhiPipeline pSprite =
        makePipeline(spriteVs, spritePs, lay, 1, RHI_FMT_RGBA8_UNORM, "ref sprite");
    RhiPipeline pProbe[NCASES], pDraw[NCASES];
    for (int i = 0; i < NCASES; i++) {
        RhiShader vs = makeShader(kCases[i].vs);
        pProbe[i] = makePipeline(vs, probePs, lay, 0, RHI_FMT_RGBA8_UINT, kCases[i].name);
        pDraw[i] = makePipeline(vs, vuPs, lay, 0, RHI_FMT_RGBA8_UNORM, kCases[i].name);
    }
    RhiBuffer ring = rhi_create_buffer(&(RhiBufferDesc){
        RING_SIZE,
        RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_STORAGE_READ | RHI_BUF_COPY_SRC,
        RHI_MEM_UPLOAD, "ring"});
    uint8_t *map = rhi_map_buffer(ring);
    const uint32_t RTU = RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC;
    RhiTexture probe = rhi_create_texture(
        &(RhiTextureDesc){ICO_VU_PROBE_FIELDS, MAXV, 1, RHI_FMT_RGBA8_UINT, RTU, "probe"});
    RhiTexture imgVu =
        rhi_create_texture(&(RhiTextureDesc){RT, RT, 1, RHI_FMT_RGBA8_UNORM, RTU, "vu"});
    RhiTexture imgRef =
        rhi_create_texture(&(RhiTextureDesc){RT, RT, 1, RHI_FMT_RGBA8_UNORM, RTU, "ref"});
    RhiTexture imgPort =
        rhi_create_texture(&(RhiTextureDesc){RT, RT, 1, RHI_FMT_RGBA8_UNORM, RTU, "port"});
    RhiTexture dummy = rhi_create_texture(&(RhiTextureDesc){
        2, 2, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "dummy"});
    RhiSampler smp = rhi_create_sampler(&(RhiSamplerDesc){RHI_FILTER_NEAREST, RHI_FILTER_NEAREST,
                                                          RHI_FILTER_NEAREST, RHI_WRAP_CLAMP,
                                                          RHI_WRAP_CLAMP, 1.0f, 0.0f, 0.0f, 0.0f});
    if (failures != setupFailures || !ring.id || !map || !probe.id || !imgVu.id || !imgRef.id ||
        !imgPort.id || !dummy.id || !smp.id) {
        FAILF("GPU setup\n");
        rhi_shutdown();
        return 1;
    }
    RhiState stProbe = RHI_STATE_UNDEFINED, stVu = RHI_STATE_UNDEFINED, stRef = RHI_STATE_UNDEFINED;
    RhiState stPort = RHI_STATE_UNDEFINED;
    int portIn = 0; /* pixels clearly inside the triangles past the window the port draws */
    RhiState stDummy = RHI_STATE_UNDEFINED;

    static float in[1024][4];
    static CaseRef cr;
    for (int ci = 0; ci < NCASES; ci++) {
        const Case *c = &kCases[ci];
        rngState = 777u + (uint32_t)ci * 31u;
        memset(in, 0, sizeof(in));
        int nq = makeInput(c, in);
        const float wide = c->wide;
        vu1ref_set_wide_x(wide);
        runRef(c, (const float (*)[4])in, nq, &cr);

        rhi_wait_frame();
        if (c->tagless) {
            /* rd_create_vu_mesh's layout: each batch's vertices, tags dropped */
            uint8_t *d = map + OFF_STREAM;
            for (int b = 0, at = 0; b < c->nbatch; b++) {
                size_t n = (size_t)c->vpb * (size_t)c->vq * 16;
                memcpy(d, in[at + c->hdr], n);
                d += n;
                at += c->hdr + c->vpb * c->vq + c->trail;
            }
        } else {
            memcpy(map + OFF_STREAM, in, (size_t)nq * 16);
        }
        /* indices: the static kicks, kick * 4 + corner; first those the VU
         * draws (nkept), then those its region test drops, which the port
         * draws too (drawn on their own, into imgPort) */
        uint32_t *idx = (uint32_t *)(map + OFF_INDEX);
        int nidx = 0, nkept = 0;
        int nvert = c->prog == P_PARTICLE ? c->vpb : cr.nv;
        if (c->prog != P_PARTICLE) {
            int kicks[MAXV];
            int nk = vu1ref_static_kicks(c->prog == P_MESH ? NULL : cr.stw, cr.first, cr.nv, kicks);
            for (int pass = 0; pass < 2; pass++) {
                for (int i = 0; i < nk; i++) {
                    if (vuDraws(c, &cr, kicks[i]) != !pass) {
                        continue;
                    }
                    for (int j = 0; j < 3; j++) {
                        idx[nidx++] = ICO_VU_INDEX(kicks[i], j);
                    }
                }
                nkept = pass == 0 ? nidx : nkept;
            }
        }
        memcpy(map + OFF_SPRITE, cr.tri, (size_t)cr.ntri * 3 * sizeof(IcoSpriteVertex));
        Vu1Ref st;
        baseState(c, &st);
        IcoVuCB vcb, vcbProbe;
        static IcoVuBoneCB bones;
        memset(&bones, 0, sizeof(bones));
        makeVuCB(c, &st, &vcb, &bones, 0);
        vcbProbe = vcb;
        vcbProbe.draw[2] |= ICO_VU_PROBE;
        IcoFrameCB fProbe, fDraw;
        frameCB(&fProbe, ICO_VU_PROBE_FIELDS, MAXV, 0, 0);
        frameCB(&fDraw, RT, RT, 2048.0f - RT / 2, 2048.0f - RT / 2);
        fProbe.space[0][0] = fDraw.space[0][0] = wide;
        IcoDrawCB dcb;
        memset(&dcb, 0, sizeof(dcb));
        dcb.tex[0] = dcb.tex[1] = 2;
        dcb.tex[2] = dcb.tex[3] = 0.5f;
        /* scissor batches are two draws (VU_F_CUT_ONLY, then VU_F_KICK_ONLY),
         * the fans first as SCISSOR_COMMON kicks them */
        IcoVuCB vcbCut = vcb, vcbKick = vcb;
        vcbCut.draw[2] |= ICO_VU_CUT_ONLY;
        vcbKick.draw[2] |= ICO_VU_KICK_ONLY;
        const void *ubo[8] = {&fProbe, &fDraw, &dcb, &vcb, &vcbProbe, &bones, &vcbCut, &vcbKick};
        const size_t usz[8] = {sizeof(fProbe), sizeof(fDraw), sizeof(dcb), sizeof(vcb),
                               sizeof(vcb),    sizeof(bones), sizeof(vcb), sizeof(vcb)};
        for (int i = 0; i < 8; i++) {
            memcpy(map + OFF_UBO + (size_t)i * UBO_SLOT, ubo[i], usz[i]);
        }
        RhiBinding b0 = {.slot = 0,
                         .type = RHI_BIND_UNIFORM_BUFFER,
                         .buffer = ring,
                         .offset = OFF_UBO,
                         .size = sizeof(IcoFrameCB)};
        RhiBindGroup g0Probe = rhi_create_bind_group(&(RhiBindGroupDesc){l0, &b0, 1});
        b0.offset = OFF_UBO + UBO_SLOT;
        RhiBindGroup g0Draw = rhi_create_bind_group(&(RhiBindGroupDesc){l0, &b0, 1});
        RhiBinding b1[4] = {
            {.slot = 0,
             .type = RHI_BIND_STORAGE_BUFFER,
             .buffer = ring,
             .offset = OFF_STREAM,
             .size = 65536},
            {.slot = 1,
             .type = RHI_BIND_UNIFORM_BUFFER,
             .buffer = ring,
             .offset = OFF_UBO + 2 * UBO_SLOT,
             .size = sizeof(IcoDrawCB)},
            {.slot = 2,
             .type = RHI_BIND_UNIFORM_BUFFER,
             .buffer = ring,
             .offset = OFF_UBO + 3 * UBO_SLOT,
             .size = sizeof(IcoVuCB)},
            {.slot = 3,
             .type = RHI_BIND_UNIFORM_BUFFER,
             .buffer = ring,
             .offset = OFF_UBO + 5 * UBO_SLOT,
             .size = sizeof(IcoVuBoneCB)},
        };
        RhiBindGroup g1Draw = rhi_create_bind_group(&(RhiBindGroupDesc){l1, b1, 4});
        b1[2].offset = OFF_UBO + 4 * UBO_SLOT;
        RhiBindGroup g1Probe = rhi_create_bind_group(&(RhiBindGroupDesc){l1, b1, 4});
        b1[2].offset = OFF_UBO + 6 * UBO_SLOT;
        RhiBindGroup g1Cut = rhi_create_bind_group(&(RhiBindGroupDesc){l1, b1, 4});
        b1[2].offset = OFF_UBO + 7 * UBO_SLOT;
        RhiBindGroup g1Kick = rhi_create_bind_group(&(RhiBindGroupDesc){l1, b1, 4});
        RhiBinding b2[3] = {{0}, {0}, {0}};
        b2[0].slot = 1;
        b2[0].type = RHI_BIND_SAMPLED_TEXTURE;
        b2[0].texture = dummy;
        b2[1].slot = 1;
        b2[1].type = RHI_BIND_SAMPLER;
        b2[1].sampler = smp;
        b2[2].slot = 2;
        b2[2].type = RHI_BIND_SAMPLED_TEXTURE;
        b2[2].texture = dummy;
        RhiBindGroup g2 = rhi_create_bind_group(&(RhiBindGroupDesc){l2, b2, 3});

        RhiCommandList cl = rhi_begin_commands();
        RhiTextureBarrier pre[5] = {{probe, stProbe, RHI_STATE_RENDER_TARGET},
                                    {imgVu, stVu, RHI_STATE_RENDER_TARGET},
                                    {imgRef, stRef, RHI_STATE_RENDER_TARGET},
                                    {imgPort, stPort, RHI_STATE_RENDER_TARGET},
                                    {dummy, stDummy, RHI_STATE_SHADER_READ}};
        rhi_cmd_barrier(cl, pre, stDummy == RHI_STATE_SHADER_READ ? 4 : 5);
        stDummy = RHI_STATE_SHADER_READ;

        RhiRenderPassDesc rp = {0};
        rp.color[0] = (RhiColorAttachment){probe, RHI_LOAD_CLEAR, {0, 0, 0, 0}, RHI_STORE_STORE};
        rp.colorCount = 1;
        rp.width = ICO_VU_PROBE_FIELDS;
        rp.height = MAXV;
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_set_pipeline(cl, pProbe[ci]);
        rhi_cmd_set_bind_group(cl, 0, g0Probe);
        rhi_cmd_set_bind_group(cl, 1, g1Probe);
        rhi_cmd_set_bind_group(cl, 2, g2);
        rhi_cmd_draw(cl, (uint32_t)nvert * ICO_VU_PROBE_FIELDS * 3, 0, 1);
        rhi_cmd_end_render_pass(cl);

        for (int pass = 0; pass < 3; pass++) {
            RhiRenderPassDesc dp = {0};
            dp.color[0] = (RhiColorAttachment){pass == 2 ? imgPort
                                               : pass    ? imgRef
                                                         : imgVu,
                                               RHI_LOAD_CLEAR,
                                               {16 / 255.f, 32 / 255.f, 48 / 255.f, 1},
                                               RHI_STORE_STORE};
            dp.colorCount = 1;
            dp.width = dp.height = RT;
            rhi_cmd_begin_render_pass(cl, &dp);
            rhi_cmd_set_bind_group(cl, 0, g0Draw);
            rhi_cmd_set_bind_group(cl, 1, g1Draw);
            rhi_cmd_set_bind_group(cl, 2, g2);
            if (pass == 0) {
                rhi_cmd_set_pipeline(cl, pDraw[ci]);
                if (c->prog == P_PARTICLE) {
                    rhi_cmd_draw(cl, (uint32_t)c->vpb * 6, 0, 1);
                } else if (nidx && c->clip == ICO_VU_CLIP_SCISSOR) {
                    rhi_cmd_set_index_buffer(cl, ring, OFF_INDEX, true);
                    rhi_cmd_set_bind_group(cl, 1, g1Cut);
                    rhi_cmd_draw_indexed(cl, (uint32_t)nidx, 0, 0, 1);
                    rhi_cmd_set_bind_group(cl, 1, g1Kick);
                    rhi_cmd_draw_indexed(cl, (uint32_t)nidx, 0, 0, 1);
                } else if (nkept) {
                    rhi_cmd_set_index_buffer(cl, ring, OFF_INDEX, true);
                    rhi_cmd_draw_indexed(cl, (uint32_t)nkept, 0, 0, 1);
                }
            } else if (pass == 2) {
                if (nidx > nkept) {
                    rhi_cmd_set_pipeline(cl, pDraw[ci]);
                    rhi_cmd_set_index_buffer(cl, ring, OFF_INDEX, true);
                    rhi_cmd_draw_indexed(cl, (uint32_t)(nidx - nkept), (uint32_t)nkept, 0, 1);
                }
            } else if (cr.ntri) {
                rhi_cmd_set_pipeline(cl, pSprite);
                rhi_cmd_set_vertex_buffer(cl, 0, ring, OFF_SPRITE);
                rhi_cmd_draw(cl, (uint32_t)cr.ntri * 3, 0, 1);
            }
            rhi_cmd_end_render_pass(cl);
        }
        RhiTextureBarrier post[4] = {{probe, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {imgVu, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {imgRef, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {imgPort, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC}};
        rhi_cmd_barrier(cl, post, 4);
        stProbe = stVu = stRef = stPort = RHI_STATE_COPY_SRC;
        rhi_end_commands(cl);
        rhi_submit(cl);

        static uint8_t pimg[ICO_VU_PROBE_FIELDS * MAXV * 4], a[RT * RT * 4], b[RT * RT * 4];
        static uint8_t pa[RT * RT * 4];
        uint32_t ppitch = 0, pitch = 0;
        if (!rhi_readback_texture(probe, RHI_ASPECT_COLOR, pimg, sizeof(pimg), &ppitch) ||
            !rhi_readback_texture(imgVu, RHI_ASPECT_COLOR, a, sizeof(a), &pitch) ||
            !rhi_readback_texture(imgRef, RHI_ASPECT_COLOR, b, sizeof(b), &pitch) ||
            !rhi_readback_texture(imgPort, RHI_ASPECT_COLOR, pa, sizeof(pa), &pitch)) {
            FAILF("%s readback\n", c->name);
            break;
        }
        int before = failures;
        compareProbe(c, &cr, pimg, ppitch);
        /* the same comparison with the reference in round toward zero (the
         * simulation thread's mode, the VU's rounding): the GPU rounds to
         * nearest, so this is the shader's distance from the VU itself */
        {
            static CaseRef rz;
            fesetround(FE_TOWARDZERO);
            runRef(c, (const float (*)[4])in, nq, &rz);
            fesetround(FE_TONEAREST);
            statSet = 1;
            compareProbe(c, &rz, pimg, ppitch);
            statSet = 0;
        }
        vu1ref_set_wide_x(1.0f);
        /* the triangles the region test drops: drawn, clipped at the near
         * plane, where the CPU puts them, unless a vertex is behind the eye */
        if (nidx > nkept) {
            int pbad = 0;
            const int pin = checkPortTriangles(c, &cr, idx, nkept, nidx - nkept, pa, pitch, &pbad);
            int pcov = 0;
            for (int i = 0; i < RT * RT; i++) {
                pcov += pa[i * 4] != 16 || pa[i * 4 + 1] != 32 || pa[i * 4 + 2] != 48;
            }
            printf("  %-15s %2d triangles the VU drops drawn: %4d pixels, %4d clearly inside, %d "
                   "mismatches\n",
                   c->name, (nidx - nkept) / 3, pcov, pin, pbad);
            if (pbad != 0) {
                FAILF("%s: the dropped triangles drawn: %d pixels against the clipped polygons\n",
                      c->name, pbad);
            }
            if (c->depart == DEPART_EYE) {
                int eye = 0;
                for (int i = nkept; i < nidx; i += 3) {
                    eye += behindEye(&cr, (int)((idx[i] & ICO_VU_INDEX_MASK) >> 2));
                }
                /* pbad above: none of their pixels is drawn */
                if (eye == 0) {
                    FAILF("%s: no dropped triangle has a vertex behind the eye\n", c->name);
                }
            }
            portIn += c->depart == DEPART_NONE ? pin : 0;
        } else if (c->depart == DEPART_EYE) {
            FAILF("%s: no triangle failed the region test\n", c->name);
        }
        if (c->depart == DEPART_SPRITE &&
            ((cr.po.count > 0 && cr.po.s[0].index == 0) || !cr.shown[0])) {
            FAILF("%s: particle 0 is not one only the port draws\n", c->name);
        }
        int maxd = 0, covered = 0;
        int bad = compareImages(a, b, pitch, &maxd);
        for (int i = 0; i < RT * RT; i++) {
            covered += b[i * 4] != 16 || b[i * 4 + 1] != 32 || b[i * 4 + 2] != 48;
        }
        if (c->measured && getenv("VU1_TEST_DUMP")) {
            for (int y = 0; y < RT; y += 2) {
                for (int x = 0; x < RT; x++) {
                    int ca = a[(y * RT + x) * 4] != 16 || a[(y * RT + x) * 4 + 1] != 32;
                    int cb = b[(y * RT + x) * 4] != 16 || b[(y * RT + x) * 4 + 1] != 32;
                    putchar(ca && cb ? '#' : ca ? 'g' : cb ? 'r' : '.');
                }
                putchar('\n');
            }
        }
        if (c->measured) {
            printf("  %-15s measured: %d of %d pixels differ by more than 1 (max %d), %d reference "
                   "triangles\n",
                   c->name, bad, RT * RT, maxd, cr.ntri);
        } else if (bad) {
            FAILF("%s: %d pixels differ by more than 1 LSB (max %d)\n", c->name, bad, maxd);
        }
        printf("  %-15s %2d vertices, %2d GPU triangles, %2d reference triangles, %4d pixels "
               "covered, max diff %d %s\n",
               c->name, nvert, c->prog == P_PARTICLE ? cr.po.count * 2 : nidx / 3, cr.ntri, covered,
               maxd, failures == before ? "ok" : "FAILED");
    }
    if (portIn == 0) {
        FAILF("no triangle past the region window was drawn\n");
    }
    for (int i = 0; i < 2; i++) {
        printf("  largest probe differences, reference rounding %s: X/Y %d/16 pixel, Z %.2g "
               "relative, RGBA %d, STQ %.2g relative\n",
               i ? "toward zero" : "to nearest", stat[i].xy, stat[i].z, stat[i].rgba, stat[i].stq);
    }
    if (rhi_vk_validation_error_count() != 0) {
        FAILF("%u validation errors\n", (unsigned)rhi_vk_validation_error_count());
    }
    rhi_shutdown();
    return 0;
}

int main(void)
{
    buildScene();
    traceNormalC();
    traceScissor();
    traceNormalL();
    traceReflect();
    traceCluster();
    traceMesh();
    traceParticle();
    traceKicks();
    traceWide();
    checkLayout();
    int cpuFailures = failures;
    printf("vu1_test: hand traces and layout: %s\n", cpuFailures ? "FAILED" : "ok");
    int g = gpuTests();
    if (g == 77) {
        printf("vu1_test: no usable Vulkan device, GPU part skipped\n");
        return failures ? 1 : 77;
    }
    if (failures) {
        printf("vu1_test: %d failures\n", failures);
        return 1;
    }
    printf("vu1_test: ok\n");
    return 0;
}
