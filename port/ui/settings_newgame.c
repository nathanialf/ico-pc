/*
 * port/ui/settings_newgame.c
 *
 * The New Game screen (settings.h ui_new_game_screen_*), between the
 * vibration choice and the start: Mirror mode and New Game+ with Off and On
 * side by side, a centred Start row, and a note for the cursor's row.  Each
 * row keeps its choice: up and down land on it, and it stays lit while the
 * cursor is away.  Moved out of settings.c unchanged (settings.c builds it
 * after the pages and forgets it with them: settings_newgame.h).
 */
#include "settings.h"
#include "settings_newgame.h"
#include "settings_states.h" /* ui__settings_built */

#include "font.h" /* UI_ALIGN_* */
#include "layout_ext.h"
#include "options.h"
#include "strings.h"
#include "ui_list.h"        /* ui_settings_add_row */
#include "../core/region.h" /* ico_region_cur: the running disc */

/* the game's side (common/; layout_texture.h declares the lt_* calls) */
extern PadState pad[16];
extern int gFlagGameClear;
extern void POSITIVE_SE(void); /* layout_action.c: the menus' sounds */
extern void NEGATIVE_SE(void);
/* layout_action.c: what la_vibe_select's confirm did after
   the vibration choice, gflagOn(382): the new game starts */
extern void la_host_new_game_go(void);

/* the port's screens confirm with the disc's confirm and go back with the
   face button that does not confirm (settings.c PAD_OK, PAD_BACK) */
#define PAD_BACK (lt_ext_port_back_buttons())
#define PAD_OK (lt_ext_confirm_button())

static int s_newGameLayout = -1;
static int s_newGameRow[2][2] = {{-1, -1}, {-1, -1}}; /* [row][off, on] */
static int s_newGameNote = -1;
static int s_newGameStart = -1; /* the centred Start row */
static int s_newGameChosen;
static int s_mirrorChoice, s_ngpChoice; /* each row's Off (0) or On (1) */

#define LAYOUT_VIBE_SELECT (ico_region_cur()->layout_vibe) /* la_vibe_select's screen (9) */
#define NEW_GAME_ROW_Y 70
#define NEW_GAME_ROW_H 34
#define NEW_GAME_START_Y 146 /* eight lines under the value rows */
#define NEW_GAME_NOTE_Y 182  /* 30 lines, clear of the bottom (212) */

static LtProperty *P(int index)
{
    return lt_ext_prop(index);
}

static int newGameScreenProc(int first, int item);

void ui__newgame_build(void)
{
    static const int kLabel[2] = {UI_STR_OPT_MIRROR, UI_STR_OPT_NEWGAME_PLUS};
    int first = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT;
    for (int r = 0; r < 2; r++) {
        const int y = NEW_GAME_ROW_Y + r * NEW_GAME_ROW_H;
        /* the label right-aligned up to x 300, Off and On after it */
        ui_settings_add_row(40, y, 260, NEW_GAME_ROW_H, 0, -1, kLabel[r], NULL, 0.0f,
                            UI_ALIGN_RIGHT);
        s_newGameRow[r][0] = ui_settings_add_row(320, y, 110, NEW_GAME_ROW_H, 1, -1, UI_STR_OFF,
                                                 NULL, 0.0f, UI_ALIGN_CENTER);
        s_newGameRow[r][1] = ui_settings_add_row(440, y, 110, NEW_GAME_ROW_H, 1, -1, UI_STR_ON,
                                                 NULL, 0.0f, UI_ALIGN_CENTER);
        P(s_newGameRow[r][0])->rightItem = s_newGameRow[r][1];
        P(s_newGameRow[r][1])->leftItem = s_newGameRow[r][0];
    }
    /* the note stays the last row built */
    s_newGameStart = ui_settings_add_row(200, NEW_GAME_START_Y, 240, NEW_GAME_ROW_H, 1, -1,
                                         UI_STR_NEWGAME_START, NULL, 0.0f, UI_ALIGN_CENTER);
    s_newGameNote = ui_settings_add_row(20, NEW_GAME_NOTE_Y, 600, 30, 0, -1, 0, " ", UI__NOTE_SIZE,
                                        UI_ALIGN_CENTER);
    P(s_newGameNote)->centerX = 1;
    ui__settings_set_note(s_newGameNote, UI_STR_MIRROR_SCREEN);
    int last = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT - 1;
    s_newGameLayout =
        ui__settings_add_layout(first, last + 1, 0.6f, newGameScreenProc, s_newGameRow[0][0]);
}

