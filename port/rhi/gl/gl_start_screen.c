/* gl_start_screen.c: OpenGL's own progress screen while the device's
 * start-up shaders compile (gl_internal.h gl_start_screen_*).
 *
 * The rc3 report: Options > Graphics > Apply to OpenGL on an RTX 3090
 * finished after a long while (NVIDIA's GLSL compiler is slow on the set,
 * not stuck), but the window stayed black for the whole compile, which
 * reads as a hang.  The renderer's "Preparing graphics" screen cannot be
 * drawn there: it is drawn with the renderer's own shaders and pipelines
 * (sprite_ui_vs, sprite_ps, font_ps), which are among the shaders being
 * made, and a frame of the renderer needs the rest of rd_init (the
 * profile, the targets) too.  So this screen needs no shader at all: every
 * shape is a scissored clear of the window's framebuffer 0 (glScissor and
 * glClearBufferfv), then SDL_GL_SwapWindow:
 *
 *  - a black frame, the bar where the renderer's screen has it (the 4:3
 *    layout grid's 120..520 x 250..266, its frame, track and fill colours)
 *    and the line "Preparing graphics for OpenGL: N of M shaders" above it
 *    (N the shaders made, M the set's size) in block letters of its own
 *    (5 x 7 cells, the few letters the line takes);
 *  - drawn when a shader of the set (RhiShaderDesc.planIndex, numbered by
 *    the dispatcher against the renderer's count, rhi.h rhi_plan_shaders)
 *    begins, and between the compile's 50 ms wait slices
 *    (gl_compile.c), first once the set has taken GL_SCREEN_AFTER_NS,
 *    then at most every GL_SCREEN_EVERY_NS (the renderer's screen's
 *    250 ms and 100 ms, port/platform/window_host.c);
 *  - only with the window's wait hooks (the game; never the tests and
 *    tools), a surface, and before the device's first command list or
 *    pipeline (gl_start_screen_end: the renderer's own screen takes over
 *    from the pipeline set on);
 *  - a draw during a compile that takes over GL_SCREEN_SLOW_NS (a driver
 *    that holds a lock its compile thread has, so the clear waits for the
 *    compile) turns the draws during compiles off for the rest of the set:
 *    the window must keep taking its events there (the pump runs between
 *    the slices), so the screen then moves between compiles only;
 *  - ICO_GL_START_SCREEN=0 turns it off (the window stays black as before).
 *
 * Its lines: the first draw's, the slow draw's, and the set's end (the
 * shaders, the time, the screens drawn and the slowest draw), each direct
 * (GL_LOG_DIRECT). */
#include "gl_internal.h"
#include <stdlib.h>
#include <string.h>

#define GL_SCREEN_AFTER_NS 250000000ull
#define GL_SCREEN_EVERY_NS 100000000ull
#define GL_SCREEN_SLOW_NS 250000000ull

/* the renderer's layout grid (port/ui/font.h UI_GRID_W, UI_GRID_H) */
#define GRID_W 640.0
#define GRID_H 452.0

/* the block letters: 5 columns (bit 4 the left one), 7 rows above the
 * baseline and 2 below; only what the line takes */
typedef struct Glyph {
    char c;
    uint8_t rows[9];
} Glyph;

static const Glyph s_glyphs[] = {
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 0x00, 0x00}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F, 0x00, 0x00}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 0x00, 0x00}},
    {'a', {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F, 0x00, 0x00}},
    {'c', {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E, 0x00, 0x00}},
    {'d', {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F, 0x00, 0x00}},
    {'e', {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E, 0x00, 0x00}},
    {'f', {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08, 0x00, 0x00}},
    {'g', {0x00, 0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E}},
    {'h', {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00, 0x00}},
    {'i', {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E, 0x00, 0x00}},
    {'n', {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11, 0x00, 0x00}},
    {'o', {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x00, 0x00}},
    {'p', {0x00, 0x00, 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'r', {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10, 0x00, 0x00}},
    {'s', {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E, 0x00, 0x00}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, 0x00, 0x00}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, 0x00, 0x00}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, 0x00, 0x00}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E, 0x00, 0x00}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02, 0x00, 0x00}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E, 0x00, 0x00}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E, 0x00, 0x00}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08, 0x00, 0x00}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, 0x00, 0x00}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C, 0x00, 0x00}},
    {':', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x00}},
};

/* the colours of the renderer's screen (window_host.c progress_overlay):
 * a rect's RGB as given, the text's GS colour over 0x80 */
