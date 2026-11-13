/* rd_replay_tiler.c: the replay's tiler passes (docs/port/RENDER.md, "The
 * replay"): DATE through the stencil bit's setup (rd__replay_date_take,
 * from rd_replay.c bindDrawEx; rd__replay_date_clear, the PRIM.AA1 DATE
 * draws' clear on the snapshot path) and the staticBlur pair fold
 * (rd__replay_blur_fold, from rd_blur_border.c rd__blur_post_sprite; the
 * list loop's look-ahead state, rd__replay_fold_list / _at / _skip).  Both
 * are gated on the device profile (RdProfile.dateStencil, blurFold) and
 * draw the bytes the passes they replace draw.  The replay's own helpers come through rd_replay.h
 * (rd__replay_*, each rd_replay.c's function named beside it). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_date_one.h"
#include "rd_glow_blur.h"
#include "rd_lamps.h"
#include "rd_replay.h"
#include "rd_bench.h" /* the bench's switch (rd__date_setup_bench_skipped) */

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

bool rd__date_sabotaged(void)
{
    static int s_sab = -1; /* read once */
    if (s_sab < 0) {
        const char *e = getenv("ICO_RD_DATE_SABOTAGE");
        s_sab = e && strcmp(e, "1") == 0;
    }
    return s_sab == 1;
}

/* DATE through the stencil bit, in the draw's own pass (begun readable,
 * its depth bound: prepareDrawEx, bindDrawEx): bit 6 of the stencil
 * (RD_DATE_STENCIL_BIT) made the destination alpha's MSB over the texels
 * the draw can touch, as the R8 snapshot holds it (date_snap_ps), unless
 * the bit already holds that in this pass (rd__date_retake's rule on the
 * pass's own record: the same target, nothing changed its alpha since,
 * the area held).  The open screen run first (its draws precede the read);
 * then over the area (the scissor; blit_vs's triangle covers the pass's
 * viewport) date_clear_ps writes 0 into the bit, a by-region barrier makes
 * the pass's colour writes visible, date_stencil_ps keeps the fragments
 * whose alpha has the MSB and writes 1 into the bit there, and a barrier
 * orders that read before the draws' writes.  The DATE draw's passes then
 * test the bit (expandPasses, rd__date_stencil_pass; dstBind sets the
 * reference): the alpha as it was before the draw's first primitive, the
 * snapshot's semantics.  False when a pipeline or a group is missing (the
 * draw is then skipped: dateStencilFor checked the pipelines) */
bool rd__replay_date_take(Replay *r, const DrawSetup *ds)
{
    if (rd__date_setup_bench_skipped()) {
        return true; /* the bench's variant (rd_bench.c): no set-up, the bit as it was */
    }
    RhiCommandList cl = rd__replay_cl();
    RdTargetRec *tc = ds->tc;
    RhiRect take;
    const bool samePass = r->dateStPass == r->passSerial && r->dateStFor != 0;
    /* a shadow composite after one that cannot set the alpha's top bit: the
     * bit holds what a set-up now would write (rd_date_one.c) */
    const uint32_t held = rd__date_one_held(samePass ? r->dateStFor : 0, r->dateStSerial,
                                            r->st.color, r->writeSerial, true);
    const uint64_t texels = (uint64_t)tc->tw * tc->th;
    if (!rd__date_retake(samePass ? r->dateStFor : 0, held, &r->dateStArea, r->st.color,
                         r->writeSerial, ds->dateWantAll ? NULL : &ds->dateWant, tc->tw, tc->th,
                         &take)) {
        rd__date_one_took(r->st.color, r->writeSerial, true, true, texels);
        return true; /* the bit holds the area's alpha */
    }
    if (take.w == 0 || take.h == 0) {
        take = (RhiRect){0, 0, tc->tw, tc->th};
    }
    const RdPipeKeyInt kc = rd__date_setup_key(RD_STENCIL_DATE_CLEAR, tc->format);
    const RdPipeKeyInt ks = rd__date_setup_key(RD_STENCIL_DATE_SET, tc->format);
    RhiPipeline pc = rd__get_pipeline(&kc), ps = rd__get_pipeline(&ks);
    RhiBinding b[2];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_INPUT_ATTACHMENT;
    b[0].texture = tc->color;
    b[1].slot = 2;
    b[1].type = RHI_BIND_SAMPLED_TEXTURE;
    b[1].texture = g_rd.dummy;
    const RhiBindGroup gl = rd__replay_bind_group(g_rd.layoutLocal, b, 2);
    const RhiBindGroup gt =
        rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                              RD_WRAP_CLAMP));
    if ((!pc.id || !ps.id) && rd__pipeline_gathering()) {
        return true; /* the keys named; nothing is drawn while gathering */
    }
    if (!pc.id || !ps.id || !gl.id || !gt.id || !r->passOpen || !r->passReadable) {
        static bool logged; /* once a run */
        if (!logged) {
            logged = true;
            rd__log("DATE: the stencil bit could not be written (a setup pipeline, its group or "
                    "the readable pass missing): the draw is skipped (reported once)");
        }
        return false;
    }
    rd__replay_flush_run(r); /* the run's draws before the read */
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = (float)tc->tw;
    cb.uvRect[3] = (float)tc->th;
    cb.tex[0] = (float)tc->tw;
    cb.tex[1] = (float)tc->th;
    cb.tex[2] = 1.0f / (float)tc->tw;
    cb.tex[3] = 1.0f / (float)tc->th;
    rhi_cmd_set_scissor(cl, &take);
    rd__set_pipeline(cl, pc, &kc);
    rd__bind_uniform(cl, 0, rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, gt);
    rhi_cmd_set_stencil_ref(cl, 0);
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_COLOR);
    rd__set_pipeline(cl, ps, &ks);
    rd__bind_uniform(cl, 0, rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, gl);
    /* ICO_RD_DATE_SABOTAGE: the wrong bit, 0 where the MSB is 1 */
    rhi_cmd_set_stencil_ref(cl, rd__date_sabotaged() ? 0u : RD_DATE_STENCIL_BIT);
    rd__replay_draw(cl, 3, 0, 1);
    rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_BACK);
    g_rdPerf.dateStencilTakes++; /* the draws after it set their own scissor */
    r->dateStFor = r->st.color;
    r->dateStSerial = r->writeSerial;
    r->dateStArea = take;
    r->dateStPass = r->passSerial;
    rd__date_one_took(r->st.color, r->writeSerial, true, false, texels);
    return true;
}

/* PRIM.AA1 under DATE on the snapshot path (rd_replay_aa1.inc
 * aa1DateDraw): bit 6 of the stencil cleared over area (texels, NULL: the
 * whole target) in the open pass, date_clear_ps under RD_STENCIL_DATE_CLEAR
 * as rd__replay_date_take clears it, so that the draw's primitives can
 * mark in it the texels where one of them changed the alpha's MSB.  The
 * stencil path's record of the bit is dropped (the bit no longer holds the
 * alpha there).  The caller's draws set their own scissor.  False when the
 * setup's pipeline or group is missing or no pass is open (nothing drawn) */
bool rd__replay_date_clear(Replay *r, const DrawSetup *ds, const RhiRect *area)
{
    RhiCommandList cl = rd__replay_cl();
    RdTargetRec *tc = ds->tc;
    const RdPipeKeyInt kc = rd__date_setup_key(RD_STENCIL_DATE_CLEAR, tc->format);
    const RhiPipeline pc = rd__get_pipeline(&kc);
    const RhiBindGroup gt =
        rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                              RD_WRAP_CLAMP));
    if (!pc.id || !gt.id || !r->passOpen) {
        return false;
    }
    RhiRect take = {0, 0, tc->tw, tc->th};
    if (area && area->w && area->h && area->x >= 0 && area->y >= 0 && (uint32_t)area->x < tc->tw &&
        (uint32_t)area->y < tc->th) {
        const uint32_t x = (uint32_t)area->x, y = (uint32_t)area->y;
        take = *area;
        take.w = x + take.w > tc->tw ? tc->tw - x : take.w;
        take.h = y + take.h > tc->th ? tc->th - y : take.h;
    }
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = (float)tc->tw;
    cb.uvRect[3] = (float)tc->th;
    cb.tex[0] = (float)tc->tw;
    cb.tex[1] = (float)tc->th;
    cb.tex[2] = 1.0f / (float)tc->tw;
    cb.tex[3] = 1.0f / (float)tc->th;
    rhi_cmd_set_scissor(cl, &take);
    rd__set_pipeline(cl, pc, &kc);
    rd__bind_uniform(cl, 0, rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, gt);
    rhi_cmd_set_stencil_ref(cl, 0);
    rd__replay_draw(cl, 3, 0, 1);
    r->dateStFor = 0;
    return true;
}

