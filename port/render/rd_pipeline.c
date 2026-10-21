/* rd_pipeline.c: from the replay state block to a GPU pipeline.
 *
 * What is pipeline state and what is not
 * --------------------------------------
 * The shaders (port/shaders) take the alpha test
 * (ATST, AREF, the AFAIL split pass), the texture function, TCC, TEXA, FBA,
 * PABE and the blend factor source as DrawCB uniforms.  So the pipeline key
 * (RdPipelineKey, rd_state.h) is normalised: atst is always ALWAYS, pabe and
 * fba are 0, afailSplit is 0, the effect of an AFAIL split pass on the
 * pipeline is carried by zwrite and colorMask, and blend holds one
 * representative mode per blend path (4 for the LERPs, 5 for the premultiplied
 * adds, 6 for the premultiplied subtracts).  DATE is a uniform too
 * (DF_DATE/DF_DATM: sprite_ps tests the R8 snapshot rd_replay.c binds at t2),
 * so date stays 0 in the key.
 *
 * Blend paths: on UNORM targets fixed-point blend factors clamp to 1.0, so
 *   Cs*F + Cd and Cd - Cs*F (modes 0, 1, 5, 6) use DF_PREMUL: the shader
 *     writes min((Cs*F) >> 7, 255), the GS term, exact for F up to 255, and
 *     the blender adds or reverse-subtracts with factor ONE;
 *   the LERPs (2, 4, 7) use the dual-source factor F/128; F above 0x80 is
 *     clamped: FIX by rd (uniform), As by the hardware (the result is Cs
 *     where the GS would overshoot past it);
 *   Cd*FIX + Cs (mode 3) uses SRC1 as the destination factor, FIX clamped to
 *     0x80;
 *   the Ad modes (8-10, disc data only) use DST_ALPHA, which reads Ad/255,
 *     not Ad/128: half strength, untested;
 *   Cd*As + Cd (mode 11, disc data only) needs a factor above 1.0 on Cd and
 *     is not representable: the draw leaves Cd unchanged (reported once).
 * PABE: a pixel whose As has its MSB clear is written Cs, unblended
 *   (gs_dual_out): c1 = 1.0 makes a lerp give Cs, c1 = 0 makes Cd*FIX + Cs
 *   give Cs (DF_C1_DST, mode 3).  The premultiplied forms (ONE, ONE) and the
 *   Ad modes cannot give Cs: those pixels blend (reported once; the game's
 *   PABE draws are all lerps, gif_SetAlpha(0, 2 or 4, ...) and
 *   queen_barrier_disp.c's ALPHA 0x44).
 * The feedback passes do not use the hardware blender at all: RDC_EXACT_BLEND
 * runs blend_int on RGBA8_UINT copies (rd_replay.c).
 *
 * On a device without dual-source blending (g_rd.noDual), only
 * the LERPs and Cd*FIX + Cs read the second output, so only they change
 * (rd__expand_no_dual).  The draw becomes two: a colour pass whose c0.a is
 * the factor c1 would carry (the same float: F/128, or 1.0 / 0 for a PABE
 * pixel left unblended), blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA (the LERPs)
 * or ONE / SRC_ALPHA (Cd*FIX + Cs) under the RGB part of the mask, then,
 * when the mask has A, an alpha pass writing the stored alpha (As, FBA's
 * MSB) with blending off, no Z write, under a Z test that admits the Z the
 * colour pass left.  The shaders are the *_nodual entries (ICO_NO_DUAL: c0
 * alone); every other path draws in one pass, with those entries.
 * Why the alpha is the one-pass alpha: blending never reads Ad on these
 * paths, so RGB does not depend on the alpha pass.  Without Z write the
 * depth buffer is the same for both passes, so the alpha pass keeps the Z
 * test and admits exactly the colour pass's fragments.  With Z write under
 * GS GEQUAL (GEQUAL here too) each passing fragment stores its depth, so
 * the depth left is the greatest of the passing fragments', and the fragments the
 * alpha pass admits are those at that depth; all of them passed, and the
 * last of them is the last fragment that passed, whose alpha the one-pass
 * draw keeps.  ALWAYS admits every fragment in both passes.  GS GREATER
 * with Z write takes GEQUAL in the alpha pass: a fragment whose Z
 * equals the stored Z failed the colour pass and passes the alpha pass,
 * the one deviation (logged once; no enumerated state draws it).  A pass
 * whose mask has no RGB blends nothing: one pass, blending off, its own Z.
 *
 * Z: the shaders map GS Z to a depth that grows with it (gs_z_to_depth, z
 * times the target's scale), so GS GEQUAL is RHI_CMP_GEQUAL and GREATER is
 * GREATER.
 *
 * Dynamic state (docs/port/RENDER.md "Pipelines"): where the device has it
 * (RhiLimits.dynamicDepth / dynamicBlend, g_rd.dynDepth / dynBlend) the
 * Z test, Z write, blend and colour mask leave the key
 * (rd__key_dynamic_normalise: ztst ALWAYS, zwrite OFF, blend RD_BLEND_COUNT,
 * mask 0xF in the cache) and every bind sets them from the draw's own key
 * (rd__set_pipeline) through depthCompare and blendState, the functions the
 * static state is made with: each draw gets the same fixed-function tuple
 * either way.  The shadow count's stencil keys keep their depth static, the
 * point keys every state; nodual stays in the key (it picks the module).
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_passring.h"
#include "shader_consts.h"
#include "shaders_gen.h"

/* A cache entry: a key and its pipeline, or a key whose creation failed
 * (pipe.id 0: it holds no slot of the RHI's pool).  A failed key is found
 * by the same hash look-up as a made one, so it costs one creation attempt
 * per session (per cache clear), never one per draw, and its draws find it
 * in O(1).  A driver's failure is not retried after a scene change either:
 * no scene change reaches this file (the cut is the replayed frame's,
 * RdFrame.cut), so a retry needs a hook from the replay (RENDER.md, "The
 * set against the pool"). */
typedef struct RdPipeEntry {
    RdPipeKeyInt key;
    RhiPipeline pipe;
} RdPipeEntry;

static RdPipeEntry s_cache[RD_PIPELINE_CACHE_MAX];

/* s_used: the entries, made and failed (s_cache[0 .. s_used)); s_count: the
 * made ones (rd__pipeline_count, the lines' "created" counts) */
static uint32_t s_used, s_count;

/* the failed entries; past RD_PIPELINE_FAIL_MAX of them s_createStopped:
 * no pipeline is made until the cache is cleared */
static uint32_t s_failedCount;

/* failed creations this session (per cache clear), the line's limiter */
static uint32_t s_failLines;

static bool s_createStopped;

static bool s_fullLogged;

/* the cache's own comparison: its keys are drawKey's, normalised */
static bool keyEqual(const RdPipeKeyInt *a, const RdPipeKeyInt *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* whether a and b select the same pipeline: equal, or equal once the
 * dynamic state's fields are normalised away (rd__key_dynamic_normalise),
 * so a key as a planner made it matches the set's normalised one */
bool rd__pipe_key_equal(const RdPipeKeyInt *a, const RdPipeKeyInt *b)
{
    if (keyEqual(a, b)) {
        return true;
    }
    if (!g_rd.dynDepth && !g_rd.dynBlend) {
        return false;
    }
    RdPipeKeyInt na = *a, nb = *b;
    rd__key_dynamic_normalise(&na);
    rd__key_dynamic_normalise(&nb);
    return keyEqual(&na, &nb);
}

RdBlendPath rd__blend_path(uint8_t b)
{
    switch (b) {
    case RD_BLEND_LERP_FIX:
    case RD_BLEND_LERP_AS:
    case RD_BLEND_LERP_AS_ALT:
        return RD_BP_LERP;
    case RD_BLEND_CS_FIX_ADD_CD:
    case RD_BLEND_CS_AS_ADD_CD:
        return RD_BP_PREMUL_ADD;
    case RD_BLEND_CD_SUB_CS_FIX:
    case RD_BLEND_CD_SUB_CS_AS:
        return RD_BP_PREMUL_REVSUB;
    case RD_BLEND_CD_FIX_ADD_CS:
        return RD_BP_DST_FIX;
    case RD_BLEND_CS_AD_ADD_CD:
        return RD_BP_AD_ADD;
    case RD_BLEND_CD_SUB_CS_AD:
        return RD_BP_AD_REVSUB;
    case RD_BLEND_LERP_AD:
        return RD_BP_AD_LERP;
    case RD_BLEND_CD_AS_ADD_CD:
        return RD_BP_CD_KEEP;
    default:
        return RD_BP_NONE;
    }
}

/* The raw ALPHA register of each mode (GifPacket.c alphaTable, rd_state.h). */
uint32_t rd__alpha_register(uint8_t blend)
{
    /* mode 3 is 0x29 (alphaTable {1, 2, 2, 0}: A Cd, B 0, C FIX, D Cs) */
    static const uint8_t kReg[RD_BLEND_COUNT] = {0x68, 0x62, 0x64, 0x29, 0x44, 0x48,
                                                 0x42, 0x44, 0x58, 0x52, 0x54, 0x49};
    return blend < RD_BLEND_COUNT ? kReg[blend] : 0x44;
}

/* Modes that reach the hardware the same way share a pipeline: the key holds
 * one representative mode per blend path (the uniforms carry the rest). */
static uint8_t canonicalBlend(uint8_t blend)
{
    switch (rd__blend_path(blend)) {
    case RD_BP_LERP:
        return RD_BLEND_LERP_AS;
    case RD_BP_PREMUL_ADD:
        return RD_BLEND_CS_AS_ADD_CD;
    case RD_BP_PREMUL_REVSUB:
        return RD_BLEND_CD_SUB_CS_AS;
    default:
        return blend;
    }
}

static int usesFix(uint8_t blend)
{
    return blend <= RD_BLEND_CD_FIX_ADD_CS; /* modes 0..3: C = FIX */
}

static uint8_t tfmtOf(RhiFormat f)
{
    switch (f) {
    case RHI_FMT_RGBA8_UINT:
        return RD_TFMT_RGBA8_INT;
    case RHI_FMT_R8_UNORM:
        return RD_TFMT_R8;
    default:
        return RD_TFMT_RGBA8;
    }
}

static bool s_alphaMergeLogged; /* rd__plan_screen_draw_ex's merge line, once a run */

/* rd__set_alpha_merge (rd_internal.h) */
static bool s_alphaMerge = true;

void rd__set_alpha_merge(bool on)
{
    s_alphaMerge = on;
}

int rd__plan_screen_draw(const RdStateBlock *s, uint8_t prim, uint8_t space, RhiFormat colorFmt,
                         RhiFormat depthFmt, RdDrawPass out[2])
{
    return rd__plan_screen_draw_ex(s, prim, 0, space, colorFmt, depthFmt, out);
}

/* PRIM.AA1: aa1 is PRIM.AA1 on a line or triangle command (rd_replay.c
 * doScreen decides; points and sprites pass 0).  The key takes the AA1 bit
 * and the AA1 shaders, the topology becomes triangles (rd_replay.c draws an
 * antialiased line as quads), blending is on whatever PRIM.ABE says (the
 * ALPHA register's equation; with ABE 0 the coverage alpha replaces every
 * fragment's alpha, ICO_DF_AA1_FULL, else only an alpha of 0x80), and an
 * antialiased line writes no Z: all its pixels are edge pixels.  The edge
 * geometry of a triangle draws with the pass's key and zwrite off
 * (rd_replay.c). */
int rd__plan_screen_draw_ex(const RdStateBlock *s, uint8_t prim, int aa1, uint8_t space,
                            RhiFormat colorFmt, RhiFormat depthFmt, RdDrawPass out[2])
{
    const RdDrawState *d = &s->ds;
    const int hasDepth = depthFmt != RHI_FMT_UNKNOWN;
    uint8_t blend = (d->abe || aa1) ? d->blend : RD_BLEND_COUNT;
    if (blend > RD_BLEND_COUNT) {
        blend = RD_BLEND_COUNT;
    }
    if (blend == RD_BLEND_LERP_AS_ALT) {
        blend = RD_BLEND_LERP_AS;
    }
    if (colorFmt == RHI_FMT_RGBA8_UINT) {
        blend = RD_BLEND_COUNT; /* integer targets never blend in hardware */
    }
    const RdBlendPath bp = rd__blend_path(blend);
    if (bp == RD_BP_CD_KEEP) {
        rd__log_once(RD_ONCE_CD_KEEP, "blend mode 11 (Cd*As + Cd) is not representable; Cd kept");
    } else if (bp >= RD_BP_AD_ADD) {
        rd__log_once(RD_ONCE_AD, "Ad blend modes 8-10 use DST_ALPHA (Ad/255, not Ad/128)");
    }
    if (!d->colclamp && bp != RD_BP_NONE) {
        rd__log_once(RD_ONCE_COLCLAMP, "COLCLAMP 0 on a hardware-blended draw clamps instead");
    }

    RdDrawPass base;
    memset(&base, 0, sizeof(base));
    RdPipeKeyInt *k = &base.key;
    k->gs.program = RD_PROG_SCREEN;
    k->gs.blend = canonicalBlend(blend);
    k->gs.atst = RD_ATST_ALWAYS;
    k->gs.date = RD_DATE_OFF;
    k->gs.ztst = (!hasDepth || !d->test.zte) ? RD_ZTST_ALWAYS : d->test.ztst;
    k->gs.zwrite = hasDepth ? d->zwrite : RD_ZWRITE_OFF;
    k->gs.colorMask = d->colorMask;
    k->gs.targetFmt = tfmtOf(colorFmt);
    k->gs.prim = prim;
    k->vs = space == RD_SPACE_WORLD ? RD_VS_SPRITE_WORLD : RD_VS_SPRITE_UI;
    k->fs = RD_FS_SPRITE;
    k->colorFmt = (uint8_t)colorFmt;
    k->depthFmt = (uint8_t)depthFmt;
    /* an R8 coverage texture (rd_create_texture_r8) is drawn by font_ps and a
     * sheet texture (rd_create_texture_sheet) by font_sheet_ps; the fragment
     * shader is part of the key */
    if (d->texEnabled) {
        const RdTexRec *tr = rd__tex_rec(s->tex);
        if (tr && tr->kind == RD_TEXKIND_IMAGE && tr->format == RD_TEXEL_R8) {
            k->fs = RD_FS_FONT;
        } else if (tr && tr->kind == RD_TEXKIND_IMAGE && tr->format == RD_TEXEL_SHEET) {
            k->fs = RD_FS_FONT_SHEET;
        }
    }
    /* PRIM.AA1 (not for an R8 texture: no AA1 draw samples one) */
    if (aa1 && k->fs == RD_FS_SPRITE) {
        k->gs.aa1 = 1;
        k->gs.prim = RD_PRIM_TRIANGLES;
        if (prim == RD_PRIM_LINES) {
            k->gs.zwrite = RD_ZWRITE_OFF;
        }
        k->vs = space == RD_SPACE_WORLD ? RD_VS_SPRITE_AA1_WORLD : RD_VS_SPRITE_AA1_UI;
        k->fs = RD_FS_SPRITE_AA1;
        if (!d->abe) {
            base.flags |= ICO_DF_AA1_FULL;
        }
    }

    /* TEXA applied per texel, before the bilinear filter */
    if (k->fs == RD_FS_SPRITE && rd__texa_per_texel(s)) {
        k->fs = RD_FS_SPRITE_TEXA;
    }

    if (d->fba) {
        base.flags |= ICO_DF_FBA;
    }
    if (d->pabe) {
        base.flags |= ICO_DF_PABE;
        if (bp == RD_BP_PREMUL_ADD || bp == RD_BP_PREMUL_REVSUB || bp >= RD_BP_AD_ADD) {
            rd__log_once(RD_ONCE_PABE, "PABE on an additive, subtractive or destination-alpha "
                                       "blend: pixels with As below 0x80 blend instead of "
                                       "writing Cs");
        }
    }
    if (bp == RD_BP_DST_FIX) {
        base.flags |= ICO_DF_C1_DST;
    }
    /* DATE: a shader test against the R8 snapshot rd_replay.c binds
     * at t2; a uniform, so the key keeps date normalised to off */
    if (d->test.date == RD_DATE_DEST_ALPHA_0) {
        base.flags |= ICO_DF_DATE;
    } else if (d->test.date == RD_DATE_DEST_ALPHA_1) {
        base.flags |= ICO_DF_DATE | ICO_DF_DATM;
    }
    base.fix = d->blendFix;
    /* the exact blend (rd_dst_read.c): FIX as the GS has it, and whether
     * the hardware path below is not the GS's arithmetic: the LERPs and
     * Cd*FIX + Cs round, the Ad forms read Ad/255, Cd*As + Cd is not
     * representable, PABE cannot drop Cd from a premultiplied form, COLCLAMP
     * 0 wraps where the blender clamps */
    base.fixGs = d->blendFix;
    base.dstWant = (uint8_t)(blend < RD_BLEND_COUNT &&
                             (bp == RD_BP_LERP || bp == RD_BP_DST_FIX || bp >= RD_BP_AD_ADD ||
                              (d->pabe && (bp == RD_BP_PREMUL_ADD || bp == RD_BP_PREMUL_REVSUB)) ||
                              !d->colclamp));
    if (blend < RD_BLEND_COUNT) {
        if (usesFix(blend)) {
            base.flags |= ICO_DF_FIX_FACTOR;
        }
        if (bp == RD_BP_PREMUL_ADD || bp == RD_BP_PREMUL_REVSUB) {
            base.flags |= ICO_DF_PREMUL;
        } else if (base.fix > 0x80) {
            base.fix = 0x80; /* LERP_FIX, Cd*FIX + Cs: the factor cannot pass 1.0 */
        }
    }
    base.aref = d->test.aref;

    const int ate = d->test.ate && d->test.atst != RD_ATST_ALWAYS;
    if (!ate) {
        out[0] = base;
        return 1;
    }
    if (d->test.afail == RD_AFAIL_KEEP) {
        out[0] = base;
        out[0].modeZ = d->test.atst | (1u << 8);
        return 1;
    }
    /* the fragments that fail: FB_ONLY keeps colour, drops Z; ZB_ONLY keeps
     * Z, drops colour; RGB_ONLY keeps RGB only */
    RdDrawPass fail = base;
    if (d->test.afail == RD_AFAIL_FB_ONLY) {
        fail.key.gs.zwrite = RD_ZWRITE_OFF;
    } else if (d->test.afail == RD_AFAIL_ZB_ONLY) {
        fail.key.gs.colorMask = 0;
    } else {
        fail.key.gs.zwrite = RD_ZWRITE_OFF;
        fail.key.gs.colorMask &= 0x7;
    }
    const int failWrites = fail.key.gs.colorMask != 0 || fail.key.gs.zwrite == RD_ZWRITE_ON;
    /* Z write off and a failing fragment writing what a passing one writes
     * (FB_ONLY, or RGB_ONLY without the alpha channel in the mask): the test
     * decides nothing, so one draw without it, every fragment in the GS's
     * primitive order.  GS User's Manual, TEST.AFAIL: FB_ONLY updates the
     * frame buffer and not the Z buffer, RGB_ONLY the frame buffer's RGB
     * only; with ZBUF.ZMSK 1 a passing fragment updates the frame buffer
     * only too.  PCSX2's software renderer skips the test there
     * (GSDrawScanline.cpp TestAlpha: FB_ONLY without zwrite, RGB_ONLY
     * without zwrite on a 24-bit frame) and its hardware renderer sets ATST
     * ALWAYS (GSRendererHW.cpp, "Failing alpha test is a NOP").  The split
     * wrote every failing fragment after every passing one, so where the two
     * overlapped on a pixel the order was not the GS's (issues 95 and
     * 103).  DATE, FBA, PABE and the blend ride on the one draw as on the
     * two. */
    if (s_alphaMerge && failWrites && base.key.gs.zwrite == RD_ZWRITE_OFF &&
        fail.key.gs.zwrite == RD_ZWRITE_OFF && fail.key.gs.colorMask == base.key.gs.colorMask) {
        out[0] = base;
        if (!s_alphaMergeLogged) {
            s_alphaMergeLogged = true;
            rd__log("alpha test: a draw with Z write off whose failing pixels write what its "
                    "passing pixels write (AFAIL FB_ONLY, RGB_ONLY without alpha) is one draw "
                    "without the test, in the GS's order (reported once)");
        }
        return 1;
    }
    if (d->test.atst == RD_ATST_NEVER) {
        if (!failWrites) {
            return 0;
        }
        out[0] = fail; /* every fragment fails: one pass, no test in the shader */
        return 1;
    }
    out[0] = base;
    out[0].modeZ = d->test.atst | (1u << 8) | (1u << 16);
    if (!failWrites) {
        return 1;
    }
    out[1] = fail;
    out[1].modeZ = d->test.atst | (1u << 8) | (2u << 16);
    return 2;
}

/* A linear-filtered texture with GS levels drawn at the GS's level
 * (DF_GS_LOD) in the Original preset: filtered as the GS filters, not by
 * the sampler's own LOD and level blend.  PCSX2's per-draw dumps of the
 * opening's mist (fog1, list 5) had the sampler's version about 5 %
 * brighter (forest: +19.5 against +18.6 levels): its fraction between
 * levels (4 to 8 bits on a device) and its texel weights off the GS's
 * 12.4 grid.  Two forms: a texture whose TEXA changes texels before the
 * filter (rd__texa_per_texel) takes the TEXA entries' four-tap integer
 * filter (gs_texa_texture_lod); every other one the sampler at the GS's
 * grid point of each level, nearest between levels, the two levels
 * blended in 256ths as the GS does (common.hlsli gs_sample_gs_bilinear,
 * rd_replay.c fillDrawCB's ICO_DF_GS_BILINEAR_GRID and gsLodSampler),
 * within 2 levels of the GS's truncating steps (docs/port/RENDER.md,
 * "Textures"). */
int rd__gs_filter_int(const RdStateBlock *s)
{
    const RdDrawState *d = &s->ds;
    if (g_rd.settings.preset != RD_PRESET_ORIGINAL || !d->texEnabled ||
        !rd_tex1_mipmapped(s->tex1Lod) ||
        (d->magFilter != RD_FILTER_LINEAR && d->minFilter != RD_FILTER_LINEAR)) {
        return 0;
    }
    const RdTexRec *tr = rd__tex_rec(s->tex);
    return tr && tr->kind == RD_TEXKIND_IMAGE && !tr->replacement &&
           ((tr->gsLevels > 1 && tr->gsChain) || tr->mipLevels > 1);
}

/* TEXA before the filter: a 24- or 16-bit source (or CLUT) under a TEXA
 * whose alpha depends on the texel (AEM, or TA0 != TA1: the two modes
 * other than 80/80) with a linear filter.  An RGBA32 texture and TEXA
 * 80/80 give the same texels before and after the filter, and take the
 * sampler (with GS levels on the GS's grid: rd__gs_filter_int). */
int rd__texa_per_texel(const RdStateBlock *s)
{
    const RdDrawState *d = &s->ds;
    if (!d->texEnabled || d->texa == RD_TEXA_80_80) {
        return 0; /* TEXA 80/80 gives every texel alpha 0x80: no order to keep */
    }
    if (d->magFilter != RD_FILTER_LINEAR && d->minFilter != RD_FILTER_LINEAR) {
        return 0; /* nearest: one texel, expanded after the fetch */
    }
    const RdTexRec *tr = rd__tex_rec(s->tex);
    return tr && tr->src != RD_TEXSRC_RGBA32 &&
           !(tr->kind == RD_TEXKIND_IMAGE && rd__texel_is_coverage(tr->format));
}

/* Converts a planned screen pass for a command whose prims carry Q != 1 to
 * the STQ vertex shader of its space and sprite_stq_ps.  Only the plain
 * sprite pass converts (an R8 font texture and PRIM.AA1 keep their
 * shaders); returns whether it did.  A sprite_texa_ps pass converts too,
 * and sprite_stq_ps expands TEXA after the sampler. */
int rd__stq_pass(RdDrawPass *dp)
{
    RdPipeKeyInt *k = &dp->key;
    if ((k->fs != RD_FS_SPRITE && k->fs != RD_FS_SPRITE_TEXA) ||
        (k->vs != RD_VS_SPRITE_UI && k->vs != RD_VS_SPRITE_WORLD)) {
        return 0;
    }
    k->vs = k->vs == RD_VS_SPRITE_WORLD ? RD_VS_SPRITE_STQ_WORLD : RD_VS_SPRITE_STQ_UI;
    k->fs = RD_FS_SPRITE_STQ;
    return 1;
}

/* The presentation overlay's state (rd.h rd_overlay_prims):
 * blend with ABE on, no Z test or write (and no depth target), no alpha
 * test, no DATE, FBA and PABE off, COLCLAMP on, all channels written. */
void rd__overlay_state(RdStateBlock *s, uint8_t blend)
{
    rd__reset_state_block(s);
    s->ds.test = rd_test_from_gs(RD_TEST_OFF);
    s->ds.zwrite = RD_ZWRITE_OFF;
    s->ds.abe = 1;
    s->ds.blend = blend;
}

RdPipeKeyInt rd__post_key(RdVsId vs, RdFsId fs, RhiFormat colorFmt)
{
    RdPipeKeyInt k;
    memset(&k, 0, sizeof(k));
    k.gs.program = RD_PROG_POST;
    k.gs.blend = RD_BLEND_COUNT;
    k.gs.atst = RD_ATST_ALWAYS;
    k.gs.ztst = RD_ZTST_ALWAYS;
    k.gs.zwrite = RD_ZWRITE_OFF;
    k.gs.colorMask = 0xF;
    k.gs.targetFmt = tfmtOf(colorFmt);
    k.gs.prim = RD_PRIM_TRIANGLES;
    k.vs = (uint8_t)vs;
    k.fs = (uint8_t)fs;
    k.colorFmt = (uint8_t)colorFmt;
    k.depthFmt = RHI_FMT_UNKNOWN;
    return k;
}

RdPipeKeyInt rd__present_depth_key(RhiFormat colorFmt)
{
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT_DEPTH, colorFmt);
    k.depthFmt = RHI_FMT_D32F;
    k.gs.ztst = RD_ZTST_ALWAYS;
    k.gs.zwrite = RD_ZWRITE_ON;
    return k;
}

