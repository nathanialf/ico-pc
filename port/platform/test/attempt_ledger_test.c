/*
 * port/platform/test/attempt_ledger_test.c
 *
 * The attempt ledger (attempt_ledger.h): every row "not reported" at the
 * start, a state with its why, the serial moving only on a change, the
 * block line's counts taken per block (the next block starts from 0), a
 * static row, rows not reported left out of the block line, the stalls
 * clause with and without an Adreno row, out-of-range ids ignored and a
 * short buffer cut and terminated.
 */
#include <stdio.h>
#include <string.h>

#include "attempt_ledger.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int starts(const char *got, const char *want)
{
    if (strncmp(got, want, strlen(want)) != 0) {
        printf("got:  %s\nwant: %s...\n", got, want);
        return 0;
    }
    return 1;
}

static int text_is(const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("got:  %s\nwant: %s\n", got, want);
        return 0;
    }
    return 1;
}

/* the deciders as each device class's owners call them: no row but the
   Adreno attempts (their own package's) left "not reported", and each
   class's no-volume break state with its reason */
typedef struct DeviceClass {
    const char *name;
    int faster, pin;
    const char *noPin;
    int dateMode, dateSetting, box, skip;
    int nvEnv, nvClass, nvDefault;
    const char *nvGate;
    IcoAttemptState wantCores, wantNoVolume;
    const char *wantNoVolumeWhy;
    /* the VU record by compute (stores, powervr) and the turn fold
       (turned): the facts as rd_vu_record.c and rd_turn_fold.c have them */
    int vuStores, vuPowervr, turned;
    IcoAttemptState wantVu, wantTurn;
} DeviceClass;