/* ------------------------------------------------------ the blur pair fold
 * staticBlur.c's blur(n) is two sprites, pass 1 WORK0 -> WORK1 (the rect
 * shrunk) and pass 2 WORK1 -> WORK0 (the UVs shrunk), n = 10..19 (RdProfile
 * .blurFold: every device class but Mali, whose ALU-bound G52 the fold does
 * not spare; [dev] blur_fold).  rd__replay_blur_fold draws such a pair as
 * one pass into WORK0 through fx_blur2_ps, which computes each WORK1 texel
 * pass 2 weighs from WORK0 the way pass 1 would have written it: the two
 * passes' integers in their order, so WORK0 gets the bytes the two passes
 * give.  WORK1 is not drawn; the pair drawn last as two passes (the next
 * post sprite is not another pass 1) writes it as the chain did.  The
 * replay recognises a pair at its pass 1 by looking ahead over the list's
 * commands: state commands up to pass 2 (applied to a copy of the state),
 * state commands up to the next pair's pass 1.  Every condition below makes
 * the fold exact by construction (blurFoldPass, blurFoldOk); a pair that
 * misses one is drawn as two passes.  The loop then skips the commands up
 * to pass 2 (s_loop.skip), their state applied.
 *
 * On a frame wider than 512 GS pixels pass 1's rect starts d = 1..50
 * sixteenths right of WORK1's pixel 0 (staticBlur.c anchors its shrink at
 * the 4:3 part), so it draws from column cx0 = ceil(d / 16) (1 at 16:9, 2
 * or 3 at 48:9, up to 4 at 60:9), while pass 2's first pixel weighs
 * column 0 (13/16 at 16:9).  Those columns hold the glow's edge strip
 * (rd_blur_border.c glowEdge), drawn after every pass 1 drawn as a pass:
 * pass 1's texel at column cx0 on each of its rows.  So a pass 1 with the
 * strip leaves its bytes in columns 0 .. cx1 (BlurFoldPass.first 0), and
 * fx_blur2_ps reads a tap left of cx0 as pass 1's pixel at cx0 (DrawCB
 * mode.w); without the strip (--no-glow-edge) first is cx0 and such a
 * pair is drawn as two passes.  WORK1 after the chain: each folded pair's written texels
 * (columns first .. cx1, rows cy0 .. cy1) lie inside the next pair's
 * (blurFoldOk's look-ahead), so inside those of the first pair after it
 * drawn as two passes, whose pass 1 (and strip) writes them before its
 * pass 2 reads WORK1 and leaves them as the unfolded chain does; a texel
 * outside is written by no folded pair either way.  At 4:3 every pair
 * covers columns 0..254 (9 of 10 fold, as before); at 16:9 (688) 1..342
 * with the strip on column 0 (9 of 10); at 48:9 (2048) and 60:9 (2560)
 * the last column moves in by one at one pair (cx1 1021 -> 1020, 1276 ->
 * 1275), which the next-pair rule refuses there (8 of 10).  The fold
 * looks for a pair at every staticBlur sprite, so after a pair drawn as
 * two passes it also sees (pass 2 of blur(n), pass 1 of blur(n + 1)), the
 * same shape with WORK0 as the intermediate; folded, that draws pass 1 of
 * blur(n + 1) without the strip after it, and pass 2 of blur(n + 1) would
 * read the strip's columns as an earlier pass left them.  So a fold whose
 * drawn pass the strip would follow is refused (on a wide frame the
 * unfixed rule folded nine such shifted pairs at 16:9 in place of the
 * true ones). */
typedef struct BlurFoldPass {
    RdPostRec p;
    float uv[4];
    RdTargetRec *tc, *src;
    uint32_t flags, mode1, tfmt;
    int32_t ox, oy; /* the origin FrameCB gives the target (12.4) */
    /* the texels the sprite covers (its rect and scissor), inclusive */
    int32_t cx0, cy0, cx1, cy1;
    int32_t sx; /* the scissor's first column */
    /* the first WORK1 column that holds this pass's bytes after it: 0 where
     * the glow's edge strip draws columns 0 .. cx0 - 1 as column cx0
     * (rd__replay_glow_edge_columns, set by rd__replay_blur_fold for a pass
     * 1), else cx0 */
    int32_t first;
} BlurFoldPass;

/* the loop's list for the look-ahead (replayFrame): its commands, the
 * index of the one being replayed, the list number, the frame head's half
 * pixel, and the commands the replay of it consumed after it */
static struct {
    const RdCmd *cmds;
    uint32_t count, index;
    int list;
    uint32_t headHalf;
    uint32_t skip;
} s_loop;

void rd__replay_fold_list(const RdCmd *cmds, uint32_t count, int list, uint32_t headHalf)
{
    s_loop.cmds = cmds;
    s_loop.count = cmds ? count : 0;
    s_loop.list = list;
    s_loop.headHalf = headHalf;
    s_loop.index = 0;
    s_loop.skip = 0;
}

void rd__replay_fold_at(uint32_t index)
{
    s_loop.index = index;
    s_loop.skip = 0;
    if (index < s_loop.count) {
        rd__const_cmd(&s_loop.cmds[index]); /* the actions walked (rd_const_read.c) */
    }
}

uint32_t rd__replay_fold_skip(void)
{
    return s_loop.skip;
}

static int32_t ceilDiv16(int32_t v)
{
    return -floorDiv16(-v);
}

/* A staticBlur sprite (the post stub c under the state of q) that a fold
 * can take as one of its passes: textured from another RGBA8 target, both
 * at scale 1 without a crop (their sizes may differ: WORK1 is 256 x 256,
 * WORK0 256 x 128), LINEAR, CLAMP on both axes, no blend
 * (FIX 128's normalised away as doBlurSpriteDraw does), no alpha test, no
 * DATE, no destination read, no depth bound, the colour mask whole, its
 * rect and UVs 16-bit integers; *out its fields and covered texels.  False
 * otherwise. */
