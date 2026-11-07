/* rhi_native_test.c: rhi_native.h on the Vulkan backend, headless
 * (lavapipe in the container); exits 0 on success, 1 on a failure or a
 * validation message, 77 (skipped) with no Vulkan device.
 *
 *   - handles: the device, physical device, queue and loader functions are
 *     real (a vkCmdPipelineBarrier is recorded through the
 *     getDeviceProcAddr the RHI hands out), and the capability answer is
 *     consistent with its parts
 *   - textures: an RGBA16F and an RGBA8 texture made with RHI_TEX_STORAGE
 *     report their VkImage, a storage grant, the format and extent, and the
 *     layouts of the RHI states (SHADER_READ, STORAGE = GENERAL); a texture
 *     made without the flag reports none; D32F with the flag is accepted
 *     (the grant follows the device)
 *   - the storage barriers (UNDEFINED -> STORAGE -> SHADER_READ) go out with
 *     the compute stage and pass the validation layer's synchronisation
 *     checks
 *   - the external bracket: refused inside a render pass, refused twice,
 *     accepted outside; the native command list is the one the RHI records
 *     into (a barrier recorded through the loader's function pointer is
 *     accepted by the layer); around an empty stretch, and around a stretch
 *     with a native barrier, the picture of two render passes is
 *     byte-identical to the picture without the pair, the second pass drawing
 *     with no pipeline or vertex buffer call after the pair (the rebinding)
 *   - the pair counts two barriers in RhiStats */
#include "rhi.h"
#include "rhi_native.h"
#include "rhi_test_spv.h"
#include "vk/rhi_vk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

static int s_fail;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            s_fail++;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct Vtx {
    float x, y, z;
    uint8_t rgba[4];
    float u, v;
} Vtx;

#define W 8
#define H 8

typedef enum Mode { MODE_PLAIN, MODE_EMPTY_PAIR, MODE_NATIVE_PAIR, MODE_REFUSED } Mode;

static void quad(Vtx *v, float x0, float x1, uint8_t r, uint8_t g, uint8_t b)
{
    static const float qx[6] = {0, 1, 0, 0, 1, 1}, qy[6] = {1, 1, 0, 0, 1, 0};
    for (int i = 0; i < 6; i++) {
        v[i] = (Vtx){x0 + (x1 - x0) * qx[i], -1.0f + 2.0f * qy[i], 0.5f, {r, g, b, 255}, 0, 0};
    }
}

/* Two render passes into one target, the left half red, the right half
 * green over a blue clear, with `mode` between them; the picture read back
 * in img (W*H*4).  `barriers`: the barriers the pair added. */
