/* rd_replay.c: an RdFrame onto the RHI.
 *
 * One command list per replay.  The lists are walked in order 0..12 (11..12
 * for a keep frame) from the frame's start state; state commands update the
 * state block, actions draw:
 *
 *   RDC_CLEAR        a render pass with load-op clear on the target
 *   RDC_SCREEN       GS window-space prims through sprite_*_vs / sprite_ps;
 *                    sprites, fans and strips become triangle lists, points
 *                    become one-pixel sprites, line strips become line lists
 *   RDC_EXACT_BLEND  blend_int on RGBA8_UINT copies of source and
 *                    destination, written to a third RGBA8_UINT texture and
 *                    copied back into the destination (the exact GS blend
 *                    for feedback passes)
 *   RDC_COPY         texture copy (gif_MoveImage)
 *   RDC_MESH, RDC_SKINNED, RDC_GRID, RDC_PARTICLES
 *                    the VU1 program shaders (rd_replay_vu.c rd__replay_vu)
 *   RDC_SHADOW_RESET, RDC_SHADOW_STRIP, RDC_SHADOW_RESOLVE
 *                    the shadow count on the stencil (recorded by
 *                    rd_shadow.c; doShadow* below)
 *   RDC_POST_STUB    of kind RD_POST_FOG: doFog; of kinds RD_POST_REDUCTION
 *                    and RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR:
 *                    doBlurSprite, the reduction's and staticBlur.c's
 *                    sprites (recorded by rd_blur.c) through fx_sprite_ps
 *   RDC_OVERLAY_TEXT nothing here: the present collects it (rd_present.c
 *                    rd__overlay_collect)
 *   RDC_WORLD_PRIMS and an RDC_POST_STUB of any other kind
 *                    no replay: rd__not_implemented logs it once and skips
 *                    it (the tests make it fatal)
 *
 * RDC_AA1 (PRIM.AA1) is a state command; doScreen draws the AA1 edge
 * pixels through the sprite_aa1_* shaders (aa1Expand).
 *
 * GS sampling rules.  The GS samples a pixel at its integer coordinate and
 * covers x0 <= x < x1 for a sprite; the GPU samples at x + 0.5 with the
 * top-left rule.  FrameCB.g_origin.zw = 0.5 moves GS coordinates half a
 * pixel right and down, so a GS integer lands on a GPU pixel centre and the
 * two rules select the same pixels; attributes interpolated at the GPU
 * centre are the GS attributes at the integer point.  The game's own -4
 * (quarter-pixel) corner nudges and +8 (half-texel) UV nudges stay in the
 * data.  Texel centres are at i + 0.5 on both, so UVs only need dividing by
 * the texture size.
 *
 * Consecutive RDC_SCREEN commands that bind the same things are one draw
 * (doScreen, joinsRun, flushScreenRun).
 *
 * Render passes stay open across draws to the same colour/depth pair and
 * are closed by a target change, a clear, a copy, or a draw that samples a
 * target whose state must change.  rd tracks every texture's RhiState
 * (backends do not).  A draw that samples its own render target samples a
 * copy taken just before it (the GS reads the live buffer).
 *
 * Before the first list the replay sizes and takes its slot's upload ring,
 * uploads the dirty textures and the static meshes, and clears the targets
 * with a new texture: rd_upload.c (rd__estimate_ring, rd__ensure_ring,
 * rd__upload_dirty_textures, rd__upload_meshes, rd__clear_new_targets).
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_passring.h"
#include "rd_replay.h"
#include "rd_mesh.h"
#include "rd_tex.h"
#include "shader_consts.h"
#include "shaders_gen.h"
#include "texpack.h"

/* the snapshots taken, copies and swaps alike (rd__blur_snapshot_count) */
static uint64_t s_blurSnapCount;

static const char *const s_vsNames[RD_VS_COUNT] = {
    "sprite_ui_vs" /* indexed by RD_VS_* */, "sprite_world_vs", "blit_vs", "blend_int_vs",
    "vu_prelit_vs", "vu_lit_vs", "vu_lit_spec_vs", "vu_reflect_vs", "vu_skin_vs", "vu_skin_spec_vs",
    "vu_skin_debug_vs", "vu_grid_vs", "vu_grid_lit_vs", "vu_grid_spec_vs", "vu_particle_vs",
    "fx_rect_vs", "sprite_aa1_ui_vs", "sprite_aa1_world_vs", "sprite_stq_ui_vs",
    "sprite_stq_world_vs", "crt_vs", "sprite_plane_ui_vs", "sprite_plane_world_vs",
    "sprite_stq_plane_ui_vs", "sprite_stq_plane_world_vs", "vu_prelit_pre_vs", "vu_lit_pre_vs",
    "vu_lit_spec_pre_vs", "vu_reflect_pre_vs", "vu_skin_pre_vs", "vu_skin_spec_pre_vs",
    "vu_skin_debug_pre_vs", "vu_grid_pre_vs", "vu_grid_lit_pre_vs", "vu_grid_spec_pre_vs",
    /* RD_VS_OPTIONAL_FIRST on */
    "vu_main_vs", "vu_particle_main_vs", "vu_tri_rule_pre_vs", "vu_tri_tick_pre_vs",
    "vu_tri_planes_pre_vs", "vu_particle_pre_vs"};

const char *rd__vs_name(uint8_t vs)
{
    return vs < RD_VS_COUNT ? s_vsNames[vs] : "?";
}

static const char *const s_fsNames[RD_FS_COUNT] = {
    "sprite_ps" /* indexed by RD_FS_* */, "blit_ps", "blend_int_ps", "date_snap_ps",
    "camera_probe_ps", "vu_ps", "fog_lut_ps", "fx_sprite_ps", "wrap_acc_ps", "wrap_resolve_ps",
    "font_ps", "font_sheet_ps", "sprite_aa1_ps", "box_reduce_ps", "sprite_stq_ps", "crt_bloom_ps",
    "crt_blur_ps", "crt_ps", "sprite_texa_ps", "vu_texa_ps", "blit_depth_ps", "fog_lut_buffer_ps",
    "aa_reduce_ps", "fog_lut_ms_ps", "depth_resolve_ps", "depth_upsample_ps", "vu_pre_ps",
    /* RD_FS_OPTIONAL_FIRST on */
    "sprite_ps_planes", "sprite_texa_ps_planes", "sprite_stq_ps_planes", "fog_lut_local_ps",
    "fx_sprite_u_ps", "fx_blur2_ps", "date_clear_ps", "date_stencil_ps", "vu_np_ps",
    "vu_texa_np_ps", "fx_frame_ps", "fx_local_ps"};

const char *rd__fs_name(uint8_t fs)
{
    return fs < RD_FS_COUNT ? s_fsNames[fs] : "?";
}

/* ------------------------------------------------------------------ init */

RhiShader rd__make_shader(const char *name)
{
    const IcoShaderBlob *b = ico_find_shader(name);
    if (!b) {
        rd__log("shader %s missing from the table", name);
        return (RhiShader){0};
    }
    RhiShaderDesc d;
    memset(&d, 0, sizeof(d));
    d.stage = b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT;
    if (rhi_backend() == RHI_BACKEND_D3D12) {
        d.bytecode = b->dxil;
        d.bytecodeSize = b->dxil_len;
    } else {
        d.bytecode = b->spirv;
        d.bytecodeSize = b->spirv_len;
    }
    d.entryPoint = b->entry;
    d.debugName = b->name;
    return rhi_create_shader(&d);
}

/* rhi_init succeeded: rd__gpu_shutdown has a device to tear down (without a
 * Vulkan loader rhi_init fails and there is nothing to wait on or destroy). */
static bool s_rhiUp;

/* the slots of every layout made here, so bindGroup can count a group that
 * leaves one without a binding (RdPerfRecord.unboundSlots) */
typedef struct LayoutSlots {
    uint32_t id, count;
    const RhiBindSlot *slots;
} LayoutSlots;

static LayoutSlots s_layoutSlots[8];
static uint32_t s_layoutSlotCount;

static RhiBindGroupLayout makeLayout(const RhiBindSlot *slots, uint32_t count, const char *name)
{
    const RhiBindGroupLayout l =
        rhi_create_bind_group_layout(&(RhiBindGroupLayoutDesc){slots, count, name});
    if (l.id && s_layoutSlotCount < sizeof(s_layoutSlots) / sizeof(s_layoutSlots[0])) {
        s_layoutSlots[s_layoutSlotCount++] = (LayoutSlots){l.id, count, slots};
    }
    return l;
}

