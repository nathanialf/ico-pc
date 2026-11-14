/* rd_health_live.c: the picture-health guard in the renderer (rd_health.h
 * says what and why): the run's state, the records judged, the parts
 * switched off, the lines, the ledger's row and the mark the window
 * writes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_health.h"
#include "rd_internal.h"
#include "rd_perf_clock.h" /* RD_RESET_* */
#include "rd_vu_compact.h"
#include "rd_vu_rec_early.h"
#include "rd_volume_fs.h"
#include "rd_comp_spec.h"
#include "rd_paste_spec.h"

/* the fake (ICO_RD_HEALTH_FAKE, the tests) */
enum { FAKE_NONE = 0, FAKE_SIGN, FAKE_UNTIL, FAKE_SELFTEST };

static struct {
    bool begun; /* the run's guard begun (the first renderer's start) */
    bool on;    /* Vulkan: the records carry the slot clock's signs */
    RdHealth h; /* the render thread's */
    RdHealthMark start;
    bool startSet;      /* rd_health_set_start gave a mark */
    char version[48];   /* the game's version for the mark (rd_health_set_start; "dev") */
    uint32_t offMirror; /* __atomic: h.off for any thread */
    int fake, fakeSign;
    int fakeGroup;
    /* the run's counts (judged pictures) */
    uint64_t judged, unfinished, hangs, restarts;
    /* the start-up self-test's window */
    bool stActive;
    uint32_t stBad, stN;
    /* the mark for the window: a serial, and the values under a lock */
    uint32_t seq; /* __atomic */
    uint32_t lock;
    char value[16], by[288];
    /* the ledger's whys: a ring of static buffers (a published one is never
       written again before seven newer ones) */
    char why[8][224];
    uint32_t whyN;
} s_hl;

static void lockTake(void)
{
    while (__atomic_exchange_n(&s_hl.lock, 1u, __ATOMIC_ACQUIRE) != 0u) {}
}

static void lockGive(void)
{
    __atomic_store_n(&s_hl.lock, 0u, __ATOMIC_RELEASE);
}

static const char *gameVersion(void)
{
    return s_hl.version[0] != '\0' ? s_hl.version : "dev";
}

/* the guard's own row, its why (head then text) into the ring */
static void rowState(IcoAttemptState st, const char *head, const char *text)
{
    char *w = s_hl.why[s_hl.whyN++ % 8u];
    snprintf(w, sizeof s_hl.why[0], "%s%s", head, text != NULL ? text : "");
    ico_attempt_state(ICO_ATT_PICTURE_HEALTH, st, w);
}

/* the attempt ledger's row of a part (none for robust access: the guard's
   own row says it) */
static int partRow(int p)
{
    switch (p) {
    case RD_HEALTH_P_COMPACT:
        return ICO_ATT_COMPACTION;
    case RD_HEALTH_P_REC:
        return ICO_ATT_REC_NEED;
    case RD_HEALTH_P_DATE_ONE:
        return ICO_ATT_DATE_ONE;
    case RD_HEALTH_P_FLAT0:
        return ICO_ATT_FLAT0;
    case RD_HEALTH_P_VOLUME_FS:
        return ICO_ATT_VOLUME_FS;
    case RD_HEALTH_P_COMP_SPEC:
        return ICO_ATT_COMP_SPEC;
    case RD_HEALTH_P_PASTE_SPEC:
        return ICO_ATT_PASTE_SPEC;
    case RD_HEALTH_P_EMPTY_SKIP:
        return ICO_ATT_EMPTY_SKIP;
    default:
        return -1;
    }
}

/* the parts' rows held off by the guard (static whys) */
static void rowsOff(uint32_t parts, const char *why)
{
    for (int p = 0; p < RD_HEALTH_PARTS; p++) {
        if (((parts >> p) & 1u) && partRow(p) >= 0) {
            ico_attempt_guard_off(partRow(p), why);
        }
    }
}

/* the parts on here now (their rows on; robust access off by the
   platform's default, Vulkan) */
