/* rd_blur_smooth.c: Options > Effects > Motion blur: Smooth
 * (RdSettings.motionBlurSmooth; docs/port/RENDER.md, "The motion blur under
 * Smooth").
 *
 * The game's motion blur (staticBlur.c MotionBlur) is one sprite in list 7:
 * the previous DISPLAY (TBP 0, H / 2 lines stretched over H) drawn over the
 * new SCENE with ALPHA (Cs - Cd) FIX / 128 + Cd, once a tick.  The port
 * replays the whole frame at every present, and Original keeps that loop
 * once a tick: every present of a tick reads the DISPLAY the tick started
 * from (DISPLAY_HELD, rd_interp.c feedback), so between ticks the trail
 * stands still while the picture in front of it moves.
 *
 * Smooth moves the trail on at every present instead.  Per present i,
 *
 *     A_i = S_i + a' (A_i-1 - S_i),   a' = FIX' / 128,
 *
 * with S_i SCENE just before the sprite (the scene of that present), in an
 * RGBA16F ping-pong pair at SCENE's texture size (an 8-bit running blend
 * would stall up to 7 levels short at 10 presents a tick: its floor step
 * of (A - S) FIX' >> 7 is 0 below 128 / (128 - FIX')).  The present draws
 * the game's own sprite, as recorded, but reading A_i-1 (resolved into an
 * RGBA8 scene-class temporary target, RES, through its RGB24 view, its UVs
 * over all H lines) with FIX' in place of FIX:
 *
 *     FIX' = clamp(round(128 (FIX / 128)^(1 / k)), FIX, 127),
 *
 * k the presents a tick (rd_interp.c rd_present: the present clock's tick
 * over its step, or under the vsync lock the presents the tick before
 * counted; 1 for the first tick), so k presents keep the share one tick
 * kept on the PS2: the same length of trail.  The sprite's 8-bit result is
 * A_i rounded; the accumulator keeps the fraction.
 *
 * What it does not keep of Original (docs/port/DISPLAY.md): the light the
 * passes after the sprite add (the aura, the brightness step, the
 * reduction's tint) is no longer fed back into the trail (A holds scenes,
 * not DISPLAYs; spread over k presents that loop would settle at another
 * brightness, rd_interp.c's header), and the PS2's vertical halving of the
 * old frame (DISPLAY's H / 2 lines) is gone.  A still picture is the same
 * within a level.
 *
 * The trail ends where a present drew no motion blur sprite (the game's
 * alpha 0 between rooms, the option off): the next sprite starts it again
 * from its own scene (A = S, FIX' 0, no old picture in it), as the targets
 * being made again, Smooth turned on, and a new scene size do.  A replay
 * the presenters did not arm (rd__blur_smooth_arm: a self-test, a dump's,
 * the replay tool's --time) draws the sprite as Original and leaves A
 * alone.
 *
 * Cost: two passes of one triangle over SCENE's texture a present, 20
 * bytes a texel (2 x RGBA16F + RGBA8: 50 MB at 2340 x 1080, 166 MB at
 * 3840 x 2160), made only while Smooth draws; a texture the GPU refuses
 * leaves the run on Original (logged).  The two pipelines are a private
 * cache (RD_BLUR_SMOOTH_PIPES, as the COLCLAMP 0 wrap's), so the
 * rd__get_pipeline set (RD_PIPELINE_REACHABLE_MAX) does not change. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "rd_replay.h"

/* the restarts named in an event line a block (rd__blur_smooth_block) */
#define SMOOTH_RESTART_LINES 3u

static struct {
    RhiTexture acc[2]; /* the pair, RGBA16F, values 0..255 */
    RhiState accState[2];
    uint32_t w, h;         /* their size: SCENE's texture */
    uint32_t cur;          /* the one holding A */
    uint32_t res;          /* RES, the RGBA8 temporary target (id), 0 none */
    bool valid;            /* A holds a trail */
    bool refused;          /* a texture or pipeline the GPU refused: Original for the run */
    bool armed;            /* the replay in progress presents (rd__blur_smooth_arm) */
    bool sawSprite;        /* and it drew the motion blur sprite */
    uint32_t k;            /* the presents a tick, 1..RD_BLUR_SMOOTH_K_MAX */
    bool kMeasured;        /* k came from the presenter (not the first tick's 1) */
    uint8_t fix, fixPrime; /* the last sprite's */
    uint32_t frame;        /* the frame of the last sprite */
    const char *why;       /* the next restart's reason */
    bool oneNoted;         /* the line for k = 1, once a run */
    /* the block's (rd__blur_smooth_block) */
    uint64_t bPresents, bDrawn, bRestarts, bOriginal;
    uint32_t bLogged, bKMin, bKMax;
    /* since start (rd__blur_smooth_counts) */
    uint64_t drawn, restarts, original;
} s_sm = {.k = 1, .why = "the first motion blur sprite"};

