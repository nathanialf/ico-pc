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
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

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
    RhiCommandList cl = rd__replay_cl();
    RdTargetRec *tc = ds->tc;
    RhiRect take;
    const bool samePass = r->dateStPass == r->passSerial && r->dateStFor != 0;
    if (!rd__date_retake(samePass ? r->dateStFor : 0, r->dateStSerial, &r->dateStArea, r->st.color,
                         r->writeSerial, ds->dateWantAll ? NULL : &ds->dateWant, tc->tw, tc->th,
                         &take)) {
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
 * 1275), which the next-pair rule refuses there (8 of 10). */
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
    RdPipeKeyInt key = rd__blur_fold_key(&q.st, p2.tc->format);
    RhiPipeline pipe = rd__get_pipeline(&key);
    if (!pipe.id) {
        return false;
    }
    /* pass 2's state from here (the loop's as it would have been) */
    r->st = q.st;
    r->halfAdded = q.halfAdded;
    r->perfTargetEffect = q.perfTargetEffect;
    /* the effect pass 2's POST_STUB sets in the loop (the glow), not the
     * WORK0 target's (none): the folded pass is begun and timed as the
     * effect's, so the glow pass count and the effect timings stay true */
    const int post2 = rd__perf_post_of_kind(s_loop.cmds[i2].b[0]);
    r->perfPost = post2 >= 0 ? post2 : q.perfPost;
    s_loop.skip = i2 - s_loop.index;
    rd__replay_perf_post(r, r->perfPost);
    RdTargetRec *tc = p2.tc;
    /* WORK0 is read (pass 1's source) and written: its snapshot, by the
     * swap where the fold covers the whole target (as doBlurSpriteDraw
     * swaps), else a copy */
    const RdPipeKeyInt fxKey = rd__blur_key(&r->st, tc->format, RHI_FMT_UNKNOWN, NULL, 0);
    if (rd__replay_blur_covers(r, tc, &p2.p, &r->st.ds, &fxKey, 0)) {
        if (!rd__replay_blur_swap(r, tc)) {
            return true; /* consumed: as doBlurSpriteDraw, nothing drawn */
        }
    } else if (!rd__replay_take_snap(r, tc, (RhiRect){0, 0, tc->tw, tc->th})) {
        return true;
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
    RhiCommandList cl = rd__replay_cl();
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    memcpy(cb.col, (const uint32_t[4]){p2.p.rgba[0], p2.p.rgba[1], p2.p.rgba[2], p2.p.rgba[3]},
           sizeof(cb.col));
    cb.mode[0] = p2.flags;
    cb.mode[1] = p2.mode1;
    cb.mode[2] = r->st.ds.test.atst | ((uint32_t)(r->st.ds.test.ate != 0) << 8) |
                 ((uint32_t)r->st.ds.test.afail << 16);
    /* no alpha test here (blurFoldPass), so not AREF: pass 1's first drawn
     * WORK1 column, which a tap left of it reads (the glow's edge strip;
     * with first == cx0 only taps of weight 0 lie there) */
    cb.mode[3] = (uint32_t)p1.cx0;
    cb.blend[0] =
        rd__alpha_register(r->st.ds.blend < RD_BLEND_COUNT ? r->st.ds.blend : RD_BLEND_LERP_AS);
    cb.blend[1] = r->st.ds.blendFix;
    cb.blend[2] = r->st.ds.colclamp;
    cb.blend[3] = p2.p.z;
    memcpy(cb.uvRect, p2.uv, sizeof(cb.uvRect));
    cb.tex[0] = p2.p.scalar[0];
    cb.tex[1] = p2.p.scalar[1];
    cb.tex[2] = (float)p2.src->tw; /* pass 2's t1: WORK1 */
    cb.tex[3] = (float)p2.src->th;
    cb.scale[0] = 1.0f;
    cb.scale[1] = 1.0f;
    memcpy(cb.param, p2.p.rect, sizeof(cb.param));
    rd__replay_blur_aux1(&cb);       /* pass 2's reciprocals and fx_texel's size (scale[0..1] 1) */
    cb.scale[0] = (float)p1.src->tw; /* pass 1's t1: WORK0, its fetch's clamp */
    cb.scale[1] = (float)p1.src->th;
    /* pass 1 (fx_sprite.hlsl fx_blur2_ps; shader_consts.h IcoDrawCB.aux0):
     * its rect and UVs as 16-bit pairs, its colour in scale.zw */
    cb.aux0[0] = (uint32_t)p1.p.rect[0] | ((uint32_t)p1.p.rect[1] << 16);
    cb.aux0[1] = (uint32_t)p1.p.rect[2] | ((uint32_t)p1.p.rect[3] << 16);
    cb.aux0[2] = (uint32_t)p1.uv[0] | ((uint32_t)p1.uv[1] << 16);
    cb.aux0[3] = (uint32_t)p1.uv[2] | ((uint32_t)p1.uv[3] << 16);
    cb.scale[2] = (float)(p1.p.rgba[0] + 256u * p1.p.rgba[1]);
    cb.scale[3] = (float)(p1.p.rgba[2] + 256u * p1.p.rgba[3]);
    rd__set_pipeline(cl, pipe, &key);
    rd__bind_uniform(cl, 0, r->frameBG);
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g2);
    rd__replay_draw(cl, 3, 0, 1);
    g_rd.stats.draws++;
    g_rdPerf.blurFolds++;
    r->writeSerial += 2; /* the two passes' alpha writes */
    static bool logged;  /* once a run: the count is the --stats line's and the profile's */
    if (!logged) {
        logged = true;
        rd__log("blur fold: on (%s): each staticBlur pair but a chain's last drawn as one pass "
                "(fx_blur2_ps)",
                (g_rd.profile.devSet & RD_PROFILE_BLUR_FOLD) ? "[dev] blur_fold"
                                                             : g_rd.profile.cls);
    }
    return true;
}