bool rd__gpu_init(void *sdlWindow)
{
    RhiDeviceDesc dd;
    memset(&dd, 0, sizeof(dd));
    dd.sdlWindow = sdlWindow;
    dd.vsync = g_rd.settings.vsync != 0;
    dd.appName = "ico";
    if (!rhi_init(&dd)) {
        return false;
    }
    rd__perf_reset(); /* the new device's counters start at 0 */
    s_rhiUp = true;
    const RhiLimits *lim = rhi_limits();
    if (!lim->stencilWrap) {
        rd__log("device lacks stencil wrap");
        return false;
    }

    enum { VS = 1u << RHI_STAGE_VERTEX, FS = 1u << RHI_STAGE_FRAGMENT };

    s_layoutSlotCount = 0;
    /* FrameCB, DrawCB, VuCB and VuBoneCB take dynamic offsets into the
     * ring, so each of these layouts has one group a replay (uniformGroup,
     * rd_replay_vu.c vuGroup) */
    if (lim->maxDynamicUniforms < 4) {
        rd__log("device has %u dynamic uniform buffers, 4 needed", lim->maxDynamicUniforms);
        return false;
    }
    static const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS}};
    static const RhiBindSlot s1[1] = {{1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS}};
    /* t2: sprite_ps's DATE snapshot */
    static const RhiBindSlot s2[3] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                      {1, RHI_BIND_SAMPLER, FS},
                                      {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    static const RhiBindSlot s3[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                                      {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    g_rd.layoutFrame = makeLayout(s0, 1, "rd frame");
    g_rd.layoutDraw = makeLayout(s1, 1, "rd draw");
    g_rd.layoutTex = makeLayout(s2, 3, "rd tex");
    g_rd.layoutInt = makeLayout(s3, 2, "rd int");
    /* the VU programs' group 1 (vu_common.hlsli): the stream, DrawCB,
     * VuCB, VuBoneCB and the vertex pre-pass's record (vu_rec_buf) */
    static const RhiBindSlot s4[5] = {{0, RHI_BIND_STORAGE_BUFFER, VS},
                                      {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS},
                                      /* VuCB and the record also to the pixel side: the
                                       * per-triangle planes are read there */
                                      {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS},
                                      {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                                      {4, RHI_BIND_STORAGE_BUFFER, VS | FS}};
    g_rd.layoutVu = makeLayout(s4, 5, "rd vu");
    bool ok = g_rd.layoutFrame.id && g_rd.layoutDraw.id && g_rd.layoutTex.id && g_rd.layoutInt.id &&
              g_rd.layoutVu.id;
    /* the exact blend's group 3 (rd_dst_read.c): the target as a storage
     * texture (interlock, ROV) or as the pass's input attachment; made only
     * for the mode [dev] blend / ICO_RD_BLEND opted into, where the device
     * has that destination read (the default fixed-function blend makes
     * neither; a missing piece leaves the mode out, rd__dst_init, never the
     * renderer) */
    const int dstAsked = rd__dst_asked_env();
    const bool dstStorage =
        dstAsked == RD_BLEND_EXACT_STORAGE && (lim->fragmentInterlock || lim->rov);
    const bool dstAttach = dstAsked == RD_BLEND_EXACT_ATTACHMENT && lim->rasterOrderAttachment;
    if (dstStorage) {
        static const RhiBindSlot sd[1] = {{0, RHI_BIND_STORAGE_TEXTURE, FS}};
        g_rd.layoutDstStorage = makeLayout(sd, 1, "rd dst storage");
    }
    if (dstAttach) {
        static const RhiBindSlot sa[1] = {{0, RHI_BIND_INPUT_ATTACHMENT, FS}};
        g_rd.layoutDstAttach = makeLayout(sa, 1, "rd dst attachment");
    }
    /* group 2 of the passes' own reads by local read (rd__fs_local_read:
     * the fog's depth, DATE's colour), wherever the device has local read */
    if (lim->localRead) {
        static const RhiBindSlot sl[2] = {{1, RHI_BIND_INPUT_ATTACHMENT, FS},
                                          {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
        g_rd.layoutLocal = makeLayout(sl, 2, "rd local read");
    }
    {
        /* the embedded modules' size, once a run (a phone's driver
           compiles the start-up set in time about proportional to it;
           tools/check_spirv.py holds each to a budget) */
        static bool logged;
        uint32_t largest = 0;
        const char *name = "";
        for (unsigned i = 0; i < g_icoShaderCount && !logged; i++) {
            if (g_icoShaders[i].spirv_len / 4u > largest) {
                largest = (uint32_t)(g_icoShaders[i].spirv_len / 4u);
                name = g_icoShaders[i].name;
            }
        }
        if (!logged) {
            rd__log("shaders: %u modules, largest %s %u words", g_icoShaderCount, name, largest);
            logged = true;
        }
    }
    for (int i = 0; i < RD_VS_COUNT; i++) {
        /* a module from RD_VS_OPTIONAL_FIRST on that is missing from the
         * table leaves its id empty (its draws take their fallback) */
        if (i >= RD_VS_OPTIONAL_FIRST && !ico_find_shader(s_vsNames[i])) {
            continue;
        }
        g_rd.vs[i] = rd__make_shader(s_vsNames[i]);
        ok = ok && g_rd.vs[i].id;
    }
    ok = rd__vu_policy_init(lim) && ok; /* the plain variants, rd_pipeline.c */
    for (int i = 0; i < RD_FS_COUNT; i++) {
        /* the per-sample fog reads SV_SampleIndex: only a device with
         * sample-rate shading takes it (else it is never asked for) */
        if (i == RD_FS_FOG_MS && !lim->sampleRateShading) {
            continue;
        }
        if (i >= RD_FS_OPTIONAL_FIRST && !ico_find_shader(s_fsNames[i])) {
            continue; /* as the vertex loop's */
        }
        /* without dual-source blending the gs_dual_out entries are not
         * created (their second output needs the feature); their *_nodual
         * twins always are (rd_set_no_dual) */
        /* the exact blend's twins (rd_dst_read.c), for the opted-in
         * destination read alone; their absence only leaves the mode out */
        if (rd__fs_has_dst((uint8_t)i)) {
            char name[64];
            if (dstStorage) {
                snprintf(name, sizeof(name), "%s_dstrov", s_fsNames[i]);
                g_rd.fsDstRov[i] = rd__make_shader(name);
            }
            if (dstAttach) {
                snprintf(name, sizeof(name), "%s_dstatt", s_fsNames[i]);
                g_rd.fsDstAtt[i] = rd__make_shader(name);
            }
        }
        if (rd__fs_has_no_dual((uint8_t)i)) {
            char name[64];
            snprintf(name, sizeof(name), "%s_nodual", s_fsNames[i]);
            g_rd.fsNoDual[i] = rd__make_shader(name);
            ok = ok && g_rd.fsNoDual[i].id;
            if (!lim->dualSourceBlend) {
                continue;
            }
        }
        g_rd.fs[i] = rd__make_shader(s_fsNames[i]);
        ok = ok && g_rd.fs[i].id;
    }
    /* set 0: the Original samplers (one level); sets 1 and 2: the Enhanced
     * filter's trilinear and anisotropic ones over the generated mips
     * (rd__upload_dirty_textures); set 1 is also the GS's linear-between-levels
     * mipmapping (gsLodSampler), set 3 its nearest-level one */
    for (int i = 0; i < RD_SAMPLER_COUNT * RD_SAMPLER_SETS; i++) {
        const int set = i / RD_SAMPLER_COUNT, k = i % RD_SAMPLER_COUNT;
        RhiSamplerDesc sd;
        memset(&sd, 0, sizeof(sd));
        sd.mag = (k & 1) ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.min = (k & 2) ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.mip = set == 1 || set == 2 ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.s = (k & 4) ? RHI_WRAP_CLAMP : RHI_WRAP_REPEAT;
        sd.t = (k & 8) ? RHI_WRAP_CLAMP : RHI_WRAP_REPEAT;
        sd.maxAnisotropy = 1.0f;
        if (set) {
            sd.maxLod = 16.0f;
        }
        if (set == 2) {
            const float a = lim->maxAnisotropy;
            sd.maxAnisotropy = a > 16.0f ? 16.0f : (a < 1.0f ? 1.0f : a);
        }
        g_rd.samplers[i] = rhi_create_sampler(&sd);
        ok = ok && g_rd.samplers[i].id;
    }
    g_rd.dummy = rhi_create_texture(&(RhiTextureDesc){
        1, 1, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd dummy", 0});
    g_rd.dummyState = RHI_STATE_UNDEFINED;
    rd__vu_prepass_init(lim); /* the vertex pre-pass: on or off, why */
    rd__perf_passes_init();   /* the per-pass timing: every replay or sampled */
    return ok && g_rd.dummy.id;
}

void rd__gpu_shutdown(void)
{
    if (!s_rhiUp) {
        return;
    }
    rhi_wait_idle();
    for (int i = 0; i < RD_SCRATCH_COUNT; i++) {
        if (g_rd.scratch[i].tex.id) {
            rhi_destroy_texture(g_rd.scratch[i].tex);
        }
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (g_rd.ring[i].id) {
            rhi_destroy_buffer(g_rd.ring[i]);
        }
    }
    rd__pipeline_cache_clear();
    rd__mesh_gpu_shutdown();   /* the mesh arena */
    rd__vu_prepass_shutdown(); /* the vertex pre-pass's record (rd_replay_vu.c) */
    rd__fog_shutdown();        /* the fog LUT */
    rd__shadow_shutdown();     /* the box-reduced shadow count */
    rd__wrap_shutdown();       /* the COLCLAMP 0 wrap's pipelines and accumulator */
    rd__msaa_free();           /* the MSAA scene pair */
    if (g_rd.dummy.id) {
        rhi_destroy_texture(g_rd.dummy);
    }
    for (int i = 0; i < RD_SAMPLER_COUNT * RD_SAMPLER_SETS; i++) {
        if (g_rd.samplers[i].id) {
            rhi_destroy_sampler(g_rd.samplers[i]);
        }
    }
    for (int i = 0; i < RD_VS_COUNT; i++) {
        if (g_rd.vs[i].id) {
            rhi_destroy_shader(g_rd.vs[i]);
        }
        if (g_rd.vsPlain[i].id) {
            rhi_destroy_shader(g_rd.vsPlain[i]);
        }
    }
    for (int i = 0; i < RD_FS_COUNT; i++) {
        if (g_rd.fs[i].id) {
            rhi_destroy_shader(g_rd.fs[i]);
        }
        if (g_rd.fsNoDual[i].id) {
            rhi_destroy_shader(g_rd.fsNoDual[i]);
        }
        if (g_rd.fsDstRov[i].id) {
            rhi_destroy_shader(g_rd.fsDstRov[i]);
        }
        if (g_rd.fsDstAtt[i].id) {
            rhi_destroy_shader(g_rd.fsDstAtt[i]);
        }
    }
    RhiBindGroupLayout ls[8] = {g_rd.layoutFrame,     g_rd.layoutDraw, g_rd.layoutTex,
                                g_rd.layoutInt,       g_rd.layoutVu,   g_rd.layoutDstStorage,
                                g_rd.layoutDstAttach, g_rd.layoutLocal};
    for (int i = 0; i < 8; i++) {
        if (ls[i].id) {
            rhi_destroy_bind_group_layout(ls[i]);
        }
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        g_rd.ring[i] = (RhiBuffer){0};
        g_rd.ringMap[i] = NULL;
        g_rd.ringCap[i] = 0;
    }
    memset(g_rd.scratch, 0, sizeof(g_rd.scratch));
    rhi_shutdown();
    s_rhiUp = false;
}

/* ------------------------------------------------------------- helpers */

static RhiCommandList s_cl;

static uint32_t s_slot;

static uint64_t s_ringOff;

RhiCommandList rd__replay_cl(void)
{
    return s_cl;
}

/* ------------------------------------------------------ the pass window
 * rd_window.c (rd_internal.h g_rdWin): the replay numbers its passes 1, 2,
 * ... as they begin (passPush) and records the draws and copies of the
 * window's passes only (rd__win_muted, rd__win_take). */

/* a pass begins: its pass ring entry and its number in the window */
static const void *passPush(RdPassKind kind, uint32_t color, uint32_t cw, uint32_t ch,
                            uint32_t depth, uint32_t dw, uint32_t dh, int storage, int post)
{
    const void *m = rd__passring_push(kind, color, cw, ch, depth, dw, dh, storage, post);
    if (g_rdWin.first && !g_rdWin.noCount) {
        g_rdWin.passes++;
        if (g_rdWin.passes == g_rdWin.last) {
            g_rdWin.lastSeq = (uint64_t)(uintptr_t)m;
        } else if (g_rdWin.last && g_rdWin.passes == g_rdWin.last + 1u) {
            g_rdWin.nextSeq = (uint64_t)(uintptr_t)m;
            g_rdWin.stop = true;
        }
    }
    return m;
}

static void drawCmd(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                    uint32_t instanceCount)
{
    if (g_rdWin.first && !rd__win_take()) {
        return;
    }
    rhi_cmd_draw(cl, vertexCount, firstVertex, instanceCount);
}

static void drawIndexedCmd(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                           int32_t vertexOffset, uint32_t instanceCount)
{
    if (g_rdWin.first && !rd__win_take()) {
        return;
    }
    rhi_cmd_draw_indexed(cl, indexCount, firstIndex, vertexOffset, instanceCount);
}

static void copyCmd(RhiCommandList cl, RhiTexture src, RhiRect srcRegion, RhiTexture dst,
                    int32_t dstX, int32_t dstY)
{
    if (!rd__win_muted()) {
        rhi_cmd_copy_texture(cl, src, srcRegion, dst, dstX, dstY);
    }
}

uint32_t rd__ring_slot(void)
{
    return s_slot;
}

/* the clear doClear has not recorded yet (RdPendingClear),
 * with the replay it belongs to and its textures */
static struct {
    RdPendingClear p;
    struct Replay *r;
    uint32_t colorTex, depthTex; /* RhiTexture ids */
} s_pend;

/* The stencil's shadow window (RdStencilWindow, by depth texture id), with
 * the colour target of the reset not recorded yet and its textures */
static struct {
    RdStencilWindow w;
    uint32_t color, depth;       /* the reset's target ids */
    uint32_t colorTex, depthTex; /* their RhiTexture ids, 0 when no reset is pending */
} s_stencil;

RdStencilWindow *rd__replay_stencil(void)
{
    return &s_stencil.w;
}

/* The MSAA epoch's state (RdSettings.antiAlias MSAA; the section "the MSAA
 * epoch" below says what it does) */
static struct {
    RhiTexture color, depth; /* SCENE_MS: RGBA8 and D32F_S8, samples each */
    RhiState colorState, depthState;
    uint32_t w, h, samples;          /* the pair's size and count (0: none made) */
    uint32_t want;                   /* this replay's count; 1 = no MSAA */
    uint32_t sceneColor, sceneDepth; /* SCENE's single-sampled textures (ids) */
    bool active;                     /* the epoch is open: SCENE's picture is in the pair */
    bool ended;                      /* it closed this replay (no re-entry) */
    bool internal;                   /* the epoch's own transitions of SCENE (no hook) */
    bool colorLive, depthLive;       /* the pair's colour / depth hold the picture */
    bool failLogged;
    struct Replay *r;
} s_ms;

static void msaaEnd(struct Replay *r);

static bool s_fogResolvedForced; /* rd__msaa_test_fog_resolved */

void rd__msaa_test_fog_resolved(bool on)
{
    s_fogResolvedForced = on;
}

static void flushClear(void);
static void flushStencilClear(void);

void rd__transition(RhiCommandList cl, RhiTexture t, RhiState *cur, RhiState want)
{
    /* the MSAA epoch: SCENE's own colour or depth touched outside a pass
     * into SCENE_MS (read, copied, drawn single-sampled) closes it first,
     * resolving the picture into them (msaaEnd); even a transition to the
     * state they are in (the epoch put them in their attachment states) */
    if (s_ms.active && !s_ms.internal && t.id != 0 &&
        (t.id == s_ms.sceneColor || t.id == s_ms.sceneDepth) && cl.id == s_cl.id) {
        msaaEnd(s_ms.r);
    }
    if (!t.id || *cur == want) {
        return;
    }
    /* a pending clear's target leaves its attachment state (read,
     * copied): the clear is recorded first.  A pending clear or reset
     * belongs to the replay's list (s_cl), so only a transition in that list
     * records it; any other list finds nothing pending (the replay's last
     * endPass records both before it ends its list). */
    if (s_pend.p.target && (t.id == s_pend.colorTex || t.id == s_pend.depthTex)) {
        assert(cl.id == s_cl.id && "a pending clear's target moved in another list");
        if (cl.id == s_cl.id) {
            flushClear();
        }
    }
    /* the same for a pending shadow reset's targets */
    if (s_stencil.depthTex && (t.id == s_stencil.colorTex || t.id == s_stencil.depthTex)) {
        assert(cl.id == s_cl.id && "a pending shadow reset's target moved in another list");
        if (cl.id == s_cl.id) {
            flushStencilClear();
        }
    }
    RhiTextureBarrier b = {t, *cur, want};
    rhi_cmd_barrier(cl, &b, 1);
    *cur = want;
}

/* the slots of layout (one of rd__gpu_init's) that b[0..n) leaves without a
 * binding (matched by slot and type); 0 for a layout made elsewhere */
static uint32_t unboundSlots(RhiBindGroupLayout layout, const RhiBinding *b, uint32_t n)
{
    for (uint32_t i = 0; i < s_layoutSlotCount; i++) {
        const LayoutSlots *l = &s_layoutSlots[i];
        if (l->id != layout.id) {
            continue;
        }
        uint32_t missing = 0;
        for (uint32_t k = 0; k < l->count; k++) {
            bool found = false;
            for (uint32_t j = 0; j < n && !found; j++) {
                found = b[j].slot == l->slots[k].slot && b[j].type == l->slots[k].type;
            }
            missing += found ? 0u : 1u;
        }
        return missing;
    }
    return 0;
}

/* every transient bind group of the replay goes through here, timed into
 * the record's bind phase and counted by kind (texture or uniform: the
 * uniform layouts' groups are the frame, draw and VU ones) */
static RhiBindGroup bindGroup(RhiBindGroupLayout layout, const RhiBinding *b, uint32_t n)
{
    const double t0 = rd__now_ms();
    const RhiBindGroup g = rhi_create_bind_group(&(RhiBindGroupDesc){layout, b, n});
    g_rdPerf.bindMs += rd__now_ms() - t0;
    g_rdPerf.unboundSlots += unboundSlots(layout, b, n);
    if (g.id) {
        /* the exact blend's group (dstBind: the pass's target as a storage
         * texture or input attachment) is a texture group too */
        if (layout.id == g_rd.layoutTex.id || layout.id == g_rd.layoutInt.id ||
            (g_rd.layoutDstStorage.id && layout.id == g_rd.layoutDstStorage.id) ||
            (g_rd.layoutDstAttach.id && layout.id == g_rd.layoutDstAttach.id)) {
            g_rdPerf.textureGroups++;
        } else {
            g_rdPerf.uniformGroups++;
        }
    }
    return g;
}

uint64_t rd__ring_alloc(uint64_t size, uint64_t align)
{
    uint64_t off = (s_ringOff + align - 1) / align * align;
    if (off + size > g_rd.ringCap[s_slot]) {
        rd__log("upload ring overflow (%llu + %llu > %llu)", (unsigned long long)off,
                (unsigned long long)size, (unsigned long long)g_rd.ringCap[s_slot]);
        return ~0ull;
    }
    s_ringOff = off + size;
    return off;
}

/* Bind groups are transient (rhi.h: valid in the frame slot
 * they were made in), so the caches below are tagged with the epoch, which
 * every rhi_wait_frame of the renderer starts anew (rd__wait_frame). */
static uint32_t s_bindEpoch = 1;

void rd__wait_frame(void)
{
    rhi_wait_frame();
    s_bindEpoch++;
}

/* FNV-1a over a uniform block's bytes */
static uint32_t hashBytes(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    for (uint32_t i = 0; i < n; i++) {
        h = (h ^ b[i]) * 16777619u;
    }
    return h;
}

/* The uniform layouts' groups.  FrameCB, DrawCB, VuCB and
 * VuBoneCB are RHI_BIND_UNIFORM_BUFFER_DYNAMIC slots: a group binds the
 * ring at offset 0 with the block's size, and each draw passes its block's
 * ring offset when it binds the group (rd__bind_uniform).  So the frame and
 * draw layouts have one group per replay (per ring buffer, which does not
 * change within one, and per block size), whatever the number of draws. */
#define RD_DYN_GROUPS 6

/* keyed by the binding's size too: the CRT pass binds IcoCrtCB through the
 * draw layout, so one layout can carry blocks of two sizes in one replay */
typedef struct DynEntry {
    uint32_t epoch, layout, buffer, size;
    RhiBindGroup group;
} DynEntry;

static DynEntry s_dynGroups[RD_DYN_GROUPS];

static RhiBindGroup dynamicGroup(RhiBindGroupLayout layout, uint32_t slot, uint32_t size)
{
    const RhiBuffer ring = g_rd.ring[s_slot];
    DynEntry *free = NULL;
    for (int i = 0; i < RD_DYN_GROUPS; i++) {
        DynEntry *e = &s_dynGroups[i];
        if (e->epoch == s_bindEpoch && e->layout == layout.id && e->buffer == ring.id &&
            e->size == size) {
            return e->group;
        }
        if (!free && e->epoch != s_bindEpoch) {
            free = e;
        }
    }
    RhiBinding b;
    memset(&b, 0, sizeof(b));
    b.slot = slot;
    b.type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
    b.buffer = ring;
    b.offset = 0;
    b.size = size;
    const RhiBindGroup g = bindGroup(layout, &b, 1);
    if (g.id && free) {
        free->epoch = s_bindEpoch;
        free->layout = layout.id;
        free->buffer = ring.id;
        free->size = size;
        free->group = g;
    }
    return g;
}

void rd__bind_uniform(RhiCommandList cl, uint32_t group, RdUniform u)
{
    rhi_cmd_set_bind_group_offsets(cl, group, u.group, &u.offset, 1);
}

/* uniform blocks by content: the same DrawCB in one replay is written once
 * (and binding the group with the offset already bound records nothing) */
#define RD_UNIFORM_CACHE 512
#define RD_UNIFORM_CACHE_BYTES 256

typedef struct UniformEntry {
    uint32_t epoch, hash, layout, size;
    uint32_t offset; /* in the ring */
    uint8_t data[RD_UNIFORM_CACHE_BYTES];
} UniformEntry;

static UniformEntry s_uniformCache[RD_UNIFORM_CACHE];

static RdUniform uniformGroup(RhiBindGroupLayout layout, uint32_t slot, const void *data,
                              uint32_t size)
{
    RdUniform u = {dynamicGroup(layout, slot, size), 0};
    if (!u.group.id) {
        return u;
    }
    UniformEntry *e = NULL;
    uint32_t h = 0;
    if (size <= RD_UNIFORM_CACHE_BYTES) {
        h = hashBytes(data, size, 2166136261u ^ layout.id);
        e = &s_uniformCache[h % RD_UNIFORM_CACHE];
        if (e->epoch == s_bindEpoch && e->hash == h && e->layout == layout.id && e->size == size &&
            memcmp(e->data, data, size) == 0) {
            u.offset = e->offset;
            return u;
        }
    }
    const uint64_t off = rd__ring_alloc(size, rhi_limits()->uniformAlign);
    if (off == ~0ull) {
        return (RdUniform){{0}, 0};
    }
    memcpy(g_rd.ringMap[s_slot] + off, data, size);
    g_rdPerf.uploadUniformBytes += size;
    u.offset = (uint32_t)off;
    if (e) {
        e->epoch = s_bindEpoch;
        e->hash = h;
        e->layout = layout.id;
        e->size = size;
        e->offset = u.offset;
        memcpy(e->data, data, size);
    }
    return u;
}

/* The camera of the frame being replayed: the frame's own, else the
 * last one a replayed frame carried (the VU keeps its matrices until the
 * game uploads new ones). */
static RdCamera s_replayCam;

static int s_hasReplayCam;

void rd__set_replay_camera(const RdCamera *cam)
{
    if (cam) {
        s_replayCam = *cam;
        s_hasReplayCam = 1;
        g_rd.frameAspect = cam->aspect; /* the frame's exact width (rd__frame_wexact) */
    }
}

RdUniform rd__frame_group(uint32_t targetW, uint32_t targetH, float originX, float originY)
{
    return rd__frame_group_z(targetW, targetH, originX, originY, 1.0f / 16777216.0f);
}

RdUniform rd__frame_group_z(uint32_t targetW, uint32_t targetH, float originX, float originY,
                            float zScale)
{
    return rd__frame_group_ex(targetW, targetH, originX, originY, zScale, 1.0f, 1.0f, 1.0f);
}

/* the MSAA epoch's expanded sprites (msaaFrame): integer GS coordinates on
 * texel edges instead of texel centres */
static bool s_frameNoHalf;

/* The exact viewport (RENDER.md "Screen planes").  With the viewport the target
 * itself, the 4:3 columns of a frame changed with its width (rd_native_test
 * measured up to 160 pixels at 1x, 600 at 2x):
 *  - the GPU clips a triangle at the viewport (the clip volume), and the
 *    corners it makes there are float results off the GS's 1/16 grid, so
 *    a triangle reaching past the target had its edges inside it moved by
 *    a rounding, and a pixel centre on such an edge (a tie the fill rule
 *    decides) went the other way; the target's edge is at another GS
 *    column at every width (at W 512 it is the 4:3 band's own edge);
 *  - the positions went to clip space through 1 / the target's size and
 *    back through the GPU's multiply by half of it, exact only at a power
 *    of two (the GPU's sub-pixel snap absorbs that for a corner on the 1/16
 *    grid, not for a corner the GPU clipped at the eye);
 *  - the colours and STs the GPU interpolates depend on the pixel's place
 *    in the target (vu_common.hlsli vu_attr_planes, the VU programs' fix).
 * Every pass the replay records instead has one viewport size, the largest
 * power of two the GPU takes (vpExtent; it reaches far past the target and
 * the scissor, always set within the target, bounds the drawing), centred
 * on the target, and FrameCB maps positions to it (rd__frame_group_ex,
 * common.hlsli gs_px_to_ndc): offset from the target's centre, times the
 * scale, plus the half texel, times 2 / the size, then the GPU's multiply
 * by half the size: every step exact for a GS position at an integer
 * scale, and the same clip-space numbers at every width (a frame-wide
 * target's centre is GS 2048), so the GPU clips the same corners the same
 * way whatever the width (and almost never at the sides).
 * On while replayFrame records its lists; the present, the films and the
 * camera probe keep the plain mapping (FrameCB vp 0). */
static bool s_vpExact;

/* The exact viewport's size for a target n texels across: the largest
 * power of two the GPU's viewport takes (maxRenderTargetSize is at most its
 * viewport limit; 16384 at most, D3D12's viewport bounds), the same for
 * every target, so the clip-space positions, and the GPU's clipping of a
 * triangle that reaches past the viewport, are the same at every frame
 * width; n itself where that is smaller than n */
static uint32_t vpExtent(uint32_t n)
{
    const uint32_t lim = g_rd.hasDevice ? rhi_limits()->maxRenderTargetSize : 16384u;
    uint32_t p = 16384u;
    while (p > lim) {
        p >>= 1;
    }
    return p >= n ? p : n;
}

/* a replay pass's viewport for a target w x h texels: the exact one while
 * replayFrame records, else the target */
static void setViewport(uint32_t w, uint32_t h)
{
    RhiViewport vp = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    if (s_vpExact) {
        const uint32_t pw = vpExtent(w), ph = vpExtent(h);
        /* on a whole texel (an odd-sized target's centre is half a texel
         * past the viewport's: FrameCB vpo carries it) */
        vp.x = floorf(((float)w - (float)pw) * 0.5f);
        vp.y = floorf(((float)h - (float)ph) * 0.5f);
        vp.w = (float)pw;
        vp.h = (float)ph;
    }
    rhi_cmd_set_viewport(s_cl, &vp);
}

/* the camera's picture of the frame group being made (Replay.picture), and
 * the height of the frame it is drawn on (0: the native height) */
static uint32_t s_framePicture, s_framePictureFrameH;

/* the bound target of the frame group being made when it is a Full pixel
 * crop of the frame (RdTargetRec.ox), else NULL: its texture is narrower
 * than its GS width times its scale (frameGroupOf) */
static const RdTargetRec *s_frameCrop;

RdUniform rd__frame_group_ex(uint32_t targetW, uint32_t targetH, float originX, float originY,
                             float zScale, float regionScale, float scaleX, float scaleY)
{
    IcoFrameCB cb;
    memset(&cb, 0, sizeof(cb));
    rd__fill_camera_cb(&cb, s_hasReplayCam ? &s_replayCam : NULL);
    cb.target[0] = (float)targetW;
    cb.target[1] = (float)targetH;
    cb.target[2] = 1.0f / (float)targetW;
    cb.target[3] = 1.0f / (float)targetH;
    cb.origin[0] = originX;
    cb.origin[1] = originY;
    const RdTargetRec *crop =
        s_frameCrop && s_frameCrop->ox > 0.0f && s_frameCrop->w == targetW ? s_frameCrop : NULL;
    if (crop && !s_vpExact) {
        /* the plain mapping over the texels the target holds: its GS width
         * tw / sx from ox / sx GS pixels in (the exact viewport's form
         * below keeps the frame's centre instead) */
        cb.origin[0] = originX + crop->ox / scaleX;
        cb.target[0] = (float)crop->tw / scaleX;
        cb.target[2] = scaleX / (float)crop->tw;
    }
    /* GS integer coordinates on GPU pixel centres; on a scaled target, on
     * the centre of the block's first texel (0.5 texel = 0.5 / s GS
     * pixels), so integer edges are block edges (expand) */
    cb.origin[2] = scaleX > 1.0f ? 0.5f / scaleX : 0.5f;
    cb.origin[3] = scaleY > 1.0f ? 0.5f / scaleY : 0.5f;
    if (s_frameNoHalf) {
        cb.origin[2] = cb.origin[3] = 0.0f;
    }
    /* The native frame: no position is transformed (a GS X is drawn where
     * the game put it, at every aspect; the mirror is applied to the
     * vertices, mirrorDraw).  frame[0] the region scale (rd__region_scale:
     * exactly 1 at 4:3 and on every target that is not frame-wide; no
     * shader reads it) */
    cb.frame[0] = regionScale;
    /* frame[1], frame[2]: the drawing camera's picture, half-width and
     * half-height in GS pixels about (2048, 2048), which the VU programs'
     * rule for the triangles their region test drops reads
     * (vu_common.hlsli vu_picture_half): a reflection camera's from its
     * draw's command (rd.h RdCamera.pictureW/H), else the full screen's,
     * 256 (gsb_SetVSMatrix's 512-pixel screen) by half the native height
     * (never less than half the frame drawn on: at 4:3 the picture is the
     * whole frame, so a triangle wholly beside it is off the frame) */
    const uint32_t pw = s_framePicture & 255u, ph = (s_framePicture >> 8) & 255u;
    const uint32_t fh = s_framePictureFrameH > g_rd.gsH ? s_framePictureFrameH : g_rd.gsH;
    cb.frame[1] = pw ? (float)pw : 256.0f;
    cb.frame[2] = ph ? (float)ph : (float)(fh >> 1);
    /* the GS Z scale of the bound depth buffer: 2^-32 for the game's PSMZ32
     * so UI Z values above 2^24 keep their order */
    cb.z[0] = zScale;
    cb.z[1] = scaleX; /* the bound target's texels per GS pixel */
    cb.z[2] = scaleY;
    /* the meshes' vertices off the 12.4 grid where the target is finer
     * than it, only under the developer switch ICO_RD_VU_OFFGRID (Enhanced
     * only).  Off the grid, a VU vertex does not land where the
     * GIF path or another mesh puts the same point, and the crack between
     * them showed the clear as a bright dot through the fog; every preset
     * keeps the GS's quantisation. */
    cb.z[3] = rd__vu_off_grid() && g_rd.settings.preset == RD_PRESET_ENHANCED &&
                      (scaleX > 1.0f || scaleY > 1.0f)
                  ? 1.0f
                  : 0.0f;
    cb.misc[0] = (float)g_rd.replayCounter;
    cb.misc[1] = (float)g_rd.settings.preset;
    if (s_vpExact && targetW && targetH) {
        /* the viewport setViewport gives the target: its texture size is
         * rd_core.c rd__target_scale_of's (scaled), the target's texels per
         * GS pixel that over the GS size (the scale itself at an integer
         * scale) */
        uint32_t tw = scaleX == 1.0f ? targetW : (uint32_t)((float)targetW * scaleX + 0.5f);
        const uint32_t th = scaleY == 1.0f ? targetH : (uint32_t)((float)targetH * scaleY + 0.5f);
        float kx = (float)tw / (float)targetW;
        if (crop) {
            /* Full pixel: the texture holds the frame's exact width (tw),
             * centred on the frame's centre (GS 2048, c in gs_px_to_ndc),
             * at the target's own scale; vpo.z the texels of the GS frame
             * left out at its left (common.hlsli gs_plane_offset, fx_sprite) */
            tw = crop->tw;
            kx = scaleX;
            cb.vpo[2] = crop->ox;
        }
        const uint32_t pw = vpExtent(tw ? tw : 1), ph = vpExtent(th ? th : 1);
        cb.vp[0] = 2.0f / (float)pw;
        cb.vp[1] = 2.0f / (float)ph;
        cb.vp[2] = kx;
        cb.vp[3] = (float)th / (float)targetH;
        cb.frame[3] = s_frameNoHalf ? 0.0f : 0.5f;
        cb.misc[2] = (float)tw / (float)pw;
        cb.misc[3] = (float)th / (float)ph;
        /* the target's centre less the viewport's (setViewport: its origin
         * floor((w - P) / 2)): 0, or 0.5 for an odd size */
        cb.vpo[0] = (float)tw * 0.5f - (floorf(((float)tw - (float)pw) * 0.5f) + (float)pw * 0.5f);
        cb.vpo[1] = (float)th * 0.5f - (floorf(((float)th - (float)ph) * 0.5f) + (float)ph * 0.5f);
        if (crop) {
            /* Full pixel: the frame's centre on texel edge floor(tw / 2),
             * the viewport's own (rd__target_scale_of's ox); the
             * fullscreen triangle a texel wider at an odd width so it
             * still covers the last column */
            cb.vpo[0] = 0.0f;
            cb.misc[2] = (float)(tw + (tw & 1u)) / (float)pw;
        }
    }
    return uniformGroup(g_rd.layoutFrame, 0, &cb, sizeof(cb));
}

/* FrameCB of a draw on target tc with XYOFFSET (ox, oy): its GS size,
 * scale and region scale, and its texture's own width where Full pixel
 * crops it (s_frameCrop) */
static RdUniform frameGroupOf(const RdTargetRec *tc, float ox, float oy, float zScale)
{
    s_frameCrop = tc->ox > 0.0f ? tc : NULL;
    const RdUniform u =
        rd__frame_group_ex(tc->w, tc->h, ox, oy, zScale, rd__region_scale(tc), tc->sx, tc->sy);
    s_frameCrop = NULL;
    return u;
}

RdUniform rd__draw_group(const void *drawCB)
{
    return uniformGroup(g_rd.layoutDraw, 1, drawCB, sizeof(IcoDrawCB));
}

RdUniform rd__crt_group(const void *crtCB)
{
    return uniformGroup(g_rd.layoutDraw, 1, crtCB, sizeof(IcoCrtCB));
}

/* texture groups cached by (texture, sampler, DATE snapshot) */
#define RD_TEX_CACHE 512

typedef struct TexEntry {
    uint32_t epoch, tex, sampler, date;
    RhiBindGroup group;
} TexEntry;

static TexEntry s_texCache[RD_TEX_CACHE];

RhiBindGroup rd__tex_group_date(RhiTexture t, RhiSampler s, RhiTexture date)
{
    const uint32_t h = (t.id * 2654435761u) ^ (s.id * 40503u) ^ (date.id * 2246822519u);
    TexEntry *e = &s_texCache[(h >> 7) % RD_TEX_CACHE];
    if (e->epoch == s_bindEpoch && e->tex == t.id && e->sampler == s.id && e->date == date.id) {
        return e->group;
    }
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = t;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = s;
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = date;
    const RhiBindGroup g = bindGroup(g_rd.layoutTex, b, 3);
    if (g.id) {
        e->epoch = s_bindEpoch;
        e->tex = t.id;
        e->sampler = s.id;
        e->date = date.id;
        e->group = g;
    }
    return g;
}

RhiBindGroup rd__tex_group(RhiTexture t, RhiSampler s)
{
    return rd__tex_group_date(t, s, g_rd.dummy);
}

RhiSampler rd__sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t)
{
    int i = (mag ? 1 : 0) | (min ? 2 : 0) | (s ? 4 : 0) | (t ? 8 : 0);
    return g_rd.samplers[i];
}

/* The sampler of a game texture.  With the Enhanced filter
 * on and the texture mipmapped, a linearly minified texture is sampled
 * trilinear (or anisotropic); textures authored nearest stay nearest, and
 * everything is the Original sampler otherwise.  Texture packs: a
 * replacement is drawn as PCSX2 draws a pack, minified linearly and
 * trilinear between its levels even under the Original filter
 * (anisotropic when the option says so); mag stays as TEX1 has it. */
static int texSamplerUpgraded(RdFilter min, int mipmapped, int replacement)
{
    return mipmapped && (replacement || (min == RD_FILTER_LINEAR && g_rd.filterUpgrade));
}

static RhiSampler texSampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t, int mipmapped,
                             int replacement)
{
    if (replacement) {
        min = RD_FILTER_LINEAR;
    }
    int i = (mag ? 1 : 0) | (min ? 2 : 0) | (s ? 4 : 0) | (t ? 8 : 0);
    if (texSamplerUpgraded(min, mipmapped, replacement)) {
        i += RD_SAMPLER_COUNT * (g_rd.filterUpgrade >= RD_FILTER_UPGRADE_ANISOTROPIC ? 2 : 1);
    }
    return g_rd.samplers[i];
}

/* ---------------------------------------------------------------- passes */

static void flushScreenRun(Replay *r);

static void beginPass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did,
                      RhiLoadOp colorLoad, const float clear[4], RhiLoadOp depthLoad,
                      float clearDepth, bool storage);

static bool msaaPass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did);

static void msaaAttach(RdTargetRec *c, RdTargetRec *d, RhiLoadOp colorLoad, RhiLoadOp depthLoad,
                       RhiTexture *colorTex, RhiTexture *depthTex);

static bool depthWantsReadable(const RdTargetRec *c, const RdTargetRec *d);

static void endPass(Replay *r)
{
    flushScreenRun(r); /* the run's draw goes before anything after it */
    if (r->passOpen) {
        rhi_cmd_end_render_pass(s_cl);
        r->passOpen = 0;
        /* a storage pass's target goes back to its attachment state, so a
         * later storage pass on it starts behind a barrier (rd_dst_read.c) */
        RdTargetRec *tc = r->passStorage ? rd__target_rec(r->passColor) : NULL;
        if (tc) {
            rd__transition(s_cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
        }
    }
    r->passStorage = r->passReadable = r->passDepthReadable = 0;
    g_rd.passSamples = 1;
    /* whoever ends a pass is about to record something else (a copy,
     * another pass): a pending clear goes first, unless beginPass took it */
    flushClear();
    flushStencilClear(); /* and a pending shadow reset (never both pending) */
}

bool rd__take_pending_clear(const RdPendingClear *p, uint32_t cid, uint32_t did,
                            RhiLoadOp *colorLoad, RhiLoadOp *depthLoad)
{
    if (!p->target || cid != p->target || *colorLoad != RHI_LOAD_LOAD) {
        return false;
    }
    if (p->depth && (did != p->target || *depthLoad != RHI_LOAD_LOAD)) {
        return false; /* the cleared depth not in this pass, or not loaded */
    }
    *colorLoad = RHI_LOAD_CLEAR;
    if (p->depth) {
        *depthLoad = RHI_LOAD_CLEAR;
    }
    return true;
}

/* A shadow reset no pass took, recorded as a pass of its own: the colour
 * and depth loaded and kept, the stencil cleared to 0 and stored (the
 * depth is live: only the end of a resolve's pass leaves the window, and
 * that pass took or flushed the reset first). */
static void flushStencilClear(void)
{
    if (!s_stencil.depthTex) {
        return;
    }
    const uint32_t depthTex = s_stencil.depthTex;
    s_stencil.w.clearFor = 0;
    s_stencil.colorTex = s_stencil.depthTex = 0;
    RdTargetRec *tc = rd__target_rec(s_stencil.color);
    RdTargetRec *td = rd__target_rec(s_stencil.depth);
    if (!tc || !td || td->depth.id != depthTex) {
        return;
    }
    const bool readable = rd__pass_wants_readable(tc); /* the state of its passes */
    const bool depthRead = depthWantsReadable(tc, td);
    rd__transition(s_cl, tc->color, &tc->colorState,
                   readable ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_RENDER_TARGET);
    rd__transition(s_cl, td->depth, &td->depthState,
                   depthRead ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.color[0].readable = readable;
    p.colorCount = 1;
    p.depth.texture = td->depth;
    p.depth.readable = depthRead;
    p.depth.depthLoad = RHI_LOAD_LOAD;
    p.depth.stencilLoad = RHI_LOAD_CLEAR;
    p.depth.clearStencil = 0;
    p.depth.stencilStore = g_rd.depthCopy || rd__stencil_live(&s_stencil.w, depthTex)
                               ? RHI_STORE_STORE
                               : RHI_STORE_DONT_CARE;
    p.width = tc->tw;
    p.height = tc->th;
    rd__perf_pass(s_cl, RD_PERF_PASS_STENCIL, s_stencil.color, tc->tw, tc->th, -1);
    rhi_cmd_begin_render_pass(s_cl, &p);
    rhi_cmd_end_render_pass(s_cl);
}

/* the pending clear recorded as a pass of its own */
static void flushClear(void)
{
    if (!s_pend.p.target) {
        return;
    }
    const RdPendingClear p = s_pend.p;
    Replay *r = s_pend.r;
    s_pend.p.target = 0;
    s_pend.colorTex = s_pend.depthTex = 0;
    RdTargetRec *t = rd__target_rec(p.target);
    if (!t || !r) {
        return;
    }
    beginPass(r, t, p.depth ? t : NULL, p.target, p.depth ? p.target : 0, RHI_LOAD_CLEAR, p.color,
              RHI_LOAD_CLEAR, p.clearDepth, false);
    endPass(r);
}

/* the exact blend's attachment read: every pass on an RGBA8 target made
 * for it is begun readable (outside the MSAA epoch) */
static bool exactReadable(const RdTargetRec *c)
{
    return g_rd.dstMode == RD_BLEND_EXACT_ATTACHMENT && (c->dstUsage & RD_DST_USAGE_ATTACHMENT) &&
           c->format == RHI_FMT_RGBA8_UNORM;
}

/* readable: the exact blend's attachment read, or DATE through the stencil
 * bit (RdProfile.dateStencil: its setup reads the alpha, rd__replay_date_take),
 * on an RGBA8 target made for it (rd_core.c: the scene class) */
bool rd__pass_wants_readable(const RdTargetRec *c)
{
    return exactReadable(c) ||
           (g_rd.profile.dateStencil && (c->dstUsage & RD_DST_USAGE_LOCAL_DATE) &&
            c->format == RHI_FMT_RGBA8_UNORM);
}

/* The depth begun readable (RhiDepthAttachment.readable, the texture in
 * RHI_STATE_RENDER_TARGET_READ): every pass on a target with its own depth,
 * the depth made for the fog's local read (RD_DST_USAGE_LOCAL_DEPTH), while
 * the fog's path is RD_FOG_LOCAL: the pass the fog lands in is not known
 * when a pass begins */
static bool depthWantsReadable(const RdTargetRec *c, const RdTargetRec *d)
{
    return d && c == d && g_rd.fogPath == RD_FOG_LOCAL && g_rd.profile.fogLocal &&
           (d->dstUsage & RD_DST_USAGE_LOCAL_DEPTH) != 0;
}

static void beginPass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did,
                      RhiLoadOp colorLoad, const float clear[4], RhiLoadOp depthLoad,
                      float clearDepth, bool storage)
{
    /* the MSAA epoch: whether this pass draws into SCENE_MS (msaaPass may
     * close the epoch instead, recording what is open first).  A storage
     * pass (the exact blend, rd_dst_read.c) is never in it: its draws come
     * from a single-sampled pass */
    bool ms = !storage && s_ms.want > 1u && msaaPass(r, c, d, cid, did);
    const bool wantReadable = !storage && rd__pass_wants_readable(c);
    /* the pending clear as this pass's load op when it can take it
     * (the same clear values, the same render area: the whole target); a
     * storage pass has no colour attachment and the exact blend's readable
     * one does not read the attachment a load op cleared (rd_dst_read.c):
     * their clear is a pass of its own (endPass below records it).  DATE's
     * stencil setup reads the attachment after a barrier that holds the
     * load op's write (vk_cmd.c rhi_cmd_local_read_barrier), so its
     * readable passes take the clear */
    float pendColor[4];
    if (s_pend.p.target && !storage && !(wantReadable && exactReadable(c)) &&
        rd__take_pending_clear(&s_pend.p, cid, d ? did : 0, &colorLoad, &depthLoad)) {
        memcpy(pendColor, s_pend.p.color, sizeof(pendColor));
        clear = pendColor;
        if (s_pend.p.depth) {
            clearDepth = s_pend.p.clearDepth;
        }
        s_pend.p.target = 0;
        s_pend.colorTex = s_pend.depthTex = 0;
    }
    /* the stencil: a pending shadow reset of this depth as its load op when
     * the pass covers the whole depth (the clear the reset's own pass made,
     * before any draw of this pass), else loaded and stored inside the
     * shadow window only (RdStencilWindow) */
    RhiLoadOp stencilLoad = depthLoad;
    RhiStoreOp stencilStore = RHI_STORE_STORE;
    /* SCENE_MS's stencil is not the shadow window's (the shadow commands
     * close the epoch): a pass into it neither takes nor keeps a reset */
    if (!ms && d &&
        rd__stencil_ops_packed(&s_stencil.w, d->depth.id, c->tw == d->tw && c->th == d->th,
                               depthLoad, &stencilLoad, &stencilStore)) {
        s_stencil.colorTex = s_stencil.depthTex = 0;
    }
    endPass(r);
    if (ms && s_ms.ended) {
        /* what endPass recorded closed the epoch: single-sampled */
        ms = false;
        if (d && rd__stencil_ops_packed(&s_stencil.w, d->depth.id, c->tw == d->tw && c->th == d->th,
                                        depthLoad, &stencilLoad, &stencilStore)) {
            s_stencil.colorTex = s_stencil.depthTex = 0;
        }
    }
    RhiTexture colorTex = c->color, depthTex = d ? d->depth : (RhiTexture){0};
    const bool readable = wantReadable && !ms;
    const bool depthRead = !ms && !storage && depthWantsReadable(c, d);
    if (ms) {
        msaaAttach(c, d, colorLoad, depthLoad, &colorTex, &depthTex);
    } else {
        rd__transition(s_cl, c->color, &c->colorState,
                       storage    ? RHI_STATE_STORAGE
                       : readable ? RHI_STATE_RENDER_TARGET_READ
                                  : RHI_STATE_RENDER_TARGET);
        if (d) {
            rd__transition(s_cl, d->depth, &d->depthState,
                           depthRead ? RHI_STATE_RENDER_TARGET_READ : RHI_STATE_DEPTH_WRITE);
        }
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = colorTex;
    p.color[0].load = colorLoad;
    p.color[0].readable = readable;
    if (clear) {
        memcpy(p.color[0].clear, clear, sizeof(p.color[0].clear));
    }
    /* a storage pass: the target is the shaders' storage texture, no
     * attachment (its contents are kept: they are the texture's) */
    p.colorCount = storage ? 0u : 1u;
    if (d) {
        p.depth.texture = depthTex;
        p.depth.depthLoad = depthLoad;
        p.depth.stencilLoad = stencilLoad;
        p.depth.stencilStore = stencilStore;
        p.depth.clearDepth = clearDepth;
        p.depth.clearStencil = 0;
        p.depth.readable = depthRead;
    }
    p.width = c->tw;
    p.height = c->th;
    rd__perf_pass(s_cl, RD_PERF_PASS_RENDER, cid, c->tw, c->th, r->perfPost);
    rhi_cmd_begin_render_pass(s_cl, &p);
    rhi_cmd_checkpoint(s_cl, passPush(RD_PASSK_PASS, cid, c->tw, c->th, d ? did : 0, d ? d->tw : 0,
                                      d ? d->th : 0, storage, r->perfPost));
    setViewport(c->tw, c->th);
    if (storage) {
        g_rdPerf.storagePasses++;
    }
    if (r->perfPost == RD_PERF_POST_GLOW) {
        g_rdPerf.glowPasses++;
    }
    r->passOpen = 1;
    r->passColor = cid;
    r->passDepth = did;
    r->passSerial++;
    r->passStorage = storage;
    r->passReadable = readable;
    r->passDepthReadable = depthRead;
    g_rd.passSamples = ms ? (uint8_t)s_ms.samples : 1;
}

/* ------------------------------------------------------------ the MSAA epoch
 * RdSettings.antiAlias MSAA2/4/8 (rd__aa_mode: never under the Original
 * preset, only for a count rd_aa_supported_mask lists).  SCENE has a
 * multisampled twin, SCENE_MS (s_ms: RGBA8 and D32F_S8 at SCENE's texture
 * size, N samples).  Per replay:
 *  - the epoch opens at the first pass whose colour is SCENE and whose depth
 *    is SCENE's or none (beginPass, msaaPass): that pass and every such pass
 *    after it draw into SCENE_MS, their pipelines with N samples
 *    (g_rd.passSamples, rd__get_pipeline).  A pass that loads an
 *    attachment the epoch has not drawn yet first spreads SCENE's own
 *    texels over the samples (msaaUpsample: the colour by blit_ps, the depth
 *    by depth_upsample_ps); a cleared one starts from the clear;
 *  - it closes (msaaEnd) at the first command that reads or writes SCENE's
 *    single-sampled colour or depth outside such a pass: every such access
 *    goes through rd__transition, which closes the epoch first (the hook;
 *    SCENE's textures are put in their attachment states when the epoch
 *    opens, so a read always transitions): a texture read of SCENE (a draw
 *    sampling it, a block or feedback copy, the reduction), a DATE snapshot,
 *    the fog's depth read (unless the fog runs per sample, below), a copy
 *    or exact blend (blend_int: its ping-pong never runs on SCENE_MS), the
 *    COLCLAMP 0 wrap's snapshot, a pass binding SCENE's depth with another
 *    colour (the shadow count) or SCENE with another depth, and the end of
 *    the replay (dumps and readbacks then read the resolved SCENE);
 *  - closing resolves: the colour by rhi_cmd_resolve_color (the mean of the
 *    samples) into SCENE's colour, the depth by depth_resolve_ps (the
 *    smallest sample: the farthest under GEQUAL) into SCENE's depth, each
 *    only when the epoch drew it.  Everything after draws single-sampled;
 *    the epoch does not open again in the replay.  The smallest because the
 *    sample patterns are centred on the pixel centre: on a plane it is at
 *    or below the centre's depth, so a surface the game draws again after
 *    the close with GEQUAL against SCENE's depth (the flare mask's and the
 *    aura's re-draws of the glowing meshes) passes on every
 *    pixel it passed at 1x; the largest sample lies above the centre on
 *    any slope and failed nearly all of them.  The cost is on silhouettes:
 *    a pixel shared by a near and a far surface resolves to the far one,
 *    so a pass after the close treats it as the far surface (the largest
 *    made it the near one: the same one-pixel error on the other side);
 *    inside a surface every pass after the close sees the 1x depth or
 *    below it by under a pixel's slope;
 *  - the fog inside the epoch, with sample-rate shading and the depth drawn
 *    in it, runs per sample (fog_lut_ms_ps reading SCENE_MS's depth at
 *    SV_SampleIndex) and keeps the epoch open; otherwise its depth read
 *    closes it and it fogs the resolved picture;
 *  - integer-aligned sprites drawn inside it cover whole pixels (doScreen:
 *    msaaSpriteExpand), so clears and backdrops keep full coverage; the
 *    per-sample fog's sprite too (doFog);
 *  - the stencil of SCENE_MS is not the shadow window's: the shadow
 *    commands draw on SCENE's own depth after the epoch closed. */
static void msaaFree(void)
{
    if (s_ms.color.id) {
        rhi_destroy_texture(s_ms.color);
    }
    if (s_ms.depth.id) {
        rhi_destroy_texture(s_ms.depth);
    }
    s_ms.color = s_ms.depth = (RhiTexture){0};
    s_ms.w = s_ms.h = s_ms.samples = 0;
    s_ms.active = s_ms.ended = false;
}

void rd__msaa_free(void)
{
    msaaFree();
}

/* At the start of a replay: the count in force and SCENE_MS at SCENE's
 * size (made or remade here); the epoch closed. */
static void msaaFrameBegin(Replay *r)
{
    s_ms.active = s_ms.ended = s_ms.internal = false;
    s_ms.colorLive = s_ms.depthLive = false;
    s_ms.r = r;
    s_ms.want = 1;
    s_ms.sceneColor = s_ms.sceneDepth = 0;
    const uint32_t n = rd_aa_samples(rd__aa_mode());
    RdTargetRec *ts = rd__target_rec(RD_TARGET_SCENE + 1);
    if (n <= 1u || !ts || !ts->color.id || !ts->withDepth || !ts->depth.id ||
        ts->format != RHI_FMT_RGBA8_UNORM) {
        if (s_ms.samples) {
            msaaFree(); /* anti-aliasing turned off: the pair goes */
        }
        return;
    }
    if (s_ms.samples != n || s_ms.w != ts->tw || s_ms.h != ts->th) {
        msaaFree();
        s_ms.color = rhi_create_texture(&(RhiTextureDesc){ts->tw, ts->th, 1, RHI_FMT_RGBA8_UNORM,
                                                          RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                          "rd scene ms", n});
        s_ms.depth = rhi_create_texture(&(RhiTextureDesc){ts->tw, ts->th, 1, RHI_FMT_D32F_S8,
                                                          RHI_TEX_DEPTH_STENCIL | RHI_TEX_SAMPLED,
                                                          "rd scene ms depth", n});
        s_ms.colorState = s_ms.depthState = RHI_STATE_UNDEFINED;
        if (!s_ms.color.id || !s_ms.depth.id) {
            msaaFree();
            if (!s_ms.failLogged) {
                s_ms.failLogged = true;
                rd__log("msaa: the %u-sample scene (%ux%u) could not be made: drawn without", n,
                        ts->tw, ts->th);
            }
            rd__note_scene_pressure(); /* the scale comes down at the next frame */
            return;
        }
        s_ms.w = ts->tw;
        s_ms.h = ts->th;
        s_ms.samples = n;
        rd__log("msaa: %u samples, scene %ux%u", n, ts->tw, ts->th);
    }
    s_ms.want = n;
    s_ms.sceneColor = ts->color.id;
    s_ms.sceneDepth = ts->depth.id;
    g_rdPerf.msaaSamples = n;
}

/* A fullscreen draw into the open pass of SCENE_MS: blit_vs with fs, src
 * bound at t1 (aspect); t2, which none of these shaders reads, holds the
 * dummy as in rd__tex_group, so every slot of the layout has its texture */
static void msaaFullscreen(RdFsId fs, RhiFormat colorFmt, RhiTexture src, RhiViewAspect aspect,
                           uint32_t w, uint32_t h)
{
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, fs, colorFmt);
    if (colorFmt == RHI_FMT_UNKNOWN) {
        k.depthFmt = RHI_FMT_D32F_S8;
        k.gs.ztst = RD_ZTST_ALWAYS;
        k.gs.zwrite = RD_ZWRITE_ON;
    }
    const RhiPipeline pipe = rd__get_pipeline(&k);
    if (!pipe.id) {
        return;
    }
    const RhiRect all = {0, 0, w, h};
    rhi_cmd_set_scissor(s_cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    for (int i = 0; i < 4; i++) {
        cb.col[i] = 0x80; /* blit_ps: modulate by 1.0 */
    }
    cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
    cb.uvRect[2] = (float)w;
    cb.uvRect[3] = (float)h;
    cb.tex[0] = (float)w;
    cb.tex[1] = (float)h;
    cb.tex[2] = 1.0f / (float)w;
    cb.tex[3] = 1.0f / (float)h;
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = src;
    b[0].aspect = aspect;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = g_rd.dummy;
    const RhiBindGroup g2 = bindGroup(g_rd.layoutTex, b, 3);
    if (!g2.id) {
        return;
    }
    rd__set_pipeline(s_cl, pipe, &k);
    rd__bind_uniform(s_cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
    rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(s_cl, 2, g2);
    drawCmd(s_cl, 3, 0, 1);
}

/* SCENE's colour (depth 0) or depth (1) spread over SCENE_MS's samples, in
 * a pass of its own (no pass is open) */
static void msaaUpsample(RdTargetRec *ts, int depth)
{
    s_ms.internal = true;
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    if (!depth) {
        rd__transition(s_cl, ts->color, &ts->colorState, RHI_STATE_SHADER_READ);
        rd__transition(s_cl, s_ms.color, &s_ms.colorState, RHI_STATE_RENDER_TARGET);
        p.color[0].texture = s_ms.color;
        p.color[0].load = RHI_LOAD_DONT_CARE;
        p.colorCount = 1;
    } else {
        rd__transition(s_cl, ts->depth, &ts->depthState, RHI_STATE_SHADER_READ);
        rd__transition(s_cl, s_ms.depth, &s_ms.depthState, RHI_STATE_DEPTH_WRITE);
        p.depth.texture = s_ms.depth;
        p.depth.depthLoad = RHI_LOAD_DONT_CARE;
        p.depth.stencilLoad = RHI_LOAD_CLEAR;
        p.depth.clearStencil = 0;
    }
    p.width = s_ms.w;
    p.height = s_ms.h;
    rd__perf_pass(s_cl, RD_PERF_PASS_MSAA, RD_TARGET_SCENE + 1u, s_ms.w, s_ms.h,
                  s_ms.r ? s_ms.r->perfPost : -1);
    rhi_cmd_begin_render_pass(s_cl, &p);
    setViewport(s_ms.w, s_ms.h);
    g_rd.passSamples = (uint8_t)s_ms.samples;
    if (!depth) {
        msaaFullscreen(RD_FS_BLIT, RHI_FMT_RGBA8_UNORM, ts->color, RHI_ASPECT_COLOR, s_ms.w,
                       s_ms.h);
    } else {
        msaaFullscreen(RD_FS_DEPTH_UPSAMPLE, RHI_FMT_UNKNOWN, ts->depth, RHI_ASPECT_DEPTH, s_ms.w,
                       s_ms.h);
    }
    g_rd.passSamples = 1;
    rhi_cmd_end_render_pass(s_cl);
    /* back in the attachment states, so a later read of SCENE transitions
     * (and closes the epoch through the hook) */
    rd__transition(s_cl, depth ? ts->depth : ts->color, depth ? &ts->depthState : &ts->colorState,
                   depth ? RHI_STATE_DEPTH_WRITE : RHI_STATE_RENDER_TARGET);
    s_ms.internal = false;
}

/* Whether the pass beginPass is about to open (colour c, depth d) draws into
 * SCENE_MS, opening the epoch if need be; a pass on SCENE's colour or depth
 * that cannot closes it */
static bool msaaPass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did)
{
    if (s_ms.want <= 1u || s_ms.ended || s_ms.internal) {
        return false;
    }
    const uint32_t scene = RD_TARGET_SCENE + 1u;
    const bool cScene = cid == scene && c->color.id == s_ms.sceneColor;
    const bool dScene = d && did == scene && d->depth.id == s_ms.sceneDepth;
    if (cScene && (!d || dScene)) {
        return true; /* opened (msaaAttach) once the pass before has ended */
    }
    if (s_ms.active && (cScene || (d && did == scene))) {
        msaaEnd(r);
    }
    return false;
}

/* Closes the epoch: the open pass (and a clear pending on SCENE) recorded
 * into SCENE_MS, then the colour and depth the epoch drew resolved into
 * SCENE's own textures */
static void msaaEnd(struct Replay *r)
{
    if (!s_ms.active || s_ms.internal) {
        return;
    }
    endPass(r); /* the run, the pass, a pending clear: still into SCENE_MS */
    s_ms.active = false;
    s_ms.ended = true;
    RdTargetRec *ts = rd__target_rec(RD_TARGET_SCENE + 1);
    if (!ts || ts->color.id != s_ms.sceneColor || ts->depth.id != s_ms.sceneDepth) {
        return;
    }
    s_ms.internal = true;
    rd__perf_pass(s_cl, RD_PERF_PASS_MSAA, RD_TARGET_SCENE + 1u, ts->tw, ts->th, RD_PERF_POST_MSAA);
    rd__perf_post(s_cl, RD_PERF_POST_MSAA); /* the resolves' GPU time */
    if (s_ms.colorLive) {
        rd__transition(s_cl, s_ms.color, &s_ms.colorState, RHI_STATE_COPY_SRC);
        rd__transition(s_cl, ts->color, &ts->colorState, RHI_STATE_COPY_DST);
        rhi_cmd_resolve_color(s_cl, s_ms.color, ts->color);
    }
    if (s_ms.depthLive) {
        rd__transition(s_cl, s_ms.depth, &s_ms.depthState, RHI_STATE_SHADER_READ);
        rd__transition(s_cl, ts->depth, &ts->depthState, RHI_STATE_DEPTH_WRITE);
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.depth.texture = ts->depth;
        p.depth.depthLoad = RHI_LOAD_LOAD; /* every texel is written; the stencil kept */
        p.depth.stencilLoad = RHI_LOAD_LOAD;
        p.width = ts->tw;
        p.height = ts->th;
        rhi_cmd_begin_render_pass(s_cl, &p);
        setViewport(ts->tw, ts->th);
        msaaFullscreen(RD_FS_DEPTH_RESOLVE, RHI_FMT_UNKNOWN, s_ms.depth, RHI_ASPECT_DEPTH, ts->tw,
                       ts->th);
        rhi_cmd_end_render_pass(s_cl);
    }
    rd__perf_post(s_cl, r ? r->perfPost : -1);
    s_ms.internal = false;
    g_rdPerf.msaaResolves++;
}

/* beginPass's attachments for a pass into SCENE_MS: the pair in its
 * attachment states, an attachment the epoch has not drawn and the pass
 * loads first spread from SCENE (msaaUpsample) */
static void msaaAttach(RdTargetRec *c, RdTargetRec *d, RhiLoadOp colorLoad, RhiLoadOp depthLoad,
                       RhiTexture *colorTex, RhiTexture *depthTex)
{
    if (!s_ms.active) {
        /* the epoch opens: SCENE's textures in their attachment states, so
         * any later read transitions them, which closes it (rd__transition) */
        s_ms.internal = true;
        rd__transition(s_cl, c->color, &c->colorState, RHI_STATE_RENDER_TARGET);
        RdTargetRec *ts = rd__target_rec(RD_TARGET_SCENE + 1);
        if (ts && ts->depth.id) {
            rd__transition(s_cl, ts->depth, &ts->depthState, RHI_STATE_DEPTH_WRITE);
        }
        s_ms.internal = false;
        s_ms.active = true;
        s_ms.colorLive = s_ms.depthLive = false;
    }
    if (!s_ms.colorLive && colorLoad == RHI_LOAD_LOAD) {
        msaaUpsample(c, 0);
    }
    s_ms.colorLive = true;
    rd__transition(s_cl, s_ms.color, &s_ms.colorState, RHI_STATE_RENDER_TARGET);
    *colorTex = s_ms.color;
    if (d) {
        if (!s_ms.depthLive && depthLoad == RHI_LOAD_LOAD) {
            msaaUpsample(d, 1);
        }
        s_ms.depthLive = true;
        rd__transition(s_cl, s_ms.depth, &s_ms.depthState, RHI_STATE_DEPTH_WRITE);
        *depthTex = s_ms.depth;
    }
}

/* doScreen: inside the epoch, a sprite command whose corners all lie on
 * whole GS pixels (after the scaled target's snapping) would leave the
 * outer half of its edge pixels' samples uncovered (its edges lie on pixel
 * centres: the half-pixel rule).  Its quads are drawn half a texel up and
 * left instead, with the origin's half texel dropped from FrameCB
 * (msaaFrame), so the edges lie on pixel boundaries and every sample of
 * the pixels the single-sampled draw covers is covered; the UVs move with
 * them, so each pixel centre keeps its attribute.  False (nothing changed)
 * for any other command. */
static bool msaaSpriteExpand(const Replay *r, const RdTargetRec *tc, uint8_t prim,
                             IcoSpriteVertex *o, uint32_t n)
{
    if (g_rd.passSamples <= 1 || prim != RD_PRIM_SPRITES || r->mirror || n == 0 || n % 6 != 0) {
        return false;
    }
    for (uint32_t i = 0; i < n; i++) {
        if ((o[i].x & 15u) != 0u || (o[i].y & 15u) != 0u) {
            return false;
        }
    }
    const float hx = 0.5f / (tc->sx > 0.0f ? tc->sx : 1.0f); /* half a texel, in GS pixels */
    const float hy = 0.5f / (tc->sy > 0.0f ? tc->sy : 1.0f);
    for (uint32_t i = 0; i < n; i += 6) {
        IcoSpriteVertex *q = &o[i]; /* q0 q1 q2 q2 q1 q3 (expand) */
        const float wx = (float)((int32_t)q[1].x - (int32_t)q[0].x) * (1.0f / 16.0f);
        const float wy = (float)((int32_t)q[2].y - (int32_t)q[0].y) * (1.0f / 16.0f);
        const float du = wx != 0.0f ? (q[1].u - q[0].u) / wx * hx : 0.0f;
        const float dv = wy != 0.0f ? (q[2].v - q[0].v) / wy * hy : 0.0f;
        for (int k = 0; k < 6; k++) {
            q[k].u -= du;
            q[k].v -= dv;
        }
    }
    return true;
}

/* ---------------------------------------------------------------- actions */

/* A clear of target t (id), its colour col and with depth its depth at GS
 * Z z.  It is not recorded yet: the next pass on the target takes it as its
 * load op (beginPass), else it is recorded as its own pass before anything
 * else touches the target (endPass, rd__transition).  The pass open now and
 * an earlier pending clear go first. */
static void pendClear(Replay *r, uint32_t id, RdTargetRec *t, const float col[4], int depth,
                      uint32_t z)
{
    r->writeSerial++;
    endPass(r);
    /* inside the MSAA epoch a clear of SCENE goes to SCENE_MS (the pass
     * that takes it, beginPass): SCENE's own textures are not touched */
    if (!(s_ms.active && t->color.id == s_ms.sceneColor)) {
        /* the state of the pass that records it (beginPass): one transition */
        rd__transition(s_cl, t->color, &t->colorState,
                       rd__pass_wants_readable(t) ? RHI_STATE_RENDER_TARGET_READ
                                                  : RHI_STATE_RENDER_TARGET);
        if (depth) {
            rd__transition(s_cl, t->depth, &t->depthState,
                           depthWantsReadable(t, t) ? RHI_STATE_RENDER_TARGET_READ
                                                    : RHI_STATE_DEPTH_WRITE);
        }
    }
    s_pend.r = r;
    s_pend.p.target = id;
    s_pend.p.depth = (uint8_t)(depth != 0);
    memcpy(s_pend.p.color, col, sizeof(s_pend.p.color));
    s_pend.p.clearDepth = rd__gs_depth(z, rd__target_z_scale(id));
    s_pend.colorTex = t->color.id;
    s_pend.depthTex = depth ? t->depth.id : 0;
}

static void doClear(Replay *r, const RdCmd *c)
{
    RdTargetRec *t = rd__target_rec(c->u[0]);
    if (!t) {
        return;
    }
    float col[4];
    for (int i = 0; i < 4; i++) {
        col[i] = t->format == RHI_FMT_RGBA8_UINT ? (float)c->b[i] : (float)c->b[i] / 255.0f;
    }
    pendClear(r, c->u[0], t, col, c->b[4] && t->withDepth, c->u[1]);
}

/* Built in a local and stored whole, so writing straight into
 * the upload ring never reads it back (the ring may be write-combined
 * device memory, where a read is an uncached bus transaction). */
static void convVtx(const RdScreenVtx *s, int uvFixed, float tw, float th, const float uvOff[2],
                    IcoSpriteVertex *out)
{
    IcoSpriteVertex v, *o = &v;
    int32_t x = s->x < 0 ? 0 : (s->x > 0xFFFF ? 0xFFFF : s->x);
    int32_t y = s->y < 0 ? 0 : (s->y > 0xFFFF ? 0xFFFF : s->y);
    o->x = (uint16_t)x;
    o->y = (uint16_t)y;
    o->z = s->z;
    memcpy(o->rgba, s->rgba, 4);
    if (uvFixed) {
        o->u = s->s * (1.0f / 16.0f);
        o->v = s->t * (1.0f / 16.0f);
    } else {
        /* STQ: the per-vertex divide; a textured triangle command with Q != 1
         * is drawn by the STQ shaders instead (doScreen), which divide per
         * pixel from the same S, T and Q */
        float q = s->q != 0.0f ? s->q : 1.0f;
        if (q != 1.0f) {
            rd__log_once(RD_ONCE_STQ, "screen prim with Q != 1 (divided per pixel when it is a "
                                      "textured triangle command, else per vertex)");
        }
        o->u = s->s / q * tw;
        o->v = s->t / q * th;
    }
    o->u += uvOff[0] * tw;
    o->v += uvOff[1] * th;
    *out = v;
}

/* Screen prims are expanded here, flipped for the mirror if
 * need be, and then copied into the ring in one go (expand reads what it
 * wrote: flat colours, the mirror's gradients) */
static IcoSpriteVertex *s_vx;

static uint32_t s_vxCap;

static IcoSpriteVertex *vxScratch(uint64_t n)
{
    if (n > s_vxCap) {
        const uint32_t cap = (uint32_t)(n < 4096 ? 4096 : n * 2);
        IcoSpriteVertex *p = realloc(s_vx, (size_t)cap * sizeof(*p));
        if (!p) {
            return NULL;
        }
        s_vx = p;
        s_vxCap = cap;
    }
    return s_vx;
}

/* Sprites on a scaled target.  The rasteriser puts GS
 * integer coordinates on the left/top edge of their s x s texel block
 * (FrameCB g_origin.zw = 0.5 / s).  A sprite's corners are snapped up to
 * whole GS pixels first (the letterbox's bars sit a quarter pixel off), so
 * it covers exactly the blocks of the pixels the GS covers, and the UVs are
 * moved by k = (s - 1) / (2s) GS pixels so that a texel's sample is taken
 * where its own centre lies in the GS pixel (the block's centre is the GS
 * pixel centre): nearest-sampled sprites then fill whole blocks with the
 * texel the GS samples, and bilinear ones interpolate between them.
 * Neither happens at scale 1 (Original). */
static float s_uvShiftX, s_uvShiftY;

/* one axis: the corners snapped up to whole GS pixels (the GS covers the
 * pixels whose integer coordinates lie in [min, max), so ceil keeps exactly
 * those), the UVs moved along, then the sample shift */
static void snapAxis(uint16_t *pa, uint16_t *pb, float *ua, float *ub, float shift)
{
    const int32_t a = *pa, b = *pb;
    if (a == b) {
        return;
    }
    const float dudp = (*ub - *ua) / ((float)(b - a) * (1.0f / 16.0f));
    const int32_t ca = (a + 15) & ~15, cb = (b + 15) & ~15;
    *ua += (float)(ca - a) * (1.0f / 16.0f) * dudp - shift * dudp;
    *ub += (float)(cb - b) * (1.0f / 16.0f) * dudp - shift * dudp;
    *pa = (uint16_t)(ca > 0xFFFF ? 0xFFF0 : ca);
    *pb = (uint16_t)(cb > 0xFFFF ? 0xFFF0 : cb);
}

static void spriteUvShift(IcoSpriteVertex *a, IcoSpriteVertex *b)
{
    if (s_uvShiftX != 0.0f) {
        snapAxis(&a->x, &b->x, &a->u, &b->u, s_uvShiftX);
    }
    if (s_uvShiftY != 0.0f) {
        snapAxis(&a->y, &b->y, &a->v, &b->v, s_uvShiftY);
    }
}

static void setUvShift(float sx, float sy)
{
    s_uvShiftX = sx > 1.0f ? (sx - 1.0f) / (2.0f * sx) : 0.0f;
    s_uvShiftY = sy > 1.0f ? (sy - 1.0f) / (2.0f * sy) : 0.0f;
}

/* The mirror mode's UI flip.  The presenter flips the whole picture
 * (rd_present.c step 2), so an RD_SPACE_UI prim drawn into SCENE or
 * DISPLAY (the targets the present shows) is flipped here about the
 * target's centre and reads normally after it.  Everything else (WORLD
 * and FULLSCREEN prims, the meshes, the posts) is drawn as recorded and
 * flips with the present.
 *
 * Exact at scale 1: a pixel p of a target w pixels wide is shown at
 * w - 1 - p.  The GS covers pixel p when x0 <= p < x1 and samples it at p;
 * reflected, pixel q = w - 1 - p would need w - 1 - x1 < q <= w - 1 - x0,
 * the open and closed ends swapped.  On the GS's 1/16 grid that is
 * w - 1 - x1 + 1/16 <= q < w - 1 - x0 + 1/16, so a triangle's x is
 * reflected as X' = C - 1 + 1/16 - X about C = 2 (ox + w / 2), which covers
 * exactly the mirrored pixels for any 12.4 edge, and the attributes taken
 * at q are then the original's at p + 1/16: each triangle's UVs are moved
 * back by a sixteenth of a pixel's worth of their x gradient (sprites and
 * quads of the layout and the font: exact UVs), and then on by
 * kMirrorUvBias where they have one, so a sample on a texel's edge stays in
 * the texel the GS takes.  Colours and Z are not
 * moved (1/16 of a pixel's step of a gradient).  Points are drawn as
 * one-pixel quads, so they follow the triangles.  Lines are reflected about
 * the pixel centre, X' = C - 1 - X.  On a scaled target "1" is one texel,
 * 1 / (sx wide) GS pixels.  The scissor is mirrored with them
 * (gsRectToTexels). */
static int mirrorUi(const Replay *r, uint8_t space)
{
    return space == RD_SPACE_UI && rd__mirror_on() &&
           (r->st.color == (uint32_t)RD_TARGET_SCENE + 1u ||
            r->st.color == (uint32_t)RD_TARGET_DISPLAY + 1u);
}

static uint16_t mirrorX(int32_t c16, float x)
{
    const float m = (float)c16 - x;
    const int32_t i = (int32_t)(m < 0.0f ? m - 0.5f : m + 0.5f);
    return (uint16_t)(i < 0 ? 0 : (i > 0xFFFF ? 0xFFFF : i));
}

/* 1/512 texel: above the float error of an interpolated UV, under the
 * GS's own steps (1/16 texel at a vertex), and not lost to a GPU's 8-bit
 * subtexel nearest (it rounds onto the edge, which is the GS's texel) */
static const float kMirrorUvBias = 1.0f / 512.0f;

static void mirrorVerts(IcoSpriteVertex *o, uint32_t n, uint8_t topo, int32_t c16, float texel16)
{
    if (topo == RD_PRIM_LINES) {
        for (uint32_t i = 0; i < n; i++) {
            o[i].x = mirrorX(c16, (float)o[i].x + texel16);
        }
        return;
    }
    for (uint32_t i = 0; i + 2 < n; i += 3) {
        IcoSpriteVertex *t = &o[i];
        const float x0 = (float)t[0].x, y0 = (float)t[0].y;
        const float ax = (float)t[1].x - x0, ay = (float)t[1].y - y0;
        const float bx = (float)t[2].x - x0, by = (float)t[2].y - y0;
        const float det = ax * by - bx * ay;
        float dudx = 0.0f, dvdx = 0.0f; /* per 1/16 GS pixel */
        if (det != 0.0f) {
            dudx = ((t[1].u - t[0].u) * by - (t[2].u - t[0].u) * ay) / det;
            dvdx = ((t[1].v - t[0].v) * by - (t[2].v - t[0].v) * ay) / det;
        }
        /* the mirrored samples run against the gradient, so a GS sample on
         * a texel's left edge (a 2x sprite's u = 0.5 + 0.5 k) can come out
         * a hair under it where the target's NDC is not exact (688 wide)
         * and the nearest texel is the one before: kMirrorUvBias moves
         * the sample that much into the texel the GS takes (floor) */
        const float bu = dudx != 0.0f ? kMirrorUvBias : 0.0f;
        const float bv = dvdx != 0.0f ? kMirrorUvBias : 0.0f;
        for (int k = 0; k < 3; k++) {
            t[k].u += bu - dudx;
            t[k].v += bv - dvdx;
            t[k].x = mirrorX(c16, (float)t[k].x + texel16 - 1.0f);
        }
    }
}

/* the reflection's C (12.4) for the bound target: 2 (ox + w / 2) GS pixels,
 * ox = 2048 - gsW / 2 as bindDraw puts the origin */
static void mirrorDraw(Replay *r, const RdTargetRec *tc, IcoSpriteVertex *o, uint32_t n,
                       uint8_t topo)
{
    const int32_t ox = 2048 - (int32_t)(r->st.gsW >> 1);
    const int32_t c16 = 32 * ox + 16 * (int32_t)tc->w;
    const float texels = tc->sx;
    mirrorVerts(o, n, topo, c16, 16.0f / (texels > 0.0f ? texels : 1.0f));
    r->mirror = 1;
}

/* Expands a screen-prim command into a triangle or line list.  Returns the
 * vertex count; *topo is RD_PRIM_TRIANGLES or RD_PRIM_LINES. */
static uint32_t expand(const RdScreenVtx *v, uint32_t n, uint8_t prim, int uvFixed, float tw,
                       float th, const float uvOff[2], IcoSpriteVertex *o, uint8_t *topo,
                       int gouraud)
{
    uint32_t k = 0;
    *topo = RD_PRIM_TRIANGLES;
    switch (prim) {
    case RD_PRIM_POINTS:
        /* one pixel: the sprite [x - 0.5, x + 0.5) covers the pixel whose
         * integer coordinate is nearest (ties to the left/top) */
        for (uint32_t i = 0; i < n; i++) {
            IcoSpriteVertex c;
            convVtx(&v[i], uvFixed, tw, th, uvOff, &c);
            IcoSpriteVertex q[4] = {c, c, c, c};
            q[0].x = q[2].x = (uint16_t)(c.x > 8 ? c.x - 8 : 0);
            q[1].x = q[3].x = (uint16_t)(c.x + 8);
            q[0].y = q[1].y = (uint16_t)(c.y > 8 ? c.y - 8 : 0);
            q[2].y = q[3].y = (uint16_t)(c.y + 8);
            o[k++] = q[0];
            o[k++] = q[1];
            o[k++] = q[2];
            o[k++] = q[2];
            o[k++] = q[1];
            o[k++] = q[3];
        }
        break;
    case RD_PRIM_LINES:
    case RD_PRIM_LINE_STRIP:
        *topo = RD_PRIM_LINES;
        for (uint32_t i = 0; i + 1 < n; i += (prim == RD_PRIM_LINES ? 2 : 1)) {
            convVtx(&v[i], uvFixed, tw, th, uvOff, &o[k++]);
            convVtx(&v[i + 1], uvFixed, tw, th, uvOff, &o[k++]);
        }
        break;
    case RD_PRIM_TRIANGLES:
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            for (int j = 0; j < 3; j++) {
                convVtx(&v[i + j], uvFixed, tw, th, uvOff, &o[k++]);
            }
        }
        break;
    case RD_PRIM_TRIANGLE_STRIP:
        for (uint32_t i = 0; i + 2 < n; i++) {
            for (int j = 0; j < 3; j++) {
                convVtx(&v[i + j], uvFixed, tw, th, uvOff, &o[k++]);
            }
        }
        break;
    case RD_PRIM_TRIANGLE_FAN:
        for (uint32_t i = 1; i + 1 < n; i++) {
            convVtx(&v[0], uvFixed, tw, th, uvOff, &o[k++]);
            convVtx(&v[i], uvFixed, tw, th, uvOff, &o[k++]);
            convVtx(&v[i + 1], uvFixed, tw, th, uvOff, &o[k++]);
        }
        break;
    case RD_PRIM_SPRITES:
        /* two corners; colour and Z from the second vertex (flat) */
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            IcoSpriteVertex a, b;
            convVtx(&v[i], uvFixed, tw, th, uvOff, &a);
            convVtx(&v[i + 1], uvFixed, tw, th, uvOff, &b);
            /* port UI text (RD_UV_FIXED_CONTINUOUS) is not GS content:
             * its glyph quads keep their sub-pixel edges and UVs */
            if ((s_uvShiftX != 0.0f || s_uvShiftY != 0.0f) && uvFixed != RD_UV_FIXED_CONTINUOUS) {
                spriteUvShift(&a, &b); /* a scaled target */
            }
            IcoSpriteVertex q[4] = {b, b, b, b};
            q[0].x = q[2].x = a.x;
            q[0].u = q[2].u = a.u;
            q[0].y = q[1].y = a.y;
            q[0].v = q[1].v = a.v;
            o[k++] = q[0];
            o[k++] = q[1];
            o[k++] = q[2];
            o[k++] = q[2];
            o[k++] = q[1];
            o[k++] = q[3];
        }
        break;
    default:
        break;
    }
    if (!gouraud && *topo != RD_PRIM_TRIANGLES) {
        /* flat lines (PRIM.IIP 0): the second vertex's colour */
        for (uint32_t i = 0; i + 1 < k; i += 2) {
            memcpy(o[i].rgba, o[i + 1].rgba, 4);
        }
    } else if (!gouraud && prim != RD_PRIM_SPRITES && prim != RD_PRIM_POINTS) {
        /* flat triangles: the last vertex's colour (sprites and points are
         * one colour already) */
        for (uint32_t i = 0; i + 2 < k; i += 3) {
            memcpy(o[i].rgba, o[i + 2].rgba, 4);
            memcpy(o[i + 1].rgba, o[i + 2].rgba, 4);
        }
    }
    return k;
}

static RdScratch *scratchGet(uint32_t w, uint32_t h, int which)
{
    int seen = 0;
    for (int i = 0; i < RD_SCRATCH_COUNT; i++) {
        RdScratch *s = &g_rd.scratch[i];
        if (s->tex.id && s->w == w && s->h == h && seen++ == which) {
            return s;
        }
    }
    for (int i = 0; i < RD_SCRATCH_COUNT; i++) {
        RdScratch *s = &g_rd.scratch[i];
        if (!s->tex.id) {
            s->tex = rhi_create_texture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UINT,
                                                          RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED |
                                                              RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST,
                                                          "rd exact scratch", 0});
            s->state = RHI_STATE_UNDEFINED;
            s->w = w;
            s->h = h;
            return s->tex.id ? s : NULL;
        }
    }
    /* pool full: recycle a slot of another size, never one of this size,
     * which may hold this operation's lower `which` (not every slot is of
     * this size, or the first loop would have found the which-th) */
    RdScratch *s = NULL;
    for (int i = RD_SCRATCH_COUNT - 1; i >= 0 && !s; i--) {
        if (g_rd.scratch[i].w != w || g_rd.scratch[i].h != h) {
            s = &g_rd.scratch[i];
        }
    }
    if (!s) {
        return NULL;
    }
    rhi_destroy_texture(s->tex);
    s->tex = (RhiTexture){0};
    return scratchGet(w, h, which);
}

/* the box-reduced shadow count (doShadowResolve) */
static RhiTexture s_shadowRed;

static RhiState s_shadowRedState;

static uint32_t s_shadowRedW, s_shadowRedH;

static uint32_t s_shadowRedFor; /* the count target (RdTarget id) s_shadowRed holds; 0 none */

/* Whether a draw blends by As (PRIM.ABE with C = As): its
 * texture's alpha is opacity, which the Enhanced mips must not raise. */
static int blendsByAs(const RdDrawState *d)
{
    if (!d->abe) {
        return 0;
    }
    switch (d->blend) {
    case RD_BLEND_LERP_AS:
    case RD_BLEND_LERP_AS_ALT:
    case RD_BLEND_CS_AS_ADD_CD:
    case RD_BLEND_CD_SUB_CS_AS:
    case RD_BLEND_CD_AS_ADD_CD:
        return 1;
    default:
        return 0;
    }
}

/* The alpha coverage the Enhanced mips of t keep: on (with
 * *ref) only for a texture that unblended draws alpha-test and no draw
 * blends by As.  A blended cut-out (the stair railings' lattice: ALPHA
 * 0x44, holes of alpha 0) fades out by its averaged alpha as the GS's own
 * blend would; raising that alpha turned its distant holes into an opaque
 * dark sheet. */
int rd__mip_boost(const RdTexRec *t, uint8_t *ref)
{
    *ref = t->mipRef;
    return (t->mipUse & RD_MIPUSE_TESTED) && !(t->mipUse & RD_MIPUSE_BLEND_AS);
}

/* Note what draw state d does with the alpha of t (a game
 * texture with Enhanced mips); when that changes the coverage its mips
 * should keep, t is uploaded again on the next replay (a few times per
 * texture at most: the bits only accumulate and the reference only
 * rises). */
static void noteMipUse(RdTexRec *t, const RdDrawState *d)
{
    if (t->mipLevels < 2 || t->replacement || t->gsLevels > 1 || d->tcc != RD_TCC_RGBA) {
        return; /* GS levels are uploaded as they are: no coverage to keep */
    }
    uint8_t use = t->mipUse, ref = t->mipRef;
    if (blendsByAs(d)) {
        use |= RD_MIPUSE_BLEND_AS;
    } else if (d->test.ate &&
               (d->test.afail == RD_AFAIL_KEEP || d->test.afail == RD_AFAIL_ZB_ONLY) &&
               (d->test.atst == RD_ATST_GREATER ||
                (d->test.atst == RD_ATST_GEQUAL && d->test.aref > 0))) {
        const uint8_t r = d->test.atst == RD_ATST_GEQUAL ? d->test.aref - 1 : d->test.aref;
        if (!(use & RD_MIPUSE_TESTED) || r > ref) {
            ref = r;
        }
        use |= RD_MIPUSE_TESTED;
    }
    if (use == t->mipUse && ref == t->mipRef) {
        return;
    }
    t->mipUse = use;
    t->mipRef = ref;
    uint8_t want;
    const int boost = rd__mip_boost(t, &want);
    if (boost == t->mipBuiltBoost && (!boost || want == t->mipBuiltRef)) {
        return;
    }
    if (!t->dirty) {
        g_rd.texDirtyCount++;
    }
    t->dirty = 1;
    t->dirtyX0 = t->dirtyY0 = 0;
    t->dirtyX1 = t->w;
    t->dirtyY1 = t->h;
}

/* The target's snapshot texture, made on first use; false when it cannot be */
static bool snapEnsure(RdTargetRec *tc)
{
    if (!tc->snap.id) {
        /* made as the target's colour is (rd__target_create_gpu): a
         * staticBlur sprite that covers the whole target draws into it and
         * the two change places (blurSwapTarget) */
        const uint32_t usage = RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC |
                               RHI_TEX_COPY_DST | rd__dst_target_usage(tc->format, NULL) |
                               (tc->uintView ? (uint32_t)RHI_TEX_UINT_VIEW : 0u);
        tc->snap = rhi_create_texture(
            &(RhiTextureDesc){tc->tw, tc->th, 1, tc->format, usage, "rd snap", 0});
        tc->snapState = RHI_STATE_UNDEFINED;
    }
    if (!tc->snap.id) {
        rd__note_target_pressure(tc);
        return false;
    }
    return true;
}

/* The target's colour copied into its snapshot texture (created on first
 * use) at the same texels, area only: the copy ends the open pass and
 * leaves the snapshot readable.  The rest of the snapshot is older content:
 * a caller copies an area that holds every texel its draw reads (the whole
 * target, or blurSnapArea's bound for a staticBlur sprite), so no draw reads
 * a stale texel.  False when there is no snapshot texture. */
static bool takeSnap(Replay *r, RdTargetRec *tc, RhiRect area)
{
    endPass(r);
    if (!snapEnsure(tc)) {
        return false;
    }
    rd__perf_pass(s_cl, RD_PERF_PASS_SNAP, (uint32_t)(tc - g_rd.targets) + 1u, area.w, area.h,
                  r->perfPost);
    g_rdPerf.snapshots++;
    s_blurSnapCount++;
    rd__transition(s_cl, tc->color, &tc->colorState, RHI_STATE_COPY_SRC);
    rd__transition(s_cl, tc->snap, &tc->snapState, RHI_STATE_COPY_DST);
    rhi_cmd_checkpoint(s_cl, passPush(RD_PASSK_SNAP, (uint32_t)(tc - g_rd.targets) + 1u, tc->tw,
                                      tc->th, 0, area.w, area.h, 0, r->perfPost));
    copyCmd(s_cl, tc->color, area, tc->snap, area.x, area.y);
    rd__transition(s_cl, tc->snap, &tc->snapState, RHI_STATE_SHADER_READ);
    return true;
}

/* The texture a draw samples, after any state change it needs.  Returns the
 * RhiTexture and its size and TEXFMT; dummy when untextured. */
static RhiTexture resolveTexture(Replay *r, RdTargetRec *drawTarget, uint32_t drawTargetId,
                                 uint32_t *w, uint32_t *h, uint32_t *fmt, int *textured,
                                 int *mipmapped)
{
    *textured = 0;
    *mipmapped = 0;
    *w = *h = 1;
    *fmt = 0;
    if (!r->st.ds.texEnabled) {
        return g_rd.dummy;
    }
    RdTexRec *t = rd__tex_rec(r->st.tex);
    if (!t) {
        rd__log_once(RD_ONCE_BAD_TEX, "draw with a destroyed or unknown texture: untextured");
        return g_rd.dummy;
    }
    *fmt = t->src;
    if (t->kind == RD_TEXKIND_IMAGE) {
        if (!t->rhi.id || t->state != RHI_STATE_SHADER_READ) {
            return g_rd.dummy;
        }
        *w = t->w;
        *h = t->h;
        *textured = 1;
        *mipmapped = t->mipLevels > 1;
        noteMipUse(t, &r->st.ds);
        return t->rhi;
    }
    RdTargetRec *src = rd__target_rec(t->target);
    if (!src) {
        return g_rd.dummy;
    }
    if (t->view == RD_VIEW_DEPTH) {
        /* only RD_POST_FOG reads a depth view (doFog, through
         * its LUT); a draw that finds the view still bound (the fog's TEX0
         * leaking, as on the GS) samples nothing */
        rd__log_once(RD_ONCE_DEPTH_VIEW, "a draw samples a depth view outside the fog: untextured");
        return g_rd.dummy;
    }
    *w = src->w;
    *h = src->h;
    *textured = 1;
    rd__block_read(src);
    if (s_shadowRedFor && t->target == s_shadowRedFor && s_shadowRed.id &&
        t->target != drawTargetId) {
        return s_shadowRed; /* the shadow count at the GS size */
    }
    if (t->target == drawTargetId && src == drawTarget) {
        /* the GS reads the buffer it is drawing into: sample a copy, whole
         * (the sampler, the mip choice and a perspective STQ leave the
         * texels it reads unbounded here) */
        if (!takeSnap(r, src, (RhiRect){0, 0, src->tw, src->th})) {
            *textured = 0;
            *w = *h = 1;
            return g_rd.dummy; /* no snapshot texture: untextured */
        }
        return src->snap;
    }
    if (src->colorState != RHI_STATE_SHADER_READ) {
        endPass(r);
        rd__transition(s_cl, src->color, &src->colorState, RHI_STATE_SHADER_READ);
    }
    return src->color;
}

/* The target the draw samples when it is a Full pixel crop of the frame
 * and tex its colour or its snapshot (the texture resolveTexture gave), else
 * NULL (an image, the shadow count's GS-size reduction, an uncropped
 * target) */
static const RdTargetRec *texCropOf(const Replay *r, RhiTexture tex)
{
    const RdTexRec *t = r->st.ds.texEnabled ? rd__tex_rec(r->st.tex) : NULL;
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view == RD_VIEW_DEPTH || !tex.id) {
        return NULL;
    }
    const RdTargetRec *src = rd__target_rec(t->target);
    if (!src || !(src->ox > 0.0f) || (tex.id != src->color.id && tex.id != src->snap.id)) {
        return NULL;
    }
    return src;
}

