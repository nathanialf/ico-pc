/* settings_states_test.c: the Save states page, the pause menu's row and the
 * Extras link (settings.c, "the Save states page"), over the shared fixture
 * (settings_fixture.h) with a fake UiStatesHost and fake picture textures.
 * CPU only; the host's own work (files, memory) is state_host_test's. */
#include "settings_fixture.h"
#include "popup.h"

#include <time.h>

/* ---------------------------------------------------------- the fakes */

static UiStateSlotInfo s_slots[UI_STATES_SLOTS];
static int s_canSave;
static int s_listCalls, s_requests, s_reqAct, s_reqSlot, s_reqRet, s_reqRefusal;
/* the frame of the last listing and of the last request */
static int s_listFrame, s_reqFrame;
/* the textures: load numbers them from 100, destroy counts */
static int s_loads, s_destroys, s_lastDestroyed;
static char s_lastLoaded[260];

static void fakeList(UiStateSlotInfo out[UI_STATES_SLOTS])
{
    s_listCalls++;
    s_listFrame = frame_count;
    memcpy(out, s_slots, sizeof(s_slots));
}

static int fakeRequest(int action, int slot, int *refusal_str)
{
    s_requests++;
    s_reqFrame = frame_count;
    s_reqAct = action;
    s_reqSlot = slot;
    *refusal_str = s_reqRet != 0 ? s_reqRefusal : 0;
    return s_reqRet;
}

static int fakeCanSave(void)
{
    return s_canSave;
}

static unsigned int fakeLoad(const char *path, int *w, int *h)
{
    snprintf(s_lastLoaded, sizeof(s_lastLoaded), "%s", path);
    *w = 320;
    *h = 240;
    return 100u + (unsigned)s_loads++;
}

static void fakeDestroy(unsigned int tex)
{
    s_destroys++;
    s_lastDestroyed = (int)tex;
}

static const UiStatesHost kHost = {fakeList, fakeRequest, fakeCanSave};
static const UiStatesTexFuncs kTex = {fakeLoad, fakeDestroy};

/* slot 1 in use and loadable, 2 empty, 3 in use but from another version
   (with a picture), 4 in use and loadable (with a picture); 2023-11-14
   22:13:20 UTC, 1 h 2 min 5 s of play */
static void fillSlots(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_slots[0].present = s_slots[0].loadable = 1;
    s_slots[0].saved_at = 1700000000LL;
    s_slots[1].present = 0;
    s_slots[2].present = 1;
    s_slots[2].loadable = 0;
    s_slots[2].refusal_str = UI_STR_STATES_REFUSED_BUILD;
    s_slots[2].saved_at = 1700000060LL;
    snprintf(s_slots[2].png_path, sizeof(s_slots[2].png_path), "slot3.png");
    s_slots[3].present = s_slots[3].loadable = 1;
    s_slots[3].saved_at = 1700000120LL;
    snprintf(s_slots[3].png_path, sizeof(s_slots[3].png_path), "slot4.png");
}

extern int systemStatus[12];

/* the play time as the card shows it for 1 h 2 min 5 s */
static unsigned playFrames(void)
{
    const int s1 = systemStatus[1] > 0 ? systemStatus[1] : 1;
    const int fps = ((60 - systemStatus[0] * 10) / s1) * s1;
    return (unsigned)fps * (3600u + 120u + 5u);
}

static void resetFakes(void)
{
    ui_popup_reset();
    fillSlots();
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        s_slots[i].play_frames = s_slots[i].present ? playFrames() : 0;
    }
    s_canSave = 1;
    s_listCalls = s_requests = s_reqAct = s_reqSlot = s_reqRet = s_reqRefusal = 0;
    s_listFrame = s_reqFrame = 0;
    s_loads = s_destroys = s_lastDestroyed = 0;
    s_lastLoaded[0] = '\0';
}

/* ------------------------------------------------------------ helpers */

static int s_errSaved = -1;

static void errCapture(void)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    s_errSaved = dup(2);
    FILE *f = freopen(p, "wb", stderr);
    (void)f;
}

static void errRelease(char *out, size_t n)
{
    char p[1100];
    path(p, sizeof(p), "settings_test_stderr.txt");
    fflush(stderr);
    dup2(s_errSaved, 2);
    close(s_errSaved);
    clearerr(stderr);
    out[0] = '\0';
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t got = fread(out, 1, n - 1, f);
        out[got] = '\0';
        fclose(f);
    }
}

