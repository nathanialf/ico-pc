/* rd_present.c: DISPLAY to the output.
 *
 * The PS2 picture: the reduced DISPLAY frame
 * (512 x H/2) is shown in a centred 4:3 rectangle of the output, each line
 * doubled (field-line doubling, nearest vertically) and filtered bilinearly
 * horizontally.  Two blits:
 *   1. DISPLAY -> lines target (512 x H), nearest: exact line doubling;
 *   2. lines target -> the aspect's box of the output, bilinear, black
 *      outside.
 * The output is the swapchain backbuffer with a window, or a headless RGBA8
 * texture of RdSettings.outputWidth x outputHeight (tests, the replay tool;
 * none when either is 0).
 *
 * The display options (wave 2, R2c: hooks; wave 7, R7a; v0.3.1: each
 * applies on its own, the preset is only the host's shortcut over them).
 * RdPresentPreset holds the present's filters and its mirror (the
 * interpolation, R7b rd_interp.c, is not one: it presents several times per
 * tick through rd_Present with RdSettings.interpolate, each present
 * replaying the frame blended from the retained pair, and this step is the
 * same either way: it shows whatever DISPLAY the replay left):
 *   mirror        R7c: step 2 may flip x (the mirror mode is a gameplay
 *                 option, not a display one); flipped when
 *                 rd__MirrorOn (rd.h rd_SetMirror, RdSettings.mirror).  Every
 *                 present goes through here, the interpolated ones (R7b
 *                 rd_Present) included; UI prims were flipped at replay
 *                 (rd_replay.c mirrorUi) so they read normally
 * The box takes the aspect option (g_rd.outAspect, 4:3 for a zeroed
 * RdSettings); the projection side is rd_frame.c rd__FillCameraCB, the
 * replay's wide x scale and GsBase.c gsbHostWideX.  With the full-height
 * option DISPLAY's texture has the scene's height (rd__TargetScaleOf) and
 * step 1 is skipped.
 * The scene resolution needs nothing here: DISPLAY's texture is whatever
 * size rd__ApplyDisplay gave it, and both steps sample it normalised.
 * rd__ApplyDisplay (below) turns RdSettings into the scales and factors the
 * targets and the replay use.
 * Field parity is not a present-time effect: the PS2 shifts the scene's
 * XYOFFSET by half a line from the field bit (sceGsSetHalfOffset), which
 * rd_FrameFlip records into the frame head (RD_TARGET_HALF_Y).
 *
 * The overlay (package OV, rd.h rd_SetPresentOverlay): after step 2, the prims the registered callback
 * gave for this present are drawn on the output in a load-preserving pass,
 * one 12.4 unit a sixteenth of an output pixel, unflipped.  The callback
 * runs before the frame's replay (rd__OverlayCollect, from replayFrame) so
 * the textures it touches upload with the frame; only the drawing is here.
 * With no callback registered nothing below step 2 runs, and the output is
 * byte for byte what it was before the overlay existed.
 *
 * Deferred text (package DEF, rd.h rd_DeferredText): in the Enhanced preset with a renderer registered,
 * rd__OverlayCollect first walks the frame's RDC_OVERLAY_TEXT items and the
 * post passes after them, and has the renderer lay each item out on the
 * output (font.c's overlay mode) in its region; the replay skips the items'
 * glyph quads, and textRecord draws the prims after step 2, before the
 * overlay.  In the Original preset nothing is collected and the quads draw.
 *
 * The blank present (package AN-C, rd.h rd_PresentBlank): an empty keep
 * frame replayed with s_blank set: rd__OverlayCollect gives the overlay the
 * output's context (no CRT grid, no deferred text) and rd__PresentRecord
 * clears the output and draws the overlay on it, without DISPLAY.
 *
 * The CRT filter (packages CRT and CRT2, rd_crt.c): with RdSettings.crtMode set and a strength above 0, the scene
 * renders at 1x (rd__ApplyDisplay), no text is deferred (the rows draw as
 * quads into the scene, as in the Original preset), the overlay's prims are
 * laid out on the filter's source grid and drawn into it, and rd__CrtRecord
 * draws the box from DISPLAY in place of steps 1 and 2 (no line doubling:
 * the scanlines are DISPLAY's own lines).  The filter is the present's last
 * pass: nothing draws above it.  Off, this file presents as before.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

_Static_assert(RD_ONCE_EFFECTS_DEPTH_CRT < 32, "rd__LogOnce keeps 32 bits");

typedef struct RdPresentPreset {
    RdFilter doubleFilter; /* step 1 */
    RdFilter scaleFilter;  /* step 2 */
    int lineDouble;        /* step 1, unless the full-height option is on */
    int mirror;            /* R7c: step 2 flips x when the mirror mode is on */
} RdPresentPreset;

#define RD_ASPECT_43 (4.0f / 3.0f)
#define RD_ASPECT_MAX (32.0f / 9.0f)

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

/* one for every preset: the options it used to gate apply on their own
 * (rd__ApplyDisplay) */
static const RdPresentPreset s_present = {RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 1};

/* step 2's box in an outW x outH output */
static void outputBox(uint32_t outW, uint32_t outH, RhiRect *box)
{
    rd__PresentBox(outW, outH, g_rd.outAspect, box);
}

static float clampAspect(float a)
{
    if (!(a > RD_ASPECT_43 + 1e-4f)) {
        return RD_ASPECT_43;
    }
    return a > RD_ASPECT_MAX ? RD_ASPECT_MAX : a;
}

