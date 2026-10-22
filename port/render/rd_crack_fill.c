/* rd_crack_fill.c: the crack filler above 1x (issue 26; docs/port/RENDER.md,
 * "Crack filling above 1x").
 *
 * The VU truncates every vertex to the GS's 1/16 pixel grid, each part of
 * the world through its own node matrix, so two triangles that meet on the
 * PS2 can leave a crack of 1/16 GS pixel between them (DIVERGENCES.md
 * VD-20; at a scale that is not a whole number the GPU's second snap moves
 * it, VD-19).  A pixel whose centre falls into it keeps the clear's depth,
 * GS Z 0, which the fog's table takes as its entry 0 (fog_lut.hlsl): a dot
 * of the full fog colour.  At 1x that is the PS2's own picture.  Above 1x a
 * crack catches texel centres along its whole length, and the dots run
 * along the seams of walls and floors (Goblan's 4K screenshots of v0.8.4).
 *
 * Under the Enhanced preset with SCENE's scale above 1 on either axis, the
 * fog of SCENE (rd_replay.c doFog, after the pass before it ends and before
 * the fog reads the depth) is preceded by one pass over SCENE with its
 * depth attached: crack_fill_ps (blit.hlsl) reads a copy of SCENE's colour
 * (its snapshot texture, rd__replay_take_snap) and of its depth (the fog's
 * depth copy object, rd__depth_copy_take: the copy path's own, made here on
 * the other paths) and gives a texel of depth 0 the nearer written texel's
 * colour and depth (SV_Depth, test ALWAYS) where a run of at most R texels
 * of depth 0 along its row or its column (it among them) is bounded by
 * written texels at both ends; every other texel is discarded.  R on an
 * axis is ceil(scale / 2) above 1x, at most 3, else 0: the 1/16 pixel
 * crack is under one texel wide up to 16x, and a slit of a GS pixel or
 * more (2 texels at 2x) is the game's and stays.  The depth goes with the
 * colour so the fog paints the neighbour's fog level, and the draws after
 * the fog (the lists' Z tests) see a surface there.  Exact copies: no
 * filtering, no colour of its own.
 *
 * Not drawn: at scale 1 on both axes, under the Original preset, on any
 * replay with MSAA asked for (the fog then reads the multisampled scene or
 * the resolve's smallest sample, DIVERGENCES.md VD-22), in the fog's
 * self-test, on a fog of another target than SCENE, without a sampleable
 * depth, and with [dev] crack_fill = false (ICO_RD_CRACK_FILL=0; the replay
 * tool's --no-crack-fill).  The local read's tiler path ends its scene pass
 * for it (the fog then begins it again, readable).
 *
 * The counters: the filled pixels are counted on the GPU, one replay in
 * RD_CRACK_COUNT_EVERY (every one for the tests and the replay tool's
 * --stats, rd__crack_fill_count_all): crack_count_ps runs the same test
 * over 16 x 16 texels a texel into a small RGBA8 target (n & 255, n >> 8),
 * copied into a readback buffer and summed once the GPU is past that
 * replay (RHI_FRAMES_IN_FLIGHT replays later, as the fog's edge probe).
 * The lines:
 *   rd: crack fill: on above 1x (scale 2x2, reach 1x1 texels); [dev]
 *       crack_fill = false turns it off          (start, a new scale)
 *   rd: crack fill: N pixels filled over M replays counted of T filled
 *       (max K a replay; scale SXxSY; reach RXxRY texels)   (10 s summary)
 *   rd: crack fill: N fogged replays above 1x not filled (...)   (the same)
 *   rd: crack fill: not drawn ...: <reason> (reported once)   (events) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* crack_count_ps's block (blit.hlsl CRACK_BLOCK) */
#define CRACK_BLOCK 16u

enum { SKIP_MSAA, SKIP_DEPTH, SKIP_PIPES, SKIP_MEMORY, SKIP_COUNT };

