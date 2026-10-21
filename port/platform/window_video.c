/*
 * port/platform/window_video.c
 *
 * The fullscreen request and its readback (window_video.h).
 */
#include "window_video.h"
#include <stdio.h>
#include <string.h>

int ico_window_video_fullscreen(SDL_Window *w, int want, int *pxW, int *pxH)
{
    const char *what = want ? "on" : "off";
    int pw = 0, ph = 0;

    /* no mode set: SDL's borderless fullscreen at the desktop resolution
       (never an exclusive mode) */
    if (!SDL_SetWindowFullscreen(w, want != 0)) {
        fprintf(stderr, "window: fullscreen %s refused: %s\n", what, SDL_GetError());
    }
    /* the window manager answers later, and may say no: wait for it, then
       read what the window is rather than what was asked */
    SDL_SyncWindow(w);
    const int real = (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    fprintf(stderr, "window: fullscreen %s (window %dx%d pixels)%s\n", real ? "on" : "off", pw, ph,
            real != (want != 0) ? "; the window manager did not follow the request" : "");
    if (pxW != NULL) {
        *pxW = pw;
    }
    if (pxH != NULL) {
        *pxH = ph;
    }
    return real;
}

/* The window's mode.  The windowed rectangle is remembered
   when the window leaves the windowed state, and comes back with it. */
static int s_haveRect;
static int s_rectX, s_rectY, s_rectW, s_rectH;
static int s_maximizedByUs; /* Wayland's borderless is a maximised window */

#define DEFAULT_W 960
#define DEFAULT_H 720

static int s_askedBorderless; /* the last request was Borderless */

/* SDL's offscreen and dummy drivers have no window borders to take away,
   so SDL leaves the frameless flag alone there: the request stands in */
static int driver_has_borders(void)
{
    const char *d = SDL_GetCurrentVideoDriver();

    return d != NULL && strcmp(d, "offscreen") != 0 && strcmp(d, "dummy") != 0;
}

static int real_mode(SDL_Window *w)
{
    const SDL_WindowFlags f = SDL_GetWindowFlags(w);

    if (f & SDL_WINDOW_FULLSCREEN) {
        return ICO_WINDOWVIDEO_FULLSCREEN;
    }
    if (f & SDL_WINDOW_BORDERLESS) {
        return ICO_WINDOWVIDEO_BORDERLESS;
    }
    return (s_askedBorderless && !driver_has_borders()) ? ICO_WINDOWVIDEO_BORDERLESS
                                                        : ICO_WINDOWVIDEO_WINDOWED;
}

static void leave_fullscreen(SDL_Window *w)
{
    if (SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN) {
        if (!SDL_SetWindowFullscreen(w, false)) {
            fprintf(stderr, "window: leaving fullscreen refused: %s\n", SDL_GetError());
        }
        SDL_SyncWindow(w);
    }
}

static void remember_rect(SDL_Window *w)
{
    if (real_mode(w) != ICO_WINDOWVIDEO_WINDOWED || s_maximizedByUs) {
        return; /* not the windowed rectangle */
    }
    SDL_GetWindowPosition(w, &s_rectX, &s_rectY);
    SDL_GetWindowSize(w, &s_rectW, &s_rectH);
    s_haveRect = s_rectW > 0 && s_rectH > 0;
}

static int on_wayland(void)
{
    const char *d = SDL_GetCurrentVideoDriver();

    return d != NULL && strcmp(d, "wayland") == 0;
}

/* A sized Borderless window: the size asked for in pixels (0 x 0: the
   display's size, or not Borderless), and whether this file took the
   window's resizable edges away for it */
static int s_sizedW, s_sizedH;
static SDL_DisplayID s_sizedDisplay; /* the display it was centred on */
static int s_unresizableByUs;

/* The window's pixels per logical unit (SDL_video.c SDL_GetWindowPixelDensity:
   pixel width over window width; 1.0 on Windows and X11, 2.0 on a Retina
   Mac, fractional on a scaled Wayland output) */
static float window_density(SDL_Window *w)
{
    const float d = SDL_GetWindowPixelDensity(w);

    return d > 0.0f ? d : 1.0f;
}

int ico_window_video_display_pixels(SDL_Window *w, int *pxW, int *pxH)
{
    SDL_Rect b;

    if (!SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &b) || b.w <= 0 || b.h <= 0) {
        return 0;
    }
    const float d = window_density(w);
    *pxW = (int)((float)b.w * d + 0.5f);
    *pxH = (int)((float)b.h * d + 0.5f);
    return 1;
}

/* The logical size of a sized Borderless window on display bounds b: the
   pixel size asked for, clamped to the display, divided by the density
   (log: say so when it was clamped) */