/* TEST.DATE: the bound target's alpha MSB into the R8 snapshot
 * (RD_TARGET_DATE_SNAPSHOT), pixel for pixel, for sprite_ps to test at t2.
 * Retaken when the target changed or anything may have written alpha since
 * the last one, so consecutive DATE draws see each other's writes as on the
 * GS (within one draw, overlapping primitives see the snapshot: accepted).
 * area (texels, NULL = the whole target) holds every fragment the draw can
 * make (rd__screen_area), and the shaders read the snapshot only at the
 * fragment's own texel, so only that area is drawn: the snapshot is right
 * inside r->dateArea and stale outside it, and a draw whose area it does
 * not hold retakes it (rd__date_retake), never reading a stale texel. */
static RhiTexture dateSnapshot(Replay *r, RdTargetRec *tc, uint32_t tcId, const RhiRect *area)
{
    RdTargetRec *sn = rd__target_rec(RD_TARGET_DATE_SNAPSHOT + 1);
    if (!sn || !sn->color.id || tc == sn) {
        return g_rd.dummy;
    }
    /* in texels: the snapshot has the scene's scale and sprite_ps
     * reads it at the fragment's texel */
    uint32_t w = tc->tw, h = tc->th;
    if (w > sn->tw || h > sn->th) {
        rd__log_once(RD_ONCE_DATE_SIZE, "DATE on a target larger than the snapshot: clipped");
        w = w > sn->tw ? sn->tw : w;
        h = h > sn->th ? sn->th : h;
    }
    RhiRect take;
    const bool retake = rd__date_retake(r->dateFor, r->dateSerial, &r->dateArea, tcId,
                                        r->writeSerial, area, w, h, &take);
    if (!retake && sn->colorState == RHI_STATE_SHADER_READ) {
        return sn->color;
    }
    if (!retake) {
        /* the snapshot was touched elsewhere since: nothing of it is known */
        take = (RhiRect){0, 0, w, h};
    }
    endPass(r);
    rd__perf_pass(s_cl, RD_PERF_PASS_DATE, tcId, take.w, take.h, r->perfPost);
    g_rdPerf.dateSnapshots++;
    rd__date_snap(tc, s_ms.active && !s_ms.internal, s_ms.ended);
    rd__transition(s_cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    rd__transition(s_cl, sn->color, &sn->colorState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = sn->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = sn->tw;
    p.height = sn->th;
    rhi_cmd_begin_render_pass(s_cl, &p);
    setViewport(w, h);
    /* the same full-target triangle and viewport, cut to the area */
    rhi_cmd_set_scissor(s_cl, &take);
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
    RhiPipeline pipe = rd__get_pipeline(&k);
    if (pipe.id) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.uvRect[2] = (float)w;
        cb.uvRect[3] = (float)h;
        cb.tex[0] = (float)tc->tw;
        cb.tex[1] = (float)tc->th;
        cb.tex[2] = 1.0f / (float)tc->tw;
        cb.tex[3] = 1.0f / (float)tc->th;
        rd__set_pipeline(s_cl, pipe, &k);
        rd__bind_uniform(s_cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
        rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(
            s_cl, 2,
            rd__tex_group(tc->color, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                 RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        drawCmd(s_cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(s_cl);
    rd__transition(s_cl, sn->color, &sn->colorState, RHI_STATE_SHADER_READ);
    r->dateFor = tcId;
    r->dateSerial = r->writeSerial;
    r->dateArea = take;
    return sn->color;
}

/* ------------------------------------------------------- scale and frame
 * A target's texture is tw x th texels for its w x h GS pixels (sx, sy
 * texels per pixel; 1 in Original).  FrameCB keeps the GS size (g_target),
 * so every vertex lands where it did and the viewport, the scissor and the
 * copies are what scale; g_z.yz carries the scale for the shaders that
 * address a target by texel (fx_sprite_ps, fog_lut_ps).
 *
 * The native frame: the scene-class targets are as wide in GS pixels as the
 * display aspect asks (gsW, GsBase.c gsFrame.w; 512 at 4:3), the origin
 * 2048 - gsW / 2, and nothing is squeezed or stretched: a world draw, a UI
 * prim and a full-screen pass are each drawn at the GS X the game gave
 * (the 2D layer's 640 grid lands on the frame's centre 2048 -+ 256; the
 * game draws a full-frame item at the frame's width).  The only aspect term
 * left is the region scale of the PS2's 4:3 window test (rd__region_scale). */

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

/* A target of the native frame's GS size (SCENE, WORK2, AURA_WORK, the
 * shadow count, a render-to-texture block the game sizes as the frame). */
static int frameSized(const RdTargetRec *tc)
{
    return tc->w == g_rd.gsW && tc->h == g_rd.gsH;
}

float rd__region_scale(const RdTargetRec *tc)
{
    if (!tc || g_rd.gsW == ICO_FRAME_BASE_W || g_rd.gsW == 0 || !frameSized(tc)) {
        return 1.0f;
    }
    return (float)ICO_FRAME_BASE_W / (float)g_rd.gsW;
}

/* GS pixels x0..x1, y0..y1 (inclusive, after XYOFFSET) of target tc in its
 * texels: the UI flip when mirror (GS pixel p is target pixel w - 1 - p),
 * clipped to the target, then the texel scale: GS pixels x0..x1 cover texels
 * floor(x0 * sx) .. ceil((x1 + 1) * sx) - 1.  A draw's box may start
 * outside (its vertices off the target's edge).  False when nothing is
 * left. */
static bool gsRectToTexels(const RdTargetRec *tc, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                           int mirror, RhiRect *out)
{
    if (mirror) {
        const int32_t a = (int32_t)tc->w - 1 - x1, b = (int32_t)tc->w - 1 - x0;
        x0 = a;
        x1 = b;
    }
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 >= (int32_t)tc->w ? (int32_t)tc->w - 1 : x1;
    y1 = y1 >= (int32_t)tc->h ? (int32_t)tc->h - 1 : y1;
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    if (tc->sx != 1.0f || tc->sy != 1.0f) {
        int32_t a = (int32_t)((float)x0 * tc->sx), b = (int32_t)((float)y0 * tc->sy);
        int32_t c = (int32_t)((float)(x1 + 1) * tc->sx + 0.999f) - 1;
        int32_t d = (int32_t)((float)(y1 + 1) * tc->sy + 0.999f) - 1;
        if (tc->ox > 0.0f) {
            /* Full pixel: the frame's columns from ox texels in
             * (rd__gs_texel_x); the GS pixels outside the window clip */
            a = (int32_t)floorf(rd__gs_texel_x(tc, (float)x0));
            c = (int32_t)ceilf(rd__gs_texel_x(tc, (float)(x1 + 1)) - 0.001f) - 1;
            a = a < 0 ? 0 : a;
            if (c < a) {
                return false;
            }
        }
        x0 = a;
        y0 = b;
        x1 = c >= (int32_t)tc->tw ? (int32_t)tc->tw - 1 : c;
        y1 = d >= (int32_t)tc->th ? (int32_t)tc->th - 1 : d;
        if (x1 < x0) {
            return false;
        }
    }
    *out = (RhiRect){x0, y0, (uint32_t)(x1 - x0 + 1), (uint32_t)(y1 - y0 + 1)};
    return true;
}

/* the scissor (SCISSOR_1, inclusive GS pixels) in texels, clipped to the
 * target first; a flipped UI draw clips where its scissor lands after the
 * flip. */
static bool scissorTexels(const RdTargetRec *tc, const RdStateBlock *st, int mirror, RhiRect *sc)
{
    int32_t x0 = st->scissor[0] < 0 ? 0 : st->scissor[0];
    int32_t y0 = st->scissor[1] < 0 ? 0 : st->scissor[1];
    int32_t x1 = st->scissor[2] >= (int32_t)tc->w ? (int32_t)tc->w - 1 : st->scissor[2];
    int32_t y1 = st->scissor[3] >= (int32_t)tc->h ? (int32_t)tc->h - 1 : st->scissor[3];
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    return gsRectToTexels(tc, x0, y0, x1, y1, mirror, sc);
}

/* the scissor of the draw being bound, in texels */
static bool scissorRect(const Replay *r, const RdTargetRec *tc, RhiRect *sc)
{
    return scissorTexels(tc, &r->st, r->mirror, sc);
}

static bool rectMeet(RhiRect a, RhiRect b, RhiRect *out)
{
    const int64_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
    const int64_t ax1 = (int64_t)a.x + a.w, bx1 = (int64_t)b.x + b.w;
    const int64_t ay1 = (int64_t)a.y + a.h, by1 = (int64_t)b.y + b.h;
    const int64_t x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;
    if (x1 <= x0 || y1 <= y0) {
        return false;
    }
    *out = (RhiRect){(int32_t)x0, (int32_t)y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
    return true;
}

static bool rectHolds(RhiRect outer, RhiRect inner)
{
    return inner.x >= outer.x && inner.y >= outer.y &&
           (int64_t)inner.x + inner.w <= (int64_t)outer.x + outer.w &&
           (int64_t)inner.y + inner.h <= (int64_t)outer.y + outer.h;
}

/* Every fragment of a screen-prim command lies in its vertices' bounding
 * box give or take a pixel: sprite corners snapped up to whole pixels
 * (snapAxis), points drawn as one-pixel quads, lines reaching the pixels
 * next to them, the mirror's sixteenth of a texel and the half-line offset
 * of a field target; AA1 adds a one-pixel fringe outside every edge
 * (aa1Fringe).  The pad covers all of them, and convVtx's clamp to the
 * 12.4 range is applied to the box as to the vertices. */
bool rd__screen_area(const RdTargetRec *tc, const RdStateBlock *st, const RdScreenVtx *v,
                     uint32_t n, int mirror, int aa1, RhiRect *area)
{
    if (!tc || n == 0) {
        return false;
    }
    int32_t lx = v[0].x, hx = v[0].x, ly = v[0].y, hy = v[0].y;
    for (uint32_t i = 1; i < n; i++) {
        lx = v[i].x < lx ? v[i].x : lx;
        hx = v[i].x > hx ? v[i].x : hx;
        ly = v[i].y < ly ? v[i].y : ly;
        hy = v[i].y > hy ? v[i].y : hy;
    }
    lx = lx < 0 ? 0 : (lx > 0xFFFF ? 0xFFFF : lx);
    hx = hx < 0 ? 0 : (hx > 0xFFFF ? 0xFFFF : hx);
    ly = ly < 0 ? 0 : (ly > 0xFFFF ? 0xFFFF : ly);
    hy = hy < 0 ? 0 : (hy > 0xFFFF ? 0xFFFF : hy);
    const int32_t pad = aa1 ? 3 : 2;
    const int32_t ox = 2048 - (int32_t)(st->gsW >> 1), oy = 2048 - (int32_t)(st->gsH >> 1);
    RhiRect box, sc;
    if (!gsRectToTexels(tc, floorDiv16(lx) - ox - pad, floorDiv16(ly) - oy - pad,
                        -floorDiv16(-hx) - ox + pad, -floorDiv16(-hy) - oy + pad, mirror, &box) ||
        !scissorTexels(tc, st, mirror, &sc)) {
        return false;
    }
    return rectMeet(box, sc, area);
}

bool rd__date_retake(uint32_t heldFor, uint32_t heldSerial, const RhiRect *held, uint32_t tcId,
                     uint32_t serial, const RhiRect *want, uint32_t tw, uint32_t th, RhiRect *take)
{
    const RhiRect whole = {0, 0, tw, th};
    RhiRect need = whole;
    if (want && !rectMeet(*want, whole, &need)) {
        need = whole;
    }
    if (heldFor == tcId && heldSerial == serial) {
        if (held && rectHolds(*held, need)) {
            return false;
        }
        /* nothing changed the target's alpha since the snapshot: what it
         * holds is still right, and the whole target ends the retakes */
        *take = whole;
        return true;
    }
    *take = need;
    return true;
}

/* The reduction's border open: Full pixel on, or off in the game on a
 * cropped frame (rd_present.c rd__frame_cropped: 16:9 and the other aspects
 * whose exact width is under gsW, where step 2's crop cuts the game's own
 * border away), where the presenter masks the border over the same picture
 * (RdSettings.borderMask, rd__border_mask), so the picture inside the mask
 * is Full pixel on's.  Off at 4:3 and wherever the frame is its exact width
 * (48:9), and in a zeroed RdSettings: the reduction's own scissor, the GS's
 * own DISPLAY with its border (the PS2's picture) */
static bool borderOpen(void)
{
    return g_rd.settings.fullPixel || (g_rd.settings.borderMask && rd__frame_cropped());
}

/* Full pixel: a sprite the game tags RD_SPACE_FULLFRAME (a bar or fade that
 * spans the frame: layout_texture.c's spanning items and primary sprite,
 * the posts' rects) that ends one pixel short of the target's edge is moved
 * to the edge, so the strip the option shows beside it is not a column of
 * the picture under the bar.  "One pixel short" is the first and last pixel
 * it covers (ceil(x0), ceil(x1) - 1 in the target's pixels) within tol of
 * the target's two edges; tol is 1 and grows by one per 640 pixels of the
 * target's width (the layout bars are inset 0.75 of a 640-grid unit on the
 * right, which is more than a pixel on a wide frame).  Only the sprite's x
 * moves; its u stays, so the added pixel reads the sprite's own end texel
 * and the texture is never extrapolated.  Returns v, or a copy with the
 * bands moved (the recorded frame is never written).  Off, a space that is
 * not FULLFRAME, a target that is not frame-sized or no short band: v
 * itself.  The tag is the whole rule: nothing is detected from the pixels. */
static const RdScreenVtx *extendFullFrame(const Replay *r, const RdTargetRec *tc,
                                          const RdScreenVtx *v, uint32_t n, uint8_t prim,
                                          uint8_t space)
{
    static RdScreenVtx *s_wv;
    static uint32_t s_wvCap;
    if (!borderOpen() || !tc || !frameSized(tc) || space != RD_SPACE_FULLFRAME ||
        prim != RD_PRIM_SPRITES || n < 2) {
        return v;
    }
    const RdScreenVtx *src = v;
    const int32_t ox = 2048 - (int32_t)(r->st.gsW >> 1);
    const int32_t tol = 1 + ((int32_t)tc->w - 1) / 640;
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        int32_t a = v[i].x, b = v[i + 1].x;
        if (a > b) {
            const int32_t t = a;
            a = b;
            b = t;
        }
        const int32_t first = -floorDiv16(-a) - ox;
        const int32_t last = -floorDiv16(-b) - 1 - ox;
        if (first > tol || last < (int32_t)tc->w - 1 - tol ||
            (first <= 0 && last >= (int32_t)tc->w - 1)) {
            continue;
        }
        if (src == v) {
            if (n > s_wvCap) {
                RdScreenVtx *p = realloc(s_wv, (size_t)n * sizeof(*p));
                if (!p) {
                    return v;
                }
                s_wv = p;
                s_wvCap = n;
            }
            memcpy(s_wv, v, (size_t)n * sizeof(*v));
            src = s_wv;
        }
        /* the smaller x is the left edge; the pixel 0 starts at ox * 16,
         * the one past the last at (ox + w) * 16 */
        RdScreenVtx *lo = &s_wv[i], *hi = &s_wv[i + 1];
        if (lo->x > hi->x) {
            RdScreenVtx *t = lo;
            lo = hi;
            hi = t;
        }
        if (first > 0) {
            lo->x = ox * 16;
        }
        if (last < (int32_t)tc->w - 1) {
            hi->x = (ox + (int32_t)tc->w) * 16;
        }
    }
    return src;
}

/* Whether a screen prim is a screen-space pass rather than geometry: a
 * sprite whose texture is a render target (the shadow pass's and the
 * softening's composites into SCENE, the reduction's fallback sprite), or a
 * sprite the game marks full-screen (the fog's fogOffsetA sheet, rd_post.c's
 * passes).  Such a sprite maps target texels onto screen pixels one for
 * one, or covers the whole target, so it keeps the offset its target was
 * recorded with (sceneKeepsHalf).  Triangles and lines that sample a target
 * are geometry and take the half pixel. */
static int screenCopy(const Replay *r, const DrawSetup *ds, uint8_t prim, uint8_t space)
{
    if (prim != RD_PRIM_SPRITES) {
        return 0;
    }
    if (space == RD_SPACE_FULLFRAME) {
        return 1;
    }
    if (!ds->textured || !r->st.ds.texEnabled) {
        return 0;
    }
    const RdTexRec *t = rd__tex_rec(r->st.tex);
    return t && t->kind == RD_TEXKIND_TARGET;
}

/* The draw offset a draw is positioned with (RD_TARGET_OFFSET,
 * RD_TARGET_HALF_Y): the target's, without the half pixel sceneKeepsHalf
 * added when the draw is a screen-space pass (DrawSetup.screenCopy). */
static uint32_t drawOffset(const Replay *r, const DrawSetup *ds)
{
    if (r->halfAdded && ds->screenCopy) {
        return r->st.useOffset & ~(uint32_t)RD_TARGET_HALF_Y;
    }
    return r->st.useOffset;
}

/* Texture packs: a replacement's UVs are normalised by the GS size of the
 * texture it replaces (RdTexRec.uvW/uvH), not by its own: the game's UVs
 * address the original's texels, and a 4x replacement covers the same
 * place.  The only use of uvW/uvH: everything else (levels, uploads,
 * readbacks) is the replacement's own size. */
static void replacementUv(uint32_t tex, DrawSetup *ds)
{
    const RdTexRec *t = ds->textured ? rd__tex_rec(tex) : NULL;
    if (!t || t->kind != RD_TEXKIND_IMAGE || !t->replacement || ds->tex.id != t->rhi.id) {
        return;
    }
    ds->replacement = 1;
    if (t->uvW && t->uvH) {
        ds->tw = t->uvW;
        ds->th = t->uvH;
    }
}

/* The GS levels the draw's texture has on the device (RdTexRec.gsLevels,
 * uploaded: mipLevels), 0 for every other texture.  A pack replacement
 * keeps the pack's own levels and its sampling (texSampler), whatever the
 * TIM2 it replaces had. */
static int drawGsLevels(const Replay *r, const DrawSetup *ds)
{
    const RdTexRec *t = ds->textured && r->st.ds.texEnabled ? rd__tex_rec(r->st.tex) : NULL;
    if (!t || t->kind != RD_TEXKIND_IMAGE || t->replacement || ds->tex.id != t->rhi.id ||
        t->mipLevels < 2) {
        return 0;
    }
    if (t->gsLevels < 2) {
        /* the Original preset: a texture whose TIM2 levels are reductions of
         * its base (RdTexRec.gsLevels 0) at the GS's level too, over the
         * chain built from the base (rd_upload.c texLevels).  PCSX2 draws the title's
         * castle (TEX1 MXL 1 to 3, K -10.3 to -11.9) at levels 1 to 3, where
         * level 0 had every brick sharp */
        return g_rd.settings.preset == RD_PRESET_ORIGINAL && rd_tex1_mipmapped(r->st.tex1Lod)
                   ? t->mipLevels
                   : 0;
    }
    return t->mipLevels < t->gsLevels ? t->mipLevels : t->gsLevels;
}

/* DATE through the stencil bit for a draw into tc with the depth of td
 * (RdProfile.dateStencil; docs/port/RENDER.md, "The replay"): the target
 * made for DATE's local read (rd_core.c: RGBA8 of the scene class), a
 * depth of its size bound (the bit lives in its stencil, which then covers
 * every texel of the draw), the setup's pipelines, no MSAA epoch open or
 * able to open (SCENE_MS is no readable target) and no exact blend's
 * storage pass (no colour attachment to read).  Otherwise the R8 snapshot */
static bool dateStencilFor(const RdTargetRec *tc, const RdTargetRec *td)
{
    if (!g_rd.profile.dateStencil || !td || !g_rd.layoutLocal.id ||
        !(tc->dstUsage & RD_DST_USAGE_LOCAL_DATE) || tc->format != RHI_FMT_RGBA8_UNORM ||
        td->tw != tc->tw || td->th != tc->th || s_ms.active || (s_ms.want > 1u && !s_ms.ended) ||
        g_rd.dstMode == RD_BLEND_EXACT_STORAGE) {
        return false;
    }
    const RdPipeKeyInt kc = rd__date_setup_key(RD_STENCIL_DATE_CLEAR, tc->format);
    const RdPipeKeyInt ks = rd__date_setup_key(RD_STENCIL_DATE_SET, tc->format);
    const bool made = rd__get_pipeline(&kc).id != 0 && rd__get_pipeline(&ks).id != 0;
    /* gathering (rd__pipeline_gather): the keys are only named, so the
     * draw's twins are named too */
    return made || rd__pipeline_gathering();
}

/* The target, the DATE snapshot and the texture (each may end the open
 * pass: they run before the draw's pass begins).  dateArea: the texels the
 * draw can touch, for the DATE snapshot (NULL: the whole target).
 * dateStencil: the caller draws its passes through expandPasses and
 * bindDrawEx (doScreen, the VU draws), so DATE may take the stencil bit
 * instead of the snapshot (dateStencilFor; r->dateStNow) */
static bool prepareDrawEx(Replay *r, DrawSetup *ds, const RhiRect *dateArea, bool dateStencil)
{
    memset(ds, 0, sizeof(*ds));
    r->mirror = 0; /* likewise */
    r->dateStNow = 0;
    ds->tc = rd__target_rec(r->st.color);
    if (!ds->tc || !ds->tc->color.id) {
        return false;
    }
    RdTargetRec *td = rd__target_rec(r->st.depth);
    if (td && !td->withDepth) {
        td = NULL;
    }
    ds->dateTex = g_rd.dummy;
    if (r->st.ds.test.date != RD_DATE_OFF) {
        if (dateStencil && dateStencilFor(ds->tc, td)) {
            ds->dateStencil = 1;
            ds->dateWantAll = dateArea == NULL;
            ds->dateWant = dateArea ? *dateArea : (RhiRect){0, 0, ds->tc->tw, ds->tc->th};
            r->dateStNow = 1;
        } else {
            ds->dateTex = dateSnapshot(r, ds->tc, r->st.color, dateArea);
        }
    }
    ds->tdId = td ? r->st.depth : 0;
    ds->depthFmt = td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
    ds->tex = resolveTexture(r, ds->tc, r->st.color, &ds->tw, &ds->th, &ds->tfmt, &ds->textured,
                             &ds->mipmapped);
    ds->texCrop = texCropOf(r, ds->tex);
    replacementUv(r->st.tex, ds);
    ds->gsLevels = drawGsLevels(r, ds);
    return true;
}

static bool prepareDraw(Replay *r, DrawSetup *ds, const RhiRect *dateArea)
{
    return prepareDrawEx(r, ds, dateArea, false);
}

/* A texture with GS levels: the level is the shader's (DF_GS_LOD, given to
 * SampleLevel), the sampler the TEX1 filters within a level, linear between
 * levels for MMIN 3 and 5 (RD_SAMPLER_SET_MIP_LINEAR), nearest for the
 * others (RD_SAMPLER_SET_MIP_NEAREST), never the Enhanced filter's.  In the
 * Original preset (rd__gs_filter_int) always nearest between levels: the
 * shader samples each level on the GS's grid and blends the two itself
 * (ICO_DF_GS_BILINEAR_GRID), or loads the texels (the TEXA entries). */
static RhiSampler gsLodSampler(const Replay *r, const DrawSetup *ds);

/* Full pixel: a draw that samples a render target (a screen-space lookup,
 * such as the mirror floors' copy of the scene and the reflections) clamps
 * at the target's edges.  The game leaves those draws on REPEAT, and at the
 * frame's outer columns the lookup wraps to the far side of the picture,
 * which the PS2's black border hid; with the border open it showed as a
 * strip of the far edge's colour.  Image textures keep their wrap, and so
 * does an axis on which a screen prim's coordinates lie wholly in a later
 * period of the REPEAT (ds->repeatKept, rd__repeat_kept: the puddle's
 * ripples, issue 106).  axis 0 is S, 1 T. */
static RdWrap drawWrap(const Replay *r, const DrawSetup *ds, RdWrap w, int axis)
{
    if (w == RD_WRAP_CLAMP || !borderOpen() || !ds->textured || !r->st.ds.texEnabled ||
        (ds->repeatKept & (1u << axis))) {
        return w;
    }
    const RdTexRec *t = rd__tex_rec(r->st.tex);
    return t && t->kind == RD_TEXKIND_TARGET ? RD_WRAP_CLAMP : w;
}

/* FrameCB of the draw (r->frameBG), made again when the target, the frame
 * size, the offset, the pass or the camera's picture changed since the last
 * draw */
static void frameRefresh(Replay *r, const DrawSetup *ds)
{
    const RdTargetRec *tc = ds->tc;
    const uint32_t useOffset = drawOffset(r, ds);
    const uint32_t fk[6] = {r->st.color, r->st.gsW,     r->st.gsH,
                            useOffset,   r->passSerial, r->picture};
    if (memcmp(fk, r->frameKey, sizeof(fk)) != 0) {
        /* XYOFFSET = (2048 - w/2, 2048 - h/2), w the native frame's width
         * (+ the preset's field offset when useOffset; zero in Original) */
        float ox = 2048.0f - (float)(r->st.gsW >> 1);
        float oy = 2048.0f - (float)(r->st.gsH >> 1);
        if (useOffset & RD_TARGET_HALF_Y) {
            oy += 0.5f; /* the flip's sceGsSetHalfOffset */
        }
        s_framePicture = r->picture;
        s_framePictureFrameH = r->st.gsH;
        r->frameBG = frameGroupOf(tc, ox, oy, rd__target_z_scale(ds->tdId));
        s_framePicture = s_framePictureFrameH = 0;
        memcpy(r->frameKey, fk, sizeof(fk));
    }
}

RdUniform rd__replay_vu_frame(const RdStateBlock *st, uint32_t picture)
{
    /* frameRefresh's FrameCB for a VU draw (no screen copy: the target's
     * own offset) under the state st, prepareDraw's target and depth */
    const RdTargetRec *tc = rd__target_rec(st->color);
    if (!tc || !tc->color.id) {
        return (RdUniform){{0}, 0};
    }
    const RdTargetRec *td = rd__target_rec(st->depth);
    const uint32_t tdId = td && td->withDepth ? st->depth : 0u;
    const float ox = 2048.0f - (float)(st->gsW >> 1);
    float oy = 2048.0f - (float)(st->gsH >> 1);
    if (st->useOffset & RD_TARGET_HALF_Y) {
        oy += 0.5f;
    }
    s_framePicture = picture;
    s_framePictureFrameH = st->gsH;
    const RdUniform u = frameGroupOf(tc, ox, oy, rd__target_z_scale(tdId));
    s_framePicture = s_framePictureFrameH = 0;
    return u;
}

/* The scissor, the pass and FrameCB; returns the texture group, id 0 when
 * the scissor leaves nothing to draw.  With scOut the scissor is returned
 * (doScreen sets it when it draws) instead of set, and the sampler's id
 * with it. */
static RhiBindGroup bindDrawEx(Replay *r, const DrawSetup *ds, RhiRect *scOut, uint32_t *smpOut,
                               bool keepStorage)
{
    RdTargetRec *tc = ds->tc;
    /* scissor (SCISSOR_1, inclusive) clipped to the target, in texels */
    RhiRect sc;
    if (!scissorRect(r, tc, &sc)) {
        return (RhiBindGroup){0};
    }
    RhiSampler smp = ds->gsLevels
                         ? gsLodSampler(r, ds)
                         : texSampler((RdFilter)r->st.ds.magFilter, (RdFilter)r->st.ds.minFilter,
                                      drawWrap(r, ds, (RdWrap)r->st.ds.wrap.s, 0),
                                      drawWrap(r, ds, (RdWrap)r->st.ds.wrap.t, 1), ds->mipmapped,
                                      ds->replacement);
    RhiBindGroup g2 = rd__tex_group_date(ds->tex, smp, ds->dateTex);

    /* a storage pass (the exact blend) stays open for the callers that
     * choose the pass kind per draw (keepStorage: doScreen, the VU draws,
     * the fog); every other draw needs the colour attachment */
    if (!r->passOpen || r->passColor != r->st.color || r->passDepth != ds->tdId ||
        (r->passStorage && !keepStorage) || (ds->dateStencil && !r->passReadable)) {
        beginPass(r, tc, ds->tdId ? rd__target_rec(ds->tdId) : NULL, r->st.color, ds->tdId,
                  RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD, 0.0f, false);
    }
    if (ds->dateStencil && !rd__replay_date_take(r, ds)) {
        return (RhiBindGroup){0}; /* the bit could not be written: nothing drawn */
    }
    frameRefresh(r, ds);
    if (scOut) {
        *scOut = sc;
        *smpOut = smp.id;
    } else {
        r->sc = sc;
        rhi_cmd_set_scissor(s_cl, &sc);
    }
    return g2;
}

static RhiBindGroup bindDraw(Replay *r, const DrawSetup *ds)
{
    return bindDrawEx(r, ds, NULL, NULL, false);
}

/* bindDraw for a draw that picks its pass kind per pass (dstPassMode) */
static RhiBindGroup bindDrawKeep(Replay *r, const DrawSetup *ds)
{
    return bindDrawEx(r, ds, NULL, NULL, true);
}

/* ------------------------------------------------- the exact blend's passes
 * rd_dst_read.c.  The destination-read mode a draw into the open pass takes:
 * the one in force, on a single-sampled RGBA8 target made for it, and for
 * the attachment read a pass begun readable; RD_BLEND_EXACT_OFF otherwise
 * (the fixed-function blend). */
static int dstModeFor(const Replay *r)
{
    const int m = g_rd.dstMode;
    if (m == RD_BLEND_EXACT_OFF || !r->passOpen || g_rd.passSamples > 1) {
        return RD_BLEND_EXACT_OFF;
    }
    const RdTargetRec *tc = rd__target_rec(r->passColor);
    if (!tc || tc->format != RHI_FMT_RGBA8_UNORM) {
        return RD_BLEND_EXACT_OFF;
    }
    if (m == RD_BLEND_EXACT_ATTACHMENT) {
        return r->passReadable ? m : RD_BLEND_EXACT_OFF;
    }
    return (tc->dstUsage & 1u) ? m : RD_BLEND_EXACT_OFF;
}

/* The planned passes as drawn into the open pass: the exact blend's images
 * (rd__expand_dst), then the two-pass fallback's (rd__expand_no_dual); at
 * most 4 for a plan of 2 (each pass becomes at most two).  merge: the open
 * pass is a storage pass and the storage pass merge is on (expandPasses):
 * a pass rd__dst_merge_ok takes is drawn through the destination-read
 * shader in it (rd__dst_merge) instead of in a pass with the colour
 * attached. */
static int expandPassesMode(const RdDrawPass *in, int n, int mode, bool merge, RdDrawPass out[8])
{
    if (mode == RD_BLEND_EXACT_OFF && g_rd.dstMode != RD_BLEND_EXACT_OFF) {
        for (int i = 0; i < n; i++) {
            if (in[i].dstWant) {
                rd__log_once(RD_ONCE_DST_FIXED,
                             "blend: a draw in the MSAA epoch or on a target of another format "
                             "keeps the fixed-function blend");
            }
        }
    }
    RdDrawPass dx[4];
    int m = 0;
    for (int i = 0; i < n && i < 2; i++) {
        if (merge && mode == RD_BLEND_EXACT_STORAGE && rd__dst_merge_ok(&in[i])) {
            out[m++] = rd__dst_merge(&in[i]); /* no second pass: blending off */
            continue;
        }
        const int nd = rd__expand_dst(&in[i], 1, mode, dx);
        m += rd__expand_no_dual(dx, nd, out + m);
    }
    return m;
}

static int expandPasses(const Replay *r, const RdDrawPass *in, int n, RdDrawPass out[8])
{
    const int mode = dstModeFor(r);
    const int m = expandPassesMode(
        in, n, mode, g_rd.passMerge && r->passStorage && mode == RD_BLEND_EXACT_STORAGE, out);
    if (r->dateStNow) {
        /* DATE through the stencil bit (prepareDrawEx): each pass as drawn
         * tests the bit with DF_DATE off (the exact blend's and the
         * two-pass fallback's images alike) */
        for (int i = 0; i < m; i++) {
            (void)rd__date_stencil_pass(&out[i]);
        }
    }
    return m;
}

/* The open pass as the kind a pass's key draws in: a storage pass for a
 * storage destination read, else one with the colour attached.  Begun
 * again on the same targets (loaded) when it is the other kind, the
 * scissor set again. */
static void dstPassMode(Replay *r, const RdPipeKeyInt *k)
{
    const bool storage = k->dst == RHI_DST_READ_STORAGE;
    if (!r->passOpen || (r->passStorage != 0) == storage) {
        return;
    }
    const uint32_t cid = r->passColor, did = r->passDepth;
    RdTargetRec *c = rd__target_rec(cid);
    if (!c) {
        return;
    }
    /* the draw's scissor (a screen run the switch draws first sets its own) */
    const RhiRect sc = r->sc;
    beginPass(r, c, did ? rd__target_rec(did) : NULL, cid, did, RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD,
              0.0f, storage);
    r->sc = sc;
    rhi_cmd_set_scissor(s_cl, &sc);
    g_rdPerf.dstSwitches++;
}

/* group 3 of a destination-read key: the open pass's target, as a storage
 * texture or as its input attachment; nothing for the other keys */
static void dstBind(Replay *r, const RdPipeKeyInt *k)
{
    if (k->gs.stencil == RD_STENCIL_DATE_TEST0 || k->gs.stencil == RD_STENCIL_DATE_TEST1) {
        /* DATE's stencil bit (every caller draws right after) */
        rhi_cmd_set_stencil_ref(s_cl, RD_DATE_STENCIL_BIT);
        g_rdPerf.dateStencilDraws++;
    }
    if (k->dst == RHI_DST_READ_NONE || !r->passOpen) {
        return;
    }
    g_rdPerf.dstDraws++; /* every caller draws right after */
    if (!r->dstGroup.id || r->dstGroupSerial != r->passSerial) {
        RdTargetRec *c = rd__target_rec(r->passColor);
        RhiBinding b;
        memset(&b, 0, sizeof(b));
        b.slot = 0;
        b.type =
            k->dst == RHI_DST_READ_STORAGE ? RHI_BIND_STORAGE_TEXTURE : RHI_BIND_INPUT_ATTACHMENT;
        b.texture = c ? c->color : (RhiTexture){0};
        r->dstGroup = bindGroup(
            k->dst == RHI_DST_READ_STORAGE ? g_rd.layoutDstStorage : g_rd.layoutDstAttach, &b, 1);
        r->dstGroupSerial = r->passSerial;
    }
    rhi_cmd_set_bind_group(s_cl, 3, r->dstGroup);
}

static RhiSampler gsLodSampler(const Replay *r, const DrawSetup *ds)
{
    const uint32_t mmin = RD_TEX1_MMIN(r->st.tex1Lod);
    const int set = (mmin == 3u || mmin == 5u) && !rd__gs_filter_int(&r->st)
                        ? RD_SAMPLER_SET_MIP_LINEAR
                        : RD_SAMPLER_SET_MIP_NEAREST;
    const int i = (r->st.ds.magFilter ? 1 : 0) | (r->st.ds.minFilter ? 2 : 0) |
                  (drawWrap(r, ds, (RdWrap)r->st.ds.wrap.s, 0) ? 4 : 0) |
                  (drawWrap(r, ds, (RdWrap)r->st.ds.wrap.t, 1) ? 8 : 0);
    return g_rd.samplers[set * RD_SAMPLER_COUNT + i];
}

/* DrawCB.blend[3] for a draw with GS levels: the state's TEX1 mipmap
 * fields (rd_tex1_lod) with MXL no further than the levels the texture
 * has, and none at all (level 0) when TEX1 asks for no mipmapping. */
static uint32_t gsLodWord(const Replay *r, const DrawSetup *ds)
{
    uint32_t p = r->st.tex1Lod;
    uint32_t mxl = RD_TEX1_MXL(p);
    if (!rd_tex1_mipmapped(p)) {
        mxl = 0;
    } else if (mxl > (uint32_t)ds->gsLevels - 1u) {
        mxl = (uint32_t)ds->gsLevels - 1u;
    }
    return (p & ~(7u << 3)) | (mxl << 3);
}

/* font_sheet_ps's style in DrawCB.param (the rim's weight, rimLevel,
 * fillLevel, dither), the sheet texture's at replay time, and the strip's
 * scale above the dither bit (dither | (scale - 1) << 1, shader_consts.h
 * ICO_SHEET_SCALE_MAX), so a 1x sheet's param[3] is the dither alone. */
static void sheetParam(const RdTexRec *t, float param[4])
{
    if (!t || t->kind != RD_TEXKIND_IMAGE || t->format != RD_TEXEL_SHEET) {
        return;
    }
    for (int k = 0; k < 4; k++) {
        param[k] = (float)t->sheet[k];
    }
    if (t->sheetScale > 1) {
        param[3] += (float)((t->sheetScale - 1) << 1);
    }
}

/* Above 1x (common.hlsli DF_ABOVE1X): a pass of sprite_ps, sprite_aa1_ps
 * or sprite_stq_ps whose texture is a colour view of a target, both filters
 * linear, no GS levels, the texture's scale not the bound target's: the
 * shadow levels (scale 1) composited into SCENE, the softening's SCENE into
 * AA0, the reduction's fallback sprite when an axis leaves the GS's filter
 * (a1xGs); never a work buffer.  The shadow chain's
 * first level reads the count already box-averaged to its GS size
 * (shadowReduce), at scale 1 as the levels are.  The scales are equal for
 * every target at scale 1 but DISPLAY's rows at full height (sy 2), which
 * only the reduction reads into, one SCENE row a DISPLAY row (a1xGs), so
 * never at scale 1. */
static int spriteAbove1x(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds)
{
    const RdDrawState *d = &r->st.ds;
    if (r->a1xGs || !ds->textured || !d->texEnabled || ds->gsLevels || ds->replacement || !ds->tc ||
        d->magFilter != RD_FILTER_LINEAR || d->minFilter != RD_FILTER_LINEAR ||
        (dp->key.fs != RD_FS_SPRITE && dp->key.fs != RD_FS_SPRITE_AA1 &&
         dp->key.fs != RD_FS_SPRITE_STQ && dp->key.fs != RD_FS_SPRITE_PLANES &&
         dp->key.fs != RD_FS_SPRITE_STQ_PLANES)) {
        return 0;
    }
    const RdTexRec *t = rd__tex_rec(r->st.tex);
    if (!t || t->kind != RD_TEXKIND_TARGET || t->view == RD_VIEW_DEPTH) {
        return 0;
    }
    const RdTargetRec *src = rd__target_rec(t->target);
    if (!src || rd__work_class(src)) {
        return 0;
    }
    /* the shadow count at its GS size (resolveTexture) is at scale 1 */
    const int red = s_shadowRed.id && ds->tex.id == s_shadowRed.id;
    return (red ? 1.0f : src->sx) != ds->tc->sx || (red ? 1.0f : src->sy) != ds->tc->sy;
}

/* DrawCB for one planned pass: sprite_ps and vu_ps read the same fields. */
static void fillDrawCB(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds, IcoDrawCB *cb)
{
    memset(cb, 0, sizeof(*cb));
    cb->mode[0] = dp->flags;
    if (ds->textured) {
        cb->mode[0] |= ICO_DF_TEXTURED;
        if (r->st.ds.texFn == RD_TEXFN_DECAL) {
            cb->mode[0] |= ICO_DF_DECAL;
        }
        if (r->st.ds.tcc == RD_TCC_RGBA) {
            cb->mode[0] |= ICO_DF_TCC_RGBA;
        }
    }
    cb->mode[1] = r->st.ds.texa | (ds->tfmt << 8);
    /* GS levels filtered as the GS filters (rd__gs_filter_int): per texel
     * where TEXA changes the texels first (rd__texa_per_texel: the TEXA
     * entries, and sprite_stq_ps through the same four-tap filter), else
     * the sampler on the GS's grid (common.hlsli gs_sample_gs_bilinear) */
    const int gsInt = ds->textured && ds->gsLevels && rd__gs_filter_int(&r->st);
    const int fs = dp->key.fs;
    const int stqTexa = (fs == RD_FS_SPRITE_STQ || fs == RD_FS_SPRITE_STQ_PLANES) &&
                        rd__gs_filter_int(&r->st) && rd__texa_per_texel(&r->st);
    const int perTexel =
        fs == RD_FS_SPRITE_TEXA || fs == RD_FS_VU_TEXA || fs == RD_FS_SPRITE_TEXA_PLANES || stqTexa;
    if (perTexel) {
        /* the sampler state the TEXA shaders' four-tap filter reproduces
         * (and sprite_stq_ps's, for GS levels under such a TEXA);
         * minified pixels of a texture texSampler upgrades (the Enhanced
         * trilinear or anisotropic filter) keep the sampler */
        const RdDrawState *d = &r->st.ds;
        cb->mode[0] |= (d->magFilter == RD_FILTER_LINEAR ? ICO_DF_TEXA_MAG_LINEAR : 0u) |
                       (d->minFilter == RD_FILTER_LINEAR ? ICO_DF_TEXA_MIN_LINEAR : 0u) |
                       (d->wrap.s != RD_WRAP_REPEAT ? ICO_DF_TEXA_CLAMP_S : 0u) |
                       (d->wrap.t != RD_WRAP_REPEAT ? ICO_DF_TEXA_CLAMP_T : 0u);
        if (!ds->gsLevels &&
            texSamplerUpgraded((RdFilter)d->minFilter, ds->mipmapped, ds->replacement)) {
            cb->mode[0] |= ICO_DF_TEXA_MIN_SAMPLED;
        }
    }
    if (ds->textured && ds->gsLevels) {
        cb->mode[0] |= ICO_DF_GS_LOD;
        cb->blend[3] = gsLodWord(r, ds);
    }
    if (gsInt && !perTexel) {
        static bool logged;
        cb->mode[0] |= ICO_DF_GS_BILINEAR_GRID;
        g_rdPerf.gridDraws++;
        if (!logged) {
            logged = true;
            rd__log("GS levels (Original): a texture without TEXA per texel is sampled on the "
                    "GS's 12.4 grid of each level, nearest between levels, the two levels "
                    "blended in 256ths (reported once)");
        }
    }
    if (perTexel && ds->textured) {
        g_rdPerf.texaDraws++;
    }
    if (ds->textured && !ds->replacement && r->st.ds.magFilter == RD_FILTER_NEAREST &&
        r->st.ds.minFilter == RD_FILTER_NEAREST) {
        /* nearest both ways (on the one-level sampler texSampler binds, or
         * at the GS's level): the GS's texel at a texel boundary
         * (common.hlsli gs_nearest_texel), not whichever side the
         * interpolated UV's rounding falls on, which depends on the pixel's
         * place in the target (a 4:3 column of a wide frame took other
         * texels than the 4:3 frame).  At the GS's level the shader loads
         * the texels itself (gs_sample_lod_nearest), wrapped as the sampler
         * (gsLodSampler) wraps */
        cb->mode[0] |= ICO_DF_NEAREST;
        if (ds->gsLevels) {
            cb->mode[0] |=
                (drawWrap(r, ds, (RdWrap)r->st.ds.wrap.s, 0) ? ICO_DF_TEXA_CLAMP_S : 0u) |
                (drawWrap(r, ds, (RdWrap)r->st.ds.wrap.t, 1) ? ICO_DF_TEXA_CLAMP_T : 0u);
        }
    }
    if (spriteAbove1x(r, dp, ds)) {
        /* above 1x: the step's kernel (sprite.hlsl sprite_above1x), the
         * wrap its loads reproduce */
        cb->mode[0] |=
            ICO_DF_ABOVE1X |
            (drawWrap(r, ds, (RdWrap)r->st.ds.wrap.s, 0) != RD_WRAP_REPEAT ? ICO_DF_TEXA_CLAMP_S
                                                                           : 0u) |
            (drawWrap(r, ds, (RdWrap)r->st.ds.wrap.t, 1) != RD_WRAP_REPEAT ? ICO_DF_TEXA_CLAMP_T
                                                                           : 0u);
        g_rd.above1xDraws++;
    }
    cb->mode[2] = dp->modeZ;
    cb->mode[3] = dp->aref;
    /* a pass the storage pass merge draws through the destination read
     * without GS blending: Cs (rd__dst_merge) */
    cb->blend[0] = dp->csOnly ? 0x00u : rd__alpha_register(r->st.ds.blend);
    cb->blend[1] = dp->fix;
    cb->blend[2] = r->st.ds.colclamp;
    cb->tex[0] = (float)ds->tw;
    cb->tex[1] = (float)ds->th;
    cb->tex[2] = 1.0f / (float)ds->tw;
    cb->tex[3] = 1.0f / (float)ds->th;
    if (ds->texCrop) {
        /* Full pixel: a GS texel u of the target is texel u sx - ox of its
         * texture (common.hlsli gs_uv_norm) */
        cb->tex[2] = ds->texCrop->sx / (float)ds->texCrop->tw;
        cb->scale[2] = ds->texCrop->ox / (float)ds->texCrop->tw;
    }
    if (dp->key.fs == RD_FS_FONT_SHEET) {
        sheetParam(rd__tex_rec(r->st.tex), cb->param);
    }
    rd__fill_draw_cb_extra(r, dp, ds, cb); /* rd_pixel_plan.c */
}

/* -------------------------------------------------------------- PRIM.AA1
 * The model is PCSX2's software renderer's
 * (GSRasterizer.cpp DrawEdgeLine / DrawEdgeTriangle, GSDrawScanline.cpp):
 * a line or triangle with PRIM.AA1 has edge pixels, which carry a coverage
 * and write no Z; sprite_aa1_ps turns the coverage into the alpha.
 *   a line       is all edge: per step along its major axis the two pixels
 *                nearest it on the minor axis, coverage 1 - d (d the
 *                distance along the minor axis from the pixel's sample
 *                point to the line, in GS pixels): drawn as the line
 *                widened by one GS pixel to each side of the minor axis,
 *                coverage 1 on the line and 0 at the far sides
 *   a triangle   is drawn as without AA1 (its interior pixels), and each of
 *                its three edges gets the pixels outside it whose sample
 *                point lies within one GS pixel of it along its minor axis,
 *                coverage 1 - d again: a one-pixel fringe; every triangle
 *                on its own, as the GS draws a strip's triangles one by one
 * The coverage is the vertex's IcoSpriteAa1Vertex.cov, interpolated without
 * perspective: ICO_AA1_INTERIOR on a triangle's own vertices. */
static int aa1Prim(uint8_t prim)
{
    return prim == RD_PRIM_LINES || prim == RD_PRIM_LINE_STRIP || prim == RD_PRIM_TRIANGLES ||
           prim == RD_PRIM_TRIANGLE_STRIP || prim == RD_PRIM_TRIANGLE_FAN;
}

static IcoSpriteAa1Vertex *s_ax;

static uint32_t s_axCap;

static IcoSpriteAa1Vertex *axScratch(uint64_t n)
{
    if (n > s_axCap) {
        const uint32_t cap = (uint32_t)(n < 4096 ? 4096 : n * 2);
        IcoSpriteAa1Vertex *p = realloc(s_ax, (size_t)cap * sizeof(*p));
        if (!p) {
            return NULL;
        }
        s_ax = p;
        s_axCap = cap;
    }
    return s_ax;
}

static IcoSpriteAa1Vertex aa1Vtx(const IcoSpriteVertex *v, int32_t mx, int32_t my, float cov)
{
    IcoSpriteAa1Vertex o;
    const int32_t x = (int32_t)v->x + mx, y = (int32_t)v->y + my;
    o.v = *v;
    o.v.x = (uint16_t)(x < 0 ? 0 : (x > 0xFFFF ? 0xFFFF : x));
    o.v.y = (uint16_t)(y < 0 ? 0 : (y > 0xFFFF ? 0xFFFF : y));
    o.cov = cov;
    return o;
}

/* the edge p-q widened by (mx, my) (one GS pixel along its minor axis, in
 * 12.4): coverage 1 on the edge, 0 on the far side; attributes the edge's */
static uint32_t aa1Band(const IcoSpriteVertex *p, const IcoSpriteVertex *q, int32_t mx, int32_t my,
                        IcoSpriteAa1Vertex *o)
{
    const IcoSpriteAa1Vertex p0 = aa1Vtx(p, 0, 0, 1.0f), q0 = aa1Vtx(q, 0, 0, 1.0f);
    const IcoSpriteAa1Vertex p1 = aa1Vtx(p, mx, my, 0.0f), q1 = aa1Vtx(q, mx, my, 0.0f);
    o[0] = p0;
    o[1] = q0;
    o[2] = q1;
    o[3] = p0;
    o[4] = q1;
    o[5] = p1;
    return 6;
}

/* the fringe of edge p-q of a triangle whose third vertex is t: outside
 * the edge along its minor axis; none for a degenerate triangle */
static uint32_t aa1Fringe(const IcoSpriteVertex *p, const IcoSpriteVertex *q,
                          const IcoSpriteVertex *t, IcoSpriteAa1Vertex *o)
{
    const int64_t dx = (int64_t)q->x - p->x, dy = (int64_t)q->y - p->y;
    const int64_t cross = dx * ((int64_t)t->y - p->y) - dy * ((int64_t)t->x - p->x);
    if (cross == 0) {
        return 0;
    }
    const int sc = cross > 0 ? 1 : -1;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        return aa1Band(p, q, 0, -16 * sc * (dx > 0 ? 1 : -1), o); /* x-major: up or down */
    }
    return aa1Band(p, q, 16 * sc * (dy > 0 ? 1 : -1), 0, o); /* y-major: left or right */
}

/* expand()'s output (line pairs or triangles) as AA1 geometry; returns the
 * vertex count.  split: the triangles' own vertices first (*nInterior of
 * them) and every fringe after, for a draw whose fringes need Z write off
 * (two draws); else triangle by triangle, the GS's order, *nInterior = the
 * total (one draw).  Lines are all fringe: *nInterior = 0. */
static uint32_t aa1Expand(const IcoSpriteVertex *in, uint32_t n, uint8_t topo, int split,
                          IcoSpriteAa1Vertex *o, uint32_t *nInterior)
{
    uint32_t k = 0;
    if (topo == RD_PRIM_LINES) {
        for (uint32_t i = 0; i + 1 < n; i += 2) {
            const IcoSpriteVertex *a = &in[i], *b = &in[i + 1];
            const int32_t dx = (int32_t)b->x - a->x, dy = (int32_t)b->y - a->y;
            const int xMajor = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy);
            if (dx == 0 && dy == 0) {
                continue;
            }
            k += aa1Band(a, b, xMajor ? 0 : -16, xMajor ? -16 : 0, &o[k]);
            k += aa1Band(a, b, xMajor ? 0 : 16, xMajor ? 16 : 0, &o[k]);
        }
        *nInterior = 0;
        return k;
    }
    for (uint32_t i = 0; i + 2 < n; i += 3) {
        for (int j = 0; j < 3; j++) {
            o[k++] = aa1Vtx(&in[i + j], 0, 0, ICO_AA1_INTERIOR);
        }
        if (!split) {
            k += aa1Fringe(&in[i], &in[i + 1], &in[i + 2], &o[k]);
            k += aa1Fringe(&in[i + 1], &in[i + 2], &in[i], &o[k]);
            k += aa1Fringe(&in[i + 2], &in[i], &in[i + 1], &o[k]);
        }
    }
    *nInterior = k;
    if (split) {
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            k += aa1Fringe(&in[i], &in[i + 1], &in[i + 2], &o[k]);
            k += aa1Fringe(&in[i + 1], &in[i + 2], &in[i], &o[k]);
            k += aa1Fringe(&in[i + 2], &in[i], &in[i + 1], &o[k]);
        }
    }
    return k;
}

/* Perspective-correct STQ.  A triangle command with texture
 * coordinates in STQ (not uvFixed) whose vertices all have Q > 0 and some
 * Q != 1 takes the STQ shaders: Q goes with the vertex and the pixel shader
 * divides.  qOrder gives each of expand()'s output vertices its source
 * vertex's Q (same order) and returns their count, 0 for a command that
 * keeps the per-vertex divide. */
static float *s_qv;

static uint32_t s_qvCap;

static uint32_t qOrder(const RdScreenVtx *v, uint32_t n, uint8_t prim, float **q)
{
    if (prim != RD_PRIM_TRIANGLES && prim != RD_PRIM_TRIANGLE_STRIP &&
        prim != RD_PRIM_TRIANGLE_FAN) {
        return 0;
    }
    int differs = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!(v[i].q > 0.0f) || !isfinite(v[i].q)) {
            return 0;
        }
        differs |= v[i].q != 1.0f;
    }
    if (!differs) {
        return 0;
    }
    const uint64_t cap = (uint64_t)n * 3;
    if (cap > s_qvCap) {
        float *p = realloc(s_qv, (size_t)cap * sizeof(*p));
        if (!p) {
            return 0;
        }
        s_qv = p;
        s_qvCap = (uint32_t)cap;
    }
    uint32_t k = 0;
    if (prim == RD_PRIM_TRIANGLES) {
        for (uint32_t i = 0; i + 2 < n; i += 3) {
            for (uint32_t j = 0; j < 3; j++) {
                s_qv[k++] = v[i + j].q;
            }
        }
    } else if (prim == RD_PRIM_TRIANGLE_STRIP) {
        for (uint32_t i = 0; i + 2 < n; i++) {
            for (uint32_t j = 0; j < 3; j++) {
                s_qv[k++] = v[i + j].q;
            }
        }
    } else {
        for (uint32_t i = 1; i + 1 < n; i++) {
            s_qv[k++] = v[0].q;
            s_qv[k++] = v[i].q;
            s_qv[k++] = v[i + 1].q;
        }
    }
    *q = s_qv;
    return k;
}

static void doScreenWrap(Replay *r, const RdFrame *f, const RdCmd *c);

/* the staged vertices of the open run and the STQ vertices of one command
 * (local, copied into the ring whole, as convVtx explains) */
static uint8_t *s_runVx;

static uint64_t s_runCap;

static IcoSpriteStqVertex *s_sq;

static uint32_t s_sqCap;

static bool s_screenMerge = true;

void rd__set_screen_merge(bool on)
{
    s_screenMerge = on;
}

/* the open run's one draw */
static void flushScreenRun(Replay *r)
{
    ScreenRun *q = &r->run;
    if (!q->count) {
        return;
    }
    const uint64_t bytes = (uint64_t)q->count * q->stride;
    const uint64_t off = rd__ring_alloc(bytes, 16);
    if (off != ~0ull) {
        memcpy(g_rd.ringMap[s_slot] + off, s_runVx, (size_t)bytes);
        /* the open pass is the kind the run was recorded in (doScreen
         * switches before it records a run of the other kind) */
        r->sc = q->sc;
        rhi_cmd_set_scissor(s_cl, &q->sc);
        rhi_cmd_set_vertex_buffer(s_cl, 0, g_rd.ring[s_slot], off);
        rd__set_pipeline(s_cl, q->pipe, &q->key);
        rd__bind_uniform(s_cl, 0, q->frame);
        rd__bind_uniform(s_cl, 1, rd__draw_group(&q->cb));
        rhi_cmd_set_bind_group(s_cl, 2, q->g2);
        dstBind(r, &q->key);
        drawCmd(s_cl, q->count, 0, 1);
        if (q->passes == 2 && q->pipe2.id) {
            /* the two-pass fallback's alpha pass over the same vertices */
            rd__set_pipeline(s_cl, q->pipe2, &q->key2);
            rd__bind_uniform(s_cl, 0, q->frame);
            rd__bind_uniform(s_cl, 1, rd__draw_group(&q->cb2));
            rhi_cmd_set_bind_group(s_cl, 2, q->g2);
            dstBind(r, &q->key2);
            drawCmd(s_cl, q->count, 0, 1);
        }
        g_rdPerf.screenDraws++; /* one draw of the run, however many passes */
    }
    q->count = 0;
    if (r->perfPending) {
        r->perfPending = 0;
        rd__perf_post(s_cl, r->perfPendingPost);
    }
}

/* The GPU work recorded from here on belongs to effect post (RD_PERF_POST_*,
   -1 none); with a screen-prim run open, from its draw on.  Timestamps only:
   nothing drawn changes. */
static void perfPost(Replay *r, int post)
{
    if (r->run.count) {
        r->perfPending = 1;
        r->perfPendingPost = post;
        return;
    }
    r->perfPending = 0;
    rd__perf_post(s_cl, post);
}

/* The field's half pixel on SCENE (the old bridge's sea).  The
 * frame head's draw environment carries the flip's half offset (GsBase.c
 * sceGsSetHalfOffset, RD_TARGET_HALF_Y): XYOFFSET.y + 0.5 pixel for the
 * scenery drawn first.  The shadow pass sets SCENE again with
 * gif_SetDrawEnviroment(useoffset = 1) (GifPacket.c), whose XYOFFSET is the
 * screen offset alone: the game's own register write drops the field's half
 * offset for every later list (on a PS2 in one field of two).  Two surfaces
 * drawn on either side of that write, the sea's grey layer umi (list 2,
 * writing Z) and the wave layers over it (list 5, testing it), stand half
 * a pixel apart on the screen, and on that grazing water half a pixel is
 * some 800 GS Z units, more than the 5 units between them: umi won over the
 * waves in a band that shifted with every sub-pixel step of the camera.  So
 * a SCENE target set with the screen offset (RD_TARGET_OFFSET) in lists 0
 * to 10 takes the half offset the frame head's SCENE target has: every
 * later list moves half a pixel to match the first ones.  At replay, so the
 * dumps recorded before show it too.  Targets set without the screen offset
 * (the full-target passes: the shadow maps, the aura's copies into SCENE)
 * and the UI's lists 11 and 12 keep what they recorded.
 *
 * The half pixel moves geometry only.  A screen-space copy of a render
 * target keeps the plain offset its target was recorded with (drawOffset,
 * DrawSetup.screenCopy): the fog sprite (list 4), which reads the depth
 * texel under each pixel by its UVs, and its full-screen fogOffsetA sheet;
 * the softening's composite (list 10) and the shadow pass's composite
 * (list 3), sprites that sample a target; the post sprites
 * (doBlurSpriteDraw).  Moved half a pixel, their lookups fall half a texel
 * off the target's grid: at 2x the fog's edge stood one row above the
 * geometry's, two rows at 4x.  On a PS2 these passes have no half offset
 * either.  VU draws (meshes, skinned, grids, particles),
 * shadow strips, and screen triangles and lines take the half pixel even
 * when they sample a target: their positions are projected geometry. */
static uint32_t sceneHeadHalf(const RdFrame *f)
{
    const uint32_t scene = rd_target(RD_TARGET_SCENE).id;
    const RdCmdList *l0 = &f->lists[0];
    for (uint32_t i = 0; i < l0->count; i++) {
        const RdCmd *c = &l0->cmds[i];
        if (c->type == RDC_TARGET && c->u[0] == scene) {
            return c->b[0] & RD_TARGET_HALF_Y;
        }
    }
    return 0;
}

static int sceneKeepsHalf(RdStateBlock *st, int list, uint32_t headHalf)
{
    if (headHalf && list <= 10 && st->color == rd_target(RD_TARGET_SCENE).id &&
        (st->useOffset & RD_TARGET_OFFSET) && !(st->useOffset & RD_TARGET_HALF_Y)) {
        st->useOffset |= RD_TARGET_HALF_Y;
        return 1;
    }
    return 0;
}

uint32_t rd__replay_scene_head_half(const RdFrame *f)
{
    return sceneHeadHalf(f);
}

int rd__replay_scene_keeps_half(RdStateBlock *st, int list, uint32_t headHalf)
{
    return sceneKeepsHalf(st, list, headHalf);
}

/* The effect a TARGET command's pass belongs to by its colour target: the
   screen softening's AA0 and AA1 and its composite (the pass into SCENE
   that follows them, rd_post.c AA_COMPOSITE), the aura's own targets;
   otherwise the effect the next post sprite names, or none */
static int perfTargetPost(const Replay *r, const RdCmd *c)
{
    const uint32_t col = c->u[0];
    const uint32_t aa0 = rd_target(RD_TARGET_AA0).id, aa1 = rd_target(RD_TARGET_AA1).id;
    if (col == aa0 || col == aa1 ||
        (col == rd_target(RD_TARGET_SCENE).id && (r->st.color == aa0 || r->st.color == aa1))) {
        return RD_PERF_POST_SOFTEN;
    }
    if (col == rd_target(RD_TARGET_AURA_WORK).id || col == rd_target(RD_TARGET_AURA_TAP).id ||
        col == rd_target(RD_TARGET_FEED128).id) {
        return RD_PERF_POST_AURA;
    }
    return -1;
}

/* whether a command drawing with these bindings joins the open run: the
 * same pass, prim type, vertex format, pipeline (key), FrameCB, DrawCB,
 * texture, sampler and DATE snapshot, scissor and state block, and nothing
 * but the run's own draws changed the write serial (a DATE draw after an
 * alpha write in the run retook the snapshot, which flushed the run) */
static bool joinsRun(const Replay *r, const ScreenRun *n)
{
    const ScreenRun *q = &r->run;
    return q->count && s_screenMerge && r->passOpen && q->passSerial == r->passSerial &&
           q->writeSerial == r->writeSerial && q->prim == n->prim && q->stride == n->stride &&
           q->pipe.id == n->pipe.id && memcmp(&q->key, &n->key, sizeof(q->key)) == 0 &&
           q->passes == n->passes && memcmp(&q->key2, &n->key2, sizeof(q->key2)) == 0 &&
           q->frame.group.id == n->frame.group.id && q->frame.offset == n->frame.offset &&
           memcmp(&q->cb, &n->cb, sizeof(q->cb)) == 0 && q->g2.id == n->g2.id && q->tex == n->tex &&
           q->sampler == n->sampler && q->dateTex == n->dateTex &&
           (r->st.ds.test.date == RD_DATE_OFF ||
            (q->dateSerial == r->dateSerial && q->dateFor == r->dateFor)) &&
           memcmp(&q->sc, &n->sc, sizeof(q->sc)) == 0 && memcmp(&q->st, &r->st, sizeof(q->st)) == 0;
}

/* One planned screen pass as the device draws it
 * (rd__expand_no_dual: itself, or the two-pass fallback's colour and alpha
 * passes), count vertices from first of the bound vertex buffer; count 0
 * binds nothing.  False when no pipeline of it could be made. */
static bool drawExpanded(Replay *r, const RdDrawPass *pass, const DrawSetup *ds, RhiBindGroup g2,
                         uint32_t count, uint32_t first)
{
    RdDrawPass ex[8];
    const int ne = expandPasses(r, pass, 1, ex); /* the exact blend, the two-pass fallback */
    bool any = false;
    for (int j = 0; j < ne; j++) {
        RhiPipeline p = rd__get_pipeline(&ex[j].key);
        if (!p.id) {
            continue;
        }
        any = true;
        if (count == 0) {
            continue;
        }
        IcoDrawCB cb;
        fillDrawCB(r, &ex[j], ds, &cb);
        dstPassMode(r, &ex[j].key); /* a storage pass for a storage destination read */
        rd__set_pipeline(s_cl, p, &ex[j].key);
        rd__bind_uniform(s_cl, 0, r->frameBG);
        rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(s_cl, 2, g2);
        dstBind(r, &ex[j].key);
        drawCmd(s_cl, count, first, 1);
    }
    return any;
}

/* The DATE snapshot only where a screen-prim command can draw: its box with
 * the full-screen, UI and mirror decisions doScreen makes after
 * prepareDraw.  False (the whole target) when the draw does not test DATE
 * or the area cannot be had. */
static bool dateAreaFor(const Replay *r, const RdCmd *c, const RdScreenVtx *v, uint32_t n,
                        RhiRect *area)
{
    const RdTargetRec *tcA = rd__target_rec(r->st.color);
    return r->st.ds.test.date != RD_DATE_OFF && tcA &&
           rd__screen_area(tcA, &r->st, v, n, mirrorUi(r, c->b[1]), r->st.aa1 && aa1Prim(c->b[0]),
                           area);
}

/* FrameCB of an expanded sprite in the MSAA epoch (msaaSpriteExpand): the
 * draw's, without the half texel */
static RdUniform msaaFrame(const Replay *r, const DrawSetup *ds)
{
    const RdTargetRec *tc = ds->tc;
    float ox = 2048.0f - (float)(r->st.gsW >> 1);
    float oy = 2048.0f - (float)(r->st.gsH >> 1);
    if (drawOffset(r, ds) & RD_TARGET_HALF_Y) {
        oy += 0.5f;
    }
    s_frameNoHalf = true;
    const RdUniform u = frameGroupOf(tc, ox, oy, rd__target_z_scale(ds->tdId));
    s_frameNoHalf = false;
    return u;
}

static void doScreen(Replay *r, const RdFrame *f, const RdCmd *c)
{
    DrawSetup ds;
    if (g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS) {
        flushScreenRun(r); /* a skipped text item ends the run */
        return;            /* the item is drawn on the output instead */
    }
    if (rd__wrap_applies(&r->st)) {
        flushScreenRun(r);
        if (r->st.aa1 && aa1Prim(c->b[0])) {
            rd__log_once(RD_ONCE_AA1_WRAP,
                         "PRIM.AA1 under COLCLAMP 0: drawn without edge coverage");
        }
        doScreenWrap(r, f, c); /* COLCLAMP 0 */
        return;
    }
    const uint32_t n = c->u[1];
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    v = extendFullFrame(r, rd__target_rec(r->st.color), v, n, c->b[0], c->b[1]);
    RhiRect dateArea;
    const RhiRect *dateAt = dateAreaFor(r, c, v, n, &dateArea) ? &dateArea : NULL;
    if (!prepareDrawEx(r, &ds, dateAt, true)) {
        return;
    }
    ds.screenCopy = screenCopy(r, &ds, c->b[0], c->b[1]);
    /* read only by drawWrap's clamp under the open border: not computed
     * with the border closed (the vertices' texel range is a loop over
     * every vertex of every screen command) */
    ds.repeatKept =
        borderOpen() ? rd__screen_repeat_kept(v, n, c->b[2], (float)ds.tw, (float)ds.th) : 0u;

    /* geometry */
    setUvShift(ds.tc->sx, ds.tc->sy);
    uint8_t topo;
    IcoSpriteVertex *out = vxScratch((uint64_t)n * 6);
    if (!out) {
        return;
    }
    const uint32_t nv = expand(v, n, c->b[0], c->b[2], (float)ds.tw, (float)ds.th, r->st.uvOffset,
                               out, &topo, r->st.gouraud != 0);
    if (nv == 0) {
        return;
    }
    if (mirrorUi(r, c->b[1])) {
        mirrorDraw(r, ds.tc, out, nv, topo); /* the mirror mode's UI flip */
    }
    /* PRIM.AA1 on a line or triangle command */
    const int aa1 = r->st.aa1 && aa1Prim(c->b[0]);
    RdDrawPass dp[2];
    int np = rd__plan_screen_draw_ex(&r->st, topo, aa1, c->b[1], ds.tc->format, ds.depthFmt, dp);
    RdAlphaRange as; /* the alpha/blend planner (rd_alpha_plan.c) */
    rd__alpha_range_of_draw(
        &r->st, &(RdAlphaSrc){RD_ALPHA_SRC_SCREEN, .f = f, .c = c, .v = v, .n = n}, &as);
    rd__plan_screen_draw_alpha(&r->st, as, dp, &np);
    if (np == 0) {
        return;
    }
    /* the vertices the draw reads (built locally, copied into the ring
     * whole when drawn) */
    const void *vb = out;
    uint32_t stride = sizeof(IcoSpriteVertex);
    /* a textured STQ triangle command with Q != 1 draws with
     * the STQ shaders, its vertices rewritten (24 bytes each) */
    if (!aa1 && !c->b[2] && ds.textured) {
        float *qv = NULL; /* set wherever nq != 0 (qOrder) */
        const uint32_t nq = qOrder(v, n, c->b[0], &qv);
        RdDrawPass sp[2];
        int stq = nq == nv;
        memcpy(sp, dp, sizeof(sp));
        for (int i = 0; i < np && stq; i++) {
            stq = rd__stq_pass(&sp[i]);
        }
        if (stq && nv > s_sqCap) {
            IcoSpriteStqVertex *p = realloc(s_sq, (size_t)nv * 2 * sizeof(*p));
            if (!p) {
                return;
            }
            s_sq = p;
            s_sqCap = nv * 2;
        }
        if (stq) {
            memcpy(dp, sp, sizeof(sp));
            for (uint32_t i = 0; i < nv; i++) {
                s_sq[i].v = out[i];
                s_sq[i].v.u *= qv[i]; /* S and T, not divided: out holds S / Q */
                s_sq[i].v.v *= qv[i];
                s_sq[i].q = qv[i];
            }
            vb = s_sq;
            stride = sizeof(IcoSpriteStqVertex);
        }
    }
    /* the exact viewport: a triangle command's triangles with their planes
     * (rd_screen_planes.c); sprites and points keep their axis-aligned path */
    if (!aa1 && s_vpExact && topo == RD_PRIM_TRIANGLES && c->b[0] != RD_PRIM_SPRITES &&
        c->b[0] != RD_PRIM_POINTS && nv % 3 == 0) {
        RdDrawPass pp[2];
        int planes = 1;
        memcpy(pp, dp, sizeof(pp));
        for (int i = 0; i < np && planes; i++) {
            planes = rd__plane_pass(&pp[i]);
        }
        const IcoSpritePlaneVertex *pv = planes ? rd__screen_planes(vb, stride, nv) : NULL;
        if (pv) {
            memcpy(dp, pp, sizeof(pp));
            vb = pv;
            stride = sizeof(IcoSpritePlaneVertex);
        }
    }
    uint32_t nDraw = nv, nFirst = nv;
    if (aa1) {
        int split = 0;
        for (int i = 0; i < np; i++) {
            split |= topo == RD_PRIM_TRIANGLES && dp[i].key.gs.zwrite == RD_ZWRITE_ON;
        }
        IcoSpriteAa1Vertex *ax = axScratch((uint64_t)nv * 7);
        if (!ax) {
            return;
        }
        nDraw = aa1Expand(out, nv, topo, split, ax, &nFirst);
        if (nDraw == 0) {
            return;
        }
        vb = ax;
        stride = sizeof(*ax);
    }
    ScreenRun nr;
    nr.g2 = bindDrawEx(r, &ds, &nr.sc, &nr.sampler, true);
    if (!nr.g2.id) {
        return;
    }
    /* the MSAA epoch: integer-aligned sprites cover whole pixels */
    const bool msExpand = g_rd.passSamples > 1 && vb == (const void *)out &&
                          msaaSpriteExpand(r, ds.tc, c->b[0], out, nv);
    const RdUniform frame = msExpand ? msaaFrame(r, &ds) : r->frameBG;

    /* one pass drawn whole joins a run (the AFAIL split and AA1's split
     * interior and fringes are two draws, which do not join a run: merged,
     * the second pass of one command would follow the first pass of the
     * next) */
    if (np == 1 && (!aa1 || nFirst == nDraw || nFirst == 0)) {
        RdDrawPass one = dp[0];
        if (aa1 && nFirst == 0) {
            /* AA1 lines: all fringe, drawn with Z write off */
            one.key.gs.zwrite = RD_ZWRITE_OFF;
        }
        /* the exact blend's pass and the two-pass fallback's colour and
         * alpha passes; a run draws its passes in one pass kind, so the
         * storage path's split (a storage colour pass and a depth pass
         * with the colour attached) draws below, as the split passes do */
        RdDrawPass ex[8];
        const int ne = expandPasses(r, &one, 1, ex);
        const bool runnable = ne == 1 || (ne == 2 && (ex[0].key.dst == RHI_DST_READ_STORAGE) ==
                                                         (ex[1].key.dst == RHI_DST_READ_STORAGE));
        if (!runnable) {
            goto immediate;
        }
        RhiPipeline p = rd__get_pipeline(&ex[0].key);
        if (!p.id) {
            return;
        }
        /* the open pass of the kind the run draws in (a run of the other
         * kind is drawn by the switch, in its own) */
        dstPassMode(r, &ex[0].key);
        /* a switch to the storage pass began a new pass: the draw's FrameCB
         * is the one the next command of the run finds (else every run
         * under the storage blend broke after its first command) */
        frameRefresh(r, &ds);
        nr.key = ex[0].key;
        nr.count = nDraw;
        nr.stride = stride;
        nr.prim = c->b[0];
        nr.pipe = p;
        nr.frame = msExpand ? frame : r->frameBG;
        fillDrawCB(r, &ex[0], &ds, &nr.cb);
        nr.passes = (uint8_t)ne;
        memset(&nr.key2, 0, sizeof(nr.key2));
        memset(&nr.cb2, 0, sizeof(nr.cb2));
        nr.pipe2 = (RhiPipeline){0};
        if (ne == 2) {
            nr.key2 = ex[1].key;
            nr.pipe2 = rd__get_pipeline(&ex[1].key);
            fillDrawCB(r, &ex[1], &ds, &nr.cb2);
        }
        nr.tex = ds.tex.id;
        nr.dateTex = ds.dateTex.id;
        if (!joinsRun(r, &nr)) {
            flushScreenRun(r);
            nr.count = 0;
            nr.passSerial = r->passSerial;
            nr.dateSerial = r->dateSerial;
            nr.dateFor = r->dateFor;
            memcpy(&nr.st, &r->st, sizeof(nr.st));
            r->run = nr;
        }
        ScreenRun *q = &r->run;
        const uint64_t need = ((uint64_t)q->count + nDraw) * stride;
        if (need > s_runCap) {
            const uint64_t cap = need < 65536 ? 65536 : need * 2;
            uint8_t *b = realloc(s_runVx, (size_t)cap);
            if (!b) {
                return;
            }
            s_runVx = b;
            s_runCap = cap;
        }
        memcpy(s_runVx + (uint64_t)q->count * stride, vb, (size_t)nDraw * stride);
        q->count += nDraw;
        g_rdPerf.screenCmds++;
        if (dp[0].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
        q->writeSerial = r->writeSerial;
        return;
    }

immediate:
    flushScreenRun(r);
    const uint64_t drawOff = rd__ring_alloc((uint64_t)nDraw * stride, 16);
    if (drawOff == ~0ull) {
        return;
    }
    memcpy(g_rd.ringMap[s_slot] + drawOff, vb, (size_t)nDraw * stride);
    r->sc = nr.sc; /* a pass begun again for the exact blend takes it back */
    rhi_cmd_set_scissor(s_cl, &nr.sc);
    rhi_cmd_set_vertex_buffer(s_cl, 0, g_rd.ring[s_slot], drawOff);
    const RdUniform frameKept = r->frameBG;
    r->frameBG = frame; /* drawExpanded binds it */
    for (int i = 0; i < np; i++) {
        if (!drawExpanded(r, &dp[i], &ds, nr.g2, (!aa1 || nFirst == nDraw) ? nDraw : nFirst, 0)) {
            continue;
        }
        if (!aa1 || nFirst == nDraw) {
            g_rdPerf.screenCmds++;
            g_rdPerf.screenDraws++;
        } else {
            /* AA1: the triangles with the pass's Z write, then
             * their fringes without (edge pixels write no Z) */
            if (nFirst) {
                g_rdPerf.screenCmds++;
                g_rdPerf.screenDraws++;
            }
            RdDrawPass ep = dp[i];
            ep.key.gs.zwrite = RD_ZWRITE_OFF;
            if (drawExpanded(r, &ep, &ds, nr.g2, nDraw - nFirst, nFirst)) {
                g_rdPerf.screenCmds++;
                g_rdPerf.screenDraws++;
            }
        }
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
    }
    r->frameBG = frameKept;
}

/* ------------------------------------------------ the presentation overlay
 * One rd_overlay_prims batch (rd.h; rd_present.c overlayRecord opens the
 * pass on the output and makes FrameCB): expanded as a screen-prim command
 * (sprites to quads, uvFixed texels), no sprite snapping or UV shift (the
 * output has no GS pixel grid), never mirrored, drawn with sprite_ui_vs /
 * sprite_ps under rd__overlay_state's block: the blend given, no Z, no
 * alpha test, no DATE, MODULATE with TCC RGBA when textured. */
void rd__overlay_draw(RhiCommandList cl, RhiFormat fmt, RdUniform frame, uint8_t prim,
                      const RdScreenVtx *v, uint32_t n, uint32_t tex, uint8_t blend)
{
    RhiTexture t = g_rd.dummy;
    uint32_t tw = 1, th = 1, tfmt = 0;
    int textured = 0;
    if (tex) {
        const RdTexRec *r = rd__tex_rec(tex);
        if (!r || r->kind != RD_TEXKIND_IMAGE || !r->rhi.id || r->state != RHI_STATE_SHADER_READ) {
            rd__log_once(RD_ONCE_BAD_TEX, "overlay prims with an unknown or unready texture");
            return;
        }
        t = r->rhi;
        tw = r->w;
        th = r->h;
        if (r->replacement && r->uvW && r->uvH) {
            /* a texture pack's replacement: the UVs address the original's
             * texels (replacementUv's rule) */
            tw = r->uvW;
            th = r->uvH;
        }
        tfmt = r->src;
        textured = 1;
    }
    setUvShift(1.0f, 1.0f);
    IcoSpriteVertex *out = vxScratch((uint64_t)n * 6);
    if (!out) {
        return;
    }
    static const float noOff[2] = {0.0f, 0.0f};
    uint8_t topo;
    const uint32_t nv = expand(v, n, prim, 1, (float)tw, (float)th, noOff, out, &topo, 1);
    if (nv == 0) {
        return;
    }
    const uint64_t vOff = rd__ring_alloc((uint64_t)nv * sizeof(*out), 16);
    if (vOff == ~0ull) {
        return;
    }
    memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(*out));
    RdStateBlock st;
    rd__overlay_state(&st, blend);
    st.tex = tex; /* the planner picks font_ps for an R8 coverage texture */
    st.ds.texEnabled = (uint8_t)textured;
    RdDrawPass pl[2], dp[4];
    /* the two-pass fallback's passes */
    const int np = rd__expand_no_dual(
        pl, rd__plan_screen_draw(&st, topo, RD_SPACE_UI, fmt, RHI_FMT_UNKNOWN, pl), dp);
    const RhiBindGroup g2 = rd__tex_group(
        t, rd__sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP));
    rhi_cmd_set_vertex_buffer(cl, 0, g_rd.ring[s_slot], vOff);
    for (int i = 0; i < np; i++) {
        RhiPipeline p = rd__get_pipeline(&dp[i].key);
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.mode[0] = dp[i].flags | (textured ? ICO_DF_TEXTURED | ICO_DF_TCC_RGBA : 0u);
        cb.mode[1] = st.ds.texa | (tfmt << 8);
        cb.mode[2] = dp[i].modeZ;
        cb.mode[3] = dp[i].aref;
        cb.blend[0] = rd__alpha_register(st.ds.blend);
        cb.blend[1] = dp[i].fix;
        cb.blend[2] = st.ds.colclamp;
        cb.tex[0] = (float)tw;
        cb.tex[1] = (float)th;
        cb.tex[2] = 1.0f / (float)tw;
        cb.tex[3] = 1.0f / (float)th;
        if (dp[i].key.fs == RD_FS_FONT_SHEET) {
            sheetParam(rd__tex_rec(tex), cb.param);
        }
        rd__set_pipeline(cl, p, &dp[i].key);
        rd__bind_uniform(cl, 0, frame);
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2, g2);
        drawCmd(cl, nv, 0, 1);
    }
}