static bool picture(Mode mode, uint8_t *img, uint64_t *pairBarriers)
{
    const RhiShader vs = rhi_create_shader(&(RhiShaderDesc){.stage = RHI_STAGE_VERTEX,
                                                            .bytecode = spv_vs_main,
                                                            .bytecodeSize = sizeof(spv_vs_main),
                                                            .entryPoint = "vs_main",
                                                            .debugName = "native vs"});
    const RhiShader ps = rhi_create_shader(&(RhiShaderDesc){.stage = RHI_STAGE_FRAGMENT,
                                                            .bytecode = spv_ps_color,
                                                            .bytecodeSize = sizeof(spv_ps_color),
                                                            .entryPoint = "ps_color",
                                                            .debugName = "native ps"});
    static const RhiVertexBinding vb = {0, sizeof(Vtx), false};
    static const RhiVertexAttr va[3] = {
        {0, 0, RHI_VTX_F32x3, offsetof(Vtx, x)},
        {1, 0, RHI_VTX_U8x4_UNORM, offsetof(Vtx, rgba)},
        {2, 0, RHI_VTX_F32x2, offsetof(Vtx, u)},
    };
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = vs;
    d.fragment = ps;
    d.vertexBindings = &vb;
    d.vertexBindingCount = 1;
    d.vertexAttrs = va;
    d.vertexAttrCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0].writeMask = 0xF;
    d.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    d.colorCount = 1;
    d.debugName = "native";
    const RhiPipeline pipe = vs.id && ps.id ? rhi_create_pipeline(&d) : (RhiPipeline){0};
    const RhiTexture tgt = rhi_create_texture(&(RhiTextureDesc){
        W, H, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC, "native tgt", 0});
    const RhiBuffer vbuf = rhi_create_buffer(
        &(RhiBufferDesc){12 * sizeof(Vtx), RHI_BUF_VERTEX, RHI_MEM_UPLOAD, "native verts"});
    Vtx *v = vbuf.id ? rhi_map_buffer(vbuf) : NULL;
    bool ok = pipe.id && tgt.id && v;
    CHECK(ok, "picture setup");
    if (ok) {
        quad(v, -1.0f, 0.0f, 255, 0, 0);
        quad(v + 6, 0.0f, 1.0f, 0, 255, 0);
        rhi_wait_frame();
        RhiCommandList cl = rhi_begin_commands();
        const RhiTextureBarrier pre = {tgt, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET};
        rhi_cmd_barrier(cl, &pre, 1);
        RhiRenderPassDesc rp = {0};
        rp.color[0].texture = tgt;
        rp.color[0].load = RHI_LOAD_CLEAR;
        rp.color[0].clear[2] = 1.0f;
        rp.color[0].clear[3] = 1.0f;
        rp.colorCount = 1;
        rp.width = W;
        rp.height = H;
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_set_vertex_buffer(cl, 0, vbuf, 0);
        rhi_cmd_set_pipeline(cl, pipe);
        rhi_cmd_draw(cl, 6, 0, 1);
        if (mode == MODE_REFUSED) {
            /* inside a pass: refused, and nothing was opened */
            CHECK(!rhi_cmd_external_begin(cl), "external begin inside a render pass");
            rhi_cmd_external_end(cl);
        }
        rhi_cmd_end_render_pass(cl);
        if (mode == MODE_EMPTY_PAIR || mode == MODE_NATIVE_PAIR) {
            RhiStats a, b;
            rhi_get_stats(&a);
            CHECK(rhi_cmd_external_begin(cl), "external begin outside a pass");
            CHECK(!rhi_cmd_external_begin(cl), "a second external begin");
            if (mode == MODE_NATIVE_PAIR) {
                void *cb = NULL;
                RhiNativeDevice nd;
                CHECK(rhi_native_command_list(cl, &cb) && cb, "native command list");
                CHECK(rhi_native_device(&nd) && nd.getDeviceProcAddr, "native device");
                PFN_vkGetDeviceProcAddr gdp;
                memcpy(&gdp, &nd.getDeviceProcAddr, sizeof(gdp));
                PFN_vkCmdPipelineBarrier barrier =
                    (PFN_vkCmdPipelineBarrier)gdp((VkDevice)nd.device, "vkCmdPipelineBarrier");
                CHECK(barrier != NULL, "vkCmdPipelineBarrier through getDeviceProcAddr");
                if (barrier && cb) {
                    /* what the external code does: its own commands in the
                     * RHI's command buffer */
                    const VkMemoryBarrier mb = {
                        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                    };
                    barrier((VkCommandBuffer)cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
                }
            }
            rhi_cmd_external_end(cl);
            rhi_cmd_external_end(cl); /* not open: nothing */
            rhi_get_stats(&b);
            if (pairBarriers) {
                *pairBarriers = b.barriers - a.barriers;
            }
        }
        /* the second pass keeps the first's pipeline and vertex buffer: no
         * set call here, after the pair or not */
        rp.color[0].load = RHI_LOAD_LOAD;
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_draw(cl, 6, 6, 1);
        rhi_cmd_end_render_pass(cl);
        const RhiTextureBarrier post = {tgt, RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC};
        rhi_cmd_barrier(cl, &post, 1);
        rhi_end_commands(cl);
        rhi_submit(cl);
        uint32_t pitch = 0;
        ok = rhi_readback_texture(tgt, RHI_ASPECT_COLOR, img, W * H * 4, &pitch) && pitch == W * 4;
        CHECK(ok, "readback");
    }
    rhi_wait_idle();
    rhi_destroy_buffer(vbuf);
    rhi_destroy_texture(tgt);
    rhi_destroy_pipeline(pipe);
    rhi_destroy_shader(vs);
    rhi_destroy_shader(ps);
    return ok;
}

