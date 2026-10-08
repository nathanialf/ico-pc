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
 * adds, 6 for the premultiplied subtracts).  DATE is a uniform too (wave 2:
 * DF_DATE/DF_DATM, sprite_ps tests the R8 snapshot rd_replay.c binds at t2),
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
 * Package AN-E, a device without dual-source blending (g_rd.noDual): only
 * the LERPs and Cd*FIX + Cs read the second output, so only they change
 * (rd__ExpandNoDual).  The draw becomes two: a colour pass whose c0.a is
 * the factor c1 would carry (the same float: F/128, or 1.0 / 0 for a PABE
 * pixel left unblended), blended SRC_ALPHA / ONE_MINUS_SRC_ALPHA (the LERPs)
 * or ONE / SRC_ALPHA (Cd*FIX + Cs) under the RGB part of the mask, then,
 * when the mask has A, an alpha pass writing the stored alpha (As, FBA's
 * MSB) with blending off, no Z write, under a Z test that admits the Z the
 * colour pass left.  The shaders are the *_nodual entries (ICO_NO_DUAL: c0
 * alone); every other path draws in one pass as before, with those entries.
 * Why the alpha is the one-pass alpha: blending never reads Ad on these
 * paths, so RGB does not depend on the alpha pass.  Without Z write the
 * depth buffer is the same for both passes, so the alpha pass keeps the Z
 * test and admits exactly the colour pass's fragments.  With Z write under
 * GS GEQUAL (LEQUAL here) each passing fragment stores its depth, so the
 * depth left is the least of the passing fragments', and the fragments the
 * alpha pass admits are those at that depth; all of them passed, and the
 * last of them is the last fragment that passed, whose alpha the one-pass
 * draw keeps.  ALWAYS admits every fragment in both passes.  GS GREATER
 * (LESS) with Z write takes LEQUAL in the alpha pass: a fragment whose Z
 * equals the stored Z failed the colour pass and passes the alpha pass,
 * the one deviation (logged once; no enumerated state draws it).  A pass
 * whose mask has no RGB blends nothing: one pass, blending off, its own Z.
 *
 * Z: the shaders map GS Z to depth = 1 - z / 2^24 (gs_z_to_depth), so a
 * larger GS Z is a smaller depth: GS GEQUAL is RHI_CMP_LEQUAL, GREATER is
 * LESS.
 */
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

typedef struct RdPipeEntry {
    RdPipeKeyInt key;
    RhiPipeline pipe;
} RdPipeEntry;

static RdPipeEntry s_cache[RD_PIPELINE_CACHE_MAX];

static uint32_t s_count;

/* F2 (C2): keys whose creation failed; looked up before a retry, so a
 * failed pipeline costs one creation attempt and one log line per session
 * (per cache clear), not two lines per draw. */
static RdPipeKeyInt s_failed[RD_PIPELINE_FAIL_MAX];

static uint32_t s_failedCount;

static bool s_fullLogged;

