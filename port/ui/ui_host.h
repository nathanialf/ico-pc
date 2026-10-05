/*
 * port/ui/ui_host.h
 *
 * port/ui in the window build (ui_host.c, linked into ico_pc only): the
 * game's GS frame, language and the rd scale fed to the text, the decoder
 * flush around port draws, and the popups' per-vsync step.  window_host.c
 * calls these (docs/port/UI.md, "Popups").
 *
 *   ui_HostInit()            after rd_Init: the font, the hooks, the
 *                            [dev] popup_test switch (ICO_UI_POPUP_TEST,
 *                            exported by host_config.c)
 *   ui_HostVsync(mainTick)   once per vsync after the simulation step: the
 *                            popup test trigger, the popup clock, and the
 *                            popup recorded into the open frame once per
 *                            game frame (frame_count)
 *   ui_HostShutdown()        before rd_Shutdown: the atlases' textures
 */
#ifndef PORT_UI_HOST_H
#define PORT_UI_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_HostInit(void);
void ui_HostVsync(unsigned int mainTick);
void ui_HostShutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_HOST_H */
