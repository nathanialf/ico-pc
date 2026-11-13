/*
 * port/platform/attempt_ledger.c
 *
 * The attempt ledger (attempt_ledger.h): the rows' table and their texts.
 */
#include "attempt_ledger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* A row's words: its name, its first count's words ("threads moved"; NULL:
   a static row, no count printed) and its second's (NULL none), each
   printed after the number */
typedef struct AttRow {
    const char *name, *unit, *unit2;
} AttRow;

/* one row an attempt: add one here and one enum value in the header */
static const AttRow k_rows[ICO_ATT_COUNT] = {
    [ICO_ATT_FEW_CORES] = {"few-cores rule", "threads moved", NULL},
    [ICO_ATT_PIPE_SET_CORES] = {"pipeline set on every core", "workers", "sets"},
    [ICO_ATT_CAPTURE_TWO_STEP] = {"capture in two steps", "captures", "grown here"},
    [ICO_ATT_FLAT0] = {"flat outputs at the provoking corner", NULL, NULL},
    [ICO_ATT_DATE_ONE] = {"one DATE set-up", "shared", "retaken by the guard"},
    [ICO_ATT_EMPTY_SKIP] = {"empty composites skipped", "skipped", NULL},
    [ICO_ATT_NO_VOLUME_BREAK] = {"no-volume break", "pictures", NULL},
    [ICO_ATT_ADRENO_A] = {"Qualcomm passes as VkRenderPass objects", "passes", NULL},
    [ICO_ATT_ADRENO_B] = {"Adreno attempt B", "fired", NULL},
    [ICO_ATT_SUBPASS] = {"subpass read", "DATE set-ups in the pass", NULL},
    [ICO_ATT_BLEND_CONST] = {"blend constant", "draws", NULL},
    [ICO_ATT_COMPACTION] = {"index compaction", "triangles left out", NULL},
    [ICO_ATT_VU_COMPUTE_CLASS] = {"VU record by compute on the PowerVR", "dispatches", NULL},
    [ICO_ATT_TURN_FOLD] = {"turn in the present's blit", "presents", "kept the turn pass"},
    [ICO_ATT_REC_NEED] = {"VU record sized from need", "growths in play", NULL},
};

/* the rows' live values (__atomic), and the block text's last counts (the
   window thread's alone) */
static struct {
    int state;
    const char *why;
    uint64_t n, n2;
    uint64_t lastN, lastN2;
} s_att[ICO_ATT_COUNT] = {
    [ICO_ATT_ADRENO_B] = {ICO_ATT_NOT_BUILT, NULL, 0, 0, 0, 0},
    [ICO_ATT_BLEND_CONST] = {ICO_ATT_NOT_BUILT, NULL, 0, 0, 0, 0},
};

/* a row's state at the start */
static int startState(int i)
{
    return i == ICO_ATT_ADRENO_B || i == ICO_ATT_BLEND_CONST ? ICO_ATT_NOT_BUILT : ICO_ATT_NOT_SET;
}

static uint32_t s_serial; /* __atomic */

static int idOk(int id)
{
    return id >= 0 && id < ICO_ATT_COUNT;
}

void ico_attempt_state(int id, IcoAttemptState state, const char *why)
{
    if (!idOk(id)) {
        return;
    }
    __atomic_store_n(&s_att[id].why, why, __ATOMIC_RELEASE);
    if (__atomic_exchange_n(&s_att[id].state, (int)state, __ATOMIC_ACQ_REL) != (int)state) {
        __atomic_fetch_add(&s_serial, 1u, __ATOMIC_RELAXED);
    }
}

void ico_attempt_fire(int id, uint64_t n)
{
    if (idOk(id)) {
        __atomic_fetch_add(&s_att[id].n, n, __ATOMIC_RELAXED);
    }
}

void ico_attempt_fire2(int id, uint64_t n)
{
    if (idOk(id)) {
        __atomic_fetch_add(&s_att[id].n2, n, __ATOMIC_RELAXED);
    }
}

IcoAttemptState ico_attempt_state_of(int id)
{
    return idOk(id) ? (IcoAttemptState)__atomic_load_n(&s_att[id].state, __ATOMIC_ACQUIRE)
                    : ICO_ATT_NOT_SET;
}

const char *ico_attempt_why_of(int id)
{
    return idOk(id) ? __atomic_load_n(&s_att[id].why, __ATOMIC_ACQUIRE) : NULL;
}

const char *ico_attempt_name(int id)
{
    return idOk(id) ? k_rows[id].name : NULL;
}