void ui__newgame_reset(void)
{
    s_newGameLayout = s_newGameStart = -1;
    s_newGameRow[0][0] = s_newGameRow[0][1] = s_newGameRow[1][0] = s_newGameRow[1][1] = -1;
    s_newGameNote = -1;
    s_newGameChosen = s_mirrorChoice = s_ngpChoice = 0;
}

/* The cursor's row takes the item it is on as its choice (curRow -1 on
   Start); up and down loop Mirror mode, New Game+, Start; the other rows'
   choices stay lit (ownerItem, layout_texture.c display_texture). */
static void newGameSync(void)
{
    const int cur = lt_ext_layout(s_newGameLayout)->curItem;
    int curRow = cur == s_newGameStart ? -1 : 0;
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            if (cur == s_newGameRow[r][c]) {
                curRow = r;
                *(r == 0 ? &s_mirrorChoice : &s_ngpChoice) = c;
            }
        }
    }
    const int choice[2] = {s_mirrorChoice, s_ngpChoice};
    for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 2; c++) {
            LtProperty *e = P(s_newGameRow[r][c]);
            e->upItem = r == 0 ? s_newGameStart : s_newGameRow[0][choice[0]];
            e->downItem = r == 0 ? s_newGameRow[1][choice[1]] : s_newGameStart;
            e->ownerItem = r != curRow && c == choice[r] ? cur : -1;
        }
    }
    LtProperty *st = P(s_newGameStart);
    st->upItem = s_newGameRow[1][choice[1]];
    st->downItem = s_newGameRow[0][choice[0]];
    st->leftItem = st->rightItem = -1;
    ui__settings_set_note(s_newGameNote, curRow == 0   ? UI_STR_MIRROR_SCREEN
                                         : curRow == 1 ? UI_STR_NEWGAME_PLUS_SCREEN
                                                       : UI_STR_NEWGAME_START_SCREEN);
}

int ui_new_game_screen_enter(void)
{
    if (!ui__settings_built() || s_newGameLayout < 0) {
        return -1;
    }
    LtProp *l = lt_ext_layout(s_newGameLayout);
    l->defaultItem = l->curItem = s_newGameRow[0][0];
    s_newGameChosen = 0;
    /* no run until the choice (a cleared save's flag does not carry over) */
    ico_opt_mirror_reset();
    s_mirrorChoice = 0;
    /* New Game+: on from a finished save, off from the title's New Game */
    s_ngpChoice = gFlagGameClear ? 1 : 0;
    newGameSync();
    return s_newGameLayout;
}

int ui_new_game_screen_layout(void)
{
    return s_newGameLayout;
}

int ui_new_game_screen_start(void)
{
    return s_newGameStart;
}

int ui_new_game_screen_row(int row, int on)
{
    return row == 0 || row == 1 ? s_newGameRow[row][on ? 1 : 0] : -1;
}

static int newGameScreenProc(int first, int item)
{
    (void)item;
    ui_settings_apply_language();
    if (first) {
        s_newGameChosen = 0;
    }
    newGameSync();
    /* as la_vibe_select: input once faded in, the choice once */
    if (s_newGameChosen || lt_fade_status() != 2) {
        return -1;
    }
    int flags = pad[0].flags;
    const int cur = lt_ext_layout(s_newGameLayout)->curItem;
    if ((flags & PAD_OK) && cur != s_newGameStart && !(flags & LT_PAD_START)) {
        /* Cross on an Off/On item picks it and moves on */
        POSITIVE_SE();
        lt_ext_layout(s_newGameLayout)->curItem = P(cur)->downItem;
        newGameSync(); /* lit for this tick's draw */
        return -1;
    }
    if (flags & (PAD_OK | LT_PAD_START)) {
        s_newGameChosen = 1;
        POSITIVE_SE();
        ico_opt_set_mirror(s_mirrorChoice);
        /* the game's only record of a second playthrough: the chosen value
           wins over the save's */
        gFlagGameClear = s_ngpChoice;
        la_host_new_game_go();
        return -1;
    }
    if (flags & PAD_BACK) {
        NEGATIVE_SE();
        return LAYOUT_VIBE_SELECT;
    }
    return -1;
}
