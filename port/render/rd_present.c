/* rd_present.c: DISPLAY to the output.
 *
 * Original preset (RENDER_API.md "Presets and display options"): the reduced DISPLAY frame
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
 * RdPresentPreset holds everything a preset may change at present time (the
 * Enhanced preset's interpolation, R7b rd_interp.c, is not one: it presents
 * several times per tick through rd_Present with RdSettings.interpolate, each
 * present replaying the frame blended from the retained pair, and this step is
 * the same either way: it shows whatever DISPLAY the replay left):
 *   aspectFromSettings  R7a: the box takes the aspect option (g_rd.outAspect)
 *                 instead of 4:3; the projection side is rd_frame.c
 *                 rd__FillCameraCB, the replay's wide x scale and GsBase.c
 *                 gsbHostWideX
 *   mirror        R7c: step 2 may flip x (both presets: the mirror mode
 *                 is a gameplay option, not a display one); flipped when
 *                 rd__MirrorOn (rd.h rd_SetMirror, RdSettings.mirror).  Every
 *                 present goes through here, the interpolated ones (R7b
 *                 rd_Present) included; UI prims were flipped at replay
 *                 (rd_replay.c mirrorUi) so they read normally
 *   fullHeight    R7a: with the full-height option DISPLAY's texture has the
 *                 scene's height (rd__TargetScaleOf) and step 1 is skipped
 * The scene resolution needs nothing here: DISPLAY's texture is whatever
 * size rd__ApplyDisplay gave it, and both steps sample it normalised.
 * rd__ApplyDisplay (below) turns RdSettings into the scales and factors the
 * targets and the replay use (RENDER_API.md "Presets and display options").
 * Field parity is not a present-time effect: the PS2 shifts the scene's
 * XYOFFSET by half a line from the field bit (sceGsSetHalfOffset), which
 * rd_FrameFlip records into the frame head (RD_TARGET_HALF_Y).
 *
 * The overlay (package OV, rd.h rd_SetPresentOverlay; RENDER_API.md "The
 * presentation overlay"): after step 2, the prims the registered callback
 * gave for this present are drawn on the output in a load-preserving pass,
 * one 12.4 unit a sixteenth of an output pixel, unflipped.  The callback
 * runs before the frame's replay (rd__OverlayCollect, from replayFrame) so
 * the textures it touches upload with the frame; only the drawing is here.
 * With no callback registered nothing below step 2 runs, and the output is
 * byte for byte what it was before the overlay existed.
 */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

typedef struct RdPresentPreset {
    RdFilter doubleFilter; /* step 1 */
    RdFilter scaleFilter;  /* step 2 */
    int lineDouble;
    /* Enhanced fields */
    int aspectFromSettings;
    int mirror; /* R7c: step 2 flips x when the mirror mode is on */
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
    {RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 0, 1, 0},
    /* RD_PRESET_ENHANCED: the aspect and full-height options (R7a), the
     * mirror (R7c) */
    {RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 1, 1, 1},
};

static const RdPresentPreset *activePreset(void)
{
    return &s_presets[g_rd.settings.preset == RD_PRESET_ENHANCED ? 1 : 0];
}

/* step 2's box in an outW x outH output */
static void outputBox(const RdPresentPreset *pr, uint32_t outW, uint32_t outH, RhiRect *box)
{
    rd__PresentBox(outW, outH, pr->aspectFromSettings ? g_rd.outAspect : RD_ASPECT_43, box);
}

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
                 RdFilter filter, int mirror)
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
        /* R7c: the source rectangle right to left (blit_vs interpolates
         * u0 + t (u1 - u0)): u = sw (1 - t), exact at the box's pixel
         * centres when the scale is a power of two */
        cb.uvRect[0] = mirror ? (float)sw : 0.0f;
        cb.uvRect[2] = mirror ? 0.0f : (float)sw;
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

/* --------------------------------------------- the overlay (package OV) */

typedef struct OverlayBatch {
    uint32_t first, count; /* in s_ov.v */
    uint32_t tex;          /* RdTex id, 0 untextured */
    uint8_t prim, blend;
} OverlayBatch;

/* outside g_rd: the registration outlives rd_Shutdown / rd_Init (port/ui
 * registers once at start-up; the tests restart rd between checks) */
static struct {
    RdOverlayFn fn;
    void *user;
    int inside; /* in fn: rd_OverlayPrims keeps prims */
    RdOverlayCtx ctx;
    RdScreenVtx *v;
    uint32_t vCount, vCap;
    OverlayBatch *b;
    uint32_t bCount, bCap;
} s_ov;

/* a present's prims at most (a popup is a few hundred) */
#define RD_OVERLAY_MAX_VERTICES (1u << 20)

void rd_SetPresentOverlay(RdOverlayFn fn, void *user)
{
    s_ov.fn = fn;
    s_ov.user = fn ? user : NULL;
    s_ov.vCount = s_ov.bCount = 0;
}

void rd_OverlayPrims(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend)
{
    if (!s_ov.inside || !v || n == 0 || (uint32_t)type > RD_PRIM_SPRITES ||
        n > RD_OVERLAY_MAX_VERTICES - s_ov.vCount) {
        return;
    }
    if (s_ov.vCount + n > s_ov.vCap) {
        uint32_t cap = s_ov.vCap ? s_ov.vCap : 1024;
        while (cap < s_ov.vCount + n) {
            cap *= 2;
        }
        RdScreenVtx *p = realloc(s_ov.v, (size_t)cap * sizeof(*p));
        if (!p) {
            return;
        }
        s_ov.v = p;
        s_ov.vCap = cap;
    }
    if (s_ov.bCount == s_ov.bCap) {
        const uint32_t cap = s_ov.bCap ? s_ov.bCap * 2 : 64;
        OverlayBatch *p = realloc(s_ov.b, (size_t)cap * sizeof(*p));
        if (!p) {
            return;
        }
        s_ov.b = p;
        s_ov.bCap = cap;
    }
    memcpy(s_ov.v + s_ov.vCount, v, (size_t)n * sizeof(*v));
    OverlayBatch *b = &s_ov.b[s_ov.bCount++];
    b->first = s_ov.vCount;
    b->count = n;
    b->tex = tex.id;
    b->prim = (uint8_t)type;
    b->blend = (uint8_t)blend;
    s_ov.vCount += n;
}

