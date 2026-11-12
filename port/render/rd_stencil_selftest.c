/* rd_stencil_selftest.c: the stencil loaded and stored inside the shadow
 * window only (rd_stencil_selftest.h; docs/port/WORK_BUFFERS.md "The stencil
 * outside the shadow window").
 *
 * Why the stencil is forced today.  v0.4.5 sampled SCENE's depth in place
 * and stopped loading and storing the stencil outside the shadow window
 * (rd__stencil_ops).  A Galaxy A36 (Adreno 710) then showed the fog at full
 * strength on near geometry: the depth read as far.  Commit a54e2f896 made
 * the fog read a copy of the depth on every tile-based GPU and D24S8, and
 * every pass load and store the stencil with the depth (g_rd.depthCopy, then
 * rd__set_fog_path: on unless the path samples in place).  The two changes
 * shipped together; which of them fixed the Adreno was never measured.  The
 * local read (RD_FOG_LOCAL) came later and set depthCopy too, so the
 * PowerVR phone loads and stores 2.4 MB of stencil each way on its list 3
 * pass at 2364x1080 that nothing reads.  The crack fill (rd_crack_fill.c
 * fillTake) still samples SCENE's depth in place (RHI_STATE_DEPTH_READ)
 * right after such a pass: the Adreno 710's sequence.
 *
 * The decision (rd__stencil_cut_forced), made again at each fog path
 * (rd__stencil_cut_path from rd__set_fog_path):
 *   - ICO_RD_STENCIL_PACKED=1: forced; =0: the window only (the tests);
 *   - the in-place path (the desktop): the window only, as since v0.4.5;
 *   - the copy and buffer paths: forced, as a54e2f896 has it;
 *   - the local path: the window only where the device profile's class is
 *     Imagination's (PowerVR; never an Adreno class, rd__profile_adreno)
 *     and the self-test below passed on this renderer; forced on every other
 *     class in this release, before the self-test and after a failed one.
 *
 * The self-test (rd__stencil_cut_selftest, from rd_fog_selftest once a
 * renderer after the fog's path is chosen; only where the class and the
 * path would take the cut, or ICO_RD_STENCIL_PACKED=0): a frame of its own
 * on SCENE and WORK2 at their real size (never recorded, never presented),
 * replayed with the stencil forced and with the cut, MSAA held off, the
 * fogs reading the exact table entry (the caller's s_st.running) and the
 * depth sampled in place (g_rd.depthCopy held off for the two replays, as
 * the crack fill samples it):
 *   list 0: SCENE cleared (depth GS Z 0), two sprites Z-written under ZTST
 *           ALWAYS: the left half at GS Z index 200, the right half at 60
 *           (the pass: depth cleared and stored, the stencil cleared and,
 *           with the cut, not stored);
 *   list 3: WORK2 cleared and fogged from SCENE's depth view: the fog's
 *           in-place read (rd__sampled_depth, RHI_STATE_DEPTH_READ, a pass
 *           of WORK2's colour alone), so WORK2 holds the table entry of the
 *           depth as the pass before stored it;
 *   list 5: SCENE again, a sprite over the whole frame at index 130 under
 *           ZTST GEQUAL without a Z write (a new pass: the depth loaded,
 *           with the cut the stencil neither loaded nor stored): drawn on the
 *           right half, kept on the left;
 *   list 6: SCENE's own fog in that pass: the local read of the depth (on
 *           the local path; in place on another).
 * Eight texels are read back, a quarter in from each corner of SCENE and of
 * WORK2, and each must be the GS arithmetic of the fog over what the depth
 * pattern gives there (within the fog self-test's 2 LSB), and the cut's
 * replay the forced one's byte for byte.  A depth read as far gives the
 * table's entry 0 (and the GEQUAL sprite over the left half): the Adreno
 * 710's failure shows in both checks.  A failure (or a test that cannot
 * run) keeps the stencil forced, said in the start line and as "profile:
 * stencil outside the shadow window off (self-test)" (the profile's own
 * flags are RdProfile fields; this one lives here). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_internal.h"
#include "rd_stencil_selftest.h"
#include "rd_lamps.h"         /* rd__lamps_lowres_on */
#include "rd_vu_skin_check.h" /* rd__profile_adreno */

