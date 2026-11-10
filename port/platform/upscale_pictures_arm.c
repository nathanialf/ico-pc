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

/* the renderer's outcome in the window's words (the same values) */
static int outcome(void)
{
    switch (rd_upscale_capture_state()) {
    case RD_UPSCALE_CAPTURE_PENDING:
        return ICO_UPSCALE_OUTCOME_PENDING;
    case RD_UPSCALE_CAPTURE_WRITTEN:
        return ICO_UPSCALE_OUTCOME_WRITTEN;
    case RD_UPSCALE_CAPTURE_FAILED:
        return ICO_UPSCALE_OUTCOME_FAILED;
    case RD_UPSCALE_CAPTURE_NO_UPSCALER:
        return ICO_UPSCALE_OUTCOME_NO_UPSCALER;
    case RD_UPSCALE_CAPTURE_EXPIRED:
        return ICO_UPSCALE_OUTCOME_EXPIRED;
    default:
        return ICO_UPSCALE_OUTCOME_NONE;
    }
}

void ico_upscale_pictures_after_present(const char *present_png, const char *zip)
{
    (void)ico_upscale_pictures_after(present_png, zip, outcome());
}

void ico_upscale_pictures_pump(void)
{
    if (ico_upscale_pictures_waiting()) {
        (void)ico_upscale_pictures_step(outcome());
    }
}