/* The entries that call gs_dual_out (their *_nodual twins
 * output c0 alone). */
bool rd__fs_has_no_dual(uint8_t fs)
{
    switch (fs) {
    case RD_FS_SPRITE:
    case RD_FS_SPRITE_TEXA:
    case RD_FS_SPRITE_AA1:
    case RD_FS_SPRITE_STQ:
    case RD_FS_FONT:
    case RD_FS_FONT_SHEET:
    case RD_FS_FOG:
    case RD_FS_FOG_BUFFER:
    case RD_FS_FOG_MS:
    case RD_FS_FOG_LOCAL:
    case RD_FS_VU:
    case RD_FS_VU_TEXA:
    case RD_FS_SPRITE_PLANES:
    case RD_FS_SPRITE_TEXA_PLANES:
    case RD_FS_SPRITE_STQ_PLANES:
    case RD_FS_VU_NP:
    case RD_FS_VU_TEXA_NP:
        return true;
    default:
        return false;
    }
}

bool rd__fs_local_read(uint8_t fs)
{
    return fs == RD_FS_FOG_LOCAL || fs == RD_FS_DATE_STENCIL || fs == RD_FS_FX_LOCAL;
}

static RdBlendPath keyBlendPath(const RdPipeKeyInt *k)
{
    return k->colorFmt == RHI_FMT_RGBA8_UINT ? RD_BP_NONE : rd__blend_path(k->gs.blend);
}

int rd__expand_no_dual(const RdDrawPass *in, int n, RdDrawPass out[4])
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        RdDrawPass p = in[i];
        /* a destination-read pass (rd_dst_read.c) has no second output to
         * split: the shader blends */
        if (!g_rd.noDual || !rd__fs_has_no_dual(p.key.fs) || p.key.dst != RHI_DST_READ_NONE) {
            out[m++] = p;
            continue;
        }
        p.key.gs.nodual = 1;
        const RdBlendPath bp = keyBlendPath(&p.key);
        if (bp != RD_BP_LERP && bp != RD_BP_DST_FIX) {
            out[m++] = p; /* no second output read: one pass, the *_nodual entry */
            continue;
        }
        const uint8_t mask = p.key.gs.colorMask;
        if ((mask & 7) == 0) {
            /* nothing blended: the stored alpha (if A is written) and Z */
            p.key.gs.blend = RD_BLEND_COUNT;
            p.flags |= ICO_DF_NODUAL_ALPHA_PASS;
            out[m++] = p;
            continue;
        }
        RdDrawPass c = p;
        c.key.gs.colorMask = mask & 7;
        c.flags |= ICO_DF_NODUAL_FACTOR;
        out[m++] = c;
        if (mask & 8) {
            RdDrawPass a = p;
            a.key.gs.blend = RD_BLEND_COUNT;
            a.key.gs.colorMask = 8;
            a.flags |= ICO_DF_NODUAL_ALPHA_PASS;
            if (a.key.gs.zwrite == RD_ZWRITE_ON) {
                a.key.gs.zwrite = RD_ZWRITE_OFF;
                if (a.key.gs.ztst == RD_ZTST_GREATER) {
                    a.key.gs.ztst = RD_ZTST_GEQUAL;
                    rd__log_once(RD_ONCE_NODUAL_GREATER,
                                 "blend: two-pass fallback under Z GREATER with Z write: a "
                                 "fragment at the stored Z writes its alpha");
                }
            }
            out[m++] = a;
        }
    }
    return m;
}

bool rd__no_dual_second(const RdDrawPass *ex, int i)
{
    return i > 0 && (ex[i].flags & ICO_DF_NODUAL_ALPHA_PASS) != 0 &&
           (ex[i - 1].flags & ICO_DF_NODUAL_FACTOR) != 0;
}

static RhiBlendState blendState(RdBlendPath bp, uint8_t mask, uint8_t nodual)
{
    RhiBlendState b;
    memset(&b, 0, sizeof(b));
    b.writeMask = mask;
    b.srcAlpha = RHI_BF_ONE; /* the GS writes As (FBA aside) whatever the equation */
    b.dstAlpha = RHI_BF_ZERO;
    b.alphaOp = RHI_BO_ADD;
    b.colorOp = RHI_BO_ADD;
    b.enable = bp != RD_BP_NONE;
    switch (bp) {
    case RD_BP_LERP:
        /* no dual-source blending: the colour pass's c0.a is the factor */
        b.srcColor = nodual ? RHI_BF_SRC_ALPHA : RHI_BF_SRC1_COLOR;
        b.dstColor = nodual ? RHI_BF_ONE_MINUS_SRC_ALPHA : RHI_BF_ONE_MINUS_SRC1_COLOR;
        break;
    case RD_BP_PREMUL_ADD:
        b.srcColor = RHI_BF_ONE;
        b.dstColor = RHI_BF_ONE;
        break;
    case RD_BP_PREMUL_REVSUB:
        b.srcColor = RHI_BF_ONE;
        b.dstColor = RHI_BF_ONE;
        b.colorOp = RHI_BO_REVERSE_SUBTRACT;
        break;
    case RD_BP_DST_FIX:
        b.srcColor = RHI_BF_ONE;
        b.dstColor = nodual ? RHI_BF_SRC_ALPHA : RHI_BF_SRC1_COLOR;
        break;
    case RD_BP_AD_ADD:
        b.srcColor = RHI_BF_DST_ALPHA;
        b.dstColor = RHI_BF_ONE;
        break;
    case RD_BP_AD_REVSUB:
        b.srcColor = RHI_BF_DST_ALPHA;
        b.dstColor = RHI_BF_ONE;
        b.colorOp = RHI_BO_REVERSE_SUBTRACT;
        break;
    case RD_BP_AD_LERP:
        b.srcColor = RHI_BF_DST_ALPHA;
        b.dstColor = RHI_BF_ONE_MINUS_DST_ALPHA;
        break;
    case RD_BP_CD_KEEP:
        b.srcColor = RHI_BF_ZERO;
        b.dstColor = RHI_BF_ONE;
        break;
    default:
        b.srcColor = RHI_BF_ONE;
        b.dstColor = RHI_BF_ZERO;
        break;
    }
    return b;
}

static RhiCompare depthCompare(uint8_t ztst)
{
    switch (ztst) {
    case RD_ZTST_NEVER:
        return RHI_CMP_NEVER;
    case RD_ZTST_GEQUAL:
        return RHI_CMP_GEQUAL;
    case RD_ZTST_GREATER:
        return RHI_CMP_GREATER;
    default:
        return RHI_CMP_ALWAYS;
    }
}

/* The stencil of the shadow count.
 * Volumes: both faces INCR_WRAP or DECR_WRAP where the depth test passes,
 * written through RD_SHADOW_STENCIL_MASK, so the stencil holds n mod 64 as
 * 4 n mod 256 wraps.  Resolve bit k: EQUAL to the reference 1 << k under
 * read mask 1 << k; resolve A: NOTEQUAL 0 under the count's mask.  The
 * resolve passes write no stencil.
 * DATE's stencil bit (RD_DATE_STENCIL_BIT, the reference
 * RD_DATE_STENCIL_BIT for every one of them, rd_replay.c): CLEAR replaces
 * the bit with the reference 0 the setup sets (ALWAYS), SET with the
 * reference's 1 where the fragment survives date_stencil_ps (ALWAYS), both
 * through the write mask of the bit alone; TEST1 passes where the bit is 1
 * (EQUAL to the reference under the bit's read mask), TEST0 where it is 0
 * (NOTEQUAL), writing nothing.  None of them reads or writes bits 0-5, the
 * count's. */
static void shadowStencil(uint8_t mode, RhiDepthStencilState *ds)
{
    RhiStencilFace f = {RHI_CMP_ALWAYS, RHI_SO_KEEP, RHI_SO_KEEP, RHI_SO_KEEP};
    switch (mode) {
    case RD_STENCIL_OFF:
        return;
    case RD_STENCIL_INCR:
    case RD_STENCIL_DECR:
        f.pass = mode == RD_STENCIL_INCR ? RHI_SO_INCR_WRAP : RHI_SO_DECR_WRAP;
        ds->stencilReadMask = 0xFF;
        ds->stencilWriteMask = RD_SHADOW_STENCIL_MASK;
        break;
    case RD_STENCIL_TEST_NONZERO:
        f.compare = RHI_CMP_NOTEQUAL;
        ds->stencilReadMask = RD_SHADOW_STENCIL_MASK;
        ds->stencilWriteMask = 0;
        break;
    case RD_STENCIL_DATE_CLEAR:
    case RD_STENCIL_DATE_SET:
        f.pass = RHI_SO_REPLACE;
        ds->stencilReadMask = 0;
        ds->stencilWriteMask = RD_DATE_STENCIL_BIT;
        break;
    case RD_STENCIL_DATE_TEST0:
    case RD_STENCIL_DATE_TEST1:
        f.compare = mode == RD_STENCIL_DATE_TEST1 ? RHI_CMP_EQUAL : RHI_CMP_NOTEQUAL;
        ds->stencilReadMask = RD_DATE_STENCIL_BIT;
        ds->stencilWriteMask = 0;
        break;
    default:
        f.compare = RHI_CMP_EQUAL;
        ds->stencilReadMask = (uint8_t)(1u << ((mode - RD_STENCIL_RESOLVE_BIT0) & 7));
        ds->stencilWriteMask = 0;
        break;
    }
    ds->stencilTest = true;
    ds->front = f;
    ds->back = f;
}

/* k's description into *d; layouts (4 entries) holds the bind group
 * layouts d points at.  false: the key's destination read has no shader or
 * layout here (it is not created) */
