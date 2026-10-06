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
 *                    for feedback passes, RENDER_API.md "Blend exactness under feedback")
 *   RDC_COPY         texture copy (gif_MoveImage)
 *   RDC_MESH, RDC_SKINNED, RDC_GRID, RDC_PARTICLES
 *                    the VU1 program shaders (wave 3, R3ab; doVu below)
 *   RDC_SHADOW_RESET, RDC_SHADOW_STRIP, RDC_SHADOW_RESOLVE
 *   RDC_POST_STUB    of kind RD_POST_FOG (wave 4, R4c): doFog; of kinds
 *                    RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR (wave 5, R5a):
 *                    doBlurSprite, staticBlur.c's sprites through fx_sprite_ps
 *                    the shadow count on the stencil (wave 4, R4b;
 *                    rd_shadow.c, doShadow* below)
 *   later waves      rd__NotImplemented
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
 * Render passes stay open across draws to the same colour/depth pair and
 * are closed by a target change, a clear, a copy, or a draw that samples a
 * target whose state must change.  rd tracks every texture's RhiState
 * (backends do not).  A draw that samples its own render target samples a
 * copy taken just before it (the GS reads the live buffer).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "rd_tex.h"
#include "shader_consts.h"
#include "shaders_gen.h"

static const char *const s_vsNames[RD_VS_COUNT] = {"sprite_ui_vs",
                                                   "sprite_world_vs",
                                                   "blit_vs",
                                                   "blend_int_vs",
                                                   "vu_prelit_vs",
                                                   "vu_lit_vs",
                                                   "vu_lit_spec_vs",
                                                   "vu_reflect_vs",
                                                   "vu_skin_vs",
                                                   "vu_skin_spec_vs",
                                                   "vu_skin_debug_vs",
                                                   "vu_grid_vs",
                                                   "vu_grid_lit_vs",
                                                   "vu_grid_spec_vs",
                                                   "vu_particle_vs",
                                                   "fx_rect_vs" /* wave 5 (R5a) */,
                                                   "sprite_aa1_ui_vs",
                                                   "sprite_aa1_world_vs" /* package AA1 */,
                                                   "sprite_stq_ui_vs",
                                                   "sprite_stq_world_vs" /* package RSMALL */,
                                                   "crt_vs" /* package CRT */};

static const char *const s_fsNames[RD_FS_COUNT] = {"sprite_ps",
                                                   "blit_ps",
                                                   "blend_int_ps",
                                                   "date_snap_ps",
                                                   "camera_probe_ps",
                                                   "vu_ps",
                                                   "fog_lut_ps",
                                                   "fx_sprite_ps" /* wave 5 (R5a) */,
                                                   "wrap_acc_ps",
                                                   "wrap_resolve_ps" /* wave 5 (R5c) */,
                                                   "font_ps" /* package R8 */,
                                                   "sprite_aa1_ps" /* package AA1 */,
                                                   "box_reduce_ps" /* package RSMALL */,
                                                   "sprite_stq_ps" /* package RSMALL */,
                                                   "crt_bloom_ps",
                                                   "crt_blur_ps",
                                                   "crt_ps" /* package CRT */};

/* ------------------------------------------------------------------ init */

static RhiShader makeShader(const char *name)
{
    const IcoShaderBlob *b = ico_FindShader(name);
    if (!b) {
        rd__Log("shader %s missing from the table", name);
        return (RhiShader){0};
    }
    RhiShaderDesc d;
    memset(&d, 0, sizeof(d));
    d.stage = b->stage == ICO_SHADER_STAGE_VERTEX ? RHI_STAGE_VERTEX : RHI_STAGE_FRAGMENT;
    if (rhi_Backend() == RHI_BACKEND_D3D12) {
        d.bytecode = b->dxil;
        d.bytecodeSize = b->dxil_len;
    } else {
        d.bytecode = b->spirv;
        d.bytecodeSize = b->spirv_len;
    }
    d.entryPoint = b->entry;
    d.debugName = b->name;
    return rhi_CreateShader(&d);
}

/* rhi_Init succeeded: rd__GpuShutdown has a device to tear down (without a
 * Vulkan loader rhi_Init fails and there is nothing to wait on or destroy). */
static bool s_rhiUp;

bool rd__GpuInit(void *sdlWindow)
{
    RhiDeviceDesc dd;
    memset(&dd, 0, sizeof(dd));
    dd.sdlWindow = sdlWindow;
    dd.vsync = g_rd.settings.vsync != 0;
    dd.appName = "ico";
    if (!rhi_Init(&dd)) {
        return false;
    }
    rd__PerfReset(); /* P1: the new device's counters start at 0 */
    s_rhiUp = true;
    const RhiLimits *lim = rhi_Limits();
    if (!lim->dualSourceBlend || !lim->stencilWrap) {
        rd__Log("device lacks dual-source blend or stencil wrap");
        return false;
    }
    const uint32_t VS = 1u << RHI_STAGE_VERTEX, FS = 1u << RHI_STAGE_FRAGMENT;
    /* package PA: FrameCB, DrawCB, VuCB and VuBoneCB take dynamic offsets
     * into the ring, so each of these layouts has one group a replay
     * (uniformGroup, vuGroup) */
    if (lim->maxDynamicUniforms < 4) {
        rd__Log("device has %u dynamic uniform buffers, 4 needed", lim->maxDynamicUniforms);
        return false;
    }
    const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS}};
    const RhiBindSlot s1[1] = {{1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS}};
    /* t2: sprite_ps's DATE snapshot (wave 2) */
    const RhiBindSlot s2[3] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                               {1, RHI_BIND_SAMPLER, FS},
                               {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    const RhiBindSlot s3[2] = {{1, RHI_BIND_SAMPLED_TEXTURE, FS},
                               {2, RHI_BIND_SAMPLED_TEXTURE, FS}};
    g_rd.layoutFrame = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s0, 1, "rd frame"});
    g_rd.layoutDraw = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s1, 1, "rd draw"});
    g_rd.layoutTex = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s2, 3, "rd tex"});
    g_rd.layoutInt = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s3, 2, "rd int"});
    /* wave 3 (R3ab): the VU programs' group 1 (vu_common.hlsli, SHADERS.md) */
    const RhiBindSlot s4[4] = {{0, RHI_BIND_STORAGE_BUFFER, VS},
                               {1, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS | FS},
                               {2, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS},
                               {3, RHI_BIND_UNIFORM_BUFFER_DYNAMIC, VS}};
    g_rd.layoutVu = rhi_CreateBindGroupLayout(&(RhiBindGroupLayoutDesc){s4, 4, "rd vu"});
    bool ok = g_rd.layoutFrame.id && g_rd.layoutDraw.id && g_rd.layoutTex.id && g_rd.layoutInt.id &&
              g_rd.layoutVu.id;
    for (int i = 0; i < RD_VS_COUNT; i++) {
        g_rd.vs[i] = makeShader(s_vsNames[i]);
        ok = ok && g_rd.vs[i].id;
    }
    for (int i = 0; i < RD_FS_COUNT; i++) {
        g_rd.fs[i] = makeShader(s_fsNames[i]);
        ok = ok && g_rd.fs[i].id;
    }
    /* set 0: the Original samplers (one level); wave 7 (R7a) sets 1 and 2:
     * the Enhanced filter's trilinear and anisotropic ones over the
     * generated mips (uploadTextures) */
    for (int i = 0; i < RD_SAMPLER_COUNT * RD_SAMPLER_SETS; i++) {
        const int set = i / RD_SAMPLER_COUNT, k = i % RD_SAMPLER_COUNT;
        RhiSamplerDesc sd;
        memset(&sd, 0, sizeof(sd));
        sd.mag = (k & 1) ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.min = (k & 2) ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.mip = set ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
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
        g_rd.samplers[i] = rhi_CreateSampler(&sd);
        ok = ok && g_rd.samplers[i].id;
    }
    g_rd.dummy = rhi_CreateTexture(&(RhiTextureDesc){
        1, 1, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd dummy"});
    g_rd.dummyState = RHI_STATE_UNDEFINED;
    return ok && g_rd.dummy.id;
}

void rd__GpuShutdown(void)
{
    if (!s_rhiUp) {
        return;
    }
    rhi_WaitIdle();
    for (int i = 0; i < RD_SCRATCH_COUNT; i++) {
        if (g_rd.scratch[i].tex.id) {
            rhi_DestroyTexture(g_rd.scratch[i].tex);
        }
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (g_rd.ring[i].id) {
            rhi_DestroyBuffer(g_rd.ring[i]);
        }
    }
    rd__PipelineCacheClear();
    rd__MeshGpuShutdown(); /* P1: the mesh arena */
    rd__FogShutdown();     /* wave 4 (R4c) */
    rd__ShadowShutdown();  /* package RSMALL */
    rd__WrapShutdown();    /* wave 5 (R5c) */
    if (g_rd.dummy.id) {
        rhi_DestroyTexture(g_rd.dummy);
    }
    for (int i = 0; i < RD_SAMPLER_COUNT * RD_SAMPLER_SETS; i++) {
        if (g_rd.samplers[i].id) {
            rhi_DestroySampler(g_rd.samplers[i]);
        }
    }
    for (int i = 0; i < RD_VS_COUNT; i++) {
        if (g_rd.vs[i].id) {
            rhi_DestroyShader(g_rd.vs[i]);
        }
    }
    for (int i = 0; i < RD_FS_COUNT; i++) {
        if (g_rd.fs[i].id) {
            rhi_DestroyShader(g_rd.fs[i]);
        }
    }
    RhiBindGroupLayout ls[5] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex, g_rd.layoutInt,
                                g_rd.layoutVu};
    for (int i = 0; i < 5; i++) {
        if (ls[i].id) {
            rhi_DestroyBindGroupLayout(ls[i]);
        }
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        g_rd.ring[i] = (RhiBuffer){0};
        g_rd.ringMap[i] = NULL;
        g_rd.ringCap[i] = 0;
    }
    memset(g_rd.scratch, 0, sizeof(g_rd.scratch));
    rhi_Shutdown();
    s_rhiUp = false;
}

/* ------------------------------------------------------------- helpers */

static RhiCommandList s_cl;

static uint32_t s_slot;

static uint64_t s_ringOff;

void rd__Transition(RhiCommandList cl, RhiTexture t, RhiState *cur, RhiState want)
{
    if (!t.id || *cur == want) {
        return;
    }
    RhiTextureBarrier b = {t, *cur, want};
    rhi_CmdBarrier(cl, &b, 1);
    *cur = want;
}

/* Package P1: every transient bind group of the replay goes through here,
 * timed into the record's bind phase (PA: and counted by kind). */
static RhiBindGroup bindGroup(RhiBindGroupLayout layout, const RhiBinding *b, uint32_t n)
{
    const double t0 = rd__NowMs();
    const RhiBindGroup g = rhi_CreateBindGroup(&(RhiBindGroupDesc){layout, b, n});
    g_rdPerf.bindMs += rd__NowMs() - t0;
    if (g.id) {
        if (layout.id == g_rd.layoutTex.id || layout.id == g_rd.layoutInt.id) {
            g_rdPerf.textureGroups++;
        } else {
            g_rdPerf.uniformGroups++;
        }
    }
    return g;
}

uint64_t rd__RingAlloc(uint64_t size, uint64_t align)
{
    uint64_t off = (s_ringOff + align - 1) / align * align;
    if (off + size > g_rd.ringCap[s_slot]) {
        rd__Log("upload ring overflow (%llu + %llu > %llu)", (unsigned long long)off,
                (unsigned long long)size, (unsigned long long)g_rd.ringCap[s_slot]);
        return ~0ull;
    }
    s_ringOff = off + size;
    return off;
}

/* Package P1: bind groups are transient (rhi.h: valid in the frame slot
 * they were made in), so the caches below are tagged with the epoch, which
 * every rhi_WaitFrame of the renderer starts anew (rd__WaitFrame). */
static uint32_t s_bindEpoch = 1;