/* the pause menu or the title built fresh, with or without the host (it is
   read when the menus are built) */
static void startMenus(int withHost, int stage)
{
    ui_settings_set_states_host(withHost ? &kHost : NULL);
    ui_settings_set_states_tex_funcs(&kTex);
    stage_no = stage;
    resetFakes();
    useConfig("version = 1\n");
    fakeTables();
    lt_ext_reset();
    ui_settings_reset();
    memset(pad, 0, sizeof(pad));
    pad[0].ana[0] = pad[0].ana[1] = pad[0].ana[2] = pad[0].ana[3] = 128;
    NonLinearCameraMove = 2;
    ui_set_language(UI_LANG_EN);
    init_layout_texture(2);
    settle(fxLayout(54), 4);
}

static void toPause(void)
{
    lt_switch_layout(fxLayout(57));
    CHECK(settle(fxLayout(57), 40), "the pause menu");
}

/* the page opened from the pause menu's row */
static int pauseToStates(void)
{
    toPause();
    texLayout[fxLayout(57)].curItem = ui_settings_states_row();
    press(lt_ext_confirm_button());
    const int l = ui_settings_page_layout(UI_PAGE_STATES);
    CHECK(settle(l, 60), "the Save states page from the pause menu (%d)", current_layout_id);
    return l;
}

/* the page opened from the title: Settings, Extras, Save states */
static int titleToStates(void)
{
    const int t = fxLayout(13);
    lt_switch_layout(t);
    CHECK(settle(t, 60), "the title");
    press(0x4000);
    press(lt_ext_confirm_button());
    const int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    CHECK(settle(mainL, 60), "the title opens Settings");
    const int exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    int labels[16];
    const int n = ui_settings_page_rows(UI_PAGE_EXTRAS, labels, NULL, NULL, 16);
    int link = -1;
    for (int i = 0; i < n; i++) {
        if (lt_ext_prop(labels[i])->right == ui_settings_page_layout(UI_PAGE_STATES)) {
            link = labels[i];
        }
    }
    CHECK(link >= 0, "the Extras page links the Save states page");
    lt_ext_layout(exL)->curItem = link;
    press(lt_ext_confirm_button());
    const int l = ui_settings_page_layout(UI_PAGE_STATES);
    CHECK(settle(l, 60), "the Save states page from Extras (%d)", current_layout_id);
    return l;
}

static const char *txt(int row)
{
    return lt_ext_row_text(row);
}

static int cardRows(int slot, int out[8])
{
    const int n = ui_settings_states_card(slot, out);
    CHECK(n == 8, "slot %d: the card's rows (%d)", slot, n);
    return n == 8;
}

static int cursorOn(int statesL)
{
    return lt_ext_layout(statesL)->curItem;
}

static void moveTo(int statesL, int slot)
{
    int c[8];
    if (cardRows(slot, c)) {
        lt_ext_layout(statesL)->curItem = c[1];
        frame(0);
    }
}

static void idle(int frames)
{
    for (int i = 0; i < frames; i++) {
        frame(0);
    }
}

/* ------------------------------------------------------------- tests */

/* the pause menu's port rows: Options 50, Photo mode 66, Save states 82,
   Back 98, End Game 120 with a stage and the host; the game's pitch
   without the host; the menus' own check (pauseRowsOk) still accepts the
   tables when they are installed again with the rows moved */
