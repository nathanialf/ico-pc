/* rd_pipeline.c: from the replay state block to a GPU pipeline.
 *
 * What is pipeline state and what is not
 * --------------------------------------
 * The shaders (port/shaders, docs/port/SHADERS.md) take the alpha test
 * (ATST, AREF, the AFAIL split pass), the texture function, TCC, TEXA, FBA,
 * PABE and the blend factor source as DrawCB uniforms.  So the pipeline key
 * (RdPipelineKey, rd_state.h) is normalised: atst is always ALWAYS, pabe and
 * fba are 0, afailSplit is 0, the effect of an AFAIL split pass on the
 * pipeline is carried by zwrite and colorMask, and blend holds one
 * representative mode per blend path (4 for the LERPs, 5 for the premultiplied
 * adds, 6 for the premultiplied subtracts).  DATE is recorded but 0 in
 * the key until the shaders read the DATE snapshot (requested in the R1b
 * report); a DATE draw is drawn without the test and reported once.
 *
 * Blend paths (RENDER_API.md section 3, SHADERS.md "Dual-source factor
 * above 1.0"): on UNORM targets fixed-point blend factors clamp to 1.0, so
 *   Cs*F + Cd and Cd - Cs*F (modes 0, 1, 5, 6) use DF_PREMUL: the shader
 *     writes min((Cs*F) >> 7, 255), the GS term, exact for F up to 255, and
 *     the blender adds or reverse-subtracts with factor ONE;
 *   the LERPs (2, 4, 7) use the dual-source factor F/128; F above 0x80 is
 *     clamped: FIX by rd (uniform), As by the hardware (the result is Cs
 *     where the GS would overshoot past it);
 *   Cd*FIX + Cs (mode 3) uses SRC1 as the destination factor, FIX clamped to
 *     0x80;
 *   the Ad modes (8-10, disc data only) use DST_ALPHA, which reads Ad/255,
 *     not Ad/128: half strength, untested (RENDER_API.md open item 6);
 *   Cd*As + Cd (mode 11, disc data only) needs a factor above 1.0 on Cd and
 *     is not representable: the draw leaves Cd unchanged (reported once).
 * The feedback passes do not use the hardware blender at all: RDC_EXACT_BLEND
 * runs blend_int on RGBA8_UINT copies (rd_replay.c).
 *
 * Z: the shaders map GS Z to depth = 1 - z / 2^24 (gs_z_to_depth), so a
 * larger GS Z is a smaller depth: GS GEQUAL is RHI_CMP_LEQUAL, GREATER is
 * LESS.
 */
#include <assert.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

typedef struct RdPipeEntry {
    RdPipeKeyInt key;
    RhiPipeline pipe;
} RdPipeEntry;

static RdPipeEntry s_cache[RD_PIPELINE_CACHE_MAX];