static const char *const kSkipWhy[SKIP_COUNT] = {
    "MSAA is asked for (the fog then reads the multisampled scene or its resolve: DIVERGENCES.md "
    "VD-22)",
    "the scene's depth cannot be sampled on this device",
    "its shaders or pipelines are missing",
    "no memory for the copies of the scene's colour and depth",
};

static struct {
    bool enabled; /* ICO_RD_CRACK_FILL, or the replay tool's switch */
    bool countAll;
    /* the count's target and readback (one in flight) */
    RhiTexture countTex;
    RhiState countState;
    uint32_t cw, ch;
    RhiBuffer buf;
    uint64_t bufSize;
    bool pending;
    uint32_t pendingReplay, pendingW, pendingH, pendingPitch;
    /* since start */
    uint64_t filled, counted, pixels, skipped;
    uint32_t last, max;
    float sx, sy;
    uint32_t rx, ry;
    /* the 10 s block's (rd__crack_fill_block) */
    uint64_t bFilled, bCounted, bPixels, bSkip[SKIP_COUNT];
    uint32_t bMax;
    bool skipSaid[SKIP_COUNT];
    char noted[192]; /* the last state line */
} s_cf;

/* the replay tool's --no-crack-fill and the tests: -1 none, else the switch */
static int s_forced = -1;

uint32_t rd__crack_fill_reach(float scale)
{
    if (!(scale > 1.0f)) {
        return 0u;
    }
    const float r = ceilf(scale * 0.5f);
    return r >= (float)RD_CRACK_REACH_MAX ? RD_CRACK_REACH_MAX : (uint32_t)r;
}

static RdTargetRec *sceneRec(void)
{
    return rd__target_rec(RD_TARGET_SCENE + 1u);
}

/* the state line, logged when it changes */
static void note(void)
{
    if (!g_rd.hasDevice) {
        return;
    }
    const RdTargetRec *t = sceneRec();
    const float sx = t ? t->sx : 1.0f, sy = t ? t->sy : 1.0f;
    char line[sizeof(s_cf.noted)];
    if (!s_cf.enabled) {
        snprintf(line, sizeof(line), "crack fill: off (%s)",
                 s_forced == 0 ? "the replay tool's --no-crack-fill"
                               : "[dev] crack_fill = false, ICO_RD_CRACK_FILL=0");
    } else if (g_rd.settings.preset != RD_PRESET_ENHANCED) {
        snprintf(line, sizeof(line),
                 "crack fill: on above 1x, not drawn under the Original preset; [dev] "
                 "crack_fill = false turns it off");
    } else if (!(sx > 1.0f || sy > 1.0f)) {
        snprintf(line, sizeof(line),
                 "crack fill: on above 1x, not drawn at scale %.4gx%.4g (the GS's own grid); "
                 "[dev] crack_fill = false turns it off",
                 (double)sx, (double)sy);
    } else {
        snprintf(line, sizeof(line),
                 "crack fill: on above 1x (scale %.4gx%.4g, reach %ux%u texels); [dev] "
                 "crack_fill = false turns it off",
                 (double)sx, (double)sy, rd__crack_fill_reach(sx), rd__crack_fill_reach(sy));
    }
    if (strcmp(line, s_cf.noted) != 0) {
        snprintf(s_cf.noted, sizeof(s_cf.noted), "%s", line);
        rd__log("%s", line);
    }
}

void rd__crack_fill_init(void)
{
    const RhiTexture tex = s_cf.countTex;
    const RhiBuffer buf = s_cf.buf;
    const bool countAll = s_cf.countAll;
    memset(&s_cf, 0, sizeof(s_cf));
    s_cf.countTex = tex; /* freed by rd__crack_fill_shutdown */
    s_cf.buf = buf;
    s_cf.countAll = countAll;
    const char *env = getenv("ICO_RD_CRACK_FILL");
    s_cf.enabled = s_forced >= 0 ? s_forced != 0 : !(env && strcmp(env, "0") == 0);
}

void rd__crack_fill_scene_made(void)
{
    s_cf.pending = false; /* a count at the old size is dropped */
    note();
}