#define SC_SAMPLES 4 /* a target */
#define SC_TOLERANCE 2

/* GS Z index (bits 16..23, the fog table's) of the two halves and the
 * GEQUAL sprite; 0x8000 into each band, as the fog's self-test */
#define SC_NEAR 200u
#define SC_FAR 60u
#define SC_MID 130u
#define SC_Z(i) (((uint32_t)(i) << 16) | 0x8000u)

enum { SC_NOT_RUN, SC_OK, SC_FAIL, SC_ERROR };

static const uint8_t kClear[4] = {30, 60, 90, 0x80};
static const uint8_t kLeft[4] = {200, 40, 40, 0x40};
static const uint8_t kRight[4] = {40, 200, 80, 0x40};
static const uint8_t kMid[4] = {90, 10, 200, 0x40};
static const uint8_t kWork[4] = {60, 120, 30, 0x20};

static struct {
    int env;      /* ICO_RD_STENCIL_PACKED: -1 unset, 0, 1 */
    int path;     /* the fog's depth path last set, -1 none yet */
    uint8_t test; /* SC_*: this renderer's self-test */
    char testWords[200];
    bool forced; /* the decision */
    bool cutNew; /* the window only where v0.11.4 forced it (the poison) */
    bool said;   /* the start line printed (this renderer) */
    int testing; /* the self-test's replays: 1 forced, 2 the cut, 0 none */
    uint32_t changes;
} s_sc = {-1, -1, SC_NOT_RUN, "", false, false, false, 0, 0};

/* the 10 s sums (rd__stencil_cut_pop under rd_perf.c's queue lock) */
static struct {
    uint64_t stores, loads, storeTexels, loadTexels;
    uint32_t replays;
} s_sum;

static void readEnv(void)
{
    const char *e = getenv("ICO_RD_STENCIL_PACKED");
    s_sc.env = e && e[0] == '1' && e[1] == '\0' ? 1 : e && e[0] == '0' && e[1] == '\0' ? 0 : -1;
}

/* forced or not, and why (static words) */
static bool decide(char *why, size_t n)
{
    if (s_sc.env == 1) {
        snprintf(why, n, "ICO_RD_STENCIL_PACKED=1");
        return true;
    }
    if (s_sc.env == 0) {
        snprintf(why, n, "ICO_RD_STENCIL_PACKED=0");
        return false;
    }
    if (s_sc.path < 0 || s_sc.path == RD_FOG_INPLACE) {
        snprintf(why, n, "the fog reads the depth in place: the desktop's path, never forced");
        return false;
    }
    if (s_sc.path != RD_FOG_LOCAL) {
        snprintf(why, n, "the fog reads %s",
                 s_sc.path == RD_FOG_COPY ? "a copy of the depth"
                                          : "the depth's words through a buffer");
        return true;
    }
    if (rd__profile_adreno()) {
        snprintf(why, n,
                 "an Adreno GPU: an Adreno 710 read the depth sampled in place as far without "
                 "it");
        return true;
    }
    if (!rd__profile_powervr()) {
        snprintf(why, n, "not the PowerVR class: the cut is the PowerVR phone's in this release");
        return true;
    }
    if (s_sc.test != SC_OK) {
        snprintf(why, n, "self-test %s",
                 s_sc.test == SC_NOT_RUN ? "not run yet"
                 : s_sc.testWords[0]     ? s_sc.testWords
                                         : "failed");
        return true;
    }
    snprintf(why, n, "self-test ok");
    return false;
}

