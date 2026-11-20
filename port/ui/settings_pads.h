/*
 * port/ui/settings_pads.h
 *
 * Options > Controls > Button pictures ([input] button_pictures,
 * port/game/options.h): the row's value text and stepping, and the
 * resolution of the set the menus, the prompts, photo mode and the touch
 * screen draw (port/ui/pad_glyphs.h), once a frame.  settings.c builds the
 * page and calls these from its case lines; they live in their own file so
 * settings.c stays under the repository's 256 KiB file cap.
 *
 * Every call on the thread that draws the menus (pad_glyphs.h's rule).
 */
#ifndef PORT_UI_SETTINGS_PADS_H
#define PORT_UI_SETTINGS_PADS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The style in force, once a frame before the frame's first picture (the
   host's ui_host_vsync, port/ui/ui_host.c, and at ui_host_init): the
   option's set, or under Auto the kind of the controller pressed last
   (port/input/input.h ico_input_last_pad_kind: Xbox, Nintendo, else
   PlayStation; the keyboard, the mouse and the touch screen give
   PlayStation; Wii Classic Controller is never Auto's).  Calls
   pad_glyphs_set_style with it every time (cheap; it lets another set's
   pictures go) and logs a change of the style or of where it came from,
   the first 8 then every 100th:
   "controls: button pictures: Xbox (Auto, the last pad used), change 2".
   Returns the style (IcoPadGlyphStyle). */
int ui_settings_pads_frame(void);
/* The row's choice in force, ICO_BUTTON_PICTURES_* (options.h): the
   option's, but a set that cannot be drawn whole (pad_glyphs_set_complete)
   reads as Auto, logged once a set:
   "controls: button pictures: Xbox is not a complete set: Auto". */
int ui__pads_choice(void);
/* UI_OPT_BUTTON_PICTURES's value text: "Auto (Xbox)" (the set Auto
   resolves to now), "PlayStation", "Xbox", "Nintendo",
   "Wii Classic Controller" (the names are not translated); buf holds the
   composed Auto text */
const char *ui__pads_text(char *buf, unsigned size);
/* Left/Right on the row: Auto, PlayStation, Xbox, Nintendo, Wii Classic
   Controller around, a set that is not complete skipped; writes [input]
   button_pictures in memory (the page's leaving saves) and resolves at
   once */
void ui__pads_step(int dir);
/* Tests: the changes ui_settings_pads_frame has logged, and a fresh start
   (no style resolved yet, the counts and the once-a-set lines forgotten) */
unsigned ui__pads_changes(void);
void ui__pads_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_PADS_H */