static void testPauseRows(void)
{
    char log[512];
    const int o = fxRow(294), b = fxRow(295), e = fxRow(296);

    startMenus(1, 11);
    const int st = ui_settings_states_row(), ph = ui_settings_photo_row();
    const int mainL = ui_settings_page_layout(UI_PAGE_MAIN);
    CHECK(st >= LT_GAME_PROPERTY_COUNT && ph >= LT_GAME_PROPERTY_COUNT && st > ph,
          "the Save states row (%d) comes after Photo mode (%d)", st, ph);
    CHECK(strcmp(txt(st), ui_str(UI_STR_STATES)) == 0, "its words \"%s\"", txt(st));
    CHECK(texProperty[o].right == mainL, "Options opens the menu: the rows were accepted");
    toPause();
    const LtProperty *po = &texProperty[o], *pb = &texProperty[b];
    CHECK(po->dispY == 50 && lt_ext_prop(ph)->dispY == 66 && lt_ext_prop(st)->dispY == 82 &&
              pb->dispY == 98 && texProperty[e].dispY == 120,
          "pause: Options %d, Photo mode %d, Save states %d, Back %d, End Game %d", po->dispY,
          lt_ext_prop(ph)->dispY, lt_ext_prop(st)->dispY, pb->dispY, texProperty[e].dispY);
    CHECK(lt_ext_prop(st)->dispH == po->dispH && lt_ext_prop(st)->dispX == lt_ext_prop(ph)->dispX &&
              !lt_ext_prop(st)->defaultMask && !lt_ext_prop(ph)->defaultMask,
          "both rows at Options' height and letters, shown");
    CHECK(pb->dispY + pb->dispH / 2 <= texProperty[e].dispY,
          "Back (to line %d) ends above End Game (%d)", pb->dispY + pb->dispH / 2,
          texProperty[e].dispY);
    CHECK(po->downItem == ph && lt_ext_prop(ph)->upItem == o && lt_ext_prop(ph)->downItem == st &&
              lt_ext_prop(st)->upItem == ph && lt_ext_prop(st)->downItem == b && pb->upItem == st,
          "the cursor walks Options, Photo mode, Save states, Back");
    CHECK(lt_ext_prop(st)->right == ui_settings_page_layout(UI_PAGE_STATES),
          "the row opens the Save states page");
    CHECK(lt_ext_row_size(st) == UI_MENU_TEXT_SIZE, "the row at the game rows' size");

    /* the same tables installed again, the rows where they were left */
    errCapture();
    ui_settings_install();
    errRelease(log, sizeof(log));
    CHECK(strstr(log, "not the expected ones") == NULL && texProperty[o].right == mainL,
          "installed again over the moved rows: accepted (\"%s\")", log);

    /* no stage: Photo mode out of the way, the row at the first place */
    stage_no = 1;
    lt_switch_layout(fxLayout(54));
    settle(fxLayout(54), 20);
    toPause();
    CHECK(lt_ext_prop(ph)->defaultMask && !lt_ext_prop(st)->defaultMask &&
              lt_ext_prop(st)->dispY == 66 && pb->dispY == 82 && po->downItem == st &&
              lt_ext_prop(st)->upItem == o && pb->upItem == st,
          "no stage: Save states at %d, Back at %d, Photo mode masked", lt_ext_prop(st)->dispY,
          pb->dispY);

    /* no host: the game's pitch, no row, nothing else moved */
    startMenus(0, 11);
    toPause();
    CHECK(ui_settings_states_row() == -1 && lt_ext_prop(ui_settings_photo_row())->dispY == 70 &&
              texProperty[b].dispY == 90 && texProperty[e].dispY == 120,
          "no host: Photo mode %d, Back %d, End Game %d, no Save states row",
          lt_ext_prop(ui_settings_photo_row())->dispY, texProperty[b].dispY, texProperty[e].dispY);
    startMenus(0, 1);
    toPause();
    CHECK(texProperty[b].dispY == 70 && texProperty[fxRow(294)].downItem == b,
          "no host, no stage: Back where the data has it");
}

/* Extras: a Save states link after Credits from both entries when the host
   was set; not there without it */
