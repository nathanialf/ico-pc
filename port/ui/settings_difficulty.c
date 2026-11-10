/*
 * port/ui/settings_difficulty.c
 *
 * The New Game's difficulty draft and Customize's pages (settings_difficulty.h).
 * The draft is one IcoDifficulty (port/game/difficulty.h) the New Game screen
 * starts (Normal, or a cleared save's rows) and the confirm hands to
 * ico_difficulty_begin; the pages' rows read and step it, never the config
 * nor the journey in force.  The rows that were config rows before v0.11.3
 * keep their options (UI_OPT_SHADOW_SPEED, UI_OPT_VAR_TOUGHNESS ...) and
 * their strings; No Shadows, the knock-down limit, its count and Permadeath
 * are new.  After every step the draft's mode is the preset its rows match,
 * else Custom; a Custom draft is kept aside, so the Difficulty row can step
 * to a preset and back to it.
 */
#include "settings.h"
#include "settings_difficulty.h"

#include <stdio.h>
#include <string.h>

#include "options.h"
#include "strings.h"
#include "variants.h"
#include "../core/region.h" /* ICO_JUN26, ICO_NTSCAUG6JUN26 */

#define N_OF(a) ((int)(sizeof(a) / sizeof((a)[0])))

static IcoDifficulty s_draft, s_custom;
static int s_haveDraft, s_haveCustom;

/* the options of the draft's rows, IcoDiffRow order */
static const int kRowOpt[ICO_DIFF_ROW_COUNT] = {UI_OPT_SHADOW_SPEED,
                                                UI_OPT_VAR_TOUGHNESS,
                                                UI_OPT_VAR_AGGRESSION,
                                                UI_OPT_VAR_PEACEFUL,
                                                UI_OPT_YORDA,
                                                UI_OPT_SHADOWS_TOGETHER,
                                                UI_OPT_DIFF_NO_SHADOWS,
                                                UI_OPT_VAR_AUTO_GUARD,
                                                UI_OPT_VAR_ATTACK_TURN,
                                                UI_OPT_VAR_SHADOWS_WAIT_DOWN,
                                                UI_OPT_VAR_ONE_HIT,
                                                UI_OPT_VAR_NEVER_DODGE,
                                                UI_OPT_VAR_BARRIER,
                                                UI_OPT_PIT_TIME,
                                                UI_OPT_YORDA_TRIPS,
                                                UI_OPT_VAR_NO_HINTS,
                                                UI_OPT_DIFF_KNOCK_LIMIT,
                                                UI_OPT_DIFF_KNOCK_SCOPE,
                                                UI_OPT_DIFF_PERMADEATH};

_Static_assert(ICO_DIFF_ROW_COUNT == 19 && ICO_DIFF_SHADOW_SPEED == 0 &&
                   ICO_DIFF_PERMADEATH == ICO_DIFF_ROW_COUNT - 1,
               "an option for each of the draft's rows, difficulty.h's order");

/* --- the pages ------------------------------------------------------------- */

/* Customize: the three pages' links, Back */
static const int kCustOpts[] = {UI_OPT_LINK, UI_OPT_LINK, UI_OPT_LINK, UI_OPT_BACK};
static const int kCustStrs[] = {UI_STR_SECTION_SHADOWS, UI_STR_SECTION_COMBAT, UI_STR_DIFF_RULES,
                                UI_STR_BACK};
static const int kCustLinks[] = {UI_PAGE_SHADOWS, UI_PAGE_COMBAT, UI_PAGE_RULES, -1};
/* Shadows: how fast, tough and bold they are, then the rules about them */
static const int kShadowOpts[] = {
    UI_OPT_SHADOW_SPEED, UI_OPT_VAR_TOUGHNESS,    UI_OPT_VAR_AGGRESSION,  UI_OPT_VAR_PEACEFUL,
    UI_OPT_YORDA,        UI_OPT_SHADOWS_TOGETHER, UI_OPT_DIFF_NO_SHADOWS, UI_OPT_BACK};