/* ------------------------------------------------------- COLCLAMP 0 wrap
 * A screen-prim command whose blend result the GS masks to 8 bits instead of
 * clamping it (COLCLAMP 0) under an equation that adds or subtracts a term of
 * the source alone, Cs * F + Cd or Cd - Cs * F (ALPHA modes 0, 5 and 1, 6):
 * the result is Cd plus the sum of the fragments' terms, modulo 256, whatever
 * their order.  darkVolume.c's count buffer is the user (Shadow.c's count
 * has its own stencil path).  Two passes:
 *
 *   1. the prims through the state's vertex shader and wrap_acc_ps into an
 *      RGBA16F accumulator of the target's size, cleared to 0, with the bound
 *      depth target (Z test and Z write as the state has them): colour
 *      ONE + ONE of each term reduced to -128..127, alpha ONE / ZERO of
 *      As + 1 (the last fragment's As, as the GS stores it);
 *   2. wrap_resolve_ps over the target: (Cd + acc) mod 256, A from the
 *      accumulator where written, Cd read from a copy taken before pass 1,
 *      under the state's colour mask and scissor.
 *
 * Exact while no pixel takes more than 16 fragments of one command (|sum| <=
 * 2048, where half floats hold every integer).  Not modelled (logged once,
 * drawn by the clamping path): DATE, PABE, an alpha test with an AFAIL
 * other than KEEP, a non-RGBA8 target. */