static void testExtrasLink(void)
{
    for (int title = 0; title < 2; title++) {
        for (int host = 0; host < 2; host++) {
            startMenus(host, 11);
            int labels[24], opts[24];
            if (title) {
                const int t = fxLayout(13);
                lt_switch_layout(t);
                settle(t, 60);
                press(0x4000);
                press(lt_ext_confirm_button());
            } else {
                pauseToMain();
            }
            const int ml = ui_settings_page_layout(UI_PAGE_MAIN);
            CHECK(settle(ml, 60), "title %d: the menu", title);
            const int exL = openPage(ml, -1, UI_PAGE_EXTRAS);
            const int n = ui_settings_page_rows(UI_PAGE_EXTRAS, labels, opts, NULL, 24);
            CHECK(n == (host ? 10 : 9), "title %d, host %d: %d Extras rows", title, host, n);
            int link = -1;
            for (int i = 0; i < n; i++) {
                if (strcmp(txt(labels[i]), ui_str(UI_STR_STATES)) == 0) {
                    link = i;
                }
            }
            if (!host) {
                CHECK(link < 0, "no host: no Save states row");
                continue;
            }
            CHECK(link == 3 && opts[link] == UI_OPT_LINK && opts[2] == UI_OPT_EXTRAS_CREDITS &&
                      strcmp(txt(labels[4]), ui_str(UI_STR_SECTION_CHARACTERS)) == 0,
                  "title %d: Save states after Credits, before Characters (row %d, then \"%s\")",
                  title, link, link >= 0 && link + 1 < n ? txt(labels[link + 1]) : "");
            if (link < 0) {
                continue; /* no row to open: labels[-1] would steer the cursor anywhere */
            }
            CHECK(lt_ext_prop(labels[link])->right == ui_settings_page_layout(UI_PAGE_STATES),
                  "the row opens the page");
            CHECK(ui_settings_row_shown(labels[link]) == 1, "title %d: the row is shown", title);
            lt_ext_layout(exL)->curItem = labels[link];
            press(lt_ext_confirm_button());
            const int sl = ui_settings_page_layout(UI_PAGE_STATES);
            CHECK(settle(sl, 60), "title %d: Cross opens the page", title);
            press(0x10);
            CHECK(settle(exL, 60), "title %d: Back to Extras", title);
            CHECK(lt_ext_layout(exL)->curItem == labels[link],
                  "title %d: the cursor on the Save states row", title);
        }
    }
}

