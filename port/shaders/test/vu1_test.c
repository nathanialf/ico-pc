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
 *           (3) the port's rule over the VU (vu1ref_region_draw, the
 *           oracle's mirror of vu_common.hlsli vu_region_rule): a
 *           triangle the region test drops (the probe's region field still
 *           says outside, as the reference) is drawn, clipped by the GPU
 *           at the native frustum, when wholly beside the camera's picture
 *           (GS 2048 -+ 256 across by 2048 -+ 224, FrameCB frame[1] and
 *           [2]: the whole 64 x 64 target around 2048 lies inside it; a
 *           reflection camera's 2048 -+ 116; a target placed right of the
 *           picture; a triangle whose part within the picture's columns
 *           lies wholly above it) and under photo mode's free camera; one
 *           that reaches the picture is not drawn out of the box (the
 *           Original preset at 4:3, the PS2's picture) and, in any other
 *           state (ICO_VU_EXTENDED: Enhanced or a wide frame), is drawn
 *           clipped when every vertex is in front of the eye and its
 *           strip is joined to another (ICO_VU_INDEX_JOINED) or a triangle
 *           of its strip passes (vu1ref_strip_passes), else not drawn (a
 *           card: a free-standing strip none of whose triangles pass; the
 *           verdicts go to the shader in VuBoneCB as rd_replay.c hands
 *           them over, cardBits, and the CPU's own card test, what
 *           rd_mesh.c decides with, vu1ref_region_inside and
 *           vu1ref_strip_card, must agree with the reference on every
 *           vertex and strip, checkCards); the
 *           content table's HIDE mark hides triangles; under
 *           ICO_VU_TICKS (a picture between two ticks) one the rule drops
 *           is drawn clipped when the rule draws it at either tick's camera
 *           (vu1ref_region_between). The strip in (2) is drawn
 *           with the triangles the port draws exactly and must match the
 *           reference; the others are drawn on their own into a third
 *           target, whose coverage is checked against the clipped ones'
 *           polygons (clipped at the near plane on the CPU): nothing where
 *           the rule drops. Particles outside the x/y window
 *           (vu1ref_particle_draw): the reference image gets the sprites
 *           the port adds whole (wholly beside the camera's picture, the
 *           free camera; field 12 of the probe: alpha and the w window).
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

/* The strip test (vu1ref_strip_passes): the strips of traceKicks' stream
 * (kicks 2, 3 | 6 | 9: vertices 0..3, 4..6, 7..9) with vertex 5 outside;
 * the 4-vertex strip with vertex 3 outside, the static kicks with the
 * mesh program's batches (no flag: kicks 2..6 and 9) */
static void traceStrip(void)
{
    float stw[10] = {0, 1, 1, 1, 0, 1, 1, 0, 1, 1};
    int first[10] = {0, 0, 0, 0, 0, 0, 0, 7, 7, 7};
    VuGsVertex v[10];
    memset(v, 0, sizeof(v));
    for (int k = 0; k < 10; k++) {
        v[k].inside = k != 5 && k != 3;
    }
    /* kick 2 (0, 1, 2) passes; kick 3 (1, 2, 3) fails, its strip passes */
    if (vu1ref_strip_passes(v, stw, first, 10, 3) != 1 ||
        vu1ref_strip_passes(v, stw, first, 10, 2) != 1) {
        FAILF("strip 0..3: a triangle passes\n");
    }
    /* kick 6 (4, 5, 6) is the strip's one triangle and fails: a card */
    if (vu1ref_strip_passes(v, stw, first, 10, 6) != 0) {
        FAILF("strip 4..6: no triangle passes\n");
    }
    /* kick 9: its own strip (the batch at 7) */
    if (vu1ref_strip_passes(v, stw, first, 10, 9) != 1) {
        FAILF("strip 7..9 passes\n");
    }
    /* not a kick: 4, 5 (strip starts), 7, 8 (the batch's first two) */
    if (vu1ref_strip_passes(v, stw, first, 10, 5) != 0 ||
        vu1ref_strip_passes(v, stw, first, 10, 8) != 0) {
        FAILF("strip of a vertex that does not kick\n");
    }
    /* the mesh program (no flag): batch 0..6 is one strip; with 3 and 5
     * outside, kick 2 still passes */
    if (vu1ref_strip_passes(v, NULL, first, 10, 6) != 1) {
        FAILF("mesh strip 0..6 passes\n");
    }
    for (int k = 0; k < 7; k++) {
        v[k].inside = k == 0 || k == 1 || k == 6;
    }
    /* a batch none of whose triangles has three vertices inside */
    if (vu1ref_strip_passes(v, NULL, first, 10, 6) != 0) {
        FAILF("mesh strip 0..6 fails\n");
    }
}

/* The region test is the program's own, on the position as computed, at
 * every aspect (no aspect term: the native frame draws a GS X where the
 * game put it), and the port's rule over it (vu1ref_region_draw,
 * vu1ref_strip_passes, vu1ref_particle_draw: vu_common.hlsli
 * vu_region_rule, vu_content_hidden; vu_particle.hlsl). */
static void regionVertex(const char *what, int k, const VuGsVertex *v, int32_t x, int inside,
                         int adc)
{
    if (v->xyz[0] != x || v->inside != inside || vu_gs_adc(v) != adc) {
        FAILF("%s v%d X %d inside %d adc %d, expected %d %d %d\n", what, k, v->xyz[0], v->inside,
              vu_gs_adc(v), x, inside, adc);
    }
}

static void traceRegion(void)
{
    Vu1Ref r;
    /* normal_c / normal_l code 32 with the scene's M (X = 8x + 2048 at
     * z = 2, Y = 2048): v0 x 0 (strip start), v1 x -2 (X 2032), v2 x 600
     * (X 6848), v3 x -600 (X -2752), v4 x 744 (X 8000), v5 x 0. v2, v3 and
     * v4 are past the window 0 < X < 4094, so every vertex is ADC: v0 and
     * v1 by the strip start, v2..v4 outside, v5 by the counter (vi05 3). */
    static const float xs[6] = {0, -2, 600, -600, 744, 0};
    static const int32_t gx[6] = {32768, 32512, 109568, -44032, 128000, 32768};
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
    for (int prog = 0; prog < 2; prog++) {
        const char *what = prog ? "normal_l 32" : "normal_c 32";
        stateNormal(&r);
        if (prog) {
            vu1ref_normal_l(&r, 32, (const float (*)[4])l, &outA);
        } else {
            vu1ref_normal_c(&r, 32, (const float (*)[4])c, &outA);
        }
        for (int k = 0; k < 6 && outA.count == 6; k++) {
            regionVertex(what, k, &outA.v[k], gx[k], in43[k], 1);
        }
        int n = vu1ref_kicks(&outA, kicks);
        if (outA.count != 6 || n != 0) {
            FAILF("%s: %d vertices, %d kicks\n", what, outA.count, n);
        }
    }

    /* the grid program (mesh code 20) with M = identity: p = pos, w = 1.
     * v0 (2048, 2048), v1 (2100, 2100), v2 (4200, 2060), v3 x 2^-20.
     * vi05 starts at 0: v0 and v1 kick (nothing drawn yet). v2 is outside
     * (ADC, vi05 = 3). v3 passes the test (0 < 2^-20), ADC by the counter. */
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
    stateMesh(&r);
    vu1ref_mesh_set_matrix(&r, m);
    vu1ref_mesh(&r, 20, (const float (*)[4])g, &outA);
    if (outA.count != 4) {
        FAILF("mesh 20 count %d\n", outA.count);
    } else {
        regionVertex("mesh 20", 0, &outA.v[0], 32768, 1, 0);
        regionVertex("mesh 20", 1, &outA.v[1], 33600, 1, 0);
        regionVertex("mesh 20", 2, &outA.v[2], 67200, 0, 1);
        regionVertex("mesh 20", 3, &outA.v[3], 0, 1, 1);
    }

    /* the rule: the state (vu1ref_extended: 0 the Original preset at 4:3
     * and 1x, 1 any other), free camera, the three tests, HIDE, whether
     * the clipped triangle reaches the camera's picture, every vertex in
     * front of the eye, the strip joined to another, whether a triangle of
     * its strip passes -> what the port draws */
    static const struct {
        int ext, free, a, b, c, hide, reach, front, joined, strip, want;
    } kRule[] = {
        {0, 0, 1, 1, 1, 0, 1, 1, 0, 1, VU1REF_DRAW_EXACT},
        {1, 0, 1, 1, 1, 0, 1, 1, 0, 1, VU1REF_DRAW_EXACT},
        {0, 0, 1, 0, 1, 0, 1, 1, 1, 1, VU1REF_DRAW_NONE}, /* reaches the picture: the PS2's */
        {1, 0, 0, 1, 1, 0, 1, 1, 0, 0, VU1REF_DRAW_NONE}, /* a card: free-standing, none passes */
        {1, 0, 0, 1, 1, 0, 1, 1, 0, 1, VU1REF_DRAW_CLIPPED}, /* a wall: its strip passes */
        {1, 0, 0, 1, 1, 0, 1, 1, 1, 0, VU1REF_DRAW_CLIPPED}, /* a wall: its strip joined */
        {1, 0, 0, 1, 1, 0, 1, 0, 1, 1, VU1REF_DRAW_NONE},    /* a vertex behind the eye */
        {0, 0, 1, 0, 1, 0, 0, 1, 0, 0, VU1REF_DRAW_CLIPPED}, /* wholly beside */
        {1, 0, 0, 1, 1, 0, 0, 0, 0, 0, VU1REF_DRAW_CLIPPED},
        {0, 1, 1, 0, 1, 0, 1, 0, 0, 0, VU1REF_DRAW_CLIPPED}, /* the free camera */
        {1, 1, 0, 0, 0, 0, 1, 0, 0, 0, VU1REF_DRAW_CLIPPED},
        {1, 0, 1, 1, 1, 1, 1, 1, 1, 1, VU1REF_DRAW_NONE}, /* HIDE: nowhere */
        {1, 1, 0, 1, 1, 1, 0, 1, 0, 1, VU1REF_DRAW_NONE},
        {0, 0, 1, 1, 1, 1, 0, 1, 0, 1, VU1REF_DRAW_NONE},
    };

    for (int i = 0; i < (int)(sizeof(kRule) / sizeof(kRule[0])); i++) {
        const int d = vu1ref_region_draw(kRule[i].ext, kRule[i].free, kRule[i].a, kRule[i].b,
                                         kRule[i].c, kRule[i].hide, kRule[i].reach, kRule[i].front,
                                         kRule[i].joined, kRule[i].strip);
        if (d != kRule[i].want) {
            FAILF("region rule %d: %d, expected %d\n", i, d, kRule[i].want);
        }
    }
    /* the state: out of the box is the Original preset on the 4:3 frame
     * (512) at 1x only */
    if (vu1ref_extended(VU1REF_ORIGINAL, 512, 1.0f) != 0 ||
        vu1ref_extended(VU1REF_ENHANCED, 512, 1.0f) != 1 ||
        vu1ref_extended(VU1REF_ORIGINAL, 688, 1.0f) != 1 ||
        vu1ref_extended(VU1REF_ORIGINAL, 512, 2.0f) != 1) {
        FAILF("vu1ref_extended\n");
    }

    /* particles: shown, drawn (the VU's window) */
    static const struct {
        int ext, free, drawn, shown, reach, want;
    } kSprite[] = {
        {0, 0, 1, 1, 1, VU1REF_DRAW_EXACT},
        {0, 0, 0, 1, 1, VU1REF_DRAW_NONE},    /* reaches the picture: out of the box */
        {1, 0, 0, 1, 1, VU1REF_DRAW_CLIPPED}, /* any other state: an effect, not a card */
        {0, 0, 0, 1, 0, VU1REF_DRAW_CLIPPED}, /* wholly beside */
        {1, 0, 0, 1, 0, VU1REF_DRAW_CLIPPED},
        {0, 1, 0, 1, 1, VU1REF_DRAW_CLIPPED}, /* the free camera */
        {1, 1, 0, 1, 1, VU1REF_DRAW_CLIPPED},
        {1, 0, 0, 0, 0, VU1REF_DRAW_NONE},
        {1, 1, 0, 0, 0, VU1REF_DRAW_NONE},
    };

    for (int i = 0; i < (int)(sizeof(kSprite) / sizeof(kSprite[0])); i++) {
        const int d = vu1ref_particle_draw(kSprite[i].ext, kSprite[i].free, kSprite[i].drawn,
                                           kSprite[i].shown, kSprite[i].reach);
        if (d != kSprite[i].want) {
            FAILF("particle rule %d: %d, expected %d\n", i, d, kSprite[i].want);
        }
    }
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
    /* FrameCB frame[0], the region scale rd sets on a frame-wide target: 1
     * (4:3), or 0.375 (a frame 1365 wide, 32:9). No VU shader reads it:
     * what is drawn is the same at every aspect, and the wide cases'
     * inputs put vertices past the window (makeInput) */
    float wide;
    int depart; /* DEPART_*: input made to show the port's departure from the VU */
    int preset; /* VU1REF_ORIGINAL (0, FrameCB misc[1] 0) or VU1REF_ENHANCED */
    int mark;   /* MARK_*: the content table's index bits on every triangle */
    /* the camera's picture's half-width and half-height (FrameCB frame[1],
     * frame[2]): 0, the frame's camera's, 256 by PICTURE_HALF_H; a
     * reflection camera's (puddle.c's 116) */
    int pictureHalf;
    /* 1: every triangle the region test drops is drawn clipped by the
     * strip rule (ICO_VU_EXTENDED, its strip has a passing triangle) */
    int strip;
} Case;

/* half the native height (448), the frame's camera's picture's half-height */
#define PICTURE_HALF_H 224.0f

/* The port's rule (vu1ref_region_draw), one camera in both presets: a
 * triangle the region test drops is not drawn when what the GPU would
 * draw of it reaches the camera's picture (GS X 1792..2304 by Y
 * 1824..2272, cut to its rows first; the 64-pixel target around 2048 lies
 * inside it) and drawn clipped by the GPU at the
 * native frustum when wholly beside it, and under photo mode's free
 * camera. DEPART_EYE: vertex 7 of each batch behind the eye (model (0, 0,
 * -1), w = -1): the VU drops its three triangles; they reach the picture,
 * not drawn. DEPART_EYE_FREE: the same input drawn with
 * ICO_VU_BEHIND_EYE (photo mode's free camera): drawn clipped in either
 * preset. DEPART_EYE_TICKS: the same input drawn with ICO_VU_TICKS (a
 * picture between two ticks), the tick before's camera the drawn
 * one and this tick's moved back 2 units: vertex 7 is in front of that
 * tick's eye and its triangles inside the window there, so they are drawn
 * clipped in either preset (vu1ref_region_between); DEPART_EYE_TICKS_BOTH:
 * moved back 0.5, vertex 7 behind the eye at both ticks: not drawn.
 * DEPART_BAND_IN: vertex 7 past the window below the picture
 * (model (0, 600, 2), Y 6848). DEPART_CARD: the strip right of the
 * target's centre (GS X 2088..2128) and vertex 7 past the window far left
 * (model (-744, y, 2), X -3904), all in front of the eye: its triangles
 * cross the target and reach the picture, not drawn out of the box; in
 * any other state the strip (vertices 5..11) has passing triangles (10,
 * 11), so they are drawn clipped (a wall). DEPART_CARD_STRIP:
 * vertices 7 and 9 past the window far left: no triangle of the strip
 * 5..11 passes, a card as the opening's tree card (st26a_near's
 * 2-triangle strip): not drawn in any state. DEPART_GRID_ROW (the grid):
 * the last batch's odd vertices past the window far left (X -3904), the
 * others at GS X 2088..2128: none of that batch's triangles passes; with
 * two batches or more its strips share rows (ICO_VU_INDEX_JOINED, as
 * rd_replay.c marks a grid), drawn clipped outside the out-of-the-box
 * state; a grid of one batch is a card. DEPART_BESIDE: the target placed right of the
 * picture (GS X 2320..2384), the strip at GS X 2330..2378 and vertex 7 at
 * X 8000: its failing triangles lie wholly beside the picture, drawn
 * clipped in both presets (walls and the sea beside a wide picture).
 * DEPART_ABOVE: the target right of the picture as DEPART_BESIDE, vertex 7
 * at GS (2000, -60000), far above the picture, in front of the eye: within
 * the picture's columns its triangles lie wholly above the picture (only
 * sky there), on the target beside it they cover pixels: wholly beside
 * the picture once cut to its rows, drawn clipped in both presets (an
 * x-only test dropped them, a hole beside the picture). DEPART_ABOVE_IN:
 * the same vertex 7 with the strip on the target around 2048, inside the
 * picture: they reach it, not drawn (the PS2's picture at 4:3).
 * DEPART_REFL_BESIDE: a reflection camera's picture, 2048 -+ 116
 * (puddle.c), the target at GS X 2168..2232, right of it and inside the
 * frame camera's 2048 -+ 256, the strip at X 2178..2226 and vertex 7 at X
 * 8000: wholly beside the reflection's picture, drawn clipped (the frame
 * camera's picture dropped them, holes in a wide reflection).
 * DEPART_SPRITE: particle 0 is a 1040-pixel sprite from X 2040 (inside the
 * target) to 3080 (past the window's 3071): the VU skips it; it reaches
 * the picture, not drawn. DEPART_SPRITE_BESIDE: the same sprite from X
 * 2328 to 3368, wholly right of the picture, into the target right of
 * it: drawn whole. DEPART_SPRITE_FREE: DEPART_SPRITE with
 * ICO_VU_BEHIND_EYE, drawn whole, and particle 1 behind the eye (model z
 * -2), which the w window still skips. */
enum {
    DEPART_NONE,
    DEPART_EYE,
    DEPART_EYE_FREE,
    DEPART_BAND_IN,
    DEPART_SPRITE,
    DEPART_SPRITE_FREE,
    DEPART_CARD,
    DEPART_BESIDE,
    DEPART_SPRITE_BESIDE,
    DEPART_ABOVE,
    DEPART_ABOVE_IN,
    DEPART_REFL_BESIDE,
    DEPART_CARD_STRIP,
    DEPART_GRID_ROW,
    DEPART_EYE_TICKS,
    DEPART_EYE_TICKS_BOTH,
    DEPART_EYE_TICKS_BESIDE,
    DEPART_EYE_TICKS_BAND,
    DEPART_CARD_TICKS,
    DEPART_SPRITE_TICKS
};

/* The content table's mark (rd_content_hide.c; vu_content_hidden):
 * MARK_HIDE every triangle carries ICO_VU_INDEX_HIDE (drawn nowhere).
 * MARK_JOINED every triangle carries ICO_VU_INDEX_JOINED (rd_mesh.c
 * markJoinedStrips: its strip is joined to another, not a card). */
enum { MARK_NONE, MARK_HIDE, MARK_JOINED };

/* DEPART_EYE's input seen through a camera blended between two
 * ticks' (ICO_VU_TICKS), each tick's camera the blended one with the model
 * moved by tickMove (makeVuCB's T): DEPART_EYE_TICKS, the tick before's
 * the blended one, this tick's moved back 2 along z: vertex 7 (model z -1)
 * lies in front of that tick's eye, every triangle of it inside the window
 * and on the target there (shown); DEPART_EYE_TICKS_BOTH, this tick's
 * moved back 0.5: behind the eye at both ticks.  DEPART_EYE_TICKS_BESIDE
 * and DEPART_EYE_TICKS_BAND: this tick's model moved 0.5 along z,
 * vertex 7 behind the eye there and its triangles reaching the picture
 * (dropped); the tick before's model moved so that they lie wholly beside
 * the picture, vertex 7 at z 0.5, behind the near plane (clipped).
 * BESIDE: the other vertices at GS X 2056..2064 (makeVertex out 12), the
 * move (66, 0, 1.5) puts them at X 2354..2359; the target around 2048
 * inside the picture, as at 4:3: the tick before drew them off the frame,
 * never seen, so between the ticks they stay dropped.  BAND: the other
 * vertices at GS X 2228..2348 (out 13), the move (40, 0, 1.5) puts them at
 * X 2334..2402 and the near cut at about 2750; the target right of the
 * picture (GS X 2320..2384), as the band beside the picture on a wider
 * frame: the tick before showed them there, so they are drawn clipped.
 * DEPART_CARD_TICKS: DEPART_CARD_STRIP's card with the tick before's
 * model moved (500, 0, 6), every vertex inside the window and the card
 * across the target there: at the blended camera the card test drops it,
 * and the ticks do not overrule the card test (the shader and
 * vu1ref_region_between alike).  DEPART_SPRITE_TICKS: DEPART_SPRITE's
 * sprite, which the VU skips (X 2040..3080, past the window's 3071) and
 * which reaches the picture, with the tick before's model moved 2 to the
 * left (X 2024..3064, inside the window, across the target): out of the
 * box the tick before drew it, so it is drawn whole between the ticks
 * (vu_particle.hlsl vu_particle_tick_shown) */
static int tickCase(const Case *c)
{
    return c->depart == DEPART_EYE_TICKS || c->depart == DEPART_EYE_TICKS_BOTH ||
           c->depart == DEPART_EYE_TICKS_BESIDE || c->depart == DEPART_EYE_TICKS_BAND ||
           c->depart == DEPART_CARD_TICKS || c->depart == DEPART_SPRITE_TICKS;
}

/* the model's move at tick's camera (x, y, z) */
static void tickMove(const Case *c, int tick, float d[3])
{
    d[0] = d[1] = d[2] = 0.0f;
    switch (c->depart) {
    case DEPART_EYE_TICKS:
        d[2] = tick ? 2.0f : 0.0f;
        break;
    case DEPART_EYE_TICKS_BOTH:
        d[2] = tick ? 0.5f : 0.0f;
        break;
    case DEPART_EYE_TICKS_BESIDE:
        d[0] = tick ? 0.0f : 66.0f;
        d[2] = tick ? 0.5f : 1.5f;
        break;
    case DEPART_EYE_TICKS_BAND:
        d[0] = tick ? 0.0f : 40.0f;
        d[2] = tick ? 0.5f : 1.5f;
        break;
    case DEPART_CARD_TICKS:
        d[0] = tick ? 0.0f : 500.0f;
        d[2] = tick ? 0.0f : 6.0f;
        break;
    case DEPART_SPRITE_TICKS:
        d[0] = tick ? 0.0f : -2.0f;
        break;
    default:
        break;
    }
}

/* the vertex behind the blended camera's eye: the tick cases but the card
 * and the sprite */
static int eyeTickCase(const Case *c)
{
    return tickCase(c) && c->depart != DEPART_CARD_TICKS && c->depart != DEPART_SPRITE_TICKS;
}

/* photo mode's free camera: the draw carries ICO_VU_BEHIND_EYE */
static int freeCamera(const Case *c)
{
    return c->depart == DEPART_EYE_FREE || c->depart == DEPART_SPRITE_FREE;
}

/* the region rule's state (vu1ref_extended): a wide case is a frame 1365
 * wide (32:9), the others 4:3 */
static int extendedState(const Case *c)
{
    return vu1ref_extended(c->preset, c->wide != 1.0f ? 1365 : 512, 1.0f);
}

/* the strips are joined: the case's mark, or a grid of two batches or
   more (rd_replay.c marks its indices ICO_VU_INDEX_JOINED) */
static int joinedCase(const Case *c)
{
    return c->mark == MARK_JOINED || (c->prog == P_MESH && c->nbatch >= 2);
}

static uint32_t markBit(const Case *c)
{
    if (c->prog == P_MESH && c->mark == MARK_NONE) {
        return joinedCase(c) ? ICO_VU_INDEX_JOINED : 0u;
    }
    return c->mark == MARK_HIDE     ? ICO_VU_INDEX_HIDE
           : c->mark == MARK_JOINED ? ICO_VU_INDEX_JOINED
                                    : 0u;
}

/* the GS X of the RT x RT target's left edge: around 2048, inside the
 * camera's picture, or right of the picture for the beside cases */
static float targetLeft(const Case *c)
{
    if (c->depart == DEPART_REFL_BESIDE) {
        return 2168.0f;
    }
    return c->depart == DEPART_BESIDE || c->depart == DEPART_SPRITE_BESIDE ||
                   c->depart == DEPART_ABOVE || c->depart == DEPART_EYE_TICKS_BAND
               ? 2320.0f
               : 2048.0f - RT / 2;
}

/* the camera's picture's half-width and half-height in GS pixels, as the
 * case's FrameCB carries them (frame[1], frame[2]) */
static float pictureHalfW(const Case *c)
{
    return c->pictureHalf ? (float)c->pictureHalf : 256.0f;
}

static float pictureHalfH(const Case *c)
{
    return c->pictureHalf ? (float)c->pictureHalf : PICTURE_HALF_H;
}

/* the camera's picture, GS (2048, 2048) -+ its half-size (vu_common.hlsli
 * vu_picture_half), in pixels of the target: left, right, top, bottom
 * (vu1_ref.h vu1ref_polygon_reaches_picture's pic) */
static void picture(const Case *c, float pic[4])
{
    pic[0] = 2048.0f - pictureHalfW(c) - targetLeft(c);
    pic[1] = 2048.0f + pictureHalfW(c) - targetLeft(c);
    pic[2] = RT / 2 - pictureHalfH(c);
    pic[3] = RT / 2 + pictureHalfH(c);
}

typedef struct CaseRef CaseRef;
static int portPolygon(const Case *c, const CaseRef *cr, int k, float poly[4][2]);
static int stripPasses(const Case *c, const CaseRef *cr, int k);

/* Whether what the GPU draws of strip triangle k (portPolygon, cut at the
 * near plane, then to the picture's rows) reaches the camera's picture
 * (vu_cut_z and vu_cut_rows, vu1ref_polygon_reaches_picture); -1
 * when it cannot be predicted. */
static int triReaches(const Case *c, const CaseRef *cr, int k)
{
    float poly[4][2], pic[4];
    const int n = portPolygon(c, cr, k, poly);
    if (n < 0) {
        return -1;
    }
    if (n < 3) {
        return 0;
    }
    picture(c, pic);
    return vu1ref_polygon_reaches_picture((const float (*)[2])poly, n, pic);
}

/* a tick case's reference at each tick's camera, while the case's
 * indices are made */
static const CaseRef *s_tickRef[2];
static int ticksDraw(const Case *c, int k, int strip, int d, int card);

/* What the port draws of strip triangle k (vertices k-2..k): the oracle's
 * rule (vu1_ref.h vu1ref_region_draw); every static kick of a program
 * without the region test is drawn as the VU draws it.  cr NULL: only
 * whether it is drawn exactly matters (the reach is not computed).  A
 * tick case (s_tickRef set): vu1ref_region_between over the rule at each
 * tick's camera, the blended camera's card test. */
static int portDraws(const Case *c, const CaseRef *cr, const VuGsVertex *v, int k)
{
    if (c->clip != ICO_VU_CLIP_REGION) {
        return VU1REF_DRAW_EXACT;
    }
    int reaches = 0, strip = 0;
    if (cr && !(v[k - 2].inside && v[k - 1].inside && v[k].inside)) {
        reaches = triReaches(c, cr, k);
        if (reaches < 0) {
            FAILF("%s: triangle %d: the reach cannot be predicted\n", c->name, k);
            reaches = 1;
        }
        strip = stripPasses(c, cr, k);
    }
    const int front = v[k - 2].clipW > 0.0f && v[k - 1].clipW > 0.0f && v[k].clipW > 0.0f;
    const int d =
        vu1ref_region_draw(extendedState(c), freeCamera(c), v[k - 2].inside, v[k - 1].inside,
                           v[k].inside, c->mark == MARK_HIDE, reaches, front, joinedCase(c), strip);
    if (!cr || !s_tickRef[0] || !s_tickRef[1]) {
        return d;
    }
    /* the blended camera's verdict is the card test's (VU_RULE_STRIP) */
    const int card = !freeCamera(c) && c->mark != MARK_HIDE && reaches && front &&
                     extendedState(c) && !(v[k - 2].inside && v[k - 1].inside && v[k].inside);
    return ticksDraw(c, k, strip, d, card);
}

/* the CPU side of one case */
struct CaseRef {
    int nv;
    VuGsVertex v[MAXV];
    float stw[MAXV];
    int first[MAXV];
    float pos[MAXV][4]; /* each vertex's model position (the input's first qword) */
    IcoSpriteVertex tri[MAXV * 3 * 3];
    int ntri;
    VuParticleOut po;
    int shown[MAXV]; /* particles: alpha and the w window (the probe's field 12) */
    int added;       /* particles the VU skips that the port draws whole */
    int dropped;     /* particles the VU skips that the port drops too */
};

/* whether a triangle of strip triangle k's strip passes the region test
 * (vu1ref_strip_passes over the case's static kicks) */
static int stripPasses(const Case *c, const CaseRef *cr, int k)
{
    return vu1ref_strip_passes(cr->v, c->prog == P_MESH ? NULL : cr->stw, cr->first, cr->nv, k);
}

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
 * 0.375) use out 2: x = 255.875 (X 4095, just past the window's 4094, and
 * below 4096 so the reference's 16-bit sprite X is the same point), and
 * out 3: x = 744 (X 8000). out 4 (DEPART_EYE): (0, 0, -1), behind the eye.
 * out 5 (DEPART_BAND_IN): (0, 600, 2), Y 6848, below the window. out 6
 * (DEPART_CARD): GS X 2088..2128, right of the target's centre. out 8
 * (DEPART_CARD): x = -744 (X -3904), z = 2, past the window on the left.
 * out 9 (DEPART_BESIDE): GS X 2330..2378, right of the camera's picture.
 * out 10 (DEPART_ABOVE): (-6, -7756, 2), GS (2000, -60000), far above the
 * picture. out 11 (DEPART_REFL_BESIDE): GS X 2178..2226. out 12
 * (DEPART_EYE_TICKS_BESIDE, _BAND): z 2, GS X 2056..2064, Y 4 to 10
 * above the centre for an even vertex, below for an odd one. out 13
 * (DEPART_EYE_TICKS_BAND): the same at GS X 2228..2348. */
static void makeVertex(int prog, float (*v)[4], int k, int start, int out, int zmode, int scissor)
{
    float z = zmode == 2 ? (k & 1 ? 0.5f : 2.0f) : zmode == 1 ? 2.0f : rnd(1.8f, 2.4f);
    float x = rnd(-3.5f, 3.5f) * z / 2.0f, y = rnd(-3.5f, 3.5f) * z / 2.0f;
    if (out == 4) {
        x = 0.0f;
        y = 0.0f;
        z = -1.0f;
    } else if (out == 5) {
        x = 0.0f;
        y = 600.0f;
        z = 2.0f;
    } else if (out == 6) {
        x = rnd(40.0f, 80.0f) * z / 16.0f;
    } else if (out == 8) {
        x = -744.0f;
        z = 2.0f;
    } else if (out == 9) {
        x = rnd(282.0f, 330.0f) * z / 16.0f;
    } else if (out == 10) {
        x = -6.0f;
        y = -7756.0f;
        z = 2.0f;
    } else if (out == 11) {
        x = rnd(130.0f, 178.0f) * z / 16.0f;
    } else if (out == 12 || out == 13) {
        z = 2.0f;
        x = (out == 12 ? rnd(8.0f, 16.0f) : rnd(180.0f, 300.0f)) * z / 16.0f;
        y = (k & 1 ? 1.0f : -1.0f) * rnd(4.0f, 10.0f) * z / 16.0f;
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

/* portDraws at a camera blended between the ticks (s_tickRef): whether
 * each tick showed the triangle (the rule at its camera with the blended
 * camera's card test (strip) draws it, and what the GPU draws of it there,
 * portPolygon, reaches the frame: the RT x RT target), then
 * vu1ref_region_between with d, the blended camera's verdict, and card,
 * whether that was the card test's */
static int ticksDraw(const Case *c, int k, int strip, int d, int card)
{
    int shown[2];
    for (int t = 0; t < 2; t++) {
        const VuGsVertex *u = s_tickRef[t]->v;
        int r = 0;
        if (!(u[k - 2].inside && u[k - 1].inside && u[k].inside)) {
            r = triReaches(c, s_tickRef[t], k);
            if (r < 0) {
                FAILF("%s: triangle %d: the reach at tick %d cannot be predicted\n", c->name, k, t);
                r = 1;
            }
        }
        const int f = u[k - 2].clipW > 0.0f && u[k - 1].clipW > 0.0f && u[k].clipW > 0.0f;
        const int at =
            vu1ref_region_draw(extendedState(c), freeCamera(c), u[k - 2].inside, u[k - 1].inside,
                               u[k].inside, c->mark == MARK_HIDE, r, f, joinedCase(c), strip);
        float poly[4][2];
        const float frame[4] = {0.0f, (float)RT, 0.0f, (float)RT};
        const int n = at != VU1REF_DRAW_NONE ? portPolygon(c, s_tickRef[t], k, poly) : 0;
        if (n < 0) {
            FAILF("%s: triangle %d: the frame reach at tick %d cannot be predicted\n", c->name, k,
                  t);
        }
        shown[t] =
            n < 0 || (n >= 3 && vu1ref_polygon_reaches_picture((const float (*)[2])poly, n, frame));
    }
    return vu1ref_region_between(d, card, shown[0], shown[1]);
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
        qw(in[6], 300, 0, 4, 2); /* outside the window: skipped by the VU (Enhanced draws it) */
        if (c->depart == DEPART_SPRITE || c->depart == DEPART_SPRITE_FREE ||
            c->depart == DEPART_SPRITE_TICKS) {
            /* h = (5120, 4096, 3 * 2^20, 2), half extent 16 * 65 / 2 = 520:
             * corners (2040, 1528) and (3080, 2568) */
            qw(in[6], 64, 0, 2, 65);
            qw(in[7], 0.25f, 0.5f, 128, 64);
        }
        if (c->depart == DEPART_SPRITE_BESIDE) {
            /* h.x 5696: corners (2328, 1528) and (3368, 2568) */
            qw(in[6], 100, 0, 2, 65);
            qw(in[7], 0.25f, 0.5f, 128, 64);
        }
        if (c->depart == DEPART_SPRITE_FREE) {
            qw(in[8], 0, 0, -2, 1); /* w = -2: behind the eye */
            qw(in[9], 0.25f, 0.5f, 128, 64);
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
            if (c->depart == DEPART_EYE_TICKS_BESIDE || c->depart == DEPART_EYE_TICKS_BAND) {
                out = c->depart == DEPART_EYE_TICKS_BAND ? 13 : 12;
            }
            if ((c->depart == DEPART_EYE || c->depart == DEPART_EYE_FREE || eyeTickCase(c)) &&
                k == 7) {
                out = 4;
            }
            if (c->depart == DEPART_BAND_IN) {
                out = k == 7 ? 5 : 0;
            }
            if (c->depart == DEPART_CARD) {
                out = k == 7 ? 8 : 6;
            }
            if (c->depart == DEPART_CARD_STRIP || c->depart == DEPART_CARD_TICKS) {
                out = k == 7 || k == 9 ? 8 : 6;
            }
            if (c->depart == DEPART_GRID_ROW) {
                out = b == c->nbatch - 1 && (k & 1) ? 8 : 6;
            }
            if (c->depart == DEPART_BESIDE) {
                out = k == 7 ? 3 : 9;
            }
            if (c->depart == DEPART_ABOVE) {
                out = k == 7 ? 10 : 9;
            }
            if (c->depart == DEPART_ABOVE_IN) {
                out = k == 7 ? 10 : 0;
            }
            if (c->depart == DEPART_REFL_BESIDE) {
                out = k == 7 ? 3 : 11;
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

/* o = a^-1 (column-major), Gauss-Jordan with partial pivoting; 0 when
 * singular */
static int invert4(const double *a, double *o)
{
    double w[4][8];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            w[r][c] = a[c * 4 + r];
            w[r][4 + c] = r == c ? 1.0 : 0.0;
        }
    }
    for (int c = 0; c < 4; c++) {
        int p = c;
        for (int r = c + 1; r < 4; r++) {
            p = fabs(w[r][c]) > fabs(w[p][c]) ? r : p;
        }
        if (fabs(w[p][c]) < 1e-12) {
            return 0;
        }
        for (int k = 0; k < 8; k++) {
            const double t = w[c][k];
            w[c][k] = w[p][k];
            w[p][k] = t;
        }
        const double d = w[c][c];
        for (int k = 0; k < 8; k++) {
            w[c][k] /= d;
        }
        for (int r = 0; r < 4; r++) {
            if (r != c) {
                const double f = w[r][c];
                for (int k = 0; k < 8; k++) {
                    w[r][k] -= f * w[c][k];
                }
            }
        }
    }
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            o[c * 4 + r] = w[r][4 + c];
        }
    }
    return 1;
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
    cb->draw[2] = (uint32_t)c->clip | (freeCamera(c) ? ICO_VU_BEHIND_EYE : 0u) |
                  (extendedState(c) ? ICO_VU_EXTENDED : 0u);
    if (tickCase(c)) {
        /* each tick's camera, R = M T M^-1 (M the model to GS screen
         * matrix, mem 16..19; T the model moved by tickMove, the
         * camera moved back), as rd_interp.c gives S_tick St^-1 */
        cb->draw[2] |= ICO_VU_TICKS;
        double m[16], im[16];
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                m[col * 4 + row] = cb->mem[16 + col][row];
            }
        }
        if (!invert4(m, im)) {
            FAILF("%s: the model matrix is singular\n", c->name);
        }
        for (int t = 0; t < 2; t++) {
            /* M T M^-1 = I + (M d) (row 3 of M^-1), d the move */
            float d[3];
            tickMove(c, t, d);
            for (int col = 0; col < 4; col++) {
                for (int row = 0; row < 4; row++) {
                    const double md = d[0] * m[0 + row] + d[1] * m[4 + row] + d[2] * m[8 + row];
                    cb->ticks[t * 4 + col][row] =
                        (float)((row == col ? 1.0 : 0.0) + md * im[col * 4 + 3]);
                }
            }
        }
    }
    cb->draw[3] = c->prog == P_PARTICLE || c->tagless ? 0u : (uint32_t)c->vpb;
    cb->batch[0] = c->tagless ? 0u : (uint32_t)c->hdr;
    cb->batch[1] = (uint32_t)c->trail;
    /* the card test on, no card bits yet (cardBits) */
    cb->strip[1] = c->prog == P_PARTICLE ? 0u : 1u;
}

