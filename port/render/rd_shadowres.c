/* rd_shadowres.c: Options > Effects > Shadow detail: High (RdSettings.shadowHigh,
 * [video] effect_shadow_high; rd_shadowres.h; docs/port/RENDER_POST.md
 * "Shadow detail: High"; docs/port/DIVERGENCES.md VD-34).
 *
 * The game's shadow (Shadow.c shadow_Draw) is a count of the volumes, three
 * blur levels drawn one from the other by a 2:1 bilinear sprite (SHADOW0..2:
 * W/2 x 256, W/4 x 128, W/8 x 64 GS pixels) and three composites that
 * darken SCENE by each level magnified back with one bilinear tap.  The
 * levels keep the PS2's sizes at every scale, so above 1x a level texel is
 * 6, 12 and 24 SCENE texels at 3x: where an edge falls against the levels'
 * grid changes the penumbra (its 10-90 width 11.3 to 24.3 SCENE texels over
 * the grid's phase, its middle wandering 3.8 texels: the staircase along a
 * slanted edge).
 *
 * The count at the GS size (rd_replay.c shadowRedMake, shadowReduce,
 * shadowCountOne; moved here from rd_replay.c).  The first level reads the
 * count with a 2:1 bilinear sprite, which averages a 2x2 block of the
 * PS2's 512x512 count exactly; on a count at s times the resolution the
 * same sprite takes 2x2 taps of a 2s x 2s footprint, and the shadow's
 * integral varies with the volume's sub-pixel position (2.6 % at 4x against
 * 2.0 %).  With the count target scaled, doShadowResolve box-averages it
 * down to its GS size (box_reduce_ps, or the one draw's box) into the
 * reduced count and resolveTexture hands that to whatever samples the
 * count as a texture, so the level reads what the PS2's did.  Unscaled
 * targets (Original) never come there.  None of that changes under High.
 *
 * High.  Where the count target is scaled (above 1 on an axis):
 *   1. the count is also kept at its full size: the seven draws and the
 *      MSAA resolve leave it there already; the one draw (rd_shadow_one.c)
 *      draws it a second time into the count target at its own texels (the
 *      box of one texel: the stencil's table texel itself), one pass more;
 *   2. at each of the game's three level sprites (rd_shadow_box.c
 *      rd__shadow_box_screen, before the sprite's own pass), the level is
 *      drawn again into a texture of its own at S texels a GS pixel on each
 *      axis, S the smallest odd whole number at or above the scene's scale
 *      there (rd_sr_level_scale: 3 at 2x, 2.41x and 3x, 5 at 4x and at
 *      2160 rows; an axis at scale 1 keeps S = 1) (shadowres_step_ps):
 *      texel m stands for the GS position m / S, its taps the GS's two
 *      (0.75 / 0.25, one PS2 texel apart), the first level's each the box
 *      of count texels its GS pixel covers (partial texels by their share
 *      at a count scale that is not S), the later levels' the level before
 *      at 2 m and 2 m + S, whole texels; the sprite's colour MODULATEs it as
 *      on the GS;
 *   3. after the third, each level is blurred in two passes (across into a
 *      temporary of its size, down back into it: shadowres_tent_ps) by the
 *      tent its bilinear magnification supplied: weights S - |t - c|,
 *      c = S / 2 - 1/2 (whole at an odd S), over t = n - m.  The composite
 *      reads the level where the PS2 read the GS texel at u, fine texel
 *      S u - 1/2; with the tent there its one bilinear tap gives exactly the
 *      mean of today's picture over the level grid's S x S placements: the
 *      same width, no staircase.  (At an even S the tent's centre would be
 *      half a texel off and the tap would soften it further: 10 % wider at
 *      2x in the edge model; hence the odd S);
 *   4. the composites are the game's, drawn as before, reading the fine
 *      level (resolveTexture, rd__shadow_fine) instead of the named one.
 * The game's own level sprites are drawn too, as before (the named targets
 * keep Original's levels: a chain High could not take, a refusal, a draw
 * into a level that is not the chain's, all read them as Original).  All of
 * it sits between the count and the composites, where SCENE's pass is
 * ended already (the DATE snapshot's and the levels' own passes): no SCENE
 * pass more on a tiler.
 *
 * At 1x (the count target's scale 1 on both axes) nothing here runs: no
 * texture, no pipeline, no pass; the same bytes as Original by
 * construction (the tests' rd_shadow_high_ab at 1x).  With the option at
 * Original nothing here runs either, and nothing is made.
 *
 * Interactions: the volumes' box and the empty composites (rd_shadow_box.c)
 * are the named levels' and are kept (a fine level's reach is the named
 * level's plus one level texel of the step (a fine texel between two named
 * ones reads both their footprints), one of the tent and 1 / S of one of the
 * composite's tap: inside the composites' RD_SB_TEX_MARGIN of 3, where the
 * named level needs one); the one DATE set-up holds
 * (every fine texel is a mean of values the named levels hold, MODULATEd
 * alike); the shadow tiles (rd_shadow_tile.c) step aside for a composite
 * that reads a fine level (their mask is the named level's); [dev]
 * shadow_bspline does nothing to a fine level (rd_fx_plan.c); a window
 * without volumes (rd_no_volume.c) skips the levels, so no chain is made.
 *
 * The lines (rd__log):
 *   shadow detail: high (above 1x the shadow's three levels at the scene's
 *     scale, the smallest odd whole one at or above it: the count at its
 *     full size, the game's step at every texel, the tent its magnification
 *     supplied; at 1x the game's own)
 *   shadow detail: high: SHADOW0 864x768, SHADOW1 432x384, SHADOW2 216x192
 *     at 3x3 from the count's 3x3 texels a GS pixel, 10 passes a shadow,
 *     7.1 MB (reported once a size)
 *   shadow detail: high: a shadow drawn as Original: <why> (reported once a
 *     reason)
 *   the 10 s clause "; shadow detail high: 1.0 shadows a replay at the
 *     scene's scale (10.0 passes), 0.0 as Original" and --stats' "shadow
 *     detail high: 1 shadows at the scene's scale, 10 passes, 0 as
 *     Original". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_clause.h"
#include "rd_internal.h"
#include "rd_replay.h"
#include "rd_shadowres.h"

/* ------------------------------------------------------------ arithmetic */

