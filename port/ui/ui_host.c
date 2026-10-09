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

/* package OV: the presenter's overlay, at every present (rd.h
   rd_SetPresentOverlay): the popup on the output (inside the picture under
   the CRT filter) */
static void hostOverlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    /* v0.4.3 I17b: where the picture is, for the mouse pointer's hit test */
    ui_MouseSetView((int)ctx->outW, (int)ctx->outH, ctx->box.x, ctx->box.y, (int)ctx->box.w,
                    (int)ctx->box.h);
    ui_PhotoDrawOverlay(ctx); /* package PHOTO: the HUD, under the popups */
    ui_PopupDrawOverlay(ctx);
}

/* package AN-T: the touch controls (package AN-G) on the presenter's top
   layer (rd.h rd_SetPresentOverlayTop): on the output at its resolution,
   never through the CRT filter; under the HUD and the popups without it */
static void hostOverlayTop(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    ui_TouchDrawOverlay(ctx);
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
    ui_SetGsFrame(&f);
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
#ifdef ICO_RD
    const RdSettings *s = rd_GetSettings();
    if (s) {
        ui_SetScale(ui_ScaleFor((int)s->preset, s->outputHeight));
    }
#endif
}

static int truthy(const char *v)
{
    return v != NULL && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0 || strcmp(v, "on") == 0 ||
                         strcmp(v, "yes") == 0);
}

#ifdef ICO_UI_HAVE_SDL

/* Q2: the title's "Quit to desktop" (settings.h): the event closing the
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

void ui_HostInit(void)
{
    ui_FontInit();
    ui__SetSyncHook(sync);
#ifdef ICO_RD
    ui__SetRecordHook(gif_HostFlush);
    rd_SetPresentOverlay(hostOverlay, NULL);
    rd_SetPresentOverlayTop(hostOverlayTop, NULL);
#endif
    ui_PopupSetDevTest(truthy(getenv("ICO_UI_POPUP_TEST")));
#ifdef ICO_UI_HAVE_SDL
    ui_SettingsSetQuitHandler(hostQuit);
#endif
}

void ui_HostVsync(unsigned int mainTick)
{
    ui_PopupDevTick(mainTick);
    ui_PopupVsync();
}

void ui_HostShutdown(void)
{
#ifdef ICO_RD
    rd_SetPresentOverlay(NULL, NULL);
    rd_SetPresentOverlayTop(NULL, NULL);
#endif
#ifdef ICO_UI_HAVE_SDL
    ui_SettingsSetQuitHandler(NULL);
#endif
    ui_FontShutdown();
    ui__SetRecordHook(NULL);
    ui__SetSyncHook(NULL);
}