/* the card test's kind of a case's program (vu1_ref.h VU1REF_CARD_*) */
static int cardKind(const Case *c)
{
    return c->prog == P_CLUSTER ? VU1REF_CARD_CLUSTER
           : c->prog == P_MESH  ? VU1REF_CARD_MESH
                                : VU1REF_CARD_NORMAL;
}

/* vertex v's first qword in the case's input (makeInput's batches) */
static const float (*caseVertex(const Case *c, const float (*in)[4], int v))[4]
{
    const int b = v / c->vpb, k = v % c->vpb;
    return in + b * (c->hdr + c->vpb * c->vq + c->trail) + c->hdr + k * c->vq;
}

/* The card verdicts as the renderer hands them to the shader (rd_mesh.h
 * RdVuCards, rd_replay.c doVuDraw): both bits of every static kick whose
 * strip has no passing triangle (the oracle's stripPasses, the blended
 * camera's in a tick case, as portDraws decides), in VuBoneCB from qword 8
 * for the cluster (its bones, addresses 16 and 20, are qwords 0..7) and
 * from 0 for the others; vu_strip = (0, 1, the kicks, that qword) */
static void cardBits(const Case *c, const CaseRef *cr, IcoVuCB *cb, IcoVuBoneCB *bones)
{
    if (c->prog == P_PARTICLE) {
        return;
    }
    const uint32_t q0 = c->prog == P_CLUSTER ? 8u : 0u;
    const uint32_t n = (uint32_t)cr->nv;
    uint32_t w[(MAXV + 15) / 16 + 4];
    memset(w, 0, sizeof(w));
    for (uint32_t k = 2; k < n; k++) {
        const int kick = (int)k - cr->first[k] >= 2 &&
                         (c->prog == P_MESH || (cr->stw[k] >= 1.0f && cr->stw[k - 1] >= 1.0f));
        if (kick && !stripPasses(c, cr, (int)k)) {
            w[k >> 4] |= 3u << ((k & 15u) * 2u);
        }
    }
    memcpy(bones->bone[q0], w, ((n + 15u) / 16u) * 4u);
    cb->strip[0] = 0u;
    cb->strip[2] = n;
    cb->strip[3] = q0;
}