static bool pipelineDesc(const RdPipeKeyInt *k, RhiPipelineDesc *d, RhiBindGroupLayout *layouts)
{
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    /* PRIM.AA1: the same and the coverage */
    static const RhiVertexBinding vbAa1 = {0, sizeof(IcoSpriteAa1Vertex), false};
    static const RhiVertexAttr vaAa1[5] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
        {4, 0, RHI_VTX_F32x1, offsetof(IcoSpriteAa1Vertex, cov)},
    };
    /* STQ: the same and Q */
    static const RhiVertexBinding vbStq = {0, sizeof(IcoSpriteStqVertex), false};
    static const RhiVertexAttr vaStq[5] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
        {4, 0, RHI_VTX_F32x1, offsetof(IcoSpriteStqVertex, q)},
    };
    /* the triangles' planes (rd_screen_planes.c): the STQ vertex and the planes */
    static const RhiVertexBinding vbPlane = {0, sizeof(IcoSpritePlaneVertex), false};
    static const RhiVertexAttr vaPlane[11] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
        {4, 0, RHI_VTX_F32x1, offsetof(IcoSpriteStqVertex, q)},
        {5, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, c0)},
        {6, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, cx)},
        {7, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, cy)},
        {8, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, s0)},
        {9, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, sx)},
        {10, 0, RHI_VTX_F32x4, offsetof(IcoSpritePlaneVertex, sy)},
    };
    if (rd__vs_is_vu_pre(k->vs) || rd__vs_is_vu_stage(k->vs)) {
        /* the vertex pre-pass and the record stages (rd_replay_vu.c
         * rd__vu_prepass): the entry reads group 1 as the VU programs do;
         * points into the record's three RGBA32_UINT targets through
         * vu_pre_ps, no depth, no blend, the key's colour mask on each
         * target (0xF but for the tick stage's R or G, rd__vu_stage_key) */
        layouts[0] = g_rd.layoutFrame;
        layouts[1] = g_rd.layoutVu;
        layouts[2] = g_rd.layoutTex;
        memset(d, 0, sizeof(*d));
        d->vertex = g_rd.vs[k->vs];
        d->fragment = g_rd.fs[RD_FS_VU_PRE];
        if (!d->vertex.id || !d->fragment.id) {
            return false;
        }
        d->layouts = layouts;
        d->layoutCount = 3;
        d->topology = RHI_TOPO_POINT_LIST;
        d->cullNone = true;
        for (uint32_t i = 0; i < ICO_VU_REC_PLANES; i++) {
            d->blend[i].writeMask = (uint8_t)(k->gs.colorMask & 0xFu);
            d->colorFormats[i] = RHI_FMT_RGBA32_UINT;
        }
        d->colorCount = ICO_VU_REC_PLANES;
        d->depthFormat = RHI_FMT_UNKNOWN;
        d->debugName = "rd vu pre-pass";
        return true;
    }
    const int vu = (k->vs >= RD_VS_VU_FIRST && k->vs <= RD_VS_VU_LAST) || rd__vs_is_vu_main(k->vs);
    /* the VU program shaders read the stream, VuCB and
     * VuBoneCB from group 1 next to DrawCB, and have no vertex input */
    /* the exact blend's destination read (rd_dst_read.c): group 3 holds the
     * target, the fragment entry is the _dstrov or _dstatt twin */
    const bool dstStorage = k->dst == RHI_DST_READ_STORAGE;
    const bool dstAttach = k->dst == RHI_DST_READ_ATTACHMENT;
    layouts[0] = g_rd.layoutFrame;
    layouts[1] = vu ? g_rd.layoutVu : g_rd.layoutDraw;
    layouts[2] = k->fs == RD_FS_BLEND_INT   ? g_rd.layoutInt
                 : rd__fs_local_read(k->fs) ? g_rd.layoutLocal
                                            : g_rd.layoutTex;
    layouts[3] = dstStorage ? g_rd.layoutDstStorage : g_rd.layoutDstAttach;
    memset(d, 0, sizeof(*d));
    /* a program entry's key: the fallback's module (rd__vu_policy_init,
     * the plain variant on a device of the table) */
    d->vertex = k->vs >= RD_VS_VU_PRELIT && k->vs <= RD_VS_VU_PARTICLE && g_rd.vsFallback[k->vs].id
                    ? g_rd.vsFallback[k->vs]
                    : g_rd.vs[k->vs];
    if (!d->vertex.id) {
        return false;
    }
    /* no dual-source blending: the *_nodual twin (c0 alone) */
    d->fragment = k->gs.nodual && g_rd.fsNoDual[k->fs].id ? g_rd.fsNoDual[k->fs] : g_rd.fs[k->fs];
    if (!d->fragment.id) {
        return false; /* a module this build does not have (rd__gpu_init's fence) */
    }
    if (dstStorage || dstAttach) {
        d->fragment = dstStorage ? g_rd.fsDstRov[k->fs] : g_rd.fsDstAtt[k->fs];
        if (!d->fragment.id || !layouts[3].id) {
            return false;
        }
        d->dstRead = dstStorage ? RHI_DST_READ_STORAGE : RHI_DST_READ_ATTACHMENT;
    }
    d->layouts = layouts;
    d->layoutCount = dstStorage || dstAttach ? 4u : 3u;
    if (k->vs == RD_VS_SPRITE_UI || k->vs == RD_VS_SPRITE_WORLD) {
        d->vertexBindings = &vb;
        d->vertexBindingCount = 1;
        d->vertexAttrs = va;
        d->vertexAttrCount = 4;
    } else if (k->vs == RD_VS_SPRITE_AA1_UI || k->vs == RD_VS_SPRITE_AA1_WORLD) {
        d->vertexBindings = &vbAa1;
        d->vertexBindingCount = 1;
        d->vertexAttrs = vaAa1;
        d->vertexAttrCount = 5;
    } else if (k->vs == RD_VS_SPRITE_STQ_UI || k->vs == RD_VS_SPRITE_STQ_WORLD) {
        d->vertexBindings = &vbStq;
        d->vertexBindingCount = 1;
        d->vertexAttrs = vaStq;
        d->vertexAttrCount = 5;
    } else if (k->vs >= RD_VS_SPRITE_PLANE_UI && k->vs <= RD_VS_SPRITE_STQ_PLANE_WORLD) {
        d->vertexBindings = &vbPlane;
        d->vertexBindingCount = 1;
        d->vertexAttrs = vaPlane;
        d->vertexAttrCount = 11;
    }
    d->topology = k->gs.prim == RD_PRIM_LINES ? RHI_TOPO_LINE_LIST : RHI_TOPO_TRIANGLE_LIST;
    d->cullNone = true;
    d->blend[0] = blendState(keyBlendPath(k), k->gs.colorMask, k->gs.nodual);
    if (k->depthFmt != RHI_FMT_UNKNOWN) {
        d->depthStencil.depthTest = true;
        d->depthStencil.depthWrite = k->gs.zwrite == RD_ZWRITE_ON;
        d->depthStencil.depthCompare = depthCompare(k->gs.ztst);
        shadowStencil(k->gs.stencil, &d->depthStencil); /* the shadow count */
    }
    d->colorFormats[0] = (RhiFormat)k->colorFmt;
    /* no colour format: a depth-only pass (the MSAA depth resolve); the
     * storage destination read writes the target itself, in a pass without
     * a colour attachment */
    d->colorCount = k->colorFmt != RHI_FMT_UNKNOWN && !dstStorage ? 1u : 0u;
    d->depthFormat = (RhiFormat)k->depthFmt;
    d->debugName = "rd";
    d->samples = k->gs.msaa; /* 0: single-sampled */
    /* the groups the key leaves to the draw (its static values above are
     * the normalised ones; rd__set_pipeline sets the draw's) */
    d->dynamic = rd__key_dynamic_bits(k);
    return true;
}

static RhiPipeline createPipeline(const RdPipeKeyInt *k)
{
    RhiPipelineDesc d;
    RhiBindGroupLayout layouts[4];
    if (!pipelineDesc(k, &d, layouts)) {
        return (RhiPipeline){0};
    }
    return rhi_create_pipeline(&d);
}

/* An open-addressed index over s_cache (slot + 1, 0 = empty),
 * so a draw's lookup is a hash and a compare, not a walk of every pipeline */
#define RD_PIPE_HASH (4 * RD_PIPELINE_CACHE_MAX)

static uint16_t s_hash[RD_PIPE_HASH];
_Static_assert(RD_PIPELINE_CACHE_MAX < 65535, "s_hash holds a cache slot + 1 in 16 bits");

static uint32_t keyHash(const RdPipeKeyInt *k)
{
    const uint8_t *b = (const uint8_t *)k;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*k); i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

static RdActivityFn s_activityFn;
/* the batch's note while its pipelines are compiled ("" outside): a
 * creation of the progress screen's own in the middle of it puts it back
 * after its own note, so a crash on a worker thread names the batch */
static char s_batchNote
    [768]; /* a 96-byte range, 320 of modules and the compiler's 96 fit (gcc 14's -Wformat-truncation at -O2 counts them); ico_diag_set_activity keeps its own bound */

void rd_set_activity_fn(RdActivityFn fn)
{
    s_activityFn = fn;
}

/* rd_set_batch_fn: a VU batch's compile times as it completes (issue 107) */
static RdActivityFn s_batchFn;

void rd_set_batch_fn(RdActivityFn fn)
{
    s_batchFn = fn;
}

/* rd_set_memory_fn: the process's memory for the batches' lines (issues
 * 93 and 107) */
static RdMemoryFn s_memoryFn;

void rd_set_memory_fn(RdMemoryFn fn)
{
    s_memoryFn = fn;
}

/* rd_set_low_memory_fn: the system's low-memory signal stops the start-up
 * set between two batches (issue 128) */
static RdLowMemoryFn s_lowMemoryFn;

void rd_set_low_memory_fn(RdLowMemoryFn fn)
{
    s_lowMemoryFn = fn;
}

/* How the start-up set is made (rd.h RdPipelineSetPolicy; createKeys):
 * zero fields are the defaults (RD_PIPE_BATCH keys a batch, the backend's
 * threads, no memory bound, the cache written as cacheKeptPerBatch says).
 * The careful values (rd_pipeline_policy_careful) are what the window
 * passes on a device of the table's careful column
 * (RhiLimits.carefulPipelines; docs/port/RENDER.md "The start-up set"):
 * batches of 8 on one worker thread, the cache written after each, the
 * set stopped once the process grew 128 MB past its size at the set's
 * start or a batch took over RD_SET_SLOW_BATCH_MS. */
static RdPipelineSetPolicy s_setPolicy;

void rd_set_pipeline_policy(const RdPipelineSetPolicy *p)
{
    if (p) {
        s_setPolicy = *p;
    } else {
        memset(&s_setPolicy, 0, sizeof(s_setPolicy));
    }
}

const RdPipelineSetPolicy *rd_pipeline_policy(void)
{
    return &s_setPolicy;
}

const RdPipelineSetPolicy *rd_pipeline_policy_careful(void)
{
    static const RdPipelineSetPolicy careful = {8u, 1u, 128u, true};
    return &careful;
}

/* Whether the pipeline cache is measured for the batches' lines and written
 * between the start-up set's batches (issues 93 and 107): on a phone or a
 * tablet (Android, iOS) always, so a set the system ends keeps what it
 * compiled and its lines show the cache's growth; on a desktop only on the
 * lazy path, on a device of the table (RhiLimits.carefulPipelines) and
 * under a policy with cacheEachBatch, never in a desktop's usual start-up
 * set.  Each measure is vkGetPipelineCacheData over the
 * whole cache (21 MB on an RTX 3090) and each write compares or writes it
 * all: with them after every batch a warm start on that card took 16.7 s
 * and 31.2 s against 1.9 s without, every pipeline a cache hit. */
static bool cacheKeptPerBatch(void)
{
#if defined(__ANDROID__) || defined(ICO_IOS)
    return true;
#else
    const RhiLimits *lim = rhi_limits();
    return rd__pipelines_lazy_on() || s_setPolicy.cacheEachBatch ||
           (lim != NULL && lim->carefulPipelines != NULL);
#endif
}

/* "memory 185 MB (most 194 MB), pipeline cache 3104 KB": the process's
 * resident memory and its peak (rd_set_memory_fn) and the pipeline cache's
 * data (rhi_pipeline_cache_size; "pipeline cache not measured" where
 * cacheKeptPerBatch says no), for a batch's line: a set that the system
 * ends shows whether the memory grew with it, batch by batch */
static void memoryText(char *out, size_t size)
{
    long rss = -1, peak = -1;
    char mem[64] = "memory unknown";
    if (s_memoryFn != NULL && s_memoryFn(&rss, &peak) == 0 && rss >= 0) {
        snprintf(mem, sizeof(mem), "memory %ld MB (most %ld MB)", rss / 1024,
                 (peak >= 0 ? peak : rss) / 1024);
    }
    if (!cacheKeptPerBatch()) {
        snprintf(out, size, "%s, pipeline cache not measured", mem);
        return;
    }
    snprintf(out, size, "%s, pipeline cache %llu KB", mem,
             (unsigned long long)((rhi_pipeline_cache_size() + 1023u) / 1024u));
}

/* the device's compiler as its driver names it ("E031.41.03.47"; "" none),
 * for the VU batches' notes and lines (rd__vu_policy_init) */
static char s_compiler[64];

/* a key's words, for the notes and the log */
static void keyWords(const RdPipeKeyInt *k, char *out, size_t size);

void rd__pipe_key_text(const RdPipeKeyInt *k, char *out, size_t size)
{
    keyWords(k, out, size);
}

/* a key with a VU vertex shader: a program entry, the old triangle stage,
 * the record path's two drawing shaders, the pre-pass's and the stages'
 * point entries */
static bool keyIsVu(const RdPipeKeyInt *k)
{
    return (k->vs >= RD_VS_VU_FIRST && k->vs <= RD_VS_VU_LAST) || rd__vs_is_vu_main(k->vs) ||
           rd__vs_is_vu_pre(k->vs) || rd__vs_is_vu_stage(k->vs);
}

/* the key's fields; a point key (the pre-pass's, a record stage's) " pre"
 * (startup_retry.h startup_retry_note_has_vu_pre reads it) */
static void keyWords(const RdPipeKeyInt *k, char *out, size_t size)
{
    snprintf(out, size, "program %u blend %u vs %u fs %u fmt %u/%u msaa %u dst %u%s", k->gs.program,
             k->gs.blend, k->vs, k->fs, k->colorFmt, k->depthFmt, k->gs.msaa, (unsigned)k->dst,
             rd__vs_is_vu_pre(k->vs) || rd__vs_is_vu_stage(k->vs) ? " pre" : "");
}

/* whether a program entry's key draws through the plain variant (the
 * fallback on a device of the table, rd__vu_policy_init) */
static bool keyPlain(const RdPipeKeyInt *k)
{
    return g_rd.vuFallbackPlain && k->vs >= RD_VS_VU_PRELIT && k->vs <= RD_VS_VU_GRID_SPEC &&
           g_rd.vsPlain[k->vs].id;
}

/* the modules a key's pipeline is made of, as pipelineDesc picks them: the
 * vertex shader (a program entry's plain variant where the fallback takes
 * it) and the fragment shader (its _dstrov, _dstatt or _nodual twin) */
static void keyVsName(const RdPipeKeyInt *k, char *out, size_t size)
{
    const char *v = rd__vs_name(k->vs);
    const size_t n = strlen(v);
    if (keyPlain(k) && n > 3) {
        snprintf(out, size, "%.*s_plain_vs", (int)(n - 3), v);
    } else {
        snprintf(out, size, "%s", v);
    }
}

static void keyFsName(const RdPipeKeyInt *k, char *out, size_t size)
{
    const char *f = rd__fs_name(k->fs);
    if (k->dst == RHI_DST_READ_STORAGE) {
        snprintf(out, size, "%s_dstrov", f);
    } else if (k->dst == RHI_DST_READ_ATTACHMENT) {
        snprintf(out, size, "%s_dstatt", f);
    } else if (k->gs.nodual && k->fs < RD_FS_COUNT && g_rd.fsNoDual[k->fs].id) {
        snprintf(out, size, "%s_nodual", f);
    } else {
        snprintf(out, size, "%s", f);
    }
}

/* "vu_lit_plain_vs 23305 words + vu_ps 9876 words": the key's modules and
 * their sizes (rd__shader_words) */
static void keyModules(const RdPipeKeyInt *k, char *out, size_t size)
{
    char v[64], f[64];
    keyVsName(k, v, sizeof(v));
    keyFsName(k, f, sizeof(f));
    snprintf(out, size, "%s %u words + %s %u words", v, rd__shader_words(v), f,
             rd__shader_words(f));
}

/* the batch's vertex shaders, each once in the batch's order with its size,
 * and its largest fragment shader: "vertex vu_prelit_plain_vs 22392 words,
 * vu_lit_plain_vs 23305 words; largest fragment vu_ps 9876 words" (at most
 * four vertex shaders named, then "and N more") */
static void batchModules(const RdPipeKeyInt *keys, uint32_t n, char *out, size_t size)
{
    char names[4][64];
    uint32_t named = 0, more = 0, fsWords = 0;
    char fsBig[64] = "";
    size_t at = 0;
    out[0] = '\0';
    for (uint32_t i = 0; i < n; i++) {
        char v[64], f[64];
        keyVsName(&keys[i], v, sizeof(v));
        keyFsName(&keys[i], f, sizeof(f));
        const uint32_t fw = rd__shader_words(f);
        if (fw > fsWords) {
            fsWords = fw;
            memcpy(fsBig, f, sizeof(fsBig));
        }
        bool seen = false;
        for (uint32_t j = 0; j < named && !seen; j++) {
            seen = strcmp(names[j], v) == 0;
        }
        if (seen) {
            continue;
        }
        if (named < 4) {
            memcpy(names[named++], v, sizeof(v));
        } else {
            /* counted once each: the keys come grouped by vertex shader */
            if (i == 0 || keys[i].vs != keys[i - 1].vs) {
                more++;
            }
        }
    }
    for (uint32_t j = 0; j < named && at < size; j++) {
        const int w =
            snprintf(out + at, size - at, "%s%s %u words (%u instructions)", j ? ", " : "vertex ",
                     names[j], rd__shader_words(names[j]), rd__shader_instructions(names[j]));
        at = w > 0 ? at + (size_t)w : at;
    }
    if (more && at < size) {
        const int w = snprintf(out + at, size - at, " and %u more", more);
        at = w > 0 ? at + (size_t)w : at;
    }
    if (at < size) {
        snprintf(out + at, size - at, "; largest fragment %s %u words", fsBig[0] ? fsBig : "none",
                 fsWords);
    }
}

/* the note before a creation: the key's words */
static void activityCreating(const RdPipeKeyInt *k)
{
    char note[320];
    char words[96], mods[160];
    if (s_activityFn == NULL) {
        return;
    }
    keyWords(k, words, sizeof(words));
    keyModules(k, mods, sizeof(mods));
    snprintf(note, sizeof(note), "creating pipeline %s (%s)", words, mods);
    s_activityFn(note);
}

/* the note after a creation: back to the batch's, or none */
static void activityDone(void)
{
    if (s_activityFn == NULL) {
        return;
    }
    s_activityFn(s_batchNote[0] != '\0' ? s_batchNote : NULL);
}

void rd__pipeline_note_begin(const char *what)
{
    char note[160];
    if (s_activityFn == NULL) {
        return;
    }
    snprintf(note, sizeof(note), "creating pipeline %s", what != NULL ? what : "");
    s_activityFn(note);
}

void rd__pipeline_note_end(void)
{
    activityDone();
}

/* The key a draw is created with: the MSAA epoch (a pipeline fetched for a
 * multisampled pass draws with its sample count; single-sampled passes keep
 * msaa 0: the same keys as without anti-aliasing) and, without dual-source
 * blending, a gs_dual_out entry as its *_nodual twin (the planned passes
 * come through rd__expand_no_dual, the single keys, the shadow count's
 * volumes, are turned here).  k itself, or tmp holding the turned key. */
static const RdPipeKeyInt *drawKeyTurned(const RdPipeKeyInt *k, RdPipeKeyInt *tmp)
{
    if (g_rd.passSamples > 1 && k->gs.msaa != g_rd.passSamples) {
        *tmp = *k;
        tmp->gs.msaa = g_rd.passSamples;
        k = tmp;
    }
    if (g_rd.noDual && !k->gs.nodual && rd__fs_has_no_dual(k->fs) && k->dst == RHI_DST_READ_NONE) {
        const RdBlendPath bp = keyBlendPath(k);
        if (bp == RD_BP_LERP || bp == RD_BP_DST_FIX) {
            rd__log_once(RD_ONCE_NODUAL_KEY,
                         "blend: a blended key (program %u blend %u fs %u) was not split into "
                         "the two-pass fallback; its alpha is the factor",
                         k->gs.program, k->gs.blend, k->fs);
        }
        if (k != tmp) {
            *tmp = *k;
        }
        tmp->gs.nodual = 1;
        k = tmp;
    }
    return k;
}