static RhiTexture s_wrapAcc;

static RhiState s_wrapAccState;

static uint32_t s_wrapW, s_wrapH;

typedef struct WrapPipe {
    uint32_t key;
    RhiPipeline pipe;
} WrapPipe;

static WrapPipe s_wrapPipes[RD_WRAP_PIPES];

static uint32_t s_wrapPipeCount;

static int wrapEquation(const RdStateBlock *s)
{
    /* +1 add, -1 subtract, 0 none */
    if (!s->ds.abe || s->ds.colclamp) {
        return 0;
    }
    switch (s->ds.blend) {
    case RD_BLEND_CS_FIX_ADD_CD:
    case RD_BLEND_CS_AS_ADD_CD:
        return 1;
    case RD_BLEND_CD_SUB_CS_FIX:
    case RD_BLEND_CD_SUB_CS_AS:
        return -1;
    default:
        return 0;
    }
}

bool rd__wrap_applies(const RdStateBlock *s)
{
    if (wrapEquation(s) == 0) {
        return false;
    }
    const int ate = s->ds.test.ate && s->ds.test.atst != RD_ATST_ALWAYS;
    if (s->ds.test.date != RD_DATE_OFF || s->ds.pabe ||
        (ate && s->ds.test.afail != RD_AFAIL_KEEP)) {
        rd__log_once(RD_ONCE_WRAP, "COLCLAMP 0 draw with DATE, PABE or an AFAIL split: clamped");
        return false;
    }
    return true;
}

