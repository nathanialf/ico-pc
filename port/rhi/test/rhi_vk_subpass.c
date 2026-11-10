/* rhi_vk_subpass.c: a pass's own colour read in the pass with a depth target
 * bound (rhi.h RhiLimits.localRead; DATE's stencil setup's shape,
 * port/render/rd_replay_tiler.c), through whichever path the device has:
 * dynamic rendering's local read, or on the VkRenderPass path the subpass
 * read (vk_renderpass.c: the colour also the subpass's input attachment 0,
 * a by-region self-dependency, the colour in GENERAL; RhiLimits
 * .localReadSubpass).  One command list under the validation layer's
 * synchronisation checks (rhi_vk_test's environment):
 *   pass 0  T (16 x 16 RGBA8) cleared to bg, colour only;
 *   pass 1  T readable and loaded, the depth D (D32F_S8) cleared: a draw of
 *           A over the left half (ps_color), the COLOR barrier, a draw of S
 *           over the whole of T through ps_subpass (the GS LERP of S over
 *           the colour it reads: (Cs - Cd) * As >> 7 + Cd), the BACK
 *           barrier, a draw of C over the bottom four rows (ps_color);
 *   pass 2  T readable, colour only, with a COLOR barrier and no draw: on
 *           the subpass read skipped and counted (no self-dependency
 *           there), on dynamic rendering recorded;
 *   pass 3  U, an RGBA8 target made without the input-attachment usage,
 *           with D loaded: the pipeline of pass 1's A and C draws drawn
 *           again (C2 over the whole of U): the subpass read's class is the
 *           formats', so a target that is never read shares its pipelines.
 * T reads back exactly: the left half above the bottom rows the LERP of S
 * over A, the right half the LERP of S over bg, the bottom four rows C; U
 * all C2.  On the subpass read: 2 passes begun with it (1 and 3), 1 barrier
 * skipped (pass 2), 0 misfits.  Then no validation error.  Skipped (0)
 * without local read; 77 without a Vulkan device.  rhi_vk_test runs it in
 * its default run and alone with the argument `subpass` (the rhi_vk_subpass*
 * ctests: dynamic rendering, ICO_VK_FAKE_API=1.1, =1.0, the phone's limits,
 * ICO_VK_LEGACY_PASSES=1 with ICO_VK_SUBPASS_READ=1 and without). */
#include "rhi.h"
#include "rhi_test_common.h"
#include "rhi_test_spv.h"
#include "vk/rhi_vk.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int rhi_vk_subpass_cell(const RhiTestConfig *cfg);

enum { SP = 16, SP_BOTTOM = 4 };

typedef struct SpVtx {
    float x, y, z;
    uint8_t rgba[4];
    float u, v;
} SpVtx;

static int s_failures;

#define SP_CHECK(cond, ...)                                                                        \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            rhi_test_log("FAIL %s subpass read: ", cfg->label);                                    \
            rhi_test_log(__VA_ARGS__);                                                             \
            s_failures++;                                                                          \
        }                                                                                          \
    } while (0)

static void spQuad(SpVtx *v, float x0, float y0, float x1, float y1, const uint8_t c[4])
{
    const float nx0 = x0 / SP * 2.0f - 1.0f, nx1 = x1 / SP * 2.0f - 1.0f;
    const float ny0 = 1.0f - y0 / SP * 2.0f, ny1 = 1.0f - y1 / SP * 2.0f;
    const float px[6] = {nx0, nx1, nx0, nx0, nx1, nx1};
    const float py[6] = {ny0, ny0, ny1, ny1, ny0, ny1};
    for (int i = 0; i < 6; i++) {
        v[i] = (SpVtx){px[i], py[i], 0.0f, {c[0], c[1], c[2], c[3]}, 0.0f, 0.0f};
    }
}

/* the GS LERP of rhi_test.hlsl dst_lerp */
static uint8_t spLerp(int s, int d, int a)
{
    const int v = (((s - d) * a) >> 7) + d;
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

static RhiShader spShader(RhiShaderStage stage, const void *code, size_t size, const char *entry)
{
    RhiShaderDesc sd;
    memset(&sd, 0, sizeof(sd));
    sd.stage = stage;
    sd.bytecode = code;
    sd.bytecodeSize = size;
    sd.entryPoint = entry;
    sd.debugName = entry;
    return rhi_create_shader(&sd);
}

static RhiPipeline spPipeline(RhiShader vs, RhiShader ps, const RhiBindGroupLayout *layouts,
                              const char *name)
{
    static const RhiVertexBinding vb = {0, sizeof(SpVtx), false};
    static const RhiVertexAttr va[3] = {
        {0, 0, RHI_VTX_F32x3, offsetof(SpVtx, x)},
        {1, 0, RHI_VTX_U8x4_UNORM, offsetof(SpVtx, rgba)},
        {2, 0, RHI_VTX_F32x2, offsetof(SpVtx, u)},
    };
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = vs;
    d.fragment = ps;
    d.layouts = layouts;
    d.layoutCount = 4;
    d.vertexBindings = &vb;
    d.vertexBindingCount = 1;
    d.vertexAttrs = va;
    d.vertexAttrCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0].writeMask = 0xF; /* blending off: ps_subpass blends */
    d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    d.colorCount = 1;
    d.depthFormat = RHI_FMT_D32F_S8; /* depth test and write off (zeroed) */
    d.debugName = name;
    return rhi_create_pipeline(&d);
}

