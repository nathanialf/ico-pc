/* rd_shadow_tile.c: the shadow composites culled outside a GPU tile mask
 * (rd_shadow_tile.h; docs/port/RENDER_POST.md "The shadow tiles").
 *
 * Why.  rd_shadow_box.c's CPU box is the volumes' extent; the volumes sweep
 * open space, so on the owner's phone the box is the whole picture at the
 * bridge and in the cage while the shadow that lands covers about 1.3 % of
 * SHADOW0 (the rc3 bridge frame v2809: alpha above 0 in (144,82)-(263,193)
 * of 424x256).  Where a shadow lands is known only on the GPU, after the
 * levels are drawn.
 *
 * The mask (shadow_tile_mask_ps).  Once a replay, at the first composite
 * that takes the tiles and before any pass on its target is open (the
 * levels' passes are on SHADOW0-2), one pass a level the replay's boxed
 * composites read: a texel per RD_ST_BLOCK x RD_ST_BLOCK level texels, 1
 * where any level texel within RD_ST_MARGIN of the block (RD_ST_MARGIN_BSPLINE
 * with the B-spline composites) holds alpha above 0, by integer loads (an
 * 8-bit alpha above 0 reads above 0.0 exactly), the indices clamped or
 * wrapped as the first composite's sampler (the state's wrap: REPEAT is
 * taken as wrapped even where the replay's screen-copy rule clamps, which
 * only widens the mask).  (2 + 2 x 3)^2 = 64 loads a mask texel.
 *
 * The set-up (rd__shadow_tile_take, in place of rd__replay_date_take for
 * such a composite, in its own pass, over the same area): CLEAR2 writes 0
 * into bits 6 and 7 of the stencil; TILE_MARK writes 1 into bit 7 where
 * shadow_tile_mark_ps finds the mask block of the pixel's UV 0 (a dead
 * tile); after the colour barrier, SET_T writes 1 into bit 6 where the
 * alpha's MSB is set and bit 7 is 0; the composite's passes test TEST0_T or
 * TEST1_T: bit 6 as before and bit 7 0.  The bit-7 read masks and the
 * reference 0x40 give exactly TEST0's and TEST1's verdict where bit 7 is 0
 * (rd.h).  The set-up is always taken whole over its area (no reuse), and
 * no later draw reuses it (dateStFor 0): every later DATE draw takes its
 * own, which writes bit 6 over its area again.
 *
 * Exact.  (1) A dead tile's pixel: its mark UV is the composite's sprite
 * map at the pixel's centre (the same affine the GPU interpolates, up to
 * rounding and the half texel of FrameCB's origin: under a level texel),
 * its block's texels and every texel within RD_ST_MARGIN of it hold alpha
 * 0; the composite's bilinear taps lie within one texel of its own UV
 * (two with the B-spline, plus the GS pixel's snap above 1x), so all hold
 * alpha 0: As is 0, and the composite's form (rd_shadow_box.c: an alpha
 * test that fails on 0, AFAIL KEEP, no Z write) writes nothing there; the
 * stencil now culls the fragment, which writes nothing either.  (2) A live
 * tile's pixel: bit 7 is 0, bit 6 as rd__replay_date_take writes it, the
 * composite's draw the same geometry, interpolation and shader: the same
 * bytes.  (3) Bit 6 in a dead tile is 0 rather than the alpha's MSB: only
 * the composite reads it (no reuse), and it is culled there.  (4) Bit 7 is
 * read by these keys alone; bits 0-5 (the count) are never touched.  So
 * every target is the same bytes; no DIVERGENCES row.
 *
 * Where.  Off by default (RD_SHADOW_TILE_DEFAULT 0: the owner's PowerVR
 * phone measured it slower); with the default 1, on for the PowerVR class
 * and the desktop classes where the start-up self-test passes, off on the
 * others (the Adreno and
 * Mali classes in this release, issues 179, 211, 123; Apple; another
 * tile-based GPU); only where DATE takes the stencil bit (Vulkan with the
 * local read: elsewhere the composites keep the R8 snapshot, which this
 * does not cull).  ICO_RD_SHADOW_TILE=0|1 (tests): off or on whatever the
 * class and the self-test.
 *
 * The self-test (rd__shadow_tile_selftest): a frame of its own at SCENE's
 * size: list 0 a ground (its top half's alpha MSB 1, its bottom half's 0),
 * list 3 the count of one volume on WORK2 with SCENE's depth, SHADOW0 drawn
 * from it, one composite into SCENE from SHADOW0 (the composite's TEST word
 * 0x3400D, LERP_AS FIX 0, MODULATE), list 4 a DATE DATM 1 sprite over the
 * whole frame (a later reader of bit 6); replayed with the tiles off and
 * on, MSAA held off; SCENE read back whole and compared byte for byte, the
 * tiled replay having drawn the composite over the mask with dead tiles
 * (the mask read back). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"
#include "rd_shadow_box.h"
#include "rd_shadow_tile.h"

enum { ST_NOT_RUN, ST_OK, ST_FAIL, ST_ERROR };

/* The default without ICO_RD_SHADOW_TILE: off on every class.  The owner's
 * Pixel (PowerVR, rc5) ran it with the self-test passing and list 3's class
 * 1.7 ms slower: on that tile-based GPU the composites' dead fragments were
 * already cheap (rejected after one texture read), and the three mark
 * draws and the per-pixel stencil test cost more than they saved.  1 turns
 * it on again where the class allows and the self-test passes. */
#define RD_SHADOW_TILE_DEFAULT 0