void rd__crack_fill_set_enabled(bool on)
{
    s_forced = on ? 1 : 0;
    s_cf.enabled = on;
    note();
}

void rd__crack_fill_count_all(bool on)
{
    s_cf.countAll = on;
}

static void countFree(void)
{
    if (s_cf.countTex.id && g_rd.hasDevice) {
        rhi_destroy_texture(s_cf.countTex);
    }
    if (s_cf.buf.id && g_rd.hasDevice) {
        rhi_destroy_buffer(s_cf.buf);
    }
    s_cf.countTex = (RhiTexture){0};
    s_cf.buf = (RhiBuffer){0};
    s_cf.countState = RHI_STATE_UNDEFINED;
    s_cf.cw = s_cf.ch = 0;
    s_cf.bufSize = 0;
    s_cf.pending = false;
}

void rd__crack_fill_shutdown(void)
{
    countFree();
}

/* the count of a replay the GPU is past (now: the caller waited for it) */
static void harvest(bool now)
{
    if (!s_cf.pending || (!now && g_rd.replayCounter < s_cf.pendingReplay + RHI_FRAMES_IN_FLIGHT)) {
        return;
    }
    s_cf.pending = false;
    const uint8_t *m = (const uint8_t *)rhi_map_buffer(s_cf.buf);
    if (!m) {
        return;
    }
    uint32_t n = 0;
    for (uint32_t y = 0; y < s_cf.pendingH; y++) {
        const uint8_t *row = m + (size_t)y * s_cf.pendingPitch;
        for (uint32_t x = 0; x < s_cf.pendingW; x++) {
            n += row[x * 4u] + 256u * row[x * 4u + 1u];
        }
    }
    rhi_unmap_buffer(s_cf.buf);
    s_cf.counted++;
    s_cf.pixels += n;
    s_cf.last = n;
    s_cf.max = n > s_cf.max ? n : s_cf.max;
    s_cf.bCounted++;
    s_cf.bPixels += n;
    s_cf.bMax = n > s_cf.bMax ? n : s_cf.bMax;
}

void rd__crack_fill_wait(void)
{
    if (s_cf.pending && g_rd.hasDevice) {
        rhi_wait_idle();
        harvest(true);
    }
}

void rd__crack_fill_counts(RdCrackCounts *out)
{
    memset(out, 0, sizeof(*out));
    out->filled = s_cf.filled;
    out->counted = s_cf.counted;
    out->pixels = s_cf.pixels;
    out->last = s_cf.last;
    out->max = s_cf.max;
    out->skipped = s_cf.skipped;
    out->sx = s_cf.sx;
    out->sy = s_cf.sy;
    out->rx = s_cf.rx;
    out->ry = s_cf.ry;
    out->enabled = s_cf.enabled;
}

void rd__crack_fill_block(void)
{
    note();
    if (s_cf.bFilled) {
        rd__log("crack fill: %llu pixels filled over %llu replays counted of %llu filled (max %u a "
                "replay; scale %.4gx%.4g; reach %ux%u texels)",
                (unsigned long long)s_cf.bPixels, (unsigned long long)s_cf.bCounted,
                (unsigned long long)s_cf.bFilled, s_cf.bMax, (double)s_cf.sx, (double)s_cf.sy,
                s_cf.rx, s_cf.ry);
    }
    const uint64_t *k = s_cf.bSkip;
    /* MSAA is a choice (its own line once a run); the others are not */
    if (k[SKIP_DEPTH] | k[SKIP_PIPES] | k[SKIP_MEMORY]) {
        rd__log("crack fill: %llu fogged replays above 1x not filled (%llu under MSAA, %llu "
                "without a sampleable depth, %llu without its pipelines, %llu without memory for "
                "its copies)",
                (unsigned long long)(k[SKIP_MSAA] + k[SKIP_DEPTH] + k[SKIP_PIPES] + k[SKIP_MEMORY]),
                (unsigned long long)k[SKIP_MSAA], (unsigned long long)k[SKIP_DEPTH],
                (unsigned long long)k[SKIP_PIPES], (unsigned long long)k[SKIP_MEMORY]);
    }
    s_cf.bFilled = s_cf.bCounted = s_cf.bPixels = 0;
    s_cf.bMax = 0;
    memset(s_cf.bSkip, 0, sizeof(s_cf.bSkip));
}

