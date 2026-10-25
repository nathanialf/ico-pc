/*
 * ico2/common/include/layout_action.h
 *
 * The declarations of what layout_action.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef LAYOUT_ACTION_H
#define LAYOUT_ACTION_H

#include "typedef.h"

/* layout_action.o's globals: the frame count main keeps from a
   stage's start (the game loop's start button needs 11), the boot flag, the
   pause enable, the title demo toggle, the game loop's pause-request count,
   the stage the title demo leaves to (main stores -2 after a movie, e3 95,
   la_game_demo -1 to 1) and the flag boyact sets from the boy's pad that the
   pause-request count follows. */
extern int startStagePauseDisableTimer;
extern int layout_boot_flag;
extern int enable_game_pause;
extern int title_demo_mode;
extern int laoutActionPauseRequest;
extern unsigned int stage_after_skipping_demo;
extern int layoutActPushStartNew;
/* the memory-card request block the layout actions and the debug menu
   drive */
extern McMgr mc;
/* The pad trigger tests and the layout actions the texture layout's tables
   call, each an `inline` definition, in the order of their out-of-line copies
   at the end of the object (first-declaration order). */
int PSH_POSITIVE(int idx);
int PSH_NEGATIVE(int idx);
int PSH_LEFT(int idx);
int PSH_RIGHT(int idx);
/* The layout actions the texture layout's tables call, each an `inline`
   definition, in the order of their out-of-line copies at the end of the
   object (first-declaration order). */
int la_boot_memory_card_check(void);
int la_boot_no_memory_card(int first, int item);
int la_boot_no_free_area(int first, int item);
int la_boot_confirm_memory_card(void);
int la_scei_logo(int first);
int la_title_demo(void);
int la_mc_file_select(int first);
int la_mc_preview_info(void);
int la_mc_current_slot(void);
int la_mc_load_current_slot_select(void);
int la_mc_save_current_slot_select(void);
int la_general_mc_confirm(void);
int la_save_confirm_no_memory_card(int first);
int la_save_confirm_no_free_area(int first);
int la_format_processing(int first);
int la_save_confirm_complete(int first, int item);
int la_save_confirm_fail(void);
int la_format_confirm_fail(void);
int la_delete_start_check(int first);
int la_delete_confirm(int first, int item);
int la_delete_confirm_complete(void);
int la_delete_confirm_fail(void);
int la_game_loading(int first);
void la_playtime_count(void);
int la_game_demo(int first);
int la_game_demo_pause(int first);
int la_game_pause(int first);
int la_switching_stage(void);
int la_save_confirm_yesno(void);

#endif /* LAYOUT_ACTION_H */