float rd_sr_count_scale(uint32_t w, uint32_t tw, float sx, float ox)
{
    if (w == 0u) {
        return 1.0f;
    }
    return ox > 0.0f ? sx : (float)tw / (float)w;
}

uint32_t rd_sr_level_scale(float s)
{
    if (!(s > 1.0f)) {
        return 1u;
    }
    /* the float's noise above a whole scale (3.0000002) is not a step up */
    const uint32_t n = (uint32_t)ceilf(s - 1.0e-3f);
    return (n & 1u) ? n : n + 1u;
}

float rd_sr_box_w(float i, float a, float s)
{
    const float b = a + s, c = b + s;
    const float w0 = fmaxf(fminf(b, i + 1.0f) - fmaxf(a, i), 0.0f);
    const float w1 = fmaxf(fminf(c, i + 1.0f) - fmaxf(b, i), 0.0f);
    return (0.75f * w0 + 0.25f * w1) / s;
}

float rd_sr_tent_w(int t, float s)
{
    const float c = 0.5f * s - 0.5f;
    return fmaxf(s - fabsf((float)t - c), 0.0f);
}

void rd_sr_tent_range(float s, int *t0, int *t1)
{
    const float c = 0.5f * s - 0.5f;
    *t0 = (int)floorf(c - s);
    const int e = (int)ceilf(c + s);
    *t1 = e < *t0 + RD_SR_SPAN_MAX ? e : *t0 + RD_SR_SPAN_MAX;
}

