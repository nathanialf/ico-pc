/* rd_present.c: DISPLAY to the output.
 *
 * Original preset (RENDER_API.md section 6): the reduced DISPLAY frame
 * (512 x H/2) is shown in a centred 4:3 rectangle of the output, each line
 * doubled (field-line doubling, nearest vertically) and filtered bilinearly
 * horizontally.  Two blits:
 *   1. DISPLAY -> lines target (512 x H), nearest: exact line doubling;
 *   2. lines target -> the 4:3 box of the output, bilinear, black outside.
 * The output is the swapchain backbuffer with a window, or a headless RGBA8
 * texture of RdSettings.outputWidth x outputHeight (tests, the replay tool;
 * none when either is 0).
 *
 * Presets (wave 2, R2c: hooks; wave 7, R7a: the display options).
 * RdPresentPreset holds everything a preset may change at present time:
 *   interpolate   R7b (rd_interp.c): present several times per simulation
 *                 tick, blending the retained frames by presentAlpha();
 *                 Original presents once per tick (inside rd_EndFrame) and
 *                 presentAlpha() is 1
 *   aspectFromSettings  R7a: the box takes the aspect option (g_rd.outAspect)
 *                 instead of 4:3; the projection side is rd_frame.c
 *                 rd__FillCameraCB, the replay's wide x scale and GsBase.c
 *                 gsbHostWideX
 *   mirror        mirror mode: step 2 flips x (not implemented)
 *   fullHeight    R7a: with the full-height option DISPLAY's texture has the
 *                 scene's height (rd__TargetScaleOf) and step 1 is skipped
 * The scene resolution needs nothing here: DISPLAY's texture is whatever
 * size rd__ApplyDisplay gave it, and both steps sample it normalised.
 * rd__ApplyDisplay (below) turns RdSettings into the scales and factors the
 * targets and the replay use (RENDER_API.md section 19).
 * Field parity is not a present-time effect: the PS2 shifts the scene's
 * XYOFFSET by half a line from the field bit (sceGsSetHalfOffset), which
 * rd_FrameFlip records into the frame head (RD_TARGET_HALF_Y).
 */
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

typedef struct RdPresentPreset {
    RdFilter doubleFilter; /* step 1 */
    RdFilter scaleFilter;  /* step 2 */
    int lineDouble;
    /* Enhanced fields */
    int interpolate; /* R7b */
    int aspectFromSettings;
    int mirror; /* not implemented */
    int fullHeight;
} RdPresentPreset;

#define RD_ASPECT_43 (4.0f / 3.0f)
#define RD_ASPECT_169 (16.0f / 9.0f)

void rd__PresentBox(uint32_t outW, uint32_t outH, float aspect, RhiRect *box)
{
    uint32_t h = outH, w;
    if (!(aspect > RD_ASPECT_43 + 1e-4f)) {
        /* 4:3, the Original box (integer arithmetic, as before R7a) */
        w = (outH * 4 + 1) / 3;
        if (w > outW) {
            w = outW;
            h = (outW * 3 + 2) / 4;
        }
    } else {
        w = (uint32_t)((float)outH * aspect + 0.5f);
        if (w > outW) {
            w = outW;
            h = (uint32_t)((float)outW / aspect + 0.5f);
        }
    }
    if (w > outW) {
        w = outW;
    }
    if (h > outH) {
        h = outH;
    }
    box->x = (int32_t)((outW - w) / 2);
    box->y = (int32_t)((outH - h) / 2);
    box->w = w ? w : 1;
    box->h = h ? h : 1;
}

static const RdPresentPreset s_presets[2] = {
    /* RD_PRESET_ORIGINAL */
    {RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 0, 0, 0, 0},
    /* RD_PRESET_ENHANCED: the aspect and full-height options (R7a) */
    {RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 0, 1, 0, 1},
};

static float clampAspect(float a)
{
    if (!(a > RD_ASPECT_43 + 1e-4f)) {
        return RD_ASPECT_43;
    }
    return a > RD_ASPECT_169 ? RD_ASPECT_169 : a;
}