/* 0 the accumulate, 1 the resolve */
static RhiPipeline s_pipes[RD_BLUR_SMOOTH_PIPES];

uint8_t rd__blur_smooth_fix(uint32_t fix, uint32_t presentsPerTick)
{
    if (presentsPerTick <= 1u || fix == 0u || fix >= 128u) {
        return (uint8_t)(fix > 255u ? 255u : fix);
    }
    const double v = 128.0 * pow((double)fix / 128.0, 1.0 / (double)presentsPerTick);
    long r = lround(v);
    if (r > 127) {
        r = 127;
    }
    if (r < (long)fix) {
        r = (long)fix;
    }
    return (uint8_t)r;
}

bool rd__blur_smooth_active(void)
{
    return g_rd.settings.motionBlurSmooth != 0 && !s_sm.refused;
}

void rd__blur_smooth_arm(int firstOfTick, uint32_t presentsPerTick)
{
    s_sm.armed = true;
    s_sm.sawSprite = false;
    if (firstOfTick) {
        const uint32_t k = presentsPerTick < 1u                     ? 1u
                           : presentsPerTick > RD_BLUR_SMOOTH_K_MAX ? RD_BLUR_SMOOTH_K_MAX
                                                                    : presentsPerTick;
        s_sm.k = k;
        s_sm.kMeasured = presentsPerTick != 0u;
    }
    if (!rd__blur_smooth_active()) {
        return;
    }
    s_sm.bPresents++;
    if (s_sm.bKMin == 0u || s_sm.k < s_sm.bKMin) {
        s_sm.bKMin = s_sm.k;
    }
    if (s_sm.k > s_sm.bKMax) {
        s_sm.bKMax = s_sm.k;
    }
}

void rd__blur_smooth_replay_end(int keep)
{
    if (s_sm.armed && !keep && !s_sm.sawSprite && s_sm.valid) {
        /* the trail ends here: the next sprite starts from its scene */
        s_sm.valid = false;
        s_sm.why = "no motion blur in the present before";
    }
    s_sm.armed = false;
    s_sm.sawSprite = false;
}

static double smoothMb(uint32_t w, uint32_t h)
{
    return (double)w * (double)h * 20.0 / (1024.0 * 1024.0);
}

/* the pair and RES, without a word */
static void freeAll(void)
{
    for (int k = 0; k < 2; k++) {
        if (s_sm.acc[k].id && g_rd.hasDevice) {
            rhi_destroy_texture(s_sm.acc[k]);
        }
        s_sm.acc[k] = (RhiTexture){0};
        s_sm.accState[k] = RHI_STATE_UNDEFINED;
    }
    RdTargetRec *t = s_sm.res ? rd__target_rec(s_sm.res) : NULL;
    if (t && g_rd.hasDevice) {
        rd__target_destroy_gpu(t); /* not parked: its memory goes now */
    }
    if (t) {
        rd__temp_target_free(s_sm.res);
    }
    s_sm.res = 0;
    s_sm.w = s_sm.h = 0;
    s_sm.cur = 0;
    s_sm.valid = false;
}

void rd__blur_smooth_free(const char *why)
{
    const bool held = s_sm.acc[0].id != 0 || s_sm.res != 0;
    if (held) {
        rd__log("motion blur: smooth: the accumulator %ux%u freed (%s)", s_sm.w, s_sm.h,
                why ? why : "asked");
    }
    freeAll();
    if (held) {
        s_sm.why = why ? why : "the accumulator freed";
    }
    s_sm.refused = false; /* a new size or a new choice: tried again */
    s_sm.oneNoted = false;
}

void rd__blur_smooth_shutdown(void)
{
    for (int k = 0; k < 2; k++) {
        if (s_sm.acc[k].id && g_rd.hasDevice) {
            rhi_destroy_texture(s_sm.acc[k]);
        }
    }
    for (int i = 0; i < RD_BLUR_SMOOTH_PIPES; i++) {
        if (s_pipes[i].id) {
            rhi_destroy_pipeline(s_pipes[i]);
        }
        s_pipes[i] = (RhiPipeline){0};
    }
    /* rd_shutdown has destroyed every target (RES among them) */
    memset(&s_sm, 0, sizeof(s_sm));
    s_sm.k = 1;
    s_sm.why = "the first motion blur sprite";
}