bool rd__PipeKeyEqual(const RdPipeKeyInt *a, const RdPipeKeyInt *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

RdBlendPath rd__BlendPath(uint8_t b)
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
uint32_t rd__AlphaRegister(uint8_t blend)
{
    /* mode 3 is 0x29 (alphaTable {1, 2, 2, 0}: A Cd, B 0, C FIX, D Cs); 0x61
     * until R5c, which is (Cd - Cs) FIX + Cd */
    static const uint8_t kReg[RD_BLEND_COUNT] = {0x68, 0x62, 0x64, 0x29, 0x44, 0x48,
                                                 0x42, 0x44, 0x58, 0x52, 0x54, 0x49};
    return blend < RD_BLEND_COUNT ? kReg[blend] : 0x44;
}

/* Modes that reach the hardware the same way share a pipeline: the key holds
 * one representative mode per blend path (the uniforms carry the rest). */
static uint8_t canonicalBlend(uint8_t blend)
{
    switch (rd__BlendPath(blend)) {
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

int rd__PlanScreenDraw(const RdStateBlock *s, uint8_t prim, uint8_t space, RhiFormat colorFmt,
                       RhiFormat depthFmt, RdDrawPass out[2])
{
    return rd__PlanScreenDrawEx(s, prim, 0, space, colorFmt, depthFmt, out);
}

/* Package AA1: aa1 is PRIM.AA1 on a line or triangle command (rd_replay.c
 * doScreen decides; points and sprites pass 0).  The key takes the AA1 bit
 * and the AA1 shaders, the topology becomes triangles (rd_replay.c draws an
 * antialiased line as quads), blending is on whatever PRIM.ABE says (the
 * ALPHA register's equation; with ABE 0 the coverage alpha replaces every
 * fragment's alpha, ICO_DF_AA1_FULL, else only an alpha of 0x80), and an
 * antialiased line writes no Z: all its pixels are edge pixels.  The edge geometry of a triangle draws with the
 * pass's key and zwrite off (rd_replay.c). */
int rd__PlanScreenDrawEx(const RdStateBlock *s, uint8_t prim, int aa1, uint8_t space,
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
    const RdBlendPath bp = rd__BlendPath(blend);
    if (bp == RD_BP_CD_KEEP) {
        rd__LogOnce(RD_ONCE_CD_KEEP, "blend mode 11 (Cd*As + Cd) is not representable; Cd kept");
    } else if (bp >= RD_BP_AD_ADD) {
        rd__LogOnce(RD_ONCE_AD, "Ad blend modes 8-10 use DST_ALPHA (Ad/255, not Ad/128)");
    }
    if (!d->colclamp && bp != RD_BP_NONE) {
        rd__LogOnce(RD_ONCE_COLCLAMP, "COLCLAMP 0 on a hardware-blended draw clamps instead");
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
    /* package R8: an R8 coverage texture (rd_CreateTextureR8) is drawn by
     * font_ps; the fragment shader is part of the key */
    if (d->texEnabled) {
        const RdTexRec *tr = rd__TexRec(s->tex);
        if (tr && tr->kind == RD_TEXKIND_IMAGE && tr->format == RD_TEXEL_R8) {
            k->fs = RD_FS_FONT;
        }
    }
    /* package AA1 (not for an R8 texture: no AA1 draw samples one) */
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

    /* package TEXA: TEXA per texel before the bilinear filter */
    if (k->fs == RD_FS_SPRITE && rd__TexaPerTexel(s)) {
        k->fs = RD_FS_SPRITE_TEXA;
    }

    if (d->fba) {
        base.flags |= ICO_DF_FBA;
    }
    if (d->pabe) {
        base.flags |= ICO_DF_PABE;
        if (bp == RD_BP_PREMUL_ADD || bp == RD_BP_PREMUL_REVSUB || bp >= RD_BP_AD_ADD) {
            rd__LogOnce(RD_ONCE_PABE, "PABE on an additive, subtractive or destination-alpha "
                                      "blend: pixels with As below 0x80 blend instead of "
                                      "writing Cs");
        }
    }
    if (bp == RD_BP_DST_FIX) {
        base.flags |= ICO_DF_C1_DST;
    }
    /* DATE (wave 2): a shader test against the R8 snapshot rd_replay.c binds
     * at t2; a uniform, so the key keeps date normalised to off */
    if (d->test.date == RD_DATE_DEST_ALPHA_0) {
        base.flags |= ICO_DF_DATE;
    } else if (d->test.date == RD_DATE_DEST_ALPHA_1) {
        base.flags |= ICO_DF_DATE | ICO_DF_DATM;
    }
    base.fix = d->blendFix;
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

int rd__TexaPerTexel(const RdStateBlock *s)
{
    const RdDrawState *d = &s->ds;
    if (!d->texEnabled || d->texa == RD_TEXA_80_80) {
        return 0; /* TEXA 80/80 gives every texel alpha 0x80: no order to keep */
    }
    if (d->magFilter != RD_FILTER_LINEAR && d->minFilter != RD_FILTER_LINEAR) {
        return 0; /* nearest: one texel, expanded after the fetch as before */
    }
    const RdTexRec *tr = rd__TexRec(s->tex);
    return tr && tr->src != RD_TEXSRC_RGBA32 &&
           !(tr->kind == RD_TEXKIND_IMAGE && tr->format == RD_TEXEL_R8);
}

/* Package RSMALL: a planned screen pass for a command whose prims carry
 * Q != 1: the STQ vertex shader of its
 * space and sprite_stq_ps.  Only the plain sprite pass converts (an R8
 * font texture and PRIM.AA1 keep their shaders); returns whether it did.
 * Package TEXA: a sprite_texa_ps pass converts too, and sprite_stq_ps
 * expands TEXA after the sampler. */
int rd__StqPass(RdDrawPass *dp)
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

/* Package OV: the presentation overlay's state (rd.h rd_OverlayPrims):
 * blend with ABE on, no Z test or write (and no depth target), no alpha
 * test, no DATE, FBA and PABE off, COLCLAMP on, all channels written. */
void rd__OverlayState(RdStateBlock *s, uint8_t blend)
{
    rd__ResetStateBlock(s);
    s->ds.test = rd_TestFromGs(RD_TEST_OFF);
    s->ds.zwrite = RD_ZWRITE_OFF;
    s->ds.abe = 1;
    s->ds.blend = blend;
}

RdPipeKeyInt rd__PostKey(RdVsId vs, RdFsId fs, RhiFormat colorFmt)
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

RdPipeKeyInt rd__PresentDepthKey(RhiFormat colorFmt)
{
    RdPipeKeyInt k = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT_DEPTH, colorFmt);
    k.depthFmt = RHI_FMT_D32F;
    k.gs.ztst = RD_ZTST_ALWAYS;
    k.gs.zwrite = RD_ZWRITE_ON;
    return k;
}

/* Package AN-E: the entries that call gs_dual_out (their *_nodual twins
 * output c0 alone). */
bool rd__FsHasNoDual(uint8_t fs)
{
    switch (fs) {
    case RD_FS_SPRITE:
    case RD_FS_SPRITE_TEXA:
    case RD_FS_SPRITE_AA1:
    case RD_FS_SPRITE_STQ:
    case RD_FS_FONT:
    case RD_FS_FOG:
    case RD_FS_VU:
    case RD_FS_VU_TEXA:
        return true;
    default:
        return false;
    }
}

static RdBlendPath keyBlendPath(const RdPipeKeyInt *k)
{
    return k->colorFmt == RHI_FMT_RGBA8_UINT ? RD_BP_NONE : rd__BlendPath(k->gs.blend);
}

int rd__ExpandNoDual(const RdDrawPass *in, int n, RdDrawPass out[4])
{
    int m = 0;
    for (int i = 0; i < n; i++) {
        RdDrawPass p = in[i];
        if (!g_rd.noDual || !rd__FsHasNoDual(p.key.fs)) {
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
                    rd__LogOnce(RD_ONCE_NODUAL_GREATER,
                                "blend: two-pass fallback under Z GREATER with Z write: a "
                                "fragment at the stored Z writes its alpha");
                }
            }
            out[m++] = a;
        }
    }
    return m;
}

bool rd__NoDualSecond(const RdDrawPass *ex, int i)
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
        /* package AN-E: the colour pass's c0.a is the factor */
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
        return RHI_CMP_LEQUAL;
    case RD_ZTST_GREATER:
        return RHI_CMP_LESS;
    default:
        return RHI_CMP_ALWAYS;
    }
}