static float texelOf(const uint8_t *src, uint32_t sw, int x, int y, int ch)
{
    return (float)src[((size_t)y * sw + (size_t)x) * 4u + (size_t)ch] / 255.0f;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* the level before at (jx, jy), linear, the indices held (sr_point) */
static void pointAt(const uint8_t *src, uint32_t sw, uint32_t sh, float jx, float jy, float w,
                    float acc[4])
{
    const float fx0 = floorf(jx), fy0 = floorf(jy);
    const float fx = jx - fx0, fy = jy - fy0;
    const int ax = clampi((int)fx0, 0, (int)sw - 1), ay = clampi((int)fy0, 0, (int)sh - 1);
    const int bx = clampi((int)fx0 + 1, 0, (int)sw - 1), by = clampi((int)fy0 + 1, 0, (int)sh - 1);
    for (int ch = 0; ch < 4; ch++) {
        const float top =
            texelOf(src, sw, ax, ay, ch) * (1.0f - fx) + texelOf(src, sw, bx, ay, ch) * fx;
        const float bot =
            texelOf(src, sw, ax, by, ch) * (1.0f - fx) + texelOf(src, sw, bx, by, ch) * fx;
        acc[ch] += (top * (1.0f - fy) + bot * fy) * w;
    }
}

void rd_sr_step_texel(const uint8_t *src, uint32_t sw, uint32_t sh, int first, float kx, float ky,
                      float ax, float ay, float sx, float sy, const uint8_t col[4], uint32_t mx,
                      uint32_t my, uint8_t out[4])
{
    const float a[2] = {(float)mx * kx + ax, (float)my * ky + ay};
    float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (first) {
        const int fx = (int)floorf(a[0]) > 0 ? (int)floorf(a[0]) : 0;
        const int fy = (int)floorf(a[1]) > 0 ? (int)floorf(a[1]) : 0;
        int lx = (int)ceilf(a[0] + 2.0f * sx), ly = (int)ceilf(a[1] + 2.0f * sy);
        lx = lx < (int)sw ? lx : (int)sw;
        ly = ly < (int)sh ? ly : (int)sh;
        lx = lx < fx + RD_SR_SPAN_MAX ? lx : fx + RD_SR_SPAN_MAX;
        ly = ly < fy + RD_SR_SPAN_MAX ? ly : fy + RD_SR_SPAN_MAX;
        for (int y = fy; y < ly; y++) {
            const float wy = rd_sr_box_w((float)y, a[1], sy);
            for (int x = fx; x < lx; x++) {
                const float w = rd_sr_box_w((float)x, a[0], sx) * wy;
                for (int ch = 0; ch < 4; ch++) {
                    sum[ch] += texelOf(src, sw, x, y, ch) * w;
                }
            }
        }
    } else {
        pointAt(src, sw, sh, a[0], a[1], 0.5625f, sum);
        pointAt(src, sw, sh, a[0] + sx, a[1], 0.1875f, sum);
        pointAt(src, sw, sh, a[0], a[1] + sy, 0.1875f, sum);
        pointAt(src, sw, sh, a[0] + sx, a[1] + sy, 0.0625f, sum);
    }
    for (int ch = 0; ch < 4; ch++) {
        const uint32_t t = (uint32_t)floorf(sum[ch] * 255.0f + 0.5f);
        const uint32_t o = (t * col[ch]) >> 7;
        out[ch] = (uint8_t)(o > 255u ? 255u : o);
    }
}

void rd_sr_tent_texel(const uint8_t *src, uint32_t w, uint32_t h, int down, float s, uint32_t x,
                      uint32_t y, uint8_t out[4])
{
    int t0, t1;
    rd_sr_tent_range(s, &t0, &t1);
    const int n = down ? (int)y : (int)x;
    const int lim = (down ? (int)h : (int)w) - 1;
    float sum[4] = {0.0f, 0.0f, 0.0f, 0.0f}, wsum = 0.0f;
    for (int t = t0; t <= t1; t++) {
        const float wt = rd_sr_tent_w(t, s);
        const int k = clampi(n - t, 0, lim);
        const uint8_t *p =
            src + ((size_t)(down ? k : (int)y) * w + (size_t)(down ? (int)x : k)) * 4u;
        for (int ch = 0; ch < 4; ch++) {
            sum[ch] += (float)p[ch] * wt;
        }
        wsum += wt;
    }
    for (int ch = 0; ch < 4; ch++) {
        out[ch] = (uint8_t)floorf(sum[ch] / (wsum > 1.0e-6f ? wsum : 1.0e-6f) + 0.5f);
    }
}

/* ------------------------------------------------------------ the replay */

enum {
    WHY_MAP = 0,  /* the sprite is not the game's 2:1 map */
    WHY_FORM,     /* its state is not the level's (blend, test, filter, colour) */
    WHY_COUNT,    /* no full-size count for this chain */
    WHY_PIPES,    /* the pipelines could not be made */
    WHY_TEXTURES, /* the textures could not be made */
    WHY_SCALE,    /* a scale above RD_SR_SCALE_MAX */
    WHY_N
};

static const char *const kWhy[WHY_N] = {
    "the sprite is not the game's 2:1 map of the level before",
    "its state is not a level's (blend, test, filter, colour, texture)",
    "the count was not kept at its full size",
    "the shaders or pipelines could not be made",
    "the textures could not be made",
    "the scene's scale is above 31 on an axis"};

static struct {
    struct Replay *r; /* the screen command's replay (rd__no_volume_skip) */
    uint32_t chain;   /* the counts begun (rd__shadow_high_reset) */
    uint32_t fullFor; /* the count target holding its full size this chain */
    uint32_t fullChain;
    uint32_t made[RD_SR_LEVELS]; /* chain + 1 of the fine level drawn, 0 none */
    bool ready;                  /* the three drawn and blurred, this chain */
    uint32_t readyChain, readyReplay;
    /* the fine levels and their temporaries (across), with their states */
    RhiTexture lvl[RD_SR_LEVELS], tmp[RD_SR_LEVELS];
    RhiState lvlState[RD_SR_LEVELS], tmpState[RD_SR_LEVELS];
    uint32_t w[RD_SR_LEVELS], h[RD_SR_LEVELS];
    /* this chain's scales: the count's (cx, cy, its left-out cox) and each
     * level's texels per GS pixel */
    float cx, cy, cox, sx[RD_SR_LEVELS], sy[RD_SR_LEVELS];
    uint32_t chainPasses;
    RhiShader fsStep, fsTent;
    RhiPipeline pipeStep, pipeTent;
    bool refused; /* the pipelines failed: Original until the option is set again */
    uint32_t saidWhy;
    uint32_t saidW, saidH; /* the size line's last SHADOW0 */
} s_sr;

static struct {
    uint64_t chains, passes, original;
    uint32_t replays;
} s_sum;

static void say(int why)
{
    g_rdPerf.shadowHighOriginal++;
    if (s_sr.saidWhy & (1u << why)) {
        return;
    }
    s_sr.saidWhy |= 1u << why;
    rd__log("shadow detail: high: a shadow drawn as Original: %s (reported once a reason)",
            kWhy[why]);
}

void rd__shadow_high_log(const char *when)
{
    if (g_rd.settings.shadowHigh) {
        rd__log("shadow detail: high (above 1x the shadow's three levels at the scene's scale, "
                "the smallest odd whole one at or above it: the count at its full size, the "
                "game's step at every texel, the tent its magnification supplied; at 1x the "
                "game's own)%s",
                when ? when : "");
    } else {
        rd__log("shadow detail: original (the game's levels at their GS sizes)%s",
                when ? when : "");
    }
}

static void destroyAll(void)
{
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        if (s_sr.lvl[k].id) {
            rhi_destroy_texture(s_sr.lvl[k]);
        }
        if (s_sr.tmp[k].id) {
            rhi_destroy_texture(s_sr.tmp[k]);
        }
        s_sr.lvl[k] = s_sr.tmp[k] = (RhiTexture){0};
        s_sr.w[k] = s_sr.h[k] = 0;
        s_sr.made[k] = 0;
    }
    s_sr.ready = false;
}