bool rd__ApplyDisplay(void)
{
    const RdSettings *st = &g_rd.settings;
    /* v0.3.1: every option applies whatever the preset; a zeroed
     * RdSettings is the PS2 picture (scale 0 and no size is the GS size
     * unless the Enhanced flag asks for the output's box, below) */
    const float aspect = clampAspect(st->aspect);
    const float wide = RD_ASPECT_43 / aspect;
    const float gw = (float)g_rd.gsW, gh = (float)g_rd.gsH;
    float w, h;
    /* package CRT2: the CRT filter shows the PS2's pixels, so the scene
     * renders at 1x while it is on, whatever the resolution asks (which
     * takes effect again with the filter off) */
    const int crtLock = rd__CrtOn() && st->sceneScale != 1.0f;
    const float scale = rd__CrtOn() ? 1.0f : st->sceneScale;
    if (scale > 0.0f) {
        h = gh * scale;
        w = gw * scale * aspect / RD_ASPECT_43;
    } else if (st->sceneWidth && st->sceneHeight) {
        w = (float)st->sceneWidth;
        h = (float)st->sceneHeight;
    } else if (st->preset == RD_PRESET_ENHANCED && st->outputWidth && st->outputHeight) {
        /* "window": the presentation box.  Only with the Enhanced flag, so
         * a zeroed RdSettings (the tests', the replay tool's default) stays
         * the GS size; the host sends scale 1 for the Original rows */
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
    const float sx = w / gw < 1.0f ? 1.0f : w / gw;
    const float sy = h / gh < 1.0f ? 1.0f : h / gh;
    const float work = rd_WorkTargetScale((uint32_t)(sy * 448.0f + 0.5f));
    uint8_t filter = st->filterUpgrade <= RD_FILTER_UPGRADE_ANISOTROPIC ? st->filterUpgrade : 0;
    if (g_rd.hasDevice && !rhi_Limits()->textureMips) {
        filter = 0;
    }
    const uint8_t full = st->fullHeightScene != 0;
    const uint32_t vs = st->vsync ? 2u : 1u;
    const bool changed = sx != g_rd.sceneSx || sy != g_rd.sceneSy || work != g_rd.workScale ||
                         full != g_rd.fullHeight;
    /* one line when what the options give differs from what was in force:
     * this runs on a settings change or a resize (rd_BeginFrame's
     * settingsPending) and at init, never per frame */
    if (changed || aspect != g_rd.outAspect || filter != g_rd.filterUpgrade ||
        vs != g_rd.vsyncApplied) {
        rd__Log("display: scene %gx%g%s, work %g, aspect %.3f, filter %u, %s height, vsync %s",
                (double)sx, (double)sy, crtLock ? " (CRT: 1x)" : "", (double)work, (double)aspect,
                (unsigned)filter, full ? "full" : "half", st->vsync ? "on" : "off");
    }
    g_rd.sceneSx = sx;
    g_rd.sceneSy = sy;
    g_rd.workScale = work;
    g_rd.wideX = wide;
    g_rd.outAspect = aspect;
    g_rd.filterUpgrade = filter;
    g_rd.fullHeight = full;
    /* vsync: the swapchain's present mode (Vulkan FIFO, else MAILBOX or
     * IMMEDIATE; D3D12 the sync interval) */
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
        rd__OutputFollowSwapchain(); /* N1: the image's own size */
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
        rd__BindUniform(cl, 0, rd__FrameGroup(dw, dh, 0.0f, 0.0f));
        rd__BindUniform(cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(
            cl, 2, rd__TexGroup(src, rd__Sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        rhi_CmdDraw(cl, 3, 0, 1);
    }
    rhi_CmdEndRenderPass(cl);
}

void rd__PresentBlit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                     RhiFormat dstFmt, uint32_t dw, uint32_t dh, RhiLoadOp load, const RhiRect *box,
                     RdFilter filter, int mirror)
{
    blit(cl, src, sw, sh, dst, dstFmt, dw, dh, load, box, filter, mirror);
}

/* ------------------------------- the effects depth (v0.4.1, package R1)
 * RdSettings.effectsDepth, with an effects program loaded (depthWanted):
 * step 2 as one pass with two targets, the output
 * and an output-size RHI_FMT_D32F depth buffer cleared to 0.0 (far), drawn
 * by blit_depth_ps: the colour exactly as blit_ps, and SV_Depth the scene's
 * depth at the same normalised source position (SCENE and DISPLAY cover
 * the same GS frame; a mirrored box flips both), read nearest from a copy
 * (SCENE's depth is an attachment, never sampled; the fog's copy in
 * rd_replay.c doFog is the precedent).  Outside the box the clear stays, so
 * the bars read as far.  The point is an effects program hooked into the
 * API (ReShade, vkBasalt): it looks for a depth buffer of the backbuffer's
 * size among the render passes, and the scene's is the scene's size.  The
 * convention is the scene's (gs_z_to_depth): the depth grows with GS Z,
 * near 1 and far 0, so ReShade's RESHADE_DEPTH_INPUT_IS_REVERSED is 1 (the
 * docs' ReShade notes, R3).  The
 * deferred text, the capture and the overlay stay colour-only passes after
 * it; under the CRT filter there is no box blit and no effects depth. */
/* Only for an effects program: the pass runs when the setting is on and
 * rhi_InjectorName() names one (ReShade, vkBasalt), never on Android (no
 * effects program hooks the game there); the tests force it (a lavapipe
 * run has no layer). */
static bool s_forceDepth;

void rd__ForceEffectsDepth(bool force)
{
    s_forceDepth = force;
}

static bool depthWanted(void)
{
    if (!g_rd.settings.effectsDepth) {
        return false;
    }
    if (s_forceDepth) {
        return true;
    }
#ifdef __ANDROID__
    return false;
#else
    return rhi_InjectorName() != NULL;
#endif
}

static struct {
    RhiTexture copy; /* SCENE's depth, D32F_S8, sampled */
    RhiState copyState;
    uint32_t copyW, copyH;
    RhiTexture out; /* the output-size D32F */
    RhiState outState;
    uint32_t outW, outH;
    int failLogged;
} s_depth;

static void depthShutdown(void)
{
    if (s_depth.copy.id) {
        rhi_DestroyTexture(s_depth.copy);
    }
    if (s_depth.out.id) {
        rhi_DestroyTexture(s_depth.out);
    }
    memset(&s_depth, 0, sizeof(s_depth));
}

/* step 2 with the effects depth; false (nothing recorded) when it cannot
 * run, and the plain blit draws the box */
static bool depthBlit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                      RhiFormat dstFmt, uint32_t dw, uint32_t dh, const RhiRect *box,
                      RdFilter filter, int mirror)
{
    RdTargetRec *ts = rd__TargetRec(RD_TARGET_SCENE + 1);
    if (!ts || !ts->withDepth || !ts->depth.id || ts->depthState == RHI_STATE_UNDEFINED) {
        return false;
    }
    if (!s_depth.copy.id || s_depth.copyW != ts->tw || s_depth.copyH != ts->th) {
        if (s_depth.copy.id) {
            rhi_DestroyTexture(s_depth.copy);
        }
        s_depth.copy = rhi_CreateTexture(&(RhiTextureDesc){
            ts->tw, ts->th, 1, RHI_FMT_D32F_S8,
            /* the depth-stencil usage: Vulkan's sampled depth layout needs it */
            RHI_TEX_SAMPLED | RHI_TEX_DEPTH_STENCIL | RHI_TEX_COPY_DST, "rd effects depth copy"});
        s_depth.copyState = RHI_STATE_UNDEFINED;
        s_depth.copyW = ts->tw;
        s_depth.copyH = ts->th;
    }
    if (!s_depth.out.id || s_depth.outW != dw || s_depth.outH != dh) {
        if (s_depth.out.id) {
            rhi_DestroyTexture(s_depth.out);
        }
        /* sampled and copyable, for the program that reads it (and the
         * tests' readback) */
        s_depth.out = rhi_CreateTexture(&(RhiTextureDesc){
            dw, dh, 1, RHI_FMT_D32F, RHI_TEX_DEPTH_STENCIL | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC,
            "rd effects depth"});
        s_depth.outState = RHI_STATE_UNDEFINED;
        s_depth.outW = dw;
        s_depth.outH = dh;
    }
    const RdPipeKeyInt k = rd__PresentDepthKey(dstFmt);
    const RhiPipeline pipe = rd__GetPipeline(&k);
    if (!s_depth.copy.id || !s_depth.out.id || !pipe.id) {
        if (!s_depth.failLogged) {
            s_depth.failLogged = 1;
            rd__Log("present: no effects depth (%s); the picture is shown without it",
                    pipe.id ? "no depth texture" : "no pipeline");
        }
        return false;
    }
    rd__Transition(cl, ts->depth, &ts->depthState, RHI_STATE_COPY_SRC);
    rd__Transition(cl, s_depth.copy, &s_depth.copyState, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(cl, ts->depth, (RhiRect){0, 0, ts->tw, ts->th}, s_depth.copy, 0, 0);
    rd__Transition(cl, s_depth.copy, &s_depth.copyState, RHI_STATE_SHADER_READ);
    rd__Transition(cl, s_depth.out, &s_depth.outState, RHI_STATE_DEPTH_WRITE);

    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.depth.texture = s_depth.out;
    p.depth.depthLoad = RHI_LOAD_CLEAR;
    p.depth.stencilLoad = RHI_LOAD_DONT_CARE;
    p.depth.clearDepth = 0.0f; /* far (GS Z 0): the bars */
    p.width = dw;
    p.height = dh;
    rhi_CmdBeginRenderPass(cl, &p);
    RhiViewport vp = {(float)box->x, (float)box->y, (float)box->w, (float)box->h, 0.0f, 1.0f};
    rhi_CmdSetViewport(cl, &vp);
    rhi_CmdSetScissor(cl, box);
    /* blit()'s constants, so the colour is blit_ps's bytes */
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    for (int i = 0; i < 4; i++) {
        cb.col[i] = 0x80;
    }
    cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
    cb.uvRect[0] = mirror ? (float)sw : 0.0f;
    cb.uvRect[2] = mirror ? 0.0f : (float)sw;
    cb.uvRect[3] = (float)sh;
    cb.tex[0] = (float)sw;
    cb.tex[1] = (float)sh;
    cb.tex[2] = 1.0f / (float)sw;
    cb.tex[3] = 1.0f / (float)sh;
    RhiBinding b[3];
    memset(b, 0, sizeof(b));
    b[0].slot = 1;
    b[0].type = RHI_BIND_SAMPLED_TEXTURE;
    b[0].texture = src;
    b[1].slot = 1;
    b[1].type = RHI_BIND_SAMPLER;
    b[1].sampler = rd__Sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = s_depth.copy;
    b[2].aspect = RHI_ASPECT_DEPTH;
    const RhiBindGroup g2 = rhi_CreateBindGroup(&(RhiBindGroupDesc){g_rd.layoutTex, b, 3});
    if (g2.id) {
        rhi_CmdSetPipeline(cl, pipe);
        rd__BindUniform(cl, 0, rd__FrameGroup(dw, dh, 0.0f, 0.0f));
        rd__BindUniform(cl, 1, rd__DrawGroup(&cb));
        rhi_CmdSetBindGroup(cl, 2, g2);
        rhi_CmdDraw(cl, 3, 0, 1);
    }
    rhi_CmdEndRenderPass(cl);
    return g2.id != 0;
}

bool rd__ReadPresentDepth(float *dst, size_t dstSize, uint32_t *w, uint32_t *h)
{
    if (!g_rd.hasDevice || !s_depth.out.id || s_depth.outState == RHI_STATE_UNDEFINED ||
        dstSize < (size_t)s_depth.outW * s_depth.outH * sizeof(float)) {
        return false;
    }
    if (w) {
        *w = s_depth.outW;
    }
    if (h) {
        *h = s_depth.outH;
    }
    if (s_depth.outState != RHI_STATE_COPY_SRC) {
        RhiCommandList cl = rhi_BeginCommands();
        if (!cl.id) {
            return false;
        }
        rd__Transition(cl, s_depth.out, &s_depth.outState, RHI_STATE_COPY_SRC);
        rhi_EndCommands(cl);
        rhi_Submit(cl);
    }
    uint32_t pitch = 0;
    if (!rhi_ReadbackTexture(s_depth.out, RHI_ASPECT_DEPTH, dst, dstSize, &pitch)) {
        return false;
    }
    return pitch == s_depth.outW * sizeof(float);
}

/* --------------------------------------------- the overlay (package OV) */

typedef struct OverlayBatch {
    uint32_t first, count; /* in s_ov.v */
    uint32_t tex;          /* RdTex id, 0 untextured */
    uint8_t prim, blend;
    RhiRect sc; /* package DEF: the region (the whole output for the overlay's) */
} OverlayBatch;

/* outside g_rd: the registration outlives rd_Shutdown / rd_Init (port/ui
 * registers once at start-up; the tests restart rd between checks) */
static struct {
    RdOverlayFn fn;
    void *user;
    RdDeferredTextFn textFn; /* package DEF */
    void *textUser;
    int inside; /* in fn or textFn: rd_OverlayPrims keeps prims */
    RhiRect sc; /* the region rd_OverlayPrims gives its batch */
    RdOverlayCtx ctx;
    RdScreenVtx *v;
    uint32_t vCount, vCap;
    OverlayBatch *b;
    uint32_t bCount, bCap;
    uint32_t textBatches; /* package DEF: b[0, textBatches) are the deferred text's */
    int grid;             /* package CRT2: the prims are on the CRT filter's source grid */
} s_ov;

/* a present's prims at most (a popup is a few hundred) */
#define RD_OVERLAY_MAX_VERTICES (1u << 20)

void rd_SetPresentOverlay(RdOverlayFn fn, void *user)
{
    s_ov.fn = fn;
    s_ov.user = fn ? user : NULL;
    s_ov.vCount = s_ov.bCount = s_ov.textBatches = 0;
}

RdOverlayFn rd_GetPresentOverlay(void **user)
{
    if (user) {
        *user = s_ov.user;
    }
    return s_ov.fn;
}

/* package AN-C: the present in progress is rd_PresentBlank's */
static int s_blank;

bool rd_PresentBlank(void)
{
    if (!g_rd.hasDevice) {
        return false;
    }
    /* lists 11 and 12 of a frame with none: no draw, no target touched */
    static RdFrame empty;
    s_blank = 1;
    const bool ok = rd__ReplayFrame(&empty, 1, true);
    s_blank = 0;
    return ok;
}

void rd_SetDeferredTextFn(RdDeferredTextFn fn, void *user)
{
    s_ov.textFn = fn;
    s_ov.textUser = fn ? user : NULL;
    s_ov.vCount = s_ov.bCount = s_ov.textBatches = 0;
}

bool rd_DeferredTextActive(void)
{
    return g_rd.deferText;
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
    b->sc = s_ov.sc;
    s_ov.vCount += n;
}

/* ------------------------------------------- deferred text (package DEF).  The frame's RDC_OVERLAY_TEXT
 * commands are walked in replay order with the state they replay under: an
 * item takes the scissor in force; an op (a post pass recorded after text)
 * changes the items before it as the pass changed the pixels they had been
 * drawn into.  FADE, BRIGHTNESS and the LETTERBOX are lerps of the
 * destination toward a colour, d' = d (1 - k) + C k, affine in d, so a text
 * pixel blended over the scene and then lerped is the lerped scene with the
 * text blended over it in the lerped colour: a lerp item's colour becomes
 * c (1 - k) + C k with its alpha kept, an additive item's (Cs As + Cd) c (1
 * - k).  The letterbox does that inside its two bands only, so an item is
 * cut into segments by scene line.  KEEP draws DISPLAY over the whole scene
 * without blending: the items before it are gone, as their quads would be.
 * The REDUCTION's tint scales the colour of either kind (linear; where the
 * GS clamps a tint above 1.0 behind a partly covered pixel, the fold is a
 * little darker).
 * Then each item is drawn once per segment, clipped to the segment, its
 * scissor and the reduction's border crop (output pixels). */

typedef struct TextSeg {
    float y0, y1; /* scene lines */
    float rgb[3]; /* the shown colour, 0..255 (the GS colour modulated by a white texel) */
} TextSeg;

#define TEXT_SEGS 5

typedef struct TextPending {
    const RdTextItem *it; /* in the frame's payload */
    int32_t sc[4];        /* the scissor, GS pixels inclusive */
    uint32_t gsW, gsH;    /* of the target it was drawn into */
    uint32_t nSeg;
    TextSeg seg[TEXT_SEGS];
} TextPending;

static struct {
    TextPending *p;
    uint32_t n, cap;
    const RdFrame *f;
} s_text;

static float shownOf(uint8_t c)
{
    const float v = (float)c * 255.0f / 128.0f;
    return v > 255.0f ? 255.0f : v;
}

static void textFold(TextSeg *g, float k, const uint8_t rgb[3], int additive)
{
    for (int i = 0; i < 3; i++) {
        g->rgb[i] = g->rgb[i] * (1.0f - k) + (additive ? 0.0f : (float)rgb[i] * k);
    }
}

/* the letterbox: segments split at the band edges, the parts in a band
 * folded toward black */
static void textBands(TextPending *t, float k, uint32_t lines)
{
    static const uint8_t black[3] = {0, 0, 0};
    const float edges[2] = {(float)lines, (float)t->gsH - (float)lines};
    for (int e = 0; e < 2; e++) {
        for (uint32_t i = 0; i < t->nSeg && t->nSeg < TEXT_SEGS; i++) {
            TextSeg *g = &t->seg[i];
            if (g->y0 < edges[e] && edges[e] < g->y1) {
                memmove(&t->seg[i + 2], &t->seg[i + 1], (t->nSeg - i - 1) * sizeof(TextSeg));
                t->seg[i + 1] = *g;
                t->seg[i + 1].y0 = edges[e];
                g->y1 = edges[e];
                t->nSeg++;
                i++;
            }
        }
    }
    for (uint32_t i = 0; i < t->nSeg; i++) {
        TextSeg *g = &t->seg[i];
        if (g->y1 <= edges[0] || g->y0 >= edges[1]) {
            textFold(g, k, black, t->it->additive);
        }
    }
}

static void textWalk(void *user, int list, uint32_t index, const RdCmd *c, const RdStateBlock *st)
{
    (void)user;
    (void)list;
    (void)index;
    if (c->type != RDC_OVERLAY_TEXT) {
        return;
    }
    const RdFrame *f = s_text.f;
    if (c->u[1] > f->payloadSize || c->u[2] > f->payloadSize - c->u[1]) {
        return;
    }
    if (c->b[0] == RD_OTEXT_ITEM) {
        if (c->u[2] != sizeof(RdTextItem)) {
            return;
        }
        if (s_text.n == s_text.cap) {
            const uint32_t cap = s_text.cap ? s_text.cap * 2 : 64;
            TextPending *p = realloc(s_text.p, (size_t)cap * sizeof(*p));
            if (!p) {
                return;
            }
            s_text.p = p;
            s_text.cap = cap;
        }
        TextPending *t = &s_text.p[s_text.n++];
        memset(t, 0, sizeof(*t));
        t->it = (const RdTextItem *)(const void *)(f->payload + c->u[1]);
        memcpy(t->sc, st->scissor, sizeof(t->sc));
        t->gsW = st->gsW ? st->gsW : g_rd.gsW;
        t->gsH = st->gsH ? st->gsH : g_rd.gsH;
        t->nSeg = 1;
        t->seg[0].y0 = -1e9f;
        t->seg[0].y1 = 1e9f;
        for (int i = 0; i < 3; i++) {
            t->seg[0].rgb[i] = shownOf(t->it->rgba[i]);
        }
        return;
    }
    if (c->b[0] != RD_OTEXT_OP || c->u[2] != sizeof(RdTextOp)) {
        return;
    }
    RdTextOp op;
    memcpy(&op, f->payload + c->u[1], sizeof(op));
    switch (c->b[1]) {
    case RD_POST_KEEP:
        s_text.n = 0;
        break;
    case RD_POST_FADE:
    case RD_POST_BRIGHTNESS: {
        const float k = (op.rgba[3] > 128 ? 128.0f : (float)op.rgba[3]) / 128.0f;
        for (uint32_t i = 0; i < s_text.n; i++) {
            for (uint32_t g = 0; g < s_text.p[i].nSeg; g++) {
                textFold(&s_text.p[i].seg[g], k, op.rgba, s_text.p[i].it->additive);
            }
        }
        break;
    }
    case RD_POST_REDUCTION: {
        /* SCENE into DISPLAY modulated by the tint (0x80 = 1.0): linear in
           the destination, so the colour of either kind of item scales */
        for (uint32_t i = 0; i < s_text.n; i++) {
            for (uint32_t g = 0; g < s_text.p[i].nSeg; g++) {
                for (int k = 0; k < 3; k++) {
                    s_text.p[i].seg[g].rgb[k] *= (float)op.rgba[k] / 128.0f;
                }
            }
        }
        break;
    }
    case RD_POST_LETTERBOX: {
        const float k = (op.fix > 128 ? 128.0f : (float)op.fix) / 128.0f;
        for (uint32_t i = 0; i < s_text.n; i++) {
            textBands(&s_text.p[i], k, op.lines);
        }
        break;
    }
    default:
        break;
    }
}

static int32_t roundPx(float v)
{
    return (int32_t)floorf(v + 0.5f);
}

/* the region of segment g of t on the output: its scissor, its lines and
 * the reduction's crop, inside the box; false when empty */
static bool textRegion(const TextPending *t, const TextSeg *g, RhiRect *out)
{
    const RdRect *b = &s_ov.ctx.box;
    const float bx = (float)b->x, by = (float)b->y, bw = (float)b->w, bh = (float)b->h;
    const float W = (float)t->gsW, H = (float)t->gsH;
    /* the 4:3 picture the UI is drawn in (font.h ui_BeginOverlay) */
    const float w43 = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    const float left = bx + (bw - w43) * 0.5f;
    /* the scissor: a side at the target's edge stays at the box's edge
     * (rd__WideScissor), the others move with the UI */
    float x0 = t->sc[0] <= 0 ? bx : left + (float)t->sc[0] * w43 / W;
    float x1 = t->sc[2] >= (int32_t)t->gsW - 1 ? bx + bw : left + (float)(t->sc[2] + 1) * w43 / W;
    float y0 = by + (float)t->sc[1] * bh / H;
    float y1 = by + (float)(t->sc[3] + 1) * bh / H;
    /* the reduction's crop (rd_post.c postReduction): 2 pixels left and
     * right, 8 lines of DISPLAY's H / 2 top and bottom (2 below 512) */
    const float crop = t->gsH >= 512 ? 8.0f : 2.0f, half = H * 0.5f;
    x0 = fmaxf(x0, bx + bw * 2.0f / W);
    x1 = fminf(x1, bx + bw * (W - 2.0f) / W);
    y0 = fmaxf(y0, by + bh * crop / half);
    y1 = fminf(y1, by + bh * (half - crop) / half);
    /* the segment */
    y0 = fmaxf(y0, by + g->y0 * bh / H);
    y1 = fminf(y1, by + g->y1 * bh / H);
    int32_t ix0 = roundPx(x0), iy0 = roundPx(y0), ix1 = roundPx(x1), iy1 = roundPx(y1);
    ix0 = ix0 < 0 ? 0 : ix0;
    iy0 = iy0 < 0 ? 0 : iy0;
    ix1 = ix1 > (int32_t)s_ov.ctx.outW ? (int32_t)s_ov.ctx.outW : ix1;
    iy1 = iy1 > (int32_t)s_ov.ctx.outH ? (int32_t)s_ov.ctx.outH : iy1;
    if (ix1 <= ix0 || iy1 <= iy0) {
        return false;
    }
    *out = (RhiRect){ix0, iy0, (uint32_t)(ix1 - ix0), (uint32_t)(iy1 - iy0)};
    return true;
}

static void textCollect(const RdFrame *f, int keep)
{
    s_text.n = 0;
    s_text.f = NULL;
    /* no item, nothing to walk for: most presents (rd_DeferredText counts
     * them; an interpolated frame adds prev's it inserts, rd__LoadFrame
     * counts a dump's) */
    if (!f->textItems) {
        return;
    }
    s_text.f = f;
    RdStateBlock st = f->startState;
    rd__Walk(f, keep, &st, textWalk, NULL);
    for (uint32_t i = 0; i < s_text.n; i++) {
        const TextPending *t = &s_text.p[i];
        if (t->it->rgba[3] == 0 || !t->it->utf8[0]) {
            continue;
        }
        for (uint32_t g = 0; g < t->nSeg; g++) {
            RhiRect r;
            if (!textRegion(t, &t->seg[g], &r)) {
                continue;
            }
            RdTextItem it = *t->it;
            it.utf8[RD_TEXT_BYTES - 1] = '\0';
            for (int k = 0; k < 3; k++) {
                const float c = t->seg[g].rgb[k] * 128.0f / 255.0f;
                it.rgba[k] = (uint8_t)(c >= 255.0f ? 255 : (c <= 0.0f ? 0 : (int)(c + 0.5f)));
            }
            if (it.additive && !(it.rgba[0] | it.rgba[1] | it.rgba[2])) {
                continue; /* adds nothing */
            }
            s_ov.sc = r;
            s_ov.inside = 1;
            s_ov.textFn(&s_ov.ctx, &it, s_ov.textUser);
            s_ov.inside = 0;
        }
    }
    s_text.n = 0;
    s_text.f = NULL;
}

void rd__OverlayCollect(const RdFrame *f, int keep)
{
    s_ov.vCount = s_ov.bCount = s_ov.textBatches = 0;
    s_ov.grid = 0;
    g_rd.deferText = false;
    uint32_t w = g_rd.settings.outputWidth, h = g_rd.settings.outputHeight;
    if ((!s_ov.fn && !s_ov.textFn) || !w || !h) {
        return;
    }
    /* the output and box rd__PresentRecord will use: both come from
     * g_rd.settings.  One case changes them after this: on the present
     * where rd__OutputFollowSwapchain takes a rebuilt swapchain's size
     * (after the acquire, later in the replay), the overlay is laid out for
     * the old size for that one frame and follows from the next */
    const RdPresentPreset *pr = &s_present;
    RhiRect box;
    outputBox(w, h, &box);
    /* package CRT2: under the CRT filter the overlay is part of the
     * picture: its context is the filter's source grid at the frame's
     * lines (the 1x frame the game's own UI is drawn in), the box all of
     * it, and rd__CrtRecord draws the prims into that grid */
    const bool crt = rd__CrtOn() && !s_blank;
    if (crt) {
        uint32_t vw, vh;
        rd__CrtGrid(&vw, &vh);
        w = vw;
        h = g_rd.gsH ? g_rd.gsH : 2 * vh;
        box = (RhiRect){0, 0, w, h};
        s_ov.grid = 1;
    }
    RdOverlayCtx *c = &s_ov.ctx;
    c->outW = w;
    c->outH = h;
    c->box = (RdRect){box.x, box.y, box.w, box.h};
    c->boxScale = (float)box.h / 448.0f;
    c->mirror = pr->mirror && rd__MirrorOn();
    /* package DEF: the deferred text first, so the overlay draws above it;
     * package CRT2: none under the CRT filter (the rows draw as quads into
     * the scene, as in the Original preset, and go through the filter) */
    if (s_ov.textFn && f && g_rd.settings.preset == RD_PRESET_ENHANCED && !crt && !s_blank) {
        g_rd.deferText = true;
        textCollect(f, keep);
    }
    s_ov.textBatches = s_ov.bCount;
    if (s_ov.fn) {
        s_ov.sc = (RhiRect){0, 0, w, h};
        s_ov.inside = 1;
        s_ov.fn(c, s_ov.user);
        s_ov.inside = 0;
    }
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

/* a region of the overlay's own frame (ctx.outW x outH) scaled into dst,
 * rounded outwards */
static RhiRect scaleRect(RhiRect r, const RhiRect *dst)
{
    const uint64_t ow = s_ov.ctx.outW, oh = s_ov.ctx.outH;
    if (r.x < 0) {
        r.w = (uint32_t)-r.x < r.w ? r.w + (uint32_t)r.x : 0;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h = (uint32_t)-r.y < r.h ? r.h + (uint32_t)r.y : 0;
        r.y = 0;
    }
    const uint32_t x0 = (uint32_t)((uint64_t)r.x * dst->w / ow);
    const uint32_t y0 = (uint32_t)((uint64_t)r.y * dst->h / oh);
    uint32_t x1 = (uint32_t)((((uint64_t)r.x + r.w) * dst->w + ow - 1) / ow);
    uint32_t y1 = (uint32_t)((((uint64_t)r.y + r.h) * dst->h + oh - 1) / oh);
    x1 = x1 > dst->w ? dst->w : x1;
    y1 = y1 > dst->h ? dst->h : y1;
    x1 = x1 < x0 ? x0 : x1;
    y1 = y1 < y0 ? y0 : y1;
    return (RhiRect){dst->x + (int32_t)x0, dst->y + (int32_t)y0, x1 - x0, y1 - y0};
}

/* batches [from, to) in one load-preserving pass on out (fmt, tw x th: the
 * output, or package CRT2's grid layer).  dst NULL: the overlay's frame is
 * out's, 1:1; else its frame is scaled into dst of out (the grid-mode
 * overlay on the output when the CRT pass could not draw it, see
 * overlayRecord) */
static void drawBatchesAt(RhiCommandList cl, RhiTexture out, RhiFormat fmt, uint32_t tw,
                          uint32_t th, const RhiRect *dst, uint32_t from, uint32_t to)
{
    const uint32_t ow = s_ov.ctx.outW, oh = s_ov.ctx.outH;
    if (from >= to || !ow || !oh || (!dst && (ow != tw || oh != th)) ||
        (dst && (!dst->w || !dst->h))) {
        return;
    }
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = out;
    p.color[0].load = RHI_LOAD_LOAD;
    p.colorCount = 1;
    p.width = tw;
    p.height = th;
    rhi_CmdBeginRenderPass(cl, &p);
    const RhiRect all = {0, 0, ow, oh};
    const RhiRect area = dst ? *dst : all;
    RhiViewport vp = {(float)area.x, (float)area.y, (float)area.w, (float)area.h, 0.0f, 1.0f};
    rhi_CmdSetViewport(cl, &vp);
    RhiRect cur = all;
    rhi_CmdSetScissor(cl, dst ? &area : &cur);
    /* sprite_ui_vs: x / 16 - origin + g_origin.zw = x / 16, so 12.4 output
     * pixels land 1:1 with integers on pixel edges (rd.h rd_OverlayPrims) */
    const RdUniform frame = rd__FrameGroup(ow, oh, 0.5f, 0.5f);
    for (uint32_t i = from; i < to; i++) {
        const OverlayBatch *b = &s_ov.b[i];
        /* package DEF: the deferred text's batches carry their regions */
        if (memcmp(&b->sc, &cur, sizeof(cur)) != 0) {
            cur = b->sc;
            if (dst) {
                const RhiRect sc = scaleRect(cur, dst);
                rhi_CmdSetScissor(cl, &sc);
            } else {
                rhi_CmdSetScissor(cl, &cur);
            }
        }
        rd__OverlayDraw(cl, fmt, frame, b->prim, s_ov.v + b->first, b->count, b->tex, b->blend);
    }
    rhi_CmdEndRenderPass(cl);
}

static void drawBatches(RhiCommandList cl, RhiTexture out, RhiFormat fmt, uint32_t ow, uint32_t oh,
                        uint32_t from, uint32_t to)
{
    drawBatchesAt(cl, out, fmt, ow, oh, NULL, from, to);
}

/* package DEF: the deferred text, at the insertion point */
static void textRecord(RhiCommandList cl, RhiTexture out)
{
    if (!s_ov.grid) {
        drawBatches(cl, out, s_outFormat, s_outW, s_outH, 0, s_ov.textBatches);
    }
}

/* the overlay on the output; under the CRT filter (package CRT2) it was
 * drawn into the filter's grid (inPicture), and this only forgets it.  A
 * grid-mode overlay the filter did not draw (rd__CrtRecord failed, or the
 * capture's pass left it out and the second pass failed) is drawn on the
 * output, its grid frame scaled into box, so the popups do not vanish */
static void overlayRecord(RhiCommandList cl, RhiTexture out, const RhiRect *box, bool inPicture)
{
    if (!s_ov.grid) {
        drawBatches(cl, out, s_outFormat, s_outW, s_outH, s_ov.textBatches, s_ov.bCount);
    } else if (!inPicture) {
        drawBatchesAt(cl, out, s_outFormat, s_outW, s_outH, box, s_ov.textBatches, s_ov.bCount);
    }
    s_ov.vCount = s_ov.bCount = s_ov.textBatches = 0;
    s_ov.grid = 0;
}

bool rd__OverlayGridPending(void)
{
    return s_ov.grid && s_ov.bCount > s_ov.textBatches;
}

void rd__OverlayGridDraw(RhiCommandList cl, RhiTexture t, RhiFormat fmt, uint32_t w, uint32_t h)
{
    if (s_ov.grid) {
        drawBatches(cl, t, fmt, w, h, s_ov.textBatches, s_ov.bCount);
    }
}

/* ------------------------------------------- the capture (package PHOTO)
 * rd.h rd_CapturePresented: the output of the next present, copied before
 * the overlay into a texture of the output's format and read back after the
 * submit.  The headless output and the swapchain image both allow copies
 * (RHI_TEX_COPY_SRC; Vulkan's swapchain is created with transfer-source
 * usage), so the window build needs no offscreen present of its own. */
static struct {
    int armed, copied;
    char path[1024];
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
    RhiFormat fmt;
    int result; /* 1 written, -1 failed, 0 none since the last rd_CaptureResult */
    char done[1024];
} s_cap;

bool rd_CapturePresented(const char *png)
{
    if (!g_rd.hasDevice || !png || !png[0] || strlen(png) >= sizeof(s_cap.path)) {
        return false;
    }
    strcpy(s_cap.path, png);
    s_cap.armed = 1;
    s_cap.result = 0;
    return true;
}

bool rd__CaptureArmed(void)
{
    return s_cap.armed != 0;
}

int rd_CaptureResult(char *path, uint32_t pathSize)
{
    const int r = s_cap.result;
    if (r != 0 && path && pathSize) {
        strncpy(path, s_cap.done, pathSize - 1);
        path[pathSize - 1] = '\0';
    }
    s_cap.result = 0;
    return r;
}

/* the output, out (RENDER_TARGET), into the capture texture; out goes back
 * to RENDER_TARGET for the overlay */
static void captureRecord(RhiCommandList cl, RhiTexture out, RhiState *outState)
{
    if (!s_cap.armed) {
        return;
    }
    s_cap.armed = 0;
    if (!s_cap.tex.id || s_cap.w != s_outW || s_cap.h != s_outH || s_cap.fmt != s_outFormat) {
        if (s_cap.tex.id) {
            rhi_DestroyTexture(s_cap.tex);
        }
        s_cap.tex = rhi_CreateTexture(&(RhiTextureDesc){s_outW, s_outH, 1, s_outFormat,
                                                        RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST,
                                                        "rd photo capture"});
        s_cap.state = RHI_STATE_UNDEFINED;
        s_cap.w = s_outW;
        s_cap.h = s_outH;
        s_cap.fmt = s_outFormat;
    }
    if (!s_cap.tex.id) {
        rd__Log("photo: capture %s: no texture for %ux%u", s_cap.path, s_outW, s_outH);
        snprintf(s_cap.done, sizeof(s_cap.done), "%s", s_cap.path);
        s_cap.result = -1;
        return;
    }
    rd__Transition(cl, out, outState, RHI_STATE_COPY_SRC);
    rd__Transition(cl, s_cap.tex, &s_cap.state, RHI_STATE_COPY_DST);
    rhi_CmdCopyTexture(cl, out, (RhiRect){0, 0, s_outW, s_outH}, s_cap.tex, 0, 0);
    rd__Transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    s_cap.copied = 1;
}

void rd__CaptureFinish(void)
{
    if (!s_cap.copied) {
        return;
    }
    s_cap.copied = 0;
    const size_t n = (size_t)s_cap.w * s_cap.h * 4;
    uint8_t *px = malloc(n);
    bool ok = px && rd__ReadRhiTexture(s_cap.tex, &s_cap.state, s_cap.w, s_cap.h, px, n);
    if (ok && s_cap.fmt == RHI_FMT_BGRA8_UNORM) {
        for (size_t i = 0; i < n; i += 4) {
            const uint8_t b = px[i];
            px[i] = px[i + 2];
            px[i + 2] = b;
        }
    }
    ok = ok && rd_WritePng(s_cap.path, px, s_cap.w, s_cap.h, s_cap.w * 4, 0);
    free(px);
    rd__Log("photo: capture %ux%u %s %s", s_cap.w, s_cap.h,
            ok ? "written to" : "failed:", s_cap.path);
    snprintf(s_cap.done, sizeof(s_cap.done), "%s", s_cap.path);
    s_cap.result = ok ? 1 : -1;
}

/* package AN-C: rd_PresentBlank's pass: the output cleared to 0, the
 * overlay on it */
static void blankRecord(RhiCommandList cl, RhiTexture out, RhiState *outState)
{
    rd__Transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = out;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = s_outW;
    p.height = s_outH;
    rhi_CmdBeginRenderPass(cl, &p);
    rhi_CmdEndRenderPass(cl);
    RhiRect box;
    outputBox(s_outW, s_outH, &box);
    overlayRecord(cl, out, &box, false);
    if (s_window) {
        rd__Transition(cl, out, outState, RHI_STATE_PRESENT);
    }
}

void rd__PresentRecord(RhiCommandList cl)
{
    if (s_blank) {
        blankRecord(cl, s_window ? s_backbuffer : g_rd.presentOut,
                    s_window ? &s_backbufferState : &g_rd.presentOutState);
        return;
    }
    RdTargetRec *disp = rd__TargetRec(RD_TARGET_DISPLAY + 1);
    if (!disp || !disp->color.id) {
        return;
    }
    const RdPresentPreset *pr = &s_present;
    RhiTexture out = s_window ? s_backbuffer : g_rd.presentOut;
    RhiState *outState = s_window ? &s_backbufferState : &g_rd.presentOutState;

    RhiTexture src = disp->color;
    uint32_t sw = disp->tw, sh = disp->th;
    rd__Transition(cl, disp->color, &disp->colorState, RHI_STATE_SHADER_READ);
    RhiRect box;
    outputBox(s_outW, s_outH, &box);
    /* package CRT: the filter draws the box from DISPLAY's own lines (its
     * scanlines are the PS2's field lines), in place of steps 1 and 2; off,
     * or when it cannot draw, nothing below changes */
    const int mirror = pr->mirror && rd__MirrorOn();
    bool filtered = false, uiInPicture = false;
    if (rd__CrtOn()) {
        if (depthWanted()) {
            /* package R1: the filter replaces the box blit that carries it */
            rd__LogOnce(RD_ONCE_EFFECTS_DEPTH_CRT,
                        "effects depth is not available with the CRT filter");
        }
        rd__Transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
        const bool ui = rd__OverlayGridPending();
        bool capOk = true;
        if (ui && rd__CaptureArmed()) {
            /* package PHOTO: a capture never carries the port's UI; under
             * the CRT filter that UI is inside the filtered picture, so the
             * capture takes a pass without it and the shown picture a
             * second pass with it */
            capOk = rd__CrtRecord(cl, disp, out, s_outFormat, s_outW, s_outH, &box, mirror, false);
            if (capOk) {
                captureRecord(cl, out, outState);
            }
        }
        /* a failed capture pass: the box blit below, the capture before the
         * overlay as without the filter */
        filtered =
            capOk && rd__CrtRecord(cl, disp, out, s_outFormat, s_outW, s_outH, &box, mirror, true);
        uiInPicture = filtered && ui;
    }
    /* the full-height scene: DISPLAY already has every line */
    if (!filtered && pr->lineDouble && !g_rd.fullHeight) {
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
    if (!filtered) {
        rd__Transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
        /* package R1: with the effects depth when asked for and an
         * effects program is loaded (never under the CRT filter, even when
         * it could not draw) */
        const bool depth = depthWanted() && !rd__CrtOn() &&
                           depthBlit(cl, src, sw, sh, out, s_outFormat, s_outW, s_outH, &box,
                                     pr->scaleFilter, mirror);
        if (!depth) {
            blit(cl, src, sw, sh, out, s_outFormat, s_outW, s_outH, RHI_LOAD_CLEAR, &box,
                 pr->scaleFilter, mirror);
        }
    }
    /* package DEF: the deferred text, drawn in list order with the regions
     * and colours the passes after it gave it.  Package CRT2: none under the CRT filter (the rows are
     * in the scene, filtered with it) */
    textRecord(cl, out);
    /* ==== INSERTION POINT for later presentation passes ====================
     * After the box (the blit) and the deferred text, before the overlay
     * (the port's UI stays sharp above it).  Each one is a pass on `out`
     * (RHI_STATE_RENDER_TARGET at this point; a pass that samples the
     * picture copies it first or blits from `src`, `box`) with
     * rd__FrameGroup(s_outW, s_outH, ...) for its FrameCB, as blit() does.
     * Keep the overlay last.  Under the CRT filter (packages CRT, CRT2) the
     * filter is the last pass: the overlay is already inside it, and a pass
     * here would draw over the tube.
     * ======================================================================= */
    /* package PHOTO: a capture takes the picture as shown, without the
     * port's own UI on the overlay (the popups, the photo HUD); under the
     * CRT filter with UI in the grid it was taken above, from a pass
     * without that UI (a no-op here then) */
    captureRecord(cl, out, outState);
    overlayRecord(cl, out, &box, uiInPicture);
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
    if (s_cap.tex.id) {
        rhi_DestroyTexture(s_cap.tex); /* package PHOTO */
    }
    memset(&s_cap, 0, sizeof(s_cap));
    depthShutdown();   /* package R1 */
    rd__CrtShutdown(); /* package CRT */
    /* the overlay's prims (the registration stays), the deferred text's list */
    free(s_ov.v);
    free(s_ov.b);
    free(s_text.p);
    s_text.p = NULL;
    s_text.n = s_text.cap = 0;
    s_ov.v = NULL;
    s_ov.b = NULL;
    s_ov.vCount = s_ov.vCap = s_ov.bCount = s_ov.bCap = s_ov.textBatches = 0;
}

bool rd_ReadPresented(void *dst, uint32_t *w, uint32_t *h)
{
    /* the window build presents the swapchain image and keeps no copy */
    if (!g_rd.hasDevice || !dst || s_window || rhi_SwapchainFormat() != RHI_FMT_UNKNOWN) {
        return false;
    }
    return rd__ReadPresent(dst, (size_t)g_rd.presentOutW * g_rd.presentOutH * 4, w, h);
}

/* the output size in the settings and the pending settings (both, so a
 * pending rd_SetSettings does not take it back at the next rd_BeginFrame) */
static void setOutputSize(uint32_t width, uint32_t height)
{
    g_rd.settings.outputWidth = width;
    g_rd.settings.outputHeight = height;
    if (!g_rd.settingsPending) {
        g_rd.pendingSettings = g_rd.settings;
    }
    g_rd.pendingSettings.outputWidth = width;
    g_rd.pendingSettings.outputHeight = height;
}

void rd_ResizeOutput(uint32_t width, uint32_t height)
{
    if (!g_rd.inited || width == 0 || height == 0) {
        return;
    }
    setOutputSize(width, height);
    if (g_rd.hasDevice && rhi_SwapchainFormat() != RHI_FMT_UNKNOWN) {
        rhi_ResizeSwapchain(width, height, g_rd.settings.vsync != 0);
    }
    /* R7a: a scene sized by the window (resolution "window", the
     * Enhanced flag) follows it at the next rd_BeginFrame
     * (rd__ApplyDisplay recreates the targets if their scale changed; a
     * fixed scale or size never changes) */
    g_rd.settingsPending = true;
}

/* v0.4.2 N1: the output follows the swapchain.  The backend rebuilds the
 * swapchain at the surface's size on its own (rhi_SwapchainSize), and the
 * window's size event may come later or never (Android); an output size
 * other than the image's drew the picture and the movie's 4:3 box for the
 * wrong size (offset, scaled, cropped).  Called with an image acquired, so
 * the swapchain is not rebuilt again here (rd_ResizeOutput would wait for
 * the GPU and destroy the image being drawn): only the settings change,
 * and the scene's targets follow at the next rd_BeginFrame. */
static bool s_followed;
static uint32_t s_followW, s_followH;

void rd__OutputFollowSwapchain(void)
{
    uint32_t sw = 0, sh = 0;
    if (!g_rd.inited || !rhi_SwapchainSize(&sw, &sh) || !sw || !sh) {
        return;
    }
    if (sw == g_rd.settings.outputWidth && sh == g_rd.settings.outputHeight) {
        return;
    }
    fprintf(stderr, "render: output follows the swapchain %ux%u (was %ux%u)\n", sw, sh,
            g_rd.settings.outputWidth, g_rd.settings.outputHeight);
    setOutputSize(sw, sh);
    g_rd.settingsPending = true;
    s_followed = true;
    s_followW = sw;
    s_followH = sh;
}

bool rd_OutputFollowed(uint32_t *w, uint32_t *h)
{
    if (!s_followed) {
        return false;
    }
    s_followed = false;
    if (w) {
        *w = s_followW;
    }
    if (h) {
        *h = s_followH;
    }
    return true;
}
