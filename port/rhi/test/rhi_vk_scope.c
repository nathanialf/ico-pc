/* rhi_vk_scope.c: the Vulkan backend's buffer barriers name only the stages
 * that can touch the buffer (vk_cmd.c "Buffer barriers"; issue 179), counted
 * by RhiStats.fragmentHolds: the barriers whose first scope holds fragment
 * work and whose second the vertex, compute or copy stages.
 *
 * Seven command lists, each ended and submitted before the next begins (one
 * hazard epoch each), under the validation layer's synchronisation checks
 * (rhi_vk_test's environment):
 *   A "a picture": a mesh upload (a copy into an arena-like buffer:
 *     index | storage read), the VU record's compute dispatch into a
 *     record-like buffer (storage read | write) in its bracket and a store
 *     pass in the vertex stores' bracket where the device has each, a
 *     colour pass on a target left in RENDER_TARGET, a texture upload left
 *     in SHADER_READ;
 *   B "the next picture": the same uploads, record and pass: no barrier
 *     holds fragment work (0): the copy waits for the arena's earlier
 *     vertex-input, vertex and compute reads, the brackets for the
 *     record's vertex and compute uses, the list's opening barrier only for
 *     attachments (before v0.11.3 the copy's, the bracket's and the opening
 *     barrier's first scopes held the fragment stage or colour output);
 *   C a texture upload over the sampled texture: 1, by its nature (the
 *     copy waits for the sampling), so the count is not blind; the target
 *     sent to SHADER_READ at the list's end;
 *   C2 the record, then the target back to RENDER_TARGET and its pass: the
 *     bracket's end, held back, goes out with the pass's barrier, whose
 *     transition waits for the sampling: one call joins the two (1; 0 on
 *     the global path, where each goes out alone).  The renderer keeps them
 *     apart with the timestamp it writes between the record and list 0
 *     (rd_replay.c RD_PERF_TS_LISTS flushes the held-back barriers);
 *   G a texture upload over the sampled texture, a mesh upload and the
 *     record, then in the same list the mesh upload, the record and the
 *     texture upload: in the order before v0.11.3 (the texture first) the
 *     texture's hold is recorded before the record (1), and in Vulkan's
 *     synchronisation scopes every later copy, the mesh upload's, waits
 *     behind it, the record behind the copy; in the renderer's order now
 *     (the mesh upload and the record first, port/render/
 *     rd_upload_late.h) none is (0); the hold itself stays (2 in all);
 *   D a fragment shader that declares a storage buffer is made
 *     (rhi_vk_ps_storage.h; vk_resource.c): from then on the copy into the
 *     arena and the bracket wait for fragment work too (1 + 1 with the
 *     compute dispatch), as they must;
 *   F a draw whose fragment shader reads the arena (that shader, over
 *     vs_tri), then a copy over the arena in the same list: the copy's
 *     barrier waits for the fragment read (1), and the synchronisation
 *     checks find no write after read (with the fragment stage left out,
 *     as before the shader was made, they report one: the check this
 *     scope rests on is the layer's);
 *   E the record copied into a readback buffer: the last dispatch's words,
 *     word for word (cs_store: invocation i stores (i, 3i + 1, 0xC0DE0000 +
 *     i, 0x5EED)).
 * Then no validation error.  77 without a Vulkan device.
 *
 * The wide scopes (rhi_tile_rule.h: v0.11.2's, on a tile-based GPU by
 * default since v0.11.4; rhi_vk_scopes_arm fakes Arm's vendor id with
 * ICO_VK_FAKE_VENDOR=0x13B5, rhi_vk_scopes_wide asks with [dev]
 * barrier_scope = wide, rhi_vk_scopes_arm_narrow fakes Arm and asks for the
 * narrow ones): RhiLimits.wideBarriers as ICO_TEST_SCOPE_WANT (wide or
 * narrow) names, and then every list holds fragment work (the opening
 * barrier's colour output reaches the transfer stage, the copy's wait
 * names the fragment stage): B at least 2 (the opening barrier and the
 * mesh upload's wait), C2 and G at least as many as their narrow counts,
 * the same lists clean under the synchronisation checks and the record
 * read back word for word. */
#include "rhi.h"
#include "rhi_test_common.h"
#include "rhi_test_spv.h"
#include "rhi_vk_ps_storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SC_POINTS 64u
#define SC_W 16u

