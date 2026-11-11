/* rd_date_cut.c: DATE's passes without the depth their draws never touch,
 * and the DATE snapshot itself with its counters (docs/port/WORK_BUFFERS.md,
 * "DATE's passes without the depth"; ICO_RD_DATE_CUT=0 or 1 for the tests:
 * no setting).
 *
 * On the DATE snapshot path (RdProfile.dateStencil off: Arm, Qualcomm,
 * Apple, the desktop's OpenGL) each DATE draw whose target's alpha was
 * written since the last snapshot ends SCENE's open pass for the R8 copy
 * and begins it again.  The shadow's three composites (Shadow.c: list 3,
 * full-frame sprites on SCENE under DATE DATM 0, ZTST ALWAYS, Z write off)
 * make that three times a picture, and each pass after a snapshot attached
 * SCENE's depth and stencil, loaded and stored them (2.3 MB each way at the
 * Mali's 912 x 512, 11 MB at the Adreno 740's 3x), though no draw in the
 * first two tests or writes them.  The store plan's walk (rd_store_plan.c
 * rd__store_plan_scan) reads the lists ahead of the replay and the rule in
 * rd_date_cut.h marks the runs of commands, each opened by such a DATE
 * draw, whose draws are all depth-free screen prims on the run's target up
 * to a command that ends the pass for certain; rd__date_cut_depth, from
 * rd_replay.c prepareDrawEx, gives a draw of such a run no depth target
 * (DrawSetup.tdId 0, depthFmt RHI_FMT_UNKNOWN), so bindDrawEx begins its
 * pass with the colour alone, as it does for any target without a depth.
 *
 * Why the bytes are the same: the depth and the stencil are not attached,
 * so the pass neither reads nor writes them, and their memory holds what
 * the last pass that stored them left, which is what a load and a store
 * with no draw touching them in between would have left (the store plan is
 * as with the cut off: a DATE draw counts as a touch, so every pass before
 * the run keeps its store, list 0's whose stencil the shadow count read
 * among them).  The colour bytes are those of the draw
 * with the depth attached: its key with the depth has the test ALWAYS and
 * the write off (rd_pipeline.c rd__plan_screen_draw_ex: ztst ALWAYS and
 * zwrite OFF without a depth, the state's ALWAYS and OFF with it), and the
 * FrameCB's Z scale does not depend on the depth target (every target's
 * zFormat is RD_ZFMT_32: checked per draw, rd__target_z_scale).  A draw
 * that needs the depth while such a pass is open begins a pass of its own
 * with it (bindDrawEx: the pass's depth differs), so a wrong prediction
 * costs a pass, never a byte (counted: dateCutAgain).  Not taken: DATE
 * through the stencil bit or the shader's read (the snapshot path only),
 * an MSAA epoch wanted, the exact blend, an open shadow window on the
 * depth, a pass open on the target with its depth (the draw joins it).
 *
 * Why it cannot bring back a device loss: it removes an attachment from
 * passes that already exist and adds no pass, no barrier, no copy, no
 * wait and no new kind of pass (a colour-only pass on SCENE is what every
 * post sprite without a Z test begins: rd_fx_plan.c, doBlurSpriteDraw).
 * Its pipelines are the screen family's no-depth keys, in the start-up set
 * beside every key with the depth (rd__enumerate_reachable_screen draws
 * each state with and without it: rd_date_cut_test checks the pairs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_date_cut.h"
#include "rd_replay.h"

#define DC_RUNS 64

static struct {
    int mode;    /* -1 not read yet; 0 off; 1 on (ICO_RD_DATE_CUT=1 or the profile) */
    bool forced; /* ICO_RD_DATE_CUT given */
    bool on;     /* this replay: the scan ran with the cut */
    RdDateCutScan scan;
    uint32_t n, cur;                       /* the depth-free runs, in order; the walk's cursor */
    uint32_t start[DC_RUNS], end[DC_RUNS]; /* [start, end) ordinals */
    uint32_t frame;
    /* the pass the hook began without the depth: its serial (Replay.passSerial) */
    uint32_t serial;
    bool serialValid;
} s_dc = {.mode = -1};