void rd__OverlayCollect(void)
{
    s_ov.vCount = s_ov.bCount = 0;
    const uint32_t w = g_rd.settings.outputWidth, h = g_rd.settings.outputHeight;
    if (!s_ov.fn || !w || !h) {
        return;
    }
    /* the output and box rd__PresentRecord will use: both come from
     * g_rd.settings, which does not change inside a replay */
    const RdPresentPreset *pr = activePreset();
    RhiRect box;
    outputBox(pr, w, h, &box);
    RdOverlayCtx *c = &s_ov.ctx;
    c->outW = w;
    c->outH = h;
    c->box = (RdRect){box.x, box.y, box.w, box.h};
    c->boxScale = (float)box.h / 448.0f;
    c->mirror = pr->mirror && rd__MirrorOn();
    s_ov.inside = 1;
    s_ov.fn(c, s_ov.user);
    s_ov.inside = 0;
}

uint64_t rd__OverlayRingBytes(void)
{
    if (!s_ov.bCount) {
        return 0;
    }
    const uint64_t align = rhi_Limits()->uniformAlign;
    /* FrameCB once, a DrawCB and the expanded vertices (sprites and points
     * give 6 a prim's 2 or 1) a batch */
    uint64_t total = sizeof(IcoFrameCB) + 2 * align;
    total += (uint64_t)s_ov.bCount * (sizeof(IcoDrawCB) + 2 * align + 16);
    total += (uint64_t)s_ov.vCount * 6 * sizeof(IcoSpriteVertex);
    return total;
}

static void overlayRecord(RhiCommandList cl, RhiTexture out)
{
    if (!s_ov.bCount || s_ov.ctx.outW != s_outW || s_ov.ctx.outH != s_outH) {
        return;
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = out;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = s_outW;
    p.height = s_outH;
    rhi_CmdBeginRenderPass(cl, &p);
    RhiViewport vp = {0.0f, 0.0f, (float)s_outW, (float)s_outH, 0.0f, 1.0f};
    rhi_CmdSetViewport(cl, &vp);
    const RhiRect full = {0, 0, s_outW, s_outH};
    rhi_CmdSetScissor(cl, &full);
    /* sprite_ui_vs: x / 16 - origin + g_origin.zw = x / 16, so 12.4 output
     * pixels land 1:1 with integers on pixel edges (rd.h rd_OverlayPrims) */
    const RhiBindGroup frame = rd__FrameGroup(s_outW, s_outH, 0.5f, 0.5f);
    for (uint32_t i = 0; i < s_ov.bCount; i++) {
        const OverlayBatch *b = &s_ov.b[i];
        rd__OverlayDraw(cl, s_outFormat, frame, b->prim, s_ov.v + b->first, b->count, b->tex,
                        b->blend);
    }
    rhi_CmdEndRenderPass(cl);
    s_ov.vCount = s_ov.bCount = 0;
}

void rd__PresentRecord(RhiCommandList cl)
{
    RdTargetRec *disp = rd__TargetRec(RD_TARGET_DISPLAY + 1);
    if (!disp || !disp->color.id) {
        return;
    }
    const RdPresentPreset *pr = activePreset();
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
             RHI_LOAD_DONT_CARE, &full, pr->doubleFilter, 0);
        rd__Transition(cl, g_rd.presentLines, &g_rd.presentLinesState, RHI_STATE_SHADER_READ);
        src = g_rd.presentLines;
        sw = lw;
        sh = lh;
    }
    RhiRect box;
    outputBox(pr, s_outW, s_outH, &box);
    rd__Transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    blit(cl, src, sw, sh, out, s_outFormat, s_outW, s_outH, RHI_LOAD_CLEAR, &box, pr->scaleFilter,
         pr->mirror && rd__MirrorOn());
    /* ==== INSERTION POINT for later presentation passes ====================
     * The plan's "deferred text" and "CRT" packages draw here: after the box
     * blit (they read or write the boxed picture) and before the overlay
     * (the port's UI stays sharp and unfiltered above them).  Each one is a
     * pass on `out` (RHI_STATE_RENDER_TARGET at this point; a pass that
     * samples the picture copies it first or blits from `src`, `box`)
     * with rd__FrameGroup(s_outW, s_outH, ...) for its FrameCB, as blit()
     * does.  Keep the overlay last.  (RENDER_API.md "The presentation
     * overlay", "Ordering".)
     * ======================================================================= */
    overlayRecord(cl, out);
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
    /* the overlay's prims (the registration stays) */
    free(s_ov.v);
    free(s_ov.b);
    s_ov.v = NULL;
    s_ov.b = NULL;
    s_ov.vCount = s_ov.vCap = s_ov.bCount = s_ov.bCap = 0;
}

bool rd_ReadPresented(void *dst, uint32_t *w, uint32_t *h)
{
    /* the window build presents the swapchain image and keeps no copy */
    if (!g_rd.hasDevice || !dst || s_window || rhi_SwapchainFormat() != RHI_FMT_UNKNOWN) {
        return false;
    }
    return rd__ReadPresent(dst, (size_t)g_rd.presentOutW * g_rd.presentOutH * 4, w, h);
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