int rhi_vk_scope_cell(const RhiTestConfig *cfg);

static int s_failures;

#define SC_CHECK(cond, ...)                                                                        \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            rhi_test_log("FAIL %s scopes: ", cfg->label);                                          \
            rhi_test_log(__VA_ARGS__);                                                             \
            s_failures++;                                                                          \
        }                                                                                          \
    } while (0)

typedef struct ScRes {
    RhiBuffer up, arena, rec, rb;
    RhiTexture target, tex;
    RhiShader cs, vs, ps, vt;
    RhiBindGroupLayout lay, layF;
    RhiPipeline pc, pv, pf;
    RhiBindGroup g, gF;
    bool compute, vstores;
} ScRes;

static uint64_t holdsNow(void)
{
    RhiStats st;
    memset(&st, 0, sizeof(st));
    rhi_get_stats(&st);
    return st.fragmentHolds;
}

static void barrier(RhiCommandList cl, RhiTexture t, RhiState before, RhiState after)
{
    const RhiTextureBarrier b = {t, before, after};
    rhi_cmd_barrier(cl, &b, 1);
}

static void upload(RhiCommandList cl, const ScRes *r)
{
    rhi_cmd_copy_buffer(cl, r->up, 0, r->arena, 0, SC_POINTS * 16u);
}

static void record(RhiCommandList cl, const ScRes *r)
{
    if (r->compute) {
        (void)rhi_cmd_buffer_compute_begin(cl, r->rec);
        rhi_cmd_set_pipeline(cl, r->pc);
        rhi_cmd_set_bind_group(cl, 0, r->g);
        rhi_cmd_dispatch(cl, 1, 1, 1);
        (void)rhi_cmd_buffer_compute_end(cl, r->rec);
    }
    if (r->vstores) {
        /* a store pass after the dispatch, into the same buffer (the
           stores' bracket: write after write) */
        (void)rhi_cmd_buffer_store_begin(cl, r->rec);
        RhiRenderPassDesc rp;
        memset(&rp, 0, sizeof(rp));
        rp.width = SC_POINTS;
        rp.height = 1;
        rhi_cmd_begin_render_pass(cl, &rp);
        rhi_cmd_set_pipeline(cl, r->pv);
        rhi_cmd_set_bind_group(cl, 0, r->g);
        rhi_cmd_draw(cl, SC_POINTS, 0, 1);
        rhi_cmd_end_render_pass(cl);
        (void)rhi_cmd_buffer_store_end(cl, r->rec);
    }
}

static void colourPass(RhiCommandList cl, const ScRes *r)
{
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    rp.color[0].texture = r->target;
    rp.color[0].load = RHI_LOAD_CLEAR;
    rp.color[0].store = RHI_STORE_STORE;
    rp.colorCount = 1;
    rp.width = rp.height = SC_W;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_end_render_pass(cl);
}

static void texUpload(RhiCommandList cl, const ScRes *r, RhiState before)
{
    barrier(cl, r->tex, before, RHI_STATE_COPY_DST);
    rhi_cmd_copy_buffer_to_texture(cl, r->up, 0, SC_W * 4u, r->tex, 0,
                                   (RhiRect){0, 0, SC_W, SC_POINTS * 16u / (SC_W * 4u)});
    barrier(cl, r->tex, RHI_STATE_COPY_DST, RHI_STATE_SHADER_READ);
}

/* one list: begun, filled by fill(cl, r), ended, submitted, waited for;
   the holds it added */
typedef void (*ScFill)(RhiCommandList cl, const ScRes *r);

static uint64_t runList(const RhiTestConfig *cfg, const char *what, const ScRes *r, ScFill fill)
{
    const uint64_t h0 = holdsNow();
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        SC_CHECK(0, "list %s: no command list\n", what);
        return 0;
    }
    fill(cl, r);
    rhi_end_commands(cl);
    rhi_submit(cl);
    rhi_wait_idle();
    return holdsNow() - h0;
}

static void fillA(RhiCommandList cl, const ScRes *r)
{
    upload(cl, r);
    record(cl, r);
    barrier(cl, r->target, RHI_STATE_UNDEFINED, RHI_STATE_RENDER_TARGET);
    colourPass(cl, r);
    texUpload(cl, r, RHI_STATE_UNDEFINED);
}