static double heldMb(void)
{
    double b = 0.0;
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        b += 8.0 * (double)s_sr.w[k] * (double)s_sr.h[k]; /* the level and its temporary */
    }
    return b / (1024.0 * 1024.0);
}

void rd__shadow_high_free(const char *why)
{
    const bool held = s_sr.lvl[0].id != 0;
    if (held && g_rd.hasDevice) {
        rd__log("shadow detail: high: the levels at the scene's scale freed, %.1f MB (%s)",
                heldMb(), why ? why : "asked");
    }
    if (g_rd.hasDevice) {
        destroyAll();
    }
    s_sr.refused = false; /* a new choice: tried again */
    s_sr.saidW = s_sr.saidH = 0;
}

void rd__shadow_high_shutdown(void)
{
    if (g_rd.hasDevice) {
        destroyAll();
        if (s_sr.pipeStep.id) {
            rhi_destroy_pipeline(s_sr.pipeStep);
        }
        if (s_sr.pipeTent.id) {
            rhi_destroy_pipeline(s_sr.pipeTent);
        }
        if (s_sr.fsStep.id) {
            rhi_destroy_shader(s_sr.fsStep);
        }
        if (s_sr.fsTent.id) {
            rhi_destroy_shader(s_sr.fsTent);
        }
    }
    memset(&s_sr, 0, sizeof(s_sr));
}

void rd__shadow_high_replay(struct Replay *r)
{
    s_sr.r = r;
}

void rd__shadow_high_reset(uint32_t color)
{
    (void)color;
    s_sr.chain++;
    s_sr.fullFor = 0;
    s_sr.ready = false;
}

