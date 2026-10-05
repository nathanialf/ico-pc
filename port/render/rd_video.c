/* rd_video.c: the FMV picture on the output (Phase 4E; port/fmv/rd_video.h,
 * docs/port/FMV.md).
 *
 * The PS2 movie player did not draw through the game's display: it set up
 * its own 720-wide interlaced display environment and sent each picture
 * from the IPU straight into it (ito/mpeg/mv_disp.c).  The port's DISPLAY
 * target is the game's reduced 512 x H/2 frame, which would halve the
 * picture's resolution, so the FMV path goes where the PS2's did, to the
 * output: one pass draws the picture into the presenter's 4:3 box
 * (pillarboxed in a wider window, letterboxed in a taller one, the rest
 * black), converting the decoder's 4:2:0 planes with the IPU's CSC in
 * yuv.hlsl, then presents.  DISPLAY and the game's frames are not touched.
 *
 * Layout: the box is the PS2 display area (rd_VideoSetDisplay, 720 x 576 or
 * 480); a w x h picture sits in it at ((dispW - w) / 2, (dispH - h) / 2),
 * the offsets mv_videodec.c's dispSetTags used, scaled with the area.
 *
 * GPU objects: its own two shaders and pipeline (one per output format)
 * over rd's bind group layouts (group 0 FrameCB, group 1 DrawCB, group 2
 * t1/s1/t2), one R8 plane texture, and per frame in flight an upload buffer
 * for the planes and the two uniform blocks.  Each call waits for its frame
 * slot (rhi_WaitFrame), records, submits and presents on its own, as
 * rd_EndFrame does for a game frame; it runs on the simulation thread
 * between game frames (the scheduler draws none while a movie plays).
 */
#include <string.h>
#include "../fmv/rd_video.h"
#include "rd_internal.h"
#include "shader_consts.h"
#include "shaders_gen.h"

static struct {
    bool ready;
    RhiShader vs, fs;
    RhiPipeline pipe[2]; /* by output: [0] swapchain format, [1] RGBA8 */
    RhiFormat pipeFmt[2];
    RhiTexture planes;
    RhiState planesState;
    uint32_t planesW, planesH;
    RhiBuffer upload[RHI_FRAMES_IN_FLIGHT];
    uint8_t *uploadMap[RHI_FRAMES_IN_FLIGHT];
    uint64_t uploadCap[RHI_FRAMES_IN_FLIGHT];
    uint32_t counter;
    uint32_t dispW, dispH;
    int mirror;
    float clear[4];
} s_v = {.dispW = 720, .dispH = 576, .mirror = 1, .clear = {0.0f, 0.0f, 0.0f, 1.0f}};

void rd_VideoSetDisplay(uint32_t dispW, uint32_t dispH)
{
    s_v.dispW = dispW ? dispW : 720;
    s_v.dispH = dispH ? dispH : 576;
}

void rd_VideoSetMirror(int on)
{
    s_v.mirror = on != 0;
}