uint32_t rd__wrap_pipeline_count(void)
{
    return s_wrapPipeCount;
}

void rd__wrap_shutdown(void)
{
    for (uint32_t i = 0; i < s_wrapPipeCount; i++) {
        rhi_destroy_pipeline(s_wrapPipes[i].pipe);
    }
    s_wrapPipeCount = 0;
    if (s_wrapAcc.id) {
        rhi_destroy_texture(s_wrapAcc);
    }
    s_wrapAcc = (RhiTexture){0};
    s_wrapW = s_wrapH = 0;
}

/* The accumulation pipeline (resolve = 0: vs, prim, depth format, Z test, Z
 * write) or the resolve pipeline (resolve = 1: colour format, colour mask). */
static RhiPipeline wrapPipeline(int resolve, uint8_t vs, uint8_t prim, RhiFormat fmt, uint8_t ztst,
                                uint8_t zwrite, uint8_t mask)
{
    const uint32_t key = (uint32_t)resolve | (uint32_t)vs << 1 | (uint32_t)prim << 6 |
                         (uint32_t)fmt << 10 | (uint32_t)ztst << 16 | (uint32_t)zwrite << 20 |
                         (uint32_t)mask << 24;
    for (uint32_t i = 0; i < s_wrapPipeCount; i++) {
        if (s_wrapPipes[i].key == key) {
            return s_wrapPipes[i].pipe;
        }
    }
    if (s_wrapPipeCount == RD_WRAP_PIPES) {
        rd__log_once(RD_ONCE_WRAP, "COLCLAMP 0 pipelines past RD_WRAP_PIPES: clamped");
        return (RhiPipeline){0};
    }
    static const RhiVertexBinding vb = {0, sizeof(IcoSpriteVertex), false};
    static const RhiVertexAttr va[4] = {
        {0, 0, RHI_VTX_U16x2_UINT, offsetof(IcoSpriteVertex, x)},
        {1, 0, RHI_VTX_U32x1, offsetof(IcoSpriteVertex, z)},
        {2, 0, RHI_VTX_U8x4_UINT, offsetof(IcoSpriteVertex, rgba)},
        {3, 0, RHI_VTX_F32x2, offsetof(IcoSpriteVertex, u)},
    };
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.layouts = layouts;
    d.layoutCount = 3;
    d.cullNone = true;
    d.colorCount = 1;
    d.blend[0].srcColor = RHI_BF_ONE;
    d.blend[0].dstColor = RHI_BF_ZERO;
    d.blend[0].colorOp = RHI_BO_ADD;
    d.blend[0].srcAlpha = RHI_BF_ONE;
    d.blend[0].dstAlpha = RHI_BF_ZERO;
    d.blend[0].alphaOp = RHI_BO_ADD;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    if (resolve) {
        d.vertex = g_rd.vs[RD_VS_BLIT];
        d.fragment = g_rd.fs[RD_FS_WRAP_RESOLVE];
        d.blend[0].writeMask = mask;
        d.colorFormats[0] = fmt;
        d.debugName = "rd wrap resolve";
    } else {
        d.vertex = g_rd.vs[vs];
        d.fragment = g_rd.fs[RD_FS_WRAP_ACC];
        d.vertexBindings = &vb;
        d.vertexBindingCount = 1;
        d.vertexAttrs = va;
        d.vertexAttrCount = 4;
        d.topology = prim == RD_PRIM_LINES ? RHI_TOPO_LINE_LIST : RHI_TOPO_TRIANGLE_LIST;
        d.blend[0].enable = true;
        d.blend[0].dstColor = RHI_BF_ONE;
        d.blend[0].writeMask = 0xF;
        d.colorFormats[0] = RHI_FMT_RGBA16F;
        d.depthFormat = fmt;
        if (fmt != RHI_FMT_UNKNOWN) {
            d.depthStencil.depthTest = true;
            d.depthStencil.depthWrite = zwrite == RD_ZWRITE_ON;
            d.depthStencil.depthCompare = ztst == RD_ZTST_NEVER     ? RHI_CMP_NEVER
                                          : ztst == RD_ZTST_GEQUAL  ? RHI_CMP_GEQUAL
                                          : ztst == RD_ZTST_GREATER ? RHI_CMP_GREATER
                                                                    : RHI_CMP_ALWAYS;
        }
        d.debugName = "rd wrap accumulate";
    }
    rd__pipeline_note_begin(d.debugName);
    RhiPipeline p = rhi_create_pipeline(&d);
    rd__pipeline_note_end();
    if (!p.id) {
        rd__log("COLCLAMP 0 pipeline creation failed (resolve %d)", resolve);
        return p;
    }
    s_wrapPipes[s_wrapPipeCount].key = key;
    s_wrapPipes[s_wrapPipeCount].pipe = p;
    s_wrapPipeCount++;
    g_rd.stats.pipelineCreates++;
    return p;
}

static void doScreenWrap(Replay *r, const RdFrame *f, const RdCmd *c)
{
    DrawSetup ds;
    if (!prepareDraw(r, &ds, NULL)) {
        return;
    }
    ds.screenCopy = screenCopy(r, &ds, c->b[0], c->b[1]);
    RdTargetRec *tc = ds.tc;
    RdTargetRec *td = ds.tdId ? rd__target_rec(ds.tdId) : NULL;
    setUvShift(tc->sx, tc->sy);
    if (tc->format != RHI_FMT_RGBA8_UNORM || (td && (td->tw != tc->tw || td->th != tc->th))) {
        rd__log_once(RD_ONCE_WRAP, "COLCLAMP 0 draw on a non-RGBA8 target or a depth of another "
                                   "size: not drawn");
        return;
    }
    const uint32_t n = c->u[1];
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    ds.repeatKept = /* drawWrap's only, as doScreen's */
        borderOpen() ? rd__screen_repeat_kept(v, n, c->b[2], (float)ds.tw, (float)ds.th) : 0u;
    const uint64_t vOff = rd__ring_alloc((uint64_t)n * 6 * sizeof(IcoSpriteVertex), 16);
    if (vOff == ~0ull) {
        return;
    }
    uint8_t topo;
    IcoSpriteVertex *out = vxScratch((uint64_t)n * 6);
    if (!out) {
        return;
    }
    const uint32_t nv = expand(v, n, c->b[0], c->b[2], (float)ds.tw, (float)ds.th, r->st.uvOffset,
                               out, &topo, r->st.gouraud != 0);
    if (nv == 0) {
        return;
    }
    if (mirrorUi(r, c->b[1])) {
        mirrorDraw(r, tc, out, nv, topo); /* the mirror mode's UI flip */
    }
    memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(*out)); /* built locally */
    /* the planner's flags, FIX and alpha test for this state (COLCLAMP set on
     * the copy: the planner logs COLCLAMP 0 as clamped, which this path is
     * not) */
    RdStateBlock st = r->st;
    st.ds.colclamp = 1;
    RdDrawPass dp[2];
    if (rd__plan_screen_draw(&st, topo, c->b[1], tc->format, ds.depthFmt, dp) != 1) {
        return;
    }
    RhiRect sc;
    if (!scissorRect(r, tc, &sc)) {
        return;
    }
    RhiPipeline pa = wrapPipeline(0, dp[0].key.vs, topo, ds.depthFmt, dp[0].key.gs.ztst,
                                  dp[0].key.gs.zwrite, 0xF);
    RhiPipeline pr = wrapPipeline(1, 0, 0, tc->format, 0, 0, dp[0].key.gs.colorMask);
    if (!pa.id || !pr.id) {
        return;
    }
    if (!s_wrapAcc.id || s_wrapW != tc->tw || s_wrapH != tc->th) {
        if (s_wrapAcc.id) {
            endPass(r);
            rhi_destroy_texture(s_wrapAcc);
        }
        s_wrapAcc = rhi_create_texture(&(RhiTextureDesc){tc->tw, tc->th, 1, RHI_FMT_RGBA16F,
                                                         RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                         "rd wrap accumulator", 0});
        s_wrapAccState = RHI_STATE_UNDEFINED;
        s_wrapW = tc->tw;
        s_wrapH = tc->th;
        if (!s_wrapAcc.id) {
            rd__note_scene_pressure();
            return;
        }
    }
    /* the destination before the command, whole */
    if (!takeSnap(r, tc, (RhiRect){0, 0, tc->tw, tc->th})) {
        return;
    }
    if (ds.textured && ds.tex.id == tc->color.id) {
        ds.tex = tc->snap; /* the GS reads the buffer it draws into */
    }

    /* 1. the terms into the accumulator */
    rd__transition(s_cl, s_wrapAcc, &s_wrapAccState, RHI_STATE_RENDER_TARGET);
    if (td) {
        rd__transition(s_cl, td->depth, &td->depthState, RHI_STATE_DEPTH_WRITE);
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_wrapAcc;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    if (td) {
        p.depth.texture = td->depth;
        p.depth.depthLoad = RHI_LOAD_LOAD;
        /* the stencil as beginPass has it (a pending reset was flushed by
         * the endPass above) */
        rd__stencil_ops_packed(&s_stencil.w, td->depth.id, false, RHI_LOAD_LOAD,
                               &p.depth.stencilLoad, &p.depth.stencilStore);
    }
    p.width = tc->tw;
    p.height = tc->th;
    rd__perf_pass(s_cl, RD_PERF_PASS_RENDER, r->st.color, tc->tw, tc->th, r->perfPost);
    rhi_cmd_begin_render_pass(s_cl, &p);
    setViewport(tc->tw, tc->th);
    rhi_cmd_set_scissor(s_cl, &sc);
    float ox = 2048.0f - (float)(r->st.gsW >> 1);
    float oy = 2048.0f - (float)(r->st.gsH >> 1);
    if (drawOffset(r, &ds) & RD_TARGET_HALF_Y) {
        oy += 0.5f;
    }
    IcoDrawCB cb;
    fillDrawCB(r, &dp[0], &ds, &cb);
    cb.mode[0] &= ~(uint32_t)ICO_DF_PREMUL;
    cb.param[0] = (float)wrapEquation(&r->st);
    RhiSampler smp = ds.gsLevels
                         ? gsLodSampler(r, &ds)
                         : texSampler((RdFilter)r->st.ds.magFilter, (RdFilter)r->st.ds.minFilter,
                                      drawWrap(r, &ds, (RdWrap)r->st.ds.wrap.s, 0),
                                      drawWrap(r, &ds, (RdWrap)r->st.ds.wrap.t, 1), ds.mipmapped,
                                      ds.replacement);
    rd__set_pipeline(s_cl, pa, NULL); /* wrapPipeline: every state static */
    rd__bind_uniform(s_cl, 0, frameGroupOf(tc, ox, oy, rd__target_z_scale(ds.tdId)));
    rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(s_cl, 2, rd__tex_group(ds.tex, smp));
    rhi_cmd_set_vertex_buffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    drawCmd(s_cl, nv, 0, 1);
    rhi_cmd_end_render_pass(s_cl);
    rd__transition(s_cl, s_wrapAcc, &s_wrapAccState, RHI_STATE_SHADER_READ);

    /* 2. (Cd + acc) mod 256 into the target */
    beginPass(r, tc, NULL, r->st.color, 0, RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD, 0.0f, false);
    rhi_cmd_set_scissor(s_cl, &sc);
    IcoDrawCB rb;
    memset(&rb, 0, sizeof(rb));
    rd__set_pipeline(s_cl, pr, NULL); /* wrapPipeline: every state static */
    rd__bind_uniform(s_cl, 0, rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f));
    rd__bind_uniform(s_cl, 1, rd__draw_group(&rb));
    rhi_cmd_set_bind_group(s_cl, 2,
                           rd__tex_group_date(s_wrapAcc,
                                              rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                          RD_WRAP_CLAMP, RD_WRAP_CLAMP),
                                              tc->snap));
    drawCmd(s_cl, 3, 0, 1);
    endPass(r);
    r->writeSerial++;
}

static void doExact(Replay *r, const RdCmd *c)
{
    RdTargetRec *src = rd__target_rec(c->u[0]);
    RdTargetRec *dst = rd__target_rec(c->u[1]);
    if (!src || !dst || !src->color.id || !dst->color.id) {
        return;
    }
    endPass(r);
    rd__perf_pass(s_cl, RD_PERF_PASS_EXACT, c->u[1], dst->tw, dst->th, r->perfPost);
    rhi_cmd_checkpoint(s_cl, passPush(RD_PASSK_EXACT, c->u[0], src->tw, src->th, c->u[1], dst->tw,
                                      dst->th, 0, r->perfPost));
    r->writeSerial++;
    uint32_t w = dst->tw, h = dst->th;
    if (src->tw != w || src->th != h) {
        rd__log_once(RD_ONCE_EXACT_SIZE, "exact blend between targets of different sizes: the "
                                         "common top-left rectangle is blended");
        w = src->tw < w ? src->tw : w;
        h = src->th < h ? src->th : h;
    }
    const RhiRect all = {0, 0, w, h};
    if (!r->st.ds.abe) {
        /* no blending: the GS writes Cs */
        rd__transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
        rd__transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
        copyCmd(s_cl, src->color, all, dst->color, 0, 0);
        return;
    }
    RdScratch *cs = scratchGet(w, h, 0), *cd = scratchGet(w, h, 1), *co = scratchGet(w, h, 2);
    if (!cs || !cd || !co) {
        return;
    }
    g_rdPerf.exactBlends++;
    rd__transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
    rd__transition(s_cl, cs->tex, &cs->state, RHI_STATE_COPY_DST);
    copyCmd(s_cl, src->color, all, cs->tex, 0, 0);
    rd__transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_SRC);
    rd__transition(s_cl, cd->tex, &cd->state, RHI_STATE_COPY_DST);
    copyCmd(s_cl, dst->color, all, cd->tex, 0, 0);
    rd__transition(s_cl, cs->tex, &cs->state, RHI_STATE_SHADER_READ);
    rd__transition(s_cl, cd->tex, &cd->state, RHI_STATE_SHADER_READ);
    rd__transition(s_cl, co->tex, &co->state, RHI_STATE_RENDER_TARGET);

    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = co->tex;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_cmd_begin_render_pass(s_cl, &p);
    setViewport(w, h);
    rhi_cmd_set_scissor(s_cl, &all);
    RdPipeKeyInt k = rd__post_key(RD_VS_BLEND_INT, RD_FS_BLEND_INT, RHI_FMT_RGBA8_UINT);
    RhiPipeline pipe = rd__get_pipeline(&k);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.blend[0] = rd__alpha_register(r->st.ds.blend);
    cb.blend[1] = r->st.ds.blendFix;
    cb.blend[2] = r->st.ds.colclamp;
    RhiBinding b[2];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = cs->tex;
    b[1].slot = 2;
    b[1].type = RHI_BIND_SAMPLED_TEXTURE;
    b[1].texture = cd->tex;
    if (pipe.id) {
        rd__set_pipeline(s_cl, pipe, &k);
        rd__bind_uniform(s_cl, 0, rd__frame_group(w, h, 0.0f, 0.0f));
        rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(s_cl, 2, bindGroup(g_rd.layoutInt, b, 2));
        drawCmd(s_cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(s_cl);
    rd__transition(s_cl, co->tex, &co->state, RHI_STATE_COPY_SRC);
    rd__transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
    copyCmd(s_cl, co->tex, all, dst->color, 0, 0);
}

static void doCopy(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdTargetRec *src = rd__target_rec(c->u[0]);
    RdTargetRec *dst = rd__target_rec(c->u[1]);
    if (!src || !dst || src == dst || src->format != dst->format) {
        return;
    }
    RdCopyRec cr;
    memcpy(&cr, f->payload + c->u[2], sizeof(cr));
    if (cr.srcX < 0 || cr.srcY < 0 || cr.dstX < 0 || cr.dstY < 0) {
        return;
    }
    /* the rectangle in each texture's texels; between targets of
     * different scales the copy cannot resample (logged once) and copies the
     * source's texels */
    if (src->sx != dst->sx || src->sy != dst->sy) {
        rd__log_once(RD_ONCE_COPY_SCALE, "copy between targets of different resolution scales: "
                                         "not resampled");
    }
    uint32_t sx0 = (uint32_t)((float)cr.srcX * src->sx);
    const uint32_t sy0 = (uint32_t)((float)cr.srcY * src->sy);
    uint32_t dx0 = (uint32_t)((float)cr.dstX * dst->sx);
    const uint32_t dy0 = (uint32_t)((float)cr.dstY * dst->sy);
    uint32_t w = src->sx == 1.0f ? cr.w : (uint32_t)((float)cr.w * src->sx + 0.5f);
    uint32_t h = src->sy == 1.0f ? cr.h : (uint32_t)((float)cr.h * src->sy + 0.5f);
    if (src->ox > 0.0f || dst->ox > 0.0f) {
        /* Full pixel: the columns from ox texels in (rd__gs_texel_x); what
         * starts left of a texture's first column is left out of both */
        const float fs = floorf(rd__gs_texel_x(src, (float)cr.srcX));
        const float fd = floorf(rd__gs_texel_x(dst, (float)cr.dstX));
        const float cut = fs < fd ? (fs < 0.0f ? -fs : 0.0f) : (fd < 0.0f ? -fd : 0.0f);
        if ((float)w <= cut) {
            return;
        }
        sx0 = (uint32_t)(fs + cut);
        dx0 = (uint32_t)(fd + cut);
        w -= (uint32_t)cut;
    }
    if (sx0 >= src->tw || sy0 >= src->th || dx0 >= dst->tw || dy0 >= dst->th) {
        return;
    }
    /* clipped without forming x0 + w, which wraps for a w near 2^32 */
    if (w > src->tw - sx0) {
        w = src->tw - sx0;
    }
    if (w > dst->tw - dx0) {
        w = dst->tw - dx0;
    }
    if (h > src->th - sy0) {
        h = src->th - sy0;
    }
    if (h > dst->th - dy0) {
        h = dst->th - dy0;
    }
    if (w == 0 || h == 0) {
        return;
    }
    endPass(r);
    rd__perf_pass(s_cl, RD_PERF_PASS_COPY, c->u[0], w, h, r->perfPost);
    r->writeSerial++;
    rd__transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
    rd__transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
    rhi_cmd_checkpoint(s_cl, passPush(RD_PASSK_COPY, c->u[0], src->tw, src->th, c->u[1], dst->tw,
                                      dst->th, 0, r->perfPost));
    copyCmd(s_cl, src->color, (RhiRect){(int32_t)sx0, (int32_t)sy0, w, h}, dst->color, (int32_t)dx0,
            (int32_t)dy0);
}

/* --------------------------------------------------------------- shadows
 * rd_shadow.c says what the three commands stand for.  All three work on
 * the state block's colour target and the depth-stencil of its depth
 * target, which must have the colour's size (shadow_Reset binds the
 * per-frame count target with SCENE's). */

/* The colour and depth-stencil the state block names, of one size; NULL
 * (reported once) otherwise. */
static RdTargetRec *shadowTargets(Replay *r, RdTargetRec **depth)
{
    RdTargetRec *tc = rd__target_rec(r->st.color);
    RdTargetRec *td = rd__target_rec(r->st.depth);
    if (!tc || !tc->color.id || !td || !td->withDepth || !td->depth.id || tc->tw != td->tw ||
        tc->th != td->th) {
        rd__log_once(RD_ONCE_SHADOW, "shadow command without a depth-stencil target of the colour "
                                     "target's size: skipped");
        return NULL;
    }
    *depth = td;
    return tc;
}

static void doShadowReset(Replay *r)
{
    RdTargetRec *td;
    RdTargetRec *tc = shadowTargets(r, &td);
    if (!tc) {
        return;
    }
    endPass(r);
    s_shadowRedFor = 0; /* a new count starts */
    /* The stencil clear is not recorded yet: the next pass on the depth
     * takes it as its stencil load op (beginPass), else it is recorded as
     * its own pass (flushStencilClear) at the next endPass or transition
     * of either target.  Nothing
     * reads or writes the stencil before the next pass on the depth (only
     * the volumes and the resolve use it, each in a pass on it), so the
     * clear is in place before its first user either way.  The window
     * opens: the depth's stencil is loaded and stored until the resolve. */
    rd__stencil_open(&s_stencil.w, td->depth.id);
    s_stencil.w.clearFor = td->depth.id;
    s_stencil.color = r->st.color;
    s_stencil.depth = r->st.depth;
    s_stencil.colorTex = tc->color.id;
    s_stencil.depthTex = td->depth.id;
}

/* RDC_SHADOW_STRIP: rd_shadow_tris's two triangle lists, or rd_shadow_strip's
 * float strip with one sign. */
static void doShadowStrip(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdTargetRec *td;
    if (!shadowTargets(r, &td)) {
        return;
    }
    DrawSetup ds;
    if (!prepareDraw(r, &ds, NULL) || !ds.tdId) {
        return;
    }
    uint32_t count[2];
    uint64_t vOff;
    static const float noOff[2] = {0.0f, 0.0f};
    if (c->b[0] == RD_SHADOW_TRIS) {
        count[0] = c->u[0];
        count[1] = c->u[3];
        const uint32_t n = count[0] + count[1];
        vOff = rd__ring_alloc((uint64_t)(n ? n : 1) * sizeof(IcoSpriteVertex), 16);
        if (vOff == ~0ull) {
            return;
        }
        const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[1]);
        IcoSpriteVertex *o = (IcoSpriteVertex *)(g_rd.ringMap[s_slot] + vOff);
        for (uint32_t i = 0; i < n; i++) {
            convVtx(&v[i], 1, 1.0f, 1.0f, noOff, &o[i]);
        }
    } else {
        const uint32_t n = c->u[0];
        const uint32_t nt = n >= 3 ? n - 2 : 0;
        vOff = rd__ring_alloc((uint64_t)(nt ? nt : 1) * 3 * sizeof(IcoSpriteVertex), 16);
        if (vOff == ~0ull) {
            return;
        }
        const float (*fv)[4] = (const float (*)[4])(f->payload + c->u[1]);
        IcoSpriteVertex *o = (IcoSpriteVertex *)(g_rd.ringMap[s_slot] + vOff);
        for (uint32_t t = 0; t < nt; t++) {
            for (uint32_t j = 0; j < 3; j++) {
                RdScreenVtx sv;
                memset(&sv, 0, sizeof(sv));
                sv.x = (int32_t)fv[t + j][0];
                sv.y = (int32_t)fv[t + j][1];
                sv.z = fv[t + j][2] <= 0.0f ? 0u : (uint32_t)fv[t + j][2];
                convVtx(&sv, 1, 1.0f, 1.0f, noOff, &o[t * 3 + j]);
            }
        }
        count[0] = c->f[0] > 0.0f ? nt * 3 : 0;
        count[1] = c->f[0] > 0.0f ? 0 : nt * 3;
    }
    RhiBindGroup g2 = bindDraw(r, &ds);
    if (!g2.id) {
        return;
    }
    rhi_cmd_set_vertex_buffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
    RdUniform g1 = rd__draw_group(&cb);
    uint32_t first = 0;
    for (int decr = 0; decr < 2; decr++) {
        if (count[decr] == 0) {
            continue;
        }
        const RdPipeKeyInt k = rd__shadow_volume_key(&r->st, ds.tc->format, decr);
        RhiPipeline p = rd__get_pipeline(&k);
        if (p.id) {
            rd__set_pipeline(s_cl, p, &k);
            rd__bind_uniform(s_cl, 0, r->frameBG);
            rd__bind_uniform(s_cl, 1, g1);
            rhi_cmd_set_bind_group(s_cl, 2, g2);
            rhi_cmd_set_stencil_ref(s_cl, 0);
            drawCmd(s_cl, count[decr], first, 1);
            g_rd.stats.draws++;
        }
        first += count[decr];
    }
}

void rd__shadow_shutdown(void)
{
    if (s_shadowRed.id) {
        rhi_destroy_texture(s_shadowRed);
    }
    s_shadowRed = (RhiTexture){0};
    s_shadowRedW = s_shadowRedH = 0;
    s_shadowRedFor = 0;
}

/* The count at the GS size.  The first blur level reads the count with a
 * 2:1 bilinear sprite, which averages a 2x2 block of the PS2's 512x512
 * count exactly; on a count at s times the resolution the same sprite
 * takes 2x2 taps of an 2s x 2s footprint, and the shadow's integral varies
 * with the volume's sub-pixel position (2.6 % at 4x against 2.0 %).  With
 * the count target scaled, doShadowResolve box-averages it down to its GS
 * size (box_reduce_ps) into s_shadowRed and resolveTexture hands that to
 * whatever samples the count as a texture, so the level reads what the
 * PS2's did.  Unscaled targets (Original) never come here. */
static void shadowReduce(Replay *r, RdTargetRec *tc)
{
    s_shadowRedFor = 0;
    if (!s_shadowRed.id || s_shadowRedW != tc->w || s_shadowRedH != tc->h) {
        if (s_shadowRed.id) {
            rhi_destroy_texture(s_shadowRed);
        }
        s_shadowRed = rhi_create_texture(&(RhiTextureDesc){tc->w, tc->h, 1, RHI_FMT_RGBA8_UNORM,
                                                           RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                           "rd shadow count reduced", 0});
        s_shadowRedState = RHI_STATE_UNDEFINED;
        s_shadowRedW = tc->w;
        s_shadowRedH = tc->h;
    }
    RdPipeKeyInt k = rd__shadow_reduce_key(); /* RGBA8, like the count */
    RhiPipeline pipe = rd__get_pipeline(&k);
    if (!s_shadowRed.id || !pipe.id) {
        return;
    }
    endPass(r);
    rd__transition(s_cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    rd__transition(s_cl, s_shadowRed, &s_shadowRedState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_shadowRed;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = tc->w;
    p.height = tc->h;
    rd__perf_pass(s_cl, RD_PERF_PASS_RENDER, 0, tc->w, tc->h, r->perfPost);
    rhi_cmd_begin_render_pass(s_cl, &p);
    setViewport(tc->w, tc->h);
    const RhiRect all = {0, 0, tc->w, tc->h};
    rhi_cmd_set_scissor(s_cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = cb.uvRect[3] = 1.0f;
    cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
    cb.param[0] = (float)tc->tw / (float)tc->w;
    cb.param[1] = (float)tc->th / (float)tc->h;
    cb.param[2] = (float)tc->tw;
    cb.param[3] = (float)tc->th;
    if (tc->ox > 0.0f) {
        /* Full pixel: GS pixel x is texels x sx - ox (blit.hlsl
         * box_reduce_ps) */
        cb.param[0] = tc->sx;
        cb.scale[2] = tc->ox;
    }
    rd__set_pipeline(s_cl, pipe, &k);
    rd__bind_uniform(s_cl, 0, rd__frame_group(tc->w, tc->h, 0.0f, 0.0f));
    rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(
        s_cl, 2,
        rd__tex_group(tc->color, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                             RD_WRAP_CLAMP)));
    drawCmd(s_cl, 3, 0, 1);
    rhi_cmd_end_render_pass(s_cl);
    rd__transition(s_cl, s_shadowRed, &s_shadowRedState, RHI_STATE_SHADER_READ);
    s_shadowRedFor = r->st.color;
}

static void doShadowResolve(Replay *r)
{
    RdTargetRec *td;
    RdTargetRec *tc = shadowTargets(r, &td);
    if (!tc) {
        return;
    }
    static const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    r->writeSerial++;
    beginPass(r, tc, td, r->st.color, r->st.depth, RHI_LOAD_CLEAR, zero, RHI_LOAD_LOAD, 0.0f,
              false);
    const RhiRect all = {0, 0, tc->tw, tc->th};
    rhi_cmd_set_scissor(s_cl, &all);
    RdUniform g0 = rd__frame_group(tc->tw, tc->th, 0.0f, 0.0f);
    RhiBindGroup g2 = rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                            RD_WRAP_CLAMP, RD_WRAP_CLAMP));
    for (int pass = 0; pass < RD_SHADOW_RESOLVE_PASSES; pass++) {
        const RdPipeKeyInt k = rd__shadow_resolve_key(pass);
        RhiPipeline p = rd__get_pipeline(&k);
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        if (pass < 6) {
            cb.col[0] = cb.col[1] = cb.col[2] = 4u << pass;
        } else {
            cb.col[3] = 0x80;
        }
        cb.uvRect[2] = cb.uvRect[3] = 1.0f;
        cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
        rd__set_pipeline(s_cl, p, &k);
        rd__bind_uniform(s_cl, 0, g0);
        rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(s_cl, 2, g2);
        rhi_cmd_set_stencil_ref(s_cl, pass < 6 ? (uint8_t)(1u << pass) : 0);
        drawCmd(s_cl, 3, 0, 1);
    }
    endPass(r);
    /* the count has been read: the window closes, and later passes on the
     * depth neither load nor store its stencil until the next reset */
    rd__stencil_close(&s_stencil.w, td->depth.id);
    s_shadowRedFor = 0;
    if (tc->tw != tc->w || tc->th != tc->h) {
        shadowReduce(r, tc);
    }
}

/* ------------------------------------------------------------------- fog
 * RD_POST_FOG (fog_DrawFog, ZFog.c).  The GS
 * copies the Z buffer to 0x2800, copies byte 2 of every word into byte 3
 * through a PSMT4 view, and draws one sprite reading the copy as PSMT8H
 * through the fog CLUT, Z-tested GEQUAL at the sprite's Z under ZMSK.  Here:
 * the Z source's depth (the target of the bound depth view, else the state's
 * depth target) is read the way g_rd.fogPath says (rd__fog_source): sampled
 * where it is, in RHI_STATE_DEPTH_READ, through a whole copy of it
 * (rd__sampled_depth: tile-based GPUs and D24S8 by default; the same
 * values, ctest rd_fog_depth_copy_dump), or as words copied out through a
 * buffer into an R32_UINT texture (fog_lut_buffer_ps), whichever passed
 * the fog's self-test (rd_fog_selftest); the LUT is uploaded into
 * s_fogLut, and the sprite is drawn through the sprite vertex shader and
 * fog_lut_ps, which reconstructs the GS Z, takes bits 16..23 as the index,
 * applies the texture function and the Z test, and blends with the state
 * block's ALPHA.  No depth attachment is bound (ZMSK: nothing to write), so
 * the depth is never sampled while it is an attachment; the next pass that
 * binds it takes it back to DEPTH_WRITE (beginPass). */
static RhiTexture s_fogLut;

static RhiState s_fogLutState;

static RdDepthCopy s_fogDepth; /* the depth's copy on the RD_FOG_COPY path */

