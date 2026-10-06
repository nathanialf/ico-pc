/*
 * port/ui/ui_host.h
 *
 * port/ui in the window build (ui_host.c, linked into ico_pc only): the
 * game's GS frame, language and the rd scale fed to the text, the decoder
 * flush around port draws, and the popups' per-vsync step.  window_host.c
 * calls these (docs/port/UI.md, "Popups").
 *
 *   ui_HostInit()            after rd_Init: the font, the hooks, the
 *                            presentation overlay that draws the popups
 *                            (rd_SetPresentOverlay), the [dev] popup_test
 *                            switch (ICO_UI_POPUP_TEST, exported by
 *                            host_config.c)
 *   ui_HostVsync(mainTick)   once per vsync after the simulation step: the
 *                            popup test trigger and the popup clock
 *   ui_HostShutdown()        before rd_Shutdown: the overlay, the atlases'
 *                            textures
 */
#ifndef PORT_UI_HOST_H
#define PORT_UI_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_HostInit(void);
void ui_HostVsync(unsigned int mainTick);
void ui_HostShutdown(void);
/* Package MV: fn draws on the presentation overlay before the popups, at
   every present (port/game/model_viewer.c's name and frame); NULL: none */
struct RdOverlayCtx;
void ui_HostSetViewerOverlay(void (*fn)(const struct RdOverlayCtx *ctx));

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_HOST_H */
