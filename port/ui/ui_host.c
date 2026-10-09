/*
 * port/ui/ui_host.c
 *
 * port/ui wired to the game and rd (ui_host.h); linked into ico_pc only,
 * since it reads the game's globals.
 */
#include "ui_host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ICO_RD
#include "rd.h"
#endif
#ifdef ICO_UI_HAVE_SDL
#include <SDL3/SDL.h>
#endif

#include "font.h"
#include "photo_ui.h"
#include "popup.h"
#include "settings.h"
#include "strings.h"
#include "touch_ui.h"
#include "ui_internal.h"
#include "ui_mouse.h"

/* the game's side (GsBase.c, main.c; common/include/main.h) */
extern int ScreenWidth;
extern int ScreenHeight;
extern float center_X;
extern float center_Y;
extern int NonLinearCameraMove; /* the language the boot screen chose, 2..6 */
#ifdef ICO_RD
extern void gif_HostFlush(void); /* GifHost.h */

/* the presenter's overlay, at every present (rd.h
   rd_set_present_overlay): the popup on the output (inside the picture under
   the CRT filter) */
static void hostOverlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    ui_photo_draw_overlay(ctx); /* the photo mode HUD, under the popups */
    ui_popup_draw_overlay(ctx);
}

/* the touch controls on the presenter's top
   layer (rd.h rd_set_present_overlay_top): on the output at its resolution,
   never through the CRT filter; under the HUD and the popups without it */
static void hostOverlayTop(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    /* where the picture is on the output, for the mouse
       pointer's hit test: this layer's ctx is always the output and its
       box, where the overlay's is the CRT filter's grid under the filter
       (the pointer's place is a fraction of the window) */
    ui_mouse_set_view((int)ctx->outW, (int)ctx->outH, ctx->box.x, ctx->box.y, (int)ctx->box.w,
                      (int)ctx->box.h);
    ui_touch_draw_overlay(ctx);
}
#endif

static void sync(void)
{
    UiGsFrame f;
    f.screenW = ScreenWidth > 0 ? ScreenWidth : 512;
    f.screenH = ScreenHeight > 0 ? ScreenHeight : 512;
    f.centerX = center_X;
    f.centerY = center_Y;
    f.z = UI_LAYOUT_Z;
    ui_set_gs_frame(&f);
    ui_set_language(ui_lang_from_game(NonLinearCameraMove));
#ifdef ICO_RD
    const RdSettings *s = rd_get_settings();
    if (s) {
        ui_set_scale(ui_scale_for((int)s->preset, s->outputHeight));
    }
#endif
}

static int truthy(const char *v)
{
    return v != NULL && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0 ||
                         strcmp(v, "yes") == 0);
}

#ifdef ICO_UI_HAVE_SDL

/* the title's "Quit to desktop" (settings.h): the event closing the
   window posts, so ico_window_pump ends the run the same way (main returns,
   the atexit handlers flush the achievements, stop the audio, close the
   window) */
static void hostQuit(void)
{
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_QUIT;
    if (!SDL_PushEvent(&e)) {
        fprintf(stderr, "ui: could not post the quit event: %s\n", SDL_GetError());
        exit(0);
    }
}

#endif

void ui_host_init(void)
{
    ui_font_init();
    ui__set_sync_hook(sync);
#ifdef ICO_RD
    ui__set_record_hook(gif_HostFlush);
    rd_set_present_overlay(hostOverlay, NULL);
    rd_set_present_overlay_top(hostOverlayTop, NULL);
#endif
    ui_popup_set_dev_test(truthy(getenv("ICO_UI_POPUP_TEST")));
#ifdef ICO_UI_HAVE_SDL
    ui_settings_set_quit_handler(hostQuit);
#endif
}

void ui_host_vsync(unsigned int mainTick)
{
    ui_popup_dev_tick(mainTick);
    ui_popup_vsync();
}

void ui_host_shutdown(void)
{
#ifdef ICO_RD
    rd_set_present_overlay(NULL, NULL);
    rd_set_present_overlay_top(NULL, NULL);
#endif
#ifdef ICO_UI_HAVE_SDL
    ui_settings_set_quit_handler(NULL);
#endif
    ui_font_shutdown();
    ui__set_record_hook(NULL);
    ui__set_sync_hook(NULL);
}