uint32_t rd__key_dynamic_bits(const RdPipeKeyInt *k)
{
    if (rd__vs_is_vu_pre(k->vs) || rd__vs_is_vu_stage(k->vs)) {
        return 0; /* three targets, no depth: every state static */
    }
    uint32_t bits = 0;
    if (g_rd.dynDepth && k->depthFmt != RHI_FMT_UNKNOWN &&
        (k->gs.stencil == RD_STENCIL_OFF || k->gs.stencil == RD_STENCIL_DATE_TEST0 ||
         k->gs.stencil == RD_STENCIL_DATE_TEST1)) {
        /* the shadow count's stencil keys and DATE's two setup keys stay
         * static; a DATE draw's own key (the stencil test of the bit, the
         * draw's depth state) takes the dynamic depth like any other draw */
        bits |= RHI_DYN_DEPTH;
    }
    if (g_rd.dynBlend && k->colorFmt != RHI_FMT_UNKNOWN && k->dst == RHI_DST_READ_NONE) {
        /* one colour target and no destination read (a storage pass has no
         * colour target; the attachment read blends in its shader, blending
         * off in its key already, and keeps its rasterization-order
         * pipeline static) */
        bits |= RHI_DYN_BLEND;
    }
    return bits;
}

void rd__key_dynamic_normalise(RdPipeKeyInt *k)
{
    const uint32_t bits = rd__key_dynamic_bits(k);
    if (bits & RHI_DYN_DEPTH) {
        k->gs.ztst = RD_ZTST_ALWAYS;
        k->gs.zwrite = RD_ZWRITE_OFF;
    }
    if (bits & RHI_DYN_BLEND) {
        k->gs.blend = RD_BLEND_COUNT;
        k->gs.colorMask = 0xF;
    }
}

/* drawKeyTurned's key with the dynamic state's fields normalised away
 * (rd__key_dynamic_normalise): the key the cache holds.  k itself, or tmp. */
static const RdPipeKeyInt *drawKey(const RdPipeKeyInt *k, RdPipeKeyInt *tmp)
{
    k = drawKeyTurned(k, tmp);
    if (rd__key_dynamic_bits(k) != 0u) {
        if (k != tmp) {
            *tmp = *k;
        }
        rd__key_dynamic_normalise(tmp);
        k = tmp;
    }
    return k;
}

/* k's slot in s_hash: its entry's (*found) or the empty slot the probe
 * ended on */
static uint32_t probeKey(const RdPipeKeyInt *k, bool *found)
{
    uint32_t at = keyHash(k) % RD_PIPE_HASH;
    for (; s_hash[at] != 0; at = (at + 1) % RD_PIPE_HASH) {
        if (keyEqual(&s_cache[s_hash[at] - 1].key, k)) {
            *found = true;
            return at;
        }
    }
    *found = false;
    return at;
}

/* k is a failed entry (O(1): the hash look-up) */
static bool keyFailed(const RdPipeKeyInt *k)
{
    bool found;
    const uint32_t at = probeKey(k, &found);
    return found && s_cache[s_hash[at] - 1].pipe.id == 0;
}

/* no assert: NDEBUG is never defined (CMakeLists.txt), so an assert here
 * would abort a release build; the tests hold the reachable set under
 * RD_PIPELINE_REACHABLE_MAX instead */
static bool cacheFull(const RdPipeKeyInt *k)
{
    if (s_used < RD_PIPELINE_CACHE_MAX) {
        return false;
    }
    if (!s_fullLogged) {
        s_fullLogged = true;
        rd__log("pipeline cache full (%d keys): new keys are not drawn (program %u blend %u "
                "vs %u fs %u)",
                RD_PIPELINE_CACHE_MAX, k->gs.program, k->gs.blend, k->vs, k->fs);
    }
    return true;
}

/* The RHI's pipeline counts around a creation (RhiStats
 * pipelinePoolFull, pipelineFailures, pipelineLastError); zero without a
 * device */
static void rhiPipeCounts(RhiStats *st)
{
    memset(st, 0, sizeof(*st));
    if (g_rd.hasDevice) {
        rhi_get_stats(st);
    }
}

/* the name of a VkResult a pipeline's creation can return, " (NAME)", or
 * "" (the renderer has no Vulkan header: the values from vulkan_core.h) */
static const char *vkResultName(int64_t r)
{
    switch (r) {
    case -1:
        return " (VK_ERROR_OUT_OF_HOST_MEMORY)";
    case -2:
        return " (VK_ERROR_OUT_OF_DEVICE_MEMORY)";
    case -3:
        return " (VK_ERROR_INITIALIZATION_FAILED)";
    case -4:
        return " (VK_ERROR_DEVICE_LOST)";
    case -13:
        return " (VK_ERROR_UNKNOWN)";
    case -1000012000:
        return " (VK_ERROR_INVALID_SHADER_NV)";
    case 1000297000:
        return " (VK_PIPELINE_COMPILE_REQUIRED)";
    default:
        return "";
    }
}

/* why the creations between a and b failed: the pool full, the driver's
 * error, a description the RHI refused, or none of the RHI's (the
 * renderer's description was not made: a module not loaded) */
static void failReason(const RhiStats *a, const RhiStats *b, char *out, size_t size)
{
    const uint64_t full = b->pipelinePoolFull - a->pipelinePoolFull;
    const uint64_t other = b->pipelineFailures - a->pipelineFailures;
    char drv[96];
    if (b->pipelineLastError != 0 && rhi_backend() == RHI_BACKEND_D3D12) {
        snprintf(drv, sizeof(drv), "the driver's error HRESULT 0x%08X",
                 (unsigned)(uint32_t)b->pipelineLastError);
    } else if (b->pipelineLastError != 0) {
        snprintf(drv, sizeof(drv), "the driver's error VkResult %lld%s",
                 (long long)b->pipelineLastError, vkResultName(b->pipelineLastError));
    } else {
        snprintf(drv, sizeof(drv), "the RHI refused its description (its line above)");
    }
    if (full != 0 && other == 0) {
        snprintf(out, size, "the RHI's pipeline pool is full (%llu of %llu alive)",
                 (unsigned long long)b->pipelinesLive, (unsigned long long)b->pipelinePoolSize);
    } else if (full == 0 && other != 0) {
        snprintf(out, size, "%s", drv);
    } else if (full != 0) {
        snprintf(out, size, "the batch met a full pipeline pool (%llu times) and %s",
                 (unsigned long long)full, drv);
    } else {
        snprintf(out, size, "its description was not made (a shader module not loaded)");
    }
}

/* where a creation ran: the replay's list, the vertex pre-pass, the
 * present or a check */
static void useWhere(char *out, size_t size)
{
    const int list = rd__replay_list();
    if (list >= 0) {
        snprintf(out, size, "list %d", list);
    } else if (rd__vu_prepass_running()) {
        snprintf(out, size, "the vertex pre-pass (before list 0)");
    } else {
        snprintf(out, size, "no draw list (the present, a check)");
    }
}

/* a creation's result into the cache (at: the empty slot probeKey gave),
 * or into the failed keys: the line names the key, when it was asked for
 * (when: "in the start-up set", "on first use in list 3") and why it
 * failed (failReason), the first 8 then every 100th */
static void keepPipeline(const RdPipeKeyInt *k, RhiPipeline p, uint32_t at, const char *when,
                         const char *why)
{
    if (!p.id) {
        s_failLines++;
        if (s_failLines <= 8u || s_failLines % 100u == 0u) {
            rd__log("pipeline creation failed %s (program %u blend %u vs %u fs %u fmt %u/%u): %s; "
                    "draws with this key are skipped (%u failed so far%s)",
                    when, k->gs.program, k->gs.blend, k->vs, k->fs, k->colorFmt, k->depthFmt, why,
                    s_failLines, s_failLines == 8u ? "; from here every 100th is written" : "");
        }
        /* a failed entry where there is room (its own probe: a caller's
           slot may be stale, or the key made meanwhile) */
        bool found;
        const uint32_t fat = probeKey(k, &found);
        if (s_failedCount < RD_PIPELINE_FAIL_MAX && s_used < RD_PIPELINE_CACHE_MAX && !found) {
            s_cache[s_used].key = *k;
            s_cache[s_used].pipe = (RhiPipeline){0};
            s_used++;
            s_hash[fat] = (uint16_t)s_used;
            s_failedCount++;
        } else if (!found && !s_createStopped) {
            /* no room to remember it: no creation at all from here, so
               this key is not retried on its every draw either */
            s_createStopped = true;
            rd__log("pipelines: %u keys failed, more than the %d remembered: no pipeline is "
                    "created until the cache is cleared (new keys are not drawn)",
                    s_failLines, RD_PIPELINE_FAIL_MAX);
        }
        return;
    }
    s_cache[s_used].key = *k;
    s_cache[s_used].pipe = p;
    s_used++;
    s_count++;
    s_hash[at] = (uint16_t)s_used;
    g_rd.stats.pipelineCreates++;
    rd__passring_pipeline(p.id, k->vs, k->fs, k->dst);
}

/* rd__pipeline_gather: the keys a replay asked for and found no pipeline
 * for, as drawKey made them, each once */
static bool s_gatherOn;
static RdPipeKeyInt s_gather[RD_PIPELINE_CACHE_MAX];
static uint32_t s_gatherN;

/* in rd__get_pipeline while gathering: k (drawKey's) noted, no pipeline */
static void gatherKey(const RdPipeKeyInt *k)
{
    if (s_createStopped || keyFailed(k) || cacheFull(k)) {
        return;
    }
    for (uint32_t i = 0; i < s_gatherN; i++) {
        if (keyEqual(&s_gather[i], k)) {
            return;
        }
    }
    if (s_gatherN < RD_PIPELINE_CACHE_MAX) {
        s_gather[s_gatherN++] = *k;
    }
}

/* rd_pipelines_lazy (issues 93 and 107): no start-up set; the pipelines
 * a frame first needs are made at once, one at a time, and counted into a
 * batch that rd_pipelines_lazy_tick closes once the creations pause
 * (RD_LAZY_QUIET_MS without one) or after RD_LAZY_BATCH_MS: the pipeline
 * cache is written then (so a start the system ends, or a device that
 * freezes, keeps what was compiled before it) and the batch's line goes to
 * the log and the activity file as a start-up batch's does */
#define RD_LAZY_QUIET_MS 500.0
#define RD_LAZY_BATCH_MS 2000.0

static struct {
    bool on;
    uint32_t n;       /* the open batch's pipelines (0: none open) */
    uint32_t made;    /* made on first use this run, the open batch's too */
    uint32_t batches; /* closed */
    float lo, hi;
    double sum;
    float slowMs;
    RdPipeKeyInt slow;
    double t0, tLast; /* the open batch's first and last creation */
} s_lazy;

bool rd__pipelines_lazy_on(void)
{
    return s_lazy.on;
}

bool rd_pipelines_lazy_on(void)
{
    return s_lazy.on;
}

static void lazyCount(const RdPipeKeyInt *k, float ms)
{
    const double now = rd__now_ms();
    if (s_lazy.n == 0) {
        s_lazy.t0 = now - ms;
        s_lazy.lo = s_lazy.hi = ms;
        s_lazy.sum = 0.0;
        s_lazy.slowMs = -1.0f;
    }
    s_lazy.lo = ms < s_lazy.lo ? ms : s_lazy.lo;
    s_lazy.hi = ms > s_lazy.hi ? ms : s_lazy.hi;
    s_lazy.sum += ms;
    if (ms > s_lazy.slowMs) {
        s_lazy.slowMs = ms;
        s_lazy.slow = *k;
    }
    s_lazy.n++;
    s_lazy.made++;
    s_lazy.tLast = now;
}

/* the start-up set ran (rd_precreate_pipelines): a pipeline made at first
 * use afterwards is a key it did not hold */
static bool s_startupSet;
/* pipelines made at first use (rd__get_pipeline): their time in all, ms
 * (the replay's walk breakdown, rd__pipeline_create_ms), and their count
 * after the start-up set (the line's limiter) */
static double s_createMs;
static uint32_t s_firstUse;

double rd__pipeline_create_ms(void)
{
    return s_createMs;
}

/* A pipeline the start-up set did not hold, made when a draw first needed
 * it: the key, its modules, the list the replay was in and the time, the
 * first 8 then every 100th (the host's rate limiter's rule; the renderer
 * does not link it).  On a driver that takes seconds a pipeline (up to
 * 3.4 s for a VU one on a PowerVR DXT-48-1536) this is the replay's
 * stall. */
static void firstUseLine(const RdPipeKeyInt *k, float ms)
{
    char key[256], vs[48], fs[48], where[40];

    s_firstUse++;
    if (s_firstUse > 8u && s_firstUse % 100u != 0u) {
        return;
    }
    /* the key's words and every other field of it, so the line names the
       state the start-up set lacks (rd__enumerate_reachable) */
    keyWords(k, key, sizeof(key) - 96u);
    const size_t at = strlen(key);
    snprintf(key + at, sizeof(key) - at,
             ", ztst %u zwrite %u mask %u target %u prim %u stencil %u aa1 %u nodual %u",
             k->gs.ztst, k->gs.zwrite, k->gs.colorMask, k->gs.targetFmt, k->gs.prim, k->gs.stencil,
             k->gs.aa1, k->gs.nodual);
    keyVsName(k, vs, sizeof(vs));
    keyFsName(k, fs, sizeof(fs));
    useWhere(where, sizeof(where));
    rd__log("pipelines: made on first use in %s: %s (%s + %s) in %.1f ms (not in the start-up "
            "set; %u so far%s)",
            where, key, vs, fs, (double)ms, s_firstUse,
            s_firstUse == 8u ? "; from here every 100th is written" : "");
}

/* The bind and its dynamic state (rd_internal.h): the depth test, write
 * and compare and the blend of k as drawKeyTurned makes it (the two-pass
 * blend's factors), through the functions pipelineDesc makes the static
 * state with, so each draw gets the fixed-function state a pipeline of
 * its own un-normalised key would have had: the same tuple, set
 * dynamically (the RHI skips the values already in force) */
void rd__set_pipeline(RhiCommandList cl, RhiPipeline p, const RdPipeKeyInt *k)
{
    rhi_cmd_set_pipeline(cl, p);
    if (k == NULL || !p.id || (!g_rd.dynDepth && !g_rd.dynBlend)) {
        return;
    }
    RdPipeKeyInt tmp;
    const RdPipeKeyInt *dk = drawKeyTurned(k, &tmp);
    const uint32_t bits = rd__key_dynamic_bits(dk);
    if (bits & RHI_DYN_DEPTH) {
        rhi_cmd_set_depth_state(cl, true, dk->gs.zwrite == RD_ZWRITE_ON, depthCompare(dk->gs.ztst));
    }
    if (bits & RHI_DYN_BLEND) {
        const RhiBlendState b = blendState(keyBlendPath(dk), dk->gs.colorMask, dk->gs.nodual);
        rhi_cmd_set_blend_state(cl, &b);
    }
}

RhiPipeline rd__get_pipeline(const RdPipeKeyInt *k)
{
    RdPipeKeyInt tmp;
    bool found;
    k = drawKey(k, &tmp);
    const uint32_t at = probeKey(k, &found);
    if (found) {
        /* a failed entry: drawn as nothing, not created again */
        const RhiPipeline p = s_cache[s_hash[at] - 1].pipe;
        if (p.id) {
            rd__passring_pipeline(p.id, k->vs, k->fs, k->dst);
        }
        return p;
    }
    if (s_gatherOn) {
        gatherKey(k); /* created afterwards in batches (rd__pipeline_gather) */
        return (RhiPipeline){0};
    }
    if (!g_rd.hasDevice || s_createStopped || cacheFull(k)) {
        return (RhiPipeline){0};
    }
    activityCreating(k);
    RhiStats st0, st1;
    rhiPipeCounts(&st0);
    const double t0 = rd__now_ms();
    RhiPipeline p = createPipeline(k);
    const float ms = (float)(rd__now_ms() - t0);
    s_createMs += ms;
    if (p.id && s_lazy.on) {
        lazyCount(k, ms);
    } else if (p.id && s_startupSet) {
        firstUseLine(k, ms);
    }
    activityDone();
    char when[64] = "", why[224] = "";
    if (!p.id) {
        char where[40];
        useWhere(where, sizeof(where));
        snprintf(when, sizeof(when), "on first use in %s", where);
        rhiPipeCounts(&st1);
        failReason(&st0, &st1, why, sizeof(why));
    }
    keepPipeline(k, p, at, when, why);
    return p;
}

void rd__pipeline_cache_clear(void)
{
    if (g_rd.hasDevice) {
        for (uint32_t i = 0; i < s_used; i++) {
            if (s_cache[i].pipe.id) {
                rhi_destroy_pipeline(s_cache[i].pipe);
            }
        }
    }
    s_count = 0;
    s_used = 0;
    s_failedCount = 0;
    s_failLines = 0;
    s_createStopped = false;
    s_fullLogged = false;
    memset(s_hash, 0, sizeof(s_hash));
}

static RdPipelineProgressFn s_progressFn;
static void *s_progressCtx;

void rd_set_pipeline_progress(RdPipelineProgressFn fn, void *ctx)
{
    s_progressFn = fn;
    s_progressCtx = fn != NULL ? ctx : NULL;
}

/* the longest compile of the start-up set so far (rd_precreate_longest_ms) */
static float s_preLongestMs;

float rd_precreate_longest_ms(void)
{
    return s_preLongestMs;
}

/* The start-up set goes to rhi_create_pipelines RD_PIPE_BATCH keys at a
 * time: the progress function runs while a batch compiles (the RHI's wait
 * function) and between two, and a crash note names the batch's first and
 * last key.  A batch holds keys with a destination read or keys without,
 * never both, so the note's dst field is the whole batch's
 * (startup_retry.h startup_retry_note_has_dst: the fixed-blend step) */
#define RD_PIPE_BATCH 32u

/* The pool's headroom (rhi_pipeline_pool.h) holds what is alive outside
 * the cache at once: one batch's descriptions (its ids held until it
 * ends, its duplicates destroyed then), the COLCLAMP 0 pipelines and the
 * movie's three (rd_video.c: two outputs, the field pipeline) */
_Static_assert(RD_PIPE_BATCH + RD_WRAP_PIPES + 3u <= (unsigned)RHI_PIPELINE_POOL_HEADROOM,
               "the RHI's pipeline pool headroom holds a batch, the wrap and the movie pipelines");
_Static_assert(RD_PIPELINE_CACHE_MAX + RHI_PIPELINE_POOL_HEADROOM == RHI_PIPELINE_POOL_MAX,
               "the RHI's pipeline pool holds the cache and the headroom");

