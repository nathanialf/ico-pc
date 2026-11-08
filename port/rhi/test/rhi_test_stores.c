/* rhi_test_stores.c: the vertex shaders' stores on any backend
 * (rhi_test_common.h rhi_test_vertex_stores; rhi.h RhiLimits.vertexStores,
 * RHI_BIND_STORAGE_BUFFER_RW, RhiPipelineDesc.noRaster,
 * rhi_cmd_buffer_store_begin / _end: the VU record's write,
 * port/render/rd_vu_record.h).
 *
 * A buffer of ST_POINTS uint4 made with RHI_BUF_STORAGE_WRITE is filled by
 * one point draw of vs_store (rhi_test.hlsl: point i writes (i, 3i + 1,
 * 0xC0DE0000 + i, 0x5EED) at element i and lies outside the clip volume)
 * through a pipeline with no fragment shader, no target and no
 * rasterization, in a pass without attachments, between the bracket's
 * begin and end; a second bracket then stores the same points again over
 * a buffer a copy wrote first (write after a copy's write), and the
 * buffer is copied into a readback buffer (the end's visibility to a later
 * copy) and compared word for word.  A backend that reports no vertex
 * stores must refuse both halves of the bracket (false), and the run is
 * skipped (77: OpenGL, D3D12 in this release; a Vulkan device without
 * vertexPipelineStoresAndAtomics or under ICO_VK_FAKE_NO_VERTEX_STORES). */
#include "rhi.h"
#include "rhi_test_common.h"
#include "rhi_test_dxil.h"
#include "rhi_test_glsl.h"
#include "rhi_test_spv.h"
#include <stdint.h>
#include <string.h>

#define ST_POINTS 64u

static uint32_t want(uint32_t i, uint32_t k)
{
    const uint32_t w[4] = {i, i * 3u + 1u, 0xC0DE0000u + i, 0x5EEDu};
    return w[k];
}

/* one store pass of n points into the buffer (group 0 g) */
static void storePass(RhiCommandList cl, RhiPipeline p, RhiBindGroup g, RhiBuffer buf, uint32_t n,
                      int *failures)
{
    if (!rhi_cmd_buffer_store_begin(cl, buf)) {
        rhi_test_log("FAIL vertex stores: rhi_cmd_buffer_store_begin refused on a device with "
                     "vertex stores\n");
        (*failures)++;
    }
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    rp.width = 64;
    rp.height = 1;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_set_pipeline(cl, p);
    rhi_cmd_set_bind_group(cl, 0, g);
    rhi_cmd_draw(cl, n, 0, 1);
    rhi_cmd_end_render_pass(cl);
    if (!rhi_cmd_buffer_store_end(cl, buf)) {
        rhi_test_log("FAIL vertex stores: rhi_cmd_buffer_store_end refused\n");
        (*failures)++;
    }
}

