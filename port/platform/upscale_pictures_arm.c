/* upscale_pictures_arm.c: F12's request for the upscaler's pictures
 * (upscale_pictures.h), the renderer's half; the rest of the module
 * (upscale_pictures.c) needs no renderer, for its test */
#include "upscale_pictures.h"

#include "rd.h"
#include "host_config.h" /* ICO_PATH_MAX */

void ico_upscale_pictures_arm(const char *present_png, int for_report)
{
    char stem[ICO_PATH_MAX];

    if (present_png == NULL || present_png[0] == '\0' ||
        ico_upscale_pictures_stem(present_png, stem, sizeof(stem)) != 0) {
        return;
    }
    (void)rd_capture_upscale(stem, for_report ? ICO_UPSCALE_PICTURE_MAX_W : 0u);
}