typedef struct PreBatch {
    uint32_t base;  /* the set's keys done before this batch */
    uint32_t total; /* the set's size */
    uint32_t last;  /* the last count the progress function was given */
    int stop;       /* the progress function asked to stop */
} PreBatch;

static int preProgress(PreBatch *b, uint32_t done)
{
    if (s_progressFn == NULL) {
        return 0;
    }
    /* never back, and the set's end only at the end */
    if (done < b->last) {
        done = b->last;
    }
    if (done >= b->total && b->total > 0) {
        done = b->total - 1;
    }
    b->last = done;
    /* the frame it may present asks for its own few pipelines: created at
     * once (each with its own note, the batch's put back after it) and
     * found again when the batch's results are kept */
    return s_progressFn(s_progressCtx, done, b->total);
}

/* the note between two batches: "creating the start-up pipelines (i + 1
 * of m)" (diag_host.c leaves it out of the start-up activity file) */
static void noteBetween(uint32_t i, uint32_t m)
{
    char note[64];
    if (s_activityFn != NULL) {
        snprintf(note, sizeof(note), "creating the start-up pipelines (%u of %u)", i + 1, m);
        s_activityFn(note);
    }
}

/* rhi_create_pipelines' wait function */
static void preWait(void *ctx, uint32_t finished)
{
    PreBatch *b = ctx;
    if (!b->stop) {
        b->stop = preProgress(b, b->base + finished);
    }
}

/* The start-up set's slowest compiles and its compile time by vertex
 * shader, from the workers' own times (RhiPipelineBatch.ms), for the log
 * line that says where a phone's first start goes */
#define PRE_SLOWEST 10

typedef struct PreTimes {
    float ms[PRE_SLOWEST];
    RdPipeKeyInt key[PRE_SLOWEST];
    uint32_t n;
    double byVs[RD_VS_COUNT]; /* by vertex shader */
} PreTimes;

static void preTime(PreTimes *t, const RdPipeKeyInt *k, float ms)
{
    if (k->vs < RD_VS_COUNT) {
        t->byVs[k->vs] += ms;
    }
    uint32_t at = t->n < PRE_SLOWEST ? t->n : PRE_SLOWEST - 1;
    if (t->n == PRE_SLOWEST && ms <= t->ms[at]) {
        return;
    }
    /* in order, the slowest first */
    while (at > 0 && t->ms[at - 1] < ms) {
        t->ms[at] = t->ms[at - 1];
        t->key[at] = t->key[at - 1];
        at--;
    }
    t->ms[at] = ms;
    t->key[at] = *k;
    t->n += t->n < PRE_SLOWEST;
}

static void preTimesLog(const PreTimes *t)
{
    /* each with its modules and their sizes (issue 107: a phone's driver
     * takes a time about proportional to the vertex shader's size) */
    static char line[2600];
    size_t at = 0;
    line[0] = '\0';
    for (uint32_t i = 0; i < t->n && at < sizeof(line); i++) {
        char w[96], mods[160];
        keyWords(&t->key[i], w, sizeof(w));
        keyModules(&t->key[i], mods, sizeof(mods));
        const int k = snprintf(line + at, sizeof(line) - at, "%s%.2f s %s (%s)", i ? "; " : "",
                               (double)t->ms[i] / 1000.0, w, mods);
        at = k > 0 ? at + (size_t)k : at;
    }
    rd__log("pipelines: the slowest: %s", t->n ? line : "none");
    bool done[RD_VS_COUNT] = {false};
    at = 0;
    line[0] = '\0';
    for (;;) {
        int best = -1;
        for (int v = 0; v < RD_VS_COUNT; v++) {
            if (!done[v] && t->byVs[v] > 0.0 && (best < 0 || t->byVs[v] > t->byVs[best])) {
                best = v;
            }
        }
        if (best < 0 || at >= sizeof(line)) {
            break;
        }
        done[best] = true;
        RdPipeKeyInt k;
        memset(&k, 0, sizeof(k));
        k.vs = (uint8_t)best;
        char name[64];
        keyVsName(&k, name, sizeof(name));
        const int n =
            snprintf(line + at, sizeof(line) - at, "%s%s %.2f s (%u words)", at ? ", " : "", name,
                     t->byVs[best] / 1000.0, rd__shader_words(name));
        at = n > 0 ? at + (size_t)n : at;
    }
    rd__log("pipelines: compile time by vertex shader: %s", at ? line : "none");
}

/* a policy other than the defaults is in force (rd_set_pipeline_policy) */
static bool policySet(void)
{
    return s_setPolicy.batch || s_setPolicy.maxThreads || s_setPolicy.memoryBudgetMB ||
           s_setPolicy.cacheEachBatch;
}

/* "batch 8, threads 1, budget 128 MB, cache each batch": the policy's
 * values (0 threads: the backend's; 0 MB: no budget) */
static void policyText(char *out, size_t size)
{
    char th[32], mb[32];
    if (s_setPolicy.maxThreads) {
        snprintf(th, sizeof(th), "threads %u", s_setPolicy.maxThreads);
    } else {
        snprintf(th, sizeof(th), "threads the backend's");
    }
    if (s_setPolicy.memoryBudgetMB) {
        snprintf(mb, sizeof(mb), "budget %u MB", s_setPolicy.memoryBudgetMB);
    } else {
        snprintf(mb, sizeof(mb), "no budget");
    }
    snprintf(out, size, "batch %u, %s, %s, cache %s",
             s_setPolicy.batch && s_setPolicy.batch < RD_PIPE_BATCH ? s_setPolicy.batch
                                                                    : RD_PIPE_BATCH,
             th, mb, s_setPolicy.cacheEachBatch ? "each batch" : "as usual");
}

/* A batch done: its compile times, each pipeline's as the worker measured
 * it (RhiPipelineBatch.ms), as one line for the log (with every time, in
 * the batch's order) and one for the activity file (rd_set_batch_fn: min,
 * max and mean, the process's memory and the pipeline cache's size after
 * it, then a VU batch's times by vertex shader with the shader's words and
 * instructions, a 2D batch's slowest pipeline with its modules), so a
 * device that froze or a start the system ended shows the sizes, times and
 * memory of every batch it finished (issue 107 for the VU batches; issue
 * 93 and 107's v0.7.7 logs for the 2D ones, both ending inside the 2D
 * half of the set) */
static void batchTimes(const RdPipeKeyInt *keys, const float *ms, uint32_t n, uint32_t first,
                       uint32_t last, uint32_t total, double wallMs, uint32_t threads)
{
    char line[600], mem[96];
    size_t at = 0;
    float lo = 0.0f, hi = 0.0f;
    double sum = 0.0;
    bool vu = false;
    uint32_t slow = 0;
    for (uint32_t j = 0; j < n; j++) {
        vu = vu || keyIsVu(&keys[j]);
        lo = j == 0 || ms[j] < lo ? ms[j] : lo;
        hi = j == 0 || ms[j] > hi ? ms[j] : hi;
        slow = ms[j] > ms[slow] ? j : slow;
        sum += ms[j];
    }
    if (n == 0) {
        return;
    }
    memoryText(mem, sizeof(mem));
#define BT_PUT(...)                                                                                \
    do {                                                                                           \
        const int k_ =                                                                             \
            at < sizeof(line) ? snprintf(line + at, sizeof(line) - at, __VA_ARGS__) : 0;           \
        at = k_ > 0 ? (at + (size_t)k_ < sizeof(line) ? at + (size_t)k_ : sizeof(line) - 1) : at;  \
    } while (0)
    if (vu) {
        BT_PUT("VU batch %u-%u of %u (compiler %s)", first, last, total,
               s_compiler[0] ? s_compiler : "unnamed by the driver");
    } else {
        BT_PUT("2D batch %u-%u of %u (compiler %s)", first, last, total,
               s_compiler[0] ? s_compiler : "unnamed by the driver");
    }
    BT_PUT(": %u pipelines %.2f-%.2f s, mean %.2f s, %.1f s on %u threads; %s", n, lo / 1000.0,
           hi / 1000.0, sum / n / 1000.0, wallMs / 1000.0, threads, mem);
    if (policySet()) {
        char pol[160];
        policyText(pol, sizeof(pol));
        BT_PUT("; policy %s", pol);
    }
    if (!vu) {
        char w[96], mods[160];
        keyWords(&keys[slow], w, sizeof(w));
        keyModules(&keys[slow], mods, sizeof(mods));
        BT_PUT("; slowest %.2f s %s (%s)", ms[slow] / 1000.0, w, mods);
    }
    /* by vertex shader, in the batch's order (the keys come grouped) */
    for (uint32_t j = 0; vu && j < n;) {
        uint32_t e = j + 1;
        while (e < n && keys[e].vs == keys[j].vs) {
            e++;
        }
        float glo = ms[j], ghi = ms[j];
        double gsum = 0.0;
        for (uint32_t k = j; k < e; k++) {
            glo = ms[k] < glo ? ms[k] : glo;
            ghi = ms[k] > ghi ? ms[k] : ghi;
            gsum += ms[k];
        }
        char v[64];
        keyVsName(&keys[j], v, sizeof(v));
        BT_PUT("; %s %u words %u instructions: %u x %.2f-%.2f s mean %.2f s", v,
               rd__shader_words(v), rd__shader_instructions(v), e - j, glo / 1000.0, ghi / 1000.0,
               gsum / (e - j) / 1000.0);
        j = e;
    }
    if (s_batchFn != NULL) {
        s_batchFn(line);
    }
    /* the log: the same and each pipeline's time */
    BT_PUT("; each (s):");
    for (uint32_t j = 0; j < n; j++) {
        BT_PUT(" %.2f", ms[j] / 1000.0);
    }
#undef BT_PUT
    rd__log("pipelines: compiled %s", line);
}

/* The start-up set's pipeline cache writes between its batches (issues 93
 * and 107), on a phone or a tablet only (cacheKeptPerBatch): after a
 * batch that ends RD_SET_SAVE_MS or more after the last write (or the
 * set's start), so a set the system ends or a device that freezes keeps
 * what it compiled and the next start compiles only the rest; a desktop's
 * set writes only at its end (the window's write) */
#define RD_SET_SAVE_MS 5000.0
static double s_setSavedMs;
/* under a policy with a memory budget, a batch longer than this stops the
 * set too (createKeys) */
#define RD_SET_SLOW_BATCH_MS 10000.0

/* the counts of one createKeys */
typedef struct PreCounts {
    uint32_t failed, compiled, threads;
    uint32_t stoppedAt; /* the keys gone over when the progress function stopped it */
    int stop;
    /* why the policy's budget stopped the set ("" if it did not): the rest
     * is made on first use (rd_pipelines_lazy) */
    char budget[192];
} PreCounts;

/* the process's resident memory in KB (rd_set_memory_fn), -1 unknown */
static long residentKb(void)
{
    long rss = -1, peak = -1;
    if (s_memoryFn == NULL || s_memoryFn(&rss, &peak) != 0) {
        return -1;
    }
    return rss;
}

/* Creates keys[0..m) not made yet through rhi_create_pipelines, in batches
 * of up to RD_PIPE_BATCH (all with a destination read or all without), the
 * progress function running while a batch compiles and between two.
 * resolved: the keys are as rd__get_pipeline's drawKey made them (a
 * replay's, rd__pipeline_gather), not adjusted again. */
static void createKeys(const RdPipeKeyInt *keys, uint32_t m, bool resolved, PreTimes *times,
                       PreCounts *c)
{
    /* the policy (rd_set_pipeline_policy): the batch's size, its threads,
     * the memory bound of the start-up set (times: not a replay's keys) */
    const uint32_t batchMax = s_setPolicy.batch > 0u && s_setPolicy.batch < RD_PIPE_BATCH
                                  ? s_setPolicy.batch
                                  : RD_PIPE_BATCH;
    const bool budgeted = times != NULL && s_setPolicy.memoryBudgetMB > 0u;
    const long startKb = budgeted ? residentKb() : -1;
    static RdPipeKeyInt bk[RD_PIPE_BATCH];
    static RhiPipelineDesc bd[RD_PIPE_BATCH];
    static RhiBindGroupLayout bl[RD_PIPE_BATCH][4];
    static RhiPipeline bp[RD_PIPE_BATCH];
    static float bms[RD_PIPE_BATCH];
    /* a failed key's line: when it was asked for */
    const char *when = times != NULL ? "in the start-up set" : "in a replay's gathered keys";
    PreBatch pb = {0, m, 0, 0};
    memset(c, 0, sizeof(*c));
    c->threads = 1;
    if (s_progressFn != NULL) {
        s_progressFn(s_progressCtx, 0, m);
    }
    uint32_t i = 0;
    while (i < m && !pb.stop) {
        /* the batch: the next keys not made yet, up to RD_PIPE_BATCH, all
         * with a destination read or all without */
        const uint32_t first = i;
        uint32_t nb = 0;
        int dstKind = -1;
        noteBetween(i, m);
        for (; i < m && nb < batchMax; i++) {
            RdPipeKeyInt tmp;
            bool found, dup = false;
            const RdPipeKeyInt *k = resolved ? &keys[i] : drawKey(&keys[i], &tmp);
            /* a batch is all with a destination read or all without (the
             * note's first key says which for the whole batch:
             * startup_retry.h) */
            const int kind = k->dst != RHI_DST_READ_NONE;
            if (dstKind >= 0 && kind != dstKind) {
                break;
            }
            const uint32_t at = probeKey(k, &found);
            if (found) {
                /* made, or a failed entry (not created again) */
                c->failed += s_cache[s_hash[at] - 1].pipe.id == 0;
                continue;
            }
            if (s_createStopped) {
                c->failed++;
                continue;
            }
            for (uint32_t j = 0; j < nb && !dup; j++) {
                dup = keyEqual(&bk[j], k);
            }
            if (dup) {
                continue;
            }
            if (cacheFull(k) || s_used + nb >= RD_PIPELINE_CACHE_MAX) {
                c->failed++;
                continue;
            }
            if (!pipelineDesc(k, &bd[nb], bl[nb])) {
                keepPipeline(k, (RhiPipeline){0}, 0, when,
                             "its description was not made (a shader module not loaded)");
                c->failed++;
                continue;
            }
            bk[nb] = *k;
            dstKind = kind;
            nb++;
        }
        if (nb > 0) {
            char note[sizeof(s_batchNote)];
            char a[96], z[96], mods[320];
            keyWords(&bk[0], a, sizeof(a));
            keyWords(&bk[nb - 1], z, sizeof(z));
            batchModules(bk, nb, mods, sizeof(mods));
            /* a batch with a VU vertex shader, the modules a phone's driver
             * is slowest on (issue 107), named in the log as it starts: a
             * log that stops here says what the driver was compiling (the
             * heartbeat repeats the note every 2 s, the activity file
             * keeps it for the next start), with the device's compiler */
            bool vuBatch = false;
            for (uint32_t j = 0; j < nb && !vuBatch; j++) {
                vuBatch = keyIsVu(&bk[j]);
            }
            char comp[96] = "";
            if (vuBatch) {
                snprintf(comp, sizeof(comp), "; compiler %s",
                         s_compiler[0] ? s_compiler : "unnamed by the driver");
            }
            {
                /* a note past its 640 bytes is cut (the compiler line last);
                   the result is read so gcc 14's -Wformat-truncation at -O2
                   sees the cut is meant */
                int n = nb == 1
                            ? snprintf(note, sizeof(note), "creating pipelines %u of %u: %s; %s%s",
                                       first + 1, m, a, mods, comp)
                            : snprintf(note, sizeof(note),
                                       "creating pipelines %u-%u of %u: %s to %s; %s%s", first + 1,
                                       i, m, a, z, mods, comp);
                (void)n;
            }
            if (vuBatch) {
                rd__log("pipelines: compiling %u-%u of %u (VU): %s", first + 1, i, m, mods);
            }
            memcpy(s_batchNote, note, sizeof(s_batchNote));
            if (s_activityFn != NULL) {
                s_activityFn(s_batchNote);
            }
            RhiPipelineBatch batch = {preWait, &pb, bms, 1, s_setPolicy.maxThreads};
            pb.base = first;
            const long beforeKb = budgeted ? residentKb() : -1;
            RhiStats st0, st1;
            rhiPipeCounts(&st0);
            const double bt0 = rd__now_ms();
            rhi_create_pipelines(bd, nb, bp, &batch);
            const double batchMs = rd__now_ms() - bt0;
            rhiPipeCounts(&st1);
            /* the batch's failures share one reason (failReason says when
               the batch had two) */
            char why[224];
            failReason(&st0, &st1, why, sizeof(why));
            batchTimes(bk, bms, nb, first + 1, i, m, batchMs, batch.threads);
            if (times != NULL && i < m &&
                (s_setPolicy.cacheEachBatch || rd__now_ms() - s_setSavedMs >= RD_SET_SAVE_MS) &&
                cacheKeptPerBatch()) {
                /* the start-up set (times: not a replay's gathered keys) on
                 * a phone or a tablet, or after every batch under a policy
                 * that asks: a desktop's set writes once, at its end (the
                 * window's write) */
                (void)rhi_pipeline_cache_save();
                s_setSavedMs = rd__now_ms();
            }
            char lowMem[48];
            if (times != NULL && i < m && s_lowMemoryFn != NULL &&
                s_lowMemoryFn(lowMem, sizeof(lowMem)) != 0) {
                /* the system reported low memory (issue 128: the Adreno
                 * 740 handheld's system took the process's pages back and
                 * ended it while its set went on): the set stops here,
                 * under any policy, before the budget's own rules (the
                 * growth rule cannot fire while the pages are taken back) */
                snprintf(c->budget, sizeof(c->budget),
                         "(memory budget: the system reported low memory (%s) after %u of %u keys; "
                         "the rest is made as the game needs it)",
                         lowMem, i, m);
            } else if (budgeted && i < m) {
                /* the policy's bound: the process's growth since the set
                 * began (the memory read before and after the batch) or a
                 * batch past RD_SET_SLOW_BATCH_MS stops the set here */
                const long afterKb = residentKb();
                const long grewMb = startKb >= 0 && afterKb >= 0 ? (afterKb - startKb) / 1024 : 0;
                if (grewMb > (long)s_setPolicy.memoryBudgetMB) {
                    snprintf(c->budget, sizeof(c->budget),
                             "(memory budget: the process grew %ld MB, from %ld to %ld MB, past "
                             "the policy's %u MB, after %u of %u keys)",
                             grewMb, startKb / 1024, afterKb / 1024, s_setPolicy.memoryBudgetMB, i,
                             m);
                } else if (batchMs > RD_SET_SLOW_BATCH_MS) {
                    snprintf(c->budget, sizeof(c->budget),
                             "(memory budget: a batch of %u took %.1f s, past %.0f s; memory %ld "
                             "to %ld MB; after %u of %u keys)",
                             nb, batchMs / 1000.0, RD_SET_SLOW_BATCH_MS / 1000.0,
                             beforeKb >= 0 ? beforeKb / 1024 : -1L,
                             afterKb >= 0 ? afterKb / 1024 : -1L, i, m);
                }
            }
            s_batchNote[0] = '\0';
            if (batch.threads > c->threads) {
                c->threads = batch.threads;
            }
            for (uint32_t j = 0; j < nb; j++) {
                bool found;
                const uint32_t at = probeKey(&bk[j], &found);
                if (bms[j] > s_preLongestMs) {
                    s_preLongestMs = bms[j];
                }
                if (times) {
                    preTime(times, &bk[j], bms[j]);
                }
                if (!bp[j].id) {
                    keepPipeline(&bk[j], bp[j], at, when, why);
                    c->failed++;
                    continue;
                }
                c->compiled++;
                /* the progress screen's frame may have made the same key
                   meanwhile, or filled the cache */
                if (found || s_used >= RD_PIPELINE_CACHE_MAX) {
                    rhi_destroy_pipeline(bp[j]);
                    continue;
                }
                keepPipeline(&bk[j], bp[j], at, when, "");
            }
        }
        if (c->budget[0]) {
            break; /* the policy's bound (above): the rest on first use */
        }
        if (!pb.stop && i < m) {
            /* the frame the progress function may present is not the
               batch's */
            noteBetween(i, m);
            pb.stop = preProgress(&pb, i);
        }
    }
    s_batchNote[0] = '\0';
    c->stop = pb.stop;
    c->stoppedAt = i;
    if (!pb.stop && s_progressFn != NULL) {
        s_progressFn(s_progressCtx, m, m);
    }
    if (s_activityFn != NULL) {
        s_activityFn(NULL);
    }
}