/* Wave 4 (R4b): the stencil of the shadow count.
 * Volumes: both faces INCR_WRAP or DECR_WRAP where the depth test passes,
 * written through RD_SHADOW_STENCIL_MASK, so the stencil holds n mod 64 as
 * 4 n mod 256 wraps.  Resolve bit k: EQUAL to the reference 1 << k under
 * read mask 1 << k; resolve A: NOTEQUAL 0 under the count's mask.  The
 * resolve passes write no stencil. */
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

static RhiPipeline createPipeline(const RdPipeKeyInt *k)
{
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    /* package AA1: the same and the coverage */
    static const RhiVertexBinding vbAa1 = {0, sizeof(IcoSpriteAa1Vertex), false};
    static const RhiVertexAttr vaAa1[5] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
        {4, 0, RHI_VTX_F32x1, offsetof(IcoSpriteAa1Vertex, cov)},
    };
    /* package RSMALL: the same and Q */
    static const RhiVertexBinding vbStq = {0, sizeof(IcoSpriteStqVertex), false};
    static const RhiVertexAttr vaStq[5] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
        {4, 0, RHI_VTX_F32x1, offsetof(IcoSpriteStqVertex, q)},
    };
    const int vu = k->vs >= RD_VS_VU_FIRST && k->vs <= RD_VS_VU_LAST;
    /* wave 3 (R3ab): the VU program shaders read the stream, VuCB and
     * VuBoneCB from group 1 next to DrawCB, and have no vertex input */
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, vu ? g_rd.layoutVu : g_rd.layoutDraw,
                                     k->fs == RD_FS_BLEND_INT ? g_rd.layoutInt : g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = g_rd.vs[k->vs];
    /* package AN-E: the *_nodual twin (c0 alone) */
    d.fragment = k->gs.nodual && g_rd.fsNoDual[k->fs].id ? g_rd.fsNoDual[k->fs] : g_rd.fs[k->fs];
    d.layouts = layouts;
    d.layoutCount = 3;
    if (k->vs == RD_VS_SPRITE_UI || k->vs == RD_VS_SPRITE_WORLD) {
        d.vertexBindings = &vb;
        d.vertexBindingCount = 1;
        d.vertexAttrs = va;
        d.vertexAttrCount = 4;
    } else if (k->vs == RD_VS_SPRITE_AA1_UI || k->vs == RD_VS_SPRITE_AA1_WORLD) {
        d.vertexBindings = &vbAa1;
        d.vertexBindingCount = 1;
        d.vertexAttrs = vaAa1;
        d.vertexAttrCount = 5;
    } else if (k->vs == RD_VS_SPRITE_STQ_UI || k->vs == RD_VS_SPRITE_STQ_WORLD) {
        d.vertexBindings = &vbStq;
        d.vertexBindingCount = 1;
        d.vertexAttrs = vaStq;
        d.vertexAttrCount = 5;
    }
    d.topology = k->gs.prim == RD_PRIM_LINES ? RHI_TOPO_LINE_LIST : RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0] = blendState(keyBlendPath(k), k->gs.colorMask, k->gs.nodual);
    if (k->depthFmt != RHI_FMT_UNKNOWN) {
        d.depthStencil.depthTest = true;
        d.depthStencil.depthWrite = k->gs.zwrite == RD_ZWRITE_ON;
        d.depthStencil.depthCompare = depthCompare(k->gs.ztst);
        shadowStencil(k->gs.stencil, &d.depthStencil); /* wave 4 (R4b) */
    }
    d.colorFormats[0] = (RhiFormat)k->colorFmt;
    d.colorCount = 1;
    d.depthFormat = (RhiFormat)k->depthFmt;
    d.debugName = "rd";
    return rhi_CreatePipeline(&d);
}

