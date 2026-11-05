/*
 * port/ui/settings_states.c
 *
 * Settings > Extras > Save states (settings_states.h): the host's listing
 * and requests, the four cards' texts and pictures, the actions on a slot
 * with their second press, and F6's latch.  settings.c builds the page (its
 * rows, handed here), opens and leaves it and runs its tick through the
 * ui__states_* calls.
 *
 * The pause menu's Save states row (placePause) and the Continue screen's
 * prompt (contProc), when a UiStatesHost was set before the menus were
 * built.  Four cards on settings.c's ST_PITCH field-line pitch from ST_Y0 (the
 * 226-line window: the last card's date ends at 204, the status line below
 * it): the slot's picture (an image row, 72 x 54), "Slot N" (selectable)
 * with the action at the
 * right (Save | Load | Delete, stepped by Left and Right; Save only where
 * the host says saves are allowed, Load and Delete only on a slot in use),
 * the area and the play time, and the date.  Up and Down move between the
 * cards, Cross does the action; Save over a slot in use and Delete ask for
 * a second Cross within two seconds (the status line says so).  The
 * pictures are textures the page makes when it opens or a slot's picture
 * changes and releases when it closes (ui__states_release).  The rows are the
 * page's rows[]: the label row of each slot (UI_OPT_STATE_SLOT) and its
 * action row as the value row; the other rows of a card are kept here. */

#include "settings.h"
#include "settings_states.h"

#include "ico_unused.h" /* a test room of Extras > Unused content: no save */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "layout_ext.h"
#include "popup.h"
#include "strings.h"

#ifdef ICO_RD
#include "rd.h"      /* rd_create_texture */
#include "texpack.h" /* the state pictures: texpack_load_png */
#endif

extern int frame_count;   /* common/src/main.c: one a flip, a Main tick */
extern void CUR_SE(void); /* layout_action.c: the menus' sounds */
extern void POSITIVE_SE(void);
extern void NEGATIVE_SE(void);

/* The Save states host (settings.h).  The pause menu's Save states row and
   the Continue screen's prompt are built only when it was set before the
   menus were built (ui_settings_install: main_host.c sets it first), so a
   build without one has neither; the page itself is always built and
   unreachable then. */
static UiStatesHost s_statesHost;
static int s_statesHostSet;

#define ST_CONFIRM_SECONDS 2
#define ST_RELIST_TICKS 30 /* the listing is cheap; the picture appears a few frames after a save */
#define ST_DATE_FORMAT "%Y-%m-%d %H:%M"

typedef struct StatesCard {
    int image, label, action, arrowL, arrowR, info1, info2;
} StatesCard;

static StatesCard s_stCard[UI_STATES_SLOTS];
static int s_stStatus = -1;
static UiStateSlotInfo s_stInfo[UI_STATES_SLOTS];
static int s_stAct[UI_STATES_SLOTS]; /* UI_STATES_ACT_*, -1: none chosen yet */
static int s_stOpen;                 /* the page is up: listing and pictures held */
static int s_stRelist;               /* list again at the next tick */
static int s_stListAge;
static int s_stConfirmSlot = -1, s_stConfirmAct, s_stConfirmAge;
static int s_stCanSave;     /* the host's can_save at the last refresh or listing */
static char s_stShown[320]; /* the status line's text as set */
/* the pictures: texture, size, and the file and time they were made from */
static unsigned int s_stTex[UI_STATES_SLOTS];
static int s_stTexW[UI_STATES_SLOTS], s_stTexH[UI_STATES_SLOTS];
static char s_stPng[UI_STATES_SLOTS][260];
static long long s_stPngAt[UI_STATES_SLOTS];
static UiStatesTexFuncs s_stTexFuncs; /* load NULL: the default (or none) */

static int stepIndex(int i, int n, int dir)
{
    return ((i + dir) % n + n) % n;
}

void ui_settings_set_states_host(const UiStatesHost *host)
{
    s_statesHostSet =
        host != NULL && host->list != NULL && host->request != NULL && host->can_save != NULL;
    if (s_statesHostSet) {
        s_statesHost = *host;
    }
}

int ui__states_hosted(void)
{
    return s_statesHostSet;
}

