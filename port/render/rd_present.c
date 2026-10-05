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
 * Presets (wave 2, R2c: hooks only).  RdPresentPreset holds everything a
 * preset may change at present time.  RD_PRESET_ORIGINAL is the only one
 * implemented; RD_PRESET_ENHANCED's entry carries the Enhanced fields
 * (interpolation, output aspect, mirror, full-height scene) set to their
 * Original values, and nothing reads them yet:
 *   interpolate   wave 7 (rd_interp.c): present several times per
 *                 simulation tick, blending the retained frames by
 *                 presentAlpha(); Original presents once per tick (inside
 *                 rd_EndFrame) and presentAlpha() is 1
 *   aspectFromSettings  widescreen (wave 6): the box takes RdSettings.aspect
 *                 instead of 4:3; the projection side is rd_frame.c
 *                 rd__FillCameraCB and GsBase.c gsbHostWideX
 *   mirror        mirror mode: step 2 flips x
 *   fullHeight    Enhanced "full-height scene": DISPLAY is the scene height
 *                 and step 1 does not double lines
 * Field parity is not a present-time effect: the PS2 shifts the scene's
 * XYOFFSET by half a line from the field bit (sceGsSetHalfOffset), which
 * rd_FrameFlip records into the frame head (RD_TARGET_HALF_Y).
 */
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

typedef struct RdPresentPreset {
    void (*box)(uint32_t outW, uint32_t outH, RhiRect *box);
    RdFilter doubleFilter; /* step 1 */
    RdFilter scaleFilter;  /* step 2 */
    int lineDouble;
    /* Enhanced hooks (unused; Original values in both entries) */
    int interpolate;
    int aspectFromSettings;
    int mirror;
    int fullHeight;
} RdPresentPreset;

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

static const RdPresentPreset s_presets[2] = {
    /* RD_PRESET_ORIGINAL */
    {box43, RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 0, 0, 0, 0},
    /* RD_PRESET_ENHANCED: the Original path until waves 6 and 7 */
    {box43, RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 0, 0, 0, 0},
};

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
        /* wave 7: blend rd__PrevFrame's DISPLAY here; unreachable until then */
    }
    RhiTexture out = s_window ? s_backbuffer : g_rd.presentOut;
    RhiState *outState = s_window ? &s_backbufferState : &g_rd.presentOutState;

    RhiTexture src = disp->color;
    uint32_t sw = disp->w, sh = disp->h;
    rd__Transition(cl, disp->color, &disp->colorState, RHI_STATE_SHADER_READ);
    if (pr->lineDouble) {
        const uint32_t lw = disp->w, lh = disp->h * 2;
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
        blit(cl, disp->color, disp->w, disp->h, g_rd.presentLines, RHI_FMT_RGBA8_UNORM, lw, lh,
             RHI_LOAD_DONT_CARE, &full, pr->doubleFilter);
        rd__Transition(cl, g_rd.presentLines, &g_rd.presentLinesState, RHI_STATE_SHADER_READ);
        src = g_rd.presentLines;
        sw = lw;
        sh = lh;
    }
    RhiRect box;
    pr->box(s_outW, s_outH, &box);
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
    g_rd.pendingSettings.outputWidth = width;
    g_rd.pendingSettings.outputHeight = height;
    if (g_rd.hasDevice && rhi_SwapchainFormat() != RHI_FMT_UNKNOWN) {
        rhi_ResizeSwapchain(width, height, g_rd.settings.vsync != 0);
    }
}
