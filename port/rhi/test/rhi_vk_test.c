/* rhi_vk_test.c: headless rendering through port/rhi/rhi.h on the Vulkan
 * backend, checked texel by texel.
 *
 * A 64x32 RGBA8 target with a D32F+S8 depth-stencil is split into eight
 * 16x16 cells, each exercising one feature the GS emulation needs
 * (docs/port/RENDER_API.md section 3):
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
 * copies, a device-local vertex buffer filled by rhi_CmdCopyBuffer, depth
 * readback, and three frames to cycle the frame slots.  Every expected
 * value is exact.
 *
 * Exit 0 on success, 1 on a mismatch, 77 (skipped) when no Vulkan device
 * can be created.  Set VK_ICD_FILENAMES to pick a driver (lavapipe in the
 * container). */
#include "rhi.h"
#include "vk/rhi_vk.h"
#include "rhi_test_spv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void fail(const char *fmt, int a, int b, int c, const uint8_t *got, const uint8_t *want)
{
    printf("FAIL ");
    printf(fmt, a, b, c);
    printf(": got %3u %3u %3u %3u, expected %3u %3u %3u %3u\n", got[0], got[1], got[2], got[3],
           want[0], want[1], want[2], want[3]);
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
        printf("FAIL pipeline %s not created\n", name);
        failures++;
    }
    return p;
}

static RhiShader makeShader(RhiShaderStage stage, const uint32_t *code, size_t size,
                            const char *entry)
{
    RhiShaderDesc d = {stage, code, size, entry, entry};
    RhiShader s = rhi_CreateShader(&d);
    if (!s.id) {
        printf("FAIL shader %s not created\n", entry);
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
        printf("     (cell %d)\n", cell);
    }
}