/* the 10 s sums (rd__date_cut_pop, rd__date_cut_text: the window's thread) */
static struct {
    uint32_t replays;
    uint64_t passes, again, texels, snaps, reused, why[RD_DATE_RETAKE_COUNT];
} s_sum;

static const char *const kRetakeWhy[RD_DATE_RETAKE_COUNT] = {
    "the first", "another target", "an alpha write since", "an area not held",
    "the copy used elsewhere"};

/* the last scan's runs, for the tests (rd__date_cut_last_runs) */
static uint32_t s_lastN, s_lastStart[DC_RUNS], s_lastEnd[DC_RUNS];

static int s_force = -1; /* rd__date_cut_force: -1 the switch's own, 0, 1 */

void rd__date_cut_force(int mode)
{
    s_force = mode < 0 ? -1 : mode != 0;
}

bool rd__date_cut_on(void)
{
    if (s_force >= 0) {
        return s_force == 1 && !g_rd.profile.dateStencil && !rd__uber_on();
    }
    if (s_dc.mode < 0) {
        const char *e = getenv("ICO_RD_DATE_CUT");
        s_dc.forced = e && (strcmp(e, "0") == 0 || strcmp(e, "1") == 0);
        s_dc.mode = s_dc.forced ? e[0] == '1' : 1;
        if (e && !s_dc.forced) {
            rd__log("DATE cut: ICO_RD_DATE_CUT=%.16s is not 0 or 1: the profile's", e);
        }
    }
    /* the snapshot path only: the stencil bit and GS state as shader data
     * break no pass for DATE */
    return s_dc.mode == 1 && !g_rd.profile.dateStencil && !rd__uber_on();
}

bool rd__date_cut_scan_begin(const RdFrame *f)
{
    s_lastN = 0; /* a replay with the cut off has no runs to show */
    s_dc.n = s_dc.cur = 0;
    memset(&s_dc.scan, 0, sizeof(s_dc.scan));
    s_dc.serialValid = false;
    s_dc.frame = f->number;
    s_dc.on = rd__date_cut_on();
    static int s_said = -1;
    if (s_said != (int)s_dc.on) {
        s_said = (int)s_dc.on;
        rd__log("DATE cut: %s", s_dc.on ? "on: a DATE snapshot's pass whose draws neither test "
                                          "nor write the depth begins without it (the same "
                                          "picture)"
                                : s_force == 0   ? "off (the tests' switch)"
                                : s_dc.mode == 0 ? "off (ICO_RD_DATE_CUT=0)"
                                                 : "off (DATE through the stencil bit or the "
                                                   "shader's read: no pass ends for DATE)");
    }
    return s_dc.on;
}

/* a run the rule closed */
static void dcClose(int did, uint32_t start, uint32_t ord)
{
    if (did != RD_DC_STEP_FREE || ord <= start) {
        return;
    }
    if (s_dc.n == DC_RUNS) {
        return; /* past the table: those passes keep their depth */
    }
    s_dc.start[s_dc.n] = start;
    s_dc.end[s_dc.n] = ord;
    s_dc.n++;
    g_rdPerf.dateCutRuns++;
}

/* a screen prim under st that neither tests nor writes the depth, takes no
 * PRIM.AA1 (its edges' stencil pairs), samples no depth view and is not
 * COLCLAMP 0's draw (doScreenWrap: its own keys) */
static bool dcDepthFree(const RdStateBlock *st)
{
    const RdDrawState *d = &st->ds;
    if ((d->test.zte && d->test.ztst != RD_ZTST_ALWAYS) || d->zwrite == RD_ZWRITE_ON || st->aa1 ||
        rd__wrap_applies(st)) {
        return false;
    }
    const RdTexRec *tv = d->texEnabled ? rd__tex_rec(st->tex) : NULL;
    return !(tv && tv->kind == RD_TEXKIND_TARGET && tv->view == RD_VIEW_DEPTH);
}

