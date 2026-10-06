/*
 * port/game/model_viewer.c
 *
 * Settings > Extras > Models, the game side (model_viewer.h).  Built from the development build's Motion Viewer
 * (sugipon/src/motionViewer.c) without calling it: MotionViewer() runs on the
 * debug CSV windows, pad 1 and the debug font.  What is taken from it:
 *
 *   objMenuProc       the object is found by its kind, its run function is
 *                     parked (fn = 0), the camera's target is set at
 *                     priority 3 (Camctrl_SetTarget), its parallel motion
 *                     table cleared and its motion shift stopped
 *                     (SetParallelMotionTableWithNoRequest, ctrl.shiftStop)
 *   motKindMenuProc   a motion of the object's block plays through
 *                     DisableChangeRootUpdateMode, DisableMotionOrientUpdate
 *                     and InitMotionOrient(obj, oriFrom, oriTo, -1, -1, id);
 *                     a motion the stage does not hold is not offered ("NO
 *                     MOTION IN THIS STAGE": area != 0, blendKind 320,
 *                     motionTable[id] == 0); the frame is
 *                     ForMotionViewer_GetCurrentAnimationFrame of
 *                     GetNbMotionFrames - 1
 *   modeMessage       the root update mode 19 ("RotOnly"): the root turns
 *                     but does not travel, so a walk stays in view
 *
 * The states:
 *
 *   MV_OFF      the title.  The model list (a port layout, ui_list.h) is
 *               opened from Settings > Extras > Models; Cross on a model
 *               saves the settings, fades the title music as a new game
 *               does and loads the model's host stage with ico_mv_active
 *               set (options.h): the stage starts no script
 *               (sceneManager.c) and the achievements are suspended
 *   MV_LOADING  until the host stage is up
 *   MV_VIEW     the object is found and every other object parked (not
 *               active: neither its functions nor its display list run;
 *               the system objects, kind -1, run on); pause is off
 *               (enable_game_pause) and the fog off; the renderer keeps
 *               only the object's own draws (rd.h rd_SetDrawFilter: its
 *               display object's key, and whatever its display list draws,
 *               learned while it runs) over a grey backdrop recorded in
 *               list 0; the camera orbits it (our own yaw, pitch and
 *               distance through SetWSMatrix, the camera left in mode 0,
 *               which does not touch the view).  Its animations are a list
 *               at the right; its name, the animation and the frame are on
 *               rows of the viewer's layout, styled as the Settings rows
 *   MV_LIST     Triangle: the model list on the host stage.  Cross loads
 *               the next model's stage (the same stage is loaded again, so
 *               each model starts from a fresh stage); Triangle goes back
 *               to the title the way the pause menu's End Game does
 *               (layout_action.c la_host_end_game)
 *   MV_LEAVING  until the title is up: then the filter is off and
 *               ico_mv_active clear
 *
 * Nothing is saved and no game flag is set but what End Game resets.  The
 * viewer's own buffers are host memory (malloc), not the game's arena.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"
#include "gobj.h"
#include "main.h"
#include "camera-root.h"
#include "geometryManager.h"
#include "motionFileManager.h"
#include "motionManager2.h"
#include "motionOrientManager.h"
#include "StageManager.h"
#include "GifPacket.h"
#include "DisplayP2O.h"
#include "layout_texture.h"

#include "font.h"
#include "layout_ext.h"
#include "model_viewer.h"
#include "options.h"
#include "settings.h"
#include "strings.h"
#include "ui_hint.h"
#include "ui_list.h"

#ifdef ICO_RD
#include "GifHost.h"
#include "rd.h"
#endif

/* --- the game's side ------------------------------------------------------- */

extern PadState pad[16];
extern int stage_no;
extern int systemStatus[12];
extern int NonLinearCameraMove;
extern int enable_game_pause; /* layout_action.c */
extern MotionDef motionKind[];
extern GenGeo objLayout[]; /* the layout rows (gamesys.h) */
/* layout_action.c (ICO_HOST) */
extern void POSITIVE_SE(void);
extern void NEGATIVE_SE(void);
extern void CUR_SE(void);
extern void la_host_leave(void);
extern void la_host_title_music_fade(void);
extern void la_host_end_game(void);
/* fumi/src/act-game.c: puts the boy, the girl and the stick at the stage's
   first entrance (what start_stage does, common/src/icoMisc.c) */
extern void ACTGame_SetActors_Debug(int stage, unsigned char flag);
/* sugipon/src/enemy.c: a shadow's loaded flag (its model drawn) */
extern void SetEnemyHitGeometryAction(GObj *self, int on);
unsigned int ico_host_main_ticks(void); /* trace_host.c */

