/* rhi_test_common.c: headless rendering through port/rhi/rhi.h on any
 * backend, checked texel by texel (generalised from rhi_vk_test.c in
 * renderer wave 6, R6c; the expected values are unchanged).
 *
 * A 64x32 RGBA8 target with a D32F+S8 depth-stencil is split into eight
 * 16x16 cells, each exercising one feature the GS emulation needs:
 *
 *   0  dual-source blend, SRC1_COLOR factors
 *   1  dual-source blend, SRC1_ALPHA factor with an RGB-only colour mask
 *   2  stencil DECR_WRAP 0 -> 255, then EQUAL 255
 *   3  stencil REPLACE 255, INCR_WRAP -> 0, then EQUAL 0
 *   4  reversed-Z: depth cleared to 0, GEQUAL with writes
 *   5  colour mask R|A
 *   6  texture uploaded by copy, sampled nearest, drawn indexed
 *   7  untouched (clear colour)
 *
 * Plus an RGBA8_UINT target (integer clear and integer output), R8 texture
 * copies (a whole copy, then a second copy over part of it: same-state copy
 * ordering), a device-local vertex buffer filled by rhi_CmdCopyBuffer, depth
 * readback, and three frames to cycle the frame slots.  Every expected
 * value is exact.  Buffer-to-texture copies use rhi_Limits()'s pitch and
 * offset alignment (1/4 on Vulkan, 256/512 on D3D12). */
#include "rhi_test_common.h"
#include "rhi.h"
#include "rhi_test_dxil.h"
#include "rhi_test_spv.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

FILE *g_rhiTestLog;

void rhi_test_Log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    if (g_rhiTestLog && g_rhiTestLog != stdout) {
        va_start(ap, fmt);
        vfprintf(g_rhiTestLog, fmt, ap);
        va_end(ap);
        fflush(g_rhiTestLog);
    }
}

#define W 64
#define H 32
#define CELL 16

typedef struct Vtx {
    float x, y, z;
    uint8_t rgba[4];
    float u, v;
} Vtx;

typedef struct TestCB {
    float src1[4];
    uint32_t uintValue[4];
} TestCB;

static int failures;
static const char *s_label = "rhi_test";

static void fail(const char *fmt, int a, int b, int c, const uint8_t *got, const uint8_t *want)
{
    char where[96];
    snprintf(where, sizeof(where), fmt, a, b, c);
    rhi_test_Log("FAIL %s: got %3u %3u %3u %3u, expected %3u %3u %3u %3u\n", where, got[0], got[1],
                 got[2], got[3], want[0], want[1], want[2], want[3]);
    failures++;
}

/* Six vertices of a quad over pixels [x0,x1) x [y0,y1) of a w x h target,
 * in D3D-style NDC (y up); the backend's viewport maps it y-down. */
static void quad(Vtx *v, float x0, float y0, float x1, float y1, float w, float h, float z,
                 uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    float nx0 = x0 / w * 2.0f - 1.0f, nx1 = x1 / w * 2.0f - 1.0f;
    float ny0 = 1.0f - y0 / h * 2.0f, ny1 = 1.0f - y1 / h * 2.0f;
    const float px[6] = {nx0, nx1, nx0, nx0, nx1, nx1};
    const float py[6] = {ny0, ny0, ny1, ny1, ny0, ny1};
    const float pu[6] = {0, 1, 0, 0, 1, 1};
    const float pv[6] = {0, 0, 1, 1, 0, 1};
    for (int i = 0; i < 6; i++) {
        v[i] = (Vtx){px[i], py[i], z, {r, g, b, a}, pu[i], pv[i]};
    }
}

static void cellQuad(Vtx *v, int cell, float z, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    float x = (float)((cell % 4) * CELL), y = (float)((cell / 4) * CELL);
    quad(v, x, y, x + CELL, y + CELL, W, H, z, r, g, b, a);
}

typedef struct Ctx {
    RhiBindGroupLayout l0, l1, l2;
    RhiShader vs, psDual, psColor, psTex, psUint;
} Ctx;

static RhiPipeline makePipeline(const Ctx *c, RhiShader ps, RhiFormat color, RhiFormat depth,
                                const RhiBlendState *blend, const RhiDepthStencilState *ds,
                                const char *name)
{
    static const RhiVertexBinding vb = {0, sizeof(Vtx), false};
    static const RhiVertexAttr va[3] = {
        {0, 0, RHI_VTX_F32x3, offsetof(Vtx, x)},
        {1, 0, RHI_VTX_U8x4_UNORM, offsetof(Vtx, rgba)},
        {2, 0, RHI_VTX_F32x2, offsetof(Vtx, u)},
    };
    RhiBindGroupLayout layouts[3] = {c->l0, c->l1, c->l2};
    RhiPipelineDesc d = {0};
    d.vertex = c->vs;
    d.fragment = ps;
    d.layouts = layouts;
    d.layoutCount = 3;
    d.vertexBindings = &vb;
    d.vertexBindingCount = 1;
    d.vertexAttrs = va;
    d.vertexAttrCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0] = *blend;
    if (ds) {
        d.depthStencil = *ds;
    }
    d.colorFormats[0] = color;
    d.colorCount = 1;
    d.depthFormat = depth;
    d.debugName = name;
    RhiPipeline p = rhi_CreatePipeline(&d);
    if (!p.id) {
        rhi_test_Log("FAIL pipeline %s not created\n", name);
        failures++;
    }
    return p;
}

