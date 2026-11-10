/*
 * port/ui/settings_difficulty.h
 *
 * The New Game's difficulty draft and the Customize pages that edit it
 * (settings_difficulty.c): which options are the draft's rows, their value
 * text, steps, notes and dimming, the four pages' rows, and the draft's
 * mode as the New Game screen's Difficulty row steps it (Easy, Normal, Hard,
 * and Custom while a customized draft is kept aside).  settings.c builds the
 * pages from ui__diff_page and routes the rows' value, step, note and shown
 * calls here; settings_newgame.c starts the draft, steps its mode and hands
 * it to port/game/difficulty.h ico_difficulty_begin at the confirm.  Kept in
 * its own file so settings.c stays under the repository's 256 KiB file cap.
 */
#ifndef PORT_UI_SETTINGS_DIFFICULTY_H
#define PORT_UI_SETTINGS_DIFFICULTY_H

#include "difficulty.h"

#ifdef __cplusplus
extern "C" {
#endif

/* the Customize page's parent: the New Game screen (settings.c parentLayout) */
#define UI__PARENT_NEW_GAME (-2)

/* 1 when opt is a row of the draft (a UiSettingsOpt of Customize's pages) */
int ui__diff_opt(int opt);
/* the draft's row an option is, -1 for none */
int ui__diff_row(int opt);
/* the value text of row opt in the draft, in the port's language */
const char *ui__diff_text(int opt, char *buf, unsigned size);
/* Left (-1) or Right (+1) on row opt: the draft's row stepped (the values
   the running disc offers), the mode classified again; logged */
void ui__diff_step(int opt, int dir);
/* whether row opt shows on the running disc (every row does today) */
int ui__diff_shown(int opt);
/* the note string of row opt on the running disc and draft value (the June
   2001 demo's toughness, Third journey's, the 2001 discs' No hints) */
int ui__diff_note(int opt);
/* row opt drawn greyed: Knock-downs count while the limit is Off */
int ui__diff_dimmed(int opt);
/* page's rows (UI_PAGE_CUSTOMIZE, UI_PAGE_SHADOWS, UI_PAGE_COMBAT,
   UI_PAGE_RULES): the options, their labels, the pages they open (-1 none)
   and the header; the count, 0 for another page */
int ui__diff_page(int page, const int **opts, const int **strs, const int **links, int *header);

/* the draft: from a record (a cleared save's rows), or Normal for NULL; the
   counts and journey are the confirm's (ico_difficulty_begin) */
void ui__diff_draft_start(const IcoDifficulty *from);
const IcoDifficulty *ui__diff_draft(void);
/* the mode the draft's rows are (ico_difficulty_classify) */
IcoDiffMode ui__diff_draft_mode(void);
/* a customized draft is kept aside (the Difficulty row offers Custom) */
int ui__diff_has_custom(void);
/* the Difficulty row's Left/Right: Easy, Normal, Hard, then Custom while one
   is kept aside, around; a preset replaces the rows, Custom restores the
   kept draft */
void ui__diff_step_mode(int dir);
/* a mode's name and its note (UI_STR_DIFF_*) */
int ui__diff_mode_str(IcoDiffMode mode);
int ui__diff_mode_note(IcoDiffMode mode);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_DIFFICULTY_H */
