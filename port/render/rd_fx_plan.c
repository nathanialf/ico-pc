/* rd_fx_plan.c: the frame-size post sprites' plan (rd_replay.h RdFxDraw,
 * rd__fx_plan_draw; rd_internal.h rd__fx_enumerate; RdProfile.fxPlan,
 * fxDstLocal; docs/port/RENDER.md "Post passes").
 *
 * A staticBlur sprite (rd_replay.c doBlurSpriteDraw) that reads its
 * destination takes a snapshot of its target, or swaps it, and draws in a
 * pass of its own through fx_sprite_ps.  Where the same bytes come another
 * way, the plan draws it in the open pass, without the snapshot:
 *   constant  an untextured sprite whose blend leaves every colour channel
 *             either the destination for every Cd and Ad (identity) or one
 *             value for every Cd and Ad (constant), proved on the CPU over
 *             the 256 values of each (rd__fx_fold): drawn through
 *             fx_sprite_ps without the blend and the destination read, the
 *             constant channels as its colour and the identity ones masked
 *             (the colour mask A alone, or the whole one: cleanUpFB's two
 *             sprites and the glow's WORK0 fill, alpha written over a
 *             picture the blend keeps).  The alpha is As (with FBA's bit)
 *             either way; DATE and AFAIL RGB_ONLY, which read the
 *             destination's alpha, are not taken
 *   additive  the four premultiplied forms Cs * FIX + Cd, Cs * As + Cd,
 *             Cd - Cs * FIX, Cd - Cs * As under COLCLAMP 1 without PABE
 *             (the glow's paste, WORK3 into SCENE through FIX 128):
 *             fx_frame_ps writes |(A - B) C >> 7| (FXF_OUT_PREMUL) and the
 *             pipeline adds it to Cd (ONE, ONE) or takes it from Cd
 *             (reverse subtract), alpha ONE, ZERO.  The UNORM8 blender's
 *             sum of two k / 255 is the integer sum, clamped at 255 (at 0
 *             for the subtract), the GS's clamp; the subtract's term is the
 *             GS's ceil(Cs C / 128) (an arithmetic shift of the negative
 *             product), not the truncation the screen prims' fixed-function
 *             subtract carries (rd_dst_read.c)
 *   local     a sprite that reads its destination but not itself as a
 *             texture (the motion blur, DISPLAY's LERP FIX into SCENE),
 *             where the profile has fxDstLocal and the target's passes are
 *             readable (rd_replay.c rd__pass_wants_readable: the scene
 *             class under DATE's stencil path): fx_local_ps reads the
 *             destination as an input attachment inside the open pass
 *             after a by-region barrier (RHI_LOCAL_READ_COLOR), and a
 *             second (RHI_LOCAL_READ_BACK) orders its reads before the
 *             draws after it.  fx_rect_vs draws one triangle, so no
 *             fragment of the sprite reads a texel another wrote: the
 *             value SubpassLoad returns is the snapshot's
 *   alpha     a sprite that reads no destination, MODULATE with RGBAQ's RGB
 *             0 and no blend (the glow's mask, SCENE's alpha into WORK2):
 *             fx_frame_ps computes the alpha alone (FXF_ALPHA_ONLY), RGB 0
 *             as (t * 0) >> 7 is.  Not counted (it had no snapshot)
 * A sprite that samples the target it draws into keeps doBlurSpriteDraw's
 * path, and so does every sprite that reads its destination in a frame
 * with the MSAA epoch or under an exact blend the player opted into
 * (g_rd.dstMode).  The pipeline is looked up
 * before anything is recorded: a key that cannot be had leaves the sprite
 * to that path too.
 *
 * The above-1x composite's steps (rd__a1x_sprite_steps, sprite.hlsl
 * sprite_above1x), the reduction's black sprite as a clear
 * (rd__fx_reduction_clears) and the local read's self-test
 * (rd__fx_local_selftest) are here too: the frame-size draws' CPU side.
 *
 * Counters (rd.h RdPerfRecord.fxConstant, fxAdditive, fxLocal): the sprites
 * drawn each way (the black sprite's clear counts as a constant). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rd_replay.h"

/* gs_math.hlsli gs_blend_term / gs_blend_factor: 0 Cs, 1 Cd, 2 (and 3) 0;
 * the factor 0 As, 1 Ad, 2 FIX */
static int fxTerm(uint32_t sel, int cs, int cd)
{
    return sel == 0u ? cs : (sel == 1u ? cd : 0);
}

static int fxFactor(uint32_t sel, int as, int ad, int fix)
{
    return sel == 0u ? as : (sel == 1u ? ad : fix);
}

/* gs_math.hlsli gs_blend_reg_ch: ((A - B) C >> 7) + D of the ALPHA register
 * reg on one channel (the shift arithmetic: the floor of the quotient),
 * clamped to 0..255 by COLCLAMP 1, wrapped to 8 bits by 0 */