/* the four cards from the fake host: texts, geometry, pictures */
static void testCards(void)
{
    setEnv("TZ", "UTC");
    tzset();
    startMenus(1, 11);
    const int sl = pauseToStates();
    int c[4][8];
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        cardRows(i, c[i]);
    }
    CHECK(s_listCalls >= 1, "the page listed the slots when it opened (%d)", s_listCalls);
    CHECK(lt_ext_layout(sl)->curItem == c[0][1], "the cursor on slot 1");
    char want[64], time5[16];
    snprintf(time5, sizeof(time5), "01:02:05");
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        snprintf(want, sizeof(want), "Slot %d", i + 1);
        CHECK(strcmp(txt(c[i][1]), want) == 0, "slot %d: label \"%s\"", i + 1, txt(c[i][1]));
    }
    CHECK(strstr(txt(c[0][5]), time5) != NULL && strcmp(txt(c[0][6]), "2023-11-14 22:13") == 0,
          "slot 1: \"%s\" and \"%s\"", txt(c[0][5]), txt(c[0][6]));
    CHECK(strcmp(txt(c[1][5]), "Empty") == 0 && strcmp(txt(c[1][6]), " ") == 0,
          "slot 2 empty: \"%s\", \"%s\"", txt(c[1][5]), txt(c[1][6]));
    CHECK(strstr(txt(c[2][5]), time5) != NULL && strcmp(txt(c[2][6]), "2023-11-14 22:14") == 0,
          "slot 3: \"%s\" and \"%s\"", txt(c[2][5]), txt(c[2][6]));
    CHECK(lt_ext_row_dim(c[2][5]) == 1 && lt_ext_row_dim(c[2][6]) == 1 &&
              lt_ext_row_dim(c[0][5]) == 0 && lt_ext_row_dim(c[3][5]) == 0,
          "only the slot that cannot load is greyed");

    /* geometry: 44 apart from 40, each card's rows inside it, all inside the
       226 lines, the status line under the last card */
    const LtProp *lay = lt_ext_layout(sl);
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        const int y = 40 + 44 * i;
        const LtProperty *im = lt_ext_prop(c[i][0]);
        CHECK(im->dispX == 40 && im->dispW == 72 && im->dispH == 54 && im->dispY == y,
              "slot %d: picture box %d,%d %dx%d", i + 1, im->dispX, im->dispY, im->dispW,
              im->dispH);
        CHECK(lt_ext_prop(c[i][1])->dispY == y && lt_ext_prop(c[i][5])->dispY == y + 13 &&
                  lt_ext_prop(c[i][6])->dispY == y + 22,
              "slot %d: label %d, texts %d and %d", i + 1, lt_ext_prop(c[i][1])->dispY,
              lt_ext_prop(c[i][5])->dispY, lt_ext_prop(c[i][6])->dispY);
        CHECK(lt_ext_prop(c[i][1])->selectable && !lt_ext_prop(c[i][5])->selectable &&
                  !lt_ext_prop(c[i][6])->selectable,
              "slot %d: only the label is a stop", i + 1);
        CHECK(lt_ext_pointer_role(c[i][0]) == LT_POINTER_NONE &&
                  lt_ext_pointer_role(c[i][5]) == LT_POINTER_NONE &&
                  lt_ext_pointer_role(c[i][3]) == LT_POINTER_LEFT &&
                  lt_ext_pointer_role(c[i][4]) == LT_POINTER_RIGHT &&
                  lt_ext_pointer_role(c[i][2]) == LT_POINTER_STEP,
              "slot %d: the pointer's roles", i + 1);
    }
    for (int r = lay->first; r < lay->last; r++) {
        const LtProperty *p = lt_ext_prop(r);
        CHECK(p->dispY >= 0 && p->dispY + p->dispH / 2 <= 226, "row %d inside the window (%d..%d)",
              r, p->dispY, p->dispY + p->dispH / 2);
    }
    CHECK(lt_ext_prop(c[3][6])->dispY + lt_ext_prop(c[3][6])->dispH / 2 <=
              lt_ext_prop(c[0][7])->dispY,
          "the status line is under the last card's date");

    /* the pictures: slots 3 and 4 have one, made at the sizes the host gave */
    int w, h;
    CHECK(s_loads == 2, "two pictures loaded when the page opened (%d)", s_loads);
    CHECK(lt_ext_row_image(c[0][0], &w, &h) == 0 && lt_ext_row_image(c[1][0], &w, &h) == 0,
          "the slots without a picture draw nothing");
    const unsigned t3 = lt_ext_row_image(c[2][0], &w, &h);
    CHECK(t3 >= 100 && w == 320 && h == 240, "slot 3: texture %u at %dx%d", t3, w, h);
    const unsigned t4 = lt_ext_row_image(c[3][0], &w, &h);
    CHECK(t4 >= 100 && t4 != t3 && w == 320 && h == 240, "slot 4: texture %u at %dx%d", t4, w, h);

    /* the listing is read again a few times a second: an unchanged picture
       is not loaded again, a new file replaces the old texture, a written-again
       picture (the same file, a new time) is loaded again, an emptied slot
       drops it */
    const int calls = s_listCalls;
    idle(80);
    CHECK(s_listCalls > calls && s_loads == 2 && s_destroys == 0,
          "listed again (%d calls), nothing reloaded (%d loads, %d destroys)", s_listCalls - calls,
          s_loads, s_destroys);
    snprintf(s_slots[3].png_path, sizeof(s_slots[3].png_path), "slot4b.png");
    idle(40);
    CHECK(s_loads == 3 && s_destroys == 1 && s_lastDestroyed == (int)t4 &&
              strcmp(s_lastLoaded, "slot4b.png") == 0,
          "a new file: loaded (%d), the old texture destroyed (%d, %d)", s_loads, s_destroys,
          s_lastDestroyed);
    s_slots[3].saved_at += 5;
    idle(40);
    CHECK(s_loads == 4 && s_destroys == 2, "written again: loaded again (%d, %d)", s_loads,
          s_destroys);
    s_slots[2].present = 0;
    idle(40);
    CHECK(lt_ext_row_image(c[2][0], &w, &h) == 0 && s_destroys == 3 &&
              strcmp(txt(c[2][5]), "Empty") == 0,
          "an emptied slot: no picture (%d destroys), \"%s\"", s_destroys, txt(c[2][5]));

    /* Back: every picture released, the rows empty */
    press(0x10);
    CHECK(settle(fxLayout(57), 60), "Back to the pause menu");
    CHECK(s_destroys == 4 && lt_ext_row_image(c[3][0], &w, &h) == 0,
          "closing the page released the last picture (%d destroys)", s_destroys);
}