/* the pad's trigger bits (keyInput.c's logical word) */
#define PAD_L1 0x0004
#define PAD_R1 0x0008
#define PAD_TRIANGLE 0x0010
#define PAD_CIRCLE 0x0020
#define PAD_CROSS 0x0040
#define PAD_SQUARE 0x0080
#define PAD_UP 0x1000
#define PAD_DOWN 0x4000
#define PAD_BACK (PAD_TRIANGLE | PAD_CIRCLE)

#define TITLE_STAGE 1
/* modeMessage's "RotOnly" */
#define ROOT_ROT_ONLY 19
/* the backdrop: a neutral grey, dark enough for the menu's light text */
#define BACKDROP_GREY 78
/* the free camera: its field of view (degrees), the sticks' dead zone and
   rates per tick (25 a second in PAL) */
#define CAM_FOV 40.0f
/* how far the model sits left of the middle, in distances */
#define MODEL_SHIFT 0.16f
#define STICK_DEAD 24
#define YAW_RATE 0.12f
#define PITCH_RATE 0.08f
#define ZOOM_RATE 0.08f
#define PITCH_MAX 1.35f
#define LOAD_TIMEOUT_TICKS 3000u
/* the title's stage back after End Game (a few hundred ticks); past this
   the viewer lets go of the game anyway */
#define LEAVE_TIMEOUT_TICKS 3000u
#define LOG_EVERY_TICKS 25u

enum { MV_OFF, MV_LOADING, MV_VIEW, MV_LIST, MV_LEAVING };

static int s_state = MV_OFF;
static int s_pauseSaved = 1;    /* enable_game_pause on the title, put back there */
static int s_model = -1;        /* the table row viewed or loading */
static int s_sawChange;         /* the stage change has begun (systemStatus[6]) */
static unsigned int s_since;    /* the Main tick the state began */
static GObj *s_obj;             /* the object viewed */
static void (*s_objDl)(GObj *); /* its display function, wrapped */
static int *s_anims;            /* the motions the stage holds, host memory */
static int s_animCount;
static int s_sel;          /* the animation selected (index into s_anims) */
static int s_playing = -1; /* the motion id playing, -1 for none */
static int s_loop;
static unsigned int s_logTick;
static int s_logFrame;
static float s_yaw, s_pitch, s_dist, s_distMin, s_distMax;
static float s_centre[3]; /* the model's box centre, model space */
static int s_rooted;      /* the target follows the root (a skeleton) */
static char s_ovName[96], s_ovAnim[128], s_ovFrame[64];

/* --- the table rows the stage can show ------------------------------------- */

/* a motion's name as the game's table spells it */
static const char *motionName(int id)
{
    return id >= 0 && id < MV_MOTION_KINDS ? motionKind[id].name : "";
}

/* motionViewer.c's test: a dynamic or swap motion that is not loaded */
static int motionHeld(int id)
{
    if (id < 0 || id >= MV_MOTION_KINDS) {
        return 0;
    }
    return !(motionKind[id].area != 0 && motionKind[id].blendKind == 320 && motionTable[id] == 0);
}

static const char *modelName(int row)
{
    return row >= 0 && row < mv_modelCount ? ui_Str((UiStrId)mv_models[row].nameStr) : "";
}

static const char *modelNameEn(int row)
{
    return row >= 0 && row < mv_modelCount ? ui_StrIn(UI_LANG_EN, (UiStrId)mv_models[row].nameStr)
                                           : "";
}

/* --- the layouts ---------------------------------------------------------- */

static int s_listLayout = -1, s_viewLayout = -1;
static int s_rowName = -1, s_rowAnim = -1, s_rowFrame = -1;
static UiList s_list, s_animList;
/* the prompts (ui_hint.h): the model list's, and the viewer's two lines
   (the sticks; the buttons) */
static UiHint s_listHint, s_viewSticks, s_viewKeys;

static int listProc(int first, int item);
static int viewProc(int first, int item);

static LtProperty *P(int index)
{
    return lt_ext_Prop(index);
}

/* as settings.c's pages */
static int addLayout(int first, int last, float shade, int (*proc)(int, int), int def)
{
    LtProp l;
    memset(&l, 0, sizeof(l));
    l.first = first;
    l.last = last;
    l.fadeInTime = 0.3f;
    l.fadeOutTime = 0.1f;
    l.colA = shade;
    l.proc = proc;
    l.procFirst = 1;
    l.defaultItem = def;
    l.curItem = def;
    l.link = -1;
    return lt_ext_AddLayout(&l);
}