static void textures(void)
{
    RhiNativeTexture nt;
    const RhiTexture f16 = rhi_create_texture(
        &(RhiTextureDesc){16, 8, 1, RHI_FMT_RGBA16F,
                          RHI_TEX_SAMPLED | RHI_TEX_STORAGE | RHI_TEX_COPY_SRC, "native f16", 0});
    const RhiTexture r8 = rhi_create_texture(&(RhiTextureDesc){
        8, 8, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_STORAGE, "native rgba8", 0});
    const RhiTexture plain = rhi_create_texture(&(RhiTextureDesc){
        8, 8, 1, RHI_FMT_RGBA16F, RHI_TEX_SAMPLED | RHI_TEX_RENDER_TARGET, "native plain", 0});
    const RhiTexture d32 = rhi_create_texture(&(RhiTextureDesc){
        8, 8, 1, RHI_FMT_D32F, RHI_TEX_DEPTH_STENCIL | RHI_TEX_STORAGE, "native d32", 0});
    CHECK(f16.id && r8.id && plain.id, "textures made");
    CHECK(d32.id != 0, "D32F with RHI_TEX_STORAGE is accepted (the flag is a grant request)");
    if (rhi_native_texture(f16, &nt)) {
        CHECK(nt.kind == RHI_NATIVE_VULKAN && nt.image != 0, "RGBA16F: image");
        CHECK(nt.format == (uint32_t)VK_FORMAT_R16G16B16A16_SFLOAT, "RGBA16F: format %u",
              nt.format);
        CHECK(nt.width == 16 && nt.height == 8 && nt.mips == 1 && nt.samples == 1,
              "RGBA16F: extent");
        CHECK(nt.storage, "RGBA16F with RHI_TEX_STORAGE: storage granted");
        CHECK(!nt.stateTracked, "Vulkan tracks no layout");
        CHECK(nt.nativeState[RHI_STATE_STORAGE] == (uint32_t)VK_IMAGE_LAYOUT_GENERAL,
              "STORAGE layout %u", nt.nativeState[RHI_STATE_STORAGE]);
        CHECK(nt.nativeState[RHI_STATE_SHADER_READ] ==
                  (uint32_t)VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              "SHADER_READ layout %u", nt.nativeState[RHI_STATE_SHADER_READ]);
    } else {
        CHECK(false, "rhi_native_texture RGBA16F");
    }
    if (rhi_native_texture(r8, &nt)) {
        CHECK(nt.storage && nt.format == (uint32_t)VK_FORMAT_R8G8B8A8_UNORM,
              "RGBA8 with RHI_TEX_STORAGE: storage %d format %u", (int)nt.storage, nt.format);
    } else {
        CHECK(false, "rhi_native_texture RGBA8");
    }
    if (rhi_native_texture(plain, &nt)) {
        CHECK(!nt.storage, "a texture made without RHI_TEX_STORAGE reports none");
    } else {
        CHECK(false, "rhi_native_texture plain");
    }
    if (d32.id && rhi_native_texture(d32, &nt)) {
        printf("D32F storage grant: %d\n", (int)nt.storage);
    }
    CHECK(!rhi_native_texture((RhiTexture){0}, &nt), "a null texture");
    if (f16.id) {
        /* the storage states with the compute stage: the layer's
         * synchronisation checks see the barriers */
        rhi_wait_frame();
        RhiCommandList cl = rhi_begin_commands();
        const RhiTextureBarrier a = {f16, RHI_STATE_UNDEFINED, RHI_STATE_STORAGE};
        const RhiTextureBarrier b = {f16, RHI_STATE_STORAGE, RHI_STATE_SHADER_READ};
        const RhiTextureBarrier c = {f16, RHI_STATE_SHADER_READ, RHI_STATE_STORAGE};
        rhi_cmd_barrier(cl, &a, 1);
        rhi_cmd_barrier(cl, &b, 1);
        rhi_cmd_barrier(cl, &c, 1);
        rhi_end_commands(cl);
        rhi_submit(cl);
        rhi_wait_idle();
    }
    rhi_destroy_texture(f16);
    rhi_destroy_texture(r8);
    rhi_destroy_texture(plain);
    rhi_destroy_texture(d32);
}