/* the actions: Save, Load, Delete stepped by Left and Right, the hidden ones */
static void testActions(void)
{
    startMenus(1, 11);
    const int sl = pauseToStates();
    int c[4][8];
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        cardRows(i, c[i]);
    }
    /* a slot in use: all three, Save first; the arrows show */
    CHECK(strcmp(txt(c[0][2]), "Save") == 0 && lt_ext_prop(c[0][3])->masked == 0 &&
              lt_ext_prop(c[0][4])->masked == 0,
          "slot 1: \"%s\", arrows shown", txt(c[0][2]));
    press(0x2000);
    CHECK(strcmp(txt(c[0][2]), "Load") == 0, "Right: \"%s\"", txt(c[0][2]));
    press(0x2000);
    CHECK(strcmp(txt(c[0][2]), "Delete") == 0, "Right: \"%s\"", txt(c[0][2]));
    press(0x2000);
    CHECK(strcmp(txt(c[0][2]), "Save") == 0, "Right wraps: \"%s\"", txt(c[0][2]));
    press(0x8000);
    CHECK(strcmp(txt(c[0][2]), "Delete") == 0, "Left wraps: \"%s\"", txt(c[0][2]));
    /* the choice is the slot's own */
    press(0x4000);
    CHECK(cursorOn(sl) == c[1][1], "Down: slot 2");
    CHECK(strcmp(txt(c[0][2]), "Delete") == 0, "slot 1 keeps its choice");
    /* an empty slot: Save alone, no arrows, Left and Right do nothing */
    CHECK(strcmp(txt(c[1][2]), "Save") == 0 && lt_ext_prop(c[1][3])->masked == 1 &&
              lt_ext_prop(c[1][4])->masked == 1,
          "slot 2 empty: \"%s\", no arrows", txt(c[1][2]));
    press(0x2000);
    CHECK(strcmp(txt(c[1][2]), "Save") == 0, "Right on a single action does nothing");
    /* the cursor loops: Up from slot 1 is slot 4 */
    moveTo(sl, 0);
    press(0x1000);
    CHECK(cursorOn(sl) == c[3][1], "Up from slot 1: slot 4");
    press(0x4000);
    CHECK(cursorOn(sl) == c[0][1], "Down from slot 4: slot 1");

    /* saves not allowed (the title; the pause menu at a moment the host
       refuses): Save is gone from every slot */
    moveTo(sl, 0);
    press(0x2000); /* slot 1 from Delete round to Save */
    CHECK(strcmp(txt(c[0][2]), "Save") == 0, "slot 1 back on Save: \"%s\"", txt(c[0][2]));
    s_canSave = 0;
    idle(2);
    CHECK(strcmp(txt(c[0][2]), "Load") == 0 && strcmp(txt(c[1][2]), " ") == 0 &&
              strcmp(txt(c[3][2]), "Load") == 0,
          "no saves: \"%s\", \"%s\", \"%s\"", txt(c[0][2]), txt(c[1][2]), txt(c[3][2]));
    CHECK(lt_ext_prop(c[0][3])->masked == 0 && lt_ext_prop(c[1][3])->masked == 1,
          "no saves: slot 1 steps Load and Delete, the empty slot has no arrows");
    press(0x2000);
    CHECK(strcmp(txt(c[0][2]), "Delete") == 0, "Right: \"%s\"", txt(c[0][2]));
    press(0x2000);
    CHECK(strcmp(txt(c[0][2]), "Load") == 0, "Right wraps past Save: \"%s\"", txt(c[0][2]));
    /* Cross on an empty slot with nothing to do: a refusal sound, no request */
    moveTo(sl, 1);
    const int neg = s_sounds[2];
    press(lt_ext_confirm_button());
    CHECK(s_requests == 0 && s_sounds[2] > neg, "Cross on an empty slot: nothing asked (%d)",
          s_requests);
    s_canSave = 1;
}

/* Load asks at once; Save over a slot in use and Delete ask twice within
   two seconds; Save on an empty slot at once; refused requests show a popup */