/* The CPU's card test (vu1_ref.h vu1ref_region_inside, vu1ref_strip_card,
 * what rd_mesh.c decides from) against the reference: every vertex's
 * region test as the program's (VuGsVertex.inside), and every static
 * kick's strip a card exactly when vu1ref_strip_passes finds no passing
 * triangle in it, at the case's camera (cb, bones) */
static void checkCards(const Case *c, const CaseRef *cr, const float (*in)[4], const IcoVuCB *cb,
                       const IcoVuBoneCB *bones)
{
    if (c->prog == P_PARTICLE || c->clip != ICO_VU_CLIP_REGION) {
        return;
    }
    const int mode = fegetround();
    fesetround(FE_TONEAREST);
    const int kind = cardKind(c);
    for (int v = 0; v < cr->nv; v++) {
        const int in1 = vu1ref_region_inside(kind, (const float (*)[4])cb->mem,
                                             (const float (*)[4])bones->bone, caseVertex(c, in, v));
        if (in1 != (cr->v[v].inside != 0)) {
            FAILF("%s: vertex %d: the CPU card test's region test %d, the reference's %d\n",
                  c->name, v, in1, cr->v[v].inside);
        }
    }
    for (int k = 2; k < cr->nv; k++) {
        if (k - cr->first[k] < 2 ||
            (c->prog != P_MESH && (cr->stw[k] < 1.0f || cr->stw[k - 1] < 1.0f))) {
            continue;
        }
        /* the strip: the run of static kicks around k, s .. e - 1 its vertices */
        int s = k - 2, e = k + 1;
        while (s > cr->first[k] && (c->prog == P_MESH || cr->stw[s] >= 1.0f)) {
            s--;
        }
        while (e < cr->nv && cr->first[e] == cr->first[k] &&
               (c->prog == P_MESH || cr->stw[e] >= 1.0f)) {
            e++;
        }
        const int card =
            vu1ref_strip_card(kind, (const float (*)[4])cb->mem, (const float (*)[4])bones->bone,
                              caseVertex(c, in, s), (uint32_t)c->vq, (uint32_t)(e - s), NULL);
        if (card != !stripPasses(c, cr, k)) {
            FAILF("%s: kick %d: the CPU card test says %s, the reference %s\n", c->name, k,
                  card ? "card" : "no card", stripPasses(c, cr, k) ? "passes" : "card");
        }
    }
    fesetround(mode);
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

/* Particle i as the port draws it somewhere (vu_particle.hlsl: the alpha
 * test and the w window of qw 3/4, not the x/y window), with the scene's M as both
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
         * ones the VU skips for its x/y window that the port draws whole
         * (vu1ref_particle_draw: wholly beside the camera's picture, or the
         * free camera) */
        static VuGsSprite added;
        for (int k = 0, pi = 0; k < c->vpb; k++) {
            const VuGsSprite *s = 0;
            cr->shown[k] = portSprite(in, k, &added);
            const int drawn = pi < cr->po.count && cr->po.s[pi].index == k;
            float pic[4];
            picture(c, pic);
            const float top = 2048.0f - RT / 2;
            const int reaches =
                vu1ref_rect_reaches_picture((float)added.xyz[0][0] / 16.0f - targetLeft(c),
                                            (float)added.xyz[0][1] / 16.0f - top,
                                            (float)added.xyz[1][0] / 16.0f - targetLeft(c),
                                            (float)added.xyz[1][1] / 16.0f - top, pic);
            int d =
                vu1ref_particle_draw(extendedState(c), freeCamera(c), drawn, cr->shown[k], reaches);
            if (d == VU1REF_DRAW_NONE && cr->shown[k] && s_tickRef[0] && s_tickRef[1]) {
                /* between two ticks: drawn whole when a tick's VU drew it
                 * on the frame (the RT x RT target) */
                int shownAt[2] = {0, 0};
                for (int t = 0; t < 2; t++) {
                    const VuParticleOut *po = &s_tickRef[t]->po;
                    for (int j = 0; j < po->count; j++) {
                        const VuGsSprite *ts = &po->s[j];
                        const float frame[4] = {0.0f, (float)RT, 0.0f, (float)RT};
                        shownAt[t] |=
                            ts->index == k && vu1ref_rect_reaches_picture(
                                                  (float)ts->xyz[0][0] / 16.0f - targetLeft(c),
                                                  (float)ts->xyz[0][1] / 16.0f - top,
                                                  (float)ts->xyz[1][0] / 16.0f - targetLeft(c),
                                                  (float)ts->xyz[1][1] / 16.0f - top, frame);
                    }
                }
                d = vu1ref_region_between(d, 0, shownAt[0], shownAt[1]);
            }
            if (drawn) {
                s = &cr->po.s[pi++];
            } else if (d == VU1REF_DRAW_CLIPPED) {
                s = &added;
                cr->added++;
            } else {
                cr->dropped += cr->shown[k];
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
            /* a kicked triangle the content table hides (the port's rule) */
            if (portDraws(c, NULL, big.v, kicks[i]) != VU1REF_DRAW_EXACT) {
                continue;
            }
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

/* Triangle k as the port draws it when the region test drops it and the
 * rule draws it clipped (VU1REF_DRAW_CLIPPED), as a polygon in pixels of
 * the RT x RT target: clipped to model z >= 1, which is GS Z >= 0 under
 * the scene's M (z' = 3 * 2^20 (z - 1), w' = z), where the GPU clips it
 * (0 <= z); then projected. Vertices in front of that use the reference's
 * GS X and Y (any program); a triangle with one behind is projected with M
 * itself, so only for normal_c and normal_l (the cluster's bones move it).
 * A triangle with a vertex behind the eye (model z <= 0, w <= 0) is cut
 * the same way. No aspect term: the GS X is drawn where it is at every
 * aspect. Returns the corner count, 0 when nothing is left, -1 when it
 * cannot be predicted. */
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
    const float half = RT / 2, left = targetLeft(c), cx = 2048.0f - left;
    int front = 0;
    for (int j = 0; j < 3; j++) {
        front += cr->pos[k - 2 + j][2] >= 1.0f;
    }
    if (front == 3) {
        for (int j = 0; j < 3; j++) {
            const VuGsVertex *v = &cr->v[k - 2 + j];
            poly[j][0] = (float)v->xyz[0] / 16.0f - left;
            poly[j][1] = half + ((float)v->xyz[1] / 16.0f - 2048.0f);
        }
        return 3;
    }
    if (c->prog != P_NORMALC && c->prog != P_NORMALL) {
        return -1;
    }
    int n = 0;
    for (int j = 0; j < 3; j++) {
        const float *a = cr->pos[k - 2 + j], *b = cr->pos[k - 2 + (j + 1) % 3];
        float m[3];
        if (a[2] >= 1.0f) {
            memcpy(m, a, sizeof(m));
            poly[n][0] = cx + 16.0f * m[0] / m[2];
            poly[n][1] = half + 16.0f * m[1] / m[2];
            n++;
        }
        if ((a[2] >= 1.0f) != (b[2] >= 1.0f)) {
            const float t = (1.0f - a[2]) / (b[2] - a[2]);
            for (int i = 0; i < 3; i++) {
                m[i] = a[i] + (b[i] - a[i]) * t;
            }
            poly[n][0] = cx + 16.0f * m[0]; /* m[2] = 1 */
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

/* The third target (the triangles the VU drops, each drawn by the port's
 * shader with the case's marks) against the polygons of those the rule
 * draws clipped (clipped[i] for triangle i of the range): a pixel centre
 * more than one pixel inside one of them is drawn, one more than a pixel
 * outside all of them is not (so a triangle the rule drops draws nothing
 * where no clipped one lies). Returns the pixels clearly inside and drawn;
 * *bad counts the mismatches, or is -1 when a triangle cannot be predicted
 * (portPolygon; none of the cases has one, so the caller fails it). */
static int checkPortTriangles(const Case *c, const CaseRef *cr, const uint32_t *idx, int first,
                              int count, const int *clipped, const uint8_t *img, uint32_t pitch,
                              int *bad)
{
    static float polys[MAXV][4][2];
    static int pn[MAXV];
    int np = 0;
    *bad = 0;
    for (int i = 0; i < count; i += 3) {
        if (!clipped[i / 3]) {
            continue;
        }
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
            in += d > 1.0f && drawn;
            *bad += (d > 1.0f && !drawn) || (d < -1.0f && drawn);
        }
    }
    return in;
}

static const Case kCases[] = {
    {"prelit 32", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit 34", "vu_prelit_vs", P_NORMALC, 34, ICO_VU_CLIP_NONE, 3, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit 36", "vu_prelit_vs", P_NORMALC, 36, ICO_VU_CLIP_SCISSOR, 3, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit 36 near", "vu_prelit_vs", P_NORMALC, 36, ICO_VU_CLIP_SCISSOR, 3, 1, 1, 8, 1, 0, 0,
     1.0f, DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit 32 tagless", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 2, 12, 0, 0, 1, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit 36", "vu_lit_vs", P_NORMALL, 36, ICO_VU_CLIP_SCISSOR, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit spec 34", "vu_lit_spec_vs", P_NORMALL, 34, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"reflect 38", "vu_reflect_vs", P_NORMALL, 38, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"skin 20 tagless", "vu_skin_vs", P_CLUSTER, 20, ICO_VU_CLIP_REGION, 5, 1, 2, 12, 0, 0, 1, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"skin 22", "vu_skin_spec_vs", P_CLUSTER, 22, ICO_VU_CLIP_REGION, 5, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"skin 24", "vu_skin_debug_vs", P_CLUSTER, 24, ICO_VU_CLIP_REGION, 5, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"grid 20", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 3, 10, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    /* as prim_makePacketMesh3D lays the buffer out: VIF qword, tag, colour,
     * vertices, MSCNT */
    {"grid 22 raw", "vu_grid_lit_vs", P_MESH, 22, ICO_VU_CLIP_REGION, 3, 3, 3, 10, 0, 1, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"grid 24", "vu_grid_spec_vs", P_MESH, 24, ICO_VU_CLIP_REGION, 3, 2, 2, 10, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"particle", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f, DEPART_NONE,
     VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    /* Enhanced: the triangles past the window (vertex 7 of each batch)
     * reach the picture; their strip has passing triangles, so they are
     * drawn clipped (out of the box, "prelit 32", they are not drawn); the
     * particle outside it lies wholly beside the picture, drawn whole in
     * both presets (nothing of it on the target) */
    {"prelit 32 enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"skin 20 enh", "vu_skin_vs", P_CLUSTER, 20, ICO_VU_CLIP_REGION, 5, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"grid 20 enh", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 2, 10, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"particle enh", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f, DEPART_NONE,
     VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* a frame-wide target at 32:9 (frame[0] 0.375): the region test is the
     * program's own, so the vertices at X 4095 and 8000 fail at every
     * aspect; their triangles cross the camera's picture. Vertices 7 and 9
     * leave no passing triangle in the strip 5..11: not drawn in either
     * preset; the grid's batch (one strip) has passing triangles 2..6:
     * drawn clipped */
    {"prelit 32 wide", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit wide enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_NONE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"lit 32 wide enh", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0,
     0.375f, DEPART_NONE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"grid 20 wide enh", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 2, 10, 0, 0, 0, 0.375f,
     DEPART_NONE, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    /* a vertex behind the eye: what of its triangles is in front of the
     * near plane reaches the picture, not drawn in any state */
    {"prelit 32 eye", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_EYE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit 32 eye", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_EYE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit eye enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_EYE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"lit eye wide enh", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0,
     0.375f, DEPART_EYE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* a vertex behind the blended camera's eye, in front of this
     * tick's (every triangle inside the window there): drawn, GPU-clipped,
     * in either preset; behind the eye at both ticks: not drawn */
    {"prelit eye ticks", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_EYE_TICKS, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit eye ticks enh", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0,
     1.0f, DEPART_EYE_TICKS, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"prelit eye both", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_EYE_TICKS_BOTH, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"lit eye both", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_EYE_TICKS_BOTH, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    /* wholly beside the picture at the tick before, behind the eye at
     * this tick: off the frame at 4:3 (never seen), not drawn between the
     * ticks; on the band beside the picture of a wider frame, drawn */
    {"prelit eye beside", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_EYE_TICKS_BESIDE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit eye beside enh", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0,
     1.0f, DEPART_EYE_TICKS_BESIDE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"prelit eye band enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_EYE_TICKS_BAND, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* a card the tick before showed inside the picture: the card test at
     * the blended camera drops it */
    {"prelit card ticks enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0,
     0, 1.0f, DEPART_CARD_TICKS, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* photo mode's free camera: drawn, GPU-clipped, in either preset */
    {"prelit 32 free", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_EYE_FREE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit 32 free", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_EYE_FREE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"lit wide free", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 0.375f,
     DEPART_EYE_FREE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* past the window below the picture; a strip across the picture with
     * one vertex past the window far left (a wall): they reach
     * the picture, not drawn out of the box (Original at 4:3), drawn
     * clipped in every other state (Enhanced, or Original on a wide
     * frame), their strips having passing triangles; a strip none of
     * whose triangles pass (the opening's tree card): not drawn
     * in any state, at 4:3 and wide */
    {"prelit band enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_BAND_IN, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"prelit card 43", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_CARD, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit card", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 0.375f,
     DEPART_CARD, VU1REF_ORIGINAL, MARK_NONE, 0, 1},
    {"prelit card enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_CARD, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"prelit card 43 enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_CARD, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"prelit cardstrip enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0,
     0, 1.0f, DEPART_CARD_STRIP, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"prelit cardstrip wide", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0,
     0, 0.375f, DEPART_CARD_STRIP, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    /* the same strip joined to another of its mesh (ICO_VU_INDEX_JOINED, a
     * plank's face): not a card, drawn clipped outside the out-of-the-box
     * state; out of the box not drawn */
    {"prelit joined enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_CARD_STRIP, VU1REF_ENHANCED, MARK_JOINED, 0, 1},
    {"lit joined wide", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0,
     0.375f, DEPART_CARD_STRIP, VU1REF_ORIGINAL, MARK_JOINED, 0, 1},
    {"skin joined enh", "vu_skin_vs", P_CLUSTER, 20, ICO_VU_CLIP_REGION, 5, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_CARD_STRIP, VU1REF_ENHANCED, MARK_JOINED, 0, 1},
    {"prelit joined 43", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_CARD_STRIP, VU1REF_ORIGINAL, MARK_JOINED, 0, 0},
    /* a grid's last batch past the window: joined to the batch before it
     * (its rows), drawn clipped outside the out-of-the-box state, not out
     * of the box; a grid of that one batch is a card, never drawn */
    {"grid row enh", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 2, 10, 0, 0, 0, 1.0f,
     DEPART_GRID_ROW, VU1REF_ENHANCED, MARK_NONE, 0, 1},
    {"grid row wide", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 2, 10, 0, 0, 0, 0.375f,
     DEPART_GRID_ROW, VU1REF_ORIGINAL, MARK_NONE, 0, 1},
    {"grid row 43", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 2, 10, 0, 0, 0, 1.0f,
     DEPART_GRID_ROW, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"grid card enh", "vu_grid_vs", P_MESH, 20, ICO_VU_CLIP_REGION, 2, 2, 1, 10, 0, 0, 0, 1.0f,
     DEPART_GRID_ROW, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* a strip wholly right of the camera's picture with a vertex past the
     * window: its failing triangles are drawn clipped beside the picture
     * in both presets (walls and the sea beside a wide picture) */
    {"prelit beside", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_BESIDE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"prelit beside enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_BESIDE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"lit beside enh", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 0.375f,
     DEPART_BESIDE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* a particle past the window: across the camera's picture out of the
     * box not drawn, in any other state drawn whole; wholly
     * beside it both draw its whole quad, which the GPU clips; the free
     * camera draws it */
    {"particle past", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f,
     DEPART_SPRITE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"particle past enh", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 0.375f,
     DEPART_SPRITE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    /* between two ticks, the tick before's VU drew it: drawn whole */
    {"particle past ticks", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f,
     DEPART_SPRITE_TICKS, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"particle free", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 1.0f,
     DEPART_SPRITE_FREE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    {"particle beside enh", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 0.375f,
     DEPART_SPRITE_BESIDE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"particle beside", "vu_particle_vs", P_PARTICLE, 18, 0, 2, 6, 1, 12, 0, 0, 0, 0.375f,
     DEPART_SPRITE_BESIDE, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    /* the content table's mark: HIDE drawn nowhere, the free camera too */
    {"prelit hide", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ENHANCED, MARK_HIDE, 0, 0},
    {"lit hide free", "vu_lit_vs", P_NORMALL, 32, ICO_VU_CLIP_REGION, 4, 1, 1, 12, 0, 0, 0, 1.0f,
     DEPART_EYE_FREE, VU1REF_ENHANCED, MARK_HIDE, 0, 0},
    /* a vertex far above the picture: on a target beside the picture its
     * triangles cover pixels while within the picture's columns they lie
     * wholly above it: drawn clipped (Enhanced wide); on the target inside
     * the picture they reach it, not drawn (Original, 4:3: the PS2's) */
    {"prelit above enh", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_ABOVE, VU1REF_ENHANCED, MARK_NONE, 0, 0},
    {"prelit above 43", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     1.0f, DEPART_ABOVE_IN, VU1REF_ORIGINAL, MARK_NONE, 0, 0},
    /* a reflection camera's picture, 2048 -+ 116 (puddle.c): a strip right
     * of it, inside the frame camera's picture, with a vertex past the
     * window, drawn clipped; the target around 2048 inside it, the
     * triangles past the window reach it, not drawn */
    {"prelit refl beside", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0,
     0.375f, DEPART_REFL_BESIDE, VU1REF_ENHANCED, MARK_NONE, 116, 0},
    {"prelit refl", "vu_prelit_vs", P_NORMALC, 32, ICO_VU_CLIP_REGION, 3, 1, 2, 12, 0, 0, 0, 1.0f,
     DEPART_NONE, VU1REF_ORIGINAL, MARK_NONE, 116, 0},
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
    f->frame[0] = 1.0f; /* the region scale of a target that is not frame-wide */
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
        &(RhiTextureDesc){ICO_VU_PROBE_FIELDS, MAXV, 1, RHI_FMT_RGBA8_UINT, RTU, "probe", 0});
    RhiTexture imgVu =
        rhi_create_texture(&(RhiTextureDesc){RT, RT, 1, RHI_FMT_RGBA8_UNORM, RTU, "vu", 0});
    RhiTexture imgRef =
        rhi_create_texture(&(RhiTextureDesc){RT, RT, 1, RHI_FMT_RGBA8_UNORM, RTU, "ref", 0});
    RhiTexture imgPort =
        rhi_create_texture(&(RhiTextureDesc){RT, RT, 1, RHI_FMT_RGBA8_UNORM, RTU, "port", 0});
    RhiTexture dummy = rhi_create_texture(&(RhiTextureDesc){
        2, 2, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "dummy", 0});
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
    int portIn = 0;      /* pixels clearly inside the triangles the VU drops, drawn clipped */
    int portDropped = 0; /* the triangles the VU drops that the port drops too (Original) */
    RhiState stDummy = RHI_STATE_UNDEFINED;

    static float in[1024][4];
    static CaseRef cr;
    for (int ci = 0; ci < NCASES; ci++) {
        const Case *c = &kCases[ci];
        rngState = 777u + (uint32_t)ci * 31u;
        memset(in, 0, sizeof(in));
        int nq = makeInput(c, in);
        const float wide = c->wide;
        /* a tick case: the reference at each tick's camera, the input's
         * positions moved by tickMove (makeVuCB's T), before the blended
         * camera's (a particle's verdict is made in runRef) */
        static CaseRef crTick[2];
        static float inTick[1024][4];
        s_tickRef[0] = s_tickRef[1] = NULL;
        for (int t = 0; t < 2 && tickCase(c); t++) {
            memcpy(inTick, in, sizeof(inTick));
            for (int k = 0; c->prog == P_PARTICLE && k < c->vpb; k++) {
                float d[3];
                tickMove(c, t, d);
                for (int i = 0; i < 3; i++) {
                    inTick[6 + 2 * k][i] += d[i];
                }
            }
            for (int b = 0, at = 0; c->prog != P_PARTICLE && b < c->nbatch; b++) {
                float d[3];
                tickMove(c, t, d);
                for (int k = 0; k < c->vpb; k++) {
                    for (int i = 0; i < 3; i++) {
                        inTick[at + c->hdr + k * c->vq][i] += d[i];
                    }
                }
                at += c->hdr + c->vpb * c->vq + c->trail;
            }
            runRef(c, (const float (*)[4])inTick, nq, &crTick[t]);
            s_tickRef[t] = &crTick[t];
        }
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
        /* indices: the static kicks, kick * 4 + corner, with the case's
         * marks; first those the port draws exactly (nkept: the VU's,
         * less what the marks hide), then the others (drawn on their own,
         * into imgPort): clippedOf says which of them the rule draws
         * clipped, nclip and nnone count the two kinds */
        uint32_t *idx = (uint32_t *)(map + OFF_INDEX);
        int nidx = 0, nkept = 0, nclip = 0, nnone = 0;
        static int clippedOf[MAXV * 3];
        int nvert = c->prog == P_PARTICLE ? c->vpb : cr.nv;
        if (c->prog != P_PARTICLE) {
            int kicks[MAXV];
            int nk = vu1ref_static_kicks(c->prog == P_MESH ? NULL : cr.stw, cr.first, cr.nv, kicks);
            for (int pass = 0; pass < 2; pass++) {
                for (int i = 0; i < nk; i++) {
                    const int d = portDraws(c, &cr, cr.v, kicks[i]);
                    if ((d == VU1REF_DRAW_EXACT) != !pass) {
                        continue;
                    }
                    if (pass) {
                        clippedOf[(nidx - nkept) / 3] = d == VU1REF_DRAW_CLIPPED;
                        nclip += d == VU1REF_DRAW_CLIPPED;
                        nnone += d == VU1REF_DRAW_NONE;
                    }
                    for (int j = 0; j < 3; j++) {
                        idx[nidx++] = ICO_VU_INDEX(kicks[i], j) | markBit(c);
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
        checkCards(c, &cr, (const float (*)[4])in, &vcb, &bones);
        cardBits(c, &cr, &vcb, &bones);
        vcbProbe = vcb;
        vcbProbe.draw[2] |= ICO_VU_PROBE;
        IcoFrameCB fProbe, fDraw;
        frameCB(&fProbe, ICO_VU_PROBE_FIELDS, MAXV, 0, 0);
        frameCB(&fDraw, RT, RT, targetLeft(c), 2048.0f - RT / 2);
        /* the frame register as rd sets it on a frame-wide target (no VU
         * shader reads it) and the preset (g_misc.y) */
        fProbe.frame[0] = fDraw.frame[0] = wide;
        /* the camera's picture (rd_replay.c rd__frame_group_ex) */
        fProbe.frame[1] = fDraw.frame[1] = pictureHalfW(c);
        fProbe.frame[2] = fDraw.frame[2] = pictureHalfH(c);
        fProbe.misc[1] = fDraw.misc[1] = (float)c->preset;
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
        rp.color[0] =
            (RhiColorAttachment){probe, RHI_LOAD_CLEAR, {0, 0, 0, 0}, RHI_STORE_STORE, false};
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
                                               RHI_STORE_STORE,
                                               false};
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
        /* the triangles the region test drops, each drawn by the port's
         * shader: the rule's clipped ones where the CPU puts them (cut at
         * the near plane, a vertex behind the eye or not), the dropped and
         * hidden ones nowhere */
        if (nidx > nkept) {
            int pbad = 0;
            const int pin =
                checkPortTriangles(c, &cr, idx, nkept, nidx - nkept, clippedOf, pa, pitch, &pbad);
            int pcov = 0;
            for (int i = 0; i < RT * RT; i++) {
                pcov += pa[i * 4] != 16 || pa[i * 4 + 1] != 32 || pa[i * 4 + 2] != 48;
            }
            printf("  %-18s %2d triangles the port does not draw exactly: %2d clipped, %2d "
                   "dropped; %4d pixels drawn, %4d clearly inside the clipped ones, %d "
                   "mismatches\n",
                   c->name, (nidx - nkept) / 3, nclip, nnone, pcov, pin, pbad);
            if (pbad != 0) {
                FAILF("%s: the triangles drawn clipped: %d pixels against the polygons\n", c->name,
                      pbad);
            }
            if (nclip == 0 && pcov != 0) {
                FAILF("%s: %d pixels drawn of triangles the port drops\n", c->name, pcov);
            }
            /* the rule per case: HIDE hides; the free camera clips;
             * DEPART_BESIDE's, DEPART_ABOVE's and DEPART_REFL_BESIDE's
             * failing triangles lie wholly beside the camera's picture
             * (DEPART_ABOVE's once cut to its rows, DEPART_REFL_BESIDE's
             * the reflection camera's) and are clipped; a Case.strip
             * case's reach it and are clipped by the strip rule; every
             * other case's reach it (the target lies inside it) and are
             * not drawn, but for any wholly beside it, off the target */
            const int hidden = c->mark == MARK_HIDE;
            const int clips =
                !hidden &&
                (freeCamera(c) || c->depart == DEPART_BESIDE || c->depart == DEPART_ABOVE ||
                 c->depart == DEPART_EYE_TICKS || c->depart == DEPART_EYE_TICKS_BAND ||
                 c->depart == DEPART_REFL_BESIDE || c->strip);
            if (c->clip == ICO_VU_CLIP_REGION && (hidden  ? nclip != 0
                                                  : clips ? nnone != 0
                                                          : nnone == 0)) {
                FAILF("%s: %d clipped, %d dropped: expected %s\n", c->name, nclip, nnone,
                      hidden  ? "all dropped"
                      : clips ? "all clipped"
                              : "some dropped");
            }
            if (clips && pin == 0) {
                FAILF("%s: the clipped triangles draw no pixel\n", c->name);
            }
            /* inside the camera's picture the PS2's picture out of the box,
             * and in any other state for a card or a vertex behind the
             * eye: nothing beside the triangles the VU kicks */
            if (!clips && pcov != 0) {
                FAILF("%s: %d pixels drawn in the picture of triangles the VU drops\n", c->name,
                      pcov);
            }
            const int eyeCase =
                c->depart == DEPART_EYE || c->depart == DEPART_EYE_FREE || eyeTickCase(c);
            if (eyeCase) {
                int eye = 0;
                for (int i = nkept; i < nidx; i += 3) {
                    eye += behindEye(&cr, (int)((idx[i] & ICO_VU_INDEX_MASK) >> 2));
                }
                if (eye == 0) {
                    FAILF("%s: no dropped triangle has a vertex behind the eye\n", c->name);
                }
            }
            portIn += pin;
            portDropped += c->preset == VU1REF_ORIGINAL && c->mark == MARK_NONE ? nnone : 0;
        } else if (c->prog != P_PARTICLE && c->clip == ICO_VU_CLIP_REGION &&
                   (c->depart != DEPART_NONE || c->wide != 1.0f || c->mark != MARK_NONE)) {
            /* a particle has no index list (its clip field is 0, which reads
             * as REGION): the sprites the VU skips are checked below */
            FAILF("%s: no triangle the port draws other than exactly\n", c->name);
        }
        /* the mark: HIDE draws nothing at all */
        if (c->mark == MARK_HIDE && (nkept != 0 || cr.ntri != 0)) {
            FAILF("%s: %d indices drawn exactly, %d reference triangles: expected none\n", c->name,
                  nkept, cr.ntri);
        }
        if (c->prog == P_PARTICLE && c->depart != DEPART_NONE && !cr.shown[0]) {
            FAILF("%s: particle 0 is not one the port can draw\n", c->name);
        }
        if (c->depart == DEPART_SPRITE_FREE && cr.shown[1]) {
            FAILF("%s: particle 1 behind the eye is drawn\n", c->name);
        }
        if ((c->depart == DEPART_SPRITE || c->depart == DEPART_SPRITE_FREE ||
             c->depart == DEPART_SPRITE_TICKS || c->depart == DEPART_SPRITE_BESIDE) &&
            ((cr.po.count > 0 && cr.po.s[0].index == 0) || !cr.shown[0])) {
            FAILF("%s: particle 0 is not one the VU skips\n", c->name);
        }
        /* the particles the VU skips: out of the box not drawn when they
         * reach the camera's picture (DEPART_SPRITE's), drawn whole when
         * wholly beside it (the other cases' particle 0 at X 3248), under
         * the free camera and in any other state (in the reference image
         * too) */
        if (c->prog == P_PARTICLE) {
            const int adds = freeCamera(c) || extendedState(c) || c->depart != DEPART_SPRITE ||
                             c->depart == DEPART_SPRITE_TICKS;
            if (adds ? (cr.added == 0 || cr.dropped != 0) : (cr.added != 0 || cr.dropped == 0)) {
                FAILF("%s: %d particles the VU skips drawn whole, %d dropped\n", c->name, cr.added,
                      cr.dropped);
            }
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
    if (portIn == 0 || portDropped == 0) {
        FAILF("triangles past the region window: %d pixels drawn clipped, %d triangles dropped "
              "in the Original preset\n",
              portIn, portDropped);
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
    traceStrip();
    traceRegion();
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