void rd__blur_smooth_sizes(uint32_t acc[2], uint32_t res[2])
{
    acc[0] = s_sm.acc[0].id ? s_sm.w : 0u;
    acc[1] = s_sm.acc[0].id ? s_sm.h : 0u;
    const RdTargetRec *t = s_sm.res ? rd__target_rec(s_sm.res) : NULL;
    res[0] = t ? t->tw : 0u;
    res[1] = t ? t->th : 0u;
}

void rd__blur_smooth_counts(uint64_t *drawn, uint64_t *restarts, uint64_t *original)
{
    if (drawn) {
        *drawn = s_sm.drawn;
    }
    if (restarts) {
        *restarts = s_sm.restarts;
    }
    if (original) {
        *original = s_sm.original;
    }
}

void rd__blur_smooth_block(void)
{
    if (s_sm.bDrawn || s_sm.bRestarts || s_sm.bOriginal) {
        const uint64_t more =
            s_sm.bRestarts > SMOOTH_RESTART_LINES ? s_sm.bRestarts - SMOOTH_RESTART_LINES : 0u;
        rd__log("motion blur: smooth: %llu presents, %u to %u presents a tick, FIX %u drawn as "
                "%u (the last); %llu sprites through the accumulator, %llu restarts (%llu not "
                "named above), %llu drawn as Original (no accumulator)",
                (unsigned long long)s_sm.bPresents, s_sm.bKMin, s_sm.bKMax, (unsigned)s_sm.fix,
                (unsigned)s_sm.fixPrime, (unsigned long long)s_sm.bDrawn,
                (unsigned long long)s_sm.bRestarts, (unsigned long long)more,
                (unsigned long long)s_sm.bOriginal);
    }
    s_sm.bPresents = s_sm.bDrawn = s_sm.bRestarts = s_sm.bOriginal = 0;
    s_sm.bLogged = s_sm.bKMin = s_sm.bKMax = 0;
}

/* the accumulate (resolve 0, into RGBA16F) or the resolve (1, into RGBA8)
 * pipeline: blit_vs, no blending, every state static */
static RhiPipeline smoothPipe(int resolve)
{
    if (s_pipes[resolve].id) {
        return s_pipes[resolve];
    }
    const int fs = resolve ? RD_FS_FX_ACC_RESOLVE : RD_FS_FX_ACC;
    if (!g_rd.fs[fs].id || !g_rd.vs[RD_VS_BLIT].id) {
        return (RhiPipeline){0};
    }
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = g_rd.vs[RD_VS_BLIT];
    d.fragment = g_rd.fs[fs];
    d.layouts = layouts;
    d.layoutCount = 3;
    d.cullNone = true;
    d.colorCount = 1;
    d.colorFormats[0] = resolve ? RHI_FMT_RGBA8_UNORM : RHI_FMT_RGBA16F;
    d.blend[0].srcColor = RHI_BF_ONE;
    d.blend[0].dstColor = RHI_BF_ZERO;
    d.blend[0].colorOp = RHI_BO_ADD;
    d.blend[0].srcAlpha = RHI_BF_ONE;
    d.blend[0].dstAlpha = RHI_BF_ZERO;
    d.blend[0].alphaOp = RHI_BO_ADD;
    d.blend[0].writeMask = 0xF;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.debugName = resolve ? "rd motion blur resolve" : "rd motion blur accumulate";
    rd__pipeline_note_begin(d.debugName);
    s_pipes[resolve] = rhi_create_pipeline(&d);
    rd__pipeline_note_end();
    if (s_pipes[resolve].id) {
        g_rd.stats.pipelineCreates++;
    }
    return s_pipes[resolve];
}

/* The pair and RES at SCENE's texture size (tc: SCENE as the sprite binds
 * it); false when they cannot be made (logged; Original for the run) */