static void testConfirm(void)
{
    const int conf = lt_ext_confirm_button();
    startMenus(1, 11);
    const int sl = pauseToStates();
    int c[4][8];
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        cardRows(i, c[i]);
    }
    /* Load */
    moveTo(sl, 0);
    press(0x2000);
    press(conf);
    CHECK(s_requests == 1 && s_reqAct == UI_STATES_ACT_LOAD && s_reqSlot == 1,
          "Load: asked at once (%d: %d, slot %d)", s_requests, s_reqAct, s_reqSlot);
    /* Save over slot 1: the first Cross only asks to press again */
    press(0x8000); /* back to Save */
    CHECK(strcmp(txt(c[0][2]), "Save") == 0, "on Save: \"%s\"", txt(c[0][2]));
    s_requests = 0;
    press(conf);
    CHECK(s_requests == 0 && strstr(txt(c[0][7]), "Press again to save") != NULL,
          "Save over a state: not yet (%d), the line says \"%s\"", s_requests, txt(c[0][7]));
    press(conf);
    CHECK(s_requests == 1 && s_reqAct == UI_STATES_ACT_SAVE && s_reqSlot == 1,
          "the second Cross saves (%d: %d, slot %d)", s_requests, s_reqAct, s_reqSlot);
    CHECK(strstr(txt(c[0][7]), "Press again") == NULL, "the line is the note again");
    /* too late: the third press arms again */
    s_requests = 0;
    press(conf);
    idle(130);
    press(conf);
    CHECK(s_requests == 0, "a second Cross after two seconds only arms again (%d)", s_requests);
    /* the cursor leaving cancels */
    press(0x4000);
    press(0x1000);
    press(conf);
    CHECK(s_requests == 0, "a Cross after the cursor left only arms (%d)", s_requests);
    press(conf);
    CHECK(s_requests == 1, "and the next one saves (%d)", s_requests);
    /* Left or Right between the presses cancels too */
    s_requests = 0;
    press(conf);
    press(0x2000);
    press(0x8000);
    press(conf);
    CHECK(s_requests == 0, "a step between the presses cancels (%d)", s_requests);
    idle(130);
    /* Delete */
    press(0x8000);
    CHECK(strcmp(txt(c[0][2]), "Delete") == 0, "on Delete: \"%s\"", txt(c[0][2]));
    s_requests = 0;
    press(conf);
    CHECK(s_requests == 0 && strstr(txt(c[0][7]), "Press again to delete") != NULL,
          "Delete: the line says \"%s\"", txt(c[0][7]));
    press(conf);
    CHECK(s_requests == 1 && s_reqAct == UI_STATES_ACT_DELETE && s_reqSlot == 1,
          "the second Cross deletes (%d: %d, slot %d)", s_requests, s_reqAct, s_reqSlot);
    /* Save on an empty slot: at once */
    moveTo(sl, 1);
    s_requests = 0;
    press(conf);
    CHECK(s_requests == 1 && s_reqAct == UI_STATES_ACT_SAVE && s_reqSlot == 2,
          "Save on an empty slot: asked at once (%d: %d, slot %d)", s_requests, s_reqAct,
          s_reqSlot);
    /* accepted: the listing is read again at the next tick (inside press,
       which runs two more frames after the one that asked) */
    CHECK(s_listFrame == s_reqFrame + 1,
          "an accepted request lists again at once (asked at frame %d, listed at %d)", s_reqFrame,
          s_listFrame);
    /* refused at once: the host's reason in a popup */
    s_reqRet = -1;
    s_reqRefusal = UI_STR_STATES_PAUSE_FIRST;
    ui_popup_reset();
    const int neg = s_sounds[2];
    press(conf);
    CHECK(ui_popup_active() && s_sounds[2] > neg, "a refusal: a popup and the refusal sound");
}

