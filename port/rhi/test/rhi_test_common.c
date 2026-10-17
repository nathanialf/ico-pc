/* rhi_test_common.c: headless rendering through port/rhi/rhi.h on any
 * backend, checked texel by texel (generalised from rhi_vk_test.c; the
 * expected values are the same as there).
 *
 * A 64x32 RGBA8 target with a D32F+S8 depth-stencil is split into eight
 * 16x16 cells, each exercising one feature the GS emulation needs:
 *
 *   0  dual-source blend, SRC1_COLOR factors
 *   1  dual-source blend, SRC1_ALPHA factor with an RGB-only colour mask
 *      (0 and 1 keep the clear colour on a device without dual-source
 *      blending: rhi_vk_nodual fakes one)
 *   2  stencil DECR_WRAP 0 -> 255, then EQUAL 255
 *   3  stencil REPLACE 255, INCR_WRAP -> 0, then EQUAL 0
 *   4  reversed-Z: depth cleared to 0, GEQUAL with writes
 *   5  colour mask R|A
 *   6  texture uploaded by copy, sampled nearest, drawn indexed
 *   7  untouched (clear colour)
 *
 * Plus an RGBA8_UINT target (integer clear and integer output), R8 texture
 * copies (a whole copy, then a second copy over part of it: same-state copy
 * ordering), a device-local vertex buffer filled by rhi_cmd_copy_buffer, depth
 * readback, and three frames to cycle the frame slots.  Every expected
 * value is exact.  Buffer-to-texture copies use rhi_limits()'s pitch and
 * offset alignment (1/4 on Vulkan, 256/512 on D3D12). */
#include "rhi_test_common.h"
#include "rhi.h"
#include "rhi_test_dxil.h"
#include "rhi_test_spv.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

FILE *g_rhiTestLog;

void rhi_test_log(const char *fmt, ...)
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
    rhi_test_log("FAIL %s: got %3u %3u %3u %3u, expected %3u %3u %3u %3u\n", where, got[0], got[1],
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
    RhiBindGroupLayout layouts[3]; /* l0, l1, l2, for the descriptions */
    RhiShader vs, psDual, psColor, psTex, psUint;
    RhiShader psDepthResolve;   /* multisampling: the depth's largest sample */
    RhiShader psRov, psSubpass; /* the destination read (dstReadCell) */
} Ctx;

/* color RHI_FMT_UNKNOWN: no colour target (a depth-only pass); samples the
 * targets' count (0: single-sampled) */
static void pipelineDesc(const Ctx *c, RhiShader ps, RhiFormat color, RhiFormat depth,
                         const RhiBlendState *blend, const RhiDepthStencilState *ds,
                         const char *name, uint32_t samples, RhiPipelineDesc *out)
{
    static const RhiVertexBinding vb = {0, sizeof(Vtx), false};
    static const RhiVertexAttr va[3] = {
        {0, 0, RHI_VTX_F32x3, offsetof(Vtx, x)},
        {1, 0, RHI_VTX_U8x4_UNORM, offsetof(Vtx, rgba)},
        {2, 0, RHI_VTX_F32x2, offsetof(Vtx, u)},
    };
    RhiPipelineDesc d = {0};
    d.vertex = c->vs;
    d.fragment = ps;
    d.layouts = c->layouts;
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
    d.colorCount = color != RHI_FMT_UNKNOWN ? 1u : 0u;
    d.depthFormat = depth;
    d.debugName = name;
    d.samples = samples;
    *out = d;
}

static RhiPipeline makePipelineN(const Ctx *c, RhiShader ps, RhiFormat color, RhiFormat depth,
                                 const RhiBlendState *blend, const RhiDepthStencilState *ds,
                                 const char *name, uint32_t samples)
{
    RhiPipelineDesc d;
    pipelineDesc(c, ps, color, depth, blend, ds, name, samples, &d);
    RhiPipeline p = rhi_create_pipeline(&d);
    if (!p.id) {
        rhi_test_log("FAIL pipeline %s not created\n", name);
        failures++;
    }
    return p;
}

static RhiPipeline makePipeline(const Ctx *c, RhiShader ps, RhiFormat color, RhiFormat depth,
                                const RhiBlendState *blend, const RhiDepthStencilState *ds,
                                const char *name)
{
    return makePipelineN(c, ps, color, depth, blend, ds, name, 0u);
}