static void spBarrier(RhiCommandList cl, RhiTexture t, RhiState before, RhiState after)
{
    const RhiTextureBarrier b = {t, before, after};
    rhi_cmd_barrier(cl, &b, 1);
}

static void spCheck(const RhiTestConfig *cfg, RhiTexture t, const char *what,
                    void (*want)(int x, int y, uint8_t out[4]))
{
    static uint8_t img[SP * SP * 4];
    uint32_t pitch = 0;
    if (!rhi_readback_texture(t, RHI_ASPECT_COLOR, img, sizeof(img), &pitch)) {
        SP_CHECK(0, "%s: readback\n", what);
        return;
    }
    for (int y = 0; y < SP; y++) {
        for (int x = 0; x < SP; x++) {
            uint8_t w[4];
            want(x, y, w);
            const uint8_t *p = &img[(size_t)y * pitch + (size_t)x * 4u];
            if (memcmp(p, w, 4) != 0) {
                SP_CHECK(0, "%s (%d,%d): %u %u %u %u, want %u %u %u %u\n", what, x, y, p[0], p[1],
                         p[2], p[3], w[0], w[1], w[2], w[3]);
                return;
            }
        }
    }
}

static const uint8_t kBg[4] = {10, 200, 30, 0x55}, kA[4] = {200, 20, 120, 0x30},
                     kS[4] = {90, 160, 250, 0x60}, kC[4] = {255, 0, 0, 0xFF},
                     kC2[4] = {0, 0, 255, 0x7F};

static void wantT(int x, int y, uint8_t out[4])
{
    if (y >= SP - SP_BOTTOM) {
        memcpy(out, kC, 4);
        return;
    }
    const uint8_t *d = x < SP / 2 ? kA : kBg;
    for (int k = 0; k < 3; k++) {
        out[k] = spLerp(kS[k], d[k], kS[3]);
    }
    out[3] = kS[3];
}

static void wantU(int x, int y, uint8_t out[4])
{
    (void)x;
    (void)y;
    memcpy(out, kC2, 4);
}