static void sized_logical(SDL_Window *w, const SDL_Rect *b, int sizeW, int sizeH, int *lw, int *lh,
                          int log)
{
    const float d = window_density(w);
    const int dispW = (int)((float)b->w * d + 0.5f), dispH = (int)((float)b->h * d + 0.5f);
    int pw = sizeW, ph = sizeH;

    if (pw > dispW || ph > dispH) {
        pw = pw > dispW ? dispW : pw;
        ph = ph > dispH ? dispH : ph;
        if (log) {
            fprintf(stderr, "window: borderless %dx%d is larger than its display; %dx%d used\n",
                    sizeW, sizeH, pw, ph);
        }
    }
    *lw = (int)((float)pw / d + 0.5f);
    *lh = (int)((float)ph / d + 0.5f);
    *lw = *lw < 1 ? 1 : (*lw > b->w ? b->w : *lw);
    *lh = *lh < 1 ? 1 : (*lh > b->h ? b->h : *lh);
}

/* The window's resizable edges: off while it is a sized Borderless window
   (SDL's Windows backend gives a resizable frameless window
   WS_THICKFRAME and WS_MAXIMIZEBOX, SDL_windowswindow.c GetWindowStyle, so
   its invisible edges could be dragged), back on when it leaves that */
static void set_sized_edges(SDL_Window *w, int sized)
{
    if (sized && !s_unresizableByUs && (SDL_GetWindowFlags(w) & SDL_WINDOW_RESIZABLE)) {
        SDL_SetWindowResizable(w, false);
        s_unresizableByUs = 1;
    } else if (!sized && s_unresizableByUs) {
        SDL_SetWindowResizable(w, true);
        s_unresizableByUs = 0;
    }
}

/* The sized Borderless request: frameless, the size, the middle of the
   window's display */
static void borderless_sized(SDL_Window *w, int sizeW, int sizeH)
{
    const SDL_DisplayID d = SDL_GetDisplayForWindow(w);
    SDL_Rect b;
    int lw = 0, lh = 0;

    if (s_maximizedByUs) {
        SDL_RestoreWindow(w);
        s_maximizedByUs = 0;
    }
    if (!SDL_GetDisplayBounds(d, &b) || b.w <= 0 || b.h <= 0) {
        fprintf(stderr, "window: the display's bounds are unknown: %s\n", SDL_GetError());
        return;
    }
    sized_logical(w, &b, sizeW, sizeH, &lw, &lh, 1);
    s_sizedDisplay = d;
    set_sized_edges(w, 1);
    /* the size first: SDL centres the size pending, in the middle of the
       display's usable bounds when the window fits them, else of its
       bounds (SDL_video.c SDL_SetWindowPosition) */
    SDL_SetWindowSize(w, lw, lh);
    if (on_wayland()) {
        /* a client cannot place its window: the compositor does */
        fprintf(stderr, "window: borderless %dx%d on Wayland: the compositor places the window\n",
                sizeW, sizeH);
    } else if (!SDL_SetWindowPosition(w, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(d),
                                      (int)SDL_WINDOWPOS_CENTERED_DISPLAY(d)) &&
               driver_has_borders()) {
        fprintf(stderr, "window: borderless: centring the window refused: %s\n", SDL_GetError());
    }
}

