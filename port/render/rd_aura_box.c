/* rd_aura_box.c: the aura's feedback read of the picture in two passes
 * (docs/port/WORK_BUFFERS.md, "The aura's feedback read"; [dev] aura_box =
 * auto|on|off, ICO_RD_AURA_BOX).
 *
 * The aura's copyCurrentFBToFeedBackArea (ico2/sugipon/src/staticBlur.c,
 * auraInspireAfter mode 2, while the game is not paused) reads the whole of
 * SCENE into FEED128 (W / 4 x 128 GS texels, at its GS size at every scale)
 * through the area box above 1x: fx_sprite.hlsl fx_sprite_texel, for each
 * FEED128 texel about 20 x 10 SCENE texels at the PowerVR phone's 4.617 x
 * 2.109 (Resolution Auto), one long loop a texel over few texels (27 136).
 * On that phone the pass is 4.9 ms where the reduction's 1:1 read of the
 * same SCENE is 0.72 ms.
 *
 * The box is separable with a rounding between its axes: per SCENE row the
 * row's R' = (16 R + Dx / 2) / Dx (R the row's weighted taps), then t =
 * (sum(wy R') + 8 Dy) / (16 Dy) over the rows.  The two passes split it at
 * that seam, in the same integers:
 *   A  ROWBOX, RGBA16_UINT, FEED128's width x SCENE's height (212 x 1080 on
 *      the phone): fx_aura_rows_ps writes, at texel (c, y), R' of FEED128
 *      column c's own box (its x taps, weights and Dx: the column's GS X,
 *      U and su as fx_sprite_ps computes them for c, so the edges that move
 *      by a 32nd from column to column are each column's own) on SCENE row
 *      y, every row 0 .. H - 1 (the range of fx_axis_y: every addressed
 *      row a box can touch).  Its pass is one triangle; columns outside
 *      the sprite's rect are discarded (never read).
 *   B  FEED128's own draw through fx_aura_sum_ps: fx_sprite_ps's coverage
 *      and V, the rows ay.first + j addressed (fx_axis_y), sum += ROWBOX(c,
 *      that row) wy(j) for j = 0, 1, ..., the rounding, then the texture
 *      function and everything after it as fx_sprite_ps.
 *
 * Why the bytes are the same (step by step against fx_sprite_texel's
 * FXF_ABOVE1X branch; the shader's comment above the two entries):
 *   1. The same DrawCB (rd_replay.c doBlurSpriteDraw's, bound to both
 *      passes), so the same flags, steps, UV rect, TEX0 size, t1 size and
 *      scale, crop, reciprocals and clamp/repeat sizes.
 *   2. Column c's X in pass A equals FEED128 texel c's: pos.x = c + 0.5 in
 *      both (pixel centres), and the FrameCB A binds (rd__frame_group_ex
 *      over ROWBOX's size) has FEED128's origin.x (2048 - gsW / 2, as
 *      rd_replay.c frameRefresh computes it), scale 1 (z.y, z.z) and no
 *      crop (vpo.z 0), as FEED128's own (a work buffer at its GS size,
 *      checked: sx = sy = 1, ox = 0).  At scale 1 every float operation of
 *      the X chain is exact (16 c + 0.5 and its floor, round(origin.x 16)),
 *      so X = ox + 16 c in both whatever the compiler contracts.
 *   3. U = u0 + gs_div_trunc_rcp(...) is the exact truncated quotient for
 *      every rounding of its float steps (gs_math.hlsli's proof), and su =
 *      round(float(U) s) - crop is one correctly rounded multiply and a
 *      round: the same su, so gs_resample_axis gives the same first tap,
 *      count, weights and Dx (integers from su and the step).
 *   4. A row's R' (fx_kernel_row) is fx_sprite_texel's inner loop and
 *      rounding: fx_texel(int2(ax.first + i, ay.first + j)) is
 *      fx_texel_at(fx_axis_x(ax.first + i), fx_axis_y(ay.first + j)); pass
 *      A's y is that fx_axis_y value, a row in 0 .. H - 1, which ROWBOX
 *      holds.  The taps are SCENE's bytes k either way (the UNORM load's
 *      floor(k / 255 * 255 + 0.5) = k; fx_sprite_u_ps reads k itself).
 *   5. R' <= 4080 (R <= 255 Dx): RGBA16_UINT stores it as it is; pass B
 *      reads it back as an integer (the UINT build, t1 ROWBOX).
 *   6. Pass B's V, ay and Dy are fx_sprite_ps's (its own coverage and V,
 *      the same FrameCB, bindDraw's); it adds R' wy(j) in the same order j
 *      = 0, 1, ..., and integer addition is associative anyway: sum(wy R')
 *      <= 24576 x 4080 < 2^32 (Dy at most the B-spline's 24576, the box's
 *      at most 1024), no wrap.  t, TFX, the alpha test, the blend and FBA
 *      are then the same function of the same t.
 * So every FEED128 texel gets the same 16x-precision sum and the same
 * byte.  The cost: about the same taps (one a ROWBOX texel's row, 4.6 M on
 * the phone, then about 10 reads a FEED128 texel), ROWBOX stored and
 * loaded once (212 x 1080 x 8 bytes, 1.8 MB each way), and one render pass
 * more (ROWBOX's: "aura ROWBOX 212x1080 1 draws" in the pass list); what
 * changes is the loop: 229 k short ones (20 taps) and 27 k of 10 reads for
 * 27 k long ones (about 200).
 *
 * Where: only the aura's SCENE -> FEED128 sprite (the target and source
 * rd_aura_kernel.c names), above 1x (blurAbove1xSteps not 0: at 1x the
 * sprite takes the GS's own bilinear, four taps, and there is no loop to
 * shorten), with the box kernel (not under [dev] aura_kernel = gs, which
 * drops the steps), without a destination read or a depth target, the
 * colour mask whole, FEED128 at its GS size.  On by default on the
 * tile-based classes (RdProfile.auraBox: Adreno, Mali, PowerVR, Apple,
 * another tiler), off on desktop Vulkan, D3D12, D3D11 and OpenGL (not
 * measured there); [dev] aura_box = on or off over it (ICO_RD_AURA_BOX=1
 * or 0, auto the default).  Its two keys are in the start-up set while it
 * is on (rd__aura_box_keys, the fx family).
 *
 * The lines:
 *   rd: aura box: the feedback read of the picture above 1x in two passes
 *       (...) (<why>)                     (start; or "in one pass (<why>)")
 *   rd: profile: ...; aura box: two passes|one pass   (the profile line)
 *   rd: aura box: ROWBOX WxH made (RGBA16 UINT, N MB)  (made or remade)
 *   rd: aura box: the feedback read drawn in one pass: <reason>
 *       (reported once)                                (events)
 *   rd: aura box: N feedback reads in two passes (ROWBOX WxH), M in one
 *       pass (...)                                     (10 s summary) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

enum { FALL_DST, FALL_DEPTH, FALL_MASK, FALL_SCALE, FALL_PIPES, FALL_MEMORY, FALL_COUNT };

static const char *const kFallWhy[FALL_COUNT] = {
    "the sprite reads its destination or itself",
    "the sprite has a depth target",
    "the sprite's colour mask is not whole",
    "FEED128 is not at its GS size",
    "its pipelines are missing",
    "no memory for ROWBOX or its texture groups",
};

static struct {
    int forced; /* tests: -1 none, else 0 or 1 */
    bool on;
    char why[160];
    RhiTexture tex; /* ROWBOX */
    RhiState state;
    uint32_t w, h;
    uint64_t two, one;
    uint64_t bTwo, bOne[FALL_COUNT];
    bool fallSaid[FALL_COUNT];
} s_ab = {.forced = -1};