/* Package P1: an open-addressed index over s_cache (slot + 1, 0 = empty),
 * so a draw's lookup is a hash and a compare, not a walk of every pipeline */
#define RD_PIPE_HASH (4 * RD_PIPELINE_CACHE_MAX)

static uint16_t s_hash[RD_PIPE_HASH];

static uint32_t keyHash(const RdPipeKeyInt *k)
{
    const uint8_t *b = (const uint8_t *)k;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < sizeof(*k); i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

RhiPipeline rd__GetPipeline(const RdPipeKeyInt *k)
{
    /* package AN-E: without dual-source blending a gs_dual_out entry draws
     * as its *_nodual twin; the planned passes come through rd__ExpandNoDual,
     * the single keys (the shadow count's volumes) are turned here */
    RdPipeKeyInt nk;
    if (g_rd.noDual && !k->gs.nodual && rd__FsHasNoDual(k->fs)) {
        nk = *k;
        nk.gs.nodual = 1;
        const RdBlendPath bp = keyBlendPath(k);
        if (bp == RD_BP_LERP || bp == RD_BP_DST_FIX) {
            rd__LogOnce(RD_ONCE_NODUAL_KEY,
                        "blend: a blended key (program %u blend %u fs %u) was not split into "
                        "the two-pass fallback; its alpha is the factor",
                        k->gs.program, k->gs.blend, k->fs);
        }
        k = &nk;
    }
    uint32_t at = keyHash(k) % RD_PIPE_HASH;
    for (; s_hash[at] != 0; at = (at + 1) % RD_PIPE_HASH) {
        if (rd__PipeKeyEqual(&s_cache[s_hash[at] - 1].key, k)) {
            return s_cache[s_hash[at] - 1].pipe;
        }
    }
    if (!g_rd.hasDevice) {
        return (RhiPipeline){0};
    }
    for (uint32_t i = 0; i < s_failedCount; i++) {
        if (rd__PipeKeyEqual(&s_failed[i], k)) {
            return (RhiPipeline){0};
        }
    }
    /* no assert: NDEBUG is never defined (CMakeLists.txt), so an assert here
     * would abort a release build; the tests hold the reachable set under
     * RD_PIPELINE_REACHABLE_MAX instead */
    if (s_count >= RD_PIPELINE_CACHE_MAX) {
        if (!s_fullLogged) {
            s_fullLogged = true;
            rd__Log("pipeline cache full (%d keys): new keys are not drawn (program %u blend %u "
                    "vs %u fs %u)",
                    RD_PIPELINE_CACHE_MAX, k->gs.program, k->gs.blend, k->vs, k->fs);
        }
        return (RhiPipeline){0};
    }
    RhiPipeline p = createPipeline(k);
    if (!p.id) {
        if (s_failedCount < RD_PIPELINE_FAIL_MAX) {
            s_failed[s_failedCount++] = *k;
            rd__Log("pipeline creation failed (program %u blend %u vs %u fs %u fmt %u/%u); "
                    "draws with this key are skipped",
                    k->gs.program, k->gs.blend, k->vs, k->fs, k->colorFmt, k->depthFmt);
        }
        return p;
    }
    s_cache[s_count].key = *k;
    s_cache[s_count].pipe = p;
    s_count++;
    s_hash[at] = (uint16_t)s_count; /* at: the empty slot the probe ended on */
    g_rd.stats.pipelineCreates++;
    return p;
}

void rd__PipelineCacheClear(void)
{
    if (g_rd.hasDevice) {
        for (uint32_t i = 0; i < s_count; i++) {
            rhi_DestroyPipeline(s_cache[i].pipe);
        }
    }
    s_count = 0;
    s_failedCount = 0;
    s_fullLogged = false;
    memset(s_hash, 0, sizeof(s_hash));
}

uint32_t rd_PrecreatePipelines(void)
{
    static RdPipeKeyInt keys[RD_PIPELINE_CACHE_MAX];
    if (!g_rd.hasDevice) {
        /* P1: said, so a call before rd_Init does not pass unnoticed */
        rd__Log("rd_PrecreatePipelines without a device (before rd_Init?): nothing created");
        return 0;
    }
    const double t0 = rd__NowMs();
    const uint32_t before = s_count;
    const uint32_t n = rd__EnumerateReachable(keys, RD_PIPELINE_CACHE_MAX);
    uint32_t failed = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!rd__GetPipeline(&keys[i]).id) {
            failed++;
        }
    }
    const double ms = rd__NowMs() - t0;
    rd__Log("pipelines: %u of the reachable set's %u created at start-up in %.1f ms (%u failed)",
            s_count - before, n, ms, failed);
    return s_count - before;
}

