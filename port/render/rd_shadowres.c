/* rd_shadowres.c: the shadow's levels at the scene's scale above 1x
 * (rd_shadowres.h; docs/port/RENDER_POST.md "The shadow's levels above 1x";
 * docs/port/DIVERGENCES.md VD-34).  Not an option: how the port draws the
 * shadow whenever the scene's scale is above 1 on an axis, in every preset
 * and at every aspect; at scale 1 on both axes nothing here is made or run.
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
 * targets never come there.  The named levels are still drawn from it.
 *
 * Above 1x (the count target's scale above 1 on an axis; each axis on its
 * own: 2.8125 x 2.41 under Resolution window; at 1 x 2 the levels keep the
 * PS2's width, the step and tent the identity across):
 *   1. the count is also kept at its full size: the seven draws and the
 *      MSAA resolve (the covered-majority resolve to one sample at the
 *      scene's size, rd_msaa_shadow.c) leave it there already; the one
 *      draw (rd_shadow_one.c) draws it a second time into the count target
 *      at its own texels (the box of one texel: the stencil's table texel
 *      itself), one pass more;
 *   2. at each of the game's three level sprites (rd_shadow_box.c
 *      rd__shadow_box_screen, before the sprite's own pass), the level is
 *      drawn again into a texture of its own at S texels a GS pixel on each
 *      axis, S the smallest odd whole number at or above the scene's scale
 *      there (rd_sr_level_scale: 3 at 2x, 2.41x, 2.8125x and 3x, 5 at 4x
 *      and at 2160 rows; an axis at scale 1 keeps S = 1) (shadowres_step_ps):
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
 *      mean of the PS2's picture over the level grid's S x S placements:
 *      the same width and darkness, no staircase.  (At an even S the tent's
 *      centre would be half a texel off and the tap would soften it further:
 *      10 % wider at 2x in the edge model; hence the odd S);
 *   4. the composites are the game's, drawn as before, reading the fine
 *      level (resolveTexture, rd__shadow_fine) instead of the named one.
 * The game's own level sprites still draw the named levels as before (a
 * chain the rule could not take, a refusal, a draw into a level that is not
 * the chain's, all read them: the PS2's size).  All of it sits between the
 * count and the composites, where SCENE's pass is ended already (the DATE
 * snapshot's and the levels' own passes): no SCENE pass more on a tiler.
 * Every backend draws it (two plain pixel shaders, Load only: SPIR-V for
 * Vulkan, GLSL for OpenGL, DXIL for Direct3D 12, the HLSL compiled at run
 * time for Direct3D 11); a backend whose shaders or pipelines cannot be
 * made keeps the PS2's size for the run, logged.
 *
 * Interactions: the volumes' box and the empty composites (rd_shadow_box.c)
 * are the named levels' and are kept (a fine level's reach is the named
 * level's plus one level texel of the step (a fine texel between two named
 * ones reads both their footprints), one of the tent and 1 / S of one of the
 * composite's tap: inside the composites' RD_SB_TEX_MARGIN of 3, where the
 * named level needs one); the one DATE set-up holds (every fine texel is a
 * mean of values the named levels hold, MODULATEd alike); the shadow tiles
 * (rd_shadow_tile.c) step aside for a composite that reads a fine level
 * (their mask is the named level's); [dev] shadow_bspline does nothing to a
 * fine level (rd_fx_plan.c); a window without volumes (rd_no_volume.c)
 * skips the levels, so no chain is made.
 *
 * Tests only (docs/port/TESTING.md): ICO_RD_SHADOW_FINE=0 keeps the PS2's
 * sizes above 1x (v0.11.6's picture: the A/B's other half);
 * ICO_RD_SHADOW_FINE=force draws the fine chain at 1x too (S = 1 on both
 * axes: the GS's own step and an identity tent).
 *
 * The lines (rd__log):
 *   shadow levels: at the scene's scale above 1x (the smallest odd whole
 *     one at or above it on each axis: the count at its full size, the
 *     game's step at every texel, the tent its magnification supplied); at
 *     1x the PS2's sizes
 *   shadow levels: at the scene's scale: SHADOW0 864x768, SHADOW1 432x384,
 *     SHADOW2 216x192 (3x3 texels a GS pixel) from the count's 3x3 texels a
 *     GS pixel, 10 passes a shadow, 7.1 MB, on vulkan (reported at each
 *     change)
 *   shadow levels: the PS2's sizes: the scene at 1x1 texels a GS pixel
 *     (reported at each change)
 *   shadow levels: a shadow above 1x drawn at the PS2's size: <why>
 *     (reported once a reason)
 *   the 10 s clause "; shadow levels: 1.0 shadows a replay at the scene's
 *     scale (10.0 passes), 0.0 at the PS2's size" and --stats' "shadow
 *     levels: 1 shadows at the scene's scale, 10 passes, 0 at the PS2's
 *     size". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_clause.h"
#include "rd_internal.h"
#include "rd_replay.h"
#include "rd_shadowres.h"

/* ------------------------------------------------------------ the replay */