static int nextRow(void)
{
    return LT_GAME_PROPERTY_COUNT + lt_ext_PropCount();
}

/* the model list: the names, the hint in the status line */
static int listCount(void *user)
{
    (void)user;
    return mv_modelCount;
}

static void listFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    out->labelStr = mv_models[d].nameStr;
}

static void listDecorate(void *user, int d)
{
    (void)user;
    (void)d;
    /* Triangle: back to Extras from the title, to the title from a model */
    ui_HintSetStr(&s_listHint, UI_HINT_MV_LIST_BACK,
                  s_state == MV_LIST ? UI_STR_MV_HINT_TITLE : UI_STR_BACK);
    ui_HintLayout(&s_listHint);
}

static void mvStart(int row);
static void mvToTitle(void);
static void putBackShadow(void);

static int listInput(void *user, int d, int flags)
{
    (void)user;
    if (flags & PAD_BACK) {
        NEGATIVE_SE();
        la_host_leave();
        if (s_state == MV_LIST) {
            mvToTitle();
            return -1;
        }
        /* back on Extras' Models row */
        int to = ui_SettingsPageLayout(UI_PAGE_EXTRAS);
        int row = ui_SettingsRowOf(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_MODELS);
        if (to >= 0 && row >= 0) {
            lt_ext_Layout(to)->defaultItem = row;
        }
        return to;
    }
    if ((flags & PAD_CROSS) && d >= 0 && d < mv_modelCount) {
        POSITIVE_SE();
        la_host_leave();
        mvStart(d);
        return -1;
    }
    return UI_LIST_PASS;
}

static const UiListDef kListDef = {listCount, listFill, NULL, listInput, listDecorate};

/* the animations: the motion names as the game's table spells them */
static int animCount(void *user)
{
    (void)user;
    return s_animCount;
}

static void animFill(void *user, int d, UiListSlot *out)
{
    (void)user;
    out->label = motionName(s_anims[d]);
    out->colA = "";
}

static const UiListDef kAnimDef = {animCount, animFill, NULL, NULL, NULL};

static int built(void)
{
    return s_listLayout >= LT_GAME_LAYOUT_COUNT &&
           s_listLayout < LT_GAME_LAYOUT_COUNT + lt_ext_LayoutCount() &&
           lt_ext_Layout(s_listLayout)->proc == listProc;
}

