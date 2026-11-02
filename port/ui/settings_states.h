/*
 * port/ui/settings_states.h
 *
 * Settings > Extras > Save states: the host's listing and requests, the
 * cards' texts and pictures, the actions on a slot and F6's latch
 * (settings_states.c), for the page settings.c builds, opens, leaves and
 * ticks.  The public side is settings.h's ui_settings_set_states_host,
 * ui_settings_set_states_tex_funcs, ui_settings_states_card and
 * ui_settings_states_key.
 */
#ifndef PORT_UI_SETTINGS_STATES_H
#define PORT_UI_SETTINGS_STATES_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* a host is set (ui_settings_set_states_host) */
int ui__states_hosted(void);
/* the page built: slot's card rows {picture, label, action word, arrow
   left, arrow right, area and play time, date} (its action not chosen
   yet), and the status line under the four */
void ui__states_set_card(int slot, const int rows[7]);
void ui__states_set_status(int row);
/* the menus uninstalled: the pictures released, every card row -1 */
void ui__states_reset(void);
/* the page closed: every picture released, the listing forgotten */
void ui__states_release(void);
/* the page is up: its listing and pictures held */
int ui__states_is_open(void);
/* the page opens: no action chosen, the listing and the pictures */
void ui__states_open(void);
/* the slot the cursor (a row index) is on, -1 for none */
int ui__states_cursor_slot(int cur);
/* the page's tick before its input: the second press's time and the
   cursor moving off it, the listing again when due */
void ui__states_tick(int slot);
/* the page's input on slot: dir -1 Left, 1 Right, 0 neither (the next
   possible action); ok Cross (the action, or its second press asked for) */
void ui__states_input(int slot, int dir, int ok);
/* the cards' texts and the status line from the listing and the cursor */
void ui__states_refresh(int cur);
/* F6's latch: its log lines' limiter (ignored 0 or 1: the first 8, then
   every 100th, each), taken (1 when a press is waiting and recent; a late
   one dropped with its line) and armed at this flip */
int ui__states_key_line(int ignored);
int ui__states_key_take(void);
void ui__states_key_arm(void);

/* settings.c's: the menus built, opened from the title (onTitle), its
   word wrap at the note size, the save screen's area name (NULL none) and
   play time */
int ui__settings_built(void);
int ui__settings_on_title(void);
void ui__settings_wrap_note(const char *text, float width, char *out, unsigned outSize);
const char *ui__settings_area_name(int stage);
void ui__settings_play_time(unsigned int frames, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_STATES_H */
