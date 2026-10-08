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

static int s_layout = -1, s_row = -1;

static int photoProc(int first, int item);

int ui_PhotoBuild(void)
{
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
    s_layout = s_row = -1;
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
        /* the pause menu, the cursor on the row (settings.c) */
        return ui_SettingsPhotoBack();
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
/* the widest line's room: the 640-wide grid less the margins either side */
#define HUD_ROOM (640.0f - 2.0f * HUD_X)

#ifdef ICO_RD
/* t with its first "%s" or "%d" replaced by arg (the translations are data,
   not formats), into buf */
static void fill(char *buf, size_t size, const char *t, const char *mark, const char *arg)
{
    const char *at = strstr(t, mark);
    if (at) {
        snprintf(buf, size, "%.*s%s%s", (int)(at - t), t, arg, at + strlen(mark));
    } else {
        snprintf(buf, size, "%s %s", t, arg);
    }
}

/* line 0: "Photo mode: Free camera, speed Normal" (the speed only for the
   free camera, the one it changes) */
static void hudTitle(char *buf, size_t size, const IcoPhotoState *st)
{
    const int freeCam = st->mode == ICO_PHOTO_CAM_FREE;
    /* French sets its colon apart */
    const char *colon = ui_GetLanguage() == UI_LANG_FR ? " : " : ": ";
    const char *cam = ui_Str(freeCam ? UI_STR_PHOTO_CAM_FREE : UI_STR_PHOTO_CAM_ORBIT);
    if (!freeCam) {
        snprintf(buf, size, "%s%s%s", ui_Str(UI_STR_PHOTO_MODE), colon, cam);
        return;
    }
    static const UiStrId words[3] = {UI_STR_PHOTO_SPEED_SLOW, UI_STR_PHOTO_SPEED_NORMAL,
                                     UI_STR_PHOTO_SPEED_FAST};
    const int sp = st->speed >= 0 && st->speed < 3 ? st->speed : ICO_PHOTO_SPEED_NORMAL;
    char speed[64];
    fill(speed, sizeof(speed), ui_Str(UI_STR_PHOTO_SPEED), "%s", ui_Str(words[sp]));
    snprintf(buf, size, "%s%s%s, %s", ui_Str(UI_STR_PHOTO_MODE), colon, cam, speed);
}
#endif

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
        char deg[16];
        snprintf(deg, sizeof(deg), "%d", (int)(ico_photo_fov_deg(&ov) + 0.5f));
        fill(fov, sizeof(fov), ui_Str(UI_STR_PHOTO_FOV), "%d", deg);
    }
    char title[160];
    hudTitle(title, sizeof(title), &st);
    const char *lines[5] = {
        title,
        ui_Str(st.mode == ICO_PHOTO_CAM_FREE ? UI_STR_PHOTO_HUD_MOVE_FREE : UI_STR_PHOTO_HUD_MOVE),
        ui_Str(UI_STR_PHOTO_HUD_LENS), ui_Str(UI_STR_PHOTO_HUD_KEYS), fov};
    ui_BeginOverlay(ctx);
    /* the text smaller when the widest line (a long translation) would
       leave the screen */
    float size = HUD_SIZE;
    float w = 0.0f;
    for (int i = 0; i < 5; i++) {
        const float m = ui_MeasureText(size, lines[i]);
        w = m > w ? m : w;
    }
    if (w > HUD_ROOM) {
        size *= HUD_ROOM / w;
        w = HUD_ROOM;
    }
    const float top = HUD_BOTTOM - 5.0f * HUD_PITCH;
    static const uint8_t panel[4] = {6, 6, 9, 0x50};
    ui_DrawRect(HUD_X - 8.0f, top - 6.0f, HUD_X + w + 8.0f, HUD_BOTTOM + 4.0f, panel);
    for (int i = 0; i < 5; i++) {
        static const uint8_t titleCol[4] = {0x80, 0x7C, 0x70, 0x80},
                             body[4] = {0x6A, 0x68, 0x62, 0x80};
        ui_DrawText(HUD_X, top + (float)i * HUD_PITCH, size, i == 0 ? titleCol : body, lines[i],
                    UI_VALIGN_TOP);
    }
    ui_EndOverlay();
#else
    (void)ctx;
#endif
}