static bool blurFoldPass(const Replay *q, const RdFrame *f, const RdCmd *c, BlurFoldPass *out)
{
    memset(out, 0, sizeof(*out));
    if (c->type != RDC_POST_STUB || !rd__is_blur_kind(c->b[0]) || c->b[0] == RD_POST_REDUCTION) {
        return false;
    }
    memcpy(&out->p, f->payload + c->u[1], sizeof(out->p));
    const RdPostRec *p = &out->p;
    if (rd__blur_screen_fallback(c->b[0], p, &q->st) != ~0u) {
        return false;
    }
    const RdStateBlock *st = &q->st;
    const RdDrawState *d = &st->ds;
    const bool abe = d->abe && !(rd__replay_blur_fix128_skip() && d->blend == RD_BLEND_LERP_FIX &&
                                 d->blendFix == 128);
    RdTargetRec *tc = rd__target_rec(st->color);
    if (!tc || !tc->color.id || tc->format != RHI_FMT_RGBA8_UNORM || tc->sx != 1.0f ||
        tc->sy != 1.0f || tc->ox != 0.0f || tc->tw != tc->w || tc->th != tc->h || abe ||
        d->test.date != RD_DATE_OFF || d->test.ate || d->colorMask != 0xF || d->fbmsk != 0 ||
        !d->texEnabled || d->magFilter != RD_FILTER_LINEAR || d->wrap.s != RD_WRAP_CLAMP ||
        d->wrap.t != RD_WRAP_CLAMP) {
        return false;
    }
    RdTargetRec *td = rd__target_rec(st->depth);
    if (td && (!td->withDepth || !td->depth.id || td->tw != tc->tw || td->th != tc->th)) {
        td = NULL;
    }
    int useDepth = 0;
    (void)rd__blur_key(st, tc->format, td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN, &useDepth, 0);
    const RdTexRec *t = rd__tex_rec(st->tex);
    RdTargetRec *src = t && t->kind == RD_TEXKIND_TARGET && t->view != RD_VIEW_DEPTH
                           ? rd__target_rec(t->target)
                           : NULL;
    if (useDepth || !src || src == tc || !src->color.id || src->format != RHI_FMT_RGBA8_UNORM ||
        src->sx != 1.0f || src->sy != 1.0f || src->ox != 0.0f || src->tw != src->w ||
        src->th != src->h) {
        return false;
    }
    rd__blur_uv_rect(c->b[0], p, out->uv);
    for (int k = 0; k < 4; k++) {
        const float a = p->rect[k], u = out->uv[k];
        if (!(a >= 0.0f && a <= 65535.0f && a == floorf(a) && u >= 0.0f && u <= 65535.0f &&
              u == floorf(u))) {
            return false;
        }
    }
    if (p->rect[2] <= p->rect[0] || p->rect[3] <= p->rect[1] || p->scalar[0] < 1.0f ||
        p->scalar[1] < 1.0f) {
        return false;
    }
    /* the origin and the coverage as fx_sprite_ps has them at scale 1 (the
     * pixel's 12.4 coordinate origin + 16 px), the scissor bindDraw sets */
    const uint32_t use = q->halfAdded ? st->useOffset & ~(uint32_t)RD_TARGET_HALF_Y : st->useOffset;
    out->ox = 16 * (2048 - (int32_t)(st->gsW >> 1));
    out->oy = 16 * (2048 - (int32_t)(st->gsH >> 1)) + ((use & RD_TARGET_HALF_Y) ? 8 : 0);
    RhiRect sc;
    if (!rd__replay_scissor(q, tc, &sc)) {
        return false;
    }
    const int32_t x0 = (int32_t)p->rect[0], y0 = (int32_t)p->rect[1];
    const int32_t x1 = (int32_t)p->rect[2], y1 = (int32_t)p->rect[3];
    int32_t a0 = ceilDiv16(x0 - out->ox), a1 = ceilDiv16(x1 - out->ox) - 1;
    int32_t b0 = ceilDiv16(y0 - out->oy), b1 = ceilDiv16(y1 - out->oy) - 1;
    a0 = a0 > sc.x ? a0 : sc.x;
    b0 = b0 > sc.y ? b0 : sc.y;
    a1 = a1 < sc.x + (int32_t)sc.w - 1 ? a1 : sc.x + (int32_t)sc.w - 1;
    b1 = b1 < sc.y + (int32_t)sc.h - 1 ? b1 : sc.y + (int32_t)sc.h - 1;
    if (a1 < a0 || b1 < b0) {
        return false;
    }
    out->cx0 = a0;
    out->cx1 = a1;
    out->cy0 = b0;
    out->cy1 = b1;
    out->sx = sc.x;
    out->first = a0;
    out->tc = tc;
    out->src = src;
    out->tfmt = t->src;
    RdDrawState dn = *d; /* the flags doBlurSpriteDraw computes (FIX 128 normalised) */
    dn.abe = 0;
    out->flags = rd__replay_blur_flags(&dn, p->lines, 1, 0);
    out->mode1 = d->texa | (t->src << 8);
    return true;
}

/* fx_sprite_ps's U at the pixel of 12.4 coordinate X on one axis: u0 +
 * (X - x0) (u1 - u0) / (x1 - x0), truncated toward zero */
static int32_t blurFoldU(int32_t X, int32_t x0, int32_t x1, int32_t u0, int32_t u1)
{
    return u0 + (int32_t)(((int64_t)(X - x0) * (u1 - u0)) / (x1 - x0));
}

/* The texels pass 2 weighs on one axis (a weight above 0: from floor((U -
 * 8) / 16) to ceil((U - 8) / 16) at each covered pixel, monotonic in U),
 * clamped as fx_texel clamps (CLAMP on the TEX0 size lsz, then the texture
 * asz): inside [lo, hi], the texels that hold pass 1's bytes (its covered
 * ones, on x from column 0 with the glow's edge strip).  And the bound
 * fx2_pass1's gs_div_trunc needs (|n| < d 2^21) at every texel of pass 1's
 * texture. */
static bool blurFoldAxis(int32_t c0, int32_t c1, int32_t o, int32_t x0, int32_t x1, int32_t u0,
                         int32_t u1, int32_t lsz, int32_t asz, int32_t lo, int32_t hi)
{
    const int32_t ua = blurFoldU(o + 16 * c0, x0, x1, u0, u1);
    const int32_t ub = blurFoldU(o + 16 * c1, x0, x1, u0, u1);
    const int32_t umin = ua < ub ? ua : ub, umax = ua > ub ? ua : ub;
    int32_t t0 = floorDiv16(umin - 8), t1 = ceilDiv16(umax - 8);
    t0 = t0 < 0 ? 0 : (t0 > lsz - 1 ? lsz - 1 : t0);
    t1 = t1 < 0 ? 0 : (t1 > lsz - 1 ? lsz - 1 : t1);
    t0 = t0 > asz - 1 ? asz - 1 : t0;
    t1 = t1 > asz - 1 ? asz - 1 : t1;
    return t0 >= lo && t1 <= hi;
}

/* pass 1's divide bound at every WORK1 texel X = o + 16 c, c in 0 .. asz - 1
 * (the taps fx2_pass1 computes, clamped into WORK1) */
static bool blurFoldBound(int32_t o, int32_t x0, int32_t x1, int32_t u0, int32_t u1, int32_t asz)
{
    const int64_t e0 = (int64_t)o - x0, e1 = (int64_t)o + 16 * ((int64_t)asz - 1) - x0;
    const int64_t m =
        (e0 < 0 ? -e0 : e0) > (e1 < 0 ? -e1 : e1) ? (e0 < 0 ? -e0 : e0) : (e1 < 0 ? -e1 : e1);
    const int64_t du = (int64_t)u1 - u0;
    return m * (du < 0 ? -du : du) < ((int64_t)x1 - x0) * (1ll << 21);
}

/* (a, b) is a pair: b reads a's target and writes its source, the same
 * flags, TEXA, TEX0 size and origin */
static bool blurFoldPair(const BlurFoldPass *a, const BlurFoldPass *b)
{
    return b->src == a->tc && b->tc == a->src && b->flags == a->flags && b->mode1 == a->mode1 &&
           a->ox == b->ox && a->oy == b->oy && a->p.scalar[0] == b->p.scalar[0] &&
           a->p.scalar[1] == b->p.scalar[1];
}

/* the pair (a, b) folds into one pass: b's every weighed WORK1 texel among
 * those holding a's bytes (a->first .. cx1, cy0 .. cy1), the bound of a's
 * divides */
static bool blurFoldOk(const BlurFoldPass *a, const BlurFoldPass *b)
{
    const int32_t lszx = (int32_t)rintf(b->p.scalar[0]), lszy = (int32_t)rintf(b->p.scalar[1]);
    const RdTargetRec *w1 = a->tc;
    return blurFoldAxis(b->cx0, b->cx1, b->ox, (int32_t)b->p.rect[0], (int32_t)b->p.rect[2],
                        (int32_t)b->uv[0], (int32_t)b->uv[2], lszx, (int32_t)w1->tw, a->first,
                        a->cx1) &&
           blurFoldAxis(b->cy0, b->cy1, b->oy, (int32_t)b->p.rect[1], (int32_t)b->p.rect[3],
                        (int32_t)b->uv[1], (int32_t)b->uv[3], lszy, (int32_t)w1->th, a->cy0,
                        a->cy1) &&
           blurFoldBound(a->ox, (int32_t)a->p.rect[0], (int32_t)a->p.rect[2], (int32_t)a->uv[0],
                         (int32_t)a->uv[2], (int32_t)w1->tw) &&
           blurFoldBound(a->oy, (int32_t)a->p.rect[1], (int32_t)a->p.rect[3], (int32_t)a->uv[1],
                         (int32_t)a->uv[3], (int32_t)w1->th);
}

