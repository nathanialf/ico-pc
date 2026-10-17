/* rd_present.c: DISPLAY to the output.
 *
 * The PS2 picture: the reduced DISPLAY frame
 * (W x H/2, W the native frame's width: 512 at 4:3) is shown in a centred
 * rectangle of the output of the frame's aspect, each line doubled
 * (field-line doubling, nearest vertically) and filtered bilinearly
 * horizontally.  Two blits:
 *   1. DISPLAY -> lines target (W x H), nearest: exact line doubling;
 *   2. lines target -> the box of the output, bilinear, black outside: the
 *      frame's exact width wExact (rd__frame_wexact; the box's aspect
 *      wExact / 384), the (W - wExact) / 2 columns past it at each side
 *      left out of the source rectangle (less than 8 pixels a side; none
 *      at 4:3 or 48:9).  The same with Full pixel on and off: off, the
 *      game's border is a black mask over that picture at its own scale
 *      (RdSettings.borderMask, rd__border_mask: 2 GS columns at the sides,
 *      the reduction's rows at the top and bottom), where the PS2's
 *      reduction scissor (rd_post.c postReduction) held them black.
 *      The mask is drawn on a cropped frame alone (rd__frame_cropped); at
 *      4:3 and wherever the frame is its exact width (48:9) Full pixel off
 *      is the GS's own DISPLAY, its border the reduction's, as on the PS2.
 * The output is the swapchain backbuffer with a window, or a headless RGBA8
 * texture of RdSettings.outputWidth x outputHeight (tests, the replay tool;
 * none when either is 0).
 *
 * The display options each apply on their own; the preset is only the
 * host's shortcut over them.
 * RdPresentPreset holds the present's filters and its mirror (the
 * interpolation, rd_interp.c, is not one: it presents several times per
 * tick through rd_present with RdSettings.interpolate, each present
 * replaying the frame blended from the retained pair, and this step is the
 * same either way: it shows whatever DISPLAY the replay left):
 *   mirror        step 2 may flip x (the mirror mode is a gameplay
 *                 option, not a display one); flipped when
 *                 rd__mirror_on (rd.h rd_set_mirror, RdSettings.mirror).  Every
 *                 present goes through here, the interpolated ones
 *                 (rd_present) included; UI prims were flipped at replay
 *                 (rd_replay.c mirrorUi) so they read normally
 * The box takes the frame's exact width (the last frame camera's aspect,
 * else the aspect option g_rd.outAspect; 4:3 for a 512-wide frame and a
 * zeroed RdSettings); the frame's width is GsBase.c's native frame
 * (gsFrame), drawn as the game projects it.  With the full-height
 * option DISPLAY's texture has the scene's height (rd__target_scale_of) and
 * step 1 is skipped.
 * The scene resolution needs nothing here: DISPLAY's texture is whatever
 * size rd__apply_display gave it, and both steps sample it normalised.
 * rd__apply_display (below) turns RdSettings into the scales and factors the
 * targets and the replay use.
 * Field parity is not a present-time effect: the PS2 shifts the scene's
 * XYOFFSET by half a line from the field bit (sceGsSetHalfOffset), which
 * rd_frame_flip records into the frame head (RD_TARGET_HALF_Y).
 *
 * The overlay (rd.h rd_set_present_overlay): after step 2, the prims the
 * registered callback gave for this present are drawn on the output in step
 * 2's pass (left open for them and the deferred text; a load-preserving
 * pass of their own under the CRT filter or the effects depth), one 12.4
 * unit a sixteenth of an output pixel, unflipped.  The callback runs before
 * the frame's replay (rd__overlay_collect, from replayFrame) so the textures
 * it touches upload with the frame; only the drawing is here.
 * With no callback registered nothing below step 2 runs, and the output is
 * byte for byte the picture without an overlay.
 *
 * Deferred text (rd.h rd_deferred_text): in the Enhanced preset with a
 * renderer registered, rd__overlay_collect first walks the frame's
 * RDC_OVERLAY_TEXT items and the post passes after them, and has the renderer
 * lay each item out on the output (font.c's overlay mode) in its region; the
 * replay skips the items' glyph quads, and textRecord draws the prims after
 * step 2, before the overlay.  In the Original preset nothing is collected
 * and the quads draw.
 *
 * The blank present (rd.h rd_present_blank): an empty keep frame replayed with
 * s_blank set: rd__overlay_collect gives the overlay the output's context (no
 * CRT grid, no deferred text) and rd__present_record clears the output and
 * draws the overlay on it, without DISPLAY.
 *
 * The CRT filter (rd_crt.c): with RdSettings.crtMode set and a strength above
 * 0, the scene renders at 1x (rd__apply_display), no text is deferred (the
 * rows draw as quads into the scene, as in the Original preset), the
 * overlay's prims are laid out on the filter's source grid and drawn into it,
 * and rd__crt_record draws the box from DISPLAY in place of steps 1 and 2 (no
 * line doubling: the scanlines are DISPLAY's own lines), cropped as step 2
 * crops but to whole GS pixels, in a box of that grid's shape (rd__crt_box;
 * the 4:3 box at 512).  The filter is the
 * present's last pass but for the top layer below.  Off, steps 1 and 2 draw
 * the box.
 *
 * The top layer (rd.h rd_set_present_overlay_top): a second callback collected
 * after the overlay's, always laid out on the output; its prims are drawn on
 * the output under the overlay's without the CRT filter and over the filtered
 * picture with it, so the touch controls stay sharp.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

typedef struct RdPresentPreset {
    RdFilter doubleFilter; /* step 1 */
    RdFilter scaleFilter;  /* step 2 */
    int lineDouble;        /* step 1, unless the full-height option is on */
    int mirror;            /* step 2 flips x when the mirror mode is on */
} RdPresentPreset;

#define RD_ASPECT_43 (4.0f / 3.0f)