int rd__aura_box_pick(const char *env, int def)
{
    if (!env || !env[0] || strcmp(env, "auto") == 0) {
        return def ? 1 : 0;
    }
    if (strcmp(env, "1") == 0 || strcmp(env, "true") == 0 || strcmp(env, "on") == 0 ||
        strcmp(env, "yes") == 0) {
        return 1;
    }
    if (strcmp(env, "0") == 0 || strcmp(env, "false") == 0 || strcmp(env, "off") == 0 ||
        strcmp(env, "no") == 0) {
        return 0;
    }
    return def ? 1 : 0; /* another word: auto (the start line says so) */
}

/* the switch and its reason (s_ab.why) */
static bool decide(void)
{
    const char *env = getenv("ICO_RD_AURA_BOX");
    const bool def = g_rd.profile.auraBox;
    int want;
    if (s_ab.forced >= 0) {
        want = s_ab.forced;
        snprintf(s_ab.why, sizeof s_ab.why, "the tests' switch");
    } else {
        want = rd__aura_box_pick(env, def);
        const bool auto_ = !env || !env[0] || strcmp(env, "auto") == 0;
        const bool known = auto_ || rd__aura_box_pick(env, 0) == rd__aura_box_pick(env, 1);
        if (!known) {
            snprintf(s_ab.why, sizeof s_ab.why,
                     "ICO_RD_AURA_BOX=%.32s is not auto, on or off: the device profile's, %s", env,
                     def ? "a tile-based GPU" : "an immediate-mode GPU");
        } else if (!auto_) {
            snprintf(s_ab.why, sizeof s_ab.why, "[dev] aura_box = %s, ICO_RD_AURA_BOX=%.32s",
                     want ? "on" : "off", env);
        } else if (def) {
            snprintf(s_ab.why, sizeof s_ab.why,
                     "a tile-based GPU's default; [dev] aura_box = off draws it in one");
        } else {
            snprintf(s_ab.why, sizeof s_ab.why,
                     "an immediate-mode GPU's default; [dev] aura_box = on draws it in two");
        }
    }
    if (want && (!g_rd.hasDevice || !g_rd.fs[RD_FS_FX_AURA_ROWS].id ||
                 !g_rd.fs[RD_FS_FX_AURA_SUM].id || !g_rd.vs[RD_VS_FX_RECT].id)) {
        snprintf(s_ab.why, sizeof s_ab.why, "asked for, but its shaders are not in this build");
        want = 0;
    }
    return want != 0;
}

