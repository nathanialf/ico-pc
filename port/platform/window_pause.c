/*
 * port/platform/window_pause.c
 *
 * The game's pause for the renderer (window_pause.h).
 */
#include "window_pause.h"

#include "photo_mode.h"
#include "rd.h"

extern int systemStatus[12]; /* ico2/common/src/main.c, .data */

int window_paused(void)
{
    return systemStatus[5] != 0 && !ico_photo_active();
}

void window_pause_hook(void)
{
    rd_set_paused_source(window_paused);
}