static const int kShadowStrs[] = {UI_STR_OPT_SHADOW_SPEED,   UI_STR_OPT_VAR_TOUGHNESS,
                                  UI_STR_OPT_VAR_AGGRESSION, UI_STR_OPT_VAR_PEACEFUL,
                                  UI_STR_OPT_YORDA,          UI_STR_OPT_SHADOWS_TOGETHER,
                                  UI_STR_DIFF_NO_SHADOWS,    UI_STR_BACK};
/* Combat: Ico's help in a fight, then the fight's rules */
static const int kCombatOpts[] = {UI_OPT_VAR_AUTO_GUARD,
                                  UI_OPT_VAR_ATTACK_TURN,
                                  UI_OPT_VAR_SHADOWS_WAIT_DOWN,
                                  UI_OPT_VAR_ONE_HIT,
                                  UI_OPT_VAR_NEVER_DODGE,
                                  UI_OPT_VAR_BARRIER,
                                  UI_OPT_BACK};
static const int kCombatStrs[] = {UI_STR_OPT_VAR_AUTO_GUARD,
                                  UI_STR_OPT_VAR_ATTACK_TURN,
                                  UI_STR_OPT_VAR_SHADOWS_WAIT_DOWN,
                                  UI_STR_OPT_VAR_ONE_HIT,
                                  UI_STR_OPT_VAR_NEVER_DODGE,
                                  UI_STR_OPT_VAR_BARRIER,
                                  UI_STR_BACK};
/* Rules: the rescue time, Yorda's falls, the hints, the limits */
static const int kRulesOpts[] = {
    UI_OPT_PIT_TIME,         UI_OPT_YORDA_TRIPS,     UI_OPT_VAR_NO_HINTS, UI_OPT_DIFF_KNOCK_LIMIT,
    UI_OPT_DIFF_KNOCK_SCOPE, UI_OPT_DIFF_PERMADEATH, UI_OPT_BACK};
static const int kRulesStrs[] = {UI_STR_OPT_PIT_TIME,
                                 UI_STR_OPT_YORDA_TRIPS,
                                 UI_STR_OPT_VAR_NO_HINTS,
                                 UI_STR_DIFF_KNOCK_LIMIT,
                                 UI_STR_DIFF_KNOCK_SCOPE,
                                 UI_STR_DIFF_PERMADEATH,
                                 UI_STR_BACK};

_Static_assert(sizeof(kCustOpts) == sizeof(kCustStrs) && sizeof(kCustOpts) == sizeof(kCustLinks),
               "a string and a link for each Customize row");
_Static_assert(sizeof(kShadowOpts) == sizeof(kShadowStrs), "a string for each Shadows row");
_Static_assert(sizeof(kCombatOpts) == sizeof(kCombatStrs), "a string for each Combat row");
_Static_assert(sizeof(kRulesOpts) == sizeof(kRulesStrs), "a string for each Rules row");
_Static_assert(N_OF(kShadowOpts) - 1 + N_OF(kCombatOpts) - 1 + N_OF(kRulesOpts) - 1 ==
                   ICO_DIFF_ROW_COUNT,
               "every row of the draft on one of the three pages");

int ui__diff_page(int page, const int **opts, const int **strs, const int **links, int *header)
{
    switch (page) {
    case UI_PAGE_CUSTOMIZE:
        *opts = kCustOpts;
        *strs = kCustStrs;
        *links = kCustLinks;
        *header = UI_STR_DIFF_CUSTOMIZE;
        return N_OF(kCustOpts);
    case UI_PAGE_SHADOWS:
        *opts = kShadowOpts;
        *strs = kShadowStrs;
        *links = NULL;
        *header = UI_STR_SECTION_SHADOWS;
        return N_OF(kShadowOpts);
    case UI_PAGE_COMBAT:
        *opts = kCombatOpts;
        *strs = kCombatStrs;
        *links = NULL;
        *header = UI_STR_SECTION_COMBAT;
        return N_OF(kCombatOpts);
    case UI_PAGE_RULES:
        *opts = kRulesOpts;
        *strs = kRulesStrs;
        *links = NULL;
        *header = UI_STR_DIFF_RULES;
        return N_OF(kRulesOpts);
    default:
        return 0;
    }
}

/* --- the draft --------------------------------------------------------------- */