const char *rd__aura_box_profile_words(void)
{
    const int forced = s_ab.forced;
    char why[sizeof s_ab.why];
    memcpy(why, s_ab.why, sizeof why);
    const bool on = decide();
    memcpy(s_ab.why, why, sizeof why); /* rd__aura_box_init keeps its own */
    s_ab.forced = forced;
    return on ? "aura box: two passes" : "aura box: one pass";
}

static void note(void)
{
    rd__log("aura box: the feedback read of the picture above 1x in %s (%s)",
            s_ab.on ? "two passes (the box's rows into ROWBOX, an integer target of FEED128's "
                      "width and the picture's height, then FEED128's sum of them: the same "
                      "bytes)"
                    : "one pass",
            s_ab.why);
}

void rd__aura_box_init(void)
{
    s_ab.on = decide();
    s_ab.two = s_ab.one = s_ab.bTwo = 0;
    memset(s_ab.bOne, 0, sizeof s_ab.bOne);
    memset(s_ab.fallSaid, 0, sizeof s_ab.fallSaid);
    note();
}

bool rd__aura_box_on(void)
{
    return s_ab.on;
}

void rd__aura_box_force(int mode)
{
    s_ab.forced = mode < 0 ? -1 : (mode ? 1 : 0);
    s_ab.on = decide();
    note();
}

uint32_t rd__aura_box_keys(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    if (!s_ab.on || !g_rd.fs[RD_FS_FX_AURA_ROWS].id || !g_rd.fs[RD_FS_FX_AURA_SUM].id) {
        return n;
    }
    /* the rows: one triangle into ROWBOX, no blend, no depth */
    const RdPipeKeyInt rows = rd__post_key(RD_VS_FX_RECT, RD_FS_FX_AURA_ROWS, RHI_FMT_RGBA16_UINT);
    n = rd__add_pipe_key(out, max, n, &rows);
    /* the sum: the aura's sprite's one-pass key (rd__blur_key: no depth
     * target, the colour mask whole) with fx_aura_sum_ps */
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.colorMask = 0xF;
    RdPipeKeyInt sum = rd__blur_key(&s, RHI_FMT_RGBA8_UNORM, RHI_FMT_UNKNOWN, NULL, 0);
    sum.fs = RD_FS_FX_AURA_SUM;
    return rd__add_pipe_key(out, max, n, &sum);
}