void rd__scene_caches_free(void)
{
    if (s_wrapAcc.id) {
        rhi_destroy_texture(s_wrapAcc);
    }
    s_wrapAcc = (RhiTexture){0};
    s_wrapW = s_wrapH = 0;
    rd__depth_copy_free(&s_fogDepth);
    rd__fog_buffer_free();
    msaaFree(); /* the MSAA scene pair (remade at SCENE's size) */
}

void rd__fog_shutdown(void)
{
    if (s_fogLut.id) {
        rhi_destroy_texture(s_fogLut);
    }
    s_fogLut = (RhiTexture){0};
    rd__depth_copy_free(&s_fogDepth);
    rd__fog_path_shutdown(); /* the buffer path's objects and the probe's */
}

/* the table the replay's upload phase copied into s_fogLut
 * (rd__fog_lut_upload): the replay (g_rd.replayCounter after it began),
 * the frame and the payload offset it holds; doFog copies another one */
static struct {
    uint32_t replay;
    const RdFrame *f;
    uint32_t lutOffset;
} s_fogLutHeld;

static bool fogLutMake(void)
{
    if (!s_fogLut.id) {
        s_fogLut = rhi_create_texture(&(RhiTextureDesc){
            256, 1, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd fog lut", 0});
        s_fogLutState = RHI_STATE_UNDEFINED;
    }
    return s_fogLut.id != 0;
}

/* the LUT at payload offset lutOffset of f into s_fogLut (outside a pass) */
static bool fogLutCopy(const RdFrame *f, uint32_t lutOffset)
{
    const uint32_t pitchA = rhi_limits()->copyRowPitchAlign;
    const uint32_t lutPitch = (256 * 4 + pitchA - 1) / pitchA * pitchA;
    const uint64_t lOff = rd__ring_alloc(lutPitch, rhi_limits()->copyOffsetAlign);
    if (lOff == ~0ull || !fogLutMake()) {
        return false;
    }
    memcpy(g_rd.ringMap[s_slot] + lOff, f->payload + lutOffset, 256 * 4);
    rd__transition(s_cl, s_fogLut, &s_fogLutState, RHI_STATE_COPY_DST);
    rhi_cmd_copy_buffer_to_texture(s_cl, g_rd.ring[s_slot], lOff, lutPitch, s_fogLut, 0,
                                   (RhiRect){0, 0, 256, 1});
    rd__transition(s_cl, s_fogLut, &s_fogLutState, RHI_STATE_SHADER_READ);
    s_fogLutHeld.replay = g_rd.replayCounter;
    s_fogLutHeld.f = f;
    s_fogLutHeld.lutOffset = lutOffset;
    return true;
}

void rd__fog_lut_upload(const RdFrame *f, int keep)
{
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            if (c->type != RDC_POST_STUB || c->b[0] != RD_POST_FOG) {
                continue;
            }
            RdPostRec p;
            memcpy(&p, f->payload + c->u[1], sizeof(p));
            if (p.lutOffset != ~0u) {
                (void)fogLutCopy(f, p.lutOffset);
            }
            return; /* the first fog's: a frame fogs once */
        }
    }
}

/* The fog's local read (RD_FOG_LOCAL; fog_lut_local_ps): its depth is the
 * colour target's own (SCENE's, zid == the colour's, the same size: every
 * fogged game frame; the depth made for it, RD_DST_USAGE_LOCAL_DEPTH), the
 * shader and the group there, no MSAA epoch open or able to open (the
 * probe then copies its texels from the depth itself), the fixed-function
 * blend (an exact blend opted into draws the fog's LERP through its own
 * twins, which have no local build: the copy then).  Then the fog
 * draws in the scene pass with the depth readable (begun so by beginPass,
 * depthWantsReadable), and nothing ends the pass or copies the depth */
static bool fogLocalFor(const Replay *r, uint32_t zid, const RdTargetRec *tz, const RdTargetRec *tc)
{
    return g_rd.fogPath == RD_FOG_LOCAL && zid == r->st.color && tz == tc &&
           depthWantsReadable(tc, tz) && g_rd.layoutLocal.id && g_rd.fs[RD_FS_FOG_LOCAL].id &&
           (!g_rd.noDual || g_rd.fsNoDual[RD_FS_FOG_LOCAL].id) && !s_ms.active &&
           !(s_ms.want > 1u && !s_ms.ended) && g_rd.dstMode == RD_BLEND_EXACT_OFF;
}

static void doFog(Replay *r, const RdFrame *f, const RdCmd *c)
{
    r->dateStNow = 0; /* no DATE here: the passes keep their own tests */
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    uint32_t zid = r->st.depth;
    const RdTexRec *tv = r->st.ds.texEnabled ? rd__tex_rec(r->st.tex) : NULL;
    if (tv && tv->kind == RD_TEXKIND_TARGET && tv->view == RD_VIEW_DEPTH) {
        zid = tv->target;
    }
    RdTargetRec *tz = rd__target_rec(zid);
    RdTargetRec *tc = rd__target_rec(r->st.color);
    if (p.lutOffset == ~0u || !tz || !tz->withDepth || !tz->depth.id || !tc || !tc->color.id) {
        rd__log_once(RD_ONCE_FOG, "fog without a LUT, a depth source or a colour target: skipped");
        return;
    }
    RdDrawPass pl[2], dp[8];
    const int buffer = g_rd.fogPath == RD_FOG_BUFFER;
    const bool local = fogLocalFor(r, zid, tz, tc);
    const int npl = rd__fog_plan(
        &r->st, tc->format, local ? RD_FOG_LOCAL : (buffer ? RD_FOG_BUFFER : RD_FOG_INPLACE), pl);
    if (npl == 0) {
        return;
    }

    /* the sprite as the GS gets it: corners (12.4), UVs (12.4), RGBAQ, Z */
    RdScreenVtx v[2];
    memset(v, 0, sizeof(v));
    for (int i = 0; i < 2; i++) {
        v[i].x = (int32_t)p.rect[i * 2];
        v[i].y = (int32_t)p.rect[i * 2 + 1];
        v[i].s = p.uv[i * 2];
        v[i].t = p.uv[i * 2 + 1];
        v[i].z = p.z;
        v[i].q = 1.0f;
        memcpy(v[i].rgba, p.rgba, 4);
    }
    static const float noOff[2] = {0.0f, 0.0f};
    const uint64_t vOff = rd__ring_alloc(6 * sizeof(IcoSpriteVertex), 16);
    if (vOff == ~0ull) {
        return;
    }
    uint8_t topo;
    IcoSpriteVertex out[6];     /* expanded here, copied into the ring */
    setUvShift(tc->sx, tc->sy); /* the fog sprite on a scaled target */
    const uint32_t nv = expand(v, 2, RD_PRIM_SPRITES, 1, (float)tz->w, (float)tz->h, noOff, out,
                               &topo, r->st.gouraud != 0);
    /* the table: the one the upload phase copied (rd__fog_lut_upload), or
     * copied here, outside a pass */
    const bool lutHeld = s_fogLut.id && s_fogLutHeld.replay == g_rd.replayCounter &&
                         s_fogLutHeld.f == f && s_fogLutHeld.lutOffset == p.lutOffset;

    if (!local) {
        endPass(r);
    }
    if (!lutHeld) {
        endPass(r);
        if (!fogLutCopy(f, p.lutOffset)) {
            return;
        }
    }
    /* the GS's BITBLT of the Z buffer to 0x2800: the depth in place, its
     * copy, its words or the pass's own depth attachment (g_rd.fogPath);
     * the pass below binds the colour target alone but on the local read,
     * where it is the scene pass with the depth.  Inside the MSAA epoch,
     * with sample-rate shading and SCENE's depth drawn in it, the fog runs
     * per sample on SCENE_MS reading its depth (fog_lut_ms_ps) and the
     * epoch stays open; otherwise the read below closes it (the fog of the
     * resolved picture) */
    RdFogSource src;
    const uint32_t scene = RD_TARGET_SCENE + 1u;
    /* a fog on SCENE while the MSAA epoch is open blends fixed-function
     * whichever depth it reads: per sample it draws in the epoch (no
     * destination read there, rd_dst_read.c), and the fog of the resolved
     * picture stands in for that one on a device without sample-rate
     * shading, so it blends the same way rather than exactly */
    const bool fogEpoch = s_ms.active && zid == scene && r->st.color == scene;
    /* the depth read made ready (the copy path's copy, the buffer path's
     * transfers), a piece of its own; none on the local read */
    if (!local) {
        rd__perf_pass(s_cl, RD_PERF_PASS_FOG_COPY, zid, tz->tw, tz->th, r->perfPost);
    }
    const bool fogMs = !local && !s_fogResolvedForced && s_ms.active && s_ms.depthLive &&
                       zid == scene && r->st.color == scene && !buffer &&
                       rhi_limits()->sampleRateShading && g_rd.fs[RD_FS_FOG_MS].id &&
                       (!g_rd.noDual || g_rd.fsNoDual[RD_FS_FOG_MS].id) && !rd__fog_probe_wanted();
    if (local) {
        rd__fog_source_local(tz, &src);
    } else if (fogMs) {
        memset(&src, 0, sizeof(src));
        rd__transition(s_cl, s_ms.depth, &s_ms.depthState, RHI_STATE_SHADER_READ);
        src.tex = s_ms.depth;
        src.state = &s_ms.depthState;
        src.aspect = RHI_ASPECT_DEPTH;
        for (int i = 0; i < npl; i++) {
            pl[i].key.fs = RD_FS_FOG_MS;
        }
    } else if (!rd__fog_source(s_cl, tz, &s_fogDepth, &src) || src.buffer != buffer) {
        return;
    }

    /* the pass on the colour target alone (on the local read the scene pass
     * with its depth, readable), FrameCB and the scissor */
    DrawSetup ds;
    memset(&ds, 0, sizeof(ds));
    ds.tc = tc;
    ds.tdId = local ? zid : 0;
    ds.depthFmt = local ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
    ds.tex = g_rd.dummy;
    ds.dateTex = g_rd.dummy;
    ds.tw = tz->w;
    ds.th = tz->h;
    ds.textured = 1;
    ds.screenCopy = 1;                      /* it reads the depth texel under each pixel */
    ds.texCrop = tz->ox > 0.0f ? tz : NULL; /* Full pixel (fog_lut.hlsl) */
    /* no prepareDraw here: the per-draw flags it resets are reset here, so
     * the previous draw's mirror (scissorRect) is not inherited.  The fog
     * sprite covers the native frame as drawn */
    r->mirror = 0;
    if (local && r->passOpen && r->passColor == r->st.color && r->passDepth == zid &&
        !r->passDepthReadable) {
        endPass(r); /* begun before the path was the local read's: begun again readable */
    }
    if (!bindDrawKeep(r, &ds).id) {
        return;
    }
    if (local && !r->passDepthReadable) {
        static bool logged; /* once a run */
        if (!logged) {
            logged = true;
            rd__log("fog: the scene pass did not bind its depth readable: the fog is skipped "
                    "(reported once)");
        }
        return;
    }
    /* the planned passes as drawn in this pass: the exact blend's (the fog
     * is a LERP, ALPHA 0x44), the two-pass fallback's */
    const int np = fogEpoch ? expandPassesMode(pl, npl, RD_BLEND_EXACT_OFF, false, dp)
                            : expandPasses(r, pl, npl, dp);
    /* per sample in the epoch the sprite covers whole pixels, as doScreen's
     * integer-aligned sprites (msaaSpriteExpand): drawn as is, its top and
     * left edges would lie on the pixel centres of the frame's first row
     * and column, leaving half their samples unfogged */
    const bool msExpand =
        fogMs && g_rd.passSamples > 1 && msaaSpriteExpand(r, tc, RD_PRIM_SPRITES, out, nv);
    const RdUniform frame = msExpand ? msaaFrame(r, &ds) : r->frameBG;
    memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(out[0]));
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    RhiBindGroup g2;
    if (local) {
        /* the pass's depth as its input attachment (the depth-only view),
         * the table at t2 (rd's layoutLocal) */
        b[0].slot = 1;
        b[0].type = RHI_BIND_INPUT_ATTACHMENT;
        b[0].texture = src.tex;
        b[0].aspect = RHI_ASPECT_DEPTH;
        b[1].slot = 2;
        b[1].type = RHI_BIND_SAMPLED_TEXTURE;
        b[1].texture = s_fogLut;
        g2 = bindGroup(g_rd.layoutLocal, b, 2);
    } else {
        b[0].slot = 1;
        b[0].type = RHI_BIND_SAMPLED_TEXTURE;
        b[0].texture = src.tex;
        b[0].aspect = src.aspect;
        b[1].slot = 1;
        b[1].type = RHI_BIND_SAMPLER;
        b[1].sampler =
            rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
        b[2].slot = 2;
        b[2].type = RHI_BIND_SAMPLED_TEXTURE;
        b[2].texture = s_fogLut;
        g2 = bindGroup(g_rd.layoutTex, b, 3);
    }
    if (!g2.id) {
        return;
    }
    rhi_cmd_set_vertex_buffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    /* the local read: the pass's depth writes so far visible to the reads */
    if (local) {
        rhi_cmd_local_read_barrier(s_cl, RHI_LOCAL_READ_DEPTH);
    }
    /* above 1x: the colour or the depth target scaled (never at scale 1);
     * the self-test's and the probe's fog read the exact entry
     * (rd_fog_path.c rd__fog_exact): they measure the depth read */
    const bool fogAbove1x =
        (tc->sx != 1.0f || tc->sy != 1.0f || tz->sx != 1.0f || tz->sy != 1.0f) && !rd__fog_exact();
    for (int i = 0; i < np; i++) {
        RhiPipeline pipe = rd__get_pipeline(&dp[i].key);
        if (!pipe.id) {
            continue;
        }
        IcoDrawCB cb;
        fillDrawCB(r, &dp[i], &ds, &cb);
        cb.mode[1] = 0; /* the LUT is PSMCT32: no TEXA */
        cb.col[0] = p.z;
        cb.col[1] = r->st.ds.test.zte ? r->st.ds.test.ztst : RD_ZTST_ALWAYS;
        cb.param[0] = rd__target_z_scale(zid);
        cb.param[1] = rd__depth_unorm_steps();
        cb.param[2] = src.sabotaged ? 1.0f : 0.0f; /* ICO_RD_FOG_SABOTAGE: reads give 0 */
        cb.scale[0] = tz->sx;                      /* the depth's texels per GS pixel */
        cb.scale[1] = tz->sy;
        if (fogAbove1x) {
            /* above 1x: the table read between its entries and dithered
             * from the target's centre texel (fog_lut.hlsl) */
            cb.param[3] = 1.0f;
            cb.uvRect[0] = (float)(tc->tw / 2u);
            cb.uvRect[1] = (float)(tc->th / 2u);
            if (!rd__no_dual_second(dp, i)) {
                g_rd.above1xDraws++;
            }
        }
        dstPassMode(r, &dp[i].key); /* a storage pass for a storage destination read */
        rd__set_pipeline(s_cl, pipe, &dp[i].key);
        rd__bind_uniform(s_cl, 0, frame);
        rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(s_cl, 2, g2);
        dstBind(r, &dp[i].key);
        drawCmd(s_cl, nv, 0, 1);
        if (!rd__no_dual_second(dp, i)) {
            g_rd.stats.draws++;
            if (local) {
                g_rdPerf.fogLocalDraws++;
            }
        }
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
    }
    /* the reads before the next draws' depth and colour writes */
    if (local) {
        rhi_cmd_local_read_barrier(s_cl, RHI_LOCAL_READ_BACK);
    }
    /* the first fogged replay after start or a new scene size: what the
     * fog read and what it drew, for the log (after the draw, so the read
     * itself is the frame's own) */
    if (rd__fog_probe_wanted()) {
        endPass(r);
        rd__fog_probe_record(s_cl, tz, zid, &src, tc);
    }
    (void)topo;
}

static RhiRect rectJoin(RhiRect a, RhiRect b)
{
    const int32_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    const int64_t ax1 = (int64_t)a.x + a.w, bx1 = (int64_t)b.x + b.w;
    const int64_t ay1 = (int64_t)a.y + a.h, by1 = (int64_t)b.y + b.h;
    const int64_t x1 = ax1 > bx1 ? ax1 : bx1, y1 = ay1 > by1 ? ay1 : by1;
    return (RhiRect){x0, y0, (uint32_t)(x1 - x0), (uint32_t)(y1 - y0)};
}

/* fx_sprite_ps's texel range on one axis of a sprite sampling the target it
 * draws into, in that target's texels: U(X) = u0 + (X - x0) (u1 - u0) /
 * (x1 - x0) (integers, 12.4, truncated: monotonic in X) for every X a
 * covered fragment can have (within a pixel of x0 .. x1; two allowed),
 * su = round(U * ss), the nearest texel su >> 4 or LINEAR's (su - 8) >> 4
 * and the one after (one texel more), CLAMP on the TEX0 size (round(size *
 * ss), at least 1) and then on the backing size asz: both monotonic, so the
 * range's ends are the ends' images.  False (the whole axis) under REPEAT,
 * for an empty or degenerate sprite and where the shader's int product
 * could overflow. */
static bool blurUvAxis(int32_t x0, int32_t x1, int32_t u0, int32_t u1, float ss, float size,
                       int clamp, uint32_t asz, int32_t crop16, int32_t *lo, int32_t *hi)
{
    const int64_t dx = (int64_t)x1 - x0, du = (int64_t)u1 - u0;
    if (!clamp || dx <= 0 || asz == 0 || (dx + 32) * (du < 0 ? -du : du) > (1ll << 30)) {
        return false;
    }
    const int64_t ua = u0 + (-32 * du) / dx, ub = u0 + ((dx + 32) * du) / dx;
    const float umin = (float)((ua < ub ? ua : ub) - 1), umax = (float)((ua > ub ? ua : ub) + 1);
    const int32_t smin = (int32_t)floorf(umin * ss) - 1, smax = (int32_t)ceilf(umax * ss) + 1;
    int32_t t0 = floorDiv16(smin - 8), t1 = floorDiv16(smax) + 1;
    /* the shader's round() is floor or ceil of the product: the low end
     * clamps on the smaller, the high end on the larger */
    int32_t lszLo = (int32_t)floorf(size * ss), lszHi = (int32_t)ceilf(size * ss);
    lszLo = lszLo < 1 ? 1 : lszLo;
    lszHi = lszHi < 1 ? 1 : lszHi;
    t0 = t0 < 0 ? 0 : (t0 > lszLo - 1 ? lszLo - 1 : t0);
    t1 = t1 < 0 ? 0 : (t1 > lszHi - 1 ? lszHi - 1 : t1);
    if (crop16 > 0) {
        /* a Full pixel crop (fx_sprite.hlsl fx_src_crop16): the frame's
         * texels moved to the crop's, a texel more each way for its
         * rounding, the crop's ends the bounds */
        t0 = floorDiv16(16 * t0 - crop16) - 1;
        t1 = floorDiv16(16 * t1 - crop16) + 1;
        t0 = t0 < 0 ? 0 : t0;
        t1 = t1 < 0 ? 0 : t1;
    }
    *lo = t0 < (int32_t)asz - 1 ? t0 : (int32_t)asz - 1;
    *hi = t1 < (int32_t)asz - 1 ? t1 : (int32_t)asz - 1;
    return true;
}

/* The snapshot texels a staticBlur sprite reads (texels of tc, the whole
 * target until narrowed).  The destination (dstRead: blend, DATE, AFAIL
 * RGB_ONLY) is read at the fragment's own texel, and fragments are the
 * pixels with x0 <= X < x1 of the sprite's rect (fx_sprite_ps's coverage)
 * inside the scissor bindDraw sets:
 * that box padded by two GS pixels (the half-line offset, rounding), in
 * texels.  Sampling itself (selfSample), the texture is read at the UVs:
 * blurUvAxis on each axis, the whole target under REPEAT; above 1x (steps,
 * blurAbove1xSteps) each axis widened by its kernel's reach past the GS's
 * taps (blurKernelReach).  Both: the box holding the two. */
static int32_t blurKernelReach(uint32_t step32)
{
    if (step32 > RD_FX_BOX_MIN32) {
        /* the box: half its width (at most 32 texels) each side of the
         * centre, a texel more for the edges' partial texels */
        const uint32_t w = step32 < 1024u ? step32 : 1024u;
        return (int32_t)((w + 63u) / 64u) + 1;
    }
    /* the B-spline: a texel each side past the bilinear's two (one more
     * kept); the GS's own filter: none */
    return step32 > 0 && step32 < RD_FX_BSPLINE_MAX32 ? 2 : 0;
}

static void blurSnapArea(const Replay *r, const RdTargetRec *tc, const RdPostRec *p,
                         const float uv[4], int dstRead, int selfSample, uint32_t steps,
                         RhiRect *need)
{
    const RhiRect whole = {0, 0, tc->tw, tc->th};
    const int32_t x0 = (int32_t)p->rect[0], y0 = (int32_t)p->rect[1];
    const int32_t x1 = (int32_t)p->rect[2], y1 = (int32_t)p->rect[3];
    RhiRect dst = whole, tex = whole;
    if (dstRead) {
        const int32_t ox = 16 * (2048 - (int32_t)(r->st.gsW >> 1));
        const int32_t oy = 16 * (2048 - (int32_t)(r->st.gsH >> 1));
        RhiRect box, sc;
        if (x1 > x0 && y1 > y0 &&
            gsRectToTexels(tc, floorDiv16(x0 - ox) - 2, floorDiv16(y0 - oy) - 2,
                           -floorDiv16(-(x1 - ox)) + 2, -floorDiv16(-(y1 - oy)) + 2, 0, &box) &&
            scissorRect(r, tc, &sc) && !rectMeet(box, sc, &dst)) {
            dst = whole; /* nothing covered: kept as it was */
        }
    }
    if (selfSample) {
        const RdDrawState *d = &r->st.ds;
        int32_t a, b, e, g;
        if (blurUvAxis(x0, x1, (int32_t)uv[0], (int32_t)uv[2], tc->sx, p->scalar[0],
                       d->wrap.s == RD_WRAP_CLAMP, tc->tw, (int32_t)floorf(tc->ox * 16.0f + 0.5f),
                       &a, &b)) {
            const int32_t k = steps ? blurKernelReach(steps >> 12) : 0;
            a = a - k < 0 ? 0 : a - k;
            b = b + k > (int32_t)tc->tw - 1 ? (int32_t)tc->tw - 1 : b + k;
            tex.x = a;
            tex.w = (uint32_t)(b - a + 1);
        }
        if (blurUvAxis(y0, y1, (int32_t)uv[1], (int32_t)uv[3], tc->sy, p->scalar[1],
                       d->wrap.t == RD_WRAP_CLAMP, tc->th, 0, &e, &g)) {
            const int32_t k = steps ? blurKernelReach(steps & 4095u) : 0;
            e = e - k < 0 ? 0 : e - k;
            g = g + k > (int32_t)tc->th - 1 ? (int32_t)tc->th - 1 : g + k;
            tex.y = e;
            tex.h = (uint32_t)(g - e + 1);
        }
    }
    *need = dstRead && selfSample ? rectJoin(dst, tex) : (dstRead ? dst : tex);
}

/* Whether a staticBlur sprite stores every texel of its target (issue 103):
 * fx_sprite_ps's coverage holds every GS pixel the target's texels stand
 * for (texel blocks 0 .. w - 1 across and 0 .. h - 1 down at the origin
 * FrameCB gets, the half line included), the scissor is the whole target,
 * nothing can discard a fragment (the alpha test but for FB_ONLY and
 * RGB_ONLY, DATE, a Z test other than ALWAYS) and every channel is
 * written.  Such a sprite needs no copy of what it reads: it draws into the
 * snapshot texture, which becomes the target's colour, reading the old
 * colour, which becomes the snapshot (blurSwapTarget).  ICO_RD_SNAP_SWAP=0
 * keeps the copy (the A/B for the picture: the same bytes either way). */
static bool blurCoversTarget(const Replay *r, const RdTargetRec *tc, const RdPostRec *p,
                             const RdDrawState *d, const RdPipeKeyInt *key, int useDepth)
{
    static int s_swap = -1;
    if (s_swap < 0) {
        const char *e = getenv("ICO_RD_SNAP_SWAP");
        s_swap = !(e && strcmp(e, "0") == 0);
    }
    if (!s_swap || key->gs.colorMask != 15u || d->test.date != RD_DATE_OFF ||
        (useDepth && key->gs.ztst != RD_ZTST_ALWAYS)) {
        return false;
    }
    if (d->test.ate && d->test.atst != RD_ATST_ALWAYS && d->test.afail != RD_AFAIL_FB_ONLY &&
        d->test.afail != RD_AFAIL_RGB_ONLY) {
        return false;
    }
    RhiRect sc;
    if (!scissorRect(r, tc, &sc) || sc.x != 0 || sc.y != 0 || sc.w != tc->tw || sc.h != tc->th) {
        return false;
    }
    /* the origin frameRefresh gives a post sprite (drawOffset, screenCopy) */
    const uint32_t use =
        r->halfAdded ? r->st.useOffset & ~(uint32_t)RD_TARGET_HALF_Y : r->st.useOffset;
    const int64_t ox = 16 * (2048 - (int64_t)(r->st.gsW >> 1));
    const int64_t oy = 16 * (2048 - (int64_t)(r->st.gsH >> 1)) + ((use & RD_TARGET_HALF_Y) ? 8 : 0);
    const int64_t x0 = (int64_t)p->rect[0], y0 = (int64_t)p->rect[1];
    const int64_t x1 = (int64_t)p->rect[2], y1 = (int64_t)p->rect[3];
    return tc->w > 0 && tc->h > 0 && x0 <= ox && x1 > ox + 16 * ((int64_t)tc->w - 1) && y0 <= oy &&
           y1 > oy + 16 * ((int64_t)tc->h - 1);
}

/* The target's colour and its snapshot change places: the snapshot holds
 * the picture before the sprite (all of it, as a whole copy would), the
 * colour is the texture the sprite draws into.  The pass ends first and
 * the old colour leaves its attachment state (closing the MSAA epoch on
 * SCENE, recording a pending clear) before it is the snapshot. */