int main(void)
{
    RhiDeviceDesc dd = {NULL, false, true, "rhi_vk_test"};
    if (!rhi_Init(&dd)) {
        printf("SKIP rhi_vk_test: no usable Vulkan device (see messages above)\n");
        return 77;
    }
    printf("rhi_vk_test: adapter %s\n", rhi_AdapterName());
    const RhiLimits *lim = rhi_Limits();
    if (!lim->dualSourceBlend || !lim->stencilWrap || lim->uniformAlign == 0) {
        printf("FAIL limits\n");
        failures++;
    }
    const uint32_t ua = lim->uniformAlign;

    Ctx c;
    static const RhiBindSlot s0[1] = {
        {0, RHI_BIND_UNIFORM_BUFFER, (1u << RHI_STAGE_VERTEX) | (1u << RHI_STAGE_FRAGMENT)}};
    static const RhiBindSlot s2[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, 1u << RHI_STAGE_FRAGMENT},
                                      {1, RHI_BIND_SAMPLER, 1u << RHI_STAGE_FRAGMENT}};
    RhiBindGroupLayoutDesc ld0 = {s0, 1, "group0"};
    RhiBindGroupLayoutDesc ld1 = {NULL, 0, "group1"};
    RhiBindGroupLayoutDesc ld2 = {s2, 2, "group2"};
    c.l0 = rhi_CreateBindGroupLayout(&ld0);
    c.l1 = rhi_CreateBindGroupLayout(&ld1);
    c.l2 = rhi_CreateBindGroupLayout(&ld2);
    c.vs = makeShader(RHI_STAGE_VERTEX, spv_vs_main, sizeof(spv_vs_main), "vs_main");
    c.psDual = makeShader(RHI_STAGE_FRAGMENT, spv_ps_dual, sizeof(spv_ps_dual), "ps_dual");
    c.psColor = makeShader(RHI_STAGE_FRAGMENT, spv_ps_color, sizeof(spv_ps_color), "ps_color");
    c.psTex = makeShader(RHI_STAGE_FRAGMENT, spv_ps_tex, sizeof(spv_ps_tex), "ps_tex");
    c.psUint = makeShader(RHI_STAGE_FRAGMENT, spv_ps_uint, sizeof(spv_ps_uint), "ps_uint");

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
    const uint64_t offTexels = 16384, offR8 = 16384 + 256, ringSize = 32768;
    RhiBufferDesc rd = {ringSize,
                        RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC,
                        RHI_MEM_UPLOAD, "ring"};
    RhiBuffer ring = rhi_CreateBuffer(&rd);
    RhiBufferDesc vd = {sizeof(verts), RHI_BUF_VERTEX | RHI_BUF_COPY_DST, RHI_MEM_DEVICE, "verts"};
    RhiBuffer vbuf = rhi_CreateBuffer(&vd);
    uint8_t *map = rhi_MapBuffer(ring);
    if (!ring.id || !vbuf.id || !map) {
        printf("FAIL buffers\n");
        rhi_Shutdown();
        return 1;
    }
    if (rhi_MapBuffer(ring) != map) {
        printf("FAIL rhi_MapBuffer is not persistent\n");
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
        printf("FAIL textures\n");
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
        memcpy(map + offTexels, texels, sizeof(texels));
        for (int i = 0; i < 16; i++) {
            map[offR8 + i] = (uint8_t)(i * 16 + 1);
        }
        const TestCB cbs[3] = {
            {{1.0f, 0.0f, 1.0f, 0.0f}, {0, 0, 0, 0}},
            {{0.0f, 0.0f, 0.0f, 0.5f}, {0, 0, 0, 0}},
            {{0.0f, 0.0f, 0.0f, 0.0f}, {7, 128, 250, 255}},
        };
        RhiBindGroup g0[3];
        for (int i = 0; i < 3; i++) {
            memcpy(map + offUbo + (uint64_t)i * ua, &cbs[i], sizeof(TestCB));
            RhiBinding b = {0};
            b.slot = 0;
            b.type = RHI_BIND_UNIFORM_BUFFER;
            b.buffer = ring;
            b.offset = offUbo + (uint64_t)i * ua;
            b.size = sizeof(TestCB);
            RhiBindGroupDesc bd = {c.l0, &b, 1};
            g0[i] = rhi_CreateBindGroup(&bd);
        }
        RhiBinding tb[2] = {{0}, {0}};
        tb[0].slot = 1;
        tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tb[0].texture = tex;
        tb[1].slot = 1;
        tb[1].type = RHI_BIND_SAMPLER;
        tb[1].sampler = smp;
        RhiBindGroupDesc tbd = {c.l2, tb, 2};
        RhiBindGroup g2 = rhi_CreateBindGroup(&tbd);
        if (!g0[0].id || !g0[1].id || !g0[2].id || !g2.id) {
            printf("FAIL bind groups\n");
            failures++;
            break;
        }

        RhiCommandList cl = rhi_BeginCommands();
        if (!cl.id) {
            printf("FAIL command list\n");
            failures++;
            break;
        }
        rhi_CmdBeginLabel(cl, "upload");
        rhi_CmdCopyBuffer(cl, ring, offVerts, vbuf, 0, sizeof(verts));
        RhiTextureBarrier up[3] = {{tex, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {r8a, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {r8b, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST}};
        rhi_CmdBarrier(cl, up, 3);
        rhi_CmdCopyBufferToTexture(cl, ring, offTexels, 8, tex, 0, (RhiRect){0, 0, 2, 2});
        rhi_CmdCopyBufferToTexture(cl, ring, offR8, 4, r8a, 0, (RhiRect){0, 0, 4, 4});
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
        rhi_CmdSetBindGroup(cl, 0, g0[0]);
        rhi_CmdDraw(cl, 6, Q_DUAL_COLOR * 6, 1);
        rhi_CmdSetPipeline(cl, pDualAlpha);
        rhi_CmdSetBindGroup(cl, 0, g0[1]);
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
        rhi_CmdSetBindGroup(cl, 0, g0[2]);
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
            printf("FAIL scene readback\n");
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
            printf("FAIL depth readback\n");
            failures++;
            break;
        }
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int cell = (y / CELL) * 4 + x / CELL;
                float want = cell == 4 ? 0.75f : 0.0f;
                if (dep[y * W + x] != want) {
                    printf("FAIL frame %d depth (%d,%d) = %.9g, expected %.9g\n", frame, x, y,
                           (double)dep[y * W + x], (double)want);
                    failures++;
                    y = H;
                    break;
                }
            }
        }

        uint8_t u[4 * 4 * 4];
        if (!rhi_ReadbackTexture(utex, RHI_ASPECT_COLOR, u, sizeof(u), &pitch) || pitch != 16) {
            printf("FAIL uint readback\n");
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
            printf("FAIL r8 readback\n");
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
                    printf("FAIL frame %d r8 (%d,%d) = %u, expected %u\n", frame, x, y,
                           r8[y * 4 + x], want);
                    failures++;
                }
            }
        }
        if (failures) {
            break;
        }
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

    uint32_t verr = rhi_vk_ValidationErrorCount();
    if (verr) {
        printf("FAIL %u validation error(s)\n", verr);
        failures++;
    }
    if (failures) {
        printf("rhi_vk_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rhi_vk_test: all checks passed\n");
    return 0;
}
