/* rhi_test_compute.c: the compute shaders' stores on any backend
 * (rhi_test_common.h rhi_test_compute_stores; rhi.h RhiLimits.computeStores,
 * RhiPipelineDesc.compute, rhi_cmd_dispatch, rhi_cmd_buffer_compute_begin /
 * _end: the VU record's write on a device whose vertex shaders cannot
 * store, port/render/rd_vu_record.h).
 *
 * A buffer of CS_POINTS uint4 made with RHI_BUF_STORAGE_WRITE is written by
 * one group of 64 invocations of cs_store (rhi_test.hlsl: invocation i
 * writes (i, 3i + 1, 0xC0DE0000 + i, 0x5EED) at element i, vs_store's
 * record) between the compute bracket's begin and end, over the bytes a
 * copy wrote first (write after a copy's write), then a copy fills it again
 * and a second dispatch stores over that, and the buffer is copied into a
 * readback buffer (the end's visibility to a later copy) and compared word
 * for word.
 *
 * Where the backend also has vertex stores (RhiLimits.vertexStores), the
 * bind-point rule: one bind group layout (u0 visible to the vertex and
 * compute stages, as the renderer's layoutVuPre) serves a vertex store
 * pipeline and the compute one; a store draw binds group A, a dispatch
 * group B, a copy refills A, and a store draw binds group B again: the
 * draw must store into B.  Vulkan binds descriptor sets per bind point, so
 * a backend whose bind cache skipped the second bind of B (it already holds
 * B, from the dispatch) would leave the graphics bind point on A, and A
 * would read back as stores instead of the copy's fill.
 *
 * A backend that reports no compute stores must refuse both halves of the
 * bracket (false) and record nothing for a dispatch; the run is skipped
 * (77: OpenGL, Direct3D 11 and 12 in this release; a Vulkan device under
 * ICO_VK_FAKE_NO_COMPUTE_STORES). */
#include "rhi.h"
#include "rhi_test_common.h"
#include "rhi_test_dxil.h"
#include "rhi_test_spv.h"
#include <stdint.h>
#include <string.h>

#define CS_POINTS 64u
#define CS_FILL 0xA5A5A5A5u

static uint32_t want(uint32_t i, uint32_t k)
{
    const uint32_t w[4] = {i, i * 3u + 1u, 0xC0DE0000u + i, 0x5EEDu};
    return w[k];
}

/* one dispatch of a group into the buffer (group 0 g), in the bracket */
static void computePass(RhiCommandList cl, RhiPipeline p, RhiBindGroup g, RhiBuffer buf,
                        int *failures)
{
    if (!rhi_cmd_buffer_compute_begin(cl, buf)) {
        rhi_test_log("FAIL compute stores: rhi_cmd_buffer_compute_begin refused on a device with "
                     "compute stores\n");
        (*failures)++;
    }
    rhi_cmd_set_pipeline(cl, p);
    rhi_cmd_set_bind_group(cl, 0, g);
    rhi_cmd_dispatch(cl, 1, 1, 1);
    if (!rhi_cmd_buffer_compute_end(cl, buf)) {
        rhi_test_log("FAIL compute stores: rhi_cmd_buffer_compute_end refused\n");
        (*failures)++;
    }
}