static void update(void)
{
    char why[256];
    const bool was = s_sc.forced;
    s_sc.forced = decide(why, sizeof why);
    s_sc.cutNew =
        !s_sc.forced && (s_sc.env == 0 || (s_sc.path >= 0 && s_sc.path != RD_FOG_INPLACE));
    if (s_sc.said && was != s_sc.forced) {
        s_sc.changes++;
        if (s_sc.changes <= 8u || s_sc.changes % 100u == 0u) {
            rd__log("stencil: now loaded and stored %s (%s; fog path %s; %u changes)",
                    s_sc.forced ? "with the depth" : "inside the shadow window only", why,
                    rd__fog_path_name(s_sc.path), s_sc.changes);
        }
    }
}

bool rd__stencil_cut_forced(void)
{
    if (s_sc.testing) {
        return s_sc.testing == 1;
    }
    return s_sc.forced;
}

void rd__stencil_cut_path(int fogPath)
{
    if (!g_rd.dateRan) {
        /* this renderer's self-tests have not run: no verdict of an
           earlier one stands (rd_init clears dateRan) */
        s_sc.test = SC_NOT_RUN;
        s_sc.testWords[0] = '\0';
        s_sc.said = false;
        s_sc.changes = 0;
    }
    readEnv();
    s_sc.path = fogPath;
    update();
}

/* ------------------------------------------------------------ the frame */

static void lutOf(uint32_t i, uint8_t out[4])
{
    out[0] = (uint8_t)i;
    out[1] = (uint8_t)(i * 97u + 13u);
    out[2] = (uint8_t)(255u - i);
    out[3] = (uint8_t)(0x10u + i * 0x70u / 255u); /* 0x10 .. 0x80: As within 1.0 */
}

/* the GS LERP (Cs - Cd) * A >> 7 + Cd, arithmetic shift, clamped */
static int lerpGs(int cs, int cd, int a)
{
    const int v = (((cs - cd) * a) >> 7) + cd;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

/* the fog over cd where the depth's index is idx (rd_fog_path.c stRun's
 * model: MODULATE by 0x80 and strength 0x80, LERP with As, alpha As) */
static void fogged(uint32_t idx, const uint8_t cd[4], uint8_t out[4])
{
    uint8_t lut[4];
    lutOf(idx, lut);
    const int as = (lut[3] * 0x80) >> 7;
    for (int ch = 0; ch < 3; ch++) {
        out[ch] = (uint8_t)lerpGs((lut[ch] * 0x80) >> 7, cd[ch], as > 0x80 ? 0x80 : as);
    }
    out[3] = (uint8_t)as;
}

/* a sprite from GS pixel (x0, y0) to (x1, y1) of a w x h frame at GS Z z */
static void sprite(RdScreenVtx v[2], uint32_t w, uint32_t h, uint32_t x0, uint32_t y0, uint32_t x1,
                   uint32_t y1, uint32_t z, const uint8_t rgba[4])
{
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    memset(v, 0, 2 * sizeof(*v));
    v[0].x = (ox + (int32_t)x0) * 16;
    v[0].y = (oy + (int32_t)y0) * 16;
    v[1].x = (ox + (int32_t)x1) * 16;
    v[1].y = (oy + (int32_t)y1) * 16;
    for (int k = 0; k < 2; k++) {
        v[k].z = z;
        v[k].q = 1.0f;
        memcpy(v[k].rgba, rgba, 4);
    }
}

static bool pushSprites(RdFrame *f, int list, const RdScreenVtx *v, uint32_t n)
{
    RdCmd *c = rd__st_push(f, list, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)(n * sizeof(*v)));
    c->u[1] = n;
    return true;
}

static bool pushTarget(RdFrame *f, int list, uint32_t color, uint32_t depth, uint32_t w, uint32_t h)
{
    RdCmd *c = rd__st_push(f, list, RDC_TARGET);
    if (c) {
        c->u[0] = color;
        c->u[1] = depth;
        c->u[2] = (w & 0xFFFF) | ((h & 0xFFFF) << 16);
        c->b[0] = RD_TARGET_OFFSET;
    }
    return c != NULL;
}

static bool pushClear(RdFrame *f, int list, uint32_t target, const uint8_t rgba[4], bool depth)
{
    RdCmd *c = rd__st_push(f, list, RDC_CLEAR);
    if (c) {
        c->u[0] = target;
        memcpy(c->b, rgba, 4);
        c->b[4] = depth ? 1 : 0;
        c->u[1] = 0; /* GS Z 0: far */
    }
    return c != NULL;
}