static struct {
    struct Replay *r; /* the screen command's replay (rd__no_volume_skip) */
    uint32_t chain;   /* the counts begun (rd__shadow_fine_reset) */
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
    bool refused;          /* the pipelines failed: the PS2's sizes for the run */
    uint32_t whyEvents;    /* the refusals so far (logged: 8, then every 100th) */
    uint32_t fineSince;    /* the shadows at the scene's scale since the last refusal */
    uint32_t saidW, saidH; /* the size line's last SHADOW0 */
} s_sr;

/* a shadow above 1x drawn at the PS2's size: counted by reason, a line the
 * first 8 times and every 100th after (detail: the field that failed) */
static void sayDetail(int why, const char *detail)
{
    g_rdPerf.shadowFineRefused++;
    if (why >= 0 && why < RD_SR_WHY_N && g_rdPerf.shadowFineWhy[why] < 0xFFFFu) {
        g_rdPerf.shadowFineWhy[why]++;
    }
    s_sr.whyEvents++;
    /* the first refusal after shadows at the scene's scale (a new scene) is
     * logged whatever the count */
    const uint32_t since = s_sr.fineSince;
    s_sr.fineSince = 0;
    if (s_sr.whyEvents > 8u && s_sr.whyEvents % 100u != 0u && since == 0u) {
        return;
    }
    rd__log("shadow levels: a shadow above 1x drawn at the PS2's size: %s%s%s (replay %u, command "
            "%u; refusal %u, after %u shadows at the scene's scale; logged: the first 8, the "
            "first after shadows at the scene's scale, then every 100th)",
            rd_sr_why_text(why), detail && detail[0] ? ": " : "", detail ? detail : "",
            (unsigned)g_rd.replayCounter, (unsigned)g_rdStoreOrd, (unsigned)s_sr.whyEvents,
            (unsigned)since);
}

static void say(int why)
{
    sayDetail(why, NULL);
}

/* ICO_RD_SHADOW_FINE (tests only): 0 the PS2's sizes above 1x, "force" the
 * fine chain at 1x too, anything else (or none) the rule */
enum { FINE_OFF = 0, FINE_RULE = 1, FINE_FORCE = 2 };

static int fineMode(void)
{
    static int s_mode = -1;
    if (s_mode < 0) {
        const char *e = getenv("ICO_RD_SHADOW_FINE");
        s_mode = e && strcmp(e, "0") == 0       ? FINE_OFF
                 : e && strcmp(e, "force") == 0 ? FINE_FORCE
                                                : FINE_RULE;
    }
    return s_mode;
}

static const char *backendName(void)
{
    switch (rhi_backend()) {
    case RHI_BACKEND_VULKAN:
        return "vulkan";
    case RHI_BACKEND_D3D12:
        return "d3d12";
    case RHI_BACKEND_OPENGL:
        return "opengl";
    case RHI_BACKEND_D3D11:
        return "d3d11";
    default:
        return "another backend";
    }
}

