/*
 * port/platform/window_host.h
 *
 * The windowed ico_pc: one SDL3 window titled "ICO", the renderer device
 * on it ([video] backend: Vulkan, or Direct3D 12 on Windows) through
 * rd_init with the display options, and the real-time pacing of the
 * simulated vsyncs, presenting between ticks when the frame rate option
 * allows.
 * main_host.c drives it; the headless build (ICO_HEADLESS) leaves it out.
 *
 *   ico_window_open(gsW, gsH)   SDL video, the window, rd_init; 0, or -1
 *                               with the reason logged
 *   ico_window_pump()           drains SDL events once per vsync: window
 *                               resize reaches rd_resize_output; Escape or
 *                               the close button returns 0 (quit), else 1;
 *                               a lost device (rhi_device_lost) shows one
 *                               message box and returns 0
 *   ico_window_pace(hz)         presents until this vsync's deadline at hz
 *                               (50 PAL, 60 NTSC) in real time, as often as
 *                               the frame rate option and vsync allow, then
 *                               sleeps to it; a host that falls more than
 *                               100 ms behind resynchronises instead of
 *                               running fast to catch up
 *   ico_window_close()          rd_shutdown, the window, SDL (atexit-safe)
 *   ico_window_progress(title, phase, pct)
 *                               Android's first start and its "Starting the
 *                               game" screen (main_host.c), once the window
 *                               is open; Android's own start-up screen
 *                               while the graphics are prepared draws the
 *                               same way before that, taking only the quit
 *                               and close requests and the keys from SDL's
 *                               queue and leaving the rest for the first
 *                               ico_window_pump.  Drains SDL events (the quit
 *                               event, Back or Escape ask to stop; a size
 *                               change reaches rd_resize_output; the rest
 *                               go to the pad layer as ico_window_pump
 *                               passes them) and presents one frame with
 *                               no scene (rd_present_blank): title, then
 *                               "phase: pct%" and a bar, or the phase alone
 *                               when pct < 0, through an overlay of its own
 *                               that replaces the registered one for that
 *                               present.  1 when the player asked to stop,
 *                               a quit or close came in on an earlier
 *                               start-up screen, or the device was lost;
 *                               else 0
 */
#ifndef ICO_PLATFORM_WINDOW_HOST_H
#define ICO_PLATFORM_WINDOW_HOST_H

int ico_window_open(unsigned int gsW, unsigned int gsH);
int ico_window_pump(void);
void ico_window_pace(int hz);
void ico_window_close(void);
int ico_window_progress(const char *title, const char *phase, int pct);

#endif /* ICO_PLATFORM_WINDOW_HOST_H */