/* fog_DrawFog's state and record (rd_fog_path.c stBuildFrame's list 4) on
 * the list's target, reading scene's depth view */
static bool pushFog(RdFrame *f, int list, uint32_t depthView, uint32_t lutOffset, uint32_t w,
                    uint32_t h)
{
    bool ok = rd__st_push1(f, list, RDC_PABE, 0);
    RdCmd *c = ok ? rd__st_push(f, list, RDC_ALPHA) : NULL;
    if (c) {
        c->b[0] = RD_BLEND_LERP_AS;
        c->b[1] = 128;
    }
    c = c ? rd__st_push(f, list, RDC_TEXTURE) : NULL;
    if (c) {
        c->u[0] = depthView;
        c->b[0] = RD_TEXFN_MODULATE;
        c->b[1] = RD_TCC_RGBA;
    }
    ok = c && rd__st_push1(f, list, RDC_ZWRITE, RD_ZWRITE_OFF) &&
         rd__st_test(f, list, RD_TEST_Z_GEQUAL);
    c = ok ? rd__st_push(f, list, RDC_FILTER) : NULL;
    if (c) {
        c->b[0] = RD_FILTER_NEAREST;
        c->b[1] = RD_FILTER_NEAREST;
    }
    ok = c && rd__st_push1(f, list, RDC_ABE, 1) && rd__st_push1(f, list, RDC_SHADE, 0);
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    p.rgba[0] = p.rgba[1] = p.rgba[2] = 0x80;
    p.rgba[3] = 0x80; /* fogStrength */
    p.z = 0xFFFFFF;
    p.rect[0] = (float)(0x8000 - (int32_t)w * 8);
    p.rect[1] = (float)(0x8000 - (int32_t)h * 8);
    p.rect[2] = (float)(0x8000 + (int32_t)w * 8);
    p.rect[3] = (float)(0x8000 + (int32_t)h * 8);
    p.uv[0] = 8.0f;
    p.uv[1] = 8.0f;
    p.uv[2] = (float)(8 + w * 16);
    p.uv[3] = (float)(8 + h * 16);
    p.lutOffset = lutOffset;
    c = ok ? rd__st_push(f, list, RDC_POST_STUB) : NULL;
    if (c) {
        c->b[0] = RD_POST_FOG;
        c->u[1] = rd__frame_payload(f, &p, (uint32_t)sizeof(p));
        c->u[2] = (uint32_t)sizeof(p);
    }
    return c != NULL;
}

static bool buildFrame(RdFrame *f, uint32_t scene, uint32_t work, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    const RdTex depthView = rd_target_texture((RdTarget){scene}, RD_VIEW_DEPTH);
    uint8_t lut[256 * 4];
    for (uint32_t i = 0; i < 256u; i++) {
        lutOf(i, &lut[i * 4u]);
    }
    const uint32_t lutOffset = rd__frame_payload(f, lut, sizeof(lut));
    RdScreenVtx halves[4], all[2];
    sprite(&halves[0], w, h, 0, 0, w / 2, h, SC_Z(SC_NEAR), kLeft);
    sprite(&halves[2], w, h, w / 2, 0, w, h, SC_Z(SC_FAR), kRight);
    sprite(all, w, h, 0, 0, w, h, SC_Z(SC_MID), kMid);
    /* list 0: the depth pattern */
    bool ok = depthView.id && pushTarget(f, 0, scene, scene, w, h) &&
              pushClear(f, 0, scene, kClear, true) && rd__st_push(f, 0, RDC_TEXTURE_OFF) &&
              rd__st_push1(f, 0, RDC_ABE, 0) && rd__st_test(f, 0, RD_TEST_Z_ALWAYS) &&
              rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_ON) && rd__st_push1(f, 0, RDC_FBA, 0) &&
              rd__st_push1(f, 0, RDC_SHADE, 0) && pushSprites(f, 0, halves, 4);
    /* list 3: WORK2 fogged from SCENE's depth, read in place */
    ok = ok && pushTarget(f, 3, work, 0, w, h) && pushClear(f, 3, work, kWork, false) &&
         pushFog(f, 3, depthView.id, lutOffset, w, h);
    /* list 5: the GEQUAL sprite in a new pass on SCENE with its depth */
    ok = ok && pushTarget(f, 5, scene, scene, w, h) && rd__st_push(f, 5, RDC_TEXTURE_OFF) &&
         rd__st_push1(f, 5, RDC_ABE, 0) && rd__st_test(f, 5, RD_TEST_Z_GEQUAL) &&
         rd__st_push1(f, 5, RDC_ZWRITE, RD_ZWRITE_OFF) && rd__st_push1(f, 5, RDC_SHADE, 0) &&
         pushSprites(f, 5, all, 2);
    /* list 6: SCENE's own fog in that pass (the local read) */
    ok = ok && pushTarget(f, 6, scene, scene, w, h) && pushFog(f, 6, depthView.id, lutOffset, w, h);
    return ok;
}