void rd__WaitFrame(void)
{
    rhi_WaitFrame();
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

/* Package PA: the uniform layouts' groups.  FrameCB, DrawCB, VuCB and
 * VuBoneCB are RHI_BIND_UNIFORM_BUFFER_DYNAMIC slots: a group binds the
 * ring at offset 0 with the block's size, and each draw passes its block's
 * ring offset when it binds the group (rd__BindUniform).  So the frame and
 * draw layouts have one group per replay (per ring buffer, which does not
 * change within one), whatever the number of draws. */
#define RD_DYN_GROUPS 4

typedef struct DynEntry {
    uint32_t epoch, layout, buffer;
    RhiBindGroup group;
} DynEntry;

static DynEntry s_dynGroups[RD_DYN_GROUPS];

static RhiBindGroup dynamicGroup(RhiBindGroupLayout layout, uint32_t slot, uint32_t size)
{
    const RhiBuffer ring = g_rd.ring[s_slot];
    DynEntry *free = NULL;
    for (int i = 0; i < RD_DYN_GROUPS; i++) {
        DynEntry *e = &s_dynGroups[i];
        if (e->epoch == s_bindEpoch && e->layout == layout.id && e->buffer == ring.id) {
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
        free->group = g;
    }
    return g;
}

void rd__BindUniform(RhiCommandList cl, uint32_t group, RdUniform u)
{
    rhi_CmdSetBindGroupOffsets(cl, group, u.group, &u.offset, 1);
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
    const uint64_t off = rd__RingAlloc(size, rhi_Limits()->uniformAlign);
    if (off == ~0ull) {
        return (RdUniform){{0}, 0};
    }
    memcpy(g_rd.ringMap[s_slot] + off, data, size);
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

/* The camera of the frame being replayed (R2c): the frame's own, else the
 * last one a replayed frame carried (the VU keeps its matrices until the
 * game uploads new ones). */
static RdCamera s_replayCam;

static int s_hasReplayCam;

void rd__SetReplayCamera(const RdCamera *cam)
{
    if (cam) {
        s_replayCam = *cam;
        s_hasReplayCam = 1;
    }
}

RdUniform rd__FrameGroup(uint32_t targetW, uint32_t targetH, float originX, float originY)
{
    return rd__FrameGroupZ(targetW, targetH, originX, originY, 1.0f / 16777216.0f);
}

RdUniform rd__FrameGroupZ(uint32_t targetW, uint32_t targetH, float originX, float originY,
                          float zScale)
{
    return rd__FrameGroupEx(targetW, targetH, originX, originY, zScale, 1.0f, 1.0f, 1.0f);
}

RdUniform rd__FrameGroupEx(uint32_t targetW, uint32_t targetH, float originX, float originY,
                           float zScale, float spaceX, float scaleX, float scaleY)
{
    IcoFrameCB cb;
    memset(&cb, 0, sizeof(cb));
    rd__FillCameraCB(&cb, s_hasReplayCam ? &s_replayCam : NULL);
    cb.target[0] = (float)targetW;
    cb.target[1] = (float)targetH;
    cb.target[2] = 1.0f / (float)targetW;
    cb.target[3] = 1.0f / (float)targetH;
    cb.origin[0] = originX;
    cb.origin[1] = originY;
    /* GS integer coordinates on GPU pixel centres; R7a: on a scaled target,
     * on the centre of the block's first texel (0.5 texel = 0.5 / s GS
     * pixels), so integer edges are block edges (rd_replay.c expand) */
    cb.origin[2] = scaleX > 1.0f ? 0.5f / scaleX : 0.5f;
    cb.origin[3] = scaleY > 1.0f ? 0.5f / scaleY : 0.5f;
    /* Original: both spaces identity.  Wave 7 (R7a): the wide projection's
     * x scale about the target's centre (spaceX = (4/3) / aspect) for draws
     * into a scene-class target other than full-screen ones (bindDraw);
     * the mirror would also go here. */
    cb.space[0][0] = cb.space[1][0] = spaceX;
    cb.space[0][1] = cb.space[1][1] = 1.0f;
    /* the GS Z scale of the bound depth buffer: 2^-32 for the game's PSMZ32
     * (R2c), so UI Z values above 2^24 keep their order */
    cb.z[0] = zScale;
    cb.z[1] = scaleX; /* R7a: the bound target's texels per GS pixel */
    cb.z[2] = scaleY;
    /* S2: the meshes' vertices off the 12.4 grid where the target is finer
     * than it (Enhanced only; Original keeps the GS's quantisation) */
    cb.z[3] = g_rd.settings.preset == RD_PRESET_ENHANCED && (scaleX > 1.0f || scaleY > 1.0f) &&
                      !rd__S2Legacy()
                  ? 1.0f
                  : 0.0f;
    cb.misc[0] = (float)g_rd.replayCounter;
    cb.misc[1] = (float)g_rd.settings.preset;
    return uniformGroup(g_rd.layoutFrame, 0, &cb, sizeof(cb));
}

RdUniform rd__DrawGroup(const void *drawCB)
{
    return uniformGroup(g_rd.layoutDraw, 1, drawCB, sizeof(IcoDrawCB));
}

RdUniform rd__CrtGroup(const void *crtCB)
{
    return uniformGroup(g_rd.layoutDraw, 1, crtCB, sizeof(IcoCrtCB));
}

/* Package P1: texture groups by (texture, sampler, DATE snapshot) */
#define RD_TEX_CACHE 512

typedef struct TexEntry {
    uint32_t epoch, tex, sampler, date;
    RhiBindGroup group;
} TexEntry;

static TexEntry s_texCache[RD_TEX_CACHE];

RhiBindGroup rd__TexGroupDate(RhiTexture t, RhiSampler s, RhiTexture date)
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

RhiBindGroup rd__TexGroup(RhiTexture t, RhiSampler s)
{
    return rd__TexGroupDate(t, s, g_rd.dummy);
}

RhiSampler rd__Sampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t)
{
    int i = (mag ? 1 : 0) | (min ? 2 : 0) | (s ? 4 : 0) | (t ? 8 : 0);
    return g_rd.samplers[i];
}

/* Wave 7 (R7a): the sampler of a game texture.  With the Enhanced filter
 * on and the texture mipmapped, a linearly minified texture is sampled
 * trilinear (or anisotropic); textures authored nearest stay nearest, and
 * everything is the Original sampler otherwise. */
static RhiSampler texSampler(RdFilter mag, RdFilter min, RdWrap s, RdWrap t, int mipmapped)
{
    int i = (mag ? 1 : 0) | (min ? 2 : 0) | (s ? 4 : 0) | (t ? 8 : 0);
    if (mipmapped && min == RD_FILTER_LINEAR && g_rd.filterUpgrade) {
        i += RD_SAMPLER_COUNT * (g_rd.filterUpgrade >= RD_FILTER_UPGRADE_ANISOTROPIC ? 2 : 1);
    }
    return g_rd.samplers[i];
}

/* ---------------------------------------------------------------- passes */

typedef struct Replay {
    RdStateBlock st;
    int passOpen;
    uint32_t passColor, passDepth;
    RdUniform frameBG;
    uint32_t frameKey[6]; /* colour, gsW, gsH, useOffset, pass serial, full-screen (R7a) */
    int stretch;          /* R7a: the draw being bound is full-screen (no wide x scale) */
    int mirror;           /* R7c: the draw being bound is a flipped UI draw (scissor too) */
    int uiPrim; /* the draw being bound is a UI-space screen prim (RSMALL: its scissor takes the wide scale) */
    uint32_t passSerial;
    uint32_t writeSerial; /* bumped by every action that may change a target's alpha */
    uint32_t dateFor;     /* target id the DATE snapshot holds, 0 = none */
    uint32_t dateSerial;  /* writeSerial when it was taken */
} Replay;

static void endPass(Replay *r)
{
    if (r->passOpen) {
        rhi_CmdEndRenderPass(s_cl);
        r->passOpen = 0;
    }
}

static void beginPass(Replay *r, RdTargetRec *c, RdTargetRec *d, uint32_t cid, uint32_t did,
                      RhiLoadOp colorLoad, const float clear[4], RhiLoadOp depthLoad,
                      float clearDepth)
{
    endPass(r);
    rd__Transition(s_cl, c->color, &c->colorState, RHI_STATE_RENDER_TARGET);
    if (d) {
        rd__Transition(s_cl, d->depth, &d->depthState, RHI_STATE_DEPTH_WRITE);
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = c->color;
    p.color[0].load = colorLoad;
    if (clear) {
        memcpy(p.color[0].clear, clear, sizeof(p.color[0].clear));
    }
    p.colorCount = 1;
    if (d) {
        p.depth.texture = d->depth;
        p.depth.depthLoad = depthLoad;
        p.depth.stencilLoad = depthLoad;
        p.depth.clearDepth = clearDepth;
        p.depth.clearStencil = 0;
    }
    p.width = c->tw;
    p.height = c->th;
    rhi_CmdBeginRenderPass(s_cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)c->tw, (float)c->th, 0.0f, 1.0f};
    rhi_CmdSetViewport(s_cl, &vp);
    r->passOpen = 1;
    r->passColor = cid;
    r->passDepth = did;
    r->passSerial++;
}

/* ---------------------------------------------------------------- actions */

static void doClear(Replay *r, const RdCmd *c)
{
    RdTargetRec *t = rd__TargetRec(c->u[0]);
    if (!t) {
        return;
    }
    float col[4];
    for (int i = 0; i < 4; i++) {
        col[i] = t->format == RHI_FMT_RGBA8_UINT ? (float)c->b[i] : (float)c->b[i] / 255.0f;
    }
    const int depth = c->b[4] && t->withDepth;
    r->writeSerial++;
    beginPass(r, t, depth ? t : NULL, c->u[0], depth ? c->u[0] : 0, RHI_LOAD_CLEAR, col,
              RHI_LOAD_CLEAR, rd__GsDepth(c->u[1], rd__TargetZScale(c->u[0])));
    endPass(r);
}

/* Package P1: built in a local and stored whole, so writing straight into
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
            rd__LogOnce(RD_ONCE_STQ, "screen prim with Q != 1 (divided per pixel when it is a "
                                     "textured triangle command, else per vertex)");
        }
        o->u = s->s / q * tw;
        o->v = s->t / q * th;
    }
    o->u += uvOff[0] * tw;
    o->v += uvOff[1] * th;
    *out = v;
}

/* Package P1: screen prims are expanded here, flipped for the mirror if
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

/* Expands a screen-prim command into a triangle or line list.  Returns the
 * vertex count; *topo is RD_PRIM_TRIANGLES or RD_PRIM_LINES. */
/* Wave 7 (R7a): sprites on a scaled target.  The rasteriser puts GS
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

/* Wave 7 (R7c): the mirror mode's UI flip (RENDER_API.md "Mirror mode").  The
 * presenter flips the whole picture (rd_present.c step 2), so an
 * RD_SPACE_UI prim drawn into SCENE or DISPLAY (the targets the present
 * shows) is flipped here about the target's centre and reads normally
 * after it.  Everything else (WORLD and FULLSCREEN prims, the meshes, the
 * posts) is drawn as recorded and flips with the present.
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
 * quads of the layout and the font: exact UVs).  Colours and Z are not
 * moved (1/16 of a pixel's step of a gradient).  Points are drawn as
 * one-pixel quads, so they follow the triangles.  Lines are reflected about
 * the pixel centre, X' = C - 1 - X.  On a scaled target "1" is one texel,
 * 1 / (sx wide) GS pixels.  The scissor is mirrored with them
 * (scissorRect). */
static float wideFor(const RdTargetRec *tc, int stretch);

static int mirrorUi(const Replay *r, uint8_t space)
{
    return space == RD_SPACE_UI && rd__MirrorOn() &&
           (r->st.color == (uint32_t)RD_TARGET_SCENE + 1u ||
            r->st.color == (uint32_t)RD_TARGET_DISPLAY + 1u);
}

static uint16_t mirrorX(int32_t c16, float x)
{
    const float m = (float)c16 - x;
    const int32_t i = (int32_t)(m < 0.0f ? m - 0.5f : m + 0.5f);
    return (uint16_t)(i < 0 ? 0 : (i > 0xFFFF ? 0xFFFF : i));
}

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
        for (int k = 0; k < 3; k++) {
            t[k].u -= dudx;
            t[k].v -= dvdx;
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
    const float texels = tc->sx * wideFor(tc, r->stretch);
    mirrorVerts(o, n, topo, c16, 16.0f / (texels > 0.0f ? texels : 1.0f));
    r->mirror = 1;
}

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
            /* T1: port UI text (RD_UV_FIXED_CONTINUOUS) is not GS content:
             * its glyph quads keep their sub-pixel edges and UVs */
            if ((s_uvShiftX != 0.0f || s_uvShiftY != 0.0f) && uvFixed != RD_UV_FIXED_CONTINUOUS) {
                spriteUvShift(&a, &b); /* R7a: a scaled target */
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
            s->tex = rhi_CreateTexture(&(RhiTextureDesc){w, h, 1, RHI_FMT_RGBA8_UINT,
                                                         RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED |
                                                             RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST,
                                                         "rd exact scratch"});
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
    rhi_DestroyTexture(s->tex);
    s->tex = (RhiTexture){0};
    return scratchGet(w, h, which);
}

/* package RSMALL: the box-reduced shadow count (doShadowResolve) */
static RhiTexture s_shadowRed;

static RhiState s_shadowRedState;

static uint32_t s_shadowRedW, s_shadowRedH;

static uint32_t s_shadowRedFor; /* the count target (RdTarget id) s_shadowRed holds; 0 none */

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
    RdTexRec *t = rd__TexRec(r->st.tex);
    if (!t) {
        rd__LogOnce(RD_ONCE_BAD_TEX, "draw with a destroyed or unknown texture: untextured");
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
        return t->rhi;
    }
    RdTargetRec *src = rd__TargetRec(t->target);
    if (!src) {
        return g_rd.dummy;
    }
    if (t->view == RD_VIEW_DEPTH) {
        /* wave 4 (R4c): only RD_POST_FOG reads a depth view (doFog, through
         * its LUT); a draw that finds the view still bound (the fog's TEX0
         * leaking, as on the GS) samples nothing */
        rd__LogOnce(RD_ONCE_DEPTH_VIEW, "a draw samples a depth view outside the fog: untextured");
        return g_rd.dummy;
    }
    *w = src->w;
    *h = src->h;
    *textured = 1;
    if (s_shadowRedFor && t->target == s_shadowRedFor && s_shadowRed.id &&
        t->target != drawTargetId) {
        return s_shadowRed; /* package RSMALL: the shadow count at the GS size */
    }
    if (t->target == drawTargetId && src == drawTarget) {
        /* the GS reads the buffer it is drawing into: sample a copy */
        endPass(r);
        if (!src->snap.id) {
            src->snap = rhi_CreateTexture(&(RhiTextureDesc){
                src->tw, src->th, 1, src->format, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd snap"});
            src->snapState = RHI_STATE_UNDEFINED;
        }
        rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
        rd__Transition(s_cl, src->snap, &src->snapState, RHI_STATE_COPY_DST);
        rhi_CmdCopyTexture(s_cl, src->color, (RhiRect){0, 0, src->tw, src->th}, src->snap, 0, 0);
        rd__Transition(s_cl, src->snap, &src->snapState, RHI_STATE_SHADER_READ);
        return src->snap;
    }
    if (src->colorState != RHI_STATE_SHADER_READ) {
        endPass(r);
        rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_SHADER_READ);
    }
    return src->color;
}

/* TEST.DATE (wave 2): the bound target's alpha MSB into the R8 snapshot
 * (RD_TARGET_DATE_SNAPSHOT), pixel for pixel, for sprite_ps to test at t2.
 * Retaken when the target changed or anything may have written alpha since
 * the last one, so consecutive DATE draws see each other's writes as on the
 * GS (within one draw, overlapping primitives see the snapshot: accepted). */
static RhiTexture dateSnapshot(Replay *r, RdTargetRec *tc, uint32_t tcId)
{
    RdTargetRec *sn = rd__TargetRec(RD_TARGET_DATE_SNAPSHOT + 1);
    if (!sn || !sn->color.id || tc == sn) {
        return g_rd.dummy;
    }
    if (r->dateFor == tcId && r->dateSerial == r->writeSerial &&
        sn->colorState == RHI_STATE_SHADER_READ) {
        return sn->color;
    }
    /* in texels: the snapshot has the scene's scale (R7a) and sprite_ps
     * reads it at the fragment's texel */
    uint32_t w = tc->tw, h = tc->th;
    if (w > sn->tw || h > sn->th) {
        rd__LogOnce(RD_ONCE_DATE_SIZE, "DATE on a target larger than the snapshot: clipped");
        w = w > sn->tw ? sn->tw : w;
        h = h > sn->th ? sn->th : h;
    }
    endPass(r);
    g_rdPerf.dateSnapshots++;
    rd__Transition(s_cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    rd__Transition(s_cl, sn->color, &sn->colorState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = sn->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = sn->tw;
    p.height = sn->th;
    rhi_CmdBeginRenderPass(s_cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    rhi_CmdSetViewport(s_cl, &vp);
    const RhiRect sc = {0, 0, w, h};
    rhi_CmdSetScissor(s_cl, &sc);
    RdPipeKeyInt k = rd__PostKey(RD_VS_BLIT, RD_FS_DATE_SNAP, RHI_FMT_R8_UNORM);
    RhiPipeline pipe = rd__GetPipeline(&k);
    if (pipe.id) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.uvRect[2] = (float)w;
        cb.uvRect[3] = (float)h;
        cb.tex[0] = (float)tc->tw;
        cb.tex[1] = (float)tc->th;
        cb.tex[2] = 1.0f / (float)tc->tw;
        cb.tex[3] = 1.0f / (float)tc->th;
        rhi_CmdSetPipeline(s_cl, pipe);
        rd__BindUniform(s_cl, 0, rd__FrameGroup(w, h, 0.0f, 0.0f));
        rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(
            s_cl, 2,
            rd__TexGroup(tc->color, rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP,
                                                RD_WRAP_CLAMP)));
        rhi_CmdDraw(s_cl, 3, 0, 1);
    }
    rhi_CmdEndRenderPass(s_cl);
    rd__Transition(s_cl, sn->color, &sn->colorState, RHI_STATE_SHADER_READ);
    r->dateFor = tcId;
    r->dateSerial = r->writeSerial;
    return sn->color;
}

/* ------------------------------------------- scale and wide (wave 7, R7a)
 * A target's texture is tw x th texels for its w x h GS pixels (sx, sy
 * texels per pixel; 1 in Original).  FrameCB keeps the GS size (g_target),
 * so every vertex lands where it did and the viewport, the scissor and the
 * copies are what scale; g_z.yz carries the scale for the shaders that
 * address a target by texel (fx_sprite_ps, fog_lut_ps).
 *
 * The wide x scale (4/3) / aspect applies, about the target's centre, to
 * draws into the scene-class targets (SCENE, WORK2, AURA_WORK, the shadow
 * count: RdTargetRec.wide), the meshes (vu_ndc) and the screen prims
 * alike, so CPU-projected and UI prims follow the wider projection and the
 * UI stays a centred 4:3 box.  Full-screen draws stretch instead: the
 * RD_SPACE_FULLSCREEN ones (rd_post.c's passes, the fog, layout_texture.c's
 * primary sprite) and any sprite that spans the target's whole width (the
 * clears, Shadow.c's resolve, the game's own full-screen fills). */

static int32_t floorDiv16(int32_t v)
{
    return v >= 0 ? v / 16 : -((-v + 15) / 16);
}

/* Package RSMALL: the scissor x0..x1 (inclusive GS pixels, inside 0..w-1) of
 * a UI draw on a target w pixels wide under the wide x scale f (the draw's x
 * about w / 2 is multiplied by f, rd_frame.c).  A side that reaches the
 * target's edge stays there; the other follows the draw, rounded outwards, so
 * the scissor clips as much as the draw does (a UI scissor used to keep its
 * 4:3 position: it clipped less, never more).  f 1 leaves it alone. */
void rd__WideScissor(int32_t *x0, int32_t *x1, int32_t w, float f)
{
    if (f == 1.0f || f <= 0.0f) {
        return;
    }
    const float c = (float)w * 0.5f;
    if (*x0 > 0) {
        *x0 = (int32_t)floorf(c + f * ((float)*x0 - c));
    }
    if (*x1 < w - 1) {
        *x1 = (int32_t)ceilf(c + f * ((float)(*x1 + 1) - c)) - 1;
    }
}

/* the scissor in texels: GS pixels x0..x1 (inclusive) cover texels
 * floor(x0 * sx) .. ceil((x1 + 1) * sx) - 1 */
static bool scissorRect(const Replay *r, const RdTargetRec *tc, RhiRect *sc)
{
    int32_t x0 = r->st.scissor[0] < 0 ? 0 : r->st.scissor[0];
    int32_t y0 = r->st.scissor[1] < 0 ? 0 : r->st.scissor[1];
    int32_t x1 = r->st.scissor[2] >= (int32_t)tc->w ? (int32_t)tc->w - 1 : r->st.scissor[2];
    int32_t y1 = r->st.scissor[3] >= (int32_t)tc->h ? (int32_t)tc->h - 1 : r->st.scissor[3];
    if (x1 < x0 || y1 < y0) {
        return false;
    }
    if (r->uiPrim) {
        /* package RSMALL: a UI draw's x scale about the target's centre (the
         * wide factor) applies to a scissor that does not reach the edges */
        rd__WideScissor(&x0, &x1, (int32_t)tc->w, wideFor(tc, r->stretch));
    }
    if (r->mirror) {
        /* R7c: a flipped UI draw clips where its scissor lands after the
         * flip (GS pixel p is target pixel w - 1 - p) */
        const int32_t a = (int32_t)tc->w - 1 - x1, b = (int32_t)tc->w - 1 - x0;
        x0 = a;
        x1 = b;
    }
    if (tc->sx != 1.0f || tc->sy != 1.0f) {
        int32_t a = (int32_t)((float)x0 * tc->sx), b = (int32_t)((float)y0 * tc->sy);
        int32_t c = (int32_t)((float)(x1 + 1) * tc->sx + 0.999f) - 1;
        int32_t d = (int32_t)((float)(y1 + 1) * tc->sy + 0.999f) - 1;
        x0 = a;
        y0 = b;
        x1 = c >= (int32_t)tc->tw ? (int32_t)tc->tw - 1 : c;
        y1 = d >= (int32_t)tc->th ? (int32_t)tc->th - 1 : d;
    }
    *sc = (RhiRect){x0, y0, (uint32_t)(x1 - x0 + 1), (uint32_t)(y1 - y0 + 1)};
    return true;
}

static float wideFor(const RdTargetRec *tc, int stretch)
{
    return tc->wide && !stretch ? g_rd.wideX : 1.0f;
}

/* Whether a screen-prim command is full-screen: tagged so, or sprites that
 * cover the target's whole width (in GS pixels, after XYOFFSET).  A sprite
 * covers the pixels p with x0 <= p < x1 (the GS's top-left rule on the 1/16
 * grid); "the whole width" allows one pixel short at either edge, as the
 * layout's screen bands are drawn: the pause and End Game menus' black bars
 * (layout_texture.c, gif_SpriteSensitiveOffset) run from 1792.25 to
 * 2303.44, pixels 1..511 of 512 (W3, RENDER_API.md "Presets and display options"). */
static int screenStretch(const Replay *r, const RdTargetRec *tc, const RdScreenVtx *v, uint32_t n,
                         uint8_t prim, uint8_t space)
{
    if (g_rd.wideX == 1.0f || !tc->wide) {
        return 0;
    }
    if (space == RD_SPACE_FULLSCREEN) {
        return 1;
    }
    if (prim != RD_PRIM_SPRITES || n < 2) {
        return 0;
    }
    const int32_t ox = 2048 - (int32_t)(r->st.gsW >> 1);
    for (uint32_t i = 0; i + 1 < n; i += 2) {
        int32_t a = v[i].x, b = v[i + 1].x;
        if (a > b) {
            const int32_t t = a;
            a = b;
            b = t;
        }
        /* the first and last pixel covered: ceil(x0), ceil(x1) - 1 */
        const int32_t first = -floorDiv16(-a) - ox, last = -floorDiv16(-b) - 1 - ox;
        if (first <= 1 && last >= (int32_t)tc->w - 2) {
            return 1;
        }
    }
    return 0;
}

/* What a draw binds besides its geometry: doScreen and the VU draws. */
typedef struct DrawSetup {
    RdTargetRec *tc;
    uint32_t tdId;
    RhiFormat depthFmt;
    RhiTexture tex, dateTex;
    uint32_t tw, th, tfmt;
    int textured;
    int mipmapped; /* R7a: the image texture has generated mips */
} DrawSetup;

/* The target, the DATE snapshot and the texture (each may end the open
 * pass: they run before the draw's pass begins). */
static bool prepareDraw(Replay *r, DrawSetup *ds)
{
    memset(ds, 0, sizeof(*ds));
    r->stretch = 0; /* R7a: doScreen decides for screen prims after this */
    r->mirror = 0;  /* R7c: likewise */
    r->uiPrim = 0;  /* doScreen and doScreenWrap set it */
    ds->tc = rd__TargetRec(r->st.color);
    if (!ds->tc || !ds->tc->color.id) {
        return false;
    }
    ds->dateTex = g_rd.dummy;
    if (r->st.ds.test.date != RD_DATE_OFF) {
        ds->dateTex = dateSnapshot(r, ds->tc, r->st.color);
    }
    RdTargetRec *td = rd__TargetRec(r->st.depth);
    if (td && !td->withDepth) {
        td = NULL;
    }
    ds->tdId = td ? r->st.depth : 0;
    ds->depthFmt = td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN;
    ds->tex = resolveTexture(r, ds->tc, r->st.color, &ds->tw, &ds->th, &ds->tfmt, &ds->textured,
                             &ds->mipmapped);
    return true;
}

/* The scissor, the pass and FrameCB; returns the texture group, id 0 when
 * the scissor leaves nothing to draw. */
static RhiBindGroup bindDraw(Replay *r, const DrawSetup *ds)
{
    RdTargetRec *tc = ds->tc;
    /* scissor (SCISSOR_1, inclusive) clipped to the target, in texels */
    RhiRect sc;
    if (!scissorRect(r, tc, &sc)) {
        return (RhiBindGroup){0};
    }
    RhiSampler smp = texSampler((RdFilter)r->st.ds.magFilter, (RdFilter)r->st.ds.minFilter,
                                (RdWrap)r->st.ds.wrap.s, (RdWrap)r->st.ds.wrap.t, ds->mipmapped);
    RhiBindGroup g2 = rd__TexGroupDate(ds->tex, smp, ds->dateTex);

    if (!r->passOpen || r->passColor != r->st.color || r->passDepth != ds->tdId) {
        beginPass(r, tc, ds->tdId ? rd__TargetRec(ds->tdId) : NULL, r->st.color, ds->tdId,
                  RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD, 0.0f);
    }
    const uint32_t fk[6] = {r->st.color,     r->st.gsW,     r->st.gsH,
                            r->st.useOffset, r->passSerial, (uint32_t)r->stretch};
    if (memcmp(fk, r->frameKey, sizeof(fk)) != 0) {
        /* XYOFFSET = (2048 - w/2, 2048 - h/2) (+ the preset's field offset
         * when useOffset; zero in Original, RENDER_API.md "Frame lifecycle,
         * camera and the post passes") */
        float ox = 2048.0f - (float)(r->st.gsW >> 1);
        float oy = 2048.0f - (float)(r->st.gsH >> 1);
        if (r->st.useOffset & RD_TARGET_HALF_Y) {
            oy += 0.5f; /* the flip's sceGsSetHalfOffset (R2c) */
        }
        r->frameBG = rd__FrameGroupEx(tc->w, tc->h, ox, oy, rd__TargetZScale(ds->tdId),
                                      wideFor(tc, r->stretch), tc->sx, tc->sy);
        memcpy(r->frameKey, fk, sizeof(fk));
    }
    rhi_CmdSetScissor(s_cl, &sc);
    return g2;
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
    cb->mode[2] = dp->modeZ;
    cb->mode[3] = dp->aref;
    cb->blend[0] = rd__AlphaRegister(r->st.ds.blend);
    cb->blend[1] = dp->fix;
    cb->blend[2] = r->st.ds.colclamp;
    cb->tex[0] = (float)ds->tw;
    cb->tex[1] = (float)ds->th;
    cb->tex[2] = 1.0f / (float)ds->tw;
    cb->tex[3] = 1.0f / (float)ds->th;
}

/* ------------------------------------------------ PRIM.AA1 (package AA1)
 * The model (RENDER_API.md "PRIM.AA1"), PCSX2's software renderer's
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

/* Package RSMALL: perspective-correct STQ.  A triangle command with texture
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

static void doScreen(Replay *r, const RdFrame *f, const RdCmd *c)
{
    DrawSetup ds;
    if (g_rd.deferText && c->b[3] == RD_SCREEN_TEXT_QUADS) {
        return; /* package DEF: the item is drawn on the output instead */
    }
    if (rd__WrapApplies(&r->st)) {
        if (r->st.aa1 && aa1Prim(c->b[0])) {
            rd__LogOnce(RD_ONCE_AA1_WRAP, "PRIM.AA1 under COLCLAMP 0: drawn without edge coverage");
        }
        doScreenWrap(r, f, c); /* wave 5 (R5c): COLCLAMP 0 */
        return;
    }
    if (!prepareDraw(r, &ds)) {
        return;
    }

    /* geometry */
    const uint32_t n = c->u[1];
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    r->stretch = screenStretch(r, ds.tc, v, n, c->b[0], c->b[1]);
    r->uiPrim = c->b[1] == RD_SPACE_UI;
    setUvShift(ds.tc->sx * wideFor(ds.tc, r->stretch), ds.tc->sy);
    const uint64_t maxBytes = (uint64_t)n * 6 * sizeof(IcoSpriteVertex);
    const uint64_t vOff = rd__RingAlloc(maxBytes, 16);
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
        mirrorDraw(r, ds.tc, out, nv, topo); /* R7c */
    }
    /* package AA1: PRIM.AA1 on a line or triangle command */
    const int aa1 = r->st.aa1 && aa1Prim(c->b[0]);
    if (!aa1) {
        memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(*out)); /* P1 */
    }
    RdDrawPass dp[2];
    const int np = rd__PlanScreenDrawEx(&r->st, topo, aa1, c->b[1], ds.tc->format, ds.depthFmt, dp);
    if (np == 0) {
        return;
    }
    /* package RSMALL: a textured STQ triangle command with Q != 1 draws with
     * the STQ shaders, its vertices rewritten into the ring slot (24 bytes
     * each, at most 3 per source vertex: inside maxBytes) */
    if (!aa1 && !c->b[2] && ds.textured) {
        float *qv;
        const uint32_t nq = qOrder(v, n, c->b[0], &qv);
        RdDrawPass sp[2];
        int stq = nq == nv;
        memcpy(sp, dp, sizeof(sp));
        for (int i = 0; i < np && stq; i++) {
            stq = rd__StqPass(&sp[i]);
        }
        if (stq) {
            memcpy(dp, sp, sizeof(sp));
            IcoSpriteStqVertex *sv = (IcoSpriteStqVertex *)(g_rd.ringMap[s_slot] + vOff);
            for (uint32_t i = 0; i < nv; i++) {
                sv[i].v = out[i];
                sv[i].v.u *= qv[i]; /* S and T, not divided: out holds S / Q */
                sv[i].v.v *= qv[i];
                sv[i].q = qv[i];
            }
        }
    }
    uint32_t nDraw = nv, nFirst = nv;
    uint64_t drawOff = vOff;
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
        drawOff = nDraw ? rd__RingAlloc((uint64_t)nDraw * sizeof(*ax), 16) : ~0ull;
        if (drawOff == ~0ull) {
            return;
        }
        memcpy(g_rd.ringMap[s_slot] + drawOff, ax, (size_t)nDraw * sizeof(*ax));
    }
    RhiBindGroup g2 = bindDraw(r, &ds);
    if (!g2.id) {
        return;
    }
    rhi_CmdSetVertexBuffer(s_cl, 0, g_rd.ring[s_slot], drawOff);
    for (int i = 0; i < np; i++) {
        RhiPipeline p = rd__GetPipeline(&dp[i].key);
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        fillDrawCB(r, &dp[i], &ds, &cb);
        rhi_CmdSetPipeline(s_cl, p);
        rd__BindUniform(s_cl, 0, r->frameBG);
        rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        if (!aa1 || nFirst == nDraw) {
            rhi_CmdDraw(s_cl, nDraw, 0, 1);
        } else {
            /* package AA1: the triangles with the pass's Z write, then
             * their fringes without (edge pixels write no Z) */
            if (nFirst) {
                rhi_CmdDraw(s_cl, nFirst, 0, 1);
            }
            RdPipeKeyInt ek = dp[i].key;
            ek.gs.zwrite = RD_ZWRITE_OFF;
            RhiPipeline pe = rd__GetPipeline(&ek);
            if (pe.id) {
                rhi_CmdSetPipeline(s_cl, pe);
                rd__BindUniform(s_cl, 0, r->frameBG);
                rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
                rhi_CmdSetBindGroup(s_cl, 2, g2);
                rhi_CmdDraw(s_cl, nDraw - nFirst, nFirst, 1);
            }
        }
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
    }
}