static uint32_t applicable(void)
{
    uint32_t a = 0;
    for (int p = 0; p < RD_HEALTH_PARTS; p++) {
        const int row = partRow(p);
        if (row >= 0 && ico_attempt_state_of(row) == ICO_ATT_ON) {
            a |= 1u << p;
        }
    }
    const RhiLimits *lim = g_rd.hasDevice ? rhi_limits() : NULL;
    if (lim != NULL && rhi_backend() == RHI_BACKEND_VULKAN && lim->robustDefaultOff) {
        a |= 1u << RD_HEALTH_P_ROBUST;
    }
    return a;
}

/* the live parts' behaviour switched now (each module also asks
   rd__health_part_off where it decides: the DATE set-up's and the empty
   composites' modes, the three entries' wanted) */
static void applyLive(uint32_t live)
{
    __atomic_store_n(&s_hl.offMirror, s_hl.h.off, __ATOMIC_RELEASE);
    if (live & (1u << RD_HEALTH_P_COMPACT)) {
        rd__vu_compact_guard_off(); /* its fourth plane given up: the record made again */
    }
    if (live & (1u << RD_HEALTH_P_REC)) {
        rd__vu_rec_floor(rd__vu_rec_rows_max()); /* grown at the next pre-pass */
    }
    if (live & (1u << RD_HEALTH_P_VOLUME_FS)) {
        rd__volume_fs_guard_note();
    }
    if (live & (1u << RD_HEALTH_P_COMP_SPEC)) {
        rd__comp_spec_guard_note();
    }
    if (live & (1u << RD_HEALTH_P_PASTE_SPEC)) {
        rd__paste_spec_guard_note();
    }
}

static void fakeRead(void)
{
    const char *e = getenv("ICO_RD_HEALTH_FAKE"); /* the tests only: no setting */
    s_hl.fake = FAKE_NONE;
    if (e == NULL || e[0] == '\0') {
        return;
    }
    if (strcmp(e, "hang") == 0 || strcmp(e, "restart") == 0 || strcmp(e, "idle") == 0) {
        s_hl.fake = FAKE_SIGN;
        s_hl.fakeSign = e[0] == 'h'   ? RD_RESET_HANG
                        : e[0] == 'r' ? RD_RESET_RESTART
                                      : RD_RESET_IDLE;
    } else if (e[1] == '\0' && e[0] >= 'a' && e[0] < 'a' + RD_HEALTH_GROUPS) {
        s_hl.fake = FAKE_UNTIL;
        s_hl.fakeGroup = e[0] - 'a';
    } else if (strcmp(e, "selftest") == 0) {
        s_hl.fake = FAKE_SELFTEST;
    } else {
        rd__log("picture health: ICO_RD_HEALTH_FAKE=%.16s is none of hang, restart, idle, a-e, "
                "selftest: ignored",
                e);
        return;
    }
    rd__log("picture health: ICO_RD_HEALTH_FAKE=%s: %s (the tests)", e,
            s_hl.fake == FAKE_SIGN       ? "every judged picture read with that sign"
            : s_hl.fake == FAKE_SELFTEST ? "the start-up self-test's pictures read as hangs"
                                         : "every judged picture read as a hang until that "
                                           "group is off");
}

void rd_health_set_start(const RdHealthMark *m, const char *version)
{
    if (s_hl.begun) {
        return; /* the run's guard holds its own state (a renderer made again) */
    }
    snprintf(s_hl.version, sizeof s_hl.version, "%s", version != NULL ? version : "");
    s_hl.startSet = m != NULL;
    if (m != NULL) {
        s_hl.start = *m;
    }
    /* before the renderer's modules start: the index compaction's and the
       flat outputs' starts ask (rd_replay_vu.c rd__vu_prepass_init), which
       come before rd__health_init */
    __atomic_store_n(&s_hl.offMirror, m != NULL ? rd_health_parts_of(m->groups) : 0u,
                     __ATOMIC_RELEASE);
}

bool rd__health_part_off(int part)
{
    return part >= 0 && part < RD_HEALTH_PARTS &&
           ((__atomic_load_n(&s_hl.offMirror, __ATOMIC_ACQUIRE) >> part) & 1u) != 0u;
}

