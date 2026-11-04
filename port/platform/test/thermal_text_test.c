/*
 * port/platform/test/thermal_text_test.c
 *
 * The thermal and screen recording words (thermal_text.h, issue 123): the
 * status name table (0 to 6, out of range), the change and recording lines,
 * the block line with every part, without a headroom, status alone,
 * recording alone, nothing reported (the not-available words); the around
 * suffix with every part, partial, none; a short buffer cut and
 * terminated.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "thermal_text.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
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

int main(void)
{
    static const char *const names[] = {"NONE",     "LIGHT",     "MODERATE", "SEVERE",
                                        "CRITICAL", "EMERGENCY", "SHUTDOWN"};
    char b[256];
    ThermalState s = thermal_state_none();

    for (int i = 0; i < 7; i++) {
        CHECK(strcmp(thermal_status_name(i), names[i]) == 0);
    }
    CHECK(strcmp(thermal_status_name(-1), "UNKNOWN") == 0);
    CHECK(strcmp(thermal_status_name(7), "UNKNOWN") == 0);

    thermal_change_text(2, 3, 212.4, b, sizeof b);
    CHECK(text_is(b,
                  "ico_pc: thermal status changed: MODERATE (2) -> SEVERE (3) (run time 212.4 s)"));
    thermal_change_text(-1, 0, 0.05, b, sizeof b);
    CHECK(text_is(b, "ico_pc: thermal status changed: UNKNOWN (-1) -> NONE (0) (run time 0.1 s)"));
    thermal_recording_text(1, 40.1, b, sizeof b);
    CHECK(text_is(b, "ico_pc: screen recording: visible (run time 40.1 s)"));
    thermal_recording_text(0, 95.0, b, sizeof b);
    CHECK(text_is(b, "ico_pc: screen recording: not visible (run time 95.0 s)"));

    /* nothing reported */
    CHECK(!thermal_state_any(&s));
    thermal_block_text(&s, "this Android version reports none", b, sizeof b);
    CHECK(text_is(b, "thermal: not available (this Android version reports none)"));
    thermal_around_text(&s, b, sizeof b);
    CHECK(text_is(b, ""));

    /* every part */
    s.headroom = 0.72;
    s.status = 2;
    s.recording = 0;
    CHECK(thermal_state_any(&s));
    thermal_block_text(&s, "", b, sizeof b);
    CHECK(text_is(b, "thermal: headroom 0.72 (1.0 is the throttling point), status MODERATE (2); "
                     "screen recording not visible"));
    s.headroom = 0.98;
    s.status = 3;
    s.recording = 1;
    thermal_around_text(&s, b, sizeof b);
    CHECK(text_is(b, "; thermal headroom 0.98, status SEVERE; screen recording visible"));

    /* no headroom (NaN: Android before 11, or asked too often) */
    s.headroom = NAN;
    thermal_block_text(&s, "", b, sizeof b);
    CHECK(text_is(b, "thermal: status SEVERE (3); screen recording visible"));
    thermal_around_text(&s, b, sizeof b);
    CHECK(text_is(b, "; thermal status SEVERE; screen recording visible"));
    /* headroom alone */
    s = thermal_state_none();
    s.headroom = 1.25;
    thermal_block_text(&s, "", b, sizeof b);
    CHECK(text_is(b, "thermal: headroom 1.25 (1.0 is the throttling point)"));
    thermal_around_text(&s, b, sizeof b);
    CHECK(text_is(b, "; thermal headroom 1.25"));
    /* recording alone (Android 15 without the thermal API answering) */
    s = thermal_state_none();
    s.recording = 0;
    thermal_block_text(&s, "", b, sizeof b);
    CHECK(text_is(b, "thermal: screen recording not visible"));
    thermal_around_text(&s, b, sizeof b);
    CHECK(text_is(b, "; screen recording not visible"));
    /* status alone */
    s = thermal_state_none();
    s.status = 0;
    thermal_block_text(&s, "", b, sizeof b);
    CHECK(text_is(b, "thermal: status NONE (0)"));
    thermal_around_text(&s, b, sizeof b);
    CHECK(text_is(b, "; thermal status NONE"));

    /* a short buffer is cut and terminated */
    s.headroom = 0.72;
    s.status = 2;
    s.recording = 0;
    {
        char small[16];
        thermal_block_text(&s, "", small, sizeof small);
        CHECK(strlen(small) == 15 && strncmp(small, "thermal: headro", 15) == 0);
        thermal_around_text(&s, small, sizeof small);
        CHECK(strlen(small) == 15);
        CHECK(strncmp(small, "; thermal headr", 15) == 0);
    }
    printf("thermal_text_test: %s\n", failures ? "FAILED" : "ok");
    return failures ? 1 : 0;
}
