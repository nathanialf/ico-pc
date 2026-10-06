/*
 * port/ui/model_overlay.h
 *
 * Extras > Models (package MV, port/game/model_viewer.c): the model's name,
 * the animation and the frame drawn on the presentation overlay (rd.h
 * rd_SetPresentOverlay, through ui_host.c), at the top left of the 4:3
 * picture: a dark translucent panel sized to the text, the name in the
 * menu's warm white, the two lines below it in grey.  Kept here, apart from
 * the game side, so ui_test draws the same panel on a 1080p present.
 */
#ifndef PORT_UI_MODEL_OVERLAY_H
#define PORT_UI_MODEL_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

struct RdOverlayCtx;
/* draws inside an rd overlay callback; empty lines are left out */
void ui_ModelOverlayDraw(const struct RdOverlayCtx *ctx, const char *name, const char *anim,
                         const char *frame);
/* the panel's grid rectangle (x0, y0, x1, y1) for those lines, measured at
   the scale in force (ui_BeginOverlay's inside an overlay) */
void ui_ModelOverlayPanel(const char *name, const char *anim, const char *frame, float rect[4]);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_MODEL_OVERLAY_H */