static void build(void)
{
    UiListStyle st;
    int first, last;

    ui_FontInit();
    /* the model list: the Settings list pages' look (settings.c) */
    first = nextRow();
    int header = ui_SettingsAddRow(20, 12, 600, 40, 0, -1, UI_STR_EXTRAS_MODELS, NULL, 30.0f,
                                   UI_ALIGN_CENTER);
    P(header)->centerX = 1;
    memset(&st, 0, sizeof(st));
    st.y0 = 40;
    st.pitch = 18;
    st.label = (UiListCol){40, 560, 24.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){440, 160, 21.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    ui_ListBuild(&s_list, &kListDef, NULL, &st);
    /* the status line stays empty: the prompts take its place */
    ui_HintBuild(&s_listHint, 196, 19.0f, ui_hint_mv_list, UI_HINT_MV_LIST_COUNT);
    last = nextRow() - 1;
    s_listLayout = addLayout(first, last + 1, 0.6f, listProc, s_list.label[0]);

    /* the viewer: the animations at the right, the hint at the bottom, no
       shade over the picture */
    first = nextRow();
    memset(&st, 0, sizeof(st));
    st.y0 = 30;
    st.pitch = 15;
    st.label = (UiListCol){404, 216, 18.0f, UI_ALIGN_LEFT};
    st.colA = (UiListCol){620, 4, 18.0f, UI_ALIGN_RIGHT};
    st.statusY = 196;
    ui_ListBuild(&s_animList, &kAnimDef, NULL, &st);
    ui_HintBuild(&s_viewSticks, 180, 19.0f, ui_hint_mv_sticks, UI_HINT_MV_STICKS_COUNT);
    ui_HintBuild(&s_viewKeys, 196, 19.0f, ui_hint_mv_keys, UI_HINT_MV_KEYS_COUNT);
    /* the name, the animation and the frame at the top left, as Settings
       rows: a list label's size, then a note's */
    s_rowName = ui_SettingsAddRow(24, 10, 360, 30, 0, -1, 0, " ", 24.0f, UI_ALIGN_LEFT);
    s_rowAnim = ui_SettingsAddRow(24, 26, 360, 30, 0, -1, 0, " ", 19.0f, UI_ALIGN_LEFT);
    s_rowFrame = ui_SettingsAddRow(24, 38, 360, 30, 0, -1, 0, " ", 19.0f, UI_ALIGN_LEFT);
    last = nextRow() - 1;
    s_viewLayout = addLayout(first, last + 1, 0.0f, viewProc, s_animList.label[0]);
    fprintf(stderr, "model_viewer: layouts %d and %d (%d of %d properties in use)\n", s_listLayout,
            s_viewLayout, lt_ext_PropCount(), LT_EXT_MAX_PROPERTIES);
}

static int listProc(int first, int item)
{
    LtProp *lay = lt_ext_Layout(s_listLayout);
    (void)item;
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    if (first) {
        ui_ListReset(&s_list);
        if (s_model >= 0 && s_model < mv_modelCount) {
            /* the cursor on the model last viewed */
            int shown = mv_modelCount < UI_LIST_SLOTS ? mv_modelCount : UI_LIST_SLOTS;
            s_list.offset = s_model < shown ? 0 : s_model - shown + 1;
            lay->curItem = s_list.label[s_model - s_list.offset];
        }
    }
    ui_ListRefresh(&s_list, lay->curItem);
    if (lt_fade_status() != 2 || (s_state != MV_OFF && s_state != MV_LIST)) {
        return -1;
    }
    lt_analog2Pad();
    int r = ui_ListProc(&s_list, lay, pad[0].flags);
    ui_ListRefresh(&s_list, lay->curItem);
    return r;
}

/* the viewer's layout: its list follows the selection; the viewer's tick
   reads the pad, so the layout's own cursor moves are off */
static void refreshView(void)
{
    LtProp *lay = lt_ext_Layout(s_viewLayout);
    int shown = s_animCount < UI_LIST_SLOTS ? s_animCount : UI_LIST_SLOTS;
    if (s_sel < s_animList.offset) {
        s_animList.offset = s_sel;
    } else if (s_sel >= s_animList.offset + shown) {
        s_animList.offset = s_sel - shown + 1;
    }
    if (shown > 0) {
        lay->curItem = s_animList.label[s_sel - s_animList.offset];
    }
    ui_ListRefresh(&s_animList, lay->curItem);
    if (s_animCount == 0) {
        lt_ext_SetStr(s_animList.label[0], UI_STR_MV_NO_ANIMATIONS);
    }
    /* without animations only Triangle is left */
    for (int i = UI_HINT_MV_PLAY; i < UI_HINT_MV_BACK; i++) {
        ui_HintShow(&s_viewKeys, i, s_animCount > 0);
    }
    ui_HintLayout(&s_viewSticks);
    ui_HintLayout(&s_viewKeys);
    lt_ext_SetText(s_rowName, s_ovName);
    lt_ext_SetText(s_rowAnim, s_ovAnim);
    lt_ext_SetText(s_rowFrame, s_ovFrame);
}

static int viewProc(int first, int item)
{
    (void)first;
    (void)item;
    ui_SetLanguage(ui_LangFromGame(NonLinearCameraMove));
    lt_item_select_disable = 1;
    refreshView();
    return -1;
}

int ico_mv_models_enter(void)
{
    if (s_state != MV_OFF) {
        return -1;
    }
    if (!built()) {
        build();
    }
    return s_listLayout;
}

/* --- the viewer ------------------------------------------------------------ */

static void freeAnims(void)
{
    free(s_anims);
    s_anims = NULL;
    s_animCount = 0;
    s_sel = 0;
    s_playing = -1;
}

static void setState(int st)
{
    s_state = st;
    s_since = ico_host_main_ticks();
}

static void mvFailed(const char *why)
{
    fprintf(stderr, "model_viewer: failed: %s (model %d, stage %d)\n", why, s_model, stage_no);
    mvToTitle();
}

static void mvStart(int row)
{
    const MvModel *m = &mv_models[row];
    if (s_state == MV_OFF) {
        /* leaving the title: what closing Settings writes, the title music
           fading as for a new game */
        ui_SettingsSave();
        la_host_title_music_fade();
        s_pauseSaved = enable_game_pause;
    }
    putBackShadow();
    s_model = row;
    s_obj = NULL;
    s_objDl = NULL;
    s_sawChange = 0;
    freeAnims();
    ico_mv_active = 1;
    setState(MV_LOADING);
    fprintf(stderr, "model_viewer: loading stage %d for \"%s\"\n", m->stage, modelNameEn(row));
    /* the boy and the girl are built where the game's object records put
       them: there, as a start_stage boot puts them (its gflag 394 is one
       End Game's gflagInit clears) */
    ACTGame_SetActors_Debug(m->stage, 0);
    stgmgrForceSwitchWithFade(m->stage, 1.0f, 8.0f);
}

/* the viewer off: the draw filter, the pause menu and the flag given back */
static void mvLetGo(void)
{
#ifdef ICO_RD
    rd_SetDrawFilter(false, NULL, 0);
#endif
    ico_mv_active = 0;
    enable_game_pause = s_pauseSaved;
    s_model = -1;
    setState(MV_OFF);
}

static void mvToTitle(void)
{
    putBackShadow();
    freeAnims();
    s_obj = NULL;
    s_sawChange = 0;
    setState(MV_LEAVING);
    fprintf(stderr, "model_viewer: back to the title\n");
    la_host_end_game();
}

/* a shadow is drawn only once it has come out (enemy.c EnemyDL and
   DisplayEnemy: Act flags18 bit 33, which enemy_act.c's subEnemyCollision
   sets each tick once the shadow has a motion request, and the work's
   loaded flag, SetEnemyHitGeometryAction): the viewer's shadow never comes
   out, so both are set for it */

/* A shadow still in its generator is hidden by its layout row (enemy_act.c
   isEnemyHyde and actEnemyFlagCheckActive: the row's display bit 21 clear
   or its dead bit 18 set); of the survey's shadows only the plain one is
   out when its stage loads (stage 46).  The row is set to "out" while the
   shadow is viewed and put back as it was when the viewer leaves the
   stage (objLayout outlives the stage). */
#define GEN_DEAD 0x40000u
#define GEN_SHOWN 0x200000u
static int s_genRow = -1;
static unsigned int s_genFlags;

static void bringOutShadow(GObj *g)
{
    if (g->kind == 4 && g->labelId > 0) {
        s_genRow = g->labelId;
        s_genFlags = objLayout[s_genRow].flags;
        objLayout[s_genRow].flags = (s_genFlags | GEN_SHOWN) & ~GEN_DEAD;
    }
}

/* at each display, as its own threads put it back in the generator */
static void showShadow(void)
{
    if (s_obj != NULL && s_obj->kind == 4 && s_obj->act != NULL) {
        GOBJ_ACT(s_obj)->flags18.ll |= 1ULL << 33;
        SetEnemyHitGeometryAction(s_obj, 1);
        if (s_genRow == s_obj->labelId) {
            objLayout[s_genRow].flags = (objLayout[s_genRow].flags | GEN_SHOWN) & ~GEN_DEAD;
        }
    }
}

static void putBackShadow(void)
{
    if (s_genRow > 0) {
        objLayout[s_genRow].flags = s_genFlags;
        s_genRow = -1;
    }
}

/* the display list of the object viewed: what it draws is the model */
static void viewDl(GObj *g)
{
    showShadow();
#ifdef ICO_RD
    rd_DrawFilterOpen(true);
#endif
    if (s_objDl != NULL) {
        s_objDl(g);
    }
#ifdef ICO_RD
    rd_DrawFilterOpen(false);
#endif
}

static GObj *findObject(const MvModel *m)
{
    GObj *g;
    if (m->label > 0) {
        g = isysGObjSearchFromObjLayoutID(m->label);
        if (g != NULL && g->kind == m->kind && g->dobj != NULL && g->dobj->modelId == m->charId) {
            return g;
        }
    }
    for (g = isysGObjGetExist_begin(); g != NULL; g = isysGObjGetExist_next(g)) {
        if (g->kind == m->kind && g->dobj != NULL && g->dobj->modelId == m->charId) {
            return g;
        }
    }
    return NULL;
}

/* the model's box (model space) gives the orbit's centre and distance */
static void frameModel(GObj *g)
{
    const Sub15C *d = g->dobj;
    float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    float r = 60.0f;
    if (d->model != NULL) {
        const PObjModel *pm = d->model;
        for (int k = 0; k < 3; k++) {
            lo[k] = hi[k] = pm->box[0][k];
        }
        for (int i = 1; i < 8; i++) {
            for (int k = 0; k < 3; k++) {
                lo[k] = pm->box[i][k] < lo[k] ? pm->box[i][k] : lo[k];
                hi[k] = pm->box[i][k] > hi[k] ? pm->box[i][k] : hi[k];
            }
        }
        float dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
        float rr = 0.5f * sqrtf(dx * dx + dy * dy + dz * dz);
        /* the object's own scale (its first node's: the queen is drawn half
           as large again) */
        if (d->nodes != NULL && d->nodes[0].scale[0] > 0.1f && d->nodes[0].scale[0] < 20.0f) {
            rr *= d->nodes[0].scale[0];
        }
        if (rr > 1.0f && rr < 100000.0f) {
            r = rr;
        } else {
            lo[0] = lo[1] = lo[2] = hi[0] = hi[1] = hi[2] = 0.0f;
        }
    }
    for (int k = 0; k < 3; k++) {
        s_centre[k] = 0.5f * (lo[k] + hi[k]);
    }
    /* a skeleton is framed on its root, the middle of the body (the hips;
       the box runs from the feet), as measured on the boy, the girl and the
       shadows: the target follows the root through the animations */
    s_rooted = d->skelNodeNum > 0;
    s_dist = r * 2.0f / tanf(CAM_FOV * 0.5f * 3.14159265f / 180.0f);
    s_distMin = r * 1.2f;
    s_distMax = s_dist * 4.0f;
    s_pitch = 0.18f;
    s_yaw = 0.0f;
    if (s_rooted) {
        /* in front of it */
        float dir[4];
        GetRootOrient(dir, g);
        s_yaw = atan2f(dir[0], dir[2]);
    }
    fprintf(stderr,
            "model_viewer: box (%.0f %.0f %.0f)-(%.0f %.0f %.0f), radius %.0f, distance %.0f\n",
            lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], r, s_dist);
}

