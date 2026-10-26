/* gl_swapchain.c: the OpenGL backend's swapchain (gl_internal.h).
 *
 * The renderer draws its picture into the backbuffer, an RGBA8 texture at
 * the window's pixel size (rows from the top, as every texture of the
 * backend); rhi_present blits it to the window's framebuffer 0 upside down
 * (GL's default framebuffer has row 0 at the bottom) and swaps.  The swap
 * interval is the vsync: 1 (FIFO) or 0 (immediate).  Absent when the
 * device is headless. */
#include "gl_internal.h"
#include <string.h>

static uint32_t s_resizeLines;

static void gl_swap_interval(bool vsync)
{
    if (!SDL_GL_SetSwapInterval(vsync ? 1 : 0)) {
        static uint32_t s_failed;
        if (gl_log_rate(&s_failed)) {
            GL_LOG("swap interval %d not taken (%s)", vsync ? 1 : 0, SDL_GetError());
        }
    }
}

bool gl_swapchain_create(uint32_t w, uint32_t h, bool vsync)
{
    if (!g_gl.window || w == 0 || h == 0) {
        return false;
    }
    gl_swapchain_destroy();
    g_gl.backbuffer = gl_register_backbuffer(w, h);
    if (!g_gl.backbuffer) {
        GL_LOG("no %ux%u backbuffer", w, h);
        return false;
    }
    g_gl.swapWidth = w;
    g_gl.swapHeight = h;
    g_gl.vsync = vsync;
    g_gl.surface = true;
    g_gl.hadSurface = true;
    g_gl.acquired = false;
    g_gl.swapchains++;
    gl_swap_interval(vsync);
    if (gl_log_rate(&s_resizeLines)) {
        GL_LOG("swapchain %ux%u: an RGBA8 texture blitted to the window, swap interval %d", w, h,
               vsync ? 1 : 0);
    }
    return true;
}

void gl_swapchain_destroy(void)
{
    if (g_gl.backbuffer) {
        gl_release_backbuffer(g_gl.backbuffer);
    }
    g_gl.backbuffer = 0;
    g_gl.swapWidth = g_gl.swapHeight = 0;
    g_gl.surface = false;
    g_gl.acquired = false;
}

bool rhi_resize_swapchain(uint32_t width, uint32_t height, bool vsync)
{
    if (!g_gl.initialised || !g_gl.window || !g_gl.context) {
        return false;
    }
    if (width == 0 || height == 0) {
        return false; /* minimised: the caller retries on the next resize */
    }
    if (g_gl.surface && g_gl.backbuffer && width == g_gl.swapWidth && height == g_gl.swapHeight) {
        if (vsync != g_gl.vsync) {
            g_gl.vsync = vsync;
            gl_swap_interval(vsync);
        }
        return true;
    }
    rhi_wait_idle();
    return gl_swapchain_create(width, height, vsync);
}

RhiFormat rhi_swapchain_format(void)
{
    /* RGBA8: the backbuffer texture's format (and still after a release) */
    return g_gl.hadSurface ? RHI_FMT_RGBA8_UNORM : RHI_FMT_UNKNOWN;
}

bool rhi_swapchain_size(uint32_t *w, uint32_t *h)
{
    if (!g_gl.surface || !g_gl.backbuffer || !g_gl.swapWidth || !g_gl.swapHeight) {
        return false;
    }
    *w = g_gl.swapWidth;
    *h = g_gl.swapHeight;
    return true;
}

void rhi_surface_poll_restart(void)
{
    /* the present follows the window's size itself; nothing to poll */
}

bool rhi_present_shown(void)
{
    return g_gl.presentShown;
}

RhiTexture rhi_acquire_backbuffer(void)
{
    RhiTexture out = {0};
    g_gl.presentShown = false; /* until this frame's present shows it */
    if (!g_gl.surface || !g_gl.backbuffer) {
        return out;
    }
    g_gl.acquired = true;
    out.id = g_gl.backbuffer;
    return out;
}

const char *rhi_present_mode_name(void)
{
    if (!g_gl.surface) {
        return "none";
    }
    return g_gl.vsync ? "fifo" : "immediate";
}

bool rhi_present_statistics(uint64_t *presentCount, uint64_t *presentRefresh, uint64_t *syncRefresh,
                            uint64_t *syncNs)
{
    (void)presentCount;
    (void)presentRefresh;
    (void)syncRefresh;
    (void)syncNs;
    return false; /* OpenGL has no count of the pictures shown */
}

