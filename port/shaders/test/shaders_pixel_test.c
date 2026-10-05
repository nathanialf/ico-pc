/* shaders_pixel_test.c: sprite.hlsl, blit.hlsl and blend_int.hlsl run
 * through the Vulkan RHI (lavapipe in the container), checked texel by
 * texel. The expected values come from gs_math.hlsli compiled as C, the
 * same arithmetic the shaders use, so a shader that disagrees with the GS
 * formulas fails here.
 *
 *   pass 1  72x8 RGBA8 target, nine 8x8 cells drawn with sprite_*_vs and
 *           sprite_ps: untextured, textured modulate, alpha test discard
 *           and pass, dual-source LERP_AS at As 0x80 / 0x40, additive with
 *           As 0xFF through DF_PREMUL (exact) and through the plain
 *           dual-source factor (measured, reported), additive with PABE
 *   pass 2  blit_ps: identity tint copy of pass 1 (exact), then tinted
 *   pass 3  blend_int_ps: three GS blends on RGBA8_UINT textures (exact)
 *
 * Exit 0 on success, 1 on a mismatch, 77 (skipped) without a Vulkan device. */
#include "hlsl_shim.h"
#include "gs_math.hlsli"
#include "rhi.h"
#include "shader_consts.h"
#include "shaders_gen.h"
#include "vk/rhi_vk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 72
#define H 8
#define CELL 8

static int failures;

static void failf(const char *what, int x, int y, const uint8_t *got, const int *want)
{
    if (failures < 30) {
        printf("FAIL %s (%d,%d): got %3u %3u %3u %3u, expected %3d %3d %3d %3d\n", what, x, y,
               got[0], got[1], got[2], got[3], want[0], want[1], want[2], want[3]);
    }
    failures++;
}

static void expect(const char *what, const uint8_t *img, uint32_t pitch, int x, int y, int r, int g,
                   int b, int a, int tol)
{
    const uint8_t *p = img + (size_t)y * pitch + (size_t)x * 4;
    const int want[4] = {r, g, b, a};
    for (int k = 0; k < 4; k++) {
        if (abs((int)p[k] - want[k]) > tol) {
            failf(what, x, y, p, want);
            return;
        }
    }
}

/* every pixel of cell c (x range [c*8, c*8+8), all rows) */
static void expectCell(const char *what, const uint8_t *img, uint32_t pitch, int cell, int r, int g,
                       int b, int a, int tol)
{
    for (int y = 0; y < H; y++) {
        for (int x = cell * CELL; x < cell * CELL + CELL; x++) {
            expect(what, img, pitch, x, y, r, g, b, a, tol);
        }
    }
}

static RhiShader makeShader(const char *name)
{
    const IcoShaderBlob *b = ico_FindShader(name);
    if (!b) {
        printf("FAIL shader %s not in the table\n", name);
        failures++;
        return (RhiShader){0};
    }
    RhiShaderDesc d = {b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT,
                       b->spirv, b->spirv_len, b->entry, b->name};
    RhiShader s = rhi_CreateShader(&d);
    if (!s.id) {
        printf("FAIL shader %s not created\n", name);
        failures++;
    }
    return s;
}

static const RhiBlendState kOpaque = {.writeMask = 0xF};

static RhiPipeline makePipeline(RhiShader vs, RhiShader ps, const RhiBindGroupLayout *layouts,
                                int sprite, RhiFormat fmt, const RhiBlendState *blend,
                                const char *name)
{
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    RhiPipelineDesc d = {0};
    d.vertex = vs;
    d.fragment = ps;
    d.layouts = layouts;
    d.layoutCount = 3;
    if (sprite) {
        d.vertexBindings = &vb;
        d.vertexBindingCount = 1;
        d.vertexAttrs = va;
        d.vertexAttrCount = 4;
    }
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0] = *blend;
    d.colorFormats[0] = fmt;
    d.colorCount = 1;
    d.depthFormat = RHI_FMT_UNKNOWN;
    d.debugName = name;
    RhiPipeline p = rhi_CreatePipeline(&d);
    if (!p.id) {
        printf("FAIL pipeline %s not created\n", name);
        failures++;
    }
    return p;
}