/* a pass 1 (c under r's state, *p from blurFoldPass): p->first 0 where the
 * glow's edge strip draws WORK1's columns 0 .. cx0 - 1 after it as its
 * column cx0 (its columns exactly: the strip's n is cx0 and the scissor
 * starts at column 0), else cx0 */
static void blurFoldFirst(const Replay *r, const RdFrame *f, const RdCmd *c, BlurFoldPass *p)
{
    const int n = rd__replay_glow_edge_columns(r, f, c);
    p->first = n > 0 && n == p->cx0 && p->sx == 0 ? 0 : p->cx0;
}

/* A pair the profile would fold drawn as two passes: once a WORK1 width
 * and reason, the pairs folded before it in this replay (a reporter's log
 * says how many of a chain's ten pairs fold on that frame) */
static void blurFoldRefused(const Replay *r, const BlurFoldPass *p1, const char *why)
{
    static struct {
        uint32_t w;
        const char *why;
    } s_logged[12];

    const uint32_t w = p1->tc->tw;
    for (size_t k = 0; k < sizeof(s_logged) / sizeof(s_logged[0]); k++) {
        if (s_logged[k].w == w && s_logged[k].why == why) {
            return;
        }
        if (s_logged[k].w == 0) {
            s_logged[k].w = w;
            s_logged[k].why = why;
            rd__log("blur fold: %s %ux%u: a pair drawn as two passes (%s) after %u folded in "
                    "this replay; pass 1 draws columns %d..%d, holds its bytes from column %d "
                    "(reported once a width and reason)",
                    rd__target_name(r->st.color), (unsigned)w, (unsigned)p1->tc->th, why,
                    (unsigned)g_rdPerf.blurFolds, (int)p1->cx0, (int)p1->cx1, (int)p1->first);
            return;
        }
    }
}

/* the commands after s_loop.cmds[from] up to the next one that is not a
 * state command, applied to q (the loop's effects of a TARGET with them);
 * its index, or s_loop.count */
static uint32_t blurFoldAhead(Replay *q, uint32_t from)
{
    uint32_t i = from + 1;
    for (; i < s_loop.count; i++) {
        const RdCmd *x = &s_loop.cmds[i];
        if (x->type == RDC_TARGET) {
            q->perfPost = q->perfTargetEffect = rd__replay_perf_target_post(q, x);
        }
        if (!rd__apply_state(&q->st, x)) {
            break;
        }
        if (x->type == RDC_TARGET) {
            q->halfAdded = rd__replay_scene_keeps_half(&q->st, s_loop.list, s_loop.headHalf);
        }
    }
    return i;
}

/* The fold's DrawCB for the pair (a pass 1, b its pass 2) under st, as
 * fx_blur2_ps reads it (fx_sprite.hlsl; shader_consts.h IcoDrawCB.aux0) */
static void foldCb(IcoDrawCB *cb, const BlurFoldPass *a, const BlurFoldPass *b,
                   const RdStateBlock *st)
{
    memset(cb, 0, sizeof(*cb));
    memcpy(cb->col, (const uint32_t[4]){b->p.rgba[0], b->p.rgba[1], b->p.rgba[2], b->p.rgba[3]},
           sizeof(cb->col));
    cb->mode[0] = b->flags;
    cb->mode[1] = b->mode1;
    cb->mode[2] = st->ds.test.atst | ((uint32_t)(st->ds.test.ate != 0) << 8) |
                  ((uint32_t)st->ds.test.afail << 16);
    /* no alpha test here (blurFoldPass), so not AREF: pass 1's first drawn
     * WORK1 column, which a tap left of it reads (the glow's edge strip;
     * with first == cx0 only taps of weight 0 lie there) */
    cb->mode[3] = (uint32_t)a->cx0;
    cb->blend[0] =
        rd__alpha_register(st->ds.blend < RD_BLEND_COUNT ? st->ds.blend : RD_BLEND_LERP_AS);
    cb->blend[1] = st->ds.blendFix;
    cb->blend[2] = st->ds.colclamp;
    cb->blend[3] = b->p.z;
    memcpy(cb->uvRect, b->uv, sizeof(cb->uvRect));
    cb->tex[0] = b->p.scalar[0];
    cb->tex[1] = b->p.scalar[1];
    cb->tex[2] = (float)b->src->tw; /* pass 2's t1: WORK1 */
    cb->tex[3] = (float)b->src->th;
    cb->scale[0] = 1.0f;
    cb->scale[1] = 1.0f;
    memcpy(cb->param, b->p.rect, sizeof(cb->param));
    rd__replay_blur_aux1(cb);         /* pass 2's reciprocals and fx_texel's size (scale[0..1] 1) */
    cb->scale[0] = (float)a->src->tw; /* pass 1's t1: WORK0, its fetch's clamp */
    cb->scale[1] = (float)a->src->th;
    /* pass 1: its rect and UVs as 16-bit pairs, its colour in scale.zw */
    cb->aux0[0] = (uint32_t)a->p.rect[0] | ((uint32_t)a->p.rect[1] << 16);
    cb->aux0[1] = (uint32_t)a->p.rect[2] | ((uint32_t)a->p.rect[3] << 16);
    cb->aux0[2] = (uint32_t)a->uv[0] | ((uint32_t)a->uv[1] << 16);
    cb->aux0[3] = (uint32_t)a->uv[2] | ((uint32_t)a->uv[3] << 16);
    cb->scale[2] = (float)(a->p.rgba[0] + 256u * a->p.rgba[1]);
    cb->scale[3] = (float)(a->p.rgba[2] + 256u * a->p.rgba[3]);
}

/* fx_blur4_ps's pair A in g_blend (no blend in a fold; fx_sprite.hlsl
 * fx_blur4_ps): pass 1A's x0, x1, y1, pass 2A's u0, u1, v1 and pass 1A's
 * first drawn WORK1 column; the rest of pair A is pair B's (fold4Pair) */
static void fold4Cb(IcoDrawCB *cb, const BlurFoldPass *a1, const BlurFoldPass *a2)
{
    cb->blend[0] = (uint32_t)a1->p.rect[0] | ((uint32_t)a1->p.rect[2] << 16);
    cb->blend[1] = (uint32_t)a1->p.rect[3] | ((uint32_t)a2->uv[0] << 16);
    cb->blend[2] = (uint32_t)a2->uv[2] | ((uint32_t)a2->uv[3] << 16);
    cb->blend[3] = (uint32_t)a1->cx0;
}