static struct {
    uint8_t test;
    char words[224];
    int testing; /* the self-test's replays: 1 off, 2 on, 0 none */

    struct {
        uint32_t target, replay;
        RhiTexture tex;
        RhiState state;
        uint32_t w, h, lw, lh;
        uint8_t repS, repT, ok;
    } mask[RD_ST_MASKS];

    /* the composite between rd__shadow_tile_prepare and _take */
    uint32_t curOrd;
    int curMask;
    float affine[4];
    uint32_t refusals;
    bool said;
} s_t = {.curOrd = UINT32_MAX};

static int envSwitch(void)
{
    const char *e = getenv("ICO_RD_SHADOW_TILE");
    return e && strcmp(e, "1") == 0 ? 1 : e && strcmp(e, "0") == 0 ? 0 : -1;
}

static bool modules(void)
{
    return g_rd.hasDevice && g_rd.profile.dateStencil && g_rd.fs[RD_FS_SHADOW_TILE_MASK].id &&
           g_rd.fs[RD_FS_SHADOW_TILE_MARK].id && g_rd.fs[RD_FS_DATE_CLEAR].id &&
           g_rd.fs[RD_FS_DATE_STENCIL].id;
}

bool rd__shadow_tile_on(void)
{
    if (s_t.testing) {
        return s_t.testing == 2;
    }
    const int env = envSwitch();
    if (env >= 0) {
        return env == 1;
    }
    return RD_SHADOW_TILE_DEFAULT != 0 && rd__profile_tile_class() != 0 && s_t.test == ST_OK;
}

bool rd__shadow_tile_keys_wanted(void)
{
    const int env = envSwitch();
    return modules() && env != 0 &&
           (env == 1 || (RD_SHADOW_TILE_DEFAULT != 0 && rd__profile_tile_class() != 0));
}

RdPipeKeyInt rd__shadow_tile_mask_key(void)
{
    return rd__post_key(RD_VS_BLIT, RD_FS_SHADOW_TILE_MASK, RHI_FMT_RGBA8_UNORM);
}

void rd__shadow_tile_test_key(RdPipeKeyInt *k)
{
    if (k->gs.stencil == RD_STENCIL_DATE_TEST0) {
        k->gs.stencil = RD_STENCIL_DATE_TEST0_T;
    } else if (k->gs.stencil == RD_STENCIL_DATE_TEST1) {
        k->gs.stencil = RD_STENCIL_DATE_TEST1_T;
    }
}

static const char *whyWords(int why)
{
    switch (why) {
    case RD_ST_WHY_OFF:
        return "off on this device (the class, the self-test or ICO_RD_SHADOW_TILE=0)";
    case RD_ST_WHY_PASS:
        return "a pass on the composite's target was open: no mask drawn";
    case RD_ST_WHY_MASK:
        return "the mask could not be made";
    case RD_ST_WHY_FORM:
        return "not one sprite, a degenerate map or another wrap than the mask's";
    case RD_ST_WHY_SETUP:
        return "the tiled set-up's pipelines or the readable pass missing";
    default:
        return "?";
    }
}

static void whole(int why)
{
    g_rdPerf.shadowTileWhy[why]++;
    if (why == RD_ST_WHY_OFF) {
        return;
    }
    s_t.refusals++;
    if (s_t.refusals <= 8u || s_t.refusals % 100u == 0u) {
        rd__log("shadow tiles: a composite drawn whole: %s (command %u; %u so far, the first 8 "
                "then every 100th)",
                whyWords(why), g_rdStoreOrd, s_t.refusals);
    }
}

static uint32_t margin(void)
{
    return g_rd.profile.shadowBspline ? RD_ST_MARGIN_BSPLINE : RD_ST_MARGIN;
}