int main(void)
{
    if (!rhi_create_backend("vulkan")) {
        printf("SKIP rhi_native_test: no Vulkan backend\n");
        return 77;
    }
    RhiDeviceDesc dd = {NULL, false, true, "rhi_native_test", NULL};
    if (!rhi_init(&dd)) {
        printf("SKIP rhi_native_test: no usable Vulkan device\n");
        return 77;
    }
    RhiNativeDevice nd;
    CHECK(rhi_native_device(&nd), "rhi_native_device");
    CHECK(nd.kind == RHI_NATIVE_VULKAN && nd.instance && nd.device && nd.physicalDevice &&
              nd.queue && nd.getDeviceProcAddr && nd.getInstanceProcAddr,
          "device handles");
    CHECK(nd.apiVersion >= VK_API_VERSION_1_0, "api version");
    RhiComputeCaps caps;
    const bool ok = rhi_device_compute_caps(&caps);
    CHECK(ok == caps.ok, "caps return value");
    CHECK(caps.ok == (caps.apiVersionOk && caps.subgroupOk && caps.storageFormatsOk) ||
              (!caps.ok && caps.why),
          "caps parts");
    CHECK(caps.ok == (caps.why == NULL), "a reason exactly when not ok");
    printf("compute caps: ok %d (api %d subgroup %d storage %d float16 %d)%s%s\n", (int)caps.ok,
           (int)caps.apiVersionOk, (int)caps.subgroupOk, (int)caps.storageFormatsOk,
           (int)caps.float16, caps.why ? ": " : "", caps.why ? caps.why : "");

    textures();

    uint8_t plain[W * H * 4], empty[W * H * 4], nat[W * H * 4], refused[W * H * 4];
    uint64_t emptyBarriers = 0, natBarriers = 0;
    if (picture(MODE_PLAIN, plain, NULL)) {
        /* left half red, right half green: the two passes both drew */
        CHECK(plain[0] == 255 && plain[1] == 0 && plain[2] == 0, "plain picture, left texel");
        CHECK(plain[(W - 1) * 4] == 0 && plain[(W - 1) * 4 + 1] == 255, "plain picture, right");
        if (caps.ok) {
            if (picture(MODE_EMPTY_PAIR, empty, &emptyBarriers)) {
                CHECK(memcmp(plain, empty, sizeof(plain)) == 0,
                      "the empty pair changed the picture");
                CHECK(emptyBarriers >= 2, "the pair added %llu barriers",
                      (unsigned long long)emptyBarriers);
            }
            if (picture(MODE_NATIVE_PAIR, nat, &natBarriers)) {
                CHECK(memcmp(plain, nat, sizeof(plain)) == 0,
                      "the pair around a native barrier changed the picture");
            }
        } else {
            printf("external compute not offered here (%s): the pair cells are skipped\n",
                   caps.why ? caps.why : "?");
            CHECK(!rhi_cmd_external_begin((RhiCommandList){0}), "external begin on a null list");
        }
        if (picture(MODE_REFUSED, refused, NULL)) {
            CHECK(memcmp(plain, refused, sizeof(plain)) == 0,
                  "a refused external begin changed the picture");
        }
    }
    printf("pair barriers: empty +%llu, native +%llu\n", (unsigned long long)emptyBarriers,
           (unsigned long long)natBarriers);
    rhi_shutdown();
    s_fail += (int)rhi_vk_validation_error_count();
    printf("rhi_native_test: %s (%d failures)\n", s_fail ? "FAIL" : "ok", s_fail);
    return s_fail ? 1 : 0;
}