/* --------------------------------- the presentation overlay (package OV)
 * One rd_OverlayPrims batch (rd.h; rd_present.c overlayRecord opens the
 * pass on the output and makes FrameCB): expanded as a screen-prim command
 * (sprites to quads, uvFixed texels), no sprite snapping or UV shift (the
 * output has no GS pixel grid), never mirrored, drawn with sprite_ui_vs /
 * sprite_ps under rd__OverlayState's block: the blend given, no Z, no
 * alpha test, no DATE, MODULATE with TCC RGBA when textured. */
void rd__OverlayDraw(RhiCommandList cl, RhiFormat fmt, RdUniform frame, uint8_t prim,
                     const RdScreenVtx *v, uint32_t n, uint32_t tex, uint8_t blend)
{
    RhiTexture t = g_rd.dummy;
    uint32_t tw = 1, th = 1, tfmt = 0;
    int textured = 0;
    if (tex) {
        const RdTexRec *r = rd__TexRec(tex);
        if (!r || r->kind != RD_TEXKIND_IMAGE || !r->rhi.id || r->state != RHI_STATE_SHADER_READ) {
            rd__LogOnce(RD_ONCE_BAD_TEX, "overlay prims with an unknown or unready texture");
            return;
        }
        t = r->rhi;
        tw = r->w;
        th = r->h;
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
    const uint64_t vOff = rd__RingAlloc((uint64_t)nv * sizeof(*out), 16);
    if (vOff == ~0ull) {
        return;
    }
    memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(*out));
    RdStateBlock st;
    rd__OverlayState(&st, blend);
    st.tex = tex; /* R8: the planner picks font_ps for a coverage texture */
    st.ds.texEnabled = (uint8_t)textured;
    RdDrawPass dp[2];
    const int np = rd__PlanScreenDraw(&st, topo, RD_SPACE_UI, fmt, RHI_FMT_UNKNOWN, dp);
    const RhiBindGroup g2 = rd__TexGroup(
        t, rd__Sampler(RD_FILTER_LINEAR, RD_FILTER_LINEAR, RD_WRAP_CLAMP, RD_WRAP_CLAMP));
    rhi_CmdSetVertexBuffer(cl, 0, g_rd.ring[s_slot], vOff);
    for (int i = 0; i < np; i++) {
        RhiPipeline p = rd__GetPipeline(&dp[i].key);
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        cb.mode[0] = dp[i].flags | (textured ? ICO_DF_TEXTURED | ICO_DF_TCC_RGBA : 0u);
        cb.mode[1] = st.ds.texa | (tfmt << 8);
        cb.mode[2] = dp[i].modeZ;
        cb.mode[3] = dp[i].aref;
        cb.blend[0] = rd__AlphaRegister(st.ds.blend);
        cb.blend[1] = dp[i].fix;
        cb.blend[2] = st.ds.colclamp;
        cb.tex[0] = (float)tw;
        cb.tex[1] = (float)th;
        cb.tex[2] = 1.0f / (float)tw;
        cb.tex[3] = 1.0f / (float)th;
        rhi_CmdSetPipeline(cl, p);
        rd__BindUniform(cl, 0, frame);
        rd__BindUniform(cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(cl, 2, g2);
        rhi_CmdDraw(cl, nv, 0, 1);
    }
}

/* ------------------------------------- COLCLAMP 0 wrap (wave 5, R5c)
 * A screen-prim command whose blend result the GS masks to 8 bits instead of
 * clamping it (COLCLAMP 0) under an equation that adds or subtracts a term of
 * the source alone, Cs * F + Cd or Cd - Cs * F (ALPHA modes 0, 5 and 1, 6):
 * the result is Cd plus the sum of the fragments' terms, modulo 256, whatever
 * their order.  darkVolume.c's count buffer is the user (RENDER_API.md
 * "Full-screen effects and the raw packet builders"; Shadow.c's count has its own stencil path).
 * Two passes:
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

bool rd__WrapApplies(const RdStateBlock *s)
{
    if (wrapEquation(s) == 0) {
        return false;
    }
    const int ate = s->ds.test.ate && s->ds.test.atst != RD_ATST_ALWAYS;
    if (s->ds.test.date != RD_DATE_OFF || s->ds.pabe ||
        (ate && s->ds.test.afail != RD_AFAIL_KEEP)) {
        rd__LogOnce(RD_ONCE_WRAP, "COLCLAMP 0 draw with DATE, PABE or an AFAIL split: clamped");
        return false;
    }
    return true;
}

uint32_t rd__WrapPipelineCount(void)
{
    return s_wrapPipeCount;
}

void rd__WrapShutdown(void)
{
    for (uint32_t i = 0; i < s_wrapPipeCount; i++) {
        rhi_DestroyPipeline(s_wrapPipes[i].pipe);
    }
    s_wrapPipeCount = 0;
    if (s_wrapAcc.id) {
        rhi_DestroyTexture(s_wrapAcc);
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
        rd__LogOnce(RD_ONCE_WRAP, "COLCLAMP 0 pipelines past RD_WRAP_PIPES: clamped");
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
                                          : ztst == RD_ZTST_GEQUAL  ? RHI_CMP_LEQUAL
                                          : ztst == RD_ZTST_GREATER ? RHI_CMP_LESS
                                                                    : RHI_CMP_ALWAYS;
        }
        d.debugName = "rd wrap accumulate";
    }
    RhiPipeline p = rhi_CreatePipeline(&d);
    if (!p.id) {
        rd__Log("COLCLAMP 0 pipeline creation failed (resolve %d)", resolve);
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
    if (!prepareDraw(r, &ds)) {
        return;
    }
    RdTargetRec *tc = ds.tc;
    RdTargetRec *td = ds.tdId ? rd__TargetRec(ds.tdId) : NULL;
    r->stretch = screenStretch(r, tc, (const RdScreenVtx *)(f->payload + c->u[0]), c->u[1], c->b[0],
                               c->b[1]);
    r->uiPrim = c->b[1] == RD_SPACE_UI;
    setUvShift(tc->sx * wideFor(tc, r->stretch), tc->sy);
    if (tc->format != RHI_FMT_RGBA8_UNORM || (td && (td->tw != tc->tw || td->th != tc->th))) {
        rd__LogOnce(RD_ONCE_WRAP, "COLCLAMP 0 draw on a non-RGBA8 target or a depth of another "
                                  "size: not drawn");
        return;
    }
    const uint32_t n = c->u[1];
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    const uint64_t vOff = rd__RingAlloc((uint64_t)n * 6 * sizeof(IcoSpriteVertex), 16);
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
        mirrorDraw(r, tc, out, nv, topo); /* R7c */
    }
    memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(*out)); /* P1 */
    /* the planner's flags, FIX and alpha test for this state (COLCLAMP set on
     * the copy: the planner logs COLCLAMP 0 as clamped, which this path is
     * not) */
    RdStateBlock st = r->st;
    st.ds.colclamp = 1;
    RdDrawPass dp[2];
    if (rd__PlanScreenDraw(&st, topo, c->b[1], tc->format, ds.depthFmt, dp) != 1) {
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
            rhi_DestroyTexture(s_wrapAcc);
        }
        s_wrapAcc = rhi_CreateTexture(&(RhiTextureDesc){tc->tw, tc->th, 1, RHI_FMT_RGBA16F,
                                                        RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                        "rd wrap accumulator"});
        s_wrapAccState = RHI_STATE_UNDEFINED;
        s_wrapW = tc->tw;
        s_wrapH = tc->th;
        if (!s_wrapAcc.id) {
            return;
        }
    }
    /* the destination before the command */
    endPass(r);
    if (!tc->snap.id) {
        tc->snap = rhi_CreateTexture(&(RhiTextureDesc){
            tc->tw, tc->th, 1, tc->format, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd snap"});
        tc->snapState = RHI_STATE_UNDEFINED;
    }
    rd__Transition(s_cl, tc->color, &tc->colorState, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, tc->snap, &tc->snapState, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, tc->color, (RhiRect){0, 0, tc->tw, tc->th}, tc->snap, 0, 0);
    rd__Transition(s_cl, tc->snap, &tc->snapState, RHI_STATE_SHADER_READ);
    if (ds.textured && ds.tex.id == tc->color.id) {
        ds.tex = tc->snap; /* the GS reads the buffer it draws into */
    }

    /* 1. the terms into the accumulator */
    rd__Transition(s_cl, s_wrapAcc, &s_wrapAccState, RHI_STATE_RENDER_TARGET);
    if (td) {
        rd__Transition(s_cl, td->depth, &td->depthState, RHI_STATE_DEPTH_WRITE);
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_wrapAcc;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    if (td) {
        p.depth.texture = td->depth;
        p.depth.depthLoad = RHI_LOAD_LOAD;
        p.depth.stencilLoad = RHI_LOAD_LOAD;
    }
    p.width = tc->tw;
    p.height = tc->th;
    rhi_CmdBeginRenderPass(s_cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)tc->tw, (float)tc->th, 0.0f, 1.0f};
    rhi_CmdSetViewport(s_cl, &vp);
    rhi_CmdSetScissor(s_cl, &sc);
    float ox = 2048.0f - (float)(r->st.gsW >> 1);
    float oy = 2048.0f - (float)(r->st.gsH >> 1);
    if (r->st.useOffset & RD_TARGET_HALF_Y) {
        oy += 0.5f;
    }
    IcoDrawCB cb;
    fillDrawCB(r, &dp[0], &ds, &cb);
    cb.mode[0] &= ~(uint32_t)ICO_DF_PREMUL;
    cb.param[0] = (float)wrapEquation(&r->st);
    RhiSampler smp = texSampler((RdFilter)r->st.ds.magFilter, (RdFilter)r->st.ds.minFilter,
                                (RdWrap)r->st.ds.wrap.s, (RdWrap)r->st.ds.wrap.t, ds.mipmapped);
    rhi_CmdSetPipeline(s_cl, pa);
    rd__BindUniform(s_cl, 0,
                    rd__FrameGroupEx(tc->w, tc->h, ox, oy, rd__TargetZScale(ds.tdId),
                                     wideFor(tc, r->stretch), tc->sx, tc->sy));
    rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
    rhi_CmdSetBindGroup(s_cl, 2, rd__TexGroup(ds.tex, smp));
    rhi_CmdSetVertexBuffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    rhi_CmdDraw(s_cl, nv, 0, 1);
    rhi_CmdEndRenderPass(s_cl);
    rd__Transition(s_cl, s_wrapAcc, &s_wrapAccState, RHI_STATE_SHADER_READ);

    /* 2. (Cd + acc) mod 256 into the target */
    beginPass(r, tc, NULL, r->st.color, 0, RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD, 0.0f);
    rhi_CmdSetScissor(s_cl, &sc);
    IcoDrawCB rb;
    memset(&rb, 0, sizeof(rb));
    rhi_CmdSetPipeline(s_cl, pr);
    rd__BindUniform(s_cl, 0, rd__FrameGroup(tc->tw, tc->th, 0.0f, 0.0f));
    rd__BindUniform(s_cl, 1, rd__DrawGroup(&rb));
    rhi_CmdSetBindGroup(s_cl, 2,
                        rd__TexGroupDate(s_wrapAcc,
                                         rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                     RD_WRAP_CLAMP, RD_WRAP_CLAMP),
                                         tc->snap));
    rhi_CmdDraw(s_cl, 3, 0, 1);
    endPass(r);
    r->writeSerial++;
}