/* the mask of level target src into slot i (outside any pass) */
static bool maskDraw(Replay *r, int i, uint32_t src, bool repS, bool repT)
{
    RdTargetRec *ts = rd__target_rec(src);
    s_t.mask[i].target = src;
    s_t.mask[i].replay = g_rd.replayCounter;
    s_t.mask[i].ok = 0;
    if (!ts || !ts->color.id || ts->tw != ts->w || ts->th != ts->h || ts->w == 0 || ts->h == 0 ||
        ts->format != RHI_FMT_RGBA8_UNORM) {
        return false;
    }
    const uint32_t mw = (ts->w + RD_ST_BLOCK - 1u) / RD_ST_BLOCK;
    const uint32_t mh = (ts->h + RD_ST_BLOCK - 1u) / RD_ST_BLOCK;
    if (!s_t.mask[i].tex.id || s_t.mask[i].w != mw || s_t.mask[i].h != mh) {
        if (s_t.mask[i].tex.id) {
            rhi_destroy_texture(s_t.mask[i].tex);
        }
        s_t.mask[i].tex = rhi_create_texture(&(RhiTextureDesc){
            mw, mh, 1, RHI_FMT_RGBA8_UNORM,
            RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC, "rd shadow tile mask", 0});
        s_t.mask[i].state = RHI_STATE_UNDEFINED;
        s_t.mask[i].w = mw;
        s_t.mask[i].h = mh;
        if (!s_t.mask[i].tex.id) {
            return false;
        }
    }
    const RdPipeKeyInt k = rd__shadow_tile_mask_key();
    RhiPipeline pipe = rd__get_pipeline_held(&k);
    if (!pipe.id) {
        return false;
    }
    RhiCommandList cl = rd__replay_cl();
    rd__transition(cl, ts->color, &ts->colorState, RHI_STATE_SHADER_READ);
    rd__transition(cl, s_t.mask[i].tex, &s_t.mask[i].state, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_t.mask[i].tex;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* every texel is written */
    p.colorCount = 1;
    p.width = mw;
    p.height = mh;
    rd__perf_pass(cl, RD_PERF_PASS_RENDER, src, mw, mh, r->perfPost);
    rhi_cmd_checkpoint(cl,
                       rd__replay_pass_push(RD_PASSK_PASS, src, mw, mh, 0, 0, 0, 0, r->perfPost));
    rhi_cmd_begin_render_pass(cl, &p);
    (void)rd__get_pipeline(&k);
    rd__replay_viewport(mw, mh);
    const RhiRect all = {0, 0, mw, mh};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.aux0[0] = RD_ST_BLOCK;
    cb.aux0[1] = margin();
    cb.aux0[2] = ts->w;
    cb.aux0[3] = ts->h;
    cb.aux1[0] = repS ? 1u : 0u;
    cb.aux1[1] = repT ? 1u : 0u;
    rd__set_pipeline(cl, pipe, &k);
    rd__bind_uniform(cl, 0, rd__frame_group(mw, mh, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(
        cl, 2,
        rd__tex_group(ts->color, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                             RD_WRAP_CLAMP)));
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, s_t.mask[i].tex, &s_t.mask[i].state, RHI_STATE_SHADER_READ);
    s_t.mask[i].lw = ts->w;
    s_t.mask[i].lh = ts->h;
    s_t.mask[i].repS = repS;
    s_t.mask[i].repT = repT;
    s_t.mask[i].ok = 1;
    g_rdPerf.shadowTileMasks++;
    return true;
}

/* this replay's mask of level src: its slot, -1 none */
static int maskFind(uint32_t src)
{
    for (int i = 0; i < RD_ST_MASKS; i++) {
        if (s_t.mask[i].replay == g_rd.replayCounter && s_t.mask[i].target == src) {
            return i;
        }
    }
    return -1;
}

static bool anyMaskThisReplay(void)
{
    for (int i = 0; i < RD_ST_MASKS; i++) {
        if (s_t.mask[i].replay == g_rd.replayCounter && s_t.mask[i].target) {
            return true;
        }
    }
    return false;
}

static int32_t clamp16(int32_t v)
{
    return v < 0 ? 0 : (v > 0xFFFF ? 0xFFFF : v);
}

void rd__shadow_tile_prepare(Replay *r, DrawSetup *ds, const RdScreenVtx *v, uint32_t n)
{
    uint32_t src = 0;
    if (!ds->dateStencil || r->dateDstNow || !rd__shadow_box_composite(&src)) {
        return; /* not a boxed composite on DATE's stencil path */
    }
    if (!rd__shadow_tile_on() || !modules()) {
        if (envSwitch() == 0) {
            whole(RD_ST_WHY_OFF); /* the tests' switch: counted */
        }
        return; /* off by default: not counted, no 10 s clause */
    }
    const bool repS = r->st.ds.wrap.s == RD_WRAP_REPEAT, repT = r->st.ds.wrap.t == RD_WRAP_REPEAT;
    int mi = maskFind(src);
    if (mi < 0) {
        if (anyMaskThisReplay() || (r->passOpen && r->passColor == r->st.color)) {
            /* the masks are drawn once, before the composites' pass */
            whole(RD_ST_WHY_PASS);
            return;
        }
        uint32_t srcs[RD_ST_MASKS];
        const unsigned ns = rd__shadow_box_composite_sources(g_rdStoreOrd, srcs, RD_ST_MASKS);
        rd__replay_end_pass(r); /* the levels' pass (it ends at this draw's pass anyway) */
        for (unsigned k = 0; k < ns && k < RD_ST_MASKS; k++) {
            (void)maskDraw(r, (int)k, srcs[k], repS, repT);
        }
        mi = maskFind(src);
    }
    if (mi < 0 || !s_t.mask[mi].ok) {
        whole(RD_ST_WHY_MASK);
        return;
    }
    const RdTargetRec *tc = ds->tc;
    if (n != 2 || s_t.mask[mi].repS != repS || s_t.mask[mi].repT != repT || !tc ||
        !(tc->sx > 0.0f) || !(tc->sy > 0.0f)) {
        whole(RD_ST_WHY_FORM);
        return;
    }
    /* the sprite's map in the level's texels per target texel: GS pixel g of
     * the target's texel centre p is (p + ox) / sx (rd__gs_texel_x), and the
     * sprite takes u = ua + (g - xa) (ub - ua) / (xb - xa) */
    const double ox16 = 16.0 * (2048.0 - (double)(r->st.gsW >> 1));
    const double oy16 = 16.0 * (2048.0 - (double)(r->st.gsH >> 1));
    const double xa = ((double)clamp16(v[0].x) - ox16) / 16.0,
                 xb = ((double)clamp16(v[1].x) - ox16) / 16.0;
    const double ya = ((double)clamp16(v[0].y) - oy16) / 16.0,
                 yb = ((double)clamp16(v[1].y) - oy16) / 16.0;
    const double uo = (double)r->st.uvOffset[0] * s_t.mask[mi].lw;
    const double vo = (double)r->st.uvOffset[1] * s_t.mask[mi].lh;
    const double ua = v[0].s / 16.0 + uo, ub = v[1].s / 16.0 + uo;
    const double va = v[0].t / 16.0 + vo, vb = v[1].t / 16.0 + vo;
    if (xb == xa || yb == ya) {
        whole(RD_ST_WHY_FORM);
        return;
    }
    const double du = (ub - ua) / (xb - xa), dv = (vb - va) / (yb - ya);
    s_t.affine[0] = (float)(du / tc->sx);
    s_t.affine[1] = (float)(ua + ((double)tc->ox / tc->sx - xa) * du);
    s_t.affine[2] = (float)(dv / tc->sy);
    s_t.affine[3] = (float)(va - ya * dv);
    s_t.curOrd = g_rdStoreOrd;
    s_t.curMask = mi;
    ds->shadowTile = 1;
    r->tileNow = 1;
}

bool rd__shadow_tile_take(Replay *r, const DrawSetup *ds)
{
    RdTargetRec *tc = ds->tc;
    const int mi = s_t.curMask;
    if (s_t.curOrd != g_rdStoreOrd || mi < 0 || mi >= RD_ST_MASKS || !s_t.mask[mi].ok) {
        r->tileNow = 0;
        return rd__replay_date_take(r, ds);
    }
    const RdPipeKeyInt kc = rd__date_setup_key(RD_STENCIL_DATE_CLEAR2, tc->format);
    const RdPipeKeyInt km = rd__date_setup_key(RD_STENCIL_TILE_MARK, tc->format);
    const RdPipeKeyInt ks = rd__date_setup_key(RD_STENCIL_DATE_SET_T, tc->format);
    RhiPipeline pc = rd__get_pipeline(&kc), pm = rd__get_pipeline(&km), ps = rd__get_pipeline(&ks);
    if ((!pc.id || !pm.id || !ps.id) && rd__pipeline_gathering()) {
        return true; /* the keys named; nothing is drawn while gathering */
    }
    RhiBinding b[2];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_INPUT_ATTACHMENT;
    b[0].texture = tc->color;
    b[1].slot = 2;
    b[1].type = RHI_BIND_SAMPLED_TEXTURE;
    b[1].texture = g_rd.dummy;
    const RhiBindGroup gl = rd__replay_bind_group(g_rd.layoutLocal, b, 2);
    const RhiSampler nearest =
        rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    const RhiBindGroup gt = rd__tex_group(g_rd.dummy, nearest);
    const RhiBindGroup gm = rd__tex_group(s_t.mask[mi].tex, nearest);
    if (!pc.id || !pm.id || !ps.id || !gl.id || !gt.id || !gm.id || !r->passOpen ||
        !r->passReadable) {
        whole(RD_ST_WHY_SETUP);
        r->tileNow = 0;
        return rd__replay_date_take(r, ds);
    }
    RhiCommandList cl = rd__replay_cl();
    RhiRect take = {0, 0, tc->tw, tc->th};
    if (!ds->dateWantAll) {
        const int64_t x0 = ds->dateWant.x > 0 ? ds->dateWant.x : 0;
        const int64_t y0 = ds->dateWant.y > 0 ? ds->dateWant.y : 0;
        int64_t x1 = (int64_t)ds->dateWant.x + ds->dateWant.w;
        int64_t y1 = (int64_t)ds->dateWant.y + ds->dateWant.h;
        x1 = x1 > (int64_t)tc->tw ? (int64_t)tc->tw : x1;
        y1 = y1 > (int64_t)tc->th ? (int64_t)tc->th : y1;
        if (x1 > x0 && y1 > y0) {
            take = (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
        }
    }
    rd__replay_flush_run(r); /* the run's draws before the stencil changes */
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = (float)tc->tw;
    cb.uvRect[3] = (float)tc->th;
    cb.tex[0] = (float)tc->tw;
    cb.tex[1] = (float)tc->th;
    cb.tex[2] = 1.0f / (float)tc->tw;
    cb.tex[3] = 1.0f / (float)tc->th;
    IcoDrawCB cm = cb;
    memcpy(cm.param, s_t.affine, sizeof(cm.param));
    cm.aux0[0] = RD_ST_BLOCK;
    cm.aux0[1] = s_t.mask[mi].w;
    cm.aux0[2] = s_t.mask[mi].h;
    cm.aux0[3] = s_t.mask[mi].lw;
    cm.aux1[0] = s_t.mask[mi].repS;
    cm.aux1[1] = s_t.mask[mi].repT;
    cm.aux1[2] = s_t.mask[mi].lh;
    const RdUniform frame = rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f);
    rhi_cmd_set_scissor(cl, &take);
    /* bits 6 and 7 cleared */
    rd__set_pipeline(cl, pc, &kc);
    rd__bind_uniform(cl, 0, frame);
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, gt);
    rhi_cmd_set_stencil_ref(cl, 0);
    rd__replay_draw(cl, 3, 0, 1);
    /* bit 7 set over the dead tiles */
    rd__set_pipeline(cl, pm, &km);
    rd__bind_uniform(cl, 0, frame);
    rd__bind_uniform(cl, 1, rd__draw_group(&cm));
    rhi_cmd_set_bind_group(cl, 2, gm);
    rhi_cmd_set_stencil_ref(cl, RD_TILE_STENCIL_BIT);
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_COLOR);
    /* bit 6 the alpha's MSB where bit 7 is 0 */
    rd__set_pipeline(cl, ps, &ks);
    rd__bind_uniform(cl, 0, frame);
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, gl);
    rhi_cmd_set_stencil_ref(cl, rd__date_sabotaged() ? 0u : RD_DATE_STENCIL_BIT);
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_BACK);
    g_rdPerf.dateStencilTakes++;
    g_rdPerf.shadowTileComposites++;
    /* no later draw reuses it: bit 6 is not the MSB in the dead tiles */
    r->dateStFor = 0;
    r->dateStSerial = r->writeSerial;
    r->dateStArea = take;
    r->dateStPass = r->passSerial;
    s_t.curOrd = UINT32_MAX;
    if (!s_t.said) {
        s_t.said = true;
        rd__log("shadow tiles: the first composite over the tile mask (command %u, %s %ux%u, "
                "mask %ux%u, the set-up over %ux%u texels); reported once",
                g_rdStoreOrd, rd__target_name(s_t.mask[mi].target), s_t.mask[mi].lw,
                s_t.mask[mi].lh, s_t.mask[mi].w, s_t.mask[mi].h, take.w, take.h);
    }
    return true;
}