/* the title: no Save, the hint under the cards; Back to Extras */
static void testTitle(void)
{
    startMenus(1, 11);
    s_canSave = 0;
    const int sl = titleToStates();
    int c[4][8];
    for (int i = 0; i < UI_STATES_SLOTS; i++) {
        cardRows(i, c[i]);
    }
    CHECK(strcmp(txt(c[0][2]), "Load") == 0 && strcmp(txt(c[1][2]), " ") == 0,
          "the title: slot 1 \"%s\", empty slot \"%s\"", txt(c[0][2]), txt(c[1][2]));
    CHECK(strstr(txt(c[0][7]), "Saving is done from the pause menu") != NULL,
          "the title: the line says \"%s\"", txt(c[0][7]));
    /* the cursor on a slot that cannot load: why */
    moveTo(sl, 2);
    idle(2);
    CHECK(strstr(txt(c[0][7]), "Saved by another version of the game") != NULL,
          "slot 3: the line says \"%s\"", txt(c[0][7]));
    moveTo(sl, 3);
    idle(2);
    CHECK(strstr(txt(c[0][7]), "Saving is done") != NULL, "slot 4: the hint again");
    /* Load from the title is asked as from the pause menu */
    press(lt_ext_confirm_button());
    CHECK(s_requests == 1 && s_reqAct == UI_STATES_ACT_LOAD && s_reqSlot == 4,
          "the title loads slot 4 (%d: %d, %d)", s_requests, s_reqAct, s_reqSlot);
    press(0x10);
    CHECK(settle(ui_settings_page_layout(UI_PAGE_EXTRAS), 60), "Back to Extras");
    CHECK(s_destroys == 2, "the pictures released (%d)", s_destroys);
    /* the pause menu's wording of the same line */
    startMenus(1, 11);
    pauseToStates();
    int c2[8];
    cardRows(0, c2);
    CHECK(strstr(txt(c2[7]), "A state belongs to this version") != NULL,
          "the pause menu: the line says \"%s\"", txt(c2[7]));
}

/* Back from the pause menu's row returns to it with the cursor on the row;
   a state loaded under the page closes it on the same row */
static void testBackAndLoad(void)
{
    startMenus(1, 11);
    pauseToStates();
    press(0x10);
    CHECK(settle(fxLayout(57), 60), "Triangle: the pause menu");
    CHECK(texLayout[fxLayout(57)].curItem == ui_settings_states_row(),
          "the cursor on the Save states row (%d, want %d)", texLayout[fxLayout(57)].curItem,
          ui_settings_states_row());
    /* a load while the page is up */
    int w, h;
    int c[8];
    const int sl = pauseToStates();
    cardRows(3, c);
    CHECK(lt_ext_row_image(c[0], &w, &h) != 0, "the picture is up");
    ui_settings_sync_after_state_load();
    const int sounds = s_sounds[2];
    idle(3);
    CHECK(current_layout_id == fxLayout(57) || settle(fxLayout(57), 60),
          "the page closes after a load (%d, was %d)", current_layout_id, sl);
    CHECK(texLayout[fxLayout(57)].curItem == ui_settings_states_row() &&
              lt_ext_row_image(c[0], &w, &h) == 0 && s_sounds[2] == sounds,
          "the pause menu on the Save states row, the picture released, no sound");
    /* a load that left the game on the pause menu: the cursor moves there */
    toPause();
    texLayout[fxLayout(57)].curItem = fxRow(294);
    ui_settings_sync_after_state_load();
    CHECK(texLayout[fxLayout(57)].curItem == ui_settings_states_row(),
          "sync on the pause menu: the cursor on the row (%d)", texLayout[fxLayout(57)].curItem);
}

/* the page without a host: reachable by nothing, but built and harmless */
static void testNoHost(void)
{
    startMenus(0, 11);
    const int sl = ui_settings_page_layout(UI_PAGE_STATES);
    CHECK(sl >= LT_GAME_LAYOUT_COUNT && ui_settings_states_row() == -1,
          "no host: the page's layout exists (%d), no pause row", sl);
    int c[8];
    cardRows(0, c);
    lt_switch_layout(sl);
    CHECK(settle(sl, 60), "the page can be switched to");
    CHECK(strcmp(txt(c[5]), "Empty") == 0 && strcmp(txt(c[2]), " ") == 0,
          "every slot empty, no action: \"%s\" \"%s\"", txt(c[5]), txt(c[2]));
    press(lt_ext_confirm_button());
    CHECK(s_requests == 0, "Cross asks nothing");
    press(0x10);
    CHECK(s_requests == 0, "Back leaves");
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        /* the other discs: the pause rows and the Extras link on their numbers */
        CHECK(ico_region_select(argv[2]) == 0, "the %s region", argv[2]);
        testPauseRows();
        testExtrasLink();
        CHECK(ico_region_select("pal") == 0, "the European region again");
        printf("settings_states_test %s: %s\n", argv[2], failures ? "failed" : "ok");
        return failures ? 1 : 0;
    }
    testPauseRows();
    testExtrasLink();
    testCards();
    testActions();
    testConfirm();
    testTitle();
    testBackAndLoad();
    testNoHost();
    if (failures) {
        printf("settings_states_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_states_test: ok\n");
    return 0;
}