/* --------------------------------------------------- VU draws (wave 3)
 * RDC_MESH / RDC_SKINNED (rd_DrawVuMesh), RDC_GRID (rd_DrawVuGrid) and
 * RDC_PARTICLES (rd_DrawVuParticles) through the vu_*.hlsl vertex shaders
 * and vu_ps, with the GS state of the replay state block at the command
 * (the material and register packets were decoded into state commands in
 * order with the meshes).  Static meshes: indexed triangle lists over the
 * mesh's stream and index list, copied into the ring once per replay.
 * Scissor batches (code 36) are two draws per batch: the triangles
 * SCISSOR_COMMON clips (ICO_VU_CUT_ONLY) with PRIM.ABE forced on, as the
 * fans' PRIM 0x5D has it, then the strip's own kicks (ICO_VU_KICK_ONLY)
 * (VU1_PROGRAMS.md findings 1 and 2). */

static uint32_t s_zeroBonesFor; /* replay counter of s_zeroBones */

static uint64_t s_zeroBones;

static uint64_t ringCopy(const void *data, uint64_t size, uint64_t align)
{
    uint64_t off = rd__RingAlloc(size ? size : 16, align);
    if (off != ~0ull && size) {
        memcpy(g_rd.ringMap[s_slot] + off, data, size);
    }
    return off;
}

/* Package PA: the VU layout's group binds the stream's whole buffer (the
 * mesh arena chunk or the ring) at t0 and the ring at offset 0 for the
 * three dynamic uniforms, so it is made once per stream buffer and replay;
 * a draw selects its stream through VuCB's vu_draw.x (the qword index of
 * its first batch, vu_common.hlsli) and its blocks through the bind-time
 * offsets.  A stream whose end lies past the device's largest storage range
 * gets a group of its own bound at its offset, vu_draw.x 0, as before. */
#define RD_VU_GROUPS (RD_MESH_CHUNKS + 2) /* every arena chunk and the ring */

typedef struct VuEntry {
    uint32_t epoch, buffer;
    uint64_t range;
    RhiBindGroup group;
} VuEntry;

static VuEntry s_vuGroups[RD_VU_GROUPS];

static RhiBindGroup vuGroupMake(RhiBuffer streamBuf, uint64_t streamOff, uint64_t streamSize)
{
    RhiBinding b[4];
    memset(b, 0, sizeof(b));
    b[0].slot = 0;
    b[0].type = RHI_BIND_STORAGE_BUFFER;
    b[0].buffer = streamBuf; /* P1: the mesh arena, or the ring */
    b[0].offset = streamOff;
    b[0].size = streamSize ? streamSize : 16;
    static const uint32_t slot[3] = {1, 2, 3};
    static const uint32_t size[3] = {sizeof(IcoDrawCB), sizeof(IcoVuCB), sizeof(IcoVuBoneCB)};
    for (int i = 0; i < 3; i++) {
        b[1 + i].slot = slot[i];
        b[1 + i].type = RHI_BIND_UNIFORM_BUFFER_DYNAMIC;
        b[1 + i].buffer = g_rd.ring[s_slot];
        b[1 + i].offset = 0;
        b[1 + i].size = size[i];
    }
    return bindGroup(g_rd.layoutVu, b, 4);
}

/* The group for a stream at streamOff of streamBuf (bufSize bytes); *base
 * receives the stream's first qword in the bound range (vu_draw.x). */
static RhiBindGroup vuGroup(RhiBuffer streamBuf, uint64_t bufSize, uint64_t streamOff,
                            uint64_t streamSize, uint32_t *base)
{
    uint64_t range = bufSize;
    const uint64_t maxRange = rhi_Limits()->maxStorageRange;
    if (maxRange && range > maxRange) {
        range = maxRange & ~(uint64_t)15u;
    }
    if (streamOff + (streamSize ? streamSize : 16) > range || (streamOff & 15u) ||
        streamOff / 16u > UINT32_MAX) {
        *base = 0;
        return vuGroupMake(streamBuf, streamOff, streamSize);
    }
    *base = (uint32_t)(streamOff / 16u);
    VuEntry *free = NULL;
    for (int i = 0; i < RD_VU_GROUPS; i++) {
        VuEntry *e = &s_vuGroups[i];
        if (e->epoch == s_bindEpoch && e->buffer == streamBuf.id && e->range == range) {
            return e->group;
        }
        if (!free && e->epoch != s_bindEpoch) {
            free = e;
        }
    }
    const RhiBindGroup g = vuGroupMake(streamBuf, 0, range);
    if (g.id && free) {
        free->epoch = s_bindEpoch;
        free->buffer = streamBuf.id;
        free->range = range;
        free->group = g;
    }
    return g;
}

