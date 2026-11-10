/*
 * port/platform/app_icon_host.c
 *
 * The window build's side of the application icon (app_icon.h): the user
 * folder, the current disc and the package label feed the folder-level
 * functions of app_icon.c, the PNG comes from rd_write_png_mem, and a
 * delivery raises the flag the window host's pump reads.
 */
#include <stdio.h>
#include <stdlib.h>

#include "app_icon.h"
#include "host_config.h"
#include "ico_version.h"  /* ICO_VERSION_LABEL, generated ("" in a development build) */
#include "../render/rd.h" /* rd_upscale_ratio */
#include "../render/rd_png.h"
#include "../core/region.h"
#include "video_options.h"

static int s_changed;

static int userAndDisc(char *dir, size_t size, const char **disc)
{
    const IcoRegionInfo *ri = ico_region_cur();
    if (ri == NULL || ri->disc_id == NULL || ico_host_pref_dir(dir, size) != 0 || dir[0] == '\0') {
        return -1;
    }
    *disc = ri->disc_id;
    return 0;
}

int ico_app_icon_wanted(void)
{
    char dir[1024];
    const char *disc;
    if (userAndDisc(dir, sizeof(dir), &disc) != 0) {
        return 0;
    }
    if (!ico_app_icon_wanted_at(dir, disc, ICO_VERSION_LABEL)) {
        return 0;
    }
    /* the capture needs the plain picture: with the CRT filter, a colour
       adjustment or Upscaling on, the white background would not come back
       white and the matte would be wrong.  The capture asks again (the
       settings changed, a later title visit, or the next run).  Upscaling
       counts as on with FSR 1.0 chosen, or with a temporal upscaler in
       force (rd_upscale_ratio: FSR 3.1 where the device runs it, [dev]
       upscale_test); an FSR 3.1 the device cannot run is drawn as Off. */
    IcoVideoOptions v;
    ico_video_get(&v);
    const int upscaleOn = v.upscale == ICO_UPSCALE_FSR1 || rd_upscale_ratio() > 0.0f;
    if (!ico_app_icon_picture_plain(v.crt, v.crtStrength, v.toneGamma, v.toneBrightness,
                                    v.toneContrast, v.toneSaturation, upscaleOn)) {
        static int s_logged;
        if (!s_logged) {
            s_logged = 1;
            fprintf(stderr, "ico_pc: icon: waiting for a plain picture (CRT filter, Brightness "
                            "or Upscaling on)\n");
        }
        return ICO_APP_ICON_NOT_PLAIN;
    }
    return 1;
}

/* the 256 x 256 image into PNG, the files, the flag */
static int store(const uint8_t *rgba256)
{
    char dir[1024];
    const char *disc;
    uint8_t *png = NULL;
    size_t pngN = 0;
    if (userAndDisc(dir, sizeof(dir), &disc) != 0) {
        fprintf(stderr, "ico_pc: icon: refused: no user folder or disc\n");
        return ICO_APP_ICON_FAILED;
    }
    if (!rd_write_png_mem(rgba256, ICO_ICON_SIZE, ICO_ICON_SIZE, ICO_ICON_SIZE * 4, 1, &png,
                          &pngN)) {
        fprintf(stderr, "ico_pc: icon: refused (%s, %s): the PNG could not be encoded\n", disc,
                ICO_VERSION_LABEL[0] ? ICO_VERSION_LABEL : "dev");
        return ICO_APP_ICON_FAILED;
    }
    const int r = ico_app_icon_store_at(dir, disc, ICO_VERSION_LABEL, png, pngN, rgba256);
    free(png);
    if (r != 0) {
        return ICO_APP_ICON_FAILED;
    }
    __atomic_store_n(&s_changed, 1, __ATOMIC_RELEASE);
    return ICO_APP_ICON_OK;
}

static int finish(uint8_t *matte, uint8_t *out, int w, int h, const char *how)
{
    int r = ICO_APP_ICON_FAILED;
    if (ico_icon_make256(matte, w, h, out) != 0) {
        fprintf(stderr, "ico_pc: icon: refused: no letter in the %s (%d x %d)\n", how, w, h);
    } else if (ico_icon_flat(out)) {
        /* a letter has clear corners; a flat wash (the fog's cream at alpha 215
           on every pixel) would be drawn as a coloured square on
           the recents card and the shortcuts */
        const uint8_t *c = out;
        fprintf(stderr,
                "ico_pc: icon: refused: the %s is opaque in all four corners of the image "
                "(corner colour %d %d %d, alpha %d): not a letter on a clear background\n",
                how, c[0], c[1], c[2], c[3]);
    } else {
        r = store(out);
    }
    free(matte);
    free(out);
    return r;
}

int ico_app_icon_deliver(const uint8_t *a, const uint8_t *b, int w, int h)
{
    if (a == NULL || b == NULL || w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        fprintf(stderr, "ico_pc: icon: refused: bad frames\n");
        return ICO_APP_ICON_FAILED;
    }
    if (ico_icon_pair_ghost(a, b, w, h)) {
        fprintf(stderr, "ico_pc: icon: refused: the two frames differ (%d x %d)\n", w, h);
        return ICO_APP_ICON_GHOST;
    }
    uint8_t *matte = malloc((size_t)w * (size_t)h * 4);
    uint8_t *out = malloc((size_t)ICO_ICON_SIZE * ICO_ICON_SIZE * 4);
    if (matte == NULL || out == NULL || ico_icon_matte(a, b, w, h, matte) != 0) {
        free(matte);
        free(out);
        fprintf(stderr, "ico_pc: icon: refused: out of memory\n");
        return ICO_APP_ICON_FAILED;
    }
    return finish(matte, out, w, h, "matte");
}

int ico_app_icon_deliver_single(const uint8_t *a, int w, int h)
{
    if (a == NULL || w <= 0 || h <= 0 || w > 16384 || h > 16384) {
        fprintf(stderr, "ico_pc: icon: refused: bad frame\n");
        return ICO_APP_ICON_FAILED;
    }
    uint8_t *matte = malloc((size_t)w * (size_t)h * 4);
    uint8_t *out = malloc((size_t)ICO_ICON_SIZE * ICO_ICON_SIZE * 4);
    if (matte == NULL || out == NULL || ico_icon_luma_key(a, w, h, matte) != 0) {
        free(matte);
        free(out);
        fprintf(stderr, "ico_pc: icon: refused: out of memory\n");
        return ICO_APP_ICON_FAILED;
    }
    fprintf(stderr, "ico_pc: icon: using the single-frame luma key (%d x %d)\n", w, h);
    return finish(matte, out, w, h, "luma key");
}

int ico_app_icon_changed_take(void)
{
    return __atomic_exchange_n(&s_changed, 0, __ATOMIC_ACQ_REL);
}

int ico_app_icon_current_path(char *out, size_t size, int ico)
{
    char dir[1024];
    if (out == NULL || size == 0) {
        return -1;
    }
    out[0] = '\0';
    if (ico_host_pref_dir(dir, sizeof(dir)) != 0 || dir[0] == '\0') {
        return -1;
    }
    if (ico_app_icon_path_at(out, size, dir, ico ? "ico-pc.ico" : "ico-pc.png") != 0 ||
        !ico_file_exists(out)) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}
