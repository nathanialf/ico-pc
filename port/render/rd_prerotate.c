/* rd_prerotate.c: the present into a swapchain made in the surface's own
 * turn (rhi_swapchain_rotation; docs/port/DISPLAY.md "The turned
 * swapchain").
 *
 * An Android phone held sideways shows its surface turned (the surface's
 * current transform ROTATE_90 or 270).  A swapchain made with identity has
 * the compositor turn every image on the GPU before it reaches the screen,
 * work that preempts the game's (Google, "Handling device orientation
 * efficiently in Vulkan with pre-rotation", android-developers.googleblog.
 * com, 2020-02: 1-3 ms a frame and a 40 % higher GPU clock on a Pixel 4 XL;
 * issue 179's logs: `surface transform 0x2 (offered 0x1ff): presenting with
 * identity, the compositor rotates`).  port/rhi/vk/vk_swapchain.c makes the
 * swapchain with that transform instead, its images the window's size
 * turned; the renderer then draws everything it drew into the backbuffer
 * (the box blit, the CRT filter, the deferred text, the overlay with the
 * touch controls and the fps corner, the films, the blank frame) into a
 * texture of the window's own size and orientation, exactly as before, and
 * this file's one pass turns that texture into the swapchain image
 * (blit.hlsl blit_vs with DrawCB g_aux1.w the quarter turns: the full-screen
 * triangle reads the source at the turned position, one texel a pixel,
 * nearest, the bytes unchanged).  So every target, every dump, the F12
 * picture and the photo capture (taken from that texture) and every place
 * on the screen (the touch coordinates are window pixels) stay as they
 * were; only the image the compositor gets is turned, and the compositor
 * turns nothing.
 *
 * The turn, by the spec's words (VkSurfaceTransformFlagBitsKHR: ROTATE_90
 * "image content is rotated 90 degrees clockwise"), in the image's own 0..1
 * position (U, V), V down, for the picture's (u, v):
 *   1 (90):  (u, v) = (V, 1 - U)    the picture's top-left at the image's
 *                                    top-right
 *   2 (180): (u, v) = (1 - U, 1 - V)
 *   3 (270): (u, v) = (1 - V, U)
 * (Google's sample turns its clip space +90 degrees about Z for ROTATE_90:
 * the same picture.)
 *
 * The twin (the tests'): a headless Vulkan device under
 * ICO_VK_FAKE_TRANSFORM=90, 180 or 270 has no swapchain to turn, so the
 * present draws its picture into its own texture as always and then this
 * pass turns it into a texture of the turned size (the twin of the
 * swapchain image); rd_read_presented reads the twin back and turns it back
 * on the CPU by the table above, so the tool's PNG is the picture the
 * screen would show, byte for byte the identity run's when the pass is
 * right (rd_replay_tool_prerotate). */
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"
#include "shader_consts.h"

/* the window's picture before the turn (w x h, the swapchain's format) */
static struct {
    RhiTexture tex;
    RhiState state;
    RhiFormat fmt;
    uint32_t w, h;
} s_pic;

/* the headless twin of the turned image */
static struct {
    RhiTexture tex;
    RhiState state;
    uint32_t w, h;       /* the turned size */
    uint32_t picW, picH; /* the picture's */
    uint32_t turns;
    bool pending; /* the last present drew it (rd__prerotate_twin_take) */
} s_twin;

static uint32_t s_loggedTurns, s_loggedW, s_loggedH;
static bool s_twinLogged;

uint32_t rd__prerotate_turns(void)
{
    if (!g_rd.hasDevice) {
        return 0;
    }
    const uint32_t t = rhi_swapchain_rotation();
    return t <= 3u ? t : 0u;
}

bool rd__prerotate_target(uint32_t w, uint32_t h, RhiFormat fmt, RhiTexture *tex, RhiState **state)
{
    if (!w || !h) {
        return false;
    }
    if (!s_pic.tex.id || s_pic.w != w || s_pic.h != h || s_pic.fmt != fmt) {
        if (s_pic.tex.id) {
            rhi_destroy_texture(s_pic.tex);
        }
        /* a render target the present's passes draw into, sampled by the
         * turn, copied from by the photo capture as the backbuffer was */
        s_pic.tex = rhi_create_texture(&(RhiTextureDesc){
            w, h, 1, fmt, RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC,
            "rd present before the turn", 0});
        s_pic.state = RHI_STATE_UNDEFINED;
        s_pic.w = w;
        s_pic.h = h;
        s_pic.fmt = fmt;
        if (!s_pic.tex.id) {
            static unsigned fails;
            if (++fails <= 8 || fails % 100 == 0) {
                rd__log("present: no %ux%u texture for the picture before the turn; a blank "
                        "image is presented in its place (%u times)",
                        w, h, fails);
            }
            s_pic.w = s_pic.h = 0;
            return false;
        }
    }
    *tex = s_pic.tex;
    *state = &s_pic.state;
    return true;
}