/* the poison's magenta over WORK0's swapped-in colour (the tests) */
static void fold4Poison(RhiCommandList cl, RdTargetRec *tc)
{
    const bool readable = rd__pass_wants_readable(tc);
    rd__transition(cl, tc->color, &tc->colorState,
                   readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc q;
    memset(&q, 0, sizeof(q));
    q.color[0].texture = tc->color;
    q.color[0].load = RHI_LOAD_CLEAR;
    q.color[0].readable = readable;
    q.color[0].clear[0] = 1.0f;
    q.color[0].clear[2] = 1.0f;
    q.color[0].clear[3] = 1.0f;
    q.colorCount = 1;
    q.width = tc->tw;
    q.height = tc->th;
    rhi_cmd_begin_render_pass(cl, &q);
    rhi_cmd_end_render_pass(cl);
}

/* one axis of the two pairs for rd__lamps_fold4_axis: c0 .. c1 pass 2B's
 * covered pixels, the passes' rect and UV on the axis (index 0 x, 1 y) */
static void fold4Axis(RdFold4Axis *ax, int k, const BlurFoldPass *a1, const BlurFoldPass *a2,
                      const BlurFoldPass *b1, const BlurFoldPass *b2)
{
    memset(ax, 0, sizeof(*ax));
    ax->o = k ? b2->oy : b2->ox;
    ax->c0 = k ? b2->cy0 : b2->cx0;
    ax->c1 = k ? b2->cy1 : b2->cx1;
    ax->b2x0 = (int32_t)b2->p.rect[k];
    ax->b2x1 = (int32_t)b2->p.rect[k + 2];
    ax->b2u0 = (int32_t)b2->uv[k];
    ax->b2u1 = (int32_t)b2->uv[k + 2];
    ax->b1x0 = (int32_t)b1->p.rect[k];
    ax->b1x1 = (int32_t)b1->p.rect[k + 2];
    ax->b1u0 = (int32_t)b1->uv[k];
    ax->b1u1 = (int32_t)b1->uv[k + 2];
    ax->a2u0 = (int32_t)a2->uv[k];
    ax->a2u1 = (int32_t)a2->uv[k + 2];
    ax->a1x0 = (int32_t)a1->p.rect[k];
    ax->a1x1 = (int32_t)a1->p.rect[k + 2];
    ax->lsz = (int32_t)rintf(b2->p.scalar[k]);
    ax->w0 = (int32_t)(k ? b1->src->th : b1->src->tw);
    ax->w1 = (int32_t)(k ? b2->src->th : b2->src->tw);
    ax->loB = k ? 0 : b1->cx0;
    ax->loA = k ? 0 : a1->cx0;
}

/* the two pairs' DrawCB holds: pair A differs from pair B only in the six
 * values fold4Cb packs, each a 16-bit integer (blurFoldPass checked that) */
static bool fold4Form(const BlurFoldPass *a1, const BlurFoldPass *a2, const BlurFoldPass *b1,
                      const BlurFoldPass *b2)
{
    return a1->tc == b1->tc && a1->src == b1->src && a2->tc == b2->tc && a2->src == b2->src &&
           a1->flags == b1->flags && a1->flags == b2->flags && a2->flags == b2->flags &&
           a1->mode1 == b1->mode1 && a2->mode1 == b2->mode1 && a1->mode1 == a2->mode1 &&
           a1->ox == b2->ox && a1->oy == b2->oy && a2->ox == b2->ox && a2->oy == b2->oy &&
           a1->p.rect[1] == b1->p.rect[1] && memcmp(a1->uv, b1->uv, sizeof(a1->uv)) == 0 &&
           memcmp(a1->p.rgba, b1->p.rgba, sizeof(a1->p.rgba)) == 0 &&
           memcmp(a2->p.rect, b2->p.rect, sizeof(a2->p.rect)) == 0 && a2->uv[1] == b2->uv[1] &&
           memcmp(a2->p.rgba, b2->p.rgba, sizeof(a2->p.rgba)) == 0 &&
           a2->p.scalar[0] == b2->p.scalar[0] && a2->p.scalar[1] == b2->p.scalar[1] &&
           a2->cx0 == b2->cx0 && a2->cy0 == b2->cy0 && a2->cx1 == b2->cx1 && a2->cy1 == b2->cy1;
}

/* Pair A (p1, p2: folding as one pass) and the pair after it (B: p3, its
 * pass 1, and the pass 2 after it) as one pass of fx_blur4_ps: pair B folds
 * as a pair does (blurFoldPair, no glow edge strip after its pass 2,
 * blurFoldOk, the pair after it, C, holding pass 1B's texels or more),
 * the DrawCB's form (fold4Form), the windows (rd__lamps_fold4_axis on each
 * axis).  True: *p4 pass 2B, *q4 the state after it, *i4 its command */
static bool fold4Pair(const RdCmd *c, const RdFrame *f, const BlurFoldPass *p1,
                      const BlurFoldPass *p2, const BlurFoldPass *p3, const Replay *q3, uint32_t i3,
                      BlurFoldPass *p4, Replay *q4, uint32_t *i4)
{
    if (!(rd__lamps_bits() & RD_LAMPS_FOLD4) || !g_rd.fs[RD_FS_FX_BLUR4].id ||
        rd__blur_fold4_test_state(NULL) != 1) {
        return false;
    }
    int why = RD_LAMPS_F4_PAIR;
    *q4 = *q3;
    const uint32_t j4 = blurFoldAhead(q4, i3);
    if (j4 < s_loop.count && blurFoldPass(q4, f, &s_loop.cmds[j4], p4) &&
        s_loop.cmds[j4].b[0] == c->b[0] && blurFoldPair(p3, p4)) {
        why = RD_LAMPS_F4_STRIP;
        if (rd__replay_glow_edge_columns(q4, f, &s_loop.cmds[j4]) == 0) {
            why = RD_LAMPS_F4_PAIR;
            if (blurFoldOk(p3, p4)) {
                why = RD_LAMPS_F4_LAST;
                static Replay q5;
                q5 = *q4;
                const uint32_t j5 = blurFoldAhead(&q5, j4);
                BlurFoldPass p5;
                if (j5 < s_loop.count && blurFoldPass(&q5, f, &s_loop.cmds[j5], &p5) &&
                    s_loop.cmds[j5].b[0] == c->b[0] && p5.tc == p3->tc && p5.src == p3->src) {
                    blurFoldFirst(&q5, f, &s_loop.cmds[j5], &p5);
                    if (p5.first <= p3->first && p5.cx1 >= p3->cx1 && p5.cy0 <= p3->cy0 &&
                        p5.cy1 >= p3->cy1) {
                        why = RD_LAMPS_F4_FORM;
                        if (fold4Form(p1, p2, p3, p4)) {
                            why = RD_LAMPS_F4_WINDOW;
                            RdFold4Axis ax, ay;
                            int32_t w2 = 0, w1 = 0;
                            fold4Axis(&ax, 0, p1, p2, p3, p4);
                            fold4Axis(&ay, 1, p1, p2, p3, p4);
                            if (rd__lamps_fold4_axis(&ax, &w2, &w1) &&
                                rd__lamps_fold4_axis(&ay, &w2, &w1)) {
                                *i4 = j4;
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    rd__lamps_fold4_refused(why);
    return false;
}

/* ---------------------------------------------- the fold of two pairs' self-test
 * (rd_lamps.h rd__blur_fold4_selftest): pair A and pair B of the chain's
 * shrink form on a 64 x 32 WORK0 and a 64 x 64 WORK1 of WORK0's usage, a
 * random WORK0 (a fixed seed), drawn as two fx_blur2_ps passes and as one
 * fx_blur4_ps pass, read back and compared texel for texel */
#define F4_W 64u
#define F4_H 32u

static struct {
    int state; /* 0 not run, 1 passed, 2 failed or not run */
    char words[160];
} s_f4;

int rd__blur_fold4_test_state(const char **words)
{
    if (words) {
        *words = s_f4.words[0] ? s_f4.words : "not run";
    }
    return s_f4.state;
}

static void f4Pass(BlurFoldPass *p, RdTargetRec *tc, RdTargetRec *src, int one, int b)
{
    memset(p, 0, sizeof(*p));
    p->tc = tc;
    p->src = src;
    p->ox = 16 * (2048 - (int32_t)(F4_W / 2u));
    p->oy = 16 * (2048 - (int32_t)(F4_H / 2u));
    const float ox = (float)p->ox, oy = (float)p->oy;
    /* the chain's shrink (staticBlur.c blur(n), list 7:114 to 7:304): pass 1
     * draws a rect a sixteenth narrower and shorter each pair, pass 2 reads
     * UVs a sixteenth less each pair */
    if (one) {
        p->p.rect[0] = ox;
        p->p.rect[1] = oy;
        p->p.rect[2] = ox + 16.0f * F4_W - 16.0f - (float)b;
        p->p.rect[3] = oy + 16.0f * F4_H - 16.0f - (float)b;
        p->uv[0] = 8.0f;
        p->uv[1] = 8.0f;
        p->uv[2] = 8.0f + 16.0f * F4_W;
        p->uv[3] = 8.0f + 16.0f * F4_H;
        p->p.rgba[0] = p->p.rgba[1] = p->p.rgba[2] = 128;
    } else {
        p->p.rect[0] = ox;
        p->p.rect[1] = oy;
        p->p.rect[2] = ox + 16.0f * F4_W;
        p->p.rect[3] = oy + 16.0f * F4_H;
        p->uv[0] = 8.0f;
        p->uv[1] = 8.0f;
        p->uv[2] = 16.0f * F4_W - 8.0f - (float)b;
        p->uv[3] = 16.0f * F4_H - 8.0f - (float)b;
        p->p.rgba[0] = 139;
        p->p.rgba[1] = 136;
        p->p.rgba[2] = 132;
    }
    p->p.scalar[0] = (float)F4_W;
    p->p.scalar[1] = (float)F4_H;
    p->flags = RD_FXF_TEXTURED | (2u << RD_FXF_TFX_SHIFT) | RD_FXF_TCC | RD_FXF_LINEAR |
               RD_FXF_CLAMP_S | RD_FXF_CLAMP_T;
    p->mode1 = 0;
    const int32_t x0 = (int32_t)p->p.rect[0], x1 = (int32_t)p->p.rect[2];
    const int32_t y0 = (int32_t)p->p.rect[1], y1 = (int32_t)p->p.rect[3];
    p->cx0 = ceilDiv16(x0 - p->ox);
    p->cx1 = ceilDiv16(x1 - p->ox) - 1;
    p->cy0 = ceilDiv16(y0 - p->oy);
    p->cy1 = ceilDiv16(y1 - p->oy) - 1;
    p->cx1 = p->cx1 < (int32_t)tc->tw - 1 ? p->cx1 : (int32_t)tc->tw - 1;
    p->cy1 = p->cy1 < (int32_t)tc->th - 1 ? p->cy1 : (int32_t)tc->th - 1;
    p->first = p->cx0;
}

static uint8_t f4Level(uint32_t i)
{
    uint32_t x = i * 2654435761u + 0x9E3779B9u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return (uint8_t)(x >> 24);
}

/* one fold pass (two of fx_blur2_ps's or one of fx_blur4_ps's) from src into dst */
static bool f4Draw(RhiCommandList cl, RhiTexture srcTex, RhiTexture dst, RhiState *dstState,
                   const BlurFoldPass *a1, const BlurFoldPass *a2, const BlurFoldPass *b1,
                   const BlurFoldPass *b2, RhiFormat fmt)
{
    RdStateBlock st;
    rd__reset_state_block(&st);
    st.ds.colorMask = 0xF;
    st.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
    st.ds.zwrite = RD_ZWRITE_OFF;
    RdPipeKeyInt key = rd__blur_fold_key(&st, fmt);
    if (b1) {
        key.fs = RD_FS_FX_BLUR4;
    }
    const RhiPipeline pipe = rd__get_pipeline(&key);
    if (!pipe.id) {
        return false;
    }
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_CLEAR; /* every texel then written by the draw */
    p.colorCount = 1;
    p.width = F4_W;
    p.height = F4_H;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)F4_W, (float)F4_H, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, F4_W, F4_H};
    rhi_cmd_set_scissor(cl, &all);
    IcoDrawCB cb;
    foldCb(&cb, b1 ? b1 : a1, b1 ? b2 : a2, &st);
    if (b1) {
        fold4Cb(&cb, a1, a2);
    }
    RhiBinding bd[3];
    memset(bd, 0, sizeof(bd));
    bd[0].slot = 1;
    bd[0].type = RHI_BIND_SAMPLED_TEXTURE;
    bd[0].texture = srcTex;
    bd[0].aspect = RHI_ASPECT_COLOR;
    bd[1].slot = 1;
    bd[1].type = RHI_BIND_SAMPLER;
    bd[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    bd[2].slot = 2;
    bd[2].type = RHI_BIND_SAMPLED_TEXTURE;
    bd[2].texture = g_rd.dummy;
    bd[2].aspect = RHI_ASPECT_COLOR;
    const RhiBindGroup g = rd__replay_bind_group(g_rd.layoutTex, bd, 3);
    if (g.id) {
        rd__set_pipeline(cl, pipe, &key);
        rd__bind_uniform(
            cl, 0,
            rd__frame_group(F4_W, F4_H, (float)(2048u - F4_W / 2u), (float)(2048u - F4_H / 2u)));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2, g);
        rhi_cmd_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
    return g.id != 0;
}

bool rd__blur_fold4_selftest(void)
{
    s_f4.state = 0;
    s_f4.words[0] = '\0';
    if (!g_rd.inited || !g_rd.hasDevice || !(rd__lamps_wanted() & RD_LAMPS_FOLD4) ||
        rhi_backend() != RHI_BACKEND_VULKAN || !g_rd.fs[RD_FS_FX_BLUR4].id ||
        !g_rd.fs[RD_FS_FX_BLUR2].id) {
        return true; /* not asked here: nothing to check */
    }
    s_f4.state = 2;
    const RhiFormat fmt = RHI_FMT_RGBA8_UNORM;
    const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
                           RHI_TEX_COPY_DST | rd__dst_target_usage(fmt, NULL);
    const uint32_t a = rhi_limits()->copyRowPitchAlign ? rhi_limits()->copyRowPitchAlign : 1u;
    const uint32_t pitch = (F4_W * 4u + a - 1u) / a * a;
    RhiTexture tex[4];
    for (int k = 0; k < 4; k++) {
        tex[k] = rhi_create_texture(
            &(RhiTextureDesc){F4_W, F4_H, 1, fmt, usage, "rd blur fold4 test", 0});
    }
    RhiBuffer up = rhi_create_buffer(&(RhiBufferDesc){(uint64_t)pitch * F4_H, RHI_BUF_COPY_SRC,
                                                      RHI_MEM_UPLOAD, "rd blur fold4 test"});
    uint8_t *m = up.id ? (uint8_t *)rhi_map_buffer(up) : NULL;
    const char *why = NULL;
    int bad = -1;
    uint8_t got[4] = {0}, want[4] = {0};
    /* the passes' records: WORK0 64 x 32 (tex), WORK1 64 x 64 (sizes only:
     * the folds never read or write it) */
    static RdTargetRec w0, w1;
    memset(&w0, 0, sizeof(w0));
    memset(&w1, 0, sizeof(w1));
    w0.w = w0.tw = F4_W;
    w0.h = w0.th = F4_H;
    w1.w = w1.tw = F4_W;
    w1.h = w1.th = 2u * F4_H;
    w0.sx = w0.sy = w1.sx = w1.sy = 1.0f;
    BlurFoldPass a1, a2, b1, b2;
    f4Pass(&a1, &w1, &w0, 1, 0);
    f4Pass(&a2, &w0, &w1, 0, 0);
    f4Pass(&b1, &w1, &w0, 1, 1);
    f4Pass(&b2, &w0, &w1, 0, 1);
    RdFold4Axis ax, ay;
    int32_t ww2 = 0, ww1 = 0;
    fold4Axis(&ax, 0, &a1, &a2, &b1, &b2);
    fold4Axis(&ay, 1, &a1, &a2, &b1, &b2);
    if (!tex[0].id || !tex[1].id || !tex[2].id || !tex[3].id || !m) {
        why = "no memory for its textures";
    } else if (!fold4Form(&a1, &a2, &b1, &b2) || !rd__lamps_fold4_axis(&ax, &ww2, &ww1) ||
               !rd__lamps_fold4_axis(&ay, &ww2, &ww1)) {
        why = "its pairs refused by the fold's own rules";
    } else {
        for (uint32_t y = 0; y < F4_H; y++) {
            for (uint32_t x = 0; x < F4_W * 4u; x++) {
                m[(size_t)y * pitch + x] = f4Level(y * F4_W * 4u + x);
            }
        }
        rhi_unmap_buffer(up);
        m = NULL;
        RhiCommandList cl =
            rd__begin_own_frame(64u * 1024u) ? rhi_begin_commands() : (RhiCommandList){0};
        if (!cl.id) {
            why = "no command list";
        } else {
            rd__upload_textures(cl);
            RhiState ss[4] = {RHI_STATE_UNDEFINED, RHI_STATE_UNDEFINED, RHI_STATE_UNDEFINED,
                              RHI_STATE_UNDEFINED};
            rd__transition(cl, tex[0], &ss[0], RHI_STATE_COPY_DST);
            rhi_cmd_copy_buffer_to_texture(cl, up, 0, pitch, tex[0], 0,
                                           (RhiRect){0, 0, F4_W, F4_H});
            rd__transition(cl, tex[0], &ss[0], RHI_STATE_SHADER_READ);
            /* two fx_blur2_ps passes (A into tex 1, B into tex 2), one
             * fx_blur4_ps pass (A and B into tex 3) */
            bool drawn = f4Draw(cl, tex[0], tex[1], &ss[1], &a1, &a2, NULL, NULL, fmt);
            rd__transition(cl, tex[1], &ss[1], RHI_STATE_SHADER_READ);
            drawn = drawn && f4Draw(cl, tex[1], tex[2], &ss[2], &b1, &b2, NULL, NULL, fmt);
            drawn = drawn && f4Draw(cl, tex[0], tex[3], &ss[3], &a1, &a2, &b1, &b2, fmt);
            rd__transition(cl, tex[2], &ss[2], RHI_STATE_COPY_SRC);
            rd__transition(cl, tex[3], &ss[3], RHI_STATE_COPY_SRC);
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
            if (!drawn) {
                why = "its pipelines missing";
            }
        }
        static uint8_t back[2][F4_W * F4_H * 4u + 4096u];
        uint32_t rp[2] = {0, 0};
        for (int t = 0; t < 2 && !why; t++) {
            if (!rhi_readback_texture(tex[2 + t], RHI_ASPECT_COLOR, back[t], sizeof(back[t]),
                                      &rp[t]) ||
                rp[t] < F4_W * 4u || (size_t)rp[t] * F4_H > sizeof(back[t])) {
                why = "its readback failed";
            }
        }
        for (uint32_t i = 0; !why && i < F4_W * F4_H && bad < 0; i++) {
            const uint32_t x = i % F4_W, y = i / F4_W;
            memcpy(want, back[0] + (size_t)y * rp[0] + x * 4u, 4);
            memcpy(got, back[1] + (size_t)y * rp[1] + x * 4u, 4);
            if (memcmp(got, want, 4) != 0) {
                bad = (int)i;
            }
        }
    }
    if (m) {
        rhi_unmap_buffer(up);
    }
    rhi_wait_idle();
    for (int k = 0; k < 4; k++) {
        if (tex[k].id) {
            rhi_destroy_texture(tex[k]);
        }
    }
    if (up.id) {
        rhi_destroy_buffer(up);
    }
    if (!why && bad < 0) {
        s_f4.state = 1;
        snprintf(s_f4.words, sizeof(s_f4.words), "passed");
        rd__log(
            "blur fold: two pairs a pass: self-test passed (two of the chain's pairs on a %ux%u "
            "WORK0 of random bytes: one fx_blur4_ps pass the bytes of two fx_blur2_ps "
            "passes, windows %d and %d texels)",
            F4_W, F4_H, (int)ww2, (int)ww1);
    } else if (why) {
        snprintf(s_f4.words, sizeof(s_f4.words), "not run: %s", why);
        rd__log("blur fold: two pairs a pass: self-test not run (%s): one pair a pass", why);
    } else {
        snprintf(s_f4.words, sizeof(s_f4.words), "failed at texel %d", bad);
        rd__log("blur fold: two pairs a pass: self-test failed at texel %d: %02X%02X%02X%02X for "
                "%02X%02X%02X%02X: one pair a pass",
                bad, got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    return s_f4.state == 1;
}

/* blur(n)'s pair at its pass 1 (c, the command the loop replays) as one
 * pass, when the profile folds and the pair takes it (the module, no pass
 * window: the loss bisect counts the passes as recorded).  True: drawn, the
 * loop skips up to pass 2 (s_loop.skip) with their state in r */
bool rd__replay_blur_fold(Replay *r, const RdFrame *f, const RdCmd *c)
{
    if (!g_rd.profile.blurFold || !g_rd.fs[RD_FS_FX_BLUR2].id || g_rdWin.first || !s_loop.cmds ||
        s_loop.index >= s_loop.count || &s_loop.cmds[s_loop.index] != c) {
        return false;
    }
    BlurFoldPass p1, p2, p3;
    r->mirror = 0; /* as doBlurSpriteDraw: no previous draw's flip in the scissor */
    if (!blurFoldPass(r, f, c, &p1)) {
        return false;
    }
    blurFoldFirst(r, f, c, &p1);
    static Replay q; /* the state after pass 2 (a replay's fields: large) */
    q = *r;
    const uint32_t i2 = blurFoldAhead(&q, s_loop.index);
    if (i2 >= s_loop.count || !blurFoldPass(&q, f, &s_loop.cmds[i2], &p2) ||
        s_loop.cmds[i2].b[0] != c->b[0] || !blurFoldPair(&p1, &p2)) {
        return false;
    }
    /* the pass the fold draws is pass 2's: where the glow's edge strip would
     * follow it (a pass 1 of blur(n) folded as the second of a pair that
     * starts at the pass 2 before it, after a pair drawn as two passes), the
     * strip's columns would keep an earlier pass's bytes, so no fold */
    if (rd__replay_glow_edge_columns(&q, f, &s_loop.cmds[i2]) > 0) {
        blurFoldRefused(r, &p1, "the glow's edge strip follows its pass 2");
        return false;
    }
    if (!blurFoldOk(&p1, &p2)) {
        blurFoldRefused(r, &p1, "pass 2 weighs a WORK1 texel pass 1 does not leave its bytes in");
        return false;
    }
    /* the next pair's pass 1: the same targets, and every texel this pass
     * 1 would have left its bytes in among its own (on x from column 0
     * with the glow's edge strip), so the first pair after it drawn as two
     * passes writes WORK1 over every texel a folded pass 1 would have
     * before its pass 2 reads it */
    static Replay q3;
    q3 = q;
    const uint32_t i3 = blurFoldAhead(&q3, i2);
    if (i3 >= s_loop.count || !blurFoldPass(&q3, f, &s_loop.cmds[i3], &p3) ||
        s_loop.cmds[i3].b[0] != c->b[0] || p3.tc != p1.tc || p3.src != p1.src) {
        blurFoldRefused(r, &p1, "the chain's last pair");
        return false;
    }
    blurFoldFirst(&q3, f, &s_loop.cmds[i3], &p3);
    if (p3.first > p1.first || p3.cx1 < p1.cx1 || p3.cy0 > p1.cy0 || p3.cy1 < p1.cy1) {
        blurFoldRefused(r, &p1, "the next pair's pass 1 leaves its bytes in fewer WORK1 texels");
        return false;
    }
    /* two pairs in one pass where the lamps' FOLD4 is on and pair B (the
     * next one) folds as well (rd_lamps.c RD_LAMPS_FOLD4) */
    static Replay q4;
    BlurFoldPass p4;
    uint32_t i4 = 0;
    const bool four = fold4Pair(c, f, &p1, &p2, &p3, &q3, i3, &p4, &q4, &i4);
    const Replay *qd = four ? &q4 : &q;
    const BlurFoldPass *pa = four ? &p3 : &p1, *pb = four ? &p4 : &p2;
    const uint32_t id = four ? i4 : i2;
    RdPipeKeyInt key = rd__blur_fold_key(&qd->st, pb->tc->format);
    if (four) {
        key.fs = RD_FS_FX_BLUR4;
    }
    RhiPipeline pipe = rd__get_pipeline(&key);
    if (!pipe.id) {
        return false;
    }
    /* the last pass's state from here (the loop's as it would have been) */
    r->st = qd->st;
    r->halfAdded = qd->halfAdded;
    r->perfTargetEffect = qd->perfTargetEffect;
    /* the effect the last pass's POST_STUB sets in the loop (the glow), not
     * the WORK0 target's (none): the folded pass is begun and timed as the
     * effect's, so the glow pass count and the effect timings stay true */
    const int post2 = rd__perf_post_of_kind(s_loop.cmds[id].b[0]);
    r->perfPost = post2 >= 0 ? post2 : qd->perfPost;
    s_loop.skip = id - s_loop.index;
    rd__replay_perf_post(r, r->perfPost);
    RdTargetRec *tc = pb->tc;
    /* WORK0 is read (pass 1's source) and written: its snapshot, by the
     * swap where the fold covers the whole target (as doBlurSpriteDraw
     * swaps), else a copy */
    const RdPipeKeyInt fxKey = rd__blur_key(&r->st, tc->format, RHI_FMT_UNKNOWN, NULL, 0);
    bool swapped = false;
    if (rd__replay_blur_covers(r, tc, &pb->p, &r->st.ds, &fxKey, 0)) {
        if (!rd__replay_blur_swap(r, tc)) {
            return true; /* consumed: as doBlurSpriteDraw, nothing drawn */
        }
        swapped = true;
    } else if (!rd__replay_take_snap(r, tc, (RhiRect){0, 0, tc->tw, tc->th})) {
        return true;
    }
    RhiCommandList cl = rd__replay_cl();
    if (four && swapped && rd__lamps_poison()) {
        /* the tests: the swapped-in colour magenta before the pass, so a
         * texel the fold of two pairs does not write shows */
        fold4Poison(cl, tc);
    }
    DrawSetup ds;
    memset(&ds, 0, sizeof(ds));
    ds.tc = tc;
    ds.tex = tc->snap;
    ds.dateTex = g_rd.dummy;
    ds.tw = tc->tw;
    ds.th = tc->th;
    ds.textured = 1;
    ds.screenCopy = 1;
    r->mirror = 0;
    RhiBindGroup g2 = rd__replay_bind_draw(r, &ds);
    if (!g2.id) {
        return true;
    }
    IcoDrawCB cb;
    foldCb(&cb, pa, pb, &r->st);
    if (four) {
        fold4Cb(&cb, &p1, &p2);
    }
    rd__set_pipeline(cl, pipe, &key);
    rd__bind_uniform(cl, 0, r->frameBG);
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g2);
    rd__replay_draw(cl, 3, 0, 1);
    rd__stats_here()->draws++;
    g_rdPerf.blurFolds += four ? 2u : 1u;
    r->writeSerial += four ? 4u : 2u; /* the passes' alpha writes */
    if (four) {
        rd__lamps_fold4_noted();
    }
    static bool logged; /* once a run: the count is the --stats line's and the profile's */
    if (!logged) {
        logged = true;
        rd__log("blur fold: on (%s): each staticBlur pair but a chain's last drawn as one pass "
                "(fx_blur2_ps)",
                (g_rd.profile.devSet & RD_PROFILE_BLUR_FOLD) ? "[dev] blur_fold"
                                                             : g_rd.profile.cls);
    }
    return true;
}

/* ------------------------------------------- the glow's chain (rd_glow_blur.c)
 * The pairs from pass 1 at c (the command the loop replays) to the eye blur,
 * each pass taken as the blur fold takes it (blurFoldPass: the fold's
 * conditions; blurFoldPair: the two targets crossed, the same flags, TEXA,
 * TEX0 size and origin; blurFoldFirst: where the glow's edge strip leaves a
 * pass 1's bytes), every pair on the first one's targets, the sprites of
 * one kind; the state after the last pass 2 kept for
 * rd__replay_blur_chain_take. */
static Replay s_chainQ; /* the state after the chain's last pass 2 (large) */

static void chainPass(const BlurFoldPass *b, uint8_t toWork1, RdBlurChainPass *o)
{
    memset(o, 0, sizeof(*o));
    for (int k = 0; k < 4; k++) {
        o->rect[k] = (int32_t)b->p.rect[k];
        o->uv[k] = (int32_t)b->uv[k];
        o->rgba[k] = b->p.rgba[k];
    }
    o->lsz[0] = (int32_t)rintf(b->p.scalar[0]);
    o->lsz[1] = (int32_t)rintf(b->p.scalar[1]);
    o->asz[0] = (int32_t)b->src->tw;
    o->asz[1] = (int32_t)b->src->th;
    o->o[0] = b->ox;
    o->o[1] = b->oy;
    o->c0[0] = b->cx0;
    o->c0[1] = b->cy0;
    o->c1[0] = b->cx1;
    o->c1[1] = b->cy1;
    o->first = b->first;
    o->toWork1 = toWork1;
    o->flags = b->flags;
    o->texfmt = b->mode1 >> 8;
}

int rd__replay_blur_chain(Replay *r, const RdFrame *f, const RdCmd *c, RdBlurChain *ch)
{
    memset(ch, 0, sizeof(*ch));
    if (!s_loop.cmds || s_loop.index >= s_loop.count || &s_loop.cmds[s_loop.index] != c) {
        return RD_GLOW_BLUR_WHY_FORM;
    }
    static Replay q, q3; /* a replay's fields: large */
    q = *r;
    q.mirror = 0; /* as doBlurSpriteDraw: no previous draw's flip in the scissor */
    BlurFoldPass a, b, a3;
    if (!blurFoldPass(&q, f, c, &a)) {
        return RD_GLOW_BLUR_WHY_FORM;
    }
    blurFoldFirst(&q, f, c, &a);
    const RdTargetRec *w1 = a.tc, *w0 = a.src;
    ch->kind = c->b[0];
    uint32_t i = s_loop.index;
    for (;;) {
        if (ch->n + 2 > RD_GLOW_BLUR_PASSES_MAX) {
            return RD_GLOW_BLUR_WHY_FORM;
        }
        const uint32_t i2 = blurFoldAhead(&q, i);
        if (i2 >= s_loop.count || s_loop.cmds[i2].b[0] != c->b[0] ||
            !blurFoldPass(&q, f, &s_loop.cmds[i2], &b) || !blurFoldPair(&a, &b)) {
            return RD_GLOW_BLUR_WHY_FORM;
        }
        chainPass(&a, 1, &ch->p[ch->n++]);
        chainPass(&b, 0, &ch->p[ch->n++]);
        ch->last = i2;
        s_chainQ = q;
        q3 = q;
        const uint32_t i3 = blurFoldAhead(&q3, i2);
        if (i3 >= s_loop.count) {
            return RD_GLOW_BLUR_WHY_FORM;
        }
        const RdCmd *x = &s_loop.cmds[i3];
        if (x->b[0] == c->b[0] && blurFoldPass(&q3, f, x, &a3) && a3.tc == w1 && a3.src == w0) {
            blurFoldFirst(&q3, f, x, &a3);
            q = q3;
            a = a3;
            i = i3;
            continue;
        }
        /* the chain's end: the eye blur reads WORK0 next */
        if (x->type != RDC_POST_STUB || x->b[0] != RD_POST_EYE_BLUR) {
            return RD_GLOW_BLUR_WHY_FORM;
        }
        break;
    }
    ch->w0 = w0->tw;
    ch->h0 = w0->th;
    ch->w1 = w1->tw;
    ch->h1 = w1->th;
    ch->work0 = s_chainQ.st.color;
    ch->work1 = r->st.color;
    return ch->n >= 4 ? RD_GLOW_BLUR_WHY_NONE : RD_GLOW_BLUR_WHY_FORM;
}

void rd__replay_blur_chain_take(Replay *r, const RdBlurChain *ch)
{
    r->st = s_chainQ.st;
    r->halfAdded = s_chainQ.halfAdded;
    r->perfTargetEffect = s_chainQ.perfTargetEffect;
    /* the effect the last pass 2's POST_STUB sets in the loop (the glow), as
     * the fold's: the passes are begun and timed as the effect's */
    const int post2 = rd__perf_post_of_kind(s_loop.cmds[ch->last].b[0]);
    r->perfPost = post2 >= 0 ? post2 : s_chainQ.perfPost;
    s_loop.skip = ch->last - s_loop.index;
    rd__replay_perf_post(r, r->perfPost);
}