/* the mark of the state now, for the window's pump */
static void publish(const char *evidence)
{
    RdHealthMark m;
    char value[sizeof s_hl.value], by[sizeof s_hl.by];
    if (s_hl.h.groups == 0u) {
        return; /* nothing off: nothing to keep */
    }
    rd_health_mark_of(&s_hl.h, gameVersion(), evidence, &m);
    if (rd_health_mark_format(value, sizeof value, by, sizeof by, &m) != 0) {
        return;
    }
    lockTake();
    memcpy(s_hl.value, value, sizeof value);
    memcpy(s_hl.by, by, sizeof by);
    lockGive();
    __atomic_fetch_add(&s_hl.seq, 1u, __ATOMIC_ACQ_REL);
}

bool rd_health_persist_take(uint32_t *seq, char *value, size_t valueSize, char *by, size_t bySize)
{
    const uint32_t now = __atomic_load_n(&s_hl.seq, __ATOMIC_ACQUIRE);
    if (seq == NULL || now == *seq || valueSize == 0 || bySize == 0) {
        return false;
    }
    lockTake();
    snprintf(value, valueSize, "%s", s_hl.value);
    snprintf(by, bySize, "%s", s_hl.by);
    lockGive();
    *seq = now;
    return true;
}

void rd__health_init(void)
{
    if (s_hl.begun) {
        /* a renderer made again in the run (Options > Graphics > Apply):
           the parts stay off, the rows said again */
        rowsOff(s_hl.h.off, "turned off earlier in this run");
        return;
    }
    s_hl.begun = true;
    s_hl.on = g_rd.hasDevice && rhi_backend() == RHI_BACKEND_VULKAN;
    fakeRead();
    rd_health_begin(&s_hl.h, s_hl.startSet ? &s_hl.start : NULL);
    __atomic_store_n(&s_hl.offMirror, s_hl.h.off, __ATOMIC_RELEASE);
    char offText[512] = "none";
    if (s_hl.h.off != 0u) {
        rd_health_parts_text(offText, sizeof offText, s_hl.h.off);
        rowsOff(s_hl.h.off, "off from this start: an earlier start's picture-health guard "
                            "([dev] picture_health)");
    }
    ico_attempt_decide_picture_health(s_hl.on ? 1 : 0);
    if (!s_hl.on) {
        rd__log("picture health: not applicable here (only Vulkan's GPU stamps say a picture "
                "came back unfinished)");
        return;
    }
    char letters[8];
    rd_health_letters(letters, sizeof letters, s_hl.start.named);
    rd__log("picture health: on: a presented 3D picture is unfinished when the GPU's own sign of "
            "a reset comes with it (recorded stamps unwritten after its work was signalled "
            "finished, its first list run twice, or its clock restarted with work queued; an "
            "idle GPU's restart is not one); %u of the last %u unfinished turns this release's "
            "newer GPU work off, one group at a time: (a) the index compaction, (b) the VU "
            "record sized from need, (c) one DATE set-up and flat outputs at the provoking "
            "corner, (d) the shadow volumes', composites' and glow paste's own entries, (e) "
            "empty composites skipped and robust access off; %u in a row with at most %u of %u "
            "unfinished confirm a step; off from this start: %s%s%s%s",
            RD_HEALTH_TRIGGER, RD_HEALTH_WINDOW, RD_HEALTH_CLEAN, RD_HEALTH_CLEAN_FAILS,
            RD_HEALTH_WINDOW, offText,
            s_hl.h.phase == RD_HEALTH_AFTER ? "; this start's pictures confirm (" : "",
            s_hl.h.phase == RD_HEALTH_AFTER ? letters[0] ? letters : "the last group" : "",
            s_hl.h.phase == RD_HEALTH_AFTER  ? ")"
            : s_hl.h.phase == RD_HEALTH_DONE ? "; every group was off at the earlier start and "
                                               "pictures still came back unfinished: no steps"
                                             : "");
    if (s_hl.h.off != 0u) {
        rowState(ICO_ATT_ON, "watching: off from this start: ", offText);
    }
}