/* the texels read back: a quarter in from each corner, the left half's two
 * first (SCENE's, then WORK2's at the same places), and what each must hold */
static void samples(const RdTargetRec *t, uint32_t xs[SC_SAMPLES], uint32_t ys[SC_SAMPLES],
                    uint8_t want[2 * SC_SAMPLES][4])
{
    for (int k = 0; k < SC_SAMPLES; k++) {
        const bool left = k < 2;
        xs[k] = left ? t->tw / 4 : t->tw - 1 - t->tw / 4;
        ys[k] = (k & 1) ? t->th - 1 - t->th / 4 : t->th / 4;
        /* SCENE: the left half kept its colour (130 is behind 200), the
           right half took the sprite (130 in front of 60); the fog over
           each at its own depth */
        fogged(left ? SC_NEAR : SC_FAR, left ? kLeft : kMid, want[k]);
        /* WORK2: the fog over its clear at SCENE's depth */
        fogged(left ? SC_NEAR : SC_FAR, kWork, want[SC_SAMPLES + k]);
    }
}

static struct {
    RhiTexture tex;
    RhiState state;
} s_read;

/* one replay; px the eight texels */
static bool run(const RdFrame *f, RdTargetRec *t, RdTargetRec *k, uint8_t px[2 * SC_SAMPLES * 4])
{
    t->clearPending = 1;
    k->clearPending = 1;
    if (!rd__replay_frame(f, 0, false)) {
        return false;
    }
    if (!s_read.tex.id) {
        s_read.tex = rhi_create_texture(&(RhiTextureDesc){2 * SC_SAMPLES, 1, 1, t->format,
                                                          RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                          "rd stencil self-test", 0});
        s_read.state = RHI_STATE_UNDEFINED;
        if (!s_read.tex.id) {
            return false;
        }
    }
    uint32_t xs[SC_SAMPLES], ys[SC_SAMPLES];
    uint8_t want[2 * SC_SAMPLES][4];
    samples(t, xs, ys, want);
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, k->color, &k->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_read.tex, &s_read.state, RHI_STATE_COPY_DST);
    for (uint32_t i = 0; i < SC_SAMPLES; i++) {
        const RhiRect at = {(int32_t)xs[i], (int32_t)ys[i], 1, 1};
        rhi_cmd_copy_texture(cl, t->color, at, s_read.tex, (int32_t)i, 0);
        rhi_cmd_copy_texture(cl, k->color, at, s_read.tex, (int32_t)(SC_SAMPLES + i), 0);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    return rd__read_rhi_texture(s_read.tex, &s_read.state, 2 * SC_SAMPLES, 1, px,
                                2 * SC_SAMPLES * 4);
}