const char *ico_attempt_state_word(IcoAttemptState s)
{
    switch (s) {
    case ICO_ATT_ON:
        return "on";
    case ICO_ATT_OFF_DEFAULT:
        return "off by default";
    case ICO_ATT_REFUSED:
        return "refused by its self-test";
    case ICO_ATT_NA:
        return "not applicable here";
    case ICO_ATT_NOT_BUILT:
        return "not in this build";
    default:
        return "not reported";
    }
}

uint32_t ico_attempt_serial(void)
{
    return __atomic_load_n(&s_serial, __ATOMIC_ACQUIRE);
}

/* snprintf into out at *at, *at kept at most cap - 1 (the total as
   snprintf's in *total) */
typedef struct Put {
    char *out;
    size_t cap, at, total;
} Put;

static void put(Put *p, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(p->out + p->at, p->cap - p->at, fmt, ap);
    va_end(ap);
    if (w < 0) {
        return;
    }
    p->total += (size_t)w;
    p->at = p->at + (size_t)w < p->cap ? p->at + (size_t)w : p->cap - 1u;
}

size_t ico_attempt_start_text(char *out, size_t cap)
{
    if (cap == 0) {
        return 0;
    }
    Put p = {out, cap, 0, 0};
    out[0] = '\0';
    put(&p, "attempts at the start:");
    for (int i = 0; i < ICO_ATT_COUNT; i++) {
        const IcoAttemptState st = ico_attempt_state_of(i);
        const char *why = __atomic_load_n(&s_att[i].why, __ATOMIC_ACQUIRE);
        put(&p, "%s %s %s", i ? ";" : "", k_rows[i].name, ico_attempt_state_word(st));
        if (st != ICO_ATT_NOT_SET && why != NULL && why[0] != '\0') {
            put(&p, " (%s)", why);
        }
    }
    return p.at;
}

size_t ico_attempt_block_text(char *out, size_t cap, int stalls)
{
    if (cap == 0) {
        return 0;
    }
    Put p = {out, cap, 0, 0};
    out[0] = '\0';
    int shown = 0;
    int adreno = -1; /* -1 the Adreno row not reported, 0 reported and not on, 1 on */
    for (int i = 0; i < ICO_ATT_COUNT; i++) {
        const IcoAttemptState st = ico_attempt_state_of(i);
        const uint64_t n = __atomic_load_n(&s_att[i].n, __ATOMIC_RELAXED);
        const uint64_t n2 = __atomic_load_n(&s_att[i].n2, __ATOMIC_RELAXED);
        const uint64_t d = n - s_att[i].lastN, d2 = n2 - s_att[i].lastN2;
        s_att[i].lastN = n;
        s_att[i].lastN2 = n2;
        if (st == ICO_ATT_NOT_SET || st == ICO_ATT_NOT_BUILT) {
            continue;
        }
        if (i == ICO_ATT_ADRENO_A) {
            adreno = st == ICO_ATT_ON ? 1 : 0;
        }
        put(&p, "%s %s %s", shown ? ";" : "attempts:", k_rows[i].name, ico_attempt_state_word(st));
        if (k_rows[i].unit == NULL) {
            put(&p, " (static)");
        } else {
            put(&p, " %llu %s", (unsigned long long)d, k_rows[i].unit);
            if (k_rows[i].unit2 != NULL) {
                put(&p, " (%llu %s)", (unsigned long long)d2, k_rows[i].unit2);
            }
        }
        shown++;
    }
    if (stalls >= 0) {
        put(&p, "%sstalls %d in the block", shown ? "; " : "attempts: ", stalls);
        if (adreno >= 0) {
            put(&p, " (the Adreno attempt %s)", adreno ? "on" : "off");
        }
        shown++;
    }
    return shown ? p.at : 0u;
}

void ico_attempt_reset(void)
{
    for (int i = 0; i < ICO_ATT_COUNT; i++) {
        __atomic_store_n(&s_att[i].state, startState(i), __ATOMIC_RELEASE);
        __atomic_store_n(&s_att[i].why, (const char *)NULL, __ATOMIC_RELEASE);
        __atomic_store_n(&s_att[i].n, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&s_att[i].n2, 0u, __ATOMIC_RELEASE);
        s_att[i].lastN = s_att[i].lastN2 = 0;
    }
}