static RhiShader makeShader(RhiShaderStage stage, const void *code, size_t size, const char *entry)
{
    RhiShaderDesc d = {stage, code, size, entry, entry};
    RhiShader s = rhi_create_shader(&d);
    if (!s.id) {
        rhi_test_log("FAIL shader %s not created\n", entry);
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
        rhi_test_log("     (cell %d)\n", cell);
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
    const RhiLimits *lim = rhi_limits();
    if (!lim->bcTextures) {
        rhi_test_log("SKIP %s: BC cell (no BC formats on this device)\n", s_label);
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

    RhiTexture tgt = rhi_create_texture(
        &(RhiTextureDesc){BW, BH, 1, CF, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "bc target", 0});
    RhiTexture bc1 = rhi_create_texture(&(RhiTextureDesc){
        8, 8, LEVELS, RHI_FMT_BC1_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "bc1", 0});
    RhiTexture bc3 = rhi_create_texture(&(RhiTextureDesc){
        4, 4, 1, RHI_FMT_BC3_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "bc3", 0});
    /* a block texture is never a target */
    RhiTexture bad = rhi_create_texture(
        &(RhiTextureDesc){4, 4, 1, RHI_FMT_BC1_UNORM, RHI_TEX_RENDER_TARGET, "bc target?", 0});
    RhiBuffer ring = rhi_create_buffer(
        &(RhiBufferDesc){65536, RHI_BUF_VERTEX | RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "bc ring"});
    uint8_t *map = ring.id ? rhi_map_buffer(ring) : NULL;
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
        smp[l] = rhi_create_sampler(&sd);
        ok = ok && smp[l].id;
    }
    if (bad.id) {
        rhi_test_log("FAIL %s: a BC render target was created\n", s_label);
        failures++;
        rhi_destroy_texture(bad);
    }
    if (!ok) {
        rhi_test_log("FAIL %s: BC cell resources\n", s_label);
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
    RhiCommandList cl = rhi_begin_commands();
    RhiTextureBarrier up[3] = {{bc1, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                               {bc3, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                               {tgt, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET}};
    rhi_cmd_barrier(cl, up, 3);
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
        rhi_cmd_copy_buffer_to_texture(cl, ring, off, pitch, isBc3 ? bc3 : bc1,
                                       isBc3 ? 0u : (uint32_t)l, (RhiRect){0, 0, w, h});
        off += (uint64_t)pitch * bh;
    }
    RhiTextureBarrier rd[2] = {{bc1, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
                               {bc3, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ}};
    rhi_cmd_barrier(cl, rd, 2);
    RhiRenderPassDesc rp = {0};
    rp.color[0] =
        (RhiColorAttachment){tgt, RHI_LOAD_CLEAR, {0.0f, 0.0f, 0.0f, 0.0f}, RHI_STORE_STORE, false};
    rp.colorCount = 1;
    rp.width = BW;
    rp.height = BH;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_set_pipeline(cl, pipe);
    rhi_cmd_set_vertex_buffer(cl, 0, ring, 0);
    for (int i = 0; i <= LEVELS; i++) {
        RhiBinding tb[2] = {{0}, {0}};
        tb[0].slot = 1;
        tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tb[0].texture = i == LEVELS ? bc3 : bc1;
        tb[1].slot = 1;
        tb[1].type = RHI_BIND_SAMPLER;
        tb[1].sampler = smp[i == LEVELS ? 0 : i];
        RhiBindGroupDesc tbd = {c->l2, tb, 2};
        rhi_cmd_set_bind_group(cl, 2, rhi_create_bind_group(&tbd));
        rhi_cmd_draw(cl, 6, (uint32_t)i * 6u, 1);
    }
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier post = {tgt, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
    rhi_cmd_barrier(cl, &post, 1);
    rhi_end_commands(cl);
    rhi_submit(cl);

    static uint8_t img[BW * BH * 4];
    uint32_t pitch = 0;
    if (!rhi_readback_texture(tgt, RHI_ASPECT_COLOR, img, sizeof(img), &pitch)) {
        rhi_test_log("FAIL %s: BC target readback\n", s_label);
        failures++;
    } else {
        for (int i = 0; i <= LEVELS; i++) {
            for (int k = 0; k < 3; k++) {
                expectRGBA(img, pitch, i * CELL + 2 + k * 6, 2 + k * 6, want[i][0], want[i][1],
                           want[i][2], want[i][3], 100 + i);
            }
        }
    }
    rhi_destroy_pipeline(pipe);
    for (int l = 0; l < LEVELS; l++) {
        rhi_destroy_sampler(smp[l]);
    }
    rhi_destroy_texture(bc1);
    rhi_destroy_texture(bc3);
    rhi_destroy_texture(tgt);
    rhi_destroy_buffer(ring);
    if (!failures) {
        rhi_test_log("%s: BC cell passed (BC1 8x8 with 4 levels, BC3 4x4)\n", s_label);
    }
}

/* Multisampling, for each of 2, 4 and 8 samples the device takes
 * (RhiLimits.msaaCounts; the others are skipped): a 16 x 16 multisampled
 * RGBA8 target with a multisampled D32F_S8 depth, cleared, a full quad at
 * depth 0.25 in red, then a triangle over the top-left half (the diagonal
 * x + y = 16 through pixel corners) at 0.75 in green, GEQUAL with writes.
 * The colour is resolved (rhi_cmd_resolve_color) and read back: every pixel
 * the triangle covers whole is green, every pixel it misses red, and the
 * pixels on the diagonal lie between (at least one of them strictly: the
 * samples saw both).  The depth is resolved by ps_depth_resolve (the
 * largest sample, SV_Depth) into a single-sampled depth target and read
 * back (with RhiLimits.depthReadback): 0.75 where any sample is green, else
 * 0.25.  A multisampled texture is never read back. */
static void msaaCell(const Ctx *c)
{
    enum { MS = 16 };

    const RhiLimits *lim = rhi_limits();
    const RhiFormat CF = RHI_FMT_RGBA8_UNORM, DF = RHI_FMT_D32F_S8;
    const uint8_t red[4] = {255, 0, 0, 255}, green[4] = {0, 255, 0, 255};
    for (uint32_t n = 2u; n <= 8u; n *= 2u) {
        if ((lim->msaaCounts & n) == 0u) {
            rhi_test_log("SKIP %s: %u samples (msaaCounts 0x%x)\n", s_label, n, lim->msaaCounts);
            continue;
        }
        rhi_wait_frame();
        RhiTextureDesc mcd = {MS,          MS, 1, CF, RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                              "ms colour", n};
        RhiTextureDesc mdd = {MS,         MS, 1, DF, RHI_TEX_DEPTH_STENCIL | RHI_TEX_SAMPLED,
                              "ms depth", n};
        RhiTextureDesc rcd = {MS,
                              MS,
                              1,
                              CF,
                              RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_DST |
                                  RHI_TEX_COPY_SRC,
                              "resolved colour",
                              0};
        RhiTextureDesc rdd = {
            MS, MS, 1, DF, RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_SRC, "resolved depth", 0};
        RhiTexture msC = rhi_create_texture(&mcd), msD = rhi_create_texture(&mdd);
        RhiTexture rc = rhi_create_texture(&rcd), rdep = rhi_create_texture(&rdd);
        RhiBuffer ring =
            rhi_create_buffer(&(RhiBufferDesc){4096, RHI_BUF_VERTEX, RHI_MEM_UPLOAD, "ms verts"});
        uint8_t *map = ring.id ? rhi_map_buffer(ring) : NULL;
        RhiBlendState opaque = {.writeMask = 0xF};
        RhiDepthStencilState geq;
        memset(&geq, 0, sizeof(geq));
        geq.depthTest = true;
        geq.depthWrite = true;
        geq.depthCompare = RHI_CMP_GEQUAL;
        RhiDepthStencilState always = geq;
        always.depthCompare = RHI_CMP_ALWAYS;
        RhiPipeline pDraw = makePipelineN(c, c->psColor, CF, DF, &opaque, &geq, "ms draw", n);
        RhiPipeline pRes = makePipelineN(c, c->psDepthResolve, RHI_FMT_UNKNOWN, DF, &opaque,
                                         &always, "ms depth resolve", 0u);
        if (!msC.id || !msD.id || !rc.id || !rdep.id || !map || !pDraw.id || !pRes.id) {
            rhi_test_log("FAIL %s: %u-sample resources\n", s_label, n);
            failures++;
            return;
        }
        Vtx v[15];
        quad(&v[0], 0, 0, MS, MS, MS, MS, 0.25f, 255, 0, 0, 255);
        /* the triangle (0,0) (16,0) (0,16) in pixels */
        v[6] = (Vtx){-1.0f, 1.0f, 0.75f, {0, 255, 0, 255}, 0, 0};
        v[7] = (Vtx){1.0f, 1.0f, 0.75f, {0, 255, 0, 255}, 0, 0};
        v[8] = (Vtx){-1.0f, -1.0f, 0.75f, {0, 255, 0, 255}, 0, 0};
        quad(&v[9], 0, 0, MS, MS, MS, MS, 0.0f, 255, 255, 255, 255); /* the depth resolve */
        memcpy(map, v, sizeof(v));

        RhiCommandList cl = rhi_begin_commands();
        RhiTextureBarrier b0[2] = {{msC, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
                                   {msD, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE}};
        rhi_cmd_barrier(cl, b0, 2);
        RhiRenderPassDesc rp;
        memset(&rp, 0, sizeof(rp));
        rp.color[0].texture = msC;
        rp.color[0].load = RHI_LOAD_CLEAR;
        rp.color[0].clear[2] = 1.0f;
        rp.color[0].clear[3] = 1.0f;
        rp.colorCount = 1;
        rp.depth.texture = msD;
        rp.depth.depthLoad = RHI_LOAD_CLEAR;
        rp.depth.stencilLoad = RHI_LOAD_CLEAR;
        rp.depth.clearDepth = 0.0f;
        rp.width = MS;
        rp.height = MS;
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_set_pipeline(cl, pDraw);
        rhi_cmd_set_vertex_buffer(cl, 0, ring, 0);
        rhi_cmd_draw(cl, 9, 0, 1);
        rhi_cmd_end_render_pass(cl);
        /* the colour resolve */
        RhiTextureBarrier b1[3] = {{msC, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                   {rc, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {msD, RHI_STATE_DEPTH_WRITE, RHI_STATE_SHADER_READ}};
        rhi_cmd_barrier(cl, b1, 3);
        rhi_cmd_resolve_color(cl, msC, rc);
        /* the depth resolve */
        RhiTextureBarrier b2[2] = {{rc, RHI_STATE_COPY_DST, RHI_STATE_COPY_SRC},
                                   {rdep, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE}};
        rhi_cmd_barrier(cl, b2, 2);
        RhiRenderPassDesc dp;
        memset(&dp, 0, sizeof(dp));
        dp.depth.texture = rdep;
        dp.depth.depthLoad = RHI_LOAD_CLEAR;
        dp.depth.stencilLoad = RHI_LOAD_CLEAR;
        dp.depth.clearDepth = 0.0f;
        dp.width = MS;
        dp.height = MS;
        rhi_cmd_begin_render_pass(cl, &dp);
        rhi_cmd_set_pipeline(cl, pRes);
        RhiBinding tb;
        memset(&tb, 0, sizeof(tb));
        tb.slot = 1;
        tb.type = RHI_BIND_SAMPLED_TEXTURE;
        tb.texture = msD;
        tb.aspect = RHI_ASPECT_DEPTH;
        RhiBindGroupDesc tbd = {c->l2, &tb, 1};
        rhi_cmd_set_bind_group(cl, 2, rhi_create_bind_group(&tbd));
        rhi_cmd_set_vertex_buffer(cl, 0, ring, 0);
        rhi_cmd_draw(cl, 6, 9, 1);
        rhi_cmd_end_render_pass(cl);
        RhiTextureBarrier b3 = {rdep, RHI_STATE_DEPTH_WRITE, RHI_STATE_COPY_SRC};
        rhi_cmd_barrier(cl, &b3, 1);
        rhi_end_commands(cl);
        rhi_submit(cl);

        static uint8_t img[MS * MS * 4];
        static float dep[MS * MS];
        uint32_t pitch = 0;
        int mixed = 0, bad = 0;
        if (!rhi_readback_texture(rc, RHI_ASPECT_COLOR, img, sizeof(img), &pitch)) {
            rhi_test_log("FAIL %s: %u samples: resolved colour readback\n", s_label, n);
            failures++;
        } else {
            for (int y = 0; y < MS; y++) {
                for (int x = 0; x < MS; x++) {
                    const uint8_t *p = img + (size_t)y * pitch + (size_t)x * 4;
                    if (x + y <= MS - 2) {
                        bad += memcmp(p, green, 4) != 0;
                    } else if (x + y >= MS) {
                        bad += memcmp(p, red, 4) != 0;
                    } else {
                        mixed += memcmp(p, green, 4) != 0 && memcmp(p, red, 4) != 0;
                        /* between: R falls as G rises, B 0, A 255 */
                        bad += p[2] != 0 || p[3] != 255 || p[0] + p[1] < 254 || p[0] + p[1] > 256;
                    }
                }
            }
            rhi_test_log("%s: %u samples: %d of %d edge pixels mixed, %d wrong\n", s_label, n,
                         mixed, MS - 1, bad);
            if (bad || mixed == 0) {
                rhi_test_log("FAIL %s: %u samples: the resolved colour\n", s_label, n);
                failures++;
            }
        }
        if (lim->depthReadback) {
            if (rhi_readback_texture(rdep, RHI_ASPECT_DEPTH, dep, sizeof(dep), &pitch)) {
                int dbad = 0;
                for (int y = 0; y < MS; y++) {
                    for (int x = 0; x < MS; x++) {
                        const float want = x + y <= MS - 1 ? 0.75f : 0.25f;
                        dbad += dep[(size_t)y * (pitch / 4u) + (size_t)x] != want;
                    }
                }
                if (dbad) {
                    rhi_test_log("FAIL %s: %u samples: the depth resolve (%d wrong)\n", s_label, n,
                                 dbad);
                    failures++;
                }
            } else {
                rhi_test_log("FAIL %s: %u samples: resolved depth readback\n", s_label, n);
                failures++;
            }
        }
        if (rhi_readback_texture(msC, RHI_ASPECT_COLOR, img, sizeof(img), &pitch)) {
            rhi_test_log("FAIL %s: a multisampled texture was read back\n", s_label);
            failures++;
        }
        rhi_destroy_pipeline(pDraw);
        rhi_destroy_pipeline(pRes);
        rhi_destroy_texture(msC);
        rhi_destroy_texture(msD);
        rhi_destroy_texture(rc);
        rhi_destroy_texture(rdep);
        rhi_destroy_buffer(ring);
    }
}

/* The destination read (rhi.h RhiDstRead), each kind the device reports
 * (RhiLimits.fragmentInterlock or .rov: STORAGE; .rasterOrderAttachment:
 * ATTACHMENT), else skipped and logged: a 16 x 16 RGBA8 target cleared to
 * (10, 200, 30, 0x55), then ps_rov / ps_subpass compute the GS LERP
 * ((Cs - Cd) * As >> 7) + Cd of the vertex colour over the target's own
 * pixel: one draw of two full quads in (200, 100, 50, 0x40), so every pixel
 * takes the second over the first inside one draw (the rasterization
 * order), then a draw of the left half in (0, 255, 255, 0x90) (As past
 * 0x80 overshoots, clamped).  Every texel is the integer formula's,
 * exactly; the fixed-function blender's rounding would put channels one
 * level up. */
static int gsLerpRef(int s, int d, int a)
{
    const int v = (((s - d) * a) >> 7) + d;
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

static void dstReadCell(const Ctx *c, RhiDstRead kind)
{
    enum { DS = 16 };

    const RhiLimits *lim = rhi_limits();
    const bool storage = kind == RHI_DST_READ_STORAGE;
    const char *name = storage ? "storage (interlock / ROV)" : "attachment read";
    if (storage ? !(lim->fragmentInterlock || lim->rov) : !lim->rasterOrderAttachment) {
        rhi_test_log("SKIP %s: destination read, %s: not reported (fragmentInterlock %d, rov %d, "
                     "rasterOrderAttachment %d)\n",
                     s_label, name, lim->fragmentInterlock, lim->rov, lim->rasterOrderAttachment);
        return;
    }
    rhi_wait_frame();
    const RhiFormat CF = RHI_FMT_RGBA8_UNORM;
    const uint32_t usage =
        RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
        (storage ? (uint32_t)RHI_TEX_STORAGE : (uint32_t)RHI_TEX_INPUT_ATTACHMENT);
    RhiTexture tgt = rhi_create_texture(&(RhiTextureDesc){DS, DS, 1, CF, usage, "dst read", 0});
    const RhiBindSlot s3 = {0, storage ? RHI_BIND_STORAGE_TEXTURE : RHI_BIND_INPUT_ATTACHMENT,
                            1u << RHI_STAGE_FRAGMENT};
    const RhiBindGroupLayout l3 =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){&s3, 1, "dst read group"});
    RhiBuffer ring =
        rhi_create_buffer(&(RhiBufferDesc){4096, RHI_BUF_VERTEX, RHI_MEM_UPLOAD, "dst verts"});
    uint8_t *map = ring.id ? rhi_map_buffer(ring) : NULL;

    static const RhiVertexBinding vb = {0, sizeof(Vtx), false};
    static const RhiVertexAttr va[3] = {
        {0, 0, RHI_VTX_F32x3, offsetof(Vtx, x)},
        {1, 0, RHI_VTX_U8x4_UNORM, offsetof(Vtx, rgba)},
        {2, 0, RHI_VTX_F32x2, offsetof(Vtx, u)},
    };
    RhiBindGroupLayout layouts[4] = {c->l0, c->l1, c->l2, l3};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = c->vs;
    d.fragment = storage ? c->psRov : c->psSubpass;
    d.layouts = layouts;
    d.layoutCount = 4;
    d.vertexBindings = &vb;
    d.vertexBindingCount = 1;
    d.vertexAttrs = va;
    d.vertexAttrCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0].writeMask = 0xF; /* blending off: the shader blends */
    d.colorFormats[0] = CF;
    d.colorCount = storage ? 0u : 1u; /* the storage form writes the target itself */
    d.depthFormat = RHI_FMT_UNKNOWN;
    d.debugName = storage ? "dst read storage" : "dst read attachment";
    d.dstRead = kind;
    RhiPipeline pipe = rhi_create_pipeline(&d);
    if (!tgt.id || !l3.id || !map || !pipe.id) {
        rhi_test_log("FAIL %s: destination read, %s: resources (target %u, layout %u, pipeline "
                     "%u)\n",
                     s_label, name, tgt.id, l3.id, pipe.id);
        failures++;
        return;
    }
    Vtx v[18];
    quad(&v[0], 0, 0, DS, DS, DS, DS, 0.0f, 200, 100, 50, 0x40);
    quad(&v[6], 0, 0, DS, DS, DS, DS, 0.0f, 200, 100, 50, 0x40);
    quad(&v[12], 0, 0, DS / 2, DS, DS, DS, 0.0f, 0, 255, 255, 0x90);
    memcpy(map, v, sizeof(v));

    static const float clearCol[4] = {10.0f / 255.0f, 200.0f / 255.0f, 30.0f / 255.0f,
                                      85.0f / 255.0f};
    RhiCommandList cl = rhi_begin_commands();
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    rp.color[0].texture = tgt;
    rp.color[0].load = RHI_LOAD_CLEAR;
    memcpy(rp.color[0].clear, clearCol, sizeof(clearCol));
    rp.colorCount = 1;
    rp.width = DS;
    rp.height = DS;
    /* the clear as a pass of its own (as rd records a clear before a pass
     * that reads the target), then the target as the ROV, or as an
     * attachment the next pass loads and reads */
    RhiTextureBarrier b0 = {tgt, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
    rhi_cmd_barrier(cl, &b0, 1);
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier b1 = {tgt, RHI_STATE_RENDER_TARGET,
                            storage ? RHI_STATE_STORAGE : RHI_STATE_RENDER_TARGET_READ};
    rhi_cmd_barrier(cl, &b1, 1);
    if (storage) {
        RhiRenderPassDesc sp;
        memset(&sp, 0, sizeof(sp));
        sp.width = DS;
        sp.height = DS;
        rhi_cmd_begin_render_pass(cl, &sp);
    } else {
        rp.color[0].load = RHI_LOAD_LOAD;
        rp.color[0].readable = true;
        rhi_cmd_begin_render_pass(cl, &rp);
    }
    RhiBinding tb;
    memset(&tb, 0, sizeof(tb));
    tb.slot = 0;
    tb.type = s3.type;
    tb.texture = tgt;
    RhiBindGroupDesc tbd = {l3, &tb, 1};
    rhi_cmd_set_pipeline(cl, pipe);
    rhi_cmd_set_bind_group(cl, 3, rhi_create_bind_group(&tbd));
    rhi_cmd_set_vertex_buffer(cl, 0, ring, 0);
    rhi_cmd_draw(cl, 12, 0, 1); /* the two quads: one over the other in one draw */
    rhi_cmd_draw(cl, 6, 12, 1); /* the left half: the next draw */
    rhi_cmd_end_render_pass(cl);
    RhiTextureBarrier b2 = {tgt, storage ? RHI_STATE_STORAGE : RHI_STATE_RENDER_TARGET_READ,
                            RHI_STATE_COPY_SRC};
    rhi_cmd_barrier(cl, &b2, 1);
    rhi_end_commands(cl);
    rhi_submit(cl);

    static uint8_t img[DS * DS * 4];
    uint32_t pitch = 0;
    if (!rhi_readback_texture(tgt, RHI_ASPECT_COLOR, img, sizeof(img), &pitch)) {
        rhi_test_log("FAIL %s: destination read, %s: readback\n", s_label, name);
        failures++;
    } else {
        static const int bg[4] = {10, 200, 30, 0x55}, s1[4] = {200, 100, 50, 0x40},
                         s2[4] = {0, 255, 255, 0x90};
        uint8_t wantR[4], wantL[4];
        for (int k = 0; k < 3; k++) {
            const int once = gsLerpRef(s1[k], bg[k], s1[3]);
            const int twice = gsLerpRef(s1[k], once, s1[3]);
            wantR[k] = (uint8_t)twice;
            wantL[k] = (uint8_t)gsLerpRef(s2[k], twice, s2[3]);
        }
        wantR[3] = (uint8_t)s1[3];
        wantL[3] = (uint8_t)s2[3];
        const int before = failures;
        for (int y = 0; y < DS && failures == before; y++) {
            for (int x = 0; x < DS && failures == before; x++) {
                const uint8_t *w = x < DS / 2 ? wantL : wantR;
                expectRGBA(img, pitch, x, y, w[0], w[1], w[2], w[3], 200 + (int)kind);
            }
        }
        if (failures == before) {
            rhi_test_log("%s: destination read, %s: every texel the GS's (%u %u %u %u / %u %u %u "
                         "%u)\n",
                         s_label, name, wantL[0], wantL[1], wantL[2], wantL[3], wantR[0], wantR[1],
                         wantR[2], wantR[3]);
        }
    }
    rhi_destroy_pipeline(pipe);
    rhi_destroy_bind_group_layout(l3);
    rhi_destroy_texture(tgt);
    rhi_destroy_buffer(ring);
}

/* rhi_create_pipelines' wait function: the finished count never goes
 * back */
typedef struct BatchWait {
    uint32_t calls, last, backwards;
} BatchWait;

static void batchWait(void *ctx, uint32_t finished)
{
    BatchWait *w = ctx;
    w->backwards += finished < w->last;
    w->last = finished;
    w->calls++;
}

int rhi_test_run_cells(const RhiTestConfig *cfg)
{
    failures = 0;
    s_label = cfg->label ? cfg->label : cfg->backend;
    if (!rhi_create_backend(cfg->backend)) {
        rhi_test_log("SKIP %s: backend %s is not linked into this build\n", s_label, cfg->backend);
        return 77;
    }
    RhiDeviceDesc dd = {NULL, false, cfg->debugLayers, s_label};
    if (!rhi_init(&dd)) {
        rhi_test_log("SKIP %s: no usable %s device (see messages above)\n", s_label, cfg->backend);
        return 77;
    }
    if (cfg->accept && !cfg->accept()) {
        rhi_shutdown();
        return 77;
    }
    rhi_test_log("%s: adapter %s\n", s_label, rhi_adapter_name());
    const RhiLimits *lim = rhi_limits();
    if (!lim->stencilWrap || lim->uniformAlign == 0) {
        rhi_test_log("FAIL limits\n");
        failures++;
    }
    /* dual-source blending is optional; without it cells 0
     * and 1 are not drawn and keep the clear colour */
    const bool dual = lim->dualSourceBlend;
    const char *fakeD24 = getenv("ICO_VK_FAKE_D24S8"); /* rhi_vk_d24s8 */
    if (fakeD24 && fakeD24[0] == '1' && rhi_backend() == RHI_BACKEND_VULKAN &&
        (lim->depthReadback || strcmp(lim->depthStencilFormatName, "D24S8") != 0)) {
        rhi_test_log("FAIL ICO_VK_FAKE_D24S8 set but %s reported\n", lim->depthStencilFormatName);
        return 1;
    }
    const char *fake = getenv("ICO_VK_FAKE_NO_DUAL"); /* rhi_vk_nodual */
    if (dual && fake && fake[0] && fake[0] != '0') {
        rhi_test_log("FAIL ICO_VK_FAKE_NO_DUAL set but dualSourceBlend reported\n");
        failures++;
    }
    if (!dual) {
        rhi_test_log("%s: no dual-source blending: cells 0 and 1 skipped\n", s_label);
    }
    /* the destination read (dstReadCell): reported, and off when faked off */
    rhi_test_log("%s: destination read: fragmentInterlock %d, rov %d, rasterOrderAttachment %d\n",
                 s_label, lim->fragmentInterlock, lim->rov, lim->rasterOrderAttachment);
    const char *fakeDst =
        getenv(rhi_backend() == RHI_BACKEND_D3D12 ? "ICO_DX_FAKE_NO_DST" : "ICO_VK_FAKE_NO_DST");
    if (fakeDst && fakeDst[0] && fakeDst[0] != '0' &&
        (lim->fragmentInterlock || lim->rov || lim->rasterOrderAttachment)) {
        rhi_test_log("FAIL the destination read faked off but reported\n");
        failures++;
    }
    const uint32_t ua = lim->uniformAlign;

    Ctx c;
    /* TestCB is a dynamic uniform, one group bound at three
     * offsets (the game's FrameCB and DrawCB are too) */
    static const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC,
                                       (1u << RHI_STAGE_VERTEX) | (1u << RHI_STAGE_FRAGMENT)}};
    static const RhiBindSlot s2[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, 1u << RHI_STAGE_FRAGMENT},
                                      {1, RHI_BIND_SAMPLER, 1u << RHI_STAGE_FRAGMENT}};
    RhiBindGroupLayoutDesc ld0 = {s0, 1, "group0"};
    RhiBindGroupLayoutDesc ld1 = {NULL, 0, "group1"};
    RhiBindGroupLayoutDesc ld2 = {s2, 2, "group2"};
    c.l0 = rhi_create_bind_group_layout(&ld0);
    c.l1 = rhi_create_bind_group_layout(&ld1);
    c.l2 = rhi_create_bind_group_layout(&ld2);
    c.layouts[0] = c.l0;
    c.layouts[1] = c.l1;
    c.layouts[2] = c.l2;
    /* the blob the backend understands: DXIL for D3D12, SPIR-V otherwise */
    const bool dxil = rhi_backend() == RHI_BACKEND_D3D12;
#define BLOB(n)                                                                                    \
    (dxil ? (const void *)dxil_##n : (const void *)spv_##n),                                       \
        (dxil ? sizeof(dxil_##n) : sizeof(spv_##n))
    c.vs = makeShader(RHI_STAGE_VERTEX, BLOB(vs_main), "vs_main");
    c.psDual = (RhiShader){0};
    if (dual) {
        c.psDual = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_dual), "ps_dual");
    }
    c.psColor = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_color), "ps_color");
    c.psTex = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_tex), "ps_tex");
    c.psUint = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_uint), "ps_uint");
    c.psDepthResolve = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_depth_resolve), "ps_depth_resolve");
    /* a destination-read shader only where its capability is enabled: the
     * interlock's SPIR-V declares FragmentShaderPixelInterlockEXT, which a
     * device without VK_EXT_fragment_shader_interlock enabled rejects */
    c.psRov = (RhiShader){0};
    c.psSubpass = (RhiShader){0};
    if (lim->fragmentInterlock || lim->rov) {
        c.psRov = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_rov), "ps_rov");
    }
    if (lim->rasterOrderAttachment) {
        c.psSubpass = makeShader(RHI_STAGE_FRAGMENT, BLOB(ps_subpass), "ps_subpass");
    }
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

    RhiPipeline pDualColor = {0}, pDualAlpha = {0};
    if (dual) {
        pDualColor = makePipeline(&c, c.psDual, CF, DF, &dualColor, &dsNone, "dualColor");
        pDualAlpha = makePipeline(&c, c.psDual, CF, DF, &dualAlpha, &dsNone, "dualAlpha");
    }
    /* these seven through rhi_create_pipelines (the renderer's start-up
     * set), with one description that cannot be created among them: the
     * cells below draw with them, and the failed one fails alone */
    RhiPipelineDesc batchDesc[8];
    RhiPipeline batchPipe[8];
    float batchMs[8];
    BatchWait bw = {0, 0, 0};
    RhiPipelineBatch batch = {batchWait, &bw, batchMs, 0, 0};
    pipelineDesc(&c, c.psColor, CF, DF, &noColor, &dsDecr, "stencilDecr", 0u, &batchDesc[0]);
    pipelineDesc(&c, c.psColor, CF, DF, &noColor, &dsReplace, "stencilRep", 0u, &batchDesc[1]);
    pipelineDesc(&c, c.psColor, CF, DF, &noColor, &dsIncr, "stencilIncr", 0u, &batchDesc[2]);
    pipelineDesc(&c, c.psColor, CF, DF, &opaque, &dsGequal, "depthGequal", 0u, &batchDesc[3]);
    pipelineDesc(&c, c.psColor, CF, DF, &opaque, &dsNone, "noVertexShader", 0u, &batchDesc[4]);
    batchDesc[4].vertex = (RhiShader){0};
    pipelineDesc(&c, c.psColor, CF, DF, &opaque, &dsEqual, "stencilEqual", 0u, &batchDesc[5]);
    pipelineDesc(&c, c.psColor, CF, DF, &maskRA, &dsNone, "maskRA", 0u, &batchDesc[6]);
    pipelineDesc(&c, c.psTex, CF, DF, &opaque, &dsNone, "tex", 0u, &batchDesc[7]);
    const uint32_t batchMade = rhi_create_pipelines(batchDesc, 8, batchPipe, &batch);
    rhi_test_log("%s: rhi_create_pipelines: %u of 8 on %u threads, %u waits\n", s_label, batchMade,
                 batch.threads, bw.calls);
    if (batchMade != 7 || batchPipe[4].id != 0 || batch.threads < 1 ||
        batch.threads > RHI_PIPELINE_THREADS_MAX || bw.backwards || bw.last > 8) {
        rhi_test_log("FAIL rhi_create_pipelines: %u made, the failed one %u, %u threads, "
                     "finished went back %u times, last %u\n",
                     batchMade, batchPipe[4].id, batch.threads, bw.backwards, bw.last);
        failures++;
    }
    for (uint32_t i = 0; i < 8; i++) {
        for (uint32_t k = 0; k < i; k++) {
            if (batchPipe[i].id && batchPipe[i].id == batchPipe[k].id) {
                rhi_test_log("FAIL rhi_create_pipelines: pipelines %u and %u share id %u\n", k, i,
                             batchPipe[i].id);
                failures++;
            }
        }
        if (batchMs[i] < 0.0f || (i == 4 && batchMs[i] != 0.0f)) {
            rhi_test_log("FAIL rhi_create_pipelines: pipeline %u took %.3f ms\n", i,
                         (double)batchMs[i]);
            failures++;
        }
    }
    RhiPipeline pDecr = batchPipe[0];
    RhiPipeline pReplace = batchPipe[1];
    RhiPipeline pIncr = batchPipe[2];
    RhiPipeline pDepth = batchPipe[3];
    RhiPipeline pEqual = batchPipe[5];
    RhiPipeline pMask = batchPipe[6];
    RhiPipeline pTex = batchPipe[7];
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
    RhiBuffer ring = rhi_create_buffer(&rd);
    RhiBufferDesc vd = {sizeof(verts), RHI_BUF_VERTEX | RHI_BUF_COPY_DST, RHI_MEM_DEVICE, "verts"};
    RhiBuffer vbuf = rhi_create_buffer(&vd);
    uint8_t *map = rhi_map_buffer(ring);
    if (!ring.id || !vbuf.id || !map) {
        rhi_test_log("FAIL buffers\n");
        rhi_shutdown();
        return 1;
    }
    if (rhi_map_buffer(ring) != map) {
        rhi_test_log("FAIL rhi_map_buffer is not persistent\n");
        failures++;
    }

    /* textures */
    RhiTextureDesc td = {W, H, 1, CF, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "scene", 0};
    RhiTexture scene = rhi_create_texture(&td);
    RhiTextureDesc dsd = {W, H, 1, DF, RHI_TEX_DEPTH_STENCIL | RHI_TEX_SAMPLED, "depth", 0};
    RhiTexture depth = rhi_create_texture(&dsd);
    RhiTextureDesc ud = {4, 4, 1, RHI_FMT_RGBA8_UINT, RHI_TEX_RENDER_TARGET, "uint", 0};
    RhiTexture utex = rhi_create_texture(&ud);
    RhiTextureDesc xd = {2, 2, 1, CF, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "tex2x2", 0};
    RhiTexture tex = rhi_create_texture(&xd);
    RhiTextureDesc r8d = {4, 4, 1, RHI_FMT_R8_UNORM, RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST, "r8", 0};
    RhiTexture r8a = rhi_create_texture(&r8d);
    RhiTexture r8b = rhi_create_texture(&r8d);
    RhiSamplerDesc sd = {RHI_FILTER_NEAREST,
                         RHI_FILTER_NEAREST,
                         RHI_FILTER_NEAREST,
                         RHI_WRAP_CLAMP,
                         RHI_WRAP_CLAMP,
                         1.0f,
                         0.0f,
                         0.0f,
                         0.0f};
    RhiSampler smp = rhi_create_sampler(&sd);
    if (!scene.id || !depth.id || !utex.id || !tex.id || !r8a.id || !r8b.id || !smp.id) {
        rhi_test_log("FAIL textures\n");
        rhi_shutdown();
        return 1;
    }

    static const uint8_t texels[16] = {10, 20,  30,  40,  50,  60,  70,  80,
                                       90, 100, 110, 120, 130, 140, 150, 160};

    for (int frame = 0; frame < 3; frame++) {
        rhi_wait_frame();
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
        const RhiBindGroup g0 = rhi_create_bind_group(&bd0);
        const uint32_t cbOff[3] = {0, ua, 2 * ua};
        RhiBinding tb[2] = {{0}, {0}};
        tb[0].slot = 1;
        tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tb[0].texture = tex;
        tb[1].slot = 1;
        tb[1].type = RHI_BIND_SAMPLER;
        tb[1].sampler = smp;
        RhiBindGroupDesc tbd = {c.l2, tb, 2};
        RhiBindGroup g2 = rhi_create_bind_group(&tbd);
        if (!g0.id || !g2.id) {
            rhi_test_log("FAIL bind groups\n");
            failures++;
            break;
        }

        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            rhi_test_log("FAIL command list\n");
            failures++;
            break;
        }
        rhi_cmd_begin_label(cl, "upload");
        rhi_cmd_copy_buffer(cl, ring, offVerts, vbuf, 0, sizeof(verts));
        RhiTextureBarrier up[3] = {{tex, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {r8a, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {r8b, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST}};
        rhi_cmd_barrier(cl, up, 3);
        rhi_cmd_copy_buffer_to_texture(cl, ring, offTexels, pitchTex, tex, 0,
                                       (RhiRect){0, 0, 2, 2});
        rhi_cmd_copy_buffer_to_texture(cl, ring, offR8, pitchR8, r8a, 0, (RhiRect){0, 0, 4, 4});
        RhiTextureBarrier r8s = {r8a, RHI_STATE_COPY_DST, RHI_STATE_COPY_SRC};
        rhi_cmd_barrier(cl, &r8s, 1);
        /* full copy, then a 2x2 block from (2,2) over (0,0) */
        rhi_cmd_copy_texture(cl, r8a, (RhiRect){0, 0, 4, 4}, r8b, 0, 0);
        rhi_cmd_copy_texture(cl, r8a, (RhiRect){2, 2, 2, 2}, r8b, 0, 0);
        rhi_cmd_end_label(cl);

        RhiTextureBarrier pre[4] = {
            {tex, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {scene, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {depth, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE},
            {utex, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
        };
        rhi_cmd_barrier(cl, pre, 4);

        RhiRenderPassDesc rp = {0};
        rp.color[0] =
            (RhiColorAttachment){scene,
                                 RHI_LOAD_CLEAR,
                                 {64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f},
                                 RHI_STORE_STORE,
                                 false};
        rp.colorCount = 1;
        rp.depth = (RhiDepthAttachment){depth, RHI_LOAD_CLEAR,  RHI_LOAD_CLEAR,  0.0f, 0,
                                        false, RHI_STORE_STORE, RHI_STORE_STORE, false};
        rp.width = W;
        rp.height = H;
        rhi_cmd_begin_label(cl, "scene");
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_set_vertex_buffer(cl, 0, vbuf, 0);

        if (dual) {
            rhi_cmd_set_pipeline(cl, pDualColor);
            rhi_cmd_set_bind_group_offsets(cl, 0, g0, &cbOff[0], 1);
            rhi_cmd_draw(cl, 6, Q_DUAL_COLOR * 6, 1);
            rhi_cmd_set_pipeline(cl, pDualAlpha);
            rhi_cmd_set_bind_group_offsets(cl, 0, g0, &cbOff[1], 1);
            rhi_cmd_draw(cl, 6, Q_DUAL_ALPHA * 6, 1);
        }

        rhi_cmd_set_pipeline(cl, pDecr);
        rhi_cmd_set_stencil_ref(cl, 0);
        rhi_cmd_draw(cl, 6, Q_DECR * 6, 1);
        rhi_cmd_set_pipeline(cl, pEqual);
        rhi_cmd_set_stencil_ref(cl, 255);
        rhi_cmd_draw(cl, 6, Q_DECR_EQ * 6, 1);

        rhi_cmd_set_pipeline(cl, pReplace);
        rhi_cmd_set_stencil_ref(cl, 255);
        rhi_cmd_draw(cl, 6, Q_REPLACE * 6, 1);
        rhi_cmd_set_pipeline(cl, pIncr);
        rhi_cmd_draw(cl, 6, Q_INCR * 6, 1);
        rhi_cmd_set_pipeline(cl, pEqual);
        rhi_cmd_set_stencil_ref(cl, 0);
        rhi_cmd_draw(cl, 6, Q_INCR_EQ * 6, 1);

        rhi_cmd_set_pipeline(cl, pDepth);
        rhi_cmd_draw(cl, 6, Q_Z50 * 6, 1);
        rhi_cmd_draw(cl, 6, Q_Z25 * 6, 1);
        rhi_cmd_draw(cl, 6, Q_Z75 * 6, 1);
        rhi_cmd_draw(cl, 6, Q_Z75_EQ * 6, 1);

        rhi_cmd_set_pipeline(cl, pMask);
        rhi_cmd_draw(cl, 6, Q_MASK * 6, 1);

        rhi_cmd_set_pipeline(cl, pTex);
        rhi_cmd_set_bind_group(cl, 2, g2);
        rhi_cmd_set_vertex_buffer(cl, 0, ring, offTexVerts);
        rhi_cmd_set_index_buffer(cl, ring, offIdx, false);
        rhi_cmd_draw_indexed(cl, 6, 0, 0, 1);
        rhi_cmd_end_render_pass(cl);
        rhi_cmd_end_label(cl);

        RhiRenderPassDesc up2 = {0};
        up2.color[0] = (RhiColorAttachment){
            utex, RHI_LOAD_CLEAR, {1.0f, 2.0f, 3.0f, 4.0f}, RHI_STORE_STORE, false};
        up2.colorCount = 1;
        up2.width = 4;
        up2.height = 4;
        rhi_cmd_begin_render_pass(cl, &up2);
        rhi_cmd_set_pipeline(cl, pUint);
        rhi_cmd_set_bind_group_offsets(cl, 0, g0, &cbOff[2], 1);
        rhi_cmd_set_vertex_buffer(cl, 0, vbuf, 0);
        rhi_cmd_draw(cl, 6, Q_UINT * 6, 1);
        rhi_cmd_end_render_pass(cl);

        RhiTextureBarrier post[4] = {
            {scene, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
            {depth, RHI_STATE_DEPTH_WRITE, RHI_STATE_COPY_SRC},
            {utex, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
            {r8b, RHI_STATE_COPY_DST, RHI_STATE_COPY_SRC},
        };
        rhi_cmd_barrier(cl, post, 4);
        rhi_end_commands(cl);
        rhi_submit(cl);

        /* --- checks --- */
        static uint8_t img[W * H * 4];
        uint32_t pitch = 0;
        if (!rhi_readback_texture(scene, RHI_ASPECT_COLOR, img, sizeof(img), &pitch) ||
            pitch != W * 4) {
            rhi_test_log("FAIL scene readback\n");
            failures++;
            break;
        }
        if (dual) {
            expectCell(img, pitch, 0, 255, 64, 0, 64, frame);
            expectCell(img, pitch, 1, 96, 96, 96, 64, frame);
        } else {
            expectCell(img, pitch, 0, 64, 64, 64, 64, frame);
            expectCell(img, pitch, 1, 64, 64, 64, 64, frame);
        }
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
        if (!lim->depthReadback) {
            /* D24S8 gives no float depth; the readback must fail cleanly */
            if (rhi_readback_texture(depth, RHI_ASPECT_DEPTH, dep, sizeof(dep), &pitch)) {
                rhi_test_log("FAIL depth readback succeeded without depthReadback\n");
                failures++;
            }
        } else if (!rhi_readback_texture(depth, RHI_ASPECT_DEPTH, dep, sizeof(dep), &pitch) ||
                   pitch != W * 4) {
            rhi_test_log("FAIL depth readback\n");
            failures++;
            break;
        } else {
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    int cell = (y / CELL) * 4 + x / CELL;
                    float want = cell == 4 ? 0.75f : 0.0f;
                    if (dep[y * W + x] != want) {
                        rhi_test_log("FAIL frame %d depth (%d,%d) = %.9g, expected %.9g\n", frame,
                                     x, y, (double)dep[y * W + x], (double)want);
                        failures++;
                        y = H;
                        break;
                    }
                }
            }
        }

        uint8_t u[4 * 4 * 4];
        if (!rhi_readback_texture(utex, RHI_ASPECT_COLOR, u, sizeof(u), &pitch) || pitch != 16) {
            rhi_test_log("FAIL uint readback\n");
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
        if (!rhi_readback_texture(r8b, RHI_ASPECT_COLOR, r8, sizeof(r8), &pitch) || pitch != 4) {
            rhi_test_log("FAIL r8 readback\n");
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
                    rhi_test_log("FAIL frame %d r8 (%d,%d) = %u, expected %u\n", frame, x, y,
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
        rhi_wait_frame();
        bcCell(&c);
    }
    if (!failures) {
        msaaCell(&c);
    }
    if (!failures) {
        dstReadCell(&c, RHI_DST_READ_STORAGE);
        dstReadCell(&c, RHI_DST_READ_ATTACHMENT);
    }

    /* destroys are deferred; the frames in flight retire them */
    rhi_destroy_pipeline(pUint);
    rhi_destroy_texture(r8b);
    rhi_destroy_buffer(vbuf);
    rhi_destroy_sampler(smp);
    rhi_destroy_shader(c.psUint);
    rhi_destroy_shader(c.psDepthResolve);
    rhi_destroy_shader(c.psRov);
    rhi_destroy_shader(c.psSubpass);
    rhi_wait_frame();
    rhi_wait_frame();
    rhi_wait_idle();
    rhi_shutdown();

    uint32_t verr = cfg->validationErrors ? cfg->validationErrors() : 0u;
    if (verr) {
        rhi_test_log("FAIL %u validation error(s)\n", verr);
        failures++;
    }
    if (failures) {
        rhi_test_log("%s: %d failure(s)\n", s_label, failures);
        return 1;
    }
    rhi_test_log("%s: all checks passed\n", s_label);
    return 0;
}