/* the pipelines line's words for the dynamic state in force
 * (rd__key_dynamic_normalise) */
static const char *dynamicWords(void)
{
    return g_rd.dynDepth && g_rd.dynBlend ? " (dynamic depth, blend)"
           : g_rd.dynDepth                ? " (dynamic depth)"
           : g_rd.dynBlend                ? " (dynamic blend)"
                                          : "";
}

uint32_t rd_precreate_pipelines(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
    if (!g_rd.hasDevice) {
        /* logged, so a call before rd_init does not pass unnoticed */
        rd__log("rd_precreate_pipelines without a device (before rd_init?): nothing created");
        return 0;
    }
    const double t0 = rd__now_ms();
    const uint32_t before = s_count;
    const uint32_t n = rd__enumerate_reachable(keys, RD_PIPELINE_CACHE_MAX);
    /* n counts the whole set, keys[] holds the first RD_PIPELINE_CACHE_MAX:
     * only those are created, and the progress counts to them */
    const uint32_t m = n < RD_PIPELINE_CACHE_MAX ? n : RD_PIPELINE_CACHE_MAX;
    /* the set against the RHI's pool: the cache holds the pool less its
       headroom (rhi_pipeline_pool.h), so a set inside the cache is inside
       the pool */
    rd__log("pipelines: the start-up set's %u keys (pool %d: the cache's %d and %d for the "
            "pipelines made outside it; %u already made)",
            n, RHI_PIPELINE_POOL_MAX, RD_PIPELINE_CACHE_MAX, RHI_PIPELINE_POOL_HEADROOM, s_count);
    {
        /* the set's families (rd__pipe_key_family): which part of it grew or shrank is
         * in a reporter's log */
        uint32_t fam[RD_KF_COUNT];
        (void)rd__pipe_family_counts(keys, n, m, fam);
        char w[160];
        size_t at = 0;
        w[0] = '\0';
        for (int f = 0; f < RD_KF_COUNT && at < sizeof(w); f++) {
            const int put = snprintf(w + at, sizeof(w) - at, "%s%s %u", f ? ", " : "",
                                     rd__pipe_family_name((RdKeyFamily)f), fam[f]);
            at += put > 0 ? (size_t)put : 0;
        }
        rd__log("pipelines: the start-up set by family: %s", w);
    }
    if (n > m) {
        rd__log("pipelines: the reachable set has %u keys, more than the %u the cache holds: "
                "the first %u are created",
                n, (unsigned)RD_PIPELINE_CACHE_MAX, m);
    }
    static PreTimes times;
    memset(&times, 0, sizeof(times));
    s_preLongestMs = 0.0f;
    s_setSavedMs = t0;
    PreCounts c;
    if (policySet()) {
        char pol[160];
        policyText(pol, sizeof(pol));
        rd__log("pipelines: the start-up set's policy: %s; with a budget the set stops past it or "
                "after a batch over %.0f s, the rest made on first use",
                pol, RD_SET_SLOW_BATCH_MS / 1000.0);
    }
    if (s_lowMemoryFn != NULL) {
        rd__log("pipelines: the start-up set stops at the system's low-memory signal (asked after "
                "each batch), the rest made on first use");
    }
    createKeys(keys, m, false, &times, &c);
    if (c.stop) {
        rd__log("pipelines: the start-up set stopped at %u of %u (the game is closing)",
                c.stoppedAt, m);
    }
    const double ms = rd__now_ms() - t0;
    s_startupSet = true;
    rd__log("pipelines: %u of the reachable set's %u created at start-up%s in %.1f ms (%u "
            "failed); %u compiled on %u thread%s, the longest %.2f s",
            s_count - before, n, dynamicWords(), ms, c.failed, c.compiled, c.threads,
            c.threads == 1 ? "" : "s", (double)s_preLongestMs / 1000.0);
    if (c.budget[0]) {
        /* the careful policy's bound: the rest as the frames first need
           them, each batch of them written to the cache */
        rd_pipelines_lazy(c.budget);
    }
    if (c.compiled) {
        preTimesLog(&times);
    }
    return s_count - before;
}

bool rd__pipeline_gathering(void)
{
    return s_gatherOn;
}

void rd__pipeline_gather(bool on)
{
    s_gatherOn = on;
    if (on) {
        s_gatherN = 0;
    }
}

uint32_t rd__pipeline_create_gathered(bool *stopped)
{
    const double t0 = rd__now_ms();
    const uint32_t before = s_count, m = s_gatherN;
    PreCounts c;
    s_gatherOn = false;
    *stopped = false;
    if (m == 0 || !g_rd.hasDevice) {
        s_gatherN = 0;
        return 0;
    }
    createKeys(s_gather, m, true, NULL, &c);
    s_gatherN = 0;
    *stopped = c.stop != 0;
    rd__log("pipelines: %u of the %u keys a replay asked for created in %.1f ms (%u failed); "
            "%u compiled on %u thread%s%s",
            s_count - before, m, rd__now_ms() - t0, c.failed, c.compiled, c.threads,
            c.threads == 1 ? "" : "s", c.stop ? "; stopped (the game is closing)" : "");
    return m;
}

uint32_t rd__pipeline_count(void)
{
    return s_count;
}

uint32_t rd_pipeline_count(void)
{
    return s_count;
}

void rd_pipelines_lazy(const char *why)
{
    memset(&s_lazy, 0, sizeof(s_lazy));
    s_lazy.on = true;
    rd__log("pipelines: no start-up set %s: pipelines are made as the game needs them%s",
            why != NULL && why[0] != '\0' ? why : "(no reason given)", dynamicWords());
    rd__log("pipelines: the pipeline cache holds %llu KB at the start; it is written after each "
            "batch of new pipelines (%.1f s without one, or %.1f s after the batch's first)",
            (unsigned long long)((rhi_pipeline_cache_size() + 1023u) / 1024u),
            RD_LAZY_QUIET_MS / 1000.0, RD_LAZY_BATCH_MS / 1000.0);
}

/* the open lazy batch closed: the cache written, then its line ("lazy
 * batch 3, pipelines 41-57 of this run (compiler E031.41.03.47): 17
 * pipelines 0.02-0.61 s, mean 0.14 s, over 1.8 s; memory 210 MB (most 214
 * MB), pipeline cache 3104 KB; slowest 0.61 s program ... (font_sheet_ps
 * ...)") to the log and the activity file (rd_set_batch_fn) */
static void lazyBatchEnd(double now)
{
    char line[600], mem[96], w[96], mods[160];
    (void)rhi_pipeline_cache_save();
    memoryText(mem, sizeof(mem));
    keyWords(&s_lazy.slow, w, sizeof(w));
    keyModules(&s_lazy.slow, mods, sizeof(mods));
    s_lazy.batches++;
    snprintf(line, sizeof(line),
             "lazy batch %u, pipelines %u-%u of this run (compiler %s): %u pipelines %.2f-%.2f "
             "s, mean %.2f s, over %.1f s; %s; slowest %.2f s %s (%s)",
             s_lazy.batches, s_lazy.made - s_lazy.n + 1, s_lazy.made,
             s_compiler[0] ? s_compiler : "unnamed by the driver", s_lazy.n, s_lazy.lo / 1000.0,
             s_lazy.hi / 1000.0, s_lazy.sum / s_lazy.n / 1000.0, (now - s_lazy.t0) / 1000.0, mem,
             s_lazy.slowMs / 1000.0, w, mods);
    s_lazy.n = 0;
    if (s_batchFn != NULL) {
        s_batchFn(line);
    }
    rd__log("pipelines: compiled %s", line);
}

void rd_pipelines_lazy_tick(void)
{
    if (!s_lazy.on || s_lazy.n == 0 || !g_rd.hasDevice) {
        return;
    }
    const double now = rd__now_ms();
    if (now - s_lazy.tLast < RD_LAZY_QUIET_MS && now - s_lazy.t0 < RD_LAZY_BATCH_MS) {
        return;
    }
    lazyBatchEnd(now);
}

/* the i-th made pipeline's key (failed entries skipped: a walk only
 * when there are any, which the tests that call this never have) */
const RdPipeKeyInt *rd__pipeline_key_at(uint32_t i)
{
    if (i >= s_count) {
        return NULL;
    }
    if (s_failedCount == 0) {
        return &s_cache[i].key;
    }
    for (uint32_t e = 0; e < s_used; e++) {
        if (s_cache[e].pipe.id && i-- == 0) {
            return &s_cache[e].key;
        }
    }
    return NULL;
}

/* ------------------------------------------------ the VU modules
 * docs/port/RENDER.md, "The fallback entries": every device draws the VU
 * programs through one module set, the record path's (vu_main_vs,
 * vu_particle_main_vs, the pre-pass's and the stages' point entries), with
 * the region rule's every step.  A draw the record path cannot take (past
 * the record, a stage pipeline that could not be made, a failed self-test,
 * the pre-pass off) draws the old way, through its program's own entry
 * with vu_ps: vu_X_vs (the extended build), or on a
 * device of the table (RhiLimits.vuFallbackPlain, vk_device.c kVuPlainOnly)
 * vu_X_plain_vs (ICO_VU_PLAIN: no card test, no tick cameras), as
 * ICO_RD_VU_PLAIN=1 or the host (rd_set_vu_plain_only) ask anywhere;
 * ICO_RD_VU_EXTENDED=1 gives the extended ones whatever the others say.
 * One choice a run (g_rd.vsFallback), no key field: the program entries'
 * keys are in the start-up set only when the pre-pass is off. */
/* Whether the picture is not the PS2's out of the box, the Original preset
 * on a 4:3 frame at 1x (ICO_VU_EXTENDED; the region rule, vu_common.hlsli
 * vu_region_rule): the Enhanced preset, a frame wider than 4:3 or a scene
 * scale above 1.  Out of the box the PS2's behaviour stays; any other
 * state draws what a game made for that picture would draw (RENDER.md
 * "Strip joining").  rd_mesh.c asks it too, when it records a draw (the
 * card verdicts, RdVuCards). */
bool rd__vu_extended(void)
{
    return g_rd.settings.preset != RD_PRESET_ORIGINAL || g_rd.gsW > ICO_SH_FRAME_BASE_W ||
           g_rd.sceneSx > 1.0f || g_rd.sceneSy > 1.0f;
}

static char s_vuHostWhy[128];

void rd_set_vu_plain_only(const char *reason)
{
    snprintf(s_vuHostWhy, sizeof(s_vuHostWhy), "%s", reason != NULL ? reason : "");
}

const char *rd_vu_plain_only(void)
{
    return g_rd.vuFallbackPlain ? g_rd.vuPlainWhy : NULL;
}

static bool envOn(const char *name)
{
    const char *v = getenv(name);
    return v != NULL && v[0] != '\0' && v[0] != '0';
}

/* the largest of the vertex modules ids[0..n) as pipelineDesc picks them
 * (keyVsName): its name and size */
static uint32_t vuLargest(const uint8_t *ids, uint32_t n, char *name, size_t size)
{
    uint32_t best = 0;
    name[0] = '\0';
    for (uint32_t i = 0; i < n; i++) {
        RdPipeKeyInt k;
        char nm[64];
        memset(&k, 0, sizeof(k));
        k.vs = ids[i];
        keyVsName(&k, nm, sizeof(nm));
        const uint32_t w = rd__shader_words(nm);
        if (w > best) {
            best = w;
            snprintf(name, size, "%s", nm);
        }
    }
    if (!name[0]) {
        snprintf(name, size, "none in this build");
    }
    return best;
}

uint32_t rd__shader_words(const char *name)
{
    const IcoShaderBlob *b = name ? ico_find_shader(name) : NULL;
    return b ? (uint32_t)(b->spirv_len / 4u) : 0u;
}

uint32_t rd__shader_instructions(const char *name)
{
    /* SPIR-V: a five-word header, then each instruction's first word
     * holds its word count in the high half */
    const IcoShaderBlob *b = name ? ico_find_shader(name) : NULL;
    if (!b || b->spirv_len < 20u) {
        return 0u;
    }
    const uint32_t words = (uint32_t)(b->spirv_len / 4u);
    uint32_t n = 0;
    for (uint32_t at = 5; at < words; n++) {
        uint32_t w;
        memcpy(&w, (const uint8_t *)b->spirv + (size_t)at * 4u, 4);
        const uint32_t len = w >> 16;
        if (len == 0) {
            break; /* not SPIR-V as expected: the count so far */
        }
        at += len;
    }
    return n;
}

bool rd__vu_policy_init(const RhiLimits *lim)
{
    /* the device's compiler, as its driver names it (Qualcomm's Adreno
     * driver: "Compiler Version: E031.41.03.47"), once per rd_init */
    rd__log("device compiler: %s",
            lim && lim->compilerVersion ? lim->compilerVersion : "the driver names none");
    {
        /* "Compiler Version: E031.41.03.47" -> "E031.41.03.47" */
        const char *c = lim && lim->compilerVersion ? lim->compilerVersion : "";
        const char *v = strstr(c, "Version:");
        v = v ? v + 8 : c;
        while (*v == ' ') {
            v++;
        }
        snprintf(s_compiler, sizeof(s_compiler), "%s", v);
    }
    /* the dynamic state (rd__key_dynamic_normalise, rd__set_pipeline) */
    g_rd.dynDepth = lim != NULL && lim->dynamicDepth;
    g_rd.dynBlend = lim != NULL && lim->dynamicBlend;
    rd__log("pipelines: dynamic state: %s",
            g_rd.dynDepth && g_rd.dynBlend
                ? "depth and blend (the Z test, Z write, blend and colour mask set at each draw, "
                  "out of the pipeline keys)"
            : g_rd.dynDepth ? "depth (the Z test and Z write set at each draw, out of the pipeline "
                              "keys; the blend in the keys)"
            : g_rd.dynBlend ? "blend (the blend and colour mask set at each draw, out of the "
                              "pipeline keys; the Z test and Z write in the keys)"
                            : "none (every state in the pipeline keys)");
    /* the fallback's program entries */
    g_rd.vuFallbackPlain = false;
    g_rd.vuPlainWhy[0] = '\0';
    if (lim && lim->vuFallbackPlain) {
        snprintf(g_rd.vuPlainWhy, sizeof(g_rd.vuPlainWhy), "on this device (%s)",
                 lim->vuFallbackPlain);
    } else if (envOn("ICO_RD_VU_PLAIN")) {
        snprintf(g_rd.vuPlainWhy, sizeof(g_rd.vuPlainWhy), "by ICO_RD_VU_PLAIN=1");
    } else if (s_vuHostWhy[0]) {
        snprintf(g_rd.vuPlainWhy, sizeof(g_rd.vuPlainWhy), "(%s)", s_vuHostWhy);
    }
    bool plain = g_rd.vuPlainWhy[0] != '\0';
    if (plain && envOn("ICO_RD_VU_EXTENDED")) {
        rd__log("VU shaders: the fallback's plain program entries %s, but ICO_RD_VU_EXTENDED=1: "
                "the extended ones are used",
                g_rd.vuPlainWhy);
        plain = false;
        g_rd.vuPlainWhy[0] = '\0';
    }
    /* the ten mesh entries' plain modules (vu_X_vs's is vu_X_plain_vs;
     * rd_replay.c rd__gpu_shutdown destroys them), made only when the
     * fallback takes them; one missing keeps the extended ones */
    for (uint8_t v = RD_VS_VU_PRELIT; plain && v <= RD_VS_VU_GRID_SPEC; v++) {
        char name[64];
        const char *e = rd__vs_name(v);
        snprintf(name, sizeof(name), "%.*s_plain_vs", (int)(strlen(e) > 3 ? strlen(e) - 3 : 0), e);
        g_rd.vsPlain[v] = rd__make_shader(name);
        if (!g_rd.vsPlain[v].id) {
            rd__log("VU shaders: %s is missing: the fallback takes the extended program entries",
                    name);
            for (uint8_t u = RD_VS_VU_PRELIT; u <= v; u++) {
                if (g_rd.vsPlain[u].id) {
                    rhi_destroy_shader(g_rd.vsPlain[u]);
                }
                g_rd.vsPlain[u] = (RhiShader){0};
            }
            plain = false;
            g_rd.vuPlainWhy[0] = '\0';
        }
    }
    g_rd.vuFallbackPlain = plain;
    for (uint8_t v = RD_VS_VU_PRELIT; v <= RD_VS_VU_PARTICLE; v++) {
        g_rd.vsFallback[v] = plain && g_rd.vsPlain[v].id ? g_rd.vsPlain[v] : g_rd.vs[v];
    }
    static const uint8_t kMain[] = {RD_VS_VU_MAIN,           RD_VS_VU_PARTICLE_MAIN,
                                    RD_VS_VU_TRI_RULE_PRE,   RD_VS_VU_TRI_TICK_PRE,
                                    RD_VS_VU_TRI_PLANES_PRE, RD_VS_VU_PARTICLE_PRE};
    uint8_t entries[RD_VS_VU_PARTICLE - RD_VS_VU_PRELIT + 1];
    for (uint8_t v = RD_VS_VU_PRELIT; v <= RD_VS_VU_PARTICLE; v++) {
        entries[v - RD_VS_VU_PRELIT] = v;
    }
    char mn[64], fn[64];
    const uint32_t mw = vuLargest(kMain, (uint32_t)sizeof(kMain), mn, sizeof(mn));
    const uint32_t fw = vuLargest(entries, (uint32_t)sizeof(entries), fn, sizeof(fn));
    rd__log("VU shaders: the record path's modules on every device (largest %s %u words); the "
            "fallback's program entries %s%s%s (largest %s %u words), in the start-up set only "
            "with the vertex pre-pass off",
            mn, mw, plain ? "plain " : "extended", plain ? g_rd.vuPlainWhy : "",
            plain ? ": no wide-screen rule and no tick cameras on those draws" : "", fn, fw);
    return true;
}

