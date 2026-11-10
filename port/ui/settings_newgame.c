/*
 * port/ui/settings_newgame.c
 *
 * The New Game screen (settings.h ui_new_game_screen_*), between the
 * vibration choice and the start, one row under the other (pitch 24):
 *
 *   Difficulty    < Normal >     y 46   Left/Right: Easy, Normal, Hard (Custom)
 *   Customize                    y 70   Cross: the Customize pages
 *   Mirror mode   Off    On      y 94
 *   New Game+     Off    On      y 118
 *              Start             y 148
 *   the cursor row's note        y 182  (30 lines: its bottom at 212)
 *
 * Each Off/On row keeps its choice: up and down land on it, and it stays lit
 * while the cursor is away.  The difficulty is a draft (settings_difficulty.c)
 * the Difficulty row steps between the presets and Customize edits row by
 * row; the confirm hands it to the journey (port/game/difficulty.h
 * ico_difficulty_begin) before the game starts.  settings.c builds the screen
 * after the pages and forgets it with them (settings_newgame.h).
 */
#include "settings.h"
#include "settings_newgame.h"
#include "settings_difficulty.h"
#include "settings_states.h" /* ui__settings_built */

#include <stdio.h>

#include "font.h" /* UI_ALIGN_* */
#include "layout_ext.h"
#include "options.h"
#include "strings.h"
#include "ui_list.h"        /* ui_settings_add_row */
#include "../core/region.h" /* ico_region_cur: the running disc */

/* the game's side (common/; layout_texture.h declares the lt_* calls) */
extern PadState pad[16];
extern int gFlagGameClear;
extern void CUR_SE(void); /* layout_action.c: the menus' sounds */
extern void POSITIVE_SE(void);
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
static int s_diffRow = -1;      /* Difficulty's label (the item); its value and arrows follow */
static int s_customizeRow = -1;
static int s_newGameChosen;
static int s_mirrorChoice, s_ngpChoice; /* each row's Off (0) or On (1) */
/* the record of the cleared save the screen was entered from (its rows the
   draft's start, kept while that save's New Game+ goes back to the
   vibration screen and comes here again) */
static IcoDifficulty s_cleared;
static int s_haveCleared;

#define LAYOUT_VIBE_SELECT (ico_region_cur()->layout_vibe) /* la_vibe_select's screen (9) */
#define NEW_GAME_PITCH 24
#define NEW_GAME_DIFF_Y 46
#define NEW_GAME_ROW_Y (NEW_GAME_DIFF_Y + 2 * NEW_GAME_PITCH) /* Mirror mode, 94 */
#define NEW_GAME_ROW_H 34
#define NEW_GAME_START_Y 148 /* six lines under the value rows */
#define NEW_GAME_NOTE_Y 182  /* 30 lines, clear of the bottom (212) */
/* Difficulty's value between its arrows, over the Off and On columns
   (320 .. 550) */
#define NEW_GAME_ARROW_L_X 320
#define NEW_GAME_ARROW_R_X 536
#define NEW_GAME_ARROW_W 14
#define NEW_GAME_VALUE_X 336
#define NEW_GAME_VALUE_W 200

static LtProperty *P(int index)
{
    return lt_ext_prop(index);
}

static int newGameScreenProc(int first, int item);