static int fxBlendCh(uint32_t reg, int cs, int cd, int as, int ad, int fix, int clampMode)
{
    const int p = (fxTerm(reg & 3u, cs, cd) - fxTerm((reg >> 2) & 3u, cs, cd)) *
                  fxFactor((reg >> 4) & 3u, as, ad, fix);
    const int q = p >= 0 ? p / 128 : -((-p + 127) / 128);
    const int v = q + fxTerm((reg >> 6) & 3u, cs, cd);
    return clampMode ? (v < 0 ? 0 : (v > 255 ? 255 : v)) : (v & 255);
}

bool rd__fx_fold(const RdDrawState *d, const uint8_t rgba[4], uint8_t *identity, uint8_t rgb[3])
{
    const uint32_t reg =
        rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : RD_BLEND_LERP_AS);
    const int as = rgba[3];
    /* PABE: an As without its MSB writes Cs unblended (fx_sprite_ps pabeOff) */
    const bool pabeOff = d->pabe && (as & 0x80) == 0;
    const int adMax = ((reg >> 4) & 3u) == 1u ? 255 : 0; /* C = Ad: every Ad as well */
    *identity = 0;
    for (int ch = 0; ch < 3; ch++) {
        bool ident = true, constant = true;
        const int k =
            pabeOff ? rgba[ch] : fxBlendCh(reg, rgba[ch], 0, as, 0, d->blendFix, d->colclamp);
        for (int ad = 0; ad <= adMax && (ident || constant); ad++) {
            for (int cd = 0; cd < 256 && (ident || constant); cd++) {
                const int v = pabeOff
                                  ? rgba[ch]
                                  : fxBlendCh(reg, rgba[ch], cd, as, ad, d->blendFix, d->colclamp);
                ident = ident && v == cd;
                constant = constant && v == k;
            }
        }
        if (!ident && !constant) {
            return false;
        }
        if (ident) {
            *identity |= (uint8_t)(1u << ch);
        }
        rgb[ch] = (uint8_t)k;
    }
    return true;
}

/* the four premultiplied forms fx_frame_ps's FXF_OUT_PREMUL writes for
 * (A Cs or 0, B 0 or Cs, C As or FIX, D Cd): 1 the add, 2 the reverse
 * subtract, 0 none */
static int fxPremulForm(uint8_t blend)
{
    switch (blend) {
    case RD_BLEND_CS_FIX_ADD_CD: /* 0x68 */
    case RD_BLEND_CS_AS_ADD_CD:  /* 0x48 */
        return 1;
    case RD_BLEND_CD_SUB_CS_FIX: /* 0x62 */
    case RD_BLEND_CD_SUB_CS_AS:  /* 0x42 */
        return 2;
    default:
        return 0;
    }
}

enum { FX_CONSTANT = 0, FX_ADDITIVE = 1, FX_LOCAL = 2, FX_ALPHA = 3 };

/* once a run each: what the plan drew, for a reporter's log */
static void fxNote(int kind)
{
    static bool said[4];
    static const char *const kWhat[4] = {
        "the constant fold (a masked draw in the open pass)",
        "the premultiplied blend (the fixed-function ONE/ONE or reverse subtract in the open pass)",
        "the destination by local read (fx_local_ps in the open pass)",
        "the alpha alone (fx_frame_ps)",
    };
    if (kind >= 0 && kind < 4 && !said[kind]) {
        said[kind] = true;
        rd__log("fx plan: a staticBlur sprite drawn by %s, without its snapshot (reported once)",
                kWhat[kind]);
    }
}

/* The pass the draw lands in is readable (local read): the one open on its
 * target and depth, or the one bindDraw begins (no MSAA epoch: the caller
 * checked) */
static bool fxPassReadable(const Replay *r, const RdFxDraw *x)
{
    const uint32_t tdId = x->useDepth ? r->st.depth : 0u;
    if (r->passOpen && r->passColor == r->st.color && r->passDepth == tdId && !r->passStorage) {
        return r->passReadable != 0;
    }
    return rd__pass_wants_readable(x->tc);
}

/* The sprite drawn as doBlurSpriteDraw draws it, through key, without the
 * snapshot: its flags with set added and clear taken away, RGB col (NULL:
 * RGBAQ's), and, with local, the destination as the input attachment.
 * False when the pipeline is not there or the pass is not readable
 * (nothing recorded: doBlurSpriteDraw draws it); true when drawn or when
 * the scissor leaves nothing to draw. */
