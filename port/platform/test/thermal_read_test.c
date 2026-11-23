/*
 * port/platform/test/thermal_read_test.c
 *
 * The phone's heat and clock state (thermal_read.h) over fixture trees the
 * test writes under the build folder: the parsers (millidegrees and
 * degrees, kHz, cpu lists, garbage), the zones chosen by type (GPU, CPU,
 * SoC, battery, skin; a type of none of these, a garbage temperature, an
 * unreadable one, two hundred zones: four chosen, the scan capped), no
 * thermal folder, the CPU clusters from cpufreq's policies and from each
 * core's folder (cores with the same hardware most in a row one cluster,
 * a core without a readable clock passed), a second's reading, the once,
 * token and block lines word for word.
 * argv[1]: a folder the test may write in (the build folder).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "host_fs.h"
#include "thermal_read.h"

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

/* every folder of path made, then the file with text */
static void put(const char *root, const char *rel, const char *text)
{
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    for (char *p = path + strlen(root) + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)ico_mkdir(path);
            *p = '/';
        }
    }
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void mkdirs(const char *root, const char *rel)
{
    char path[1400];
    snprintf(path, sizeof path, "%s/%s/x", root, rel);
    for (char *p = path + strlen(root) + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)ico_mkdir(path);
            *p = '/';
        }
    }
}