void ico_attempt_decide_cores(int faster, int pin, const char *noPin)
{
    const char *why;
    IcoAttemptState st = ICO_ATT_NA;
    if (!pin) {
        why = noPin != NULL ? noPin : "no core pinned on this device";
    } else if (faster == 1 || faster == 2) {
        st = ICO_ATT_ON;
        why = faster == 2 ? "2 cores faster than the slowest" : "1 core faster than the slowest";
    } else if (faster >= 3) {
        why = "3 or more cores faster than the slowest: the keeper's rule";
    } else {
        why = "the cores are of one speed";
    }
    ico_attempt_state(ICO_ATT_FEW_CORES, st, why);
    ico_attempt_state(ICO_ATT_PIPE_SET_CORES, st, why);
}

void ico_attempt_decide_capture(void)
{
    ico_attempt_state(ICO_ATT_CAPTURE_TWO_STEP, ICO_ATT_ON,
                      "the copy into a block the dump worker made");
}

void ico_attempt_decide_flat0(int on)
{
    ico_attempt_state(ICO_ATT_FLAT0, on ? ICO_ATT_ON : ICO_ATT_OFF_DEFAULT,
                      on ? "every device and backend" : "ICO_RD_VU_FLAT0=0: the tests' twins");
}

void ico_attempt_decide_date_one(int mode, int setting)
{
    if (mode == 0) {
        ico_attempt_state(ICO_ATT_DATE_ONE, ICO_ATT_OFF_DEFAULT,
                          "ICO_RD_DATE_ONE=0: the tests' switch");
    } else if (setting) {
        ico_attempt_state(ICO_ATT_DATE_ONE, ICO_ATT_NA,
                          "MSAA or the exact blend in force: nothing shared");
    } else {
        ico_attempt_state(ICO_ATT_DATE_ONE, ICO_ATT_ON,
                          mode == 2 ? "ICO_RD_DATE_ONE=force: the tests' sabotage"
                                    : "every device and backend, where no composite can set the "
                                      "alpha's top bit");
    }
}

void ico_attempt_decide_empty_skip(int box, int mode)
{
    if (!box) {
        ico_attempt_state(ICO_ATT_EMPTY_SKIP, ICO_ATT_OFF_DEFAULT, "ICO_RD_SHADOW_BOX=0: no box");
    } else if (!mode) {
        ico_attempt_state(ICO_ATT_EMPTY_SKIP, ICO_ATT_OFF_DEFAULT,
                          "ICO_RD_EMPTY_SKIP=0: the tests' switch");
    } else {
        ico_attempt_state(ICO_ATT_EMPTY_SKIP, ICO_ATT_ON, "every device and backend");
    }
}

void ico_attempt_decide_no_volume_break(int env, int classOk, int defaultOn, const char *gateWhy)
{
    if (env == 0) {
        ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_OFF_DEFAULT,
                          "ICO_RD_NO_VOLUME_BREAK=0: the tests' switch");
    } else if (env != 1 && !classOk) {
        ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_NA,
                          "not the PowerVR or MoltenVK class under Vulkan");
    } else if (gateWhy != NULL) {
        ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_NA, gateWhy);
    } else if (env == 1) {
        ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_ON, "ICO_RD_NO_VOLUME_BREAK=1");
    } else if (!defaultOn) {
        ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_OFF_DEFAULT,
                          "off by default until a phone rc shows the PowerVR phone's title and "
                          "pause menu clean with it");
    } else {
        ico_attempt_state(ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_ON, "the PowerVR or MoltenVK class");
    }
}

void ico_attempt_decide_rec_need(int wanted, uint32_t made, uint32_t bound, uint32_t need)
{
    if (!wanted) {
        ico_attempt_state(ICO_ATT_REC_NEED, ICO_ATT_NA, "the VU pre-pass is off");
    } else if (made == 0u) {
        ico_attempt_state(ICO_ATT_REC_NEED, ICO_ATT_NA,
                          "nothing made at start-up: under the free-memory floor, after the "
                          "system's low-memory signal or no row within the bound");
    } else if (need != 0u && made != 0u && made < bound) {
        ico_attempt_state(ICO_ATT_REC_NEED, ICO_ATT_ON,
                          "made at the run's need, less than the memory bound allowed");
    } else {
        ico_attempt_state(ICO_ATT_REC_NEED, ICO_ATT_NA,
                          "the memory bound allowed no more than the run's need");
    }
}

void ico_attempt_decide_pass_objects(const char *row)
{
    ico_attempt_state(ICO_ATT_ADRENO_A, row != NULL ? ICO_ATT_ON : ICO_ATT_NA,
                      row != NULL ? row : "not Qualcomm's own driver");
}