/* the first texel off the model by more than the tolerance (-1 none) */
static int offModel(const uint8_t px[2 * SC_SAMPLES * 4], uint8_t want[2 * SC_SAMPLES][4])
{
    for (int i = 0; i < 2 * SC_SAMPLES; i++) {
        for (int ch = 0; ch < 4; ch++) {
            if (abs((int)px[i * 4 + ch] - (int)want[i][ch]) > SC_TOLERANCE) {
                return i;
            }
        }
    }
    return -1;
}

static void hex(char out[9], const uint8_t *c)
{
    snprintf(out, 9, "%02X%02X%02X%02X", c[0], c[1], c[2], c[3]);
}

/* the test itself: s_sc.test and its words */
static void runTest(void)
{
    /* a frame-sized scratch beside SCENE: WORK2, or AURA_WORK (the same size)
     * where the lamps' LOWRES makes WORK2 the GS frame's size (rd_lamps.h) */
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const uint32_t work =
        rd_target(rd__lamps_lowres_on() ? RD_TARGET_AURA_WORK : RD_TARGET_WORK2).id;
    RdTargetRec *t = rd__target_rec(scene), *k = rd__target_rec(work);
    if (!t || !t->withDepth || !t->color.id || !t->depth.id || t->format != RHI_FMT_RGBA8_UNORM ||
        !k || !k->color.id || k->tw != t->tw || k->th != t->th || k->w != t->w || k->h != t->h ||
        k->format != RHI_FMT_RGBA8_UNORM) {
        s_sc.test = SC_ERROR;
        snprintf(s_sc.testWords, sizeof s_sc.testWords,
                 "not run: no SCENE and WORK2 of one size to run it on");
        return;
    }
    if (!t->depthSampled) {
        /* nothing samples it in place then (the crack fill needs it) */
        s_sc.test = SC_ERROR;
        snprintf(s_sc.testWords, sizeof s_sc.testWords,
                 "not run: SCENE's depth is not sampled on this device");
        return;
    }
    static RdFrame f;
    if (!buildFrame(&f, scene, work, t->w, t->h)) {
        rd__frame_free(&f);
        s_sc.test = SC_ERROR;
        snprintf(s_sc.testWords, sizeof s_sc.testWords, "not run: could not build its frame");
        return;
    }
    uint8_t ref[2 * SC_SAMPLES * 4], cut[2 * SC_SAMPLES * 4];
    memset(ref, 0, sizeof ref);
    memset(cut, 0, sizeof cut);
    const bool wasBlocked = g_rd.msaaBlocked, wasCopy = g_rd.depthCopy;
    g_rd.msaaBlocked = true; /* no MSAA epoch: SCENE's own passes */
    g_rd.depthCopy = false;  /* the in-place read, as the crack fill's */
    s_sc.testing = 1;
    const bool doneRef = run(&f, t, k, ref);
    const uint32_t localRef = g_rdPerf.fogLocalDraws;
    s_sc.testing = 2;
    const bool doneCut = run(&f, t, k, cut);
    const uint32_t localCut = g_rdPerf.fogLocalDraws;
    const uint32_t storesCut = g_rdPerf.stencilCutStores;
    s_sc.testing = 0;
    g_rd.depthCopy = wasCopy;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    if (s_read.tex.id) {
        rhi_destroy_texture(s_read.tex);
    }
    s_read.tex = (RhiTexture){0};
    t->clearPending = 1; /* the next replay starts SCENE and WORK2 from their clears */
    k->clearPending = 1;

    uint32_t xs[SC_SAMPLES], ys[SC_SAMPLES];
    uint8_t want[2 * SC_SAMPLES][4];
    samples(t, xs, ys, want);
    if (!doneRef || !doneCut) {
        s_sc.test = SC_ERROR;
        snprintf(s_sc.testWords, sizeof s_sc.testWords,
                 "error: the %s replay or its readback could not be done",
                 !doneRef ? "forced" : "cut's");
        return;
    }
    const int badRef = offModel(ref, want), badCut = offModel(cut, want);
    int differ = -1;
    for (int i = 0; i < 2 * SC_SAMPLES && differ < 0; i++) {
        if (memcmp(&ref[i * 4], &cut[i * 4], 4) != 0) {
            differ = i;
        }
    }
    char g[9], w[9];
    const char *local = localCut > 0 ? "the local read" : "the depth in place";
    if (badRef >= 0) {
        /* the frame itself is not drawn as modelled: it says nothing of
           the cut (the stencil stays forced) */
        hex(g, &ref[badRef * 4]);
        hex(w, want[badRef]);
        s_sc.test = SC_ERROR;
        snprintf(s_sc.testWords, sizeof s_sc.testWords,
                 "error: the forced replay differs from the GS at %s sample %d: %s for %s",
                 badRef < SC_SAMPLES ? "SCENE" : "WORK2", badRef % SC_SAMPLES, g, w);
        return;
    }
    if (badCut >= 0 || differ >= 0) {
        const int i = badCut >= 0 ? badCut : differ;
        hex(g, &cut[i * 4]);
        hex(w, badCut >= 0 ? want[i] : &ref[i * 4]);
        s_sc.test = SC_FAIL;
        snprintf(s_sc.testWords, sizeof s_sc.testWords, "failed at %s sample %d: %s for %s (%s)",
                 i < SC_SAMPLES ? "SCENE" : "WORK2", i % SC_SAMPLES, g, w,
                 badCut >= 0 ? "the GS arithmetic" : "the forced replay's");
        return;
    }
    s_sc.test = SC_OK;
    snprintf(s_sc.testWords, sizeof s_sc.testWords,
             "%d texels the GS's and the forced replay's; the fog by %s, %u local fog draws "
             "forced and %u cut, %u stencil stores not asked",
             2 * SC_SAMPLES, local, localRef, localCut, storesCut);
}