/* The backbuffer into framebuffer 0, y flipped (and stretched to the
 * window's size if it has changed since the last resize), with the scissor
 * test off (a blit honours it); the scissor test is put back for the
 * command code.  Framebuffer 0 is left bound for read and draw, its read
 * buffer GL_BACK, through SDL_GL_SwapWindow: PC port: a capture hook (OBS's
 * game capture) reads the bound read framebuffer's GL_BACK at the swap, and
 * with the last pass's framebuffer still bound it captured that pass's
 * top-down texture (upside down).  Every pass begin and every copy binds the
 * framebuffer it uses (gl_cmd.c gl_fbo_get, gl_cmd_activate), so
 * nothing relies on the binding surviving the present. */
static void gl_blit_backbuffer(const GlTexture *t, int ww, int wh)
{
    const GLboolean scissor = glf.IsEnabled(GL_SCISSOR_TEST);
    glf.BindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.scratchRead);
    glf.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->name, 0);
    glf.ReadBuffer(GL_COLOR_ATTACHMENT0);
    glf.BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glf.DrawBuffer(GL_BACK);
    if (scissor) {
        glf.Disable(GL_SCISSOR_TEST);
    }
    const bool same = (uint32_t)ww == t->width && (uint32_t)wh == t->height;
    glf.BlitFramebuffer(0, 0, (GLint)t->width, (GLint)t->height, 0, wh, ww, 0, GL_COLOR_BUFFER_BIT,
                        same ? GL_NEAREST : GL_LINEAR);
    if (scissor) {
        glf.Enable(GL_SCISSOR_TEST);
    }
    glf.FramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glf.BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glf.ReadBuffer(GL_BACK);
}

void rhi_present(void)
{
    g_gl.presentShown = false;
    if (!g_gl.surface || !g_gl.acquired || !g_gl.backbuffer || !g_gl.window) {
        return;
    }
    g_gl.acquired = false;
    RhiTexture h = {g_gl.backbuffer};
    const GlTexture *t = gl_get_texture(h);
    if (!t) {
        return;
    }
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(g_gl.window, &ww, &wh);
    const uint64_t t0 = gl_now_ns();
    if (ww > 0 && wh > 0) {
        gl_blit_backbuffer(t, ww, wh);
    }
    const bool swapped = SDL_GL_SwapWindow(g_gl.window);
    g_gl.presentNs += gl_now_ns() - t0; /* RhiStats */
    g_gl.presents++;
    if (!swapped) {
        static uint32_t s_failed;
        if (gl_log_rate(&s_failed)) {
            GL_LOG("SDL_GL_SwapWindow failed (%s; %u so far)", SDL_GetError(), s_failed);
        }
    }
    const SDL_WindowFlags flags = SDL_GetWindowFlags(g_gl.window);
    g_gl.presentShown =
        swapped && ww > 0 && wh > 0 && !(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN));
    gl_check("rhi_present");
    /* GL has no out-of-date result: follow the window's size here, as the
     * Vulkan backend does on VK_ERROR_OUT_OF_DATE_KHR */
    if (ww > 0 && wh > 0 && ((uint32_t)ww != g_gl.swapWidth || (uint32_t)wh != g_gl.swapHeight)) {
        rhi_resize_swapchain((uint32_t)ww, (uint32_t)wh, g_gl.vsync);
    }
}

/* rhi.h's window-loss protocol.  Release: the GPU idle, the backbuffer
 * gone, presents do nothing; the context and every object stay.
 * Recreate: the same context made current on the window (the one given, or
 * the one rhi_init had), so nothing made on it is lost, and a backbuffer
 * at the window's size with the last vsync. */
void rhi_release_surface(void)
{
    if (!g_gl.initialised || !g_gl.surface) {
        return;
    }
    rhi_wait_idle();
    gl_swapchain_destroy();
    GL_LOG("surface released");
}

bool rhi_recreate_surface(void *window)
{
    if (!g_gl.initialised || !g_gl.context) {
        return false;
    }
    SDL_Window *w = window ? (SDL_Window *)window : g_gl.window;
    if (!w) {
        return false; /* headless */
    }
    if (g_gl.surface) {
        rhi_release_surface();
    }
    if (!(SDL_GetWindowFlags(w) & SDL_WINDOW_OPENGL)) {
        GL_LOG("recreate: the window was made without SDL_WINDOW_OPENGL");
        return false;
    }
    if (w != g_gl.window && !SDL_GL_MakeCurrent(w, g_gl.context)) {
        GL_LOG("recreate: the context is not current on the new window (%s)", SDL_GetError());
        return false;
    }
    g_gl.window = w;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    if (pw <= 0 || ph <= 0) {
        return false; /* no size yet: the caller tries again later */
    }
    return gl_swapchain_create((uint32_t)pw, (uint32_t)ph, g_gl.vsync);
}
