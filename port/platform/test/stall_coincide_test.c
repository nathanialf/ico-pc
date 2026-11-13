/*
 * port/platform/test/stall_coincide_test.c
 *
 * What a stall coincided with (stall_coincide.h): the block's medians (none
 * before five, the ring's last 64, a full table), the spike rule at its
 * bounds, the classes of a moment (issue 179's: memory under the floor;
 * a core wait; pressure rising; the GPU clock low; none), the tally and
 * moment texts word for word, the pressure and status parsers, the
 * driver's thread names (CPU only).
 */
#include <stdio.h>
#include <string.h>

#include "stall_coincide.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static void text_is(const char *got, const char *want, int line)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL line %d:\n got  %s\n want %s\n", line, got, want);
        failures++;
    }
}

static void moment_179(StallMoment *m)
{
    memset(m, 0, sizeof(*m));
    m->gameCore = 6;
    m->gameMhz = 909;
    m->renderCore = 7;
    m->renderMhz = 1443;
    m->haveWaits = true;
    const double w[STALL_WAIT_THREADS] = {60, 40, 5, 3, -1};
    memcpy(m->waitMs, w, sizeof(w));
    m->drivers = 1;
    snprintf(m->driverName[0], sizeof m->driverName[0], "mali-cmar-backe");
    m->driverRunMs[0] = 120;
    m->driverWaitMs[0] = 30;
    m->availMb = 501;
    m->floorMb = 641;
    m->pressure = 1;
    m->some10 = 12.3;
    m->full10 = 4.1;
    m->some10Before = 12.3;
    m->headroom = 0.80;
    m->gpuMhz = m->gpuMaxMhz = -1;
}

int main(void)
{
    StallMedians md;
    stall_medians_reset(&md);
    /* none before five of the key */
    for (int i = 0; i < 5; i++) {
        CHECK(stall_medians_add(&md, 7, 10.0 + i) < 0.0);
    }
    CHECK(stall_medians_add(&md, 7, 100.0) == 12.0); /* 10..14 */
    CHECK(stall_medians_add(&md, 7, 0.0) == 12.5);   /* 10..14, 100: even count */
    CHECK(stall_medians_add(&md, 8, 1.0) < 0.0);     /* another key */
    /* the ring keeps the last 64 */
    for (int i = 0; i < 200; i++) {
        (void)stall_medians_add(&md, 9, i < 136 ? 1000.0 : 2.0);
    }
    CHECK(stall_medians_add(&md, 9, 2.0) == 2.0);
    /* a full table drops a new key */
    for (uint32_t k = 100; md.used < STALL_KEYS; k++) {
        (void)stall_medians_add(&md, k, 1.0);
    }
    CHECK(stall_medians_add(&md, 99999, 1.0) < 0.0);

    /* the rule: over 8x and over 40 ms */
    CHECK(stall_pass_spike(283.3, 48.0) == false); /* 5.9x */
    CHECK(stall_pass_spike(400.0, 48.0) == true);
    CHECK(stall_pass_spike(40.0, 4.0) == false); /* not over 40 ms */
    CHECK(stall_pass_spike(40.1, 4.0) == true);
    CHECK(stall_pass_spike(500.0, -1.0) == false); /* no median */

    StallMoment m;
    moment_179(&m);
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_MEMORY);
    char t[1024];
    (void)stall_moment_text(&m, t, sizeof t);
    text_is(t,
            "the game thread on core 6 (909 MHz), the render thread on core 7 (1443 MHz); in the "
            "last second the core waits: game 60 ms, render 40 ms, submit 5 ms, SDL audio 3 ms; "
            "the driver's threads: mali-cmar-backe ran 120 ms, waited 30 ms; memory 501 MB "
            "available against the floor 641 MB (under it); pressure some 12.3 %, full 4.1 % "
            "(avg10; some 12.3 % a second before: not rising); thermal headroom 0.80; coincided "
            "with: memory under the floor",
            __LINE__);

    StallTally ty;
    memset(&ty, 0, sizeof ty);
    (void)stall_tally_text(&ty, t, sizeof t);
    text_is(t, "stalls by what coincided: 0 in the block", __LINE__);
    stall_tally_add(&ty, &m);
    m.waitMs[1] = 150.0;  /* the render thread waited */
    m.some10Before = 3.2; /* rising */
    CHECK(stall_classes(&m) ==
          ((1u << STALL_CLASS_WAIT) | (1u << STALL_CLASS_MEMORY) | (1u << STALL_CLASS_PRESSURE)));
    stall_tally_add(&ty, &m);
    moment_179(&m);
    m.availMb = 900; /* above the floor */
    m.some10Before = -1.0;
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_NONE);
    stall_tally_add(&ty, &m);
    (void)stall_tally_text(&ty, t, sizeof t);
    text_is(t,
            "stalls by what coincided: 3 in the block (a core wait over 100 ms 1, memory under "
            "the floor 2, memory pressure rising 1, the GPU clock low 0 (not readable), none of "
            "these 1)",
            __LINE__);
    /* the GPU clock where readable */
    m.gpuMhz = 300;
    m.gpuMaxMhz = 950;
    CHECK(stall_classes(&m) == 1u << STALL_CLASS_GPU);
    m.pressure = 0;
    m.pressureErr = 13;
    m.haveWaits = false;
    m.gameCore = -1;
    m.renderCore = -1;
    m.drivers = 0;
    m.headroom = -1.0;
    m.availMb = -1;
    (void)stall_moment_text(&m, t, sizeof t);
    text_is(t,
            "the game thread's core not known; the core waits not sampled; device memory not "
            "known; memory pressure not readable (errno 13); the GPU clock 300 MHz of 950; "
            "coincided with: the GPU clock low",
            __LINE__);
    {
        char s[16];
        const size_t n = stall_moment_text(&m, s, sizeof s);
        CHECK(n == strlen(s) && n == sizeof s - 1u);
    }

    /* the parsers */
    double some = -1, full = -1;
    const char *pr = "some avg10=12.34 avg60=1.00 avg300=0.50 total=123\n"
                     "full avg10=4.10 avg60=0.20 avg300=0.10 total=45\n";
    CHECK(stall_pressure_parse(pr, strlen(pr), &some, &full) && some == 12.34 && full == 4.10);
    CHECK(!stall_pressure_parse("some avg10=1.0\n", 15, &some, &full));
    long a = 0, f = 0, s = 0;
    const char *st = "Name:\tico-main\nVmRSS:\t  839000 kB\nRssAnon:\t  412000 kB\n"
                     "RssFile:\t  420000 kB\nRssShmem:\t    7000 kB\n";
    CHECK(stall_status_parse(st, strlen(st), &a, &f, &s) && a == 412000 && f == 420000 &&
          s == 7000);
    CHECK(!stall_status_parse("VmRSS: 1 kB\n", 12, &a, &f, &s));

    /* the driver's thread names */
    CHECK(stall_driver_name("mali-cmar-backe"));
    CHECK(stall_driver_name("MALI-utility-wo"));
    CHECK(stall_driver_name("kgsl_worker_thr"));
    CHECK(stall_driver_name("QGLThread"));
    CHECK(!stall_driver_name("ico-gpu-sysfs"));
    CHECK(!stall_driver_name("SDLAudioP15"));
    CHECK(!stall_driver_name("ico render"));

    if (failures) {
        printf("stall_coincide_test: %d failures\n", failures);
        return 1;
    }
    printf("stall_coincide_test: ok\n");
    return 0;
}