static bool fall(int why)
{
    s_ab.one++;
    s_ab.bOne[why]++;
    if (!s_ab.fallSaid[why]) {
        s_ab.fallSaid[why] = true;
        rd__log("aura box: the feedback read drawn in one pass: %s (reported once)", kFallWhy[why]);
    }
    return false;
}

static void texFree(void)
{
    if (s_ab.tex.id && g_rd.hasDevice) {
        rhi_destroy_texture(s_ab.tex);
    }
    s_ab.tex = (RhiTexture){0};
    s_ab.state = RHI_STATE_UNDEFINED;
    s_ab.w = s_ab.h = 0;
}

void rd__aura_box_shutdown(void)
{
    texFree();
}

/* ROWBOX at w x h */
static bool ensure(uint32_t w, uint32_t h)
{
    if (s_ab.tex.id && s_ab.w == w && s_ab.h == h) {
        return true;
    }
    texFree();
    s_ab.tex = rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA16_UINT,
                                                    RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                    "rd aura ROWBOX", 0});
    if (!s_ab.tex.id) {
        return false;
    }
    s_ab.w = w;
    s_ab.h = h;
    rd__log("aura box: ROWBOX %ux%u made (RGBA16 UINT, %.1f MB)", w, h,
            (double)w * (double)h * 8.0 / (1024.0 * 1024.0));
    return true;
}

/* a texture group of t1 (and the nearest sampler) and t2 */
static RhiBindGroup group(RhiTexture t1, RhiTexture t2)
{
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = t1;
    b[0].aspect = RHI_ASPECT_COLOR;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = t2;
    b[2].aspect = RHI_ASPECT_COLOR;
    return rd__replay_bind_group(g_rd.layoutTex, b, 3);
}