static IcoDifficulty *draft(void)
{
    if (!s_haveDraft) {
        ui__diff_draft_start(NULL);
    }
    return &s_draft;
}

static const char *const kModeLog[ICO_DIFF_MODE_COUNT] = {"Easy", "Normal", "Hard", "Custom"};

static const char *modeLog(IcoDiffMode m)
{
    return m >= 0 && m < ICO_DIFF_MODE_COUNT ? kModeLog[m] : "Custom";
}

/* the draft's mode after a change, a Custom draft kept aside */
static void classified(void)
{
    s_draft.mode = ico_difficulty_classify(&s_draft);
    if (s_draft.mode == ICO_DIFF_CUSTOM) {
        s_custom = s_draft;
        s_haveCustom = 1;
    }
}

void ui__diff_draft_start(const IcoDifficulty *from)
{
    if (from != NULL) {
        s_draft = *from;
    } else {
        ico_difficulty_preset(ICO_DIFF_NORMAL, &s_draft);
    }
    /* the journey's own numbers are the confirm's (ico_difficulty_begin) */
    s_draft.knocks_room = s_draft.knocks_journey = s_draft.in_fall = 0;
    s_draft.over_pending = s_draft.finished = 0;
    s_haveDraft = 1;
    s_haveCustom = 0;
    memset(&s_custom, 0, sizeof(s_custom));
    classified();
}

const IcoDifficulty *ui__diff_draft(void)
{
    return draft();
}

const struct IcoDifficulty *ui_new_game_screen_draft(void)
{
    return draft();
}

IcoDiffMode ui__diff_draft_mode(void)
{
    return ico_difficulty_classify(draft());
}

int ui__diff_has_custom(void)
{
    (void)draft();
    return s_haveCustom;
}

void ui__diff_step_mode(int dir)
{
    IcoDiffMode list[ICO_DIFF_MODE_COUNT] = {ICO_DIFF_EASY, ICO_DIFF_NORMAL, ICO_DIFF_HARD,
                                             ICO_DIFF_CUSTOM};
    const int n = ui__diff_has_custom() ? 4 : 3;
    const IcoDiffMode cur = ui__diff_draft_mode();
    int i = 0;
    for (int k = 0; k < n; k++) {
        if (list[k] == cur) {
            i = k;
        }
    }
    const IcoDiffMode to = list[((i + (dir < 0 ? -1 : 1)) % n + n) % n];
    if (to == ICO_DIFF_CUSTOM) {
        memcpy(s_draft.row, s_custom.row, sizeof(s_draft.row));
    } else {
        IcoDifficulty p;
        ico_difficulty_preset(to, &p);
        memcpy(s_draft.row, p.row, sizeof(s_draft.row));
    }
    s_draft.mode = ico_difficulty_classify(&s_draft);
    fprintf(stderr, "newgame: Difficulty %s (was %s%s)\n", modeLog(s_draft.mode), modeLog(cur),
            s_haveCustom && to != ICO_DIFF_CUSTOM ? "; the Custom rows kept aside" : "");
}

int ui__diff_mode_str(IcoDiffMode mode)
{
    static const int kStr[ICO_DIFF_MODE_COUNT] = {UI_STR_DIFF_EASY, UI_STR_DIFF_NORMAL,
                                                  UI_STR_DIFF_HARD, UI_STR_DIFF_CUSTOM};
    return kStr[mode >= 0 && mode < ICO_DIFF_MODE_COUNT ? mode : ICO_DIFF_CUSTOM];
}

int ui__diff_mode_note(IcoDiffMode mode)
{
    static const int kStr[ICO_DIFF_MODE_COUNT] = {UI_STR_DIFF_EASY_NOTE, UI_STR_DIFF_NORMAL_NOTE,
                                                  UI_STR_DIFF_HARD_NOTE, UI_STR_DIFF_CUSTOM_NOTE};
    return kStr[mode >= 0 && mode < ICO_DIFF_MODE_COUNT ? mode : ICO_DIFF_CUSTOM];
}

/* --- the rows ------------------------------------------------------------------ */

