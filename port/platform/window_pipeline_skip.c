/*
 * port/platform/window_pipeline_skip.c
 *
 * [dev] pipeline_skip at the window's start (window_pipeline_skip.h,
 * issue 161).
 */
#include "window_pipeline_skip.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "diag_host.h"
#include "pipeline_skip.h"
#include "rd.h"
#include "rhi.h"
#ifdef __ANDROID__
#include "android/host_android.h"
#endif

/* Issue 161: [dev] pipeline_skip (port/platform/pipeline_skip.h), after
   rd_init made the device and before the start-up set.  The start that
   follows an end while the driver created a pipeline of a family the
   renderer can draw another way (the activity file's note names only that
   family) writes the record for this device and driver, with one line and
   one box; every start on the same device and driver then makes none of
   the family (rd_set_sprite_planes_off: its draws take the plain sprite
   builds).  A record of another device or driver is kept and not used. */
void window_pipeline_skip(SDL_Window *window, const char *cfgName)
{
    static bool s_boxShown;
    const RhiLimits *lim = rhi_limits();
    const char *device = rhi_adapter_name();
    const char *cfg = cfgName != NULL ? cfgName : "config.toml";
    char driver[64];
    PipelineSkip rec;

    pipeline_skip_driver_word(driver, sizeof(driver), lim != NULL ? lim->compilerVersion : NULL,
                              lim != NULL ? lim->driverText : NULL);
    if (ico_diag_startup_end_count(ICO_DIAG_END_PIPELINES) > 0) {
        const char *note = ico_diag_startup_activity_prev();
        const int fam = pipeline_skip_family_of_note(note);
        char value[256];

        memset(&rec, 0, sizeof(rec));
        rec.family = fam;
        snprintf(rec.device, sizeof(rec.device), "%s", device != NULL ? device : "");
        snprintf(rec.driver, sizeof(rec.driver), "%s", driver);
        {
            const time_t now = time(NULL);
            const struct tm *tm = localtime(&now);
            if (tm != NULL) {
                (void)strftime(rec.date, sizeof(rec.date), "%Y-%m-%d", tm);
            }
        }
        if (fam != PIPELINE_SKIP_NONE && pipeline_skip_format(value, sizeof(value), &rec) == 0) {
            const int saved =
                ico_config_set_string("dev.pipeline_skip", value) == 0 && ico_config_save() == 0;

            fprintf(stderr,
                    "window: pipelines: the previous start ended while the graphics driver "
                    "created %s (%s); [dev] pipeline_skip = \"%s\"%s%s: this start and every start "
                    "after it on this device and driver draw without them (pipeline_skip = \"\" "
                    "makes them again)\n",
                    pipeline_skip_family_text(fam), note, value,
                    saved ? " written to " : " could not be written to ", cfg);
            if (!s_boxShown) {
                char box[512];

                s_boxShown = true;
                snprintf(box, sizeof(box),
                         "The last start closed because the graphics driver stopped while "
                         "preparing %s.\n\nThe game draws them another way from now on, so "
                         "this should not happen again.",
                         pipeline_skip_family_plain(fam));
#ifdef __ANDROID__
                ico_android_message_box(box, 0);
#else
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "ICO PC", box, window);
#endif
            }
        } else if (note != NULL && note[0] != '\0') {
            fprintf(stderr,
                    "window: pipelines: the previous start's end names no one family the game can "
                    "draw another way (%s); [dev] pipeline_skip is not written\n",
                    note);
        }
    }
    {
        const char *value = ico_config_get_string("dev.pipeline_skip", "");
        char why[200] = "";

        if (value != NULL && value[0] != '\0' && pipeline_skip_parse(value, &rec) != 0) {
            fprintf(stderr,
                    "window: [dev] pipeline_skip = \"%s\" names no pipeline family this version "
                    "knows; ignored\n",
                    value);
        } else if (value != NULL && value[0] != '\0' &&
                   !pipeline_skip_matches(&rec, device, driver)) {
            fprintf(stderr,
                    "window: [dev] pipeline_skip names %s with driver %s; this device is %s with "
                    "driver %s: %s are made (the record is kept)\n",
                    rec.device[0] ? rec.device : "any device", rec.driver[0] ? rec.driver : "any",
                    device != NULL ? device : "unnamed", driver[0] ? driver : "unnamed",
                    pipeline_skip_family_text(rec.family));
        } else if (value != NULL && value[0] != '\0') {
            snprintf(why, sizeof(why),
                     "the driver hung creating them%s%s, compiler %s; [dev] pipeline_skip",
                     rec.date[0] ? " on " : "", rec.date, rec.driver[0] ? rec.driver : "any");
        }
        /* every open (a renderer switch opens another device): off only on
           the record's own device and driver */
        rd_set_sprite_planes_off(why[0] != '\0' ? why : NULL);
    }
}

/* the block's count (window_pipeline_skip_count) */
static uint64_t s_noPlanes;

void window_pipeline_skip_count(uint32_t commands)
{
    s_noPlanes += commands;
}

void window_pipeline_skip_line(void)
{
    if (rd_sprite_planes_off()) {
        fprintf(stderr,
                "window: sprite no-planes: %llu commands in these 10 s (the sprite planes builds "
                "off)\n",
                (unsigned long long)s_noPlanes);
    }
    s_noPlanes = 0;
}