static bool fxDraw(Replay *r, const RdFxDraw *x, const RdPipeKeyInt *key, uint32_t set,
                   uint32_t clear, const uint8_t *col, int local, int kind)
{
    RhiPipeline pipe = rd__get_pipeline(key);
    if (!pipe.id || (local && !fxPassReadable(r, x))) {
        return false;
    }
    RhiTexture srcTex = x->srcTex;
    if (x->src) {
        if (x->src->colorState != RHI_STATE_SHADER_READ) {
            rd__replay_end_pass(r);
            rd__transition(rd__replay_cl(), x->src->color, &x->src->colorState,
                           RHI_STATE_SHADER_READ);
        }
        srcTex = x->src->color;
    }
    DrawSetup ds;
    memset(&ds, 0, sizeof(ds));
    ds.tc = x->tc;
    ds.tdId = x->useDepth ? r->st.depth : 0;
    ds.depthFmt = x->useDepth ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
    ds.tex = local ? g_rd.dummy : srcTex;
    ds.dateTex = g_rd.dummy;
    ds.tw = x->tw;
    ds.th = x->th;
    ds.textured = x->textured;
    ds.screenCopy = 1; /* a post sprite: target texels onto screen pixels */
    RhiBindGroup g2 = rd__replay_bind_draw(r, &ds);
    if (!g2.id) {
        return true; /* the scissor leaves nothing: nothing drawn, as doBlurSpriteDraw */
    }
    RhiCommandList cl = rd__replay_cl();
    if (local) {
        if (!r->passOpen || !r->passReadable || r->passColor != r->st.color) {
            static bool logged; /* fxPassReadable said otherwise: the snapshot draws it */
            if (!logged) {
                logged = true;
                rd__log("fx plan: the pass a sprite landed in is not readable: drawn through "
                        "its snapshot (reported once)");
            }
            return false;
        }
        RhiBinding b[2];
        memset(b, 0, sizeof(b));
        b[0].slot = 1;
        b[0].type = RHI_BIND_INPUT_ATTACHMENT;
        b[0].texture = x->tc->color;
        b[1].slot = 2;
        b[1].type = RHI_BIND_SAMPLED_TEXTURE;
        b[1].texture = x->textured ? srcTex : g_rd.dummy;
        g2 = rd__replay_bind_group(g_rd.layoutLocal, b, 2);
        if (!g2.id) {
            return false;
        }
    }
    /* doBlurSpriteDraw's DrawCB (fx_sprite.hlsl) */
    const RdDrawState *d = x->d;
    const RdPostRec *p = x->p;
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    uint32_t fl = rd__replay_blur_flags(d, p->lines, x->textured, x->dstRead);
    if (x->steps) {
        fl |= RD_FXF_ABOVE1X;
        g_rd.above1xDraws++;
    }
    fl = (fl | set) & ~clear;
    cb.col[0] = col ? col[0] : p->rgba[0];
    cb.col[1] = col ? col[1] : p->rgba[1];
    cb.col[2] = col ? col[2] : p->rgba[2];
    cb.col[3] = p->rgba[3];
    cb.mode[0] = fl;
    cb.mode[1] = d->texa | (x->tfmt << 8);
    cb.mode[2] =
        d->test.atst | ((uint32_t)(d->test.ate != 0) << 8) | ((uint32_t)d->test.afail << 16);
    cb.mode[3] = d->test.aref;
    cb.blend[0] = rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : RD_BLEND_LERP_AS);
    cb.blend[1] = d->blendFix;
    cb.blend[2] = d->colclamp;
    cb.blend[3] = p->z;
    memcpy(cb.uvRect, x->uv, sizeof(cb.uvRect));
    cb.tex[0] = p->scalar[0];
    cb.tex[1] = p->scalar[1];
    cb.tex[2] = (float)x->tw;
    cb.tex[3] = (float)x->th;
    cb.scale[0] = x->src ? x->src->sx : 1.0f;
    cb.scale[1] = x->src ? x->src->sy : 1.0f;
    if (x->src && x->src->ox > 0.0f) {
        cb.scale[2] =
            x->src->ox / (float)x->src->tw; /* Full pixel (fx_sprite.hlsl fx_src_crop16) */
    }
    cb.scale[3] = (float)x->steps; /* below 2^24: exact */
    memcpy(cb.param, p->rect, sizeof(cb.param));
    rd__replay_blur_aux1(&cb);
    if (local) {
        rd__replay_flush_run(r); /* the run's draws before the read */
        rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_COLOR);
    }
    rd__set_pipeline(cl, pipe, key);
    rd__bind_uniform(cl, 0, r->frameBG);
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2, g2);
    rd__replay_draw(cl, 3, 0, 1);
    if (local) {
        rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_BACK);
    }
    g_rd.stats.draws++;
    if (key->gs.colorMask & 8) {
        r->writeSerial++;
    }
    if (kind == FX_CONSTANT) {
        g_rdPerf.fxConstant++;
    } else if (kind == FX_ADDITIVE) {
        g_rdPerf.fxAdditive++;
    } else if (kind == FX_LOCAL) {
        g_rdPerf.fxLocal++;
    }
    fxNote(kind);
    return true;
}

/* the constant fold of an untextured blending sprite: -1 not taken, else
 * whether it was drawn (1) */
static int fxConstantDraw(Replay *r, const RdFxDraw *x)
{
    uint8_t ident = 0, rgb[3];
    if (!rd__fx_fold(x->d, x->p->rgba, &ident, rgb)) {
        return -1;
    }
    RdPipeKeyInt k = *x->key;
    const uint8_t mask = (uint8_t)(k.gs.colorMask & ~ident);
    if (mask == 0 && !(x->useDepth && k.gs.zwrite == RD_ZWRITE_ON)) {
        g_rdPerf.fxConstant++; /* it writes nothing that changes: no draw */
        fxNote(FX_CONSTANT);
        return 1;
    }
    if (mask != 8u && mask != 0xFu) {
        return -1; /* a mask the start-up set has no key of */
    }
    k.gs.colorMask = mask;
    return fxDraw(r, x, &k, 0, RD_FXF_ABE | RD_FXF_DST | RD_FXF_PABE, rgb, 0, FX_CONSTANT) ? 1 : -1;
}

