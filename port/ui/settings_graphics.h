/*
 * port/ui/settings_graphics.h
 *
 * Settings > Graphics: the Graphics API and Graphics card rows' values and
 * the Apply switch with its countdown (settings_graphics.c), for the page
 * settings.c builds.  The public side (the renderer and card names, the
 * host) is settings.h's ui_settings_set_backend_names,
 * ui_settings_set_adapter_names and ui_settings_set_graphics_host.
 */
#ifndef PORT_UI_SETTINGS_GRAPHICS_H
#define PORT_UI_SETTINGS_GRAPHICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* the page's state: nothing under way, Apply's switch, the countdown that
   follows it, the switch back */
enum { UI__GFX_IDLE, UI__GFX_APPLYING, UI__GFX_COUNTDOWN, UI__GFX_REVERTING };

int ui__gfx_state(void);
/* the countdown's vsyncs left */
int ui__gfx_left(void);
/* 1 once after the countdown began (the cursor goes to Revert) */
int ui__gfx_take_focus(void);
/* the page entered: Revert's values are the ones found from now on (while
   nothing is under way) */
void ui__gfx_page_entered(void);
/* the renderers listed (the Graphics API row and the page show with two
   or more) and the suitable cards (the Graphics card row shows with two or
   more) */
int ui__gfx_backend_count(void);
int ui__gfx_adapter_count(void);
/* the Graphics API row's text into buf (Automatic, or the renderer's
   proper name) */
const char *ui__gfx_backend_text(char *buf, unsigned size);
/* [video] adapter as the row shows it: the trial's while Apply's switch or
   the countdown is under way, else the config's ("" Automatic) */
const char *ui__gfx_adapter_value(void);
/* Left or Right on a row: 1 when the config changed */
int ui__gfx_step_backend(int dir);
int ui__gfx_step_adapter(int dir);
/* Apply's row shows: a host, nothing under way, and the rows' values name
   a renderer or a card other than the one running */
int ui__gfx_apply_shown(void);
void ui__gfx_apply(void);
/* Revert, Circle (UI_GFX_WHY_REVERT) or the countdown's end
   (UI_GFX_WHY_TIMEOUT) */
void ui__gfx_revert(int why);
/* Keep: 1 when the config changed (written on leaving) */
int ui__gfx_keep(void);
/* a switch the page waits for (every frame and every vsync) */
void ui__gfx_poll(void);
/* one vsync of the countdown */
void ui__gfx_vsync(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_GRAPHICS_H */