/* a quad over cell c, texels (0,0)-(tw,th) */
static void cellQuad(IcoSpriteVertex *v, int cell, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                     float tw, float th)
{
    const uint32_t x0 = (uint32_t)(cell * CELL) * 16u, x1 = x0 + CELL * 16u;
    const uint32_t y0 = 0, y1 = H * 16u;
    const uint32_t px[6] = {x0, x1, x0, x0, x1, x1};
    const uint32_t py[6] = {y0, y0, y1, y1, y0, y1};
    const float pu[6] = {0, tw, 0, 0, tw, tw};
    const float pv[6] = {0, 0, th, th, 0, th};
    for (int i = 0; i < 6; i++) {
        v[i] = (IcoSpriteVertex){(uint16_t)px[i], (uint16_t)py[i], 0, {r, g, b, a}, pu[i], pv[i]};
    }
}

enum {
    Q_UNTEX,
    Q_TEX,
    Q_ATEST_FAIL,
    Q_ATEST_PASS,
    Q_LERP80,
    Q_LERP40,
    Q_ADD_FF,
    Q_PABE,
    Q_ADD_PLAIN,
    Q_COUNT
};

static void setTex(IcoDrawCB *cb, float w, float h)
{
    cb->tex[0] = w;
    cb->tex[1] = h;
    cb->tex[2] = 1.0f / w;
    cb->tex[3] = 1.0f / h;
}