bool rd__fx_plan_draw(Replay *r, const RdFxDraw *x)
{
    if (!g_rd.profile.fxPlan || !g_rd.hasDevice || x->selfSample) {
        return false;
    }
    const RdDrawState *d = x->d;
    const bool rgbOnly = d->test.ate && d->test.afail == RD_AFAIL_RGB_ONLY;
    const bool dstAlpha = d->test.date != RD_DATE_OFF || rgbOnly; /* the destination's alpha */
    if (x->dstRead) {
        /* the snapshot's semantics under the MSAA epoch and an opted-in
         * exact blend stay its own */
        if (g_rd.dstMode != RD_BLEND_EXACT_OFF || rd__replay_msaa_want() > 1u) {
            return false;
        }
        if (!x->textured && d->abe && !dstAlpha) {
            const int k = fxConstantDraw(r, x);
            if (k >= 0) {
                return k != 0;
            }
        }
        const int form = d->abe ? fxPremulForm(d->blend) : 0;
        if (form && !d->pabe && d->colclamp && !dstAlpha && g_rd.fs[RD_FS_FX_FRAME].id) {
            RdPipeKeyInt k = *x->key;
            k.fs = RD_FS_FX_FRAME;
            k.gs.blend = form == 1 ? RD_BLEND_CS_AS_ADD_CD : RD_BLEND_CD_SUB_CS_AS;
            if (fxDraw(r, x, &k, RD_FXF_OUT_PREMUL, RD_FXF_ABE | RD_FXF_DST, NULL, 0,
                       FX_ADDITIVE)) {
                return true;
            }
        }
        if (g_rd.profile.fxDstLocal && g_rd.fs[RD_FS_FX_LOCAL].id && g_rd.layoutLocal.id) {
            RdPipeKeyInt k = *x->key;
            k.fs = RD_FS_FX_LOCAL;
            return fxDraw(r, x, &k, 0, 0, NULL, 1, FX_LOCAL);
        }
        return false;
    }
    /* the alpha alone: MODULATE (blurFlags: no DECAL) with RGBAQ's RGB 0,
     * no blend */
    if (x->textured && !d->abe && (x->p->lines & 3u) == 0 && d->texFn != RD_TEXFN_DECAL &&
        x->p->rgba[0] == 0 && x->p->rgba[1] == 0 && x->p->rgba[2] == 0 &&
        g_rd.fs[RD_FS_FX_FRAME].id) {
        RdPipeKeyInt k = *x->key;
        k.fs = RD_FS_FX_FRAME;
        return fxDraw(r, x, &k, RD_FXF_ALPHA_ONLY, 0, NULL, 0, FX_ALPHA);
    }
    return false;
}

uint32_t rd__fx_enumerate(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    if (!g_rd.profile.fxPlan) {
        return n;
    }
    /* rd__enumerate_reachable_blur's states: Z ALWAYS, GEQUAL or GREATER,
     * Z write on or off, the colour mask whole */
    static const uint8_t kZ[3] = {RD_ZTST_ALWAYS, RD_ZTST_GEQUAL, RD_ZTST_GREATER};
    const bool frame = g_rd.fs[RD_FS_FX_FRAME].id != 0;
    const bool local =
        g_rd.profile.fxDstLocal && g_rd.fs[RD_FS_FX_LOCAL].id != 0 && g_rd.layoutLocal.id != 0;
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.colorMask = 0xF;
    for (int zw = 0; zw < 2; zw++) {
        for (int z = 0; z < 3; z++) {
            s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
            s.ds.test.ztst = kZ[z];
            s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
            const RdPipeKeyInt base =
                rd__blur_key(&s, RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, NULL, 0);
            RdPipeKeyInt k = base;
            k.gs.colorMask = 8; /* the constant fold: the alpha alone */
            n = rd__add_pipe_key(out, max, n, &k);
            if (frame) {
                k = base;
                k.fs = RD_FS_FX_FRAME; /* the alpha alone */
                n = rd__add_pipe_key(out, max, n, &k);
                k.gs.blend = RD_BLEND_CS_AS_ADD_CD; /* the premultiplied add */
                n = rd__add_pipe_key(out, max, n, &k);
                k.gs.blend = RD_BLEND_CD_SUB_CS_AS; /* and subtract */
                n = rd__add_pipe_key(out, max, n, &k);
            }
            if (local) {
                k = base;
                k.fs = RD_FS_FX_LOCAL;
                n = rd__add_pipe_key(out, max, n, &k);
            }
        }
    }
    return n;
}