static void targetOf(float t[3])
{
    const Sub15C *d = s_obj->dobj;
    if (s_rooted) {
        float p[4];
        GetRootPosition(p, s_obj);
        t[0] = p[0];
        t[1] = p[1];
        t[2] = p[2];
    } else {
        /* the centre where the object's first node puts it (the matrix it
           is drawn with; a stage animation object's is the animation's),
           else its own matrix */
        const float (*mx)[4] = d->nodeNum > 0 && d->nodeMtx != 0
                                   ? (const float (*)[4])(const void *)(char *)d->nodeMtx
                                   : (const float (*)[4])d->matrix;
        for (int k = 0; k < 3; k++) {
            t[k] =
                mx[0][k] * s_centre[0] + mx[1][k] * s_centre[1] + mx[2][k] * s_centre[2] + mx[3][k];
        }
    }
}

static void placeCamera(void)
{
    float t[3];

    union {
        float f[12];
    } in;

    targetOf(t);
    memset(&in, 0, sizeof(in));
    float cp = cosf(s_pitch), sp = sinf(s_pitch);
    /* the model left of the picture's middle, clear of the list at the
       right: eye and target moved along the view's right */
    float side = s_dist * MODEL_SHIFT;
    float rx = -cosf(s_yaw) * side, rz = sinf(s_yaw) * side;
    in.f[0] = t[0] + s_dist * cp * sinf(s_yaw) + rx;
    in.f[1] = t[1] - s_dist * sp;
    in.f[2] = t[2] + s_dist * cp * cosf(s_yaw) + rz;
    in.f[4] = t[0] + rx;
    in.f[5] = t[1];
    in.f[6] = t[2] + rz;
    in.f[8] = CAM_FOV;
    CameraSetMode(0);
    SetWSMatrix(&in);
}