void rd__date_cut_scan_cmd(const RdCmd *c, const RdStateBlock *st, bool state, uint32_t ord)
{
    if (!s_dc.on) {
        return;
    }
    RdDateCutCmd d;
    memset(&d, 0, sizeof(d));
    d.target = st->color;
    if (state || c->type == RDC_NOP || c->type == RDC_OVERLAY_TEXT) {
        d.kind =
            RD_DC_STATE; /* nothing drawn in the replay's passes (the present takes the text) */
    } else if (c->type == RDC_SCREEN) {
        if ((g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS) || rd__fades_fold_skip(ord)) {
            d.kind = RD_DC_STATE; /* doScreen draws nothing */
        } else {
            const RdTargetRec *td = rd__target_rec(st->depth);
            d.kind = RD_DC_SCREEN;
            d.date = st->ds.test.date != RD_DATE_OFF;
            d.depthFree = dcDepthFree(st);
            d.alpha = (st->ds.colorMask & 8u) != 0;
            d.hasDepth = td && td->withDepth;
        }
    } else if (c->type >= RDC_MESH && c->type <= RDC_PARTICLES) {
        d.kind = RD_DC_DRAW;
    } else {
        d.kind = RD_DC_OTHER;
    }
    /* the step first, then its run's start: as arguments of one call their
     * order is unspecified (gcc read start before the step had set it: every
     * run began at command 0, list 0's draws lost their depth) */
    uint32_t start = 0;
    const int did = rd_date_cut_step(&s_dc.scan, &d, ord, &start);
    dcClose(did, start, ord);
}

void rd__date_cut_scan_end(uint32_t ord)
{
    s_lastN = 0;
    if (!s_dc.on) {
        return;
    }
    uint32_t start = 0;
    const int did = rd_date_cut_finish(&s_dc.scan, ord, &start);
    dcClose(did, start, ord);
    s_lastN = s_dc.n;
    memcpy(s_lastStart, s_dc.start, sizeof(s_lastStart));
    memcpy(s_lastEnd, s_dc.end, sizeof(s_lastEnd));
}

uint32_t rd__date_cut_last_runs(uint32_t *start, uint32_t *end, uint32_t max)
{
    for (uint32_t i = 0; i < s_lastN && i < max; i++) {
        start[i] = s_lastStart[i];
        end[i] = s_lastEnd[i];
    }
    return s_lastN;
}

void rd__date_cut_end(void)
{
    s_dc.on = false; /* a draw outside the walk keeps its depth */
    s_dc.n = s_dc.cur = 0;
    s_dc.serialValid = false;
}

/* command ord lies in a depth-free run (the walk's ordinals only grow) */
static bool dcMember(uint32_t ord)
{
    while (s_dc.cur < s_dc.n && s_dc.end[s_dc.cur] <= ord) {
        s_dc.cur++;
    }
    return s_dc.cur < s_dc.n && s_dc.start[s_dc.cur] <= ord;
}