void ui__newgame_build(void)
{
    static const int kLabel[2] = {UI_STR_OPT_MIRROR, UI_STR_OPT_NEWGAME_PLUS};
    int first = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT;
    /* Difficulty: the label is the item (right-aligned up to x 300), its
       value and arrows lit with it, the Settings pages' stepped row */
    s_diffRow = ui_settings_add_row(40, NEW_GAME_DIFF_Y, 260, NEW_GAME_ROW_H, 1, -1, UI_STR_DIFF,
                                    NULL, 0.0f, UI_ALIGN_RIGHT);
    const int v = ui_settings_add_row(NEW_GAME_VALUE_X, NEW_GAME_DIFF_Y, NEW_GAME_VALUE_W,
                                      NEW_GAME_ROW_H, 1, s_diffRow, 0, " ", 0.0f, UI_ALIGN_CENTER);
    const int al =
        ui_settings_add_row(NEW_GAME_ARROW_L_X, NEW_GAME_DIFF_Y, NEW_GAME_ARROW_W, NEW_GAME_ROW_H,
                            1, s_diffRow, 0, "\xE2\x80\xB9", 0.0f, UI_ALIGN_LEFT);
    const int ar =
        ui_settings_add_row(NEW_GAME_ARROW_R_X, NEW_GAME_DIFF_Y, NEW_GAME_ARROW_W, NEW_GAME_ROW_H,
                            1, s_diffRow, 0, "\xE2\x80\xBA", 0.0f, UI_ALIGN_LEFT);
    /* the pointer: the arrows are Left and Right, the value steps as Right
       does (ui_mouse.c: a click, never a hover, on this screen) */
    lt_ext_set_pointer_role(v, LT_POINTER_STEP);
    lt_ext_set_pointer_role(al, LT_POINTER_LEFT);
    lt_ext_set_pointer_role(ar, LT_POINTER_RIGHT);
    s_customizeRow = ui_settings_add_row(40, NEW_GAME_DIFF_Y + NEW_GAME_PITCH, 260, NEW_GAME_ROW_H,
                                         1, -1, UI_STR_DIFF_CUSTOMIZE, NULL, 0.0f, UI_ALIGN_RIGHT);
    for (int r = 0; r < 2; r++) {
        const int y = NEW_GAME_ROW_Y + r * NEW_GAME_PITCH;
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
    ui__settings_set_note(s_newGameNote, UI_STR_DIFF_NORMAL_NOTE);
    int last = lt_ext_prop_count() + LT_GAME_PROPERTY_COUNT - 1;
    s_newGameLayout = ui__settings_add_layout(first, last + 1, 0.6f, newGameScreenProc, s_diffRow);
}

void ui__newgame_reset(void)
{
    s_newGameLayout = s_newGameStart = -1;
    s_newGameRow[0][0] = s_newGameRow[0][1] = s_newGameRow[1][0] = s_newGameRow[1][1] = -1;
    s_newGameNote = s_diffRow = s_customizeRow = -1;
    s_newGameChosen = s_mirrorChoice = s_ngpChoice = 0;
    s_haveCleared = 0;
}

/* The cursor's row takes the item it is on as its choice (curRow -1 on
   Start, 2 on Difficulty, 3 on Customize); up and down loop Difficulty,
   Customize, Mirror mode, New Game+, Start; the Off/On rows' choices stay
   lit (ownerItem, layout_texture.c display_texture); Difficulty's value
   shows the draft's mode. */
static void newGameSync(void)
{
    const int cur = lt_ext_layout(s_newGameLayout)->curItem;
    int curRow = cur == s_diffRow ? 2 : cur == s_customizeRow ? 3 : -1;
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
            e->upItem = r == 0 ? s_customizeRow : s_newGameRow[0][choice[0]];
            e->downItem = r == 0 ? s_newGameRow[1][choice[1]] : s_newGameStart;
            e->ownerItem = r != curRow && c == choice[r] ? cur : -1;
        }
    }
    LtProperty *d = P(s_diffRow);
    d->upItem = s_newGameStart;
    d->downItem = s_customizeRow;
    d->leftItem = d->rightItem = -1; /* Left and Right step the mode (the proc) */
    LtProperty *cz = P(s_customizeRow);
    cz->upItem = s_diffRow;
    cz->downItem = s_newGameRow[0][choice[0]];
    cz->leftItem = cz->rightItem = -1;
    LtProperty *st = P(s_newGameStart);
    st->upItem = s_newGameRow[1][choice[1]];
    st->downItem = s_diffRow;
    st->leftItem = st->rightItem = -1;
    const IcoDiffMode mode = ui__diff_draft_mode();
    lt_ext_set_str(s_diffRow + 1, ui__diff_mode_str(mode));
    ui__settings_set_note(s_newGameNote, curRow == 0   ? UI_STR_MIRROR_SCREEN
                                         : curRow == 1 ? UI_STR_NEWGAME_PLUS_SCREEN
                                         : curRow == 2 ? ui__diff_mode_note(mode)
                                         : curRow == 3 ? UI_STR_DIFF_CUSTOMIZE_NOTE
                                                       : UI_STR_NEWGAME_START_SCREEN);
}