/* The VU vertex shader of a recorded (RdProg, code). */
static uint8_t vuVs(uint8_t prog, uint8_t code)
{
    switch (prog) {
    case RD_PROG_PRELIT:
        return RD_VS_VU_PRELIT;
    case RD_PROG_LIT:
        return RD_VS_VU_LIT;
    case RD_PROG_LIT_SPEC:
        return RD_VS_VU_LIT_SPEC;
    case RD_PROG_REFLECT:
        return RD_VS_VU_REFLECT;
    case RD_PROG_SKIN:
        return RD_VS_VU_SKIN;
    case RD_PROG_SKIN_SPEC:
        return code == 24 ? RD_VS_VU_SKIN_DEBUG : RD_VS_VU_SKIN_SPEC;
    case RD_PROG_GRID:
        return RD_VS_VU_GRID;
    case RD_PROG_GRID_LIT:
        return code == 24 ? RD_VS_VU_GRID_SPEC : RD_VS_VU_GRID_LIT;
    default:
        return RD_VS_VU_PARTICLE;
    }
}

/* One VU draw call under the planned passes of state s. */
static void vuDraw(Replay *r, const RdStateBlock *s, const DrawSetup *ds, RhiBindGroup g2,
                   uint8_t prog, uint8_t vs, RhiBuffer streamBuf, uint64_t streamBufSize,
                   uint64_t streamOff, uint64_t streamSize, const IcoVuCB *vcb0, uint64_t bonesOff,
                   int indexed, uint32_t first, uint32_t count)
{
    if (count == 0) {
        return;
    }
    /* package PA: one group per stream buffer; the stream's place in it
     * goes into vu_draw.x */
    IcoVuCB vcbAt = *vcb0;
    const IcoVuCB *vcb = &vcbAt;
    const RhiBindGroup g1 =
        vuGroup(streamBuf, streamBufSize, streamOff, streamSize, &vcbAt.draw[0]);
    if (!g1.id) {
        return;
    }
    const uint64_t ua = rhi_Limits()->uniformAlign;
    RdDrawPass dp[2];
    const int np =
        rd__PlanScreenDraw(s, RD_PRIM_TRIANGLES, RD_SPACE_WORLD, ds->tc->format, ds->depthFmt, dp);
    const RdStateBlock saved = r->st;
    r->st = *s; /* fillDrawCB reads the state the passes were planned from */
    for (int i = 0; i < np; i++) {
        dp[i].key.gs.program = prog;
        dp[i].key.vs = vs;
        dp[i].key.fs = RD_FS_VU;
        RhiPipeline p = rd__GetPipeline(&dp[i].key);
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        fillDrawCB(r, &dp[i], ds, &cb);
        const uint64_t dOff = ringCopy(&cb, sizeof(cb), ua);
        const uint64_t vOff = ringCopy(vcb, sizeof(*vcb), ua);
        if (dOff == ~0ull || vOff == ~0ull) {
            continue;
        }
        const uint32_t offs[3] = {(uint32_t)dOff, (uint32_t)vOff, (uint32_t)bonesOff};
        rhi_CmdSetPipeline(s_cl, p);
        rd__BindUniform(s_cl, 0, r->frameBG);
        rhi_CmdSetBindGroupOffsets(s_cl, 1, g1, offs, 3);
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        if (indexed) {
            rhi_CmdDrawIndexed(s_cl, count, first, 0, 1);
        } else {
            rhi_CmdDraw(s_cl, count, first, 1);
        }
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
        g_rd.stats.draws++;
    }
    r->st = saved;
}

static void doVu(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdVuPayload p;
    const uint8_t *at = f->payload + c->u[1];
    memcpy(&p, at, sizeof(p));
    at += sizeof(p);
    IcoVuCB vcb;
    memset(&vcb, 0, sizeof(vcb));
    memcpy(vcb.mem, at, sizeof(RdVuBlock));
    at += sizeof(RdVuBlock);
    const uint8_t *bones = p.boneQw ? at : NULL;
    at += (size_t)p.boneQw * 16;
    const uint8_t *stream = p.streamQw ? at : NULL;

    RdMeshRec *m = NULL;
    if (c->type == RDC_MESH || c->type == RDC_SKINNED) {
        m = rd__MeshRec(c->u[0]);
        if (!m || !m->vu || p.firstBatch >= m->batchCount) {
            return;
        }
    }
    DrawSetup ds;
    if (!prepareDraw(r, &ds)) {
        return;
    }
    const uint64_t ua = rhi_Limits()->uniformAlign;
    /* bones: VuBoneCB (VU memory 16..255); a zero block for the others */
    uint64_t bonesOff;
    if (bones) {
        IcoVuBoneCB bc;
        memset(&bc, 0, sizeof(bc));
        memcpy(bc.bone, bones, (size_t)(p.boneQw > 240 ? 240 : p.boneQw) * 16);
        bonesOff = ringCopy(&bc, sizeof(bc), ua);
    } else {
        if (s_zeroBonesFor != g_rd.replayCounter) {
            static const IcoVuBoneCB zero;
            s_zeroBones = ringCopy(&zero, sizeof(zero), ua);
            s_zeroBonesFor = g_rd.replayCounter;
        }
        bonesOff = s_zeroBones;
    }
    uint64_t streamOff, streamSize, indexOff = 0;
    RhiBuffer streamBuf = g_rd.ring[s_slot], indexBuf = g_rd.ring[s_slot];
    uint64_t streamBufSize = g_rd.ringCap[s_slot];
    if (m && m->gpuChunk && !m->gpuDirty && !m->transient) {
        /* P1: the device copy uploadMeshes keeps */
        streamBuf = indexBuf = rd__MeshGpuBuffer(m->gpuChunk);
        streamBufSize = rd__MeshGpuBufferSize(m->gpuChunk);
        streamOff = m->gpuOff;
        streamSize = (uint64_t)m->vertexCount * m->qwPerVertex * 16;
        indexOff = m->gpuIndexOff;
        vcb.draw[1] = m->qwPerVertex;
    } else if (m) {
        if (m->replaySeen != g_rd.replayCounter) {
            m->ringStream = ringCopy(m->stream, (uint64_t)m->vertexCount * m->qwPerVertex * 16, ua);
            m->ringIndex = ringCopy(m->index, (uint64_t)m->indexCount * 4, 16);
            m->replaySeen = g_rd.replayCounter;
            g_rdPerf.meshUploads++;
            g_rdPerf.meshUploadBytes +=
                (uint64_t)m->vertexCount * m->qwPerVertex * 16 + (uint64_t)m->indexCount * 4;
        }
        streamOff = m->ringStream;
        streamSize = (uint64_t)m->vertexCount * m->qwPerVertex * 16;
        indexOff = m->ringIndex;
        vcb.draw[1] = m->qwPerVertex;
    } else {
        streamSize = (uint64_t)p.streamQw * 16;
        streamOff = ringCopy(stream, streamSize, ua);
        vcb.draw[1] = p.qwPerVertex;
    }
    if (bonesOff == ~0ull || streamOff == ~0ull || indexOff == ~0ull) {
        return;
    }
    const uint8_t vs = vuVs(p.prog, p.code);
    vcb.draw[0] = 0;
    vcb.draw[2] = p.clip;

    if (c->type == RDC_GRID) {
        /* the Mesh3D buffer as it is: per strip the VIF qword, tag and colour
         * before the vertices, MSCNT after them; indices k = 2 .. stripLen-1
         * of every strip (mesh.vsm has no strip flag) */
        const uint32_t vpb = p.vertsPerBatch;
        vcb.draw[3] = vpb;
        vcb.batch[0] = 3;
        vcb.batch[1] = 1;
        const uint32_t ni = p.batchCount * (vpb - 2) * 3;
        const uint64_t iOff = rd__RingAlloc((uint64_t)ni * 4, 16);
        if (iOff == ~0ull) {
            return;
        }
        uint32_t *idx = (uint32_t *)(g_rd.ringMap[s_slot] + iOff);
        for (uint32_t b = 0, k = 0; b < p.batchCount; b++) {
            for (uint32_t v = 2; v < vpb; v++) {
                for (uint32_t cc = 0; cc < 3; cc++) {
                    idx[k++] = ICO_VU_INDEX(b * vpb + v, cc);
                }
            }
        }
        RhiBindGroup g2 = bindDraw(r, &ds);
        if (!g2.id) {
            return;
        }
        rhi_CmdSetIndexBuffer(s_cl, g_rd.ring[s_slot], iOff, true);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &vcb, bonesOff, 1, 0, ni);
        return;
    }
    if (c->type == RDC_PARTICLES) {
        RhiBindGroup g2 = bindDraw(r, &ds);
        if (!g2.id) {
            return;
        }
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &vcb, bonesOff, 0, 0, 6 * p.vertsPerBatch);
        return;
    }
    /* static and skinned meshes */
    RhiBindGroup g2 = bindDraw(r, &ds);
    if (!g2.id) {
        return;
    }
    rhi_CmdSetIndexBuffer(s_cl, indexBuf, indexOff, true);
    uint32_t last = p.firstBatch + p.batchCount;
    if (last > m->batchCount || p.batchCount == 0) {
        last = m->batchCount;
    }
    if (p.clip != RD_VU_CLIP_SCISSOR) {
        const RdVuBatchRec *b0 = &m->batches[p.firstBatch], *b1 = &m->batches[last - 1];
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &vcb, bonesOff, 1, b0->firstIndex, b1->firstIndex + b1->indexCount - b0->firstIndex);
        return;
    }
    IcoVuCB cut = vcb, kick = vcb;
    cut.draw[2] |= ICO_VU_CUT_ONLY;
    kick.draw[2] |= ICO_VU_KICK_ONLY;
    RdStateBlock fan = r->st;
    fan.ds.abe = 1; /* the fans' PRIM is the common block's 0x5D: ABE on */
    for (uint32_t b = p.firstBatch; b < last; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        vuDraw(r, &fan, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize, &cut,
               bonesOff, 1, br->firstIndex, br->indexCount);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamBuf, streamBufSize, streamOff, streamSize,
               &kick, bonesOff, 1, br->firstIndex, br->indexCount);
    }
}

static void doExact(Replay *r, const RdCmd *c)
{
    RdTargetRec *src = rd__TargetRec(c->u[0]);
    RdTargetRec *dst = rd__TargetRec(c->u[1]);
    if (!src || !dst || !src->color.id || !dst->color.id) {
        return;
    }
    endPass(r);
    r->writeSerial++;
    uint32_t w = dst->tw, h = dst->th;
    if (src->tw != w || src->th != h) {
        rd__LogOnce(RD_ONCE_EXACT_SIZE, "exact blend between targets of different sizes: the "
                                        "common top-left rectangle is blended");
        w = src->tw < w ? src->tw : w;
        h = src->th < h ? src->th : h;
    }
    const RhiRect all = {0, 0, w, h};
    if (!r->st.ds.abe) {
        /* no blending: the GS writes Cs */
        rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
        rd__Transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
        rhi_CmdCopyTexture(s_cl, src->color, all, dst->color, 0, 0);
        return;
    }
    RdScratch *cs = scratchGet(w, h, 0), *cd = scratchGet(w, h, 1), *co = scratchGet(w, h, 2);
    if (!cs || !cd || !co) {
        return;
    }
    g_rdPerf.exactBlends++;
    rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, cs->tex, &cs->state, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, src->color, all, cs->tex, 0, 0);
    rd__Transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, cd->tex, &cd->state, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, dst->color, all, cd->tex, 0, 0);
    rd__Transition(s_cl, cs->tex, &cs->state, RHI_STATE_SHADER_READ);
    rd__Transition(s_cl, cd->tex, &cd->state, RHI_STATE_SHADER_READ);
    rd__Transition(s_cl, co->tex, &co->state, RHI_STATE_RENDER_TARGET);

    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = co->tex;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = w;
    p.height = h;
    rhi_CmdBeginRenderPass(s_cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f};
    rhi_CmdSetViewport(s_cl, &vp);
    rhi_CmdSetScissor(s_cl, &all);
    RdPipeKeyInt k = rd__PostKey(RD_VS_BLEND_INT, RD_FS_BLEND_INT, RHI_FMT_RGBA8_UINT);
    RhiPipeline pipe = rd__GetPipeline(&k);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.blend[0] = rd__AlphaRegister(r->st.ds.blend);
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
        rhi_CmdSetPipeline(s_cl, pipe);
        rd__BindUniform(s_cl, 0, rd__FrameGroup(w, h, 0.0f, 0.0f));
        rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, bindGroup(g_rd.layoutInt, b, 2));
        rhi_CmdDraw(s_cl, 3, 0, 1);
    }
    rhi_CmdEndRenderPass(s_cl);
    rd__Transition(s_cl, co->tex, &co->state, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, co->tex, all, dst->color, 0, 0);
}

static void doCopy(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdTargetRec *src = rd__TargetRec(c->u[0]);
    RdTargetRec *dst = rd__TargetRec(c->u[1]);
    if (!src || !dst || src == dst || src->format != dst->format) {
        return;
    }
    RdCopyRec cr;
    memcpy(&cr, f->payload + c->u[2], sizeof(cr));
    if (cr.srcX < 0 || cr.srcY < 0 || cr.dstX < 0 || cr.dstY < 0) {
        return;
    }
    /* R7a: the rectangle in each texture's texels; between targets of
     * different scales the copy cannot resample (logged once) and copies the
     * source's texels */
    if (src->sx != dst->sx || src->sy != dst->sy) {
        rd__LogOnce(RD_ONCE_COPY_SCALE, "copy between targets of different resolution scales: "
                                        "not resampled");
    }
    const uint32_t sx0 = (uint32_t)((float)cr.srcX * src->sx);
    const uint32_t sy0 = (uint32_t)((float)cr.srcY * src->sy);
    const uint32_t dx0 = (uint32_t)((float)cr.dstX * dst->sx);
    const uint32_t dy0 = (uint32_t)((float)cr.dstY * dst->sy);
    uint32_t w = src->sx == 1.0f ? cr.w : (uint32_t)((float)cr.w * src->sx + 0.5f);
    uint32_t h = src->sy == 1.0f ? cr.h : (uint32_t)((float)cr.h * src->sy + 0.5f);
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
    r->writeSerial++;
    rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, src->color, (RhiRect){(int32_t)sx0, (int32_t)sy0, w, h}, dst->color,
                       (int32_t)dx0, (int32_t)dy0);
}

/* ------------------------------------------------- shadows (wave 4, R4b)
 * rd_shadow.c says what the three commands stand for (RENDER_API.md
 * "Shadows").  All three work on the state block's colour target and the
 * depth-stencil of its depth target, which must have the colour's size
 * (shadow_Reset binds the per-frame count target with SCENE's). */

/* The colour and depth-stencil the state block names, of one size; NULL
 * (reported once) otherwise. */