void ui__states_set_card(int slot, const int rows[7])
{
    StatesCard *c = &s_stCard[slot];
    c->image = rows[0];
    c->label = rows[1];
    c->action = rows[2];
    c->arrowL = rows[3];
    c->arrowR = rows[4];
    c->info1 = rows[5];
    c->info2 = rows[6];
    s_stAct[slot] = -1;
}

void ui__states_set_status(int row)
{
    s_stStatus = row;
    s_stShown[0] = '\0';
}

#ifdef ICO_RD
/* PC port: the renderer's own: the file read whole, decoded by the texture
   pack's PNG reader, uploaded as an RGBA8 texture (ico_fopen: host_fs.h,
   UTF-8 paths on Windows) */
extern FILE *ico_fopen(const char *path, const char *mode);

static unsigned int statesDefaultLoad(const char *path, int *w, int *h)
{
    enum { MAX_PNG = 8 << 20 };

    FILE *f = ico_fopen(path, "rb");
    unsigned int id = 0;
    if (f == NULL) {
        return 0;
    }
    uint8_t *buf = malloc(MAX_PNG);
    const size_t n = buf ? fread(buf, 1, MAX_PNG, f) : 0;
    fclose(f);
    TexpackImage img;
    if (n > 0 && texpack_load_png(buf, n, path, &img) == 0) {
        if (img.lv[0].pitch == img.w * 4u) {
            id = rd_create_texture(img.w, img.h, img.lv[0].data, RD_TEXA_80_80, "state picture").id;
            *w = (int)img.w;
            *h = (int)img.h;
        }
        texpack_free_image(&img);
    }
    free(buf);
    return id;
}

static void statesDefaultDestroy(unsigned int tex)
{
    RdTex t = {tex};
    rd_destroy_texture(t);
}
#endif

void ui_settings_set_states_tex_funcs(const UiStatesTexFuncs *funcs)
{
    ui__states_release();
    memset(&s_stTexFuncs, 0, sizeof(s_stTexFuncs));
    if (funcs != NULL && funcs->load != NULL && funcs->destroy != NULL) {
        s_stTexFuncs = *funcs;
    }
}

static unsigned int statesTexLoad(const char *path, int *w, int *h)
{
    *w = *h = 0;
    if (s_stTexFuncs.load != NULL) {
        return s_stTexFuncs.load(path, w, h);
    }
#ifdef ICO_RD
    return statesDefaultLoad(path, w, h);
#else
    return 0;
#endif
}

static void statesTexDestroy(unsigned int tex)
{
    if (s_stTexFuncs.destroy != NULL) {
        s_stTexFuncs.destroy(tex);
        return;
    }
#ifdef ICO_RD
    statesDefaultDestroy(tex);
#endif
}

/* one slot's picture released (the row first, then the texture) */
static void statesDropPicture(int slot)
{
    if (s_stCard[slot].image >= 0) {
        lt_ext_set_image(s_stCard[slot].image, 0, 0, 0);
    }
    if (s_stTex[slot] != 0) {
        statesTexDestroy(s_stTex[slot]);
    }
    s_stTex[slot] = 0;
    s_stTexW[slot] = s_stTexH[slot] = 0;
    s_stPng[slot][0] = '\0';
    s_stPngAt[slot] = 0;
}

/* the page closed: every picture released, the listing forgotten */
void ui__states_release(void)
{
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        statesDropPicture(i);
    }
    s_stOpen = 0;
    s_stConfirmSlot = -1;
    memset(s_stInfo, 0, sizeof(s_stInfo));
}

/* a slot's picture follows its listing: made when a file appears or was
   written again, dropped when the slot empties */
static void statesPictureSync(int slot)
{
    const UiStateSlotInfo *in = &s_stInfo[slot];
    const char *want = in->present ? in->png_path : "";
    if (strcmp(want, s_stPng[slot]) == 0 && (want[0] == '\0' || in->saved_at == s_stPngAt[slot])) {
        return;
    }
    statesDropPicture(slot);
    snprintf(s_stPng[slot], sizeof(s_stPng[slot]), "%s", want);
    s_stPngAt[slot] = in->saved_at;
    if (want[0] != '\0') {
        int w, h;
        const unsigned int tex = statesTexLoad(want, &w, &h);
        if (tex != 0) {
            s_stTex[slot] = tex;
            s_stTexW[slot] = w;
            s_stTexH[slot] = h;
            lt_ext_set_image(s_stCard[slot].image, tex, w, h);
        } else {
            fprintf(stderr, "state: the picture of slot %d could not be loaded (%s)\n", slot + 1,
                    want);
        }
    }
}