static void fillB(RhiCommandList cl, const ScRes *r)
{
    upload(cl, r);
    record(cl, r);
    colourPass(cl, r); /* the target still in RENDER_TARGET */
}

static void fillC(RhiCommandList cl, const ScRes *r)
{
    texUpload(cl, r, RHI_STATE_SHADER_READ);
    barrier(cl, r->target, RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
}

static void fillC2(RhiCommandList cl, const ScRes *r)
{
    record(cl, r);
    barrier(cl, r->target, RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
    colourPass(cl, r);
}

/* G: a texture upload over the sampled texture, a mesh upload and the
   record, in the order before v0.11.3 (the texture first) and then, in the
   same list, in the renderer's order now (the mesh upload and the record
   first, port/render/rd_upload_late.h); s_holdsBefore[0], [1]: the holds
   recorded before each record's first command since that order began (one
   list: the slot holds VKR_MAX_CMD_LISTS) */
static uint64_t s_holdsBefore[2];

static void fillG(RhiCommandList cl, const ScRes *r)
{
    uint64_t h0 = holdsNow();
    texUpload(cl, r, RHI_STATE_SHADER_READ);
    upload(cl, r);
    s_holdsBefore[0] = holdsNow() - h0;
    record(cl, r);
    h0 = holdsNow();
    upload(cl, r);
    s_holdsBefore[1] = holdsNow() - h0;
    record(cl, r);
    texUpload(cl, r, RHI_STATE_SHADER_READ);
}

static void fillD(RhiCommandList cl, const ScRes *r)
{
    upload(cl, r);
    if (r->compute) {
        (void)rhi_cmd_buffer_compute_begin(cl, r->rec);
        rhi_cmd_set_pipeline(cl, r->pc);
        rhi_cmd_set_bind_group(cl, 0, r->g);
        rhi_cmd_dispatch(cl, 1, 1, 1);
        (void)rhi_cmd_buffer_compute_end(cl, r->rec);
    }
}

static void fillF(RhiCommandList cl, const ScRes *r)
{
    RhiRenderPassDesc rp;
    memset(&rp, 0, sizeof(rp));
    rp.color[0].texture = r->target;
    rp.color[0].load = RHI_LOAD_LOAD;
    rp.color[0].store = RHI_STORE_STORE;
    rp.colorCount = 1;
    rp.width = rp.height = SC_W;
    rhi_cmd_begin_render_pass(cl, &rp);
    rhi_cmd_set_pipeline(cl, r->pf);
    rhi_cmd_set_bind_group(cl, 0, r->gF);
    rhi_cmd_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    upload(cl, r);
}

/* the fragment read's pipeline: vs_tri and ps_storage, the arena at t0 of
   group 0, visible to the fragment stage alone */
static bool setupF(ScRes *r)
{
    RhiShaderDesc sd;
    memset(&sd, 0, sizeof(sd));
    sd.stage = RHI_STAGE_VERTEX;
    sd.bytecode = spv_vs_tri;
    sd.bytecodeSize = sizeof(spv_vs_tri);
    sd.entryPoint = "vs_tri";
    sd.debugName = "vs_tri";
    r->vt = rhi_create_shader(&sd);

    enum { FS = 1u << RHI_STAGE_FRAGMENT };

    const RhiBindSlot slot[1] = {{0, RHI_BIND_STORAGE_BUFFER, FS}};
    r->layF =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){slot, 1, "rhi test scopes read"});
    if (!r->vt.id || !r->ps.id || !r->layF.id) {
        return false;
    }
    RhiPipelineDesc pd;
    memset(&pd, 0, sizeof(pd));
    pd.vertex = r->vt;
    pd.fragment = r->ps;
    pd.layouts = &r->layF;
    pd.layoutCount = 1;
    pd.topology = RHI_TOPO_TRIANGLE_LIST;
    pd.cullNone = true;
    pd.blend[0].writeMask = 0xF;
    pd.colorFormats[0] = RHI_FMT_RGBA8_UNORM;
    pd.colorCount = 1;
    pd.depthFormat = RHI_FMT_UNKNOWN;
    pd.debugName = "rhi test scopes fragment read";
    r->pf = rhi_create_pipeline(&pd);
    RhiBinding b;
    memset(&b, 0, sizeof(b));
    b.slot = 0;
    b.type = RHI_BIND_STORAGE_BUFFER;
    b.buffer = r->arena;
    b.size = SC_POINTS * 16u;
    r->gF = rhi_create_bind_group(&(RhiBindGroupDesc){r->layF, &b, 1});
    return r->pf.id && r->gF.id;
}