static RhiShader makeShader(RhiShaderStage stage, const void *code, size_t size, const char *entry)
{
    RhiShaderDesc d = {stage, code, size, entry, entry};
    RhiShader s = rhi_CreateShader(&d);
    if (!s.id) {
        rhi_test_Log("FAIL shader %s not created\n", entry);
        failures++;
    }
    return s;
}

static void expectRGBA(const uint8_t *img, uint32_t pitch, int x, int y, uint8_t r, uint8_t g,
                       uint8_t b, uint8_t a, int frame)
{
    const uint8_t *p = img + (size_t)y * pitch + (size_t)x * 4;
    const uint8_t want[4] = {r, g, b, a};
    if (memcmp(p, want, 4) != 0) {
        fail("frame %d pixel (%d,%d)", frame, x, y, p, want);
    }
}

/* every pixel of a cell must hold the same value */
static void expectCell(const uint8_t *img, uint32_t pitch, int cell, uint8_t r, uint8_t g,
                       uint8_t b, uint8_t a, int frame)
{
    int x0 = (cell % 4) * CELL, y0 = (cell / 4) * CELL;
    int before = failures;
    for (int y = y0; y < y0 + CELL && failures == before; y++) {
        for (int x = x0; x < x0 + CELL && failures == before; x++) {
            expectRGBA(img, pitch, x, y, r, g, b, a, frame);
        }
    }
    if (failures != before) {
        rhi_test_Log("     (cell %d)\n", cell);
    }
}

/* Texture packs: block-compressed textures.  A BC1 texture with a full
 * chain (8x8, 4x4 and the levels under one block, 2x2 and 1x1, which copy
 * their real size) and a BC3 4x4, uploaded by buffer copies at the
 * backend's pitch and offset alignment, each level drawn into its own
 * 16x16 cell through a nearest sampler pinned to it (minLod = maxLod) and
 * read back exactly: every block is one solid colour, so the decode is
 * exact on any implementation.  Skipped (not failed) on a device without
 * BC. */
static void bcBlock(uint8_t *o, uint16_t c565, int bc3, uint8_t alpha)
{
    if (bc3) {
        /* alpha0 = alpha1 = alpha, every 3-bit index 0 */
        memset(o, 0, 8);
        o[0] = o[1] = alpha;
        o += 8;
    }
    /* colour0 = colour1, every 2-bit index 0: colour0 */
    o[0] = o[2] = (uint8_t)(c565 & 0xFF);
    o[1] = o[3] = (uint8_t)(c565 >> 8);
    o[4] = o[5] = o[6] = o[7] = 0;
}