int ico_window_video_mode(SDL_Window *w, int mode, int sizeW, int sizeH, int *pxW, int *pxH)
{
    int pw = 0, ph = 0;

    if (mode < ICO_WINDOWVIDEO_WINDOWED || mode > ICO_WINDOWVIDEO_FULLSCREEN) {
        mode = ICO_WINDOWVIDEO_WINDOWED;
    }
    if (sizeW <= 0 || sizeH <= 0 || mode != ICO_WINDOWVIDEO_BORDERLESS) {
        sizeW = sizeH = 0; /* the display's size (or not Borderless) */
    }
    s_askedBorderless = mode == ICO_WINDOWVIDEO_BORDERLESS;
    s_sizedW = sizeW;
    s_sizedH = sizeH;
    if (sizeW == 0) {
        set_sized_edges(w, 0);
    }
    if (mode == ICO_WINDOWVIDEO_FULLSCREEN) {
        remember_rect(w);
        if (s_maximizedByUs) {
            SDL_RestoreWindow(w);
            s_maximizedByUs = 0;
        }
        SDL_SetWindowBordered(w, true);
        /* no mode set: SDL's borderless fullscreen at the desktop resolution
           (never an exclusive mode) */
        if (!SDL_SetWindowFullscreen(w, true)) {
            fprintf(stderr, "window: fullscreen refused: %s\n", SDL_GetError());
        }
    } else if (mode == ICO_WINDOWVIDEO_BORDERLESS && sizeW > 0) {
        remember_rect(w); /* before leaving fullscreen: that is not the windowed rectangle */
        leave_fullscreen(w);
        SDL_SetWindowBordered(w, false);
        borderless_sized(w, sizeW, sizeH);
    } else if (mode == ICO_WINDOWVIDEO_BORDERLESS) {
        remember_rect(w); /* before leaving fullscreen: that is not the windowed rectangle */
        leave_fullscreen(w);
        SDL_SetWindowBordered(w, false);
        if (on_wayland()) {
            /* a client cannot place its window there: maximised instead */
            fprintf(stderr, "window: borderless on Wayland: maximised without a frame\n");
            SDL_MaximizeWindow(w);
            s_maximizedByUs = 1;
        } else {
            SDL_Rect b;

            if (s_maximizedByUs) {
                SDL_RestoreWindow(w);
                s_maximizedByUs = 0;
            }
            if (SDL_GetDisplayBounds(SDL_GetDisplayForWindow(w), &b) && b.w > 0 && b.h > 0) {
                SDL_SetWindowPosition(w, b.x, b.y);
                SDL_SetWindowSize(w, b.w, b.h);
            } else {
                fprintf(stderr, "window: the display's bounds are unknown: %s\n", SDL_GetError());
            }
        }
    } else {
        leave_fullscreen(w);
        if (s_maximizedByUs) {
            SDL_RestoreWindow(w);
            s_maximizedByUs = 0;
        }
        SDL_SetWindowBordered(w, true);
        if (s_haveRect) {
            SDL_SetWindowSize(w, s_rectW, s_rectH);
            SDL_SetWindowPosition(w, s_rectX, s_rectY);
        } else {
            SDL_SetWindowSize(w, DEFAULT_W, DEFAULT_H);
            SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    /* the window manager answers later, and may say no: wait, then read what
       the window is rather than what was asked */
    SDL_SyncWindow(w);
    const int real = real_mode(w);
    SDL_GetWindowSizeInPixels(w, &pw, &ph);
    {
        char what[96] = "";
        int dw = 0, dh = 0;

        if (real == ICO_WINDOWVIDEO_BORDERLESS && ico_window_video_display_pixels(w, &dw, &dh)) {
            if (sizeW > 0 && (pw != dw || ph != dh)) {
                snprintf(what, sizeof(what), "; display %dx%d", dw, dh);
            } else {
                snprintf(what, sizeof(what), ", the display's size");
            }
        }
        fprintf(stderr, "window: mode %s (window %dx%d pixels%s)%s\n",
                real == ICO_WINDOWVIDEO_FULLSCREEN   ? "fullscreen"
                : real == ICO_WINDOWVIDEO_BORDERLESS ? "borderless"
                                                     : "windowed",
                pw, ph, what,
                real != mode ? "; the window manager did not follow the request" : "");
    }
    if (pxW != NULL) {
        *pxW = pw;
    }
    if (pxH != NULL) {
        *pxH = ph;
    }
    return real;
}

int ico_window_video_refit(SDL_Window *w)
{
    SDL_Rect b;
    int x = 0, y = 0, pw = 0, ph = 0;

    if (real_mode(w) != ICO_WINDOWVIDEO_BORDERLESS || on_wayland()) {
        return 0; /* Wayland's maximised window follows its display itself, and
                     the compositor places a sized one */
    }
    const SDL_DisplayID d = SDL_GetDisplayForWindow(w);
    if (!SDL_GetDisplayBounds(d, &b) || b.w <= 0 || b.h <= 0) {
        return 0;
    }
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &pw, &ph);
    if (s_sizedW > 0) {
        /* a sized window on another display (or its display's size
           changed): its size clamped to that display, in the middle of it.
           The display, not the position, is compared: a driver that cannot
           place a window would be asked again at every event */
        int lw = 0, lh = 0;

        sized_logical(w, &b, s_sizedW, s_sizedH, &lw, &lh, 0);
        if (d == s_sizedDisplay && pw == lw && ph == lh) {
            return 0;
        }
        fprintf(stderr,
                "window: borderless %dx%d centred on its display again (%dx%d of %dx%d at %d,%d)\n",
                s_sizedW, s_sizedH, lw, lh, b.w, b.h, b.x, b.y);
        s_sizedDisplay = d;
        SDL_SetWindowSize(w, lw, lh);
        SDL_SetWindowPosition(w, (int)SDL_WINDOWPOS_CENTERED_DISPLAY(d),
                              (int)SDL_WINDOWPOS_CENTERED_DISPLAY(d));
        return 1;
    }
    if (x == b.x && y == b.y && pw == b.w && ph == b.h) {
        return 0;
    }
    fprintf(stderr, "window: borderless refitted to its display (%dx%d at %d,%d)\n", b.w, b.h, b.x,
            b.y);
    SDL_SetWindowPosition(w, b.x, b.y);
    SDL_SetWindowSize(w, b.w, b.h);
    return 1;
}
