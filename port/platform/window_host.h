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
 *   ico_window_progress(title, phase, pct)
 *                               package AN-C, the Android first start
 *                               (main_host.c): drains SDL events (the quit
 *                               event, Back or Escape ask to stop; a size
 *                               change reaches rd_ResizeOutput; the rest
 *                               go to the pad layer as ico_window_pump
 *                               passes them) and presents one frame with
 *                               no scene (rd_PresentBlank): title, then
 *                               "phase: pct%" and a bar, or the phase alone
 *                               when pct < 0, through an overlay of its own
 *                               that replaces the registered one for that
 *                               present.  1 when the player asked to stop
 *                               (or the device was lost), else 0
 */
#ifndef ICO_PLATFORM_WINDOW_HOST_H
#define ICO_PLATFORM_WINDOW_HOST_H

int ico_window_open(unsigned int gsW, unsigned int gsH);
int ico_window_pump(void);
void ico_window_pace(int hz);
void ico_window_close(void);
int ico_window_progress(const char *title, const char *phase, int pct);

#endif /* ICO_PLATFORM_WINDOW_HOST_H */