static float stick(int v)
{
    int d = v - 128;
    if (d > -STICK_DEAD && d < STICK_DEAD) {
        return 0.0f;
    }
    return (float)(d > 0 ? d - STICK_DEAD : d + STICK_DEAD) / (float)(128 - STICK_DEAD);
}

/* the backdrop, first in the frame's first list */
static void drawBackdrop(void)
{
    static const char kTag = 0;
    GifRect r = {-400, -140, 800, 280};
    GifColor c = {BACKDROP_GREY, BACKDROP_GREY, BACKDROP_GREY + 2, 0x80};
#ifdef ICO_RD
    int space = rd_SetSpaceOverride(RD_SPACE_FULLSCREEN);
    gif_HostDrawKey(&kTag, 0, 0);
#else
    (void)kTag;
#endif
    gif_StartPacketPri(0);
    gif_SetZWrite(0);
    gif_SetZTest(0);
    gif_Sprite(&r, 0, 0, &c, 0);
    gif_SetZTest(1);
    gif_SetZWrite(1);
    gif_EndPacket();
#ifdef ICO_RD
    gif_HostDrawKey(0, 0, 0);
    rd_SetSpaceOverride(space);
#endif
}

static int currentFrame(int *total)
{
    int mot = ForMotionViewer_GetCurrentMotion(s_obj);
    int n = motionHeld(mot) ? GetNbMotionFrames(mot) - 1 : 0;
    float f = ForMotionViewer_GetCurrentAnimationFrame(s_obj);
    *total = n > 0 ? n : 0;
    return f > 0.0f ? (int)f : 0;
}