int ui__diff_row(int opt)
{
    for (int r = 0; r < ICO_DIFF_ROW_COUNT; r++) {
        if (kRowOpt[r] == opt) {
            return r;
        }
    }
    return -1;
}

int ui__diff_opt(int opt)
{
    return ui__diff_row(opt) >= 0;
}

/* the Queen's barrier as the disc ships it (queen.c: 7 on the June 2001
   demo, difficulty.c shipped_barrier): the row's 0 */
static int shippedBarrier(void)
{
    return ICO_JUN26 ? 7 : 5;
}

const char *ui__diff_text(int opt, char *buf, unsigned size)
{
    static const int kSpeed[ICO_SPEED_COUNT] = {UI_STR_VAL_SPEED_SLOWER, UI_STR_VAL_ORIGINAL,
                                                UI_STR_VAL_SPEED_FASTER, UI_STR_VAL_SPEED_FASTEST};
    static const int kTough[ICO_TOUGHNESS_COUNT] = {UI_STR_VAL_ORIGINAL, UI_STR_VAL_VAR_AMERICAN,
                                                    UI_STR_VAL_VAR_NOT_TOUGHER,
                                                    UI_STR_VAL_VAR_THIRD_JOURNEY};
    static const int kAgg[ICO_AGGRESSION_COUNT] = {
        UI_STR_VAL_VAR_AGG_OWN, UI_STR_VAL_VAR_AGG_BOLD_PATIENT, UI_STR_VAL_VAR_AGG_BOLD_QUICK,
        UI_STR_VAL_VAR_AGG_WARY_PATIENT, UI_STR_VAL_VAR_AGG_WARY_QUICK};
    const int row = ui__diff_row(opt);
    if (row < 0) {
        return "";
    }
    const int v = draft()->row[row];
    switch (row) {
    case ICO_DIFF_SHADOW_SPEED:
        return ui_str(kSpeed[v >= 0 && v < ICO_SPEED_COUNT ? v : ICO_SPEED_ORIGINAL]);
    case ICO_DIFF_SHADE_TOUGHNESS:
        return ui_str(kTough[v >= 0 && v < ICO_TOUGHNESS_COUNT ? v : 0]);
    case ICO_DIFF_AGGRESSION:
        return ui_str(kAgg[v >= 0 && v < ICO_AGGRESSION_COUNT ? v : 0]);
    case ICO_DIFF_BARRIER_HITS:
        /* the disc's own number for 0 */
        return ui_str((v == 0 ? shippedBarrier() : v) == 7 ? UI_STR_VAL_VAR_HITS_7
                                                           : UI_STR_VAL_VAR_HITS_5);
    case ICO_DIFF_PIT_TIME:
        /* whole seconds; Original shows the disc's own time with the word */
        if (v == ICO_PIT_ORIGINAL) {
            snprintf(buf, size, "%d s (%s)", ico_opt_pit_base_seconds(),
                     ui_str(UI_STR_VAL_ORIGINAL));
        } else {
            snprintf(buf, size, "%d s", v);
        }
        return buf;
    case ICO_DIFF_KNOCK_LIMIT:
        if (v <= 0) {
            return ui_str(UI_STR_OFF);
        }
        snprintf(buf, size, "%d", v);
        return buf;
    case ICO_DIFF_KNOCK_SCOPE:
        return ui_str(v == ICO_DIFF_SCOPE_JOURNEY ? UI_STR_DIFF_SCOPE_JOURNEY
                                                  : UI_STR_DIFF_SCOPE_ROOM);
    default:
        return ui_str(v ? UI_STR_ON : UI_STR_OFF);
    }
}

void ui__diff_step(int opt, int dir)
{
    const int row = ui__diff_row(opt);
    IcoDifficulty *d = draft();
    if (row < 0 || dir == 0) {
        return;
    }
    const int was = d->row[row];
    if (row == ICO_DIFF_BARRIER_HITS) {
        /* the two numbers the game knows, the disc's own stored as 0 (the
           record's "the disc's"), so a step always changes what shows */
        const int shown = was == 0 ? shippedBarrier() : was;
        const int to = shown == 7 ? 5 : 7;
        d->row[row] = to == shippedBarrier() ? 0 : to;
    } else {
        (void)ico_difficulty_row_step(d, (IcoDiffRow)row, dir);
    }
    classified();
    fprintf(stderr, "newgame: Customize: %s %d -> %d (the draft is %s)\n",
            ico_difficulty_row_key((IcoDiffRow)row), was, (int)d->row[row], modeLog(s_draft.mode));
}