static void fillE(RhiCommandList cl, const ScRes *r)
{
    rhi_cmd_copy_buffer(cl, r->rec, 0, r->rb, 0, SC_POINTS * 16u);
}

static bool setup(ScRes *r)
{
    const uint64_t size = SC_POINTS * 16u;
    r->up = rhi_create_buffer(
        &(RhiBufferDesc){size, RHI_BUF_COPY_SRC, RHI_MEM_UPLOAD, "rhi test scopes fill"});
    r->arena = rhi_create_buffer(
        &(RhiBufferDesc){size, RHI_BUF_INDEX | RHI_BUF_STORAGE_READ | RHI_BUF_COPY_DST,
                         RHI_MEM_DEVICE, "rhi test scopes arena"});
    r->rec = rhi_create_buffer(
        &(RhiBufferDesc){size, RHI_BUF_STORAGE_READ | RHI_BUF_STORAGE_WRITE | RHI_BUF_COPY_SRC,
                         RHI_MEM_DEVICE, "rhi test scopes record"});
    r->rb = rhi_create_buffer(&(RhiBufferDesc){size, RHI_BUF_READBACK | RHI_BUF_COPY_DST,
                                               RHI_MEM_READBACK, "rhi test scopes readback"});
    r->target = rhi_create_texture(&(RhiTextureDesc){SC_W, SC_W, 1, RHI_FMT_RGBA8_UNORM,
                                                     RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                     "rhi test scopes target", 0});
    r->tex = rhi_create_texture(
        &(RhiTextureDesc){SC_W, SC_POINTS * 16u / (SC_W * 4u), 1, RHI_FMT_RGBA8_UNORM,
                          RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rhi test scopes texture", 0});
    uint8_t *m = r->up.id ? rhi_map_buffer(r->up) : NULL;
    if (m) {
        memset(m, 0x5A, (size_t)size);
        rhi_unmap_buffer(r->up);
    }
    if (!r->up.id || !r->arena.id || !r->rec.id || !r->rb.id || !r->target.id || !r->tex.id || !m) {
        return false;
    }
    r->compute = rhi_limits()->computeStores;
    r->vstores = rhi_limits()->vertexStores;
    if (!r->compute && !r->vstores) {
        return true; /* the copies and passes alone */
    }
    RhiShaderDesc sd;
    memset(&sd, 0, sizeof(sd));
    if (r->compute) {
        sd.stage = RHI_STAGE_COMPUTE;
        sd.bytecode = spv_cs_store;
        sd.bytecodeSize = sizeof(spv_cs_store);
        sd.entryPoint = "cs_store";
        sd.debugName = "cs_store";
        r->cs = rhi_create_shader(&sd);
    }
    if (r->vstores) {
        sd.stage = RHI_STAGE_VERTEX;
        sd.bytecode = spv_vs_store;
        sd.bytecodeSize = sizeof(spv_vs_store);
        sd.entryPoint = "vs_store";
        sd.debugName = "vs_store";
        r->vs = rhi_create_shader(&sd);
    }

    enum { VS = 1u << RHI_STAGE_VERTEX, CS = 1u << RHI_STAGE_COMPUTE };

    const RhiBindSlot slot[1] = {{0, RHI_BIND_STORAGE_BUFFER_RW, r->compute ? VS | CS : VS}};
    r->lay = rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){slot, 1, "rhi test scopes"});
    RhiPipelineDesc pd;
    if (r->compute && r->cs.id && r->lay.id) {
        memset(&pd, 0, sizeof(pd));
        pd.compute = r->cs;
        pd.layouts = &r->lay;
        pd.layoutCount = 1;
        pd.debugName = "rhi test scopes compute";
        r->pc = rhi_create_pipeline(&pd);
    }
    if (r->vstores && r->vs.id && r->lay.id) {
        memset(&pd, 0, sizeof(pd));
        pd.vertex = r->vs;
        pd.layouts = &r->lay;
        pd.layoutCount = 1;
        pd.topology = RHI_TOPO_POINT_LIST;
        pd.cullNone = true;
        pd.depthFormat = RHI_FMT_UNKNOWN;
        pd.noRaster = true;
        pd.debugName = "rhi test scopes stores";
        r->pv = rhi_create_pipeline(&pd);
    }
    RhiBinding b;
    memset(&b, 0, sizeof(b));
    b.slot = 0;
    b.type = RHI_BIND_STORAGE_BUFFER_RW;
    b.buffer = r->rec;
    b.size = size;
    r->g =
        r->lay.id ? rhi_create_bind_group(&(RhiBindGroupDesc){r->lay, &b, 1}) : (RhiBindGroup){0};
    return r->g.id && (!r->compute || r->pc.id) && (!r->vstores || r->pv.id);
}

