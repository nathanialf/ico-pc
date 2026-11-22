/*
 * port/platform/test/power_read_test.c
 *
 * The battery for the log (power_read.h, issue 179), from strings in the
 * test: the sysfs numbers (a sign, spaces, none, a number too long), the
 * status words and the type word, a sample from the sysfs files' texts
 * (the kernel's uA and uV as mA and mV, a level past 100 refused, every
 * file missing), a sample from the Java side's four ints (BatteryManager's
 * INT_MIN for no current, its status numbers), a block's currents (past 12
 * counted), voltages, level and status, the once and block lines word for
 * word.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "power_read.h"

static int fails;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

#define S(x) x, sizeof(x) - 1u

int main(void)
{
    long long v = 0;
    CHECK(power_read_long(S("-812000\n"), &v) && v == -812000);
    CHECK(power_read_long(S("  42"), &v) && v == 42);
    CHECK(power_read_long(S("+5\n"), &v) && v == 5);
    CHECK(!power_read_long(S(""), &v));
    CHECK(!power_read_long(S("abc\n"), &v));
    CHECK(!power_read_long(S("-\n"), &v));
    CHECK(!power_read_long(S("99999999999999999999999\n"), &v));

    CHECK(power_read_status(S("Charging\n")) == POWER_STATUS_CHARGING);
    CHECK(power_read_status(S("Discharging\n")) == POWER_STATUS_DISCHARGING);
    CHECK(power_read_status(S("Not charging\n")) == POWER_STATUS_NOT_CHARGING);
    CHECK(power_read_status(S("Full")) == POWER_STATUS_FULL);
    CHECK(power_read_status(S("Unknown\n")) == POWER_STATUS_UNKNOWN);
    CHECK(power_read_is_battery(S("Battery\n")));
    CHECK(!power_read_is_battery(S("USB\n")));
    CHECK(!power_read_is_battery(S("Mains\n")));

    PowerSample s;
    CHECK(power_from_sysfs("-812000\n", "3850000\n", "67\n", "Discharging\n", &s));
    CHECK(s.haveCurrent && s.currentMa == -812.0);
    CHECK(s.haveVoltage && s.voltageMv == 3850.0);
    CHECK(s.haveLevel && s.level == 67);
    CHECK(s.status == POWER_STATUS_DISCHARGING);
    CHECK(!power_from_sysfs(NULL, NULL, NULL, NULL, &s));
    CHECK(power_from_sysfs(NULL, NULL, "150\n", "Charging\n", &s) && !s.haveLevel &&
          s.level == -1 && s.status == POWER_STATUS_CHARGING);

    {
        const int none[4] = {INT_MIN, -1, -1, -1};
        CHECK(!power_from_android(none, &s));
        const int a[4] = {-450000, 3870, 55, 3};
        CHECK(power_from_android(a, &s) && s.haveCurrent && s.currentMa == -450.0 &&
              s.haveVoltage && s.voltageMv == 3870.0 && s.level == 55 &&
              s.status == POWER_STATUS_DISCHARGING);
        CHECK(power_status_from_android(2) == POWER_STATUS_CHARGING);
        CHECK(power_status_from_android(4) == POWER_STATUS_NOT_CHARGING);
        CHECK(power_status_from_android(5) == POWER_STATUS_FULL);
        CHECK(power_status_from_android(1) == POWER_STATUS_UNKNOWN);
    }

    char b[512];
    PowerBlock pb;
    power_block_reset(&pb);
    CHECK(power_block_text(&pb, POWER_SOURCE_SYSFS, b, sizeof b) == 0 && b[0] == '\0');
    {
        static const char *const cur[3] = {"-812000", "-790000", "-805000"};
        static const char *const volt[3] = {"3850000", "3862000", "3855000"};
        static const char *const lvl[3] = {"67", "67", "66"};
        for (int i = 0; i < 3; i++) {
            CHECK(power_from_sysfs(cur[i], volt[i], lvl[i], "Discharging", &s));
            power_block_add(&pb, &s);
        }
    }
    CHECK(power_block_text(&pb, POWER_SOURCE_SYSFS, b, sizeof b) == strlen(b));
    CHECK(text_is(b, "battery each second: current -812 -790 -805 mA, voltage 3850-3862 mV, "
                     "level 66 %, discharging (sysfs; the current's sign as the device gives "
                     "it)"));
    /* past 12 currents: counted; BatteryManager's */
    power_block_reset(&pb);
    for (int i = 0; i < 14; i++) {
        const int a[4] = {-100000 * (i + 1), 3900, 50, 2};
        CHECK(power_from_android(a, &s));
        power_block_add(&pb, &s);
    }
    (void)power_block_text(&pb, POWER_SOURCE_ANDROID, b, sizeof b);
    CHECK(text_is(b, "battery each second: current -100 -200 -300 -400 -500 -600 -700 -800 -900 "
                     "-1000 -1100 -1200 mA (the first 12 of 14), voltage 3900-3900 mV, level 50 "
                     "%, charging (BatteryManager; the current's sign as the device gives it)"));
    /* no current, no voltage */
    power_block_reset(&pb);
    CHECK(power_from_sysfs(NULL, NULL, "80", "Full", &s));
    power_block_add(&pb, &s);
    (void)power_block_text(&pb, POWER_SOURCE_SYSFS, b, sizeof b);
    CHECK(text_is(b, "battery each second: no current, level 80 %, full (sysfs; the current's "
                     "sign as the device gives it)"));

    (void)power_source_text(POWER_SOURCE_SYSFS, "/sys/class/power_supply/battery", 0, NULL, b,
                            sizeof b);
    CHECK(text_is(b, "battery: read once a second on the log thread from "
                     "/sys/class/power_supply/battery (current, voltage, level, status)"));
    (void)power_source_text(POWER_SOURCE_ANDROID, "", 13, NULL, b, sizeof b);
    CHECK(text_is(b, "battery: /sys/class/power_supply not readable (errno 13): read once a "
                     "second on the log thread from Android's BatteryManager"));
    (void)power_source_text(POWER_SOURCE_NONE, "", 0, "no answer from the app's Java side", b,
                            sizeof b);
    CHECK(text_is(b, "battery: not readable (no battery in /sys/class/power_supply; "
                     "BatteryManager: no answer from the app's Java side): no battery lines"));
    /* a short buffer: cut and ended */
    {
        char t[16];
        const size_t n = power_source_text(POWER_SOURCE_NONE, "", 13, NULL, t, sizeof t);
        CHECK(n == sizeof t - 1u && strlen(t) == n);
    }

    if (fails) {
        printf("power_read_test: %d failures\n", fails);
        return 1;
    }
    printf("power_read_test: ok\n");
    return 0;
}