/* ------------------------------------------------- the above-1x composite
 * sprite.hlsl sprite_above1x's steps from the CPU (DrawCB.scale.w), with
 * the plan on (RdProfile.fxPlan): for a
 * sprite command whose texture is a target's colour, t1 texels per bound
 * texel on each axis in 1/32, rounded, at most 4095, packed x * 4096 + y,
 * the same for every sprite of the command; 0 (the pixel's footprint, as
 * the shader finds it) for any other command.  The shader's own form is
 * max(|ddx(tc)|, |ddy(tc)|) * 32 + 0.5, floored, tc = uv * t1's size: for
 * an axis-aligned sprite ddy(tc).x and ddx(tc).y are 0 and ddx(tc).x is
 * the U per bound texel, (du / dx in GS pixels) * DrawCB.tex.z (t1's UV
 * normalisation, fillDrawCB) * t1's width / the bound target's scale.  The
 * CPU's quotient is exact where the GPU's derivative carries its
 * interpolation's rounding: the two differ only where the step lies within
 * that rounding of a half (docs/port/DIVERGENCES.md). */
uint32_t rd__a1x_sprite_steps(const Replay *r, const DrawSetup *ds, const IcoSpriteVertex *v,
                              uint32_t nv, uint8_t prim)
{
    if (!g_rd.profile.fxPlan || prim != RD_PRIM_SPRITES || nv < 6 || !ds->textured || !ds->tc ||
        !(ds->tc->sx > 0.0f) || !(ds->tc->sy > 0.0f) || ds->tw == 0 || ds->th == 0) {
        return 0;
    }
    const RdTexRec *t = rd__tex_rec(r->st.tex);
    const RdTargetRec *src = t && t->kind == RD_TEXKIND_TARGET && t->view != RD_VIEW_DEPTH
                                 ? rd__target_rec(t->target)
                                 : NULL;
    if (!src || src->tw == 0 || src->th == 0) {
        return 0;
    }
    /* fillDrawCB's DrawCB.tex.zw: 1 / the GS size, or a crop's scale over
     * its width */
    const double nx = ds->texCrop ? (double)ds->texCrop->sx / ds->texCrop->tw : 1.0 / ds->tw;
    const double ny = 1.0 / ds->th;
    uint32_t steps = 0;
    for (uint32_t i = 0; i + 5 < nv; i += 6) {
        /* expand's sprite: q0 (corner a) first, q3 (corner b) last */
        const IcoSpriteVertex *a = &v[i], *b = &v[i + 5];
        const double dx = ((double)b->x - (double)a->x) / 16.0;
        const double dy = ((double)b->y - (double)a->y) / 16.0;
        if (dx == 0.0 || dy == 0.0) {
            return 0;
        }
        const double kx = fabs(((double)b->u - (double)a->u) / dx) * nx * src->tw / ds->tc->sx;
        const double ky = fabs(((double)b->v - (double)a->v) / dy) * ny * src->th / ds->tc->sy;
        const double fx = floor(kx * 32.0 + 0.5), fy = floor(ky * 32.0 + 0.5);
        const uint32_t sx = fx >= 4095.0 ? 4095u : (uint32_t)fx;
        const uint32_t sy = fy >= 4095.0 ? 4095u : (uint32_t)fy;
        const uint32_t s = sx * 4096u + sy;
        if (i > 0 && s != steps) {
            return 0; /* sprites of other steps: each pixel finds its own */
        }
        steps = s;
    }
    return steps; /* 0 on both axes: the GS's filter there (gs_rs_kind), as the footprint's */
}

/* -------------------------------------------- the reduction's black sprite
 * rd_post.c's reduction clears DISPLAY with an untextured opaque sprite
 * before the textured one.  On a scaled target it is a hardware sprite
 * (rd_replay.c doBlurSpriteDraw, rd__blur_screen_fallback), drawn through
 * sprite_ps over the texels whose centres it covers.  Where it covers every
 * texel of DISPLAY and writes all four channels of its own colour with
 * nothing else in force (no texture, blend, alpha test, DATE, FBA, AA1,
 * depth, the colour mask whole, the scissor the whole target, no Full
 * pixel crop), it is the clear of that colour (blurSpriteClears' reasoning
 * for the hardware path), which the next pass on DISPLAY takes as its load
 * op.  Coverage as the exact viewport maps a sprite on a scaled axis: its
 * corners snapped up to whole GS pixels (rd_replay.c snapAxis), GS pixel
 * coordinate g at texel (g - origin) * (texels / GS size) + 0.5 (FrameCB's
 * half texel), an edge on a texel centre included at the left and top (the
 * top-left rule of Vulkan's and Direct3D's rasterisers), excluded at the
 * right and bottom.  col: the clear's RGBA, k / 255. */
static bool fxAxisCovers(int32_t a, int32_t b, int scaled, int32_t origin16, uint32_t texels,
                         uint32_t gs)
{
    if (a > b) {
        const int32_t t = a;
        a = b;
        b = t;
    }
    if (scaled) {
        a = (a + 15) & ~15;
        b = (b + 15) & ~15;
    }
    const double k = (double)texels / (double)gs;
    const double lo = ((double)(a - origin16) / 16.0) * k + 0.5;
    const double hi = ((double)(b - origin16) / 16.0) * k + 0.5;
    return lo <= 0.5 + 1e-3 && hi > (double)texels - 0.5 + 0.25;
}

