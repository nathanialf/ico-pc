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
 *                    for feedback passes, RENDER_API.md section 7)
 *   RDC_COPY         texture copy (gif_MoveImage)
 *   RDC_MESH, RDC_SKINNED, RDC_GRID, RDC_PARTICLES
 *                    the VU1 program shaders (wave 3, R3ab; doVu below)
 *   RDC_SHADOW_RESET, RDC_SHADOW_STRIP, RDC_SHADOW_RESOLVE
 *   RDC_POST_STUB    of kind RD_POST_FOG (wave 4, R4c): doFog
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
#include <string.h>
#include "rd_internal.h"
#include "rd_mesh.h"
#include "shader_consts.h"
#include "shaders_gen.h"

static const char *const s_vsNames[RD_VS_COUNT] = {
    "sprite_ui_vs",     "sprite_world_vs", "blit_vs",        "blend_int_vs",    "vu_prelit_vs",
    "vu_lit_vs",        "vu_lit_spec_vs",  "vu_reflect_vs",  "vu_skin_vs",      "vu_skin_spec_vs",
    "vu_skin_debug_vs", "vu_grid_vs",      "vu_grid_lit_vs", "vu_grid_spec_vs", "vu_particle_vs"};

static const char *const s_fsNames[RD_FS_COUNT] = {
    "sprite_ps",       "blit_ps", "blend_int_ps", "date_snap_ps",
    "camera_probe_ps", "vu_ps",   "fog_lut_ps"};

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
    s_rhiUp = true;
    const RhiLimits *lim = rhi_Limits();
    if (!lim->dualSourceBlend || !lim->stencilWrap) {
        rd__Log("device lacks dual-source blend or stencil wrap");
        return false;
    }
    const uint32_t VS = 1u << RHI_STAGE_VERTEX, FS = 1u << RHI_STAGE_FRAGMENT;
    const RhiBindSlot s0[1] = {{0, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
    const RhiBindSlot s1[1] = {{1, RHI_BIND_UNIFORM_BUFFER, VS | FS}};
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
                               {1, RHI_BIND_UNIFORM_BUFFER, VS | FS},
                               {2, RHI_BIND_UNIFORM_BUFFER, VS},
                               {3, RHI_BIND_UNIFORM_BUFFER, VS}};
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
    for (int i = 0; i < RD_SAMPLER_COUNT; i++) {
        RhiSamplerDesc sd;
        memset(&sd, 0, sizeof(sd));
        sd.mag = (i & 1) ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.min = (i & 2) ? RHI_FILTER_LINEAR : RHI_FILTER_NEAREST;
        sd.mip = RHI_FILTER_NEAREST;
        sd.s = (i & 4) ? RHI_WRAP_CLAMP : RHI_WRAP_REPEAT;
        sd.t = (i & 8) ? RHI_WRAP_CLAMP : RHI_WRAP_REPEAT;
        sd.maxAnisotropy = 1.0f;
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
    rd__FogShutdown(); /* wave 4 (R4c) */
    if (g_rd.dummy.id) {
        rhi_DestroyTexture(g_rd.dummy);
    }
    for (int i = 0; i < RD_SAMPLER_COUNT; i++) {
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

static RhiBindGroup uniformGroup(RhiBindGroupLayout layout, uint32_t slot, const void *data,
                                 uint32_t size)
{
    uint64_t off = rd__RingAlloc(size, rhi_Limits()->uniformAlign);
    if (off == ~0ull) {
        return (RhiBindGroup){0};
    }
    memcpy(g_rd.ringMap[s_slot] + off, data, size);
    RhiBinding b;
    memset(&b, 0, sizeof(b));
    b.slot = slot;
    b.type = RHI_BIND_UNIFORM_BUFFER;
    b.buffer = g_rd.ring[s_slot];
    b.offset = off;
    b.size = size;
    return rhi_CreateBindGroup(&(RhiBindGroupDesc){layout, &b, 1});
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

RhiBindGroup rd__FrameGroup(uint32_t targetW, uint32_t targetH, float originX, float originY)
{
    return rd__FrameGroupZ(targetW, targetH, originX, originY, 1.0f / 16777216.0f);
}

RhiBindGroup rd__FrameGroupZ(uint32_t targetW, uint32_t targetH, float originX, float originY,
                             float zScale)
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
    cb.origin[2] = 0.5f; /* GS integer coordinates on GPU pixel centres */
    cb.origin[3] = 0.5f;
    /* Original: both spaces identity.  Mirror, widescreen anchoring and the
     * wide projection fill these in the Enhanced presets (wave 6). */
    cb.space[0][0] = cb.space[0][1] = 1.0f;
    cb.space[1][0] = cb.space[1][1] = 1.0f;
    /* the GS Z scale of the bound depth buffer: 2^-32 for the game's PSMZ32
     * (R2c), so UI Z values above 2^24 keep their order */
    cb.z[0] = zScale;
    cb.misc[0] = (float)g_rd.replayCounter;
    cb.misc[1] = (float)g_rd.settings.preset;
    return uniformGroup(g_rd.layoutFrame, 0, &cb, sizeof(cb));
}

RhiBindGroup rd__DrawGroup(const void *drawCB)
{
    return uniformGroup(g_rd.layoutDraw, 1, drawCB, sizeof(IcoDrawCB));
}

RhiBindGroup rd__TexGroupDate(RhiTexture t, RhiSampler s, RhiTexture date)
{
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
    return rhi_CreateBindGroup(&(RhiBindGroupDesc){g_rd.layoutTex, b, 3});
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

/* ---------------------------------------------------------------- passes */

typedef struct Replay {
    RdStateBlock st;
    int passOpen;
    uint32_t passColor, passDepth;
    RhiBindGroup frameBG;
    uint32_t frameKey[5]; /* colour, gsW, gsH, useOffset, pass serial */
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
    p.width = c->w;
    p.height = c->h;
    rhi_CmdBeginRenderPass(s_cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)c->w, (float)c->h, 0.0f, 1.0f};
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

static void convVtx(const RdScreenVtx *s, int uvFixed, float tw, float th, const float uvOff[2],
                    IcoSpriteVertex *o)
{
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
        /* STQ: per-vertex divide; screen prims carry Q = 1 in practice */
        float q = s->q != 0.0f ? s->q : 1.0f;
        if (q != 1.0f) {
            rd__LogOnce(RD_ONCE_STQ, "screen prim with Q != 1: divided per vertex");
        }
        o->u = s->s / q * tw;
        o->v = s->t / q * th;
    }
    o->u += uvOff[0] * tw;
    o->v += uvOff[1] * th;
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
    /* pool full of other sizes: recycle the first slot */
    RdScratch *s = &g_rd.scratch[which];
    rhi_DestroyTexture(s->tex);
    s->tex = (RhiTexture){0};
    return scratchGet(w, h, which);
}

/* The texture a draw samples, after any state change it needs.  Returns the
 * RhiTexture and its size and TEXFMT; dummy when untextured. */
static RhiTexture resolveTexture(Replay *r, RdTargetRec *drawTarget, uint32_t drawTargetId,
                                 uint32_t *w, uint32_t *h, uint32_t *fmt, int *textured)
{
    *textured = 0;
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
    if (t->target == drawTargetId && src == drawTarget) {
        /* the GS reads the buffer it is drawing into: sample a copy */
        endPass(r);
        if (!src->snap.id) {
            src->snap = rhi_CreateTexture(&(RhiTextureDesc){
                src->w, src->h, 1, src->format, RHI_TEX_SAMPLED | RHI_TEX_COPY_DST, "rd snap"});
            src->snapState = RHI_STATE_UNDEFINED;
        }
        rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
        rd__Transition(s_cl, src->snap, &src->snapState, RHI_STATE_COPY_DST);
        rhi_CmdCopyTexture(s_cl, src->color, (RhiRect){0, 0, src->w, src->h}, src->snap, 0, 0);
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
    uint32_t w = tc->w, h = tc->h;
    if (w > sn->w || h > sn->h) {
        rd__LogOnce(RD_ONCE_DATE_SIZE, "DATE on a target larger than the snapshot: clipped");
        w = w > sn->w ? sn->w : w;
        h = h > sn->h ? sn->h : h;
    }
    endPass(r);
    rd__Transition(s_cl, tc->color, &tc->colorState, RHI_STATE_SHADER_READ);
    rd__Transition(s_cl, sn->color, &sn->colorState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = sn->color;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = sn->w;
    p.height = sn->h;
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
        cb.tex[0] = (float)tc->w;
        cb.tex[1] = (float)tc->h;
        cb.tex[2] = 1.0f / (float)tc->w;
        cb.tex[3] = 1.0f / (float)tc->h;
        rhi_CmdSetPipeline(s_cl, pipe);
        rhi_CmdSetBindGroup(s_cl, 0, rd__FrameGroup(w, h, 0.0f, 0.0f));
        rhi_CmdSetBindGroup(s_cl, 1, rd__DrawGroup(&cb));
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

/* What a draw binds besides its geometry: doScreen and the VU draws. */
typedef struct DrawSetup {
    RdTargetRec *tc;
    uint32_t tdId;
    RhiFormat depthFmt;
    RhiTexture tex, dateTex;
    uint32_t tw, th, tfmt;
    int textured;
} DrawSetup;

/* The target, the DATE snapshot and the texture (each may end the open
 * pass: they run before the draw's pass begins). */
static bool prepareDraw(Replay *r, DrawSetup *ds)
{
    memset(ds, 0, sizeof(*ds));
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
    ds->tex = resolveTexture(r, ds->tc, r->st.color, &ds->tw, &ds->th, &ds->tfmt, &ds->textured);
    return true;
}

/* The scissor, the pass and FrameCB; returns the texture group, id 0 when
 * the scissor leaves nothing to draw. */
static RhiBindGroup bindDraw(Replay *r, const DrawSetup *ds)
{
    RdTargetRec *tc = ds->tc;
    /* scissor (SCISSOR_1, inclusive) clipped to the target */
    int32_t x0 = r->st.scissor[0] < 0 ? 0 : r->st.scissor[0];
    int32_t y0 = r->st.scissor[1] < 0 ? 0 : r->st.scissor[1];
    int32_t x1 = r->st.scissor[2] >= (int32_t)tc->w ? (int32_t)tc->w - 1 : r->st.scissor[2];
    int32_t y1 = r->st.scissor[3] >= (int32_t)tc->h ? (int32_t)tc->h - 1 : r->st.scissor[3];
    if (x1 < x0 || y1 < y0) {
        return (RhiBindGroup){0};
    }
    RhiSampler smp = rd__Sampler((RdFilter)r->st.ds.magFilter, (RdFilter)r->st.ds.minFilter,
                                 (RdWrap)r->st.ds.wrap.s, (RdWrap)r->st.ds.wrap.t);
    RhiBindGroup g2 = rd__TexGroupDate(ds->tex, smp, ds->dateTex);

    if (!r->passOpen || r->passColor != r->st.color || r->passDepth != ds->tdId) {
        beginPass(r, tc, ds->tdId ? rd__TargetRec(ds->tdId) : NULL, r->st.color, ds->tdId,
                  RHI_LOAD_LOAD, NULL, RHI_LOAD_LOAD, 0.0f);
    }
    const uint32_t fk[5] = {r->st.color, r->st.gsW, r->st.gsH, r->st.useOffset, r->passSerial};
    if (memcmp(fk, r->frameKey, sizeof(fk)) != 0) {
        /* XYOFFSET = (2048 - w/2, 2048 - h/2) (+ the preset's field offset
         * when useOffset; zero in Original, RENDER_API.md open item 4) */
        float ox = 2048.0f - (float)(r->st.gsW >> 1);
        float oy = 2048.0f - (float)(r->st.gsH >> 1);
        if (r->st.useOffset & RD_TARGET_HALF_Y) {
            oy += 0.5f; /* the flip's sceGsSetHalfOffset (R2c) */
        }
        r->frameBG = rd__FrameGroupZ(tc->w, tc->h, ox, oy, rd__TargetZScale(ds->tdId));
        memcpy(r->frameKey, fk, sizeof(fk));
    }
    RhiRect sc = {x0, y0, (uint32_t)(x1 - x0 + 1), (uint32_t)(y1 - y0 + 1)};
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

static void doScreen(Replay *r, const RdFrame *f, const RdCmd *c)
{
    DrawSetup ds;
    if (!prepareDraw(r, &ds)) {
        return;
    }

    /* geometry */
    const uint32_t n = c->u[1];
    const RdScreenVtx *v = (const RdScreenVtx *)(f->payload + c->u[0]);
    const uint64_t maxBytes = (uint64_t)n * 6 * sizeof(IcoSpriteVertex);
    const uint64_t vOff = rd__RingAlloc(maxBytes, 16);
    if (vOff == ~0ull) {
        return;
    }
    uint8_t topo;
    IcoSpriteVertex *out = (IcoSpriteVertex *)(g_rd.ringMap[s_slot] + vOff);
    const uint32_t nv = expand(v, n, c->b[0], c->b[2], (float)ds.tw, (float)ds.th, r->st.uvOffset,
                               out, &topo, r->st.gouraud != 0);
    if (nv == 0) {
        return;
    }
    RdDrawPass dp[2];
    const int np = rd__PlanScreenDraw(&r->st, topo, c->b[1], ds.tc->format, ds.depthFmt, dp);
    if (np == 0) {
        return;
    }
    RhiBindGroup g2 = bindDraw(r, &ds);
    if (!g2.id) {
        return;
    }
    rhi_CmdSetVertexBuffer(s_cl, 0, g_rd.ring[s_slot], vOff);
    for (int i = 0; i < np; i++) {
        RhiPipeline p = rd__GetPipeline(&dp[i].key);
        if (!p.id) {
            continue;
        }
        IcoDrawCB cb;
        fillDrawCB(r, &dp[i], &ds, &cb);
        rhi_CmdSetPipeline(s_cl, p);
        rhi_CmdSetBindGroup(s_cl, 0, r->frameBG);
        rhi_CmdSetBindGroup(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        rhi_CmdDraw(s_cl, nv, 0, 1);
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
    }
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

static RhiBindGroup vuGroup(uint64_t streamOff, uint64_t streamSize, const IcoDrawCB *dcb,
                            const IcoVuCB *vcb, uint64_t bonesOff)
{
    const uint64_t ua = rhi_Limits()->uniformAlign;
    const uint64_t dOff = ringCopy(dcb, sizeof(*dcb), ua);
    const uint64_t vOff = ringCopy(vcb, sizeof(*vcb), ua);
    if (dOff == ~0ull || vOff == ~0ull) {
        return (RhiBindGroup){0};
    }
    RhiBinding b[4];
    memset(b, 0, sizeof(b));
    b[0].slot = 0;
    b[0].type = RHI_BIND_STORAGE_BUFFER;
    b[0].buffer = g_rd.ring[s_slot];
    b[0].offset = streamOff;
    b[0].size = streamSize ? streamSize : 16;
    b[1].slot = 1;
    b[1].type = RHI_BIND_UNIFORM_BUFFER;
    b[1].buffer = g_rd.ring[s_slot];
    b[1].offset = dOff;
    b[1].size = sizeof(IcoDrawCB);
    b[2].slot = 2;
    b[2].type = RHI_BIND_UNIFORM_BUFFER;
    b[2].buffer = g_rd.ring[s_slot];
    b[2].offset = vOff;
    b[2].size = sizeof(IcoVuCB);
    b[3].slot = 3;
    b[3].type = RHI_BIND_UNIFORM_BUFFER;
    b[3].buffer = g_rd.ring[s_slot];
    b[3].offset = bonesOff;
    b[3].size = sizeof(IcoVuBoneCB);
    return rhi_CreateBindGroup(&(RhiBindGroupDesc){g_rd.layoutVu, b, 4});
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
                   uint8_t prog, uint8_t vs, uint64_t streamOff, uint64_t streamSize,
                   const IcoVuCB *vcb, uint64_t bonesOff, int indexed, uint32_t first,
                   uint32_t count)
{
    if (count == 0) {
        return;
    }
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
        RhiBindGroup g1 = vuGroup(streamOff, streamSize, &cb, vcb, bonesOff);
        if (!g1.id) {
            continue;
        }
        rhi_CmdSetPipeline(s_cl, p);
        rhi_CmdSetBindGroup(s_cl, 0, r->frameBG);
        rhi_CmdSetBindGroup(s_cl, 1, g1);
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
    if (m) {
        if (m->replaySeen != g_rd.replayCounter) {
            m->ringStream = ringCopy(m->stream, (uint64_t)m->vertexCount * m->qwPerVertex * 16, ua);
            m->ringIndex = ringCopy(m->index, (uint64_t)m->indexCount * 4, 16);
            m->replaySeen = g_rd.replayCounter;
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
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamOff, streamSize, &vcb, bonesOff, 1, 0, ni);
        return;
    }
    if (c->type == RDC_PARTICLES) {
        RhiBindGroup g2 = bindDraw(r, &ds);
        if (!g2.id) {
            return;
        }
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamOff, streamSize, &vcb, bonesOff, 0, 0,
               6 * p.vertsPerBatch);
        return;
    }
    /* static and skinned meshes */
    RhiBindGroup g2 = bindDraw(r, &ds);
    if (!g2.id) {
        return;
    }
    rhi_CmdSetIndexBuffer(s_cl, g_rd.ring[s_slot], indexOff, true);
    uint32_t last = p.firstBatch + p.batchCount;
    if (last > m->batchCount || p.batchCount == 0) {
        last = m->batchCount;
    }
    if (p.clip != RD_VU_CLIP_SCISSOR) {
        const RdVuBatchRec *b0 = &m->batches[p.firstBatch], *b1 = &m->batches[last - 1];
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamOff, streamSize, &vcb, bonesOff, 1,
               b0->firstIndex, b1->firstIndex + b1->indexCount - b0->firstIndex);
        return;
    }
    IcoVuCB cut = vcb, kick = vcb;
    cut.draw[2] |= ICO_VU_CUT_ONLY;
    kick.draw[2] |= ICO_VU_KICK_ONLY;
    RdStateBlock fan = r->st;
    fan.ds.abe = 1; /* the fans' PRIM is the common block's 0x5D: ABE on */
    for (uint32_t b = p.firstBatch; b < last; b++) {
        const RdVuBatchRec *br = &m->batches[b];
        vuDraw(r, &fan, &ds, g2, p.prog, vs, streamOff, streamSize, &cut, bonesOff, 1,
               br->firstIndex, br->indexCount);
        vuDraw(r, &r->st, &ds, g2, p.prog, vs, streamOff, streamSize, &kick, bonesOff, 1,
               br->firstIndex, br->indexCount);
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
    uint32_t w = dst->w, h = dst->h;
    if (src->w != w || src->h != h) {
        rd__LogOnce(RD_ONCE_EXACT_SIZE, "exact blend between targets of different sizes: the "
                                        "common top-left rectangle is blended");
        w = src->w < w ? src->w : w;
        h = src->h < h ? src->h : h;
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
        rhi_CmdSetBindGroup(s_cl, 0, rd__FrameGroup(w, h, 0.0f, 0.0f));
        rhi_CmdSetBindGroup(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2,
                            rhi_CreateBindGroup(&(RhiBindGroupDesc){g_rd.layoutInt, b, 2}));
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
    uint32_t w = cr.w, h = cr.h;
    if ((uint32_t)cr.srcX + w > src->w) {
        w = src->w - (uint32_t)cr.srcX;
    }
    if ((uint32_t)cr.dstX + w > dst->w) {
        w = dst->w - (uint32_t)cr.dstX;
    }
    if ((uint32_t)cr.srcY + h > src->h) {
        h = src->h - (uint32_t)cr.srcY;
    }
    if ((uint32_t)cr.dstY + h > dst->h) {
        h = dst->h - (uint32_t)cr.dstY;
    }
    if ((int32_t)w <= 0 || (int32_t)h <= 0) {
        return;
    }
    endPass(r);
    r->writeSerial++;
    rd__Transition(s_cl, src->color, &src->colorState, RHI_STATE_COPY_SRC);
    rd__Transition(s_cl, dst->color, &dst->colorState, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(s_cl, src->color, (RhiRect){cr.srcX, cr.srcY, w, h}, dst->color, cr.dstX,
                       cr.dstY);
}

/* ------------------------------------------------- shadows (wave 4, R4b)
 * rd_shadow.c says what the three commands stand for (RENDER_API.md
 * section 14).  All three work on the state block's colour target and the
 * depth-stencil of its depth target, which must have the colour's size
 * (shadow_Reset binds the per-frame count target with SCENE's). */

/* The colour and depth-stencil the state block names, of one size; NULL
 * (reported once) otherwise. */
static RdTargetRec *shadowTargets(Replay *r, RdTargetRec **depth)
{
    RdTargetRec *tc = rd__TargetRec(r->st.color);
    RdTargetRec *td = rd__TargetRec(r->st.depth);
    if (!tc || !tc->color.id || !td || !td->withDepth || !td->depth.id || tc->w != td->w ||
        tc->h != td->h) {
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
    p.width = tc->w;
    p.height = tc->h;
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
    RhiBindGroup g1 = rd__DrawGroup(&cb);
    uint32_t first = 0;
    for (int decr = 0; decr < 2; decr++) {
        if (count[decr] == 0) {
            continue;
        }
        const RdPipeKeyInt k = rd__ShadowVolumeKey(&r->st, ds.tc->format, decr);
        RhiPipeline p = rd__GetPipeline(&k);
        if (p.id) {
            rhi_CmdSetPipeline(s_cl, p);
            rhi_CmdSetBindGroup(s_cl, 0, r->frameBG);
            rhi_CmdSetBindGroup(s_cl, 1, g1);
            rhi_CmdSetBindGroup(s_cl, 2, g2);
            rhi_CmdSetStencilRef(s_cl, 0);
            rhi_CmdDraw(s_cl, count[decr], first, 1);
            g_rd.stats.draws++;
        }
        first += count[decr];
    }
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
    const RhiRect all = {0, 0, tc->w, tc->h};
    rhi_CmdSetScissor(s_cl, &all);
    RhiBindGroup g0 = rd__FrameGroup(tc->w, tc->h, 0.0f, 0.0f);
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
        rhi_CmdSetBindGroup(s_cl, 0, g0);
        rhi_CmdSetBindGroup(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        rhi_CmdSetStencilRef(s_cl, pass < 6 ? (uint8_t)(1u << pass) : 0);
        rhi_CmdDraw(s_cl, 3, 0, 1);
    }
    endPass(r);
}

/* --------------------------------------------------- fog (wave 4, R4c)
 * RD_POST_FOG (fog_DrawFog, ZFog.c; RENDER_API.md section 15).  The GS
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
    IcoSpriteVertex *out = (IcoSpriteVertex *)(g_rd.ringMap[s_slot] + vOff);
    const uint32_t nv = expand(v, 2, RD_PRIM_SPRITES, 1, (float)tz->w, (float)tz->h, noOff, out,
                               &topo, r->st.gouraud != 0);
    memcpy(g_rd.ringMap[s_slot] + lOff, f->payload + p.lutOffset, 256 * 4);

    endPass(r);
    /* the Z copy (the GS's BITBLT of the Z buffer to 0x2800) */
    if (!s_fogDepth.id || s_fogDepthW != tz->w || s_fogDepthH != tz->h) {
        if (s_fogDepth.id) {
            rhi_DestroyTexture(s_fogDepth);
        }
        s_fogDepth = rhi_CreateTexture(&(RhiTextureDesc){
            tz->w, tz->h, 1, RHI_FMT_D32F_S8,
            /* the depth-stencil usage: Vulkan's sampled depth layout
             * (DEPTH_STENCIL_READ_ONLY_OPTIMAL) requires it */
            RHI_TEX_SAMPLED | RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_DST, "rd fog depth"});
        s_fogDepthState = RHI_STATE_UNDEFINED;
        s_fogDepthW = tz->w;
        s_fogDepthH = tz->h;
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
    rhi_CmdCopyTexture(s_cl, tz->depth, (RhiRect){0, 0, tz->w, tz->h}, s_fogDepth, 0, 0);
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
    const RhiBindGroup g2 = rhi_CreateBindGroup(&(RhiBindGroupDesc){g_rd.layoutTex, b, 3});
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
        rhi_CmdSetPipeline(s_cl, pipe);
        rhi_CmdSetBindGroup(s_cl, 0, r->frameBG);
        rhi_CmdSetBindGroup(s_cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(s_cl, 2, g2);
        rhi_CmdDraw(s_cl, nv, 0, 1);
        g_rd.stats.draws++;
        if (dp[i].key.gs.colorMask & 8) {
            r->writeSerial++;
        }
    }
    (void)topo;
}

/* ----------------------------------------------------------------- frame */

static uint64_t estimateRing(const RdFrame *f, int keep)
{
    const uint64_t align = rhi_Limits()->uniformAlign;
    const uint64_t pitchA = rhi_Limits()->copyRowPitchAlign;
    uint64_t total = 64 * 1024;
    for (int l = rd__FirstList(keep); l < RD_LIST_COUNT; l++) {
        const RdCmdList *cl = &f->lists[l];
        for (uint32_t i = 0; i < cl->count; i++) {
            const RdCmd *c = &cl->cmds[i];
            total += 4 * align;
            if (c->type == RDC_SCREEN) {
                total += (uint64_t)c->u[1] * 6 * sizeof(IcoSpriteVertex) + 16;
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
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        const RdTexRec *t = &g_rd.textures[i];
        if (t->live && t->kind == RD_TEXKIND_IMAGE && t->dirty) {
            uint64_t pitch = ((uint64_t)t->w * 4 + pitchA - 1) / pitchA * pitchA;
            total += pitch * t->h + rhi_Limits()->copyOffsetAlign;
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
    for (uint32_t i = 0; i < RD_MAX_TEXTURES; i++) {
        RdTexRec *t = &g_rd.textures[i];
        if (!t->live || t->kind != RD_TEXKIND_IMAGE || !t->dirty) {
            continue;
        }
        if (!t->rhi.id) {
            t->rhi = rhi_CreateTexture(&(RhiTextureDesc){t->w, t->h, 1, RHI_FMT_RGBA8_UNORM,
                                                         RHI_TEX_SAMPLED | RHI_TEX_COPY_DST,
                                                         "rd texture"});
            t->state = RHI_STATE_UNDEFINED;
            if (!t->rhi.id) {
                continue;
            }
        }
        const uint32_t pitch = (t->w * 4 + pitchA - 1) / pitchA * pitchA;
        uint64_t off = rd__RingAlloc((uint64_t)pitch * t->h, offA);
        if (off == ~0ull) {
            continue;
        }
        for (uint32_t y = 0; y < t->h; y++) {
            memcpy(g_rd.ringMap[s_slot] + off + (uint64_t)y * pitch,
                   t->pixels + (size_t)y * t->w * 4, (size_t)t->w * 4);
        }
        rd__Transition(s_cl, t->rhi, &t->state, RHI_STATE_COPY_DST);
        rhi_CmdCopyBufferToTexture(s_cl, g_rd.ring[s_slot], off, pitch, t->rhi, 0,
                                   (RhiRect){0, 0, t->w, t->h});
        rd__Transition(s_cl, t->rhi, &t->state, RHI_STATE_SHADER_READ);
        t->dirty = 0;
        g_rd.stats.textureUploads++;
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

bool rd__ReplayFrame(const RdFrame *f, int keep, bool present)
{
    if (!g_rd.hasDevice || !f) {
        return false;
    }
    rhi_WaitFrame();
    s_slot = g_rd.replayCounter % RHI_FRAMES_IN_FLIGHT;
    g_rd.replayCounter++;
    s_ringOff = 0;
    if (!ensureRing(estimateRing(f, keep))) {
        rd__Log("could not allocate the upload ring");
        return false;
    }
    const bool doPresent = present && rd__PresentAcquire();
    s_cl = rhi_BeginCommands();
    if (!s_cl.id) {
        return false;
    }
    uploadTextures();

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
                endPass(&r);
                rd__NotImplemented(stubName(c->type));
                break;
            default:
                endPass(&r);
                rd__NotImplemented(stubName(c->type));
                break;
            }
        }
    }
    endPass(&r);
    if (doPresent) {
        rd__PresentRecord(s_cl);
    }
    rhi_EndCommands(s_cl);
    rhi_Submit(s_cl);
    if (doPresent) {
        rd__PresentFinish();
    }
    g_rd.stats.pipelines = rd__PipelineCount();
    return true;
}

/* ------------------------------------------------------------- readback */

static bool readTexture(RhiTexture t, RhiState *state, uint32_t w, uint32_t h, void *dst,
                        size_t dstSize)
{
    if (!t.id || dstSize < (size_t)w * h * 4) {
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
    if (!rhi_ReadbackTexture(t, RHI_ASPECT_COLOR, dst, dstSize, &pitch)) {
        return false;
    }
    if (pitch != w * 4) {
        uint8_t *p = dst;
        for (uint32_t y = 1; y < h; y++) {
            memmove(p + (size_t)y * w * 4, p + (size_t)y * pitch, (size_t)w * 4);
        }
    }
    return true;
}

bool rd__ReadTarget(RdTarget target, void *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    RdTargetRec *t = rd__TargetRec(target.id);
    if (!g_rd.hasDevice || !t || t->format == RHI_FMT_R8_UNORM) {
        return false;
    }
    if (w) {
        *w = t->w;
    }
    if (h) {
        *h = t->h;
    }
    return readTexture(t->color, &t->colorState, t->w, t->h, dst, dstSize);
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
    rhi_WaitFrame();
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
            rhi_CmdSetBindGroup(s_cl, 0, rd__FrameGroup(PW, PH, 0.0f, 0.0f));
            rhi_CmdSetBindGroup(s_cl, 1, rd__DrawGroup(&cb));
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
