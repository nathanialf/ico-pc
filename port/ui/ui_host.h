/*
 * port/ui/ui_host.h
 *
 * port/ui in the window build (ui_host.c, linked into ico_pc only): the
 * game's GS frame, language and the rd scale fed to the text, the decoder
 * flush around port draws, and the popups' per-vsync step.  window_host.c
 * calls these.
 *
 *   ui_host_init()            after rd_init: the font, the hooks, the
 *                            presentation overlay that draws the popups
 *                            (rd_set_present_overlay), the [dev] popup_test
 *                            switch (ICO_UI_POPUP_TEST, exported by
 *                            host_config.c)
 *   ui_host_vsync(mainTick)   once per vsync after the simulation step: the
 *                            popup test trigger and the popup clock
 *   ui_host_shutdown()        before rd_shutdown: the overlay, the atlases'
 *                            textures
 *   ui_host_curtain(on)       the title's icon capture (port/game/title_icon.c):
 *                            while on, every present's top overlay layer
 *                            starts with an opaque black quad over the
 *                            whole output, so the player sees black frames
 *                            instead of the letter alone over black and
 *                            white.  Without the CRT filter the top layer
 *                            is drawn under the main overlay layer, so a
 *                            popup or the HUD shows over the quad (with the
 *                            filter the main layer is in its grid, under
 *                            it).  The capture copies the picture before
 *                            the overlay, so the quad never reaches it.
 *                            Any thread (an atomic flag)
 */
#ifndef PORT_UI_HOST_H
#define PORT_UI_HOST_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_host_init(void);
void ui_host_vsync(unsigned int mainTick);
void ui_host_shutdown(void);
void ui_host_curtain(int on);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_HOST_H */