int main(void)
{
    RhiDeviceDesc dd = {NULL, false, true, "shaders_pixel_test"};
    if (!rhi_Init(&dd)) {
        printf("SKIP shaders_pixel_test: no usable Vulkan device\n");
        return 77;
    }
    printf("shaders_pixel_test: adapter %s\n", rhi_AdapterName());
    if (rhi_Limits()->uniformAlign > 512 || 512 % rhi_Limits()->uniformAlign) {
        printf("FAIL uniformAlign %u does not divide 512\n", rhi_Limits()->uniformAlign);
        failures++;
    }
    const uint32_t FS = 1u << RHI_STAGE_FRAGMENT, VS = 1u << RHI_STAGE_VERTEX;

    static const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
    static const RhiBindSlot s1[1] = {{1, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
    /* t2: sprite_ps's DATE snapshot (wave 2), read only under DF_DATE */
    static const RhiBindSlot s2a[3] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                       {1, RHI_BIND_SAMPLER, FS},
                                       {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    static const RhiBindSlot s2b[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                       {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    RhiBindGroupLayout l0 = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s0, 1, "frame"});
    RhiBindGroupLayout l1 = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s1, 1, "draw"});
    RhiBindGroupLayout l2a = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s2a, 3, "texsmp"});
    RhiBindGroupLayout l2b = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s2b, 2, "tex2"});
    const RhiBindGroupLayout layA[3] = {l0, l1, l2a}, layB[3] = {l0, l1, l2b};

    RhiShader svsUi = makeShader("sprite_ui_vs"), svsWorld = makeShader("sprite_world_vs");
    RhiShader sps = makeShader("sprite_ps");
    RhiShader bvs = makeShader("blit_vs"), bps = makeShader("blit_ps");
    RhiShader nvs = makeShader("blend_int_vs"), nps = makeShader("blend_int_ps");

    const RhiBlendState lerp = {true,
                                RHI_BF_SRC1_COLOR,
                                RHI_BF_ONE_MINUS_SRC1_COLOR,
                                RHI_BO_ADD,
                                RHI_BF_ONE,
                                RHI_BF_ZERO,
                                RHI_BO_ADD,
                                0xF};
    const RhiBlendState add = {true,       RHI_BF_SRC1_COLOR, RHI_BF_ONE, RHI_BO_ADD,
                               RHI_BF_ONE, RHI_BF_ZERO,       RHI_BO_ADD, 0xF};
    RhiPipeline pOpaque =
        makePipeline(svsUi, sps, layA, 1, RHI_FMT_RGBA8_UNORM, &kOpaque, "opaque");
    RhiPipeline pLerp = makePipeline(svsUi, sps, layA, 1, RHI_FMT_RGBA8_UNORM, &lerp, "lerp");
    RhiPipeline pAdd = makePipeline(svsUi, sps, layA, 1, RHI_FMT_RGBA8_UNORM, &add, "add");
    const RhiBlendState addOne = {true,       RHI_BF_ONE,  RHI_BF_ONE, RHI_BO_ADD,
                                  RHI_BF_ONE, RHI_BF_ZERO, RHI_BO_ADD, 0xF};
    RhiPipeline pAddPremul =
        makePipeline(svsUi, sps, layA, 1, RHI_FMT_RGBA8_UNORM, &addOne, "addPremul");
    RhiPipeline pAddWorld =
        makePipeline(svsWorld, sps, layA, 1, RHI_FMT_RGBA8_UNORM, &add, "addWorld");
    RhiPipeline pBlit = makePipeline(bvs, bps, layA, 0, RHI_FMT_RGBA8_UNORM, &kOpaque, "blit");
    RhiPipeline pInt = makePipeline(nvs, nps, layB, 0, RHI_FMT_RGBA8_UINT, &kOpaque, "blendInt");
    if (failures) {
        rhi_Shutdown();
        return 1;
    }

    /* ---- data ---- */
    IcoSpriteVertex verts[Q_COUNT * 6];
    cellQuad(&verts[Q_UNTEX * 6], 0, 0x80, 0x40, 0x20, 0x80, 0, 0);
    cellQuad(&verts[Q_TEX * 6], 1, 0x40, 0x80, 0xFF, 0x80, 2, 2);
    cellQuad(&verts[Q_ATEST_FAIL * 6], 2, 200, 100, 50, 0x40, 0, 0);
    cellQuad(&verts[Q_ATEST_PASS * 6], 3, 10, 20, 30, 0x41, 0, 0);
    cellQuad(&verts[Q_LERP80 * 6], 4, 200, 100, 50, 0x80, 0, 0);
    cellQuad(&verts[Q_LERP40 * 6], 5, 200, 100, 50, 0x40, 0, 0);
    cellQuad(&verts[Q_ADD_FF * 6], 6, 64, 64, 64, 0xFF, 0, 0);
    cellQuad(&verts[Q_PABE * 6], 7, 90, 90, 90, 0x7F, 0, 0);
    cellQuad(&verts[Q_ADD_PLAIN * 6], 8, 64, 64, 64, 0xFF, 0, 0);

    static const uint8_t texels[4][4] = {
        {200, 100, 50, 0xFF}, {10, 20, 30, 0x80}, {255, 255, 255, 0x40}, {0, 129, 127, 0}};

    /* blend_int inputs: 4x4 UINT, a different value per texel */
    uint8_t srcInt[16 * 4], dstInt[16 * 4];
    for (int i = 0; i < 16; i++) {
        srcInt[i * 4 + 0] = (uint8_t)(i * 17 + 3);
        srcInt[i * 4 + 1] = (uint8_t)(255 - i * 13);
        srcInt[i * 4 + 2] = (uint8_t)(i * 31);
        srcInt[i * 4 + 3] = (uint8_t)(i * 16 + 5); /* As */
        dstInt[i * 4 + 0] = (uint8_t)(250 - i * 9);
        dstInt[i * 4 + 1] = (uint8_t)(i * 7 + 100);
        dstInt[i * 4 + 2] = (uint8_t)(i * 5);
        dstInt[i * 4 + 3] = (uint8_t)(i * 3);
    }

    /* ---- resources ---- */
    const uint64_t offVerts = 0, offTex = 4096, offSrc = 4608, offDst = 5120, offUbo = 8192;
    const uint64_t ringSize = 8192 + 64 * 256;
    RhiBuffer ring = rhi_CreateBuffer(&(RhiBufferDesc){
        ringSize, RHI_BUF_VERTEX | RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "ring"});
    uint8_t *map = rhi_MapBuffer(ring);
    if (!ring.id || !map) {
        printf("FAIL ring\n");
        rhi_Shutdown();
        return 1;
    }
    const uint32_t RT = RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC;
    RhiTexture A = rhi_CreateTexture(
        &(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT | RHI_TEX_SAMPLED, "A"});
    RhiTexture B = rhi_CreateTexture(&(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT, "B"});
    RhiTexture C = rhi_CreateTexture(&(RhiTextureDesc){W, H, 1, RHI_FMT_RGBA8_UNORM, RT, "C"});
    RhiTexture tex = rhi_CreateTexture(&(RhiTextureDesc){
        2, 2, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "tex2x2"});
    RhiTexture usrc = rhi_CreateTexture(
        &(RhiTextureDesc){4, 4, 1, RHI_FMT_RGBA8_UINT, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "usrc"});
    RhiTexture udst = rhi_CreateTexture(
        &(RhiTextureDesc){4, 4, 1, RHI_FMT_RGBA8_UINT, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "udst"});
    RhiTexture uout[3];
    for (int i = 0; i < 3; i++) {
        uout[i] = rhi_CreateTexture(&(RhiTextureDesc){4, 4, 1, RHI_FMT_RGBA8_UINT, RT, "uout"});
    }
    RhiSampler smp = rhi_CreateSampler(&(RhiSamplerDesc){RHI_FILTER_NEAREST, RHI_FILTER_NEAREST,
                                                         RHI_FILTER_NEAREST, RHI_WRAP_CLAMP,
                                                         RHI_WRAP_CLAMP, 1.0f, 0.0f, 0.0f, 0.0f});
    if (!A.id || !B.id || !C.id || !tex.id || !usrc.id || !udst.id || !uout[2].id || !smp.id) {
        printf("FAIL resources\n");
        rhi_Shutdown();
        return 1;
    }

    /* ---- uniforms ---- */
    IcoFrameCB fcb;
    memset(&fcb, 0, sizeof(fcb));
    fcb.target[0] = W;
    fcb.target[1] = H;
    fcb.target[2] = 1.0f / W;
    fcb.target[3] = 1.0f / H;
    fcb.space[0][0] = fcb.space[0][1] = fcb.space[1][0] = fcb.space[1][1] = 1.0f;
    fcb.z[0] = 1.0f / 16777216.0f;

    enum {
        D_UNTEX,
        D_TEX,
        D_ATEST_FAIL,
        D_ATEST_PASS,
        D_LERP80,
        D_LERP40,
        D_ADD_FF,
        D_PABE,
        D_ADD_PLAIN,
        D_BLIT_ID,
        D_BLIT_TINT,
        D_INT0,
        D_INT1,
        D_INT2,
        D_COUNT
    };

    IcoDrawCB dcb[D_COUNT];
    memset(dcb, 0, sizeof(dcb));
    for (int i = 0; i < D_COUNT; i++) {
        setTex(&dcb[i], 2, 2);
    }
    dcb[D_TEX].mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
    dcb[D_ATEST_FAIL].mode[2] = ATST_GREATER | (1u << 8);
    dcb[D_ATEST_FAIL].mode[3] = 0x40;
    dcb[D_ATEST_PASS].mode[2] = ATST_GREATER | (1u << 8);
    dcb[D_ATEST_PASS].mode[3] = 0x40;
    dcb[D_ADD_FF].mode[0] = ICO_DF_PREMUL;
    dcb[D_PABE].mode[0] = ICO_DF_PABE;
    for (int i = D_BLIT_ID; i <= D_BLIT_TINT; i++) {
        setTex(&dcb[i], W, H);
        dcb[i].mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
        dcb[i].uvRect[2] = W;
        dcb[i].uvRect[3] = H;
    }
    for (int k = 0; k < 4; k++) {
        dcb[D_BLIT_ID].col[k] = 0x80;
    }
    const uint32_t tint[4] = {0x40, 0x80, 0xFF, 0x60};
    memcpy(dcb[D_BLIT_TINT].col, tint, sizeof(tint));
    const uint32_t regs[3] = {0x44, 0x64, 0x48}; /* LERP_AS, LERP_FIX 0x70, add wrap */
    const uint32_t fix[3] = {0, 0x70, 0};
    const uint32_t clampMode[3] = {1, 1, 0};
    for (int i = 0; i < 3; i++) {
        dcb[D_INT0 + i].blend[0] = regs[i];
        dcb[D_INT0 + i].blend[1] = fix[i];
        dcb[D_INT0 + i].blend[2] = clampMode[i];
    }

    int exitCode = 0;
    for (int frame = 0; frame < 2; frame++) {
        rhi_WaitFrame();
        memcpy(map + offVerts, verts, sizeof(verts));
        memcpy(map + offTex, texels, sizeof(texels));
        memcpy(map + offSrc, srcInt, sizeof(srcInt));
        memcpy(map + offDst, dstInt, sizeof(dstInt));
        RhiBindGroup g1[D_COUNT];
        for (int i = 0; i < D_COUNT; i++) {
            memcpy(map + offUbo + (uint64_t)(i + 1) * 512, &dcb[i], sizeof(IcoDrawCB));
            RhiBinding b = {0};
            b.slot = 1;
            b.type = RHI_BIND_UNIFORM_BUFFER;
            b.buffer = ring;
            b.offset = offUbo + (uint64_t)(i + 1) * 512;
            b.size = sizeof(IcoDrawCB);
            g1[i] = rhi_CreateBindGroup(&(RhiBindGroupDesc){l1, &b, 1});
        }
        memcpy(map + offUbo, &fcb, sizeof(fcb));
        RhiBinding fb = {0};
        fb.slot = 0;
        fb.type = RHI_BIND_UNIFORM_BUFFER;
        fb.buffer = ring;
        fb.offset = offUbo;
        fb.size = sizeof(IcoFrameCB);
        RhiBindGroup g0 = rhi_CreateBindGroup(&(RhiBindGroupDesc){l0, &fb, 1});

        RhiBinding t2x2[3] = {{0}, {0}, {0}};
        t2x2[0].slot = 1;
        t2x2[0].type = RHI_BIND_SAMPLED_TEXTURE;
        t2x2[0].texture = tex;
        t2x2[1].slot = 1;
        t2x2[1].type = RHI_BIND_SAMPLER;
        t2x2[1].sampler = smp;
        t2x2[2].slot = 2;
        t2x2[2].type = RHI_BIND_SAMPLED_TEXTURE;
        t2x2[2].texture = tex;
        RhiBindGroup g2tex = rhi_CreateBindGroup(&(RhiBindGroupDesc){l2a, t2x2, 3});
        RhiBinding tA[3] = {t2x2[0], t2x2[1], t2x2[2]};
        tA[0].texture = A;
        RhiBindGroup g2A = rhi_CreateBindGroup(&(RhiBindGroupDesc){l2a, tA, 3});
        RhiBinding tI[2] = {{0}, {0}};
        tI[0].slot = 1;
        tI[0].type = RHI_BIND_SAMPLED_TEXTURE;
        tI[0].texture = usrc;
        tI[1].slot = 2;
        tI[1].type = RHI_BIND_SAMPLED_TEXTURE;
        tI[1].texture = udst;
        RhiBindGroup g2int = rhi_CreateBindGroup(&(RhiBindGroupDesc){l2b, tI, 2});

        RhiCommandList cl = rhi_BeginCommands();
        if (!cl.id || !g0.id || !g2tex.id || !g2A.id || !g2int.id) {
            printf("FAIL setup\n");
            failures++;
            break;
        }
        RhiTextureBarrier up[3] = {{tex, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {usrc, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST},
                                   {udst, RHI_STATE_UNDEFINED, RHI_STATE_COPY_DST}};
        rhi_CmdBarrier(cl, up, 3);
        rhi_CmdCopyBufferToTexture(cl, ring, offTex, 8, tex, 0, (RhiRect){0, 0, 2, 2});
        rhi_CmdCopyBufferToTexture(cl, ring, offSrc, 16, usrc, 0, (RhiRect){0, 0, 4, 4});
        rhi_CmdCopyBufferToTexture(cl, ring, offDst, 16, udst, 0, (RhiRect){0, 0, 4, 4});
        RhiTextureBarrier pre[8] = {
            {tex, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {usrc, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {udst, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ},
            {A, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {B, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {C, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {uout[0], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
            {uout[1], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET},
        };
        rhi_CmdBarrier(cl, pre, 8);
        RhiTextureBarrier pre2 = {uout[2], RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
        rhi_CmdBarrier(cl, &pre2, 1);

        /* pass 1: the sprite cells; the clear (64,64,64,64) shows through
         * where nothing is drawn */
        RhiRenderPassDesc rp = {0};
        rp.color[0] = (RhiColorAttachment){
            A, RHI_LOAD_CLEAR, {64 / 255.f, 64 / 255.f, 64 / 255.f, 64 / 255.f}};
        rp.colorCount = 1;
        rp.width = W;
        rp.height = H;
        rhi_CmdBeginRenderPass(cl, &rp);
        rhi_CmdSetVertexBuffer(cl, 0, ring, offVerts);
        rhi_CmdSetBindGroup(cl, 0, g0);
        rhi_CmdSetBindGroup(cl, 2, g2tex);

        struct {
            int q, d;
            RhiPipeline p;
        } draws[Q_COUNT] = {
            {Q_UNTEX, D_UNTEX, pOpaque},           {Q_TEX, D_TEX, pOpaque},
            {Q_ATEST_FAIL, D_ATEST_FAIL, pOpaque}, {Q_ATEST_PASS, D_ATEST_PASS, pOpaque},
            {Q_LERP80, D_LERP80, pLerp},           {Q_LERP40, D_LERP40, pLerp},
            {Q_ADD_FF, D_ADD_FF, pAddPremul},      {Q_PABE, D_PABE, pAddWorld},
            {Q_ADD_PLAIN, D_ADD_PLAIN, pAdd},
        };

        for (int i = 0; i < Q_COUNT; i++) {
            rhi_CmdSetPipeline(cl, draws[i].p);
            rhi_CmdSetBindGroup(cl, 1, g1[draws[i].d]);
            rhi_CmdDraw(cl, 6, (uint32_t)draws[i].q * 6, 1);
        }
        rhi_CmdEndRenderPass(cl);
        RhiTextureBarrier aRead = {A, RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ};
        rhi_CmdBarrier(cl, &aRead, 1);

        /* pass 2: blit A to B (identity tint) and to C (tinted) */
        for (int k = 0; k < 2; k++) {
            RhiRenderPassDesc bp = {0};
            bp.color[0] = (RhiColorAttachment){k ? C : B, RHI_LOAD_DONT_CARE, {0}};
            bp.colorCount = 1;
            bp.width = W;
            bp.height = H;
            rhi_CmdBeginRenderPass(cl, &bp);
            rhi_CmdSetPipeline(cl, pBlit);
            rhi_CmdSetBindGroup(cl, 0, g0);
            rhi_CmdSetBindGroup(cl, 1, g1[D_BLIT_ID + k]);
            rhi_CmdSetBindGroup(cl, 2, g2A);
            rhi_CmdDraw(cl, 3, 0, 1);
            rhi_CmdEndRenderPass(cl);
        }

        /* pass 3: three integer blends */
        for (int k = 0; k < 3; k++) {
            RhiRenderPassDesc ip = {0};
            ip.color[0] = (RhiColorAttachment){uout[k], RHI_LOAD_DONT_CARE, {0}};
            ip.colorCount = 1;
            ip.width = 4;
            ip.height = 4;
            rhi_CmdBeginRenderPass(cl, &ip);
            rhi_CmdSetPipeline(cl, pInt);
            rhi_CmdSetBindGroup(cl, 0, g0);
            rhi_CmdSetBindGroup(cl, 1, g1[D_INT0 + k]);
            rhi_CmdSetBindGroup(cl, 2, g2int);
            rhi_CmdDraw(cl, 3, 0, 1);
            rhi_CmdEndRenderPass(cl);
        }
        RhiTextureBarrier post[6] = {{A, RHI_STATE_SHADER_READ, RHI_STATE_COPY_SRC},
                                     {B, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {C, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {uout[0], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {uout[1], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC},
                                     {uout[2], RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC}};
        rhi_CmdBarrier(cl, post, 6);
        rhi_EndCommands(cl);
        rhi_Submit(cl);

        /* ---- checks ---- */
        static uint8_t imgA[W * H * 4], imgB[W * H * 4], imgC[W * H * 4];
        uint32_t pitch = 0;
        if (!rhi_ReadbackTexture(A, RHI_ASPECT_COLOR, imgA, sizeof(imgA), &pitch) ||
            !rhi_ReadbackTexture(B, RHI_ASPECT_COLOR, imgB, sizeof(imgB), &pitch) ||
            !rhi_ReadbackTexture(C, RHI_ASPECT_COLOR, imgC, sizeof(imgC), &pitch)) {
            printf("FAIL readback\n");
            failures++;
            break;
        }
        expectCell("untextured", imgA, pitch, 0, 0x80, 0x40, 0x20, 0x80, 0);
        /* textured: the 2x2 texture over 8x8, texel = 4x4 pixels */
        for (int y = 0; y < H; y++) {
            for (int x = CELL; x < 2 * CELL; x++) {
                const uint8_t *t = texels[(y / 4) * 2 + ((x - CELL) / 4)];
                const uint32_t c[4] = {0x40, 0x80, 0xFF, 0x80};
                expect("textured", imgA, pitch, x, y, (int)gs_tfx_mod(t[0], c[0]),
                       (int)gs_tfx_mod(t[1], c[1]), (int)gs_tfx_mod(t[2], c[2]),
                       (int)gs_tfx_mod(t[3], c[3]), 0);
            }
        }
        expectCell("alpha test discard", imgA, pitch, 2, 64, 64, 64, 64, 0);
        expectCell("alpha test pass", imgA, pitch, 3, 10, 20, 30, 0x41, 0);
        expectCell("lerp As=0x80", imgA, pitch, 4, 200, 100, 50, 0x80, 0);
        /* hardware unorm blend: within one LSB of the integer formula */
        expectCell("lerp As=0x40", imgA, pitch, 5, gs_blend_reg_ch(0x44, 200, 64, 0x40, 0, 0, 1),
                   gs_blend_reg_ch(0x44, 100, 64, 0x40, 0, 0, 1),
                   gs_blend_reg_ch(0x44, 50, 64, 0x40, 0, 0, 1), 0x40, 1);
        /* Cs*As + Cd with As = 0xFF: the term is 64 * 255 >> 7 = 127. With
         * DF_PREMUL the shader computes it, exactly. With the plain
         * dual-source factor 255/128 the hardware clamps the factor to 1.0
         * on UNORM targets (reported, not asserted: driver behaviour). */
        {
            int want = gs_blend_reg_ch(0x48, 64, 64, 0xFF, 0, 0, 1);
            expectCell("add As=0xFF, DF_PREMUL", imgA, pitch, 6, want, want, want, 0xFF, 0);
            if (frame == 0) {
                const uint8_t *p = imgA + 8 * CELL * 4;
                printf("  plain dual-source additive As=0xFF: got %u, GS formula %d (%s)\n", p[0],
                       want, p[0] == want ? "factor above 1.0 survives" : "factor clamped to 1.0");
            }
        }
        expectCell("add with PABE, As MSB clear", imgA, pitch, 7, 64, 64, 64, 0x7F, 0);

        /* blit identity: exact copy; tinted: the texture function */
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                const uint8_t *s = imgA + (size_t)y * pitch + (size_t)x * 4;
                expect("blit identity", imgB, pitch, x, y, s[0], s[1], s[2], s[3], 0);
                expect("blit tint", imgC, pitch, x, y, (int)gs_tfx_mod(s[0], tint[0]),
                       (int)gs_tfx_mod(s[1], tint[1]), (int)gs_tfx_mod(s[2], tint[2]),
                       (int)gs_tfx_mod(s[3], tint[3]), 0);
            }
        }

        /* blend_int against the arithmetic of gs_math.hlsli */
        for (int k = 0; k < 3; k++) {
            uint8_t u[4 * 4 * 4];
            uint32_t up2 = 0;
            if (!rhi_ReadbackTexture(uout[k], RHI_ASPECT_COLOR, u, sizeof(u), &up2)) {
                printf("FAIL uint readback\n");
                failures++;
                break;
            }
            for (int i = 0; i < 16; i++) {
                const uint8_t *s = &srcInt[i * 4], *d = &dstInt[i * 4];
                int want[3];
                for (int c = 0; c < 3; c++) {
                    want[c] =
                        gs_blend_reg_ch(regs[k], s[c], d[c], s[3], d[3], (int)fix[k], clampMode[k]);
                }
                expect("blend_int", u, up2, i % 4, i / 4, want[0], want[1], want[2], s[3], 0);
            }
        }
    }
    if (rhi_vk_ValidationErrorCount() != 0) {
        printf("FAIL %u validation errors\n", (unsigned)rhi_vk_ValidationErrorCount());
        failures++;
    }
    if (failures) {
        printf("shaders_pixel_test: %d failures\n", failures);
        exitCode = 1;
    } else {
        printf("shaders_pixel_test: ok\n");
    }
    rhi_Shutdown();
    return exitCode;
}