bool rd__fx_reduction_clears(Replay *r, const RdScreenVtx v[2], float col[4])
{
    const RdDrawState *d = &r->st.ds;
    RdTargetRec *tc = rd__target_rec(r->st.color);
    const RdTargetRec *td = rd__target_rec(r->st.depth);
    if (!g_rd.profile.fxPlan || !tc || !tc->color.id || tc->format != RHI_FMT_RGBA8_UNORM ||
        d->texEnabled || d->abe || d->fba || r->st.aa1 || d->test.date != RD_DATE_OFF ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS) || d->colorMask != 0xF || d->fbmsk != 0 ||
        tc->ox != 0.0f || (td && td->withDepth) || tc->w == 0 || tc->h == 0 || r->st.gsW != tc->w ||
        r->st.gsH != tc->h || memcmp(v[0].rgba, v[1].rgba, 4) != 0) {
        return false;
    }
    r->mirror = 0; /* as doBlurSpriteDraw: no previous draw's flip in the scissor */
    RhiRect sc;
    if (!rd__replay_scissor(r, tc, &sc) || sc.x != 0 || sc.y != 0 || sc.w != tc->tw ||
        sc.h != tc->th) {
        return false;
    }
    /* the draw offset (rd_replay.c drawOffset: a screen copy drops the half
     * pixel sceneKeepsHalf added) */
    const uint32_t use =
        r->halfAdded ? r->st.useOffset & ~(uint32_t)RD_TARGET_HALF_Y : r->st.useOffset;
    const int32_t ox16 = 16 * (2048 - (int32_t)(r->st.gsW >> 1));
    const int32_t oy16 =
        16 * (2048 - (int32_t)(r->st.gsH >> 1)) + ((use & RD_TARGET_HALF_Y) ? 8 : 0);
    if (!fxAxisCovers(v[0].x, v[1].x, tc->sx > 1.0f, ox16, tc->tw, tc->w) ||
        !fxAxisCovers(v[0].y, v[1].y, tc->sy > 1.0f, oy16, tc->th, tc->h)) {
        return false;
    }
    for (int k = 0; k < 4; k++) {
        col[k] = (float)v[1].rgba[k] / 255.0f;
    }
    g_rdPerf.fxConstant++;
    static bool logged;
    if (!logged) {
        logged = true;
        rd__log("fx plan: the reduction's black sprite on a scaled DISPLAY drawn as its clear "
                "(reported once)");
    }
    return true;
}

/* -------------------------------------------------------- the self-test
 * fx_local_ps reads the colour attachment by local read inside the pass, a
 * driver path the snapshot's copy does not take, so it is checked on the
 * device before the game draws with it, on SCENE at its real size: a frame
 * of its own (never recorded, never presented) clears SCENE, draws four
 * cells of known colours across it, then one untextured staticBlur sprite
 * over the whole frame with ALPHA LERP FIX 0x20 (the motion blur's form),
 * which reads every cell as its destination.  The frame is replayed with
 * the local read and through the snapshot, the four cells' centres read
 * back each time and compared with the GS's arithmetic, every channel
 * exactly; the local replay must have drawn its sprite that way
 * (g_rdPerf.fxLocal), else its samples say nothing of it:
 *   fx local read: on (self-test: local ok 4, snapshot ok 4) on SCENE ...
 *   fx local read: off (self-test: local fail 1 at sample 2: ...) ...
 * The flag stays only with its four right; otherwise it goes off
 * (rd__profile_self_test_off, "(self-test)").  ICO_RD_FX_DST_LOCAL=1 ([dev]
 * fx_dst_local) keeps it whatever it finds.  Nothing runs where the
 * profile has it off; the line says so where the scene passes are not
 * readable (DATE's stencil path off), where every sprite keeps its
 * snapshot anyway. */
#define FX_ST_SAMPLES 4
#define FX_ST_FIX 0x20u

static const uint8_t kStClear[4] = {10, 20, 30, 0x00};
static const uint8_t kStCells[FX_ST_SAMPLES][4] = {
    {200, 40, 40, 0x80}, {40, 200, 80, 0x10}, {255, 255, 255, 0xFF}, {0, 0, 0, 0x00}};
static const uint8_t kStSprite[4] = {250, 10, 128, 0x40};

static bool stPushSprites(RdFrame *f, const RdScreenVtx *v, uint32_t n)
{
    RdCmd *c = rd__st_push(f, 0, RDC_SCREEN);
    if (!c) {
        return false;
    }
    c->b[0] = RD_PRIM_SPRITES;
    c->b[1] = RD_SPACE_FULLFRAME;
    c->u[0] = rd__frame_payload(f, v, (uint32_t)(n * sizeof(*v)));
    c->u[1] = n;
    return true;
}