static void bcCell(const Ctx *c)
{
    const RhiLimits *lim = rhi_Limits();
    if (!lim->bcTextures) {
        rhi_test_Log("SKIP %s: BC cell (no BC formats on this device)\n", s_label);
        return;
    }

    enum { LEVELS = 4, BW = 5 * CELL, BH = CELL };

    static const uint16_t colour[LEVELS + 1] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0xF81F};
    static const uint8_t want[LEVELS + 1][4] = {{255, 0, 0, 255},
                                                {0, 255, 0, 255},
                                                {0, 0, 255, 255},
                                                {255, 255, 255, 255},
                                                {255, 0, 255, 128}};
    const RhiFormat CF = RHI_FMT_RGBA8_UNORM;
    const uint32_t pa = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1u;
    uint32_t oa = lim->copyOffsetAlign ? lim->copyOffsetAlign : 1u;
    oa = oa < 16u ? 16u : oa; /* and a whole block */

    RhiTexture tgt = rhi_CreateTexture(
        &(RhiTextureDesc){BW, BH, 1, CF, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "bc target"});
    RhiTexture bc1 = rhi_CreateTexture(&(RhiTextureDesc){
        8, 8, LEVELS, RHI_FMT_BC1_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "bc1"});
    RhiTexture bc3 = rhi_CreateTexture(
        &(RhiTextureDesc){4, 4, 1, RHI_FMT_BC3_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "bc3"});
    /* a block texture is never a target */
    RhiTexture bad = rhi_CreateTexture(
        &(RhiTextureDesc){4, 4, 1, RHI_FMT_BC1_UNORM, RHI_TEX_RENDER_TARGET, "bc target?"});
    RhiBuffer ring = rhi_CreateBuffer(
        &(RhiBufferDesc){65536, RHI_BUF_VERTEX | RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "bc ring"});
    uint8_t *map = ring.id ? rhi_MapBuffer(ring) : NULL;
    RhiBlendState opaque = {.writeMask = 0xF};
    RhiPipeline pipe = makePipeline(c, c->psTex, CF, RHI_FMT_UNKNOWN, &opaque, NULL, "bc");
    RhiSampler smp[LEVELS];
    int ok = tgt.id && bc1.id && bc3.id && map && pipe.id;
    for (int l = 0; l < LEVELS; l++) {
        RhiSamplerDesc sd = {RHI_FILTER_NEAREST,
                             RHI_FILTER_NEAREST,
                             RHI_FILTER_NEAREST,
                             RHI_WRAP_CLAMP,
                             RHI_WRAP_CLAMP,
                             1.0f,
                             0.0f,
                             (float)l,
                             (float)l};
        smp[l] = rhi_CreateSampler(&sd);
        ok = ok && smp[l].id;
    }
    if (bad.id) {
        rhi_test_Log("FAIL %s: a BC render target was created\n", s_label);
        failures++;
        rhi_DestroyTexture(bad);
    }
    if (!ok) {
        rhi_test_Log("FAIL %s: BC cell resources\n", s_label);
        failures++;
        return;
    }

    /* the quads, then the levels' blocks */
    Vtx v[(LEVELS + 1) * 6];
    for (int i = 0; i <= LEVELS; i++) {
        quad(&v[i * 6], (float)(i * CELL), 0, (float)(i * CELL + CELL), CELL, BW, BH, 0.0f, 255,
             255, 255, 255);
    }
    memcpy(map, v, sizeof(v));
    uint64_t off = 4096;
    RhiCommandList cl = rhi_BeginCommands();
    RhiTextureBarrier up[3] = {{bc1, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                               {bc3, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                               {tgt, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET}};
    rhi_CmdBarrier(cl, up, 3);
    for (int l = 0; l <= LEVELS; l++) {
        const int isBc3 = l == LEVELS;
        const uint32_t w = isBc3 ? 4u : 8u >> l, h = w;
        const uint32_t bb = isBc3 ? 16u : 8u;
        const uint32_t bw = (w + 3u) / 4u, bh = (h + 3u) / 4u;
        const uint32_t pitch = (bw * bb + pa - 1u) / pa * pa;
        off = (off + oa - 1u) / oa * oa;
        for (uint32_t by = 0; by < bh; by++) {
            for (uint32_t bx = 0; bx < bw; bx++) {
                bcBlock(map + off + (uint64_t)by * pitch + (uint64_t)bx * bb, colour[l], isBc3,
                        0x80);
            }
        }
        rhi_CmdCopyBufferToTexture(cl, ring, off, pitch, isBc3 ? bc3 : bc1,
                                   isBc3 ? 0u : (uint32_t)l, (RhiRect){0, 0, w, h});
        off += (uint64_t)pitch * bh;
    }
    RhiTextureBarrier rd[2] = {{bc1, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
                               {bc3, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ}};
    rhi_CmdBarrier(cl, rd, 2);
    RhiRenderPassDesc rp = {0};
    rp.color[0] = (RhiColorAttachment){tgt, RHI_LOAD_CLEAR, {0.0f, 0.0f, 0.0f, 0.0f}};
    rp.colorCount = 1;
    rp.width = BW;
    rp.height = BH;
    rhi_CmdBeginRenderPass(cl, &rp);
    rhi_CmdSetPipeline(cl, pipe);
    rhi_CmdSetVertexBuffer(cl, 0, ring, 0);
    for (int i = 0; i <= LEVELS; i++) {
        RhiBinding tb[2] = {{0}, {0}};
        tb[0].slot = 1;
        tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tb[0].texture = i == LEVELS ? bc3 : bc1;
        tb[1].slot = 1;
        tb[1].type = RHI_BIND_SAMPLER;
        tb[1].sampler = smp[i == LEVELS ? 0 : i];
        RhiBindGroupDesc tbd = {c->l2, tb, 2};
        rhi_CmdSetBindGroup(cl, 2, rhi_CreateBindGroup(&tbd));
        rhi_CmdDraw(cl, 6, (uint32_t)i * 6u, 1);
    }
    rhi_CmdEndRenderPass(cl);
    RhiTextureBarrier post = {tgt, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
    rhi_CmdBarrier(cl, &post, 1);
    rhi_EndCommands(cl);
    rhi_Submit(cl);

    static uint8_t img[BW * BH * 4];
    uint32_t pitch = 0;
    if (!rhi_ReadbackTexture(tgt, RHI_ASPECT_COLOR, img, sizeof(img), &pitch)) {
        rhi_test_Log("FAIL %s: BC target readback\n", s_label);
        failures++;
    } else {
        for (int i = 0; i <= LEVELS; i++) {
            for (int k = 0; k < 3; k++) {
                expectRGBA(img, pitch, i * CELL + 2 + k * 6, 2 + k * 6, want[i][0], want[i][1],
                           want[i][2], want[i][3], 100 + i);
            }
        }
    }
    rhi_DestroyPipeline(pipe);
    for (int l = 0; l < LEVELS; l++) {
        rhi_DestroySampler(smp[l]);
    }
    rhi_DestroyTexture(bc1);
    rhi_DestroyTexture(bc3);
    rhi_DestroyTexture(tgt);
    rhi_DestroyBuffer(ring);
    if (!failures) {
        rhi_test_Log("%s: BC cell passed (BC1 8x8 with 4 levels, BC3 4x4)\n", s_label);
    }
}

int rhi_test_RunCells(const RhiTestConfig *cfg)
{
    failures = 0;
    s_label = cfg->label ? cfg->label : cfg->backend;
    if (!rhi_CreateBackend(cfg->backend)) {
        rhi_test_Log("SKIP %s: backend %s is not linked into this build\n", s_label, cfg->backend);
        return 77;
    }
    RhiDeviceDesc dd = {NULL, false, cfg->debugLayers, s_label};
    if (!rhi_Init(&dd)) {
        rhi_test_Log("SKIP %s: no usable %s device (see messages above)\n", s_label, cfg->backend);
        return 77;
    }
    if (cfg->accept && !cfg->accept()) {
        rhi_Shutdown();
        return 77;
    }
    rhi_test_Log("%s: adapter %s\n", s_label, rhi_AdapterName());
    const RhiLimits *lim = rhi_Limits();
    if (!lim->dualSourceBlend || !lim->stencilWrap || lim->uniformAlign == 0) {
        rhi_test_Log("FAIL limits\n");
        failures++;
    }
    const uint32_t ua = lim->uniformAlign;

    Ctx c;
    /* package PA: TestCB is a dynamic uniform, one group bound at three
     * offsets (the game's FrameCB and DrawCB are too) */
    static const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC,
                                       (1u << RHI_STAGE_VERTEX) | (1u << RHI_STAGE_FRAGMENT)}};
    static const RhiBindSlot s2[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, 1u << RHI_STAGE_FRAGMENT},
                                      {1, RHI_BIND_SAMPLER, 1u << RHI_STAGE_FRAGMENT}};
    RhiBindGroupLayoutDesc ld0 = {s0, 1, "group0"};
    RhiBindGroupLayoutDesc ld1 = {NULL, 0, "group1"};
    RhiBindGroupLayoutDesc ld2 = {s2, 2, "group2"};
    c.l0 = rhi_CreateBindGroupLayout(&ld0);
    c.l1 = rhi_CreateBindGroupLayout(&ld1);
    c.l2 = rhi_CreateBindGroupLayout(&ld2);
    /* the blob the backend understands: DXIL for D3D12, SPIR-V otherwise */
    const bool dxil = rhi_Backend() == RHI_BACKEND_D3D12;
#define BLOB(n)                                                                                    \
    (dxil ? (const void *)dxil_##n : (const void *)spv_##n),                                       \
        (dxil ? sizeof(dxil_##n) : sizeof(spv_##n))
    c.vs = makeShader(RHI_STAGE_VERTEX, BLOB(vs_main), "vs_main");
    c.psDual = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_dual), "ps_dual");
    c.psColor = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_color), "ps_color");
    c.psTex = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_tex), "ps_tex");
    c.psUint = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_uint), "ps_uint");