/* one store pass of CS_POINTS points into the buffer (group 0 g) */
static void storePass(RhiCommandList cl, RhiPipeline p, RhiBindGroup g, RhiBuffer buf,
                      int *failures)
{
    if (!rhi_cmd_buffer_store_begin(cl, buf)) {
        rhi_test_log("FAIL compute stores: rhi_cmd_buffer_store_begin refused on a device with "
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
    rhi_cmd_draw(cl, CS_POINTS, 0, 1);
    rhi_cmd_end_render_pass(cl);
    if (!rhi_cmd_buffer_store_end(cl, buf)) {
        rhi_test_log("FAIL compute stores: rhi_cmd_buffer_store_end refused\n");
        (*failures)++;
    }
}

/* the readback's words against the records (records) or the fill */
static uint32_t compareWords(const char *label, const char *what, RhiBuffer rb, bool records)
{
    const uint32_t *w = rhi_map_buffer(rb);
    uint32_t bad = 0, first = 0;
    for (uint32_t i = 0; w && i < CS_POINTS; i++) {
        for (uint32_t k = 0; k < 4; k++) {
            const uint32_t x = records ? want(i, k) : CS_FILL;
            if (w[i * 4u + k] != x && bad++ == 0) {
                first = i * 4u + k;
            }
        }
    }
    if (!w || bad) {
        rhi_test_log("FAIL %s compute stores: %s: %u of %u words differ (the first, word %u: "
                     "%08x want %08x)\n",
                     label, what, w ? bad : CS_POINTS * 4u, CS_POINTS * 4u, first,
                     w ? w[first] : 0u, records ? want(first / 4u, first % 4u) : CS_FILL);
    }
    if (w) {
        rhi_unmap_buffer(rb);
    }
    return w ? bad : 1u;
}

int rhi_test_compute_stores(const RhiTestConfig *cfg)
{
    if (!rhi_create_backend(cfg->backend)) {
        rhi_test_log("SKIP %s compute stores: no %s backend\n", cfg->label, cfg->backend);
        return 77;
    }
    RhiDeviceDesc dd;
    memset(&dd, 0, sizeof(dd));
    dd.debugLayers = cfg->debugLayers;
    dd.appName = "rhi test compute stores";
    if (!rhi_init(&dd)) {
        rhi_test_log("SKIP %s compute stores: no device\n", cfg->label);
        return 77;
    }
    int failures = 0;
    const uint64_t size = (uint64_t)CS_POINTS * 16u;
    const uint32_t use =
        RHI_BUF_STORAGE_READ | RHI_BUF_STORAGE_WRITE | RHI_BUF_COPY_SRC | RHI_BUF_COPY_DST;
    RhiBuffer bufA =
        rhi_create_buffer(&(RhiBufferDesc){size, use, RHI_MEM_DEVICE, "rhi test compute A"});
    if (!rhi_limits()->computeStores) {
        /* the bracket refuses and a dispatch records nothing where no
           compute shader may store */
        RhiCommandList cl = rhi_begin_commands();
        const bool b = cl.id && bufA.id ? rhi_cmd_buffer_compute_begin(cl, bufA) : false;
        if (cl.id) {
            rhi_cmd_dispatch(cl, 1, 1, 1);
        }
        const bool e = cl.id && bufA.id ? rhi_cmd_buffer_compute_end(cl, bufA) : false;
        if (cl.id) {
            rhi_end_commands(cl);
            rhi_submit(cl);
        }
        rhi_wait_idle();
        RhiStats st;
        memset(&st, 0, sizeof(st));
        rhi_get_stats(&st);
        const char *off = rhi_limits()->computeStoresOff;
        if (bufA.id) {
            rhi_destroy_buffer(bufA);
        }
        rhi_shutdown();
        if (b || e || st.dispatches != 0 || off == NULL || off[0] == '\0') {
            rhi_test_log("FAIL %s compute stores: none reported, yet the bracket took a buffer "
                         "(begin %d, end %d), %llu dispatches were recorded or no reason was "
                         "given (%s)\n",
                         cfg->label, (int)b, (int)e, (unsigned long long)st.dispatches,
                         off ? off : "NULL");
            return 1;
        }
        rhi_test_log("SKIP %s compute stores: the backend reports none (%s; the bracket "
                     "refused and the dispatch recorded nothing, as they must)\n",
                     cfg->label, off);
        return 77;
    }
    const bool dxil = rhi_backend() == RHI_BACKEND_D3D12;
    const bool vstores = rhi_limits()->vertexStores;
    RhiShaderDesc sd;
    memset(&sd, 0, sizeof(sd));
    sd.stage = RHI_STAGE_COMPUTE;
    sd.bytecode = dxil ? (const void *)dxil_cs_store : (const void *)spv_cs_store;
    sd.bytecodeSize = dxil ? sizeof(dxil_cs_store) : sizeof(spv_cs_store);
    sd.entryPoint = "cs_store";
    sd.debugName = "cs_store";
    RhiShader cs = rhi_create_shader(&sd);
    RhiShader vs = {0};
    if (vstores) {
        memset(&sd, 0, sizeof(sd));
        sd.stage = RHI_STAGE_VERTEX;
        sd.bytecode = dxil ? (const void *)dxil_vs_store : (const void *)spv_vs_store;
        sd.bytecodeSize = dxil ? sizeof(dxil_vs_store) : sizeof(spv_vs_store);
        sd.entryPoint = "vs_store";
        sd.debugName = "vs_store";
        vs = rhi_create_shader(&sd);
    }

    enum { VS = 1u << RHI_STAGE_VERTEX, CS = 1u << RHI_STAGE_COMPUTE };

    /* one layout for both kinds (the renderer's layoutVuPre: VS | CS) */
    static const RhiBindSlot slot[1] = {{0, RHI_BIND_STORAGE_BUFFER_RW, VS | CS}};
    RhiBindGroupLayout lay =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){slot, 1, "rhi test compute"});
    RhiPipelineDesc pd;
    memset(&pd, 0, sizeof(pd));
    pd.compute = cs;
    pd.layouts = &lay;
    pd.layoutCount = 1;
    pd.debugName = "rhi test compute";
    RhiPipeline p = cs.id && lay.id ? rhi_create_pipeline(&pd) : (RhiPipeline){0};
    RhiPipeline pv = {0};
    if (vstores && vs.id && lay.id) {
        memset(&pd, 0, sizeof(pd));
        pd.vertex = vs;
        pd.layouts = &lay;
        pd.layoutCount = 1;
        pd.topology = RHI_TOPO_POINT_LIST;
        pd.cullNone = true;
        pd.depthFormat = RHI_FMT_UNKNOWN;
        pd.noRaster = true;
        pd.debugName = "rhi test compute (vertex stores)";
        pv = rhi_create_pipeline(&pd);
    }
    RhiBuffer bufB =
        rhi_create_buffer(&(RhiBufferDesc){size, use, RHI_MEM_DEVICE, "rhi test compute B"});
    RhiBuffer rbA = rhi_create_buffer(&(RhiBufferDesc){
        size, RHI_BUF_READBACK | RHI_BUF_COPY_DST, RHI_MEM_READBACK, "rhi test compute read A"});
    RhiBuffer rbB = rhi_create_buffer(&(RhiBufferDesc){
        size, RHI_BUF_READBACK | RHI_BUF_COPY_DST, RHI_MEM_READBACK, "rhi test compute read B"});
    RhiBuffer up = rhi_create_buffer(
        &(RhiBufferDesc){size, RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "rhi test compute fill"});
    uint8_t *upMap = up.id ? rhi_map_buffer(up) : NULL;
    if (!bufA.id || !bufB.id || !cs.id || !lay.id || !p.id || !rbA.id || !rbB.id || !upMap ||
        (vstores && (!vs.id || !pv.id))) {
        rhi_test_log("FAIL %s compute stores: setup (buffers %u %u, compute shader %u, layout %u, "
                     "pipeline %u, readbacks %u %u, fill %u; vertex stores %d: shader %u, "
                     "pipeline %u)\n",
                     cfg->label, bufA.id, bufB.id, cs.id, lay.id, p.id, rbA.id, rbB.id,
                     upMap != NULL, (int)vstores, vs.id, pv.id);
        failures++;
    } else {
        memset(upMap, 0xA5, (size_t)size);
        rhi_unmap_buffer(up);
        RhiCommandList cl = rhi_begin_commands();
        RhiBinding b;
        memset(&b, 0, sizeof(b));
        b.slot = 0;
        b.type = RHI_BIND_STORAGE_BUFFER_RW;
        b.buffer = bufA;
        b.size = size;
        RhiBindGroup gA = rhi_create_bind_group(&(RhiBindGroupDesc){lay, &b, 1});
        b.buffer = bufB;
        RhiBindGroup gB = rhi_create_bind_group(&(RhiBindGroupDesc){lay, &b, 1});
        RhiStats st0, st1;
        memset(&st0, 0, sizeof(st0));
        memset(&st1, 0, sizeof(st1));
        rhi_get_stats(&st0);
        if (!cl.id || !gA.id || !gB.id) {
            rhi_test_log("FAIL %s compute stores: no command list or bind group\n", cfg->label);
            failures++;
        } else {
            /* A: a copy's fill, the dispatch over it, the fill again, the
               dispatch again (write after write both ways), then the copy
               out (the end's visibility to a later copy) */
            rhi_cmd_copy_buffer(cl, up, 0, bufA, 0, size);
            computePass(cl, p, gA, bufA, &failures);
            rhi_cmd_copy_buffer(cl, up, 0, bufA, 0, size);
            computePass(cl, p, gA, bufA, &failures);
            if (vstores) {
                /* the bind-point rule: B filled, a store draw into A (the
                   graphics bind point holds A), a dispatch into B (the
                   compute one holds B), A filled again, a store draw into
                   B: B is bound again at the graphics bind point, A keeps
                   the fill */
                rhi_cmd_copy_buffer(cl, up, 0, bufB, 0, size);
                storePass(cl, pv, gA, bufA, &failures);
                computePass(cl, p, gB, bufB, &failures);
                rhi_cmd_copy_buffer(cl, up, 0, bufA, 0, size);
                rhi_cmd_copy_buffer(cl, up, 0, bufB, 0, size);
                storePass(cl, pv, gB, bufB, &failures);
            }
            rhi_cmd_copy_buffer(cl, bufA, 0, rbA, 0, size);
            rhi_cmd_copy_buffer(cl, bufB, 0, rbB, 0, size);
            rhi_end_commands(cl);
            rhi_submit(cl);
            rhi_wait_idle();
            rhi_get_stats(&st1);
            const uint64_t made = vstores ? 3u : 2u;
            if (st1.dispatches - st0.dispatches != made) {
                rhi_test_log("FAIL %s compute stores: %llu dispatches counted, %llu recorded\n",
                             cfg->label, (unsigned long long)(st1.dispatches - st0.dispatches),
                             (unsigned long long)made);
                failures++;
            }
            if (vstores) {
                failures += compareWords(cfg->label, "the bind-point rule: A after its refill", rbA,
                                         false) != 0;
                failures += compareWords(cfg->label, "the bind-point rule: B stored by the draw",
                                         rbB, true) != 0;
            } else {
                failures += compareWords(cfg->label, "A", rbA, true) != 0;
            }
        }
    }
    rhi_wait_idle();
    if (p.id) {
        rhi_destroy_pipeline(p);
    }
    if (pv.id) {
        rhi_destroy_pipeline(pv);
    }
    if (lay.id) {
        rhi_destroy_bind_group_layout(lay);
    }
    if (cs.id) {
        rhi_destroy_shader(cs);
    }
    if (vs.id) {
        rhi_destroy_shader(vs);
    }
    RhiBuffer bufs[5] = {bufA, bufB, rbA, rbB, up};
    for (int i = 0; i < 5; i++) {
        if (bufs[i].id) {
            rhi_destroy_buffer(bufs[i]);
        }
    }
    rhi_shutdown();
    const uint32_t errs = cfg->validationErrors ? cfg->validationErrors() : 0u;
    if (errs) {
        rhi_test_log("FAIL %s compute stores: %u validation error(s)\n", cfg->label, errs);
        failures++;
    }
    if (!failures) {
        rhi_test_log("%s compute stores: %u invocations stored by a compute shader, ordered after "
                     "a copy and before one, read back word for word%s\n",
                     cfg->label, CS_POINTS,
                     vstores ? "; a store draw after a dispatch bound its own group again (the "
                               "bind-point rule)"
                             : "");
    }
    return failures ? 1 : 0;
}