void rd__present_box(uint32_t outW, uint32_t outH, float aspect, RhiRect *box)
{
    uint32_t h = outH, w;
    if (!(aspect > RD_ASPECT_43 + 1e-4f)) {
        /* 4:3, the Original box (integer arithmetic) */
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

static int32_t roundPx(float v)
{
    return (int32_t)floorf(v + 0.5f);
}

/* one for every preset: the display options apply on their own
 * (rd__apply_display) */
static const RdPresentPreset s_present = {RD_FILTER_NEAREST, RD_FILTER_LINEAR, 1, 1};

static float clampAspect(float a)
{
    if (!(a > RD_ASPECT_43 + 1e-4f)) {
        return RD_ASPECT_43;
    }
    return a > ICO_FRAME_ASPECT_MAX ? ICO_FRAME_ASPECT_MAX : a;
}

/* The exact width of a frame gsW wide for a display aspect: 512 exactly at
 * 512 (4:3: the Original integer box and no crop, whatever the aspect
 * asked), else 384 aspect (512 k) held to gsW. */
static float wexactFor(float aspect)
{
    const uint32_t gw = g_rd.gsW ? g_rd.gsW : ICO_FRAME_BASE_W;
    if (gw == ICO_FRAME_BASE_W) {
        return (float)ICO_FRAME_BASE_W;
    }
    const float we = 384.0f * clampAspect(aspect);
    return we < (float)gw ? we : (float)gw;
}

float rd__frame_wexact(void)
{
    const uint32_t gw = g_rd.gsW ? g_rd.gsW : ICO_FRAME_BASE_W;
    if (gw != ICO_FRAME_BASE_W && g_rd.frameWExact >= (float)ICO_FRAME_BASE_W) {
        return g_rd.frameWExact < (float)gw ? g_rd.frameWExact : (float)gw;
    }
    return wexactFor(g_rd.frameAspect > 0.0f ? g_rd.frameAspect : g_rd.outAspect);
}

bool rd__frame_cropped(void)
{
    const uint32_t gw = g_rd.gsW ? g_rd.gsW : ICO_FRAME_BASE_W;
    return gw != ICO_FRAME_BASE_W && rd__frame_wexact() < (float)gw;
}

/* step 2's box in an outW x outH output: the frame's exact width over its
 * height, wExact / 384 (4:3 exactly at 512), with Full pixel on and off */
static void outputBox(uint32_t outW, uint32_t outH, RhiRect *box)
{
    const float we = rd__frame_wexact();
    rd__present_box(outW, outH, we == (float)ICO_FRAME_BASE_W ? RD_ASPECT_43 : we / 384.0f, box);
}

/* output pixels a GS pixel across in a box boxW wide showing we columns:
 * boxW / we, held to a 1/4096 step when that keeps the box's width
 * (2.8125 = 11520 / 4096 exactly, where boxW / we in float may be a last
 * bit off) */
static double boxScale(uint32_t boxW, float we)
{
    double k = (double)boxW / (double)we;
    const double kq = floor(k * 4096.0 + 0.5) / 4096.0;
    if (floor((double)we * kq + 0.5) == (double)boxW) {
        k = kq;
    }
    return k;
}

bool rd__border_mask(const RhiRect *box, float cols, uint32_t gsH, RhiRect *inner)
{
    *inner = *box;
    /* only where step 2 crops the frame (rd__frame_cropped: 16:9 and the
     * other aspects whose exact width is under gsW), which cuts the game's
     * own border away; at 4:3 and wherever the frame is its exact width
     * (48:9) the replay keeps the reduction's scissor (rd_replay.c
     * borderOpen) and the picture is the GS's own border, the PS2's */
    if (!g_rd.settings.borderMask || g_rd.settings.fullPixel || !(cols > 0.0f) ||
        !rd__frame_cropped()) {
        return false;
    }
    /* the reduction's scissor (rd_post.c postReduction): columns 2 ..
     * W - 3 and rows crop .. H / 2 - 1 - crop of DISPLAY's H / 2 lines, so
     * 2 GS columns at each side and crop / (H / 2) of the box's height at
     * the top and bottom */
    const uint32_t h = gsH ? gsH : 448u;
    const int32_t side = roundPx((float)(2.0 * boxScale(box->w, cols)));
    const int32_t rows = roundPx((float)box->h * 2.0f * (float)rd__reduction_crop(h) / (float)h);
    if (2 * side >= (int32_t)box->w || 2 * rows >= (int32_t)box->h) {
        return false;
    }
    inner->x = box->x + side;
    inner->y = box->y + rows;
    inner->w = box->w - 2u * (uint32_t)side;
    inner->h = box->h - 2u * (uint32_t)rows;
    return true;
}

/* the UI box's left edge and width in a presentation box, exactly: the
 * box's own width unless it is wider than 4:3, else its height's 4:3 */
static void uiBoxF(const RdRect *b, float *left, float *w)
{
    const float bw = (float)b->w, bh = (float)b->h;
    *w = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    *left = (float)b->x + (bw - *w) * 0.5f;
}

RdRect rd_ui_box_of(const RdRect *present)
{
    RdRect o = *present;
    /* the 4:3 width of the box's height with rd__present_box's rounding: a
     * box built for 4:3 is its own UI box whatever the rounding */
    const uint64_t w43 = ((uint64_t)present->h * 4u + 1u) / 3u;
    if (present->w > w43) {
        float left, w;
        uiBoxF(present, &left, &w);
        o.w = (uint32_t)(w + 0.5f);
        o.x = (int32_t)floorf(left + 0.5f);
    }
    return o;
}

RdRect rd_ui_box(void)
{
    RhiRect b;
    outputBox(g_rd.settings.outputWidth, g_rd.settings.outputHeight, &b);
    const RdRect p = {b.x, b.y, b.w, b.h};
    return rd_ui_box_of(&p);
}

void rd__ui_box(uint32_t outW, uint32_t outH, RhiRect *box)
{
    outputBox(outW, outH, box);
    const RdRect p = {box->x, box->y, box->w, box->h};
    const RdRect u = rd_ui_box_of(&p);
    *box = (RhiRect){u.x, u.y, u.w, u.h};
}

/* The texels of a source sw wide that step 2 leaves out at each side: the
 * frame's columns past its exact width, (gsW - wExact) / 2 GS pixels at
 * sw / gsW texels each (the CRT filter crops the same columns rounded to
 * whole GS pixels: rd_crt.c rd__crt_grid).  0 at 512 (4:3).  disp:
 * DISPLAY, of which the source is a copy as wide (sw texels); a Full pixel
 * DISPLAY (ox) holds the exact width and its guard texels, which are left
 * out (RD_FULL_PIXEL_GUARD each side, rd__target_scale_of). */
static float sourceCrop(const RdTargetRec *disp, uint32_t sw)
{
    if (disp && disp->ox > 0.0f && disp->tw > 2u * RD_FULL_PIXEL_GUARD) {
        return (float)RD_FULL_PIXEL_GUARD * (float)sw / (float)disp->tw;
    }
    const uint32_t gw = g_rd.gsW ? g_rd.gsW : ICO_FRAME_BASE_W;
    const float we = rd__frame_wexact();
    /* Full pixel (frameCropWe) where DISPLAY is not cropped (no ox:
     * rd__target_scale_of crops a scaled target only): nothing is left out
     * but what a narrower frame than the targets were made for leaves (none
     * in the game: the frame's exact width is the option's aspect's) */
    const float held = g_rd.frameCropWe > 0.0f ? g_rd.frameCropWe : (float)gw;
    if (gw == ICO_FRAME_BASE_W || !(we < held)) {
        return 0.0f;
    }
    return (held - we) * 0.5f * (float)sw / held;
}

bool rd__apply_display(void)
{
    const RdSettings *st = &g_rd.settings;
    /* every option applies whatever the preset; a zeroed
     * RdSettings is the PS2 picture (scale 0 and no size is the GS size
     * unless the Enhanced flag asks for the output's box, below).  The GS
     * size is the native frame's (gsW: GsBase.c gsFrame.w, as wide as the
     * aspect asks), so a scale is the resolution's alone, the same both
     * ways */
    const float aspect = clampAspect(st->aspect);
    const float gw = (float)g_rd.gsW, gh = (float)g_rd.gsH;
    float w, h;
    /* the CRT filter shows the PS2's pixels, so the scene
     * renders at 1x while it is on, whatever the resolution asks (which
     * takes effect again with the filter off) */
    const int crtLock = rd__crt_on() && st->sceneScale != 1.0f;
    const float scale = rd__crt_on() ? 1.0f : st->sceneScale;
    /* The window (the Enhanced flag, the "window" resolution, the frame
     * wider than its exact width; Full pixel on or off, which differ in the
     * border mask alone, rd__border_mask): the scene is the box's own pixels, one
     * texel an output pixel, with no frame rounded to whole GS pixels
     * around it and nothing cropped.  The scale is the box's width over the
     * frame's exact width (2.8125 at 1080p 16:9, as 1935 / 688 is) and the
     * scene-class targets hold that exact width alone (cropWe:
     * rd_core.c rd__target_scale_of) */
    float cropWe = 0.0f;
    if (scale > 0.0f) {
        h = gh * scale;
        w = gw * scale;
    } else if (st->sceneWidth && st->sceneHeight) {
        w = (float)st->sceneWidth;
        h = (float)st->sceneHeight;
    } else if (st->preset == RD_PRESET_ENHANCED && st->outputWidth && st->outputHeight) {
        /* "window": the presentation box, one texel an output pixel.  Only
         * with the Enhanced flag, so a zeroed RdSettings (the tests', the
         * replay tool's default) stays the GS size; the host sends scale 1
         * for the Original rows.  The box shows the frame's exact width, so
         * the texture is gsW / wExact of the box across (1 at 4:3); the box
         * keeps the PS2's pixel shape (7:6 at 448 lines) */
        RhiRect b;
        rd__present_box(st->outputWidth, st->outputHeight, aspect, &b);
        const float we = wexactFor(aspect);
        if (we < gw) {
            /* the frame's width at the box's scale: b.w / we texels a GS
             * pixel (boxScale), so the 4:3 columns are the 4:3 box's */
            w = (float)((double)gw * boxScale(b.w, we));
            cropWe = we;
        } else {
            /* the frame is its exact width (4:3, 48:9): one texel an
             * output pixel, w is the box's width */
            w = (float)b.w;
        }
        h = (float)b.h;
    } else {
        w = gw;
        h = gh;
    }
    /* supersampling (RdSettings.antiAlias, rd__aa_mode: never under the
     * Original preset): the scene at 2 or 4 times the Resolution row's
     * size, before the GPU's limit below (and the halving fallback after
     * it, createNamedTargets); the presenter box-reduces DISPLAY back
     * (rd__present_record).  Not under the CRT filter, which shows the
     * PS2's pixels at 1x */
    const int aaMode = rd__aa_mode();
    const float ssaaAsked = rd__crt_on() ? 1.0f : (float)rd__aa_ssaa_factor(aaMode);
    float ssaa = ssaaAsked;
    float perPx = 0.0f; /* the Resolution row's texels an output pixel, 0 unknown */
    if (ssaa > 1.0f && st->outputWidth && st->outputHeight) {
        /* only as much as the picture lacks: a scene that already has
         * the factor's texels an output pixel both ways is supersampled
         * as it stands (8x at 48:9 on a 3840x2160 display is over 4 texels
         * a pixel each way).  The rule: t = the smaller of the two axes'
         * texels an output pixel (across: the frame's exact width of w over
         * the box's width; down: DISPLAY's rows, h or h / 2 at half
         * height, over the box's height); the factor applied is
         * ceil(factor / t), at least 1 and at most the factor */
        RhiRect b;
        rd__present_box(st->outputWidth, st->outputHeight, aspect, &b);
        const float tx = w * (wexactFor(aspect) / gw) / (float)b.w;
        const float ty = (st->fullHeightScene ? h : h * 0.5f) / (float)b.h;
        perPx = tx < ty ? tx : ty;
        if (perPx > 0.0f) {
            const float need = ceilf(ssaa / perPx - 1e-4f);
            ssaa = need < 1.0f ? 1.0f : (need > ssaa ? ssaa : need);
        }
    }
    if (ssaa > 1.0f) {
        w *= ssaa;
        h *= ssaa;
    }
    /* from the GS size up to the GPU's largest render target, both axes scaled
     * together so the shape is kept (16384 where there is no device) */
    float lim = g_rd.hasDevice ? (float)rhi_limits()->maxRenderTargetSize : 16384.0f;
    if (g_rd.hasDevice && rhi_limits()->tiler && lim > 4096.0f) {
        lim = 4096.0f; /* phones share memory with the system: a big scene could
                          get the app killed before the allocation reports failure */
    }
    if (w > lim) {
        h *= lim / w;
        w = lim;
    }
    if (h > lim) {
        w *= lim / h;
        h = lim;
    }
    /* MSAA on a tile-based GPU: the multisampled scene pair (4 + 8 bytes a
     * sample) shares the phone's memory, so the scale halves (both axes, not
     * below the GS size) until the pair fits RD_MSAA_TILER_BUDGET */
    if (g_rd.hasDevice && rhi_limits()->tiler && rd_aa_samples(aaMode) > 1u) {
        const double per = 12.0 * (double)rd_aa_samples(aaMode);
        while ((double)w * (double)h * per > (double)RD_MSAA_TILER_BUDGET && w > gw * 1.5f &&
               h > gh * 1.5f) {
            w *= 0.5f;
            h *= 0.5f;
        }
    }
    const float sx = w / gw < 1.0f ? 1.0f : w / gw;
    const float sy = h / gh < 1.0f ? 1.0f : h / gh;
    uint8_t filter = st->filterUpgrade <= RD_FILTER_UPGRADE_ANISOTROPIC ? st->filterUpgrade : 0;
    if (g_rd.hasDevice && !rhi_limits()->textureMips) {
        filter = 0;
    }
    const uint8_t full = st->fullHeightScene != 0;
    const uint32_t vs = st->vsync ? 2u : 1u;
    /* compared with what the options asked for last time, not with what the
     * allocation fallback (createNamedTargets) settled on, so an unrelated
     * change does not recreate the targets just to fail the same way again */
    const bool changed = sx != g_rd.sceneReqSx || sy != g_rd.sceneReqSy ||
                         full != g_rd.fullHeight || cropWe != g_rd.frameCropWe;
    /* one line when what the options give differs from what was in force:
     * this runs on a settings change or a resize (rd_begin_frame's
     * settingsPending) and at init, never per frame */
    if (changed || aspect != g_rd.outAspect || filter != g_rd.filterUpgrade ||
        vs != g_rd.vsyncApplied) {
        char ssaaNote[96] = "";
        if (ssaaAsked > 1.0f && ssaa > 1.0f && ssaa < ssaaAsked) {
            snprintf(ssaaNote, sizeof(ssaaNote),
                     " (at %gx: the resolution has %.2f texels an output pixel)", (double)ssaa,
                     (double)perPx);
        } else if (ssaaAsked > 1.0f && !(ssaa > 1.0f)) {
            snprintf(ssaaNote, sizeof(ssaaNote),
                     " (skipped: the resolution has %.2f texels an output pixel)", (double)perPx);
        }
        /* the scene's texels (SCENE's texture: the frame's exact width
         * alone with Full pixel, rd__target_scale_of) */
        const float sceneW = (cropWe > 0.0f ? cropWe : gw) * sx;
        /* MSAA is asked for here; the fog's self-test settles it later
         * (rd__fog_aa_reconcile) and logs "msaa: off" when the device cannot */
        rd__log("display: scene %gx%g (%.0fx%.0f)%s, aspect %.3f, filter %u, %s height, "
                "full pixel %s, vsync %s%s%s%s",
                (double)sx, (double)sy, floor((double)sceneW + 0.5), floor((double)(gh * sy) + 0.5),
                crtLock ? " (CRT: 1x)" : "", (double)aspect, (unsigned)filter,
                full ? "full" : "half", st->fullPixel ? "on" : "off", st->vsync ? "on" : "off",
                aaMode == RD_AA_OFF ? "" : rd__aa_label(aaMode),
                rd_aa_samples(aaMode) > 1u ? " (asked)" : "", ssaaNote);
    }
    if (changed) {
        g_rd.frameCropWe = cropWe;
        g_rd.sceneReqSx = sx;
        g_rd.sceneReqSy = sy;
        g_rd.sceneSx = sx;
        g_rd.sceneSy = sy;
        g_rd.sceneFellBack = false;
    }
    g_rd.ssaa = ssaa;
    g_rd.outAspect = aspect;
    g_rd.filterUpgrade = filter;
    g_rd.fullHeight = full;
    /* vsync: the swapchain's present mode (Vulkan FIFO, else MAILBOX or
     * IMMEDIATE; D3D12 the sync interval) */
    if (g_rd.vsyncApplied && g_rd.vsyncApplied != vs && g_rd.hasDevice &&
        rhi_swapchain_format() != RHI_FMT_UNKNOWN && st->outputWidth && st->outputHeight) {
        rhi_wait_idle();
        rhi_resize_swapchain(st->outputWidth, st->outputHeight, st->vsync != 0);
    }
    g_rd.vsyncApplied = vs;
    return changed;
}

/* rd_display_probe's texture: one texel per point, DISPLAY's format */
static struct {
    RhiTexture tex;
    RhiState state;
    RhiFormat format;
} s_probe;

static RhiTexture s_backbuffer;

static RhiState s_backbufferState;

static RhiFormat s_outFormat;

static uint32_t s_outW, s_outH;

static bool s_window;

bool rd__present_acquire(void)
{
    s_backbuffer = (RhiTexture){0};
    s_window = rhi_swapchain_format() != RHI_FMT_UNKNOWN;
    if (s_window) {
        s_backbuffer = rhi_acquire_backbuffer();
        if (!s_backbuffer.id) {
            rhi_resize_swapchain(g_rd.settings.outputWidth, g_rd.settings.outputHeight,
                                 g_rd.settings.vsync != 0);
            s_backbuffer = rhi_acquire_backbuffer();
        }
        if (!s_backbuffer.id) {
            return false;
        }
        rd__output_follow_swapchain(); /* the image's own size */
        s_backbufferState = RHI_STATE_UNDEFINED;
        s_outFormat = rhi_swapchain_format();
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
            rhi_destroy_texture(g_rd.presentOut);
        }
        g_rd.presentOut = rhi_create_texture(&(RhiTextureDesc){
            w, h, 1, RHI_FMT_RGBA8_UNORM,
            RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC, "rd present out", 0});
        g_rd.presentOutState = RHI_STATE_UNDEFINED;
        g_rd.presentOutW = w;
        g_rd.presentOutH = h;
    }
    s_outFormat = RHI_FMT_RGBA8_UNORM;
    s_outW = w;
    s_outH = h;
    return g_rd.presentOut.id != 0;
}

/* The source window of a blit that leaves crop texels out at each side of a
 * source sw wide, into a box box_w wide: u0 .. u1 (mirrored: swapped by the
 * caller).  Whole texels (u0 = floor(crop + 1/2), u1 = u0 + box_w, at most
 * half an output pixel of shift) when the scene's density is the box's, so
 * every output pixel is one texel and a linear filter blends nothing: the
 * frame's exact width over the native width is a fraction of a texel at
 * 16:9 (crop 7.5), where the symmetric window puts every pixel centre on a
 * texel boundary.  Otherwise (and at 4:3, crop 0) crop .. sw - crop. */
static bool snapWindow(uint32_t sw, float crop, uint32_t boxW, float *u0, float *u1)
{
    const float c = crop > 0.0f ? crop : 0.0f;
    if (c > 0.0f && fabsf(((float)sw - 2.0f * c) - (float)boxW) < 0.0625f) {
        /* the tie (crop k + 1/2, as at 1920 across 16:9) rounds up whatever
         * the float error of the exact width (7.49997 for 16:9 as a float):
         * the 1/64 is far below the density test's 1/16 */
        *u0 = floorf(c + 0.5f + 1.0f / 64.0f);
        *u1 = *u0 + (float)boxW;
        return true;
    }
    *u0 = c;
    *u1 = (float)sw - c;
    return false;
}

/* src (sw x sh) into box of dst in a pass of its own, crop texels left out
 * at each side of src (sourceCrop; 0: all of it); keepOpen leaves the pass
 * open for the caller to draw more into dst and end it.  sc: the scissor
 * inside the box (the border mask, rd__border_mask: the cleared black
 * stays in the bands, the picture keeps the box's scale), NULL the box */
static void blit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                 RhiFormat dstFmt, uint32_t dw, uint32_t dh, RhiLoadOp load, const RhiRect *box,
                 const RhiRect *sc, RdFilter filter, int mirror, float crop, bool keepOpen)
{
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = load;
    p.colorCount = 1;
    p.width = dw;
    p.height = dh;
    rhi_cmd_begin_render_pass(cl, &p);
    RhiViewport vp = {(float)box->x, (float)box->y, (float)box->w, (float)box->h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    rhi_cmd_set_scissor(cl, sc ? sc : box);
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT, dstFmt);
    RhiPipeline pipe = rd__get_pipeline(&k);
    if (pipe.id) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        for (int i = 0; i < 4; i++) {
            cb.col[i] = 0x80; /* modulate by 1.0: identity */
        }
        cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
        /* mirrored: the source rectangle right to left (blit_vs interpolates
         * u0 + t (u1 - u0)): u = sw (1 - t), exact at the box's pixel
         * centres when the scale is a power of two.  A crop narrows it by
         * as much at each side (none at 4:3: the same constants) */
        float u0, u1;
        snapWindow(sw, crop, box->w, &u0, &u1);
        cb.uvRect[0] = mirror ? u1 : u0;
        cb.uvRect[2] = mirror ? u0 : u1;
        cb.uvRect[3] = (float)sh;
        cb.tex[0] = (float)sw;
        cb.tex[1] = (float)sh;
        cb.tex[2] = 1.0f / (float)sw;
        cb.tex[3] = 1.0f / (float)sh;
        rhi_cmd_set_pipeline(cl, pipe);
        rd__bind_uniform(cl, 0, rd__frame_group(dw, dh, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(
            cl, 2, rd__tex_group(src, rd__sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        rhi_cmd_draw(cl, 3, 0, 1);
    }
    if (!keepOpen) {
        rhi_cmd_end_render_pass(cl);
    }
}

void rd__present_blit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                      RhiFormat dstFmt, uint32_t dw, uint32_t dh, RhiLoadOp load,
                      const RhiRect *box, RdFilter filter, int mirror, float crop)
{
    blit(cl, src, sw, sh, dst, dstFmt, dw, dh, load, box, NULL, filter, mirror, crop, false);
}

/* ------------------------------------------------------ the supersampling
 * With anti-aliasing on (rd__aa_mode, never under the Original preset) and
 * more than one source texel per output pixel both ways (supersampling
 * makes it 2 or 4 at the window's resolution), step 2's source is first
 * box-averaged into a texture of the box's own size (aa_reduce_ps: the mean
 * over each pixel's footprint on a 1/16-texel grid, rounded half up; exact
 * for the integer ratios), and step 2 then draws that texture into the box
 * one texel a pixel, nearest, mirrored as the box blit is.  The crop (sourceCrop) is
 * taken in the reduce.  Off, or at 1 texel a pixel or fewer, nothing here
 * runs and the box blit samples DISPLAY itself.  Without anti-aliasing the
 * same reduce runs when the scene is scaled and has more than two texels a
 * pixel on an axis (a resolution above the window's), where the box blit's
 * one bilinear tap would skip texels; never at scale 1. */
static struct {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;
} s_reduce;

/* the footprint of one of B pixels over an extent of S16 sixteenths
 * (a = S16 / B, b = S16 % B: aa_reduce_ps) */
static void reduceAxis(uint32_t s16, uint32_t b, uint32_t c16, uint32_t out[4])
{
    out[0] = s16 / b;
    out[1] = s16 % b;
    out[2] = b;
    out[3] = c16;
}

/* the reduce into s_reduce; false (nothing recorded) when it does not apply
 * or cannot run.  On true *src, *sw, *sh are the reduced texture's */
static bool reduceRecord(RhiCommandList cl, RhiTexture *src, uint32_t *sw, uint32_t *sh,
                         const RhiRect *box, float crop)
{
    if (box->w == 0 || box->h == 0) {
        return false;
    }
    const float ew = (float)*sw - 2.0f * (crop > 0.0f ? crop : 0.0f);
    /* without anti-aliasing, a scaled scene more than two texels a pixel
     * on an axis (a fixed resolution above the window's): the box blit's
     * one bilinear tap would skip texels there */
    if (rd__aa_mode() == RD_AA_OFF &&
        !((g_rd.sceneSx != 1.0f || g_rd.sceneSy != 1.0f) &&
          (ew > 2.0f * (float)box->w || (float)*sh > 2.0f * (float)box->h))) {
        return false;
    }
    float su0, su1;
    if (snapWindow(*sw, crop, box->w, &su0, &su1)) {
        return false; /* a texel a pixel: the box blit's whole-texel window */
    }
    if (!(ew > (float)box->w && (float)*sh > (float)box->h)) {
        return false; /* one texel a pixel or fewer: the plain box blit */
    }
    if (!s_reduce.tex.id || s_reduce.w != box->w || s_reduce.h != box->h) {
        if (s_reduce.tex.id) {
            rhi_destroy_texture(s_reduce.tex);
        }
        s_reduce.tex = rhi_create_texture(&(RhiTextureDesc){box->w, box->h, 1, RHI_FMT_RGBA8_UNORM,
                                                            RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED,
                                                            "rd present reduce", 0});
        s_reduce.state = RHI_STATE_UNDEFINED;
        s_reduce.w = box->w;
        s_reduce.h = box->h;
    }
    const RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_AA_REDUCE, RHI_FMT_RGBA8_UNORM);
    const RhiPipeline pipe = rd__get_pipeline(&k);
    if (!s_reduce.tex.id || !pipe.id) {
        return false;
    }
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    reduceAxis((uint32_t)(ew * 16.0f + 0.5f), box->w,
               (uint32_t)((crop > 0.0f ? crop : 0.0f) * 16.0f + 0.5f), cb.col);
    reduceAxis(*sh * 16u, box->h, 0u, cb.blend);
    cb.mode[0] = *sw;
    cb.mode[1] = *sh;
    cb.uvRect[2] = cb.uvRect[3] = 1.0f;
    cb.tex[0] = cb.tex[1] = cb.tex[2] = cb.tex[3] = 1.0f;
    rd__transition(cl, s_reduce.tex, &s_reduce.state, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = s_reduce.tex;
    p.color[0].load = RHI_LOAD_DONT_CARE;
    p.colorCount = 1;
    p.width = box->w;
    p.height = box->h;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)box->w, (float)box->h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, box->w, box->h};
    rhi_cmd_set_scissor(cl, &all);
    rhi_cmd_set_pipeline(cl, pipe);
    rd__bind_uniform(cl, 0, rd__frame_group(box->w, box->h, 0.0f, 0.0f));
    rd__bind_uniform(cl, 1, rd__draw_group(&cb));
    rhi_cmd_set_bind_group(cl, 2,
                           rd__tex_group(*src, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                           RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
    rhi_cmd_draw(cl, 3, 0, 1);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, s_reduce.tex, &s_reduce.state, RHI_STATE_SHADER_READ);
    *src = s_reduce.tex;
    *sw = box->w;
    *sh = box->h;
    return true;
}

/* ------------------------------------------------------ the effects depth
 * RdSettings.effectsDepth, with an effects program loaded (depthWanted):
 * step 2 as one pass with two targets, the output
 * and an output-size RHI_FMT_D32F depth buffer cleared to 0.0 (far), drawn
 * by blit_depth_ps: the colour exactly as blit_ps, and SV_Depth the scene's
 * depth at the same normalised source position (SCENE and DISPLAY cover
 * the same GS frame; a mirrored box flips both), read nearest from SCENE's
 * depth as rd_replay.c doFog reads it (rd__sampled_depth: in place, or a
 * copy with g_rd.depthCopy).  Outside the box the
 * clear stays, so the bars read as far.  The point is an effects program
 * hooked into the API (ReShade, vkBasalt): it looks for a depth buffer of
 * the backbuffer's size among the render passes, and the scene's is the
 * scene's size.  The convention is the scene's (gs_z_to_depth): the depth
 * grows with GS Z, near 1 and far 0, so ReShade's
 * RESHADE_DEPTH_INPUT_IS_REVERSED is 1 (docs/RESHADE.md).  The deferred
 * text, the capture and the overlay stay colour-only passes after it; under
 * the CRT filter there is no box blit and no effects depth. */
/* Only for an effects program: the pass runs when the setting is on and
 * rhi_injector_name() names one (ReShade, vkBasalt), never on Android (no
 * effects program hooks the game there); the tests force it (a lavapipe
 * run has no layer). */
static bool s_forceDepth;

void rd__force_effects_depth(bool force)
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
    return rhi_injector_name() != NULL;
#endif
}