static bool blurSwapTarget(Replay *r, RdTargetRec *tc)
{
    endPass(r);
    if (!snapEnsure(tc)) {
        return false;
    }
    rd__transition(s_cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    const RhiTexture old = tc->color;
    const RhiState oldState = tc->colorState;
    tc->color = tc->snap;
    tc->colorState = tc->snapState;
    tc->snap = old;
    tc->snapState = oldState;
    if (s_ms.sceneColor == old.id) {
        s_ms.sceneColor = tc->color.id; /* SCENE's colour for the epoch's checks */
    }
    g_rdPerf.snapSwaps++;
    s_blurSnapCount++;
    return true;
}

/* fx_sprite_ps's flags (DrawCB.mode.x) for a staticBlur sprite: the texture
 * function and sampling, ABE, PABE, FBA, DATE and the destination read */
static uint32_t blurFlags(const RdDrawState *d, uint32_t lines, int textured, int dstRead)
{
    uint32_t fl = 0;
    if (textured) {
        fl |= RD_FXF_TEXTURED | ((lines & 3u) << RD_FXF_TFX_SHIFT);
        if (d->texFn == RD_TEXFN_DECAL && (lines & 3u) == 0) {
            fl |= 1u << RD_FXF_TFX_SHIFT; /* a DECAL bound through rd_texture */
        }
        if (d->tcc == RD_TCC_RGBA) {
            fl |= RD_FXF_TCC;
        }
        if (d->magFilter == RD_FILTER_LINEAR) {
            fl |= RD_FXF_LINEAR;
        }
        if (d->wrap.s == RD_WRAP_CLAMP) {
            fl |= RD_FXF_CLAMP_S;
        }
        if (d->wrap.t == RD_WRAP_CLAMP) {
            fl |= RD_FXF_CLAMP_T;
        }
    }
    if (d->abe) {
        fl |= RD_FXF_ABE;
    }
    if (d->pabe) {
        fl |= RD_FXF_PABE;
    }
    if (d->fba) {
        fl |= RD_FXF_FBA;
    }
    if (d->test.date != RD_DATE_OFF) {
        fl |= RD_FXF_DATE | (d->test.date == RD_DATE_DEST_ALPHA_1 ? RD_FXF_DATM : 0u);
    }
    if (dstRead) {
        fl |= RD_FXF_DST;
    }
    return fl;
}

/* Whether a staticBlur sprite writes its own colour into every texel of
 * its target and touches nothing else: untextured, without blend, alpha
 * test, DATE, destination read or depth, all four channels written whole,
 * and its rect and the scissor holding the whole target.  fx_sprite_ps then
 * returns RGBAQ (alpha with FBA's bit) / 255 at every texel, which a clear
 * to the same value stores byte for byte.  The aura's buffer is cleared
 * this way every frame (auraInspireBefore: a black sprite over AURA_WORK,
 * a scene-sized target).  Coverage as fx_sprite_ps has it: texel px stands
 * for GS pixel floor(px / s), at window coordinate origin + 16 that, kept
 * when inside [x0, x1); the last texel's GS pixel is held a hundredth of
 * a pixel inside the target so the GPU's division cannot round it out. */
static bool blurSpriteClears(const Replay *r, const RdTargetRec *tc, const RdPostRec *p,
                             const RdDrawState *d, int textured, int dstRead, int useDepth)
{
    if (textured || dstRead || useDepth || d->abe || d->test.date != RD_DATE_OFF ||
        (d->test.ate && d->test.atst != RD_ATST_ALWAYS) || d->colorMask != 0xF || d->fbmsk != 0 ||
        r->st.useOffset != 0 || r->st.gsW != tc->w || r->st.gsH != tc->h || tc->w == 0 ||
        tc->h == 0 || tc->tw == 0 || tc->th == 0 || !(tc->sx > 0.0f) || !(tc->sy > 0.0f)) {
        return false;
    }
    if ((float)(tc->tw - 1) / tc->sx + 0.01f >= (float)tc->w ||
        (float)(tc->th - 1) / tc->sy + 0.01f >= (float)tc->h) {
        return false;
    }
    const int32_t ox = 16 * (2048 - (int32_t)(r->st.gsW >> 1));
    const int32_t oy = 16 * (2048 - (int32_t)(r->st.gsH >> 1));
    if ((int32_t)p->rect[0] > ox || (int32_t)p->rect[1] > oy ||
        (int32_t)p->rect[2] <= ox + 16 * ((int32_t)tc->w - 1) ||
        (int32_t)p->rect[3] <= oy + 16 * ((int32_t)tc->h - 1)) {
        return false;
    }
    RhiRect sc;
    return scissorRect(r, tc, &sc) && sc.x == 0 && sc.y == 0 && sc.w == tc->tw && sc.h == tc->th;
}

/* ------------------------------------------------------------ staticBlur
 * RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR and the reduction's sprites
 * (RD_POST_REDUCTION): one GS sprite through fx_rect_vs and fx_sprite_ps
 * (rd_blur.c says what is modelled).  The state block's
 * colour target is drawn into; its depth target is bound when the Z test or
 * Z write needs it and has the colour target's size; the texture is the
 * state's (a target view or an image), read with Load.  When the sprite
 * blends, tests DATE or keeps the destination alpha, or samples the target
 * it draws into, the target is first copied into its snapshot, which the
 * shader reads as the destination (t2) and, sampling itself, as the
 * texture: the GS reads both before it writes. */
/* rd__blur_snapshot_count (rd_internal.h): the snapshots taken, copies and
   swaps alike (tests) */
uint64_t rd__blur_snapshot_count(void)
{
    return s_blurSnapCount;
}

/* rd__set_blur_fix128_skip (rd_internal.h) */
static bool s_blurFix128Skip = true;

void rd__set_blur_fix128_skip(bool on)
{
    s_blurFix128Skip = on;
}

/* rd__set_a1x_gs_keep (rd_internal.h) */
static bool s_a1xGsKeep = true;

void rd__set_a1x_gs_keep(bool on)
{
    s_a1xGsKeep = on;
}

/* Above 1x (fx_sprite.hlsl FXF_ABOVE1X): a sprite sampling target src into
 * tc when a scaled target is involved (either target's scale not 1, the
 * predicate rd__blur_screen_fallback takes): the steps, src texels per tc
 * texel in 1/32 (rounded), packed step32x * 4096 + step32y for
 * DrawCB.scale[3].  0, and the flag left off, when every scale is 1 (the
 * GS's own arithmetic, whatever the step: the PS2's 4:1 and 3.5:1 chain
 * steps stay its point samples), when neither axis leaves the GS's
 * filter (RD_FX_BOX_MIN32, RD_FX_BSPLINE_MAX32) or src is a work buffer. */
static uint32_t blurAbove1xSteps(const RdTargetRec *tc, const RdTargetRec *src, const float uv[4],
                                 const float rect[4], int linear)
{
    if (!src || (tc->sx == 1.0f && tc->sy == 1.0f && src->sx == 1.0f && src->sy == 1.0f) ||
        rd__work_class(src)) {
        return 0;
    }
    const double dx = fabs((double)rect[2] - (double)rect[0]);
    const double dy = fabs((double)rect[3] - (double)rect[1]);
    if (!(dx > 0.0) || !(dy > 0.0) || !(tc->sx > 0.0f) || !(tc->sy > 0.0f)) {
        return 0;
    }
    const double kx = fabs((double)uv[2] - (double)uv[0]) / dx * src->sx / tc->sx * 32.0;
    const double ky = fabs((double)uv[3] - (double)uv[1]) / dy * src->sy / tc->sy * 32.0;
    const uint32_t sx = kx >= RD_FX_STEP_MAX ? RD_FX_STEP_MAX : (uint32_t)floor(kx + 0.5);
    const uint32_t sy = ky >= RD_FX_STEP_MAX ? RD_FX_STEP_MAX : (uint32_t)floor(ky + 0.5);
    const int leaves =
        sx > RD_FX_BOX_MIN32 || sy > RD_FX_BOX_MIN32 ||
        (linear && ((sx > 0 && sx < RD_FX_BSPLINE_MAX32) || (sy > 0 && sy < RD_FX_BSPLINE_MAX32)));
    return leaves ? sx * 4096u + sy : 0;
}

/* fx_sprite_u_ps's texture group: t1 and t2 through their RGBA8_UINT views
 * (RHI_ASPECT_UINT), the sampler slot filled and unused; one per pair of
 * textures and bind epoch, as rd__tex_group_date caches its own */
static RhiBindGroup blurUintGroup(RhiTexture t1, RhiTexture t2)
{
    static struct {
        uint32_t epoch, t1, t2;
        RhiBindGroup group;
    } cache[16];

    const uint32_t h = ((t1.id * 2654435761u) ^ (t2.id * 2246822519u)) >> 28;
    if (cache[h].epoch == s_bindEpoch && cache[h].t1 == t1.id && cache[h].t2 == t2.id) {
        return cache[h].group;
    }
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = t1;
    b[0].aspect = RHI_ASPECT_UINT;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = g_rd.samplers[0];
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = t2;
    b[2].aspect = RHI_ASPECT_UINT;
    const RhiBindGroup g = bindGroup(g_rd.layoutTex, b, 3);
    if (g.id) {
        cache[h].epoch = s_bindEpoch;
        cache[h].t1 = t1.id;
        cache[h].t2 = t2.id;
        cache[h].group = g;
    }
    return g;
}

/* fx_sprite_ps's DrawCB.aux1 from the fields doBlurSpriteDraw filled
 * (fx_sprite.hlsl): x, y the UV divides' reciprocals 1 / (x1 - x0),
 * 1 / (y1 - y0) as float bits, correctly rounded (IEEE single, round to
 * nearest: gs_math.hlsli gs_div_trunc_rcp), 0 for an empty span, which
 * covers no pixel; z, w fx_texel's REPEAT and CLAMP size,
 * max(1, round(TEX0 size * t1's scale)) with the shader's float product and
 * its round half to even (DXC's RoundEven, D3D's round_ne), so the same
 * integers the shader computed per pixel. */
static void blurAux1(IcoDrawCB *cb)
{
    const int32_t dx = (int32_t)cb->param[2] - (int32_t)cb->param[0];
    const int32_t dy = (int32_t)cb->param[3] - (int32_t)cb->param[1];
    const float rx = dx > 0 ? 1.0f / (float)dx : 0.0f;
    const float ry = dy > 0 ? 1.0f / (float)dy : 0.0f;
    memcpy(&cb->aux1[0], &rx, sizeof(rx));
    memcpy(&cb->aux1[1], &ry, sizeof(ry));
    for (int k = 0; k < 2; k++) {
        const float sc = cb->scale[k] > 0.0f ? cb->scale[k] : 1.0f;
        const float l = rintf(cb->tex[k] * sc);
        cb->aux1[2 + k] = l >= 1.0f ? (uint32_t)l : 1u;
    }
}

static void doBlurSpriteDraw(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const uint32_t screenVtx = rd__blur_screen_fallback(c->b[0], &p, &r->st);
    if (screenVtx != ~0u) { /* the reduction on a scaled target: a hardware sprite */
        RdCmd sc = *c;
        sc.type = RDC_SCREEN;
        sc.b[0] = RD_PRIM_SPRITES;
        sc.b[1] = RD_SPACE_FULLFRAME;
        sc.b[2] = 1;
        sc.u[0] = 0;
        sc.u[1] = 2;
        /* S moved to a whole scene texel (rd_blur.c rd__blur_snap_verts), in
         * a frame of its own: the recorded pair stays as it is */
        RdScreenVtx snap[2];
        memcpy(snap, f->payload + screenVtx, sizeof(snap));
        rd__blur_snap_verts(&r->st, snap);
        RdFrame sf = *f;
        sf.payload = (uint8_t *)snap;
        /* the GS's filter on both axes at the sprite's steps (one SCENE
         * row a DISPLAY row at full height, scale 1): the flag left off,
         * as sprite_above1x would take the GS's path at every pixel */
        const RdTexRec *tr = r->st.ds.texEnabled ? rd__tex_rec(r->st.tex) : NULL;
        const RdTargetRec *dst = rd__target_rec(r->st.color);
        const RdTargetRec *src =
            tr && tr->kind == RD_TEXKIND_TARGET ? rd__target_rec(tr->target) : NULL;
        const int gs =
            s_a1xGsKeep && dst && src && blurAbove1xSteps(dst, src, p.uv, p.rect, 1) == 0;
        if (gs) {
            flushScreenRun(r); /* the run before keeps its own flags */
        }
        r->a1xGs = gs;
        doScreen(r, &sf, &sc);
        flushScreenRun(r); /* the fallback sprite is a run of its own */
        r->a1xGs = 0;
        return;
    }
    RdTargetRec *tc = rd__target_rec(r->st.color);
    if (!tc || !tc->color.id || tc->format != RHI_FMT_RGBA8_UNORM) {
        rd__log_once(RD_ONCE_BLUR, "staticBlur sprite without an RGBA8 colour target: skipped");
        return;
    }
    RdDrawState draw = r->st.ds;
    /* GS FIX 128: ((Cs - Cd) * 128 >> 7) + Cd is Cs, even with PABE.
     * Normalize this draw only; later packets still inherit the GS state. */
    draw.abe =
        draw.abe && !(s_blurFix128Skip && draw.blend == RD_BLEND_LERP_FIX && draw.blendFix == 128);
    const RdDrawState *d = &draw;
    RdTargetRec *td = rd__target_rec(r->st.depth);
    if (td && (!td->withDepth || !td->depth.id || td->tw != tc->tw || td->th != tc->th)) {
        td = NULL;
    }
    int useDepth = 0;
    const RdPipeKeyInt key =
        rd__blur_key(&r->st, tc->format, td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN, &useDepth, 0);
    if (d->test.ate && d->test.atst != RD_ATST_ALWAYS && d->test.afail == RD_AFAIL_FB_ONLY &&
        useDepth && d->zwrite == RD_ZWRITE_ON) {
        rd__log_once(RD_ONCE_BLUR, "staticBlur sprite with AFAIL FB_ONLY and Z write: failing "
                                   "fragments write Z");
    }

    /* the texture */
    RdTargetRec *src = NULL;
    RhiTexture srcTex = g_rd.dummy;
    uint32_t tw = 1, th = 1, tfmt = 0;
    float ssx = 1.0f, ssy = 1.0f; /* t1 texels per GS texel */
    int textured = 0;
    if (d->texEnabled) {
        RdTexRec *t = rd__tex_rec(r->st.tex);
        if (t && t->kind == RD_TEXKIND_IMAGE && t->rhi.id && t->state == RHI_STATE_SHADER_READ) {
            srcTex = t->rhi;
            /* texture packs: a replacement's UVs as replacementUv has them */
            tw = t->replacement && t->uvW && t->uvH ? t->uvW : t->w;
            th = t->replacement && t->uvW && t->uvH ? t->uvH : t->h;
            tfmt = t->src;
            textured = 1;
        } else if (t && t->kind == RD_TEXKIND_TARGET && t->view != RD_VIEW_DEPTH) {
            src = rd__target_rec(t->target);
            if (src && src->color.id && src->format == RHI_FMT_RGBA8_UNORM) {
                tw = src->tw;
                th = src->th;
                ssx = src->sx;
                ssy = src->sy;
                tfmt = t->src;
                textured = 1;
            } else {
                src = NULL;
            }
        }
    }
    const int dstRead = (d->abe && !(r->st.ds.blend == RD_BLEND_COUNT)) ||
                        d->test.date != RD_DATE_OFF ||
                        (d->test.ate && d->test.afail == RD_AFAIL_RGB_ONLY);
    const int selfSample = src == tc;
    /* as doFog: prepareDraw's other per-draw flags, not the previous draw's
     * (the scissor and blurSnapArea read mirror); fx_rect_vs covers the
     * target in its own GS pixels */
    r->mirror = 0;
    if (blurSpriteClears(r, tc, &p, d, textured, dstRead, useDepth)) {
        /* a clear instead of a full-target draw (blurSpriteClears) */
        const uint32_t a = p.rgba[3] | (d->fba ? 0x80u : 0u);
        const float col[4] = {(float)p.rgba[0] / 255.0f, (float)p.rgba[1] / 255.0f,
                              (float)p.rgba[2] / 255.0f, (float)(a & 0xFFu) / 255.0f};
        pendClear(r, r->st.color, tc, col, 0, 0);
        return;
    }
    float uv[4];
    rd__blur_uv_rect(c->b[0], &p, uv); /* the reduction's UVs under the mirror */
    /* above 1x: a target source and a scaled target (blurAbove1xSteps) */
    const uint32_t steps =
        textured ? blurAbove1xSteps(tc, src, p.uv, p.rect, d->magFilter == RD_FILTER_LINEAR) : 0;
    /* the frame-size post sprites' plan (rd_fx_plan.c): drawn without the snapshot */
    if (rd__fx_plan_draw(r, &(RdFxDraw){f, c, &p, d, tc, td, src, srcTex, &key, uv, tw, th, tfmt,
                                        steps, useDepth, textured, dstRead, selfSample})) {
        return;
    }
    RhiRect need = {0, 0, tc->tw, tc->th};
    if (dstRead || selfSample) {
        /* the texels the sprite reads from the snapshot: the destination
         * at its own fragments, the texture at its UVs and its kernel's
         * reach (blurSnapArea) */
        blurSnapArea(r, tc, &p, uv, dstRead, selfSample, steps, &need);
        if (blurCoversTarget(r, tc, &p, d, &key, useDepth)) {
            if (!blurSwapTarget(r, tc)) {
                return;
            }
        } else if (!takeSnap(r, tc, need)) {
            return;
        }
    }
    if (src) {
        if (selfSample) {
            srcTex = tc->snap;
        } else {
            if (src->colorState != RHI_STATE_SHADER_READ) {
                endPass(r);
                rd__transition(s_cl, src->color, &src->colorState, RHI_STATE_SHADER_READ);
            }
            srcTex = src->color;
        }
    }
    /* fx_sprite_u_ps when every texture the sprite reads has the RGBA8_UINT
     * view (RdTargetRec.uintView): its source target, or the snapshot it
     * samples itself through, and the snapshot it reads the destination
     * from; an image never has it.  The same bytes as fx_sprite_ps */
    const int srcView = textured && src && (selfSample ? tc->uintView : src->uintView);
    const int useUint =
        (textured || dstRead) && (!textured || srcView) && (!dstRead || tc->uintView);
    const RdPipeKeyInt pkey =
        useUint
            ? rd__blur_key(&r->st, tc->format, td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN, &useDepth, 1)
            : key;

    DrawSetup ds;
    memset(&ds, 0, sizeof(ds));
    ds.tc = tc;
    ds.tdId = useDepth ? r->st.depth : 0;
    ds.depthFmt = useDepth ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
    ds.tex = srcTex;
    ds.dateTex = dstRead ? tc->snap : g_rd.dummy;
    ds.tw = tw;
    ds.th = th;
    ds.textured = textured;
    ds.screenCopy = 1; /* a post sprite: target texels onto screen pixels */
    if (useUint) {
        ds.tex = ds.dateTex = g_rd.dummy; /* bindDraw's group unused: the UINT one */
    }
    RhiBindGroup g2 = bindDraw(r, &ds);
    if (g2.id && useUint) {
        const RhiTexture t1 = textured ? srcTex : tc->snap;
        g2 = blurUintGroup(t1, dstRead ? tc->snap : t1);
    }
    if (!g2.id) {
        return;
    }
    RhiPipeline pipe = rd__get_pipeline(&pkey);
    if (!pipe.id) {
        return;
    }

    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    uint32_t fl = blurFlags(d, p.lines, textured, dstRead);
    if (steps) {
        fl |= RD_FXF_ABOVE1X;
        g_rd.above1xDraws++;
    }
    memcpy(cb.col, (const uint32_t[4]){p.rgba[0], p.rgba[1], p.rgba[2], p.rgba[3]}, sizeof(cb.col));
    cb.mode[0] = fl;
    cb.mode[1] = d->texa | (tfmt << 8);
    cb.mode[2] =
        d->test.atst | ((uint32_t)(d->test.ate != 0) << 8) | ((uint32_t)d->test.afail << 16);
    cb.mode[3] = d->test.aref;
    cb.blend[0] = rd__alpha_register(d->blend < RD_BLEND_COUNT ? d->blend : RD_BLEND_LERP_AS);
    cb.blend[1] = d->blendFix;
    cb.blend[2] = d->colclamp;
    cb.blend[3] = p.z;
    memcpy(cb.uvRect, uv, sizeof(cb.uvRect));
    cb.tex[0] = p.scalar[0];
    cb.tex[1] = p.scalar[1];
    cb.tex[2] = (float)tw;
    cb.tex[3] = (float)th;
    cb.scale[0] = ssx;
    cb.scale[1] = ssy;
    if (src && src->ox > 0.0f) {
        cb.scale[2] = src->ox / (float)src->tw; /* Full pixel (fx_sprite.hlsl fx_src_crop16) */
    }
    cb.scale[3] = (float)steps; /* below 2^24: exact */
    memcpy(cb.param, p.rect, sizeof(cb.param));
    blurAux1(&cb);
    if (useUint) {
        g_rdPerf.uintDraws++;
    }
    rd__set_pipeline(s_cl, pipe, &pkey);
    rd__bind_uniform(s_cl, 0, r->frameBG);
    rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(s_cl, 2, g2);
    drawCmd(s_cl, 3, 0, 1);
    g_rd.stats.draws++;
    if (key.gs.colorMask & 8) {
        r->writeSerial++;
    }
}

/* The reduction's textured sprite with Full pixel on (or the border mask:
 * borderOpen).  The game draws it
 * inside a scissor two columns and a few rows in from the frame's edge (the
 * black border), and with the wrap it never sets to CLAMP, so the last
 * column also blends in a quarter of the first.  Here the scissor is the
 * whole target and both axes clamp, so the border shows the picture SCENE
 * holds under it and the edges read their own texels.  The recorded scissor
 * and wrap stay in the frame (and in the dumps); this replaces them for this
 * one draw and puts them back after it, whichever way it returns.  The black
 * clear sprite before it is not textured and covers everything already. */
static void doBlurSprite(Replay *r, const RdFrame *f, const RdCmd *c)
{
    if (rd__replay_blur_fold(r, f, c)) {
        return; /* a pair folded into one pass (rd_replay_tiler.c) */
    }
    if (!borderOpen() || c->b[0] != RD_POST_REDUCTION || !r->st.ds.texEnabled) {
        doBlurSpriteDraw(r, f, c);
        return;
    }
    const RdTargetRec *tc = rd__target_rec(r->st.color);
    if (!tc) {
        doBlurSpriteDraw(r, f, c);
        return;
    }
    int32_t scissor[4];
    memcpy(scissor, r->st.scissor, sizeof(scissor));
    const RdSamplerWrap wrap = r->st.ds.wrap;
    r->st.scissor[0] = 0;
    r->st.scissor[1] = 0;
    r->st.scissor[2] = (int32_t)tc->w - 1;
    r->st.scissor[3] = (int32_t)tc->h - 1;
    r->st.ds.wrap.s = RD_WRAP_CLAMP;
    r->st.ds.wrap.t = RD_WRAP_CLAMP;
    doBlurSpriteDraw(r, f, c);
    memcpy(r->st.scissor, scissor, sizeof(scissor));
    r->st.ds.wrap = wrap;
}

/* ----------------------------------------------------------------- frame */

bool rd__begin_own_frame(uint64_t ringBytes)
{
    rd__wait_frame();
    s_slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    g_rd.replayCounter++;
    s_ringOff = 0;
    return rd__ensure_ring(ringBytes);
}

void rd__upload_textures(RhiCommandList cl)
{
    const RhiCommandList prev = s_cl;
    s_cl = cl;
    rd__upload_dirty_textures();
    s_cl = prev;
}

static const char *stubName(uint8_t type)
{
    switch (type) {
    case RDC_WORLD_PRIMS:
        return "rd_world_prims";
    case RDC_POST_STUB:
        return "an rd_post kind without a replay";
    default:
        return "unknown command";
    }
}

static bool replayFrame(const RdFrame *f, int keep, bool present);

/* the longest replay (CPU side: recording, pipeline creation, the
 * submit and present) since rd_replay_time_max last reset it, in ms */
static double s_replayMaxMs;

static uint32_t s_replayCount;

bool rd__replay_frame(const RdFrame *f, int keep, bool present)
{
    const double t0 = rd__now_ms();
    rd__perf_begin(f, keep, present);
    const bool ok = replayFrame(f, keep, present);
    g_rdPerf.uploadBytes = s_ringOff;
    rd__perf_end();
    const double ms = rd__now_ms() - t0;
    if (ms > s_replayMaxMs) {
        s_replayMaxMs = ms;
    }
    s_replayCount++;
    return ok;
}

double rd_replay_time_max(int reset, uint32_t *count)
{
    const double m = s_replayMaxMs;
    if (count) {
        *count = s_replayCount;
    }
    if (reset) {
        s_replayMaxMs = 0.0;
        s_replayCount = 0;
    }
    return m;
}

static bool replayFrame(const RdFrame *f, int keep, bool present)
{
    if (!g_rd.hasDevice || !f) {
        return false;
    }
    double t = rd__now_ms(), t1;
    rd__wait_frame();
    if (rhi_device_lost()) {
        /* nothing more reaches the GPU: no recording, so the pass ring
         * ends at the last replay submitted (rd_passring.h) */
        return false;
    }
    rd__perf_collect_gpu(); /* the timestamps of the slot just recycled */
    g_rdPerf.waitMs = (t1 = rd__now_ms()) - t;
    t = t1;
    s_slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    g_rd.replayCounter++;
    rd__passring_frame(g_rdPerf.replay, f->number, keep); /* the loss report's names */
    rd__diag_frame(NULL, 0);
    /* the pass window's count starts again (rd__replay_set_window) */
    g_rdWin.passes = g_rdWin.lastDraws = g_rdWin.drawn = 0;
    memset(&g_rdWin.note, 0, sizeof(g_rdWin.note));
    g_rdWin.lastSeq = g_rdWin.nextSeq = 0;
    g_rdWin.stop = false;
    rhi_set_submit_tag(g_rdPerf.replay, f->number);
    s_ringOff = 0;
    g_rd.deferText = false;
    if (present) {
        /* the overlay before the ring and the uploads (it adds to the
         * ring's size), and the deferred text first (sets g_rd.deferText) */
        rd__overlay_collect(f, keep);
    }
    if (!rd__ensure_ring(rd__estimate_ring(f, keep) + rd__overlay_ring_bytes())) {
        rd__log("could not allocate the upload ring");
        return false;
    }
    g_rdPerf.uploadMs = (t1 = rd__now_ms()) - t;
    t = t1;
    const bool doPresent = present && rd__present_acquire();
    g_rdPerf.acquireMs = (t1 = rd__now_ms()) - t;
    t = t1;
    s_cl = rhi_begin_commands();
    if (!s_cl.id) {
        return false;
    }
    s_vpExact = true; /* the lists' passes: the exact viewport (setViewport) */
    rd__perf_stamp(s_cl, RD_PERF_TS_BEGIN);
    /* no shadow window from another replay (rd__clear_new_targets reads it) */
    memset(&s_stencil, 0, sizeof(s_stencil));
    rhi_cmd_checkpoint(s_cl, rd__passring_push(RD_PASSK_UPLOAD, 0, 0, 0, 0, 0, 0, 0, -1));
    rd__perf_pass(s_cl, RD_PERF_PASS_UPLOAD, 0, 0, 0, -1);
    const uint64_t texOff = s_ringOff;
    rd__upload_dirty_textures();
    g_rdPerf.uploadTextureBytes = s_ringOff - texOff;
    rd__fog_lut_upload(f, keep); /* the fog's table, outside any pass (doFog) */
    rd__join_parts(f, keep);     /* the card test over a part's packets (rd_join.c) */
    rd__upload_meshes(f, keep);
    rd__passring_bytes(s_ringOff);
    rd__clear_new_targets();
    g_rdPerf.uploadMs += (t1 = rd__now_ms()) - t;
    t = t1;
    const double bind0 = g_rdPerf.bindMs;
    /* the walk from here: the vertex pre-pass, then the lists */
    rd__perf_stamp(s_cl, RD_PERF_TS_PREPASS);
    rd__vu_prepass(f, keep); /* each mesh and grid vertex once (rd_replay_vu.c) */
    rd__perf_stamp(s_cl, RD_PERF_TS_LISTS);

    g_rd.frameWExact = f->wExact; /* the present's exact width (rd__frame_wexact) */
    if (f->hasCamera) {
        rd__set_replay_camera(&f->camera);
    }
    Replay r;
    memset(&r, 0, sizeof(r));
    r.st = f->startState;
    r.perfPost = r.perfTargetEffect = -1;
    memset(&s_pend, 0, sizeof(s_pend)); /* no clear pending from another replay */
    msaaFrameBegin(&r);                 /* the MSAA epoch's count and scene pair */
    const uint32_t headHalf = sceneHeadHalf(f);
    for (int l = rd__first_list(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        rd__passring_list(l);
        /* the list for the blur fold's look-ahead (rd_replay_tiler.c) */
        rd__replay_fold_list(list->cmds, list->count, l, headHalf);
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            rd__replay_fold_at(i);
            if (g_rdWin.first) {
                /* the draw note's place (rd__replay_set_window_draws) */
                g_rdWin.list = (int8_t)l;
                g_rdWin.cmd = i;
                g_rdWin.tex = r.st.tex;
            }
            if (c->type == RDC_TARGET) {
                /* before the state takes the new target */
                r.perfPost = r.perfTargetEffect = perfTargetPost(&r, c);
            }
            if (rd__apply_state(&r.st, c)) {
                if (c->type == RDC_TARGET) {
                    r.halfAdded = sceneKeepsHalf(&r.st, l, headHalf);
                }
                continue;
            }
            if (c->type != RDC_SCREEN && c->type != RDC_NOP && c->type != RDC_OVERLAY_TEXT) {
                flushScreenRun(&r); /* the run's draw before this action's */
            }
            if (c->type == RDC_POST_STUB && rd__perf_post_of_kind(c->b[0]) >= 0) {
                r.perfPost = rd__perf_post_of_kind(c->b[0]);
            }
            if (c->type != RDC_NOP && c->type != RDC_OVERLAY_TEXT) {
                perfPost(&r, r.perfPost); /* the GPU timing's effect for this action */
            }
            switch (c->type) {
            case RDC_NOP:
            case RDC_OVERLAY_TEXT: /* collected by the present */
                break;
            case RDC_CLEAR:
                doClear(&r, c);
                break;
            case RDC_SCREEN:
                doScreen(&r, f, c);
                break;
            case RDC_EXACT_BLEND:
                doExact(&r, c);
                break;
            case RDC_COPY:
                doCopy(&r, f, c);
                break;
            case RDC_MESH:
            case RDC_SKINNED:
            case RDC_GRID:
            case RDC_PARTICLES:
                rd__replay_vu(&r, f, c);
                break;
            case RDC_SHADOW_RESET:
                doShadowReset(&r);
                break;
            case RDC_SHADOW_STRIP:
                doShadowStrip(&r, f, c);
                break;
            case RDC_SHADOW_RESOLVE:
                doShadowResolve(&r);
                break;
            case RDC_POST_STUB:
                if (c->b[0] == RD_POST_FOG) {
                    doFog(&r, f, c);
                    break;
                }
                if (rd__is_blur_kind(c->b[0])) {
                    doBlurSprite(&r, f, c);
                    break;
                }
                endPass(&r);
                rd__not_implemented(stubName(c->type));
                break;
            default:
                endPass(&r);
                rd__not_implemented(stubName(c->type));
                break;
            }
            /* the commands a fold consumed (their state applied) */
            i += rd__replay_fold_skip();
            if (g_rdWin.stop) {
                break; /* the pass window's end: this command began the next pass */
            }
        }
        rd__replay_fold_list(NULL, 0, l, headHalf);
        flushScreenRun(&r); /* a run does not cross a list (its timestamp) */
        rd__perf_stamp(s_cl, RD_PERF_TS_LIST0 + (uint32_t)l);
        /* the next list's work is the effect of the target in force, not
         * of this list's last post sprite: the motion blur, the last sprite
         * of list 7, would otherwise take every list up to the next TARGET
         * (list 12's) with it */
        r.perfPost = r.perfTargetEffect;
        if (g_rdWin.stop) {
            break;
        }
    }
    msaaEnd(&r); /* an epoch still open: SCENE resolved before anything reads it */
    endPass(&r);
    s_ms.r = NULL;
    rd__diag_frame(f, s_ms.want);
    s_vpExact = false; /* the present sets its own viewports */
    if (doPresent) {
        uint32_t sw = 0, sh = 0; /* the pass ring's present entry: DISPLAY onto the window */
        rhi_swapchain_size(&sw, &sh);
        const RdTargetRec *dt = rd__target_rec(RD_TARGET_DISPLAY + 1);
        rhi_cmd_checkpoint(s_cl,
                           rd__passring_push(RD_PASSK_PRESENT, RD_TARGET_DISPLAY + 1,
                                             dt ? dt->tw : 0, dt ? dt->th : 0, 0, sw, sh, 0, -1));
        rd__perf_pass(s_cl, RD_PERF_PASS_PRESENT, RD_PERF_ID_WINDOW, sw, sh, -1);
        rd__present_record(s_cl);
        rd__perf_stamp(s_cl, RD_PERF_TS_PRESENT);
    }
    rd__perf_pass_end(s_cl); /* the last piece's end (rd_perf.c) */
    g_rdPerf.walkMs = (t1 = rd__now_ms()) - t - (g_rdPerf.bindMs - bind0);
    rd__perf_walk_end(); /* the walk's pipelines and creations end here */
    t = t1;
    rhi_end_commands(s_cl);
    rhi_submit(s_cl);
    g_rdPerf.submitMs = (t1 = rd__now_ms()) - t;
    t = t1;
    if (doPresent) {
        rd__present_finish();
        rd__capture_finish(); /* a capture this present copied */
    }
    g_rdPerf.presentMs = rd__now_ms() - t;
    g_rd.stats.pipelines = rd__pipeline_count();
    g_rd.deferText = false;
    return true;
}

/* ------------------------------------------------------------- readback */

/* bpp: the format's bytes a texel (1 for the R8 coverage textures) */
static bool readTextureBpp(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, uint32_t bpp,
                           void *dst, size_t dstSize)
{
    if (!t.id || dstSize < (size_t)w * h * bpp) {
        return false;
    }
    if (*state != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            return false;
        }
        rd__transition(cl, t, state, RHI_STATE_COPY_SRC);
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    uint32_t pitch = 0;
    const double t0 = rd__now_ms();
    const bool ok = rhi_readback_texture(t, RHI_ASPECT_COLOR, dst, dstSize, &pitch);
    rd__perf_readback_ms(rd__now_ms() - t0); /* dumps and screenshots only */
    if (!ok) {
        return false;
    }
    if (pitch != w * bpp) {
        uint8_t *p = dst;
        for (uint32_t y = 1; y < h; y++) {
            memmove(p + (size_t)y * w * bpp, p + (size_t)y * pitch, (size_t)w * bpp);
        }
    }
    return true;
}

static bool readTexture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                        size_t dstSize)
{
    return readTextureBpp(t, state, w, h, 4, dst, dstSize);
}

bool rd__read_rhi_texture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                          size_t dstSize)
{
    return readTexture(t, state, w, h, dst, dstSize);
}

bool rd__read_texture(RdTex tex, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTexRec *t = rd__tex_rec(tex.id);
    if (!g_rd.hasDevice || !t || t->kind != RD_TEXKIND_IMAGE || !t->rhi.id ||
        t->state == RHI_STATE_UNDEFINED || rd__texel_is_block(t->format)) {
        return false; /* a pack's BC replacement has no texels to read back */
    }
    if (w) {
        *w = t->w;
    }
    if (h) {
        *h = t->h;
    }
    const bool ok =
        readTextureBpp(t->rhi, &t->state, t->w, t->h, rd__texel_bytes(t->format), dst, dstSize);
    /* draws sample image textures only in SHADER_READ (resolveTexture) */
    if (t->state != RHI_STATE_SHADER_READ) {
        RhiCommandList cl = rhi_begin_commands();
        if (cl.id) {
            rd__transition(cl, t->rhi, &t->state, RHI_STATE_SHADER_READ);
            rhi_end_commands(cl);
            rhi_submit(cl);
        }
    }
    return ok;
}

bool rd__read_target(RdTarget target, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__target_rec(target.id);
    if (!g_rd.hasDevice || !t || t->format == RHI_FMT_R8_UNORM) {
        return false;
    }
    if (w) {
        *w = t->tw;
    }
    if (h) {
        *h = t->th;
    }
    return readTexture(t->color, &t->colorState, t->tw, t->th, dst, dstSize);
}

/* A target's depth buffer as floats (tests). */
bool rd__read_target_depth(RdTarget target, float *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__target_rec(target.id);
    if (!g_rd.hasDevice || !t || !t->withDepth || !t->depth.id ||
        dstSize < (size_t)t->tw * t->th * sizeof(float)) {
        return false;
    }
    if (w) {
        *w = t->tw;
    }
    if (h) {
        *h = t->th;
    }
    if (t->depthState != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            return false;
        }
        rd__transition(cl, t->depth, &t->depthState, RHI_STATE_COPY_SRC);
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    uint32_t pitch = 0;
    if (!rhi_readback_texture(t->depth, RHI_ASPECT_DEPTH, dst, dstSize, &pitch)) {
        return false;
    }
    return pitch == t->tw * sizeof(float);
}

/* ---------------------------------------------------------- camera probe */

bool rd__camera_probe(const RdCamera *cam, const float p[4], float out[3][4])
{
    if (!g_rd.hasDevice || !cam) {
        return false;
    }

    /* one pixel per float: column = component, row = mul(g_view, p),
     * mul(g_proj, mul(g_view, p)), mul(g_viewProj, p) */
    enum { PW = 4, PH = 3 };

    RhiTexture t = rhi_create_texture(&(RhiTextureDesc){PW, PH, 1, RHI_FMT_RGBA8_UNORM,
                                                        RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                        "rd camera probe", 0});
    RhiState ts = RHI_STATE_UNDEFINED;
    if (!t.id) {
        return false;
    }
    rd__wait_frame();
    s_slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    g_rd.replayCounter++;
    s_ringOff = 0;
    bool ok = rd__ensure_ring(64 * 1024);
    s_cl = ok ? rhi_begin_commands() : (RhiCommandList){0};
    ok = ok && s_cl.id;
    if (ok) {
        const RdCamera saved = s_replayCam;
        const int savedHas = s_hasReplayCam;
        rd__set_replay_camera(cam);
        rd__upload_dirty_textures(); /* the dummy texture group 2 binds */
        rd__transition(s_cl, t, &ts, RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc pd;
        memset(&pd, 0, sizeof(pd));
        pd.color[0].texture = t;
        pd.color[0].load = RHI_LOAD_CLEAR;
        pd.colorCount = 1;
        pd.width = PW;
        pd.height = PH;
        rhi_cmd_begin_render_pass(s_cl, &pd);
        RhiViewport vp = {0.0f, 0.0f, (float)PW, (float)PH, 0.0f, 1.0f};
        rhi_cmd_set_viewport(s_cl, &vp);
        const RhiRect sc = {0, 0, PW, PH};
        rhi_cmd_set_scissor(s_cl, &sc);
        RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_CAMERA_PROBE, RHI_FMT_RGBA8_UNORM);
        RhiPipeline pipe = rd__get_pipeline(&k);
        if (pipe.id) {
            IcoDrawCB cb;
            memset(&cb, 0, sizeof(cb));
            memcpy(cb.param, p, sizeof(cb.param));
            cb.uvRect[2] = cb.uvRect[3] = 1.0f;
            cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
            rd__set_pipeline(s_cl, pipe, &k);
            rd__bind_uniform(s_cl, 0, rd__frame_group(PW, PH, 0.0f, 0.0f));
            rd__bind_uniform(s_cl, 1, rd__draw_group(&cb));
            rhi_cmd_set_bind_group(
                s_cl, 2,
                rd__tex_group(g_rd.dummy, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                      RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
            drawCmd(s_cl, 3, 0, 1);
        }
        rhi_cmd_end_render_pass(s_cl);
        rhi_end_commands(s_cl);
        rhi_submit(s_cl);
        s_replayCam = saved;
        s_hasReplayCam = savedHas;
        ok = pipe.id != 0;
    }
    uint8_t px[PW * PH * 4];
    ok = ok && readTexture(t, &ts, PW, PH, px, sizeof(px));
    rhi_wait_idle();
    rhi_destroy_texture(t);
    if (ok) {
        for (int r = 0; r < PH; r++) {
            for (int c = 0; c < PW; c++) {
                const uint8_t *b = &px[(r * PW + c) * 4];
                const uint32_t bits = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                                      ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
                memcpy(&out[r][c], &bits, 4);
            }
        }
    }
    return ok;
}

bool rd__read_present(void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    if (!g_rd.hasDevice || !g_rd.presentOut.id) {
        return false;
    }
    if (w) {
        *w = g_rd.presentOutW;
    }
    if (h) {
        *h = g_rd.presentOutH;
    }
    return readTexture(g_rd.presentOut, &g_rd.presentOutState, g_rd.presentOutW, g_rd.presentOutH,
                       dst, dstSize);
}

/* ------------------------------------------------- for rd_replay_vu.c
 * The replay's own helpers the VU draws call (rd_replay.h). */
void rd__replay_begin_pass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did,
                           RhiLoadOp colorLoad, const float clear[4], RhiLoadOp depthLoad,
                           float clearDepth, bool storage)
{
    beginPass(r, c, d, cid, did, colorLoad, clear, depthLoad, clearDepth, storage);
}

RhiBindGroup rd__replay_bind_draw_keep(Replay *r, const DrawSetup *ds)
{
    return bindDrawKeep(r, ds);
}

RhiBindGroup rd__replay_bind_group(RhiBindGroupLayout layout, const RhiBinding *b, uint32_t n)
{
    return bindGroup(layout, b, n);
}

uint32_t rd__replay_bind_epoch(void)
{
    return s_bindEpoch;
}

void rd__replay_draw(RhiCommandList cl, uint32_t vertexCount, uint32_t firstVertex,
                     uint32_t instanceCount)
{
    drawCmd(cl, vertexCount, firstVertex, instanceCount);
}

const void *rd__replay_pass_push(RdPassKind kind, uint32_t color, uint32_t cw, uint32_t ch,
                                 uint32_t depth, uint32_t dw, uint32_t dh, int storage, int post)
{
    return passPush(kind, color, cw, ch, depth, dw, dh, storage, post);
}

void rd__replay_draw_indexed(RhiCommandList cl, uint32_t indexCount, uint32_t firstIndex,
                             int32_t vertexOffset, uint32_t instanceCount)
{
    drawIndexedCmd(cl, indexCount, firstIndex, vertexOffset, instanceCount);
}

void rd__replay_dst_bind(Replay *r, const RdPipeKeyInt *k)
{
    dstBind(r, k);
}

void rd__replay_dst_pass_mode(Replay *r, const RdPipeKeyInt *k)
{
    dstPassMode(r, k);
}

int rd__replay_expand_passes(const Replay *r, const RdDrawPass *in, int n, RdDrawPass out[8])
{
    return expandPasses(r, in, n, out);
}

void rd__replay_fill_draw_cb(const Replay *r, const RdDrawPass *dp, const DrawSetup *ds,
                             IcoDrawCB *cb)
{
    fillDrawCB(r, dp, ds, cb);
}

bool rd__replay_prepare_draw(Replay *r, DrawSetup *ds, const RhiRect *dateArea)
{
    return prepareDrawEx(r, ds, dateArea, true); /* the VU draws: DATE may take the stencil */
}

uint32_t rd__replay_msaa_want(void)
{
    return s_ms.want;
}

/* rd_replay_tiler.c's helpers (rd_replay.h): the functions of this file
 * named beside them */
void rd__replay_flush_run(Replay *r)
{
    flushScreenRun(r);
}

RhiBindGroup rd__replay_bind_draw(Replay *r, const DrawSetup *ds)
{
    return bindDraw(r, ds);
}

bool rd__replay_scissor(const Replay *r, const RdTargetRec *tc, RhiRect *sc)
{
    return scissorRect(r, tc, sc);
}

uint32_t rd__replay_blur_flags(const RdDrawState *d, uint32_t lines, int textured, int dstRead)
{
    return blurFlags(d, lines, textured, dstRead);
}

void rd__replay_blur_aux1(IcoDrawCB *cb)
{
    blurAux1(cb);
}

bool rd__replay_blur_covers(const Replay *r, const RdTargetRec *tc, const RdPostRec *p,
                            const RdDrawState *d, const RdPipeKeyInt *key, int useDepth)
{
    return blurCoversTarget(r, tc, p, d, key, useDepth);
}

bool rd__replay_blur_swap(Replay *r, RdTargetRec *tc)
{
    return blurSwapTarget(r, tc);
}

bool rd__replay_take_snap(Replay *r, RdTargetRec *tc, RhiRect area)
{
    return takeSnap(r, tc, area);
}

int rd__replay_perf_target_post(const Replay *r, const RdCmd *c)
{
    return perfTargetPost(r, c);
}

void rd__replay_perf_post(Replay *r, int post)
{
    perfPost(r, post);
}

bool rd__replay_blur_fix128_skip(void)
{
    return s_blurFix128Skip;
}

bool rd__replay_exact_readable(uint32_t target)
{
    const RdTargetRec *c = rd__target_rec(target);
    return c && exactReadable(c);
}