/* the step's line and its parts switched */
static void doStep(const struct RdPerfRecord *r, const RdHealthAct *a)
{
    char live[512] = "", next[512] = "", ev[160];
    rd_health_parts_text(live, sizeof live, a->live);
    rd_health_parts_text(next, sizeof next, a->next);
    snprintf(ev, sizeof ev, "%u of %u 3D pictures unfinished at replay %u, frame %u", a->fails,
             a->judged, r->replay, r->frame);
    if (a->kind == RD_HEALTH_STEP) {
        applyLive(a->live);
        rowsOff(a->live, "3D pictures kept coming back unfinished with it on: off for the rest "
                         "of the run and from the next start");
        rowsOff(a->next, "3D pictures kept coming back unfinished: off from the next start "
                         "(made at the start)");
        rd__log("picture health: %u of the last %u 3D pictures came back unfinished (replay %u, "
                "frame %u: %u of its recorded stamps unwritten%s%s%s; in the run %llu unfinished "
                "of %llu judged, %llu with stamps unwritten, %llu clock restarts): turning off "
                "%s now%s%s%s; then %u 3D pictures in a row decide (step %u of the run)",
                a->fails, a->judged, r->replay, r->frame, (unsigned)r->gpuStampsMissing,
                r->gpuFirstMissing[0] ? ", the first: " : "", r->gpuFirstMissing,
                r->gpuGapBackNs != 0u ? ", its clock restarted" : "",
                (unsigned long long)s_hl.unfinished, (unsigned long long)s_hl.judged,
                (unsigned long long)s_hl.hangs, (unsigned long long)s_hl.restarts, live,
                (a->live & (1u << RD_HEALTH_P_REC)) ? " (the record grown to its largest at the "
                                                      "next picture, as v0.11.5 made it)"
                                                    : "",
                next[0] ? "; from the next start (made at the start): " : "", next, RD_HEALTH_CLEAN,
                s_hl.h.steps);
        char letters[8];
        rd_health_letters(letters, sizeof letters, a->groups);
        char head[32];
        snprintf(head, sizeof head, "stepped to (%s): ", letters);
        rowState(ICO_ATT_ON, head, live);
        publish(ev);
        return;
    }
    /* exhausted */
    __atomic_store_n(&s_hl.offMirror, s_hl.h.off, __ATOMIC_RELEASE);
    rowsOff(a->next, "3D pictures kept coming back unfinished: off from the next start (made "
                     "at the start)");
    if (s_hl.h.groups == 0u) {
        rd__log("picture health: %u of the last %u 3D pictures came back unfinished (replay %u, "
                "frame %u), and nothing this release added to the 3D path is on here to turn "
                "off: no steps",
                a->fails, a->judged, r->replay, r->frame);
        rowState(ICO_ATT_ON, "pictures unfinished, nothing of this release's on here", NULL);
        return;
    }
    char pend[512] = "";
    rd_health_parts_text(pend, sizeof pend,
                         rd_health_parts_of(a->pendingGroups) & s_hl.h.off &
                             ~rd_health_live_parts());
    if (a->pendingGroups != 0u) {
        rd__log("picture health: stepping everything off in this run did not recover: %u of the "
                "last %u 3D pictures still unfinished (replay %u, frame %u) with every group the "
                "guard switches in the run off; from the next start (made at the start): %s, "
                "whose pictures decide; no more steps in this run",
                a->fails, a->judged, r->replay, r->frame, pend);
        rowState(ICO_ATT_ON,
                 "everything off in the run, not recovered; from the next start: ", pend);
    } else {
        rd__log("picture health: stepping everything off did not recover: %u of the last %u 3D "
                "pictures still unfinished (replay %u, frame %u) with every group off, the "
                "parts that need a restart too: v0.11.5's GPU work does not complete them "
                "either; no more steps",
                a->fails, a->judged, r->replay, r->frame);
        rowState(ICO_ATT_ON, "everything off, not recovered: no more steps", NULL);
    }
    publish(ev);
}