/* the count target's scales on both axes; false when neither is above 1
 * (the option takes no path: 1x) */
static bool countScales(const RdTargetRec *tc, float *cx, float *cy)
{
    *cx = rd_sr_count_scale(tc->w, tc->tw, tc->sx, tc->ox);
    *cy = rd_sr_count_scale(tc->h, tc->th, tc->sy, 0.0f);
    return *cx > 1.0f || *cy > 1.0f;
}

bool rd__shadow_high_count_wanted(const RdTargetRec *tc)
{
    float cx, cy;
    return g_rd.settings.shadowHigh && g_rd.hasDevice && !s_sr.refused && tc &&
           countScales(tc, &cx, &cy) && cx <= RD_SR_SCALE_MAX && cy <= RD_SR_SCALE_MAX;
}

void rd__shadow_high_count_full(uint32_t color)
{
    if (!g_rd.settings.shadowHigh) {
        return;
    }
    s_sr.fullFor = color;
    s_sr.fullChain = s_sr.chain;
}

/* blit_vs and one of the two entries, RGBA8, no blend, every state static */
static RhiPipeline makePipe(RhiShader fs, const char *name)
{
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = g_rd.vs[RD_VS_BLIT];
    d.fragment = fs;
    d.layouts = layouts;
    d.layoutCount = 3;
    d.cullNone = true;
    d.colorCount = 1;
    d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    d.blend[0].srcColor = RHI_BF_ONE;
    d.blend[0].dstColor = RHI_BF_ZERO;
    d.blend[0].colorOp = RHI_BO_ADD;
    d.blend[0].srcAlpha = RHI_BF_ONE;
    d.blend[0].dstAlpha = RHI_BF_ZERO;
    d.blend[0].alphaOp = RHI_BO_ADD;
    d.blend[0].writeMask = 0xF;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.debugName = name;
    rd__pipeline_note_begin(d.debugName);
    const RhiPipeline p = rhi_create_pipeline(&d);
    rd__pipeline_note_end();
    if (p.id) {
        rd__stats_here()->pipelineCreates++;
    }
    return p;
}

static bool pipes(void)
{
    if (s_sr.pipeStep.id && s_sr.pipeTent.id) {
        return true;
    }
    if (s_sr.refused || !g_rd.vs[RD_VS_BLIT].id) {
        return false;
    }
    if (!s_sr.fsStep.id) {
        s_sr.fsStep = rd__make_shader("shadowres_step_ps");
    }
    if (!s_sr.fsTent.id) {
        s_sr.fsTent = rd__make_shader("shadowres_tent_ps");
    }
    if (s_sr.fsStep.id && !s_sr.pipeStep.id) {
        s_sr.pipeStep = makePipe(s_sr.fsStep, "rd shadow detail step");
    }
    if (s_sr.fsTent.id && !s_sr.pipeTent.id) {
        s_sr.pipeTent = makePipe(s_sr.fsTent, "rd shadow detail tent");
    }
    if (!s_sr.pipeStep.id || !s_sr.pipeTent.id) {
        rd__log("shadow detail: high: its shaders or pipelines could not be made (step %s, tent "
                "%s): the shadows are drawn as Original until the option is chosen again",
                s_sr.pipeStep.id ? "made" : "missing", s_sr.pipeTent.id ? "made" : "missing");
        s_sr.refused = true;
        return false;
    }
    return true;
}

/* level k's texture and its temporary at w x h */
static bool texturesFor(int k, uint32_t w, uint32_t h)
{
    if (s_sr.lvl[k].id && s_sr.tmp[k].id && s_sr.w[k] == w && s_sr.h[k] == h) {
        return true;
    }
    static const char *const kNames[RD_SR_LEVELS][2] = {
        {"rd shadow detail level 0", "rd shadow detail level 0 across"},
        {"rd shadow detail level 1", "rd shadow detail level 1 across"},
        {"rd shadow detail level 2", "rd shadow detail level 2 across"}};
    if (s_sr.lvl[k].id) {
        rhi_destroy_texture(s_sr.lvl[k]);
    }
    if (s_sr.tmp[k].id) {
        rhi_destroy_texture(s_sr.tmp[k]);
    }
    const uint32_t use = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC;
    s_sr.lvl[k] =
        rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM, use, kNames[k][0], 0});
    s_sr.tmp[k] =
        rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UNORM, use, kNames[k][1], 0});
    s_sr.lvlState[k] = s_sr.tmpState[k] = RHI_STATE_UNDEFINED;
    s_sr.w[k] = w;
    s_sr.h[k] = h;
    s_sr.made[k] = 0;
    return s_sr.lvl[k].id && s_sr.tmp[k].id;
}

