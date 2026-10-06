/*
 * port/ui/photo_ui.h
 *
 * Photo mode's screen (package PHOTO; docs/port/DISPLAY.md "Photo mode",
 * docs/port/SETTINGS.md): the port layout the pause menu's Options >
 * "Photo mode" row opens.  It has one row, masked, so the game's layout
 * code draws nothing of it and no cursor; its proc runs once a Main tick
 * and hands the pad to port/game/photo_mode.h, which the window turns into
 * the renderer's camera override.  Triangle, Circle or Start go back to the
 * Options screen with the cursor on the row.  The help lines (the HUD) are
 * drawn on the presentation overlay, never in the game's frame, so a
 * capture never shows them.
 */
#ifndef PORT_UI_PHOTO_UI_H
#define PORT_UI_PHOTO_UI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the layout (settings.c's build, once); returns its index, -1 when
   the layout extension is full.  back: the row the cursor returns to on
   layout 58. */
int ui_PhotoBuild(int back);
int ui_PhotoLayout(void);
/* Whether the pause menu offers photo mode: a stage is running (not the
   title's stage 1). */
int ui_PhotoAvailable(void);
/* Drops the layout (ui_SettingsReset). */
void ui_PhotoReset(void);

/* A capture's result (the window, port/platform/window_host.c): a popup
   with the file's name, and a log line. */
void ui_PhotoCaptureDone(int ok, const char *name);

struct RdOverlayCtx;
/* The HUD on the output (ui_host.c's overlay callback): nothing unless
   photo mode is on with its HUD shown. */
void ui_PhotoDrawOverlay(const struct RdOverlayCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_PHOTO_UI_H */