static void play(void)
{
    const MvModel *m = &mv_models[s_model];
    if (s_animCount == 0) {
        return;
    }
    int id = s_anims[s_sel];
    DisableChangeRootUpdateMode(s_obj);
    DisableMotionOrientUpdate(s_obj);
    InitMotionOrient(s_obj, m->oriFrom, m->oriTo, -1, -1, id);
    s_playing = id;
    s_logTick = ico_host_main_ticks();
    fprintf(stderr, "model_viewer: motion \"%s\" frame 0/%d\n", motionName(id),
            GetNbMotionFrames(id) - 1);
    s_logFrame = 0;
}

static void setup(void)
{
    const MvModel *m = &mv_models[s_model];
    GObj *g = findObject(m);
    if (g == NULL) {
        mvFailed("the object is not in its stage");
        return;
    }
    /* every other object parked; the system objects (the camera, kind -1)
       run on */
    for (GObj *o = isysGObjGetExist_begin(); o != NULL; o = isysGObjGetExist_next(o)) {
        if (o != g && o->kind >= 0) {
            o->active = 0;
        }
    }
    /* the run lists on, as the game layout's first tick turns them on
       (layout_action.c la_game_loop), which the viewer's layout replaces */
    isysGObjActiveLink(0, 1);
    s_obj = g;
    g->active = 1;
    g->fn = 0;
    bringOutShadow(g);
    showShadow();
    s_objDl = g->dl;
    g->dl = viewDl;
    enable_game_pause = 0;
    GlobalStageSetting.fogOn = 0;
    /* the animations the stage holds */
    s_anims =
        malloc(sizeof(int) * (size_t)(m->motLast > m->motFirst ? m->motLast - m->motFirst : 1));
    s_animCount = 0;
    if (s_anims == NULL) {
        mvFailed("out of memory");
        return;
    }
    if (g->dobj->skelNodeNum > 0) {
        for (int id = m->motFirst; id < m->motLast; id++) {
            if (motionHeld(id)) {
                s_anims[s_animCount++] = id;
            }
        }
    }
    if (s_animCount > 0) {
        /* objMenuProc's set-up */
        Camctrl_SetTarget(g, 0, 3);
        SetParallelMotionTableWithNoRequest(g, 0, 0);
        g->dobj->ctrl.shiftStop = 1;
        SetMotionPlaySpeedRatio(g, 1.0f);
        SetRootUpdateMode(g, ROOT_ROT_ONLY);
        /* the motion it stands in, if it is one of them */
        int cur = ForMotionViewer_GetCurrentMotion(g);
        for (int i = 0; i < s_animCount; i++) {
            if (s_anims[i] == cur) {
                s_sel = i;
            }
        }
    }
    frameModel(g);
#ifdef ICO_RD
    {
        const void *own[1] = {g->dobj};
        rd_SetDrawFilter(true, own, 1);
    }
#endif
    s_loop = 0;
    s_playing = -1;
    setState(MV_VIEW);
    ui_ListReset(&s_animList);
    fprintf(stderr, "model_viewer: stage %d loaded id %d \"%s\" (%d animations)\n", stage_no,
            m->charId, modelNameEn(s_model), s_animCount);
}

static void viewInput(void)
{
    int flags = pad[0].flags;
    /* the right stick (ana[0] x, ana[1] y) orbits, the left stick's vertical
       axis (ana[3]) zooms; the signs are as they were when the sticks were
       the other way round */
    s_yaw -= stick(pad[0].ana[0]) * YAW_RATE;
    s_pitch += stick(pad[0].ana[1]) * PITCH_RATE;
    s_pitch = s_pitch > PITCH_MAX ? PITCH_MAX : s_pitch < -PITCH_MAX ? -PITCH_MAX : s_pitch;
    s_dist *= expf(stick(pad[0].ana[3]) * ZOOM_RATE);
    s_dist = s_dist < s_distMin ? s_distMin : s_dist > s_distMax ? s_distMax : s_dist;
    if (lt_fade_status() != 2 || current_layout_id != s_viewLayout) {
        return;
    }
    if (flags & PAD_BACK) {
        NEGATIVE_SE();
        setState(MV_LIST);
        lt_switch_layout(s_listLayout);
        return;
    }
    if (s_animCount == 0) {
        return;
    }
    if (flags & (PAD_L1 | PAD_UP)) {
        s_sel = (s_sel + s_animCount - 1) % s_animCount;
        CUR_SE();
    }
    if (flags & (PAD_R1 | PAD_DOWN)) {
        s_sel = (s_sel + 1) % s_animCount;
        CUR_SE();
    }
    if (flags & PAD_CROSS) {
        POSITIVE_SE();
        play();
    }
    if (flags & PAD_SQUARE) {
        s_loop = !s_loop;
        CUR_SE();
    }
}

