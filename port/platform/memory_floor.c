/*
 * port/platform/memory_floor.c
 *
 * The start-up set's free-memory floor (memory_floor.h).
 */
#include "memory_floor.h"

#include <stdlib.h>
#include <string.h>

long ico_memory_floor_mb(long total_mb, long setting_mb)
{
    long pct;
    if (setting_mb == 0) {
        return 0;
    }
    if (setting_mb > 0) {
        return setting_mb;
    }
    pct = total_mb > 0 ? total_mb * ICO_MEMORY_FLOOR_PERCENT / 100L : 0;
    if (total_mb > ICO_MEMORY_FLOOR_MIN_ABOVE_MB && pct < ICO_MEMORY_FLOOR_MIN_MB) {
        return ICO_MEMORY_FLOOR_MIN_MB;
    }
    return pct;
}

int ico_memory_floor_stops(long avail_mb, long total_mb, long setting_mb)
{
    const long floor = ico_memory_floor_mb(total_mb, setting_mb);
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