unsigned rd__shadow_tile_count(uint32_t *target, uint32_t *live, uint32_t *total, unsigned max)
{
    unsigned n = 0;
    for (int i = 0; i < RD_ST_MASKS && n < max; i++) {
        if (s_t.mask[i].replay != g_rd.replayCounter || !s_t.mask[i].ok) {
            continue;
        }
        const size_t size = (size_t)s_t.mask[i].w * s_t.mask[i].h * 4u;
        uint8_t *px = malloc(size);
        if (!px) {
            continue;
        }
        uint32_t l = 0;
        if (rd__read_rhi_texture(s_t.mask[i].tex, &s_t.mask[i].state, s_t.mask[i].w, s_t.mask[i].h,
                                 px, size)) {
            for (size_t k = 0; k < size; k += 4u) {
                l += px[k] != 0;
            }
        }
        free(px);
        target[n] = s_t.mask[i].target;
        live[n] = l;
        total[n] = s_t.mask[i].w * s_t.mask[i].h;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------ self-test */

#define STT_GROUND 0x40000000u
#define STT_FRONT 0x60000000u
#define STT_BACK 0x20000000u

static RdCmd *push(RdFrame *f, int list, uint8_t type)
{
    return rd__st_push(f, list, type);
}

static bool pushTarget(RdFrame *f, int list, uint32_t color, uint32_t depth, uint32_t w, uint32_t h)
{
    RdCmd *c = push(f, list, RDC_TARGET);
    if (c) {
        c->u[0] = color;
        c->u[1] = depth;
        c->u[2] = (w & 0xFFFF) | ((h & 0xFFFF) << 16);
        c->b[0] = RD_TARGET_OFFSET;
    }
    return c != NULL;
}

static bool pushSprites(RdFrame *f, int list, const RdScreenVtx *v, uint32_t n)
{
    RdCmd *c = push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_WORLD;
    c->b[2] = 1; /* uvFixed */
    c->u[0] = rd__frame_payload(f, v, (uint32_t)(n * sizeof(*v)));
    c->u[1] = n;
    return true;
}

/* a sprite from 12.4 (x0, y0) to (x1, y1), UVs in 12.4 texels */
static void sprite(RdScreenVtx v[2], int32_t x0, int32_t y0, int32_t x1, int32_t y1, float u0,
                   float v0, float u1, float v1, uint32_t z, const uint8_t rgba[4])
{
    memset(v, 0, 2 * sizeof(*v));
    v[0].x = x0;
    v[0].y = y0;
    v[1].x = x1;
    v[1].y = y1;
    v[0].s = u0 * 16.0f;
    v[0].t = v0 * 16.0f;
    v[1].s = u1 * 16.0f;
    v[1].t = v1 * 16.0f;
    for (int k = 0; k < 2; k++) {
        v[k].z = z;
        v[k].q = 1.0f;
        memcpy(v[k].rgba, rgba, 4);
    }
}

static void quad(RdScreenVtx *o, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t z)
{
    const int32_t xs[6] = {x0, x1, x0, x0, x1, x1}, ys[6] = {y0, y0, y1, y1, y0, y1};
    for (int i = 0; i < 6; i++) {
        memset(&o[i], 0, sizeof(o[i]));
        o[i].x = xs[i];
        o[i].y = ys[i];
        o[i].z = z;
        o[i].q = 1.0f;
    }
}

/* the volumes of a variant, in sixteenths of the frame: [x0, x1) x [y0, y1) */
static const uint8_t kVols[2][2][4] = {
    {{2, 4, 2, 4}, {2, 4, 2, 4}},      /* a corner (twice: the same volume) */
    {{0, 2, 0, 3}, {13, 16, 12, 16}}}; /* the top left and bottom right edges */

static bool buildFrame(RdFrame *f, int variant, uint32_t scene, uint32_t work, uint32_t level,
                       uint32_t w, uint32_t h, uint32_t lw, uint32_t lh)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    const int32_t ox = (2048 - (int32_t)(w / 2)) * 16, oy = (2048 - (int32_t)(h / 2)) * 16;
    static const uint8_t kClear[4] = {30, 60, 90, 0x00};
    static const uint8_t kTop[4] = {0x60, 0x50, 0x40, 0x80};
    static const uint8_t kBottom[4] = {0x40, 0x70, 0x30, 0x00};
    static const uint8_t kGrey[4] = {0x80, 0x80, 0x80, 0x80};
    static const uint8_t kShade[4] = {0x40, 0x40, 0x40, 0x80};
    static const uint8_t kRed[4] = {200, 10, 10, 0x80};
    /* list 0: the ground, its top half's alpha MSB 1, its bottom half's 0 */
    bool ok = pushTarget(f, 0, scene, scene, w, h);
    RdCmd *c = ok ? push(f, 0, RDC_CLEAR) : NULL;
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && push(f, 0, RDC_TEXTURE_OFF) && rd__st_push1(f, 0, RDC_ABE, 0) &&
         rd__st_test(f, 0, RD_TEST_Z_ALWAYS) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) &&
         rd__st_push1(f, 0, RDC_FBA, 0) && rd__st_push1(f, 0, RDC_SHADE, 0);
    RdScreenVtx g[4];
    sprite(&g[0], ox, oy, ox + (int32_t)w * 16, oy + (int32_t)(h / 2) * 16, 0, 0, 0, 0, STT_GROUND,
           kTop);
    sprite(&g[2], ox, oy + (int32_t)(h / 2) * 16, ox + (int32_t)w * 16, oy + (int32_t)h * 16, 0, 0,
           0, 0, STT_GROUND, kBottom);
    ok = ok && pushSprites(f, 0, g, 4);
    /* list 3: the count on WORK2 with SCENE's depth */
    ok = ok && pushTarget(f, 3, work, scene, w, h) && push(f, 3, RDC_TEXTURE_OFF) &&
         rd__st_push1(f, 3, RDC_ABE, 0) && rd__st_test(f, 3, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 3, RDC_ZWRITE, RD_ZWRITE_OFF);
    RdCmd *rc = ok ? push(f, 3, RDC_SHADOW_RESET) : NULL;
    const uint8_t b0 = (uint8_t)(rd__shadow_disc_step() == 8 ? 8 : 0);
    if (rc) {
        rc->b[0] = b0;
    }
    ok = ok && rc;
    if (variant < 2) {
        RdScreenVtx q[24];
        for (int k = 0; k < 2; k++) {
            const uint8_t *a = kVols[variant][k];
            const int32_t x0 = ox + (int32_t)(a[0] * w * 16u / 16u) + 7,
                          x1 = ox + (int32_t)(a[1] * w * 16u / 16u) + 7;
            const int32_t y0 = oy + (int32_t)(a[2] * h * 16u / 16u) + 5,
                          y1 = oy + (int32_t)(a[3] * h * 16u / 16u) + 5;
            quad(&q[k * 6], x0, y0, x1, y1, STT_FRONT);
            quad(&q[12 + k * 6], x0, y0, x1, y1, STT_BACK);
        }
        const uint32_t off = rd__frame_payload(f, q, (uint32_t)sizeof(q));
        RdCmd *s = ok ? push(f, 3, RDC_SHADOW_STRIP) : NULL;
        if (s) {
            s->b[0] = RD_SHADOW_TRIS;
            s->u[0] = 12;
            s->u[1] = off;
            s->u[2] = (uint32_t)sizeof(q);
            s->u[3] = 12;
        }
        ok = ok && s;
    }
    RdCmd *rv = ok ? push(f, 3, RDC_SHADOW_RESOLVE) : NULL;
    if (rv) {
        rv->b[0] = b0;
    }
    ok = ok && rv;
    /* SHADOW0 from the count: one 2:1 sprite over the whole level */
    const RdTex countTex = rd_target_texture((RdTarget){work}, RD_VIEW_RGBA);
    const RdTex levelTex = rd_target_texture((RdTarget){level}, RD_VIEW_RGBA);
    const int32_t lox = (2048 - (int32_t)(lw / 2)) * 16, loy = (2048 - (int32_t)(lh / 2)) * 16;
    ok = ok && countTex.id && levelTex.id && pushTarget(f, 3, level, 0, lw, lh) &&
         rd__st_test(f, 3, RD_TEST_Z_ALWAYS) && rd__st_push1(f, 3, RDC_ZWRITE, RD_ZWRITE_OFF);
    c = ok ? push(f, 3, RDC_TEXTURE) : NULL;
    if (c) {
        c->u[0] = countTex.id;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    RdCmd *fl = c ? push(f, 3, RDC_FILTER) : NULL;
    if (fl) {
        fl->b[0] = RD_FILTER_LINEAR;
        fl->b[1] = RD_FILTER_LINEAR;
    }
    ok = ok && fl && rd__st_push1(f, 3, RDC_ABE, 0) && rd__st_push1(f, 3, RDC_SHADE, 0);
    RdScreenVtx lv[2];
    sprite(lv, lox - 4, loy - 4, lox + (int32_t)lw * 16 - 4, loy + (int32_t)lh * 16 - 4, 0.25f,
           0.25f, 2.0f * (float)lw + 0.25f, 2.0f * (float)lh + 0.25f, 0, kGrey);
    ok = ok && pushSprites(f, 3, lv, 2);
    /* the composite into SCENE from SHADOW0 */
    ok = ok && pushTarget(f, 3, scene, scene, w, h);
    c = ok ? push(f, 3, RDC_ALPHA) : NULL;
    if (c) {
        c->b[0] = RD_BLEND_LERP_AS;
        c->b[1] = 0;
    }
    ok = ok && c && rd__st_test(f, 3, RD_TEST_AT_GT0_DATE0);
    c = ok ? push(f, 3, RDC_TEXTURE) : NULL;
    if (c) {
        c->u[0] = levelTex.id;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    fl = c ? push(f, 3, RDC_FILTER) : NULL;
    if (fl) {
        fl->b[0] = RD_FILTER_LINEAR;
        fl->b[1] = RD_FILTER_LINEAR;
    }
    ok = ok && fl && rd__st_push1(f, 3, RDC_ABE, 1) && rd__st_push1(f, 3, RDC_SHADE, 0);
    RdScreenVtx cv[2];
    /* as Shadow.c's: the level's width across, half the frame's rows down */
    sprite(cv, ox, oy, ox + (int32_t)w * 16, oy + (int32_t)h * 16, 0.25f, 0.25f, (float)lw + 0.25f,
           (float)(h / 2u) + 0.25f, 0, kShade);
    ok = ok && pushSprites(f, 3, cv, 2);
    /* list 4: a later DATE DATM 1 reader of the frame */
    ok = ok && pushTarget(f, 4, scene, scene, w, h) && push(f, 4, RDC_TEXTURE_OFF) &&
         rd__st_push1(f, 4, RDC_ABE, 0) && rd__st_test(f, 4, RD_TEST_RGBONLY_DATE1);
    RdScreenVtx rv4[2];
    sprite(rv4, ox, oy + (int32_t)(h / 4) * 16, ox + (int32_t)(w / 2) * 16,
           oy + (int32_t)(3 * h / 4) * 16, 0, 0, 0, 0, 0, kRed);
    ok = ok && pushSprites(f, 4, rv4, 2);
    return ok;
}

static bool readScene(RdTargetRec *t, uint8_t *px, size_t size)
{
    return rd__read_rhi_texture(t->color, &t->colorState, t->tw, t->th, px, size);
}

bool rd__shadow_tile_selftest_variant(int variant, char *words, size_t size)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id, work = rd_target(RD_TARGET_WORK2).id;
    const uint32_t level = rd_target(RD_TARGET_SHADOW0).id;
    RdTargetRec *t = rd__target_rec(scene), *k = rd__target_rec(work), *l = rd__target_rec(level);
    if (!t || !t->withDepth || !t->color.id || !k || !k->color.id || k->w != t->w || k->h != t->h ||
        k->tw != t->tw || k->th != t->th || !l || !l->color.id || l->tw != l->w || l->th != l->h) {
        snprintf(words, size, "not run: no SCENE, WORK2 of its size and SHADOW0 at scale 1");
        return false;
    }
    static RdFrame f;
    if (!buildFrame(&f, variant, scene, work, level, t->w, t->h, l->w, l->h)) {
        rd__frame_free(&f);
        snprintf(words, size, "not run: could not build its frame");
        return false;
    }
    const size_t bytes = (size_t)t->tw * t->th * 4u;
    uint8_t *ref = malloc(bytes), *got = malloc(bytes);
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true;
    bool doneRef = false, doneGot = false;
    uint32_t tiled = 0, liveSum = 0, totalSum = 0;
    if (ref && got) {
        s_t.testing = 1;
        t->clearPending = k->clearPending = l->clearPending = 1;
        doneRef = rd__replay_frame(&f, 0, false) && readScene(t, ref, bytes);
        s_t.testing = 2;
        t->clearPending = k->clearPending = l->clearPending = 1;
        doneGot = rd__replay_frame(&f, 0, false);
        tiled = g_rdPerf.shadowTileComposites;
        uint32_t tg[RD_ST_MASKS], lv[RD_ST_MASKS], tt[RD_ST_MASKS];
        const unsigned nm = doneGot ? rd__shadow_tile_count(tg, lv, tt, RD_ST_MASKS) : 0;
        for (unsigned i = 0; i < nm; i++) {
            liveSum += lv[i];
            totalSum += tt[i];
        }
        doneGot = doneGot && readScene(t, got, bytes);
    }
    s_t.testing = 0;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    t->clearPending = k->clearPending = l->clearPending = 1;
    rd__shadow_count_forget(); /* the count's target was WORK2 */
    bool pass = false;
    if (!doneRef || !doneGot) {
        snprintf(words, size, "error: a replay or its readback could not be done");
    } else {
        size_t diff = 0, first = 0;
        for (size_t i = 0; i < bytes; i += 4u) {
            if (memcmp(ref + i, got + i, 4) != 0 && !diff++) {
                first = i / 4u;
            }
        }
        if (diff) {
            snprintf(words, size, "failed: SCENE differs at %zu of %zu texels, the first (%zu,%zu)",
                     diff, bytes / 4u, first % t->tw, first / t->tw);
        } else if (variant < 2 &&
                   (tiled != 1u || totalSum == 0u || liveSum == 0u || liveSum >= totalSum)) {
            snprintf(words, size,
                     "not run: the composite was not drawn over a mask with dead tiles (%u over "
                     "the mask, %u of %u mask texels live)",
                     tiled, liveSum, totalSum);
        } else {
            pass = true;
            snprintf(words, size,
                     "ok: SCENE the same bytes over %zu texels, %u of %u mask texels live",
                     bytes / 4u, liveSum, totalSum);
        }
    }
    free(ref);
    free(got);
    return pass;
}

bool rd__shadow_tile_selftest(void)
{
    s_t.test = ST_NOT_RUN;
    s_t.words[0] = '\0';
    const int env = envSwitch();
    const int cls = rd__profile_tile_class();
    if (env == 0 || !modules() || (env < 0 && (cls == 0 || RD_SHADOW_TILE_DEFAULT == 0))) {
        rd__log("shadow tiles: off (%s)",
                env == 0          ? "ICO_RD_SHADOW_TILE=0"
                : !g_rd.hasDevice ? "no device"
                : !g_rd.profile.dateStencil
                    ? "DATE by the snapshot here: the tiles need its stencil bit"
                : !modules() ? "the shaders are not on this backend"
                : RD_SHADOW_TILE_DEFAULT == 0
                    ? "not measured to help on a tile-based GPU; ICO_RD_SHADOW_TILE=1 turns it on "
                      "for the tests"
                    : "not the PowerVR or a desktop class: off in this release");
        return true;
    }
    const bool pass = rd__shadow_tile_selftest_variant(0, s_t.words, sizeof s_t.words);
    s_t.test = pass ? ST_OK : strncmp(s_t.words, "failed", 6) == 0 ? ST_FAIL : ST_ERROR;
    if (env == 1) {
        rd__log("shadow tiles: on (ICO_RD_SHADOW_TILE=1; self-test %s)", s_t.words);
    } else if (pass) {
        rd__log("shadow tiles: on (%s; self-test %s)",
                cls == 1 ? "the PowerVR class" : "a desktop class", s_t.words);
    } else {
        rd__log("shadow tiles: off (self-test %s)", s_t.words);
    }
    return pass;
}

bool rd__shadow_tile_test_ok(void)
{
    return s_t.test == ST_OK;
}

void rd__shadow_tile_shutdown(void)
{
    for (int i = 0; i < RD_ST_MASKS; i++) {
        if (s_t.mask[i].tex.id && g_rd.hasDevice) {
            rhi_destroy_texture(s_t.mask[i].tex);
        }
    }
    /* g_rd.replayCounter starts again at rd_init: a kept slot would match a
     * replay of the next renderer and hand it the old device's handle */
    memset(s_t.mask, 0, sizeof(s_t.mask));
    s_t.test = ST_NOT_RUN;
    s_t.words[0] = '\0';
    s_t.curOrd = UINT32_MAX;
    s_t.curMask = 0;
}

/* ------------------------------------------------------------ the lines */

static struct {
    uint64_t composites, masks, why[RD_SHADOW_TILE_WHYS];
    uint32_t replays;
} s_sum;

void rd__shadow_tile_pop(const RdPerfRecord *rec)
{
    s_sum.composites += rec->shadowTileComposites;
    s_sum.masks += rec->shadowTileMasks;
    for (int k = 0; k < RD_SHADOW_TILE_WHYS; k++) {
        s_sum.why[k] += rec->shadowTileWhy[k];
    }
    s_sum.replays++;
}

static void whyText(char *out, size_t size, const uint64_t *why, double n, bool perReplay)
{
    static const char *const kShort[RD_ST_WHY_COUNT] = {"off", "pass open", "mask", "form",
                                                        "set-up"};
    size_t at = 0;
    out[0] = '\0';
    for (int k = 0; k < RD_ST_WHY_COUNT && at < size; k++) {
        if (!why[k]) {
            continue;
        }
        const int w = perReplay ? snprintf(out + at, size - at, "%s%s %.1f", at ? ", " : "",
                                           kShort[k], (double)why[k] / n)
                                : snprintf(out + at, size - at, "%s%s %llu", at ? ", " : "",
                                           kShort[k], (unsigned long long)why[k]);
        at += w > 0 && (size_t)w < size - at ? (size_t)w : 0;
    }
}

void rd__shadow_tile_text(char *out, size_t size, size_t *at)
{
    uint64_t k = 0;
    for (int i = 0; i < RD_SHADOW_TILE_WHYS; i++) {
        k += s_sum.why[i];
    }
    if (s_sum.replays == 0 || (s_sum.composites == 0 && k == 0) || *at >= size) {
        memset(&s_sum, 0, sizeof(s_sum));
        return;
    }
    const double n = (double)s_sum.replays;
    char why[160];
    whyText(why, sizeof why, s_sum.why, n, true);
    const int w = snprintf(out + *at, size - *at,
                           "; shadow tiles: %.1f composites a replay over the tile mask (%.1f "
                           "masks), %.1f whole%s%s%s",
                           (double)s_sum.composites / n, (double)s_sum.masks / n, (double)k / n,
                           why[0] ? " (" : "", why, why[0] ? ")" : "");
    *at += w > 0 && (size_t)w < size - *at ? (size_t)w : 0;
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__shadow_tile_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    uint64_t why[RD_SHADOW_TILE_WHYS], k = 0;
    for (int i = 0; i < RD_SHADOW_TILE_WHYS; i++) {
        why[i] = rec->shadowTileWhy[i];
        k += why[i];
    }
    char words[160];
    whyText(words, sizeof words, why, 1.0, false);
    size_t at = (size_t)snprintf(out, size,
                                 "shadow tiles %s: %u composites over the tile mask (%u masks), "
                                 "%llu whole%s%s%s; tiles live",
                                 rd__shadow_tile_on() ? "on" : "off", rec->shadowTileComposites,
                                 rec->shadowTileMasks, (unsigned long long)k, words[0] ? " (" : "",
                                 words, words[0] ? ")" : "");
    uint32_t tg[RD_ST_MASKS], lv[RD_ST_MASKS], tt[RD_ST_MASKS];
    const unsigned nm = rec->shadowTileMasks ? rd__shadow_tile_count(tg, lv, tt, RD_ST_MASKS) : 0;
    for (unsigned i = 0; i < nm && at < size; i++) {
        const int w =
            snprintf(out + at, size - at, " %s %u of %u", rd__target_name(tg[i]), lv[i], tt[i]);
        at += w > 0 && (size_t)w < size - at ? (size_t)w : 0;
    }
    if (nm == 0 && at < size) {
        snprintf(out + at, size - at, " none");
    }
}
