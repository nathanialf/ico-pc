/*
 * port/platform/window_host.h
 *
 * The windowed ico_pc (renderer wave 2, package R2a): one SDL3 window
 * titled "ICO", the Vulkan RHI device on it through rd_Init (Original
 * preset, vsync on), and the real-time pacing of the simulated vsyncs.
 * main_host.c drives it; the headless build (ICO_HEADLESS) leaves it out.
 *
 *   ico_window_open(gsW, gsH)   SDL video, the window, rd_Init; 0, or -1
 *                               with the reason logged
 *   ico_window_pump()           drains SDL events once per vsync: window
 *                               resize reaches rd_ResizeOutput; Escape or
 *                               the close button returns 0 (quit), else 1;
 *                               a lost device (rhi_DeviceLost) shows one
 *                               message box and returns 0
 *   ico_window_pace(hz)         sleeps until this vsync's deadline at hz
 *                               (50 PAL, 60 NTSC) in real time; a host that
 *                               falls more than 100 ms behind resynchronises
 *                               instead of running fast to catch up
 *   ico_window_close()          rd_Shutdown, the window, SDL (atexit-safe)
 */
#ifndef ICO_PLATFORM_WINDOW_HOST_H
#define ICO_PLATFORM_WINDOW_HOST_H

int ico_window_open(unsigned int gsW, unsigned int gsH);
int ico_window_pump(void);
void ico_window_pace(int hz);
void ico_window_close(void);

#endif /* ICO_PLATFORM_WINDOW_HOST_H */