int ui_new_game_screen_enter(void)
{
    if (!ui__settings_built() || s_newGameLayout < 0) {
        return -1;
    }
    /* a cleared save's New Game+: the save's own rows are the draft's start,
       taken before the title's reset below puts Normal in force (that
       record has a journey; Normal at the title has none) */
    const IcoDifficulty active = *ico_difficulty_active();
    if (gFlagGameClear && ico_difficulty_has_journey()) {
        s_cleared = active;
        s_haveCleared = 1;
    } else if (!gFlagGameClear) {
        s_haveCleared = 0;
    }
    LtProp *l = lt_ext_layout(s_newGameLayout);
    l->defaultItem = l->curItem = s_diffRow;
    s_newGameChosen = 0;
    /* no run until the choice (a cleared save's flag does not carry over);
       the journey in force is Normal again (difficulty.h ico_difficulty_reset) */
    ico_opt_mirror_reset();
    s_mirrorChoice = 0;
    /* New Game+: on from a finished save, off from the title's New Game */
    s_ngpChoice = gFlagGameClear ? 1 : 0;
    ui__diff_draft_start(s_haveCleared ? &s_cleared : NULL);
    {
        char id[20];
        ico_difficulty_journey_text(&s_cleared, id, sizeof(id));
        fprintf(stderr, "newgame: the screen opened: Difficulty %s (%s%s), New Game+ %s\n",
                ico_difficulty_mode_name(ui__diff_draft_mode()),
                s_haveCleared ? "the cleared save's rows, journey " : "the preset",
                s_haveCleared ? id : "", s_ngpChoice ? "on" : "off");
    }
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

int ui_new_game_screen_difficulty(void)
{
    return s_diffRow;
}

int ui_new_game_screen_customize(void)
{
    return s_customizeRow;
}

int ui__newgame_customize_back(void)
{
    if (s_newGameLayout >= 0) {
        LtProp *l = lt_ext_layout(s_newGameLayout);
        l->defaultItem = l->curItem = s_customizeRow;
    }
    fprintf(stderr, "newgame: back from Customize: Difficulty %s\n",
            ico_difficulty_mode_name(ui__diff_draft_mode()));
    return s_newGameLayout;
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
    if (cur == s_diffRow && (flags & (LT_PAD_LEFT | LT_PAD_RIGHT)) && !(flags & LT_PAD_START)) {
        /* the mode: a preset's rows, or the kept Custom draft */
        CUR_SE();
        ui__diff_step_mode((flags & LT_PAD_LEFT) ? -1 : 1);
        newGameSync();
        return -1;
    }
    if ((flags & PAD_OK) && cur == s_customizeRow && !(flags & LT_PAD_START)) {
        POSITIVE_SE();
        fprintf(stderr, "newgame: Customize opened on Difficulty %s\n",
                ico_difficulty_mode_name(ui__diff_draft_mode()));
        return ui__settings_open_customize();
    }
    if ((flags & PAD_OK) && cur != s_newGameStart && !(flags & LT_PAD_START)) {
        /* Cross on Difficulty or an Off/On item picks it and moves on */
        POSITIVE_SE();
        lt_ext_layout(s_newGameLayout)->curItem = P(cur)->downItem;
        newGameSync(); /* lit for this tick's draw */
        return -1;
    }
    if (flags & (PAD_OK | LT_PAD_START)) {
        s_newGameChosen = 1;
        POSITIVE_SE();
        fprintf(stderr, "newgame: start: Difficulty %s, Mirror mode %s, New Game+ %s\n",
                ico_difficulty_mode_name(ui__diff_draft_mode()), s_mirrorChoice ? "on" : "off",
                s_ngpChoice ? "on" : "off");
        /* the journey's difficulty before the game reads any of it */
        ico_difficulty_begin(ui__diff_draft());
        s_haveCleared = 0;
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