/* whether saves are allowed now: asked at each listing and refresh, not at
   each use */
static void statesCanSaveUpdate(void)
{
    s_stCanSave = s_statesHostSet && s_statesHost.can_save() != 0;
}

static int statesCanSave(void)
{
    return s_stCanSave;
}

static void statesList(void)
{
    memset(s_stInfo, 0, sizeof(s_stInfo));
    if (s_statesHostSet) {
        s_statesHost.list(s_stInfo);
    }
    statesCanSaveUpdate();
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        statesPictureSync(i);
    }
    s_stRelist = 0;
    s_stListAge = 0;
}

static int statesActAvail(int slot, int act)
{
    return act == UI_STATES_ACT_SAVE ? statesCanSave() : s_stInfo[slot].present != 0;
}

/* the action the slot shows: the one chosen if it is still possible, else
   the first possible one (Save, Load, Delete); -1 when there is none */
static int statesActOf(int slot)
{
    if (s_stAct[slot] >= 0 && statesActAvail(slot, s_stAct[slot])) {
        return s_stAct[slot];
    }
    for (int a = UI_STATES_ACT_SAVE; a <= UI_STATES_ACT_DELETE; a++) {
        if (statesActAvail(slot, a)) {
            return a;
        }
    }
    return -1;
}

static int statesActCount(int slot)
{
    int n = 0;
    for (int a = UI_STATES_ACT_SAVE; a <= UI_STATES_ACT_DELETE; a++) {
        n += statesActAvail(slot, a);
    }
    return n;
}

static int statesActStr(int act)
{
    return act == UI_STATES_ACT_SAVE   ? UI_STR_STATES_ACT_SAVE
           : act == UI_STATES_ACT_LOAD ? UI_STR_STATES_ACT_LOAD
                                       : UI_STR_STATES_ACT_DELETE;
}

/* Left (-1) or Right (+1) on a slot: the next possible action */
static void statesStepAct(int slot, int dir)
{
    int a = statesActOf(slot);
    for (int k = 0; k < 3 && a >= 0; k++) {
        a = stepIndex(a, 3, dir);
        if (statesActAvail(slot, a)) {
            s_stAct[slot] = a;
            return;
        }
    }
}

/* the slot the cursor is on, -1 for none */
int ui__states_cursor_slot(int cur)
{
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        if (s_stCard[i].label == cur) {
            return i;
        }
    }
    return -1;
}

int ui__states_is_open(void)
{
    return s_stOpen;
}

void ui__states_open(void)
{
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        s_stAct[i] = -1;
    }
    s_stOpen = 1;
    statesList();
}

void ui__states_reset(void)
{
    ui__states_release();
    memset(s_stCard, 0xFF, sizeof(s_stCard)); /* every row -1 */
    s_stStatus = -1;
    s_stRelist = s_stCanSave = 0;
}

/* the status line: text as set, wrapped at the note width (setNote's wrap,
   but for a text made at run time) */
static void statesStatus(const char *text)
{
    if (s_stStatus < 0 || strcmp(text, s_stShown) == 0) {
        return;
    }
    char buf[320];
    ui__settings_wrap_note(text, 580.0f, buf, sizeof(buf));
    lt_ext_set_text(s_stStatus, buf);
    snprintf(s_stShown, sizeof(s_stShown), "%s", text);
}

/* a refusal string as a line: a "%s" (the detail the host's popup adds) and
   the punctuation before it cut off */