uint32_t rd__PipelineCount(void)
{
    return s_count;
}

const RdPipeKeyInt *rd__PipelineKeyAt(uint32_t i)
{
    return i < s_count ? &s_cache[i].key : NULL;
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
 *   The DATE snapshot (wave 2): blit_vs/date_snap_ps into R8.
 *   PRIM.AA1 lines and triangles (package AA1): WORLD screen prims through
 *   sprite_aa1_world_vs / sprite_aa1_ps, the states stormTest.c and puddle.c
 *   draw them under (rd__EnumerateReachableScreen lists them).
 *   Blits: blit_vs/blit_ps into RGBA8 (presenter line doubling, headless
 *   output) and into the swapchain format (BGRA8 or RGBA8); blend_int into
 *   RGBA8_UINT (exact feedback blends).
 *
 * The mode 3 and Ad blends are reachable only through BGA lightning data,
 * drawn by rd_WorldPrims (wave 5), so they are not in the screen families.
 * Each enumerated state goes through rd__PlanScreenDraw, the function the
 * replayer uses, so the count is the count the cache would reach.
 *
 * Wave 3 (R3ab): rd__EnumerateReachable adds the VU program families
 * (rd__EnumerateReachableVu, rd_mesh.c) to these; the screen and post set
 * alone stays rd__EnumerateReachableScreen. */
static uint32_t addKey(RdPipeKeyInt *out, uint32_t max, uint32_t n, const RdPipeKeyInt *k)
{
    for (uint32_t i = 0; i < n && i < max; i++) {
        if (rd__PipeKeyEqual(&out[i], k)) {
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
    uint32_t n = rd__EnumerateReachableVu(out, max, rd__EnumerateReachableScreen(out, max));
    n = rd__EnumerateReachableShadow(out, max, n);  /* wave 4 (R4b) */
    n = rd__EnumerateReachableFog(out, max, n);     /* wave 4 (R4c) */
    n = rd__EnumerateReachableWater(out, max, n);   /* wave 5 (R5b) */
    n = rd__EnumerateReachableCrt(out, max, n);     /* package CRT */
    return rd__EnumerateReachableBlur(out, max, n); /* wave 5 (R5a) */
}

/* Package AN-E: in the two-pass fallback every key is what rd__ExpandNoDual
 * makes of it (the families above plan with dual-source blending), so the
 * set precreated is the set the draws reach (rd_mesh.c's VU and rd_water.c's
 * families among them). */
uint32_t rd__EnumerateReachable(RdPipeKeyInt *out, uint32_t max)
{
    if (!g_rd.noDual) {
        return enumerateAll(out, max);
    }
    static RdPipeKeyInt raw[RD_PIPELINE_CACHE_MAX];
    const uint32_t nr = enumerateAll(raw, RD_PIPELINE_CACHE_MAX);
    uint32_t n = 0;
    for (uint32_t i = 0; i < nr && i < RD_PIPELINE_CACHE_MAX; i++) {
        RdDrawPass in, ex[4];
        memset(&in, 0, sizeof(in));
        in.key = raw[i];
        const int m = rd__ExpandNoDual(&in, 1, ex);
        for (int j = 0; j < m; j++) {
            n = addKey(out, max, n, &ex[j].key);
        }
    }
    return n;
}

/* package CRT (rd_crt.c): the glow passes into RGBA16F, the composite on
 * the headless output (RGBA8) and the swapchain (BGRA8); the virtual
 * source's box reduction is the shadow family's key */
uint32_t rd__EnumerateReachableCrt(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    const RdPipeKeyInt keys[4] = {rd__PostKey(RD_VS_CRT, RD_FS_CRT_BLOOM, RHI_FMT_RGBA16F),
                                  rd__PostKey(RD_VS_CRT, RD_FS_CRT_BLUR, RHI_FMT_RGBA16F),
                                  rd__PostKey(RD_VS_CRT, RD_FS_CRT, RHI_FMT_RGBA8_UNORM),
                                  rd__PostKey(RD_VS_CRT, RD_FS_CRT, RHI_FMT_BGRA8_UNORM)};
    for (int i = 0; i < 4; i++) {
        n = addKey(out, max, n, &keys[i]);
    }
    return n;
}

/* ----------------------------------------------------- fog (wave 4, R4c) */

int rd__FogPlan(const RdStateBlock *s, RhiFormat colorFmt, RdDrawPass out[2])
{
    const int np = rd__PlanScreenDraw(s, RD_PRIM_TRIANGLES, RD_SPACE_FULLSCREEN, colorFmt,
                                      RHI_FMT_UNKNOWN, out);
    for (int i = 0; i < np; i++) {
        out[i].key.gs.program = RD_PROG_POST;
        out[i].key.fs = RD_FS_FOG;
    }
    return np;
}

uint32_t rd__EnumerateReachableFog(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    /* fog_DrawFog: TEST 0x50000, ZBUF with ZMSK, ALPHA 0x44, PRIM 0x156 (ABE)
     * into SCENE (RGBA8) */
    RdStateBlock s;
    rd__ResetStateBlock(&s);
    s.ds.test = rd_TestFromGs(RD_TEST_Z_GEQUAL);
    s.ds.zwrite = RD_ZWRITE_OFF;
    s.ds.abe = 1;
    s.ds.blend = RD_BLEND_LERP_AS;
    RdDrawPass dp[2];
    const int np = rd__FogPlan(&s, RHI_FMT_RGBA8_UNORM, dp);
    for (int i = 0; i < np; i++) {
        n = addKey(out, max, n, &dp[i].key);
    }
    return n;
}

/* ------------------------------------------------- shadows (wave 4, R4b) */

RdPipeKeyInt rd__ShadowVolumeKey(const RdStateBlock *s, RhiFormat colorFmt, int decr)
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

/* Package RSMALL: the box reduction of the scaled count to the GS size
 * (rd_replay.c shadowReduce): RGBA8 like the count. */
RdPipeKeyInt rd__ShadowReduceKey(void)
{
    return rd__PostKey(RD_VS_BLIT, RD_FS_BOX_REDUCE, RHI_FMT_RGBA8_UNORM);
}

RdPipeKeyInt rd__ShadowResolveKey(int pass)
{
    RdPipeKeyInt k = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_RGBA8_UNORM);
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

uint32_t rd__EnumerateReachableShadow(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    RdStateBlock s;
    rd__ResetStateBlock(&s);
    s.ds.test = rd_TestFromGs(RD_TEST_Z_GEQUAL); /* shadow_Reset's TEST 0x50000 */
    for (int decr = 0; decr < 2; decr++) {
        const RdPipeKeyInt k = rd__ShadowVolumeKey(&s, RHI_FMT_RGBA8_UNORM, decr);
        n = addKey(out, max, n, &k);
    }
    for (int p = 0; p < RD_SHADOW_RESOLVE_PASSES; p++) {
        const RdPipeKeyInt k = rd__ShadowResolveKey(p);
        n = addKey(out, max, n, &k);
    }
    const RdPipeKeyInt kr = rd__ShadowReduceKey(); /* package RSMALL */
    n = addKey(out, max, n, &kr);
    return n;
}

uint32_t rd__EnumerateReachableScreen(RdPipeKeyInt *out, uint32_t max)
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
                                rd__ResetStateBlock(&s);
                                s.ds.test = rd_TestFromGs(tests[t]);
                                s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                                s.ds.abe = kBlends[b] >= 0;
                                s.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
                                s.ds.colorMask = m ? 0x7 : 0xF;
                                RdDrawPass dp[2];
                                int np = rd__PlanScreenDraw(&s, kPrims[p],
                                                            space ? RD_SPACE_WORLD : RD_SPACE_UI,
                                                            RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
                                for (int i = 0; i < np; i++) {
                                    n = addKey(out, max, n, &dp[i].key);
                                    /* package TEXA: the same state on a 24-
                                     * or 16-bit texture under AEM */
                                    RdPipeKeyInt kt = dp[i].key;
                                    kt.fs = RD_FS_SPRITE_TEXA;
                                    n = addKey(out, max, n, &kt);
                                }
                                /* package RSMALL: a textured STQ triangle
                                 * command with Q != 1 (the lightning's strips,
                                 * raw GIF writes: WORLD space) */
                                if (space && kPrims[p] == RD_PRIM_TRIANGLES) {
                                    for (int i = 0; i < np; i++) {
                                        if (rd__StqPass(&dp[i])) {
                                            n = addKey(out, max, n, &dp[i].key);
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
    /* package AA1: the PRIM.AA1 lines and triangles, WORLD space (the storm's
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
                        rd__ResetStateBlock(&s);
                        s.ds.test = rd_TestFromGs(kAa1Tests[t]);
                        s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                        s.ds.abe = 1;
                        s.ds.blend = (uint8_t)kAa1Blends[b];
                        s.aa1 = 1;
                        RdDrawPass dp[2];
                        const int np = rd__PlanScreenDrawEx(&s, kPrims[p], 1, RD_SPACE_WORLD,
                                                            RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
                        for (int i = 0; i < np; i++) {
                            n = addKey(out, max, n, &dp[i].key);
                        }
                    }
                }
            }
        }
    }
    /* package OV: the presentation overlay on the headless output and the
     * swapchain (sprites and triangles; the popups' blend and the glow's) */
    static const uint8_t kOverlayBlends[] = {RD_BLEND_LERP_AS, RD_BLEND_CS_AS_ADD_CD};
    static const RhiFormat kOutFormats[] = {RHI_FMT_RGBA8_UNORM, RHI_FMT_BGRA8_UNORM};
    for (size_t f = 0; f < 2; f++) {
        for (size_t b = 0; b < 2; b++) {
            RdStateBlock s;
            rd__OverlayState(&s, kOverlayBlends[b]);
            RdDrawPass dp[2];
            const int np = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI, kOutFormats[f],
                                              RHI_FMT_UNKNOWN, dp);
            for (int i = 0; i < np; i++) {
                n = addKey(out, max, n, &dp[i].key);
                /* package R8: the font atlas on the overlay (font_ps) */
                dp[i].key.fs = RD_FS_FONT;
                n = addKey(out, max, n, &dp[i].key);
            }
        }
    }
    /* package R8: the port's text in the frame (port/ui/font.c setState:
     * TEST 0x30000, no Z write, ALPHA 0x44 or 0x48; UI_KEEP_STATE keeps the
     * layout packet's, which differs only in the alpha test, a uniform)
     * through font_ps, with and without the depth target */
    for (size_t b = 0; b < 2; b++) {
        for (size_t dz = 0; dz < 2; dz++) {
            RdStateBlock s;
            rd__ResetStateBlock(&s);
            s.ds.test = rd_TestFromGs(RD_TEST_Z_ALWAYS);
            s.ds.zwrite = RD_ZWRITE_OFF;
            s.ds.abe = 1;
            s.ds.blend = kOverlayBlends[b];
            RdDrawPass dp[2];
            const int np = rd__PlanScreenDraw(&s, RD_PRIM_TRIANGLES, RD_SPACE_UI,
                                              RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
            for (int i = 0; i < np; i++) {
                dp[i].key.fs = RD_FS_FONT;
                n = addKey(out, max, n, &dp[i].key);
                dp[i].key.gs.colorMask = 0x7; /* under the dark volume's FBMSK, as above */
                n = addKey(out, max, n, &dp[i].key);
            }
        }
    }
    const RdPipeKeyInt blitA = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt blitB = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_BGRA8_UNORM);
    const RdPipeKeyInt exact = rd__PostKey(RD_VS_BLEND_INT, RD_FS_BLEND_INT, RHI_FMT_RGBA8_UINT);
    const RdPipeKeyInt dateSnap = rd__PostKey(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
    n = addKey(out, max, n, &dateSnap);
    n = addKey(out, max, n, &blitA);
    n = addKey(out, max, n, &blitB);
    /* v0.4.1 (R1): the box blit with the effects depth, on both outputs */
    const RdPipeKeyInt depthA = rd__PresentDepthKey(RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt depthB = rd__PresentDepthKey(RHI_FMT_BGRA8_UNORM);
    n = addKey(out, max, n, &depthA);
    n = addKey(out, max, n, &depthB);
    n = addKey(out, max, n, &exact);
    return n;
}

/* ------------------------------------------------ staticBlur (wave 5, R5a)
 * fx_sprite_ps does the alpha test, DATE, the blend, PABE, FBA and COLCLAMP
 * itself on integers, so the key keeps only what the hardware does: the
 * colour mask (FRAME.FBMSK) and, with a depth target, the Z test and Z
 * write.  A Z test of ALWAYS without Z write needs no depth target and
 * binds none. */
RdPipeKeyInt rd__BlurKey(const RdStateBlock *s, RhiFormat colorFmt, RhiFormat depthFmt,
                         int *useDepth)
{
    RdPipeKeyInt k = rd__PostKey(RD_VS_FX_RECT, RD_FS_FX_SPRITE, colorFmt);
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

uint32_t rd__EnumerateReachableBlur(RdPipeKeyInt *out, uint32_t max, uint32_t n)
{
    /* staticBlur.c draws with Z ALWAYS or GEQUAL (TEST 0x30000, 0x50000,
     * 0x5000D, 0x30815, 0x34003, 0x34000, 0x3000C, 0) and ZMSK; the list-7
     * and list-8 defaults it can inherit add Z write on; colour mask full */
    static const uint8_t kZ[3] = {RD_ZTST_ALWAYS, RD_ZTST_GEQUAL, RD_ZTST_GREATER};
    RdStateBlock s;
    rd__ResetStateBlock(&s);
    s.ds.colorMask = 0xF;
    for (int zw = 0; zw < 2; zw++) {
        for (int z = 0; z < 3; z++) {
            s.ds.test = rd_TestFromGs(RD_TEST_Z_ALWAYS);
            s.ds.test.ztst = kZ[z];
            s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
            const RdPipeKeyInt k = rd__BlurKey(&s, RHI_FMT_RGBA8_UNORM, RHI_FMT_D32F_S8, NULL);
            n = addKey(out, max, n, &k);
        }
    }
    return n;
}