static RhiShader makeShader(const char *name)
{
    const IcoShaderBlob *b = ico_FindShader(name);
    RhiShaderDesc d;

    if (!b) {
        rd__Log("rd_video: shader %s missing from the table", name);
        return (RhiShader){0};
    }
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

static bool ensureInit(void)
{
    if (!g_rd.inited || !g_rd.hasDevice) {
        return false;
    }
    if (!s_v.ready) {
        s_v.vs = makeShader("yuv_vs");
        s_v.fs = makeShader("yuv_ps");
        s_v.ready = s_v.vs.id && s_v.fs.id;
    }
    return s_v.ready;
}

static RhiPipeline pipelineFor(RhiFormat fmt)
{
    for (int i = 0; i < 2; i++) {
        if (s_v.pipe[i].id && s_v.pipeFmt[i] == fmt) {
            return s_v.pipe[i];
        }
    }
    int slot = s_v.pipe[0].id ? 1 : 0;
    if (s_v.pipe[slot].id) {
        rhi_DestroyPipeline(s_v.pipe[slot]);
    }
    RhiBindGroupLayout layouts[3] = {g_rd.layoutFrame, g_rd.layoutDraw, g_rd.layoutTex};
    RhiPipelineDesc d;
    memset(&d, 0, sizeof(d));
    d.vertex = s_v.vs;
    d.fragment = s_v.fs;
    d.layouts = layouts;
    d.layoutCount = 3;
    d.topology = RHI_TOPO_TRIANGLE_LIST;
    d.cullNone = true;
    d.blend[0].writeMask = 0xF;
    d.colorFormats[0] = fmt;
    d.colorCount = 1;
    d.depthFormat = RHI_FMT_UNKNOWN;
    d.debugName = "rd video";
    s_v.pipe[slot] = rhi_CreatePipeline(&d);
    s_v.pipeFmt[slot] = fmt;
    return s_v.pipe[slot];
}

static bool ensureUpload(int slot, uint64_t need)
{
    if (s_v.uploadCap[slot] >= need) {
        return true;
    }
    if (s_v.upload[slot].id) {
        rhi_DestroyBuffer(s_v.upload[slot]);
    }
    uint64_t cap = 1u << 20;
    while (cap < need) {
        cap *= 2;
    }
    s_v.upload[slot] = rhi_CreateBuffer(&(RhiBufferDesc){cap, RHI_BUF_UNIFORM | RHI_BUF_COPY_SRC,
                                                         RHI_MEM_UPLOAD, "rd video upload"});
    s_v.uploadMap[slot] = s_v.upload[slot].id ? rhi_MapBuffer(s_v.upload[slot]) : NULL;
    s_v.uploadCap[slot] = s_v.uploadMap[slot] ? cap : 0;
    return s_v.uploadMap[slot] != NULL;
}

static RhiBindGroup uniformGroup(RhiBindGroupLayout layout, uint32_t bindSlot, RhiBuffer buf,
                                 uint64_t off, uint32_t size)
{
    RhiBinding b;
    memset(&b, 0, sizeof(b));
    b.slot = bindSlot;
    b.type = RHI_BIND_UNIFORM_BUFFER;
    b.buffer = buf;
    b.offset = off;
    b.size = size;
    return rhi_CreateBindGroup(&(RhiBindGroupDesc){layout, &b, 1});
}

/* The presenter's 4:3 box (rd_present.c box43). */
static void box43(uint32_t outW, uint32_t outH, RhiRect *box)
{
    uint32_t h = outH, w = (outH * 4 + 1) / 3;
    if (w > outW) {
        w = outW;
        h = (outW * 3 + 2) / 4;
    }
    box->x = (int32_t)((outW - w) / 2);
    box->y = (int32_t)((outH - h) / 2);
    box->w = w ? w : 1;
    box->h = h ? h : 1;
}

/* The output: the swapchain's next image, or rd's headless present target
 * (tests).  On success *tex, *state and the size are set. */
typedef struct VideoOut {
    RhiTexture tex;
    RhiState *state;
    RhiState localState;
    RhiFormat fmt;
    uint32_t w, h;
    bool window;
} VideoOut;

static bool acquireOut(VideoOut *o)
{
    memset(o, 0, sizeof(*o));
    o->window = rhi_SwapchainFormat() != RHI_FMT_UNKNOWN;
    if (o->window) {
        o->tex = rhi_AcquireBackbuffer();
        if (!o->tex.id) {
            rhi_ResizeSwapchain(g_rd.settings.outputWidth, g_rd.settings.outputHeight,
                                g_rd.settings.vsync != 0);
            o->tex = rhi_AcquireBackbuffer();
        }
        o->localState = RHI_STATE_UNDEFINED;
        o->state = &o->localState;
        o->fmt = rhi_SwapchainFormat();
        o->w = g_rd.settings.outputWidth;
        o->h = g_rd.settings.outputHeight;
        return o->tex.id && o->w && o->h;
    }
    if (!rd__PresentAcquire()) {
        return false;
    }
    o->tex = g_rd.presentOut;
    o->state = &g_rd.presentOutState;
    o->fmt = RHI_FMT_RGBA8_UNORM;
    o->w = g_rd.presentOutW;
    o->h = g_rd.presentOutH;
    return o->tex.id != 0;
}

/* One present: the picture (planes != NULL) or the clear colour alone. */
static int presentVideo(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                        const uint32_t pitch[3], uint32_t w, uint32_t h, const uint8_t rgba[4])
{
    if (!ensureInit()) {
        return -1;
    }
    rhi_WaitFrame();
    const int slot = (int)(s_v.counter++ % RHI_FRAMES_IN_FLIGHT);
    const RhiLimits *lim = rhi_Limits();
    const uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    const uint32_t tw = 2 * cw, th = h + ch;
    const uint32_t pitchA = lim->copyRowPitchAlign ? lim->copyRowPitchAlign : 1;
    const uint32_t rowPitch = (tw + pitchA - 1) / pitchA * pitchA;
    const uint64_t ua = lim->uniformAlign ? lim->uniformAlign : 256;
    const uint64_t offA = lim->copyOffsetAlign ? lim->copyOffsetAlign : 4;
    const uint64_t cbOff = 0;
    const uint64_t dcOff = (sizeof(IcoFrameCB) + ua - 1) / ua * ua;
    const uint64_t texOff =
        ((dcOff + sizeof(IcoDrawCB) + ua - 1) / ua * ua + offA - 1) / offA * offA;
    const bool picture = y != NULL && w > 0 && h > 0;

    if (!ensureUpload(slot, texOff + (picture ? (uint64_t)rowPitch * th : 0))) {
        return -1;
    }
    VideoOut out;
    if (!acquireOut(&out)) {
        return -1;
    }
    uint8_t *map = s_v.uploadMap[slot];
    RhiBuffer buf = s_v.upload[slot];

    /* the planes into one R8 image: Y on top, Cb | Cr below */
    if (picture) {
        if (!s_v.planes.id || s_v.planesW != tw || s_v.planesH != th) {
            if (s_v.planes.id) {
                rhi_DestroyTexture(s_v.planes);
            }
            s_v.planes = rhi_CreateTexture(&(RhiTextureDesc){tw, th, 1, RHI_FMT_R8_UNORM,
                                                             RHI_TEX_SAMPLED | RHI_TEX_COPY_DST,
                                                             "rd video planes"});
            s_v.planesState = RHI_STATE_UNDEFINED;
            s_v.planesW = tw;
            s_v.planesH = th;
            if (!s_v.planes.id) {
                return -1;
            }
        }
        uint8_t *dst = map + texOff;
        for (uint32_t r = 0; r < h; r++) {
            memcpy(dst + (size_t)r * rowPitch, y + (size_t)r * pitch[0], w);
            if (tw > w) {
                dst[(size_t)r * rowPitch + w] = dst[(size_t)r * rowPitch + w - 1];
            }
        }
        for (uint32_t r = 0; r < ch; r++) {
            uint8_t *row = dst + (size_t)(h + r) * rowPitch;
            memcpy(row, u + (size_t)r * pitch[1], cw);
            memcpy(row + cw, v + (size_t)r * pitch[2], cw);
        }
    }

    /* group 0: a FrameCB (unused by yuv.hlsl, bound for the layout) */
    IcoFrameCB fcb;
    memset(&fcb, 0, sizeof(fcb));
    fcb.target[0] = (float)out.w;
    fcb.target[1] = (float)out.h;
    memcpy(map + cbOff, &fcb, sizeof(fcb));
    IcoDrawCB dcb;
    memset(&dcb, 0, sizeof(dcb));
    dcb.tex[0] = (float)w;
    dcb.tex[1] = (float)h;
    dcb.mode[0] = cw;
    dcb.mode[1] = h;
    dcb.param[0] = (g_rd.settings.mirror && s_v.mirror) ? 1.0f : 0.0f;
    memcpy(map + dcOff, &dcb, sizeof(dcb));

    RhiCommandList cl = rhi_BeginCommands();
    if (!cl.id) {
        return -1;
    }
    if (picture) {
        rd__Transition(cl, s_v.planes, &s_v.planesState, RHI_STATE_COPY_DST);
        rhi_CmdCopyBufferToTexture(cl, buf, texOff, rowPitch, s_v.planes, 0,
                                   (RhiRect){0, 0, tw, th});
        rd__Transition(cl, s_v.planes, &s_v.planesState, RHI_STATE_SHADER_READ);
    }
    rd__Transition(cl, out.tex, out.state, RHI_STATE_RENDER_TARGET);

    RhiRect box;
    box43(out.w, out.h, &box);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = out.tex;
    p.color[0].load = RHI_LOAD_CLEAR;
    if (rgba != NULL) {
        /* dispClear's colour: what the PS2 showed around the picture (the
           whole screen there; the whole output here, bars included) */
        for (int i = 0; i < 3; i++) {
            s_v.clear[i] = (float)rgba[i] / 255.0f;
        }
    }
    memcpy(p.color[0].clear, s_v.clear, sizeof(s_v.clear));
    p.colorCount = 1;
    p.width = out.w;
    p.height = out.h;
    rhi_CmdBeginRenderPass(cl, &p);
    if (picture) {
        /* the picture's rectangle in the box, from its place in the PS2
           display area (integer offsets as mv_videodec.c computed them) */
        const int32_t ox = ((int32_t)s_v.dispW - (int32_t)w) >> 1;
        const int32_t oy = ((int32_t)s_v.dispH - (int32_t)h) >> 1;
        const float sx = (float)box.w / (float)s_v.dispW, sy = (float)box.h / (float)s_v.dispH;
        RhiViewport vp = {(float)box.x + (float)ox * sx,
                          (float)box.y + (float)oy * sy,
                          (float)w * sx,
                          (float)h * sy,
                          0.0f,
                          1.0f};
        if (dcb.param[0] != 0.0f) {
            vp.x = (float)box.x + (float)box.w - (float)ox * sx - vp.w; /* the rectangle mirrored */
        }
        RhiRect sc = box;
        rhi_CmdSetViewport(cl, &vp);
        rhi_CmdSetScissor(cl, &sc);
        RhiPipeline pipe = pipelineFor(out.fmt);
        if (pipe.id) {
            RhiBinding tb[3];
            memset(tb, 0, sizeof(tb));
            tb[0].slot = 1;
            tb[0].type = RHI_BIND_SAMPLED_TEXTURE;
            tb[0].texture = s_v.planes;
            tb[1].slot = 1;
            tb[1].type = RHI_BIND_SAMPLER;
            tb[1].sampler =
                rd__Sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
            tb[2].slot = 2;
            tb[2].type = RHI_BIND_SAMPLED_TEXTURE;
            tb[2].texture = g_rd.dummy;
            rhi_CmdSetPipeline(cl, pipe);
            rhi_CmdSetBindGroup(
                cl, 0, uniformGroup(g_rd.layoutFrame, 0, buf, cbOff, (uint32_t)sizeof(IcoFrameCB)));
            rhi_CmdSetBindGroup(
                cl, 1, uniformGroup(g_rd.layoutDraw, 1, buf, dcOff, (uint32_t)sizeof(IcoDrawCB)));
            rhi_CmdSetBindGroup(cl, 2,
                                rhi_CreateBindGroup(&(RhiBindGroupDesc){g_rd.layoutTex, tb, 3}));
            rhi_CmdDraw(cl, 3, 0, 1);
        }
    }
    rhi_CmdEndRenderPass(cl);
    if (out.window) {
        rd__Transition(cl, out.tex, out.state, RHI_STATE_PRESENT);
    }
    rhi_EndCommands(cl);
    rhi_Submit(cl);
    if (out.window) {
        rhi_Present();
    }
    return 0;
}

int rd_VideoFrame(const uint8_t *y, const uint8_t *u, const uint8_t *v, const uint32_t pitch[3],
                  uint32_t w, uint32_t h)
{
    if (!y || !u || !v || !pitch || w == 0 || h == 0) {
        return -1;
    }
    return presentVideo(y, u, v, pitch, w, h, NULL);
}

int rd_VideoClear(const uint8_t rgba[4])
{
    return presentVideo(NULL, NULL, NULL, NULL, 0, 0, rgba);
}

void rd_VideoShutdown(void)
{
    if (!g_rd.hasDevice) {
        memset(&s_v.pipe, 0, sizeof(s_v.pipe));
        s_v.ready = false;
        return;
    }
    rhi_WaitIdle();
    for (int i = 0; i < 2; i++) {
        if (s_v.pipe[i].id) {
            rhi_DestroyPipeline(s_v.pipe[i]);
        }
        s_v.pipe[i] = (RhiPipeline){0};
    }
    for (int i = 0; i < RHI_FRAMES_IN_FLIGHT; i++) {
        if (s_v.upload[i].id) {
            rhi_DestroyBuffer(s_v.upload[i]);
        }
        s_v.upload[i] = (RhiBuffer){0};
        s_v.uploadMap[i] = NULL;
        s_v.uploadCap[i] = 0;
    }
    if (s_v.planes.id) {
        rhi_DestroyTexture(s_v.planes);
    }
    s_v.planes = (RhiTexture){0};
    s_v.planesW = s_v.planesH = 0;
    if (s_v.vs.id) {
        rhi_DestroyShader(s_v.vs);
    }
    if (s_v.fs.id) {
        rhi_DestroyShader(s_v.fs);
    }
    s_v.vs = s_v.fs = (RhiShader){0};
    s_v.ready = false;
}