bool rd__stencil_cut_selftest(void)
{
    readEnv();
    s_sc.test = SC_NOT_RUN;
    s_sc.testWords[0] = '\0';
    const bool want = s_sc.env == 0 || (s_sc.env < 0 && s_sc.path == RD_FOG_LOCAL &&
                                        !rd__profile_adreno() && rd__profile_powervr());
    if (want && g_rd.inited && g_rd.hasDevice) {
        runTest();
    }
    s_sc.said = false;
    update();
    char why[256];
    (void)decide(why, sizeof why);
    const RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    const uint32_t tw = t ? t->tw : 0, th = t ? t->th : 0;
    if (s_sc.test != SC_NOT_RUN && s_sc.test != SC_OK && s_sc.env < 0) {
        rd__log("profile: stencil outside the shadow window off (self-test)");
    }
    if (s_sc.env == 0) {
        rd__log(
            "stencil: loaded and stored inside the shadow window only (ICO_RD_STENCIL_PACKED=0; "
            "self-test %s%s%s); fog path %s, SCENE %ux%u texels",
            s_sc.test == SC_OK        ? "ok"
            : s_sc.test == SC_NOT_RUN ? "not run"
                                      : "",
            s_sc.test == SC_OK && s_sc.testWords[0] ? ": " : "", s_sc.testWords,
            rd__fog_path_name(s_sc.path), tw, th);
    } else if (!s_sc.forced && s_sc.test == SC_OK) {
        rd__log("stencil: loaded and stored inside the shadow window only (self-test ok); %s; fog "
                "path %s, SCENE %ux%u texels",
                s_sc.testWords, rd__fog_path_name(s_sc.path), tw, th);
    } else if (!s_sc.forced) {
        rd__log("stencil: loaded and stored inside the shadow window only (%s)", why);
    } else {
        rd__log("stencil: loaded and stored with the depth (%s)", why);
    }
    s_sc.said = true;
    return !want || s_sc.test == SC_OK;
}

bool rd__stencil_cut_test_ok(void)
{
    return s_sc.test == SC_OK;
}

const char *rd__stencil_cut_test_words(void)
{
    return s_sc.testWords;
}

/* ------------------------------------------------------- the passes' ops */

