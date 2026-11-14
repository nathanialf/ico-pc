/* rd_health_live.c: the picture-health report in the renderer (rd_health.h
 * says what and why): the records judged, the attempt ledger's row, the
 * line once when the rule is met, and the start-up self-test's window.
 * Nothing here changes what is drawn. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attempt_ledger.h"
#include "rd_health.h"
#include "rd_internal.h"
#include "rd_perf_clock.h" /* RD_RESET_*, rd_perf_reset_counted */

static struct {
    bool begun;    /* the first record or self-test window decided on */
    bool on;       /* Vulkan: the records carry the slot clock's signs */
    bool fakeSelf; /* ICO_RD_HEALTH_FAKE=selftest (the tests) */
    RdHealth h;    /* the render thread's */
    uint64_t judged, unfinished;
    /* the start-up self-test's window */
    bool stActive;
    uint32_t stBad, stN;
} s_hl;

/* once: Vulkan or not, the ledger's row, the tests' fake */
static void begin(void)
{
    if (s_hl.begun || !g_rd.hasDevice) {
        return;
    }
    s_hl.begun = true;
    s_hl.on = rhi_backend() == RHI_BACKEND_VULKAN;
    rd_health_reset(&s_hl.h);
    ico_attempt_decide_picture_health(s_hl.on ? 1 : 0);
    const char *e = getenv("ICO_RD_HEALTH_FAKE"); /* the tests only: no setting */
    if (e != NULL && e[0] != '\0') {
        s_hl.fakeSelf = strcmp(e, "selftest") == 0;
        rd__log("picture health: ICO_RD_HEALTH_FAKE=%.16s: %s", e,
                s_hl.fakeSelf ? "the start-up self-test's pictures read as hangs (the tests)"
                              : "not selftest: ignored");
    }
}

/* the GPU attempts on for this device (the ledger's rows on), for the line */
static void attemptsOn(char *out, size_t size)
{
    static const int rows[] = {
        ICO_ATT_COMPACTION,      ICO_ATT_REC_NEED,         ICO_ATT_FLAT0,     ICO_ATT_DATE_ONE,
        ICO_ATT_EMPTY_SKIP,      ICO_ATT_VOLUME_FS,        ICO_ATT_COMP_SPEC, ICO_ATT_PASTE_SPEC,
        ICO_ATT_NO_VOLUME_BREAK, ICO_ATT_VU_COMPUTE_CLASS, ICO_ATT_TURN_FOLD, ICO_ATT_SUBPASS,
        ICO_ATT_ADRENO_A};
    size_t at = 0;
    out[0] = '\0';
    for (size_t i = 0; i < sizeof rows / sizeof rows[0] && at + 1u < size; i++) {
        if (ico_attempt_state_of(rows[i]) != ICO_ATT_ON) {
            continue;
        }
        const int w =
            snprintf(out + at, size - at, "%s%s", at ? ", " : "", ico_attempt_name(rows[i]));
        if (w < 0) {
            break;
        }
        at = at + (size_t)w < size ? at + (size_t)w : size - 1u;
    }
    if (at == 0) {
        snprintf(out, size, "none of this release's GPU attempts");
    }
}

void rd__health_note(const RdPerfRecord *r)
{
    begin();
    if (!s_hl.on || r == NULL) {
        return;
    }
    const bool judged = r->presented && r->vuDraws > 0u;
    int sign = r->gpuResetSign;
    if (s_hl.stActive) {
        /* the self-test's pictures: its own verdict, not the report's */
        sign = s_hl.fakeSelf ? RD_RESET_HANG : sign;
        s_hl.stN++;
        s_hl.stBad += rd_perf_reset_counted(sign) ? 1u : 0u;
        return;
    }
    if (!judged) {
        return;
    }
    s_hl.judged++;
    s_hl.unfinished += rd_perf_reset_counted(sign) ? 1u : 0u;
    if (sign == RD_RESET_HANG) {
        ico_attempt_fire(ICO_ATT_PICTURE_HEALTH, 1);
    }
    if (r->gpuGapBackNs != 0u && sign != RD_RESET_IDLE) {
        ico_attempt_fire2(ICO_ATT_PICTURE_HEALTH, 1);
    }
    uint32_t fails = 0, n = 0;
    if (!rd_health_pic(&s_hl.h, true, sign, &fails, &n)) {
        return;
    }
    char on[512];
    attemptsOn(on, sizeof on);
    const RhiLimits *lim = rhi_limits();
    rd__log("picture health: 3D pictures are coming back unfinished from the GPU: %u of the last "
            "%u (replay %u, frame %u: %u of its recorded stamps unwritten after its work was "
            "signalled finished%s%s%s; %llu of %llu 3D pictures in the run); the picture is "
            "probably black; on for this device: %s; robust access %s",
            fails, n, r->replay, r->frame, (unsigned)r->gpuStampsMissing,
            r->gpuFirstMissing[0] ? ", the first: " : "", r->gpuFirstMissing,
            r->gpuGapBackNs != 0u ? ", its clock restarted" : "",
            (unsigned long long)s_hl.unfinished, (unsigned long long)s_hl.judged, on,
            lim != NULL && lim->robustText != NULL ? lim->robustText : "not known");
}

void rd__health_selftest_begin(void)
{
    begin();
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