/* one pass of one triangle over dst (w x h) reading src through pipe */
static void drawPass(struct Replay *r, RhiPipeline pipe, RhiTexture dst, RhiState *dstState,
                     RhiTexture src, RhiState *srcState, uint32_t w, uint32_t h, uint32_t name,
                     const IcoDrawCB *cb)
{
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, src, srcState, RHI_STATE_SHADER_READ);
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, name, w, h, r->perfPost);
    rhi_cmd_checkpoint(cl,
                       rd__replay_pass_push(RD_PASSK_PASS, name, w, h, 0, 0, 0, 0, r->perfPost));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel is written */
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    rd__set_pipeline(cl, pipe, NULL); /* makePipe: every state static */
    rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(cb));
    rhi_cmd_set_bind_group(cl, 2,
                           rd__tex_group(src, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                          RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
    rd__replay_draw(cl, 3, 0, 1);
    rd__stats_here()->draws++;
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst, dstState, RHI_STATE_SHADER_READ);
    g_rdPerf.shadowHighPasses++;
    s_sr.chainPasses++;
}

/* the level index of a named level target, -1 for any other */
static int levelOf(uint32_t target)
{
    for (int k = 0; k < RD_SR_LEVELS; k++) {
        if (target == rd_target((RdTargetId)(RD_TARGET_SHADOW0 + k)).id) {
            return k;
        }
    }
    return -1;
}

/* the sprite is the chain's: the GS's u = 2 x + 0.75 (and v) over the whole
 * level, one colour; false with *why otherwise */
static bool chainSprite(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v, uint32_t n,
                        const RdTargetRec *dst, const RdTargetRec *src, int *why)
{
    const RdDrawState *d = &st->ds;
    *why = WHY_FORM;
    if (c->b[0] != RD_PRIM_SPRITES || n != 2 || c->b[2] == 0 || c->b[1] == RD_SPACE_UI ||
        c->b[4] != 0 || c->b[5] != 0 || st->aa1 || d->tcc != RD_TCC_RGBA ||
        d->texFn != RD_TEXFN_MODULATE || d->abe || d->fba || d->colorMask != 0xF ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS) || d->test.date != RD_DATE_OFF ||
        (d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || d->magFilter != RD_FILTER_LINEAR ||
        d->minFilter != RD_FILTER_LINEAR || d->wrap.s != RD_WRAP_CLAMP ||
        d->wrap.t != RD_WRAP_CLAMP || memcmp(v[0].rgba, v[1].rgba, 4) != 0 || dst->tw != dst->w ||
        dst->th != dst->h || src->w == 0 || src->h == 0) {
        return false;
    }
    *why = WHY_MAP;
    const float ox = (float)(2048 - (int32_t)(st->gsW >> 1)),
                oy = (float)(2048 - (int32_t)(st->gsH >> 1));
    const float xa = (float)v[0].x / 16.0f - ox, xb = (float)v[1].x / 16.0f - ox;
    const float ya = (float)v[0].y / 16.0f - oy, yb = (float)v[1].y / 16.0f - oy;
    const float ua = v[0].s / 16.0f + st->uvOffset[0] * (float)src->w;
    const float ub = v[1].s / 16.0f + st->uvOffset[0] * (float)src->w;
    const float va = v[0].t / 16.0f + st->uvOffset[1] * (float)src->h;
    const float vb = v[1].t / 16.0f + st->uvOffset[1] * (float)src->h;
    if (!(xb - xa > 0.0f) || !(yb - ya > 0.0f)) {
        return false;
    }
    const float du = (ub - ua) / (xb - xa), dv = (vb - va) / (yb - ya);
    /* u at the level's pixel 0 (the GS samples pixel x at x) */
    const float u0 = ua - du * xa, v0 = va - dv * ya;
    if (fabsf(du - 2.0f) > 1.0e-4f || fabsf(dv - 2.0f) > 1.0e-4f || fabsf(u0 - 0.75f) > 1.0e-4f ||
        fabsf(v0 - 0.75f) > 1.0e-4f || xa > 0.0f || ya > 0.0f || xb < (float)dst->w ||
        yb < (float)dst->h) {
        return false;
    }
    return true;
}