RdPipeKeyInt rd__vu_pre_key(uint8_t preVs)
{
    /* normalised as every key (rd_state_test testEnumeration): no alpha
     * test, no blend; the program is the one the drawing keys carry
     * (rd_replay_vu.c vuDraw) */
    RdPipeKeyInt k;
    memset(&k, 0, sizeof(k));
    k.gs.program = RD_PROG_PRELIT;
    k.gs.blend = RD_BLEND_COUNT;
    k.gs.atst = RD_ATST_ALWAYS;
    k.gs.colorMask = 0xF;
    k.gs.prim = RD_PRIM_POINTS;
    k.vs = preVs;
    k.fs = RD_FS_VU_PRE;
    k.colorFmt = RHI_FMT_RGBA32_UINT;
    k.depthFmt = RHI_FMT_UNKNOWN;
    return k;
}

RdPipeKeyInt rd__vu_stage_key(uint8_t vs, uint8_t colorMask)
{
    RdPipeKeyInt k = rd__vu_pre_key(vs);
    k.gs.colorMask = (uint8_t)(colorMask & 0xFu);
    return k;
}

/* The start-up set's order: the 2D keys, then the VU keys (the drawing
 * shaders', the pre-pass's and the stages'), each in its enumeration order,
 * so a driver that dies on the largest modules has built every other key
 * before it meets one, and the activity note names the class of the batch
 * it died in */
static uint32_t keyClass(const RdPipeKeyInt *k)
{
    return keyIsVu(k) ? 1u : 0u;
}

/* The families of the set (rd_internal.h RdKeyFamily), by what the key
 * holds, not by the enumerator that listed it: the DATE twins of VU and
 * screen keys count as DATE, the water's states as 2D.  The order class
 * above stays the VU/other split, so a DATE twin of a VU key is still
 * made with the VU batches. */
RdKeyFamily rd__pipe_key_family(const RdPipeKeyInt *k)
{
    switch (k->gs.stencil) {
    case RD_STENCIL_DATE_CLEAR:
    case RD_STENCIL_DATE_SET:
    case RD_STENCIL_DATE_TEST0:
    case RD_STENCIL_DATE_TEST1:
        return RD_KF_DATE;
    default:
        break;
    }
    if (rd__vs_is_vu_pre(k->vs) || rd__vs_is_vu_stage(k->vs)) {
        return RD_KF_VU_POINT;
    }
    if (keyIsVu(k)) {
        return RD_KF_VU;
    }
    switch (k->fs) {
    case RD_FS_BOX_REDUCE:
        return RD_KF_SHADOW;
    case RD_FS_FOG:
    case RD_FS_FOG_BUFFER:
    case RD_FS_FOG_MS:
    case RD_FS_FOG_LOCAL:
    case RD_FS_DEPTH_RESOLVE:
    case RD_FS_DEPTH_UPSAMPLE:
        return RD_KF_FOG;
    case RD_FS_CRT_BLOOM:
    case RD_FS_CRT_BLUR:
    case RD_FS_CRT:
        return RD_KF_CRT;
    case RD_FS_FX_SPRITE:
    case RD_FS_FX_SPRITE_UINT:
    case RD_FS_FX_BLUR2:
    case RD_FS_FX_FRAME:
    case RD_FS_FX_LOCAL:
        return RD_KF_FX;
    default:
        break;
    }
    if (k->gs.stencil == RD_STENCIL_INCR || k->gs.stencil == RD_STENCIL_DECR ||
        k->gs.stencil == RD_STENCIL_TEST_NONZERO) {
        return RD_KF_SHADOW;
    }
    return RD_KF_2D;
}

const char *rd__pipe_family_name(RdKeyFamily f)
{
    static const char *const kName[RD_KF_COUNT] = {"2D",  "VU",  "VU point", "shadow",
                                                   "fog", "CRT", "fx",       "DATE"};
    return (unsigned)f < RD_KF_COUNT ? kName[f] : "?";
}

uint32_t rd__pipe_family_counts(const RdPipeKeyInt *keys, uint32_t n, uint32_t max,
                                uint32_t counts[RD_KF_COUNT])
{
    memset(counts, 0, sizeof(counts[0]) * RD_KF_COUNT);
    const uint32_t m = n < max ? n : max;
    for (uint32_t i = 0; i < m; i++) {
        counts[rd__pipe_key_family(&keys[i])]++;
    }
    return m;
}

static void orderSet(RdPipeKeyInt *out, uint32_t n)
{
    static RdPipeKeyInt tmp[RD_PIPELINE_CACHE_MAX];
    if (n > RD_PIPELINE_CACHE_MAX) {
        n = RD_PIPELINE_CACHE_MAX;
    }
    uint32_t at = 0;
    for (uint32_t c = 0; c < 2; c++) {
        for (uint32_t i = 0; i < n; i++) {
            if (keyClass(&out[i]) == c) {
                tmp[at++] = out[i];
            }
        }
    }
    memcpy(out, tmp, (size_t)n * sizeof(out[0]));
}

/* -------------------------------------------------------------- reachable
 * The families the implemented programs (screen prims and the post/present
 * blits) can reach, each from the state the game uses with it:
 *
 *   UI / FULLSCREEN screen prims (GifPacket.c 2D helpers, layout_texture.c,
 *   jimaku.c, DisplayFont.c, the GsBase.c post sprites): TEST 0x30000 (20
 *   sites, "2D, post"), 0x3000C, 0x30815 (flare), 0 (one site); ZBUF on and
 *   off (the list defaults and gif_SetZWrite); ALPHA off or modes 0, 1, 2,
 *   4, 5, 6, 7 (the gif_SetAlpha literals); sprites
 *   and strips (triangles) and lines; SCENE (with depth) or a target
 *   without depth (DISPLAY, WORK, AA, FEED128, temporaries).
 *   WORLD screen prims (CPU-projected 2D: points and lines of list 2,
 *   glows): the same plus the depth-tested literals 0x50000, 0x5000D, the
 *   list 1/2 default 0x5140D (AFAIL split) and the list 4 default 0x5C000
 *   (DATE, normalised out).
 *   The DATE snapshot: blit_vs/date_snap_ps into R8.
 *   PRIM.AA1 lines and triangles: WORLD screen prims through
 *   sprite_aa1_world_vs / sprite_aa1_ps, the states stormTest.c and puddle.c
 *   draw them under (rd__enumerate_reachable_screen lists them).
 *   Blits: blit_vs/blit_ps into RGBA8 (presenter line doubling, headless
 *   output) and into the swapchain format (BGRA8 or RGBA8); blend_int into
 *   RGBA8_UINT (exact feedback blends).
 *
 * The mode 3 and Ad blends are reachable only through BGA lightning data,
 * drawn by rd_world_prims, so they are not in the screen families.
 * Each enumerated state goes through rd__plan_screen_draw, the function the
 * replayer uses, so the count is the count the cache would reach.
 *
 * rd__enumerate_reachable adds the VU program families
 * (rd__enumerate_reachable_vu, rd_mesh.c) to these; the screen and post set
 * alone stays rd__enumerate_reachable_screen. */
uint32_t rd__add_pipe_key(RdPipeKeyInt *out, uint32_t max, uint32_t n, const RdPipeKeyInt *k)
{
    for (uint32_t i = 0; i < n && i < max; i++) {
        if (keyEqual(&out[i], k)) {
            return n;
        }
    }
    if (n < max) {
        out[n] = *k;
    }
    return n + 1;
}

static uint32_t enumerateAll(RdPipeKeyInt *out, uint32_t max)
{
    uint32_t n = rd__enumerate_reachable_vu(out, max, rd__enumerate_reachable_screen(out, max));
    n = rd__enumerate_reachable_shadow(out, max, n);
    n = rd__enumerate_reachable_fog(out, max, n);
    n = rd__enumerate_reachable_water(out, max, n);
    n = rd__enumerate_reachable_crt(out, max, n);
    n = rd__enumerate_reachable_date(out, max, n); /* after every family that draws DATE */
    return rd__enumerate_reachable_blur(out, max, n);
}

/* In the two-pass fallback every key is what rd__expand_no_dual
 * makes of it (the families above plan with dual-source blending), so the
 * set precreated is the set the draws reach (rd_mesh.c's VU and rd_water.c's
 * families among them).  With the exact blend (rd_dst_read.c) the
 * destination-read images of the blended keys join them (the plain keys
 * stay: the MSAA epoch, other formats and the storage path's Z-writing
 * discarding draws keep the fixed-function blend). */
uint32_t rd__enumerate_reachable(RdPipeKeyInt *out, uint32_t max)
{
    const bool dyn = g_rd.dynDepth || g_rd.dynBlend;
    if (!g_rd.noDual && g_rd.dstMode == RD_BLEND_EXACT_OFF && !dyn) {
        const uint32_t n = enumerateAll(out, max);
        orderSet(out, n < max ? n : max);
        return n;
    }
    static RdPipeKeyInt raw[RD_PIPELINE_CACHE_MAX];
    static RdPipeKeyInt planned[RD_PIPELINE_CACHE_MAX];
    const uint32_t nr = enumerateAll(raw, RD_PIPELINE_CACHE_MAX);
    const uint32_t nk = nr < RD_PIPELINE_CACHE_MAX ? nr : RD_PIPELINE_CACHE_MAX;
    /* the keys as the draws plan them: the families' own, or what the
     * two-pass fallback and the exact blend make of them */
    RdPipeKeyInt *dst = dyn ? planned : out;
    const uint32_t dmax = dyn ? RD_PIPELINE_CACHE_MAX : max;
    uint32_t n = 0;
    if (!g_rd.noDual && g_rd.dstMode == RD_BLEND_EXACT_OFF) {
        for (uint32_t i = 0; i < nk; i++) {
            n = rd__add_pipe_key(dst, dmax, n, &raw[i]);
        }
    } else {
        for (uint32_t i = 0; i < nk; i++) {
            RdDrawPass in, ex[4];
            memset(&in, 0, sizeof(in));
            in.key = raw[i];
            const int m = rd__expand_no_dual(&in, 1, ex);
            for (int j = 0; j < m; j++) {
                n = rd__add_pipe_key(dst, dmax, n, &ex[j].key);
            }
        }
        n = rd__enumerate_dst(raw, nk, dst, dmax, n);
    }
    if (dyn) {
        /* the dynamic state's fields normalised away
         * (rd__key_dynamic_normalise, as drawKey does): the set collapses
         * to the keys the draws' pipelines are made from */
        const uint32_t np = n < RD_PIPELINE_CACHE_MAX ? n : RD_PIPELINE_CACHE_MAX;
        n = 0;
        for (uint32_t i = 0; i < np; i++) {
            RdPipeKeyInt k = planned[i];
            rd__key_dynamic_normalise(&k);
            n = rd__add_pipe_key(out, max, n, &k);
        }
    }
    orderSet(out, n < max ? n : max);
    return n;
}

/* the CRT filter (rd_crt.c): the glow passes into RGBA16F, the composite on
 * the headless output (RGBA8) and the swapchain (BGRA8); the virtual
 * source's box reduction is the shadow family's key */
uint32_t rd__enumerate_reachable_crt(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    const RdPipeKeyInt keys[4] = {rd__post_key(RD_VS_CRT, RD_FS_CRT_BLOOM, RHI_FMT_RGBA16F),
                                  rd__post_key(RD_VS_CRT, RD_FS_CRT_BLUR, RHI_FMT_RGBA16F),
                                  rd__post_key(RD_VS_CRT, RD_FS_CRT, RHI_FMT_RGBA8_UNORM),
                                  rd__post_key(RD_VS_CRT, RD_FS_CRT, RHI_FMT_BGRA8_UNORM)};
    for (int i = 0; i < 4; i++) {
        n = rd__add_pipe_key(out, max, n, &keys[i]);
    }
    return n;
}

/* ------------------------------------------------------------------- fog */

int rd__fog_plan(const RdStateBlock *s, RhiFormat colorFmt, int path, RdDrawPass out[2])
{
    const int np = rd__plan_screen_draw(s, RD_PRIM_TRIANGLES, RD_SPACE_FULLFRAME, colorFmt,
                                        RHI_FMT_UNKNOWN, out);
    for (int i = 0; i < np; i++) {
        out[i].key.gs.program = RD_PROG_POST;
        out[i].key.fs = path == RD_FOG_BUFFER  ? RD_FS_FOG_BUFFER
                        : path == RD_FOG_LOCAL ? RD_FS_FOG_LOCAL
                                               : RD_FS_FOG;
        if (path == RD_FOG_LOCAL) {
            /* the scene pass's depth bound (the input attachment the shader
             * reads), its test ALWAYS and no write: the shader tests Z */
            out[i].key.depthFmt = RHI_FMT_D32F_S8;
            out[i].key.gs.ztst = RD_ZTST_ALWAYS;
            out[i].key.gs.zwrite = RD_ZWRITE_OFF;
        }
    }
    return np;
}

uint32_t rd__enumerate_reachable_fog(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    /* fog_DrawFog: TEST 0x50000, ZBUF with ZMSK, ALPHA 0x44, PRIM 0x156 (ABE)
     * into SCENE (RGBA8) */
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
    s.ds.zwrite = RD_ZWRITE_OFF;
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    /* the shader of the path the self-test picks, or the override; the
     * local read's where the profile has it (rd_fog_path.c) */
    const int local = g_rd.profile.fogLocal && g_rd.fs[RD_FS_FOG_LOCAL].id != 0;
    static const int kPaths[3] = {RD_FOG_INPLACE, RD_FOG_BUFFER, RD_FOG_LOCAL};
    for (int j = 0; j < 2 + local; j++) {
        RdDrawPass dp[2];
        const int np = rd__fog_plan(&s, RHI_FMT_RGBA8_UNORM, kPaths[j], dp);
        for (int i = 0; i < np; i++) {
            n = rd__add_pipe_key(out, max, n, &dp[i].key);
        }
    }
    return n;
}

/* --------------------------------------------------------------- DATE */

RdPipeKeyInt rd__date_setup_key(uint8_t mode, RhiFormat colorFmt)
{
    RdPipeKeyInt k = rd__post_key(
        RD_VS_BLIT, mode == RD_STENCIL_DATE_CLEAR ? RD_FS_DATE_CLEAR : RD_FS_DATE_STENCIL,
        colorFmt);
    k.depthFmt = RHI_FMT_D32F_S8;
    k.gs.ztst = RD_ZTST_ALWAYS;
    k.gs.zwrite = RD_ZWRITE_OFF;
    k.gs.colorMask = 0;
    k.gs.stencil = mode;
    return k;
}

bool rd__date_stencil_pass(RdDrawPass *p)
{
    RdPipeKeyInt *k = &p->key;
    /* the planner keeps the key's date off and DATE in the flags (rd__plan_screen_draw) */
    if (!(p->flags & ICO_DF_DATE) || k->gs.stencil != RD_STENCIL_OFF ||
        k->depthFmt == RHI_FMT_UNKNOWN) {
        return false;
    }
    k->gs.stencil = (p->flags & ICO_DF_DATM) ? RD_STENCIL_DATE_TEST1 : RD_STENCIL_DATE_TEST0;
    k->gs.date = RD_DATE_OFF;
    p->flags &= ~(uint32_t)(ICO_DF_DATE | ICO_DF_DATM);
    return true;
}

/* DATE through the stencil bit (RdProfile.dateStencil, its shaders in the
 * table): the twins of rd__date_stencil_pass of the draws the game makes
 * with DATE (rd_state.h's literal TEST words with DATE: the shadow
 * composite's 0x3400D, the aura's 0x34000 and 0x34003, the puddle's
 * 0x3F000 and 0x3F001, Z ALWAYS; the list 4 default 0x5C000, Z GEQUAL)
 * through the screen families' planner (UI and world space, triangles and
 * lines, Z write on and off, every blend the families list, the scene
 * depth, sprite_ps and its TEXA twin); the VU draws' twins of the
 * enumerated VU keys under Z GEQUAL with the scene depth (the list 4
 * default: DATM 1); and the two setup keys (RGBA8: the scene class).
 * Keys missing here are made on first use. */