int rhi_test_vertex_stores(const RhiTestConfig *cfg)
{
    if (!rhi_create_backend(cfg->backend)) {
        rhi_test_log("SKIP %s vertex stores: no %s backend\n", cfg->label, cfg->backend);
        return 77;
    }
    RhiDeviceDesc dd;
    memset(&dd, 0, sizeof(dd));
    dd.debugLayers = cfg->debugLayers;
    dd.appName = "rhi test vertex stores";
    if (!rhi_init(&dd)) {
        rhi_test_log("SKIP %s vertex stores: no device\n", cfg->label);
        return 77;
    }
    int failures = 0;
    const uint64_t size = (uint64_t)ST_POINTS * 16u;
    RhiBuffer buf = rhi_create_buffer(&(RhiBufferDesc){
        size, RHI_BUF_STORAGE_READ | RHI_BUF_STORAGE_WRITE | RHI_BUF_COPY_SRC | RHI_BUF_COPY_DST,
        RHI_MEM_DEVICE, "rhi test stores"});
    if (!rhi_limits()->vertexStores) {
        /* the bracket refuses where nothing may store */
        RhiCommandList cl = rhi_begin_commands();
        const bool b = cl.id && buf.id ? rhi_cmd_buffer_store_begin(cl, buf) : false;
        const bool e = cl.id && buf.id ? rhi_cmd_buffer_store_end(cl, buf) : false;
        if (cl.id) {
            rhi_end_commands(cl);
            rhi_submit(cl);
        }
        rhi_wait_idle();
        if (buf.id) {
            rhi_destroy_buffer(buf);
        }
        rhi_shutdown();
        if (b || e) {
            rhi_test_log("FAIL %s vertex stores: no vertex stores reported, yet the bracket "
                         "took a buffer (begin %d, end %d)\n",
                         cfg->label, (int)b, (int)e);
            return 1;
        }
        rhi_test_log("SKIP %s vertex stores: the backend reports no vertex stores (the bracket "
                     "refused, as it must)\n",
                     cfg->label);
        return 77;
    }
    const bool dxil = rhi_backend() == RHI_BACKEND_D3D12;
    const bool glsl = rhi_backend() == RHI_BACKEND_OPENGL;
    RhiShaderDesc sd;
    memset(&sd, 0, sizeof(sd));
    sd.stage = RHI_STAGE_VERTEX;
    sd.bytecode = dxil   ? (const void *)dxil_vs_store
                  : glsl ? (const void *)glsl_vs_store
                         : (const void *)spv_vs_store;
    sd.bytecodeSize = dxil   ? sizeof(dxil_vs_store)
                      : glsl ? sizeof(glsl_vs_store)
                             : sizeof(spv_vs_store);
    sd.entryPoint = "vs_store";
    sd.debugName = "vs_store";
    RhiShader vs = rhi_create_shader(&sd);

    enum { VS = 1u << RHI_STAGE_VERTEX };

    static const RhiBindSlot slot[1] = {{0, RHI_BIND_STORAGE_BUFFER_RW, VS}};
    RhiBindGroupLayout lay =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){slot, 1, "rhi test stores"});
    RhiPipelineDesc pd;
    memset(&pd, 0, sizeof(pd));
    pd.vertex = vs;
    pd.layouts = &lay;
    pd.layoutCount = 1;
    pd.topology = RHI_TOPO_POINT_LIST;
    pd.cullNone = true;
    pd.colorCount = 0;
    pd.depthFormat = RHI_FMT_UNKNOWN;
    pd.noRaster = true;
    pd.debugName = "rhi test stores";
    RhiPipeline p = vs.id && lay.id ? rhi_create_pipeline(&pd) : (RhiPipeline){0};
    RhiBuffer rb = rhi_create_buffer(&(RhiBufferDesc){size, RHI_BUF_READBACK | RHI_BUF_COPY_DST,
                                                      RHI_MEM_READBACK, "rhi test stores read"});
    RhiBuffer up = rhi_create_buffer(
        &(RhiBufferDesc){size, RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "rhi test stores fill"});
    uint8_t *upMap = up.id ? rhi_map_buffer(up) : NULL;
    if (!buf.id || !vs.id || !lay.id || !p.id || !rb.id || !upMap) {
        rhi_test_log("FAIL %s vertex stores: setup (buffer %u, shader %u, layout %u, pipeline "
                     "%u, readback %u, fill %u)\n",
                     cfg->label, buf.id, vs.id, lay.id, p.id, rb.id, upMap != NULL);
        failures++;
    } else {
        memset(upMap, 0xA5, (size_t)size);
        rhi_unmap_buffer(up);
        RhiCommandList cl = rhi_begin_commands();
        RhiBinding b;
        memset(&b, 0, sizeof(b));
        b.slot = 0;
        b.type = RHI_BIND_STORAGE_BUFFER_RW;
        b.buffer = buf;
        b.size = size;
        RhiBindGroup g = rhi_create_bind_group(&(RhiBindGroupDesc){lay, &b, 1});
        if (!cl.id || !g.id) {
            rhi_test_log("FAIL %s vertex stores: no command list or bind group\n", cfg->label);
            failures++;
        } else {
            /* the stores, then a copy's fill over them, then the stores again
               (write after write both ways), then the copy out (the end's
               visibility to a later copy) */
            storePass(cl, p, g, buf, ST_POINTS, &failures);
            rhi_cmd_copy_buffer(cl, up, 0, buf, 0, size);
            storePass(cl, p, g, buf, ST_POINTS, &failures);
            rhi_cmd_copy_buffer(cl, buf, 0, rb, 0, size);
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
            const uint32_t *w = rhi_map_buffer(rb);
            uint32_t bad = 0, first = 0;
            for (uint32_t i = 0; w && i < ST_POINTS; i++) {
                for (uint32_t k = 0; k < 4; k++) {
                    if (w[i * 4u + k] != want(i, k) && bad++ == 0) {
                        first = i * 4u + k;
                    }
                }
            }
            if (!w || bad) {
                rhi_test_log("FAIL %s vertex stores: %u of %u words differ (the first, word %u: "
                             "%08x want %08x)\n",
                             cfg->label, bad, ST_POINTS * 4u, first, w ? w[first] : 0u,
                             want(first / 4u, first % 4u));
                failures++;
            }
            if (w) {
                rhi_unmap_buffer(rb);
            }
        }
    }
    rhi_wait_idle();
    if (p.id) {
        rhi_destroy_pipeline(p);
    }
    if (lay.id) {
        rhi_destroy_bind_group_layout(lay);
    }
    if (vs.id) {
        rhi_destroy_shader(vs);
    }
    if (buf.id) {
        rhi_destroy_buffer(buf);
    }
    if (rb.id) {
        rhi_destroy_buffer(rb);
    }
    if (up.id) {
        rhi_destroy_buffer(up);
    }
    rhi_shutdown();
    const uint32_t errs = cfg->validationErrors ? cfg->validationErrors() : 0u;
    if (errs) {
        rhi_test_log("FAIL %s vertex stores: %u validation error(s)\n", cfg->label, errs);
        failures++;
    }
    if (!failures) {
        rhi_test_log("%s vertex stores: %u points stored by a vertex shader without "
                     "rasterization, ordered after a copy and before one, read back word for "
                     "word\n",
                     cfg->label, ST_POINTS);
    }
    return failures ? 1 : 0;
}