/* src (w x h) into dst (its size turned), one pass */
static void turnPass(RhiCommandList cl, RhiTexture src, RhiState *srcState, uint32_t w, uint32_t h,
                     RhiTexture dst, RhiState *dstState, RhiFormat dstFmt, uint32_t turns)
{
    const uint32_t dw = (turns & 1u) ? h : w, dh = (turns & 1u) ? w : h;
    rd__transition(cl, src, srcState, RHI_STATE_SHADER_READ);
    rd__transition(cl, dst, dstState, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = dst;
    p.color[0].load = RHI_LOAD_DONT_CARE; /* the triangle covers every pixel */
    p.colorCount = 1;
    p.width = dw;
    p.height = dh;
    rhi_cmd_begin_render_pass(cl, &p);
    const RhiViewport vp = {0.0f, 0.0f, (float)dw, (float)dh, 0.0f, 1.0f};
    rhi_cmd_set_viewport(cl, &vp);
    const RhiRect all = {0, 0, dw, dh};
    rhi_cmd_set_scissor(cl, &all);
    /* the box blit's own pipeline for this format: no pipeline the
     * start-up set does not already make */
    RdPipeKeyInt k = rd__post_key(RD_VS_BLIT, RD_FS_BLIT, dstFmt);
    const RhiPipeline pipe = rd__get_pipeline(&k);
    if (pipe.id) {
        IcoDrawCB cb;
        memset(&cb, 0, sizeof(cb));
        for (int i = 0; i < 4; i++) {
            cb.col[i] = 0x80; /* modulate by 1.0: the texel as it is */
        }
        cb.mode[0] = ICO_DF_TEXTURED | ICO_DF_TCC_RGBA;
        cb.uvRect[2] = (float)w;
        cb.uvRect[3] = (float)h;
        cb.tex[0] = (float)w;
        cb.tex[1] = (float)h;
        cb.tex[2] = 1.0f / (float)w;
        cb.tex[3] = 1.0f / (float)h;
        cb.aux1[3] = turns; /* blit_vs: the source read at the turned position */
        rd__set_pipeline(cl, pipe, &k);
        rd__bind_uniform(cl, 0, rd__frame_group(dw, dh, 0.0f, 0.0f));
        rd__bind_uniform(cl, 1, rd__draw_group(&cb));
        rhi_cmd_set_bind_group(cl, 2,
                               rd__tex_group(src, rd__sampler(RD_FILTER_NEAREST, RD_FILTER_NEAREST,
                                                              RD_WRAP_CLAMP, RD_WRAP_CLAMP)));
        rhi_cmd_draw(cl, 3, 0, 1);
    }
    rhi_cmd_end_render_pass(cl);
}

void rd__prerotate_record(RhiCommandList cl, RhiTexture src, RhiState *srcState, uint32_t w,
                          uint32_t h, RhiTexture dst, RhiState *dstState, RhiFormat dstFmt,
                          uint32_t turns)
{
    if (turns != s_loggedTurns || w != s_loggedW || h != s_loggedH) {
        s_loggedTurns = turns;
        s_loggedW = w;
        s_loggedH = h;
        rd__log("present: the %ux%u picture turned %u degrees into the swapchain's %ux%u images "
                "in one pass (pre-rotated: the compositor shows them as they are)",
                w, h, turns * 90u, (turns & 1u) ? h : w, (turns & 1u) ? w : h);
    }
    turnPass(cl, src, srcState, w, h, dst, dstState, dstFmt, turns);
}

void rd__prerotate_blank(RhiCommandList cl, RhiTexture image, RhiState *state, uint32_t w,
                         uint32_t h, uint32_t turns)
{
    /* a swapchain image acquired must be presented (rd_video.c's rule: the
     * next acquire would otherwise have to adopt it); with no picture to
     * turn into it, the image cleared at its own (turned) size */
    rd__transition(cl, image, state, RHI_STATE_RENDER_TARGET);
    RhiRenderPassDesc p;
    memset(&p, 0, sizeof(p));
    p.color[0].texture = image;
    p.color[0].load = RHI_LOAD_CLEAR;
    p.colorCount = 1;
    p.width = (turns & 1u) ? h : w;
    p.height = (turns & 1u) ? w : h;
    rhi_cmd_begin_render_pass(cl, &p);
    rhi_cmd_end_render_pass(cl);
    rd__transition(cl, image, state, RHI_STATE_PRESENT);
}

void rd__prerotate_twin(RhiCommandList cl, RhiTexture src, RhiState *srcState, uint32_t w,
                        uint32_t h, uint32_t turns)
{
    const uint32_t tw = (turns & 1u) ? h : w, th = (turns & 1u) ? w : h;
    if (!s_twin.tex.id || s_twin.w != tw || s_twin.h != th) {
        if (s_twin.tex.id) {
            rhi_destroy_texture(s_twin.tex);
        }
        s_twin.tex = rhi_create_texture(
            &(RhiTextureDesc){tw, th, 1, RHI_FMT_RGBA8_UNORM,
                              RHI_TEX_RENDER_TARGET | RHI_TEX_SAMPLED | RHI_TEX_COPY_SRC,
                              "rd present turned twin", 0});
        s_twin.state = RHI_STATE_UNDEFINED;
        s_twin.w = s_twin.tex.id ? tw : 0;
        s_twin.h = s_twin.tex.id ? th : 0;
    }
    if (!s_twin.tex.id) {
        return; /* the read takes the unturned picture: the test then fails its line */
    }
    if (!s_twinLogged) {
        s_twinLogged = true;
        rd__log("present: turned twin: the %ux%u picture turned %u degrees into %ux%u, read back "
                "turned back (ICO_VK_FAKE_TRANSFORM)",
                w, h, turns * 90u, tw, th);
    }
    turnPass(cl, src, srcState, w, h, s_twin.tex, &s_twin.state, RHI_FMT_RGBA8_UNORM, turns);
    s_twin.picW = w;
    s_twin.picH = h;
    s_twin.turns = turns;
    s_twin.pending = true;
}

void rd__prerotate_twin_clear(void)
{
    s_twin.pending = false;
}

bool rd__prerotate_twin_pending(void)
{
    return s_twin.pending;
}

bool rd__prerotate_twin_read(void *dst, size_t dstSize, uint32_t w, uint32_t h)
{
    if (!s_twin.pending || s_twin.picW != w || s_twin.picH != h || !w || !h ||
        dstSize < (size_t)w * h * 4u) {
        return false;
    }
    const size_t n = (size_t)w * h * 4u;
    uint8_t *img = malloc(n);
    if (img == NULL) {
        return false;
    }
    const bool ok = rd__read_rhi_texture(s_twin.tex, &s_twin.state, s_twin.w, s_twin.h, img, n);
    if (ok) {
        /* the picture's pixel (x, y) from the image's (X, Y), the header's
         * table in pixels: 1 (X, Y) = (h - 1 - y, x) in an image h wide;
         * 2 (w - 1 - x, h - 1 - y); 3 (y, w - 1 - x) in an image h wide */
        uint8_t *out = dst;
        const uint32_t q = s_twin.turns;
        for (uint32_t y = 0; y < h; y++) {
            for (uint32_t x = 0; x < w; x++) {
                size_t at;
                if (q == 1u) {
                    at = (size_t)x * h + (h - 1u - y);
                } else if (q == 2u) {
                    at = (size_t)(h - 1u - y) * w + (w - 1u - x);
                } else if (q == 3u) {
                    at = (size_t)(w - 1u - x) * h + y;
                } else {
                    at = (size_t)y * w + x;
                }
                memcpy(out + ((size_t)y * w + x) * 4u, img + at * 4u, 4u);
            }
        }
    }
    free(img);
    return ok;
}

void rd__prerotate_shutdown(void)
{
    if (s_pic.tex.id) {
        rhi_destroy_texture(s_pic.tex);
    }
    if (s_twin.tex.id) {
        rhi_destroy_texture(s_twin.tex);
    }
    memset(&s_pic, 0, sizeof(s_pic));
    memset(&s_twin, 0, sizeof(s_twin));
    s_loggedTurns = s_loggedW = s_loggedH = 0;
    s_twinLogged = false;
}