static void skip(int why, const RdTargetRec *tc)
{
    s_cf.skipped++;
    s_cf.bSkip[why]++;
    if (!s_cf.skipSaid[why]) {
        s_cf.skipSaid[why] = true;
        rd__log("crack fill: not drawn on SCENE at scale %.4gx%.4g: %s (reported once)",
                (double)tc->sx, (double)tc->sy, kSkipWhy[why]);
    }
}

/* the count's target (cw x ch) and its readback buffer */
static bool countEnsure(uint32_t cw, uint32_t ch, uint32_t pitch)
{
    if (s_cf.countTex.id && s_cf.cw == cw && s_cf.ch == ch &&
        s_cf.bufSize >= (uint64_t)pitch * ch) {
        return true;
    }
    countFree();
    s_cf.countTex = rhi_create_texture(&(RhiTextureDesc){cw, ch, 1, RHI_FMT_RGBA8_UNORM,
                                                         RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                         "rd crack fill count", 0});
    s_cf.countState = RHI_STATE_UNDEFINED;
    s_cf.bufSize = (uint64_t)pitch * ch;
    s_cf.buf = rhi_create_buffer(&(RhiBufferDesc){s_cf.bufSize, RHI_BUF_COPY_DST | RHI_BUF_READBACK,
                                                  RHI_MEM_READBACK, "rd crack fill count"});
    if (!s_cf.countTex.id || !s_cf.buf.id) {
        countFree();
        return false;
    }
    s_cf.cw = cw;
    s_cf.ch = ch;
    return true;
}

/* one triangle over a w x h pass already begun: the pipeline of key k,
 * t1 and t2 (t1's aspect a1), the reach in DrawCB.param.xy */
static void fullscreen(RhiCommandList cl, const RdPipeKeyInt *k, RhiPipeline pipe, uint32_t w,
                       uint32_t h, RhiTexture t1, RhiViewAspect a1, RhiTexture t2)
{
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.param[0] = (float)s_cf.rx;
    cb.param[1] = (float)s_cf.ry;
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = t1;
    b[0].aspect = a1;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = t2;
    b[2].aspect = t2.id == g_rd.dummy.id ? RHI_ASPECT_COLOR : RHI_ASPECT_DEPTH;
    const RhiBindGroup g = rd__replay_bind_group(g_rd.layoutTex, b, 3);
    if (!g.id) {
        return;
    }
    rd__set_pipeline(cl, pipe, k);
    rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g);
    rd__replay_draw(cl, 3, 0, 1);
}