static void statesPlain(int strId, char *out, size_t size)
{
    snprintf(out, size, "%s", ui_str((UiStrId)strId));
    char *pc = strstr(out, "%s");
    if (pc == NULL) {
        return;
    }
    *pc = '\0';
    size_t n = strlen(out);
    while (n > 0) {
        const unsigned char *e = (const unsigned char *)out + n;
        if (e[-1] == ' ' || e[-1] == ':') {
            n -= 1;
        } else if (n >= 3 && e[-3] == 0xEF && e[-2] == 0xBC && e[-1] == 0x9A) {
            n -= 3; /* the full-width colon */
        } else if (n >= 3 && e[-3] == 0xE3 && e[-2] == 0x80 && e[-1] == 0x80) {
            n -= 3; /* the ideographic space */
        } else {
            break;
        }
    }
    out[n] = '\0';
}

static void statesMask(int row, int masked)
{
    lt_ext_prop(row)->defaultMask = masked;
    lt_mask_property(row, masked);
}

/* The cards' texts and the status line from the listing and the cursor */
void ui__states_refresh(int cur)
{
    char buf[320], tmp[160];
    const int on = ui__states_cursor_slot(cur);
    statesCanSaveUpdate();
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        const StatesCard *c = &s_stCard[i];
        const UiStateSlotInfo *in = &s_stInfo[i];
        const int act = statesActOf(i);
        snprintf(buf, sizeof(buf), ui_str(UI_STR_STATES_SLOT), i + 1);
        lt_ext_set_text(c->label, buf);
        lt_ext_set_text(c->action, act >= 0 ? ui_str((UiStrId)statesActStr(act)) : " ");
        const int arrows = statesActCount(i) >= 2;
        statesMask(c->arrowL, !arrows);
        statesMask(c->arrowR, !arrows);
        if (!in->present) {
            lt_ext_set_text(c->info1, ui_str(UI_STR_STATES_EMPTY));
            lt_ext_set_text(c->info2, " ");
        } else {
            const char *area = ui__settings_area_name(in->stage_no);
            ui__settings_play_time(in->play_frames, tmp, sizeof(tmp));
            if (area != NULL) {
                snprintf(buf, sizeof(buf), "%s  %s", area, tmp);
            } else {
                snprintf(buf, sizeof(buf), "%s", tmp);
            }
            lt_ext_set_text(c->info1, buf);
            const time_t t = (time_t)in->saved_at;
            struct tm lt;
            buf[0] = '\0';
#if defined(_WIN32)
            if (localtime_s(&lt, &t) == 0) {
                strftime(buf, sizeof(buf), ST_DATE_FORMAT, &lt);
            }
#else
            if (localtime_r(&t, &lt) != NULL) {
                strftime(buf, sizeof(buf), ST_DATE_FORMAT, &lt);
            }
#endif
            lt_ext_set_text(c->info2, buf[0] ? buf : " ");
        }
        /* a state that cannot load now is greyed */
        lt_ext_set_dim(c->info1, in->present && !in->loadable);
        lt_ext_set_dim(c->info2, in->present && !in->loadable);
    }
    /* the status line: the second press asked for, the reason the slot
       under the cursor cannot load, what the page is for */
    if (on >= 0 && s_stConfirmSlot == on) {
        const char *word = ui_str((UiStrId)statesActStr(s_stConfirmAct));
        char w[64];
        snprintf(w, sizeof(w), "%s", word);
        if (w[0] >= 'A' && w[0] <= 'Z') {
            w[0] = (char)(w[0] - 'A' + 'a'); /* mid-sentence in English; no case elsewhere */
        }
        snprintf(buf, sizeof(buf), ui_str(UI_STR_STATES_CONFIRM), w);
        statesStatus(buf);
    } else if (on >= 0 && s_stInfo[on].present && !s_stInfo[on].loadable &&
               s_stInfo[on].refusal_str > 0) {
        statesPlain(s_stInfo[on].refusal_str, buf, sizeof(buf));
        statesStatus(buf);
    } else {
        statesStatus(
            ui_str(ui__settings_on_title() ? UI_STR_STATES_TITLE_HINT : UI_STR_STATES_NOTE));
    }
}

int ui_settings_states_card(int slot, int out[8])
{
    if (!ui__settings_built() || slot < 0 || slot >= UI_STATES_SLOTS || s_stCard[slot].label < 0) {
        return 0;
    }
    const StatesCard *c = &s_stCard[slot];
    const int v[8] = {c->image,  c->label, c->action, c->arrowL,
                      c->arrowR, c->info1, c->info2,  s_stStatus};
    memcpy(out, v, sizeof(v));
    return 8;
}