static uint64_t passTexels(const RhiRenderPassDesc *p)
{
    uint64_t w = p->width, h = p->height;
    if (p->area.w != 0 && p->area.h != 0) {
        /* clamped to the size, as rhi_traffic.c counts it */
        const int64_t x = p->area.x > 0 ? p->area.x : 0, y = p->area.y > 0 ? p->area.y : 0;
        const int64_t r = x + (int64_t)p->area.w, b = y + (int64_t)p->area.h;
        const int64_t cw = (r < (int64_t)p->width ? r : (int64_t)p->width) - x;
        const int64_t ch = (b < (int64_t)p->height ? b : (int64_t)p->height) - y;
        if (cw > 0 && ch > 0) {
            w = (uint64_t)cw;
            h = (uint64_t)ch;
        }
    }
    return w * h;
}

void rd__stencil_cut_pass(RhiRenderPassDesc *p, bool ms)
{
    RhiDepthAttachment *d = &p->depth;
    if (ms || d->texture.id == 0) {
        return;
    }
    const uint64_t px = passTexels(p);
    if (!d->readOnlyDepth && d->store == RHI_STORE_STORE &&
        d->stencilStore == RHI_STORE_DONT_CARE) {
        g_rdPerf.stencilCutStores++;
        g_rdPerf.stencilCutStoreTexels += px;
    }
    if (d->depthLoad != RHI_LOAD_LOAD || d->stencilLoad != RHI_LOAD_DONT_CARE ||
        rd__stencil_live(rd__replay_stencil(), d->texture.id)) {
        return;
    }
    g_rdPerf.stencilCutLoads++;
    g_rdPerf.stencilCutLoadTexels += px;
    /* the tests' garbage where the cut leaves the stencil undefined (a
       load v0.11.4 asked for), after the pass area and the shadow merge's
       arm read the ops: neither changes with it */
    const char *poison = getenv("ICO_RD_STORE_POISON");
    const bool cutNew = s_sc.testing ? s_sc.testing == 2 : s_sc.cutNew;
    if (cutNew && poison && strcmp(poison, "1") == 0) {
        d->stencilLoad = RHI_LOAD_CLEAR;
        d->clearStencil = 0x5A;
    }
}

/* ------------------------------------------------------------ the lines */

void rd__stencil_cut_pop(const RdPerfRecord *rec)
{
    s_sum.stores += rec->stencilCutStores;
    s_sum.loads += rec->stencilCutLoads;
    s_sum.storeTexels += rec->stencilCutStoreTexels;
    s_sum.loadTexels += rec->stencilCutLoadTexels;
    s_sum.replays++;
}

void rd__stencil_cut_text(char *out, size_t size, size_t *at)
{
    /* only where the cut leaves out what the same device asked for before
       (the PowerVR phone after its self-test, the tests' =0): the desktop's
       in-place path never asked, the forced paths ask every one */
    if (!s_sc.cutNew || s_sum.replays == 0 || *at >= size) {
        memset(&s_sum, 0, sizeof(s_sum));
        return;
    }
    const double n = (double)s_sum.replays;
    const int k = snprintf(
        out + *at, size - *at,
        "; stencil: %.1f stores and %.1f loads a replay not asked with the "
        "depth's (%.1f/%.1f MB), loaded and stored %s",
        (double)s_sum.stores / n, (double)s_sum.loads / n,
        (double)s_sum.storeTexels / n / 1048576.0, (double)s_sum.loadTexels / n / 1048576.0,
        rd__stencil_cut_forced() ? "with the depth" : "inside the shadow window only");
    *at += k > 0 && (size_t)k < size - *at ? (size_t)k : 0;
    memset(&s_sum, 0, sizeof(s_sum));
}

void rd__stencil_cut_stats(const RdPerfRecord *rec, char *out, size_t size)
{
    snprintf(out, size,
             "stencil %s: %u stores and %u loads not asked with the depth's (%.2f MB stored, "
             "%.2f MB loaded)",
             rd__stencil_cut_forced() ? "with the depth" : "inside the shadow window only",
             rec->stencilCutStores, rec->stencilCutLoads,
             (double)rec->stencilCutStoreTexels / 1048576.0,
             (double)rec->stencilCutLoadTexels / 1048576.0);
}