bool rd__aura_box_draw(Replay *r, DrawSetup *ds, const RdPipeKeyInt *key, const IcoDrawCB *cb,
                       const RdTargetRec *src, RhiTexture srcTex, uint32_t steps, int other)
{
    RdTargetRec *tc = ds ? ds->tc : NULL;
    const uint32_t need = RD_FXF_TEXTURED | RD_FXF_ABOVE1X;
    if (!s_ab.on || !steps || !g_rd.hasDevice || !tc || !src || !srcTex.id ||
        tc != rd__target_rec(rd_target(RD_TARGET_FEED128).id) ||
        src != rd__target_rec(rd_target(RD_TARGET_SCENE).id) || (cb->mode[0] & need) != need) {
        return false; /* not the aura's read above 1x: nothing to say */
    }
    if (other || (cb->mode[0] & (RD_FXF_DST | RD_FXF_DATE)) != 0u ||
        key->dst != RHI_DST_READ_NONE) {
        return fall(FALL_DST);
    }
    if (key->depthFmt != RHI_FMT_UNKNOWN || key->fs != RD_FS_FX_SPRITE) {
        return fall(FALL_DEPTH);
    }
    if (key->gs.colorMask != 0xF) {
        return fall(FALL_MASK);
    }
    if (tc->sx != 1.0f || tc->sy != 1.0f || tc->ox != 0.0f) {
        return fall(FALL_SCALE);
    }
    const RdPipeKeyInt kRows = rd__post_key(RD_VS_FX_RECT, RD_FS_FX_AURA_ROWS, RHI_FMT_RGBA16_UINT);
    RdPipeKeyInt kSum = *key;
    kSum.fs = RD_FS_FX_AURA_SUM;
    const RhiPipeline pRows = rd__get_pipeline(&kRows);
    const RhiPipeline pSum = rd__get_pipeline(&kSum);
    if (!pRows.id || !pSum.id) {
        if (rd__pipeline_gathering()) {
            return false; /* a gathering replay names the keys only */
        }
        return fall(FALL_PIPES);
    }
    /* ROWBOX: FEED128's columns, SCENE's rows (the clamp of fx_axis_y,
     * DrawCB.tex.w = SCENE's texture height) */
    const uint32_t w = tc->tw, h = src->th;
    rd__replay_end_pass(r); /* the pass before (usually ended for SCENE's read already) */
    if (!ensure(w, h)) {
        return fall(FALL_MEMORY);
    }
    const RhiBindGroup gA = group(srcTex, g_rd.dummy);
    const RhiBindGroup gB = group(s_ab.tex, s_ab.tex);
    if (!gA.id || !gB.id) {
        return fall(FALL_MEMORY);
    }
    RhiCommandList cl = rd__replay_cl();

    /* pass A: the rows into ROWBOX */
    rd__transition(cl, s_ab.tex, &s_ab.state, RHI_STATE_RENDER_TARGET);
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, RD_PERF_ID_ROWBOX, w, h, r->perfPost);
    rhi_cmd_checkpoint(
        cl, rd__replay_pass_push(RD_PASSK_PASS, RD_PERF_ID_ROWBOX, w, h, 0, 0, 0, 0, r->perfPost));
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_ab.tex;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel pass B reads is written */
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(w, h);
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(cl, &all);
    /* FEED128's origin.x (rd_replay.c frameRefresh), scale 1, no crop */
    const float ox = 2048.0f - (float)(r->st.gsW >> 1);
    const float oy = 2048.0f - (float)(r->st.gsH >> 1);
    const RdUniform frame =
        rd__frame_group_ex(w, h, ox, oy, rd__target_z_scale(ds->tdId), 1.0f, 1.0f, 1.0f);
    rd__set_pipeline(cl, pRows, &kRows);
    rd__bind_uniform(cl, 0, frame);
    rd__bind_uniform(cl, 1, rd__draw_group(cb));
    rhi_cmd_set_bind_group(cl, 2, gA);
    rd__replay_draw(cl, 3, 0, 1);
    rd__stats_here()->draws++;
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, s_ab.tex, &s_ab.state, RHI_STATE_SHADER_READ);

    /* pass B: FEED128's draw, its pass, FrameCB and scissor as bindDraw
     * has them for the one-pass draw */
    if (!rd__replay_bind_draw(r, ds).id) {
        return true; /* the scissor leaves nothing to draw, as in one pass */
    }
    rd__set_pipeline(cl, pSum, &kSum);
    rd__bind_uniform(cl, 0, r->frameBG);
    rd__bind_uniform(cl, 1, rd__draw_group(cb));
    rhi_cmd_set_bind_group(cl, 2, gB);
    rd__replay_draw(cl, 3, 0, 1);
    rd__stats_here()->draws++;
    if (key->gs.colorMask & 8) {
        r->writeSerial++;
    }
    g_rd.above1xDraws++;
    g_rdPerf.auraBoxDraws++;
    s_ab.two++;
    s_ab.bTwo++;
    return true;
}

void rd__aura_box_block(void)
{
    uint64_t one = 0;
    for (int i = 0; i < FALL_COUNT; i++) {
        one += s_ab.bOne[i];
    }
    if (s_ab.bTwo || one) {
        const uint64_t *k = s_ab.bOne;
        rd__log("aura box: %llu feedback reads in two passes (ROWBOX %ux%u), %llu in one pass "
                "(%llu reading their destination, %llu with a depth target, %llu with a part "
                "mask, %llu with FEED128 scaled, %llu without the pipelines, %llu without "
                "memory)",
                (unsigned long long)s_ab.bTwo, s_ab.w, s_ab.h, (unsigned long long)one,
                (unsigned long long)k[FALL_DST], (unsigned long long)k[FALL_DEPTH],
                (unsigned long long)k[FALL_MASK], (unsigned long long)k[FALL_SCALE],
                (unsigned long long)k[FALL_PIPES], (unsigned long long)k[FALL_MEMORY]);
    }
    s_ab.bTwo = 0;
    memset(s_ab.bOne, 0, sizeof s_ab.bOne);
}

void rd__aura_box_counts(uint64_t *two, uint64_t *one)
{
    if (two) {
        *two = s_ab.two;
    }
    if (one) {
        *one = s_ab.one;
    }
}
