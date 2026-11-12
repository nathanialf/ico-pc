/*
 * port/platform/capture_trigger.c
 *
 * A release candidate's frames of the opening: when to take them
 * (capture_trigger.h).
 */
#include "capture_trigger.h"

#include <stdio.h>
#include <string.h>

static void runs_end(IcoCaptureTrigger *t)
{
    t->runPlan = ICO_CAPTURE_PLAN_NONE;
    t->run = 0;
    t->darkAfter = 0;
    t->haveFrame = 0;
}

void ico_capture_trigger_init(IcoCaptureTrigger *t)
{
    memset(t, 0, sizeof(*t));
    t->runPlan = ICO_CAPTURE_PLAN_NONE;
    t->armed = -1;
}

static int spaced(const IcoCaptureTrigger *t, uint64_t nowNs)
{
    return !t->haveLast || (nowNs >= t->lastNs && nowNs - t->lastNs >= ICO_CAPTURE_SPACING_NS);
}

int ico_capture_trigger_picture(IcoCaptureTrigger *t, int inOpening, int plan, uint32_t frame,
                                uint64_t nowNs)
{
    if (!inOpening) {
        runs_end(t);
        return -1;
    }
    if (plan != ICO_CAPTURE_PLAN_DARK && plan != ICO_CAPTURE_PLAN_LAMPS &&
        plan != ICO_CAPTURE_PLAN_WHOLE) {
        if (!(t->haveFrame && frame == t->lastFrame)) {
            runs_end(t); /* a new frame without a plan */
        }
        return -1;
    }
    const uint32_t before = t->runPlan == plan ? t->run : 0u;
    const uint32_t darkBefore = t->darkAfter;

    t->runPlan = plan;
    t->run = before < UINT32_MAX ? before + 1u : before;
    t->haveFrame = 1;
    t->lastFrame = frame;
    if (plan != ICO_CAPTURE_PLAN_DARK) {
        t->darkAfter = 0;
    } else if ((t->used & (1u << ICO_CAPTURE_LAMPS)) != 0 && t->darkAfter < UINT32_MAX) {
        t->darkAfter++;
    }
    if (t->armed >= 0 || t->dumps >= ICO_CAPTURE_MAX || !spaced(t, nowNs)) {
        return -1;
    }
    int reason = -1;
    uint32_t runBefore = 0;

    if (plan == ICO_CAPTURE_PLAN_LAMPS && before >= ICO_CAPTURE_LAMPS_RUN &&
        (t->used & (1u << ICO_CAPTURE_LAMPS)) == 0) {
        reason = ICO_CAPTURE_LAMPS;
        runBefore = before;
    } else if (plan == ICO_CAPTURE_PLAN_WHOLE && before >= ICO_CAPTURE_SUN_RUN &&
               (t->used & (1u << ICO_CAPTURE_SUN)) == 0) {
        reason = ICO_CAPTURE_SUN;
        runBefore = before;
    } else if (plan == ICO_CAPTURE_PLAN_DARK && darkBefore >= ICO_CAPTURE_LATER_RUN &&
               (t->used & (1u << ICO_CAPTURE_LATER)) == 0) {
        reason = ICO_CAPTURE_LATER;
        runBefore = darkBefore;
    }
    if (reason >= 0) {
        t->armed = reason;
        t->armedRun = runBefore;
    }
    return reason;
}

int ico_capture_trigger_take(IcoCaptureTrigger *t, int inOpening, int busy, uint64_t nowNs)
{
    if (t->armed < 0) {
        return -1;
    }
    if (!inOpening) {
        t->armed = -1; /* not used: a later picture of the opening may arm it again */
        return -1;
    }
    if (busy) {
        return -1;
    }
    const int reason = t->armed;

    t->armed = -1;
    t->used |= 1u << reason;
    t->dumps++;
    t->haveLast = 1;
    t->lastNs = nowNs;
    if (reason == ICO_CAPTURE_LAMPS) {
        t->darkAfter = 0; /* LATER counts the DARK pictures after this dump */
    }
    return reason;
}

void ico_capture_trigger_done(IcoCaptureTrigger *t, uint64_t nowNs)
{
    t->haveLast = 1;
    t->lastNs = nowNs;
}

const char *ico_capture_trigger_name(int reason)
{
    static const char *const kName[ICO_CAPTURE_REASONS] = {
        [ICO_CAPTURE_LAMPS] = "the lamps picture",
        [ICO_CAPTURE_SUN] = "the sun picture",
        [ICO_CAPTURE_LATER] = "the later picture",
    };
    return reason >= 0 && reason < ICO_CAPTURE_REASONS ? kName[reason] : "";
}

const char *ico_capture_trigger_start_text(void)
{
    return "capture: on (a release candidate): frames of the opening are saved by themselves (a "
           "lamps picture, a sun picture, a later picture) into the dumps folder and sent with "
           "\"Share a bug report\"";
}

size_t ico_capture_trigger_line(char *buf, size_t size, int reason, uint32_t runBefore, int dumpNo,
                                uint32_t frame, const char *zipName)
{
    static const char *const kWhy[ICO_CAPTURE_REASONS] = {
        [ICO_CAPTURE_LAMPS] = "lamps pictures in a row",
        [ICO_CAPTURE_SUN] = "sun pictures (the glow drawn whole) in a row",
        [ICO_CAPTURE_LATER] = "dark pictures in a row after the lamps picture",
    };
    const char *why = reason >= 0 && reason < ICO_CAPTURE_REASONS ? kWhy[reason] : "pictures";
    int n;

    if (size == 0) {
        return 0;
    }
    if (zipName != NULL && zipName[0] != '\0') {
        n = snprintf(buf, size, "capture: %s (dump %d of %d this run, after %u %s): frame %u, %s",
                     ico_capture_trigger_name(reason), dumpNo, ICO_CAPTURE_MAX, (unsigned)runBefore,
                     why, (unsigned)frame, zipName);
    } else {
        n = snprintf(buf, size,
                     "capture: %s (dump %d of %d this run, after %u %s): frame %u not saved (no "
                     "zip; the lines above say why); the capture goes on",
                     ico_capture_trigger_name(reason), dumpNo, ICO_CAPTURE_MAX, (unsigned)runBefore,
                     why, (unsigned)frame);
    }
    if (n < 0) {
        buf[0] = '\0';
        return 0;
    }
    return (size_t)n < size ? (size_t)n : size - 1;
}
