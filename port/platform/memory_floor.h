/*
 * port/platform/memory_floor.h
 *
 * The start-up set's free-memory floor (issue 161, follow-up): on Android
 * the set stops, and the next start makes no set, once the device's
 * available memory (/proc/meminfo MemAvailable, the "device N MB available
 * of T MB" words) is under the floor.  The default floor is 17 % of
 * MemTotal, and at least 1536 MB on a device of more than 8 GB (8192 MB)
 * only: a 4 GB phone's floor is 696 MB, a 3 GB phone's 522 MB, the
 * Retroid Pocket 6's (11265 MB) 1915 MB (review finding 1: an absolute
 * 1536 MB stopped every start-up set on a 4 GB phone); [dev]
 * pipeline_memory_floor_mb sets it in MB, 0 turns it off.  Asked by diag_host.c ico_diag_low_memory_stop, which
 * the set asks before each pipeline (the careful set) or after each batch.
 *
 * Pure C: the unit test is port/platform/test/memory_floor_test.c.
 */
#ifndef ICO_MEMORY_FLOOR_H
#define ICO_MEMORY_FLOOR_H

/* the default floor's parts */
#define ICO_MEMORY_FLOOR_MIN_MB 1536L
#define ICO_MEMORY_FLOOR_PERCENT 17L
/* the minimum applies only above this MemTotal */
#define ICO_MEMORY_FLOOR_MIN_ABOVE_MB 8192L

/* The floor in MB for a device of total_mb: setting_mb < 0 (unset) the
   default, 0 off (0), above 0 that value */
long ico_memory_floor_mb(long total_mb, long setting_mb);
/* Whether avail_mb of total_mb is under the floor (never when the floor is
   off or either figure is unknown, below 0) */
int ico_memory_floor_stops(long avail_mb, long total_mb, long setting_mb);
/* [dev] pipeline_memory_floor_mb's value: "" or NULL (unset) or "auto" -1,
   a whole number 0 to 1048576 that number, anything else -2 (not read) */
long ico_memory_floor_setting(const char *text);

#endif /* ICO_MEMORY_FLOOR_H */