int main(int argc, char **argv)
{
    char base[1000];
    snprintf(base, sizeof base, "%s/thermal_read_files", argc > 1 ? argv[1] : ".");
    (void)ico_remove_tree(base);
    (void)ico_mkdir(base);

    /* the parsers */
    double c = 0.0;
    int mhz = 0, first = -1, last = -1;
    CHECK(thermal_temp_c("45000\n", &c) && c == 45.0);
    CHECK(thermal_temp_c("45\n", &c) && c == 45.0);
    CHECK(thermal_temp_c("-5000", &c) && c == -5.0);
    CHECK(!thermal_temp_c("abc\n", &c));
    CHECK(!thermal_temp_c("45000 x\n", &c));
    CHECK(!thermal_temp_c("", &c));
    CHECK(thermal_khz_mhz("1800000\n", &mhz) && mhz == 1800);
    CHECK(thermal_khz_mhz("1799500", &mhz) && mhz == 1800);
    CHECK(!thermal_khz_mhz("-1", &mhz));
    CHECK(thermal_cpu_list("0-3\n", &first, &last) && first == 0 && last == 3);
    CHECK(thermal_cpu_list("4 5 6 7\n", &first, &last) && first == 4 && last == 7);
    CHECK(thermal_cpu_list("0-1,4", &first, &last) && first == 0 && last == 4);
    CHECK(!thermal_cpu_list("\n", &first, &last));
    CHECK(thermal_type_rank("gpu0-usr") == 0 && thermal_type_rank("CPU-1-0") == 1 &&
          thermal_type_rank("soc_max") == 2 && thermal_type_rank("battery") == 3 &&
          thermal_type_rank("skin-therm") == 4 && thermal_type_rank("xo-therm") == -1);

    /* a phone's tree: seven zones, cpufreq policies */
    char phone[1100];
    snprintf(phone, sizeof phone, "%s/phone", base);
    (void)ico_mkdir(phone);
    static const char *const kType[7] = {"battery",  "gpu0-usr",   "xo-therm", "cpu-1-0-usr",
                                         "mtktscpu", "skin-therm", "gpu1-usr"};
    static const char *const kTemp[7] = {"31000\n", "43200\n", "38000\n",  "45000\n",
                                         "46500\n", "36000\n", "garbage\n"};
    for (int i = 0; i < 7; i++) {
        char rel[96];
        snprintf(rel, sizeof rel, "sys/class/thermal/thermal_zone%d/type", i);
        put(phone, rel, kType[i]);
        snprintf(rel, sizeof rel, "sys/class/thermal/thermal_zone%d/temp", i);
        put(phone, rel, kTemp[i]);
    }
    put(phone, "sys/class/thermal/cooling_device0/type", "fan\n"); /* not a zone */
    put(phone, "sys/devices/system/cpu/cpufreq/policy0/related_cpus", "0 1 2 3 4 5\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq", "1200000\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy0/scaling_max_freq", "1800000\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy0/cpuinfo_max_freq", "1800000\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy6/related_cpus", "6-7\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy6/scaling_cur_freq", "2000000\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy6/scaling_max_freq", "1500000\n");
    put(phone, "sys/devices/system/cpu/cpufreq/policy6/cpuinfo_max_freq", "2000000\n");

    ThermalZones z;
    ThermalCpus cp;
    thermal_zones_scan(phone, &z);
    CHECK(z.err == 0 && z.seen == 7 && z.readable == 6 && z.n == 4);
    /* by type: the GPU's, then the CPU's two (by number), then the battery */
    CHECK(strcmp(z.type[0], "gpu0-usr") == 0 && strcmp(z.type[1], "cpu-1-0-usr") == 0 &&
          strcmp(z.type[2], "mtktscpu") == 0 && strcmp(z.type[3], "battery") == 0);
    thermal_cpus_scan(phone, &cp);
    CHECK(cp.err == 0 && cp.policy && cp.n == 2);
    CHECK(cp.first[0] == 0 && cp.last[0] == 5 && cp.hwMaxMhz[0] == 1800);
    CHECK(cp.first[1] == 6 && cp.last[1] == 7 && cp.hwMaxMhz[1] == 2000);

    ThermalSample s;
    thermal_sample_none(&s);
    s.status = 1;
    s.headroom = 0.67;
    CHECK(thermal_sample_read(&z, &cp, &s));
    CHECK(s.zones == 4 && s.hottest == 2 && s.hottestC == 46.5);
    CHECK(s.clusters == 2 && s.curMhz[0] == 1200 && s.maxMhz[0] == 1800 && s.curMhz[1] == 2000 &&
          s.maxMhz[1] == 1500);
    char t[1024];
    (void)thermal_token(&s, t, sizeof t);
    CHECK(text_is(t, "1/0.67/46.5/1200:1800,2000:1500"));
    CHECK(thermal_read_open_text(&z, &cp, &s, 31, "", "", t, sizeof t) == strlen(t));
    CHECK(text_is(t, "thermal: status from the system, headroom from the system (API 31); zones "
                     "read: 4 of 7 (gpu0-usr 43.2 C, cpu-1-0-usr 45.0 C, mtktscpu 46.5 C, battery "
                     "31.0 C; 6 readable, the others' types not GPU, CPU, SoC, battery or skin, "
                     "or past 4); CPU clusters read (cpufreq policies): cores 0-5 (most 1800 "
                     "MHz), cores 6-7 (most 2000 MHz)"));
    {
        ThermalSample none;
        thermal_sample_none(&none);
        (void)thermal_token(&none, t, sizeof t);
        CHECK(text_is(t, "-/-/-/-"));
    }

    /* the block over three seconds: the status and an allowed most change;
       a second not known left out */
    {
        ThermalSample b[4];
        bool known[4] = {true, true, false, true};
        b[0] = s;
        b[1] = s;
        b[1].status = 2;
        b[1].headroom = 0.71;
        b[1].hottestC = 47.0;
        b[1].curMhz[1] = 1100;
        b[1].maxMhz[1] = 1200;
        thermal_sample_none(&b[2]);
        b[3] = s;
        (void)thermal_read_block_text(b, known, 4, &cp, t, sizeof t);
        CHECK(text_is(t, "thermal in the block: status 1-2 (changed), headroom 0.67-0.71, hottest "
                         "zone 46.5-47.0 C; cores 0-5 1200-1200 MHz, allowed 1800-1800 MHz of "
                         "1800; cores 6-7 1100-2000 MHz, allowed 1200-1500 MHz of 2000; the "
                         "allowed most changed: yes"));
        bool none[1] = {false};
        CHECK(thermal_read_block_text(b, none, 1, &cp, t, sizeof t) == 0 && t[0] == '\0');
        /* steady */
        b[1] = s;
        (void)thermal_read_block_text(b, known, 2, &cp, t, sizeof t);
        CHECK(strstr(t, "status 1-1 (steady)") != NULL &&
              strstr(t, "the allowed most changed: no") != NULL);
    }

    /* a zone that became unreadable or garbage this second: not read */
    put(phone, "sys/class/thermal/thermal_zone4/temp", "x\n");
    thermal_sample_none(&s);
    CHECK(thermal_sample_read(&z, &cp, &s));
    CHECK(s.zoneC[2] < -100.0 && s.hottest == 1 && s.hottestC == 45.0);
    CHECK(s.status == THERMAL_STATUS_NONE_READ);

    /* no thermal folder, no cpu folder */
    char empty[1100];
    snprintf(empty, sizeof empty, "%s/empty", base);
    (void)ico_mkdir(empty);
    thermal_zones_scan(empty, &z);
    CHECK(z.err == ENOENT && z.n == 0 && z.seen == 0);
    thermal_cpus_scan(empty, &cp);
    CHECK(cp.n == 0 && cp.err == ENOENT);
    (void)thermal_read_open_text(&z, &cp, NULL, 0, "not on Android", "not on Android", t, sizeof t);
    CHECK(text_is(t, "thermal: status not on Android, headroom not on Android; zones not "
                     "readable (errno 2, 0 found); CPU clocks not readable (errno 2)"));

    /* two hundred zones, every one a CPU's: four chosen, the scan capped */
    char many[1100];
    snprintf(many, sizeof many, "%s/many", base);
    (void)ico_mkdir(many);
    for (int i = 0; i < 200; i++) {
        char rel[96];
        snprintf(rel, sizeof rel, "sys/class/thermal/thermal_zone%d/type", i);
        put(many, rel, "cpu\n");
        snprintf(rel, sizeof rel, "sys/class/thermal/thermal_zone%d/temp", i);
        put(many, rel, "40000\n");
    }
    thermal_zones_scan(many, &z);
    CHECK(z.seen == 200 && z.readable == THERMAL_ZONES_SCAN && z.n == THERMAL_ZONES_MAX);

    /* each core's cpufreq folder (no policies): cores 0-1 and 2-3 by their
       hardware most; core 4 without a readable clock passed; core 5 none */
    char cores[1100];
    snprintf(cores, sizeof cores, "%s/cores", base);
    (void)ico_mkdir(cores);
    for (int i = 0; i < 4; i++) {
        char rel[96];
        snprintf(rel, sizeof rel, "sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
        put(cores, rel, "900000\n");
        snprintf(rel, sizeof rel, "sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
        put(cores, rel, i < 2 ? "1800000\n" : "2000000\n");
    }
    mkdirs(cores, "sys/devices/system/cpu/cpu4");
    thermal_cpus_scan(cores, &cp);
    CHECK(!cp.policy && cp.n == 2 && cp.first[0] == 0 && cp.last[0] == 1 && cp.first[1] == 2 &&
          cp.last[1] == 3 && cp.hwMaxMhz[1] == 2000);
    thermal_zones_scan(cores, &z);
    thermal_sample_none(&s);
    CHECK(thermal_sample_read(&z, &cp, &s) && s.curMhz[0] == 900 && s.maxMhz[0] < 0);
    (void)thermal_token(&s, t, sizeof t);
    CHECK(text_is(t, "-/-/-/900:-,900:-"));

    /* an unreadable zone (not as root: root reads it anyway) */
    if (geteuid() != 0) {
        char locked[1100], p[1300];
        snprintf(locked, sizeof locked, "%s/locked", base);
        (void)ico_mkdir(locked);
        put(locked, "sys/class/thermal/thermal_zone0/type", "gpu\n");
        put(locked, "sys/class/thermal/thermal_zone0/temp", "50000\n");
        snprintf(p, sizeof p, "%s/sys/class/thermal/thermal_zone0/temp", locked);
        CHECK(chmod(p, 0) == 0);
        thermal_zones_scan(locked, &z);
        CHECK(z.n == 0 && z.seen == 1 && z.readable == 0 && z.err == EACCES);
        (void)chmod(p, 0644);
    }

    /* a short buffer: cut and ended */
    {
        char sh[16];
        thermal_sample_none(&s);
        s.status = 3;
        s.headroom = 1.25;
        const size_t n = thermal_token(&s, sh, sizeof sh);
        CHECK(n == strlen(sh) && n < sizeof sh);
    }

    if (fails) {
        printf("thermal_read_test: %d failures\n", fails);
        return 1;
    }
    printf("thermal_read_test: ok\n");
    return 0;
}