static bool stBuild(RdFrame *f, uint32_t scene, uint32_t w, uint32_t h)
{
    memset(f, 0, sizeof(*f));
    rd__reset_state_block(&f->startState);
    f->gsW = w;
    f->gsH = h;
    bool ok = rd__st_target(f, 0, scene, w, h);
    RdCmd *c = rd__st_push(f, 0, RDC_CLEAR);
    if (c) {
        c->u[0] = scene;
        memcpy(c->b, kStClear, 4);
        c->b[4] = 1;
        c->u[1] = 0;
    }
    ok = ok && c && rd__st_push(f, 0, RDC_TEXTURE_OFF) && rd__st_push1(f, 0, RDC_ABE, 0) &&
         rd__st_test(f, 0, RD_TEST_Z_ALWAYS) && rd__st_push1(f, 0, RDC_ZWRITE, RD_ZWRITE_OFF) &&
         rd__st_push1(f, 0, RDC_FBA, 0) && rd__st_push1(f, 0, RDC_PABE, 0) &&
         rd__st_push1(f, 0, RDC_COLCLAMP, 1) && rd__st_push1(f, 0, RDC_SHADE, 0);
    const int32_t ox = 2048 - (int32_t)(w / 2), oy = 2048 - (int32_t)(h / 2);
    RdScreenVtx cells[2 * FX_ST_SAMPLES];
    memset(cells, 0, sizeof(cells));
    for (uint32_t k = 0; k < FX_ST_SAMPLES; k++) {
        RdScreenVtx *q = &cells[2 * k];
        q[0].x = (ox + (int32_t)(w * k / FX_ST_SAMPLES)) * 16;
        q[0].y = oy * 16;
        q[1].x = (ox + (int32_t)(w * (k + 1) / FX_ST_SAMPLES)) * 16;
        q[1].y = (oy + (int32_t)h) * 16;
        for (int j = 0; j < 2; j++) {
            q[j].q = 1.0f;
            memcpy(q[j].rgba, kStCells[k], 4);
        }
    }
    ok = ok && stPushSprites(f, cells, 2 * FX_ST_SAMPLES);
    RdCmd *b = ok ? rd__st_push(f, 0, RDC_BLEND) : NULL;
    if (b) {
        b->b[0] = RD_BLEND_LERP_FIX;
        b->b[1] = (uint8_t)FX_ST_FIX;
        b->b[2] = 1;
    }
    RdPostRec p;
    memset(&p, 0, sizeof(p));
    memcpy(p.rgba, kStSprite, 4);
    p.exactInt = 1;
    p.rect[0] = (float)(ox * 16);
    p.rect[1] = (float)(oy * 16);
    p.rect[2] = (float)((ox + (int32_t)w) * 16);
    p.rect[3] = (float)((oy + (int32_t)h) * 16);
    p.scalar[0] = p.scalar[1] = 1024.0f;
    p.scalar[2] = 1.0f;
    p.lutOffset = ~0u;
    RdCmd *s = b ? rd__st_push(f, 0, RDC_POST_STUB) : NULL;
    if (s) {
        s->b[0] = (uint8_t)RD_POST_MOTION_BLUR;
        s->u[1] = rd__frame_payload(f, &p, (uint32_t)sizeof(p));
        s->u[2] = (uint32_t)sizeof(p);
    }
    return ok && b && s;
}

static struct {
    RhiTexture tex;
    RhiState state;
} s_stRead;

/* the cells' centres (one row, the middle) and what the GS leaves there:
 * ((Cs - Cd) FIX >> 7) + Cd per colour channel, As */
static void stWant(const RdTargetRec *t, uint32_t xs[FX_ST_SAMPLES], uint32_t *y,
                   uint8_t want[FX_ST_SAMPLES][4])
{
    *y = t->th / 2;
    for (uint32_t k = 0; k < FX_ST_SAMPLES; k++) {
        xs[k] = (uint32_t)(((uint64_t)t->tw * (2 * k + 1)) / (2 * FX_ST_SAMPLES));
        for (int ch = 0; ch < 3; ch++) {
            want[k][ch] = (uint8_t)fxBlendCh(0x64u, kStSprite[ch], kStCells[k][ch], kStSprite[3], 0,
                                             (int)FX_ST_FIX, 1);
        }
        want[k][3] = kStSprite[3];
    }
}

/* One replay of f; *ok the samples equal to the GS's, *bad the first that
 * is not (-1 none) and got its RGBA, *locals the sprites the local read
 * drew.  False when the replay or the readback could not be done. */
