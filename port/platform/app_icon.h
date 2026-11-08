/*
 * port/platform/app_icon.h
 *
 * The application icon: the title screen's "I", cut out of two captured
 * frames of the title (the clear colour black, then white) and written
 * under <user folder>/icon/ for the window and the shortcuts to use.
 *
 *   ico-pc-<disc id>.png   256 x 256 RGBA
 *   ico-pc-<disc id>.ico   16, 32, 48 as BMP entries and 256 as a PNG entry
 *   ico-pc-<disc id>.txt   the stamp: ICO_VERSION_LABEL ("dev" when empty)
 *   ico-pc.png, ico-pc.ico current copies of the disc's pair
 *
 * Every file is written as <name>.tmp and moved over the old one
 * (ico_rename_replace), the stamp last, so a cut-off write never leaves a
 * stamp that vouches for half a set.  The image maths is
 * app_icon_image.h, the .ico writer is ico_icon_write below.
 *
 * No SDL, heap only: the game thread runs on 256 KB fiber stacks.
 *
 * Thread use: ico_app_icon_deliver runs on the game thread while the
 * renderer still shows the title's held picture (rd.h rd_icon_hold; about
 * 30 to 130 ms at 3840 x 2160: the pair test and the matte over every
 * pixel); ico_app_icon_changed_take is read
 * by the window host's pump.  The *_at functions take the folder and the
 * names explicitly (the tests use a fake folder under the build folder);
 * ico_app_icon_wanted / _deliver read the user folder, the current disc and
 * the package label (app_icon_host.c, the window build).
 */
#ifndef ICO_PLATFORM_APP_ICON_H
#define ICO_PLATFORM_APP_ICON_H

#include <stddef.h>
#include <stdint.h>

#include "app_icon_image.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ico_app_icon_deliver answers */
#define ICO_APP_ICON_OK 0
#define ICO_APP_ICON_GHOST 1 /* the pair moved between the frames: nothing written */
#define ICO_APP_ICON_FAILED (-1)

/* The ICO file: ICONDIR (0, 1, 4) and four 16-byte entries, 16, 32 and 48
 * as 32-bit BGRA bottom-up BMP images (BITMAPINFOHEADER with biHeight =
 * 2 * h, a zero 1-bit AND mask padded to 4-byte rows) and 256 as the PNG
 * png256[0..n) (width and height bytes 0).  The 48/32/16 images are box
 * downscales of rgba256 (256 x 256, straight alpha).  Written as
 * <path>.tmp and moved over path.  0, or -1. */
int ico_icon_write(const char *path, const uint8_t *png256, size_t n, const uint8_t *rgba256);

/* data to path as <path>.tmp moved over it (the .tmp is removed on a
 * failure).  0, or -1. */
int ico_app_icon_write_atomic(const char *path, const uint8_t *data, size_t n);

/* The wanted test for a folder: 1 when <dir>/icon/ico-pc-<discId>.png is
 * missing, or the stamp differs from label.  An empty label (a development
 * build) wants only a missing PNG.  A label is compared as "dev" when it is
 * "" in the stamp that is written. */
int ico_app_icon_wanted_at(const char *dir, const char *discId, const char *label);

/* 1 when the presented picture is plain enough to cut the icon from: the
 * CRT filter is off (crtOn 0, or its strength 0) and the four colour
 * adjustments (Picture page: gamma, brightness, contrast, saturation, 1..100,
 * 50 the game's own, 0 read as 50) are all neutral.  The capture's white
 * background must come back as 255; the filter or the tone would move it and
 * the matte with it.  Upscaling must be off too (upscaleOn 0): FSR 1.0's
 * edge-adaptive scale and sharpening are not linear in the background
 * colour (fringes at the letter's edge), and FSR 3.1 carries its history
 * and its jitter from the shot over black to the one over white.  Screen
 * softening is fine (a blur of a flat field is flat).  ico_app_icon_wanted
 * adds this to its answer. */
int ico_app_icon_picture_plain(int crtOn, float crtStrength, int gamma, int brightness,
                               int contrast, int saturation, int upscaleOn);

/* Writes the set for a disc into <dir>/icon/: the PNG (png[0..pngN), the
 * 256 x 256 file as encoded), the .ico, the current copies, then the
 * stamp.  rgba256 is the same image decoded.  One log line per call.
 * 0, or -1. */
int ico_app_icon_store_at(const char *dir, const char *discId, const char *label,
                          const uint8_t *png, size_t pngN, const uint8_t *rgba256);

/* <dir>/icon/<name> into out; 0, or -1 when it does not fit. */
int ico_app_icon_path_at(char *out, size_t size, const char *dir, const char *name);

/* The window build's wrappers (app_icon_host.c) ------------------------ */

/* 1 when the current disc's icon is to be made: the user folder
 * (ico_host_pref_dir), ico_region_cur()->disc_id and ICO_VERSION_LABEL
 * through ico_app_icon_wanted_at; 0 when it is not (made and current, or no
 * user folder or disc); ICO_APP_ICON_NOT_PLAIN when it is to be made but the
 * picture is not plain now (ico_app_icon_picture_plain: the CRT filter, a
 * colour adjustment or Upscaling on), so the capture asks again later. */
#define ICO_APP_ICON_NOT_PLAIN (-1)
int ico_app_icon_wanted(void);

/* a = the title frame over black, b = the same tick over white (RGBA8, w x
 * h, rows w * 4 bytes apart, alpha 255, as rd_capture_shot hands them).
 * Matte, crop, 256, PNG, files; sets the changed flag.  ICO_APP_ICON_GHOST
 * when the two frames differ (the caller retries once, then uses
 * _deliver_single), ICO_APP_ICON_FAILED on a file or memory error. */
int ico_app_icon_deliver(const uint8_t *a, const uint8_t *b, int w, int h);

/* The fallback: one frame over black, alpha = the brightest channel
 * (a luma key).  Logged as such. */
int ico_app_icon_deliver_single(const uint8_t *a, int w, int h);

/* 1 once per delivery (reads and clears the flag the window pump watches). */
int ico_app_icon_changed_take(void);

/* The current copy's path (<user folder>/icon/ico-pc.png or .ico) into out
 * when the file exists: 0, or -1. */
int ico_app_icon_current_path(char *out, size_t size, int ico);

#ifdef __cplusplus
}
#endif
#endif /* ICO_PLATFORM_APP_ICON_H */