void rd__shadow_fine_log(void)
{
    const int m = fineMode();
    if (m == FINE_OFF) {
        rd__log("shadow levels: the PS2's sizes at every scale (ICO_RD_SHADOW_FINE=0, the "
                "tests' switch)");
        return;
    }
    rd__log("shadow levels: at the scene's scale above 1x (the smallest odd whole one at or "
            "above it on each axis: the count at its full size, the game's step at every texel, "
            "the tent its magnification supplied); at 1x the PS2's sizes%s",
            m == FINE_FORCE ? "; at 1x too (ICO_RD_SHADOW_FINE=force, the tests' switch)" : "");
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

void rd__shadow_fine_shutdown(void)
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

void rd__shadow_fine_replay(struct Replay *r)
{
    s_sr.r = r;
}

void rd__shadow_fine_reset(uint32_t color)
{
    (void)color;
    s_sr.chain++;
    s_sr.fullFor = 0;
    s_sr.ready = false;
}

/* the count target's scales on both axes; false when neither is above 1
 * (1x: no path of its own) unless the tests force it */
static bool countScales(const RdTargetRec *tc, float *cx, float *cy)
{
    *cx = rd_sr_count_scale(tc->w, tc->tw, tc->sx, tc->ox);
    *cy = rd_sr_count_scale(tc->h, tc->th, tc->sy, 0.0f);
    return *cx > 1.0f || *cy > 1.0f || fineMode() == FINE_FORCE;
}

bool rd__shadow_fine_count_wanted(const RdTargetRec *tc)
{
    float cx, cy;
    return fineMode() != FINE_OFF && g_rd.hasDevice && !s_sr.refused && tc &&
           countScales(tc, &cx, &cy) && cx <= RD_SR_SCALE_MAX && cy <= RD_SR_SCALE_MAX;
}

void rd__shadow_fine_count_full(uint32_t color)
{
    if (fineMode() == FINE_OFF) {
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
        s_sr.pipeStep = makePipe(s_sr.fsStep, "rd shadow levels step");
    }
    if (s_sr.fsTent.id && !s_sr.pipeTent.id) {
        s_sr.pipeTent = makePipe(s_sr.fsTent, "rd shadow levels tent");
    }
    if (!s_sr.pipeStep.id || !s_sr.pipeTent.id) {
        rd__log("shadow levels: the shaders or pipelines of the levels at the scene's scale could "
                "not be made on %s (step %s, tent %s): the PS2's sizes for this run",
                backendName(), s_sr.pipeStep.id ? "made" : "missing",
                s_sr.pipeTent.id ? "made" : "missing");
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
        {"rd shadow fine level 0", "rd shadow fine level 0 across"},
        {"rd shadow fine level 1", "rd shadow fine level 1 across"},
        {"rd shadow fine level 2", "rd shadow fine level 2 across"}};
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
    g_rdPerf.shadowFinePasses++;
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

/* the sprite is the chain's (rd_sr_recognise, rd_shadowres_math.c): the
 * draw's state and vertices handed over as plain values */
static bool chainSprite(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v, uint32_t n,
                        const RdTargetRec *dst, const RdTargetRec *src, int *why, char *detail,
                        size_t dsize, int *ox, int *oy, float *fx, float *fy)
{
    const RdDrawState *d = &st->ds;
    RdSrSprite sp;
    memset(&sp, 0, sizeof(sp));
    sp.prim = c->b[0];
    sp.space = c->b[1];
    sp.uvFixed = c->b[2];
    sp.frameCam = c->b[4];
    sp.stShift = c->b[5];
    sp.aa1 = (uint8_t)(st->aa1 != 0);
    sp.n = n;
    sp.tcc = (uint8_t)d->tcc;
    sp.texFn = (uint8_t)d->texFn;
    sp.abe = (uint8_t)(d->abe != 0);
    sp.fba = (uint8_t)(d->fba != 0);
    sp.colorMask = (uint8_t)d->colorMask;
    sp.ate = (uint8_t)(d->test.ate != 0);
    sp.atst = (uint8_t)d->test.atst;
    sp.date = (uint8_t)d->test.date;
    sp.zte = (uint8_t)(d->test.zte != 0);
    sp.ztst = (uint8_t)d->test.ztst;
    sp.magF = (uint8_t)d->magFilter;
    sp.minF = (uint8_t)d->minFilter;
    for (uint32_t i = 0; i < 2 && i < n; i++) {
        memcpy(sp.rgba[i], v[i].rgba, 4);
        sp.x16[i] = (int32_t)v[i].x;
        sp.y16[i] = (int32_t)v[i].y;
        sp.s16[i] = v[i].s;
        sp.t16[i] = v[i].t;
    }
    sp.uOff = st->uvOffset[0] * (float)src->w;
    sp.vOff = st->uvOffset[1] * (float)src->h;
    sp.gsW = st->gsW;
    sp.gsH = st->gsH;
    sp.dstW = dst->w;
    sp.dstH = dst->h;
    sp.srcW = src->w;
    sp.srcH = src->h;
    *why = rd_sr_recognise(&sp, ox, oy, fx, fy, detail, dsize);
    return *why < 0;
}

/* the chain in force, at each change: the fine levels' sizes (saidW, saidH)
 * or the PS2's at a scale of 1 (saidW 1, saidH 0) */
static void sizeLine(void)
{
    if (s_sr.saidW == s_sr.w[0] && s_sr.saidH == s_sr.h[0]) {
        return;
    }
    s_sr.saidW = s_sr.w[0];
    s_sr.saidH = s_sr.h[0];
    const uint32_t ms = g_rdPerf.msaaSamples;
    rd__log("shadow levels: at the scene's scale: SHADOW0 %ux%u, SHADOW1 %ux%u, SHADOW2 %ux%u "
            "(%.4gx%.4g texels a GS pixel) from the count's %.4gx%.4g texels a GS pixel, %u "
            "passes a shadow, %.1f MB, on %s%s (reported at each change)",
            s_sr.w[0], s_sr.h[0], s_sr.w[1], s_sr.h[1], s_sr.w[2], s_sr.h[2], (double)s_sr.sx[0],
            (double)s_sr.sy[0], (double)s_sr.cx, (double)s_sr.cy, s_sr.chainPasses, heldMb(),
            backendName(),
            ms > 1u ? ", the count from the MSAA epoch's covered-majority resolve" : "");
}

static void ps2Line(float cx, float cy)
{
    if (s_sr.saidW == 1u && s_sr.saidH == 0u) {
        return;
    }
    s_sr.saidW = 1u;
    s_sr.saidH = 0u;
    rd__log("shadow levels: the PS2's sizes: the scene at %.4gx%.4g texels a GS pixel (reported "
            "at each change)",
            (double)cx, (double)cy);
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

void rd__shadow_fine_screen(const RdStateBlock *st, const RdCmd *c, const RdScreenVtx *v,
                            uint32_t n)
{
    struct Replay *r = s_sr.r;
    if (fineMode() == FINE_OFF || !g_rd.hasDevice || !r || &r->st != st) {
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
            ps2Line(cx, cy);
            return; /* 1x: the game's own, no path of its own */
        }
        if (cx > RD_SR_SCALE_MAX || cy > RD_SR_SCALE_MAX) {
            say(RD_SR_WHY_SCALE);
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
    if (k == 0 && (s_sr.fullFor == 0u || s_sr.fullChain != s_sr.chain)) {
        return; /* no count resolved under the rule this chain (a self-test's frame) */
    }
    if (k == 0 && s_sr.fullFor != t->target) {
        say(RD_SR_WHY_COUNT); /* a level 0 from another target than the count */
        return;
    }
    int why = RD_SR_WHY_MAP, ofx = 0, ofy = 0;
    float wfx = 0.25f, wfy = 0.25f;
    char detail[192];
    if (!chainSprite(st, c, v, n, dst, src, &why, detail, sizeof detail, &ofx, &ofy, &wfx, &wfy)) {
        sayDetail(why, detail);
        return;
    }
    if (!pipes()) {
        say(RD_SR_WHY_PIPES);
        return;
    }
    const uint32_t w = dst->w * rd_sr_level_scale(s_sr.cx), h = dst->h * rd_sr_level_scale(s_sr.cy);
    rd__replay_end_pass(r); /* the open pass (the level before's) */
    if (!texturesFor(k, w, h)) {
        say(RD_SR_WHY_TEXTURES);
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
    cb.mode[1] =
        (st->ds.wrap.s == RD_WRAP_REPEAT ? 1u : 0u) | (st->ds.wrap.t == RD_WRAP_REPEAT ? 2u : 0u);
    cb.param[0] = 2.0f * ssx / s_sr.sx[k];
    cb.param[1] = 2.0f * ssy / s_sr.sy[k];
    cb.param[2] = ssx;
    cb.param[3] = ssy;
    cb.scale[0] = (float)ofx * ssx - ox;
    cb.scale[1] = (float)ofy * ssy;
    cb.uvRect[0] = wfx;
    cb.uvRect[1] = wfy;
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
    g_rdPerf.shadowFineChains++;
    s_sr.fineSince++;
    sizeLine();
}

RhiTexture rd__shadow_fine(uint32_t target, uint32_t drawTarget)
{
    if (!s_sr.ready || s_sr.readyChain != s_sr.chain || s_sr.readyReplay != g_rd.replayCounter ||
        fineMode() == FINE_OFF || levelOf(drawTarget) >= 0) {
        return (RhiTexture){0};
    }
    const int k = levelOf(target);
    if (k < 0 || !s_sr.lvl[k].id || s_sr.lvlState[k] != RHI_STATE_SHADER_READ) {
        return (RhiTexture){0};
    }
    return s_sr.lvl[k];
}

bool rd__shadow_fine_tex(uint32_t texId)
{
    for (int k = 0; texId && k < RD_SR_LEVELS; k++) {
        if (s_sr.lvl[k].id == texId) {
            return true;
        }
    }
    return false;
}

bool rd__shadow_fine_level(uint32_t target)
{
    return levelOf(target) >= 0 && rd__shadow_fine(target, 0).id != 0;
}