static bool ensure(Replay *r, const RdTargetRec *tc)
{
    const RdTargetRec *rt = s_sm.res ? rd__target_rec(s_sm.res) : NULL;
    if (s_sm.acc[0].id && s_sm.acc[1].id && rt && rt->color.id && s_sm.w == tc->tw &&
        s_sm.h == tc->th && rt->tw == tc->tw && rt->th == tc->th) {
        return true;
    }
    if (s_sm.acc[0].id || s_sm.res) {
        rd__replay_end_pass(r);
        rd__log("motion blur: smooth: the scene is %ux%u texels now (the accumulator %ux%u): made "
                "again",
                tc->tw, tc->th, s_sm.w, s_sm.h);
        freeAll();
        s_sm.why = "the scene's size changed";
    }
    const RhiPipeline pa = smoothPipe(0), pr = smoothPipe(1);
    if (!pa.id || !pr.id) {
        rd__log("motion blur: smooth: its pipelines could not be made (accumulate %s, resolve "
                "%s): the motion blur is drawn as Original for this run",
                pa.id ? "made" : "missing", pr.id ? "made" : "missing");
        s_sm.refused = true;
        return false;
    }
    bool ok = true;
    s_sm.res = rd__temp_target_alloc(tc->w, tc->h, 0, 1);
    rt = s_sm.res ? rd__target_rec(s_sm.res) : NULL;
    ok = rt && rt->color.id && rt->format == RHI_FMT_RGBA8_UNORM && rt->tw == tc->tw &&
         rt->th == tc->th;
    for (int k = 0; ok && k < 2; k++) {
        s_sm.acc[k] = rhi_create_texture(&(RhiTextureDesc){
            tc->tw, tc->th, 1, RHI_FMT_RGBA16F, RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
            k ? "rd motion blur accumulator 1" : "rd motion blur accumulator 0", 0});
        s_sm.accState[k] = RHI_STATE_UNDEFINED;
        ok = s_sm.acc[k].id != 0;
    }
    if (!ok) {
        rd__log("motion blur: smooth: no memory for the accumulator %ux%u (RGBA16F x2 + RGBA8, "
                "%.1f MB%s): the motion blur is drawn as Original for this run",
                tc->tw, tc->th, smoothMb(tc->tw, tc->th),
                rt && (rt->tw != tc->tw || rt->th != tc->th) ? ", its target not the scene's size"
                                                             : "");
        freeAll();
        s_sm.refused = true;
        return false;
    }
    s_sm.w = tc->tw;
    s_sm.h = tc->th;
    s_sm.cur = 0;
    s_sm.valid = false;
    rd__log("motion blur: smooth: accumulator %ux%u (RGBA16F x2 + RGBA8, %.1f MB)", s_sm.w, s_sm.h,
            smoothMb(s_sm.w, s_sm.h));
    return true;
}

/* one pass of one triangle over a w x h texture: dst (in its state *st)
 * from t1 (and t2, the accumulate's SCENE), a' in DrawCB.param.x */
static void smoothPass(Replay *r, RhiPipeline pipe, RhiTexture dst, RhiState *st, uint32_t perfId,
                       RhiTexture t1, RhiTexture t2, float a)
{
    RhiCommandList cl = rd__replay_cl();
    const uint32_t w = s_sm.w, h = s_sm.h;
    rd__transition(cl, dst, st, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, perfId, w, h, r->perfPost);
    rhi_cmd_checkpoint(cl,
                       rd__replay_pass_push(RD_PASSK_PASS, perfId, w, h, 0, 0, 0, 0, r->perfPost));
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
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.param[0] = a;
    const RhiSampler smp =
        rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    rd__set_pipeline(cl, pipe, NULL); /* smoothPipe: every state static */
    rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, t2.id ? rd__tex_group_date(t1, smp, t2) : rd__tex_group(t1, smp));
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, dst, st, RHI_STATE_SHADER_READ);
    g_rdPerf.blurSmoothPasses++;
}

/* The sprite's state: SCENE as its target, a texture, a record of its own
 * (no table); NULL with the reason otherwise */
static RdTargetRec *spriteTarget(const Replay *r, const RdFrame *f, const RdCmd *c, RdPostRec *p,
                                 const char **why)
{
    *why = NULL;
    if (c->u[1] > f->payloadSize || f->payloadSize - c->u[1] < sizeof(*p)) {
        *why = "its record is outside the frame";
        return NULL;
    }
    memcpy(p, f->payload + c->u[1], sizeof(*p));
    RdTargetRec *tc = rd__target_rec(r->st.color);
    if (!tc || !tc->color.id || tc->format != RHI_FMT_RGBA8_UNORM || tc->w != g_rd.gsW ||
        tc->h != g_rd.gsH) {
        *why = "its target is not the scene";
    } else if (!r->st.ds.texEnabled || p->lutOffset != ~0u) {
        *why = "it is not the game's textured sprite";
    }
    return *why ? NULL : tc;
}

