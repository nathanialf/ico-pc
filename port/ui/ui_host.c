/*
 * port/ui/ui_host.c
 *
 * port/ui wired to the game and rd (ui_host.h); linked into ico_pc only,
 * since it reads the game's globals.
 */
#include "ui_host.h"

#include <stdlib.h>
#include <string.h>

#ifdef ICO_RD
#include "rd.h"
#endif

#include "font.h"
#include "popup.h"
#include "strings.h"
#include "ui_internal.h"

/* the game's side (GsBase.c, main.c; common/include/main.h) */
extern int ScreenWidth;
extern int ScreenHeight;
extern float center_X;
extern float center_Y;
extern int NonLinearCameraMove; /* the language the boot screen chose, 2..6 */
extern int frame_count;         /* gsb_UpdateGSSystem's flip count */
#ifdef ICO_RD
extern void gif_HostFlush(void); /* GifHost.h */
#endif

static int s_lastFrame = -1;

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

void ui_HostInit(void)
{
    ui_FontInit();
    ui__SetSyncHook(sync);
#ifdef ICO_RD
    ui__SetRecordHook(gif_HostFlush);
#endif
    ui_PopupSetDevTest(truthy(getenv("ICO_UI_POPUP_TEST")));
}

void ui_HostVsync(unsigned int mainTick)
{
    ui_PopupDevTick(mainTick);
    ui_PopupVsync();
#ifdef ICO_RD
    if (ui_PopupActive() && rd_FrameOpen() && frame_count != s_lastFrame) {
        s_lastFrame = frame_count;
        ui_PopupRecord();
    }
#endif
}

void ui_HostShutdown(void)
{
    ui_FontShutdown();
    ui__SetRecordHook(NULL);
    ui__SetSyncHook(NULL);
}