static const GLfloat s_black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
static const GLfloat s_frame[4] = {96.0f / 255.0f, 96.0f / 255.0f, 96.0f / 255.0f, 1.0f};
static const GLfloat s_track[4] = {24.0f / 255.0f, 24.0f / 255.0f, 24.0f / 255.0f, 1.0f};
static const GLfloat s_fill[4] = {200.0f / 255.0f, 180.0f / 255.0f, 120.0f / 255.0f, 1.0f};
static const GLfloat s_text[4] = {102.0f / 128.0f, 100.0f / 128.0f, 94.0f / 128.0f, 1.0f};

static const Glyph *glyphOf(char c)
{
    for (size_t i = 0; i < sizeof(s_glyphs) / sizeof(s_glyphs[0]); i++) {
        if (s_glyphs[i].c == c) {
            return &s_glyphs[i];
        }
    }
    return NULL; /* a space (or a letter the line never takes) */
}

/* a rectangle of the window, rows from the top (framebuffer 0 has row 0 at
 * the bottom), filled through the scissor */
static void fillRect(int x, int y, int w, int h, int wh, const GLfloat c[4])
{
    if (w <= 0 || h <= 0) {
        return;
    }
    glf.Scissor(x, wh - y - h, w, h);
    glf.ClearBufferfv(GL_COLOR, 0, c);
}

/* the block letters of text centred on cx, baseline at y, unit px */
static void drawText(const char *text, int cx, int y, int px, int wh)
{
    const int n = (int)strlen(text);
    int x = cx - (n * 6 * px - px) / 2;
    for (int i = 0; i < n; i++, x += 6 * px) {
        const Glyph *g = glyphOf(text[i]);
        if (!g) {
            continue;
        }
        for (int r = 0; r < 9; r++) {
            const unsigned bits = g->rows[r];
            /* each row's runs of lit cells as one rectangle */
            for (int col = 0; col < 5;) {
                if (!(bits & (0x10u >> col))) {
                    col++;
                    continue;
                }
                int end = col;
                while (end < 5 && (bits & (0x10u >> end))) {
                    end++;
                }
                fillRect(x + col * px, y - (7 - r) * px, (end - col) * px, px, wh, s_text);
                col = end;
            }
        }
    }
}

/* the screen into framebuffer 0 at ww x wh, then swapped */
static void paint(int ww, int wh, uint32_t done, uint32_t total)
{
    GLint keep[4] = {0, 0, 0, 0};
    glf.GetIntegerv(GL_SCISSOR_BOX, keep);
    glf.BindFramebuffer(GL_FRAMEBUFFER, 0);
    glf.DrawBuffer(GL_BACK);
    gl_cmd_open_colour_mask();   /* the colour write mask open, as the cache knows it */
    glf.Enable(GL_SCISSOR_TEST); /* the device's state (gl_defaults) */
    fillRect(0, 0, ww, wh, wh, s_black);
    /* the 4:3 box of the window, the grid laid on it */
    double bw = (double)wh * 4.0 / 3.0, bh = (double)wh;
    if (bw > (double)ww) {
        bw = (double)ww;
        bh = (double)ww * 3.0 / 4.0;
    }
    const double bx = ((double)ww - bw) / 2.0, by = ((double)wh - bh) / 2.0;
    const double sx = bw / GRID_W, sy = bh / GRID_H;
#define GX(v) ((int)(bx + (v) * sx + 0.5))
#define GY(v) ((int)(by + (v) * sy + 0.5))
    const int x0 = GX(120.0), x1 = GX(520.0), y0 = GY(250.0), y1 = GY(266.0);
    const int in = (int)(2.0 * sy + 0.5) > 0 ? (int)(2.0 * sy + 0.5) : 1;
    fillRect(x0, y0, x1 - x0, y1 - y0, wh, s_frame);
    fillRect(x0 + in, y0 + in, x1 - x0 - 2 * in, y1 - y0 - 2 * in, wh, s_track);
    const uint32_t shown = done > total ? total : done;
    const int fillW = total ? (int)((double)(x1 - x0 - 2 * in) * shown / total + 0.5) : 0;
    fillRect(x0 + in, y0 + in, fillW, y1 - y0 - 2 * in, wh, s_fill);
    char line[96];
    snprintf(line, sizeof(line), "Preparing graphics for OpenGL: %u of %u shaders", shown, total);
    const int px = (int)(1.6 * sy + 0.5) > 0 ? (int)(1.6 * sy + 0.5) : 1;
    drawText(line, GX(GRID_W / 2.0), GY(232.0), px, wh);
#undef GX
#undef GY
    glf.Scissor(keep[0], keep[1], keep[2], keep[3]);
    if (!SDL_GL_SwapWindow(g_gl.window)) {
        static uint32_t s_failed;
        if (gl_log_rate(&s_failed)) {
            GL_LOG("start screen: SDL_GL_SwapWindow failed (%s; %u so far)", SDL_GetError(),
                   s_failed);
        }
    }
}