int ico_attempt_decide_vu_compute(int byName, int stores, int compute, int powervr, int env,
                                  int defaultOn)
{
    IcoAttemptState st = ICO_ATT_NA;
    const char *why;
    int on = 0;
    if (byName) {
        why = "[dev] vu_record asked for a write by name";
    } else if (!stores) {
        why = "no vertex stores here: auto's own order";
    } else if (!compute) {
        why = "no compute stores here";
    } else if (env == 0) {
        st = ICO_ATT_OFF_DEFAULT;
        why = "ICO_RD_VU_COMPUTE_CLASS=0: the tests' switch";
    } else if (env == 1) {
        st = ICO_ATT_ON;
        on = 1;
        why = powervr ? "ICO_RD_VU_COMPUTE_CLASS=1 on the PowerVR class"
                      : "ICO_RD_VU_COMPUTE_CLASS=1: the tests' switch on another class";
    } else if (!powervr) {
        why = "not the PowerVR class under Vulkan";
    } else if (!defaultOn) {
        st = ICO_ATT_OFF_DEFAULT;
        why = "off by default until a phone rc shows the PowerVR phone's opening with it";
    } else {
        st = ICO_ATT_ON;
        on = 1;
        why = "the PowerVR class: compute dispatches in place of the vertex stores";
    }
    ico_attempt_state(ICO_ATT_VU_COMPUTE_CLASS, st, why);
    return on;
}

int ico_attempt_decide_turn_fold(int turned, int env, int defaultOn, int selftest)
{
    IcoAttemptState st = ICO_ATT_NA;
    const char *why;
    int on = 0;
    if (!turned) {
        why = "the swapchain is not pre-rotated here";
    } else if (env == 0) {
        st = ICO_ATT_OFF_DEFAULT;
        why = "ICO_RD_TURN_FOLD=0: the tests' switch";
    } else if (env != 1 && !defaultOn) {
        st = ICO_ATT_OFF_DEFAULT;
        why = "off by default until a phone rc shows the turned phone's pictures with it";
    } else if (selftest == 0) {
        st = ICO_ATT_REFUSED;
        why = "the box blit drawn turned differs from the turn pass's bytes";
    } else if (selftest < 0) {
        st = ICO_ATT_OFF_DEFAULT;
        why = "its self-test not run yet";
    } else {
        st = ICO_ATT_ON;
        on = 1;
        why = env == 1 ? "ICO_RD_TURN_FOLD=1, self-test passed"
                       : "self-test passed: presents of the box blit alone";
    }
    ico_attempt_state(ICO_ATT_TURN_FOLD, st, why);
    return on;
}

int ico_attempt_decide_compaction(int compute, const char *notWhy, int arm, int env, int defaultOn,
                                  int armDefaultOn, int selftest)
{
    IcoAttemptState st = ICO_ATT_NA;
    const char *why;
    int on = 0;
    if (!compute) {
        why = notWhy != NULL ? notWhy : "not the compute record";
    } else if (env == 0) {
        st = ICO_ATT_OFF_DEFAULT;
        why = "ICO_RD_VU_COMPACT=0: the tests' switch";
    } else if (selftest == 0) {
        st = ICO_ATT_REFUSED;
        why = "the record's self-test failed with it and passed without it: every draw reads its "
              "own index list";
    } else if (env != 1 && !(arm ? armDefaultOn : defaultOn)) {
        st = ICO_ATT_OFF_DEFAULT;
        why = arm ? "off by default on the Arm class (RD_VU_COMPACT_ARM_DEFAULT 0) until a "
                    "Mali-G52 log shows it"
                  : "off by default (RD_VU_COMPACT_DEFAULT 0)";
    } else {
        st = ICO_ATT_ON;
        on = 1;
        why = selftest == 1 ? (arm ? "the Arm class, its start-up self-test passed"
                                   : "its start-up self-test passed")
              : env == 1    ? "ICO_RD_VU_COMPACT=1: the tests' switch"
                            : "the compute record; the start-up self-test decides where it runs";
    }
    ico_attempt_state(ICO_ATT_COMPACTION, st, why);
    return on;
}

void ico_attempt_decide_subpass(int route, int on, int refused, int offDefault, const char *why)
{
    IcoAttemptState st = ICO_ATT_NA;
    if (!route) {
        why = "not a VkRenderPass path without local read (dynamic rendering's local read, or "
              "another backend)";
    } else if (on) {
        st = ICO_ATT_ON;
    } else if (refused) {
        st = ICO_ATT_REFUSED;
    } else if (offDefault) {
        st = ICO_ATT_OFF_DEFAULT;
    }
    ico_attempt_state(ICO_ATT_SUBPASS, st, why);
}
