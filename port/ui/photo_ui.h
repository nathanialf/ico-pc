/*
 * port/ui/photo_ui.h
 *
 * Photo mode's screen: the port layout the pause menu's "Photo mode" row
 * opens.  It has one row, masked, so the game's layout code draws nothing
 * of it and no cursor; its proc runs once a Main tick and hands the pad to
 * port/game/photo_mode.h, which the window turns into the camera the paused
 * game draws from.  Triangle, Circle or Start go back to the pause menu
 * with the cursor on the row.  The help panel (the HUD) is drawn on the
 * presentation overlay, never in the game's frame, so a capture never shows
 * it.  Beside each action it shows the button's picture from the game's own
 * button sheets (layout_ext.h's glyphs, the textures the menus draw them
 * from) or, for a button the sheets have no picture of (the sticks, L3, R3,
 * Up, Down, Select), its name; once a key or the mouse was the last thing
 * you pressed, your keys' names in square brackets instead.
 */
#ifndef PORT_UI_PHOTO_UI_H
#define PORT_UI_PHOTO_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the layout (settings.c's build, once); returns its index, -1 when
   the layout extension is full.  Leaving it goes back to the pause menu
   through settings.h ui_settings_photo_back. */
int ui_photo_build(void);
int ui_photo_layout(void);
/* Whether the pause menu offers photo mode: a stage is running (not the
   title's stage 1). */
int ui_photo_available(void);
/* Drops the layout (ui_settings_reset). */
void ui_photo_reset(void);

/* A capture's result (the window, port/platform/window_host.c): a popup
   with the file's name, and a log line. */
void ui_photo_capture_done(int ok, const char *name);

struct RdOverlayCtx;
/* The HUD on the output (ui_host.c's overlay callback): nothing unless
   photo mode is on with its HUD shown. */
void ui_photo_draw_overlay(const struct RdOverlayCtx *ctx);

#ifdef ICO_RD
/* The panel's make-up, split from the drawing so a test can read it.  The
   grid is the overlay's 640 x 448; the panel starts UI_PHOTO_HUD_X in and
   its widest line may use UI_PHOTO_HUD_ROOM. */
#define UI_PHOTO_HUD_X 22.0f
#define UI_PHOTO_HUD_ROOM (640.0f - 2.0f * UI_PHOTO_HUD_X)
#define UI_HUD_LINES 5
#define UI_HUD_ITEMS 24
#define UI_HUD_ICONS 2
#define UI_HUD_KEYS 24

/* One button: a picture from the game's sheets (glyph, an LtExtGlyph) or,
   glyph -1, its name in the menu font (a button with no picture, or your
   key in square brackets: "[Space]", "[W A S D]"). */
typedef struct UiHudIcon {
    int glyph;
    const char *word;
} UiHudIcon;

/* Buttons, then the action: the L1 and R1 pictures, then "roll".  A
   line's items run left to right. */
typedef struct UiHudItem {
    int line;
    int nicon;
    UiHudIcon icon[UI_HUD_ICONS];
    const char *text;
} UiHudItem;

typedef struct UiHudSet {
    int n;
    int nkeys;
    UiHudItem item[UI_HUD_ITEMS];
    char keys[UI_HUD_KEYS][112];
} UiHudSet;

/* Where one item lies, in grid units from the panel's left edge. */
typedef struct UiHudPlaced {
    float x0, x1;
    float iconX[UI_HUD_ICONS], iconW[UI_HUD_ICONS];
    float textX, textW;
} UiHudPlaced;

/* The items for the camera in use; keyboard names the keys from the live
   bindings (the first key, else the first mouse button, else the pad's
   button); title and fov are the first and last lines. */
void ui__photo_hud_build(UiHudSet *set, int freeCam, int keyboard, const char *title,
                         const char *fov);
/* Places the items at the text size; gaps between a line's items squeeze
   when it is wider than room.  Returns the widest line's width. */
float ui__photo_hud_layout(const UiHudItem *items, int n, float size, float room, UiHudPlaced *out);
/* ui__photo_hud_layout at *size, the size made smaller until the widest line
   fits room (the words' widths and the squeezed gaps do not scale exactly
   with the size, so one proportional step can leave a few units over).
   Returns the widest line's width; *size is the size used. */
float ui__photo_hud_fit(const UiHudItem *items, int n, float *size, float room, UiHudPlaced *out);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_PHOTO_UI_H */