void rd__date_cut_depth(Replay *r, DrawSetup *ds, const RdTargetRec *td)
{
    ds->tdId = td ? r->st.depth : 0;
    ds->depthFmt = td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
    if (!td || !s_dc.on) {
        return;
    }
    const bool ours = s_dc.serialValid && r->passOpen && r->passSerial == s_dc.serial &&
                      r->passColor == r->st.color && r->passDepth == 0;
    if (!dcMember(g_rdStoreOrd)) {
        if (ours) {
            /* the pass begun without the depth meets a draw that needs it:
             * bindDrawEx begins one with it (a pass more, the same bytes) */
            s_dc.serialValid = false;
            g_rdPerf.dateCutAgain++;
            static uint32_t s_seen;
            if (++s_seen <= 8u || s_seen % 100u == 0u) {
                rd__log("DATE cut: a draw that needs %s's depth after its pass began without it "
                        "(frame %u, list %d): a pass of its own with the depth (%u so far)",
                        rd__target_name(r->st.color), s_dc.frame, rd__replay_list(), s_seen);
            }
        }
        return;
    }
    if (ds->dateStencil || r->dateStNow || r->dateDstNow || rd__replay_msaa_want() > 1u ||
        g_rd.dstMode != RD_BLEND_EXACT_OFF ||
        rd__stencil_live(rd__replay_stencil(), td->depth.id) ||
        rd__target_z_scale(r->st.depth) != rd__target_z_scale(0) ||
        !rd_date_cut_scale_ok(ds->tc->sx, ds->tc->sy)) {
        g_rdPerf.dateCutKept++;
        return;
    }
    if (r->passOpen && r->passColor == r->st.color) {
        if (r->passDepth != 0 || r->passStorage) {
            g_rdPerf.dateCutKept++; /* the open pass has the depth: the draw joins it */
            return;
        }
        /* the open pass on the target has no depth (the run's, or a post
         * sprite's): the draw joins it */
    } else {
        /* bindDrawEx begins the draw's pass, now without the depth */
        g_rdPerf.dateCutPasses++;
        g_rdPerf.dateCutTexels += (uint64_t)td->tw * td->th;
        s_dc.serial = r->passSerial + 1u;
        s_dc.serialValid = true;
        static bool s_said;
        if (!s_said) {
            s_said = true;
            rd__log("DATE cut: %s's pass after a DATE snapshot begun without its depth (frame %u, "
                    "list %d): its draws neither test nor write the depth and none uses the "
                    "stencil; the same picture (reported once)",
                    rd__target_name(r->st.color), s_dc.frame, rd__replay_list());
        }
    }
    ds->tdId = 0;
    ds->depthFmt = RHI_FMT_UNKNOWN;
    g_rdPerf.dateCutDraws++;
}

/* rd_replay.c's dateSnapshot: TEST.DATE, the bound target's alpha MSB into
 * the R8 snapshot (RD_TARGET_DATE_SNAPSHOT), pixel for pixel, for sprite_ps
 * to test at t2.  Retaken when the target changed or anything may have
 * written alpha since the last one, so consecutive DATE draws see each
 * other's writes as on the GS (within one draw, overlapping primitives see
 * the snapshot: accepted).  area (texels, NULL = the whole target) holds
 * every fragment the draw can make (rd__screen_area), and the shaders read
 * the snapshot only at the fragment's own texel, so only that area is
 * drawn: the snapshot is right inside r->dateArea and stale outside it, and
 * a draw whose area it does not hold retakes it (rd__date_retake), never
 * reading a stale texel.  closing, ended: the MSAA epoch's state for
 * rd__date_snap (open outside its own transitions; closed this replay).
 * Each snapshot is counted by why it was retaken (RD_DATE_RETAKE_*), or as
 * reused (RdPerfRecord.dateSnapReused). */
