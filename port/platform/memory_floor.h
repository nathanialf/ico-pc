/*
 * port/platform/memory_floor.h
 *
 * The start-up set's free-memory floor (issue 161, follow-up): on Android
 * the set stops once the device's available memory (/proc/meminfo
 * MemAvailable, the "device N MB available of T MB" words) is under the
 * floor.  The floor depends on the driver class:
 *
 *   Qualcomm's own   17 % of MemTotal, at most 2048 MB on a device of more
 *   driver           than 8 GB (8192 MB): 696 MB on a 4 GB phone, 1392 MB
 *                    on an 8 GB one, 1915 MB on the Retroid Pocket 6's
 *                    11265 MB, 2048 MB on 12 GB and 16 GB.  This is the
 *                    rule tuned for issue 161: that console's Adreno 740 on
 *                    Qualcomm's own driver stalled in the driver's
 *                    pipeline compiler at 1693 MB available with no
 *                    low-memory signal and was ended 18-20 s later, so the
 *                    floor there guards against the compiler itself.  A
 *                    driver not known yet (vendor 0) takes this rule too.
 *   any other        the set's own need with a margin: 17 % of MemTotal,
 *   driver           at most 512 MB on a device of more than 8 GB.  On the
 *                    PowerVR DXT-48 phone (15552 MB) the process held 183
 *                    MB at the set's start and 251 MB at its end (the set
 *                    itself about 70 MB resident; the cache file 39 MB),
 *                    415 MB at most in play with the set made, and 694 MB
 *                    at most while the set compiled its VU batches on 7
 *                    threads (the compilers' working memory, about 70 MB a
 *                    thread, given back after the batch); its
 *                    MemAvailable showed 1823 to 2647 MB while it ran
 *                    well, which the 2048 MB cap stopped at 1823 MB.  512 MB
 *                    covers the set's resident growth about 7 times and the
 *                    background finisher's 4 compiling threads (about 280
 *                    MB) close to 2 times; the system's low-memory signal
 *                    stays the real guard on every driver.
 *
 * [dev] pipeline_memory_floor_mb sets it in MB, 0 turns it off.  Asked by
 * diag_host.c ico_diag_low_memory_stop, which the set asks before each
 * pipeline (the careful set) or after each batch, and by
 * ico_diag_background_memory before each batch of the finisher
 * (port/render/rd.h rd_pipelines_background_start).
 *
 * Pure C: the unit test is port/platform/test/memory_floor_test.c.
 */
#ifndef ICO_MEMORY_FLOOR_H
#define ICO_MEMORY_FLOOR_H

#include <stddef.h>

/* the default floor's parts */
#define ICO_MEMORY_FLOOR_PERCENT 17L
/* the cap on Qualcomm's own driver (and a driver not known) */
#define ICO_MEMORY_FLOOR_MAX_MB 2048L
/* the cap on every other driver: the set's need with a margin */
#define ICO_MEMORY_FLOOR_SET_MAX_MB 512L
/* the caps apply above this MemTotal (17 % of it is 1392 MB) */
#define ICO_MEMORY_FLOOR_MAX_ABOVE_MB 8192L

/* The driver class the floor is chosen by */
typedef enum IcoMemoryFloorDriver {
    ICO_MEMORY_FLOOR_DRIVER_UNKNOWN = 0, /* no device yet: the cautious rule */
    ICO_MEMORY_FLOOR_DRIVER_QUALCOMM,    /* Qualcomm's own Adreno driver */
    ICO_MEMORY_FLOOR_DRIVER_OTHER        /* any other (Turnip, PowerVR, Mali, ...) */
} IcoMemoryFloorDriver;

/* The class of a device: vendor 0x5143 (Qualcomm) whose driver text names
   neither "Mesa" nor "turnip" (any case; Mesa's Turnip reports Qualcomm's
   vendor id and names itself there) Qualcomm's own; vendor 0 unknown; any
   other vendor other */
int ico_memory_floor_driver(unsigned vendor_id, const char *driver_text);
/* The class's words for the log lines: "Qualcomm's own driver", "another
   driver than Qualcomm's own", "the driver not known yet" */
const char *ico_memory_floor_driver_words(int driver);

/* The floor in MB for a device of total_mb: setting_mb < 0 (unset) the
   default of the driver class, 0 off (0), above 0 that value */
long ico_memory_floor_mb(long total_mb, long setting_mb, int driver);
/* Whether avail_mb of total_mb is under the floor (never when the floor is
   off or either figure is unknown, below 0) */
int ico_memory_floor_stops(long avail_mb, long total_mb, long setting_mb, int driver);
/* The words for where the floor came from, for the log lines (setting_mb
   < 0: the class's rule; "[dev] pipeline_memory_floor_mb" above 0; "off"
   for 0) */
const char *ico_memory_floor_rule(long setting_mb, int driver);
/* [dev] pipeline_memory_floor_mb's value: "" or NULL (unset) or "auto" -1,
   a whole number 0 to 1048576 that number, anything else -2 (not read) */
long ico_memory_floor_setting(const char *text);

/* Issue 123: a start whose set stopped at the floor on Qualcomm's own
 * driver makes no set at the next start (diag_host.c: the file's "set:
 * stopped at the system's low-memory signal (<why>)" line, whose <why> for
 * a floor stop is "N MB available of T MB, floor F MB").  The compiler's
 * memory, not the set, is what ran the device low: with this shader set's
 * pipeline cache file on disk and not empty the driver answers the set's
 * creations from the cache and does not run the compiler, so that next
 * start tries the set again (stopping at the floor again if it comes). */
/* Whether an activity-file line of that kind names a floor stop (not the
   system's own signal): the "MB available of" figures after the line's
   words */
int ico_memory_floor_line_is_floor(const char *line);
/* The retry's decision: 1 only when the previous start stopped at the
   floor (prev_floor_stop), the driver is Qualcomm's own, and the cache file
   holds cache_bytes > 0 (a file that is not there or empty: a negative or
   0 figure).  why: the decision's words for the log, either way (may be
   NULL). */
int ico_memory_floor_retry(int prev_floor_stop, int driver, long long cache_bytes, char *why,
                           size_t size);

#endif /* ICO_MEMORY_FLOOR_H */