static void doRecovered(const struct RdPerfRecord *r, const RdHealthAct *a)
{
    char parts[512] = "", ev[160];
    /* the confirmed groups' parts that are off: in the run the live ones
       (the next-start parts taken with them are not in force yet), from
       the start all of them */
    rd_health_parts_text(parts, sizeof parts,
                         rd_health_parts_of(a->culprit) & s_hl.h.off &
                             (a->fromStart ? ~0u : rd_health_live_parts()));
    snprintf(ev, sizeof ev, "complete again from replay %u, frame %u", r->replay, r->frame);
    if (a->fromStart) {
        rd__log(
            "picture health: pictures complete from the start with these turned off: %s "
            "(an earlier start turned them off for this one): %u 3D pictures in a row with at most %u of "
            "%u unfinished (replay %u); the culprit is among them; they stay off for every "
            "start of the same version ([dev] picture_health)",
            parts, RD_HEALTH_CLEAN, RD_HEALTH_CLEAN_FAILS, RD_HEALTH_WINDOW, r->replay);
    } else {
        rd__log("picture health: pictures complete again after turning off %s: %u 3D pictures in "
                "a row with at most %u of %u unfinished (replay %u); it stays off, and every start "
                "of the same version begins without it ([dev] picture_health)",
                parts, RD_HEALTH_CLEAN, RD_HEALTH_CLEAN_FAILS, RD_HEALTH_WINDOW, r->replay);
    }
    rowState(ICO_ATT_ON, "culprit: ", parts);
    publish(ev);
}

/* the sign the rule reads: the record's, or the tests' fake */
static int signOf(const struct RdPerfRecord *r, bool judged)
{
    if (s_hl.fake == FAKE_SIGN && judged) {
        return s_hl.fakeSign;
    }
    if (s_hl.fake == FAKE_UNTIL && judged &&
        !((rd_health_parts_of(1u << s_hl.fakeGroup) & s_hl.h.off) != 0u)) {
        return RD_RESET_HANG;
    }
    if (s_hl.fake == FAKE_SELFTEST && s_hl.stActive) {
        return RD_RESET_HANG;
    }
    return r->gpuResetSign;
}

void rd__health_note(const RdPerfRecord *r)
{
    if (!s_hl.on || r == NULL) {
        return;
    }
    const bool judged = r->presented && r->vuDraws > 0u;
    const int sign = signOf(r, judged);
    if (s_hl.stActive) {
        /* the self-test's pictures: its own verdict, not the guard's */
        s_hl.stN++;
        s_hl.stBad += rd_perf_reset_counted(sign) ? 1u : 0u;
        return;
    }
    if (!judged) {
        return;
    }
    s_hl.judged++;
    if (rd_perf_reset_counted(sign)) {
        s_hl.unfinished++;
    }
    if (sign == RD_RESET_HANG) {
        s_hl.hangs++;
        ico_attempt_fire(ICO_ATT_PICTURE_HEALTH, 1);
    }
    if (r->gpuGapBackNs != 0u && sign != RD_RESET_IDLE) {
        s_hl.restarts++;
        ico_attempt_fire2(ICO_ATT_PICTURE_HEALTH, 1);
    }
    const RdHealthPic p = {r->replay, true, sign};
    RdHealthAct a;
    const int k = rd_health_pic(&s_hl.h, &p, &a);
    if (k == RD_HEALTH_TRIGGER_HIT) {
        rd_health_take(&s_hl.h, applicable(), r->replay + RD_HEALTH_SETTLE, &a);
        doStep(r, &a);
    } else if (k == RD_HEALTH_RECOVERED) {
        doRecovered(r, &a);
    }
}

void rd__health_selftest_begin(void)
{
    s_hl.stActive = true;
    s_hl.stBad = 0;
    s_hl.stN = 0;
}

uint32_t rd__health_selftest_end(uint32_t *n)
{
    s_hl.stActive = false;
    if (n != NULL) {
        *n = s_hl.stN;
    }
    return s_hl.stBad;
}