bool rd__blur_smooth_sprite(Replay *r, const RdFrame *f, const RdCmd *c)
{
    if (!g_rd.settings.motionBlurSmooth || !g_rd.hasDevice) {
        return false;
    }
    if (!s_sm.armed || s_sm.refused) {
        s_sm.original++; /* a replay that does not present, or no accumulator */
        s_sm.bOriginal += s_sm.armed;
        return false;
    }
    s_sm.sawSprite = true;
    RdPostRec p;
    const char *why = NULL;
    RdTargetRec *tc = spriteTarget(r, f, c, &p, &why);
    if (!tc) {
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("motion blur: smooth: the sprite of frame %u is drawn as Original: %s "
                    "(reported once)",
                    f->number, why);
        }
        s_sm.original++;
        s_sm.bOriginal++;
        return false;
    }
    rd__replay_flush_run(r);
    if (!ensure(r, tc)) {
        s_sm.original++;
        s_sm.bOriginal++;
        return false;
    }
    const uint32_t fix = r->st.ds.blendFix;
    const bool restart = !s_sm.valid;
    const uint8_t fp = restart ? 0u : rd__blur_smooth_fix(fix, s_sm.k);
    s_sm.fix = (uint8_t)fix;
    s_sm.fixPrime = fp;
    s_sm.frame = f->number;
    RdTargetRec *rt = rd__target_rec(s_sm.res);
    rd__replay_end_pass(r);
    RhiCommandList cl = rd__replay_cl();
    /* SCENE as the passes read it (an open MSAA epoch resolves into it here,
     * rd__transition) */
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    const uint32_t a = s_sm.cur, b = s_sm.cur ^ 1u;
    if (!restart) {
        /* RES = A_i-1 rounded, which the sprite reads */
        rd__transition(cl, s_sm.acc[a], &s_sm.accState[a], RHI_STATE_SHADER_READ);
        smoothPass(r, s_pipes[1], rt->color, &rt->colorState, s_sm.res, s_sm.acc[a],
                   (RhiTexture){0}, 0.0f);
    }
    /* A_i = S_i + a' (A_i-1 - S_i); A_i = S_i on a restart */
    if (s_sm.accState[a] != RHI_STATE_SHADER_READ) {
        rd__transition(cl, s_sm.acc[a], &s_sm.accState[a], RHI_STATE_SHADER_READ);
    }
    smoothPass(r, s_pipes[0], s_sm.acc[b], &s_sm.accState[b], r->st.color, s_sm.acc[a], tc->color,
               restart ? 0.0f : (float)fp / 128.0f);
    s_sm.cur = b;
    s_sm.valid = true;
    /* the game's sprite as recorded, with FIX' and (but on a restart, whose
     * FIX' 0 leaves SCENE's colour as it is) RES over all H lines in place
     * of DISPLAY's H / 2 */
    static RdPostRec s_rec;
    static RdFrame s_sf;
    s_rec = p;
    const uint32_t keepTex = r->st.tex;
    const uint8_t keepFix = r->st.ds.blendFix;
    r->st.ds.blendFix = fp;
    s_rec.fix = fp;
    if (!restart) {
        const RdTex view = rd_target_texture((RdTarget){s_sm.res}, RD_VIEW_RGB24_TA0);
        if (view.id) {
            r->st.tex = view.id;
            s_rec.src = s_sm.res;
            s_rec.srcView = RD_VIEW_RGB24_TA0;
            /* RES holds H lines: V over twice the recorded span from the
             * same start (4 .. 4100 is 4 .. 8196, one texel a row as U's) */
            s_rec.uv[3] = p.uv[1] + 2.0f * (p.uv[3] - p.uv[1]);
        }
    }
    s_sf = *f;
    s_sf.payload = (uint8_t *)&s_rec;
    s_sf.payloadSize = (uint32_t)sizeof(s_rec);
    RdCmd sc = *c;
    sc.u[1] = 0;
    sc.u[2] = (uint32_t)sizeof(RdPostRec);
    rd__replay_blur_sprite_draw(r, &s_sf, &sc);
    r->st.tex = keepTex;
    r->st.ds.blendFix = keepFix;
    s_sm.drawn++;
    s_sm.bDrawn++;
    if (restart) {
        s_sm.restarts++;
        s_sm.bRestarts++;
        if (s_sm.bLogged < SMOOTH_RESTART_LINES) {
            s_sm.bLogged++;
            rd__log("motion blur: smooth: the trail starts again at frame %u (%s): this present's "
                    "scene is its first picture, no old frame in it",
                    f->number, s_sm.why);
        }
    }
    if (!s_sm.oneNoted && s_sm.kMeasured && s_sm.k == 1u) {
        s_sm.oneNoted = true;
        rd__log("motion blur: smooth at one present a tick (FIX %u as recorded): the trail moves "
                "on with the game's frames as Original's; it differs from Original only in what "
                "it holds (the scene before the glow and the tint, at full height)",
                (unsigned)fix);
    }
    return true;
}