void rd__crack_fill(Replay *r, RdTargetRec *tc, const RdTargetRec *tz, RdDepthCopy *copy, bool msaa)
{
    harvest(false);
    /* SCENE's own fog above 1x under Enhanced; not the fog's self-test
     * (rd__fog_exact without the probe), which measures the depth read */
    if (!s_cf.enabled || !g_rd.hasDevice || g_rd.settings.preset != RD_PRESET_ENHANCED || !tc ||
        tz != tc || r->st.color != RD_TARGET_SCENE + 1u || !(tc->sx > 1.0f || tc->sy > 1.0f) ||
        (rd__fog_exact() && !rd__fog_probe_wanted())) {
        return;
    }
    if (msaa) {
        skip(SKIP_MSAA, tc);
        return;
    }
    if (!tc->withDepth || !tc->depth.id || !tc->depthSampled || !tc->color.id ||
        tc->format != RHI_FMT_RGBA8_UNORM) {
        skip(SKIP_DEPTH, tc);
        return;
    }
    const RdPipeKeyInt kFill = rd__crack_fill_key(0), kCount = rd__crack_fill_key(1);
    const RhiPipeline pFill =
        g_rd.fs[RD_FS_CRACK_FILL].id ? rd__get_pipeline(&kFill) : (RhiPipeline){0};
    const RhiPipeline pCount =
        g_rd.fs[RD_FS_CRACK_COUNT].id ? rd__get_pipeline(&kCount) : (RhiPipeline){0};
    if (!pFill.id || !pCount.id) {
        if (!rd__pipeline_gathering()) { /* a gathering replay names the keys only */
            skip(SKIP_PIPES, tc);
        }
        return;
    }
    const uint32_t w = tc->tw, h = tc->th;
    /* the pass before ends (the local read's scene pass among them); SCENE's
     * colour into its snapshot, its depth into the copy */
    if (!rd__replay_take_snap(r, tc, (RhiRect){0, 0, w, h})) {
        skip(SKIP_MEMORY, tc);
        return;
    }
    RhiCommandList cl = rd__replay_cl();
    const RhiTexture zc = rd__depth_copy_take(cl, tc, copy, "rd fog depth");
    if (!zc.id) {
        skip(SKIP_MEMORY, tc);
        return;
    }
    s_cf.sx = tc->sx;
    s_cf.sy = tc->sy;
    s_cf.rx = rd__crack_fill_reach(tc->sx);
    s_cf.ry = rd__crack_fill_reach(tc->sy);
    const uint32_t scene = r->st.color;

    /* the fill: SCENE and its depth, loaded and stored, stencil untouched */
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    rd__transition(cl, tc->depth, &tc->depthState, RHI_STATE_DEPTH_WRITE);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, w, h, r->perfPost);
    rhi_cmd_checkpoint(
        cl, rd__replay_pass_push(RD_PASSK_PASS, scene, w, h, scene, w, h, 0, r->perfPost));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.depth.texture = tc->depth;
    p.depth.depthLoad = RHI_LOAD_LOAD;
    p.depth.stencilLoad = RHI_LOAD_LOAD;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    fullscreen(cl, &kFill, pFill, w, h, tc->snap, RHI_ASPECT_COLOR, zc);
    rhi_cmd_end_render_pass(cl);
    r->writeSerial++; /* SCENE's colour and alpha changed (DATE's snapshot) */
    g_rdPerf.crackFillPasses++;
    s_cf.filled++;
    s_cf.bFilled++;

    /* the count, one replay in RD_CRACK_COUNT_EVERY with none in flight */
    if (s_cf.pending || !(s_cf.countAll || (s_cf.filled - 1u) % RD_CRACK_COUNT_EVERY == 0u)) {
        return;
    }
    const uint32_t cw = (w + CRACK_BLOCK - 1u) / CRACK_BLOCK,
                   ch = (h + CRACK_BLOCK - 1u) / CRACK_BLOCK;
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t pitch = (cw * 4u + a - 1u) / a * a;
    if (!countEnsure(cw, ch, pitch)) {
        return; /* the fill stands; this replay is not counted */
    }
    rd__transition(cl, s_cf.countTex, &s_cf.countState, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, scene, cw, ch, r->perfPost);
    rhi_cmd_checkpoint(cl,
                       rd__replay_pass_push(RD_PASSK_PASS, scene, cw, ch, 0, 0, 0, 0, r->perfPost));
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_cf.countTex;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel is written */
    p.colorCount = 1;
    p.width = cw;
    p.height = ch;
    rhi_cmd_begin_render_pass(cl, &p);
    fullscreen(cl, &kCount, pCount, cw, ch, zc, RHI_ASPECT_DEPTH, g_rd.dummy);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, s_cf.countTex, &s_cf.countState, RHI_STATE_COPY_SRC);
    rhi_cmd_copy_texture_to_buffer(cl, s_cf.countTex, RHI_ASPECT_COLOR, (RhiRect){0, 0, cw, ch},
                                   s_cf.buf, 0, pitch);
    s_cf.pending = true;
    s_cf.pendingReplay = g_rd.replayCounter;
    s_cf.pendingW = cw;
    s_cf.pendingH = ch;
    s_cf.pendingPitch = pitch;
    g_rdPerf.crackCountPasses++;
}