bool rd__ApplyDisplay(void)
{
    const RdSettings *st = &g_rd.settings;
    float sx = 1.0f, sy = 1.0f, work = 1.0f, wide = 1.0f, aspect = RD_ASPECT_43;
    uint8_t filter = 0, full = 0;
    if (st->preset == RD_PRESET_ENHANCED) {
        aspect = clampAspect(st->aspect);
        wide = RD_ASPECT_43 / aspect;
        const float gw = (float)g_rd.gsW, gh = (float)g_rd.gsH;
        float w, h;
        if (st->sceneScale > 0.0f) {
            h = gh * st->sceneScale;
            w = gw * st->sceneScale * aspect / RD_ASPECT_43;
        } else if (st->sceneWidth && st->sceneHeight) {
            w = (float)st->sceneWidth;
            h = (float)st->sceneHeight;
        } else if (st->outputWidth && st->outputHeight) {
            RhiRect b;
            rd__PresentBox(st->outputWidth, st->outputHeight, aspect, &b);
            w = (float)b.w;
            h = (float)b.h;
        } else {
            w = gw;
            h = gh;
        }
        /* from the GS size up to 4K (3840 x 2160) */
        if (w > 3840.0f) {
            h *= 3840.0f / w;
            w = 3840.0f;
        }
        if (h > 2160.0f) {
            w *= 2160.0f / h;
            h = 2160.0f;
        }
        sx = w / gw < 1.0f ? 1.0f : w / gw;
        sy = h / gh < 1.0f ? 1.0f : h / gh;
        work = rd_WorkTargetScale(RD_PRESET_ENHANCED, (uint32_t)(sy * 448.0f + 0.5f));
        filter = st->filterUpgrade <= RD_FILTER_UPGRADE_ANISOTROPIC ? st->filterUpgrade : 0;
        if (g_rd.hasDevice && !rhi_Limits()->textureMips) {
            filter = 0;
        }
        full = st->fullHeightScene != 0;
    }
    const bool changed = sx != g_rd.sceneSx || sy != g_rd.sceneSy || work != g_rd.workScale ||
                         full != g_rd.fullHeight;
    g_rd.sceneSx = sx;
    g_rd.sceneSy = sy;
    g_rd.workScale = work;
    g_rd.wideX = wide;
    g_rd.outAspect = aspect;
    g_rd.filterUpgrade = filter;
    g_rd.fullHeight = full;
    /* vsync: the swapchain's present mode (Vulkan FIFO, else MAILBOX or
     * IMMEDIATE; D3D12 the sync interval) */
    const uint32_t vs = st->vsync ? 2u : 1u;
    if (g_rd.vsyncApplied && g_rd.vsyncApplied != vs && g_rd.hasDevice &&
        rhi_SwapchainFormat() != RHI_FMT_UNKNOWN && st->outputWidth && st->outputHeight) {
        rhi_WaitIdle();
        rhi_ResizeSwapchain(st->outputWidth, st->outputHeight, st->vsync != 0);
    }
    g_rd.vsyncApplied = vs;
    return changed;
}

/* The interpolation hook (wave 7): the weight of the newest retained frame
 * at this present.  Original presents each frame once, whole. */
static float presentAlpha(const RdPresentPreset *pr)
{
    (void)pr;
    return 1.0f;
}

static RhiTexture s_backbuffer;

static RhiState s_backbufferState;

static RhiFormat s_outFormat;

static uint32_t s_outW, s_outH;

static bool s_window;

bool rd__PresentAcquire(void)
{
    s_backbuffer = (RhiTexture){0};
    s_window = rhi_SwapchainFormat() != RHI_FMT_UNKNOWN;
    if (s_window) {
        s_backbuffer = rhi_AcquireBackbuffer();
        if (!s_backbuffer.id) {
            rhi_ResizeSwapchain(g_rd.settings.outputWidth, g_rd.settings.outputHeight,
                                g_rd.settings.vsync != 0);
            s_backbuffer = rhi_AcquireBackbuffer();
        }
        if (!s_backbuffer.id) {
            return false;
        }
        s_backbufferState = RHI_STATE_UNDEFINED;
        s_outFormat = rhi_SwapchainFormat();
        s_outW = g_rd.settings.outputWidth;
        s_outH = g_rd.settings.outputHeight;
        return s_outW && s_outH;
    }
    const uint32_t w = g_rd.settings.outputWidth, h = g_rd.settings.outputHeight;
    if (!w || !h) {
        return false;
    }
    if (!g_rd.presentOut.id || g_rd.presentOutW != w || g_rd.presentOutH != h) {
        if (g_rd.presentOut.id) {
            rhi_DestroyTexture(g_rd.presentOut);
        }
        g_rd.presentOut = rhi_CreateTexture(&(RhiTextureDesc){
            w, h, 1, RHI_FMT_RGBA8_UNORM,
            RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC, "rd present out"});
        g_rd.presentOutState = RHI_STATE_UNDEFINED;
        g_rd.presentOutW = w;
        g_rd.presentOutH = h;
    }
    s_outFormat = RHI_FMT_RGBA8_UNORM;
    s_outW = w;
    s_outH = h;
    return g_rd.presentOut.id != 0;
}

