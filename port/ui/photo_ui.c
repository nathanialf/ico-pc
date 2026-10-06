/*
 * port/ui/photo_ui.c
 *
 * Photo mode's screen (photo_ui.h): a port layout of one masked row whose
 * proc feeds port/game/photo_mode.h, and the HUD on the overlay.
 */
#include "photo_ui.h"

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "layout_ext.h"
#include "photo_mode.h"
#include "popup.h"
#include "settings.h"
#include "strings.h"
#include "ui_list.h" /* ui_SettingsAddRow */

#ifdef ICO_RD
#include "rd.h"
#endif

/* the game's side */
extern PadState pad[16];
extern int stage_no;            /* common/src/StageManager.c: 1 is the boot and title */
extern int NonLinearCameraMove; /* the language, 2..6 */
extern int systemStatus[12];    /* [0]: 1 PAL 50 Hz, 0 60 Hz; [1]: the frame step */
extern void NEGATIVE_SE(void);
extern void la_host_leave(void);
/* the object the game camera follows (omori/src/camera-root.c) and its root
   position (sugipon/src/geometryManager.c): photo mode's pivot, read once */
extern void *default_cameratarget_gobj;
extern void GetRootPosition(void *pos, void *obj);

#define LAYOUT_PAUSE_OPTIONS 58

static int s_layout = -1, s_row = -1, s_back = -1;

static int photoProc(int first, int item);

int ui_PhotoBuild(int back)
{
    s_back = back;
    /* one row, masked: nothing drawn, no cursor (the layout's curItem -1),
       and a texel rectangle of its own as every port row has */
    s_row =
        ui_SettingsAddRow(120, 200, 400, 40, 0, -1, UI_STR_PHOTO_MODE, NULL, 0.0f, UI_ALIGN_CENTER);
    if (s_row < 0) {
        return -1;
    }
    lt_ext_Prop(s_row)->defaultMask = 1;
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = s_row;
    l.last = s_row + 1;
    l.fadeInTime = 0.3f;
    l.fadeOutTime = 0.1f;
    l.colA = 0.0f; /* no dimming: the scene as it is */
    l.proc = photoProc;
    l.procFirst = 1;
    l.defaultItem = -1;
    l.curItem = -1;
    l.link = -1;
    s_layout = lt_ext_AddLayout(&l);
    return s_layout;
}

int ui_PhotoLayout(void)
{
    return s_layout;
}

void ui_PhotoReset(void)
{
    s_layout = s_row = s_back = -1;
    ico_photo_exit();
}

int ui_PhotoAvailable(void)
{
    return stage_no > 1;
}

static int photoProc(int first, int item)
{
    (void)item;
    if (first) {
        /* 25 or 30 Main ticks a second: the frame step over the field rate */
        const int step = systemStatus[1] > 0 ? systemStatus[1] : 2;
        ico_photo_set_tick_hz((systemStatus[0] ? 50 : 60) / step);
        if (default_cameratarget_gobj != NULL) {
            float pos[4];
            GetRootPosition(pos, default_cameratarget_gobj);
            ico_photo_set_subject(pos);
        } else {
            ico_photo_set_subject(NULL);
        }
        ico_photo_enter();
    }
    if (lt_fade_status() != 2) {
        return -1;
    }
    IcoPhotoPad p;
    p.held = (unsigned int)pad[0].now;
    p.pressed = (unsigned int)pad[0].flags;
    memcpy(p.ana, pad[0].ana, sizeof(p.ana));
    if (!ico_photo_active()) {
        ico_photo_enter(); /* came back without a first call (a reset) */
    }
    if (ico_photo_update(&p)) {
        ico_photo_exit();
        NEGATIVE_SE();
        la_host_leave();
        if (s_back >= 0) {
            texLayout[LAYOUT_PAUSE_OPTIONS].defaultItem = s_back;
        }
        return LAYOUT_PAUSE_OPTIONS;
    }
    return -1;
}

void ui_PhotoCaptureDone(int ok, const char *name)
{
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    ui_PopupPush(ui_Str(ok ? UI_STR_PHOTO_SAVED : UI_STR_PHOTO_FAILED), name ? name : "");
    fprintf(stderr, "photo: %s %s\n", ok ? "saved" : "not saved", name ? name : "");
}

/* ------------------------------------------------------------ the HUD */

#define HUD_SIZE 17.0f
#define HUD_X 22.0f
#define HUD_BOTTOM 438.0f
#define HUD_PITCH 21.0f

void ui_PhotoDrawOverlay(const struct RdOverlayCtx *ctx)
{
#ifdef ICO_RD
    if (!ctx || !ico_photo_hud() || !ui_FontInit()) {
        return;
    }
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    IcoPhotoState st;
    ico_photo_get(&st);
    RdCamera game, ov;
    char fov[96] = "";
    if (rd_PhotoSceneCamera(&game) && ico_photo_camera(&ov, &game)) {
        snprintf(fov, sizeof(fov), ui_Str(UI_STR_PHOTO_FOV), (int)(ico_photo_fov_deg(&ov) + 0.5f));
    }
    const char *lines[5] = {ui_Str(UI_STR_PHOTO_MODE), ui_Str(UI_STR_PHOTO_HUD_MOVE),
                            ui_Str(UI_STR_PHOTO_HUD_LENS), ui_Str(UI_STR_PHOTO_HUD_KEYS), fov};
    ui_BeginOverlay(ctx);
    float w = 0.0f;
    for (int i = 0; i < 5; i++) {
        const float m = ui_MeasureText(HUD_SIZE, lines[i]);
        w = m > w ? m : w;
    }
    const float top = HUD_BOTTOM - 5.0f * HUD_PITCH;
    static const uint8_t panel[4] = {6, 6, 9, 0x50};
    ui_DrawRect(HUD_X - 8.0f, top - 6.0f, HUD_X + w + 8.0f, HUD_BOTTOM + 4.0f, panel);
    for (int i = 0; i < 5; i++) {
        static const uint8_t title[4] = {0x80, 0x7C, 0x70, 0x80},
                             body[4] = {0x6A, 0x68, 0x62, 0x80};
        ui_DrawText(HUD_X, top + (float)i * HUD_PITCH, HUD_SIZE, i == 0 ? title : body, lines[i],
                    UI_VALIGN_TOP);
    }
    ui_EndOverlay();
#else
    (void)ctx;
#endif
}