static struct {
    RhiTexture out; /* the output-size D32F */
    RhiState outState;
    uint32_t outW, outH;
    int failLogged;
    RdDepthCopy copy; /* SCENE's depth with g_rd.depthCopy */
} s_depth;

void rd__effects_depth_free(void)
{
    rd__depth_copy_free(&s_depth.copy);
}

static void depthShutdown(void)
{
    if (s_depth.out.id) {
        rhi_destroy_texture(s_depth.out);
    }
    rd__depth_copy_free(&s_depth.copy);
    memset(&s_depth, 0, sizeof(s_depth));
}

/* step 2 with the effects depth; false (nothing recorded) when it cannot
 * run, and the plain blit draws the box */
static bool depthBlit(RhiCommandList cl, RhiTexture src, uint32_t sw, uint32_t sh, RhiTexture dst,
                      RhiFormat dstFmt, uint32_t dw, uint32_t dh, const RhiRect *box,
                      const RhiRect *sc, RdFilter filter, int mirror, float crop)
{
    RdTargetRec *ts = rd__target_rec(RD_TARGET_SCENE + 1);
    if (!ts || !ts->withDepth || !ts->depth.id || ts->depthState == RHI_STATE_UNDEFINED) {
        return false;
    }
    if (!s_depth.out.id || s_depth.outW != dw || s_depth.outH != dh) {
        if (s_depth.out.id) {
            rhi_destroy_texture(s_depth.out);
        }
        /* sampled and copyable, for the program that reads it (and the
         * tests' readback) */
        s_depth.out = rhi_create_texture(&(RhiTextureDesc){
            dw, dh, 1, RHI_FMT_D32F, RHI_TEX_DEPTH_STENCIL | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC,
            "rd effects depth", 0});
        s_depth.outState = RHI_STATE_UNDEFINED;
        s_depth.outW = dw;
        s_depth.outH = dh;
    }
    const RdPipeKeyInt k = rd__present_depth_key(dstFmt);
    const RhiPipeline pipe = rd__get_pipeline(&k);
    if (!s_depth.out.id || !pipe.id) {
        if (!s_depth.failLogged) {
            s_depth.failLogged = 1;
            rd__log("present: no effects depth (%s); the picture is shown without it",
                    pipe.id ? "no depth texture" : "no pipeline");
        }
        return false;
    }
    /* SCENE's depth in place, or its copy (g_rd.depthCopy) */
    const RhiTexture zTex = rd__sampled_depth(cl, ts, &s_depth.copy, "rd effects depth copy");
    if (!zTex.id) {
        return false;
    }
    rd__transition(cl, s_depth.out, &s_depth.outState, RHI_STATE_DEPTH_WRITE);

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
    rhi_cmd_begin_render_pass(cl, &p);
    RhiViewport vp = {(float)box->x, (float)box->y, (float)box->w, (float)box->h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    /* blit()'s scissor: the border mask's bands keep the cleared black and
     * the far depth of the bars */
    rhi_cmd_set_scissor(cl, sc ? sc : box);
    /* blit()'s constants, so the colour is blit_ps's bytes */
    IcoDrawCB cb;
    memset(&cb, 0, sizeof(cb));
    for (int i = 0; i < 4; i++) {
        cb.col[i] = 0x80;
    }
    cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
    float u0, u1;
    snapWindow(sw, crop, box->w, &u0, &u1);
    cb.uvRect[0] = mirror ? u1 : u0;
    cb.uvRect[2] = mirror ? u0 : u1;
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
    b[1].sampler = rd__sampler(filter, filter, RD_WRAP_CLAMP, RD_WRAP_CLAMP);
    b[2].slot = 2;
    b[2].type = RHI_BIND_SAMPLED_TEXTURE;
    b[2].texture = zTex;
    b[2].aspect = RHI_ASPECT_DEPTH;
    const RhiBindGroup g2 = rhi_create_bind_group(&(RhiBindGroupDesc){g_rd.layoutTex, b, 3});
    if (g2.id) {
        rhi_cmd_set_pipeline(cl, pipe);
        rd__bind_uniform(cl, 0, rd__frame_group(dw, dh, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2, g2);
        rhi_cmd_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
    return g2.id != 0;
}

bool rd__read_present_depth(float *dst, size_t dstSize, uint32_t *w, uint32_t *h)
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
        RhiCommandList cl = rhi_begin_commands();
        if (!cl.id) {
            return false;
        }
        rd__transition(cl, s_depth.out, &s_depth.outState, RHI_STATE_COPY_SRC);
        rhi_end_commands(cl);
        rhi_submit(cl);
    }
    uint32_t pitch = 0;
    if (!rhi_readback_texture(s_depth.out, RHI_ASPECT_DEPTH, dst, dstSize, &pitch)) {
        return false;
    }
    return pitch == s_depth.outW * sizeof(float);
}

/* ------------------------------------------------------------ the overlay */

typedef struct OverlayBatch {
    uint32_t first, count; /* in s_ov.v */
    uint32_t tex;          /* RdTex id, 0 untextured */
    uint8_t prim, blend;
    RhiRect sc; /* the deferred text's region (the whole output for the overlay's) */
} OverlayBatch;

/* outside g_rd: the registration outlives rd_shutdown / rd_init (port/ui
 * registers once at start-up; the tests restart rd between checks) */
static struct {
    RdOverlayFn fn;
    void *user;
    RdOverlayFn topFn; /* the top layer, on the output after the CRT filter */
    void *topUser;
    RdDeferredTextFn textFn; /* the deferred text's renderer */
    void *textUser;
    int inside;          /* in fn or textFn: rd_overlay_prims keeps prims */
    RhiRect sc;          /* the region rd_overlay_prims gives its batch */
    RdOverlayCtx ctx;    /* the main layer's (the deferred text's too) */
    RdOverlayCtx topCtx; /* the top layer's, always the output */
    RdScreenVtx *v;
    uint32_t vCount, vCap;
    OverlayBatch *b;
    uint32_t bCount, bCap;
    uint32_t textBatches; /* b[0, textBatches) are the deferred text's */
    uint32_t topFirst;    /* b[topFirst, bCount) are the top layer's,
                           * b[textBatches, topFirst) the main layer's */
    int grid;             /* the main layer is on the CRT filter's source grid */
} s_ov;

/* the batches collected for a present, forgotten together */
static void overlayForget(void)
{
    s_ov.vCount = s_ov.bCount = s_ov.textBatches = s_ov.topFirst = 0;
}

/* a present's prims at most (a popup is a few hundred) */
#define RD_OVERLAY_MAX_VERTICES (1u << 20)

void rd_set_present_overlay(RdOverlayFn fn, void *user)
{
    s_ov.fn = fn;
    s_ov.user = fn ? user : NULL;
    overlayForget();
}

void rd_set_present_overlay_top(RdOverlayFn fn, void *user)
{
    s_ov.topFn = fn;
    s_ov.topUser = fn ? user : NULL;
    overlayForget();
}

RdOverlayFn rd_get_present_overlay_top(void **user)
{
    if (user) {
        *user = s_ov.topUser;
    }
    return s_ov.topFn;
}

RdOverlayFn rd_get_present_overlay(void **user)
{
    if (user) {
        *user = s_ov.user;
    }
    return s_ov.fn;
}

/* the present in progress is rd_present_blank's */
static int s_blank;

bool rd_present_blank(void)
{
    if (!g_rd.hasDevice) {
        return false;
    }
    /* lists 11 and 12 of a frame with none: no draw, no target touched */
    static RdFrame empty;
    s_blank = 1;
    const bool ok = rd__replay_frame(&empty, 1, true);
    s_blank = 0;
    return ok;
}

void rd_set_deferred_text_fn(RdDeferredTextFn fn, void *user)
{
    s_ov.textFn = fn;
    s_ov.textUser = fn ? user : NULL;
    overlayForget();
}

bool rd_deferred_text_active(void)
{
    return g_rd.deferText;
}

void rd_overlay_prims(RdPrim type, const RdScreenVtx *v, uint32_t n, RdTex tex, RdBlend blend)
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

/* ---------------------------------------------------------- deferred text
 * The frame's RDC_OVERLAY_TEXT commands are walked in replay order with the
 * state they replay under: an item takes the scissor in force; an op (a post
 * pass recorded after text) changes the items before it as the pass changed
 * the pixels they had been drawn into.  FADE, BRIGHTNESS and the LETTERBOX
 * are lerps of the destination toward a colour, d' = d (1 - k) + C k, affine
 * in d, so a text pixel blended over the scene and then lerped is the lerped
 * scene with the text blended over it in the lerped colour: a lerp item's
 * colour becomes c (1 - k) + C k with its alpha kept, an additive item's (Cs
 * As + Cd) c (1 - k).  The letterbox does that inside its two bands only, so
 * an item is cut into segments by scene line.  KEEP draws DISPLAY over the
 * whole scene without blending: the items before it are gone, as their quads
 * would be.  The REDUCTION's tint scales the colour of either kind (linear;
 * where the GS clamps a tint above 1.0 behind a partly covered pixel, the
 * fold is a little darker).  Then each item is drawn once per segment,
 * clipped to the segment, its scissor and the reduction's border crop (output
 * pixels). */

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

/* the region of segment g of t on the output: its scissor, its lines and
 * the reduction's crop or the border mask (full pixel off), inside the
 * box; false when empty */
static bool textRegion(const TextPending *t, const TextSeg *g, RhiRect *out)
{
    const RdRect *b = &s_ov.ctx.box;
    const RhiRect real = {b->x, b->y, (uint32_t)b->w, (uint32_t)b->h};
    const float bx = (float)real.x, by = (float)real.y, bw = (float)real.w, bh = (float)real.h;
    const float W = (float)t->gsW, H = (float)t->gsH;
    /* the 2D layer's 4:3 box inside the presentation box (rd_ui_box_of's): the
     * frame's centre 512 columns, GS 2048 -+ 256 */
    float left, w43;
    uiBoxF(b, &left, &w43);
    /* the scissor: a side at the target's edge stays at the box's edge,
     * the others move with the UI.  A UI-tagged scissor is in the target's
     * pixels, 512 of which are the UI box (the frame is gsW wide: its centre
     * 512 columns); GS pixel p is (p - (W - 512) / 2) of them */
    const float ox = (W - (float)ICO_FRAME_BASE_W) * 0.5f;
    float x0 = t->sc[0] <= 0 ? bx : left + ((float)t->sc[0] - ox) * w43 / (float)ICO_FRAME_BASE_W;
    float x1 = t->sc[2] >= (int32_t)t->gsW - 1
                   ? bx + bw
                   : left + ((float)(t->sc[2] + 1) - ox) * w43 / (float)ICO_FRAME_BASE_W;
    float y0 = by + (float)t->sc[1] * bh / H;
    float y1 = by + (float)(t->sc[3] + 1) * bh / H;
    /* the reduction's crop (rd_post.c postReduction): 2 pixels left and
     * right of the native frame, 8 lines of DISPLAY's H / 2 top and bottom
     * (2 below 512), which the picture holds black.  With the border mask
     * (Full pixel off in the game on a cropped frame, rd__frame_cropped)
     * that black is the mask's bands at the box's edges, the text clipped
     * to the blit's scissor (rd__border_mask).  With full pixel on the
     * reduction draws the whole frame (rd_replay.c), so there is no black
     * strip to keep the text out of.  Neither (a zeroed RdSettings, or Full pixel off at 4:3 and
     * wherever the frame is its exact width): the reduction's own border in
     * DISPLAY; the box shows the frame's exact width we, (W - we) / 2
     * columns left out at each side (sourceCrop), so what is left of the 2
     * columns is 2 less that, none once the crop passes them */
    RhiRect mask;
    if (rd__border_mask(&real, rd__frame_wexact(), t->gsH, &mask)) {
        x0 = fmaxf(x0, (float)mask.x);
        x1 = fminf(x1, (float)(mask.x + (int32_t)mask.w));
        y0 = fmaxf(y0, (float)mask.y);
        y1 = fminf(y1, (float)(mask.y + (int32_t)mask.h));
    } else if (!g_rd.settings.fullPixel) {
        const float crop = (float)rd__reduction_crop(t->gsH), half = H * 0.5f;
        const float wf = rd__frame_wexact();
        const float we = wf > 0.0f && wf < W ? wf : W;
        const float edge = fmaxf(0.0f, 2.0f - (W - we) * 0.5f);
        x0 = fmaxf(x0, bx + bw * edge / we);
        x1 = fminf(x1, bx + bw * (we - edge) / we);
        y0 = fmaxf(y0, by + bh * crop / half);
        y1 = fminf(y1, by + bh * (half - crop) / half);
    }
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
    /* no item, nothing to walk for: most presents (rd_deferred_text counts
     * them; an interpolated frame adds prev's it inserts, rd__load_frame
     * counts a dump's) */
    if (!f->textItems) {
        return;
    }
    s_text.f = f;
    RdStateBlock st = f->startState;
    rd__walk(f, keep, &st, textWalk, NULL);
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

/* rd__overlay_collect for a game frame (filmBox NULL) or for a film's
 * present (rd__overlay_collect_film: the film's 4:3 box on the output, no
 * CRT grid, no deferred text) */
static void overlayCollect(const RdFrame *f, int keep, const RhiRect *filmBox)
{
    overlayForget();
    s_ov.grid = 0;
    g_rd.deferText = false;
    uint32_t w = g_rd.settings.outputWidth, h = g_rd.settings.outputHeight;
    const uint32_t outW = w, outH = h;
    if ((!s_ov.fn && !s_ov.textFn && !s_ov.topFn) || !w || !h) {
        return;
    }
    /* the output and box rd__present_record will use: both come from
     * g_rd.settings.  One case changes them after this: on the present
     * where rd__output_follow_swapchain takes a rebuilt swapchain's size
     * (after the acquire, later in the replay), the overlay is laid out for
     * the old size for that one frame and follows from the next */
    const RdPresentPreset *pr = &s_present;
    RhiRect box;
    if (filmBox) {
        box = *filmBox;
    } else {
        outputBox(w, h, &box);
    }
    const RhiRect outBox = box;
    /* under the CRT filter the overlay is part of the
     * picture: its context is the filter's source grid at the frame's
     * lines (the 1x frame the game's own UI is drawn in), the box all of
     * it, and rd__crt_record draws the prims into that grid */
    const bool crt = rd__crt_on() && !s_blank && !filmBox;
    if (crt) {
        uint32_t vw, vh;
        rd__crt_grid(&vw, &vh);
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
    c->mirror = pr->mirror && rd__mirror_on();
    /* the deferred text first, so the overlay draws above it; none under
     * the CRT filter (the rows draw as quads into the scene, as in the
     * Original preset, and go through the filter) */
    if (s_ov.textFn && f && !filmBox && g_rd.settings.preset == RD_PRESET_ENHANCED && !crt &&
        !s_blank) {
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
    /* the top layer (the touch controls) last, laid out on
     * the output in every mode, never on the grid: overlayRecord draws it
     * on the output after the CRT filter, so it stays sharp */
    s_ov.topFirst = s_ov.bCount;
    if (s_ov.topFn) {
        RdOverlayCtx *t = &s_ov.topCtx;
        t->outW = outW;
        t->outH = outH;
        t->box = (RdRect){outBox.x, outBox.y, outBox.w, outBox.h};
        t->boxScale = (float)outBox.h / 448.0f;
        t->mirror = c->mirror;
        s_ov.sc = (RhiRect){0, 0, outW, outH};
        s_ov.inside = 1;
        s_ov.topFn(t, s_ov.topUser);
        s_ov.inside = 0;
    }
}

void rd__overlay_collect(const RdFrame *f, int keep)
{
    overlayCollect(f, keep, NULL);
}

void rd__overlay_collect_film(const RhiRect *box)
{
    overlayCollect(NULL, 0, box);
}

uint64_t rd__overlay_ring_bytes(void)
{
    if (!s_ov.bCount) {
        return 0;
    }
    const uint64_t align = rhi_limits()->uniformAlign;
    /* a FrameCB a pass (three at most: the deferred text, the main layer
     * or the CRT filter's two grid passes, the top layer), a
     * DrawCB and the expanded vertices (sprites and points give 6 a prim's
     * 2 or 1) a batch */
    uint64_t total = 3 * ((uint64_t)sizeof(IcoFrameCB) + 2 * align);
    total += (uint64_t)s_ov.bCount * (sizeof(IcoDrawCB) + 2 * align + 16);
    total += (uint64_t)s_ov.vCount * 6 * sizeof(IcoSpriteVertex);
    return total;
}

/* a region of a layer's own frame (ow x oh) scaled into dst, rounded
 * outwards */
static RhiRect scaleRect(RhiRect r, const RhiRect *dst, uint64_t ow, uint64_t oh)
{
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

/* batches [from, to), laid out on c's frame, in one load-preserving pass
 * on out (fmt, tw x th: the output, or the CRT filter's grid layer).  dst
 * NULL: c's frame is out's, 1:1; else it is scaled into dst of out (the
 * grid-mode overlay on the output when the CRT pass could not draw it, see
 * overlayRecord).  inPass: a pass on out is already open (the box blit's,
 * rd__present_record); the batches draw into it and leave it open, with the
 * viewport, scissor and FrameCB set here all the same */
static void drawBatchesAt(RhiCommandList cl, RhiTexture out, RhiFormat fmt, uint32_t tw,
                          uint32_t th, const RdOverlayCtx *c, const RhiRect *dst, uint32_t from,
                          uint32_t to, bool inPass)
{
    const uint32_t ow = c->outW, oh = c->outH;
    if (from >= to || !ow || !oh || (!dst && (ow != tw || oh != th)) ||
        (dst && (!dst->w || !dst->h))) {
        return;
    }
    if (!inPass) {
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = out;
        p.color[0].load = RHI_LOAD_LOAD;
        p.colorCount = 1;
        p.width = tw;
        p.height = th;
        rhi_cmd_begin_render_pass(cl, &p);
    }
    const RhiRect all = {0, 0, ow, oh};
    const RhiRect area = dst ? *dst : all;
    RhiViewport vp = {(float)area.x, (float)area.y, (float)area.w, (float)area.h, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    RhiRect cur = all;
    rhi_cmd_set_scissor(cl, dst ? &area : &cur);
    /* sprite_ui_vs: x / 16 - origin + g_origin.zw = x / 16, so 12.4 output
     * pixels land 1:1 with integers on pixel edges (rd.h rd_overlay_prims) */
    const RdUniform frame = rd__frame_group(ow, oh, 0.5f, 0.5f);
    for (uint32_t i = from; i < to; i++) {
        const OverlayBatch *b = &s_ov.b[i];
        /* the deferred text's batches carry their regions */
        if (memcmp(&b->sc, &cur, sizeof(cur)) != 0) {
            cur = b->sc;
            if (dst) {
                const RhiRect sc = scaleRect(cur, dst, ow, oh);
                rhi_cmd_set_scissor(cl, &sc);
            } else {
                rhi_cmd_set_scissor(cl, &cur);
            }
        }
        rd__overlay_draw(cl, fmt, frame, b->prim, s_ov.v + b->first, b->count, b->tex, b->blend);
    }
    if (!inPass) {
        rhi_cmd_end_render_pass(cl);
    }
}

static void drawBatches(RhiCommandList cl, RhiTexture out, RhiFormat fmt, uint32_t ow, uint32_t oh,
                        const RdOverlayCtx *c, uint32_t from, uint32_t to, bool inPass)
{
    drawBatchesAt(cl, out, fmt, ow, oh, c, NULL, from, to, inPass);
}

/* the deferred text, at the insertion point (inPass as for
 * drawBatchesAt) */
static void textRecord(RhiCommandList cl, RhiTexture out, bool inPass)
{
    if (!s_ov.grid) {
        drawBatches(cl, out, s_outFormat, s_outW, s_outH, &s_ov.ctx, 0, s_ov.textBatches, inPass);
    }
}

/* the overlay on the output.  The main layer: under the CRT filter it was
 * drawn into the filter's grid (inPicture), and this only forgets it; a
 * grid-mode main layer the filter did not draw (rd__crt_record failed, or the
 * capture's pass left it out and the second pass failed) is drawn on the
 * output, its grid frame scaled into box, so the popups do not vanish.  The
 * top layer is always on the output: under the main layer when that is on the
 * output too (the touch controls under the HUD and the popups), over the
 * filtered picture when the main layer is in the grid.  inPass as for
 * drawBatchesAt */
static void overlayRecord(RhiCommandList cl, RhiTexture out, const RhiRect *box, bool inPicture,
                          bool inPass)
{
    if (!s_ov.grid) {
        drawBatches(cl, out, s_outFormat, s_outW, s_outH, &s_ov.topCtx, s_ov.topFirst, s_ov.bCount,
                    inPass);
        drawBatches(cl, out, s_outFormat, s_outW, s_outH, &s_ov.ctx, s_ov.textBatches,
                    s_ov.topFirst, inPass);
    } else {
        if (!inPicture) {
            drawBatchesAt(cl, out, s_outFormat, s_outW, s_outH, &s_ov.ctx, box, s_ov.textBatches,
                          s_ov.topFirst, inPass);
        }
        drawBatches(cl, out, s_outFormat, s_outW, s_outH, &s_ov.topCtx, s_ov.topFirst, s_ov.bCount,
                    inPass);
    }
    overlayForget();
    s_ov.grid = 0;
}

/* the film's overlay, in a pass of its own on out (RENDER_TARGET, fmt,
 * w x h): the touch controls over the HUD and the popups, as overlayRecord
 * stacks them on a game frame */
void rd__overlay_film_record(RhiCommandList cl, RhiTexture out, RhiFormat fmt, uint32_t w,
                             uint32_t h)
{
    drawBatches(cl, out, fmt, w, h, &s_ov.topCtx, s_ov.topFirst, s_ov.bCount, false);
    drawBatches(cl, out, fmt, w, h, &s_ov.ctx, s_ov.textBatches, s_ov.topFirst, false);
    overlayForget();
    s_ov.grid = 0;
}

bool rd__overlay_grid_pending(void)
{
    return s_ov.grid && s_ov.topFirst > s_ov.textBatches;
}

void rd__overlay_grid_draw(RhiCommandList cl, RhiTexture t, RhiFormat fmt, uint32_t w, uint32_t h)
{
    if (s_ov.grid) {
        drawBatches(cl, t, fmt, w, h, &s_ov.ctx, s_ov.textBatches, s_ov.topFirst, false);
    }
}

/* ------------------------------------------------------------ the capture
 * rd.h rd_capture_presented: the output of the next present, copied before
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
    int result; /* 1 written, -1 failed, 0 none since the last rd_capture_result */
    char done[1024];
} s_cap;

bool rd_capture_presented(const char *png)
{
    if (!g_rd.hasDevice || !png || !png[0] || strlen(png) >= sizeof(s_cap.path)) {
        return false;
    }
    strcpy(s_cap.path, png);
    s_cap.armed = 1;
    s_cap.result = 0;
    return true;
}

bool rd__capture_armed(void)
{
    return s_cap.armed != 0;
}

int rd_capture_result(char *path, uint32_t pathSize)
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
 * to RENDER_TARGET for the overlay.  inPass: the output's pass is open
 * (rd__present_record); a copy cannot be recorded inside a pass, so it is
 * ended before the copy and a pass that loads the output is opened after
 * it, for the overlay to draw in */
static void captureRecord(RhiCommandList cl, RhiTexture out, RhiState *outState, bool inPass)
{
    if (!s_cap.armed) {
        return;
    }
    s_cap.armed = 0;
    if (!s_cap.tex.id || s_cap.w != s_outW || s_cap.h != s_outH || s_cap.fmt != s_outFormat) {
        if (s_cap.tex.id) {
            rhi_destroy_texture(s_cap.tex);
        }
        s_cap.tex = rhi_create_texture(&(RhiTextureDesc){s_outW, s_outH, 1, s_outFormat,
                                                         RHI_TEX_COPY_SRC | RHI_TEX_COPY_DST,
                                                         "rd photo capture", 0});
        s_cap.state = RHI_STATE_UNDEFINED;
        s_cap.w = s_outW;
        s_cap.h = s_outH;
        s_cap.fmt = s_outFormat;
    }
    if (!s_cap.tex.id) {
        rd__log("photo: capture %s: no texture for %ux%u", s_cap.path, s_outW, s_outH);
        snprintf(s_cap.done, sizeof(s_cap.done), "%s", s_cap.path);
        s_cap.result = -1;
        return;
    }
    if (inPass) {
        rhi_cmd_end_render_pass(cl);
    }
    rd__transition(cl, out, outState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_cap.tex, &s_cap.state, RHI_STATE_COPY_DST);
    rhi_cmd_copy_texture(cl, out, (RhiRect){0, 0, s_outW, s_outH}, s_cap.tex, 0, 0);
    rd__transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    if (inPass) {
        RhiRenderPassDesc p;
        memset(&p, 0, sizeof(p));
        p.color[0].texture = out;
        p.color[0].load = RHI_LOAD_LOAD;
        p.colorCount = 1;
        p.width = s_outW;
        p.height = s_outH;
        rhi_cmd_begin_render_pass(cl, &p);
    }
    s_cap.copied = 1;
}

void rd__capture_finish(void)
{
    if (!s_cap.copied) {
        return;
    }
    s_cap.copied = 0;
    const size_t n = (size_t)s_cap.w * s_cap.h * 4;
    uint8_t *px = malloc(n);
    bool ok = px && rd__read_rhi_texture(s_cap.tex, &s_cap.state, s_cap.w, s_cap.h, px, n);
    if (ok && s_cap.fmt == RHI_FMT_BGRA8_UNORM) {
        for (size_t i = 0; i < n; i += 4) {
            const uint8_t b = px[i];
            px[i] = px[i + 2];
            px[i + 2] = b;
        }
    }
    ok = ok && rd_write_png(s_cap.path, px, s_cap.w, s_cap.h, s_cap.w * 4, 0);
    free(px);
    rd__log("photo: capture %ux%u %s %s", s_cap.w, s_cap.h,
            ok ? "written to" : "failed:", s_cap.path);
    snprintf(s_cap.done, sizeof(s_cap.done), "%s", s_cap.path);
    s_cap.result = ok ? 1 : -1;
}

/* rd_present_blank's pass: the output cleared to 0, the
 * overlay on it */
static void blankRecord(RhiCommandList cl, RhiTexture out, RhiState *outState)
{
    rd__transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = out;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = s_outW;
    p.height = s_outH;
    rhi_cmd_begin_render_pass(cl, &p);
    rhi_cmd_end_render_pass(cl);
    RhiRect box;
    outputBox(s_outW, s_outH, &box);
    overlayRecord(cl, out, &box, false, false);
    if (s_window) {
        rd__transition(cl, out, outState, RHI_STATE_PRESENT);
    }
}

void rd__present_record(RhiCommandList cl)
{
    if (s_blank) {
        blankRecord(cl, s_window ? s_backbuffer : g_rd.presentOut,
                    s_window ? &s_backbufferState : &g_rd.presentOutState);
        return;
    }
    RdTargetRec *disp = rd__target_rec(RD_TARGET_DISPLAY + 1);
    if (!disp || !disp->color.id) {
        return;
    }
    const RdPresentPreset *pr = &s_present;
    RhiTexture out = s_window ? s_backbuffer : g_rd.presentOut;
    RhiState *outState = s_window ? &s_backbufferState : &g_rd.presentOutState;

    RhiTexture src = disp->color;
    uint32_t sw = disp->tw, sh = disp->th;
    rd__transition(cl, disp->color, &disp->colorState, RHI_STATE_SHADER_READ);
    RhiRect box;
    outputBox(s_outW, s_outH, &box);
    rd__note_present_box(s_outW, s_outH, &box); /* for the tests */
    /* the CRT filter draws the box from DISPLAY's own lines (its
     * scanlines are the PS2's field lines), in place of steps 1 and 2; off,
     * or when it cannot draw, nothing below changes */
    const int mirror = pr->mirror && rd__mirror_on();
    bool filtered = false, uiInPicture = false;
    if (rd__crt_on()) {
        /* the filter's box: its grid's shape (the exact width in whole
         * columns, rd__crt_grid), box at 4:3 */
        RhiRect cbox;
        rd__crt_box(s_outW, s_outH, &cbox);
        if (depthWanted()) {
            /* the filter replaces the box blit that carries the effects depth */
            rd__log_once(RD_ONCE_EFFECTS_DEPTH_CRT,
                         "effects depth is not available with the CRT filter");
        }
        rd__transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
        rd__perf_post(cl, RD_PERF_POST_CRT); /* the GPU timing's CRT stretch */
        const bool ui = rd__overlay_grid_pending();
        bool capOk = true;
        if (ui && rd__capture_armed()) {
            /* a capture never carries the port's UI; under
             * the CRT filter that UI is inside the filtered picture, so the
             * capture takes a pass without it and the shown picture a
             * second pass with it */
            capOk =
                rd__crt_record(cl, disp, out, s_outFormat, s_outW, s_outH, &cbox, mirror, false);
            if (capOk) {
                captureRecord(cl, out, outState, false);
            }
        }
        /* a failed capture pass: the box blit below, the capture before the
         * overlay as without the filter */
        filtered = capOk &&
                   rd__crt_record(cl, disp, out, s_outFormat, s_outW, s_outH, &cbox, mirror, true);
        uiInPicture = filtered && ui;
        rd__perf_post(cl, -1);
    }
    /* the full-height scene: DISPLAY already has every line */
    if (!filtered && pr->lineDouble && !g_rd.fullHeight) {
        const uint32_t lw = disp->tw, lh = disp->th * 2;
        if (!g_rd.presentLines.id || g_rd.presentLinesW != lw || g_rd.presentLinesH != lh) {
            if (g_rd.presentLines.id) {
                rhi_destroy_texture(g_rd.presentLines);
            }
            g_rd.presentLines = rhi_create_texture(
                &(RhiTextureDesc){lw, lh, 1, RHI_FMT_RGBA8_UNORM,
                                  RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED, "rd present lines", 0});
            g_rd.presentLinesState = RHI_STATE_UNDEFINED;
            g_rd.presentLinesW = lw;
            g_rd.presentLinesH = lh;
        }
        rd__transition(cl, g_rd.presentLines, &g_rd.presentLinesState, RHI_STATE_RENDER_TARGET);
        const RhiRect full = {0, 0, lw, lh};
        blit(cl, disp->color, disp->tw, disp->th, g_rd.presentLines, RHI_FMT_RGBA8_UNORM, lw, lh,
             RHI_LOAD_DONT_CARE, &full, NULL, pr->doubleFilter, 0, 0.0f, false);
        rd__transition(cl, g_rd.presentLines, &g_rd.presentLinesState, RHI_STATE_SHADER_READ);
        src = g_rd.presentLines;
        sw = lw;
        sh = lh;
    }
    /* the output's pass, open from the box blit to the end of the overlay
     * without the CRT filter and the effects depth: the deferred text and
     * both overlay layers draw in the blit's pass instead of a pass each.
     * The draws, their order, blend states, viewports and scissors are
     * those of the separate passes, and those passes only loaded what the
     * one before stored on the same single colour attachment, so the
     * pixels are the same, and a tile-based GPU does not write the output
     * out and read it back between them.  The CRT filter and the effects
     * depth keep a pass each */
    bool open = false;
    if (!filtered) {
        rd__transition(cl, out, outState, RHI_STATE_RENDER_TARGET);
        /* with the effects depth when asked for and an
         * effects program is loaded (never under the CRT filter, even when
         * it could not draw) */
        /* the frame's columns past its exact width left out (none at 4:3) */
        float crop = sourceCrop(disp, sw);
        RdFilter scaleFilter = pr->scaleFilter;
        /* anti-aliasing (or a scaled scene over two texels a pixel): the
         * source box-averaged to the box first (reduceRecord; the reduce
         * takes the crop; the box blit is then one texel a pixel, nearest) */
        if (reduceRecord(cl, &src, &sw, &sh, &box, crop)) {
            crop = 0.0f;
            scaleFilter = RD_FILTER_NEAREST;
        }
        /* Full pixel off: the game's border over the picture (the scissor
         * inset, the viewport the box's) */
        RhiRect mask;
        const RhiRect *sc =
            rd__border_mask(&box, rd__frame_wexact(), g_rd.gsH, &mask) ? &mask : NULL;
        const bool depth = depthWanted() && !rd__crt_on() &&
                           depthBlit(cl, src, sw, sh, out, s_outFormat, s_outW, s_outH, &box, sc,
                                     scaleFilter, mirror, crop);
        if (!depth) {
            open = !rd__crt_on();
            blit(cl, src, sw, sh, out, s_outFormat, s_outW, s_outH, RHI_LOAD_CLEAR, &box, sc,
                 scaleFilter, mirror, crop, open);
        }
    }
    /* the deferred text, drawn in list order with the regions and colours
     * the passes after it gave it.  None under the CRT filter (the rows are
     * in the scene, filtered with it) */
    textRecord(cl, out, open);
    /* ==== INSERTION POINT for later presentation passes ====================
     * After the box (the blit) and the deferred text, before the overlay
     * (the port's UI stays sharp above it).  Each one is a pass on `out`
     * (RHI_STATE_RENDER_TARGET at this point; a pass that samples the
     * picture copies it first or blits from `src`, `box`) with
     * rd__frame_group(s_outW, s_outH, ...) for its FrameCB, as blit() does.
     * With `open` the output's pass is still open: a new pass ends it
     * first and reopens it with RHI_LOAD_LOAD after, as captureRecord does
     * around its copy.
     * Keep the overlay last.  Under the CRT filter the filter is the last
     * pass but for the top layer (the touch controls, over the tube): the
     * main overlay is already inside it, and a pass here would draw over the
     * tube.
     * ======================================================================= */
    /* a capture takes the picture as shown, without the
     * port's own UI on the overlay (the popups, the photo HUD); under the
     * CRT filter with UI in the grid it was taken above, from a pass
     * without that UI (a no-op here then) */
    captureRecord(cl, out, outState, open);
    overlayRecord(cl, out, &box, uiInPicture, open);
    if (open) {
        rhi_cmd_end_render_pass(cl);
    }
    if (s_window) {
        rd__transition(cl, out, outState, RHI_STATE_PRESENT);
    }
}

void rd__present_finish(void)
{
    if (s_window && s_backbuffer.id) {
        rhi_present();
    }
}

void rd__present_shutdown(void)
{
    if (g_rd.presentLines.id) {
        rhi_destroy_texture(g_rd.presentLines);
    }
    if (g_rd.presentOut.id) {
        rhi_destroy_texture(g_rd.presentOut);
    }
    g_rd.presentLines = g_rd.presentOut = (RhiTexture){0};
    if (s_cap.tex.id) {
        rhi_destroy_texture(s_cap.tex); /* the capture's texture */
    }
    memset(&s_cap, 0, sizeof(s_cap));
    if (s_probe.tex.id) {
        rhi_destroy_texture(s_probe.tex); /* rd_display_probe's */
    }
    memset(&s_probe, 0, sizeof(s_probe));
    depthShutdown(); /* the effects depth */
    if (s_reduce.tex.id) {
        rhi_destroy_texture(s_reduce.tex); /* the supersampling's reduce */
    }
    memset(&s_reduce, 0, sizeof(s_reduce));
    rd__crt_shutdown(); /* the CRT filter */
    /* the overlay's prims (the registration stays), the deferred text's list */
    free(s_ov.v);
    free(s_ov.b);
    free(s_text.p);
    s_text.p = NULL;
    s_text.n = s_text.cap = 0;
    s_ov.v = NULL;
    s_ov.b = NULL;
    s_ov.vCap = s_ov.bCap = 0;
    overlayForget();
}

bool rd_display_probe(uint8_t rgba[RD_DISPLAY_PROBE_POINTS][4])
{
    RdTargetRec *disp = rd__target_rec(rd_target(RD_TARGET_DISPLAY).id);
    if (!g_rd.hasDevice || !rgba || !disp || !disp->color.id || disp->tw < 4 || disp->th < 4 ||
        disp->format == RHI_FMT_R8_UNORM) {
        return false;
    }
    if (s_probe.tex.id && s_probe.format != disp->format) {
        rhi_wait_idle();
        rhi_destroy_texture(s_probe.tex);
        s_probe.tex = (RhiTexture){0};
    }
    if (!s_probe.tex.id) {
        s_probe.tex = rhi_create_texture(
            &(RhiTextureDesc){RD_DISPLAY_PROBE_POINTS, 1, 1, disp->format,
                              RHI_TEX_COPY_DST | RHI_TEX_COPY_SRC, "rd display probe", 0});
        s_probe.state = RHI_STATE_UNDEFINED;
        s_probe.format = disp->format;
        if (!s_probe.tex.id) {
            return false;
        }
    }
    const uint32_t w = disp->tw, h = disp->th;
    const uint32_t xs[RD_DISPLAY_PROBE_POINTS] = {w / 2, w / 4, 3 * w / 4, w / 4, 3 * w / 4};
    const uint32_t ys[RD_DISPLAY_PROBE_POINTS] = {h / 2, h / 4, h / 4, 3 * h / 4, 3 * h / 4};
    RhiCommandList cl = rhi_begin_commands();
    if (!cl.id) {
        return false;
    }
    rd__transition(cl, disp->color, &disp->colorState, RHI_STATE_COPY_SRC);
    rd__transition(cl, s_probe.tex, &s_probe.state, RHI_STATE_COPY_DST);
    for (int k = 0; k < RD_DISPLAY_PROBE_POINTS; k++) {
        const RhiRect r = {(int32_t)xs[k], (int32_t)ys[k], 1, 1};
        rhi_cmd_copy_texture(cl, disp->color, r, s_probe.tex, k, 0);
    }
    rhi_end_commands(cl);
    rhi_submit(cl);
    uint8_t px[RD_DISPLAY_PROBE_POINTS * 4];
    if (!rd__read_rhi_texture(s_probe.tex, &s_probe.state, RD_DISPLAY_PROBE_POINTS, 1, px,
                              sizeof(px))) {
        return false;
    }
    const bool bgra = disp->format == RHI_FMT_BGRA8_UNORM;
    for (int k = 0; k < RD_DISPLAY_PROBE_POINTS; k++) {
        rgba[k][0] = px[k * 4 + (bgra ? 2 : 0)];
        rgba[k][1] = px[k * 4 + 1];
        rgba[k][2] = px[k * 4 + (bgra ? 0 : 2)];
        rgba[k][3] = px[k * 4 + 3];
    }
    return true;
}

bool rd_read_presented(void *dst, uint32_t *w, uint32_t *h)
{
    /* the window build presents the swapchain image and keeps no copy */
    if (!g_rd.hasDevice || !dst || s_window || rhi_swapchain_format() != RHI_FMT_UNKNOWN) {
        return false;
    }
    return rd__read_present(dst, (size_t)g_rd.presentOutW * g_rd.presentOutH * 4, w, h);
}

/* the output size in the settings and the pending settings (both, so a
 * pending rd_set_settings does not take it back at the next rd_begin_frame) */
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

void rd_resize_output(uint32_t width, uint32_t height)
{
    if (!g_rd.inited || width == 0 || height == 0) {
        return;
    }
    setOutputSize(width, height);
    if (g_rd.hasDevice && rhi_swapchain_format() != RHI_FMT_UNKNOWN) {
        rhi_resize_swapchain(width, height, g_rd.settings.vsync != 0);
    }
    /* a scene sized by the window (resolution "window", the
     * Enhanced flag) follows it at the next rd_begin_frame
     * (rd__apply_display recreates the targets if their scale changed; a
     * fixed scale or size never changes) */
    g_rd.settingsPending = true;
}

/* The output follows the swapchain.  The backend rebuilds the

 * swapchain at the surface's size on its own (rhi_swapchain_size), and the
 * window's size event may come later or never (Android); an output size
 * other than the image's drew the picture and the movie's 4:3 box for the
 * wrong size (offset, scaled, cropped).  Called with an image acquired, so
 * the swapchain is not rebuilt again here (rd_resize_output would wait for
 * the GPU and destroy the image being drawn): only the settings change,
 * and the scene's targets follow at the next rd_begin_frame. */
static bool s_followed;
static uint32_t s_followW, s_followH;

void rd__output_follow_swapchain(void)
{
    uint32_t sw = 0, sh = 0;
    if (!g_rd.inited || !rhi_swapchain_size(&sw, &sh) || !sw || !sh) {
        return;
    }
    if (sw == g_rd.settings.outputWidth && sh == g_rd.settings.outputHeight) {
        return;
    }
    rd__log("output follows the swapchain %ux%u (was %ux%u)", sw, sh, g_rd.settings.outputWidth,
            g_rd.settings.outputHeight);
    setOutputSize(sw, sh);
    g_rd.settingsPending = true;
    s_followed = true;
    s_followW = sw;
    s_followH = sh;
}

static bool s_boxNoted;
static uint32_t s_boxOutW, s_boxOutH;
static RhiRect s_boxLast;

void rd__note_present_box(uint32_t outW, uint32_t outH, const RhiRect *box)
{
    s_boxNoted = true;
    s_boxOutW = outW;
    s_boxOutH = outH;
    s_boxLast = *box;
}

bool rd__last_present_box(uint32_t *outW, uint32_t *outH, RhiRect *box)
{
    if (!s_boxNoted) {
        return false;
    }
    *outW = s_boxOutW;
    *outH = s_boxOutH;
    *box = s_boxLast;
    return true;
}

bool rd_output_followed(uint32_t *w, uint32_t *h)
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