/* the size line, once a SHADOW0 size */
static void sizeLine(void)
{
    if (s_sr.saidW == s_sr.w[0] && s_sr.saidH == s_sr.h[0]) {
        return;
    }
    s_sr.saidW = s_sr.w[0];
    s_sr.saidH = s_sr.h[0];
    rd__log("shadow detail: high: SHADOW0 %ux%u, SHADOW1 %ux%u, SHADOW2 %ux%u at %.4gx%.4g from "
            "the count's %.4gx%.4g texels a GS pixel, %u passes a shadow, %.1f MB (reported "
            "once a size)",
            s_sr.w[0], s_sr.h[0], s_sr.w[1], s_sr.h[1], s_sr.w[2], s_sr.h[2], (double)s_sr.sx[0],
            (double)s_sr.sy[0], (double)s_sr.cx, (double)s_sr.cy, s_sr.chainPasses, heldMb());
}

/* the tent across (level -> temporary) and down (temporary -> level) */
static void tent(struct Replay *r, int k, uint32_t name)
{
    for (int down = 0; down < 2; down++) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.mode[0] = down ? 1u : 0u;
        cb.param[0] = down ? s_sr.sy[k] : s_sr.sx[k];
        cb.tex[0] = (float)s_sr.w[k];
        cb.tex[1] = (float)s_sr.h[k];
        cb.tex[2] = 1.0f / (float)s_sr.w[k];
        cb.tex[3] = 1.0f / (float)s_sr.h[k];
        if (down) {
            drawPass(r, s_sr.pipeTent, s_sr.lvl[k], &s_sr.lvlState[k], s_sr.tmp[k],
                     &s_sr.tmpState[k], s_sr.w[k], s_sr.h[k], name, &cb);
        } else {
            drawPass(r, s_sr.pipeTent, s_sr.tmp[k], &s_sr.tmpState[k], s_sr.lvl[k],
                     &s_sr.lvlState[k], s_sr.w[k], s_sr.h[k], name, &cb);
        }
    }
}

void rd__shadow_high_screen(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v,
                            uint32_t n)
{
    struct Replay *r = s_sr.r;
    if (!g_rd.settings.shadowHigh || !g_rd.hasDevice || !r || &r->st != st) {
        return;
    }
    const int k = levelOf(st->color);
    if (k < 0) {
        return;
    }
    /* a draw into a level: the fine levels stand for the named ones no
     * more until this chain's step k is drawn again */
    s_sr.ready = false;
    const RdTexRec *t = st->ds.texEnabled ? rd__tex_rec(st->tex) : NULL;
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view != RD_VIEW_RGBA ||
        t->src != RD_TEXSRC_RGBA32) {
        return; /* not a level sprite: the named level as recorded */
    }
    RdTargetRec *src = rd__target_rec(t->target);
    RdTargetRec *dst = rd__target_rec(st->color);
    if (!src || !dst) {
        return;
    }
    if (k == 0) {
        float cx, cy;
        if (!countScales(src, &cx, &cy)) {
            return; /* 1x: the game's own, no path of its own */
        }
        if (cx > RD_SR_SCALE_MAX || cy > RD_SR_SCALE_MAX) {
            say(WHY_SCALE);
            return;
        }
        s_sr.cx = cx;
        s_sr.cy = cy;
        s_sr.cox = src->ox > 0.0f ? src->ox : 0.0f;
    } else if (levelOf(t->target) != k - 1 || s_sr.made[k - 1] != s_sr.chain + 1u) {
        /* not the chain's next step, or the step before was not taken (1x,
         * or its refusal already counted): the named level as recorded */
        return;
    }
    int why = WHY_FORM;
    if (!chainSprite(st, c, v, n, dst, src, &why)) {
        say(why);
        return;
    }
    if (k == 0 && (s_sr.fullFor != t->target || s_sr.fullChain != s_sr.chain)) {
        say(WHY_COUNT);
        return;
    }
    if (!pipes()) {
        say(WHY_PIPES);
        return;
    }
    const uint32_t w = dst->w * rd_sr_level_scale(s_sr.cx), h = dst->h * rd_sr_level_scale(s_sr.cy);
    rd__replay_end_pass(r); /* the open pass (the level before's) */
    if (!texturesFor(k, w, h)) {
        say(WHY_TEXTURES);
        destroyAll();
        return;
    }
    s_sr.sx[k] = (float)w / (float)dst->w;
    s_sr.sy[k] = (float)h / (float)dst->h;
    if (k == 0) {
        s_sr.chainPasses = 0;
    }
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    float ssx, ssy, ox;
    uint32_t sw, sh;
    RhiTexture stex;
    RhiState *sstate;
    if (k == 0) {
        ssx = s_sr.cx;
        ssy = s_sr.cy;
        ox = s_sr.cox;
        sw = src->tw;
        sh = src->th;
        stex = src->color;
        sstate = &src->colorState;
    } else {
        ssx = s_sr.sx[k - 1];
        ssy = s_sr.sy[k - 1];
        ox = 0.0f;
        sw = s_sr.w[k - 1];
        sh = s_sr.h[k - 1];
        stex = s_sr.lvl[k - 1];
        sstate = &s_sr.lvlState[k - 1];
    }
    cb.mode[0] = k == 0 ? 0u : 1u;
    cb.param[0] = 2.0f * ssx / s_sr.sx[k];
    cb.param[1] = 2.0f * ssy / s_sr.sy[k];
    cb.param[2] = ssx;
    cb.param[3] = ssy;
    cb.scale[0] = -ox;
    cb.tex[0] = (float)sw;
    cb.tex[1] = (float)sh;
    cb.tex[2] = 1.0f / (float)sw;
    cb.tex[3] = 1.0f / (float)sh;
    for (int ch = 0; ch < 4; ch++) {
        cb.col[ch] = v[1].rgba[ch];
    }
    drawPass(r, s_sr.pipeStep, s_sr.lvl[k], &s_sr.lvlState[k], stex, sstate, w, h, st->color, &cb);
    s_sr.made[k] = s_sr.chain + 1u;
    if (k + 1 < RD_SR_LEVELS) {
        return;
    }
    for (int j = 0; j < RD_SR_LEVELS; j++) {
        tent(r, j, rd_target((RdTargetId)(RD_TARGET_SHADOW0 + j)).id);
    }
    s_sr.ready = true;
    s_sr.readyChain = s_sr.chain;
    s_sr.readyReplay = g_rd.replayCounter;
    g_rdPerf.shadowHighChains++;
    sizeLine();
}