static bool screenOn(void)
{
    GlStartScreen *s = &g_gl.screen;
    if (s->ended || !g_gl.hasWaits || !g_gl.window || !g_gl.surface || g_gl.compileStalled) {
        return false;
    }
    if (!s->envRead) {
        const char *env = getenv("ICO_GL_START_SCREEN");
        s->envRead = true;
        s->off = env && env[0] == '0';
        if (s->off) {
            GL_LOG_DIRECT("start screen: off (ICO_GL_START_SCREEN=0): the window stays black "
                          "while the shaders compile");
        }
    }
    return !s->off && s->total > 0;
}

static void maybeDraw(bool waiting)
{
    GlStartScreen *s = &g_gl.screen;
    if (!screenOn()) {
        return;
    }
    const uint64_t now = gl_now_ns();
    if (now - s->startNs < GL_SCREEN_AFTER_NS ||
        (s->drawn && now - s->lastNs < GL_SCREEN_EVERY_NS) || (waiting && s->noWaitDraws)) {
        return;
    }
    int ww = 0, wh = 0;
    SDL_GetWindowSizeInPixels(g_gl.window, &ww, &wh);
    if (ww <= 0 || wh <= 0 ||
        (SDL_GetWindowFlags(g_gl.window) & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))) {
        return;
    }
    /* the shaders made: the one in hand is not yet */
    const uint32_t done = s->index > 0 ? s->index - 1u : 0u;
    paint(ww, wh, done, s->total);
    const uint64_t end = gl_now_ns();
    const uint64_t took = end - now;
    s->lastNs = end;
    s->drawn++;
    if (took > s->slowestNs) {
        s->slowestNs = took;
    }
    if (s->drawn == 1) {
        GL_LOG_DIRECT("start screen: drawn after %.1f s at shader %u of %u (%dx%d; scissored "
                      "clears, no shader: the renderer's own screen needs the shaders being "
                      "made); then every %.1f s at most, %s",
                      (double)(now - s->startNs) / 1e9, s->index, s->total, ww, wh,
                      (double)GL_SCREEN_EVERY_NS / 1e9,
                      g_gl.compiler || g_gl.parallelKhr
                          ? "between compiles and during one"
                          : "between compiles only (each compile blocks the window)");
    }
    if (waiting && took >= GL_SCREEN_SLOW_NS) {
        s->noWaitDraws = true;
        GL_LOG_DIRECT("start screen: a draw during the compile of shader %u took %.2f s (the "
                      "driver held it for the compile): drawn between compiles only for the "
                      "rest of the set",
                      s->index, (double)took / 1e9);
    }
}

void gl_start_screen_shader(uint32_t index, uint32_t total)
{
    GlStartScreen *s = &g_gl.screen;
    if (index == 0 || s->ended) {
        return;
    }
    if (s->total == 0) {
        s->startNs = gl_now_ns();
    }
    s->index = index;
    s->total = total;
    maybeDraw(false);
}

void gl_start_screen_wait(void)
{
    maybeDraw(true);
}

void gl_start_screen_end(void)
{
    GlStartScreen *s = &g_gl.screen;
    if (s->ended) {
        return;
    }
    s->ended = true;
    if (s->drawn) {
        GL_LOG_DIRECT("start screen: over at shader %u of %u after %.1f s, %u screens drawn "
                      "(the slowest %.0f ms%s); the renderer's own screen from here on",
                      s->index, s->total, (double)(gl_now_ns() - s->startNs) / 1e9, s->drawn,
                      (double)s->slowestNs / 1e6,
                      s->noWaitDraws ? ", between compiles only after a slow one" : "");
    } else if (s->total && !s->off && g_gl.hasWaits) {
        GL_LOG("start screen: not drawn (%u shaders in %.2f s: under %.2f s, or no window to "
               "draw in)",
               s->index, (double)(gl_now_ns() - s->startNs) / 1e9,
               (double)GL_SCREEN_AFTER_NS / 1e9);
    }
}