static void blit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                 RhiFormat dstFmt, uint32_t dw, uint32_t dh, RhiLoadOp load, const RhiRect *box,
                 RdFilter filter)
{
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = load;
    p.colorCount = 1;
    p.width = dw;
    p.height = dh;
    rhi_CmdBeginRenderPass(cl, &p);
    RhiViewport vp = {(float)box->x, (float)box->y, (float)box->w, (float)box->h, 0.0f, 1.0f};
    rhi_CmdSetViewport(cl, &vp);
    rhi_CmdSetScissor(cl, box);
    RdPipeKeyInt k = rd__PostKey(RD_VS_BLIT, RD_FS_BLIT, dstFmt);
    RhiPipeline pipe = rd__GetPipeline(&k);
    if (pipe.id) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        for (int i = 0; i < 4; i++) {
            cb.col[i] = 0x80; /* modulate by 1.0: identity */
        }
        cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
        cb.uvRect[2] = (float)sw;
        cb.uvRect[3] = (float)sh;
        cb.tex[0] = (float)sw;
        cb.tex[1] = (float)sh;
        cb.tex[2] = 1.0f / (float)sw;
        cb.tex[3] = 1.0f / (float)sh;
        rhi_CmdSetPipeline(cl, pipe);
        rhi_CmdSetBindGroup(cl, 0, rd__FrameGroup(dw, dh, 0.0f, 0.0f));
        rhi_CmdSetBindGroup(cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(
            cl, 2, rd__TexGroup(src, rd__Sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        rhi_CmdDraw(cl, 3, 0, 1);
    }
    rhi_CmdEndRenderPass(cl);
}

void rd__PresentRecord(RhiCommandList cl)
{
    RdTargetRec *disp = rd__TargetRec(RD_TARGET_DISPLAY + 1);
    if (!disp || !disp->color.id) {
        return;
    }
    const RdPresentPreset *pr = &s_presets[g_rd.settings.preset == RD_PRESET_ENHANCED ? 1 : 0];
    if (pr->interpolate && presentAlpha(pr) < 1.0f) {
        /* R7b: blend rd__PrevFrame's DISPLAY here; unreachable until then */
    }
    RhiTexture out = s_window ? s_backbuffer : g_rd.presentOut;
    RhiState *outState = s_window ? &s_backbufferState : &g_rd.presentOutState;

    RhiTexture src = disp->color;
    uint32_t sw = disp->tw, sh = disp->th;
    rd__Transition(cl, disp->color, &disp->colorState, RHI_STATE_SHADER_READ);
    /* the full-height scene: DISPLAY already has every line */
    if (pr->lineDouble && !(pr->fullHeight && g_rd.fullHeight)) {
        const uint32_t lw = disp->tw, lh = disp->th * 2;
        if (!g_rd.presentLines.id || g_rd.presentLinesW != lw || g_rd.presentLinesH != lh) {
            if (g_rd.presentLines.id) {
                rhi_DestroyTexture(g_rd.presentLines);
            }
            g_rd.presentLines = rhi_CreateTexture(
                &(RhiTextureDesc){lw, lh, 1, RHI_FMT_RGBA8_UNORM,
                                  RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED, "rd present lines"});
            g_rd.presentLinesState = RHI_STATE_UNDEFINED;
            g_rd.presentLinesW = lw;
            g_rd.presentLinesH = lh;
        }
        rd__Transition(cl, g_rd.presentLines, &g_rd.presentLinesState, RHI_STATE_RENDER_TARGET);
        const RhiRect full = {0, 0, lw, lh};
        blit(cl, disp->color, disp->tw, disp->th, g_rd.presentLines, RHI_FMT_RGBA8_UNORM, lw, lh,
             RHI_LOAD_DONT_CARE, &full, pr->doubleFilter);
        rd__Transition(cl, g_rd.presentLines, &g_rd.presentLinesState, RHI_STATE_SHADER_READ);
        src = g_rd.presentLines;
        sw = lw;
        sh = lh;
    }
    RhiRect box;
    rd__PresentBox(s_outW, s_outH, pr->aspectFromSettings ? g_rd.outAspect : RD_ASPECT_43, &box);
    rd__Transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    blit(cl, src, sw, sh, out, s_outFormat, s_outW, s_outH, RHI_LOAD_CLEAR, &box, pr->scaleFilter);
    if (s_window) {
        rd__Transition(cl, out, outState, RHI_STATE_PRESENT);
    }
}

void rd__PresentFinish(void)
{
    if (s_window && s_backbuffer.id) {
        rhi_Present();
    }
}

void rd__PresentShutdown(void)
{
    if (g_rd.presentLines.id) {
        rhi_DestroyTexture(g_rd.presentLines);
    }
    if (g_rd.presentOut.id) {
        rhi_DestroyTexture(g_rd.presentOut);
    }
    g_rd.presentLines = g_rd.presentOut = (RhiTexture){0};
}

void rd_ResizeOutput(uint32_t width, uint32_t height)
{
    if (!g_rd.inited || width == 0 || height == 0) {
        return;
    }
    g_rd.settings.outputWidth = width;
    g_rd.settings.outputHeight = height;
    if (!g_rd.settingsPending) {
        g_rd.pendingSettings = g_rd.settings;
    }
    g_rd.pendingSettings.outputWidth = width;
    g_rd.pendingSettings.outputHeight = height;
    if (g_rd.hasDevice && rhi_SwapchainFormat() != RHI_FMT_UNKNOWN) {
        rhi_ResizeSwapchain(width, height, g_rd.settings.vsync != 0);
    }
    /* R7a: an Enhanced scene sized by the window follows it at the next
     * rd_BeginFrame (rd__ApplyDisplay recreates the targets if their scale
     * changed; Original never changes) */
    g_rd.settingsPending = true;
}
