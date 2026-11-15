/*
 * port/platform/memory_floor.c
 *
 * The start-up set's free-memory floor (memory_floor.h).
 */
#include "memory_floor.h"

#include "rhi_device_table.h" /* port/rhi: the driver class's rows */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The class is the device table's MEMORY_FLOOR_CLASS (port/rhi/
   rhi_device_table.c: the rows vendor-unknown and qualcomm-text, another
   driver the default) on an identity of the arguments; the rule this
   function held before, vendor 0 unknown, vendor 0x5143 with a driver id
   other than 18 (Turnip's) and neither "mesa" nor "turnip" in the driver
   text Qualcomm's own, is those rows' match, and
   port/rhi/test/rhi_device_table_equiv_test.c compares the two on every
   identity of its matrix */
_Static_assert((int)ICO_MEMORY_FLOOR_DRIVER_UNKNOWN == (int)RHI_DT_FLOOR_UNKNOWN &&
                   (int)ICO_MEMORY_FLOOR_DRIVER_QUALCOMM == (int)RHI_DT_FLOOR_QUALCOMM &&
                   (int)ICO_MEMORY_FLOOR_DRIVER_OTHER == (int)RHI_DT_FLOOR_OTHER,
               "the table's floor classes are IcoMemoryFloorDriver's values");

int ico_memory_floor_driver(unsigned vendor_id, unsigned driver_id, const char *driver_text)
{
    RhiDeviceId id;

    memset(&id, 0, sizeof id);
    id.vendorId = vendor_id;
    id.driverId = driver_id;
    id.driverText = driver_text;
    return (int)rhi_device_table_choice(&id, RHI_DC_MEMORY_FLOOR_CLASS, NULL);
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

int ico_memory_floor_line_is_floor(const char *line)
{
    return line != NULL && strstr(line, " MB available of ") != NULL &&
           strstr(line, ", floor ") != NULL;
}

int ico_memory_floor_line_retried(const char *line)
{
    return line != NULL && strstr(line, ")" ICO_MEMORY_FLOOR_RETRIED_MARK) != NULL;
}

int ico_memory_floor_retry(int prev_floor_stop, int prev_retried, int driver, long long cache_bytes,
                           int cache_loaded, char *why, size_t size)
{
    const char *words;
    int retry = 0;

    if (!prev_floor_stop) {
        words = "the previous start did not stop at the free-memory floor";
    } else if (prev_retried) {
        words = "the previous start was already a retry that stopped at the floor again (at most "
                "one retry in a row)";
    } else if (driver != ICO_MEMORY_FLOOR_DRIVER_QUALCOMM) {
        words = "not Qualcomm's own driver";
    } else if (cache_bytes <= 0) {
        words = "the pipeline cache file for this shader set is missing or empty (the driver's "
                "compiler would run)";
    } else if (!cache_loaded) {
        words = "the renderer did not load the pipeline cache file (the driver refused it: another "
                "driver or shader set; the driver's compiler would run)";
    } else {
        words = "the pipeline cache file for this shader set is on disk";
        retry = 1;
    }
    if (why != NULL && size > 0) {
        snprintf(why, size, "%s", words);
    }
    return retry;
}
