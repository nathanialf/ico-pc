/*
 * port/platform/memory_floor.c
 *
 * The start-up set's free-memory floor (memory_floor.h).
 */
#include "memory_floor.h"

#include <stdlib.h>
#include <string.h>

/* word anywhere in s, any case (the driver texts' own spelling varies:
   "turnip Mesa driver", "Mesa 25.1.0") */
static int has_word(const char *s, const char *word)
{
    const size_t n = strlen(word);

    for (; s != NULL && *s != '\0'; s++) {
        size_t i = 0;

        while (i < n && s[i] != '\0' && (s[i] | 0x20) == (word[i] | 0x20)) {
            i++;
        }
        if (i == n) {
            return 1;
        }
    }
    return 0;
}

int ico_memory_floor_driver(unsigned vendor_id, const char *driver_text)
{
    if (vendor_id == 0u) {
        return ICO_MEMORY_FLOOR_DRIVER_UNKNOWN;
    }
    if (vendor_id == 0x5143u && !has_word(driver_text, "mesa") &&
        !has_word(driver_text, "turnip")) {
        return ICO_MEMORY_FLOOR_DRIVER_QUALCOMM;
    }
    return ICO_MEMORY_FLOOR_DRIVER_OTHER;
}

const char *ico_memory_floor_driver_words(int driver)
{
    switch (driver) {
    case ICO_MEMORY_FLOOR_DRIVER_QUALCOMM:
        return "Qualcomm's own driver";
    case ICO_MEMORY_FLOOR_DRIVER_OTHER:
        return "another driver than Qualcomm's own";
    default:
        return "the driver not known yet";
    }
}

long ico_memory_floor_mb(long total_mb, long setting_mb, int driver)
{
    long pct, cap;
    if (setting_mb == 0) {
        return 0;
    }
    if (setting_mb > 0) {
        return setting_mb;
    }
    cap = driver == ICO_MEMORY_FLOOR_DRIVER_OTHER ? ICO_MEMORY_FLOOR_SET_MAX_MB
                                                  : ICO_MEMORY_FLOOR_MAX_MB;
    pct = total_mb > 0 ? total_mb * ICO_MEMORY_FLOOR_PERCENT / 100L : 0;
    if (total_mb > ICO_MEMORY_FLOOR_MAX_ABOVE_MB && pct > cap) {
        return cap;
    }
    return pct;
}

const char *ico_memory_floor_rule(long setting_mb, int driver)
{
    if (setting_mb == 0) {
        return "off";
    }
    if (setting_mb > 0) {
        return "[dev] pipeline_memory_floor_mb";
    }
    if (driver == ICO_MEMORY_FLOOR_DRIVER_OTHER) {
        return "the set's need with a margin: 17 % of total, at most 512 MB above 8 GB; "
               "Qualcomm's own driver keeps 17 % of total, at most 2048 MB above 8 GB";
    }
    if (driver == ICO_MEMORY_FLOOR_DRIVER_QUALCOMM) {
        return "Qualcomm's own driver: 17 % of total, at most 2048 MB above 8 GB; other "
               "drivers at most 512 MB";
    }
    return "the driver not known yet: 17 % of total, at most 2048 MB above 8 GB";
}

int ico_memory_floor_stops(long avail_mb, long total_mb, long setting_mb, int driver)
{
    const long floor = ico_memory_floor_mb(total_mb, setting_mb, driver);
    return floor > 0 && avail_mb >= 0 && total_mb > 0 && avail_mb < floor;
}

long ico_memory_floor_setting(const char *text)
{
    char *end = NULL;
    long v;
    if (text == NULL || text[0] == '\0' || strcmp(text, "auto") == 0) {
        return -1;
    }
    v = strtol(text, &end, 10);
    if (end == text || *end != '\0' || v < 0 || v > 1048576L) {
        return -2;
    }
    return v;
}
