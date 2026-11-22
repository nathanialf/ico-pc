/*
 * port/platform/power_read.h
 *
 * The battery's state for the log (issue 179: whether the phone's power
 * limit or the vendor's frame governor slows the GPU, which this Mali's
 * clock and load nodes cannot say: they are not readable there).  Read
 * once a second on Android's log thread (window_stall.c asks_tick), read
 * only: the kernel's /sys/class/power_supply/<the battery>/ files first
 * (current_now in uA, voltage_now in uV, capacity in %, status), and
 * where they cannot be read the system's BatteryManager through the app's
 * Java side (host_android.c ico_android_battery).  Whether it could be
 * read is said once:
 *   battery: read once a second on the log thread from
 *     /sys/class/power_supply/battery (current, voltage, level, status)
 *   battery: /sys/class/power_supply not readable (errno 13): read once a
 *     second on the log thread from Android's BatteryManager
 *   battery: not readable (/sys/class/power_supply not readable (errno
 *     13); BatteryManager: no answer from the app's Java side): no
 *     battery lines
 * and each 10 s block with a reading:
 *   battery each second: current -812 -790 -805 mA, voltage 3850-3862 mV,
 *     level 67 %, discharging (sysfs; the current's sign as the device
 *     gives it)
 * Each stall's row (stall_coincide.h) has the last second's current.
 *
 * Pure: the parsers and the texts (port/platform/test/power_read_test.c,
 * from strings).
 */
#ifndef ICO_PLATFORM_POWER_READ_H
#define ICO_PLATFORM_POWER_READ_H

#include <stdbool.h>
#include <stddef.h>

enum {
    POWER_STATUS_UNKNOWN = 0,
    POWER_STATUS_CHARGING,
    POWER_STATUS_DISCHARGING,
    POWER_STATUS_NOT_CHARGING,
    POWER_STATUS_FULL
};

/* where the readings come from */
enum { POWER_SOURCE_NONE = 0, POWER_SOURCE_SYSFS, POWER_SOURCE_ANDROID };

typedef struct PowerSample {
    bool haveCurrent, haveVoltage, haveLevel;
    double currentMa, voltageMv;
    int level;  /* % */
    int status; /* POWER_STATUS_* */
} PowerSample;

/* a sysfs file's one number ("-812000\n"): false when there is none */
bool power_read_long(const char *text, size_t len, long long *out);
/* a status file's word ("Charging\n"): POWER_STATUS_* */
int power_read_status(const char *text, size_t len);
/* a type file names a battery ("Battery\n") */
bool power_read_is_battery(const char *text, size_t len);
/* BatteryManager's BATTERY_STATUS_* (1 unknown, 2 charging, 3
   discharging, 4 not charging, 5 full) as POWER_STATUS_* */
int power_status_from_android(int status);
const char *power_status_name(int status);
/* the sample from the sysfs files' texts (NULL: that file not read):
   current_now uA, voltage_now uV, capacity %, status.  true when any
   value is there */
bool power_from_sysfs(const char *current, const char *voltage, const char *capacity,
                      const char *status, PowerSample *out);
/* the sample from the Java side's four ints: the current in uA
   (INT_MIN: none, BatteryManager's own word for it), the voltage in mV
   (< 0 none), the level in % (< 0 none), BATTERY_STATUS_* (< 0 none).
   true when any value is there */
bool power_from_android(const int v[4], PowerSample *out);

/* Where to read from (the first tick's choice, POWER_SOURCE_*): the
   battery's sysfs files when they give a current, else BatteryManager
   when it gives one, else whichever gives anything (sysfs first): a phone
   whose sysfs gives the level but denies current_now still gets a
   current from BatteryManager */
int power_pick(bool sysfsAny, bool sysfsCurrent, bool androidAny, bool androidCurrent);
/* A stall row takes the last reading's current only when it is at most
   POWER_FRESH_MS old at nowMs (one tick missed): a reading that stopped
   is not carried on */
#define POWER_FRESH_MS 2500.0
bool power_fresh(double atMs, double nowMs);

/* The block's readings (the first POWER_BLOCK_MAX currents kept) */
#define POWER_BLOCK_MAX 12

typedef struct PowerBlock {
    unsigned n;        /* samples */
    unsigned currents; /* of them with a current (the first POWER_BLOCK_MAX kept) */
    double currentMa[POWER_BLOCK_MAX];
    double vMinMv, vMaxMv; /* vMinMv < 0: no voltage */
    int level, status;     /* the last sample's (level < 0 none) */
} PowerBlock;

void power_block_reset(PowerBlock *b);
void power_block_add(PowerBlock *b, const PowerSample *s);
/* "battery each second: ..." (after "window: "); 0 with no sample */
size_t power_block_text(const PowerBlock *b, int source, char *out, size_t cap);
/* the once line (after "window: "): source POWER_SOURCE_*, path the
   battery's sysfs folder (sysfs), sysErr the sysfs errno (0 none: no
   battery folder found), androidWhy why BatteryManager gave nothing
   (NONE) */
size_t power_source_text(int source, const char *path, int sysErr, const char *androidWhy,
                         char *out, size_t cap);

#endif
