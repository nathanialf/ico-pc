/*
 * port/platform/window_sweep.h
 *
 * Shift+F12's aspect sweep in photo mode (window_sweep.c; the long comment
 * on how it works is there).
 */
#ifndef ICO_PLATFORM_WINDOW_SWEEP_H
#define ICO_PLATFORM_WINDOW_SWEEP_H

/* the Shift+F12 hotkey: starts the sweep (photo mode only) */
void window_sweep_start(void);
/* once a pump (a vsync): captures the aspect in force once settled and asks for the next */
void window_sweep_pump(void);

#endif /* ICO_PLATFORM_WINDOW_SWEEP_H */
