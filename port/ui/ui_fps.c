/*
 * port/ui/ui_fps.c
 *
 * The Frame rate counter's drawing and the calls that feed its number
 * (ui_fps.h).  Compiled into the window build only (it reads the renderer's
 * replay count and the video options).
 */
#include "ui_fps.h"

#include <stdint.h>
#include <stdio.h>

#include "fps_counter.h"
#include "strings.h"
#include "touch_ui.h"
#include "video_options.h"

#ifdef ICO_RD
#include "font.h"
#include "rd.h"

static FpsCounter s_fps;
static int s_logged; /* the option as the log last said it */

void ui_fps_present(void)
{
    fps_present(&s_fps);
}

void ui_fps_tick(uint64_t now_ns)
{
    uint32_t replays = 0;
    const int on = ico_video_show_fps() != 0;

    (void)rd_replay_time_max(0, &replays); /* the 10 s line zeroes it */
    fps_tick(&s_fps, now_ns, rd_interpolation_active(), replays);
    if (on != s_logged) {
        s_logged = on;
        fprintf(stderr, "window: frame rate counter %s\n", on ? "on" : "off");
    }
}

#define HEIGHT_FRAC 0.035f /* the text's em, of the output's height */
#define MIN_PX 18.0f
#define MARGIN_FRAC 0.012f
#define MARGIN_MIN 6.0f
#define SHRINK_MIN_PX 12.0f

/* an output pixel to the font's overlay grid (touch_ui.c pxToGrid) */
static void pxToGrid(const RdOverlayCtx *ctx, float px, float py, float *gx, float *gy)
{
    const float bw = (float)ctx->box.w, bh = (float)ctx->box.h;
    const float W = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    const float left = (float)ctx->box.x + (bw - W) * 0.5f;

    *gx = (px - left) * 640.0f / W;
    *gy = (py - (float)ctx->box.y) * 448.0f / bh + 2.0f;
}

void ui_fps_draw_overlay(const RdOverlayCtx *ctx)
{
    static const uint8_t kColour[4] = {
        255, 255, 255, 235}; /* the overlay's own light text, not a colour of its own */
    char text[24];
    const int n = fps_value(&s_fps);

    if (!ctx || n <= 0 || ctx->outW == 0 || ctx->outH == 0 || ctx->box.w == 0 || ctx->box.h == 0 ||
        !ui_font_init()) {
        return;
    }
    const float outH = (float)ctx->outH, bw = (float)ctx->box.w, bh = (float)ctx->box.h;
    const float W = bw < bh * (4.0f / 3.0f) ? bw : bh * (4.0f / 3.0f);
    float margin = MARGIN_FRAC * outH, px, py, x1 = 0.0f, emPx = HEIGHT_FRAC * outH;

    margin = margin < MARGIN_MIN ? MARGIN_MIN : margin;
    emPx = emPx < MIN_PX ? MIN_PX : emPx;
    snprintf(text, sizeof(text), "%d %s", n, ui_str(UI_STR_FPS_UNIT));
    ui_begin_overlay(ctx);
    float size = emPx * 448.0f / bh, gx, gy;
    const float floorSize = SHRINK_MIN_PX * 448.0f / bh;
    const float natW = ui_measure_text(size, text) * W / 640.0f;
    const float minW = ui_measure_text(floorSize < size ? floorSize : size, text) * W / 640.0f;

    px = py = margin;
    /* the player's actual touch layout decides: the corner when no drawn
       control is in the way, else the first free place along the keys' row
       (touch_gap.h) */
    if (!ui_touch_top_gap(ctx, margin, emPx, natW, minW, &px, &x1, &py)) {
        px = py = margin;
        x1 = 0.0f;
    }
    {
        /* once per change: where the counter sits (the log of a report that
           says it covered a control) */
        static int s_where = -1;
        const int where = py > margin + 0.5f ? 1 : 0;

        if (where != s_where) {
            s_where = where;
            fprintf(stderr, "window: frame rate counter at %s (x %d y %d of %ux%u)\n",
                    where ? "the touch controls' row, clear of the drawn controls" : "the corner",
                    (int)px, (int)py, ctx->outW, ctx->outH);
        }
    }
    if (x1 > px) {
        /* the touch layout's stretch: shrunk until the text fits it */
        for (int i = 0; i < 16 && size * bh / 448.0f > SHRINK_MIN_PX &&
                        ui_measure_text(size, text) * W / 640.0f > x1 - px;
             i++) {
            size *= 0.92f;
        }
    }
    pxToGrid(ctx, px, py, &gx, &gy);
    ui_draw_text(gx, gy, size, kColour, text, UI_ALIGN_LEFT | UI_VALIGN_TOP);
    ui_end_overlay();
}

#else

void ui_fps_tick(uint64_t now_ns)
{
    (void)now_ns;
}

void ui_fps_present(void) {}

void ui_fps_draw_overlay(const struct RdOverlayCtx *ctx)
{
    (void)ctx;
}

#endif
