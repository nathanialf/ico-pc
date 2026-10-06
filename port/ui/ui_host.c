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
#include "popup.h"
#include "settings.h"
#include "strings.h"
#include "ui_internal.h"

/* the game's side (GsBase.c, main.c; common/include/main.h) */
extern int ScreenWidth;
extern int ScreenHeight;
extern float center_X;
extern float center_Y;
extern int NonLinearCameraMove; /* the language the boot screen chose, 2..6 */
#ifdef ICO_RD
extern void gif_HostFlush(void); /* GifHost.h */

/* package MV: the model viewer's overlay (ui_host.h) */
static void (*s_viewerOverlay)(const struct RdOverlayCtx *ctx);

void ui_HostSetViewerOverlay(void (*fn)(const struct RdOverlayCtx *ctx))
{
    s_viewerOverlay = fn;
}

/* package OV: the presenter's overlay, at every present (rd.h
   rd_SetPresentOverlay): the popup on the output */
static void hostOverlay(const RdOverlayCtx *ctx, void *user)
{
    (void)user;
    /* package MV: the model viewer's text, under the popups */
    if (s_viewerOverlay != NULL) {
        s_viewerOverlay(ctx);
    }
    ui_PopupDrawOverlay(ctx);
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
#endif
    /* package DEF: the menu rows at the output's resolution (Enhanced) */
    ui_InstallDeferredText(1);
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
#endif
    ui_InstallDeferredText(0);
#ifdef ICO_UI_HAVE_SDL
    ui_SettingsSetQuitHandler(NULL);
#endif
    ui_FontShutdown();
    ui__SetRecordHook(NULL);
    ui__SetSyncHook(NULL);
}