int ui__diff_shown(int opt)
{
    /* every row on every disc: a value the disc lacks is never stepped to
       (ico_difficulty_row_step, ico_variant_offered), and the rows that do
       nothing there say so in their notes */
    return ui__diff_opt(opt);
}

int ui__diff_note(int opt)
{
    const int row = ui__diff_row(opt);
    switch (row) {
    case ICO_DIFF_SHADOW_SPEED:
        return UI_STR_OPT_SHADOW_SPEED_NOTE;
    case ICO_DIFF_SHADE_TOUGHNESS:
        /* the June 2001 demo's Shadows are its own; Third journey explains
           itself when stepped to */
        if (ICO_JUN26) {
            return UI_STR_OPT_VAR_TOUGHNESS_NOTE_DEMO;
        }
        return draft()->row[row] == ICO_TOUGHNESS_THIRD_JOURNEY
                   ? UI_STR_OPT_VAR_TOUGHNESS_NOTE_THIRD
                   : UI_STR_OPT_VAR_TOUGHNESS_NOTE;
    case ICO_DIFF_AGGRESSION:
        return UI_STR_OPT_VAR_AGGRESSION_NOTE;
    case ICO_DIFF_PEACEFUL:
        return UI_STR_OPT_VAR_PEACEFUL_NOTE;
    case ICO_DIFF_YORDA_SAFE:
        return UI_STR_OPT_YORDA_NOTE;
    case ICO_DIFF_SHADOWS_TOGETHER:
        return UI_STR_OPT_SHADOWS_TOGETHER_NOTE;
    case ICO_DIFF_NO_SHADOWS:
        return UI_STR_DIFF_NO_SHADOWS_NOTE;
    case ICO_DIFF_AUTO_GUARD:
        return UI_STR_OPT_VAR_AUTO_GUARD_NOTE;
    case ICO_DIFF_ATTACK_TURN:
        return UI_STR_OPT_VAR_ATTACK_TURN_NOTE;
    case ICO_DIFF_SHADOWS_WAIT_DOWN:
        return UI_STR_OPT_VAR_SHADOWS_WAIT_DOWN_NOTE;
    case ICO_DIFF_ONE_HIT:
        return UI_STR_OPT_VAR_ONE_HIT_NOTE;
    case ICO_DIFF_NEVER_DODGE:
        return UI_STR_OPT_VAR_NEVER_DODGE_NOTE;
    case ICO_DIFF_BARRIER_HITS:
        return UI_STR_OPT_VAR_BARRIER_NOTE;
    case ICO_DIFF_PIT_TIME:
        return UI_STR_OPT_PIT_TIME_NOTE;
    case ICO_DIFF_YORDA_TRIPS:
        return UI_STR_OPT_YORDA_TRIPS_NOTE;
    case ICO_DIFF_NO_HINTS:
        /* the three 2001 discs have no hints (variants.c ico_hints_on) */
        return ICO_NTSCAUG6JUN26 ? UI_STR_OPT_VAR_NO_HINTS_NOTE_2001 : UI_STR_OPT_VAR_NO_HINTS_NOTE;
    case ICO_DIFF_KNOCK_LIMIT:
        return UI_STR_DIFF_KNOCK_LIMIT_NOTE;
    case ICO_DIFF_KNOCK_SCOPE:
        return UI_STR_DIFF_KNOCK_SCOPE_NOTE;
    case ICO_DIFF_PERMADEATH:
        return UI_STR_DIFF_PERMADEATH_NOTE;
    default:
        return 0;
    }
}

int ui__diff_dimmed(int opt)
{
    /* the count does nothing while there is no limit; Permadeath covers
       every game over, so it is never dimmed */
    return opt == UI_OPT_DIFF_KNOCK_SCOPE && draft()->row[ICO_DIFF_KNOCK_LIMIT] <= 0;
}