RhiTexture rd__shadow_fine(uint32_t target, uint32_t drawTarget)
{
    if (!s_sr.ready || s_sr.readyChain != s_sr.chain || s_sr.readyReplay != g_rd.replayCounter ||
        !g_rd.settings.shadowHigh || levelOf(drawTarget) >= 0) {
        return (RhiTexture){0};
    }
    const int k = levelOf(target);
    if (k < 0 || !s_sr.lvl[k].id || s_sr.lvlState[k] != RHI_STATE_SHADER_READ) {
        return (RhiTexture){0};
    }
    return s_sr.lvl[k];
}

bool rd__shadow_high_tex(uint32_t texId)
{
    for (int k = 0; texId && k < RD_SR_LEVELS; k++) {
        if (s_sr.lvl[k].id == texId) {
            return true;
        }
    }
    return false;
}

bool rd__shadow_high_level(uint32_t target)
{
    return levelOf(target) >= 0 && rd__shadow_fine(target, 0).id != 0;
}

/* ------------------------------------------------------------ the lines */

void rd__shadow_high_pop(const RdPerfRecord *rec)
{
    s_sum.chains += rec->shadowHighChains;
    s_sum.passes += rec->shadowHighPasses;
    s_sum.original += rec->shadowHighOriginal;
    s_sum.replays++;
}

void rd__shadow_high_text(char *out, size_t size, size_t *at)
{
    if (s_sum.replays == 0 || (s_sum.chains == 0 && s_sum.original == 0) || *at >= size) {
        memset(&s_sum, 0, sizeof(s_sum));
        return;
    }
    const double n = (double)s_sum.replays;
    const int w =
        snprintf(out + *at, size - *at,
                 "; shadow detail high: %.1f shadows a replay at the scene's scale "
                 "(%.1f passes), %.1f as Original",
                 (double)s_sum.chains / n, (double)s_sum.passes / n, (double)s_sum.original / n);
    rd_clause_done(out, size, at, w);
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__shadow_high_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    snprintf(out, size,
             "shadow detail %s: %u shadows at the scene's scale, %u passes, %u as Original",
             g_rd.settings.shadowHigh ? "high" : "original", rec->shadowHighChains,
             rec->shadowHighPasses, rec->shadowHighOriginal);
}