static RdTargetRec *shadowTargets(Replay *r, RdTargetRec **depth)
{
    RdTargetRec *tc = rd__TargetRec(r->st.color);
    RdTargetRec *td = rd__TargetRec(r->st.depth);
    if (!tc || !tc->color.id || !td || !td->withDepth || !td->depth.id || tc->tw != td->tw ||
        tc->th != td->th) {
        rd__LogOnce(RD_ONCE_SHADOW, "shadow command without a depth-stencil target of the colour "
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
    s_shadowRedFor = 0; /* package RSMALL: a new count starts */
    rd__Transition(s_cl, tc->color, &tc->colorState, RHI_STATE_RENDER_TARGET);
    rd__Transition(s_cl, td->depth, &td->depthState, RHI_STATE_DEPTH_WRITE);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = tc->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.depth.texture = td->depth;
    p.depth.depthLoad = RHI_LOAD_LOAD;
    p.depth.stencilLoad = RHI_LOAD_CLEAR;
    p.depth.clearStencil = 0;
    p.width = tc->tw;
    p.height = tc->th;
    rhi_CmdBeginRenderPass(s_cl, &p);
    rhi_CmdEndRenderPass(s_cl);
}

/* RDC_SHADOW_STRIP: rd_ShadowTris's two triangle lists, or rd_ShadowStrip's
 * float strip with one sign. */
static void doShadowStrip(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdTargetRec *td;
    if (!shadowTargets(r, &td)) {
        return;
    }
    DrawSetup ds;
    if (!prepareDraw(r, &ds) || !ds.tdId) {
        return;
    }
    uint32_t count[2];
    uint64_t vOff;
    static const float noOff[2] = {0.0f, 0.0f};
    if (c->b[0] == RD_SHADOW_TRIS) {
        count[0] = c->u[0];
        count[1] = c->u[3];
        const uint32_t n = count[0] + count[1];
        vOff = rd__RingAlloc((uint64_t)(n ? n : 1) * sizeof(IcoSpriteVertex), 16);
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
        vOff = rd__RingAlloc((uint64_t)(nt ? nt : 1) * 3 * sizeof(IcoSpriteVertex), 16);
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
    rhi_CmdSetVertexBuffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
    RdUniform g1 = rd__DrawGroup(&cb);
    uint32_t first = 0;
    for (int decr = 0; decr < 2; decr++) {
        if (count[decr] == 0) {
            continue;
        }
        const RdPipeKeyInt k = rd__ShadowVolumeKey(&r->st, ds.tc->format, decr);
        RhiPipeline p = rd__GetPipeline(&k);
        if (p.id) {
            rhi_CmdSetPipeline(s_cl, p);
            rd__BindUniform(s_cl, 0, r->frameBG);
            rd__BindUniform(s_cl, 1, g1);
            rhi_CmdSetBindGroup(s_cl, 2, g2);
            rhi_CmdSetStencilRef(s_cl, 0);
            rhi_CmdDraw(s_cl, count[decr], first, 1);
            g_rd.stats.draws++;
        }
        first += count[decr];
    }
}

/* Package RSMALL: the count at the GS size.  The first blur level reads the
 * count with a 2:1 bilinear sprite, which averages a 2x2 block of the PS2's
 * 512x512 count exactly; on a count at s times the resolution the same
 * sprite takes 2x2 taps of an 2s x 2s footprint, and the shadow's integral
 * varies with the volume's sub-pixel position (2.6 % at 4x against 2.0 %).
 * With the count target scaled, doShadowResolve box-averages it down to its
 * GS size (box_reduce_ps) into s_shadowRed and resolveTexture hands that to
 * whatever samples the count as a texture, so the level reads what the PS2's
 * did.  Unscaled targets (Original) never come here. */
void rd__ShadowShutdown(void)
{
    if (s_shadowRed.id) {
        rhi_DestroyTexture(s_shadowRed);
    }
    s_shadowRed = (RhiTexture){0};
    s_shadowRedW = s_shadowRedH = 0;
    s_shadowRedFor = 0;
}

static void shadowReduce(Replay *r, RdTargetRec *tc)
{
    s_shadowRedFor = 0;
    if (!s_shadowRed.id || s_shadowRedW != tc->w || s_shadowRedH != tc->h) {
        if (s_shadowRed.id) {
            rhi_DestroyTexture(s_shadowRed);
        }
        s_shadowRed = rhi_CreateTexture(&(RhiTextureDesc){tc->w, tc->h, 1, RHI_FMT_RGBA8_UNORM,
                                                          RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                          "rd shadow count reduced"});
        s_shadowRedState = RHI_STATE_UNDEFINED;
        s_shadowRedW = tc->w;
        s_shadowRedH = tc->h;
    }
    RdPipeKeyInt k = rd__ShadowReduceKey(); /* RGBA8, like the count */
    RhiPipeline pipe = rd__GetPipeline(&k);
    if (!s_shadowRed.id || !pipe.id) {
        return;
    }
    endPass(r);
    rd__Transition(s_cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    rd__Transition(s_cl, s_shadowRed, &s_shadowRedState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_shadowRed;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = tc->w;
    p.height = tc->h;
    rhi_CmdBeginRenderPass(s_cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)tc->w, (float)tc->h, 0.0f, 1.0f};
    rhi_CmdSetViewport(s_cl, &vp);
    const RhiRect all = {0, 0, tc->w, tc->h};
    rhi_CmdSetScissor(s_cl, &all);
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    cb.uvRect[2] = cb.uvRect[3] = 1.0f;
    cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
    cb.param[0] = (float)tc->tw / (float)tc->w;
    cb.param[1] = (float)tc->th / (float)tc->h;
    cb.param[2] = (float)tc->tw;
    cb.param[3] = (float)tc->th;
    rhi_CmdSetPipeline(s_cl, pipe);
    rd__BindUniform(s_cl, 0, rd__FrameGroup(tc->w, tc->h, 0.0f, 0.0f));
    rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
    rhi_CmdSetBindGroup(s_cl, 2,
                        rd__TexGroup(tc->color, rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                            RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
    rhi_CmdDraw(s_cl, 3, 0, 1);
    rhi_CmdEndRenderPass(s_cl);
    rd__Transition(s_cl, s_shadowRed, &s_shadowRedState, RHI_STATE_SHADER_READ);
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
    beginPass(r, tc, td, r->st.color, r->st.depth, RHI_LOAD_CLEAR, zero, RHI_LOAD_LOAD, 0.0f);
    const RhiRect all = {0, 0, tc->tw, tc->th};
    rhi_CmdSetScissor(s_cl, &all);
    RdUniform g0 = rd__FrameGroup(tc->tw, tc->th, 0.0f, 0.0f);
    RhiBindGroup g2 = rd__TexGroup(g_rd.dummy, rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                           RD_WRAP_CLAMP, RD_WRAP_CLAMP));
    for (int pass = 0; pass < RD_SHADOW_RESOLVE_PASSES; pass++) {
        const RdPipeKeyInt k = rd__ShadowResolveKey(pass);
        RhiPipeline p = rd__GetPipeline(&k);
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
        rhi_CmdSetPipeline(s_cl, p);
        rd__BindUniform(s_cl, 0, g0);
        rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        rhi_CmdSetStencilRef(s_cl, pass < 6 ? (uint8_t)(1u << pass) : 0);
        rhi_CmdDraw(s_cl, 3, 0, 1);
    }
    endPass(r);
    s_shadowRedFor = 0;
    if (tc->tw != tc->w || tc->th != tc->h) {
        shadowReduce(r, tc);
    }
}

/* --------------------------------------------------- fog (wave 4, R4c)
 * RD_POST_FOG (fog_DrawFog, ZFog.c; RENDER_API.md "Depth fog").  The GS
 * copies the Z buffer to 0x2800, copies byte 2 of every word into byte 3
 * through a PSMT4 view, and draws one sprite reading the copy as PSMT8H
 * through the fog CLUT, Z-tested GEQUAL at the sprite's Z under ZMSK.  Here:
 * the Z source's depth (the target of the bound depth view, else the state's
 * depth target) is copied into s_fogDepth, a D32F_S8 texture that can be
 * sampled (the scene's depth is an attachment, not sampled), the LUT is
 * uploaded into s_fogLut, and the sprite is drawn through the sprite vertex
 * shader and fog_lut_ps, which reconstructs the GS Z, takes bits 16..23 as
 * the index, applies the texture function and the Z test, and blends with
 * the state block's ALPHA.  No depth attachment is bound (ZMSK: nothing to
 * write; the test reads the copy, which holds the same values). */
static RhiTexture s_fogDepth, s_fogLut;

static RhiState s_fogDepthState, s_fogLutState;

static uint32_t s_fogDepthW, s_fogDepthH;

void rd__FogShutdown(void)
{
    if (s_fogDepth.id) {
        rhi_DestroyTexture(s_fogDepth);
    }
    if (s_fogLut.id) {
        rhi_DestroyTexture(s_fogLut);
    }
    s_fogDepth = s_fogLut = (RhiTexture){0};
    s_fogDepthW = s_fogDepthH = 0;
}

static void doFog(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    uint32_t zid = r->st.depth;
    const RdTexRec *tv = r->st.ds.texEnabled ? rd__TexRec(r->st.tex) : NULL;
    if (tv && tv->kind == RD_TEXKIND_TARGET && tv->view == RD_VIEW_DEPTH) {
        zid = tv->target;
    }
    RdTargetRec *tz = rd__TargetRec(zid);
    RdTargetRec *tc = rd__TargetRec(r->st.color);
    if (p.lutOffset == ~0u || !tz || !tz->withDepth || !tz->depth.id || !tc || !tc->color.id) {
        rd__LogOnce(RD_ONCE_FOG, "fog without a LUT, a depth source or a colour target: skipped");
        return;
    }
    RdDrawPass dp[2];
    const int np = rd__FogPlan(&r->st, tc->format, dp);
    if (np == 0) {
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
    const uint64_t vOff = rd__RingAlloc(6 * sizeof(IcoSpriteVertex), 16);
    const uint32_t pitchA = rhi_Limits()->copyRowPitchAlign;
    const uint32_t lutPitch = (256 * 4 + pitchA - 1) / pitchA * pitchA;
    const uint64_t lOff = rd__RingAlloc(lutPitch, rhi_Limits()->copyOffsetAlign);
    if (vOff == ~0ull || lOff == ~0ull) {
        return;
    }
    uint8_t topo;
    IcoSpriteVertex out[6];     /* P1: expanded here, copied into the ring */
    setUvShift(tc->sx, tc->sy); /* R7a: the fog sprite on a scaled target */
    const uint32_t nv = expand(v, 2, RD_PRIM_SPRITES, 1, (float)tz->w, (float)tz->h, noOff, out,
                               &topo, r->st.gouraud != 0);
    memcpy(g_rd.ringMap[s_slot] + vOff, out, (size_t)nv * sizeof(out[0]));
    memcpy(g_rd.ringMap[s_slot] + lOff, f->payload + p.lutOffset, 256 * 4);

    endPass(r);
    /* the Z copy (the GS's BITBLT of the Z buffer to 0x2800) */
    if (!s_fogDepth.id || s_fogDepthW != tz->tw || s_fogDepthH != tz->th) {
        if (s_fogDepth.id) {
            rhi_DestroyTexture(s_fogDepth);
        }
        s_fogDepth = rhi_CreateTexture(&(RhiTextureDesc){
            tz->tw, tz->th, 1, RHI_FMT_D32F_S8,
            /* the depth-stencil usage: Vulkan's sampled depth layout
             * (DEPTH_STENCIL_READ_ONLY_OPTIMAL) requires it */
            RHI_TEX_SAMPLED | RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_DST, "rd fog depth"});
        s_fogDepthState = RHI_STATE_UNDEFINED;
        s_fogDepthW = tz->tw;
        s_fogDepthH = tz->th;
    }
    if (!s_fogLut.id) {
        s_fogLut = rhi_CreateTexture(&(RhiTextureDesc){
            256, 1, 1, RHI_FMT_RGBA8_UNORM, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd fog lut"});
        s_fogLutState = RHI_STATE_UNDEFINED;
    }
    if (!s_fogDepth.id || !s_fogLut.id) {
        return;
    }
    rd__Transition(s_cl, tz->depth, &tz->depthState, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, s_fogDepth, &s_fogDepthState, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, tz->depth, (RhiRect){0, 0, tz->tw, tz->th}, s_fogDepth, 0, 0);
    rd__Transition(s_cl, s_fogDepth, &s_fogDepthState, RHI_STATE_SHADER_READ);
    rd__Transition(s_cl, s_fogLut, &s_fogLutState, RHI_STATE_COPY_DST);
    rhi_CmdCopyBufferToTexture(s_cl, g_rd.ring[s_slot], lOff, lutPitch, s_fogLut, 0,
                               (RhiRect){0, 0, 256, 1});
    rd__Transition(s_cl, s_fogLut, &s_fogLutState, RHI_STATE_SHADER_READ);

    /* the pass on the colour target alone, FrameCB and the scissor */
    DrawSetup ds;
    memset(&ds, 0, sizeof(ds));
    ds.tc = tc;
    ds.tex = g_rd.dummy;
    ds.dateTex = g_rd.dummy;
    ds.tw = tz->w;
    ds.th = tz->h;
    ds.textured = 1;
    r->stretch = 1; /* R7a: the fog sprite covers the screen */
    if (!bindDraw(r, &ds).id) {
        return;
    }
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = s_fogDepth;
    b[0].aspect = RHI_ASPECT_DEPTH;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = s_fogLut;
    const RhiBindGroup g2 = bindGroup(g_rd.layoutTex, b, 3);
    if (!g2.id) {
        return;
    }
    rhi_CmdSetVertexBuffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    for (int i = 0; i < np; i++) {
        RhiPipeline pipe = rd__GetPipeline(&dp[i].key);
        if (!pipe.id) {
            continue;
        }
        IcoDrawCB cb;
        fillDrawCB(r, &dp[i], &ds, &cb);
        cb.mode[1] = 0; /* the LUT is PSMCT32: no TEXA */
        cb.col[0] = p.z;
        cb.col[1] = r->st.ds.test.zte ? r->st.ds.test.ztst : RD_ZTST_ALWAYS;
        cb.param[0] = rd__TargetZScale(zid);
        cb.scale[0] = tz->sx; /* R7a: the depth copy's texels per GS pixel */
        cb.scale[1] = tz->sy;
        rhi_CmdSetPipeline(s_cl, pipe);
        rd__BindUniform(s_cl, 0, r->frameBG);
        rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        rhi_CmdDraw(s_cl, nv, 0, 1);
        g_rd.stats.draws++;
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
    }
    (void)topo;
}

/* ------------------------------------------- staticBlur (wave 5, R5a)
 * RD_POST_MOTION_BLUR .. RD_POST_EYE_BLUR: one GS sprite through fx_rect_vs
 * and fx_sprite_ps (rd_blur.c says what is modelled).  The state block's
 * colour target is drawn into; its depth target is bound when the Z test or
 * Z write needs it and has the colour target's size; the texture is the
 * state's (a target view or an image), read with Load.  When the sprite
 * blends, tests DATE or keeps the destination alpha, or samples the target
 * it draws into, the target is first copied into its snapshot, which the
 * shader reads as the destination (t2) and, sampling itself, as the
 * texture: the GS reads both before it writes. */
static void doBlurSprite(Replay *r, const RdFrame *f, const RdCmd *c)
{
    RdPostRec p;
    memcpy(&p, f->payload + c->u[1], sizeof(p));
    const uint32_t screenVtx = rd__BlurScreenFallback(c->b[0], &p, &r->st);
    if (screenVtx != ~0u) { /* R-POST: the reduction at a scale, a hardware sprite */
        RdCmd sc = *c;
        sc.type = RDC_SCREEN;
        sc.b[0] = RD_PRIM_SPRITES;
        sc.b[1] = RD_SPACE_FULLSCREEN;
        sc.b[2] = 1;
        sc.u[0] = screenVtx;
        sc.u[1] = 2;
        doScreen(r, f, &sc);
        return;
    }
    RdTargetRec *tc = rd__TargetRec(r->st.color);
    if (!tc || !tc->color.id || tc->format != RHI_FMT_RGBA8_UNORM) {
        rd__LogOnce(RD_ONCE_BLUR, "staticBlur sprite without an RGBA8 colour target: skipped");
        return;
    }
    const RdDrawState *d = &r->st.ds;
    RdTargetRec *td = rd__TargetRec(r->st.depth);
    if (td && (!td->withDepth || !td->depth.id || td->tw != tc->tw || td->th != tc->th)) {
        td = NULL;
    }
    int useDepth = 0;
    const RdPipeKeyInt key =
        rd__BlurKey(&r->st, tc->format, td ? RHI_FMT_D32F_S8 : RHI_FMT_UNKNOWN, &useDepth);
    if (d->test.ate && d->test.atst != RD_ATST_ALWAYS && d->test.afail == RD_AFAIL_FB_ONLY &&
        useDepth && d->zwrite == RD_ZWRITE_ON) {
        rd__LogOnce(RD_ONCE_BLUR, "staticBlur sprite with AFAIL FB_ONLY and Z write: failing "
                                  "fragments write Z");
    }

    /* the texture */
    RdTargetRec *src = NULL;
    RhiTexture srcTex = g_rd.dummy;
    uint32_t tw = 1, th = 1, tfmt = 0;
    float ssx = 1.0f, ssy = 1.0f; /* R7a: t1 texels per GS texel */
    int textured = 0;
    if (d->texEnabled) {
        RdTexRec *t = rd__TexRec(r->st.tex);
        if (t && t->kind == RD_TEXKIND_IMAGE && t->rhi.id && t->state == RHI_STATE_SHADER_READ) {
            srcTex = t->rhi;
            tw = t->w;
            th = t->h;
            tfmt = t->src;
            textured = 1;
        } else if (t && t->kind == RD_TEXKIND_TARGET && t->view != RD_VIEW_DEPTH) {
            src = rd__TargetRec(t->target);
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
    if (dstRead || selfSample) {
        endPass(r);
        if (!tc->snap.id) {
            tc->snap = rhi_CreateTexture(&(RhiTextureDesc){
                tc->tw, tc->th, 1, tc->format, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd snap"});
            tc->snapState = RHI_STATE_UNDEFINED;
        }
        if (!tc->snap.id) {
            return;
        }
        rd__Transition(s_cl, tc->color, &tc->colorState, RHI_STATE_COPY_SRC);
        rd__Transition(s_cl, tc->snap, &tc->snapState, RHI_STATE_COPY_DST);
        rhi_CmdCopyTexture(s_cl, tc->color, (RhiRect){0, 0, tc->tw, tc->th}, tc->snap, 0, 0);
        rd__Transition(s_cl, tc->snap, &tc->snapState, RHI_STATE_SHADER_READ);
    }
    if (src) {
        if (selfSample) {
            srcTex = tc->snap;
        } else {
            if (src->colorState != RHI_STATE_SHADER_READ) {
                endPass(r);
                rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_SHADER_READ);
            }
            srcTex = src->color;
        }
    }

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
    r->stretch = 1; /* R7a: fx_rect_vs covers the target; no wide x scale */
    const RhiBindGroup g2 = bindDraw(r, &ds);
    if (!g2.id) {
        return;
    }
    RhiPipeline pipe = rd__GetPipeline(&key);
    if (!pipe.id) {
        return;
    }

    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    uint32_t fl = 0;
    if (textured) {
        fl |= RD_FXF_TEXTURED | ((p.lines & 3u) << RD_FXF_TFX_SHIFT);
        if (d->texFn == RD_TEXFN_DECAL && (p.lines & 3u) == 0) {
            fl |= 1u << RD_FXF_TFX_SHIFT; /* a DECAL bound through rd_Texture */
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
    memcpy(cb.col, (const uint32_t[4]){p.rgba[0], p.rgba[1], p.rgba[2], p.rgba[3]}, sizeof(cb.col));
    cb.mode[0] = fl;
    cb.mode[1] = d->texa | (tfmt << 8);
    cb.mode[2] =
        d->test.atst | ((uint32_t)(d->test.ate != 0) << 8) | ((uint32_t)d->test.afail << 16);
    cb.mode[3] = d->test.aref;
    cb.blend[0] = rd__AlphaRegister(d->blend < RD_BLEND_COUNT ? d->blend : RD_BLEND_LERP_AS);
    cb.blend[1] = rd__BlurFeedbackFix(d->blend, d->blendFix, p.scalar[2]);
    cb.blend[2] = d->colclamp;
    cb.blend[3] = p.z;
    rd__BlurUvRect(c->b[0], &p, cb.uvRect); /* R-POST: the reduction's mirror */
    cb.tex[0] = p.scalar[0];
    cb.tex[1] = p.scalar[1];
    cb.tex[2] = (float)tw;
    cb.tex[3] = (float)th;
    cb.scale[0] = ssx;
    cb.scale[1] = ssy;
    memcpy(cb.param, p.rect, sizeof(cb.param));
    rhi_CmdSetPipeline(s_cl, pipe);
    rd__BindUniform(s_cl, 0, r->frameBG);
    rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
    rhi_CmdSetBindGroup(s_cl, 2, g2);
    rhi_CmdDraw(s_cl, 3, 0, 1);
    g_rd.stats.draws++;
    if (key.gs.colorMask & 8) {
        r->writeSerial++;
    }
}

/* ----------------------------------------------------------------- frame */

/* Wave 7 (R7a): the levels a game texture gets: the full chain with the
 * Enhanced filter on and power-of-two sides, else 1 (Original). */
static uint8_t texLevels(const RdTexRec *t)
{
    if (!g_rd.filterUpgrade || t->format == RD_TEXEL_R8 || t->w < 2 || t->h < 2 ||
        (t->w & (t->w - 1)) || (t->h & (t->h - 1))) {
        return 1;
    }
    uint8_t n = 1;
    for (uint32_t m = t->w > t->h ? t->w : t->h; m > 1; m >>= 1) {
        n++;
    }
    return n;
}

/* The alpha the coverage of the Enhanced mips keeps: the semi-transparent
 * lists' default test, alpha > 64 (GS alpha, 0x80 = 1.0; rd.h RdListDefault) */
#define RD_MIP_COVERAGE_REF 64

static uint64_t estimateRing(const RdFrame *f, int keep)
{
    const uint64_t align = rhi_Limits()->uniformAlign;
    const uint64_t pitchA = rhi_Limits()->copyRowPitchAlign;
    uint64_t total = 64 * 1024;
    /* package AA1: with PRIM.AA1 anywhere in the frame, room for every
     * screen command's AA1 geometry too (at most 7 vertices per expanded
     * vertex, 21 per vertex of a triangle strip) */
    int hasAa1 = f->startState.aa1 != 0;
    for (int l = rd__FirstList(keep); l < RD_LIST_COUNT && !hasAa1; l++) {
        for (uint32_t i = 0; i < f->lists[l].count && !hasAa1; i++) {
            hasAa1 = f->lists[l].cmds[i].type == RDC_AA1;
        }
    }
    for (int l = rd__FirstList(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            total += 4 * align;
            if (c->type == RDC_SCREEN) {
                total += (uint64_t)c->u[1] * 6 * sizeof(IcoSpriteVertex) + 16;
                if (hasAa1) {
                    total += (uint64_t)c->u[1] * 21 * sizeof(IcoSpriteAa1Vertex) + 16;
                }
            } else if (c->type == RDC_SHADOW_STRIP) {
                /* wave 4 (R4b): the triangles, or a strip's 3 (n - 2) */
                total += ((uint64_t)c->u[0] * 3 + c->u[3]) * sizeof(IcoSpriteVertex) + 64;
            } else if (c->type == RDC_SHADOW_RESOLVE) {
                total += (RD_SHADOW_RESOLVE_PASSES + 1) * (sizeof(IcoDrawCB) + align);
            } else if (c->type == RDC_POST_STUB && c->b[0] == RD_POST_FOG) {
                /* wave 4 (R4c): the sprite, the LUT upload, two DrawCBs */
                total += 6 * sizeof(IcoSpriteVertex) + 16 + 256 * 4 + pitchA +
                         rhi_Limits()->copyOffsetAlign + 2 * (sizeof(IcoDrawCB) + align);
            } else if (c->type >= RDC_MESH && c->type <= RDC_PARTICLES) {
                /* wave 3: per pass DrawCB + VuCB, the bones, the stream and
                 * the indices (a mesh drawn twice is counted twice) */
                RdVuPayload p;
                memcpy(&p, f->payload + c->u[1], sizeof(p));
                const uint64_t passes = 4 * (2 * (uint64_t)(p.batchCount ? p.batchCount : 1) + 1);
                total += passes * (sizeof(IcoDrawCB) + sizeof(IcoVuCB) + 2 * align);
                total += sizeof(IcoVuBoneCB) + align + (uint64_t)p.streamQw * 16 + align;
                total += (uint64_t)p.batchCount * (p.vertsPerBatch + 1) * 12 + 16;
                const RdMeshRec *m = c->type <= RDC_SKINNED ? rd__MeshRec(c->u[0]) : NULL;
                if (m) {
                    total += (uint64_t)m->vertexCount * m->qwPerVertex * 16 +
                             (uint64_t)m->indexCount * 4 + 2 * align;
                }
            }
        }
    }
    const bool walk = g_rd.texDirtyCount != 0 || g_rd.texLevelsFilter != (int)g_rd.filterUpgrade;
    for (uint32_t i = 0; walk && i < RD_MAX_TEXTURES; i++) {
        const RdTexRec *t = &g_rd.textures[i];
        if (t->live && t->kind == RD_TEXKIND_IMAGE && (t->dirty || t->mipLevels != texLevels(t))) {
            uint64_t pitch =
                ((uint64_t)t->w * rd__TexelBytes(t->format) + pitchA - 1) / pitchA * pitchA;
            uint64_t one = pitch * t->h + rhi_Limits()->copyOffsetAlign;
            /* R7a: a mip chain adds at most the base again (with the
             * padding of every level) */
            total +=
                texLevels(t) > 1 ? 2 * one + 16 * (pitchA + rhi_Limits()->copyOffsetAlign) : one;
        }
    }
    return total;
}

static bool ensureRing(uint64_t need)
{
    if (g_rd.ringCap[s_slot] >= need) {
        return true;
    }
    uint64_t cap = g_rd.ringCap[s_slot] ? g_rd.ringCap[s_slot] : 4u << 20;
    while (cap < need) {
        cap *= 2;
    }
    if (g_rd.ring[s_slot].id) {
        rhi_DestroyBuffer(g_rd.ring[s_slot]);
    }
    g_rd.ring[s_slot] = rhi_CreateBuffer(&(RhiBufferDesc){
        cap,
        RHI_BUF_VERTEX | RHI_BUF_INDEX | RHI_BUF_UNIFORM | RHI_BUF_STORAGE_READ | RHI_BUF_COPY_SRC,
        RHI_MEM_UPLOAD, "rd ring"});
    g_rd.ringMap[s_slot] = g_rd.ring[s_slot].id ? rhi_MapBuffer(g_rd.ring[s_slot]) : NULL;
    g_rd.ringCap[s_slot] = g_rd.ringMap[s_slot] ? cap : 0;
    return g_rd.ringMap[s_slot] != NULL;
}

/* R7a: levels 1.. of a game texture: 2x2 box filtered from the base
 * (rdtex_BuildMipChain), alpha coverage kept (rdtex_KeepAlphaCoverage),
 * each level copied into the ring and onto its subresource. */
static void uploadMips(RdTexRec *t, uint8_t levels)
{
    const uint32_t pitchA = rhi_Limits()->copyRowPitchAlign;
    const uint32_t offA = rhi_Limits()->copyOffsetAlign;
    uint8_t *chain = malloc(rdtex_MipChainBytes(t->w, t->h) + 4);
    if (!chain) {
        return;
    }
    const uint32_t n = rdtex_BuildMipChain(t->pixels, t->w, t->h, chain);
    rdtex_KeepAlphaCoverage(t->pixels, t->w, t->h, chain, n, RD_MIP_COVERAGE_REF);
    const uint8_t *src = chain;
    uint32_t w = t->w, h = t->h;
    for (uint32_t l = 1; l < levels && l <= n; l++) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
        const uint32_t pitch = (w * 4 + pitchA - 1) / pitchA * pitchA;
        const uint64_t off = rd__RingAlloc((uint64_t)pitch * h, offA);
        if (off == ~0ull) {
            break;
        }
        for (uint32_t y = 0; y < h; y++) {
            memcpy(g_rd.ringMap[s_slot] + off + (uint64_t)y * pitch, src + (size_t)y * w * 4,
                   (size_t)w * 4);
        }
        rhi_CmdCopyBufferToTexture(s_cl, g_rd.ring[s_slot], off, pitch, t->rhi, l,
                                   (RhiRect){0, 0, w, h});
        src += (size_t)w * h * 4;
    }
    free(chain);
}

static void uploadTextures(void)
{
    const uint32_t pitchA = rhi_Limits()->copyRowPitchAlign;
    const uint32_t offA = rhi_Limits()->copyOffsetAlign;
    if (g_rd.dummyState != RHI_STATE_SHADER_READ) {
        uint64_t off = rd__RingAlloc(4, offA);
        static const uint8_t white[4] = {255, 255, 255, 255};
        memcpy(g_rd.ringMap[s_slot] + off, white, 4);
        rd__Transition(s_cl, g_rd.dummy, &g_rd.dummyState, RHI_STATE_COPY_DST);
        rhi_CmdCopyBufferToTexture(s_cl, g_rd.ring[s_slot], off, pitchA > 4 ? pitchA : 4,
                                   g_rd.dummy, 0, (RhiRect){0, 0, 1, 1});
        rd__Transition(s_cl, g_rd.dummy, &g_rd.dummyState, RHI_STATE_SHADER_READ);
    }
    /* P1: the table is walked only while a texture is dirty or the filter
     * option changed since the last walk */
    if (g_rd.texDirtyCount == 0 && g_rd.texLevelsFilter == (int)g_rd.filterUpgrade) {
        return;
    }
    g_rd.texLevelsFilter = (int)g_rd.filterUpgrade;
    uint32_t stillDirty = 0;
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        RdTexRec *t = &g_rd.textures[i];
        if (!t->live || t->kind != RD_TEXKIND_IMAGE) {
            continue;
        }
        const uint8_t levels = texLevels(t);
        if (t->rhi.id && t->mipLevels != levels) {
            /* R7a: the filter option changed: a new texture with the new
             * level count (the old one is destroyed after the frames in
             * flight, rhi_DestroyTexture) */
            rhi_DestroyTexture(t->rhi);
            t->rhi = (RhiTexture){0};
            t->dirty = 1;
        }
        stillDirty += t->dirty; /* less the ones uploaded below */
        if (!t->dirty) {
            continue;
        }
        /* R8: the format's texels; the changed rectangle only, unless the
         * RHI texture is new or has a mip chain to rebuild */
        const uint32_t bpp = rd__TexelBytes(t->format);
        int whole = levels > 1;
        if (!t->rhi.id) {
            t->rhi = rhi_CreateTexture(
                &(RhiTextureDesc){t->w, t->h, levels,
                                  t->format == RD_TEXEL_R8 ? RHI_FMT_R8_UNORM : RHI_FMT_RGBA8_UNORM,
                                  RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd texture"});
            t->state = RHI_STATE_UNDEFINED;
            t->mipLevels = levels;
            if (!t->rhi.id) {
                continue;
            }
            whole = 1;
        }
        uint32_t rx = 0, ry = 0, rw = t->w, rh = t->h;
        if (!whole && t->dirtyX1 > t->dirtyX0 && t->dirtyY1 > t->dirtyY0 && t->dirtyX1 <= t->w &&
            t->dirtyY1 <= t->h) {
            rx = t->dirtyX0;
            ry = t->dirtyY0;
            rw = t->dirtyX1 - t->dirtyX0;
            rh = t->dirtyY1 - t->dirtyY0;
        }
        const uint32_t pitch = (rw * bpp + pitchA - 1) / pitchA * pitchA;
        uint64_t off = rd__RingAlloc((uint64_t)pitch * rh, offA);
        if (off == ~0ull) {
            continue;
        }
        for (uint32_t y = 0; y < rh; y++) {
            memcpy(g_rd.ringMap[s_slot] + off + (uint64_t)y * pitch,
                   t->pixels + ((size_t)(ry + y) * t->w + rx) * bpp, (size_t)rw * bpp);
        }
        rd__Transition(s_cl, t->rhi, &t->state, RHI_STATE_COPY_DST);
        rhi_CmdCopyBufferToTexture(s_cl, g_rd.ring[s_slot], off, pitch, t->rhi, 0,
                                   (RhiRect){(int32_t)rx, (int32_t)ry, rw, rh});
        if (levels > 1) {
            uploadMips(t, levels);
        }
        rd__Transition(s_cl, t->rhi, &t->state, RHI_STATE_SHADER_READ);
        t->dirty = 0;
        t->dirtyX0 = t->dirtyY0 = t->dirtyX1 = t->dirtyY1 = 0;
        stillDirty--;
        g_rd.stats.textureUploads++;
        g_rdPerf.textureUploads++;
        /* P1: a texture uploaded on 60 replays in a row is named once */
        t->uploadStreak = t->lastUpload + 2 >= g_rd.replayCounter ? t->uploadStreak + 1 : 0;
        t->lastUpload = g_rd.replayCounter;
        if (t->uploadStreak == 60 && !t->streakLogged) {
            t->streakLogged = 1;
            rd__Log("texture \"%s\" (%ux%u) uploaded on 60 replays in a row: its content "
                    "changes every frame",
                    t->name, t->w, t->h);
        }
    }
    g_rd.texDirtyCount = stillDirty;
}

/* Package P1: the static meshes a frame draws live in the device arena
 * (rd_mesh.c): uploaded the first time a replay draws them and again only
 * after rd_UpdateVuMesh rewrote them (gpuDirty, the morphs), through the
 * ring and one buffer copy each, all before the first pass.  The
 * interpolation's scratch meshes (transient) and a mesh the arena has no
 * room for stay on the ring path in doVu. */
static void uploadMeshes(const RdFrame *f, int keep)
{
    const uint64_t ua = rhi_Limits()->uniformAlign;
    for (int l = rd__FirstList(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            if (c->type != RDC_MESH && c->type != RDC_SKINNED) {
                continue;
            }
            RdMeshRec *m = rd__MeshRec(c->u[0]);
            if (!m || !m->vu || m->transient || (m->gpuChunk && !m->gpuDirty) ||
                m->replaySeen == g_rd.replayCounter) {
                continue;
            }
            m->replaySeen = g_rd.replayCounter;
            const uint64_t sb = (uint64_t)m->vertexCount * m->qwPerVertex * 16;
            const uint64_t ib = (uint64_t)m->indexCount * 4;
            if (!rd__MeshGpuReserve(m, sb, ib, ua)) {
                m->replaySeen = 0; /* doVu's ring path */
                continue;
            }
            /* stream and indices in the ring as they lie in the arena */
            const uint64_t span = m->gpuIndexOff - m->gpuOff + (ib ? ib : 4);
            const uint64_t off = rd__RingAlloc(span, 16);
            if (off == ~0ull) {
                /* the range is reserved but not written: doVu must not draw
                 * from it, and the next replay uploads it */
                m->replaySeen = 0;
                m->gpuDirty = 1;
                continue;
            }
            if (sb) {
                memcpy(g_rd.ringMap[s_slot] + off, m->stream, sb);
            }
            if (ib) {
                memcpy(g_rd.ringMap[s_slot] + off + (m->gpuIndexOff - m->gpuOff), m->index, ib);
            }
            rhi_CmdCopyBuffer(s_cl, g_rd.ring[s_slot], off, rd__MeshGpuBuffer(m->gpuChunk),
                              m->gpuOff, span);
            m->gpuDirty = 0;
            g_rdPerf.meshUploads++;
            g_rdPerf.meshUploadBytes += sb + ib;
        }
    }
}

/* Package P1: a temporary target that took a pooled texture (rd_core.c
 * rd__TempTargetAlloc), or a new one, starts as a new texture does on the
 * drivers the tests run on: zero colour, depth and stencil.  Every pending
 * target is cleared, whichever frame it belongs to: none of them was drawn
 * since it was taken. */
static void clearNewTargets(void)
{
    for (int i = RD_TARGET_COUNT; i < RD_MAX_TARGETS; i++) {
        RdTargetRec *t = &g_rd.targets[i];
        if (!t->live || !t->clearPending || !t->color.id) {
            continue;
        }
        t->clearPending = 0;
        rd__Transition(s_cl, t->color, &t->colorState, RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = t->color;
        p.color[0].load = RHI_LOAD_CLEAR;
        p.colorCount = 1;
        if (t->withDepth && t->depth.id) {
            rd__Transition(s_cl, t->depth, &t->depthState, RHI_STATE_DEPTH_WRITE);
            p.depth.texture = t->depth;
            p.depth.depthLoad = RHI_LOAD_CLEAR;
            p.depth.stencilLoad = RHI_LOAD_CLEAR;
        }
        p.width = t->tw;
        p.height = t->th;
        rhi_CmdBeginRenderPass(s_cl, &p);
        rhi_CmdEndRenderPass(s_cl);
        g_rdPerf.tempClears++;
    }
}

static const char *stubName(uint8_t type)
{
    switch (type) {
    case RDC_WORLD_PRIMS:
        return "rd_WorldPrims (wave 5)";
    case RDC_POST_STUB:
        return "rd_Post (shadow resolve, blur: wave 5)";
    default:
        return "unknown command";
    }
}

static bool replayFrame(const RdFrame *f, int keep, bool present);

/* F2: the longest replay (CPU side: recording, pipeline creation, the
 * submit and present) since rd_ReplayTimeMax last reset it, in ms */
static double s_replayMaxMs;

static uint32_t s_replayCount;

bool rd__ReplayFrame(const RdFrame *f, int keep, bool present)
{
    const double t0 = rd__NowMs();
    rd__PerfBegin(f, keep, present);
    const bool ok = replayFrame(f, keep, present);
    g_rdPerf.uploadBytes = s_ringOff;
    rd__PerfEnd();
    const double ms = rd__NowMs() - t0;
    if (ms > s_replayMaxMs) {
        s_replayMaxMs = ms;
    }
    s_replayCount++;
    return ok;
}

double rd_ReplayTimeMax(int reset, uint32_t *count)
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
    double t = rd__NowMs(), t1;
    rd__WaitFrame();
    rd__PerfCollectGpu(); /* P1: the timestamps of the slot just recycled */
    g_rdPerf.waitMs = (t1 = rd__NowMs()) - t;
    t = t1;
    s_slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    g_rd.replayCounter++;
    s_ringOff = 0;
    g_rd.deferText = false;
    if (present) {
        /* package OV: before the ring and the uploads; package DEF: the
         * deferred text first (sets g_rd.deferText) */
        rd__OverlayCollect(f, keep);
    }
    if (!ensureRing(estimateRing(f, keep) + rd__OverlayRingBytes())) {
        rd__Log("could not allocate the upload ring");
        return false;
    }
    g_rdPerf.uploadMs = (t1 = rd__NowMs()) - t;
    t = t1;
    const bool doPresent = present && rd__PresentAcquire();
    g_rdPerf.acquireMs = (t1 = rd__NowMs()) - t;
    t = t1;
    s_cl = rhi_BeginCommands();
    if (!s_cl.id) {
        return false;
    }
    rd__PerfStamp(s_cl, RD_PERF_TS_BEGIN);
    uploadTextures();
    uploadMeshes(f, keep); /* P1 */
    clearNewTargets();     /* P1 */
    rd__PerfStamp(s_cl, RD_PERF_TS_LISTS);
    g_rdPerf.uploadMs += (t1 = rd__NowMs()) - t;
    t = t1;
    const double bind0 = g_rdPerf.bindMs;

    if (f->hasCamera) {
        rd__SetReplayCamera(&f->camera);
    }
    Replay r;
    memset(&r, 0, sizeof(r));
    r.st = f->startState;
    for (int l = rd__FirstList(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *list = &f->lists[l];
        for (uint32_t i = 0; i < list->count; i++) {
            const RdCmd *c = &list->cmds[i];
            if (rd__ApplyState(&r.st, c)) {
                continue;
            }
            switch (c->type) {
            case RDC_NOP:
            case RDC_OVERLAY_TEXT: /* package DEF: collected by the present */
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
                doVu(&r, f, c); /* wave 3 (R3ab) */
                break;
            case RDC_SHADOW_RESET: /* wave 4 (R4b) */
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
                    doFog(&r, f, c); /* wave 4 (R4c) */
                    break;
                }
                if (rd__IsBlurKind(c->b[0])) {
                    doBlurSprite(&r, f, c); /* wave 5 (R5a) */
                    break;
                }
                endPass(&r);
                rd__NotImplemented(stubName(c->type));
                break;
            default:
                endPass(&r);
                rd__NotImplemented(stubName(c->type));
                break;
            }
        }
        rd__PerfStamp(s_cl, RD_PERF_TS_LIST0 + (uint32_t)l);
    }
    endPass(&r);
    if (doPresent) {
        rd__PresentRecord(s_cl);
        rd__PerfStamp(s_cl, RD_PERF_TS_PRESENT);
    }
    g_rdPerf.walkMs = (t1 = rd__NowMs()) - t - (g_rdPerf.bindMs - bind0);
    t = t1;
    rhi_EndCommands(s_cl);
    rhi_Submit(s_cl);
    g_rdPerf.submitMs = (t1 = rd__NowMs()) - t;
    t = t1;
    if (doPresent) {
        rd__PresentFinish();
    }
    g_rdPerf.presentMs = rd__NowMs() - t;
    g_rd.stats.pipelines = rd__PipelineCount();
    g_rd.deferText = false;
    return true;
}

/* ------------------------------------------------------------- readback */

/* bpp: the format's bytes a texel (R8: 1 for the coverage textures) */
static bool readTextureBpp(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, uint32_t bpp,
                           void *dst, size_t dstSize)
{
    if (!t.id || dstSize < (size_t)w * h * bpp) {
        return false;
    }
    if (*state != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_BeginCommands();
        if (!cl.id) {
            return false;
        }
        rd__Transition(cl, t, state, RHI_STATE_COPY_SRC);
        rhi_EndCommands(cl);
        rhi_Submit(cl);
    }
    uint32_t pitch = 0;
    const double t0 = rd__NowMs();
    const bool ok = rhi_ReadbackTexture(t, RHI_ASPECT_COLOR, dst, dstSize, &pitch);
    rd__PerfReadbackMs(rd__NowMs() - t0); /* P1: dumps and screenshots only */
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

bool rd__ReadTexture(RdTex tex, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTexRec *t = rd__TexRec(tex.id);
    if (!g_rd.hasDevice || !t || t->kind != RD_TEXKIND_IMAGE || !t->rhi.id ||
        t->state == RHI_STATE_UNDEFINED) {
        return false;
    }
    if (w) {
        *w = t->w;
    }
    if (h) {
        *h = t->h;
    }
    const bool ok =
        readTextureBpp(t->rhi, &t->state, t->w, t->h, rd__TexelBytes(t->format), dst, dstSize);
    /* draws sample image textures only in SHADER_READ (resolveTexture) */
    if (t->state != RHI_STATE_SHADER_READ) {
        RhiCommandList cl = rhi_BeginCommands();
        if (cl.id) {
            rd__Transition(cl, t->rhi, &t->state, RHI_STATE_SHADER_READ);
            rhi_EndCommands(cl);
            rhi_Submit(cl);
        }
    }
    return ok;
}

bool rd__ReadTarget(RdTarget target, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__TargetRec(target.id);
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

/* ---------------------------------------------------------- camera probe */

bool rd__CameraProbe(const RdCamera *cam, const float p[4], float out[3][4])
{
    if (!g_rd.hasDevice || !cam) {
        return false;
    }

    /* one pixel per float: column = component, row = mul(g_view, p),
     * mul(g_proj, mul(g_view, p)), mul(g_viewProj, p) */
    enum { PW = 4, PH = 3 };

    RhiTexture t = rhi_CreateTexture(&(RhiTextureDesc){PW, PH, 1, RHI_FMT_RGBA8_UNORM,
                                                       RHI_TEX_RENDER_TARGET | RHI_TEX_COPY_SRC,
                                                       "rd camera probe"});
    RhiState ts = RHI_STATE_UNDEFINED;
    if (!t.id) {
        return false;
    }
    rd__WaitFrame();
    s_slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    g_rd.replayCounter++;
    s_ringOff = 0;
    bool ok = ensureRing(64 * 1024);
    s_cl = ok ? rhi_BeginCommands() : (RhiCommandList){0};
    ok = ok && s_cl.id;
    if (ok) {
        const RdCamera saved = s_replayCam;
        const int savedHas = s_hasReplayCam;
        rd__SetReplayCamera(cam);
        uploadTextures(); /* the dummy texture group 2 binds */
        rd__Transition(s_cl, t, &ts, RHI_STATE_RENDER_TARGET);
        RhiRenderPassDesc pd;
        memset(&pd, 0, sizeof(pd));
        pd.color[0].texture = t;
        pd.color[0].load = RHI_LOAD_CLEAR;
        pd.colorCount = 1;
        pd.width = PW;
        pd.height = PH;
        rhi_CmdBeginRenderPass(s_cl, &pd);
        RhiViewport vp = {0.0f, 0.0f, (float)PW, (float)PH, 0.0f, 1.0f};
        rhi_CmdSetViewport(s_cl, &vp);
        const RhiRect sc = {0, 0, PW, PH};
        rhi_CmdSetScissor(s_cl, &sc);
        RdPipeKeyInt k = rd__PostKey(RD_VS_BLIT, RD_FS_CAMERA_PROBE, RHI_FMT_RGBA8_UNORM);
        RhiPipeline pipe = rd__GetPipeline(&k);
        if (pipe.id) {
            IcoDrawCB cb;
            memset(&cb, 0, sizeof(cb));
            memcpy(cb.param, p, sizeof(cb.param));
            cb.uvRect[2] = cb.uvRect[3] = 1.0f;
            cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
            rhi_CmdSetPipeline(s_cl, pipe);
            rd__BindUniform(s_cl, 0, rd__FrameGroup(PW, PH, 0.0f, 0.0f));
            rd__BindUniform(s_cl, 1, rd__DrawGroup(&cb));
            rhi_CmdSetBindGroup(
                s_cl, 2,
                rd__TexGroup(g_rd.dummy, rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                     RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
            rhi_CmdDraw(s_cl, 3, 0, 1);
        }
        rhi_CmdEndRenderPass(s_cl);
        rhi_EndCommands(s_cl);
        rhi_Submit(s_cl);
        s_replayCam = saved;
        s_hasReplayCam = savedHas;
        ok = pipe.id != 0;
    }
    uint8_t px[PW * PH * 4];
    ok = ok && readTexture(t, &ts, PW, PH, px, sizeof(px));
    rhi_WaitIdle();
    rhi_DestroyTexture(t);
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

bool rd__ReadPresent(void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
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
