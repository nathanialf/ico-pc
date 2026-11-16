/*
 * port/platform/window_pause.h
 *
 * The game's pause for the renderer's paused scene's hold (port/render/
 * rd_hold.c, rd.h rd_set_paused_source; issue 123): window_pause_hook
 * (window_host.c, once the renderer is up) hands rd_end_frame the rule
 * window_paused: the pause menu's and the title's systemStatus[5]
 * (common/src/main.c) set, photo mode not open (its paused game follows the
 * photo camera, and its feedback follows the picture as in play:
 * staticBlur.c).
 */
#ifndef ICO_PLATFORM_WINDOW_PAUSE_H
#define ICO_PLATFORM_WINDOW_PAUSE_H

int window_paused(void);
void window_pause_hook(void);

#endif