static void teardown(ScRes *r)
{
    rhi_wait_idle();
    if (r->pc.id) {
        rhi_destroy_pipeline(r->pc);
    }
    if (r->pv.id) {
        rhi_destroy_pipeline(r->pv);
    }
    if (r->pf.id) {
        rhi_destroy_pipeline(r->pf);
    }
    if (r->lay.id) {
        rhi_destroy_bind_group_layout(r->lay);
    }
    if (r->layF.id) {
        rhi_destroy_bind_group_layout(r->layF);
    }
    const RhiShader shaders[4] = {r->cs, r->vs, r->ps, r->vt};
    for (int i = 0; i < 4; i++) {
        if (shaders[i].id) {
            rhi_destroy_shader(shaders[i]);
        }
    }
    const RhiBuffer bufs[4] = {r->up, r->arena, r->rec, r->rb};
    for (int i = 0; i < 4; i++) {
        if (bufs[i].id) {
            rhi_destroy_buffer(bufs[i]);
        }
    }
    if (r->target.id) {
        rhi_destroy_texture(r->target);
    }
    if (r->tex.id) {
        rhi_destroy_texture(r->tex);
    }
}

int rhi_vk_scope_cell(const RhiTestConfig *cfg)
{
    s_failures = 0;
    if (!rhi_create_backend(cfg->backend)) {
        rhi_test_log("SKIP %s scopes: no %s backend\n", cfg->label, cfg->backend);
        return 77;
    }
    RhiDeviceDesc dd;
    memset(&dd, 0, sizeof(dd));
    dd.debugLayers = cfg->debugLayers;
    dd.appName = "rhi test scopes";
    if (!rhi_init(&dd)) {
        rhi_test_log("SKIP %s scopes: no device\n", cfg->label);
        return 77;
    }
    const char *env = getenv("ICO_VK_GLOBAL_BARRIERS");
    const bool global = env && env[0] && env[0] != '0';
    /* the scopes the device runs (rhi_tile_rule.h), and the ones the test
       was registered for */
    const bool wide = rhi_limits()->wideBarriers;
    const char *want = getenv("ICO_TEST_SCOPE_WANT");
    SC_CHECK(!want || !want[0] || strcmp(want, wide ? "wide" : "narrow") == 0,
             "the device runs the %s scopes (%s), %s wanted\n", wide ? "wide" : "narrow",
             rhi_limits()->barrierWords ? rhi_limits()->barrierWords : "?", want ? want : "");
    ScRes r;
    memset(&r, 0, sizeof(r));
    if (!setup(&r)) {
        SC_CHECK(0, "setup (buffers, textures, shaders, pipelines; compute %d, vertex stores %d)\n",
                 (int)r.compute, (int)r.vstores);
    } else {
        const uint64_t a = runList(cfg, "A", &r, fillA);
        const uint64_t b = runList(cfg, "B", &r, fillB);
        SC_CHECK(wide ? b >= 2 : b == 0,
                 "list B (the next picture's uploads, record and pass): %llu barriers held "
                 "fragment work, %s expected (the %s scopes)\n",
                 (unsigned long long)b, wide ? "2 or more" : "0", wide ? "wide" : "narrow");
        const uint64_t c = runList(cfg, "C", &r, fillC);
        SC_CHECK(wide ? c >= 1 : c == 1,
                 "list C (a texture upload over a sampled texture): %llu, %s expected\n",
                 (unsigned long long)c, wide ? "1 or more" : "1");
        const bool rec = r.compute || r.vstores;
        const uint64_t c2 = runList(cfg, "C2", &r, fillC2);
        const uint64_t c2want = rec && !global ? 1u : 0u;
        SC_CHECK(wide ? c2 >= c2want : c2 == c2want,
                 "list C2 (the record's end joined to a transition out of SHADER_READ): %llu, "
                 "%s%llu expected\n",
                 (unsigned long long)c2, wide ? "at least " : "", (unsigned long long)c2want);
        /* G: the texture upload's hold before the record in the old order
           (every later copy, the mesh upload's, waits behind it, and the
           record behind the copy), none before it in the new; the hold
           itself stays (one in each order) */
        const uint64_t g = runList(cfg, "G", &r, fillG);
        const uint64_t g1Before = s_holdsBefore[0], g2Before = s_holdsBefore[1];
        SC_CHECK(wide ? g1Before >= 1 && g >= 2 : g1Before == 1 && g2Before == 0 && g == 2,
                 "list G (a texture upload over a sampled texture, a mesh upload, the record): "
                 "before the record %llu in the old order (1 expected), %llu in the new (0 "
                 "expected); in all %llu (2 expected)\n",
                 (unsigned long long)g1Before, (unsigned long long)g2Before, (unsigned long long)g);
        /* a fragment module that declares a storage buffer */
        RhiShaderDesc sd;
        memset(&sd, 0, sizeof(sd));
        sd.stage = RHI_STAGE_FRAGMENT;
        sd.bytecode = spv_ps_storage;
        sd.bytecodeSize = sizeof(spv_ps_storage);
        sd.entryPoint = "ps_storage";
        sd.debugName = "ps_storage";
        r.ps = rhi_create_shader(&sd);
        SC_CHECK(r.ps.id != 0, "the storage fragment shader was not made\n");
        const uint64_t d = runList(cfg, "D", &r, fillD);
        const uint64_t dwant = 1u + (r.compute ? 1u : 0u);
        SC_CHECK(wide ? d >= dwant : d == dwant,
                 "list D (after a fragment shader that reads a storage buffer: the upload%s): "
                 "%llu, %llu expected\n",
                 r.compute ? " and the compute bracket" : "", (unsigned long long)d,
                 (unsigned long long)dwant);
        uint64_t f = 0;
        if (setupF(&r)) {
            f = runList(cfg, "F", &r, fillF);
            SC_CHECK(wide ? f >= 1 : f == 1,
                     "list F (a copy over the arena after a fragment shader read it): %llu, 1 "
                     "expected\n",
                     (unsigned long long)f);
        } else {
            SC_CHECK(0, "the fragment read's pipeline was not made\n");
        }
        (void)runList(cfg, "E", &r, fillE);
        if (r.compute) {
            const uint32_t *w = rhi_map_buffer(r.rb);
            uint32_t bad = 0;
            for (uint32_t i = 0; w && i < SC_POINTS; i++) {
                const uint32_t want[4] = {i, i * 3u + 1u, 0xC0DE0000u + i, 0x5EEDu};
                bad += memcmp(&w[i * 4u], want, sizeof(want)) != 0;
            }
            SC_CHECK(w && bad == 0, "the record read back: %u of %u points differ\n",
                     w ? bad : SC_POINTS, SC_POINTS);
            if (w) {
                rhi_unmap_buffer(r.rb);
            }
        }
        rhi_test_log("%s scopes: fragment holds A %llu, B %llu, C %llu, C2 %llu, G before the "
                     "record %llu old order / %llu new, D %llu, F %llu (compute %d, vertex "
                     "stores %d, global barriers %d, %s scopes)\n",
                     cfg->label, (unsigned long long)a, (unsigned long long)b,
                     (unsigned long long)c, (unsigned long long)c2, (unsigned long long)g1Before,
                     (unsigned long long)g2Before, (unsigned long long)d, (unsigned long long)f,
                     (int)r.compute, (int)r.vstores, (int)global, wide ? "wide" : "narrow");
    }
    teardown(&r);
    rhi_shutdown();
    const uint32_t errs = cfg->validationErrors ? cfg->validationErrors() : 0u;
    SC_CHECK(errs == 0, "%u validation error(s)\n", errs);
    if (!s_failures) {
        rhi_test_log("%s scopes: ok\n", cfg->label);
    }
    return s_failures ? 1 : 0;
}