int rhi_vk_subpass_cell(const RhiTestConfig *cfg)
{
    s_failures = 0;
    if (!rhi_create_backend(cfg->backend)) {
        rhi_test_log("SKIP %s subpass read: no %s backend\n", cfg->label, cfg->backend);
        return 77;
    }
    RhiDeviceDesc dd;
    memset(&dd, 0, sizeof(dd));
    dd.debugLayers = cfg->debugLayers;
    dd.appName = "rhi test subpass read";
    if (!rhi_init(&dd)) {
        rhi_test_log("SKIP %s subpass read: no device\n", cfg->label);
        return 77;
    }
    const RhiLimits *lim = rhi_limits();
    uint32_t p0 = 0, s0 = 0, m0 = 0;
    const bool subpass = vkr_test_subpass_read(&p0, &s0, &m0);
    SP_CHECK(subpass == lim->localReadSubpass, "the device says %d, the limits %d\n", (int)subpass,
             (int)lim->localReadSubpass);
    SP_CHECK(!subpass || (lim->localRead && vkr_test_legacy_passes()),
             "the subpass read without local read (%d) or VkRenderPass objects (%d)\n",
             (int)lim->localRead, (int)vkr_test_legacy_passes());
    if (!lim->localRead) {
        rhi_test_log("%s subpass read: no local read on this device (render passes %s): not "
                     "run\n",
                     cfg->label, vkr_test_legacy_passes() ? "legacy" : "dynamic");
        rhi_shutdown();
        return s_failures ? 1 : 0;
    }
    const RhiTexture t = rhi_create_texture(&(RhiTextureDesc){
        SP, SP, 1, RHI_FMT_RGBA8_UNORM,
        RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC | RHI_TEX_INPUT_ATTACHMENT,
        "rhi test subpass read T", 0});
    const RhiTexture u = rhi_create_texture(
        &(RhiTextureDesc){SP, SP, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                          "rhi test subpass read U (never read)", 0});
    const RhiTexture z = rhi_create_texture(&(RhiTextureDesc){
        SP, SP, 1, RHI_FMT_D32F_S8, RHI_TEX_DEPTH_STENCIL, "rhi test subpass read depth", 0});
    const RhiShader vs = spShader(RHI_STAGE_VERTEX, spv_vs_main, sizeof(spv_vs_main), "vs_main");
    const RhiShader psC =
        spShader(RHI_STAGE_FRAGMENT, spv_ps_color, sizeof(spv_ps_color), "ps_color");
    const RhiShader psR =
        spShader(RHI_STAGE_FRAGMENT, spv_ps_subpass, sizeof(spv_ps_subpass), "ps_subpass");
    const RhiBindSlot inSlot = {0, RHI_BIND_INPUT_ATTACHMENT, 1u << RHI_STAGE_FRAGMENT};
    const RhiBindGroupLayout empty =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){NULL, 0, "subpass empty"});
    const RhiBindGroupLayout lin =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){&inSlot, 1, "subpass input"});
    const RhiBindGroupLayout layouts[4] = {empty, empty, empty, lin};
    const RhiPipeline pc = spPipeline(vs, psC, layouts, "subpass colour");
    const RhiPipeline pr = spPipeline(vs, psR, layouts, "subpass read");
    RhiBuffer ring =
        rhi_create_buffer(&(RhiBufferDesc){4096, RHI_BUF_VERTEX, RHI_MEM_UPLOAD, "subpass verts"});
    uint8_t *map = ring.id ? rhi_map_buffer(ring) : NULL;
    if (!t.id || !u.id || !z.id || !vs.id || !psC.id || !psR.id || !empty.id || !lin.id || !pc.id ||
        !pr.id || !map) {
        SP_CHECK(0,
                 "resources (T %u, U %u, depth %u, shaders %u %u %u, layouts %u %u, "
                 "pipelines %u %u, ring %d)\n",
                 t.id, u.id, z.id, vs.id, psC.id, psR.id, empty.id, lin.id, pc.id, pr.id,
                 map != NULL);
    } else {
        SpVtx v[24];
        spQuad(&v[0], 0, 0, SP / 2, SP, kA);
        spQuad(&v[6], 0, 0, SP, SP, kS);
        spQuad(&v[12], 0, SP - SP_BOTTOM, SP, SP, kC);
        spQuad(&v[18], 0, 0, SP, SP, kC2);
        memcpy(map, v, sizeof(v));
        rhi_wait_frame();
        RhiCommandList cl = rhi_begin_commands();
        /* the frame's bind groups: T as the input attachment, and groups 0
         * to 2 empty, bound as the game binds its own */
        RhiBinding b;
        memset(&b, 0, sizeof(b));
        b.slot = 0;
        b.type = RHI_BIND_INPUT_ATTACHMENT;
        b.texture = t;
        const RhiBindGroup g = rhi_create_bind_group(&(RhiBindGroupDesc){lin, &b, 1});
        const RhiBindGroup ge = rhi_create_bind_group(&(RhiBindGroupDesc){empty, NULL, 0});
        SP_CHECK(g.id && ge.id, "bind groups %u %u\n", g.id, ge.id);
        spBarrier(cl, t, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET);
        spBarrier(cl, z, RHI_STATE_UNDEFINED, RHI_STATE_DEPTH_WRITE);
        RhiRenderPassDesc rp;
        memset(&rp, 0, sizeof(rp));
        rp.color[0].texture = t;
        rp.color[0].load = RHI_LOAD_CLEAR;
        for (int k = 0; k < 4; k++) {
            rp.color[0].clear[k] = (float)kBg[k] / 255.0f;
        }
        rp.colorCount = 1;
        rp.width = rp.height = SP;
        rhi_cmd_begin_render_pass(cl, &rp); /* pass 0: the clear, colour only */
        rhi_cmd_end_render_pass(cl);
        spBarrier(cl, t, RHI_STATE_RENDER_TARGET, RHI_STATE_RENDER_TARGET_READ);
        rp.color[0].load = RHI_LOAD_LOAD;
        rp.color[0].readable = true;
        rp.depth.texture = z;
        rp.depth.depthLoad = RHI_LOAD_CLEAR;
        rp.depth.stencilLoad = RHI_LOAD_CLEAR;
        rp.depth.clearDepth = 1.0f;
        rhi_cmd_begin_render_pass(cl, &rp); /* pass 1 */
        rhi_cmd_set_vertex_buffer(cl, 0, ring, 0);
        rhi_cmd_set_pipeline(cl, pc);
        rhi_cmd_draw(cl, 6, 0, 1); /* A, the left half */
        rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_COLOR);
        rhi_cmd_set_pipeline(cl, pr);
        for (uint32_t k = 0; k < 3u; k++) {
            rhi_cmd_set_bind_group(cl, k, ge);
        }
        rhi_cmd_set_bind_group(cl, 3, g);
        rhi_cmd_draw(cl, 6, 6, 1); /* S over what it reads */
        rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_BACK);
        rhi_cmd_set_pipeline(cl, pc);
        rhi_cmd_draw(cl, 6, 12, 1); /* C, the bottom rows */
        rhi_cmd_end_render_pass(cl);
        RhiRenderPassDesc rc = rp; /* pass 2: readable, colour only */
        memset(&rc.depth, 0, sizeof(rc.depth));
        rhi_cmd_begin_render_pass(cl, &rc);
        rhi_cmd_local_read_barrier(cl, RHI_LOCAL_READ_COLOR);
        rhi_cmd_end_render_pass(cl);
        spBarrier(cl, u, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc ru;
        memset(&ru, 0, sizeof(ru));
        ru.color[0].texture = u;
        ru.color[0].load = RHI_LOAD_CLEAR;
        ru.colorCount = 1;
        ru.depth.texture = z;
        ru.depth.depthLoad = RHI_LOAD_LOAD;
        ru.depth.stencilLoad = RHI_LOAD_LOAD;
        ru.width = ru.height = SP;
        rhi_cmd_begin_render_pass(cl, &ru); /* pass 3: U, never read */
        rhi_cmd_set_vertex_buffer(cl, 0, ring, 0);
        rhi_cmd_set_pipeline(cl, pc);
        rhi_cmd_draw(cl, 6, 18, 1);
        rhi_cmd_end_render_pass(cl);
        spBarrier(cl, t, RHI_STATE_RENDER_TARGET_READ, RHI_STATE_COPY_SRC);
        spBarrier(cl, u, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC);
        rhi_end_commands(cl);
        rhi_submit(cl);
        rhi_wait_idle();
        spCheck(cfg, t, "T", wantT);
        spCheck(cfg, u, "U", wantU);
        uint32_t p1 = 0, s1 = 0, m1 = 0;
        (void)vkr_test_subpass_read(&p1, &s1, &m1);
        if (subpass) {
            SP_CHECK(p1 - p0 == 2u && s1 - s0 == 1u && m1 == m0,
                     "%u passes with the subpass read (2 wanted), %u barriers skipped (1), %u "
                     "misfits (0)\n",
                     p1 - p0, s1 - s0, m1 - m0);
        } else {
            SP_CHECK(p1 == 0u && s1 == 0u && m1 == 0u,
                     "dynamic rendering counted %u %u %u subpass-read events\n", p1, s1, m1);
        }
        if (s_failures == 0) {
            rhi_test_log(
                "%s subpass read: %s: T and U exact (the read over A %u %u %u %u, over "
                "bg %u %u %u %u)%s\n",
                cfg->label,
                subpass ? "the VkRenderPass path's subpass read" : "dynamic rendering's local read",
                spLerp(kS[0], kA[0], kS[3]), spLerp(kS[1], kA[1], kS[3]),
                spLerp(kS[2], kA[2], kS[3]), kS[3], spLerp(kS[0], kBg[0], kS[3]),
                spLerp(kS[1], kBg[1], kS[3]), spLerp(kS[2], kBg[2], kS[3]), kS[3],
                subpass ? "; 2 passes with it, the colour-only pass's barrier skipped" : "");
        }
    }
    rhi_wait_idle();
    const RhiPipeline pipes[2] = {pc, pr};
    for (int i = 0; i < 2; i++) {
        if (pipes[i].id) {
            rhi_destroy_pipeline(pipes[i]);
        }
    }
    const RhiBindGroupLayout lays[2] = {empty, lin};
    for (int i = 0; i < 2; i++) {
        if (lays[i].id) {
            rhi_destroy_bind_group_layout(lays[i]);
        }
    }
    const RhiShader shaders[3] = {vs, psC, psR};
    for (int i = 0; i < 3; i++) {
        if (shaders[i].id) {
            rhi_destroy_shader(shaders[i]);
        }
    }
    const RhiTexture texs[3] = {t, u, z};
    for (int i = 0; i < 3; i++) {
        if (texs[i].id) {
            rhi_destroy_texture(texs[i]);
        }
    }
    if (ring.id) {
        rhi_destroy_buffer(ring);
    }
    rhi_shutdown();
    const uint32_t errs = cfg->validationErrors ? cfg->validationErrors() : 0u;
    SP_CHECK(errs == 0u, "%u validation errors\n", errs);
    return s_failures ? 1 : 0;
}