uint32_t rd__enumerate_reachable_date(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    if (!g_rd.profile.dateStencil || !g_rd.fs[RD_FS_DATE_STENCIL].id ||
        !g_rd.fs[RD_FS_DATE_CLEAR].id) {
        return n;
    }
    /* one TEST word and one blend for each key they plan to, so no
     * candidate is planned only to be found twice: RD_TEST_DATE0 (ATE off)
     * and RD_TEST_AT_ALWAYS_DATE0 (ATE ALWAYS) plan to RD_TEST_AT_GT0_DATE0's
     * keys (AFAIL KEEP: one pass, the test in its modeZ, not in the key);
     * the families' blends 0, 1, 2 and 7 to 5, 6, 4 and 4 (canonicalBlend:
     * one key per blend path, FIX in the pass's flags) */
    static const uint64_t kTests[] = {RD_TEST_AT_GT0_DATE0, RD_TEST_RGBONLY_DATE1,
                                      RD_TEST_NEVER_RGBONLY_DATE1, RD_TEST_DATE1_Z_GEQUAL};
    static const int kBlends[] = {-1, 4, 5, 6};
    static const uint8_t kPrims[] = {RD_PRIM_TRIANGLES, RD_PRIM_LINES};
    const uint32_t n0 = n < max ? n : max;
    for (size_t t = 0; t < sizeof(kTests) / sizeof(kTests[0]); t++) {
        for (int space = 0; space < 2; space++) {
            for (int zw = 0; zw < 2; zw++) {
                for (size_t b = 0; b < sizeof(kBlends) / sizeof(kBlends[0]); b++) {
                    for (size_t pr = 0; pr < sizeof(kPrims); pr++) {
                        RdStateBlock st;
                        rd__reset_state_block(&st);
                        st.ds.test = rd_test_from_gs(kTests[t]);
                        st.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                        st.ds.abe = kBlends[b] >= 0;
                        st.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
                        RdDrawPass dp[2];
                        const int np = rd__plan_screen_draw(
                            &st, kPrims[pr], space ? RD_SPACE_WORLD : RD_SPACE_UI,
                            RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, dp);
                        for (int i = 0; i < np; i++) {
                            RdDrawPass kt = dp[i];
                            kt.key.fs = RD_FS_SPRITE_TEXA;
                            if (rd__date_stencil_pass(&dp[i])) {
                                n = rd__add_pipe_key(out, max, n, &dp[i].key);
                            }
                            if (rd__date_stencil_pass(&kt)) {
                                n = rd__add_pipe_key(out, max, n, &kt.key);
                            }
                        }
                    }
                }
            }
        }
    }
    for (uint32_t i = 0; i < n0; i++) {
        const RdPipeKeyInt *k = &out[i];
        const int vu =
            (k->vs >= RD_VS_VU_FIRST && k->vs <= RD_VS_VU_LAST) || rd__vs_is_vu_main(k->vs);
        if (vu && k->depthFmt != RHI_FMT_UNKNOWN && k->gs.stencil == RD_STENCIL_OFF &&
            k->gs.ztst == RD_ZTST_GEQUAL && k->dst == RHI_DST_READ_NONE) {
            RdPipeKeyInt t = *k;
            t.gs.stencil = RD_STENCIL_DATE_TEST1;
            n = rd__add_pipe_key(out, max, n, &t);
        }
    }
    const RdPipeKeyInt clear = rd__date_setup_key(RD_STENCIL_DATE_CLEAR, RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt set = rd__date_setup_key(RD_STENCIL_DATE_SET, RHI_FMT_RGBA8_UNORM);
    n = rd__add_pipe_key(out, max, n, &clear);
    return rd__add_pipe_key(out, max, n, &set);
}

/* --------------------------------------------------------------- shadows */

RdPipeKeyInt rd__shadow_volume_key(const RdStateBlock *s, RhiFormat colorFmt, int decr)
{
    RdPipeKeyInt k;
    memset(&k, 0, sizeof(k));
    k.gs.program = RD_PROG_SHADOW_VOLUME;
    k.gs.blend = RD_BLEND_COUNT;
    k.gs.atst = RD_ATST_ALWAYS;
    k.gs.date = RD_DATE_OFF;
    k.gs.ztst = s->ds.test.zte ? s->ds.test.ztst : RD_ZTST_ALWAYS;
    k.gs.zwrite = RD_ZWRITE_OFF; /* the count never writes Z (ZBUF.ZMSK in shadow_Reset) */
    k.gs.colorMask = 0;
    k.gs.stencil = decr ? RD_STENCIL_DECR : RD_STENCIL_INCR;
    k.gs.targetFmt = tfmtOf(colorFmt);
    k.gs.prim = RD_PRIM_TRIANGLES;
    k.vs = RD_VS_SPRITE_WORLD;
    k.fs = RD_FS_SPRITE;
    k.colorFmt = (uint8_t)colorFmt;
    k.depthFmt = RHI_FMT_D32F_S8;
    return k;
}

/* The box reduction of the scaled count to the GS size
 * (rd_replay.c shadowReduce): RGBA8 like the count. */
RdPipeKeyInt rd__shadow_reduce_key(void)
{
    return rd__post_key(RD_VS_BLIT, RD_FS_BOX_REDUCE, RHI_FMT_RGBA8_UNORM);
}

RdPipeKeyInt rd__shadow_resolve_key(int pass)
{
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_RGBA8_UNORM);
    k.depthFmt = RHI_FMT_D32F_S8;
    if (pass < 6) {
        /* additive (ONE, ONE): the six bits' 4 << k sum to 4 n, at most 252 */
        k.gs.blend = RD_BLEND_CS_AS_ADD_CD;
        k.gs.colorMask = 0x7;
        k.gs.stencil = (uint8_t)(RD_STENCIL_RESOLVE_BIT0 + pass);
    } else {
        k.gs.colorMask = 0x8;
        k.gs.stencil = RD_STENCIL_TEST_NONZERO;
    }
    return k;
}

uint32_t rd__enumerate_reachable_shadow(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL); /* shadow_Reset's TEST 0x50000 */
    for (int decr = 0; decr < 2; decr++) {
        const RdPipeKeyInt k = rd__shadow_volume_key(&s, RHI_FMT_RGBA8_UNORM, decr);
        n = rd__add_pipe_key(out, max, n, &k);
    }
    for (int p = 0; p < RD_SHADOW_RESOLVE_PASSES; p++) {
        const RdPipeKeyInt k = rd__shadow_resolve_key(p);
        n = rd__add_pipe_key(out, max, n, &k);
    }
    const RdPipeKeyInt kr = rd__shadow_reduce_key(); /* box reduction to the GS size */
    n = rd__add_pipe_key(out, max, n, &kr);
    return n;
}

uint32_t rd__enumerate_reachable_screen(RdPipeKeyInt *out, uint32_t max)
{
    static const uint64_t kUiTests[] = {RD_TEST_Z_ALWAYS, RD_TEST_Z_ALWAYS_ATST_GT,
                                        RD_TEST_AT_LT129, RD_TEST_OFF};
    static const uint64_t kWorldTests[] = {
        RD_TEST_Z_ALWAYS, RD_TEST_Z_ALWAYS_ATST_GT, RD_TEST_AT_LT129,       RD_TEST_OFF,
        RD_TEST_Z_GEQUAL, RD_TEST_AT_GT0_Z_GEQUAL,  RD_TEST_AT_GT64_FBONLY, RD_TEST_DATE1_Z_GEQUAL};
    static const int kBlends[] = {-1, 0, 1, 2, 4, 5, 6, 7};
    static const uint8_t kPrims[] = {RD_PRIM_TRIANGLES, RD_PRIM_LINES};
    static const RhiFormat kDepth[] = {RHI_FMT_D32F_S8, RHI_FMT_UNKNOWN};
    uint32_t n = 0;
    for (int space = 0; space < 2; space++) {
        const uint64_t *tests = space ? kWorldTests : kUiTests;
        const int nt = space ? (int)(sizeof(kWorldTests) / sizeof(kWorldTests[0]))
                             : (int)(sizeof(kUiTests) / sizeof(kUiTests[0]));
        for (int t = 0; t < nt; t++) {
            for (int zw = 0; zw < 2; zw++) {
                for (size_t b = 0; b < sizeof(kBlends) / sizeof(kBlends[0]); b++) {
                    for (size_t p = 0; p < sizeof(kPrims); p++) {
                        for (size_t dz = 0; dz < 2; dz++) {
                            /* UI space also under the dark volume's colour mask
                             * 7 (PSMCT24 composite: FBMSK holds until the next
                             * FRAME write, so the list-11 2D draws of a frame
                             * without the anti-alias pass take it) */
                            for (int m = 0; m < (space ? 1 : 2); m++) {
                                RdStateBlock s;
                                rd__reset_state_block(&s);
                                s.ds.test = rd_test_from_gs(tests[t]);
                                s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                                s.ds.abe = kBlends[b] >= 0;
                                s.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
                                s.ds.colorMask = m ? 0x7 : 0xF;
                                RdDrawPass dp[2];
                                int np = rd__plan_screen_draw(&s, kPrims[p],
                                                              space ? RD_SPACE_WORLD : RD_SPACE_UI,
                                                              RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
                                /* a triangle command draws with its
                                 * planes when every pass takes them
                                 * (rd_replay.c doScreen, rd_screen_planes.c);
                                 * a sprite command's passes (the same
                                 * triangle topology) without */
                                int planes = kPrims[p] == RD_PRIM_TRIANGLES;
                                for (int i = 0; i < np && planes; i++) {
                                    RdDrawPass t = dp[i];
                                    planes = rd__plane_pass(&t);
                                }
                                for (int i = 0; i < np; i++) {
                                    n = rd__add_pipe_key(out, max, n, &dp[i].key);
                                    /* TEXA per texel: the same state on a
                                     * 24- or 16-bit texture under AEM */
                                    RdDrawPass kt = dp[i];
                                    kt.key.fs = RD_FS_SPRITE_TEXA;
                                    n = rd__add_pipe_key(out, max, n, &kt.key);
                                    RdDrawPass pl = dp[i];
                                    if (planes && rd__plane_pass(&pl) && rd__plane_pass(&kt)) {
                                        n = rd__add_pipe_key(out, max, n, &pl.key);
                                        n = rd__add_pipe_key(out, max, n, &kt.key);
                                    }
                                }
                                /* STQ: a textured STQ triangle
                                 * command with Q != 1 (the lightning's strips,
                                 * raw GIF writes: WORLD space) */
                                if (space && kPrims[p] == RD_PRIM_TRIANGLES) {
                                    for (int i = 0; i < np; i++) {
                                        if (rd__stq_pass(&dp[i])) {
                                            if (planes) {
                                                rd__plane_pass(&dp[i]);
                                            }
                                            n = rd__add_pipe_key(out, max, n, &dp[i].key);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    /* UI-space sprites under TEST Z GEQUAL with the depth target, Z write
       on and off, colour mask F, every blend: every list's head writes
       that TEST (GsBase.c gsb_setNormalReg, lists 0 and 7-12), so a 2D draw
       of list 11 before the list's first TEST write takes it (the phone's
       first-use line: program 9 blend 4, sprite_ui_vs + sprite_ps, fmt
       1/8) */
    for (int zw = 0; zw < 2; zw++) {
        for (size_t b = 0; b < sizeof(kBlends) / sizeof(kBlends[0]); b++) {
            RdStateBlock s;
            rd__reset_state_block(&s);
            s.ds.test = rd_test_from_gs(RD_TEST_Z_GEQUAL);
            s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
            s.ds.abe = kBlends[b] >= 0;
            s.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
            RdDrawPass dp[2];
            const int np = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI,
                                                RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, dp);
            for (int i = 0; i < np; i++) {
                n = rd__add_pipe_key(out, max, n, &dp[i].key);
                dp[i].key.fs = RD_FS_SPRITE_TEXA; /* TEXA per texel */
                n = rd__add_pipe_key(out, max, n, &dp[i].key);
            }
        }
    }
    /* the PRIM.AA1 lines and triangles, WORLD space (the storm's
     * line strips, stormTest.c: list 11, TEST 0x50000, ZMSK, ALPHA mode 5
     * with ABE 0; the puddle's ripple strips, puddle.c: TEST 0x3F000, mode 0
     * or 4 with ABE 1), with every game blend literal, Z on and off, with and
     * without a depth target; the edge geometry's zwrite-off keys are among
     * them */
    static const uint64_t kAa1Tests[] = {RD_TEST_Z_ALWAYS, RD_TEST_Z_GEQUAL, RD_TEST_RGBONLY_DATE1};
    static const int kAa1Blends[] = {0, 1, 2, 4, 5, 6, 7};
    for (size_t t = 0; t < sizeof(kAa1Tests) / sizeof(kAa1Tests[0]); t++) {
        for (int zw = 0; zw < 2; zw++) {
            for (size_t b = 0; b < sizeof(kAa1Blends) / sizeof(kAa1Blends[0]); b++) {
                for (size_t p = 0; p < sizeof(kPrims); p++) {
                    for (size_t dz = 0; dz < 2; dz++) {
                        RdStateBlock s;
                        rd__reset_state_block(&s);
                        s.ds.test = rd_test_from_gs(kAa1Tests[t]);
                        s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                        s.ds.abe = 1;
                        s.ds.blend = (uint8_t)kAa1Blends[b];
                        s.aa1 = 1;
                        RdDrawPass dp[2];
                        const int np = rd__plan_screen_draw_ex(&s, kPrims[p], 1, RD_SPACE_WORLD,
                                                               RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
                        for (int i = 0; i < np; i++) {
                            n = rd__add_pipe_key(out, max, n, &dp[i].key);
                        }
                    }
                }
            }
        }
    }
    /* the presentation overlay on the headless output and the
     * swapchain (sprites and triangles; the popups' blend and the glow's) */
    static const uint8_t kOverlayBlends[] = {RD_BLEND_LERP_AS, RD_BLEND_CS_AS_ADD_CD};
    static const RhiFormat kOutFormats[] = {RHI_FMT_RGBA8_UNORM, RHI_FMT_BGRA8_UNORM};
    for (size_t f = 0; f < 2; f++) {
        for (size_t b = 0; b < 2; b++) {
            RdStateBlock s;
            rd__overlay_state(&s, kOverlayBlends[b]);
            RdDrawPass dp[2];
            const int np = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, kOutFormats[f],
                                                RHI_FMT_UNKNOWN, dp);
            for (int i = 0; i < np; i++) {
                n = rd__add_pipe_key(out, max, n, &dp[i].key);
                /* the font atlas on the overlay (font_ps) */
                dp[i].key.fs = RD_FS_FONT;
                n = rd__add_pipe_key(out, max, n, &dp[i].key);
                /* the popups' and the photo panel's sheet text
                 * (font_sheet_ps) */
                dp[i].key.fs = RD_FS_FONT_SHEET;
                n = rd__add_pipe_key(out, max, n, &dp[i].key);
            }
        }
    }
    /* the port's text in the frame (port/ui/font.c setState:
     * TEST 0x30000, no Z write, ALPHA 0x44 or 0x48; UI_KEEP_STATE keeps the
     * layout packet's, which differs only in the alpha test, a uniform)
     * through font_ps, with and without the depth target */
    for (size_t b = 0; b < 2; b++) {
        for (size_t dz = 0; dz < 2; dz++) {
            RdStateBlock s;
            rd__reset_state_block(&s);
            s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
            s.ds.zwrite = RD_ZWRITE_OFF;
            s.ds.abe = 1;
            s.ds.blend = kOverlayBlends[b];
            RdDrawPass dp[2];
            const int np = rd__plan_screen_draw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI,
                                                RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
            for (int i = 0; i < np; i++) {
                /* the menus' sheet text (font_sheet_ps) in the same
                 * states */
                for (int sh = 0; sh < 2; sh++) {
                    dp[i].key.fs = sh ? RD_FS_FONT_SHEET : RD_FS_FONT;
                    dp[i].key.gs.colorMask = 0xF;
                    n = rd__add_pipe_key(out, max, n, &dp[i].key);
                    dp[i].key.gs.colorMask = 0x7; /* under the dark volume's FBMSK, as above */
                    n = rd__add_pipe_key(out, max, n, &dp[i].key);
                }
            }
        }
    }
    const RdPipeKeyInt blitA = rd__post_key(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt blitB = rd__post_key(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_BGRA8_UNORM);
    const RdPipeKeyInt exact = rd__post_key(RD_VS_BLEND_INT, RD_FS_BLEND_INT, RHI_FMT_RGBA8_UINT);
    const RdPipeKeyInt dateSnap = rd__post_key(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
    n = rd__add_pipe_key(out, max, n, &dateSnap);
    n = rd__add_pipe_key(out, max, n, &blitA);
    n = rd__add_pipe_key(out, max, n, &blitB);
    /* the box blit with the effects depth, on both outputs */
    const RdPipeKeyInt depthA = rd__present_depth_key(RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt depthB = rd__present_depth_key(RHI_FMT_BGRA8_UNORM);
    n = rd__add_pipe_key(out, max, n, &depthA);
    n = rd__add_pipe_key(out, max, n, &depthB);
    n = rd__add_pipe_key(out, max, n, &exact);
    return n;
}

/* ------------------------------------------------------------ staticBlur

 * fx_sprite_ps does the alpha test, DATE, the blend, PABE, FBA and COLCLAMP
 * itself on integers, so the key keeps only what the hardware does: the
 * colour mask (FRAME.FBMSK) and, with a depth target, the Z test and Z
 * write.  A Z test of ALWAYS without Z write needs no depth target and
 * binds none.  uintSrc: fx_sprite_u_ps, the twin that reads its textures
 * through their RGBA8_UINT views (rd_replay.c doBlurSpriteDraw). */
RdPipeKeyInt rd__blur_key(const RdStateBlock *s, RhiFormat colorFmt, RhiFormat depthFmt,
                          int *useDepth, int uintSrc)
{
    RdPipeKeyInt k =
        rd__post_key(RD_VS_FX_RECT, uintSrc ? RD_FS_FX_SPRITE_UINT : RD_FS_FX_SPRITE, colorFmt);
    k.gs.colorMask = s->ds.colorMask;
    const uint8_t ztst = s->ds.test.zte ? s->ds.test.ztst : RD_ZTST_ALWAYS;
    const int depth =
        depthFmt != RHI_FMT_UNKNOWN && (ztst != RD_ZTST_ALWAYS || s->ds.zwrite == RD_ZWRITE_ON);
    if (depth) {
        k.gs.ztst = ztst;
        k.gs.zwrite = s->ds.zwrite;
        k.depthFmt = (uint8_t)depthFmt;
    }
    if (useDepth) {
        *useDepth = depth;
    }
    return k;
}

uint32_t rd__enumerate_reachable_blur(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    /* staticBlur.c draws with Z ALWAYS or GEQUAL (TEST 0x30000, 0x50000,
     * 0x5000D, 0x30815, 0x34003, 0x34000, 0x3000C, 0) and ZMSK; the list-7
     * and list-8 defaults it can inherit add Z write on; colour mask full */
    static const uint8_t kZ[3] = {RD_ZTST_ALWAYS, RD_ZTST_GEQUAL, RD_ZTST_GREATER};
    RdStateBlock s;
    rd__reset_state_block(&s);
    s.ds.colorMask = 0xF;
    /* the UINT twins where a target can have the view (rd_core.c
     * rd__target_create_gpu: the profile's flags, the device's view) */
    const int twins = (g_rd.profile.uintViewWork || g_rd.profile.uintViewScene) && g_rd.hasDevice &&
                      rhi_limits()->uintView && g_rd.fs[RD_FS_FX_SPRITE_UINT].id != 0;
    for (int zw = 0; zw < 2; zw++) {
        for (int z = 0; z < 3; z++) {
            s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
            s.ds.test.ztst = kZ[z];
            s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
            for (int u = 0; u <= twins; u++) {
                const RdPipeKeyInt k =
                    rd__blur_key(&s, RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, NULL, u);
                n = rd__add_pipe_key(out, max, n, &k);
            }
        }
    }
    /* the fold of a blur pair (rd_replay_tiler.c rd__replay_blur_fold) where the profile
     * folds: Z ALWAYS, no Z write, no depth, colour mask whole */
    if (g_rd.profile.blurFold && g_rd.fs[RD_FS_FX_BLUR2].id) {
        s.ds.test = rd_test_from_gs(RD_TEST_Z_ALWAYS);
        s.ds.zwrite = RD_ZWRITE_OFF;
        const RdPipeKeyInt k = rd__blur_fold_key(&s, RHI_FMT_RGBA8_UNORM);
        n = rd__add_pipe_key(out, max, n, &k);
    }
    return rd__fx_enumerate(out, max, n); /* the frame-size post sprites (rd_fx_plan.c) */
}

RdPipeKeyInt rd__blur_fold_key(const RdStateBlock *s, RhiFormat colorFmt)
{
    RdPipeKeyInt k = rd__blur_key(s, colorFmt, RHI_FMT_UNKNOWN, NULL, 0);
    k.fs = RD_FS_FX_BLUR2;
    return k;
}