static void viewTick(void)
{
    int total = 0, frame = 0;
    if (s_state == MV_VIEW) {
        if (current_layout_id != s_viewLayout && lt_fade_status() == 2) {
            lt_switch_layout(s_viewLayout);
        }
        viewInput();
    }
    if (s_state != MV_VIEW && s_state != MV_LIST) {
        return;
    }
    enable_game_pause = 0;
    showShadow();
    if (s_animCount > 0) {
        frame = currentFrame(&total);
        int mot = ForMotionViewer_GetCurrentMotion(s_obj);
        if (s_loop && s_playing >= 0 &&
            (mot != s_playing || s_obj->dobj->ctrl.frameEnd != 0 || frame >= total)) {
            play();
            mot = ForMotionViewer_GetCurrentMotion(s_obj);
            frame = currentFrame(&total);
        }
        /* while it plays: once a second, while the frame moves */
        if (s_playing >= 0 && ico_host_main_ticks() - s_logTick >= LOG_EVERY_TICKS &&
            frame != s_logFrame) {
            s_logTick = ico_host_main_ticks();
            s_logFrame = frame;
            fprintf(stderr, "model_viewer: motion \"%s\" frame %d/%d\n", motionName(mot), frame,
                    total);
        }
        snprintf(s_ovAnim, sizeof(s_ovAnim), "%s: %s%s%s", ui_Str(UI_STR_MV_ANIMATION),
                 motionName(mot), s_loop ? "  \xC2\xB7  " : "",
                 s_loop ? ui_Str(UI_STR_MV_LOOP) : "");
        snprintf(s_ovFrame, sizeof(s_ovFrame), "%s %d / %d", ui_Str(UI_STR_MV_FRAME), frame, total);
    } else {
        snprintf(s_ovAnim, sizeof(s_ovAnim), "%s", ui_Str(UI_STR_MV_NO_ANIMATIONS));
        s_ovFrame[0] = '\0';
    }
    snprintf(s_ovName, sizeof(s_ovName), "%s", modelName(s_model));
    placeCamera();
    drawBackdrop();
}

void ico_mv_tick(void)
{
    static int hooked;
    if (!hooked) {
        hooked = 1;
        ui_SettingsSetModelsHandler(ico_mv_models_enter);
    }
    unsigned int now = ico_host_main_ticks();
    switch (s_state) {
    case MV_OFF:
        return;
    case MV_LOADING:
        if (systemStatus[6] != 0 && !s_sawChange) {
            s_sawChange = 1;
#ifdef ICO_RD
            /* the stage being left is behind the fade: from here no world
               draw is kept until the model is found */
            rd_SetDrawFilter(true, NULL, 0);
#endif
        }
        if (s_sawChange && systemStatus[6] == 0 && stage_no == mv_models[s_model].stage) {
            setup();
        } else if (now - s_since > LOAD_TIMEOUT_TICKS) {
            mvFailed("the stage did not load");
        }
        return;
    case MV_VIEW:
    case MV_LIST:
        if (stage_no != mv_models[s_model].stage || systemStatus[6] != 0) {
            /* a stage change the viewer did not ask for: its objects are
               gone; the host stage again starts it over, any other stage
               times out to the title */
            fprintf(stderr, "model_viewer: the stage changed under the viewer\n");
            putBackShadow();
#ifdef ICO_RD
            rd_SetDrawFilter(true, NULL, 0);
#endif
            s_obj = NULL;
            s_objDl = NULL;
            freeAnims();
            s_sawChange = 1;
            setState(MV_LOADING);
            return;
        }
        viewTick();
        return;
    case MV_LEAVING:
        if (systemStatus[6] != 0) {
            s_sawChange = 1;
        }
        if (s_sawChange && systemStatus[6] == 0 && stage_no == TITLE_STAGE) {
            mvLetGo();
            fprintf(stderr, "model_viewer: the title is back\n");
        } else if (now - s_since > LEAVE_TIMEOUT_TICKS) {
            /* the title never came: the draw filter, the pause and the flag
               given back where the game stands */
            mvLetGo();
            fprintf(stderr, "model_viewer: failed: the title did not come back (stage %d)\n",
                    stage_no);
        }
        return;
    }
}
