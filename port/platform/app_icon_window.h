/*
 * port/platform/app_icon_window.h
 *
 * The application icon on the window (app_icon.h makes the files): Windows
 * and X11 take the window's own icon, macOS the Dock icon of the running
 * program (app_icon_mac.m).  Android and iOS keep their packaged icons.
 */
#ifndef ICO_PLATFORM_APP_ICON_WINDOW_H
#define ICO_PLATFORM_APP_ICON_WINDOW_H

#include <SDL3/SDL.h>

/* Sets win's icon (and on macOS the Dock icon) from <user folder>/icon/ico-pc.png
 * when that file exists and decodes; nothing otherwise (SDL's own default
 * stays).  Main thread only (SDL_SetWindowIcon).  1 when an icon was set,
 * else 0; one log line per call that finds a file. */
int ico_app_icon_apply(SDL_Window *win);

#if defined(__APPLE__)
/* app_icon_mac.m: [NSApp setApplicationIconImage:] from the PNG file's
 * bytes.  1 when set. */
int ico_app_icon_mac_set(const unsigned char *png, unsigned long n);
#endif

#endif /* ICO_PLATFORM_APP_ICON_WINDOW_H */