RhiTexture rd__date_snapshot(Replay *r, RdTargetRec *tc, uint32_t tcId, const RhiRect *area,
                             bool closing, bool ended)
{
    RdTargetRec *sn = rd__target_rec(RD_TARGET_DATE_SNAPSHOT + 1);
    if (!sn || !sn->color.id || tc == sn) {
        return g_rd.dummy;
    }
    /* in texels: the snapshot has the scene's scale and sprite_ps
     * reads it at the fragment's texel */
    uint32_t w = tc->tw, h = tc->th;
    if (w > sn->tw || h > sn->th) {
        rd__log_once(RD_ONCE_DATE_SIZE, "DATE on a target larger than the snapshot: clipped");
        w = w > sn->tw ? sn->tw : w;
        h = h > sn->th ? sn->th : h;
    }
    RhiRect take;
    const bool retake = rd__date_retake(r->dateFor, r->dateSerial, &r->dateArea, tcId,
                                        r->writeSerial, area, w, h, &take);
    if (!retake && sn->colorState == RHI_STATE_SHADER_READ) {
        g_rdPerf.dateSnapReused++;
        return sn->color;
    }
    const int why = !retake                           ? RD_DATE_RETAKE_TOUCHED
                    : r->dateFor == 0                 ? RD_DATE_RETAKE_FIRST
                    : r->dateFor != tcId              ? RD_DATE_RETAKE_TARGET
                    : r->dateSerial != r->writeSerial ? RD_DATE_RETAKE_WRITTEN
                                                      : RD_DATE_RETAKE_AREA;
    g_rdPerf.dateRetakeWhy[why]++;
    if (!retake) {
        /* the snapshot was touched elsewhere since: nothing of it is known */
        take = (RhiRect){0, 0, w, h};
    }
    RhiCommandList cl = rd__replay_cl();
    rd__replay_end_pass(r);
    rd__perf_pass(cl, RD_PERF_PASS_DATE, tcId, take.w, take.h, r->perfPost);
    g_rdPerf.dateSnapshots++;
    rd__date_snap(tc, closing, ended);
    rd__transition(cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    rd__transition(cl, sn->color, &sn->colorState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = sn->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = sn->tw;
    p.height = sn->th;
    rhi_cmd_begin_render_pass(cl, &p);
    rd__replay_viewport(w, h);
    /* the same full-target triangle and viewport, cut to the area */
    rhi_cmd_set_scissor(cl, &take);
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
    RhiPipeline pipe = rd__get_pipeline(&k);
    if (pipe.id) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.uvRect[2] = (float)w;
        cb.uvRect[3] = (float)h;
        cb.tex[0] = (float)tc->tw;
        cb.tex[1] = (float)tc->th;
        cb.tex[2] = 1.0f / (float)tc->tw;
        cb.tex[3] = 1.0f / (float)tc->th;
        rd__set_pipeline(cl, pipe, &k);
        rd__bind_uniform(cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(
            cl, 2,
            rd__tex_group(tc->color, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                 RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        rd__replay_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, sn->color, &sn->colorState, RHI_STATE_SHADER_READ);
    r->dateFor = tcId;
    r->dateSerial = r->writeSerial;
    r->dateArea = take;
    return sn->color;
}

/* the 10 s line's sums (rd_perf.c rd_perf_pop: each record popped) */
void rd__date_cut_pop(const RdPerfRecord *r)
{
    s_sum.replays++;
    s_sum.passes += r->dateCutPasses;
    s_sum.again += r->dateCutAgain;
    s_sum.texels += r->dateCutTexels;
    s_sum.snaps += r->dateSnapshots;
    s_sum.reused += r->dateSnapReused;
    for (int i = 0; i < RD_DATE_RETAKE_COUNT; i++) {
        s_sum.why[i] += r->dateRetakeWhy[i];
    }
}

void rd__date_cut_text(char *out, size_t n, size_t *at)
{
    if (s_sum.replays && (s_sum.snaps || s_sum.reused || s_sum.passes) && *at < n) {
        const double k = (double)s_sum.replays;
        /* the depth's 4 bytes and the stencil's 1 a texel, each way: what
         * the passes would have asked at most (the store plan may have left
         * the stencil's store out already) */
        const double mb = (double)s_sum.texels * 5.0 / 1048576.0 / k;
        int w = snprintf(out + *at, n - *at,
                         "; DATE: %.1f passes a replay begun without the depth their draws never "
                         "touch (up to %.1f MB of depth and stencil loads and as many stores "
                         "not asked), %.1f begun again with it; snapshots %.1f a replay taken (",
                         (double)s_sum.passes / k, mb, (double)s_sum.again / k,
                         (double)s_sum.snaps / k);
        *at += w > 0 && (size_t)w < n - *at ? (size_t)w : 0;
        for (int i = 0; i < RD_DATE_RETAKE_COUNT && *at < n; i++) {
            w = snprintf(out + *at, n - *at, "%s%s %.1f", i ? ", " : "", kRetakeWhy[i],
                         (double)s_sum.why[i] / k);
            *at += w > 0 && (size_t)w < n - *at ? (size_t)w : 0;
        }
        if (*at < n) {
            w = snprintf(out + *at, n - *at, "), %.1f reused", (double)s_sum.reused / k);
            *at += w > 0 && (size_t)w < n - *at ? (size_t)w : 0;
        }
    }
    memset(&s_sum, 0, sizeof(s_sum));
}

const char *rd__date_retake_word(int why)
{
    return why >= 0 && why < RD_DATE_RETAKE_COUNT ? kRetakeWhy[why] : "none";
}