static void test_classes(void)
{
    static const DeviceClass k[] = {
        {"Mali-G52 (2 + 6)",
         2,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_ON,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         0,
         0,
         1,
         ICO_ATT_NA,
         ICO_ATT_OFF_DEFAULT},
        {"Pixel (PowerVR D, 1 + 5 over 2)",
         6,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         1,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_OFF_DEFAULT,
         "off by default until a phone rc shows the PowerVR phone's title and pause menu clean "
         "with it",
         1,
         1,
         1,
         ICO_ATT_OFF_DEFAULT,
         ICO_ATT_OFF_DEFAULT},
        {"PowerVR B (no local read)",
         2,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         1,
         0,
         "the composites' DATE by the R8 snapshot or the shader's read",
         ICO_ATT_ON,
         ICO_ATT_NA,
         "the composites' DATE by the R8 snapshot or the shader's read",
         1,
         1,
         1,
         ICO_ATT_OFF_DEFAULT,
         ICO_ATT_OFF_DEFAULT},
        {"Adreno 740 (4 + 3 + 1)",
         4,
         1,
         NULL,
         1,
         0,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         1,
         0,
         1,
         ICO_ATT_NA,
         ICO_ATT_OFF_DEFAULT},
        {"desktop with MSAA",
         0,
         0,
         "only on Android",
         1,
         1,
         1,
         1,
         -1,
         0,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_NA,
         "not the PowerVR or MoltenVK class under Vulkan",
         1,
         0,
         0,
         ICO_ATT_NA,
         ICO_ATT_NA},
        {"a Mac (MoltenVK), the break forced",
         0,
         0,
         "only on Android",
         1,
         0,
         1,
         1,
         1,
         1,
         0,
         NULL,
         ICO_ATT_NA,
         ICO_ATT_ON,
         "ICO_RD_NO_VOLUME_BREAK=1",
         1,
         0,
         0,
         ICO_ATT_NA,
         ICO_ATT_NA},
    };
    for (size_t c = 0; c < sizeof k / sizeof k[0]; c++) {
        const DeviceClass *d = &k[c];
        ico_attempt_reset();
        ico_attempt_decide_cores(d->faster, d->pin, d->noPin);
        ico_attempt_decide_capture();
        ico_attempt_decide_flat0(1);
        ico_attempt_decide_date_one(d->dateMode, d->dateSetting);
        ico_attempt_decide_empty_skip(d->box, d->skip);
        ico_attempt_decide_no_volume_break(d->nvEnv, d->nvClass, d->nvDefault, d->nvGate);
        CHECK(ico_attempt_decide_vu_compute(0, d->vuStores, 1, d->vuPowervr, -1, 0) == 0);
        CHECK(ico_attempt_decide_turn_fold(d->turned, -1, 0, -1) == 0);
        for (int i = 0; i < ICO_ATT_COUNT; i++) {
            if (i == ICO_ATT_ADRENO_A || i == ICO_ATT_ADRENO_B) {
                continue;
            }
            if (ico_attempt_state_of(i) == ICO_ATT_NOT_SET) {
                printf("FAIL %s: %s not reported\n", d->name, ico_attempt_name(i));
                failures++;
            }
        }
        CHECK(ico_attempt_state_of(ICO_ATT_FEW_CORES) == d->wantCores);
        CHECK(ico_attempt_state_of(ICO_ATT_PIPE_SET_CORES) == d->wantCores);
        CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == d->wantNoVolume);
        if (ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) != d->wantVu ||
            ico_attempt_state_of(ICO_ATT_TURN_FOLD) != d->wantTurn) {
            printf("FAIL %s: VU compute %s, turn fold %s\n", d->name,
                   ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS)),
                   ico_attempt_state_word(ico_attempt_state_of(ICO_ATT_TURN_FOLD)));
            failures++;
        }
        char b[2048], want[256];
        (void)ico_attempt_start_text(b, sizeof b);
        snprintf(want, sizeof want, "; no-volume break %s (%s)",
                 ico_attempt_state_word(d->wantNoVolume), d->wantNoVolumeWhy);
        if (strstr(b, want) == NULL) {
            printf("FAIL %s: %s not in %s\n", d->name, want, b);
            failures++;
        }
    }
    /* the DATE row's and the empty row's other states */
    ico_attempt_decide_date_one(1, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_DATE_ONE) == ICO_ATT_NA);
    ico_attempt_decide_date_one(0, 0);
    CHECK(ico_attempt_state_of(ICO_ATT_DATE_ONE) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_empty_skip(0, 1);
    CHECK(ico_attempt_state_of(ICO_ATT_EMPTY_SKIP) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_no_volume_break(0, 1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == ICO_ATT_OFF_DEFAULT);
    ico_attempt_decide_no_volume_break(-1, 1, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_NO_VOLUME_BREAK) == ICO_ATT_ON);
    ico_attempt_decide_cores(0, 1, NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_FEW_CORES) == ICO_ATT_NA);
    /* the VU record by compute: the tests' switch, the default flipped, a
       mode asked by name, no compute stores */
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 0, 1, 0) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_ON);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 1, -1, 1) == 1);
    CHECK(strcmp(ico_attempt_why_of(ICO_ATT_VU_COMPUTE_CLASS),
                 "the PowerVR class: compute dispatches in place of the vertex stores") == 0);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 1, 1, 0, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_vu_compute(1, 1, 1, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    CHECK(ico_attempt_decide_vu_compute(0, 1, 0, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_VU_COMPUTE_CLASS) == ICO_ATT_NA);
    /* the turn fold: on only with its self-test passed */
    CHECK(ico_attempt_decide_turn_fold(1, 1, 0, 1) == 1);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_ON);
    CHECK(ico_attempt_decide_turn_fold(1, -1, 1, 1) == 1);
    CHECK(ico_attempt_decide_turn_fold(1, 1, 0, 0) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_REFUSED);
    CHECK(ico_attempt_decide_turn_fold(1, -1, 1, -1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_turn_fold(1, 0, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_OFF_DEFAULT);
    CHECK(ico_attempt_decide_turn_fold(0, 1, 1, 1) == 0);
    CHECK(ico_attempt_state_of(ICO_ATT_TURN_FOLD) == ICO_ATT_NA);
    CHECK(ico_attempt_why_of(ICO_ATT_COUNT) == NULL);
    ico_attempt_reset();
}

int main(void)
{
    test_classes();
    char b[2048];

    ico_attempt_reset();
    /* nothing reported: the start line names every row */
    (void)ico_attempt_start_text(b, sizeof(b));
    CHECK(starts(b, "attempts at the start: few-cores rule not reported; pipeline set on every "
                    "core not reported; "));
    CHECK(strstr(b, "; index compaction not in this build") != NULL);
    CHECK(ico_attempt_state_of(ICO_ATT_SUBPASS) == ICO_ATT_NOT_BUILT);
    /* and the block line is empty, with no stalls asked */
    CHECK(ico_attempt_block_text(b, sizeof(b), -1) == 0 && b[0] == '\0');
    CHECK(ico_attempt_block_text(b, sizeof(b), 0) > 0 &&
          text_is(b, "attempts: stalls 0 in the block"));

    /* a state with its why; the serial moves once */
    const uint32_t s0 = ico_attempt_serial();
    ico_attempt_state(ICO_ATT_FEW_CORES, ICO_ATT_ON, "2 cores faster than the slowest");
    CHECK(ico_attempt_serial() == s0 + 1u);
    ico_attempt_state(ICO_ATT_FEW_CORES, ICO_ATT_ON, "2 cores faster than the slowest");
    CHECK(ico_attempt_serial() == s0 + 1u); /* the same state: no change */
    ico_attempt_state(ICO_ATT_FLAT0, ICO_ATT_ON, "every device");
    ico_attempt_state(ICO_ATT_CAPTURE_TWO_STEP, ICO_ATT_ON, NULL);
    ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_OFF_DEFAULT, "a phone rc turns it on");
    CHECK(ico_attempt_serial() == s0 + 4u);
    CHECK(ico_attempt_state_of(ICO_ATT_FLAT0) == ICO_ATT_ON);
    CHECK(ico_attempt_state_of(-1) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_state_of(ICO_ATT_COUNT) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_name(ICO_ATT_COUNT) == NULL);
    CHECK(strcmp(ico_attempt_name(ICO_ATT_DATE_ONE), "one DATE set-up") == 0);
    (void)ico_attempt_start_text(b, sizeof(b));
    CHECK(starts(b, "attempts at the start: few-cores rule on (2 cores faster than the slowest); "
                    "pipeline set on every core not reported; capture in two steps on; flat "
                    "outputs at the provoking corner on (every device); one DATE set-up not "
                    "reported; empty composites skipped not reported; no-volume break off by "
                    "default (a phone rc turns it on); "));

    /* the block's counts; out-of-range ids ignored */
    ico_attempt_fire(ICO_ATT_FEW_CORES, 3);
    ico_attempt_fire(ICO_ATT_CAPTURE_TWO_STEP, 1);
    ico_attempt_fire2(ICO_ATT_CAPTURE_TWO_STEP, 0);
    ico_attempt_fire(-1, 5);
    ico_attempt_fire(ICO_ATT_COUNT, 5);
    ico_attempt_fire2(ICO_ATT_COUNT, 5);
    ico_attempt_fire(ICO_ATT_DATE_ONE, 9); /* counted, not printed: not reported */
    (void)ico_attempt_block_text(b, sizeof(b), -1);
    CHECK(text_is(b, "attempts: few-cores rule on 3 threads moved; capture in two steps on 1 "
                     "captures (0 grown here); flat outputs at the provoking corner on (static); "
                     "no-volume break off by default 0 pictures"));
    /* the next block starts from 0 */
    ico_attempt_fire(ICO_ATT_FEW_CORES, 2);
    (void)ico_attempt_block_text(b, sizeof(b), 4);
    CHECK(starts(b, "attempts: few-cores rule on 2 threads moved; capture in two steps on 0 "
                    "captures (0 grown here); "));
    CHECK(strstr(b, "; stalls 4 in the block") != NULL &&
          strstr(b, "(the Adreno attempts") == NULL);
    /* reported late: counts since the last block (9 before, 1 since) */
    ico_attempt_state(ICO_ATT_DATE_ONE, ICO_ATT_ON, "every device");
    ico_attempt_fire(ICO_ATT_DATE_ONE, 1);
    ico_attempt_fire2(ICO_ATT_DATE_ONE, 2);
    (void)ico_attempt_block_text(b, sizeof(b), -1);
    CHECK(strstr(b, "; one DATE set-up on 1 shared (2 retaken by the guard)") != NULL);

    /* the Adreno rows: the stalls clause says whether one is on */
    ico_attempt_state(ICO_ATT_ADRENO_A, ICO_ATT_NA, "not an Adreno 740");
    (void)ico_attempt_block_text(b, sizeof(b), 2);
    CHECK(strstr(b, "; stalls 2 in the block (the Adreno attempts off)") != NULL);
    ico_attempt_state(ICO_ATT_ADRENO_B, ICO_ATT_ON, "Adreno 740, Qualcomm's driver");
    (void)ico_attempt_block_text(b, sizeof(b), 1);
    CHECK(strstr(b, "; stalls 1 in the block (the Adreno attempts on)") != NULL);
    CHECK(strcmp(ico_attempt_state_word(ICO_ATT_REFUSED), "refused by its self-test") == 0);
    CHECK(strcmp(ico_attempt_state_word(ICO_ATT_NA), "not applicable here") == 0);

    /* a short buffer: cut, terminated, the length what it holds */
    {
        char t[24];
        const size_t n = ico_attempt_block_text(t, sizeof(t), 1);
        CHECK(n == strlen(t) && n == sizeof(t) - 1u);
        const size_t m = ico_attempt_start_text(t, sizeof(t));
        CHECK(m == strlen(t) && m == sizeof(t) - 1u);
        CHECK(ico_attempt_start_text(t, 0) == 0);
    }

    ico_attempt_reset();
    CHECK(ico_attempt_state_of(ICO_ATT_FEW_CORES) == ICO_ATT_NOT_SET);
    CHECK(ico_attempt_block_text(b, sizeof(b), -1) == 0);

    if (failures) {
        printf("attempt_ledger_test: %d failures\n", failures);
        return 1;
    }
    printf("attempt_ledger_test: ok\n");
    return 0;
}