/* F6 (port/platform/hotkeys.h ICO_HOTKEY_STATES_PAGE): a press latched as
   the flip it came at (frame_count + 1; 0 none) and taken within
   ST_KEY_TICKS of it by the pause menu once its fade-in is done (entryProc:
   the page opens) or by the page (back to the game), so the pause menu a
   press in play opens through Start has time to come up.  Its lines, the
   ignored ones and the others each: the first 8, then every 100th */
#define ST_KEY_TICKS 60
static int s_stKeyAt;
static unsigned s_stKeyLines[2];

int ui__states_key_line(int ignored)
{
    const unsigned n = ++s_stKeyLines[ignored != 0];
    return n <= 8 || n % 100 == 0;
}

/* the latch taken: 1 when a press is waiting and recent; a late one is
   dropped with its line */
int ui__states_key_take(void)
{
    const int at = __atomic_exchange_n(&s_stKeyAt, 0, __ATOMIC_ACQ_REL);
    if (at == 0) {
        return 0;
    }
    const int age = frame_count - (at - 1);
    if (age >= 0 && age <= ST_KEY_TICKS) {
        return 1;
    }
    if (ui__states_key_line(1)) {
        fprintf(stderr, "hotkeys: F6 ignored: the pause menu did not come up within %d frames\n",
                ST_KEY_TICKS);
    }
    return 0;
}

void ui__states_key_arm(void)
{
    __atomic_store_n(&s_stKeyAt, frame_count + 1, __ATOMIC_RELEASE);
}

/* Cross on a slot: the action shown, after a second Cross within
   ST_CONFIRM_SECONDS when it overwrites or deletes a state */
static void statesPerform(int slot)
{
    const int act = statesActOf(slot);
    if (act < 0) {
        NEGATIVE_SE();
        return;
    }
    const int destroys =
        act == UI_STATES_ACT_DELETE || (act == UI_STATES_ACT_SAVE && s_stInfo[slot].present);
    if (destroys && !(s_stConfirmSlot == slot && s_stConfirmAct == act)) {
        s_stConfirmSlot = slot;
        s_stConfirmAct = act;
        s_stConfirmAge = 0;
        CUR_SE();
        return;
    }
    s_stConfirmSlot = -1;
    if (act == UI_STATES_ACT_SAVE && ico_unused_active()) {
        /* a test room has no save stage, and a state taken there would
           come back without the room's flag (its game-over guard, its
           return to the title): refused before the host; a load goes on */
        NEGATIVE_SE();
        fprintf(stderr, "unused: save state refused in a test room (slot %d)\n", slot + 1);
        (void)ui_popup_push(ui_str(UI_STR_STATES), ui_str(UI_STR_STATES_REFUSED_UNUSED));
        return;
    }
    int refusal = 0;
    const int rc = s_statesHost.request(act, slot + 1, &refusal);
    if (rc != 0) {
        NEGATIVE_SE();
        (void)ui_popup_push(ui_str(UI_STR_STATES),
                            ui_str(refusal > 0 ? (UiStrId)refusal : UI_STR_STATES_OFF));
        return;
    }
    POSITIVE_SE();
    s_stRelist = 1;
}

void ui__states_tick(int slot)
{
    if (s_stConfirmSlot >= 0 && ++s_stConfirmAge > ST_CONFIRM_SECONDS * ui_popup_vsync_hz()) {
        s_stConfirmSlot = -1;
    }
    if (s_stConfirmSlot >= 0 && slot != s_stConfirmSlot) {
        s_stConfirmSlot = -1; /* the cursor moved off */
    }
    if (s_stRelist || ++s_stListAge >= ST_RELIST_TICKS) {
        statesList();
    }
}

void ui__states_input(int slot, int dir, int ok)
{
    if (slot >= 0 && dir != 0 && statesActCount(slot) >= 2) {
        statesStepAct(slot, dir);
        s_stConfirmSlot = -1;
        CUR_SE();
    }
    if (slot >= 0 && ok) {
        statesPerform(slot);
    }
}
