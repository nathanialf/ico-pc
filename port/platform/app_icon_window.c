/*
 * port/platform/app_icon_window.c
 *
 * The application icon on the window (app_icon_window.h): the current PNG
 * copy in the user folder is decoded with the texture packs' loader and
 * handed to SDL as the window's icon.  SDL_SetWindowIcon copies the pixels,
 * so the decoded image is freed here.  On X11 it sets _NET_WM_ICON, on
 * Windows the window's big and small icons; the Wayland build of SDL 3.4
 * has no xdg-toplevel-icon support, so a Wayland session shows the
 * applications-menu entry's icon or none.
 */
#include "app_icon_window.h"

#include <stdio.h>
#include <stdlib.h>

#include "app_icon.h"
#include "host_fs.h"
#include "../render/texpack.h"

#if defined(__ANDROID__) || defined(ICO_IOS)
int ico_app_icon_apply(SDL_Window *win)
{
    (void)win;
    return 0;
}
#else

/* the whole file into memory (the PNG is a few hundred KB at most) */
static uint8_t *read_all(const char *path, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    uint8_t *buf = NULL;
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        len = ftell(f);
    }
    if (len > 0 && len <= (16L << 20) && fseek(f, 0, SEEK_SET) == 0) {
        buf = malloc((size_t)len);
        if (buf != NULL && fread(buf, 1, (size_t)len, f) != (size_t)len) {
            free(buf);
            buf = NULL;
        }
    }
    fclose(f);
    *n = buf != NULL ? (size_t)len : 0;
    return buf;
}

int ico_app_icon_apply(SDL_Window *win)
{
    char path[1024];
    size_t n = 0;
    int set = 0;

    if (win == NULL || ico_app_icon_current_path(path, sizeof(path), 0) != 0) {
        return 0;
    }
    uint8_t *file = read_all(path, &n);
    if (file == NULL) {
        fprintf(stderr, "window: icon: %s could not be read\n", path);
        return 0;
    }
    TexpackImage img;
    if (texpack_load_png(file, n, path, &img) == 0) {
        if (img.w > 0 && img.h > 0 && img.lv[0].pitch == img.w * 4 &&
            img.lv[0].size == (size_t)img.w * 4 * img.h) { /* RGBA8, not a BC level */
            SDL_Surface *s = SDL_CreateSurfaceFrom((int)img.w, (int)img.h, SDL_PIXELFORMAT_RGBA32,
                                                   (void *)img.lv[0].data, (int)img.lv[0].pitch);
            if (s != NULL) {
                set = SDL_SetWindowIcon(win, s) ? 1 : 0;
                if (!set) {
                    fprintf(stderr, "window: icon: SDL_SetWindowIcon failed: %s\n", SDL_GetError());
                }
                SDL_DestroySurface(s);
            } else {
                fprintf(stderr, "window: icon: no surface: %s\n", SDL_GetError());
            }
        }
        texpack_free_image(&img);
    }
#if defined(__APPLE__)
    /* SDL's Cocoa windows have no icon of their own: the Dock shows the
       program's */
    if (ico_app_icon_mac_set(file, n)) {
        set = 1;
    }
#endif
    free(file);
    if (set) {
        fprintf(stderr, "window: icon: set from %s\n", path);
    }
    return set;
}
#endif
