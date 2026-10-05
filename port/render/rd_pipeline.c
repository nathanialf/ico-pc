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
 * adds, 6 for the premultiplied subtracts).  DATE is a uniform too (wave 2:
 * DF_DATE/DF_DATM, sprite_ps tests the R8 snapshot rd_replay.c binds at t2),
 * so date stays 0 in the key.
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

/* Wave 4 (R4b): the stencil of the shadow count (RENDER_API.md section 14).
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
    const int vu = k->vs >= RD_VS_VU_FIRST && k->vs <= RD_VS_VU_LAST;
    /* wave 3 (R3ab): the VU program shaders read the stream, VuCB and
     * VuBoneCB from group 1 next to DrawCB, and have no vertex input */
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, vu ? g_rd.layoutVu : g_rd.layoutDraw,
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
 *   4, 5, 6, 7 (the gif_SetAlpha literals, RENDER_API.md section 2); sprites
 *   and strips (triangles) and lines; SCENE (with depth) or a target
 *   without depth (DISPLAY, WORK, AA, FEED128, temporaries).
 *   WORLD screen prims (CPU-projected 2D: points and lines of list 2,
 *   glows): the same plus the depth-tested literals 0x50000, 0x5000D, the
 *   list 1/2 default 0x5140D (AFAIL split) and the list 4 default 0x5C000
 *   (DATE, normalised out).
 *   The DATE snapshot (wave 2): blit_vs/date_snap_ps into R8.
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

uint32_t rd__EnumerateReachable(RdPipeKeyInt *out, uint32_t max)
{
    uint32_t n = rd__EnumerateReachableVu(out, max, rd__EnumerateReachableScreen(out, max));
    n = rd__EnumerateReachableShadow(out, max, n);  /* wave 4 (R4b) */
    n = rd__EnumerateReachableFog(out, max, n);     /* wave 4 (R4c) */
    n = rd__EnumerateReachableWater(out, max, n);   /* wave 5 (R5b) */
    return rd__EnumerateReachableBlur(out, max, n); /* wave 5 (R5a) */
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
    const RdPipeKeyInt dateSnap = rd__PostKey(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
    n = addKey(out, max, n, &dateSnap);
    n = addKey(out, max, n, &blitA);
    n = addKey(out, max, n, &blitB);
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