static bool stRun(const RdFrame *f, RdTargetRec *t, int *ok, int *bad, uint8_t got[4],
                  uint32_t *locals)
{
    if (!rd__replay_frame(f, 0, false)) {
        return false;
    }
    *locals = g_rdPerf.fxLocal; /* the replay's own record (rd__perf_begin) */
    if (!s_stRead.tex.id) {
        s_stRead.tex = rhi_create_texture(&(RhiTextureDesc){FX_ST_SAMPLES, 1, 1, t->format,
                                                            RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC,
                                                            "rd fx local self-test", 0});
        s_stRead.state = RHI_STATE_UNDEFINED;
        if (!s_stRead.tex.id) {
            return false;
        }
    }
    uint32_t xs[FX_ST_SAMPLES], y;
    uint8_t want[FX_ST_SAMPLES][4];
    stWant(t, xs, &y, want);
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, t->color, &t->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_stRead.tex, &s_stRead.state, RHI_STATE_COPY_DST);
    for (uint32_t k = 0; k < FX_ST_SAMPLES; k++) {
        rhi_cmd_copy_texture(cl, t->color, (RhiRect){(int32_t)xs[k], (int32_t)y, 1, 1},
                             s_stRead.tex, (int32_t)k, 0);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    uint8_t px[FX_ST_SAMPLES * 4];
    if (!rd__read_rhi_texture(s_stRead.tex, &s_stRead.state, FX_ST_SAMPLES, 1, px, sizeof(px))) {
        return false;
    }
    *ok = 0;
    *bad = -1;
    for (int k = 0; k < FX_ST_SAMPLES; k++) {
        if (memcmp(&px[k * 4], want[k], 4) == 0) {
            (*ok)++;
        } else if (*bad < 0) {
            *bad = k;
            memcpy(got, &px[k * 4], 4);
        }
    }
    return true;
}

/* "ok 4", "fail 2 at sample 1: 28C850FF for 28C85000", "error", or for the
 * local replay that drew no sprite by local read, "not run (...)" */
static void stResult(char *out, size_t size, bool done, bool ran, int ok, int bad,
                     const uint8_t got[4], const RdTargetRec *t)
{
    if (!done) {
        snprintf(out, size, "error");
        return;
    }
    if (!ran) {
        snprintf(out, size, "not run (the sprite did not take the local read)");
        return;
    }
    if (bad < 0) {
        snprintf(out, size, "ok %d", ok);
        return;
    }
    uint32_t xs[FX_ST_SAMPLES], y;
    uint8_t want[FX_ST_SAMPLES][4];
    stWant(t, xs, &y, want);
    snprintf(out, size, "fail %d at sample %d: %02X%02X%02X%02X for %02X%02X%02X%02X",
             FX_ST_SAMPLES - ok, bad, got[0], got[1], got[2], got[3], want[bad][0], want[bad][1],
             want[bad][2], want[bad][3]);
}

bool rd__fx_local_selftest(void)
{
    if (!g_rd.inited || !g_rd.hasDevice || !g_rd.profile.fxPlan || !g_rd.profile.fxDstLocal) {
        return true; /* the snapshot draws the destination reads: nothing to check */
    }
    RdTargetRec *t = rd__target_rec(rd_target(RD_TARGET_SCENE).id);
    if (!t || !t->color.id || t->format != RHI_FMT_RGBA8_UNORM) {
        rd__log("fx local read: self-test: no SCENE target to run it on");
        return false;
    }
    if (!rd__pass_wants_readable(t) || !g_rd.fs[RD_FS_FX_LOCAL].id || !g_rd.layoutLocal.id) {
        rd__log("fx local read: not used: the scene passes are not readable (DATE's stencil "
                "path off) or fx_local_ps or the local-read layout is missing; a sprite that "
                "reads its destination takes its snapshot");
        return true;
    }
    static RdFrame f;
    if (!stBuild(&f, rd_target(RD_TARGET_SCENE).id, t->w, t->h)) {
        rd__frame_free(&f);
        rd__log("fx local read: self-test: could not build its frame");
        return false;
    }
    int okL = 0, badL = -1, okS = 0, badS = -1;
    uint8_t gotL[4] = {0}, gotS[4] = {0};
    uint32_t localL = 0, localS = 0;
    const bool wasBlocked = g_rd.msaaBlocked;
    g_rd.msaaBlocked = true; /* no MSAA epoch: the local read's own draws */
    const bool doneL = stRun(&f, t, &okL, &badL, gotL, &localL);
    g_rd.profile.fxDstLocal = false; /* the snapshot's replay */
    const bool doneS = stRun(&f, t, &okS, &badS, gotS, &localS);
    g_rd.profile.fxDstLocal = true;
    g_rd.msaaBlocked = wasBlocked;
    rd__frame_free(&f);
    if (s_stRead.tex.id) {
        rhi_destroy_texture(s_stRead.tex);
    }
    s_stRead.tex = (RhiTexture){0};
    t->clearPending = 1; /* the next replay starts SCENE from its clear */
    const bool ranL = localL > 0;
    const bool passed = doneL && ranL && badL < 0;
    const bool kept = (g_rd.profile.devSet & RD_PROFILE_FX_DST_LOCAL) != 0;
    char rl[96], rs[96];
    stResult(rl, sizeof rl, doneL, ranL, okL, badL, gotL, t);
    stResult(rs, sizeof rs, doneS, true, okS, badS, gotS, t);
    if (!passed && !kept) {
        rd__profile_self_test_off(RD_PROFILE_FX_DST_LOCAL);
    }
    rd__log("fx local read: %s (self-test: local %s, snapshot %s)%s on SCENE %ux%u at %ux%u texels",
            g_rd.profile.fxDstLocal ? "on" : "off", rl, rs,
            !passed && kept ? "; kept by [dev] fx_dst_local" : "", t->w, t->h, t->tw, t->th);
    return passed;
}