static uint32_t s_count;

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
    static const uint8_t kReg[RD_BLEND_COUNT] = {0x68, 0x62, 0x64, 0x61, 0x44, 0x48,
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
    const RdDrawState *d = &s->ds;
    const int hasDepth = depthFmt != RHI_FMT_UNKNOWN;
    uint8_t blend = d->abe ? d->blend : RD_BLEND_COUNT;
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
    if (d->test.date != RD_DATE_OFF) {
        rd__LogOnce(RD_ONCE_DATE, "DATE is not applied yet (needs the DATE snapshot in sprite_ps)");
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

    if (d->fba) {
        base.flags |= ICO_DF_FBA;
    }
    if (d->pabe) {
        base.flags |= ICO_DF_PABE;
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

static RhiBlendState blendState(RdBlendPath bp, uint8_t mask)
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
        b.srcColor = RHI_BF_SRC1_COLOR;
        b.dstColor = RHI_BF_ONE_MINUS_SRC1_COLOR;
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
        b.dstColor = RHI_BF_SRC1_COLOR;
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

static RhiPipeline createPipeline(const RdPipeKeyInt *k)
{
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw,
                                     k->fs == RD_FS_BLEND_INT ? g_rd.layoutInt : g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = g_rd.vs[k->vs];
    d.fragment = g_rd.fs[k->fs];
    d.layouts = layouts;
    d.layoutCount = 3;
    if (k->vs == RD_VS_SPRITE_UI || k->vs == RD_VS_SPRITE_WORLD) {
        d.vertexBindings = &vb;
        d.vertexBindingCount = 1;
        d.vertexAttrs = va;
        d.vertexAttrCount = 4;
    }
    d.topology = k->gs.prim == RD_PRIM_LINES ? RHI_TOPO_LINE_LIST : RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    RdBlendPath bp = k->colorFmt == RHI_FMT_RGBA8_UINT ? RD_BP_NONE : rd__BlendPath(k->gs.blend);
    d.blend[0] = blendState(bp, k->gs.colorMask);
    if (k->depthFmt != RHI_FMT_UNKNOWN) {
        d.depthStencil.depthTest = true;
        d.depthStencil.depthWrite = k->gs.zwrite == RD_ZWRITE_ON;
        d.depthStencil.depthCompare = depthCompare(k->gs.ztst);
    }
    d.colorFormats[0] = (RhiFormat)k->colorFmt;
    d.colorCount = 1;
    d.depthFormat = (RhiFormat)k->depthFmt;
    d.debugName = "rd";
    return rhi_CreatePipeline(&d);
}

RhiPipeline rd__GetPipeline(const RdPipeKeyInt *k)
{
    for (uint32_t i = 0; i < s_count; i++) {
        if (rd__PipeKeyEqual(&s_cache[i].key, k)) {
            return s_cache[i].pipe;
        }
    }
    if (!g_rd.hasDevice) {
        return (RhiPipeline){0};
    }
    assert(s_count < RD_PIPELINE_CACHE_MAX && "rd: pipeline cache past RD_PIPELINE_CACHE_MAX");
    if (s_count >= RD_PIPELINE_CACHE_MAX) {
        return (RhiPipeline){0};
    }
    RhiPipeline p = createPipeline(k);
    if (!p.id) {
        rd__Log("pipeline creation failed (program %u blend %u vs %u fs %u fmt %u/%u)",
                k->gs.program, k->gs.blend, k->vs, k->fs, k->colorFmt, k->depthFmt);
        return p;
    }
    s_cache[s_count].key = *k;
    s_cache[s_count].pipe = p;
    s_count++;
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
 *   4, 5, 6, 7 (the gif_SetAlpha literals, RENDER_API.md section 2); sprites
 *   and strips (triangles) and lines; SCENE (with depth) or a target
 *   without depth (DISPLAY, WORK, AA, FEED128, temporaries).
 *   WORLD screen prims (CPU-projected 2D: points and lines of list 2,
 *   glows): the same plus the depth-tested literals 0x50000, 0x5000D, the
 *   list 1/2 default 0x5140D (AFAIL split) and the list 4 default 0x5C000
 *   (DATE, normalised out).
 *   Blits: blit_vs/blit_ps into RGBA8 (presenter line doubling, headless
 *   output) and into the swapchain format (BGRA8 or RGBA8); blend_int into
 *   RGBA8_UINT (exact feedback blends).
 *
 * The mode 3 and Ad blends are reachable only through BGA lightning data,
 * drawn by rd_WorldPrims (wave 5), so they are not in the screen families.
 * Each enumerated state goes through rd__PlanScreenDraw, the function the
 * replayer uses, so the count is the count the cache would reach. */
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

uint32_t rd__EnumerateReachable(RdPipeKeyInt *out, uint32_t max)
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
                            RdStateBlock s;
                            rd__ResetStateBlock(&s);
                            s.ds.test = rd_TestFromGs(tests[t]);
                            s.ds.zwrite = zw ? RD_ZWRITE_ON : RD_ZWRITE_OFF;
                            s.ds.abe = kBlends[b] >= 0;
                            s.ds.blend = (uint8_t)(kBlends[b] >= 0 ? kBlends[b] : 0);
                            RdDrawPass dp[2];
                            int np = rd__PlanScreenDraw(&s, kPrims[p],
                                                        space ? RD_SPACE_WORLD : RD_SPACE_UI,
                                                        RHI_FMT_RGBA8_UNORM, kDepth[dz], dp);
                            for (int i = 0; i < np; i++) {
                                n = addKey(out, max, n, &dp[i].key);
                            }
                        }
                    }
                }
            }
        }
    }
    const RdPipeKeyInt blitA = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_RGBA8_UNORM);
    const RdPipeKeyInt blitB = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT, RHI_FMT_BGRA8_UNORM);
    const RdPipeKeyInt exact = rd__PostKey(RD_VS_BLEND_INT, RD_FS_BLEND_INT, RHI_FMT_RGBA8_UINT);
    n = addKey(out, max, n, &blitA);
    n = addKey(out, max, n, &blitB);
    n = addKey(out, max, n, &exact);
    return n;
}