#undef BLOB

    const RhiFormat CF = RHI_FMT_RGBA8_UNORM, DF = RHI_FMT_D32F_S8;
    RhiBlendState opaque = {.writeMask = 0xF};
    RhiBlendState dualColor = {true,
                               RHI_BF_SRC1_COLOR,
                               RHI_BF_ONE_MINUS_SRC1_COLOR,
                               RHI_BO_ADD,
                               RHI_BF_SRC1_ALPHA,
                               RHI_BF_ONE_MINUS_SRC1_ALPHA,
                               RHI_BO_ADD,
                               0xF};
    RhiBlendState dualAlpha = {true,
                               RHI_BF_SRC1_ALPHA,
                               RHI_BF_ONE_MINUS_SRC1_ALPHA,
                               RHI_BO_ADD,
                               RHI_BF_SRC1_ALPHA,
                               RHI_BF_ONE_MINUS_SRC1_ALPHA,
                               RHI_BO_ADD,
                               0x7}; /* RGB only */
    RhiBlendState noColor = {.writeMask = 0};
    RhiBlendState maskRA = {.writeMask = 0x9};

    RhiDepthStencilState dsNone = {0};
    RhiDepthStencilState dsMark = {
        .stencilTest = true, .stencilReadMask = 0xFF, .stencilWriteMask = 0xFF};
    dsMark.front = (RhiStencilFace){RHI_CMP_ALWAYS, RHI_SO_DECR_WRAP, RHI_SO_KEEP, RHI_SO_KEEP};
    dsMark.back = dsMark.front;
    RhiDepthStencilState dsDecr = dsMark;
    dsMark.front.pass = dsMark.back.pass = RHI_SO_REPLACE;
    RhiDepthStencilState dsReplace = dsMark;
    dsMark.front.pass = dsMark.back.pass = RHI_SO_INCR_WRAP;
    RhiDepthStencilState dsIncr = dsMark;
    dsMark.front = (RhiStencilFace){RHI_CMP_EQUAL, RHI_SO_KEEP, RHI_SO_KEEP, RHI_SO_KEEP};
    dsMark.back = dsMark.front;
    RhiDepthStencilState dsEqual = dsMark;
    RhiDepthStencilState dsGequal = {
        .depthTest = true, .depthWrite = true, .depthCompare = RHI_CMP_GEQUAL};

    RhiPipeline pDualColor = makePipeline(&c, c.psDual, CF, DF, &dualColor, &dsNone, "dualColor");
    RhiPipeline pDualAlpha = makePipeline(&c, c.psDual, CF, DF, &dualAlpha, &dsNone, "dualAlpha");
    RhiPipeline pDecr = makePipeline(&c, c.psColor, CF, DF, &noColor, &dsDecr, "stencilDecr");
    RhiPipeline pReplace = makePipeline(&c, c.psColor, CF, DF, &noColor, &dsReplace, "stencilRep");
    RhiPipeline pIncr = makePipeline(&c, c.psColor, CF, DF, &noColor, &dsIncr, "stencilIncr");
    RhiPipeline pEqual = makePipeline(&c, c.psColor, CF, DF, &opaque, &dsEqual, "stencilEqual");
    RhiPipeline pDepth = makePipeline(&c, c.psColor, CF, DF, &opaque, &dsGequal, "depthGequal");
    RhiPipeline pMask = makePipeline(&c, c.psColor, CF, DF, &maskRA, &dsNone, "maskRA");
    RhiPipeline pTex = makePipeline(&c, c.psTex, CF, DF, &opaque, &dsNone, "tex");
    RhiPipeline pUint =
        makePipeline(&c, c.psUint, RHI_FMT_RGBA8_UINT, RHI_FMT_UNKNOWN, &opaque, NULL, "uint");

    /* geometry: every quad of the frame, in one array */
    enum {
        Q_DUAL_COLOR,
        Q_DUAL_ALPHA,
        Q_DECR,
        Q_DECR_EQ,
        Q_REPLACE,
        Q_INCR,
        Q_INCR_EQ,
        Q_Z50,
        Q_Z25,
        Q_Z75,
        Q_Z75_EQ,
        Q_MASK,
        Q_UINT,
        Q_COUNT
    };

    Vtx verts[Q_COUNT * 6];
    cellQuad(&verts[Q_DUAL_COLOR * 6], 0, 0.0f, 255, 255, 0, 255);
    cellQuad(&verts[Q_DUAL_ALPHA * 6], 1, 0.0f, 128, 128, 128, 255);
    cellQuad(&verts[Q_DECR * 6], 2, 0.0f, 255, 0, 0, 255);
    cellQuad(&verts[Q_DECR_EQ * 6], 2, 0.0f, 0, 255, 0, 255);
    cellQuad(&verts[Q_REPLACE * 6], 3, 0.0f, 255, 0, 0, 255);
    cellQuad(&verts[Q_INCR * 6], 3, 0.0f, 255, 0, 0, 255);
    cellQuad(&verts[Q_INCR_EQ * 6], 3, 0.0f, 0, 0, 255, 255);
    cellQuad(&verts[Q_Z50 * 6], 4, 0.5f, 255, 0, 0, 255);
    cellQuad(&verts[Q_Z25 * 6], 4, 0.25f, 0, 0, 255, 255);
    cellQuad(&verts[Q_Z75 * 6], 4, 0.75f, 0, 255, 0, 255);
    cellQuad(&verts[Q_Z75_EQ * 6], 4, 0.75f, 255, 255, 0, 255);
    cellQuad(&verts[Q_MASK * 6], 5, 0.0f, 255, 255, 255, 255);
    quad(&verts[Q_UINT * 6], 0, 0, 2, 4, 4, 4, 0.0f, 0, 0, 0, 0); /* left half of 4x4 */
    /* cell 6 is indexed: four corners */
    Vtx texVerts[4];
    {
        Vtx q[6];
        cellQuad(q, 6, 0.0f, 255, 255, 255, 255);
        texVerts[0] = q[0]; /* top-left */
        texVerts[1] = q[1]; /* top-right */
        texVerts[2] = q[2]; /* bottom-left */
        texVerts[3] = q[5]; /* bottom-right */
    }
    static const uint16_t texIdx[6] = {0, 1, 2, 2, 1, 3};

    /* buffers: an upload ring holding vertices, indices, uniforms and
     * texel data; a device vertex buffer filled from it */
    const uint64_t offVerts = 0, offTexVerts = 4096, offIdx = 4096 + 512, offUbo = 8192;
    /* texel rows and offsets for buffer-to-texture copies follow the
     * backend's alignment (D3D12: pitch 256, offset 512) */
    const uint32_t pa = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1u;
    const uint32_t oa = lim->copyOffsetAlign ? lim->copyOffsetAlign : 1u;
    const uint32_t pitchTex = (8u + pa - 1u) / pa * pa, pitchR8 = (4u + pa - 1u) / pa * pa;
    const uint64_t offTexels = 16384;
    const uint64_t offR8 = (offTexels + 2u * pitchTex + oa - 1u) / oa * oa;
    const uint64_t ringSize = (offR8 + 4u * pitchR8 + 32767u) / 32768u * 32768u;
    RhiBufferDesc rd = {ringSize,
                        RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC,
                        RHI_MEM_UPLOAD, "ring"};
    RhiBuffer ring = rhi_CreateBuffer(&rd);
    RhiBufferDesc vd = {sizeof(verts), RHI_BUF_VERTEX | RHI_BUF_COPY_DST, RHI_MEM_DEVICE, "verts"};
    RhiBuffer vbuf = rhi_CreateBuffer(&vd);
    uint8_t *map = rhi_MapBuffer(ring);
    if (!ring.id || !vbuf.id || !map) {
        rhi_test_Log("FAIL buffers\n");
        rhi_Shutdown();
        return 1;
    }
    if (rhi_MapBuffer(ring) != map) {
        rhi_test_Log("FAIL rhi_MapBuffer is not persistent\n");
        failures++;
    }

    /* textures */
    RhiTextureDesc td = {W, H, 1, CF, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "scene"};
    RhiTexture scene = rhi_CreateTexture(&td);
    RhiTextureDesc dsd = {W, H, 1, DF, RHI_TEX_DEPTH_STENCIL | RHI_TEX_SAMPLED, "depth"};
    RhiTexture depth = rhi_CreateTexture(&dsd);
    RhiTextureDesc ud = {4, 4, 1, RHI_FMT_RGBA8_UINT, RHI_TEX_RENDER_TARGET, "uint"};
    RhiTexture utex = rhi_CreateTexture(&ud);
    RhiTextureDesc xd = {2, 2, 1, CF, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "tex2x2"};
    RhiTexture tex = rhi_CreateTexture(&xd);
    RhiTextureDesc r8d = {4, 4, 1, RHI_FMT_R8_UNORM, RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST, "r8"};
    RhiTexture r8a = rhi_CreateTexture(&r8d);
    RhiTexture r8b = rhi_CreateTexture(&r8d);
    RhiSamplerDesc sd = {RHI_FILTER_NEAREST,
                         RHI_FILTER_NEAREST,
                         RHI_FILTER_NEAREST,
                         RHI_WRAP_CLAMP,
                         RHI_WRAP_CLAMP,
                         1.0f,
                         0.0f,
                         0.0f,
                         0.0f};
    RhiSampler smp = rhi_CreateSampler(&sd);
    if (!scene.id || !depth.id || !utex.id || !tex.id || !r8a.id || !r8b.id || !smp.id) {
        rhi_test_Log("FAIL textures\n");
        rhi_Shutdown();
        return 1;
    }

    static const uint8_t texels[16] = {10, 20,  30,  40,  50,  60,  70,  80,
                                       90, 100, 110, 120, 130, 140, 150, 160};

    for (int frame = 0; frame < 3; frame++) {
        rhi_WaitFrame();
        /* the ring is rewritten every frame, as rd_core would */
        memcpy(map + offVerts, verts, sizeof(verts));
        memcpy(map + offTexVerts, texVerts, sizeof(texVerts));
        memcpy(map + offIdx, texIdx, sizeof(texIdx));
        for (int row = 0; row < 2; row++) {
            memcpy(map + offTexels + (uint64_t)row * pitchTex, &texels[row * 8], 8);
        }
        for (int i = 0; i < 16; i++) {
            map[offR8 + (uint64_t)(i / 4) * pitchR8 + (uint64_t)(i % 4)] = (uint8_t)(i * 16 + 1);
        }
        const TestCB cbs[3] = {
            {{1.0f, 0.0f, 1.0f, 0.0f}, {0, 0, 0, 0}},
            {{0.0f, 0.0f, 0.0f, 0.5f}, {0, 0, 0, 0}},
            {{0.0f, 0.0f, 0.0f, 0.0f}, {7, 128, 250, 255}},
        };
        for (int i = 0; i < 3; i++) {
            memcpy(map + offUbo + (uint64_t)i * ua, &cbs[i], sizeof(TestCB));
        }
        RhiBinding b0 = {0};
        b0.slot = 0;
        b0.type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
        b0.buffer = ring;
        b0.offset = offUbo;
        b0.size = sizeof(TestCB);
        RhiBindGroupDesc bd0 = {c.l0, &b0, 1};
        const RhiBindGroup g0 = rhi_CreateBindGroup(&bd0);
        const uint32_t cbOff[3] = {0, ua, 2 * ua};
        RhiBinding tb[2] = {{0}, {0}};
        tb[0].slot = 1;
        tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tb[0].texture = tex;
        tb[1].slot = 1;
        tb[1].type = RHI_BIND_SAMPLER;
        tb[1].sampler = smp;
        RhiBindGroupDesc tbd = {c.l2, tb, 2};
        RhiBindGroup g2 = rhi_CreateBindGroup(&tbd);
        if (!g0.id || !g2.id) {
            rhi_test_Log("FAIL bind groups\n");
            failures++;
            break;
        }

        RhiCommandList cl = rhi_BeginCommands();
        if (!cl.id) {
            rhi_test_Log("FAIL command list\n");
            failures++;
            break;
        }
        rhi_CmdBeginLabel(cl, "upload");
        rhi_CmdCopyBuffer(cl, ring, offVerts, vbuf, 0, sizeof(verts));
        RhiTextureBarrier up[3] = {{tex, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {r8a, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {r8b, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST}};
        rhi_CmdBarrier(cl, up, 3);
        rhi_CmdCopyBufferToTexture(cl, ring, offTexels, pitchTex, tex, 0, (RhiRect){0, 0, 2, 2});
        rhi_CmdCopyBufferToTexture(cl, ring, offR8, pitchR8, r8a, 0, (RhiRect){0, 0, 4, 4});
        RhiTextureBarrier r8s = {r8a, RHI_STATE_COPY_DST, RHI_STATE_COPY_SRC};
        rhi_CmdBarrier(cl, &r8s, 1);
        /* full copy, then a 2x2 block from (2,2) over (0,0) */
        rhi_CmdCopyTexture(cl, r8a, (RhiRect){0, 0, 4, 4}, r8b, 0, 0);
        rhi_CmdCopyTexture(cl, r8a, (RhiRect){2, 2, 2, 2}, r8b, 0, 0);
        rhi_CmdEndLabel(cl);

        RhiTextureBarrier pre[4] = {
            {tex, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {scene, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {depth, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE},
            {utex, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
        };
        rhi_CmdBarrier(cl, pre, 4);

        RhiRenderPassDesc rp = {0};
        rp.color[0] =
            (RhiColorAttachment){scene,
                                 RHI_LOAD_CLEAR,
                                 {64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f}};
        rp.colorCount = 1;
        rp.depth = (RhiDepthAttachment){depth, RHI_LOAD_CLEAR, RHI_LOAD_CLEAR, 0.0f, 0, false};
        rp.width = W;
        rp.height = H;
        rhi_CmdBeginLabel(cl, "scene");
        rhi_CmdBeginRenderPass(cl, &rp);
        rhi_CmdSetVertexBuffer(cl, 0, vbuf, 0);

        rhi_CmdSetPipeline(cl, pDualColor);
        rhi_CmdSetBindGroupOffsets(cl, 0, g0, &cbOff[0], 1);
        rhi_CmdDraw(cl, 6, Q_DUAL_COLOR * 6, 1);
        rhi_CmdSetPipeline(cl, pDualAlpha);
        rhi_CmdSetBindGroupOffsets(cl, 0, g0, &cbOff[1], 1);
        rhi_CmdDraw(cl, 6, Q_DUAL_ALPHA * 6, 1);

        rhi_CmdSetPipeline(cl, pDecr);
        rhi_CmdSetStencilRef(cl, 0);
        rhi_CmdDraw(cl, 6, Q_DECR * 6, 1);
        rhi_CmdSetPipeline(cl, pEqual);
        rhi_CmdSetStencilRef(cl, 255);
        rhi_CmdDraw(cl, 6, Q_DECR_EQ * 6, 1);

        rhi_CmdSetPipeline(cl, pReplace);
        rhi_CmdSetStencilRef(cl, 255);
        rhi_CmdDraw(cl, 6, Q_REPLACE * 6, 1);
        rhi_CmdSetPipeline(cl, pIncr);
        rhi_CmdDraw(cl, 6, Q_INCR * 6, 1);
        rhi_CmdSetPipeline(cl, pEqual);
        rhi_CmdSetStencilRef(cl, 0);
        rhi_CmdDraw(cl, 6, Q_INCR_EQ * 6, 1);

        rhi_CmdSetPipeline(cl, pDepth);
        rhi_CmdDraw(cl, 6, Q_Z50 * 6, 1);
        rhi_CmdDraw(cl, 6, Q_Z25 * 6, 1);
        rhi_CmdDraw(cl, 6, Q_Z75 * 6, 1);
        rhi_CmdDraw(cl, 6, Q_Z75_EQ * 6, 1);

        rhi_CmdSetPipeline(cl, pMask);
        rhi_CmdDraw(cl, 6, Q_MASK * 6, 1);

        rhi_CmdSetPipeline(cl, pTex);
        rhi_CmdSetBindGroup(cl, 2, g2);
        rhi_CmdSetVertexBuffer(cl, 0, ring, offTexVerts);
        rhi_CmdSetIndexBuffer(cl, ring, offIdx, false);
        rhi_CmdDrawIndexed(cl, 6, 0, 0, 1);
        rhi_CmdEndRenderPass(cl);
        rhi_CmdEndLabel(cl);

        RhiRenderPassDesc up2 = {0};
        up2.color[0] = (RhiColorAttachment){utex, RHI_LOAD_CLEAR, {1.0f, 2.0f, 3.0f, 4.0f}};
        up2.colorCount = 1;
        up2.width = 4;
        up2.height = 4;
        rhi_CmdBeginRenderPass(cl, &up2);
        rhi_CmdSetPipeline(cl, pUint);
        rhi_CmdSetBindGroupOffsets(cl, 0, g0, &cbOff[2], 1);
        rhi_CmdSetVertexBuffer(cl, 0, vbuf, 0);
        rhi_CmdDraw(cl, 6, Q_UINT * 6, 1);
        rhi_CmdEndRenderPass(cl);

        RhiTextureBarrier post[4] = {
            {scene, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
            {depth, RHI_STATE_DEPTH_WRITE, RHI_STATE_COPY_SRC},
            {utex, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
            {r8b, RHI_STATE_COPY_DST, RHI_STATE_COPY_SRC},
        };
        rhi_CmdBarrier(cl, post, 4);
        rhi_EndCommands(cl);
        rhi_Submit(cl);

        /* --- checks --- */
        static uint8_t img[W * H * 4];
        uint32_t pitch = 0;
        if (!rhi_ReadbackTexture(scene, RHI_ASPECT_COLOR, img, sizeof(img), &pitch) ||
            pitch != W * 4) {
            rhi_test_Log("FAIL scene readback\n");
            failures++;
            break;
        }
        expectCell(img, pitch, 0, 255, 64, 0, 64, frame);
        expectCell(img, pitch, 1, 96, 96, 96, 64, frame);
        expectCell(img, pitch, 2, 0, 255, 0, 255, frame);
        expectCell(img, pitch, 3, 0, 0, 255, 255, frame);
        expectCell(img, pitch, 4, 255, 255, 0, 255, frame);
        expectCell(img, pitch, 5, 255, 64, 64, 255, frame);
        expectCell(img, pitch, 7, 64, 64, 64, 64, frame);
        for (int ty = 0; ty < 2; ty++) {
            for (int tx = 0; tx < 2; tx++) {
                const uint8_t *t = &texels[(ty * 2 + tx) * 4];
                for (int k = 0; k < 2; k++) {
                    int x = 2 * CELL + tx * 8 + 1 + k * 6, y = CELL + ty * 8 + 1 + k * 6;
                    expectRGBA(img, pitch, x, y, t[0], t[1], t[2], t[3], frame);
                }
            }
        }

        static float dep[W * H];
        if (!rhi_ReadbackTexture(depth, RHI_ASPECT_DEPTH, dep, sizeof(dep), &pitch) ||
            pitch != W * 4) {
            rhi_test_Log("FAIL depth readback\n");
            failures++;
            break;
        }
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int cell = (y / CELL) * 4 + x / CELL;
                float want = cell == 4 ? 0.75f : 0.0f;
                if (dep[y * W + x] != want) {
                    rhi_test_Log("FAIL frame %d depth (%d,%d) = %.9g, expected %.9g\n", frame, x, y,
                                 (double)dep[y * W + x], (double)want);
                    failures++;
                    y = H;
                    break;
                }
            }
        }

        uint8_t u[4 * 4 * 4];
        if (!rhi_ReadbackTexture(utex, RHI_ASPECT_COLOR, u, sizeof(u), &pitch) || pitch != 16) {
            rhi_test_Log("FAIL uint readback\n");
            failures++;
            break;
        }
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                if (x < 2) {
                    expectRGBA(u, pitch, x, y, 7, 128, 250, 255, frame);
                } else {
                    expectRGBA(u, pitch, x, y, 1, 2, 3, 4, frame);
                }
            }
        }

        uint8_t r8[16];
        if (!rhi_ReadbackTexture(r8b, RHI_ASPECT_COLOR, r8, sizeof(r8), &pitch) || pitch != 4) {
            rhi_test_Log("FAIL r8 readback\n");
            failures++;
            break;
        }
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                int sx = x, sy = y;
                if (x < 2 && y < 2) {
                    sx += 2;
                    sy += 2;
                }
                uint8_t want = (uint8_t)((sy * 4 + sx) * 16 + 1);
                if (r8[y * 4 + x] != want) {
                    rhi_test_Log("FAIL frame %d r8 (%d,%d) = %u, expected %u\n", frame, x, y,
                                 r8[y * 4 + x], want);
                    failures++;
                }
            }
        }
        if (failures) {
            break;
        }
    }

    if (!failures) {
        rhi_WaitFrame();
        bcCell(&c);
    }

    /* destroys are deferred; the frames in flight retire them */
    rhi_DestroyPipeline(pUint);
    rhi_DestroyTexture(r8b);
    rhi_DestroyBuffer(vbuf);
    rhi_DestroySampler(smp);
    rhi_DestroyShader(c.psUint);
    rhi_WaitFrame();
    rhi_WaitFrame();
    rhi_WaitIdle();
    rhi_Shutdown();

    uint32_t verr = cfg->validationErrors ? cfg->validationErrors() : 0u;
    if (verr) {
        rhi_test_Log("FAIL %u validation error(s)\n", verr);
        failures++;
    }
    if (failures) {
        rhi_test_Log("%s: %d failure(s)\n", s_label, failures);
        return 1;
    }
    rhi_test_Log("%s: all checks passed\n", s_label);
    return 0;
}
