/*
 * port/platform/memory_floor.h
 *
 * The start-up set's free-memory floor (issue 161, follow-up): on Android
 * the set stops, and the next start makes no set, once the device's
 * available memory (/proc/meminfo MemAvailable, the "device N MB available
 * of T MB" words) is under the floor.  The default floor is 17 % of
 * MemTotal, and at most 1536 MB on a device of more than 8 GB (8192 MB): a
 * 4 GB phone's floor is 696 MB, a 3 GB phone's 522 MB, an 8 GB one's
 * 1392 MB, a 12 GB or 16 GB one's 1536 MB.  Android's MemAvailable is
 * cautious on a phone with much memory: a PowerVR DXT-48 phone of 15552 MB
 * showed 2554 to 2647 MB available while it ran the game well, under the
 * plain 17 % (2643 MB), which stopped its set after 33 of 1193 pipelines
 * and left the rest to be made in play (a 13.6 s stop on the title); with
 * the cap its floor is 1536 MB, under those readings.  On the Retroid
 * Pocket 6 (11265 MB) the floor is 1536 MB too, so its issue 161 reading
 * (1693 MB) no longer stops the set by itself: the system's low-memory
 * signal still does (it came at 1900 MB on that console), as do the
 * careful policy's budget and the creation limit.  [dev]
 * pipeline_memory_floor_mb sets it in MB, 0 turns it off.  Asked by
 * diag_host.c ico_diag_low_memory_stop, which the set asks before each
 * pipeline (the careful set) or after each batch.
 *
 * Pure C: the unit test is port/platform/test/memory_floor_test.c.
 */
#ifndef ICO_MEMORY_FLOOR_H
#define ICO_MEMORY_FLOOR_H

/* the default floor's parts */
#define ICO_MEMORY_FLOOR_PERCENT 17L
#define ICO_MEMORY_FLOOR_MAX_MB 1536L
/* the cap applies above this MemTotal (17 % of it is 1392 MB, under the
   cap, so below it the floor is the 17 % alone either way) */
#define ICO_MEMORY_FLOOR_MAX_ABOVE_MB 8192L

/* The floor in MB for a device of total_mb: setting_mb < 0 (unset) the
   default, 0 off (0), above 0 that value */
long ico_memory_floor_mb(long total_mb, long setting_mb);
/* Whether avail_mb of total_mb is under the floor (never when the floor is
   off or either figure is unknown, below 0) */
int ico_memory_floor_stops(long avail_mb, long total_mb, long setting_mb);
/* The words for where the floor came from, for the log lines: "17 % of
   total, at most 1536 MB above 8 GB" (setting_mb < 0), "[dev]
   pipeline_memory_floor_mb" (above 0), "off" (0) */
const char *ico_memory_floor_rule(long setting_mb);
/* [dev] pipeline_memory_floor_mb's value: "" or NULL (unset) or "auto" -1,
   a whole number 0 to 1048576 that number, anything else -2 (not read) */
long ico_memory_floor_setting(const char *text);

#endif /* ICO_MEMORY_FLOOR_H */
